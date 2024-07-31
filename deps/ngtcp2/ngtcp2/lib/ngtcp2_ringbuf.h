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
#ifndef NGTCP2_RINGBUF_H
#define NGTCP2_RINGBUF_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <ngtcp2/ngtcp2.h>

#include "ngtcp2_mem.h"

typedef struct ngtcp2_ringbuf {
  /* buf points to the underlying buffer. */
  uint8_t *buf;
  const ngtcp2_mem *mem;
  /* mask is the bit mask to cover all bits for the maximum number of
     elements.  The maximum number of elements is mask + 1. */
  size_t mask;
  /* size is the size of each element. */
  size_t size;
  /* first is the offset to the first element. */
  size_t first;
  /* len is the number of elements actually stored. */
  size_t len;
} ngtcp2_ringbuf;

/*
 * ngtcp2_ringbuf_init initializes |rb|.  |nmemb| is the number of
 * elements that can be stored in this buffer.  |size| is the size of
 * each element.  |size| must be power of 2.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 */
int ngtcp2_ringbuf_init(ngtcp2_ringbuf *rb, size_t nmemb, size_t size,
                        const ngtcp2_mem *mem);

/*
 * ngtcp2_ringbuf_buf_init initializes |rb| with given buffer and
 * size.
 */
void ngtcp2_ringbuf_buf_init(ngtcp2_ringbuf *rb, size_t nmemb, size_t size,
                             uint8_t *buf, const ngtcp2_mem *mem);

/*
 * ngtcp2_ringbuf_free frees resources allocated for |rb|.  This
 * function does not free the memory pointed by |rb|.
 */
void ngtcp2_ringbuf_free(ngtcp2_ringbuf *rb);

/* ngtcp2_ringbuf_push_front moves the offset to the first element in
   the buffer backward, and returns the pointer to the element.
   Caller can store data to the buffer pointed by the returned
   pointer.  If this action exceeds the capacity of the ring buffer,
   the last element is silently overwritten, and rb->len remains
   unchanged. */
void *ngtcp2_ringbuf_push_front(ngtcp2_ringbuf *rb);

/* ngtcp2_ringbuf_push_back moves the offset to the last element in
   the buffer forward, and returns the pointer to the element.  Caller
   can store data to the buffer pointed by the returned pointer.  If
   this action exceeds the capacity of the ring buffer, the first
   element is silently overwritten, and rb->len remains unchanged. */
void *ngtcp2_ringbuf_push_back(ngtcp2_ringbuf *rb);

/*
 * ngtcp2_ringbuf_pop_front removes first element in |rb|.
 */
void ngtcp2_ringbuf_pop_front(ngtcp2_ringbuf *rb);

/*
 * ngtcp2_ringbuf_pop_back removes the last element in |rb|.
 */
void ngtcp2_ringbuf_pop_back(ngtcp2_ringbuf *rb);

/* ngtcp2_ringbuf_resize changes the number of elements stored.  This
   does not change the capacity of the underlying buffer. */
void ngtcp2_ringbuf_resize(ngtcp2_ringbuf *rb, size_t len);

/* ngtcp2_ringbuf_get returns the pointer to the element at
   |offset|. */
void *ngtcp2_ringbuf_get(ngtcp2_ringbuf *rb, size_t offset);

/* ngtcp2_ringbuf_len returns the number of elements stored. */
#define ngtcp2_ringbuf_len(RB) ((RB)->len)

/* ngtcp2_ringbuf_full returns nonzero if |rb| is full. */
int ngtcp2_ringbuf_full(ngtcp2_ringbuf *rb);

/* ngtcp2_static_ringbuf_def defines ngtcp2_ringbuf struct wrapper
   which uses a statically allocated buffer that is suitable for a
   usage that does not change buffer size with ngtcp2_ringbuf_resize.
   ngtcp2_ringbuf_free should never be called for rb field. */
#define ngtcp2_static_ringbuf_def(NAME, NMEMB, SIZE)                           \
  typedef struct ngtcp2_static_ringbuf_##NAME {                                \
    ngtcp2_ringbuf rb;                                                         \
    uint8_t buf[(NMEMB) * (SIZE)];                                             \
  } ngtcp2_static_ringbuf_##NAME;                                              \
                                                                               \
  static inline void ngtcp2_static_ringbuf_##NAME##_init(                      \
      ngtcp2_static_ringbuf_##NAME *srb) {                                     \
    ngtcp2_ringbuf_buf_init(&srb->rb, (NMEMB), (SIZE), srb->buf, NULL);        \
  }

#endif /* NGTCP2_RINGBUF_H */
