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
#endif /* HAVE_CONFIG_H */

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

/* "less" function, return nonzero if |lhs| is less than |rhs|. */
typedef int (*ngtcp2_less)(const ngtcp2_pq_entry *lhs,
                           const ngtcp2_pq_entry *rhs);

typedef struct ngtcp2_pq {
  /* The pointer to the pointer to the item stored */
  ngtcp2_pq_entry **q;
  /* Memory allocator */
  const ngtcp2_mem *mem;
  /* The number of items stored */
  size_t length;
  /* The maximum number of items this pq can store. This is
     automatically extended when length is reached to this value. */
  size_t capacity;
  /* The less function between items */
  ngtcp2_less less;
} ngtcp2_pq;

/*
 * Initializes priority queue |pq| with compare function |cmp|.
 */
void ngtcp2_pq_init(ngtcp2_pq *pq, ngtcp2_less less, const ngtcp2_mem *mem);

/*
 * Deallocates any resources allocated for |pq|.  The stored items are
 * not freed by this function.
 */
void ngtcp2_pq_free(ngtcp2_pq *pq);

/*
 * Adds |item| to the priority queue |pq|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 */
int ngtcp2_pq_push(ngtcp2_pq *pq, ngtcp2_pq_entry *item);

/*
 * Returns item at the top of the queue |pq|.  It is undefined if the
 * queue is empty.
 */
ngtcp2_pq_entry *ngtcp2_pq_top(ngtcp2_pq *pq);

/*
 * Pops item at the top of the queue |pq|. The popped item is not
 * freed by this function.
 */
void ngtcp2_pq_pop(ngtcp2_pq *pq);

/*
 * Returns nonzero if the queue |pq| is empty.
 */
int ngtcp2_pq_empty(ngtcp2_pq *pq);

/*
 * Returns the number of items in the queue |pq|.
 */
size_t ngtcp2_pq_size(ngtcp2_pq *pq);

typedef int (*ngtcp2_pq_item_cb)(ngtcp2_pq_entry *item, void *arg);

/*
 * Applies |fun| to each item in |pq|.  The |arg| is passed as arg
 * parameter to callback function.  This function must not change the
 * ordering key.  If the return value from callback is nonzero, this
 * function returns 1 immediately without iterating remaining items.
 * Otherwise this function returns 0.
 */
int ngtcp2_pq_each(ngtcp2_pq *pq, ngtcp2_pq_item_cb fun, void *arg);

/*
 * Removes |item| from priority queue.
 */
void ngtcp2_pq_remove(ngtcp2_pq *pq, ngtcp2_pq_entry *item);

#endif /* NGTCP2_PQ_H */
