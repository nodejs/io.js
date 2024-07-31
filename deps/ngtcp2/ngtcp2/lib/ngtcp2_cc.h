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
#ifndef NGTCP2_CC_H
#define NGTCP2_CC_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <ngtcp2/ngtcp2.h>

#include "ngtcp2_pktns_id.h"
#include "ngtcp2_window_filter.h"

#define NGTCP2_LOSS_REDUCTION_FACTOR_BITS 1
#define NGTCP2_PERSISTENT_CONGESTION_THRESHOLD 3

typedef struct ngtcp2_log ngtcp2_log;
typedef struct ngtcp2_conn_stat ngtcp2_conn_stat;

/**
 * @struct
 *
 * :type:`ngtcp2_cc_pkt` is a convenient structure to include
 * acked/lost/sent packet.
 */
typedef struct ngtcp2_cc_pkt {
  /**
   * :member:`pkt_num` is the packet number
   */
  int64_t pkt_num;
  /**
   * :member:`pktlen` is the length of packet.
   */
  size_t pktlen;
  /**
   * :member:`pktns_id` is the ID of packet number space which this
   * packet belongs to.
   */
  ngtcp2_pktns_id pktns_id;
  /**
   * :member:`sent_ts` is the timestamp when packet is sent.
   */
  ngtcp2_tstamp sent_ts;
  /**
   * :member:`lost` is the number of bytes lost when this packet was
   * sent.
   */
  uint64_t lost;
  /**
   * :member:`tx_in_flight` is the bytes in flight when this packet
   * was sent.
   */
  uint64_t tx_in_flight;
  /**
   * :member:`is_app_limited` is nonzero if the connection is
   * app-limited when this packet was sent.
   */
  int is_app_limited;
} ngtcp2_cc_pkt;

/**
 * @struct
 *
 * :type:`ngtcp2_cc_ack` is a convenient structure which stores
 * acknowledged and lost bytes.
 */
typedef struct ngtcp2_cc_ack {
  /**
   * :member:`prior_bytes_in_flight` is the in-flight bytes before
   * processing this ACK.
   */
  uint64_t prior_bytes_in_flight;
  /**
   * :member:`bytes_delivered` is the number of bytes acknowledged.
   */
  uint64_t bytes_delivered;
  /**
   * :member:`bytes_lost` is the number of bytes declared lost.
   */
  uint64_t bytes_lost;
  /**
   * :member:`pkt_delivered` is the cumulative acknowledged bytes when
   * the last packet acknowledged by this ACK was sent.
   */
  uint64_t pkt_delivered;
  /**
   * :member:`largest_pkt_sent_ts` is the time when the largest
   * acknowledged packet was sent.  It is UINT64_MAX if it is unknown.
   */
  ngtcp2_tstamp largest_pkt_sent_ts;
  /**
   * :member:`rtt` is the RTT sample.  It is UINT64_MAX if no RTT
   * sample is available.
   */
  ngtcp2_duration rtt;
} ngtcp2_cc_ack;

typedef struct ngtcp2_cc ngtcp2_cc;

/**
 * @functypedef
 *
 * :type:`ngtcp2_cc_on_pkt_acked` is a callback function which is
 * called with an acknowledged packet.
 */
typedef void (*ngtcp2_cc_on_pkt_acked)(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                                       const ngtcp2_cc_pkt *pkt,
                                       ngtcp2_tstamp ts);

/**
 * @functypedef
 *
 * :type:`ngtcp2_cc_on_pkt_lost` is a callback function which is
 * called with a lost packet.
 */
typedef void (*ngtcp2_cc_on_pkt_lost)(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                                      const ngtcp2_cc_pkt *pkt,
                                      ngtcp2_tstamp ts);
/**
 * @functypedef
 *
 * :type:`ngtcp2_cc_congestion_event` is a callback function which is
 * called when congestion event happens (e.g., when packet is lost).
 */
typedef void (*ngtcp2_cc_congestion_event)(ngtcp2_cc *cc,
                                           ngtcp2_conn_stat *cstat,
                                           ngtcp2_tstamp sent_ts,
                                           ngtcp2_tstamp ts);

/**
 * @functypedef
 *
 * :type:`ngtcp2_cc_on_spurious_congestion` is a callback function
 * which is called when a spurious congestion is detected.
 */
typedef void (*ngtcp2_cc_on_spurious_congestion)(ngtcp2_cc *cc,
                                                 ngtcp2_conn_stat *cstat,
                                                 ngtcp2_tstamp ts);

/**
 * @functypedef
 *
 * :type:`ngtcp2_cc_on_persistent_congestion` is a callback function
 * which is called when persistent congestion is established.
 */
typedef void (*ngtcp2_cc_on_persistent_congestion)(ngtcp2_cc *cc,
                                                   ngtcp2_conn_stat *cstat,
                                                   ngtcp2_tstamp ts);

/**
 * @functypedef
 *
 * :type:`ngtcp2_cc_on_ack_recv` is a callback function which is
 * called when an acknowledgement is received.
 */
typedef void (*ngtcp2_cc_on_ack_recv)(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                                      const ngtcp2_cc_ack *ack,
                                      ngtcp2_tstamp ts);

/**
 * @functypedef
 *
 * :type:`ngtcp2_cc_on_pkt_sent` is a callback function which is
 * called when an ack-eliciting packet is sent.
 */
typedef void (*ngtcp2_cc_on_pkt_sent)(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                                      const ngtcp2_cc_pkt *pkt);

/**
 * @functypedef
 *
 * :type:`ngtcp2_cc_new_rtt_sample` is a callback function which is
 * called when new RTT sample is obtained.
 */
typedef void (*ngtcp2_cc_new_rtt_sample)(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                                         ngtcp2_tstamp ts);

/**
 * @functypedef
 *
 * :type:`ngtcp2_cc_reset` is a callback function which is called when
 * congestion state must be reset.
 */
typedef void (*ngtcp2_cc_reset)(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                                ngtcp2_tstamp ts);

/**
 * @enum
 *
 * :type:`ngtcp2_cc_event_type` defines congestion control events.
 */
typedef enum ngtcp2_cc_event_type {
  /**
   * :enum:`NGTCP2_CC_EVENT_TX_START` occurs when ack-eliciting packet
   * is sent and no other ack-eliciting packet is present.
   */
  NGTCP2_CC_EVENT_TYPE_TX_START
} ngtcp2_cc_event_type;

/**
 * @functypedef
 *
 * :type:`ngtcp2_cc_event` is a callback function which is called when
 * a specific event happens.
 */
typedef void (*ngtcp2_cc_event)(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                                ngtcp2_cc_event_type event, ngtcp2_tstamp ts);

/**
 * @struct
 *
 * :type:`ngtcp2_cc` is congestion control algorithm interface shared
 * by implementations.  All callback functions are optional.
 */
typedef struct ngtcp2_cc {
  /**
   * :member:`log` is ngtcp2 library internal logger.
   */
  ngtcp2_log *log;
  /**
   * :member:`on_pkt_acked` is a callback function which is called
   * when a packet is acknowledged.
   */
  ngtcp2_cc_on_pkt_acked on_pkt_acked;
  /**
   * :member:`on_pkt_lost` is a callback function which is called when
   * a packet is lost.
   */
  ngtcp2_cc_on_pkt_lost on_pkt_lost;
  /**
   * :member:`congestion_event` is a callback function which is called
   * when congestion event happens (.e.g, packet is lost).
   */
  ngtcp2_cc_congestion_event congestion_event;
  /**
   * :member:`on_spurious_congestion` is a callback function which is
   * called when a spurious congestion is detected.
   */
  ngtcp2_cc_on_spurious_congestion on_spurious_congestion;
  /**
   * :member:`on_persistent_congestion` is a callback function which
   * is called when persistent congestion is established.
   */
  ngtcp2_cc_on_persistent_congestion on_persistent_congestion;
  /**
   * :member:`on_ack_recv` is a callback function which is called when
   * an acknowledgement is received.
   */
  ngtcp2_cc_on_ack_recv on_ack_recv;
  /**
   * :member:`on_pkt_sent` is a callback function which is called when
   * ack-eliciting packet is sent.
   */
  ngtcp2_cc_on_pkt_sent on_pkt_sent;
  /**
   * :member:`new_rtt_sample` is a callback function which is called
   * when new RTT sample is obtained.
   */
  ngtcp2_cc_new_rtt_sample new_rtt_sample;
  /**
   * :member:`reset` is a callback function which is called when
   * congestion control state must be reset.
   */
  ngtcp2_cc_reset reset;
  /**
   * :member:`event` is a callback function which is called when a
   * specific event happens.
   */
  ngtcp2_cc_event event;
} ngtcp2_cc;

/*
 * ngtcp2_cc_compute_initcwnd computes initial cwnd.
 */
uint64_t ngtcp2_cc_compute_initcwnd(size_t max_packet_size);

ngtcp2_cc_pkt *ngtcp2_cc_pkt_init(ngtcp2_cc_pkt *pkt, int64_t pkt_num,
                                  size_t pktlen, ngtcp2_pktns_id pktns_id,
                                  ngtcp2_tstamp sent_ts, uint64_t lost,
                                  uint64_t tx_in_flight, int is_app_limited);

/* ngtcp2_cc_reno is the RENO congestion controller. */
typedef struct ngtcp2_cc_reno {
  ngtcp2_cc cc;
  ngtcp2_window_filter delivery_rate_sec_filter;
  uint64_t ack_count;
  uint64_t target_cwnd;
  uint64_t pending_add;
} ngtcp2_cc_reno;

void ngtcp2_cc_reno_init(ngtcp2_cc_reno *reno, ngtcp2_log *log);

void ngtcp2_cc_reno_cc_on_pkt_acked(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                                    const ngtcp2_cc_pkt *pkt, ngtcp2_tstamp ts);

void ngtcp2_cc_reno_cc_congestion_event(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                                        ngtcp2_tstamp sent_ts,
                                        ngtcp2_tstamp ts);

void ngtcp2_cc_reno_cc_on_persistent_congestion(ngtcp2_cc *cc,
                                                ngtcp2_conn_stat *cstat,
                                                ngtcp2_tstamp ts);

void ngtcp2_cc_reno_cc_on_ack_recv(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                                   const ngtcp2_cc_ack *ack, ngtcp2_tstamp ts);

void ngtcp2_cc_reno_cc_reset(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                             ngtcp2_tstamp ts);

/* ngtcp2_cc_cubic is CUBIC congestion controller. */
typedef struct ngtcp2_cc_cubic {
  ngtcp2_cc cc;
  ngtcp2_window_filter delivery_rate_sec_filter;
  uint64_t ack_count;
  uint64_t target_cwnd;
  uint64_t w_last_max;
  uint64_t w_tcp;
  uint64_t origin_point;
  ngtcp2_tstamp epoch_start;
  uint64_t k;
  /* prior stores the congestion state when a congestion event occurs
     in order to restore the state when it turns out that the event is
     spurious. */
  struct {
    uint64_t cwnd;
    uint64_t ssthresh;
    uint64_t w_last_max;
    uint64_t w_tcp;
    uint64_t origin_point;
    ngtcp2_tstamp epoch_start;
    uint64_t k;
  } prior;
  /* HyStart++ variables */
  size_t rtt_sample_count;
  uint64_t current_round_min_rtt;
  uint64_t last_round_min_rtt;
  int64_t window_end;
  uint64_t pending_add;
  uint64_t pending_w_add;
} ngtcp2_cc_cubic;

void ngtcp2_cc_cubic_init(ngtcp2_cc_cubic *cc, ngtcp2_log *log);

void ngtcp2_cc_cubic_cc_on_pkt_acked(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                                     const ngtcp2_cc_pkt *pkt,
                                     ngtcp2_tstamp ts);

void ngtcp2_cc_cubic_cc_congestion_event(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                                         ngtcp2_tstamp sent_ts,
                                         ngtcp2_tstamp ts);

void ngtcp2_cc_cubic_cc_on_spurious_congestion(ngtcp2_cc *ccx,
                                               ngtcp2_conn_stat *cstat,
                                               ngtcp2_tstamp ts);

void ngtcp2_cc_cubic_cc_on_persistent_congestion(ngtcp2_cc *cc,
                                                 ngtcp2_conn_stat *cstat,
                                                 ngtcp2_tstamp ts);

void ngtcp2_cc_cubic_cc_on_ack_recv(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                                    const ngtcp2_cc_ack *ack, ngtcp2_tstamp ts);

void ngtcp2_cc_cubic_cc_on_pkt_sent(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                                    const ngtcp2_cc_pkt *pkt);

void ngtcp2_cc_cubic_cc_new_rtt_sample(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                                       ngtcp2_tstamp ts);

void ngtcp2_cc_cubic_cc_reset(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                              ngtcp2_tstamp ts);

void ngtcp2_cc_cubic_cc_event(ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                              ngtcp2_cc_event_type event, ngtcp2_tstamp ts);

uint64_t ngtcp2_cbrt(uint64_t n);

#endif /* NGTCP2_CC_H */
