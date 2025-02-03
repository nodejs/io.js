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
#include "ngtcp2_ringbuf.h"

#include <assert.h>
#ifdef WIN32
#  include <intrin.h>
#endif /* defined(WIN32) */

#include "ngtcp2_macro.h"

#ifndef NDEBUG
static int ispow2(size_t n) {
#  if defined(_MSC_VER) && !defined(__clang__) &&                              \
    (defined(_M_ARM) || (defined(_M_ARM64) && _MSC_VER < 1941))
  return n && !(n & (n - 1));
#  elif defined(WIN32)
  return 1 == __popcnt((unsigned int)n);
#  else  /* !((defined(_MSC_VER) && !defined(__clang__) && (defined(_M_ARM) || \
            (defined(_M_ARM64) && _MSC_VER < 1941))) || defined(WIN32)) */
  return 1 == __builtin_popcount((unsigned int)n);
#  endif /* !((defined(_MSC_VER) && !defined(__clang__) && (defined(_M_ARM) || \
            (defined(_M_ARM64) && _MSC_VER < 1941))) || defined(WIN32)) */
}
#endif /* !defined(NDEBUG) */

int ngtcp2_ringbuf_init(ngtcp2_ringbuf *rb, size_t nmemb, size_t size,
                        const ngtcp2_mem *mem) {
  uint8_t *buf = ngtcp2_mem_malloc(mem, nmemb * size);

  if (buf == NULL) {
    return NGTCP2_ERR_NOMEM;
  }

  ngtcp2_ringbuf_buf_init(rb, nmemb, size, buf, mem);

  return 0;
}

void ngtcp2_ringbuf_buf_init(ngtcp2_ringbuf *rb, size_t nmemb, size_t size,
                             uint8_t *buf, const ngtcp2_mem *mem) {
  assert(ispow2(nmemb));

  rb->buf = buf;
  rb->mem = mem;
  rb->mask = nmemb - 1;
  rb->size = size;
  rb->first = 0;
  rb->len = 0;
}

void ngtcp2_ringbuf_free(ngtcp2_ringbuf *rb) {
  if (rb == NULL) {
    return;
  }

  ngtcp2_mem_free(rb->mem, rb->buf);
}

void *ngtcp2_ringbuf_push_front(ngtcp2_ringbuf *rb) {
  rb->first = (rb->first - 1) & rb->mask;
  if (rb->len < rb->mask + 1) {
    ++rb->len;
  }

  return (void *)&rb->buf[rb->first * rb->size];
}

void *ngtcp2_ringbuf_push_back(ngtcp2_ringbuf *rb) {
  size_t offset = (rb->first + rb->len) & rb->mask;

  if (rb->len == rb->mask + 1) {
    rb->first = (rb->first + 1) & rb->mask;
  } else {
    ++rb->len;
  }

  return (void *)&rb->buf[offset * rb->size];
}

void ngtcp2_ringbuf_pop_front(ngtcp2_ringbuf *rb) {
  rb->first = (rb->first + 1) & rb->mask;
  --rb->len;
}

void ngtcp2_ringbuf_pop_back(ngtcp2_ringbuf *rb) {
  assert(rb->len);
  --rb->len;
}

void ngtcp2_ringbuf_resize(ngtcp2_ringbuf *rb, size_t len) {
  assert(len <= rb->mask + 1);
  rb->len = len;
}

void *ngtcp2_ringbuf_get(const ngtcp2_ringbuf *rb, size_t offset) {
  assert(offset < rb->len);
  offset = (rb->first + offset) & rb->mask;

  return &rb->buf[offset * rb->size];
}

int ngtcp2_ringbuf_full(ngtcp2_ringbuf *rb) { return rb->len == rb->mask + 1; }
