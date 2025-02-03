/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2012 Tatsuhiro Tsujikawa
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
#include "nghttp2_pq.h"

#include <stdio.h>
#include <assert.h>

#include "nghttp2_helper.h"

void nghttp2_pq_init(nghttp2_pq *pq, nghttp2_less less, nghttp2_mem *mem) {
  pq->mem = mem;
  pq->capacity = 0;
  pq->q = NULL;
  pq->length = 0;
  pq->less = less;
}

void nghttp2_pq_free(nghttp2_pq *pq) {
  nghttp2_mem_free(pq->mem, pq->q);
  pq->q = NULL;
}

static void swap(nghttp2_pq *pq, size_t i, size_t j) {
  nghttp2_pq_entry *a = pq->q[i];
  nghttp2_pq_entry *b = pq->q[j];

  pq->q[i] = b;
  b->index = i;
  pq->q[j] = a;
  a->index = j;
}

static void bubble_up(nghttp2_pq *pq, size_t index) {
  size_t parent;
  while (index != 0) {
    parent = (index - 1) / 2;
    if (!pq->less(pq->q[index], pq->q[parent])) {
      return;
    }
    swap(pq, parent, index);
    index = parent;
  }
}

int nghttp2_pq_push(nghttp2_pq *pq, nghttp2_pq_entry *item) {
  if (pq->capacity <= pq->length) {
    void *nq;
    size_t ncapacity;

    ncapacity = nghttp2_max_size(4, (pq->capacity * 2));

    nq = nghttp2_mem_realloc(pq->mem, pq->q,
                             ncapacity * sizeof(nghttp2_pq_entry *));
    if (nq == NULL) {
      return NGHTTP2_ERR_NOMEM;
    }
    pq->capacity = ncapacity;
    pq->q = nq;
  }
  pq->q[pq->length] = item;
  item->index = pq->length;
  ++pq->length;
  bubble_up(pq, pq->length - 1);
  return 0;
}

nghttp2_pq_entry *nghttp2_pq_top(nghttp2_pq *pq) {
  if (pq->length == 0) {
    return NULL;
  } else {
    return pq->q[0];
  }
}

static void bubble_down(nghttp2_pq *pq, size_t index) {
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

void nghttp2_pq_pop(nghttp2_pq *pq) {
  if (pq->length > 0) {
    pq->q[0] = pq->q[pq->length - 1];
    pq->q[0]->index = 0;
    --pq->length;
    bubble_down(pq, 0);
  }
}

void nghttp2_pq_remove(nghttp2_pq *pq, nghttp2_pq_entry *item) {
  assert(pq->q[item->index] == item);

  if (item->index == 0) {
    nghttp2_pq_pop(pq);
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

int nghttp2_pq_empty(nghttp2_pq *pq) { return pq->length == 0; }

size_t nghttp2_pq_size(nghttp2_pq *pq) { return pq->length; }

void nghttp2_pq_update(nghttp2_pq *pq, nghttp2_pq_item_cb fun, void *arg) {
  size_t i;
  int rv = 0;
  if (pq->length == 0) {
    return;
  }
  for (i = 0; i < pq->length; ++i) {
    rv |= (*fun)(pq->q[i], arg);
  }
  if (rv) {
    for (i = pq->length; i > 0; --i) {
      bubble_down(pq, i - 1);
    }
  }
}

int nghttp2_pq_each(nghttp2_pq *pq, nghttp2_pq_item_cb fun, void *arg) {
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
