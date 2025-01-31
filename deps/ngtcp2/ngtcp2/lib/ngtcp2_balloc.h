/*
 * ngtcp2
 *
 * Copyright (c) 2022 ngtcp2 contributors
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
#ifndef NGTCP2_BALLOC_H
#define NGTCP2_BALLOC_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <ngtcp2/ngtcp2.h>

#include "ngtcp2_buf.h"

typedef struct ngtcp2_memblock_hd ngtcp2_memblock_hd;

/*
 * ngtcp2_memblock_hd is the header of memory block.
 */
struct ngtcp2_memblock_hd {
  union {
    ngtcp2_memblock_hd *next;
    uint64_t pad;
  };
};

/*
 * ngtcp2_balloc is a custom memory allocator.  It allocates |blklen|
 * bytes of memory at once on demand, and returns its slice when the
 * allocation is requested.
 */
typedef struct ngtcp2_balloc {
  /* mem is the underlying memory allocator. */
  const ngtcp2_mem *mem;
  /* blklen is the size of memory block. */
  size_t blklen;
  /* head points to the list of memory block allocated so far. */
  ngtcp2_memblock_hd *head;
  /* buf wraps the current memory block for allocation requests. */
  ngtcp2_buf buf;
} ngtcp2_balloc;

/*
 * ngtcp2_balloc_init initializes |balloc| with |blklen| which is the
 * size of memory block.  |blklen| must be divisible by 16.
 */
void ngtcp2_balloc_init(ngtcp2_balloc *balloc, size_t blklen,
                        const ngtcp2_mem *mem);

/*
 * ngtcp2_balloc_free releases all allocated memory blocks.
 */
void ngtcp2_balloc_free(ngtcp2_balloc *balloc);

/*
 * ngtcp2_balloc_get allocates |n| bytes of memory and assigns its
 * pointer to |*pbuf|.
 *
 * It returns 0 if it succeeds, or one of the following negative error
 * codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 */
int ngtcp2_balloc_get(ngtcp2_balloc *balloc, void **pbuf, size_t n);

/*
 * ngtcp2_balloc_clear releases all allocated memory blocks and
 * initializes its state.
 */
void ngtcp2_balloc_clear(ngtcp2_balloc *balloc);

#endif /* !defined(NGTCP2_BALLOC_H) */
