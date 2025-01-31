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
#include "nghttp3_balloc.h"

#include <assert.h>

#include "nghttp3_mem.h"

void nghttp3_balloc_init(nghttp3_balloc *balloc, size_t blklen,
                         const nghttp3_mem *mem) {
  assert((blklen & 0xfu) == 0);

  balloc->mem = mem;
  balloc->blklen = blklen;
  balloc->head = NULL;
  nghttp3_buf_wrap_init(&balloc->buf, (void *)"", 0);
}

void nghttp3_balloc_free(nghttp3_balloc *balloc) {
  if (balloc == NULL) {
    return;
  }

  nghttp3_balloc_clear(balloc);
}

void nghttp3_balloc_clear(nghttp3_balloc *balloc) {
  nghttp3_memblock_hd *p, *next;

  for (p = balloc->head; p; p = next) {
    next = p->next;
    nghttp3_mem_free(balloc->mem, p);
  }

  balloc->head = NULL;
  nghttp3_buf_wrap_init(&balloc->buf, (void *)"", 0);
}

int nghttp3_balloc_get(nghttp3_balloc *balloc, void **pbuf, size_t n) {
  uint8_t *p;
  nghttp3_memblock_hd *hd;

  assert(n <= balloc->blklen);

  if (nghttp3_buf_left(&balloc->buf) < n) {
    p = nghttp3_mem_malloc(balloc->mem,
                           sizeof(nghttp3_memblock_hd) + 0x8u + balloc->blklen);
    if (p == NULL) {
      return NGHTTP3_ERR_NOMEM;
    }

    hd = (nghttp3_memblock_hd *)(void *)p;
    hd->next = balloc->head;
    balloc->head = hd;
    nghttp3_buf_wrap_init(
      &balloc->buf,
      (uint8_t *)(((uintptr_t)p + sizeof(nghttp3_memblock_hd) + 0xfu) &
                  ~(uintptr_t)0xfu),
      balloc->blklen);
  }

  assert(((uintptr_t)balloc->buf.last & 0xfu) == 0);

  *pbuf = balloc->buf.last;
  balloc->buf.last += (n + 0xfu) & ~(uintptr_t)0xfu;

  return 0;
}
