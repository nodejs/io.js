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
#ifndef NGTCP2_BBR_H
#define NGTCP2_BBR_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <ngtcp2/ngtcp2.h>

#include "ngtcp2_cc.h"
#include "ngtcp2_window_filter.h"

typedef struct ngtcp2_rst ngtcp2_rst;

typedef enum ngtcp2_bbr_state {
  NGTCP2_BBR_STATE_STARTUP,
  NGTCP2_BBR_STATE_DRAIN,
  NGTCP2_BBR_STATE_PROBE_BW_DOWN,
  NGTCP2_BBR_STATE_PROBE_BW_CRUISE,
  NGTCP2_BBR_STATE_PROBE_BW_REFILL,
  NGTCP2_BBR_STATE_PROBE_BW_UP,
  NGTCP2_BBR_STATE_PROBE_RTT,
} ngtcp2_bbr_state;

typedef enum ngtcp2_bbr_ack_phase {
  NGTCP2_BBR_ACK_PHASE_ACKS_PROBE_STARTING,
  NGTCP2_BBR_ACK_PHASE_ACKS_PROBE_STOPPING,
  NGTCP2_BBR_ACK_PHASE_ACKS_PROBE_FEEDBACK,
  NGTCP2_BBR_ACK_PHASE_ACKS_REFILLING,
} ngtcp2_bbr_ack_phase;

/*
 * ngtcp2_cc_bbr is BBR v2 congestion controller, described in
 * https://datatracker.ietf.org/doc/html/draft-cardwell-iccrg-bbr-congestion-control-01
 */
typedef struct ngtcp2_cc_bbr {
  ngtcp2_cc cc;

  uint64_t initial_cwnd;
  ngtcp2_rst *rst;
  ngtcp2_rand rand;
  ngtcp2_rand_ctx rand_ctx;

  /* max_bw_filter for tracking the maximum recent delivery rate
    samples for estimating max_bw. */
  ngtcp2_window_filter max_bw_filter;

  ngtcp2_window_filter extra_acked_filter;

  ngtcp2_duration min_rtt;
  ngtcp2_tstamp min_rtt_stamp;
  ngtcp2_tstamp probe_rtt_done_stamp;
  int probe_rtt_round_done;
  uint64_t prior_cwnd;
  int idle_restart;
  ngtcp2_tstamp extra_acked_interval_start;
  uint64_t extra_acked_delivered;

  /* Congestion signals */
  int loss_in_round;
  uint64_t bw_latest;
  uint64_t inflight_latest;

  /* Lower bounds */
  uint64_t bw_lo;
  uint64_t inflight_lo;

  /* Round counting */
  uint64_t next_round_delivered;
  int round_start;
  uint64_t round_count;

  /* Full pipe */
  int filled_pipe;
  uint64_t full_bw;
  size_t full_bw_count;

  /* Pacing rate */
  uint64_t pacing_gain_h;

  ngtcp2_bbr_state state;
  uint64_t cwnd_gain_h;

  int loss_round_start;
  uint64_t loss_round_delivered;
  uint64_t rounds_since_bw_probe;
  uint64_t max_bw;
  uint64_t bw;
  uint64_t cycle_count;
  uint64_t extra_acked;
  uint64_t bytes_lost_in_round;
  size_t loss_events_in_round;
  uint64_t offload_budget;
  uint64_t probe_up_cnt;
  ngtcp2_tstamp cycle_stamp;
  ngtcp2_bbr_ack_phase ack_phase;
  ngtcp2_duration bw_probe_wait;
  int bw_probe_samples;
  size_t bw_probe_up_rounds;
  uint64_t bw_probe_up_acks;
  uint64_t inflight_hi;
  uint64_t bw_hi;
  int probe_rtt_expired;
  ngtcp2_duration probe_rtt_min_delay;
  ngtcp2_tstamp probe_rtt_min_stamp;
  int in_loss_recovery;
  int packet_conservation;
  uint64_t max_inflight;
  ngtcp2_tstamp congestion_recovery_start_ts;
  uint64_t congestion_recovery_next_round_delivered;

  uint64_t prior_inflight_lo;
  uint64_t prior_inflight_hi;
  uint64_t prior_bw_lo;
} ngtcp2_cc_bbr;

void ngtcp2_cc_bbr_init(ngtcp2_cc_bbr *bbr, ngtcp2_log *log,
                        ngtcp2_conn_stat *cstat, ngtcp2_rst *rst,
                        ngtcp2_tstamp initial_ts, ngtcp2_rand rand,
                        const ngtcp2_rand_ctx *rand_ctx);

#endif /* NGTCP2_BBR_H */
