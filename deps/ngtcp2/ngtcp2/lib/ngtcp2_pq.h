/*
 * ngtcp2
 *
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
#ifndef NGTCP2_PQ_H
#define NGTCP2_PQ_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <ngtcp2/ngtcp2.h>

#include "ngtcp2_mem.h"

/* Implementation of priority queue */

/* NGTCP2_PQ_BAD_INDEX is the priority queue index which indicates
   that an entry is not queued.  Assigning this value to
   ngtcp2_pq_entry.index can check that the entry is queued or not. */
#define NGTCP2_PQ_BAD_INDEX SIZE_MAX

typedef struct ngtcp2_pq_entry {
  size_t index;
} ngtcp2_pq_entry;

/* ngtcp2_pq_less is a "less" function, that returns nonzero if |lhs|
   is considered to be less than |rhs|. */
typedef int (*ngtcp2_pq_less)(const ngtcp2_pq_entry *lhs,
                              const ngtcp2_pq_entry *rhs);

typedef struct ngtcp2_pq {
  /* q is a pointer to an array that stores the items. */
  ngtcp2_pq_entry **q;
  /* mem is a memory allocator. */
  const ngtcp2_mem *mem;
  /* length is the number of items stored. */
  size_t length;
  /* capacity is the maximum number of items this queue can store.
     This is automatically extended when length is reached to this
     limit. */
  size_t capacity;
  /* less is the less function to compare items. */
  ngtcp2_pq_less less;
} ngtcp2_pq;

/*
 * ngtcp2_pq_init initializes |pq| with compare function |cmp|.
 */
void ngtcp2_pq_init(ngtcp2_pq *pq, ngtcp2_pq_less less, const ngtcp2_mem *mem);

/*
 * ngtcp2_pq_free deallocates any resources allocated for |pq|.  The
 * stored items are not freed by this function.
 */
void ngtcp2_pq_free(ngtcp2_pq *pq);

/*
 * ngtcp2_pq_push adds |item| to |pq|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 */
int ngtcp2_pq_push(ngtcp2_pq *pq, ngtcp2_pq_entry *item);

/*
 * ngtcp2_pq_top returns item at the top of |pq|.  It is undefined if
 * |pq| is empty.
 */
ngtcp2_pq_entry *ngtcp2_pq_top(const ngtcp2_pq *pq);

/*
 * ngtcp2_pq_pop pops item at the top of |pq|.  The popped item is not
 * freed by this function.  It is undefined if |pq| is empty.
 */
void ngtcp2_pq_pop(ngtcp2_pq *pq);

/*
 * ngtcp2_pq_empty returns nonzero if |pq| is empty.
 */
int ngtcp2_pq_empty(const ngtcp2_pq *pq);

/*
 * ngtcp2_pq_size returns the number of items |pq| contains.
 */
size_t ngtcp2_pq_size(const ngtcp2_pq *pq);

typedef int (*ngtcp2_pq_item_cb)(ngtcp2_pq_entry *item, void *arg);

/*
 * ngtcp2_pq_each applies |fun| to each item in |pq|.  The |arg| is
 * passed as arg parameter to callback function.  This function must
 * not change the ordering key.  If the return value from callback is
 * nonzero, this function returns 1 immediately without iterating
 * remaining items.  Otherwise this function returns 0.
 */
int ngtcp2_pq_each(const ngtcp2_pq *pq, ngtcp2_pq_item_cb fun, void *arg);

/*
 * ngtcp2_pq_remove removes |item| from |pq|.  |pq| must contain
 * |item| otherwise the behavior is undefined.
 */
void ngtcp2_pq_remove(ngtcp2_pq *pq, ngtcp2_pq_entry *item);

#endif /* !defined(NGTCP2_PQ_H) */
