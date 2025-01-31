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
#ifndef NGTCP2_QLOG_H
#define NGTCP2_QLOG_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <ngtcp2/ngtcp2.h>

#include "ngtcp2_pkt.h"
#include "ngtcp2_cc.h"
#include "ngtcp2_buf.h"
#include "ngtcp2_rtb.h"

/* NGTCP2_QLOG_BUFLEN is the length of heap allocated buffer for
   qlog. */
#define NGTCP2_QLOG_BUFLEN 4096

typedef enum ngtcp2_qlog_side {
  NGTCP2_QLOG_SIDE_LOCAL,
  NGTCP2_QLOG_SIDE_REMOTE,
} ngtcp2_qlog_side;

typedef struct ngtcp2_qlog {
  /* write is a callback function to write qlog. */
  ngtcp2_qlog_write write;
  /* ts is the initial timestamp */
  ngtcp2_tstamp ts;
  /* last_ts is the timestamp observed last time. */
  ngtcp2_tstamp last_ts;
  /* buf is a heap allocated buffer to write exclusively
     packet_received and packet_sent. */
  ngtcp2_buf buf;
  /* user_data is an opaque pointer which is passed to write
     callback. */
  void *user_data;
} ngtcp2_qlog;

/*
 * ngtcp2_qlog_init initializes |qlog|.
 */
void ngtcp2_qlog_init(ngtcp2_qlog *qlog, ngtcp2_qlog_write write,
                      ngtcp2_tstamp ts, void *user_data);

/*
 * ngtcp2_qlog_start writes qlog preamble.
 */
void ngtcp2_qlog_start(ngtcp2_qlog *qlog, const ngtcp2_cid *odcid, int server);

/*
 * ngtcp2_qlog_end writes closing part of qlog.
 */
void ngtcp2_qlog_end(ngtcp2_qlog *qlog);

/*
 * ngtcp2_qlog_write_frame writes |fr| to qlog->buf.
 * ngtcp2_qlog_pkt_received_start or ngtcp2_qlog_pkt_sent_start must
 * be called before calling this function.
 */
void ngtcp2_qlog_write_frame(ngtcp2_qlog *qlog, const ngtcp2_frame *fr);

/*
 * ngtcp2_qlog_pkt_received_start starts to write packet_received
 * event.  It initializes qlog->buf.  It writes qlog to qlog->buf.
 * ngtcp2_qlog_pkt_received_end will flush the content of qlog->buf to
 * write callback.
 */
void ngtcp2_qlog_pkt_received_start(ngtcp2_qlog *qlog);

/*
 * ngtcp2_qlog_pkt_received_end ends packet_received event and sends
 * the content of qlog->buf to qlog->write callback.
 */
void ngtcp2_qlog_pkt_received_end(ngtcp2_qlog *qlog, const ngtcp2_pkt_hd *hd,
                                  size_t pktlen);

/*
 * ngtcp2_qlog_pkt_sent_start starts to write packet_sent event.  It
 * initializes qlog->buf.  It writes qlog to qlog->buf.
 * ngtcp2_qlog_pkt_sent_end will flush the content of qlog->buf to
 * write callback.
 */
void ngtcp2_qlog_pkt_sent_start(ngtcp2_qlog *qlog);

/*
 * ngtcp2_qlog_pkt_sent_end ends packet_sent event and sends the
 * content of qlog->buf to qlog->write callback.
 */
void ngtcp2_qlog_pkt_sent_end(ngtcp2_qlog *qlog, const ngtcp2_pkt_hd *hd,
                              size_t pktlen);

/*
 * ngtcp2_qlog_parameters_set_transport_params writes |params| to qlog
 * as parameters_set event.  |server| is nonzero if the local endpoint
 * is server.  If |local| is nonzero, it is "owner" field becomes
 * "local", otherwise "remote".
 */
void ngtcp2_qlog_parameters_set_transport_params(
  ngtcp2_qlog *qlog, const ngtcp2_transport_params *params, int server,
  ngtcp2_qlog_side side);

/*
 * ngtcp2_qlog_metrics_updated writes metrics_updated event of
 * recovery category.
 */
void ngtcp2_qlog_metrics_updated(ngtcp2_qlog *qlog,
                                 const ngtcp2_conn_stat *cstat);

/*
 * ngtcp2_qlog_pkt_lost writes packet_lost event.
 */
void ngtcp2_qlog_pkt_lost(ngtcp2_qlog *qlog, ngtcp2_rtb_entry *ent);

/*
 * ngtcp2_qlog_retry_pkt_received writes packet_received event for a
 * received Retry packet.
 */
void ngtcp2_qlog_retry_pkt_received(ngtcp2_qlog *qlog, const ngtcp2_pkt_hd *hd,
                                    const ngtcp2_pkt_retry *retry);

/*
 * ngtcp2_qlog_stateless_reset_pkt_received writes packet_received
 * event for a received Stateless Reset packet.
 */
void ngtcp2_qlog_stateless_reset_pkt_received(
  ngtcp2_qlog *qlog, const ngtcp2_pkt_stateless_reset *sr);

/*
 * ngtcp2_qlog_version_negotiation_pkt_received writes packet_received
 * event for a received Version Negotiation packet.
 */
void ngtcp2_qlog_version_negotiation_pkt_received(ngtcp2_qlog *qlog,
                                                  const ngtcp2_pkt_hd *hd,
                                                  const uint32_t *sv,
                                                  size_t nsv);

#endif /* !defined(NGTCP2_QLOG_H) */
