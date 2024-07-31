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
#ifndef NGTCP2_RST_H
#define NGTCP2_RST_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <ngtcp2/ngtcp2.h>

#include "ngtcp2_window_filter.h"

typedef struct ngtcp2_rtb_entry ngtcp2_rtb_entry;
typedef struct ngtcp2_conn_stat ngtcp2_conn_stat;

/**
 * @struct
 *
 * ngtcp2_rs contains connection state for delivery rate estimation.
 */
typedef struct ngtcp2_rs {
  ngtcp2_duration interval;
  uint64_t delivered;
  uint64_t prior_delivered;
  ngtcp2_tstamp prior_ts;
  uint64_t tx_in_flight;
  uint64_t lost;
  uint64_t prior_lost;
  ngtcp2_duration send_elapsed;
  ngtcp2_duration ack_elapsed;
  int is_app_limited;
} ngtcp2_rs;

void ngtcp2_rs_init(ngtcp2_rs *rs);

/*
 * ngtcp2_rst implements delivery rate estimation described in
 * https://tools.ietf.org/html/draft-cheng-iccrg-delivery-rate-estimation-00
 */
typedef struct ngtcp2_rst {
  ngtcp2_rs rs;
  ngtcp2_window_filter wf;
  uint64_t delivered;
  ngtcp2_tstamp delivered_ts;
  ngtcp2_tstamp first_sent_ts;
  uint64_t app_limited;
  uint64_t next_round_delivered;
  uint64_t round_count;
  uint64_t lost;
  int is_cwnd_limited;
} ngtcp2_rst;

void ngtcp2_rst_init(ngtcp2_rst *rst);

void ngtcp2_rst_on_pkt_sent(ngtcp2_rst *rst, ngtcp2_rtb_entry *ent,
                            const ngtcp2_conn_stat *cstat);
void ngtcp2_rst_on_ack_recv(ngtcp2_rst *rst, ngtcp2_conn_stat *cstat,
                            uint64_t pkt_delivered);
void ngtcp2_rst_update_rate_sample(ngtcp2_rst *rst, const ngtcp2_rtb_entry *ent,
                                   ngtcp2_tstamp ts);
void ngtcp2_rst_update_app_limited(ngtcp2_rst *rst, ngtcp2_conn_stat *cstat);

#endif /* NGTCP2_RST_H */
