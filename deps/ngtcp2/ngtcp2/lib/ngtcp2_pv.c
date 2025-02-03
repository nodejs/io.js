/*
 * ngtcp2
 *
 * Copyright (c) 2019 ngtcp2 contributors
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
#include "ngtcp2_pv.h"

#include <string.h>
#include <assert.h>

#include "ngtcp2_mem.h"
#include "ngtcp2_log.h"
#include "ngtcp2_macro.h"
#include "ngtcp2_addr.h"

void ngtcp2_pv_entry_init(ngtcp2_pv_entry *pvent, const uint8_t *data,
                          ngtcp2_tstamp expiry, uint8_t flags) {
  memcpy(pvent->data, data, sizeof(pvent->data));
  pvent->expiry = expiry;
  pvent->flags = flags;
}

int ngtcp2_pv_new(ngtcp2_pv **ppv, const ngtcp2_dcid *dcid,
                  ngtcp2_duration timeout, uint8_t flags, ngtcp2_log *log,
                  const ngtcp2_mem *mem) {
  (*ppv) = ngtcp2_mem_malloc(mem, sizeof(ngtcp2_pv));
  if (*ppv == NULL) {
    return NGTCP2_ERR_NOMEM;
  }

  ngtcp2_static_ringbuf_pv_ents_init(&(*ppv)->ents);

  ngtcp2_dcid_copy(&(*ppv)->dcid, dcid);

  (*ppv)->mem = mem;
  (*ppv)->log = log;
  (*ppv)->timeout = timeout;
  (*ppv)->fallback_pto = 0;
  (*ppv)->started_ts = UINT64_MAX;
  (*ppv)->probe_pkt_left = NGTCP2_PV_NUM_PROBE_PKT;
  (*ppv)->round = 0;
  (*ppv)->flags = flags;

  return 0;
}

void ngtcp2_pv_del(ngtcp2_pv *pv) {
  if (pv == NULL) {
    return;
  }
  ngtcp2_mem_free(pv->mem, pv);
}

void ngtcp2_pv_add_entry(ngtcp2_pv *pv, const uint8_t *data,
                         ngtcp2_tstamp expiry, uint8_t flags,
                         ngtcp2_tstamp ts) {
  ngtcp2_pv_entry *ent;

  assert(pv->probe_pkt_left);

  if (ngtcp2_ringbuf_len(&pv->ents.rb) == 0) {
    pv->started_ts = ts;
  }

  ent = ngtcp2_ringbuf_push_back(&pv->ents.rb);
  ngtcp2_pv_entry_init(ent, data, expiry, flags);

  pv->flags &= (uint8_t)~NGTCP2_PV_FLAG_CANCEL_TIMER;
  --pv->probe_pkt_left;
}

int ngtcp2_pv_validate(ngtcp2_pv *pv, uint8_t *pflags, const uint8_t *data) {
  size_t len = ngtcp2_ringbuf_len(&pv->ents.rb);
  size_t i;
  ngtcp2_pv_entry *ent;

  if (len == 0) {
    return NGTCP2_ERR_INVALID_STATE;
  }

  for (i = 0; i < len; ++i) {
    ent = ngtcp2_ringbuf_get(&pv->ents.rb, i);
    if (memcmp(ent->data, data, sizeof(ent->data)) == 0) {
      *pflags = ent->flags;
      ngtcp2_log_info(pv->log, NGTCP2_LOG_EVENT_PTV, "path has been validated");
      return 0;
    }
  }

  return NGTCP2_ERR_INVALID_ARGUMENT;
}

void ngtcp2_pv_handle_entry_expiry(ngtcp2_pv *pv, ngtcp2_tstamp ts) {
  ngtcp2_pv_entry *ent;

  if (ngtcp2_ringbuf_len(&pv->ents.rb) == 0) {
    return;
  }

  ent = ngtcp2_ringbuf_get(&pv->ents.rb, ngtcp2_ringbuf_len(&pv->ents.rb) - 1);

  if (ent->expiry > ts) {
    return;
  }

  ++pv->round;
  pv->probe_pkt_left = NGTCP2_PV_NUM_PROBE_PKT;
}

int ngtcp2_pv_should_send_probe(ngtcp2_pv *pv) {
  return pv->probe_pkt_left > 0;
}

int ngtcp2_pv_validation_timed_out(ngtcp2_pv *pv, ngtcp2_tstamp ts) {
  ngtcp2_tstamp t;
  ngtcp2_pv_entry *ent;

  if (pv->started_ts == UINT64_MAX) {
    return 0;
  }

  assert(ngtcp2_ringbuf_len(&pv->ents.rb));

  ent = ngtcp2_ringbuf_get(&pv->ents.rb, ngtcp2_ringbuf_len(&pv->ents.rb) - 1);

  t = pv->started_ts + pv->timeout;
  t = ngtcp2_max_uint64(t, ent->expiry);

  return t <= ts;
}

ngtcp2_tstamp ngtcp2_pv_next_expiry(ngtcp2_pv *pv) {
  ngtcp2_pv_entry *ent;

  if ((pv->flags & NGTCP2_PV_FLAG_CANCEL_TIMER) ||
      ngtcp2_ringbuf_len(&pv->ents.rb) == 0) {
    return UINT64_MAX;
  }

  ent = ngtcp2_ringbuf_get(&pv->ents.rb, ngtcp2_ringbuf_len(&pv->ents.rb) - 1);

  return ent->expiry;
}

void ngtcp2_pv_cancel_expired_timer(ngtcp2_pv *pv, ngtcp2_tstamp ts) {
  ngtcp2_tstamp expiry = ngtcp2_pv_next_expiry(pv);

  if (expiry > ts) {
    return;
  }

  pv->flags |= NGTCP2_PV_FLAG_CANCEL_TIMER;
}
