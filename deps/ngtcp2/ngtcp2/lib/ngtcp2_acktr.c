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
#include "ngtcp2_acktr.h"

#include <assert.h>

#include "ngtcp2_macro.h"
#include "ngtcp2_tstamp.h"

ngtcp2_objalloc_def(acktr_entry, ngtcp2_acktr_entry, oplent);

static void acktr_entry_init(ngtcp2_acktr_entry *ent, int64_t pkt_num,
                             ngtcp2_tstamp tstamp) {
  ent->pkt_num = pkt_num;
  ent->len = 1;
  ent->tstamp = tstamp;
}

int ngtcp2_acktr_entry_objalloc_new(ngtcp2_acktr_entry **ent, int64_t pkt_num,
                                    ngtcp2_tstamp tstamp,
                                    ngtcp2_objalloc *objalloc) {
  *ent = ngtcp2_objalloc_acktr_entry_get(objalloc);
  if (*ent == NULL) {
    return NGTCP2_ERR_NOMEM;
  }

  acktr_entry_init(*ent, pkt_num, tstamp);

  return 0;
}

void ngtcp2_acktr_entry_objalloc_del(ngtcp2_acktr_entry *ent,
                                     ngtcp2_objalloc *objalloc) {
  ngtcp2_objalloc_acktr_entry_release(objalloc, ent);
}

static int greater(const ngtcp2_ksl_key *lhs, const ngtcp2_ksl_key *rhs) {
  return *(int64_t *)lhs > *(int64_t *)rhs;
}

int ngtcp2_acktr_init(ngtcp2_acktr *acktr, ngtcp2_log *log,
                      const ngtcp2_mem *mem) {
  int rv;

  ngtcp2_objalloc_acktr_entry_init(&acktr->objalloc, 32, mem);

  rv = ngtcp2_ringbuf_init(&acktr->acks, 32, sizeof(ngtcp2_acktr_ack_entry),
                           mem);
  if (rv != 0) {
    goto fail_acks_init;
  }

  ngtcp2_ksl_init(&acktr->ents, greater, sizeof(int64_t), mem);

  acktr->log = log;
  acktr->mem = mem;
  acktr->flags = NGTCP2_ACKTR_FLAG_NONE;
  acktr->first_unacked_ts = UINT64_MAX;
  acktr->rx_npkt = 0;

  return 0;

fail_acks_init:
  ngtcp2_objalloc_free(&acktr->objalloc);
  return rv;
}

void ngtcp2_acktr_free(ngtcp2_acktr *acktr) {
#ifdef NOMEMPOOL
  ngtcp2_ksl_it it;
#endif /* NOMEMPOOL */

  if (acktr == NULL) {
    return;
  }

#ifdef NOMEMPOOL
  for (it = ngtcp2_ksl_begin(&acktr->ents); !ngtcp2_ksl_it_end(&it);
       ngtcp2_ksl_it_next(&it)) {
    ngtcp2_acktr_entry_objalloc_del(ngtcp2_ksl_it_get(&it), &acktr->objalloc);
  }
#endif /* NOMEMPOOL */

  ngtcp2_ksl_free(&acktr->ents);

  ngtcp2_ringbuf_free(&acktr->acks);

  ngtcp2_objalloc_free(&acktr->objalloc);
}

int ngtcp2_acktr_add(ngtcp2_acktr *acktr, int64_t pkt_num, int active_ack,
                     ngtcp2_tstamp ts) {
  ngtcp2_ksl_it it, prev_it;
  ngtcp2_acktr_entry *ent, *prev_ent, *delent;
  int rv;
  int added = 0;

  if (ngtcp2_ksl_len(&acktr->ents)) {
    it = ngtcp2_ksl_lower_bound(&acktr->ents, &pkt_num);
    if (ngtcp2_ksl_it_end(&it)) {
      ngtcp2_ksl_it_prev(&it);
      ent = ngtcp2_ksl_it_get(&it);

      assert(ent->pkt_num >= pkt_num + (int64_t)ent->len);

      if (ent->pkt_num == pkt_num + (int64_t)ent->len) {
        ++ent->len;
        added = 1;
      }
    } else {
      ent = ngtcp2_ksl_it_get(&it);

      assert(ent->pkt_num != pkt_num);

      if (ngtcp2_ksl_it_begin(&it)) {
        if (ent->pkt_num + 1 == pkt_num) {
          ngtcp2_ksl_update_key(&acktr->ents, &ent->pkt_num, &pkt_num);
          ent->pkt_num = pkt_num;
          ent->tstamp = ts;
          ++ent->len;
          added = 1;
        }
      } else {
        prev_it = it;
        ngtcp2_ksl_it_prev(&prev_it);
        prev_ent = ngtcp2_ksl_it_get(&prev_it);

        assert(prev_ent->pkt_num >= pkt_num + (int64_t)prev_ent->len);

        if (ent->pkt_num + 1 == pkt_num) {
          if (prev_ent->pkt_num == pkt_num + (int64_t)prev_ent->len) {
            prev_ent->len += ent->len + 1;
            ngtcp2_ksl_remove_hint(&acktr->ents, NULL, &it, &ent->pkt_num);
            ngtcp2_acktr_entry_objalloc_del(ent, &acktr->objalloc);
            added = 1;
          } else {
            ngtcp2_ksl_update_key(&acktr->ents, &ent->pkt_num, &pkt_num);
            ent->pkt_num = pkt_num;
            ent->tstamp = ts;
            ++ent->len;
            added = 1;
          }
        } else if (prev_ent->pkt_num == pkt_num + (int64_t)prev_ent->len) {
          ++prev_ent->len;
          added = 1;
        }
      }
    }
  }

  if (!added) {
    rv = ngtcp2_acktr_entry_objalloc_new(&ent, pkt_num, ts, &acktr->objalloc);
    if (rv != 0) {
      return rv;
    }
    rv = ngtcp2_ksl_insert(&acktr->ents, NULL, &ent->pkt_num, ent);
    if (rv != 0) {
      ngtcp2_acktr_entry_objalloc_del(ent, &acktr->objalloc);
      return rv;
    }
  }

  if (active_ack) {
    acktr->flags |= NGTCP2_ACKTR_FLAG_ACTIVE_ACK;
    if (acktr->first_unacked_ts == UINT64_MAX) {
      acktr->first_unacked_ts = ts;
    }
  }

  if (ngtcp2_ksl_len(&acktr->ents) > NGTCP2_ACKTR_MAX_ENT) {
    it = ngtcp2_ksl_end(&acktr->ents);
    ngtcp2_ksl_it_prev(&it);
    delent = ngtcp2_ksl_it_get(&it);
    ngtcp2_ksl_remove_hint(&acktr->ents, NULL, &it, &delent->pkt_num);
    ngtcp2_acktr_entry_objalloc_del(delent, &acktr->objalloc);
  }

  return 0;
}

void ngtcp2_acktr_forget(ngtcp2_acktr *acktr, ngtcp2_acktr_entry *ent) {
  ngtcp2_ksl_it it;

  it = ngtcp2_ksl_lower_bound(&acktr->ents, &ent->pkt_num);
  assert(*(int64_t *)ngtcp2_ksl_it_key(&it) == (int64_t)ent->pkt_num);

  for (; !ngtcp2_ksl_it_end(&it);) {
    ent = ngtcp2_ksl_it_get(&it);
    ngtcp2_ksl_remove_hint(&acktr->ents, &it, &it, &ent->pkt_num);
    ngtcp2_acktr_entry_objalloc_del(ent, &acktr->objalloc);
  }
}

ngtcp2_ksl_it ngtcp2_acktr_get(ngtcp2_acktr *acktr) {
  return ngtcp2_ksl_begin(&acktr->ents);
}

int ngtcp2_acktr_empty(ngtcp2_acktr *acktr) {
  ngtcp2_ksl_it it = ngtcp2_ksl_begin(&acktr->ents);
  return ngtcp2_ksl_it_end(&it);
}

ngtcp2_acktr_ack_entry *ngtcp2_acktr_add_ack(ngtcp2_acktr *acktr,
                                             int64_t pkt_num,
                                             int64_t largest_ack) {
  ngtcp2_acktr_ack_entry *ent = ngtcp2_ringbuf_push_front(&acktr->acks);

  ent->largest_ack = largest_ack;
  ent->pkt_num = pkt_num;

  return ent;
}

/*
 * acktr_remove removes |ent| from |acktr|.  |it| must point to the
 * node whose key identifies |ent|.  The iterator which points to the
 * entry next to |ent| is assigned to |it|.
 */
static void acktr_remove(ngtcp2_acktr *acktr, ngtcp2_ksl_it *it,
                         ngtcp2_acktr_entry *ent) {
  ngtcp2_ksl_remove_hint(&acktr->ents, it, it, &ent->pkt_num);
  ngtcp2_acktr_entry_objalloc_del(ent, &acktr->objalloc);
}

static void acktr_on_ack(ngtcp2_acktr *acktr, ngtcp2_ringbuf *rb,
                         size_t ack_ent_offset) {
  ngtcp2_acktr_ack_entry *ack_ent;
  ngtcp2_acktr_entry *ent;
  ngtcp2_ksl_it it;

  assert(ngtcp2_ringbuf_len(rb));

  ack_ent = ngtcp2_ringbuf_get(rb, ack_ent_offset);

  /* Assume that ngtcp2_pkt_validate_ack(fr) returns 0 */
  it = ngtcp2_ksl_lower_bound(&acktr->ents, &ack_ent->largest_ack);
  for (; !ngtcp2_ksl_it_end(&it);) {
    ent = ngtcp2_ksl_it_get(&it);
    acktr_remove(acktr, &it, ent);
  }

  if (ngtcp2_ksl_len(&acktr->ents)) {
    assert(ngtcp2_ksl_it_end(&it));

    ngtcp2_ksl_it_prev(&it);
    ent = ngtcp2_ksl_it_get(&it);
    if (ent->pkt_num > ack_ent->largest_ack &&
        ack_ent->largest_ack >= ent->pkt_num - (int64_t)(ent->len - 1)) {
      ent->len = (size_t)(ent->pkt_num - ack_ent->largest_ack);
    }
  }

  ngtcp2_ringbuf_resize(rb, ack_ent_offset);
}

void ngtcp2_acktr_recv_ack(ngtcp2_acktr *acktr, const ngtcp2_ack *fr) {
  ngtcp2_acktr_ack_entry *ent;
  int64_t largest_ack = fr->largest_ack, min_ack;
  size_t i, j;
  ngtcp2_ringbuf *rb = &acktr->acks;
  size_t nacks = ngtcp2_ringbuf_len(rb);

  /* Assume that ngtcp2_pkt_validate_ack(fr) returns 0 */
  for (j = 0; j < nacks; ++j) {
    ent = ngtcp2_ringbuf_get(rb, j);
    if (largest_ack >= ent->pkt_num) {
      break;
    }
  }
  if (j == nacks) {
    return;
  }

  min_ack = largest_ack - (int64_t)fr->first_ack_range;

  if (min_ack <= ent->pkt_num && ent->pkt_num <= largest_ack) {
    acktr_on_ack(acktr, rb, j);
    return;
  }

  for (i = 0; i < fr->rangecnt && j < nacks; ++i) {
    largest_ack = min_ack - (int64_t)fr->ranges[i].gap - 2;
    min_ack = largest_ack - (int64_t)fr->ranges[i].len;

    for (;;) {
      if (ent->pkt_num > largest_ack) {
        ++j;
        if (j == nacks) {
          return;
        }
        ent = ngtcp2_ringbuf_get(rb, j);
        continue;
      }
      if (ent->pkt_num < min_ack) {
        break;
      }
      acktr_on_ack(acktr, rb, j);
      return;
    }
  }
}

void ngtcp2_acktr_commit_ack(ngtcp2_acktr *acktr) {
  acktr->flags &= (uint16_t) ~(NGTCP2_ACKTR_FLAG_ACTIVE_ACK |
                               NGTCP2_ACKTR_FLAG_IMMEDIATE_ACK |
                               NGTCP2_ACKTR_FLAG_CANCEL_TIMER);
  acktr->first_unacked_ts = UINT64_MAX;
  acktr->rx_npkt = 0;
}

int ngtcp2_acktr_require_active_ack(ngtcp2_acktr *acktr,
                                    ngtcp2_duration max_ack_delay,
                                    ngtcp2_tstamp ts) {
  return ngtcp2_tstamp_elapsed(acktr->first_unacked_ts, max_ack_delay, ts);
}

void ngtcp2_acktr_immediate_ack(ngtcp2_acktr *acktr) {
  acktr->flags |= NGTCP2_ACKTR_FLAG_IMMEDIATE_ACK;
}
