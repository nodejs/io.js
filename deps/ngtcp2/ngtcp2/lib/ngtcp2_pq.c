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
#include "ngtcp2_pq.h"

#include <assert.h>

#include "ngtcp2_macro.h"

void ngtcp2_pq_init(ngtcp2_pq *pq, ngtcp2_pq_less less, const ngtcp2_mem *mem) {
  pq->q = NULL;
  pq->mem = mem;
  pq->length = 0;
  pq->capacity = 0;
  pq->less = less;
}

void ngtcp2_pq_free(ngtcp2_pq *pq) {
  if (!pq) {
    return;
  }

  ngtcp2_mem_free(pq->mem, pq->q);
}

static void swap(ngtcp2_pq *pq, size_t i, size_t j) {
  ngtcp2_pq_entry *a = pq->q[i];
  ngtcp2_pq_entry *b = pq->q[j];

  pq->q[i] = b;
  b->index = i;
  pq->q[j] = a;
  a->index = j;
}

static void bubble_up(ngtcp2_pq *pq, size_t index) {
  size_t parent;

  while (index) {
    parent = (index - 1) / 2;
    if (!pq->less(pq->q[index], pq->q[parent])) {
      return;
    }

    swap(pq, parent, index);
    index = parent;
  }
}

int ngtcp2_pq_push(ngtcp2_pq *pq, ngtcp2_pq_entry *item) {
  if (pq->capacity <= pq->length) {
    void *nq;
    size_t ncapacity;

    ncapacity = ngtcp2_max_size(4, pq->capacity * 2);

    nq =
      ngtcp2_mem_realloc(pq->mem, pq->q, ncapacity * sizeof(ngtcp2_pq_entry *));
    if (nq == NULL) {
      return NGTCP2_ERR_NOMEM;
    }

    pq->capacity = ncapacity;
    pq->q = nq;
  }

  pq->q[pq->length] = item;
  item->index = pq->length;
  ++pq->length;
  bubble_up(pq, item->index);

  return 0;
}

ngtcp2_pq_entry *ngtcp2_pq_top(const ngtcp2_pq *pq) {
  assert(pq->length);
  return pq->q[0];
}

static void bubble_down(ngtcp2_pq *pq, size_t index) {
  size_t i, j, minindex;

  for (;;) {
    j = index * 2 + 1;
    minindex = index;

    for (i = 0; i < 2; ++i, ++j) {
      if (j >= pq->length) {
        break;
      }

      if (pq->less(pq->q[j], pq->q[minindex])) {
        minindex = j;
      }
    }

    if (minindex == index) {
      return;
    }

    swap(pq, index, minindex);
    index = minindex;
  }
}

void ngtcp2_pq_pop(ngtcp2_pq *pq) {
  assert(pq->length);

  pq->q[0] = pq->q[pq->length - 1];
  pq->q[0]->index = 0;
  --pq->length;
  bubble_down(pq, 0);
}

void ngtcp2_pq_remove(ngtcp2_pq *pq, ngtcp2_pq_entry *item) {
  assert(pq->q[item->index] == item);

  if (item->index == 0) {
    ngtcp2_pq_pop(pq);
    return;
  }

  if (item->index == pq->length - 1) {
    --pq->length;
    return;
  }

  pq->q[item->index] = pq->q[pq->length - 1];
  pq->q[item->index]->index = item->index;
  --pq->length;

  if (pq->less(item, pq->q[item->index])) {
    bubble_down(pq, item->index);
  } else {
    bubble_up(pq, item->index);
  }
}

int ngtcp2_pq_empty(const ngtcp2_pq *pq) { return pq->length == 0; }

size_t ngtcp2_pq_size(const ngtcp2_pq *pq) { return pq->length; }

int ngtcp2_pq_each(const ngtcp2_pq *pq, ngtcp2_pq_item_cb fun, void *arg) {
  size_t i;

  if (pq->length == 0) {
    return 0;
  }

  for (i = 0; i < pq->length; ++i) {
    if ((*fun)(pq->q[i], arg)) {
      return 1;
    }
  }

  return 0;
}
