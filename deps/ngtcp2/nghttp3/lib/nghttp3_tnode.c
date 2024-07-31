/*
 * nghttp3
 *
 * Copyright (c) 2019 nghttp3 contributors
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
#include "nghttp3_tnode.h"

#include <assert.h>

#include "nghttp3_macro.h"
#include "nghttp3_stream.h"
#include "nghttp3_conn.h"
#include "nghttp3_conv.h"

void nghttp3_tnode_init(nghttp3_tnode *tnode, int64_t id) {
  tnode->pe.index = NGHTTP3_PQ_BAD_INDEX;
  tnode->id = id;
  tnode->cycle = 0;
  tnode->pri.urgency = NGHTTP3_DEFAULT_URGENCY;
  tnode->pri.inc = 0;
}

void nghttp3_tnode_free(nghttp3_tnode *tnode) { (void)tnode; }

static void tnode_unschedule(nghttp3_tnode *tnode, nghttp3_pq *pq) {
  assert(tnode->pe.index != NGHTTP3_PQ_BAD_INDEX);

  nghttp3_pq_remove(pq, &tnode->pe);
  tnode->pe.index = NGHTTP3_PQ_BAD_INDEX;
}

void nghttp3_tnode_unschedule(nghttp3_tnode *tnode, nghttp3_pq *pq) {
  if (tnode->pe.index == NGHTTP3_PQ_BAD_INDEX) {
    return;
  }

  tnode_unschedule(tnode, pq);
}

static uint64_t pq_get_first_cycle(nghttp3_pq *pq) {
  nghttp3_tnode *top;

  if (nghttp3_pq_empty(pq)) {
    return 0;
  }

  top = nghttp3_struct_of(nghttp3_pq_top(pq), nghttp3_tnode, pe);
  return top->cycle;
}

int nghttp3_tnode_schedule(nghttp3_tnode *tnode, nghttp3_pq *pq,
                           uint64_t nwrite) {
  uint64_t penalty = nwrite / NGHTTP3_STREAM_MIN_WRITELEN;

  if (tnode->pe.index == NGHTTP3_PQ_BAD_INDEX) {
    tnode->cycle =
        pq_get_first_cycle(pq) +
        ((nwrite == 0 || !tnode->pri.inc) ? 0 : nghttp3_max(1, penalty));
  } else if (nwrite > 0) {
    if (!tnode->pri.inc || nghttp3_pq_size(pq) == 1) {
      return 0;
    }

    nghttp3_pq_remove(pq, &tnode->pe);
    tnode->pe.index = NGHTTP3_PQ_BAD_INDEX;
    tnode->cycle += nghttp3_max(1, penalty);
  } else {
    return 0;
  }

  return nghttp3_pq_push(pq, &tnode->pe);
}

int nghttp3_tnode_is_scheduled(nghttp3_tnode *tnode) {
  return tnode->pe.index != NGHTTP3_PQ_BAD_INDEX;
}
