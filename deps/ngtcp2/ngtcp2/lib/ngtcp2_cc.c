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
#include "ngtcp2_rst.h"
#include "ngtcp2_unreachable.h"

uint64_t ngtcp2_cc_compute_initcwnd(size_t max_udp_payload_size) {
  uint64_t n = 2 * max_udp_payload_size;
  n = ngtcp2_max_uint64(n, 14720);
  return ngtcp2_min_uint64(10 * max_udp_payload_size, n);
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

static void reno_cc_reset(ngtcp2_cc_reno *reno) { reno->pending_add = 0; }

void ngtcp2_cc_reno_init(ngtcp2_cc_reno *reno, ngtcp2_log *log) {
  memset(reno, 0, sizeof(*reno));

  reno->cc.log = log;
  reno->cc.on_pkt_acked = ngtcp2_cc_reno_cc_on_pkt_acked;
  reno->cc.congestion_event = ngtcp2_cc_reno_cc_congestion_event;
  reno->cc.on_persistent_congestion =
    ngtcp2_cc_reno_cc_on_persistent_congestion;
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

  if (in_congestion_recovery(cstat, pkt->sent_ts) || pkt->is_app_limited) {
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
                                        uint64_t bytes_lost, ngtcp2_tstamp ts) {
  ngtcp2_cc_reno *reno = ngtcp2_struct_of(cc, ngtcp2_cc_reno, cc);
  uint64_t min_cwnd;
  (void)bytes_lost;

  if (in_congestion_recovery(cstat, sent_ts)) {
    return;
  }

  cstat->congestion_recovery_start_ts = ts;
  cstat->cwnd >>= NGTCP2_LOSS_REDUCTION_FACTOR_BITS;
  min_cwnd = 2 * cstat->max_tx_udp_payload_size;
  cstat->cwnd = ngtcp2_max_uint64(cstat->cwnd, min_cwnd);
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

void ngtcp2_cc_reno_cc_reset(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                             ngtcp2_tstamp ts) {
  ngtcp2_cc_reno *reno = ngtcp2_struct_of(cc, ngtcp2_cc_reno, cc);
  (void)cstat;
  (void)ts;

  reno_cc_reset(reno);
}

static void cubic_vars_reset(ngtcp2_cubic_vars *v) {
  v->cwnd_prior = 0;
  v->w_max = 0;
  v->k = 0;
  v->epoch_start = UINT64_MAX;
  v->w_est = 0;

  v->state = NGTCP2_CUBIC_STATE_INITIAL;
  v->app_limited_start_ts = UINT64_MAX;
  v->app_limited_duration = 0;
  v->pending_bytes_delivered = 0;
  v->pending_est_bytes_delivered = 0;
}

static void cubic_cc_reset(ngtcp2_cc_cubic *cubic) {
  cubic_vars_reset(&cubic->current);
  cubic_vars_reset(&cubic->undo.v);
  cubic->undo.cwnd = 0;
  cubic->undo.ssthresh = 0;

  cubic->hs.current_round_min_rtt = UINT64_MAX;
  cubic->hs.last_round_min_rtt = UINT64_MAX;
  cubic->hs.curr_rtt = UINT64_MAX;
  cubic->hs.rtt_sample_count = 0;
  cubic->hs.css_baseline_min_rtt = UINT64_MAX;
  cubic->hs.css_round = 0;

  cubic->next_round_delivered = 0;
}

void ngtcp2_cc_cubic_init(ngtcp2_cc_cubic *cubic, ngtcp2_log *log,
                          ngtcp2_rst *rst) {
  memset(cubic, 0, sizeof(*cubic));

  cubic->cc.log = log;
  cubic->cc.on_ack_recv = ngtcp2_cc_cubic_cc_on_ack_recv;
  cubic->cc.congestion_event = ngtcp2_cc_cubic_cc_congestion_event;
  cubic->cc.on_spurious_congestion = ngtcp2_cc_cubic_cc_on_spurious_congestion;
  cubic->cc.on_persistent_congestion =
    ngtcp2_cc_cubic_cc_on_persistent_congestion;
  cubic->cc.reset = ngtcp2_cc_cubic_cc_reset;

  cubic->rst = rst;

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

/* RFC 9406 HyStart++ constants */
#define NGTCP2_HS_MIN_RTT_THRESH (4 * NGTCP2_MILLISECONDS)
#define NGTCP2_HS_MAX_RTT_THRESH (16 * NGTCP2_MILLISECONDS)
#define NGTCP2_HS_MIN_RTT_DIVISOR 8
#define NGTCP2_HS_N_RTT_SAMPLE 8
#define NGTCP2_HS_CSS_GROWTH_DIVISOR 4
#define NGTCP2_HS_CSS_ROUNDS 5

static uint64_t cubic_cc_compute_w_cubic(ngtcp2_cc_cubic *cubic,
                                         const ngtcp2_conn_stat *cstat,
                                         ngtcp2_tstamp ts) {
  ngtcp2_duration t = ts - cubic->current.epoch_start;
  uint64_t delta;
  uint64_t tx = (t << 10) / NGTCP2_SECONDS;
  uint64_t kx = (cubic->current.k << 10) / NGTCP2_SECONDS;
  uint64_t time_delta;

  if (tx < kx) {
    return UINT64_MAX;
  }

  time_delta = tx - kx;

  delta = cstat->max_tx_udp_payload_size *
          ((((time_delta * time_delta) >> 10) * time_delta) >> 10) * 4 / 10;

  return cubic->current.w_max + (delta >> 10);
}

void ngtcp2_cc_cubic_cc_on_ack_recv(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                                    const ngtcp2_cc_ack *ack,
                                    ngtcp2_tstamp ts) {
  ngtcp2_cc_cubic *cubic = ngtcp2_struct_of(cc, ngtcp2_cc_cubic, cc);
  uint64_t w_cubic, w_cubic_next, target, m;
  ngtcp2_duration rtt_thresh;
  int round_start;

  if (in_congestion_recovery(cstat, ack->largest_pkt_sent_ts)) {
    return;
  }

  if (cubic->current.state == NGTCP2_CUBIC_STATE_CONGESTION_AVOIDANCE) {
    if (cubic->rst->rs.is_app_limited && !cubic->rst->is_cwnd_limited) {
      if (cubic->current.app_limited_start_ts == UINT64_MAX) {
        cubic->current.app_limited_start_ts = ts;
      }

      return;
    }

    if (cubic->current.app_limited_start_ts != UINT64_MAX) {
      cubic->current.app_limited_duration +=
        ts - cubic->current.app_limited_start_ts;
      cubic->current.app_limited_start_ts = UINT64_MAX;
    }
  } else if (cubic->rst->rs.is_app_limited && !cubic->rst->is_cwnd_limited) {
    return;
  }

  round_start = ack->pkt_delivered >= cubic->next_round_delivered;
  if (round_start) {
    cubic->next_round_delivered = cubic->rst->delivered;

    cubic->rst->is_cwnd_limited = 0;
  }

  if (cstat->cwnd < cstat->ssthresh) {
    /* slow-start */
    if (cubic->hs.css_round) {
      cstat->cwnd += ack->bytes_delivered / NGTCP2_HS_CSS_GROWTH_DIVISOR;
    } else {
      cstat->cwnd += ack->bytes_delivered;
    }

    ngtcp2_log_info(cubic->cc.log, NGTCP2_LOG_EVENT_CCA,
                    "%" PRIu64 " bytes acked, slow start cwnd=%" PRIu64,
                    ack->bytes_delivered, cstat->cwnd);

    if (round_start) {
      cubic->hs.last_round_min_rtt = cubic->hs.current_round_min_rtt;
      cubic->hs.current_round_min_rtt = UINT64_MAX;
      cubic->hs.rtt_sample_count = 0;

      if (cubic->hs.css_round) {
        ++cubic->hs.css_round;
      }
    }

    cubic->hs.current_round_min_rtt =
      ngtcp2_min_uint64(cubic->hs.current_round_min_rtt, ack->rtt);
    ++cubic->hs.rtt_sample_count;

    if (cubic->hs.css_round) {
      if (cubic->hs.current_round_min_rtt < cubic->hs.css_baseline_min_rtt) {
        cubic->hs.css_baseline_min_rtt = UINT64_MAX;
        cubic->hs.css_round = 0;
        return;
      }

      if (cubic->hs.css_round >= NGTCP2_HS_CSS_ROUNDS) {
        ngtcp2_log_info(cubic->cc.log, NGTCP2_LOG_EVENT_CCA,
                        "HyStart++ exit slow start");

        cstat->ssthresh = cstat->cwnd;
      }

      return;
    }

    if (cubic->hs.rtt_sample_count >= NGTCP2_HS_N_RTT_SAMPLE &&
        cubic->hs.current_round_min_rtt != UINT64_MAX &&
        cubic->hs.last_round_min_rtt != UINT64_MAX) {
      rtt_thresh =
        ngtcp2_max_uint64(NGTCP2_HS_MIN_RTT_THRESH,
                          ngtcp2_min_uint64(cubic->hs.last_round_min_rtt /
                                              NGTCP2_HS_MIN_RTT_DIVISOR,
                                            NGTCP2_HS_MAX_RTT_THRESH));

      if (cubic->hs.current_round_min_rtt >=
          cubic->hs.last_round_min_rtt + rtt_thresh) {
        cubic->hs.css_baseline_min_rtt = cubic->hs.current_round_min_rtt;
        cubic->hs.css_round = 1;
      }
    }

    return;
  }

  /* congestion avoidance */

  switch (cubic->current.state) {
  case NGTCP2_CUBIC_STATE_INITIAL:
    m = cstat->max_tx_udp_payload_size * ack->bytes_delivered +
        cubic->current.pending_bytes_delivered;
    cstat->cwnd += m / cstat->cwnd;
    cubic->current.pending_bytes_delivered = m % cstat->cwnd;
    return;
  case NGTCP2_CUBIC_STATE_RECOVERY:
    cubic->current.state = NGTCP2_CUBIC_STATE_CONGESTION_AVOIDANCE;
    cubic->current.epoch_start = ts;
    break;
  default:
    break;
  }

  w_cubic = cubic_cc_compute_w_cubic(cubic, cstat,
                                     ts - cubic->current.app_limited_duration);
  w_cubic_next = cubic_cc_compute_w_cubic(
    cubic, cstat,
    ts - cubic->current.app_limited_duration + cstat->smoothed_rtt);

  if (w_cubic_next == UINT64_MAX || w_cubic_next < cstat->cwnd) {
    target = cstat->cwnd;
  } else if (2 * w_cubic_next > 3 * cstat->cwnd) {
    target = cstat->cwnd * 3 / 2;
  } else {
    target = w_cubic_next;
  }

  m = ack->bytes_delivered * cstat->max_tx_udp_payload_size +
      cubic->current.pending_est_bytes_delivered;
  cubic->current.pending_est_bytes_delivered = m % cstat->cwnd;

  if (cubic->current.w_est < cubic->current.cwnd_prior) {
    cubic->current.w_est += m * 9 / 17 / cstat->cwnd;
  } else {
    cubic->current.w_est += m / cstat->cwnd;
  }

  if (w_cubic == UINT64_MAX || cubic->current.w_est > w_cubic) {
    cstat->cwnd = cubic->current.w_est;
  } else {
    m = (target - cstat->cwnd) * cstat->max_tx_udp_payload_size +
        cubic->current.pending_bytes_delivered;
    cstat->cwnd += m / cstat->cwnd;
    cubic->current.pending_bytes_delivered = m % cstat->cwnd;
  }

  ngtcp2_log_info(cubic->cc.log, NGTCP2_LOG_EVENT_CCA,
                  "%" PRIu64 " bytes acked, cubic-ca cwnd=%" PRIu64
                  " k=%" PRIi64 " target=%" PRIu64 " w_est=%" PRIu64,
                  ack->bytes_delivered, cstat->cwnd, cubic->current.k, target,
                  cubic->current.w_est);
}

void ngtcp2_cc_cubic_cc_congestion_event(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                                         ngtcp2_tstamp sent_ts,
                                         uint64_t bytes_lost,
                                         ngtcp2_tstamp ts) {
  ngtcp2_cc_cubic *cubic = ngtcp2_struct_of(cc, ngtcp2_cc_cubic, cc);
  uint64_t flight_size;

  if (in_congestion_recovery(cstat, sent_ts)) {
    return;
  }

  if (cubic->undo.cwnd < cstat->cwnd) {
    cubic->undo.v = cubic->current;
    cubic->undo.cwnd = cstat->cwnd;
    cubic->undo.ssthresh = cstat->ssthresh;
  }

  cstat->congestion_recovery_start_ts = ts;

  cubic->current.state = NGTCP2_CUBIC_STATE_RECOVERY;
  cubic->current.epoch_start = UINT64_MAX;
  cubic->current.app_limited_start_ts = UINT64_MAX;
  cubic->current.app_limited_duration = 0;
  cubic->current.pending_bytes_delivered = 0;
  cubic->current.pending_est_bytes_delivered = 0;

  if (cstat->cwnd < cubic->current.w_max) {
    cubic->current.w_max = cstat->cwnd * 17 / 20;
  } else {
    cubic->current.w_max = cstat->cwnd;
  }

  cstat->ssthresh = cstat->cwnd * 7 / 10;

  if (cubic->rst->rs.delivered * 2 < cstat->cwnd) {
    flight_size = cstat->bytes_in_flight + bytes_lost;
    cstat->ssthresh = ngtcp2_min_uint64(
      cstat->ssthresh,
      ngtcp2_max_uint64(cubic->rst->rs.delivered, flight_size) * 7 / 10);
  }

  cstat->ssthresh =
    ngtcp2_max_uint64(cstat->ssthresh, 2 * cstat->max_tx_udp_payload_size);

  cubic->current.cwnd_prior = cstat->cwnd;
  cstat->cwnd = cstat->ssthresh;

  cubic->current.w_est = cstat->cwnd;

  if (cstat->cwnd < cubic->current.w_max) {
    cubic->current.k =
      ngtcp2_cbrt(((cubic->current.w_max - cstat->cwnd) << 10) * 10 / 4 /
                  cstat->max_tx_udp_payload_size) *
      NGTCP2_SECONDS;
    cubic->current.k >>= 10;
  } else {
    cubic->current.k = 0;
  }

  ngtcp2_log_info(cubic->cc.log, NGTCP2_LOG_EVENT_CCA,
                  "reduce cwnd because of packet loss cwnd=%" PRIu64,
                  cstat->cwnd);
}

void ngtcp2_cc_cubic_cc_on_spurious_congestion(ngtcp2_cc *cc,
                                               ngtcp2_conn_stat *cstat,
                                               ngtcp2_tstamp ts) {
  ngtcp2_cc_cubic *cubic = ngtcp2_struct_of(cc, ngtcp2_cc_cubic, cc);
  (void)ts;

  cstat->congestion_recovery_start_ts = UINT64_MAX;

  if (cstat->cwnd < cubic->undo.cwnd) {
    cubic->current = cubic->undo.v;
    cstat->cwnd = cubic->undo.cwnd;
    cstat->ssthresh = cubic->undo.ssthresh;

    ngtcp2_log_info(cubic->cc.log, NGTCP2_LOG_EVENT_CCA,
                    "spurious congestion is detected and congestion state is "
                    "restored cwnd=%" PRIu64,
                    cstat->cwnd);
  }

  cubic_vars_reset(&cubic->undo.v);
  cubic->undo.cwnd = 0;
  cubic->undo.ssthresh = 0;
}

void ngtcp2_cc_cubic_cc_on_persistent_congestion(ngtcp2_cc *cc,
                                                 ngtcp2_conn_stat *cstat,
                                                 ngtcp2_tstamp ts) {
  ngtcp2_cc_cubic *cubic = ngtcp2_struct_of(cc, ngtcp2_cc_cubic, cc);
  (void)ts;

  cubic_cc_reset(cubic);

  cstat->cwnd = 2 * cstat->max_tx_udp_payload_size;
  cstat->congestion_recovery_start_ts = UINT64_MAX;
}

void ngtcp2_cc_cubic_cc_reset(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                              ngtcp2_tstamp ts) {
  ngtcp2_cc_cubic *cubic = ngtcp2_struct_of(cc, ngtcp2_cc_cubic, cc);
  (void)cstat;
  (void)ts;

  cubic_cc_reset(cubic);
}
