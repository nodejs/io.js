/*
 * nghttp3
 *
 * Copyright (c) 2022 nghttp3 contributors
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
#ifndef NGHTTP3_BALLOC_H
#define NGHTTP3_BALLOC_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <nghttp3/nghttp3.h>

#include "nghttp3_buf.h"

typedef struct nghttp3_memblock_hd nghttp3_memblock_hd;

/*
 * nghttp3_memblock_hd is the header of memory block.
 */
struct nghttp3_memblock_hd {
  union {
    nghttp3_memblock_hd *next;
    uint64_t pad;
  };
};

/*
 * nghttp3_balloc is a custom memory allocator.  It allocates |blklen|
 * bytes of memory at once on demand, and returns its slice when the
 * allocation is requested.
 */
typedef struct nghttp3_balloc {
  /* mem is the underlying memory allocator. */
  const nghttp3_mem *mem;
  /* blklen is the size of memory block. */
  size_t blklen;
  /* head points to the list of memory block allocated so far. */
  nghttp3_memblock_hd *head;
  /* buf wraps the current memory block for allocation requests. */
  nghttp3_buf buf;
} nghttp3_balloc;

/*
 * nghttp3_balloc_init initializes |balloc| with |blklen| which is the
 * size of memory block.
 */
void nghttp3_balloc_init(nghttp3_balloc *balloc, size_t blklen,
                         const nghttp3_mem *mem);

/*
 * nghttp3_balloc_free releases all allocated memory blocks.
 */
void nghttp3_balloc_free(nghttp3_balloc *balloc);

/*
 * nghttp3_balloc_get allocates |n| bytes of memory and assigns its
 * pointer to |*pbuf|.
 *
 * It returns 0 if it succeeds, or one of the following negative error
 * codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 */
int nghttp3_balloc_get(nghttp3_balloc *balloc, void **pbuf, size_t n);

/*
 * nghttp3_balloc_clear releases all allocated memory blocks and
 * initializes its state.
 */
void nghttp3_balloc_clear(nghttp3_balloc *balloc);

#endif /* !defined(NGHTTP3_BALLOC_H) */
