/*
 * ngtcp2
 *
 * Copyright (c) 2022 ngtcp2 contributors
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
#include "ngtcp2_pmtud.h"

#include <assert.h>

#include "ngtcp2_mem.h"
#include "ngtcp2_macro.h"

/* NGTCP2_PMTUD_PROBE_NUM_MAX is the maximum number of packets sent
   for each probe. */
#define NGTCP2_PMTUD_PROBE_NUM_MAX 3

static size_t mtu_probes[] = {
    1454 - 48, /* The well known MTU used by a domestic optic fiber
                  service in Japan. */
    1390 - 48, /* Typical Tunneled MTU */
    1280 - 48, /* IPv6 minimum MTU */
    1492 - 48, /* PPPoE */
};

#define NGTCP2_MTU_PROBESLEN ngtcp2_arraylen(mtu_probes)

int ngtcp2_pmtud_new(ngtcp2_pmtud **ppmtud, size_t max_udp_payload_size,
                     size_t hard_max_udp_payload_size, int64_t tx_pkt_num,
                     const ngtcp2_mem *mem) {
  ngtcp2_pmtud *pmtud = ngtcp2_mem_malloc(mem, sizeof(ngtcp2_pmtud));

  if (pmtud == NULL) {
    return NGTCP2_ERR_NOMEM;
  }

  pmtud->mem = mem;
  pmtud->mtu_idx = 0;
  pmtud->num_pkts_sent = 0;
  pmtud->expiry = UINT64_MAX;
  pmtud->tx_pkt_num = tx_pkt_num;
  pmtud->max_udp_payload_size = max_udp_payload_size;
  pmtud->hard_max_udp_payload_size = hard_max_udp_payload_size;
  pmtud->min_fail_udp_payload_size = SIZE_MAX;

  for (; pmtud->mtu_idx < NGTCP2_MTU_PROBESLEN; ++pmtud->mtu_idx) {
    if (mtu_probes[pmtud->mtu_idx] > pmtud->hard_max_udp_payload_size) {
      continue;
    }
    if (mtu_probes[pmtud->mtu_idx] > pmtud->max_udp_payload_size) {
      break;
    }
  }

  *ppmtud = pmtud;

  return 0;
}

void ngtcp2_pmtud_del(ngtcp2_pmtud *pmtud) {
  if (!pmtud) {
    return;
  }

  ngtcp2_mem_free(pmtud->mem, pmtud);
}

size_t ngtcp2_pmtud_probelen(ngtcp2_pmtud *pmtud) {
  assert(pmtud->mtu_idx < NGTCP2_MTU_PROBESLEN);

  return mtu_probes[pmtud->mtu_idx];
}

void ngtcp2_pmtud_probe_sent(ngtcp2_pmtud *pmtud, ngtcp2_duration pto,
                             ngtcp2_tstamp ts) {
  ngtcp2_tstamp timeout;

  if (++pmtud->num_pkts_sent < NGTCP2_PMTUD_PROBE_NUM_MAX) {
    timeout = pto;
  } else {
    timeout = 3 * pto;
  }

  pmtud->expiry = ts + timeout;
}

int ngtcp2_pmtud_require_probe(ngtcp2_pmtud *pmtud) {
  return pmtud->expiry == UINT64_MAX;
}

static void pmtud_next_probe(ngtcp2_pmtud *pmtud) {
  assert(pmtud->mtu_idx < NGTCP2_MTU_PROBESLEN);

  ++pmtud->mtu_idx;
  pmtud->num_pkts_sent = 0;
  pmtud->expiry = UINT64_MAX;

  for (; pmtud->mtu_idx < NGTCP2_MTU_PROBESLEN; ++pmtud->mtu_idx) {
    if (mtu_probes[pmtud->mtu_idx] <= pmtud->max_udp_payload_size ||
        mtu_probes[pmtud->mtu_idx] > pmtud->hard_max_udp_payload_size) {
      continue;
    }

    if (mtu_probes[pmtud->mtu_idx] < pmtud->min_fail_udp_payload_size) {
      break;
    }
  }
}

void ngtcp2_pmtud_probe_success(ngtcp2_pmtud *pmtud, size_t payloadlen) {
  pmtud->max_udp_payload_size =
      ngtcp2_max(pmtud->max_udp_payload_size, payloadlen);

  assert(pmtud->mtu_idx < NGTCP2_MTU_PROBESLEN);

  if (mtu_probes[pmtud->mtu_idx] > pmtud->max_udp_payload_size) {
    return;
  }

  pmtud_next_probe(pmtud);
}

void ngtcp2_pmtud_handle_expiry(ngtcp2_pmtud *pmtud, ngtcp2_tstamp ts) {
  if (ts < pmtud->expiry) {
    return;
  }

  pmtud->expiry = UINT64_MAX;

  if (pmtud->num_pkts_sent < NGTCP2_PMTUD_PROBE_NUM_MAX) {
    return;
  }

  pmtud->min_fail_udp_payload_size =
      ngtcp2_min(pmtud->min_fail_udp_payload_size, mtu_probes[pmtud->mtu_idx]);

  pmtud_next_probe(pmtud);
}

int ngtcp2_pmtud_finished(ngtcp2_pmtud *pmtud) {
  return pmtud->mtu_idx >= NGTCP2_MTU_PROBESLEN;
}
