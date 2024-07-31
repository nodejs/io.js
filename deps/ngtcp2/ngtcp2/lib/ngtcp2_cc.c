/*
 * ngtcp2
 *
 * Copyright (c) 2018 ngtcp2 contributors
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
#include "ngtcp2_cc.h"

#include <assert.h>
#include <string.h>

#include "ngtcp2_log.h"
#include "ngtcp2_macro.h"
#include "ngtcp2_mem.h"
#include "ngtcp2_rcvry.h"
#include "ngtcp2_conn_stat.h"
#include "ngtcp2_unreachable.h"

/* NGTCP2_CC_DELIVERY_RATE_SEC_FILTERLEN is the window length of
   delivery rate filter driven by ACK clocking. */
#define NGTCP2_CC_DELIVERY_RATE_SEC_FILTERLEN 10

uint64_t ngtcp2_cc_compute_initcwnd(size_t max_udp_payload_size) {
  uint64_t n = 2 * max_udp_payload_size;
  n = ngtcp2_max(n, 14720);
  return ngtcp2_min(10 * max_udp_payload_size, n);
}

ngtcp2_cc_pkt *ngtcp2_cc_pkt_init(ngtcp2_cc_pkt *pkt, int64_t pkt_num,
                                  size_t pktlen, ngtcp2_pktns_id pktns_id,
                                  ngtcp2_tstamp sent_ts, uint64_t lost,
                                  uint64_t tx_in_flight, int is_app_limited) {
  pkt->pkt_num = pkt_num;
  pkt->pktlen = pktlen;
  pkt->pktns_id = pktns_id;
  pkt->sent_ts = sent_ts;
  pkt->lost = lost;
  pkt->tx_in_flight = tx_in_flight;
  pkt->is_app_limited = is_app_limited;

  return pkt;
}

static void reno_cc_reset(ngtcp2_cc_reno *reno) {
  ngtcp2_window_filter_init(&reno->delivery_rate_sec_filter,
                            NGTCP2_CC_DELIVERY_RATE_SEC_FILTERLEN);
  reno->ack_count = 0;
  reno->target_cwnd = 0;
  reno->pending_add = 0;
}

void ngtcp2_cc_reno_init(ngtcp2_cc_reno *reno, ngtcp2_log *log) {
  memset(reno, 0, sizeof(*reno));

  reno->cc.log = log;
  reno->cc.on_pkt_acked = ngtcp2_cc_reno_cc_on_pkt_acked;
  reno->cc.congestion_event = ngtcp2_cc_reno_cc_congestion_event;
  reno->cc.on_persistent_congestion =
      ngtcp2_cc_reno_cc_on_persistent_congestion;
  reno->cc.on_ack_recv = ngtcp2_cc_reno_cc_on_ack_recv;
  reno->cc.reset = ngtcp2_cc_reno_cc_reset;

  reno_cc_reset(reno);
}

static int in_congestion_recovery(const ngtcp2_conn_stat *cstat,
                                  ngtcp2_tstamp sent_time) {
  return cstat->congestion_recovery_start_ts != UINT64_MAX &&
         sent_time <= cstat->congestion_recovery_start_ts;
}

void ngtcp2_cc_reno_cc_on_pkt_acked(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                                    const ngtcp2_cc_pkt *pkt,
                                    ngtcp2_tstamp ts) {
  ngtcp2_cc_reno *reno = ngtcp2_struct_of(cc, ngtcp2_cc_reno, cc);
  uint64_t m;
  (void)ts;

  if (in_congestion_recovery(cstat, pkt->sent_ts)) {
    return;
  }

  if (reno->target_cwnd && reno->target_cwnd < cstat->cwnd) {
    return;
  }

  if (cstat->cwnd < cstat->ssthresh) {
    cstat->cwnd += pkt->pktlen;
    ngtcp2_log_info(reno->cc.log, NGTCP2_LOG_EVENT_CCA,
                    "pkn=%" PRId64 " acked, slow start cwnd=%" PRIu64,
                    pkt->pkt_num, cstat->cwnd);
    return;
  }

  m = cstat->max_tx_udp_payload_size * pkt->pktlen + reno->pending_add;
  reno->pending_add = m % cstat->cwnd;

  cstat->cwnd += m / cstat->cwnd;
}

void ngtcp2_cc_reno_cc_congestion_event(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                                        ngtcp2_tstamp sent_ts,
                                        ngtcp2_tstamp ts) {
  ngtcp2_cc_reno *reno = ngtcp2_struct_of(cc, ngtcp2_cc_reno, cc);
  uint64_t min_cwnd;

  if (in_congestion_recovery(cstat, sent_ts)) {
    return;
  }

  cstat->congestion_recovery_start_ts = ts;
  cstat->cwnd >>= NGTCP2_LOSS_REDUCTION_FACTOR_BITS;
  min_cwnd = 2 * cstat->max_tx_udp_payload_size;
  cstat->cwnd = ngtcp2_max(cstat->cwnd, min_cwnd);
  cstat->ssthresh = cstat->cwnd;

  reno->pending_add = 0;

  ngtcp2_log_info(reno->cc.log, NGTCP2_LOG_EVENT_CCA,
                  "reduce cwnd because of packet loss cwnd=%" PRIu64,
                  cstat->cwnd);
}

void ngtcp2_cc_reno_cc_on_persistent_congestion(ngtcp2_cc *cc,
                                                ngtcp2_conn_stat *cstat,
                                                ngtcp2_tstamp ts) {
  (void)cc;
  (void)ts;

  cstat->cwnd = 2 * cstat->max_tx_udp_payload_size;
  cstat->congestion_recovery_start_ts = UINT64_MAX;
}

void ngtcp2_cc_reno_cc_on_ack_recv(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                                   const ngtcp2_cc_ack *ack, ngtcp2_tstamp ts) {
  ngtcp2_cc_reno *reno = ngtcp2_struct_of(cc, ngtcp2_cc_reno, cc);
  uint64_t target_cwnd, initcwnd;
  uint64_t max_delivery_rate_sec;
  (void)ack;
  (void)ts;

  ++reno->ack_count;

  ngtcp2_window_filter_update(&reno->delivery_rate_sec_filter,
                              cstat->delivery_rate_sec, reno->ack_count);

  max_delivery_rate_sec =
      ngtcp2_window_filter_get_best(&reno->delivery_rate_sec_filter);

  if (cstat->min_rtt != UINT64_MAX && max_delivery_rate_sec) {
    target_cwnd = max_delivery_rate_sec * cstat->smoothed_rtt / NGTCP2_SECONDS;
    initcwnd = ngtcp2_cc_compute_initcwnd(cstat->max_tx_udp_payload_size);
    reno->target_cwnd = ngtcp2_max(initcwnd, target_cwnd) * 289 / 100;

    ngtcp2_log_info(reno->cc.log, NGTCP2_LOG_EVENT_CCA,
                    "target_cwnd=%" PRIu64 " max_delivery_rate_sec=%" PRIu64
                    " smoothed_rtt=%" PRIu64,
                    reno->target_cwnd, max_delivery_rate_sec,
                    cstat->smoothed_rtt);
  }
}

void ngtcp2_cc_reno_cc_reset(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                             ngtcp2_tstamp ts) {
  ngtcp2_cc_reno *reno = ngtcp2_struct_of(cc, ngtcp2_cc_reno, cc);
  (void)cstat;
  (void)ts;

  reno_cc_reset(reno);
}

static void cubic_cc_reset(ngtcp2_cc_cubic *cubic) {
  ngtcp2_window_filter_init(&cubic->delivery_rate_sec_filter,
                            NGTCP2_CC_DELIVERY_RATE_SEC_FILTERLEN);
  cubic->ack_count = 0;
  cubic->target_cwnd = 0;
  cubic->w_last_max = 0;
  cubic->w_tcp = 0;
  cubic->origin_point = 0;
  cubic->epoch_start = UINT64_MAX;
  cubic->k = 0;

  cubic->prior.cwnd = 0;
  cubic->prior.ssthresh = 0;
  cubic->prior.w_last_max = 0;
  cubic->prior.w_tcp = 0;
  cubic->prior.origin_point = 0;
  cubic->prior.epoch_start = UINT64_MAX;
  cubic->prior.k = 0;

  cubic->rtt_sample_count = 0;
  cubic->current_round_min_rtt = UINT64_MAX;
  cubic->last_round_min_rtt = UINT64_MAX;
  cubic->window_end = -1;
}

void ngtcp2_cc_cubic_init(ngtcp2_cc_cubic *cubic, ngtcp2_log *log) {
  memset(cubic, 0, sizeof(*cubic));

  cubic->cc.log = log;
  cubic->cc.on_pkt_acked = ngtcp2_cc_cubic_cc_on_pkt_acked;
  cubic->cc.congestion_event = ngtcp2_cc_cubic_cc_congestion_event;
  cubic->cc.on_spurious_congestion = ngtcp2_cc_cubic_cc_on_spurious_congestion;
  cubic->cc.on_persistent_congestion =
      ngtcp2_cc_cubic_cc_on_persistent_congestion;
  cubic->cc.on_ack_recv = ngtcp2_cc_cubic_cc_on_ack_recv;
  cubic->cc.on_pkt_sent = ngtcp2_cc_cubic_cc_on_pkt_sent;
  cubic->cc.new_rtt_sample = ngtcp2_cc_cubic_cc_new_rtt_sample;
  cubic->cc.reset = ngtcp2_cc_cubic_cc_reset;
  cubic->cc.event = ngtcp2_cc_cubic_cc_event;

  cubic_cc_reset(cubic);
}

uint64_t ngtcp2_cbrt(uint64_t n) {
  size_t s;
  uint64_t y = 0;
  uint64_t b;

  for (s = 63; s > 0; s -= 3) {
    y <<= 1;
    b = 3 * y * (y + 1) + 1;
    if ((n >> s) >= b) {
      n -= b << s;
      y++;
    }
  }

  y <<= 1;
  b = 3 * y * (y + 1) + 1;
  if (n >= b) {
    n -= b;
    y++;
  }

  return y;
}

/* HyStart++ constants */
#define NGTCP2_HS_MIN_SSTHRESH 16
#define NGTCP2_HS_N_RTT_SAMPLE 8
#define NGTCP2_HS_MIN_ETA (4 * NGTCP2_MILLISECONDS)
#define NGTCP2_HS_MAX_ETA (16 * NGTCP2_MILLISECONDS)

void ngtcp2_cc_cubic_cc_on_pkt_acked(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                                     const ngtcp2_cc_pkt *pkt,
                                     ngtcp2_tstamp ts) {
  ngtcp2_cc_cubic *cubic = ngtcp2_struct_of(cc, ngtcp2_cc_cubic, cc);
  ngtcp2_duration t, eta;
  uint64_t target, cwnd_thres;
  uint64_t tx, kx, time_delta, delta;
  uint64_t add, tcp_add;
  uint64_t m;

  if (pkt->pktns_id == NGTCP2_PKTNS_ID_APPLICATION && cubic->window_end != -1 &&
      cubic->window_end <= pkt->pkt_num) {
    cubic->window_end = -1;
  }

  if (in_congestion_recovery(cstat, pkt->sent_ts)) {
    return;
  }

  if (cstat->cwnd < cstat->ssthresh) {
    /* slow-start */
    if (cubic->target_cwnd == 0 || cubic->target_cwnd > cstat->cwnd) {
      cstat->cwnd += pkt->pktlen;
    }

    ngtcp2_log_info(cubic->cc.log, NGTCP2_LOG_EVENT_CCA,
                    "pkn=%" PRId64 " acked, slow start cwnd=%" PRIu64,
                    pkt->pkt_num, cstat->cwnd);

    if (cubic->last_round_min_rtt != UINT64_MAX &&
        cubic->current_round_min_rtt != UINT64_MAX &&
        cstat->cwnd >=
            NGTCP2_HS_MIN_SSTHRESH * cstat->max_tx_udp_payload_size &&
        cubic->rtt_sample_count >= NGTCP2_HS_N_RTT_SAMPLE) {
      eta = cubic->last_round_min_rtt / 8;

      if (eta < NGTCP2_HS_MIN_ETA) {
        eta = NGTCP2_HS_MIN_ETA;
      } else if (eta > NGTCP2_HS_MAX_ETA) {
        eta = NGTCP2_HS_MAX_ETA;
      }

      if (cubic->current_round_min_rtt >= cubic->last_round_min_rtt + eta) {
        ngtcp2_log_info(cubic->cc.log, NGTCP2_LOG_EVENT_CCA,
                        "HyStart++ exit slow start");

        cubic->w_last_max = cstat->cwnd;
        cstat->ssthresh = cstat->cwnd;
      }
    }

    return;
  }

  /* congestion avoidance */

  if (cubic->epoch_start == UINT64_MAX) {
    cubic->epoch_start = ts;
    if (cstat->cwnd < cubic->w_last_max) {
      cubic->k = ngtcp2_cbrt((cubic->w_last_max - cstat->cwnd) * 10 / 4 /
                             cstat->max_tx_udp_payload_size);
      cubic->origin_point = cubic->w_last_max;
    } else {
      cubic->k = 0;
      cubic->origin_point = cstat->cwnd;
    }

    cubic->w_tcp = cstat->cwnd;

    ngtcp2_log_info(cubic->cc.log, NGTCP2_LOG_EVENT_CCA,
                    "cubic-ca epoch_start=%" PRIu64 " k=%" PRIu64
                    " origin_point=%" PRIu64,
                    cubic->epoch_start, cubic->k, cubic->origin_point);

    cubic->pending_add = 0;
    cubic->pending_w_add = 0;
  }

  t = ts - cubic->epoch_start;

  tx = (t << 10) / NGTCP2_SECONDS;
  kx = (cubic->k << 10);

  if (tx > kx) {
    time_delta = tx - kx;
  } else {
    time_delta = kx - tx;
  }

  delta = cstat->max_tx_udp_payload_size *
          ((((time_delta * time_delta) >> 10) * time_delta) >> 10) * 4 / 10;
  delta >>= 10;

  if (tx > kx) {
    target = cubic->origin_point + delta;
  } else {
    target = cubic->origin_point - delta;
  }

  cwnd_thres =
      (target * (((t + cstat->smoothed_rtt) << 10) / NGTCP2_SECONDS)) >> 10;
  if (cwnd_thres < cstat->cwnd) {
    target = cstat->cwnd;
  } else if (2 * cwnd_thres > 3 * cstat->cwnd) {
    target = cstat->cwnd * 3 / 2;
  } else {
    target = cwnd_thres;
  }

  if (target > cstat->cwnd) {
    m = cubic->pending_add +
        cstat->max_tx_udp_payload_size * (target - cstat->cwnd);
    add = m / cstat->cwnd;
    cubic->pending_add = m % cstat->cwnd;
  } else {
    m = cubic->pending_add + cstat->max_tx_udp_payload_size;
    add = m / (100 * cstat->cwnd);
    cubic->pending_add = m % (100 * cstat->cwnd);
  }

  m = cubic->pending_w_add + cstat->max_tx_udp_payload_size * pkt->pktlen;

  cubic->w_tcp += m / cstat->cwnd;
  cubic->pending_w_add = m % cstat->cwnd;

  if (cubic->w_tcp > cstat->cwnd) {
    tcp_add = cstat->max_tx_udp_payload_size * (cubic->w_tcp - cstat->cwnd) /
              cstat->cwnd;
    if (tcp_add > add) {
      add = tcp_add;
    }
  }

  if (cubic->target_cwnd == 0 || cubic->target_cwnd > cstat->cwnd) {
    cstat->cwnd += add;
  }

  ngtcp2_log_info(cubic->cc.log, NGTCP2_LOG_EVENT_CCA,
                  "pkn=%" PRId64 " acked, cubic-ca cwnd=%" PRIu64 " t=%" PRIu64
                  " k=%" PRIi64 " time_delta=%" PRIu64 " delta=%" PRIu64
                  " target=%" PRIu64 " w_tcp=%" PRIu64,
                  pkt->pkt_num, cstat->cwnd, t, cubic->k, time_delta >> 4,
                  delta, target, cubic->w_tcp);
}

void ngtcp2_cc_cubic_cc_congestion_event(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                                         ngtcp2_tstamp sent_ts,
                                         ngtcp2_tstamp ts) {
  ngtcp2_cc_cubic *cubic = ngtcp2_struct_of(cc, ngtcp2_cc_cubic, cc);
  uint64_t min_cwnd;

  if (in_congestion_recovery(cstat, sent_ts)) {
    return;
  }

  if (cubic->prior.cwnd < cstat->cwnd) {
    cubic->prior.cwnd = cstat->cwnd;
    cubic->prior.ssthresh = cstat->ssthresh;
    cubic->prior.w_last_max = cubic->w_last_max;
    cubic->prior.w_tcp = cubic->w_tcp;
    cubic->prior.origin_point = cubic->origin_point;
    cubic->prior.epoch_start = cubic->epoch_start;
    cubic->prior.k = cubic->k;
  }

  cstat->congestion_recovery_start_ts = ts;

  cubic->epoch_start = UINT64_MAX;
  if (cstat->cwnd < cubic->w_last_max) {
    cubic->w_last_max = cstat->cwnd * 17 / 10 / 2;
  } else {
    cubic->w_last_max = cstat->cwnd;
  }

  min_cwnd = 2 * cstat->max_tx_udp_payload_size;
  cstat->ssthresh = cstat->cwnd * 7 / 10;
  cstat->ssthresh = ngtcp2_max(cstat->ssthresh, min_cwnd);
  cstat->cwnd = cstat->ssthresh;

  ngtcp2_log_info(cubic->cc.log, NGTCP2_LOG_EVENT_CCA,
                  "reduce cwnd because of packet loss cwnd=%" PRIu64,
                  cstat->cwnd);
}

void ngtcp2_cc_cubic_cc_on_spurious_congestion(ngtcp2_cc *cc,
                                               ngtcp2_conn_stat *cstat,
                                               ngtcp2_tstamp ts) {
  ngtcp2_cc_cubic *cubic = ngtcp2_struct_of(cc, ngtcp2_cc_cubic, cc);
  (void)ts;

  if (cstat->cwnd >= cubic->prior.cwnd) {
    return;
  }

  cstat->congestion_recovery_start_ts = UINT64_MAX;

  cstat->cwnd = cubic->prior.cwnd;
  cstat->ssthresh = cubic->prior.ssthresh;
  cubic->w_last_max = cubic->prior.w_last_max;
  cubic->w_tcp = cubic->prior.w_tcp;
  cubic->origin_point = cubic->prior.origin_point;
  cubic->epoch_start = cubic->prior.epoch_start;
  cubic->k = cubic->prior.k;

  cubic->prior.cwnd = 0;
  cubic->prior.ssthresh = 0;
  cubic->prior.w_last_max = 0;
  cubic->prior.w_tcp = 0;
  cubic->prior.origin_point = 0;
  cubic->prior.epoch_start = UINT64_MAX;
  cubic->prior.k = 0;

  ngtcp2_log_info(cubic->cc.log, NGTCP2_LOG_EVENT_CCA,
                  "spurious congestion is detected and congestion state is "
                  "restored cwnd=%" PRIu64,
                  cstat->cwnd);
}

void ngtcp2_cc_cubic_cc_on_persistent_congestion(ngtcp2_cc *cc,
                                                 ngtcp2_conn_stat *cstat,
                                                 ngtcp2_tstamp ts) {
  (void)cc;
  (void)ts;

  cstat->cwnd = 2 * cstat->max_tx_udp_payload_size;
  cstat->congestion_recovery_start_ts = UINT64_MAX;
}

void ngtcp2_cc_cubic_cc_on_ack_recv(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                                    const ngtcp2_cc_ack *ack,
                                    ngtcp2_tstamp ts) {
  ngtcp2_cc_cubic *cubic = ngtcp2_struct_of(cc, ngtcp2_cc_cubic, cc);
  uint64_t target_cwnd, initcwnd;
  uint64_t max_delivery_rate_sec;
  (void)ack;
  (void)ts;

  ++cubic->ack_count;

  ngtcp2_window_filter_update(&cubic->delivery_rate_sec_filter,
                              cstat->delivery_rate_sec, cubic->ack_count);

  max_delivery_rate_sec =
      ngtcp2_window_filter_get_best(&cubic->delivery_rate_sec_filter);

  if (cstat->min_rtt != UINT64_MAX && max_delivery_rate_sec) {
    target_cwnd = max_delivery_rate_sec * cstat->smoothed_rtt / NGTCP2_SECONDS;
    initcwnd = ngtcp2_cc_compute_initcwnd(cstat->max_tx_udp_payload_size);
    cubic->target_cwnd = ngtcp2_max(initcwnd, target_cwnd) * 289 / 100;

    ngtcp2_log_info(cubic->cc.log, NGTCP2_LOG_EVENT_CCA,
                    "target_cwnd=%" PRIu64 " max_delivery_rate_sec=%" PRIu64
                    " smoothed_rtt=%" PRIu64,
                    cubic->target_cwnd, max_delivery_rate_sec,
                    cstat->smoothed_rtt);
  }
}

void ngtcp2_cc_cubic_cc_on_pkt_sent(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                                    const ngtcp2_cc_pkt *pkt) {
  ngtcp2_cc_cubic *cubic = ngtcp2_struct_of(cc, ngtcp2_cc_cubic, cc);
  (void)cstat;

  if (pkt->pktns_id != NGTCP2_PKTNS_ID_APPLICATION || cubic->window_end != -1) {
    return;
  }

  cubic->window_end = pkt->pkt_num;
  cubic->last_round_min_rtt = cubic->current_round_min_rtt;
  cubic->current_round_min_rtt = UINT64_MAX;
  cubic->rtt_sample_count = 0;
}

void ngtcp2_cc_cubic_cc_new_rtt_sample(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                                       ngtcp2_tstamp ts) {
  ngtcp2_cc_cubic *cubic = ngtcp2_struct_of(cc, ngtcp2_cc_cubic, cc);
  (void)ts;

  if (cubic->window_end == -1) {
    return;
  }

  cubic->current_round_min_rtt =
      ngtcp2_min(cubic->current_round_min_rtt, cstat->latest_rtt);
  ++cubic->rtt_sample_count;
}

void ngtcp2_cc_cubic_cc_reset(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                              ngtcp2_tstamp ts) {
  ngtcp2_cc_cubic *cubic = ngtcp2_struct_of(cc, ngtcp2_cc_cubic, cc);
  (void)cstat;
  (void)ts;

  cubic_cc_reset(cubic);
}

void ngtcp2_cc_cubic_cc_event(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                              ngtcp2_cc_event_type event, ngtcp2_tstamp ts) {
  ngtcp2_cc_cubic *cubic = ngtcp2_struct_of(cc, ngtcp2_cc_cubic, cc);
  ngtcp2_tstamp last_ts;

  if (event != NGTCP2_CC_EVENT_TYPE_TX_START ||
      cubic->epoch_start == UINT64_MAX) {
    return;
  }

  last_ts = cstat->last_tx_pkt_ts[NGTCP2_PKTNS_ID_APPLICATION];
  if (last_ts == UINT64_MAX || last_ts <= cubic->epoch_start) {
    return;
  }

  assert(ts >= last_ts);

  cubic->epoch_start += ts - last_ts;
}
