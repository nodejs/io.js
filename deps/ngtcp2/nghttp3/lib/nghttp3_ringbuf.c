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
#include "nghttp3_ringbuf.h"

#include <assert.h>
#include <string.h>
#ifdef WIN32
#  include <intrin.h>
#endif /* defined(WIN32) */

#include "nghttp3_macro.h"

static int ispow2(size_t n) {
#if defined(_MSC_VER) && !defined(__clang__) &&                                \
  (defined(_M_ARM) || (defined(_M_ARM64) && _MSC_VER < 1941))
  return n && !(n & (n - 1));
#elif defined(WIN32)
  return 1 == __popcnt((unsigned int)n);
#else  /* !((defined(_MSC_VER) && !defined(__clang__) && (defined(_M_ARM) ||   \
          (defined(_M_ARM64) && _MSC_VER < 1941))) || defined(WIN32)) */
  return 1 == __builtin_popcount((unsigned int)n);
#endif /* !((defined(_MSC_VER) && !defined(__clang__) && (defined(_M_ARM) ||   \
          (defined(_M_ARM64) && _MSC_VER < 1941))) || defined(WIN32)) */
}

int nghttp3_ringbuf_init(nghttp3_ringbuf *rb, size_t nmemb, size_t size,
                         const nghttp3_mem *mem) {
  if (nmemb) {
    assert(ispow2(nmemb));

    rb->buf = nghttp3_mem_malloc(mem, nmemb * size);
    if (rb->buf == NULL) {
      return NGHTTP3_ERR_NOMEM;
    }
  } else {
    rb->buf = NULL;
  }

  rb->mem = mem;
  rb->nmemb = nmemb;
  rb->size = size;
  rb->first = 0;
  rb->len = 0;

  return 0;
}

void nghttp3_ringbuf_free(nghttp3_ringbuf *rb) {
  if (rb == NULL) {
    return;
  }

  nghttp3_mem_free(rb->mem, rb->buf);
}

void *nghttp3_ringbuf_push_front(nghttp3_ringbuf *rb) {
  rb->first = (rb->first - 1) & (rb->nmemb - 1);
  rb->len = nghttp3_min_size(rb->nmemb, rb->len + 1);

  return (void *)&rb->buf[rb->first * rb->size];
}

void *nghttp3_ringbuf_push_back(nghttp3_ringbuf *rb) {
  size_t offset = (rb->first + rb->len) & (rb->nmemb - 1);

  if (rb->len == rb->nmemb) {
    rb->first = (rb->first + 1) & (rb->nmemb - 1);
  } else {
    ++rb->len;
  }

  return (void *)&rb->buf[offset * rb->size];
}

void nghttp3_ringbuf_pop_front(nghttp3_ringbuf *rb) {
  rb->first = (rb->first + 1) & (rb->nmemb - 1);
  --rb->len;
}

void nghttp3_ringbuf_pop_back(nghttp3_ringbuf *rb) {
  assert(rb->len);
  --rb->len;
}

void nghttp3_ringbuf_resize(nghttp3_ringbuf *rb, size_t len) {
  assert(len <= rb->nmemb);
  rb->len = len;
}

void *nghttp3_ringbuf_get(nghttp3_ringbuf *rb, size_t offset) {
  assert(offset < rb->len);
  offset = (rb->first + offset) & (rb->nmemb - 1);
  return &rb->buf[offset * rb->size];
}

int nghttp3_ringbuf_full(nghttp3_ringbuf *rb) { return rb->len == rb->nmemb; }

int nghttp3_ringbuf_reserve(nghttp3_ringbuf *rb, size_t nmemb) {
  uint8_t *buf;

  if (rb->nmemb >= nmemb) {
    return 0;
  }

  assert(ispow2(nmemb));

  buf = nghttp3_mem_malloc(rb->mem, nmemb * rb->size);
  if (buf == NULL) {
    return NGHTTP3_ERR_NOMEM;
  }

  if (rb->buf != NULL) {
    if (rb->first + rb->len <= rb->nmemb) {
      memcpy(buf, rb->buf + rb->first * rb->size, rb->len * rb->size);
      rb->first = 0;
    } else {
      memcpy(buf, rb->buf + rb->first * rb->size,
             (rb->nmemb - rb->first) * rb->size);
      memcpy(buf + (rb->nmemb - rb->first) * rb->size, rb->buf,
             (rb->len - (rb->nmemb - rb->first)) * rb->size);
      rb->first = 0;
    }

    nghttp3_mem_free(rb->mem, rb->buf);
  }

  rb->buf = buf;
  rb->nmemb = nmemb;

  return 0;
}
