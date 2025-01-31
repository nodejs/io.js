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
#ifndef NGTCP2_LOG_H
#define NGTCP2_LOG_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <ngtcp2/ngtcp2.h>

#include "ngtcp2_pkt.h"

typedef struct ngtcp2_log {
  /* log_printf is a sink to write log.  NULL means no logging
     output. */
  ngtcp2_printf log_printf;
  /* events is an event filter.  Only events set in this field are
     emitted. */
  uint8_t events;
  /* ts is the time point used to write time delta in the log. */
  ngtcp2_tstamp ts;
  /* last_ts is the most recent time point that this object is
     told. */
  ngtcp2_tstamp last_ts;
  /* user_data is user-defined opaque data which is passed to
     log_pritnf. */
  void *user_data;
  /* scid is SCID encoded as NULL-terminated hex string. */
  uint8_t scid[NGTCP2_MAX_CIDLEN * 2 + 1];
} ngtcp2_log;

/**
 * @enum
 *
 * :type:`ngtcp2_log_event` defines an event of ngtcp2 library
 * internal logger.
 */
typedef enum ngtcp2_log_event {
  /**
   * :enum:`NGTCP2_LOG_EVENT_NONE` represents no event.
   */
  NGTCP2_LOG_EVENT_NONE,
  /**
   * :enum:`NGTCP2_LOG_EVENT_CON` is a connection (catch-all) event
   */
  NGTCP2_LOG_EVENT_CON = 0x1,
  /**
   * :enum:`NGTCP2_LOG_EVENT_PKT` is a packet event.
   */
  NGTCP2_LOG_EVENT_PKT = 0x2,
  /**
   * :enum:`NGTCP2_LOG_EVENT_FRM` is a QUIC frame event.
   */
  NGTCP2_LOG_EVENT_FRM = 0x4,
  /**
   * :enum:`NGTCP2_LOG_EVENT_LDC` is a loss detection and congestion
   * control event.
   */
  NGTCP2_LOG_EVENT_LDC = 0x8,
  /**
   * :enum:`NGTCP2_LOG_EVENT_CRY` is a crypto event.
   */
  NGTCP2_LOG_EVENT_CRY = 0x10,
  /**
   * :enum:`NGTCP2_LOG_EVENT_PTV` is a path validation event.
   */
  NGTCP2_LOG_EVENT_PTV = 0x20,
  /**
   * :enum:`NGTCP2_LOG_EVENT_CCA` is a congestion controller algorithm
   * event.
   */
  NGTCP2_LOG_EVENT_CCA = 0x40,
} ngtcp2_log_event;

void ngtcp2_log_init(ngtcp2_log *log, const ngtcp2_cid *scid,
                     ngtcp2_printf log_printf, ngtcp2_tstamp ts,
                     void *user_data);

void ngtcp2_log_rx_fr(ngtcp2_log *log, const ngtcp2_pkt_hd *hd,
                      const ngtcp2_frame *fr);
void ngtcp2_log_tx_fr(ngtcp2_log *log, const ngtcp2_pkt_hd *hd,
                      const ngtcp2_frame *fr);

void ngtcp2_log_rx_vn(ngtcp2_log *log, const ngtcp2_pkt_hd *hd,
                      const uint32_t *sv, size_t nsv);

void ngtcp2_log_rx_sr(ngtcp2_log *log, const ngtcp2_pkt_stateless_reset *sr);

void ngtcp2_log_remote_tp(ngtcp2_log *log,
                          const ngtcp2_transport_params *params);

void ngtcp2_log_pkt_lost(ngtcp2_log *log, int64_t pkt_num, uint8_t type,
                         uint8_t flags, ngtcp2_tstamp sent_ts);

void ngtcp2_log_rx_pkt_hd(ngtcp2_log *log, const ngtcp2_pkt_hd *hd);

void ngtcp2_log_tx_pkt_hd(ngtcp2_log *log, const ngtcp2_pkt_hd *hd);

void ngtcp2_log_tx_cancel(ngtcp2_log *log, const ngtcp2_pkt_hd *hd);

/**
 * @function
 *
 * `ngtcp2_log_info` writes info level log.
 */
void ngtcp2_log_info(ngtcp2_log *log, ngtcp2_log_event ev, const char *fmt,
                     ...);

#endif /* !defined(NGTCP2_LOG_H) */
