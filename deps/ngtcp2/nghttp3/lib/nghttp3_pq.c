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
#include "nghttp3_pq.h"

#include <assert.h>

#include "nghttp3_macro.h"

void nghttp3_pq_init(nghttp3_pq *pq, nghttp3_pq_less less,
                     const nghttp3_mem *mem) {
  pq->q = NULL;
  pq->mem = mem;
  pq->length = 0;
  pq->capacity = 0;
  pq->less = less;
}

void nghttp3_pq_free(nghttp3_pq *pq) {
  if (!pq) {
    return;
  }

  nghttp3_mem_free(pq->mem, pq->q);
}

static void swap(nghttp3_pq *pq, size_t i, size_t j) {
  nghttp3_pq_entry *a = pq->q[i];
  nghttp3_pq_entry *b = pq->q[j];

  pq->q[i] = b;
  b->index = i;
  pq->q[j] = a;
  a->index = j;
}

static void bubble_up(nghttp3_pq *pq, size_t index) {
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

int nghttp3_pq_push(nghttp3_pq *pq, nghttp3_pq_entry *item) {
  if (pq->capacity <= pq->length) {
    void *nq;
    size_t ncapacity;

    ncapacity = nghttp3_max_size(4, pq->capacity * 2);

    nq = nghttp3_mem_realloc(pq->mem, pq->q,
                             ncapacity * sizeof(nghttp3_pq_entry *));
    if (nq == NULL) {
      return NGHTTP3_ERR_NOMEM;
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

nghttp3_pq_entry *nghttp3_pq_top(const nghttp3_pq *pq) {
  assert(pq->length);
  return pq->q[0];
}

static void bubble_down(nghttp3_pq *pq, size_t index) {
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

void nghttp3_pq_pop(nghttp3_pq *pq) {
  assert(pq->length);

  pq->q[0] = pq->q[pq->length - 1];
  pq->q[0]->index = 0;
  --pq->length;
  bubble_down(pq, 0);
}

void nghttp3_pq_remove(nghttp3_pq *pq, nghttp3_pq_entry *item) {
  assert(pq->q[item->index] == item);

  if (item->index == 0) {
    nghttp3_pq_pop(pq);
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

int nghttp3_pq_empty(const nghttp3_pq *pq) { return pq->length == 0; }

size_t nghttp3_pq_size(const nghttp3_pq *pq) { return pq->length; }

int nghttp3_pq_each(const nghttp3_pq *pq, nghttp3_pq_item_cb fun, void *arg) {
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

void nghttp3_pq_clear(nghttp3_pq *pq) { pq->length = 0; }
