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
#include "ngtcp2_rst.h"

#include <assert.h>

#include "ngtcp2_rtb.h"
#include "ngtcp2_cc.h"
#include "ngtcp2_macro.h"
#include "ngtcp2_conn_stat.h"

void ngtcp2_rs_init(ngtcp2_rs *rs) {
  rs->interval = UINT64_MAX;
  rs->delivered = 0;
  rs->prior_delivered = 0;
  rs->prior_ts = 0;
  rs->tx_in_flight = 0;
  rs->lost = 0;
  rs->prior_lost = 0;
  rs->send_elapsed = 0;
  rs->ack_elapsed = 0;
  rs->is_app_limited = 0;
}

void ngtcp2_rst_init(ngtcp2_rst *rst) {
  ngtcp2_rs_init(&rst->rs);
  ngtcp2_window_filter_init(&rst->wf, 12);
  rst->delivered = 0;
  rst->delivered_ts = 0;
  rst->first_sent_ts = 0;
  rst->app_limited = 0;
  rst->next_round_delivered = 0;
  rst->round_count = 0;
  rst->is_cwnd_limited = 0;
  rst->lost = 0;
}

void ngtcp2_rst_on_pkt_sent(ngtcp2_rst *rst, ngtcp2_rtb_entry *ent,
                            const ngtcp2_conn_stat *cstat) {
  if (cstat->bytes_in_flight == 0) {
    rst->first_sent_ts = rst->delivered_ts = ent->ts;
  }
  ent->rst.first_sent_ts = rst->first_sent_ts;
  ent->rst.delivered_ts = rst->delivered_ts;
  ent->rst.delivered = rst->delivered;
  ent->rst.is_app_limited = rst->app_limited != 0;
  ent->rst.tx_in_flight = cstat->bytes_in_flight + ent->pktlen;
  ent->rst.lost = rst->lost;
}

void ngtcp2_rst_on_ack_recv(ngtcp2_rst *rst, ngtcp2_conn_stat *cstat,
                            uint64_t pkt_delivered) {
  ngtcp2_rs *rs = &rst->rs;
  uint64_t rate;

  if (rst->app_limited && rst->delivered > rst->app_limited) {
    rst->app_limited = 0;
  }

  if (pkt_delivered >= rst->next_round_delivered) {
    rst->next_round_delivered = pkt_delivered;
    ++rst->round_count;
  }

  if (rs->prior_ts == 0) {
    return;
  }

  rs->interval = ngtcp2_max(rs->send_elapsed, rs->ack_elapsed);

  rs->delivered = rst->delivered - rs->prior_delivered;
  rs->lost = rst->lost - rs->prior_lost;

  if (rs->interval < cstat->min_rtt) {
    rs->interval = UINT64_MAX;
    return;
  }

  if (!rs->interval) {
    return;
  }

  rate = rs->delivered * NGTCP2_SECONDS / rs->interval;

  if (rate > ngtcp2_window_filter_get_best(&rst->wf) || !rst->app_limited) {
    ngtcp2_window_filter_update(&rst->wf, rate, rst->round_count);
    cstat->delivery_rate_sec = ngtcp2_window_filter_get_best(&rst->wf);
  }
}

void ngtcp2_rst_update_rate_sample(ngtcp2_rst *rst, const ngtcp2_rtb_entry *ent,
                                   ngtcp2_tstamp ts) {
  ngtcp2_rs *rs = &rst->rs;

  rst->delivered += ent->pktlen;
  rst->delivered_ts = ts;

  if (ent->rst.delivered > rs->prior_delivered) {
    rs->prior_delivered = ent->rst.delivered;
    rs->prior_ts = ent->rst.delivered_ts;
    rs->is_app_limited = ent->rst.is_app_limited;
    rs->send_elapsed = ent->ts - ent->rst.first_sent_ts;
    rs->ack_elapsed = rst->delivered_ts - ent->rst.delivered_ts;
    rs->tx_in_flight = ent->rst.tx_in_flight;
    rs->prior_lost = ent->rst.lost;
    rst->first_sent_ts = ent->ts;
  }
}

void ngtcp2_rst_update_app_limited(ngtcp2_rst *rst, ngtcp2_conn_stat *cstat) {
  (void)rst;
  (void)cstat;
  /* TODO Not implemented */
}
