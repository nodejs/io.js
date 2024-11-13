/*
 * Copyright (C) 2024 Intel Corporation. All rights reserved.
 * Authors:
 *  Gustavo A Espinoza   <gustavo.adolfo.espinoza.quintero@intel.com>
 *                       <gustavoaespinozaq@hotmail.com>
 *
 * For conditions of distribution and use, see copyright notice in zlib.h
 */
#ifndef DEFLATE_QAT_H
#define DEFLATE_QAT_H

#include "deflate.h"

#ifdef __cplusplus
extern "C" {
#endif

/* This is a 1:1 mapping of the block states that deflate_fast, deflate_slow,
 * deflate_rle, etc.. return.
 * The added 'qat_failure' value is used for signaling the caller to revert
 * back into software mode.
 */
typedef enum {
    qat_block_need_more,
    qat_block_done,
    qat_block_finish_started,
    qat_block_finish_done,
    qat_failure
} qat_block_state;

/* Initialize QAT for the calling process if it has not been yet initialized. */
int qat_deflate_init();

/* Initialize a QAT stream state for a deflate_state object. */
struct qat_deflate *qat_deflate_state_init(int level, int wra);

/* Flush QAT output buffer into the zstream.next_out pointer. */
void qat_flush_pending(deflate_state*);

/* Compresses/copies/flushes any data in the internal QAT state
 * input/output buffers.
*/
qat_block_state qat_deflate_step(deflate_state*, int flush);

/* Frees all the QAT-related buffers and objects for a given deflate_state. */
int qat_deflate_state_free(deflate_state*);

struct qat_deflate *qat_deflate_copy(deflate_state *ss);

#ifdef __cplusplus
}
#endif

#endif