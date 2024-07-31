/*
 * ngtcp2
 *
 * Copyright (c) 2017 ngtcp2 contributors
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
#ifndef NGTCP2_STRM_H
#define NGTCP2_STRM_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <ngtcp2/ngtcp2.h>

#include "ngtcp2_rob.h"
#include "ngtcp2_map.h"
#include "ngtcp2_gaptr.h"
#include "ngtcp2_ksl.h"
#include "ngtcp2_pq.h"
#include "ngtcp2_pkt.h"

typedef struct ngtcp2_frame_chain ngtcp2_frame_chain;

/* NGTCP2_STRM_FLAG_NONE indicates that no flag is set. */
#define NGTCP2_STRM_FLAG_NONE 0x00u
/* NGTCP2_STRM_FLAG_SHUT_RD indicates that further reception of stream
   data is not allowed. */
#define NGTCP2_STRM_FLAG_SHUT_RD 0x01u
/* NGTCP2_STRM_FLAG_SHUT_WR indicates that further transmission of
   stream data is not allowed. */
#define NGTCP2_STRM_FLAG_SHUT_WR 0x02u
#define NGTCP2_STRM_FLAG_SHUT_RDWR                                             \
  (NGTCP2_STRM_FLAG_SHUT_RD | NGTCP2_STRM_FLAG_SHUT_WR)
/* NGTCP2_STRM_FLAG_RESET_STREAM indicates that RESET_STREAM is sent
   from the local endpoint.  In this case, NGTCP2_STRM_FLAG_SHUT_WR is
   also set. */
#define NGTCP2_STRM_FLAG_RESET_STREAM 0x04u
/* NGTCP2_STRM_FLAG_RESET_STREAM_RECVED indicates that RESET_STREAM is
   received from the remote endpoint.  In this case,
   NGTCP2_STRM_FLAG_SHUT_RD is also set. */
#define NGTCP2_STRM_FLAG_RESET_STREAM_RECVED 0x08u
/* NGTCP2_STRM_FLAG_STOP_SENDING indicates that STOP_SENDING is sent
   from the local endpoint. */
#define NGTCP2_STRM_FLAG_STOP_SENDING 0x10u
/* NGTCP2_STRM_FLAG_RESET_STREAM_ACKED indicates that the outgoing
   RESET_STREAM is acknowledged by peer. */
#define NGTCP2_STRM_FLAG_RESET_STREAM_ACKED 0x20u
/* NGTCP2_STRM_FLAG_FIN_ACKED indicates that a STREAM with FIN bit set
   is acknowledged by a remote endpoint. */
#define NGTCP2_STRM_FLAG_FIN_ACKED 0x40u
/* NGTCP2_STRM_FLAG_ANY_ACKED indicates that any portion of stream
   data, including 0 length segment, is acknowledged. */
#define NGTCP2_STRM_FLAG_ANY_ACKED 0x80u
/* NGTCP2_STRM_FLAG_APP_ERROR_CODE_SET indicates that app_error_code
   field is set.  This resolves the ambiguity that the initial
   app_error_code value 0 might be a proper application error code.
   In this case, without this flag, we are unable to distinguish
   assigned value from unassigned one.  */
#define NGTCP2_STRM_FLAG_APP_ERROR_CODE_SET 0x100u
/* NGTCP2_STRM_FLAG_SEND_STOP_SENDING is set when STOP_SENDING frame
   should be sent. */
#define NGTCP2_STRM_FLAG_SEND_STOP_SENDING 0x200u
/* NGTCP2_STRM_FLAG_SEND_RESET_STREAM is set when RESET_STREAM frame
   should be sent. */
#define NGTCP2_STRM_FLAG_SEND_RESET_STREAM 0x400u
/* NGTCP2_STRM_FLAG_STOP_SENDING_RECVED indicates that STOP_SENDING is
   received from the remote endpoint.  In this case,
   NGTCP2_STRM_FLAG_SHUT_WR is also set. */
#define NGTCP2_STRM_FLAG_STOP_SENDING_RECVED 0x800u

typedef struct ngtcp2_strm ngtcp2_strm;

struct ngtcp2_strm {
  union {
    struct {
      ngtcp2_pq_entry pe;
      uint64_t cycle;
      ngtcp2_objalloc *frc_objalloc;

      struct {
        /* acked_offset tracks acknowledged outgoing data. */
        ngtcp2_gaptr *acked_offset;
        /* cont_acked_offset is the offset that all data up to this offset
           is acknowledged by a remote endpoint.  It is used until the
           remote endpoint acknowledges data in out-of-order.  After that,
           acked_offset is used instead. */
        uint64_t cont_acked_offset;
        /* streamfrq contains STREAM or CRYPTO frame for
           retransmission.  The flow control credits have been paid
           when they are transmitted first time.  There are no
           restriction regarding flow control for retransmission. */
        ngtcp2_ksl *streamfrq;
        /* offset is the next offset of outgoing data.  In other words, it
           is the number of bytes sent in this stream without
           duplication. */
        uint64_t offset;
        /* max_tx_offset is the maximum offset that local endpoint can
           send for this stream. */
        uint64_t max_offset;
        /* last_blocked_offset is the largest offset where the
           transmission of stream data is blocked. */
        uint64_t last_blocked_offset;
        /* last_max_stream_data_ts is the timestamp when last
           MAX_STREAM_DATA frame is sent. */
        ngtcp2_tstamp last_max_stream_data_ts;
        /* loss_count is the number of packets that contain STREAM
           frame for this stream and are declared to be lost.  It may
           include the spurious losses.  It does not include a packet
           whose contents have been reclaimed for PTO and which is
           later declared to be lost.  Those data are not blocked by
           the flow control and will be sent immediately if no other
           restrictions are applied. */
        size_t loss_count;
        /* last_lost_pkt_num is the packet number of the packet that
           is counted to loss_count.  It is used to avoid to count
           multiple STREAM frames in one lost packet. */
        int64_t last_lost_pkt_num;
        /* stop_sending_app_error_code is the application specific
           error code that is sent along with STOP_SENDING. */
        uint64_t stop_sending_app_error_code;
        /* reset_stream_app_error_code is the application specific
           error code that is sent along with RESET_STREAM. */
        uint64_t reset_stream_app_error_code;
      } tx;

      struct {
        /* rob is the reorder buffer for incoming stream data.  The data
           received in out of order is buffered and sorted by its offset
           in this object. */
        ngtcp2_rob *rob;
        /* cont_offset is the largest offset of consecutive data.  It is
           used until the endpoint receives out-of-order data.  After
           that, rob is used to track the offset and data. */
        uint64_t cont_offset;
        /* last_offset is the largest offset of stream data received for
           this stream. */
        uint64_t last_offset;
        /* max_offset is the maximum offset that remote endpoint can send
           to this stream. */
        uint64_t max_offset;
        /* unsent_max_offset is the maximum offset that remote endpoint
           can send to this stream, and it is not notified to the remote
           endpoint.  unsent_max_offset >= max_offset must be hold. */
        uint64_t unsent_max_offset;
        /* window is the stream-level flow control window size. */
        uint64_t window;
      } rx;

      const ngtcp2_mem *mem;
      int64_t stream_id;
      void *stream_user_data;
      /* flags is bit-wise OR of zero or more of NGTCP2_STRM_FLAG_*. */
      uint32_t flags;
      /* app_error_code is an error code the local endpoint sent in
         RESET_STREAM or STOP_SENDING, or received from a remote endpoint
         in RESET_STREAM or STOP_SENDING.  First application error code is
         chosen and when set, NGTCP2_STRM_FLAG_APP_ERROR_CODE_SET flag is
         set in flags field. */
      uint64_t app_error_code;
    };

    ngtcp2_opl_entry oplent;
  };
};

/*
 * ngtcp2_strm_init initializes |strm|.
 */
void ngtcp2_strm_init(ngtcp2_strm *strm, int64_t stream_id, uint32_t flags,
                      uint64_t max_rx_offset, uint64_t max_tx_offset,
                      void *stream_user_data, ngtcp2_objalloc *frc_objalloc,
                      const ngtcp2_mem *mem);

/*
 * ngtcp2_strm_free deallocates memory allocated for |strm|.  This
 * function does not free the memory pointed by |strm| itself.
 */
void ngtcp2_strm_free(ngtcp2_strm *strm);

/*
 * ngtcp2_strm_rx_offset returns the minimum offset of stream data
 * which is not received yet.
 */
uint64_t ngtcp2_strm_rx_offset(ngtcp2_strm *strm);

/*
 * ngtcp2_strm_recv_reordering handles reordered data.
 *
 * It returns 0 if it succeeds, or one of the following negative error
 * codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory
 */
int ngtcp2_strm_recv_reordering(ngtcp2_strm *strm, const uint8_t *data,
                                size_t datalen, uint64_t offset);

/*
 * ngtcp2_strm_update_rx_offset tells that data up to offset bytes are
 * received in order.
 */
void ngtcp2_strm_update_rx_offset(ngtcp2_strm *strm, uint64_t offset);

/*
 * ngtcp2_strm_discard_reordered_data discards all buffered reordered
 * data.
 */
void ngtcp2_strm_discard_reordered_data(ngtcp2_strm *strm);

/*
 * ngtcp2_strm_shutdown shutdowns |strm|.  |flags| should be
 * NGTCP2_STRM_FLAG_SHUT_RD, and/or NGTCP2_STRM_FLAG_SHUT_WR.
 */
void ngtcp2_strm_shutdown(ngtcp2_strm *strm, uint32_t flags);

/*
 * ngtcp2_strm_streamfrq_push pushes |frc| to streamfrq for
 * retransmission.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory
 */
int ngtcp2_strm_streamfrq_push(ngtcp2_strm *strm, ngtcp2_frame_chain *frc);

/*
 * ngtcp2_strm_streamfrq_pop pops the first ngtcp2_frame_chain and
 * assigns it to |*pfrc|.  This function splits into or merges several
 * ngtcp2_frame_chain objects so that the returned ngtcp2_frame_chain
 * has at most |left| data length.  If there is no frames to send,
 * this function returns 0 and |*pfrc| is NULL.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory
 */
int ngtcp2_strm_streamfrq_pop(ngtcp2_strm *strm, ngtcp2_frame_chain **pfrc,
                              size_t left);

/*
 * ngtcp2_strm_streamfrq_unacked_offset returns the smallest offset of
 * unacknowledged stream data held in strm->tx.streamfrq.
 */
uint64_t ngtcp2_strm_streamfrq_unacked_offset(ngtcp2_strm *strm);

/*
 * ngtcp2_strm_streamfrq_top returns the first ngtcp2_frame_chain.
 * The queue must not be empty.
 */
ngtcp2_frame_chain *ngtcp2_strm_streamfrq_top(ngtcp2_strm *strm);

/*
 * ngtcp2_strm_streamfrq_empty returns nonzero if streamfrq is empty.
 */
int ngtcp2_strm_streamfrq_empty(ngtcp2_strm *strm);

/*
 * ngtcp2_strm_streamfrq_clear removes all frames from streamfrq.
 */
void ngtcp2_strm_streamfrq_clear(ngtcp2_strm *strm);

/*
 * ngtcp2_strm_is_tx_queued returns nonzero if |strm| is queued.
 */
int ngtcp2_strm_is_tx_queued(ngtcp2_strm *strm);

/*
 * ngtcp2_strm_is_all_tx_data_acked returns nonzero if all outgoing
 * data for |strm| which have sent so far have been acknowledged.
 */
int ngtcp2_strm_is_all_tx_data_acked(ngtcp2_strm *strm);

/*
 * ngtcp2_strm_is_all_tx_data_fin_acked behaves like
 * ngtcp2_strm_is_all_tx_data_acked, but it also requires that STREAM
 * frame with fin bit set is acknowledged.
 */
int ngtcp2_strm_is_all_tx_data_fin_acked(ngtcp2_strm *strm);

/*
 * ngtcp2_strm_get_unacked_range_after returns the range that is not
 * acknowledged yet and intersects or comes after |offset|.
 */
ngtcp2_range ngtcp2_strm_get_unacked_range_after(ngtcp2_strm *strm,
                                                 uint64_t offset);

/*
 * ngtcp2_strm_get_acked_offset returns offset, that is the data up to
 * this offset have been acknowledged by a remote endpoint.  It
 * returns 0 if no data is acknowledged.
 */
uint64_t ngtcp2_strm_get_acked_offset(ngtcp2_strm *strm);

/*
 * ngtcp2_strm_ack_data tells |strm| that the data [offset,
 * offset+len) is acknowledged by a remote endpoint.
 */
int ngtcp2_strm_ack_data(ngtcp2_strm *strm, uint64_t offset, uint64_t len);

/*
 * ngtcp2_strm_set_app_error_code sets |app_error_code| to |strm| and
 * set NGTCP2_STRM_FLAG_APP_ERROR_CODE_SET flag.  If the flag is
 * already set, this function does nothing.
 */
void ngtcp2_strm_set_app_error_code(ngtcp2_strm *strm, uint64_t app_error_code);

/*
 * ngtcp2_strm_require_retransmit_reset_stream returns nonzero if
 * RESET_STREAM frame should be retransmitted.
 */
int ngtcp2_strm_require_retransmit_reset_stream(ngtcp2_strm *strm);

/*
 * ngtcp2_strm_require_retransmit_stop_sending returns nonzero if
 * STOP_SENDING frame should be retransmitted.
 */
int ngtcp2_strm_require_retransmit_stop_sending(ngtcp2_strm *strm);

/*
 * ngtcp2_strm_require_retransmit_max_stream_data returns nonzero if
 * MAX_STREAM_DATA frame should be retransmitted.
 */
int ngtcp2_strm_require_retransmit_max_stream_data(ngtcp2_strm *strm,
                                                   ngtcp2_max_stream_data *fr);

/*
 * ngtcp2_strm_require_retransmit_stream_data_blocked returns nonzero
 * if STREAM_DATA_BLOCKED frame frame should be retransmitted.
 */
int ngtcp2_strm_require_retransmit_stream_data_blocked(
    ngtcp2_strm *strm, ngtcp2_stream_data_blocked *fr);

#endif /* NGTCP2_STRM_H */
