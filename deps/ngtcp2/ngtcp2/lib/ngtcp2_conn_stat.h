/*
 * ngtcp2
 *
 * Copyright (c) 2023 ngtcp2 contributors
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
#ifndef NGTCP2_CONN_STAT_H
#define NGTCP2_CONN_STAT_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <ngtcp2/ngtcp2.h>

/**
 * @struct
 *
 * :type:`ngtcp2_conn_stat` holds various connection statistics, and
 * computed data for recovery and congestion controller.
 */
typedef struct ngtcp2_conn_stat {
  /**
   * :member:`latest_rtt` is the latest RTT sample which is not
   * adjusted by acknowledgement delay.
   */
  ngtcp2_duration latest_rtt;
  /**
   * :member:`min_rtt` is the minimum RTT seen so far.  It is not
   * adjusted by acknowledgement delay.
   */
  ngtcp2_duration min_rtt;
  /**
   * :member:`smoothed_rtt` is the smoothed RTT.
   */
  ngtcp2_duration smoothed_rtt;
  /**
   * :member:`rttvar` is a mean deviation of observed RTT.
   */
  ngtcp2_duration rttvar;
  /**
   * :member:`initial_rtt` is the initial RTT which is used when no
   * RTT sample is available.
   */
  ngtcp2_duration initial_rtt;
  /**
   * :member:`first_rtt_sample_ts` is the timestamp when the first RTT
   * sample is obtained.
   */
  ngtcp2_tstamp first_rtt_sample_ts;
  /**
   * :member:`pto_count` is the count of successive PTO timer
   * expiration.
   */
  size_t pto_count;
  /**
   * :member:`loss_detection_timer` is the deadline of the current
   * loss detection timer.
   */
  ngtcp2_tstamp loss_detection_timer;
  /**
   * :member:`last_tx_pkt_ts` corresponds to
   * time_of_last_ack_eliciting_packet in :rfc:`9002`.
   */
  ngtcp2_tstamp last_tx_pkt_ts[NGTCP2_PKTNS_ID_MAX];
  /**
   * :member:`loss_time` corresponds to loss_time in :rfc:`9002`.
   */
  ngtcp2_tstamp loss_time[NGTCP2_PKTNS_ID_MAX];
  /**
   * :member:`cwnd` is the size of congestion window.
   */
  uint64_t cwnd;
  /**
   * :member:`ssthresh` is slow start threshold.
   */
  uint64_t ssthresh;
  /**
   * :member:`congestion_recovery_start_ts` is the timestamp when
   * congestion recovery started.
   */
  ngtcp2_tstamp congestion_recovery_start_ts;
  /**
   * :member:`bytes_in_flight` is the number in bytes of all sent
   * packets which have not been acknowledged.
   */
  uint64_t bytes_in_flight;
  /**
   * :member:`max_tx_udp_payload_size` is the maximum size of UDP
   * datagram payload that this endpoint transmits.  It is used by
   * congestion controller to compute congestion window.
   */
  size_t max_tx_udp_payload_size;
  /**
   * :member:`delivery_rate_sec` is the current sending rate measured
   * in byte per second.
   */
  uint64_t delivery_rate_sec;
  /**
   * :member:`pacing_interval` is the inverse of pacing rate, which is
   * the current packet sending rate computed by a congestion
   * controller.  0 if a congestion controller does not set pacing
   * interval.  Even if this value is set to 0, the library paces
   * packets.
   */
  ngtcp2_duration pacing_interval;
  /**
   * :member:`send_quantum` is the maximum size of a data aggregate
   * scheduled and transmitted together.
   */
  size_t send_quantum;
} ngtcp2_conn_stat;

#endif /* NGTCP2_CONN_STAT_H */
