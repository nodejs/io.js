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
#ifndef NGHTTP3_TNODE_H
#define NGHTTP3_TNODE_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <nghttp3/nghttp3.h>

#include "nghttp3_pq.h"

#define NGHTTP3_TNODE_MAX_CYCLE_GAP (1llu << 24)

typedef struct nghttp3_tnode {
  nghttp3_pq_entry pe;
  size_t num_children;
  int64_t id;
  uint64_t cycle;
  /* pri is a stream priority produced by nghttp3_pri_to_uint8. */
  nghttp3_pri pri;
} nghttp3_tnode;

void nghttp3_tnode_init(nghttp3_tnode *tnode, int64_t id);

void nghttp3_tnode_free(nghttp3_tnode *tnode);

void nghttp3_tnode_unschedule(nghttp3_tnode *tnode, nghttp3_pq *pq);

/*
 * nghttp3_tnode_schedule schedules |tnode| using |nwrite| as penalty.
 * If |tnode| has already been scheduled, it is rescheduled by the
 * amount of |nwrite|.
 */
int nghttp3_tnode_schedule(nghttp3_tnode *tnode, nghttp3_pq *pq,
                           uint64_t nwrite);

/*
 * nghttp3_tnode_is_scheduled returns nonzero if |tnode| is scheduled.
 */
int nghttp3_tnode_is_scheduled(nghttp3_tnode *tnode);

#endif /* NGHTTP3_TNODE_H */
