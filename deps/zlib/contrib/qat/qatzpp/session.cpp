/*
 * Copyright (C) 2024 Intel Corporation. All rights reserved.
 * Authors:
 *  Gustavo A Espinoza   <gustavo.adolfo.espinoza.quintero@intel.com>
 *                       <gustavoaespinozaq@hotmail.com>
 *
 * For conditions of distribution and use, see copyright notice in zlib.h
 */
#include <iostream>
#include <semaphore.h>

#include "memory.hpp"
#include "session.hpp"

namespace qat
{

constexpr CpaDcHuffType kHuffType = CPA_DC_HT_FULL_DYNAMIC;

DeflateSession::DeflateSession(
    std::shared_ptr<Instance> &qat_instance,
    CpaDcCompLvl comp_level, CpaDcChecksum checksum,
    uint32_t numa_node):
    qat_instance_(qat_instance)
{
    uint32_t session_size = 0;
    uint32_t ctx_size = 0;

    CpaDcSessionSetupData sd{};
    sd.compLevel = comp_level;
    sd.compType = CPA_DC_DEFLATE;
    sd.huffType = kHuffType;
    sd.autoSelectBestHuffmanTree = CPA_DC_ASB_UNCOMP_STATIC_DYNAMIC_WITH_STORED_HDRS;
    sd.sessDirection = CPA_DC_DIR_COMBINED;
    sd.sessState = CPA_DC_STATELESS;
    sd.checksum = checksum;

    cpaDcGetSessionSize(qat_instance_->GetHandle(), &sd, &session_size, &ctx_size);
    session_ = AllocBlockArray<uint8_t>(session_size, numa_node);

    cpaDcInitSession(
        qat_instance_->GetHandle(),
        session_,
        &sd,
        nullptr, // No context for stateless operations
        &dc_callback
    );

}

DeflateSession::~DeflateSession()
{
    if (session_) {
        cpaDcRemoveSession(qat_instance_->GetHandle(), session_);
        Free(session_);
    }

    session_ = nullptr;
}

std::unique_ptr<QATTask> DeflateSession::Deflate(
    std::unique_ptr<IOBuffers> &&buffers,
    bool flush_final)
{
    CpaDcOpData op_data{};
    op_data.flushFlag = (flush_final) ?
        CPA_DC_FLUSH_FINAL : CPA_DC_FLUSH_FULL;
    op_data.compressAndVerify = CPA_TRUE;
    op_data.inputSkipData.skipMode = CPA_DC_SKIP_DISABLED;
    op_data.outputSkipData.skipMode = CPA_DC_SKIP_DISABLED;

    auto task = std::make_unique<QATTask>(
        qat_instance_, std::move(buffers),
        std::make_unique<CpaDcRqResults>()
    );

    cpaDcCompressData2(
        qat_instance_->GetHandle(),
        session_,
        &task->GetBuffers()->GetSrc()->list,
        &task->GetBuffers()->GetDst()->list,
        &op_data,
        task->GetResults(),
        static_cast<void*>(task.get())
    );

    return std::move(task);
}

std::unique_ptr<QATTask> DeflateSession::Inflate(std::unique_ptr<IOBuffers> &&buffers)
{
    CpaDcOpData op_data = {};
    op_data.flushFlag = CPA_DC_FLUSH_FINAL;
    op_data.compressAndVerify = CPA_TRUE;
    op_data.inputSkipData.skipMode = CPA_DC_SKIP_DISABLED;
    op_data.outputSkipData.skipMode = CPA_DC_SKIP_DISABLED;

    auto task = std::make_unique<QATTask>(
        qat_instance_, std::move(buffers),
        std::make_unique<CpaDcRqResults>()
    );

    cpaDcDecompressData2(
        qat_instance_->GetHandle(),
        session_,
        &task->GetBuffers()->GetSrc()->list,
        &task->GetBuffers()->GetDst()->list,
        &op_data,
        task->GetResults(),
        static_cast<void*>(task.get())
    );

    return std::move(task);
}

uint32_t DeflateSession::GetDeflateBound(uint32_t input_size)
{
    uint32_t output_size = 0;

    cpaDcDeflateCompressBound(
        qat_instance_->GetHandle(),
        kHuffType,
        input_size, &output_size
    );

    return output_size;
}

}
