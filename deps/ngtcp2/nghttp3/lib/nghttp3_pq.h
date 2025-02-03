/*
 * nghttp3
 *
 * Copyright (c) 2019 nghttp3 contributors
 * Copyright (c) 2017 ngtcp2 contributors
 * Copyright (c) 2012 nghttp2 contributors
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
#ifndef NGHTTP3_PQ_H
#define NGHTTP3_PQ_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <nghttp3/nghttp3.h>

#include "nghttp3_mem.h"

/* Implementation of priority queue */

/* NGHTTP3_PQ_BAD_INDEX is the priority queue index which indicates
   that an entry is not queued.  Assigning this value to
   nghttp3_pq_entry.index can check that the entry is queued or
   not. */
#define NGHTTP3_PQ_BAD_INDEX SIZE_MAX

typedef struct nghttp3_pq_entry {
  size_t index;
} nghttp3_pq_entry;

/* nghttp3_pq_less is a "less" function, that returns nonzero if |lhs|
   is considered to be less than |rhs|. */
typedef int (*nghttp3_pq_less)(const nghttp3_pq_entry *lhs,
                               const nghttp3_pq_entry *rhs);

typedef struct nghttp3_pq {
  /* q is a pointer to an array that stores the items. */
  nghttp3_pq_entry **q;
  /* mem is a memory allocator. */
  const nghttp3_mem *mem;
  /* length is the number of items stored. */
  size_t length;
  /* capacity is the maximum number of items this queue can store.
     This is automatically extended when length is reached to this
     limit. */
  size_t capacity;
  /* less is the less function to compare items. */
  nghttp3_pq_less less;
} nghttp3_pq;

/*
 * nghttp3_pq_init initializes |pq| with compare function |cmp|.
 */
void nghttp3_pq_init(nghttp3_pq *pq, nghttp3_pq_less less,
                     const nghttp3_mem *mem);

/*
 * nghttp3_pq_free deallocates any resources allocated for |pq|.  The
 * stored items are not freed by this function.
 */
void nghttp3_pq_free(nghttp3_pq *pq);

/*
 * nghttp3_pq_push adds |item| to |pq|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 */
int nghttp3_pq_push(nghttp3_pq *pq, nghttp3_pq_entry *item);

/*
 * nghttp3_pq_top returns item at the top of |pq|.  It is undefined if
 * |pq| is empty.
 */
nghttp3_pq_entry *nghttp3_pq_top(const nghttp3_pq *pq);

/*
 * nghttp3_pq_pop pops item at the top of |pq|.  The popped item is
 * not freed by this function.  It is undefined if |pq| is empty.
 */
void nghttp3_pq_pop(nghttp3_pq *pq);

/*
 * nghttp3_pq_empty returns nonzero if |pq| is empty.
 */
int nghttp3_pq_empty(const nghttp3_pq *pq);

/*
 * nghttp3_pq_size returns the number of items |pq| contains.
 */
size_t nghttp3_pq_size(const nghttp3_pq *pq);

typedef int (*nghttp3_pq_item_cb)(nghttp3_pq_entry *item, void *arg);

/*
 * nghttp3_pq_each applies |fun| to each item in |pq|.  The |arg| is
 * passed as arg parameter to callback function.  This function must
 * not change the ordering key.  If the return value from callback is
 * nonzero, this function returns 1 immediately without iterating
 * remaining items.  Otherwise this function returns 0.
 */
int nghttp3_pq_each(const nghttp3_pq *pq, nghttp3_pq_item_cb fun, void *arg);

/*
 * nghttp3_pq_remove removes |item| from |pq|.  |pq| must contain
 * |item| otherwise the behavior is undefined.
 */
void nghttp3_pq_remove(nghttp3_pq *pq, nghttp3_pq_entry *item);

/*
 * nghttp3_pq_clear removes all items from |pq|.
 */
void nghttp3_pq_clear(nghttp3_pq *pq);

#endif /* !defined(NGHTTP3_PQ_H) */
