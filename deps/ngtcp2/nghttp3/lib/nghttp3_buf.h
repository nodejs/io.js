/*
 * nghttp3
 *
 * Copyright (c) 2019 nghttp3 contributors
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
#ifndef NGHTTP3_BUF_H
#define NGHTTP3_BUF_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <nghttp3/nghttp3.h>

#include "nghttp3_mem.h"

void nghttp3_buf_wrap_init(nghttp3_buf *buf, uint8_t *src, size_t len);

/*
 * nghttp3_buf_cap returns the capacity of the buffer.  In other
 * words, it returns buf->end - buf->begin.
 */
size_t nghttp3_buf_cap(const nghttp3_buf *buf);

int nghttp3_buf_reserve(nghttp3_buf *buf, size_t size, const nghttp3_mem *mem);

/*
 * nghttp3_buf_swap swaps |a| and |b|.
 */
void nghttp3_buf_swap(nghttp3_buf *a, nghttp3_buf *b);

typedef enum nghttp3_buf_type {
  /* NGHTTP3_BUF_TYPE_PRIVATE indicates that memory is allocated for
     this buffer only and should be freed after its use. */
  NGHTTP3_BUF_TYPE_PRIVATE,
  /* NGHTTP3_BUF_TYPE_SHARED indicates that buffer points to shared
     memory. */
  NGHTTP3_BUF_TYPE_SHARED,
  /* NGHTTP3_BUF_TYPE_ALIEN indicates that the buffer points to a
     memory which comes from outside of the library. */
  NGHTTP3_BUF_TYPE_ALIEN,
} nghttp3_buf_type;

typedef struct nghttp3_typed_buf {
  nghttp3_buf buf;
  nghttp3_buf_type type;
} nghttp3_typed_buf;

void nghttp3_typed_buf_init(nghttp3_typed_buf *tbuf, const nghttp3_buf *buf,
                            nghttp3_buf_type type);

void nghttp3_typed_buf_free(nghttp3_typed_buf *tbuf);

#endif /* !defined(NGHTTP3_BUF_H) */
