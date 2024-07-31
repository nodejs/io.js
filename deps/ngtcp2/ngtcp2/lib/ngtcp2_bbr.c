/*
 * ngtcp2
 *
 * Copyright (c) 2021 ngtcp2 contributors
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
#include "ngtcp2_bbr.h"

#include <assert.h>
#include <string.h>

#include "ngtcp2_log.h"
#include "ngtcp2_macro.h"
#include "ngtcp2_mem.h"
#include "ngtcp2_rcvry.h"
#include "ngtcp2_rst.h"
#include "ngtcp2_conn_stat.h"

#define NGTCP2_BBR_MAX_BW_FILTERLEN 2

#define NGTCP2_BBR_EXTRA_ACKED_FILTERLEN 10

#define NGTCP2_BBR_STARTUP_PACING_GAIN_H 277

#define NGTCP2_BBR_STARTUP_CWND_GAIN_H 200

#define NGTCP2_BBR_PROBE_RTT_CWND_GAIN_H 50

#define NGTCP2_BBR_BETA_NUMER 7
#define NGTCP2_BBR_BETA_DENOM 10

#define NGTCP2_BBR_LOSS_THRESH_NUMER 2
#define NGTCP2_BBR_LOSS_THRESH_DENOM 100

#define NGTCP2_BBR_HEADROOM_NUMER 15
#define NGTCP2_BBR_HEADROOM_DENOM 100

#define NGTCP2_BBR_PROBE_RTT_INTERVAL (5 * NGTCP2_SECONDS)
#define NGTCP2_BBR_MIN_RTT_FILTERLEN (10 * NGTCP2_SECONDS)

#define NGTCP2_BBR_PROBE_RTT_DURATION (200 * NGTCP2_MILLISECONDS)

#define NGTCP2_BBR_PACING_MARGIN_PERCENT 1

static void bbr_on_init(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat,
                        ngtcp2_tstamp initial_ts);

static void bbr_on_transmit(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat,
                            ngtcp2_tstamp ts);

static void bbr_reset_congestion_signals(ngtcp2_cc_bbr *bbr);

static void bbr_reset_lower_bounds(ngtcp2_cc_bbr *bbr);

static void bbr_init_round_counting(ngtcp2_cc_bbr *bbr);

static void bbr_init_full_pipe(ngtcp2_cc_bbr *bbr);

static void bbr_init_pacing_rate(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat);

static void bbr_set_pacing_rate_with_gain(ngtcp2_cc_bbr *bbr,
                                          ngtcp2_conn_stat *cstat,
                                          uint64_t pacing_gain_h);

static void bbr_set_pacing_rate(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat);

static void bbr_enter_startup(ngtcp2_cc_bbr *bbr);

static void bbr_check_startup_done(ngtcp2_cc_bbr *bbr,
                                   const ngtcp2_cc_ack *ack);

static void bbr_update_on_ack(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat,
                              const ngtcp2_cc_ack *ack, ngtcp2_tstamp ts);

static void bbr_update_model_and_state(ngtcp2_cc_bbr *cc,
                                       ngtcp2_conn_stat *cstat,
                                       const ngtcp2_cc_ack *ack,
                                       ngtcp2_tstamp ts);

static void bbr_update_control_parameters(ngtcp2_cc_bbr *cc,
                                          ngtcp2_conn_stat *cstat,
                                          const ngtcp2_cc_ack *ack);

static void bbr_update_on_loss(ngtcp2_cc_bbr *cc, ngtcp2_conn_stat *cstat,
                               const ngtcp2_cc_pkt *pkt, ngtcp2_tstamp ts);

static void bbr_update_latest_delivery_signals(ngtcp2_cc_bbr *bbr,
                                               ngtcp2_conn_stat *cstat);

static void bbr_advance_latest_delivery_signals(ngtcp2_cc_bbr *bbr,
                                                ngtcp2_conn_stat *cstat);

static void bbr_update_congestion_signals(ngtcp2_cc_bbr *bbr,
                                          ngtcp2_conn_stat *cstat,
                                          const ngtcp2_cc_ack *ack);

static void bbr_adapt_lower_bounds_from_congestion(ngtcp2_cc_bbr *bbr,
                                                   ngtcp2_conn_stat *cstat);

static void bbr_init_lower_bounds(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat);

static void bbr_loss_lower_bounds(ngtcp2_cc_bbr *bbr);

static void bbr_bound_bw_for_model(ngtcp2_cc_bbr *bbr);

static void bbr_update_max_bw(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat,
                              const ngtcp2_cc_ack *ack);

static void bbr_update_round(ngtcp2_cc_bbr *bbr, const ngtcp2_cc_ack *ack);

static void bbr_start_round(ngtcp2_cc_bbr *bbr);

static int bbr_is_in_probe_bw_state(ngtcp2_cc_bbr *bbr);

static void bbr_update_ack_aggregation(ngtcp2_cc_bbr *bbr,
                                       ngtcp2_conn_stat *cstat,
                                       const ngtcp2_cc_ack *ack,
                                       ngtcp2_tstamp ts);

static void bbr_enter_drain(ngtcp2_cc_bbr *bbr);

static void bbr_check_drain(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat,
                            ngtcp2_tstamp ts);

static void bbr_enter_probe_bw(ngtcp2_cc_bbr *bbr, ngtcp2_tstamp ts);

static void bbr_start_probe_bw_down(ngtcp2_cc_bbr *bbr, ngtcp2_tstamp ts);

static void bbr_start_probe_bw_cruise(ngtcp2_cc_bbr *bbr);

static void bbr_start_probe_bw_refill(ngtcp2_cc_bbr *bbr);

static void bbr_start_probe_bw_up(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat,
                                  ngtcp2_tstamp ts);

static void bbr_update_probe_bw_cycle_phase(ngtcp2_cc_bbr *bbr,
                                            ngtcp2_conn_stat *cstat,
                                            const ngtcp2_cc_ack *ack,
                                            ngtcp2_tstamp ts);

static int bbr_check_time_to_cruise(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat,
                                    ngtcp2_tstamp ts);

static int bbr_has_elapsed_in_phase(ngtcp2_cc_bbr *bbr,
                                    ngtcp2_duration interval, ngtcp2_tstamp ts);

static uint64_t bbr_inflight_with_headroom(ngtcp2_cc_bbr *bbr,
                                           ngtcp2_conn_stat *cstat);

static void bbr_raise_inflight_hi_slope(ngtcp2_cc_bbr *bbr,
                                        ngtcp2_conn_stat *cstat);

static void bbr_probe_inflight_hi_upward(ngtcp2_cc_bbr *bbr,
                                         ngtcp2_conn_stat *cstat,
                                         const ngtcp2_cc_ack *ack);

static void bbr_adapt_upper_bounds(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat,
                                   const ngtcp2_cc_ack *ack, ngtcp2_tstamp ts);

static int bbr_check_time_to_probe_bw(ngtcp2_cc_bbr *bbr,
                                      ngtcp2_conn_stat *cstat,
                                      ngtcp2_tstamp ts);

static void bbr_pick_probe_wait(ngtcp2_cc_bbr *bbr);

static int bbr_is_reno_coexistence_probe_time(ngtcp2_cc_bbr *bbr,
                                              ngtcp2_conn_stat *cstat);

static uint64_t bbr_target_inflight(ngtcp2_cc_bbr *bbr,
                                    ngtcp2_conn_stat *cstat);

static int bbr_check_inflight_too_high(ngtcp2_cc_bbr *bbr,
                                       ngtcp2_conn_stat *cstat,
                                       ngtcp2_tstamp ts);

static int is_inflight_too_high(const ngtcp2_rs *rs);

static void bbr_handle_inflight_too_high(ngtcp2_cc_bbr *bbr,
                                         ngtcp2_conn_stat *cstat,
                                         const ngtcp2_rs *rs, ngtcp2_tstamp ts);

static void bbr_handle_lost_packet(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat,
                                   const ngtcp2_cc_pkt *pkt, ngtcp2_tstamp ts);

static uint64_t bbr_inflight_hi_from_lost_packet(ngtcp2_cc_bbr *bbr,
                                                 const ngtcp2_rs *rs,
                                                 const ngtcp2_cc_pkt *pkt);

static void bbr_update_min_rtt(ngtcp2_cc_bbr *bbr, const ngtcp2_cc_ack *ack,
                               ngtcp2_tstamp ts);

static void bbr_check_probe_rtt(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat,
                                ngtcp2_tstamp ts);

static void bbr_enter_probe_rtt(ngtcp2_cc_bbr *bbr);

static void bbr_handle_probe_rtt(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat,
                                 ngtcp2_tstamp ts);

static void bbr_check_probe_rtt_done(ngtcp2_cc_bbr *bbr,
                                     ngtcp2_conn_stat *cstat, ngtcp2_tstamp ts);

static void bbr_mark_connection_app_limited(ngtcp2_cc_bbr *bbr,
                                            ngtcp2_conn_stat *cstat);

static void bbr_exit_probe_rtt(ngtcp2_cc_bbr *bbr, ngtcp2_tstamp ts);

static void bbr_handle_restart_from_idle(ngtcp2_cc_bbr *bbr,
                                         ngtcp2_conn_stat *cstat,
                                         ngtcp2_tstamp ts);

static uint64_t bbr_bdp_multiple(ngtcp2_cc_bbr *bbr, uint64_t bw,
                                 uint64_t gain_h);

static uint64_t bbr_quantization_budget(ngtcp2_cc_bbr *bbr,
                                        ngtcp2_conn_stat *cstat,
                                        uint64_t inflight);

static uint64_t bbr_inflight(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat,
                             uint64_t bw, uint64_t gain_h);

static void bbr_update_max_inflight(ngtcp2_cc_bbr *bbr,
                                    ngtcp2_conn_stat *cstat);

static void bbr_update_offload_budget(ngtcp2_cc_bbr *bbr,
                                      ngtcp2_conn_stat *cstat);

static uint64_t min_pipe_cwnd(size_t max_udp_payload_size);

static void bbr_advance_max_bw_filter(ngtcp2_cc_bbr *bbr);

static void bbr_modulate_cwnd_for_recovery(ngtcp2_cc_bbr *bbr,
                                           ngtcp2_conn_stat *cstat,
                                           const ngtcp2_cc_ack *ack);

static void bbr_save_cwnd(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat);

static void bbr_restore_cwnd(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat);

static uint64_t bbr_probe_rtt_cwnd(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat);

static void bbr_bound_cwnd_for_probe_rtt(ngtcp2_cc_bbr *bbr,
                                         ngtcp2_conn_stat *cstat);

static void bbr_set_cwnd(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat,
                         const ngtcp2_cc_ack *ack);

static void bbr_bound_cwnd_for_model(ngtcp2_cc_bbr *bbr,
                                     ngtcp2_conn_stat *cstat);

static void bbr_set_send_quantum(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat);

static int in_congestion_recovery(const ngtcp2_conn_stat *cstat,
                                  ngtcp2_tstamp sent_time);

static void bbr_handle_recovery(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat,
                                const ngtcp2_cc_ack *ack, ngtcp2_tstamp ts);

static void bbr_on_init(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat,
                        ngtcp2_tstamp initial_ts) {
  ngtcp2_window_filter_init(&bbr->max_bw_filter, NGTCP2_BBR_MAX_BW_FILTERLEN);
  ngtcp2_window_filter_init(&bbr->extra_acked_filter,
                            NGTCP2_BBR_EXTRA_ACKED_FILTERLEN);

  bbr->min_rtt = UINT64_MAX;
  bbr->min_rtt_stamp = initial_ts;
  /* remark: Use UINT64_MAX instead of 0 for consistency. */
  bbr->probe_rtt_done_stamp = UINT64_MAX;
  bbr->probe_rtt_round_done = 0;
  bbr->prior_cwnd = 0;
  bbr->idle_restart = 0;
  bbr->extra_acked_interval_start = initial_ts;
  bbr->extra_acked_delivered = 0;

  bbr_reset_congestion_signals(bbr);
  bbr_reset_lower_bounds(bbr);
  bbr_init_round_counting(bbr);
  bbr_init_full_pipe(bbr);
  bbr_init_pacing_rate(bbr, cstat);
  bbr_enter_startup(bbr);

  cstat->send_quantum = cstat->max_tx_udp_payload_size * 10;

  /* Missing in documentation */
  bbr->loss_round_start = 0;
  bbr->loss_round_delivered = UINT64_MAX;

  bbr->rounds_since_bw_probe = 0;

  bbr->max_bw = 0;
  bbr->bw = 0;

  bbr->cycle_count = 0;

  bbr->extra_acked = 0;

  bbr->bytes_lost_in_round = 0;
  bbr->loss_events_in_round = 0;

  bbr->offload_budget = 0;

  bbr->probe_up_cnt = UINT64_MAX;
  bbr->cycle_stamp = UINT64_MAX;
  bbr->ack_phase = 0;
  bbr->bw_probe_wait = 0;
  bbr->bw_probe_samples = 0;
  bbr->bw_probe_up_rounds = 0;
  bbr->bw_probe_up_acks = 0;

  bbr->inflight_hi = UINT64_MAX;
  bbr->bw_hi = UINT64_MAX;

  bbr->probe_rtt_expired = 0;
  bbr->probe_rtt_min_delay = UINT64_MAX;
  bbr->probe_rtt_min_stamp = initial_ts;

  bbr->in_loss_recovery = 0;
  bbr->packet_conservation = 0;

  bbr->max_inflight = 0;

  bbr->congestion_recovery_start_ts = UINT64_MAX;
  bbr->congestion_recovery_next_round_delivered = 0;

  bbr->prior_inflight_lo = 0;
  bbr->prior_inflight_hi = 0;
  bbr->prior_bw_lo = 0;
}

static void bbr_reset_congestion_signals(ngtcp2_cc_bbr *bbr) {
  bbr->loss_in_round = 0;
  bbr->bw_latest = 0;
  bbr->inflight_latest = 0;
}

static void bbr_reset_lower_bounds(ngtcp2_cc_bbr *bbr) {
  bbr->bw_lo = UINT64_MAX;
  bbr->inflight_lo = UINT64_MAX;
}

static void bbr_init_round_counting(ngtcp2_cc_bbr *bbr) {
  bbr->next_round_delivered = 0;
  bbr->round_start = 0;
  bbr->round_count = 0;
}

static void bbr_init_full_pipe(ngtcp2_cc_bbr *bbr) {
  bbr->filled_pipe = 0;
  bbr->full_bw = 0;
  bbr->full_bw_count = 0;
}

static void bbr_check_startup_full_bandwidth(ngtcp2_cc_bbr *bbr) {
  if (bbr->filled_pipe || !bbr->round_start || bbr->rst->rs.is_app_limited) {
    return;
  }

  if (bbr->max_bw * 100 >= bbr->full_bw * 125) {
    bbr->full_bw = bbr->max_bw;
    bbr->full_bw_count = 0;
  }

  ++bbr->full_bw_count;

  if (bbr->full_bw_count >= 3) {
    bbr->filled_pipe = 1;

    ngtcp2_log_info(bbr->cc.log, NGTCP2_LOG_EVENT_CCA,
                    "bbr filled pipe, full_bw=%" PRIu64, bbr->full_bw);
  }
}

static void bbr_check_startup_high_loss(ngtcp2_cc_bbr *bbr,
                                        const ngtcp2_cc_ack *ack) {
  if (bbr->filled_pipe || !bbr->round_start || bbr->rst->rs.is_app_limited) {
    return;
  }

  if (bbr->loss_events_in_round <= 3) {
    return;
  }

  /* loss_thresh = 2% */
  if (bbr->bytes_lost_in_round * 100 <= ack->prior_bytes_in_flight * 2) {
    return;
  }

  bbr->filled_pipe = 1;
}

static void bbr_init_pacing_rate(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat) {
  cstat->pacing_interval = NGTCP2_MILLISECONDS * 100 /
                           NGTCP2_BBR_STARTUP_PACING_GAIN_H / bbr->initial_cwnd;
}

static void bbr_set_pacing_rate_with_gain(ngtcp2_cc_bbr *bbr,
                                          ngtcp2_conn_stat *cstat,
                                          uint64_t pacing_gain_h) {
  ngtcp2_duration interval;

  if (bbr->bw == 0) {
    return;
  }

  interval = NGTCP2_SECONDS * 100 * 100 / pacing_gain_h / bbr->bw /
             (100 - NGTCP2_BBR_PACING_MARGIN_PERCENT);

  if (bbr->filled_pipe || interval < cstat->pacing_interval) {
    cstat->pacing_interval = interval;
  }
}

static void bbr_set_pacing_rate(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat) {
  bbr_set_pacing_rate_with_gain(bbr, cstat, bbr->pacing_gain_h);
}

static void bbr_enter_startup(ngtcp2_cc_bbr *bbr) {
  ngtcp2_log_info(bbr->cc.log, NGTCP2_LOG_EVENT_CCA, "bbr enter Startup");

  bbr->state = NGTCP2_BBR_STATE_STARTUP;
  bbr->pacing_gain_h = NGTCP2_BBR_STARTUP_PACING_GAIN_H;
  bbr->cwnd_gain_h = NGTCP2_BBR_STARTUP_CWND_GAIN_H;
}

static void bbr_check_startup_done(ngtcp2_cc_bbr *bbr,
                                   const ngtcp2_cc_ack *ack) {
  bbr_check_startup_full_bandwidth(bbr);
  bbr_check_startup_high_loss(bbr, ack);

  if (bbr->state == NGTCP2_BBR_STATE_STARTUP && bbr->filled_pipe) {
    bbr_enter_drain(bbr);
  }
}

static void bbr_on_transmit(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat,
                            ngtcp2_tstamp ts) {
  bbr_handle_restart_from_idle(bbr, cstat, ts);
}

static void bbr_update_on_ack(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat,
                              const ngtcp2_cc_ack *ack, ngtcp2_tstamp ts) {
  bbr_update_model_and_state(bbr, cstat, ack, ts);
  bbr_update_control_parameters(bbr, cstat, ack);
}

static void bbr_update_model_and_state(ngtcp2_cc_bbr *bbr,
                                       ngtcp2_conn_stat *cstat,
                                       const ngtcp2_cc_ack *ack,
                                       ngtcp2_tstamp ts) {
  bbr_update_latest_delivery_signals(bbr, cstat);
  bbr_update_congestion_signals(bbr, cstat, ack);
  bbr_update_ack_aggregation(bbr, cstat, ack, ts);
  bbr_check_startup_done(bbr, ack);
  bbr_check_drain(bbr, cstat, ts);
  bbr_update_probe_bw_cycle_phase(bbr, cstat, ack, ts);
  bbr_update_min_rtt(bbr, ack, ts);
  bbr_check_probe_rtt(bbr, cstat, ts);
  bbr_advance_latest_delivery_signals(bbr, cstat);
  bbr_bound_bw_for_model(bbr);
}

static void bbr_update_control_parameters(ngtcp2_cc_bbr *bbr,
                                          ngtcp2_conn_stat *cstat,
                                          const ngtcp2_cc_ack *ack) {
  bbr_set_pacing_rate(bbr, cstat);
  bbr_set_send_quantum(bbr, cstat);
  bbr_set_cwnd(bbr, cstat, ack);
}

static void bbr_update_on_loss(ngtcp2_cc_bbr *cc, ngtcp2_conn_stat *cstat,
                               const ngtcp2_cc_pkt *pkt, ngtcp2_tstamp ts) {
  bbr_handle_lost_packet(cc, cstat, pkt, ts);
}

static void bbr_update_latest_delivery_signals(ngtcp2_cc_bbr *bbr,
                                               ngtcp2_conn_stat *cstat) {
  bbr->loss_round_start = 0;
  bbr->bw_latest = ngtcp2_max(bbr->bw_latest, cstat->delivery_rate_sec);
  bbr->inflight_latest =
      ngtcp2_max(bbr->inflight_latest, bbr->rst->rs.delivered);

  if (bbr->rst->rs.prior_delivered >= bbr->loss_round_delivered) {
    bbr->loss_round_delivered = bbr->rst->delivered;
    bbr->loss_round_start = 1;
  }
}

static void bbr_advance_latest_delivery_signals(ngtcp2_cc_bbr *bbr,
                                                ngtcp2_conn_stat *cstat) {
  if (bbr->loss_round_start) {
    bbr->bw_latest = cstat->delivery_rate_sec;
    bbr->inflight_latest = bbr->rst->rs.delivered;
  }
}

static void bbr_update_congestion_signals(ngtcp2_cc_bbr *bbr,
                                          ngtcp2_conn_stat *cstat,
                                          const ngtcp2_cc_ack *ack) {
  bbr_update_max_bw(bbr, cstat, ack);

  if (ack->bytes_lost) {
    bbr->bytes_lost_in_round += ack->bytes_lost;
    ++bbr->loss_events_in_round;

    if (!bbr->loss_in_round) {
      bbr->loss_in_round = 1;
      bbr->loss_round_delivered = bbr->rst->delivered;
    }
  }

  if (!bbr->loss_round_start) {
    return;
  }

  bbr_adapt_lower_bounds_from_congestion(bbr, cstat);

  bbr->loss_in_round = 0;
}

static void bbr_adapt_lower_bounds_from_congestion(ngtcp2_cc_bbr *bbr,
                                                   ngtcp2_conn_stat *cstat) {
  if (bbr_is_in_probe_bw_state(bbr)) {
    return;
  }

  if (bbr->loss_in_round) {
    bbr_init_lower_bounds(bbr, cstat);
    bbr_loss_lower_bounds(bbr);
  }
}

static void bbr_init_lower_bounds(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat) {
  if (bbr->bw_lo == UINT64_MAX) {
    bbr->bw_lo = bbr->max_bw;
  }

  if (bbr->inflight_lo == UINT64_MAX) {
    bbr->inflight_lo = cstat->cwnd;
  }
}

static void bbr_loss_lower_bounds(ngtcp2_cc_bbr *bbr) {
  bbr->bw_lo = ngtcp2_max(bbr->bw_latest, bbr->bw_lo * NGTCP2_BBR_BETA_NUMER /
                                              NGTCP2_BBR_BETA_DENOM);
  bbr->inflight_lo = ngtcp2_max(bbr->inflight_latest,
                                bbr->inflight_lo * NGTCP2_BBR_BETA_NUMER /
                                    NGTCP2_BBR_BETA_DENOM);
}

static void bbr_bound_bw_for_model(ngtcp2_cc_bbr *bbr) {
  bbr->bw = ngtcp2_min(bbr->max_bw, bbr->bw_lo);
  bbr->bw = ngtcp2_min(bbr->bw, bbr->bw_hi);
}

static void bbr_update_max_bw(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat,
                              const ngtcp2_cc_ack *ack) {
  bbr_update_round(bbr, ack);

  if (cstat->delivery_rate_sec >= bbr->max_bw || !bbr->rst->rs.is_app_limited) {
    ngtcp2_window_filter_update(&bbr->max_bw_filter, cstat->delivery_rate_sec,
                                bbr->cycle_count);

    bbr->max_bw = ngtcp2_window_filter_get_best(&bbr->max_bw_filter);
  }
}

static void bbr_update_round(ngtcp2_cc_bbr *bbr, const ngtcp2_cc_ack *ack) {
  if (ack->pkt_delivered >= bbr->next_round_delivered) {
    bbr_start_round(bbr);

    ++bbr->round_count;
    ++bbr->rounds_since_bw_probe;
    bbr->round_start = 1;

    bbr->bytes_lost_in_round = 0;
    bbr->loss_events_in_round = 0;

    bbr->rst->is_cwnd_limited = 0;

    return;
  }

  bbr->round_start = 0;
}

static void bbr_start_round(ngtcp2_cc_bbr *bbr) {
  bbr->next_round_delivered = bbr->rst->delivered;
}

static int bbr_is_in_probe_bw_state(ngtcp2_cc_bbr *bbr) {
  switch (bbr->state) {
  case NGTCP2_BBR_STATE_PROBE_BW_DOWN:
  case NGTCP2_BBR_STATE_PROBE_BW_CRUISE:
  case NGTCP2_BBR_STATE_PROBE_BW_REFILL:
  case NGTCP2_BBR_STATE_PROBE_BW_UP:
    return 1;
  default:
    return 0;
  }
}

static void bbr_update_ack_aggregation(ngtcp2_cc_bbr *bbr,
                                       ngtcp2_conn_stat *cstat,
                                       const ngtcp2_cc_ack *ack,
                                       ngtcp2_tstamp ts) {
  ngtcp2_duration interval = ts - bbr->extra_acked_interval_start;
  uint64_t expected_delivered = bbr->bw * interval / NGTCP2_SECONDS;
  uint64_t extra;

  if (bbr->extra_acked_delivered <= expected_delivered) {
    bbr->extra_acked_delivered = 0;
    bbr->extra_acked_interval_start = ts;
    expected_delivered = 0;
  }

  bbr->extra_acked_delivered += ack->bytes_delivered;
  extra = bbr->extra_acked_delivered - expected_delivered;
  extra = ngtcp2_min(extra, cstat->cwnd);

  ngtcp2_window_filter_update(&bbr->extra_acked_filter, extra,
                              bbr->round_count);

  bbr->extra_acked = ngtcp2_window_filter_get_best(&bbr->extra_acked_filter);
}

static void bbr_enter_drain(ngtcp2_cc_bbr *bbr) {
  ngtcp2_log_info(bbr->cc.log, NGTCP2_LOG_EVENT_CCA, "bbr enter Drain");

  bbr->state = NGTCP2_BBR_STATE_DRAIN;
  bbr->pacing_gain_h = 100 * 100 / NGTCP2_BBR_STARTUP_CWND_GAIN_H;
  bbr->cwnd_gain_h = NGTCP2_BBR_STARTUP_CWND_GAIN_H;
}

static void bbr_check_drain(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat,
                            ngtcp2_tstamp ts) {
  if (bbr->state == NGTCP2_BBR_STATE_DRAIN &&
      cstat->bytes_in_flight <= bbr_inflight(bbr, cstat, bbr->bw, 100)) {
    bbr_enter_probe_bw(bbr, ts);
  }
}

static void bbr_enter_probe_bw(ngtcp2_cc_bbr *bbr, ngtcp2_tstamp ts) {
  bbr_start_probe_bw_down(bbr, ts);
}

static void bbr_start_probe_bw_down(ngtcp2_cc_bbr *bbr, ngtcp2_tstamp ts) {
  ngtcp2_log_info(bbr->cc.log, NGTCP2_LOG_EVENT_CCA, "bbr start ProbeBW_DOWN");

  bbr_reset_congestion_signals(bbr);

  bbr->probe_up_cnt = UINT64_MAX;

  bbr_pick_probe_wait(bbr);

  bbr->cycle_stamp = ts;
  bbr->ack_phase = NGTCP2_BBR_ACK_PHASE_ACKS_PROBE_STOPPING;

  bbr_start_round(bbr);

  bbr->state = NGTCP2_BBR_STATE_PROBE_BW_DOWN;
  bbr->pacing_gain_h = 90;
  bbr->cwnd_gain_h = 200;
}

static void bbr_start_probe_bw_cruise(ngtcp2_cc_bbr *bbr) {
  ngtcp2_log_info(bbr->cc.log, NGTCP2_LOG_EVENT_CCA,
                  "bbr start ProbeBW_CRUISE");

  bbr->state = NGTCP2_BBR_STATE_PROBE_BW_CRUISE;
  bbr->pacing_gain_h = 100;
  bbr->cwnd_gain_h = 200;
}

static void bbr_start_probe_bw_refill(ngtcp2_cc_bbr *bbr) {
  ngtcp2_log_info(bbr->cc.log, NGTCP2_LOG_EVENT_CCA,
                  "bbr start ProbeBW_REFILL");

  bbr_reset_lower_bounds(bbr);

  bbr->bw_probe_up_rounds = 0;
  bbr->bw_probe_up_acks = 0;
  bbr->ack_phase = NGTCP2_BBR_ACK_PHASE_ACKS_REFILLING;

  bbr_start_round(bbr);

  bbr->state = NGTCP2_BBR_STATE_PROBE_BW_REFILL;
  bbr->pacing_gain_h = 100;
  bbr->cwnd_gain_h = 200;
}

static void bbr_start_probe_bw_up(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat,
                                  ngtcp2_tstamp ts) {
  ngtcp2_log_info(bbr->cc.log, NGTCP2_LOG_EVENT_CCA, "bbr start ProbeBW_UP");

  bbr->ack_phase = NGTCP2_BBR_ACK_PHASE_ACKS_PROBE_STARTING;

  bbr_start_round(bbr);

  bbr->cycle_stamp = ts;
  bbr->state = NGTCP2_BBR_STATE_PROBE_BW_UP;
  bbr->pacing_gain_h = 125;
  bbr->cwnd_gain_h = 225;

  bbr_raise_inflight_hi_slope(bbr, cstat);
}

static void bbr_update_probe_bw_cycle_phase(ngtcp2_cc_bbr *bbr,
                                            ngtcp2_conn_stat *cstat,
                                            const ngtcp2_cc_ack *ack,
                                            ngtcp2_tstamp ts) {
  if (!bbr->filled_pipe) {
    return;
  }

  bbr_adapt_upper_bounds(bbr, cstat, ack, ts);

  if (!bbr_is_in_probe_bw_state(bbr)) {
    return;
  }

  switch (bbr->state) {
  case NGTCP2_BBR_STATE_PROBE_BW_DOWN:
    if (bbr_check_time_to_probe_bw(bbr, cstat, ts)) {
      return;
    }

    if (bbr_check_time_to_cruise(bbr, cstat, ts)) {
      bbr_start_probe_bw_cruise(bbr);
    }

    break;
  case NGTCP2_BBR_STATE_PROBE_BW_CRUISE:
    if (bbr_check_time_to_probe_bw(bbr, cstat, ts)) {
      return;
    }

    break;
  case NGTCP2_BBR_STATE_PROBE_BW_REFILL:
    if (bbr->round_start) {
      bbr->bw_probe_samples = 1;
      bbr_start_probe_bw_up(bbr, cstat, ts);
    }

    break;
  case NGTCP2_BBR_STATE_PROBE_BW_UP:
    if (bbr_has_elapsed_in_phase(bbr, bbr->min_rtt, ts) &&
        cstat->bytes_in_flight > bbr_inflight(bbr, cstat, bbr->max_bw, 125)) {
      bbr_start_probe_bw_down(bbr, ts);
    }

    break;
  default:
    break;
  }
}

static int bbr_check_time_to_cruise(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat,
                                    ngtcp2_tstamp ts) {
  (void)ts;

  if (cstat->bytes_in_flight > bbr_inflight_with_headroom(bbr, cstat)) {
    return 0;
  }

  if (cstat->bytes_in_flight <= bbr_inflight(bbr, cstat, bbr->max_bw, 100)) {
    return 1;
  }

  return 0;
}

static int bbr_has_elapsed_in_phase(ngtcp2_cc_bbr *bbr,
                                    ngtcp2_duration interval,
                                    ngtcp2_tstamp ts) {
  return ts > bbr->cycle_stamp + interval;
}

static uint64_t bbr_inflight_with_headroom(ngtcp2_cc_bbr *bbr,
                                           ngtcp2_conn_stat *cstat) {
  uint64_t headroom;
  uint64_t mpcwnd;
  if (bbr->inflight_hi == UINT64_MAX) {
    return UINT64_MAX;
  }

  headroom = ngtcp2_max(cstat->max_tx_udp_payload_size,
                        bbr->inflight_hi * NGTCP2_BBR_HEADROOM_NUMER /
                            NGTCP2_BBR_HEADROOM_DENOM);
  mpcwnd = min_pipe_cwnd(cstat->max_tx_udp_payload_size);

  if (bbr->inflight_hi > headroom) {
    return ngtcp2_max(bbr->inflight_hi - headroom, mpcwnd);
  }

  return mpcwnd;
}

static void bbr_raise_inflight_hi_slope(ngtcp2_cc_bbr *bbr,
                                        ngtcp2_conn_stat *cstat) {
  uint64_t growth_this_round = cstat->max_tx_udp_payload_size
                               << bbr->bw_probe_up_rounds;

  bbr->bw_probe_up_rounds = ngtcp2_min(bbr->bw_probe_up_rounds + 1, 30);
  bbr->probe_up_cnt = ngtcp2_max(cstat->cwnd / growth_this_round, 1) *
                      cstat->max_tx_udp_payload_size;
}

static void bbr_probe_inflight_hi_upward(ngtcp2_cc_bbr *bbr,
                                         ngtcp2_conn_stat *cstat,
                                         const ngtcp2_cc_ack *ack) {
  uint64_t delta;

  if (!bbr->rst->is_cwnd_limited || cstat->cwnd < bbr->inflight_hi) {
    return;
  }

  bbr->bw_probe_up_acks += ack->bytes_delivered;

  if (bbr->bw_probe_up_acks >= bbr->probe_up_cnt) {
    delta = bbr->bw_probe_up_acks / bbr->probe_up_cnt;
    bbr->bw_probe_up_acks -= delta * bbr->probe_up_cnt;
    bbr->inflight_hi += delta * cstat->max_tx_udp_payload_size;
  }

  if (bbr->round_start) {
    bbr_raise_inflight_hi_slope(bbr, cstat);
  }
}

static void bbr_adapt_upper_bounds(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat,
                                   const ngtcp2_cc_ack *ack, ngtcp2_tstamp ts) {
  if (bbr->ack_phase == NGTCP2_BBR_ACK_PHASE_ACKS_PROBE_STARTING &&
      bbr->round_start) {
    bbr->ack_phase = NGTCP2_BBR_ACK_PHASE_ACKS_PROBE_FEEDBACK;
  }

  if (bbr->ack_phase == NGTCP2_BBR_ACK_PHASE_ACKS_PROBE_STOPPING &&
      bbr->round_start) {
    if (bbr_is_in_probe_bw_state(bbr) && !bbr->rst->rs.is_app_limited) {
      bbr_advance_max_bw_filter(bbr);
    }
  }

  if (!bbr_check_inflight_too_high(bbr, cstat, ts)) {
    /* bbr->bw_hi never be updated */
    if (bbr->inflight_hi == UINT64_MAX /* || bbr->bw_hi == UINT64_MAX */) {
      return;
    }

    if (bbr->rst->rs.tx_in_flight > bbr->inflight_hi) {
      bbr->inflight_hi = bbr->rst->rs.tx_in_flight;
    }

    if (cstat->delivery_rate_sec > bbr->bw_hi) {
      bbr->bw_hi = cstat->delivery_rate_sec;
    }

    if (bbr->state == NGTCP2_BBR_STATE_PROBE_BW_UP) {
      bbr_probe_inflight_hi_upward(bbr, cstat, ack);
    }
  }
}

static int bbr_check_time_to_probe_bw(ngtcp2_cc_bbr *bbr,
                                      ngtcp2_conn_stat *cstat,
                                      ngtcp2_tstamp ts) {
  if (bbr_has_elapsed_in_phase(bbr, bbr->bw_probe_wait, ts) ||
      bbr_is_reno_coexistence_probe_time(bbr, cstat)) {
    bbr_start_probe_bw_refill(bbr);

    return 1;
  }

  return 0;
}

static void bbr_pick_probe_wait(ngtcp2_cc_bbr *bbr) {
  uint8_t rand;

  bbr->rand(&rand, 1, &bbr->rand_ctx);

  bbr->rounds_since_bw_probe = (uint64_t)(rand * 2 / 256);

  bbr->rand(&rand, 1, &bbr->rand_ctx);

  bbr->bw_probe_wait =
      2 * NGTCP2_SECONDS + (ngtcp2_tstamp)(NGTCP2_SECONDS * rand / 255);
}

static int bbr_is_reno_coexistence_probe_time(ngtcp2_cc_bbr *bbr,
                                              ngtcp2_conn_stat *cstat) {
  uint64_t reno_rounds =
      bbr_target_inflight(bbr, cstat) / cstat->max_tx_udp_payload_size;

  return bbr->rounds_since_bw_probe >= ngtcp2_min(reno_rounds, 63);
}

static uint64_t bbr_target_inflight(ngtcp2_cc_bbr *bbr,
                                    ngtcp2_conn_stat *cstat) {
  uint64_t bdp = bbr_inflight(bbr, cstat, bbr->bw, 100);

  return ngtcp2_min(bdp, cstat->cwnd);
}

static int bbr_check_inflight_too_high(ngtcp2_cc_bbr *bbr,
                                       ngtcp2_conn_stat *cstat,
                                       ngtcp2_tstamp ts) {
  if (is_inflight_too_high(&bbr->rst->rs)) {
    if (bbr->bw_probe_samples) {
      bbr_handle_inflight_too_high(bbr, cstat, &bbr->rst->rs, ts);
    }

    return 1;
  }

  return 0;
}

static int is_inflight_too_high(const ngtcp2_rs *rs) {
  return rs->lost * NGTCP2_BBR_LOSS_THRESH_DENOM >
         rs->tx_in_flight * NGTCP2_BBR_LOSS_THRESH_NUMER;
}

static void bbr_handle_inflight_too_high(ngtcp2_cc_bbr *bbr,
                                         ngtcp2_conn_stat *cstat,
                                         const ngtcp2_rs *rs,
                                         ngtcp2_tstamp ts) {
  bbr->bw_probe_samples = 0;

  if (!rs->is_app_limited) {
    bbr->inflight_hi = ngtcp2_max(
        rs->tx_in_flight, bbr_target_inflight(bbr, cstat) *
                              NGTCP2_BBR_BETA_NUMER / NGTCP2_BBR_BETA_DENOM);
  }

  if (bbr->state == NGTCP2_BBR_STATE_PROBE_BW_UP) {
    bbr_start_probe_bw_down(bbr, ts);
  }
}

static void bbr_handle_lost_packet(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat,
                                   const ngtcp2_cc_pkt *pkt, ngtcp2_tstamp ts) {
  ngtcp2_rs rs = {0};

  if (!bbr->bw_probe_samples) {
    return;
  }

  rs.tx_in_flight = pkt->tx_in_flight;
  /* bbr->rst->lost is not incremented for pkt yet */
  rs.lost = bbr->rst->lost + pkt->pktlen - pkt->lost;
  rs.is_app_limited = pkt->is_app_limited;

  if (is_inflight_too_high(&rs)) {
    rs.tx_in_flight = bbr_inflight_hi_from_lost_packet(bbr, &rs, pkt);

    bbr_handle_inflight_too_high(bbr, cstat, &rs, ts);
  }
}

static uint64_t bbr_inflight_hi_from_lost_packet(ngtcp2_cc_bbr *bbr,
                                                 const ngtcp2_rs *rs,
                                                 const ngtcp2_cc_pkt *pkt) {
  uint64_t inflight_prev, lost_prev, lost_prefix;
  (void)bbr;

  assert(rs->tx_in_flight >= pkt->pktlen);

  inflight_prev = rs->tx_in_flight - pkt->pktlen;

  assert(rs->lost >= pkt->pktlen);

  lost_prev = rs->lost - pkt->pktlen;

  if (inflight_prev * NGTCP2_BBR_LOSS_THRESH_NUMER <
      lost_prev * NGTCP2_BBR_LOSS_THRESH_DENOM) {
    return inflight_prev;
  }

  lost_prefix = (inflight_prev * NGTCP2_BBR_LOSS_THRESH_NUMER -
                 lost_prev * NGTCP2_BBR_LOSS_THRESH_DENOM) /
                (NGTCP2_BBR_LOSS_THRESH_DENOM - NGTCP2_BBR_LOSS_THRESH_NUMER);

  return inflight_prev + lost_prefix;
}

static void bbr_update_min_rtt(ngtcp2_cc_bbr *bbr, const ngtcp2_cc_ack *ack,
                               ngtcp2_tstamp ts) {
  int min_rtt_expired;

  bbr->probe_rtt_expired =
      ts > bbr->probe_rtt_min_stamp + NGTCP2_BBR_PROBE_RTT_INTERVAL;

  if (ack->rtt != UINT64_MAX &&
      (ack->rtt < bbr->probe_rtt_min_delay || bbr->probe_rtt_expired)) {
    bbr->probe_rtt_min_delay = ack->rtt;
    bbr->probe_rtt_min_stamp = ts;
  }

  min_rtt_expired = ts > bbr->min_rtt_stamp + NGTCP2_BBR_MIN_RTT_FILTERLEN;

  if (bbr->probe_rtt_min_delay < bbr->min_rtt || min_rtt_expired) {
    bbr->min_rtt = bbr->probe_rtt_min_delay;
    bbr->min_rtt_stamp = bbr->probe_rtt_min_stamp;

    ngtcp2_log_info(bbr->cc.log, NGTCP2_LOG_EVENT_CCA,
                    "bbr update min_rtt=%" PRIu64, bbr->min_rtt);
  }
}

static void bbr_check_probe_rtt(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat,
                                ngtcp2_tstamp ts) {
  if (bbr->state != NGTCP2_BBR_STATE_PROBE_RTT && bbr->probe_rtt_expired &&
      !bbr->idle_restart) {
    bbr_enter_probe_rtt(bbr);
    bbr_save_cwnd(bbr, cstat);

    bbr->probe_rtt_done_stamp = UINT64_MAX;
    bbr->ack_phase = NGTCP2_BBR_ACK_PHASE_ACKS_PROBE_STOPPING;

    bbr_start_round(bbr);
  }

  if (bbr->state == NGTCP2_BBR_STATE_PROBE_RTT) {
    bbr_handle_probe_rtt(bbr, cstat, ts);
  }

  if (bbr->rst->rs.delivered) {
    bbr->idle_restart = 0;
  }
}

static void bbr_enter_probe_rtt(ngtcp2_cc_bbr *bbr) {
  ngtcp2_log_info(bbr->cc.log, NGTCP2_LOG_EVENT_CCA, "bbr enter ProbeRTT");

  bbr->state = NGTCP2_BBR_STATE_PROBE_RTT;
  bbr->pacing_gain_h = 100;
  bbr->cwnd_gain_h = NGTCP2_BBR_PROBE_RTT_CWND_GAIN_H;
}

static void bbr_handle_probe_rtt(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat,
                                 ngtcp2_tstamp ts) {
  bbr_mark_connection_app_limited(bbr, cstat);

  if (bbr->probe_rtt_done_stamp == UINT64_MAX &&
      cstat->bytes_in_flight <= bbr_probe_rtt_cwnd(bbr, cstat)) {
    bbr->probe_rtt_done_stamp = ts + NGTCP2_BBR_PROBE_RTT_DURATION;
    bbr->probe_rtt_round_done = 0;

    bbr_start_round(bbr);

    return;
  }

  if (bbr->probe_rtt_done_stamp != UINT64_MAX) {
    if (bbr->round_start) {
      bbr->probe_rtt_round_done = 1;
    }

    if (bbr->probe_rtt_round_done) {
      bbr_check_probe_rtt_done(bbr, cstat, ts);
    }
  }
}

static void bbr_check_probe_rtt_done(ngtcp2_cc_bbr *bbr,
                                     ngtcp2_conn_stat *cstat,
                                     ngtcp2_tstamp ts) {
  if (bbr->probe_rtt_done_stamp != UINT64_MAX &&
      ts > bbr->probe_rtt_done_stamp) {
    bbr->probe_rtt_min_stamp = ts;
    bbr_restore_cwnd(bbr, cstat);
    bbr_exit_probe_rtt(bbr, ts);
  }
}

static void bbr_mark_connection_app_limited(ngtcp2_cc_bbr *bbr,
                                            ngtcp2_conn_stat *cstat) {
  uint64_t app_limited = bbr->rst->delivered + cstat->bytes_in_flight;

  if (app_limited) {
    bbr->rst->app_limited = app_limited;
  } else {
    bbr->rst->app_limited = cstat->max_tx_udp_payload_size;
  }
}

static void bbr_exit_probe_rtt(ngtcp2_cc_bbr *bbr, ngtcp2_tstamp ts) {
  bbr_reset_lower_bounds(bbr);

  if (bbr->filled_pipe) {
    bbr_start_probe_bw_down(bbr, ts);
    bbr_start_probe_bw_cruise(bbr);
  } else {
    bbr_enter_startup(bbr);
  }
}

static void bbr_handle_restart_from_idle(ngtcp2_cc_bbr *bbr,
                                         ngtcp2_conn_stat *cstat,
                                         ngtcp2_tstamp ts) {
  if (cstat->bytes_in_flight == 0 && bbr->rst->app_limited) {
    ngtcp2_log_info(bbr->cc.log, NGTCP2_LOG_EVENT_CCA, "bbr restart from idle");

    bbr->idle_restart = 1;
    bbr->extra_acked_interval_start = ts;

    if (bbr_is_in_probe_bw_state(bbr)) {
      bbr_set_pacing_rate_with_gain(bbr, cstat, 100);
    } else if (bbr->state == NGTCP2_BBR_STATE_PROBE_RTT) {
      bbr_check_probe_rtt_done(bbr, cstat, ts);
    }
  }
}

static uint64_t bbr_bdp_multiple(ngtcp2_cc_bbr *bbr, uint64_t bw,
                                 uint64_t gain_h) {
  uint64_t bdp;

  if (bbr->min_rtt == UINT64_MAX) {
    return bbr->initial_cwnd;
  }

  bdp = bw * bbr->min_rtt / NGTCP2_SECONDS;

  return (uint64_t)(bdp * gain_h / 100);
}

static uint64_t min_pipe_cwnd(size_t max_udp_payload_size) {
  return max_udp_payload_size * 4;
}

static uint64_t bbr_quantization_budget(ngtcp2_cc_bbr *bbr,
                                        ngtcp2_conn_stat *cstat,
                                        uint64_t inflight) {
  bbr_update_offload_budget(bbr, cstat);

  inflight = ngtcp2_max(inflight, bbr->offload_budget);
  inflight =
      ngtcp2_max(inflight, min_pipe_cwnd(cstat->max_tx_udp_payload_size));

  if (bbr->state == NGTCP2_BBR_STATE_PROBE_BW_UP) {
    inflight += 2 * cstat->max_tx_udp_payload_size;
  }

  return inflight;
}

static uint64_t bbr_inflight(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat,
                             uint64_t bw, uint64_t gain_h) {
  uint64_t inflight = bbr_bdp_multiple(bbr, bw, gain_h);

  return bbr_quantization_budget(bbr, cstat, inflight);
}

static void bbr_update_max_inflight(ngtcp2_cc_bbr *bbr,
                                    ngtcp2_conn_stat *cstat) {
  uint64_t inflight;

  /* Not documented */
  /* bbr_update_aggregation_budget(bbr); */

  inflight =
      bbr_bdp_multiple(bbr, bbr->bw, bbr->cwnd_gain_h) + bbr->extra_acked;
  bbr->max_inflight = bbr_quantization_budget(bbr, cstat, inflight);
}

static void bbr_update_offload_budget(ngtcp2_cc_bbr *bbr,
                                      ngtcp2_conn_stat *cstat) {
  bbr->offload_budget = 3 * cstat->send_quantum;
}

static void bbr_advance_max_bw_filter(ngtcp2_cc_bbr *bbr) {
  ++bbr->cycle_count;
}

static void bbr_modulate_cwnd_for_recovery(ngtcp2_cc_bbr *bbr,
                                           ngtcp2_conn_stat *cstat,
                                           const ngtcp2_cc_ack *ack) {
  if (ack->bytes_lost > 0) {
    if (cstat->cwnd > ack->bytes_lost) {
      cstat->cwnd -= ack->bytes_lost;
      cstat->cwnd = ngtcp2_max(cstat->cwnd, 2 * cstat->max_tx_udp_payload_size);
    } else {
      cstat->cwnd = 2 * cstat->max_tx_udp_payload_size;
    }
  }

  if (bbr->packet_conservation) {
    cstat->cwnd =
        ngtcp2_max(cstat->cwnd, cstat->bytes_in_flight + ack->bytes_delivered);
  }
}

static void bbr_save_cwnd(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat) {
  if (!bbr->in_loss_recovery && bbr->state != NGTCP2_BBR_STATE_PROBE_RTT) {
    bbr->prior_cwnd = cstat->cwnd;
    return;
  }

  bbr->prior_cwnd = ngtcp2_max(bbr->prior_cwnd, cstat->cwnd);
}

static void bbr_restore_cwnd(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat) {
  cstat->cwnd = ngtcp2_max(cstat->cwnd, bbr->prior_cwnd);
}

static uint64_t bbr_probe_rtt_cwnd(ngtcp2_cc_bbr *bbr,
                                   ngtcp2_conn_stat *cstat) {
  uint64_t probe_rtt_cwnd =
      bbr_bdp_multiple(bbr, bbr->bw, NGTCP2_BBR_PROBE_RTT_CWND_GAIN_H);
  uint64_t mpcwnd = min_pipe_cwnd(cstat->max_tx_udp_payload_size);

  return ngtcp2_max(probe_rtt_cwnd, mpcwnd);
}

static void bbr_bound_cwnd_for_probe_rtt(ngtcp2_cc_bbr *bbr,
                                         ngtcp2_conn_stat *cstat) {
  uint64_t probe_rtt_cwnd;

  if (bbr->state == NGTCP2_BBR_STATE_PROBE_RTT) {
    probe_rtt_cwnd = bbr_probe_rtt_cwnd(bbr, cstat);

    cstat->cwnd = ngtcp2_min(cstat->cwnd, probe_rtt_cwnd);
  }
}

static void bbr_set_cwnd(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat,
                         const ngtcp2_cc_ack *ack) {
  uint64_t mpcwnd;

  bbr_update_max_inflight(bbr, cstat);
  bbr_modulate_cwnd_for_recovery(bbr, cstat, ack);

  if (!bbr->packet_conservation) {
    if (bbr->filled_pipe) {
      cstat->cwnd += ack->bytes_delivered;
      cstat->cwnd = ngtcp2_min(cstat->cwnd, bbr->max_inflight);
    } else if (cstat->cwnd < bbr->max_inflight ||
               bbr->rst->delivered < bbr->initial_cwnd) {
      cstat->cwnd += ack->bytes_delivered;
    }

    mpcwnd = min_pipe_cwnd(cstat->max_tx_udp_payload_size);
    cstat->cwnd = ngtcp2_max(cstat->cwnd, mpcwnd);
  }

  bbr_bound_cwnd_for_probe_rtt(bbr, cstat);
  bbr_bound_cwnd_for_model(bbr, cstat);
}

static void bbr_bound_cwnd_for_model(ngtcp2_cc_bbr *bbr,
                                     ngtcp2_conn_stat *cstat) {
  uint64_t cap = UINT64_MAX;
  uint64_t mpcwnd = min_pipe_cwnd(cstat->max_tx_udp_payload_size);

  if (bbr_is_in_probe_bw_state(bbr) &&
      bbr->state != NGTCP2_BBR_STATE_PROBE_BW_CRUISE) {
    cap = bbr->inflight_hi;
  } else if (bbr->state == NGTCP2_BBR_STATE_PROBE_RTT ||
             bbr->state == NGTCP2_BBR_STATE_PROBE_BW_CRUISE) {
    cap = bbr_inflight_with_headroom(bbr, cstat);
  }

  cap = ngtcp2_min(cap, bbr->inflight_lo);
  cap = ngtcp2_max(cap, mpcwnd);

  cstat->cwnd = ngtcp2_min(cstat->cwnd, cap);
}

static void bbr_set_send_quantum(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat) {
  size_t floor, send_quantum;
  (void)bbr;

  if (cstat->pacing_interval > (NGTCP2_SECONDS * 8 * 10 / 12) >> 20) {
    floor = cstat->max_tx_udp_payload_size;
  } else {
    floor = 2 * cstat->max_tx_udp_payload_size;
  }

  if (cstat->pacing_interval) {
    send_quantum = (size_t)(NGTCP2_MILLISECONDS / cstat->pacing_interval);
    send_quantum = ngtcp2_min(send_quantum, 64 * 1024);
  } else {
    send_quantum = 64 * 1024;
  }

  cstat->send_quantum = ngtcp2_max(send_quantum, floor);
}

static int in_congestion_recovery(const ngtcp2_conn_stat *cstat,
                                  ngtcp2_tstamp sent_time) {
  return cstat->congestion_recovery_start_ts != UINT64_MAX &&
         sent_time <= cstat->congestion_recovery_start_ts;
}

static void bbr_handle_recovery(ngtcp2_cc_bbr *bbr, ngtcp2_conn_stat *cstat,
                                const ngtcp2_cc_ack *ack, ngtcp2_tstamp ts) {
  if (bbr->in_loss_recovery) {
    if (ts - cstat->congestion_recovery_start_ts >= cstat->smoothed_rtt) {
      bbr->packet_conservation = 0;
    }

    if (ack->largest_pkt_sent_ts != UINT64_MAX &&
        !in_congestion_recovery(cstat, ack->largest_pkt_sent_ts)) {
      bbr->in_loss_recovery = 0;
      bbr->packet_conservation = 0;
      bbr_restore_cwnd(bbr, cstat);
    }

    return;
  }

  if (bbr->congestion_recovery_start_ts != UINT64_MAX) {
    bbr->in_loss_recovery = 1;
    bbr_save_cwnd(bbr, cstat);
    cstat->cwnd =
        cstat->bytes_in_flight +
        ngtcp2_max(ack->bytes_delivered, cstat->max_tx_udp_payload_size);

    cstat->congestion_recovery_start_ts = bbr->congestion_recovery_start_ts;
    bbr->congestion_recovery_start_ts = UINT64_MAX;
    bbr->packet_conservation = 1;
    bbr->congestion_recovery_next_round_delivered = bbr->rst->delivered;
    bbr->prior_inflight_hi = bbr->inflight_hi;
    bbr->prior_inflight_lo = bbr->inflight_lo;
    bbr->prior_bw_lo = bbr->bw_lo;
  }
}

static void bbr_cc_on_pkt_lost(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                               const ngtcp2_cc_pkt *pkt, ngtcp2_tstamp ts) {
  ngtcp2_cc_bbr *bbr = ngtcp2_struct_of(cc, ngtcp2_cc_bbr, cc);

  bbr_update_on_loss(bbr, cstat, pkt, ts);
}

static void bbr_cc_congestion_event(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                                    ngtcp2_tstamp sent_ts, ngtcp2_tstamp ts) {
  ngtcp2_cc_bbr *bbr = ngtcp2_struct_of(cc, ngtcp2_cc_bbr, cc);

  if (bbr->in_loss_recovery ||
      bbr->congestion_recovery_start_ts != UINT64_MAX ||
      in_congestion_recovery(cstat, sent_ts)) {
    return;
  }

  bbr->congestion_recovery_start_ts = ts;
}

static void bbr_cc_on_spurious_congestion(ngtcp2_cc *cc,
                                          ngtcp2_conn_stat *cstat,
                                          ngtcp2_tstamp ts) {
  ngtcp2_cc_bbr *bbr = ngtcp2_struct_of(cc, ngtcp2_cc_bbr, cc);
  (void)ts;

  bbr->congestion_recovery_start_ts = UINT64_MAX;
  cstat->congestion_recovery_start_ts = UINT64_MAX;

  if (bbr->in_loss_recovery) {
    bbr->in_loss_recovery = 0;
    bbr->packet_conservation = 0;
    bbr_restore_cwnd(bbr, cstat);
    bbr->full_bw_count = 0;
    bbr->loss_in_round = 0;
    bbr->inflight_lo = ngtcp2_max(bbr->inflight_lo, bbr->prior_inflight_lo);
    bbr->inflight_hi = ngtcp2_max(bbr->inflight_hi, bbr->prior_inflight_hi);
    bbr->bw_lo = ngtcp2_max(bbr->bw_lo, bbr->prior_bw_lo);
  }
}

static void bbr_cc_on_persistent_congestion(ngtcp2_cc *cc,
                                            ngtcp2_conn_stat *cstat,
                                            ngtcp2_tstamp ts) {
  ngtcp2_cc_bbr *bbr = ngtcp2_struct_of(cc, ngtcp2_cc_bbr, cc);
  (void)ts;

  cstat->congestion_recovery_start_ts = UINT64_MAX;
  bbr->congestion_recovery_start_ts = UINT64_MAX;
  bbr->in_loss_recovery = 0;
  bbr->packet_conservation = 0;

  bbr_save_cwnd(bbr, cstat);
  cstat->cwnd = cstat->bytes_in_flight + cstat->max_tx_udp_payload_size;
  cstat->cwnd =
      ngtcp2_max(cstat->cwnd, min_pipe_cwnd(cstat->max_tx_udp_payload_size));
}

static void bbr_cc_on_ack_recv(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                               const ngtcp2_cc_ack *ack, ngtcp2_tstamp ts) {
  ngtcp2_cc_bbr *bbr = ngtcp2_struct_of(cc, ngtcp2_cc_bbr, cc);

  bbr_handle_recovery(bbr, cstat, ack, ts);
  bbr_update_on_ack(bbr, cstat, ack, ts);
}

static void bbr_cc_on_pkt_sent(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                               const ngtcp2_cc_pkt *pkt) {
  ngtcp2_cc_bbr *bbr = ngtcp2_struct_of(cc, ngtcp2_cc_bbr, cc);

  bbr_on_transmit(bbr, cstat, pkt->sent_ts);
}

static void bbr_cc_reset(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                         ngtcp2_tstamp ts) {
  ngtcp2_cc_bbr *bbr = ngtcp2_struct_of(cc, ngtcp2_cc_bbr, cc);

  bbr_on_init(bbr, cstat, ts);
}

void ngtcp2_cc_bbr_init(ngtcp2_cc_bbr *bbr, ngtcp2_log *log,
                        ngtcp2_conn_stat *cstat, ngtcp2_rst *rst,
                        ngtcp2_tstamp initial_ts, ngtcp2_rand rand,
                        const ngtcp2_rand_ctx *rand_ctx) {
  memset(bbr, 0, sizeof(*bbr));

  bbr->cc.log = log;
  bbr->cc.on_pkt_lost = bbr_cc_on_pkt_lost;
  bbr->cc.congestion_event = bbr_cc_congestion_event;
  bbr->cc.on_spurious_congestion = bbr_cc_on_spurious_congestion;
  bbr->cc.on_persistent_congestion = bbr_cc_on_persistent_congestion;
  bbr->cc.on_ack_recv = bbr_cc_on_ack_recv;
  bbr->cc.on_pkt_sent = bbr_cc_on_pkt_sent;
  bbr->cc.reset = bbr_cc_reset;

  bbr->rst = rst;
  bbr->rand = rand;
  bbr->rand_ctx = *rand_ctx;
  bbr->initial_cwnd = cstat->cwnd;

  bbr_on_init(bbr, cstat, initial_ts);
}
