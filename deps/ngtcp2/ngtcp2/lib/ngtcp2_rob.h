/*
 * ngtcp2
 *
 * Copyright (c) 2017 ngtcp2 contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef NGTCP2_ROB_H
#define NGTCP2_ROB_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <ngtcp2/ngtcp2.h>

#include "ngtcp2_mem.h"
#include "ngtcp2_range.h"
#include "ngtcp2_ksl.h"

/*
 * ngtcp2_rob_gap represents the gap, which is the range of stream
 * data that is not received yet.
 */
typedef struct ngtcp2_rob_gap {
  /* range is the range of this gap. */
  ngtcp2_range range;
} ngtcp2_rob_gap;

/*
 * ngtcp2_rob_gap_new allocates new ngtcp2_rob_gap object, and assigns
 * its pointer to |*pg|.  The caller should call ngtcp2_rob_gap_del to
 * delete it when it is no longer used.  The range of the gap is
 * [begin, end).  |mem| is custom memory allocator to allocate memory.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 */
int ngtcp2_rob_gap_new(ngtcp2_rob_gap **pg, uint64_t begin, uint64_t end,
                       const ngtcp2_mem *mem);

/*
 * ngtcp2_rob_gap_del deallocates |g|.  It deallocates the memory
 * pointed by |g| it self.  |mem| is custom memory allocator to
 * deallocate memory.
 */
void ngtcp2_rob_gap_del(ngtcp2_rob_gap *g, const ngtcp2_mem *mem);

/*
 * ngtcp2_rob_data holds the buffered stream data.
 */
typedef struct ngtcp2_rob_data {
  /* range is the range of this data. */
  ngtcp2_range range;
  /* begin points to the buffer. */
  uint8_t *begin;
} ngtcp2_rob_data;

/*
 * ngtcp2_rob_data_new allocates new ngtcp2_rob_data object, and
 * assigns its pointer to |*pd|.  The caller should call
 * ngtcp2_rob_data_del to delete it when it is no longer used.
 * |offset| is the stream offset of the first byte of this data.
 * |chunk| is the size of the buffer.  |offset| must be multiple of
 * |chunk|.  |mem| is custom memory allocator to allocate memory.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 */
int ngtcp2_rob_data_new(ngtcp2_rob_data **pd, uint64_t offset, size_t chunk,
                        const ngtcp2_mem *mem);

/*
 * ngtcp2_rob_data_del deallocates |d|.  It deallocates the memory
 * pointed by |d| itself.  |mem| is custom memory allocator to
 * deallocate memory.
 */
void ngtcp2_rob_data_del(ngtcp2_rob_data *d, const ngtcp2_mem *mem);

/*
 * ngtcp2_rob is the reorder buffer which reassembles stream data
 * received in out of order.
 */
typedef struct ngtcp2_rob {
  /* gapksl maintains the range of offset which is not received
     yet. Initially, its range is [0, UINT64_MAX). */
  ngtcp2_ksl gapksl;
  /* dataksl maintains the buffers which store received out-of-order
     data ordered by stream offset. */
  ngtcp2_ksl dataksl;
  /* mem is custom memory allocator */
  const ngtcp2_mem *mem;
  /* chunk is the size of each buffer in data field */
  size_t chunk;
} ngtcp2_rob;

/*
 * ngtcp2_rob_init initializes |rob|.  |chunk| is the size of buffer
 * per chunk.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 */
int ngtcp2_rob_init(ngtcp2_rob *rob, size_t chunk, const ngtcp2_mem *mem);

/*
 * ngtcp2_rob_free frees resources allocated for |rob|.
 */
void ngtcp2_rob_free(ngtcp2_rob *rob);

/*
 * ngtcp2_rob_push adds new data pointed by |data| of length |datalen|
 * at the stream offset |offset|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory
 */
int ngtcp2_rob_push(ngtcp2_rob *rob, uint64_t offset, const uint8_t *data,
                    size_t datalen);

/*
 * ngtcp2_rob_remove_prefix removes gap up to |offset|, exclusive.  It
 * also removes buffered data if it is completely included in
 * |offset|.
 */
void ngtcp2_rob_remove_prefix(ngtcp2_rob *rob, uint64_t offset);

/*
 * ngtcp2_rob_data_at stores the pointer to the buffer of stream
 * offset |offset| to |*pdest| if it is available, and returns the
 * valid length of available data.  If no data is available, it
 * returns 0.  This function only returns the data before the first
 * gap.  It returns 0 even if data is available after the first gap.
 */
size_t ngtcp2_rob_data_at(const ngtcp2_rob *rob, const uint8_t **pdest,
                          uint64_t offset);

/*
 * ngtcp2_rob_pop clears data at stream offset |offset| of length
 * |len|.
 *
 * |offset| must be the offset given in ngtcp2_rob_data_at.  |len|
 * must be the return value of ngtcp2_rob_data_at when |offset| is
 * passed.
 *
 * Caller should call this function from offset 0 in non-decreasing
 * order.
 */
void ngtcp2_rob_pop(ngtcp2_rob *rob, uint64_t offset, size_t len);

/*
 * ngtcp2_rob_first_gap_offset returns the offset to the first gap.
 * If there is no gap, it returns UINT64_MAX.
 */
uint64_t ngtcp2_rob_first_gap_offset(const ngtcp2_rob *rob);

/*
 * ngtcp2_rob_data_buffered returns nonzero if any data is buffered.
 */
int ngtcp2_rob_data_buffered(const ngtcp2_rob *rob);

#endif /* !defined(NGTCP2_ROB_H) */
