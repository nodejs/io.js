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
#include "ngtcp2_balloc.h"

#include <assert.h>

#include "ngtcp2_mem.h"

void ngtcp2_balloc_init(ngtcp2_balloc *balloc, size_t blklen,
                        const ngtcp2_mem *mem) {
  assert((blklen & 0xfu) == 0);

  balloc->mem = mem;
  balloc->blklen = blklen;
  balloc->head = NULL;
  ngtcp2_buf_init(&balloc->buf, (void *)"", 0);
}

void ngtcp2_balloc_free(ngtcp2_balloc *balloc) {
  if (balloc == NULL) {
    return;
  }

  ngtcp2_balloc_clear(balloc);
}

void ngtcp2_balloc_clear(ngtcp2_balloc *balloc) {
  ngtcp2_memblock_hd *p, *next;

  for (p = balloc->head; p; p = next) {
    next = p->next;
    ngtcp2_mem_free(balloc->mem, p);
  }

  balloc->head = NULL;
  ngtcp2_buf_init(&balloc->buf, (void *)"", 0);
}

int ngtcp2_balloc_get(ngtcp2_balloc *balloc, void **pbuf, size_t n) {
  uint8_t *p;
  ngtcp2_memblock_hd *hd;

  assert(n <= balloc->blklen);

  if (ngtcp2_buf_left(&balloc->buf) < n) {
    p = ngtcp2_mem_malloc(balloc->mem,
                          sizeof(ngtcp2_memblock_hd) + 0x8u + balloc->blklen);
    if (p == NULL) {
      return NGTCP2_ERR_NOMEM;
    }

    hd = (ngtcp2_memblock_hd *)(void *)p;
    hd->next = balloc->head;
    balloc->head = hd;
    ngtcp2_buf_init(
      &balloc->buf,
      (uint8_t *)(((uintptr_t)p + sizeof(ngtcp2_memblock_hd) + 0xfu) &
                  ~(uintptr_t)0xfu),
      balloc->blklen);
  }

  assert(((uintptr_t)balloc->buf.last & 0xfu) == 0);

  *pbuf = balloc->buf.last;
  balloc->buf.last += (n + 0xfu) & ~(uintptr_t)0xfu;

  return 0;
}
