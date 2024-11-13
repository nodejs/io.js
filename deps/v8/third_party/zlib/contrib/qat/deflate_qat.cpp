/*
 * Copyright (C) 2024 Intel Corporation. All rights reserved.
 * Authors:
 *  Gustavo A Espinoza   <gustavo.adolfo.espinoza.quintero@intel.com>
 *                       <gustavoaespinozaq@hotmail.com>
 *
 * For conditions of distribution and use, see copyright notice in zlib.h
 */
#include "deflate_qat.h"
#include "deflate.h"

#include "session.hpp"
#include "qat_instance.hpp"
#include "qat_buffer_list.hpp"
#include "qat.hpp"

#include <memory>

/*
*   TODO(gustavoa): Make the input size adjustable from the memlevel
*   attribute on deflateInit.
*/
static constexpr size_t kInputSize = 1024 * 1024;

/* QAT Instances obtained available from the library. */
static std::vector<std::shared_ptr<qat::Instance>> qat_instances;

/*
*   TODO(gustavoa): Verify if the ordering of the struct fields won't create
*   unnecessary holes in the structure that requires extraneous padding.
*/
struct qat_deflate {
    std::unique_ptr<qat::DeflateSession> qat_session;

    /*  QAT requires contiguous physical pages. Cannot be allocated using
    *   malloc/new.
    */
    uint8_t *input_buffer;
    uint8_t *output_buffer;

    /* Pointer to the next byte in the output buffer. */
    uint8_t *pending_out;

    unsigned input_buffer_size;
    unsigned output_buffer_size;

    unsigned pending_in_count;
    unsigned pending_out_count;
};

static std::unique_ptr<qat::DeflateSession> qat_create_session(int level, int wrap)
{
    CpaDcChecksum checksum = CPA_DC_NONE;

    switch(wrap) {
    case 1:
        checksum = CPA_DC_ADLER32;
        break;
    case 2:
        checksum = CPA_DC_CRC32;
        break;
    }

    return std::make_unique<qat::DeflateSession>(
        qat_instances[0],
        (CpaDcCompLvl)level,
        checksum,
        0
    );
}


int qat_deflate_init()
{
    return (qat::Initialize()) ? Z_ERRNO : Z_OK;
}

struct qat_deflate* qat_deflate_state_init(int level, int wrap)
{
    if (qat_instances.empty()) {
        qat_instances = qat::Instance::Create();
    }
    if (qat_instances.empty()) {
        return nullptr;
    }

    struct qat_deflate *qat_deflate = new struct qat_deflate;
    if (!qat_deflate) {
        return nullptr;
    }

    /* TODO(gustavoa): Find a way to utilize all the available instances for the same
     * process.
     */
    qat_instances[0]->Start();

    qat_deflate->qat_session = qat_create_session(level, wrap);

    qat_deflate->input_buffer_size = kInputSize;
    qat_deflate->input_buffer = qat::AllocBlockArray<uint8_t>(kInputSize, 0);
    qat_deflate->output_buffer_size =
        qat_deflate->qat_session->GetDeflateBound(qat_deflate->input_buffer_size);
    qat_deflate->pending_out = qat_deflate->output_buffer =
        qat::AllocBlockArray<uint8_t>(qat_deflate->output_buffer_size, 0);

    qat_deflate->pending_in_count = qat_deflate->pending_out_count = 0;

    if (!qat_deflate->input_buffer || !qat_deflate->output_buffer) {
        return nullptr;
    }

    return qat_deflate;
}

static unsigned qat_read_buf(z_streamp strm, struct qat_deflate* qat, unsigned size)
{
    unsigned len = strm->avail_in;

    if (len > size) {
        len = size;
    }
    if (len == 0) return 0;

    strm->avail_in -= len;
    strm->total_in += len;

    zmemcpy(
        qat->input_buffer + qat->pending_in_count,
        strm->next_in,
        len
    );

    strm->next_in += len;
    qat->pending_in_count += len;

    return len;
}

void qat_flush_pending(deflate_state* s)
{
    unsigned len;
    z_streamp strm = s->strm;
    struct qat_deflate* qat = s->qat_s;

    len = qat->pending_out_count;
    if (len > strm->avail_out) len = strm->avail_out;
    if (len == 0) return;

    zmemcpy(strm->next_out, qat->pending_out, len);

    qat->pending_out        += len;
    qat->pending_out_count -= len;
    strm->next_out          += len;
    strm->avail_out         -= len;
    strm->total_out         += len;
    if (qat->pending_out_count == 0) {
        qat->pending_out = qat->output_buffer;
    }
}

static int qat_compress_pending(deflate_state*s, int flush)
{
    struct qat_deflate* qat = s->qat_s;
    uint32_t metadata_size;

    /* TODO(gustavoa): find a way to make qatzpp setup this number internally. */
    cpaDcBufferListGetMetaSize(qat->qat_session->getInstance()->GetHandle(), 1, &metadata_size);

    auto job = qat->qat_session->Deflate(
        std::make_unique<qat::IOBuffers>(
            std::make_unique<qat::BufferListUser>(
                qat->input_buffer,
                qat->pending_in_count,
                metadata_size
            ),
            std::make_unique<qat::BufferListUser>(
                qat->output_buffer,
                qat->output_buffer_size,
                metadata_size
            )
        ), (flush == Z_FINISH && s->strm->avail_in == 0)
    );

    job->WaitCompletion();

    /*
     *  TODO(gustavoa): make QAT perform the checksum combine.
     */
    if (s->wrap == 2) {
        s->strm->adler = crc32_combine(
            s->strm->adler,
            job->GetResults()->checksum,
            job->GetResults()->consumed
        );
    } else if (s->wrap == 1) {
        s->strm->adler = adler32(
            s->strm->adler,
            qat->input_buffer,
            job->GetResults()->consumed
        );
    }

    qat->pending_out_count = job->GetResults()->produced;
    qat->pending_in_count -= job->GetResults()->consumed;

    if(qat->pending_in_count != 0) {
        /* Copy any remaining bytes to the beginning of the buffer. */
        zmemcpy(
            qat->input_buffer,
            qat->input_buffer + job->GetResults()->consumed,
            qat->pending_in_count
        );
    }

    return 0;
}

qat_block_state qat_deflate_step(deflate_state* s, int flush)
{
    z_streamp strm = s->strm;
    struct qat_deflate* qat_state = s->qat_s;

    for (;;) {
        if (qat_state->pending_in_count < qat_state->input_buffer_size) {
            qat_read_buf(
                strm,
                qat_state,
                qat_state->input_buffer_size - qat_state->pending_in_count
            );
            if (qat_state->pending_in_count < qat_state->input_buffer_size && flush == Z_NO_FLUSH) {
                return qat_block_need_more;
            } else {
                qat_compress_pending(s, flush);
            }
            if (strm->avail_in == 0) {
                break;
            }
        } else {
            qat_compress_pending(s, flush);
        }

        qat_flush_pending(s);
        if (strm->avail_out == 0) {
            return (flush == Z_FINISH) ? qat_block_finish_started : qat_block_need_more;
        }
    }

    if (flush == Z_FINISH) {
        qat_flush_pending(s);
        if (strm->avail_out == 0) {
            return qat_block_finish_started;
        } else {
            return qat_block_finish_done;
        }
    }

    qat_flush_pending(s);
    if (strm->avail_out == 0) {
        return qat_block_done;
    }

    return qat_block_need_more;
}

int qat_deflate_state_free(deflate_state* s)
{
    struct qat_deflate* qat_state = s->qat_s;
    if (qat_state->input_buffer) {
        qat::Free(qat_state->input_buffer);
    }
    if (qat_state->output_buffer) {
        qat::Free(qat_state->output_buffer);
    }

    qat_state->qat_session.reset();
    delete qat_state;
    s->qat_s = nullptr;

    return Z_OK;
}

struct qat_deflate *qat_deflate_copy(deflate_state *ss)
{
    struct qat_deflate *sqat = ss->qat_s;
    struct qat_deflate *dqat = nullptr;

    if (!sqat) {
        return nullptr;
    }

    dqat = new struct qat_deflate;

    dqat->qat_session = qat_create_session(ss->level, ss->wrap);

    dqat->input_buffer_size = sqat->input_buffer_size;
    dqat->input_buffer = qat::AllocBlockArray<uint8_t>(dqat->input_buffer_size, 0);

    dqat->output_buffer_size = sqat->output_buffer_size;
    dqat->output_buffer = qat::AllocBlockArray<uint8_t>(dqat->output_buffer_size, 0);

    dqat->pending_in_count = sqat->pending_in_count;
    dqat->pending_out_count = sqat->pending_out_count;

    dqat->pending_out =
        dqat->output_buffer + (sqat->pending_out - sqat->output_buffer);

    zmemcpy(dqat->input_buffer, sqat->input_buffer, dqat->input_buffer_size);
    zmemcpy(dqat->output_buffer, sqat->output_buffer, dqat->output_buffer_size);

    return dqat;
}

