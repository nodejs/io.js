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
#ifndef NGHTTP3_RINGBUF_H
#define NGHTTP3_RINGBUF_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <nghttp3/nghttp3.h>

#include "nghttp3_mem.h"

typedef struct nghttp3_ringbuf {
  /* buf points to the underlying buffer. */
  uint8_t *buf;
  const nghttp3_mem *mem;
  /* nmemb is the number of elements that can be stored in this ring
     buffer. */
  size_t nmemb;
  /* size is the size of each element. */
  size_t size;
  /* first is the offset to the first element. */
  size_t first;
  /* len is the number of elements actually stored. */
  size_t len;
} nghttp3_ringbuf;

/*
 * nghttp3_ringbuf_init initializes |rb|.  |nmemb| is the number of
 * elements that can be stored in this buffer.  |size| is the size of
 * each element.  |size| must be power of 2.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 */
int nghttp3_ringbuf_init(nghttp3_ringbuf *rb, size_t nmemb, size_t size,
                         const nghttp3_mem *mem);

/*
 * nghttp3_ringbuf_free frees resources allocated for |rb|.  This
 * function does not free the memory pointed by |rb|.
 */
void nghttp3_ringbuf_free(nghttp3_ringbuf *rb);

/* nghttp3_ringbuf_push_front moves the offset to the first element in
   the buffer backward, and returns the pointer to the element.
   Caller can store data to the buffer pointed by the returned
   pointer.  If this action exceeds the capacity of the ring buffer,
   the last element is silently overwritten, and rb->len remains
   unchanged. */
void *nghttp3_ringbuf_push_front(nghttp3_ringbuf *rb);

/* nghttp3_ringbuf_push_back moves the offset to the last element in
   the buffer forward, and returns the pointer to the element.  Caller
   can store data to the buffer pointed by the returned pointer.  If
   this action exceeds the capacity of the ring buffer, the first
   element is silently overwritten, and rb->len remains unchanged. */
void *nghttp3_ringbuf_push_back(nghttp3_ringbuf *rb);

/*
 * nghttp3_ringbuf_pop_front removes first element in |rb|.
 */
void nghttp3_ringbuf_pop_front(nghttp3_ringbuf *rb);

/*
 * nghttp3_ringbuf_pop_back removes the last element in |rb|.
 */
void nghttp3_ringbuf_pop_back(nghttp3_ringbuf *rb);

/* nghttp3_ringbuf_resize changes the number of elements stored.  This
   does not change the capacity of the underlying buffer. */
void nghttp3_ringbuf_resize(nghttp3_ringbuf *rb, size_t len);

/* nghttp3_ringbuf_get returns the pointer to the element at
   |offset|. */
void *nghttp3_ringbuf_get(nghttp3_ringbuf *rb, size_t offset);

/* nghttp3_ringbuf_len returns the number of elements stored. */
#define nghttp3_ringbuf_len(RB) ((RB)->len)

/* nghttp3_ringbuf_full returns nonzero if |rb| is full. */
int nghttp3_ringbuf_full(nghttp3_ringbuf *rb);

int nghttp3_ringbuf_reserve(nghttp3_ringbuf *rb, size_t nmemb);

#endif /* !defined(NGHTTP3_RINGBUF_H) */
