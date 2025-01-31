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
#include "ngtcp2_rtb.h"

#include <assert.h>
#include <string.h>

#include "ngtcp2_macro.h"
#include "ngtcp2_conn.h"
#include "ngtcp2_log.h"
#include "ngtcp2_vec.h"
#include "ngtcp2_cc.h"
#include "ngtcp2_rcvry.h"
#include "ngtcp2_rst.h"
#include "ngtcp2_unreachable.h"
#include "ngtcp2_tstamp.h"
#include "ngtcp2_frame_chain.h"

ngtcp2_objalloc_def(rtb_entry, ngtcp2_rtb_entry, oplent)

static void rtb_entry_init(ngtcp2_rtb_entry *ent, const ngtcp2_pkt_hd *hd,
                           ngtcp2_frame_chain *frc, ngtcp2_tstamp ts,
                           size_t pktlen, uint16_t flags) {
  memset(ent, 0, sizeof(*ent));

  ent->hd.pkt_num = hd->pkt_num;
  ent->hd.type = hd->type;
  ent->hd.flags = hd->flags;
  ent->frc = frc;
  ent->ts = ts;
  ent->lost_ts = UINT64_MAX;
  ent->pktlen = pktlen;
  ent->flags = flags;
}

int ngtcp2_rtb_entry_objalloc_new(ngtcp2_rtb_entry **pent,
                                  const ngtcp2_pkt_hd *hd,
                                  ngtcp2_frame_chain *frc, ngtcp2_tstamp ts,
                                  size_t pktlen, uint16_t flags,
                                  ngtcp2_objalloc *objalloc) {
  *pent = ngtcp2_objalloc_rtb_entry_get(objalloc);
  if (*pent == NULL) {
    return NGTCP2_ERR_NOMEM;
  }

  rtb_entry_init(*pent, hd, frc, ts, pktlen, flags);

  return 0;
}

void ngtcp2_rtb_entry_objalloc_del(ngtcp2_rtb_entry *ent,
                                   ngtcp2_objalloc *objalloc,
                                   ngtcp2_objalloc *frc_objalloc,
                                   const ngtcp2_mem *mem) {
  ngtcp2_frame_chain_list_objalloc_del(ent->frc, frc_objalloc, mem);

  ent->frc = NULL;

  ngtcp2_objalloc_rtb_entry_release(objalloc, ent);
}

void ngtcp2_rtb_init(ngtcp2_rtb *rtb, ngtcp2_rst *rst, ngtcp2_cc *cc,
                     int64_t cc_pkt_num, ngtcp2_log *log, ngtcp2_qlog *qlog,
                     ngtcp2_objalloc *rtb_entry_objalloc,
                     ngtcp2_objalloc *frc_objalloc, const ngtcp2_mem *mem) {
  rtb->rtb_entry_objalloc = rtb_entry_objalloc;
  rtb->frc_objalloc = frc_objalloc;
  ngtcp2_ksl_init(&rtb->ents, ngtcp2_ksl_int64_greater,
                  ngtcp2_ksl_int64_greater_search, sizeof(int64_t), mem);
  rtb->rst = rst;
  rtb->cc = cc;
  rtb->log = log;
  rtb->qlog = qlog;
  rtb->mem = mem;
  rtb->largest_acked_tx_pkt_num = -1;
  rtb->num_ack_eliciting = 0;
  rtb->num_retransmittable = 0;
  rtb->num_pto_eliciting = 0;
  rtb->probe_pkt_left = 0;
  rtb->cc_pkt_num = cc_pkt_num;
  rtb->cc_bytes_in_flight = 0;
  rtb->persistent_congestion_start_ts = UINT64_MAX;
  rtb->num_lost_pkts = 0;
  rtb->num_lost_pmtud_pkts = 0;
}

void ngtcp2_rtb_free(ngtcp2_rtb *rtb) {
  ngtcp2_ksl_it it;

  if (rtb == NULL) {
    return;
  }

  it = ngtcp2_ksl_begin(&rtb->ents);

  for (; !ngtcp2_ksl_it_end(&it); ngtcp2_ksl_it_next(&it)) {
    ngtcp2_rtb_entry_objalloc_del(ngtcp2_ksl_it_get(&it),
                                  rtb->rtb_entry_objalloc, rtb->frc_objalloc,
                                  rtb->mem);
  }

  ngtcp2_ksl_free(&rtb->ents);
}

static void rtb_on_add(ngtcp2_rtb *rtb, ngtcp2_rtb_entry *ent,
                       ngtcp2_conn_stat *cstat) {
  ngtcp2_rst_on_pkt_sent(rtb->rst, ent, cstat);

  assert(rtb->cc_pkt_num <= ent->hd.pkt_num);

  cstat->bytes_in_flight += ent->pktlen;
  rtb->cc_bytes_in_flight += ent->pktlen;

  ngtcp2_rst_update_app_limited(rtb->rst, cstat);

  if (ent->flags & NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING) {
    ++rtb->num_ack_eliciting;
  }

  if (ent->flags & NGTCP2_RTB_ENTRY_FLAG_RETRANSMITTABLE) {
    ++rtb->num_retransmittable;
  }

  if (ent->flags & NGTCP2_RTB_ENTRY_FLAG_PTO_ELICITING) {
    ++rtb->num_pto_eliciting;
  }
}

static size_t rtb_on_remove(ngtcp2_rtb *rtb, ngtcp2_rtb_entry *ent,
                            ngtcp2_conn_stat *cstat) {
  if (ent->flags & NGTCP2_RTB_ENTRY_FLAG_LOST_RETRANSMITTED) {
    assert(rtb->num_lost_pkts);
    --rtb->num_lost_pkts;

    if (ent->flags & NGTCP2_RTB_ENTRY_FLAG_PMTUD_PROBE) {
      assert(rtb->num_lost_pmtud_pkts);
      --rtb->num_lost_pmtud_pkts;
    }

    return 0;
  }

  if (ent->flags & NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING) {
    assert(rtb->num_ack_eliciting);
    --rtb->num_ack_eliciting;
  }

  if ((ent->flags & NGTCP2_RTB_ENTRY_FLAG_RETRANSMITTABLE) &&
      !(ent->flags & NGTCP2_RTB_ENTRY_FLAG_PTO_RECLAIMED)) {
    assert(rtb->num_retransmittable);
    --rtb->num_retransmittable;
  }

  if (ent->flags & NGTCP2_RTB_ENTRY_FLAG_PTO_ELICITING) {
    assert(rtb->num_pto_eliciting);
    --rtb->num_pto_eliciting;
  }

  if (rtb->cc_pkt_num <= ent->hd.pkt_num) {
    assert(cstat->bytes_in_flight >= ent->pktlen);
    cstat->bytes_in_flight -= ent->pktlen;

    assert(rtb->cc_bytes_in_flight >= ent->pktlen);
    rtb->cc_bytes_in_flight -= ent->pktlen;

    /* If PMTUD packet is lost, we do not report the lost bytes to the
       caller in order to ignore loss of PMTUD packet. */
    if (ent->flags & NGTCP2_RTB_ENTRY_FLAG_PMTUD_PROBE) {
      return 0;
    }

    return ent->pktlen;
  }

  return 0;
}

/* NGTCP2_RECLAIM_FLAG_NONE indicates that no flag is set. */
#define NGTCP2_RECLAIM_FLAG_NONE 0x00u
/* NGTCP2_RECLAIM_FLAG_ON_LOSS indicates that frames are reclaimed
   because of the packet loss.*/
#define NGTCP2_RECLAIM_FLAG_ON_LOSS 0x01u

/*
 * rtb_reclaim_frame copies and queues frames included in |ent| for
 * retransmission.  The frames are not deleted from |ent|.  It returns
 * the number of frames queued.  |flags| is bitwise OR of 0 or more of
 * NGTCP2_RECLAIM_FLAG_*.
 */
static ngtcp2_ssize rtb_reclaim_frame(ngtcp2_rtb *rtb, uint8_t flags,
                                      ngtcp2_conn *conn, ngtcp2_pktns *pktns,
                                      ngtcp2_rtb_entry *ent) {
  ngtcp2_frame_chain *frc, *nfrc, **pfrc = &pktns->tx.frq;
  ngtcp2_frame *fr;
  ngtcp2_strm *strm;
  ngtcp2_range gap, range;
  size_t num_reclaimed = 0;
  int rv;

  assert(ent->flags & NGTCP2_RTB_ENTRY_FLAG_RETRANSMITTABLE);

  /* TODO Reconsider the order of pfrc */
  for (frc = ent->frc; frc; frc = frc->next) {
    fr = &frc->fr;

    /* Check that a late ACK acknowledged this frame. */
    if (frc->binder &&
        (frc->binder->flags & NGTCP2_FRAME_CHAIN_BINDER_FLAG_ACK)) {
      continue;
    }

    switch (frc->fr.type) {
    case NGTCP2_FRAME_STREAM:
      strm = ngtcp2_conn_find_stream(conn, fr->stream.stream_id);
      if (strm == NULL || (strm->flags & NGTCP2_STRM_FLAG_RESET_STREAM)) {
        continue;
      }

      gap = ngtcp2_strm_get_unacked_range_after(strm, fr->stream.offset);

      range.begin = fr->stream.offset;
      range.end =
        fr->stream.offset + ngtcp2_vec_len(fr->stream.data, fr->stream.datacnt);
      range = ngtcp2_range_intersect(&range, &gap);

      if (ngtcp2_range_len(&range) == 0) {
        if (!fr->stream.fin) {
          /* 0 length STREAM frame with offset == 0 must be
             retransmitted if no non-empty data are sent to this
             stream, fin flag is not set, and no data in this stream
             are acknowledged. */
          if (fr->stream.offset != 0 || fr->stream.datacnt != 0 ||
              strm->tx.offset ||
              (strm->flags &
               (NGTCP2_STRM_FLAG_SHUT_WR | NGTCP2_STRM_FLAG_ANY_ACKED))) {
            continue;
          }
        } else if (strm->flags & NGTCP2_STRM_FLAG_FIN_ACKED) {
          continue;
        }
      }

      if ((flags & NGTCP2_RECLAIM_FLAG_ON_LOSS) &&
          ent->hd.pkt_num != strm->tx.last_lost_pkt_num) {
        strm->tx.last_lost_pkt_num = ent->hd.pkt_num;
        ++strm->tx.loss_count;
      }

      rv = ngtcp2_frame_chain_stream_datacnt_objalloc_new(
        &nfrc, fr->stream.datacnt, rtb->frc_objalloc, rtb->mem);
      if (rv != 0) {
        return rv;
      }

      nfrc->fr = *fr;
      ngtcp2_vec_copy(nfrc->fr.stream.data, fr->stream.data,
                      fr->stream.datacnt);

      rv = ngtcp2_strm_streamfrq_push(strm, nfrc);
      if (rv != 0) {
        ngtcp2_frame_chain_objalloc_del(nfrc, rtb->frc_objalloc, rtb->mem);
        return rv;
      }

      if (!ngtcp2_strm_is_tx_queued(strm)) {
        strm->cycle = ngtcp2_conn_tx_strmq_first_cycle(conn);

        rv = ngtcp2_conn_tx_strmq_push(conn, strm);
        if (rv != 0) {
          return rv;
        }
      }

      ++num_reclaimed;

      continue;
    case NGTCP2_FRAME_CRYPTO:
      /* Do not resend CRYPTO frame if the whole region it contains
         has been acknowledged */
      gap = ngtcp2_strm_get_unacked_range_after(&pktns->crypto.strm,
                                                fr->stream.offset);

      range.begin = fr->stream.offset;
      range.end =
        fr->stream.offset + ngtcp2_vec_len(fr->stream.data, fr->stream.datacnt);
      range = ngtcp2_range_intersect(&range, &gap);

      if (ngtcp2_range_len(&range) == 0) {
        continue;
      }

      rv = ngtcp2_frame_chain_stream_datacnt_objalloc_new(
        &nfrc, fr->stream.datacnt, rtb->frc_objalloc, rtb->mem);
      if (rv != 0) {
        return rv;
      }

      nfrc->fr = *fr;
      ngtcp2_vec_copy(nfrc->fr.stream.data, fr->stream.data,
                      fr->stream.datacnt);

      rv = ngtcp2_strm_streamfrq_push(&pktns->crypto.strm, nfrc);
      if (rv != 0) {
        assert(ngtcp2_err_is_fatal(rv));
        ngtcp2_frame_chain_objalloc_del(nfrc, rtb->frc_objalloc, rtb->mem);
        return rv;
      }

      ++num_reclaimed;

      continue;
    case NGTCP2_FRAME_NEW_TOKEN:
      rv = ngtcp2_frame_chain_new_token_objalloc_new(
        &nfrc, fr->new_token.token, fr->new_token.tokenlen, rtb->frc_objalloc,
        rtb->mem);
      if (rv != 0) {
        return rv;
      }

      rv = ngtcp2_bind_frame_chains(frc, nfrc, rtb->mem);
      if (rv != 0) {
        return rv;
      }

      ++num_reclaimed;

      nfrc->next = *pfrc;
      *pfrc = nfrc;
      pfrc = &nfrc->next;

      continue;
    case NGTCP2_FRAME_DATAGRAM:
    case NGTCP2_FRAME_DATAGRAM_LEN:
      continue;
    case NGTCP2_FRAME_RESET_STREAM:
      strm = ngtcp2_conn_find_stream(conn, fr->reset_stream.stream_id);
      if (strm == NULL || !ngtcp2_strm_require_retransmit_reset_stream(strm)) {
        continue;
      }

      break;
    case NGTCP2_FRAME_STOP_SENDING:
      strm = ngtcp2_conn_find_stream(conn, fr->stop_sending.stream_id);
      if (strm == NULL || !ngtcp2_strm_require_retransmit_stop_sending(strm)) {
        continue;
      }

      break;
    case NGTCP2_FRAME_MAX_STREAM_DATA:
      strm = ngtcp2_conn_find_stream(conn, fr->max_stream_data.stream_id);
      if (strm == NULL || !ngtcp2_strm_require_retransmit_max_stream_data(
                            strm, &fr->max_stream_data)) {
        continue;
      }

      break;
    case NGTCP2_FRAME_STREAM_DATA_BLOCKED:
      strm = ngtcp2_conn_find_stream(conn, fr->stream_data_blocked.stream_id);
      if (strm == NULL || !ngtcp2_strm_require_retransmit_stream_data_blocked(
                            strm, &fr->stream_data_blocked)) {
        continue;
      }

      break;
    }

    rv = ngtcp2_frame_chain_objalloc_new(&nfrc, rtb->frc_objalloc);
    if (rv != 0) {
      return rv;
    }

    nfrc->fr = *fr;

    rv = ngtcp2_bind_frame_chains(frc, nfrc, rtb->mem);
    if (rv != 0) {
      return rv;
    }

    ++num_reclaimed;

    nfrc->next = *pfrc;
    *pfrc = nfrc;
    pfrc = &nfrc->next;
  }

  return (ngtcp2_ssize)num_reclaimed;
}

/*
 * conn_process_lost_datagram calls ngtcp2_lost_datagram callback for
 * lost DATAGRAM frames.
 */
static int conn_process_lost_datagram(ngtcp2_conn *conn,
                                      ngtcp2_rtb_entry *ent) {
  ngtcp2_frame_chain *frc;
  int rv;

  for (frc = ent->frc; frc; frc = frc->next) {
    switch (frc->fr.type) {
    case NGTCP2_FRAME_DATAGRAM:
    case NGTCP2_FRAME_DATAGRAM_LEN:
      assert(conn->callbacks.lost_datagram);

      rv = conn->callbacks.lost_datagram(conn, frc->fr.datagram.dgram_id,
                                         conn->user_data);
      if (rv != 0) {
        return NGTCP2_ERR_CALLBACK_FAILURE;
      }

      break;
    }
  }

  return 0;
}

static int rtb_on_pkt_lost(ngtcp2_rtb *rtb, ngtcp2_rtb_entry *ent,
                           ngtcp2_conn_stat *cstat, ngtcp2_conn *conn,
                           ngtcp2_pktns *pktns, ngtcp2_tstamp ts) {
  int rv;
  ngtcp2_ssize reclaimed;
  ngtcp2_cc *cc = rtb->cc;
  ngtcp2_cc_pkt pkt;

  ngtcp2_log_pkt_lost(rtb->log, ent->hd.pkt_num, ent->hd.type, ent->hd.flags,
                      ent->ts);

  if (rtb->qlog) {
    ngtcp2_qlog_pkt_lost(rtb->qlog, ent);
  }

  if (ent->flags & NGTCP2_RTB_ENTRY_FLAG_PMTUD_PROBE) {
    ++rtb->num_lost_pmtud_pkts;
  } else if (rtb->cc->on_pkt_lost) {
    cc->on_pkt_lost(cc, cstat,
                    ngtcp2_cc_pkt_init(&pkt, ent->hd.pkt_num, ent->pktlen,
                                       pktns->id, ent->ts, ent->rst.lost,
                                       ent->rst.tx_in_flight,
                                       ent->rst.is_app_limited),
                    ts);
  }

  if (ent->flags & NGTCP2_RTB_ENTRY_FLAG_PTO_RECLAIMED) {
    ngtcp2_log_info(rtb->log, NGTCP2_LOG_EVENT_LDC,
                    "pkn=%" PRId64 " has already been reclaimed on PTO",
                    ent->hd.pkt_num);
    assert(!(ent->flags & NGTCP2_RTB_ENTRY_FLAG_LOST_RETRANSMITTED));
    assert(UINT64_MAX == ent->lost_ts);
  } else {
    if (conn->callbacks.lost_datagram &&
        (ent->flags & NGTCP2_RTB_ENTRY_FLAG_DATAGRAM)) {
      rv = conn_process_lost_datagram(conn, ent);
      if (rv != 0) {
        return rv;
      }
    }

    if (ent->flags & NGTCP2_RTB_ENTRY_FLAG_RETRANSMITTABLE) {
      assert(ent->frc);
      assert(!(ent->flags & NGTCP2_RTB_ENTRY_FLAG_LOST_RETRANSMITTED));
      assert(UINT64_MAX == ent->lost_ts);

      reclaimed =
        rtb_reclaim_frame(rtb, NGTCP2_RECLAIM_FLAG_ON_LOSS, conn, pktns, ent);
      if (reclaimed < 0) {
        return (int)reclaimed;
      }
    }
  }

  ent->flags |= NGTCP2_RTB_ENTRY_FLAG_LOST_RETRANSMITTED;
  ent->lost_ts = ts;

  ++rtb->num_lost_pkts;

  return 0;
}

int ngtcp2_rtb_add(ngtcp2_rtb *rtb, ngtcp2_rtb_entry *ent,
                   ngtcp2_conn_stat *cstat) {
  int rv;

  rv = ngtcp2_ksl_insert(&rtb->ents, NULL, &ent->hd.pkt_num, ent);
  if (rv != 0) {
    return rv;
  }

  rtb_on_add(rtb, ent, cstat);

  return 0;
}

ngtcp2_ksl_it ngtcp2_rtb_head(const ngtcp2_rtb *rtb) {
  return ngtcp2_ksl_begin(&rtb->ents);
}

static void rtb_remove(ngtcp2_rtb *rtb, ngtcp2_ksl_it *it,
                       ngtcp2_rtb_entry **pent, ngtcp2_rtb_entry *ent,
                       ngtcp2_conn_stat *cstat) {
  int rv;
  (void)rv;

  rv = ngtcp2_ksl_remove_hint(&rtb->ents, it, it, &ent->hd.pkt_num);
  assert(0 == rv);
  rtb_on_remove(rtb, ent, cstat);

  assert(ent->next == NULL);

  ngtcp2_list_insert(ent, pent);
}

static void conn_ack_crypto_data(ngtcp2_conn *conn, ngtcp2_pktns *pktns,
                                 uint64_t datalen) {
  ngtcp2_buf_chain **pbufchain, *bufchain;
  size_t left;

  for (pbufchain = &pktns->crypto.tx.data; *pbufchain;) {
    left = ngtcp2_buf_len(&(*pbufchain)->buf);
    if (left > datalen) {
      (*pbufchain)->buf.pos += datalen;
      return;
    }

    bufchain = *pbufchain;
    *pbufchain = bufchain->next;

    ngtcp2_mem_free(conn->mem, bufchain);

    datalen -= left;

    if (datalen == 0) {
      return;
    }
  }

  assert(datalen == 0);

  return;
}

static int process_acked_pkt(ngtcp2_rtb_entry *ent, ngtcp2_conn *conn,
                             ngtcp2_pktns *pktns) {
  ngtcp2_frame_chain *frc;
  uint64_t prev_stream_offset, stream_offset;
  ngtcp2_strm *strm;
  int rv;
  uint64_t datalen;
  ngtcp2_strm *crypto = &pktns->crypto.strm;

  if ((ent->flags & NGTCP2_RTB_ENTRY_FLAG_PMTUD_PROBE) && conn->pmtud &&
      conn->pmtud->tx_pkt_num <= ent->hd.pkt_num) {
    ngtcp2_pmtud_probe_success(conn->pmtud, ent->pktlen);

    if (conn->dcid.current.max_udp_payload_size < ent->pktlen) {
      conn->dcid.current.max_udp_payload_size = ent->pktlen;
      conn->cstat.max_tx_udp_payload_size =
        ngtcp2_conn_get_path_max_tx_udp_payload_size(conn);
    }

    if (ngtcp2_pmtud_finished(conn->pmtud)) {
      ngtcp2_conn_stop_pmtud(conn);
    }
  }

  for (frc = ent->frc; frc; frc = frc->next) {
    if (frc->binder) {
      if (frc->binder->flags & NGTCP2_FRAME_CHAIN_BINDER_FLAG_ACK) {
        continue;
      }

      frc->binder->flags |= NGTCP2_FRAME_CHAIN_BINDER_FLAG_ACK;
    }

    switch (frc->fr.type) {
    case NGTCP2_FRAME_STREAM:
      strm = ngtcp2_conn_find_stream(conn, frc->fr.stream.stream_id);
      if (strm == NULL) {
        break;
      }

      strm->flags |= NGTCP2_STRM_FLAG_ANY_ACKED;

      if (frc->fr.stream.fin) {
        strm->flags |= NGTCP2_STRM_FLAG_FIN_ACKED;
      }

      prev_stream_offset = ngtcp2_strm_get_acked_offset(strm);

      rv = ngtcp2_strm_ack_data(
        strm, frc->fr.stream.offset,
        ngtcp2_vec_len(frc->fr.stream.data, frc->fr.stream.datacnt));
      if (rv != 0) {
        return rv;
      }

      if (conn->callbacks.acked_stream_data_offset) {
        stream_offset = ngtcp2_strm_get_acked_offset(strm);

        datalen = stream_offset - prev_stream_offset;
        if (datalen == 0 && !frc->fr.stream.fin) {
          break;
        }

        rv = conn->callbacks.acked_stream_data_offset(
          conn, strm->stream_id, prev_stream_offset, datalen, conn->user_data,
          strm->stream_user_data);
        if (rv != 0) {
          return NGTCP2_ERR_CALLBACK_FAILURE;
        }
      }

      rv = ngtcp2_conn_close_stream_if_shut_rdwr(conn, strm);
      if (rv != 0) {
        return rv;
      }

      break;
    case NGTCP2_FRAME_CRYPTO:
      prev_stream_offset = ngtcp2_strm_get_acked_offset(crypto);

      rv = ngtcp2_strm_ack_data(
        crypto, frc->fr.stream.offset,
        ngtcp2_vec_len(frc->fr.stream.data, frc->fr.stream.datacnt));
      if (rv != 0) {
        return rv;
      }

      stream_offset = ngtcp2_strm_get_acked_offset(crypto);

      datalen = stream_offset - prev_stream_offset;
      if (datalen == 0) {
        break;
      }

      conn_ack_crypto_data(conn, pktns, datalen);

      break;
    case NGTCP2_FRAME_RESET_STREAM:
      strm = ngtcp2_conn_find_stream(conn, frc->fr.reset_stream.stream_id);
      if (strm == NULL) {
        break;
      }

      strm->flags |= NGTCP2_STRM_FLAG_RESET_STREAM_ACKED;

      rv = ngtcp2_conn_close_stream_if_shut_rdwr(conn, strm);
      if (rv != 0) {
        return rv;
      }

      break;
    case NGTCP2_FRAME_RETIRE_CONNECTION_ID:
      ngtcp2_conn_untrack_retired_dcid_seq(conn,
                                           frc->fr.retire_connection_id.seq);
      break;
    case NGTCP2_FRAME_NEW_CONNECTION_ID:
      assert(conn->scid.num_in_flight);

      --conn->scid.num_in_flight;

      break;
    case NGTCP2_FRAME_DATAGRAM:
    case NGTCP2_FRAME_DATAGRAM_LEN:
      if (!conn->callbacks.ack_datagram) {
        break;
      }

      rv = conn->callbacks.ack_datagram(conn, frc->fr.datagram.dgram_id,
                                        conn->user_data);
      if (rv != 0) {
        return NGTCP2_ERR_CALLBACK_FAILURE;
      }

      break;
    }
  }

  return 0;
}

static void rtb_on_pkt_acked(ngtcp2_rtb *rtb, ngtcp2_rtb_entry *ent,
                             ngtcp2_conn_stat *cstat, const ngtcp2_pktns *pktns,
                             ngtcp2_tstamp ts) {
  ngtcp2_cc *cc = rtb->cc;
  ngtcp2_cc_pkt pkt;

  ngtcp2_rst_update_rate_sample(rtb->rst, ent, ts);

  if (cc->on_pkt_acked) {
    cc->on_pkt_acked(cc, cstat,
                     ngtcp2_cc_pkt_init(&pkt, ent->hd.pkt_num, ent->pktlen,
                                        pktns->id, ent->ts, ent->rst.lost,
                                        ent->rst.tx_in_flight,
                                        ent->rst.is_app_limited),
                     ts);
  }

  if (!(ent->flags & NGTCP2_RTB_ENTRY_FLAG_PROBE) &&
      (ent->flags & NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING)) {
    cstat->pto_count = 0;
  }
}

static void conn_verify_ecn(ngtcp2_conn *conn, ngtcp2_pktns *pktns,
                            ngtcp2_cc *cc, ngtcp2_conn_stat *cstat,
                            const ngtcp2_ack *fr, size_t ecn_acked,
                            ngtcp2_tstamp largest_pkt_sent_ts,
                            ngtcp2_tstamp ts) {
  if (conn->tx.ecn.state == NGTCP2_ECN_STATE_FAILED) {
    return;
  }

  if ((ecn_acked && fr->type == NGTCP2_FRAME_ACK) ||
      (fr->type == NGTCP2_FRAME_ACK_ECN &&
       (pktns->rx.ecn.ack.ect0 > fr->ecn.ect0 ||
        pktns->rx.ecn.ack.ect1 > fr->ecn.ect1 ||
        pktns->rx.ecn.ack.ce > fr->ecn.ce ||
        (fr->ecn.ect0 - pktns->rx.ecn.ack.ect0) +
            (fr->ecn.ce - pktns->rx.ecn.ack.ce) <
          ecn_acked ||
        fr->ecn.ect0 > pktns->tx.ecn.ect0 || fr->ecn.ect1))) {
    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_CON,
                    "path is not ECN capable");
    conn->tx.ecn.state = NGTCP2_ECN_STATE_FAILED;

    return;
  }

  if (conn->tx.ecn.state != NGTCP2_ECN_STATE_CAPABLE && ecn_acked) {
    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_CON, "path is ECN capable");
    conn->tx.ecn.state = NGTCP2_ECN_STATE_CAPABLE;
  }

  if (fr->type == NGTCP2_FRAME_ACK_ECN) {
    if (cc->congestion_event && largest_pkt_sent_ts != UINT64_MAX &&
        fr->ecn.ce > pktns->rx.ecn.ack.ce) {
      cc->congestion_event(cc, cstat, largest_pkt_sent_ts, 0, ts);
    }

    pktns->rx.ecn.ack.ect0 = fr->ecn.ect0;
    pktns->rx.ecn.ack.ect1 = fr->ecn.ect1;
    pktns->rx.ecn.ack.ce = fr->ecn.ce;
  }
}

static int rtb_detect_lost_pkt(ngtcp2_rtb *rtb, uint64_t *ppkt_lost,
                               ngtcp2_conn *conn, ngtcp2_pktns *pktns,
                               ngtcp2_conn_stat *cstat, ngtcp2_tstamp ts);

ngtcp2_ssize ngtcp2_rtb_recv_ack(ngtcp2_rtb *rtb, const ngtcp2_ack *fr,
                                 ngtcp2_conn_stat *cstat, ngtcp2_conn *conn,
                                 ngtcp2_pktns *pktns, ngtcp2_tstamp pkt_ts,
                                 ngtcp2_tstamp ts) {
  ngtcp2_rtb_entry *ent;
  int64_t largest_ack = fr->largest_ack, min_ack;
  size_t i;
  int rv;
  ngtcp2_ksl_it it;
  size_t num_acked = 0;
  ngtcp2_tstamp largest_pkt_sent_ts = UINT64_MAX;
  int64_t pkt_num;
  ngtcp2_cc *cc = rtb->cc;
  ngtcp2_rtb_entry *acked_ent = NULL;
  int ack_eliciting_pkt_acked = 0;
  size_t ecn_acked = 0;
  int verify_ecn = 0;
  ngtcp2_cc_ack cc_ack = {0};
  size_t num_lost_pkts = rtb->num_lost_pkts - rtb->num_lost_pmtud_pkts;

  cc_ack.prior_bytes_in_flight = cstat->bytes_in_flight;
  cc_ack.rtt = UINT64_MAX;

  if (conn && (conn->flags & NGTCP2_CONN_FLAG_KEY_UPDATE_NOT_CONFIRMED) &&
      (conn->flags & NGTCP2_CONN_FLAG_KEY_UPDATE_INITIATOR) &&
      largest_ack >= conn->pktns.crypto.tx.ckm->pkt_num) {
    conn->flags &= (uint32_t) ~(NGTCP2_CONN_FLAG_KEY_UPDATE_NOT_CONFIRMED |
                                NGTCP2_CONN_FLAG_KEY_UPDATE_INITIATOR);
    conn->crypto.key_update.confirmed_ts = ts;

    ngtcp2_log_info(rtb->log, NGTCP2_LOG_EVENT_CRY, "key update confirmed");
  }

  if (rtb->largest_acked_tx_pkt_num < largest_ack) {
    rtb->largest_acked_tx_pkt_num = largest_ack;
    verify_ecn = 1;
  }

  /* Assume that ngtcp2_pkt_validate_ack(fr) returns 0 */
  it = ngtcp2_ksl_lower_bound(&rtb->ents, &largest_ack);
  if (ngtcp2_ksl_it_end(&it)) {
    if (conn && verify_ecn) {
      conn_verify_ecn(conn, pktns, rtb->cc, cstat, fr, ecn_acked,
                      largest_pkt_sent_ts, ts);
    }

    return 0;
  }

  min_ack = largest_ack - (int64_t)fr->first_ack_range;

  for (; !ngtcp2_ksl_it_end(&it);) {
    pkt_num = *(int64_t *)ngtcp2_ksl_it_key(&it);

    assert(pkt_num <= largest_ack);

    if (pkt_num < min_ack) {
      break;
    }

    ent = ngtcp2_ksl_it_get(&it);

    if (largest_ack == pkt_num) {
      largest_pkt_sent_ts = ent->ts;
    }

    if (ent->flags & NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING) {
      ack_eliciting_pkt_acked = 1;
    }

    rtb_remove(rtb, &it, &acked_ent, ent, cstat);
    ++num_acked;
  }

  for (i = 0; i < fr->rangecnt; ++i) {
    largest_ack = min_ack - (int64_t)fr->ranges[i].gap - 2;
    min_ack = largest_ack - (int64_t)fr->ranges[i].len;

    it = ngtcp2_ksl_lower_bound(&rtb->ents, &largest_ack);
    if (ngtcp2_ksl_it_end(&it)) {
      break;
    }

    for (; !ngtcp2_ksl_it_end(&it);) {
      pkt_num = *(int64_t *)ngtcp2_ksl_it_key(&it);
      if (pkt_num < min_ack) {
        break;
      }

      ent = ngtcp2_ksl_it_get(&it);

      if (ent->flags & NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING) {
        ack_eliciting_pkt_acked = 1;
      }

      rtb_remove(rtb, &it, &acked_ent, ent, cstat);
      ++num_acked;
    }
  }

  if (largest_pkt_sent_ts != UINT64_MAX && ack_eliciting_pkt_acked) {
    cc_ack.rtt =
      ngtcp2_max_uint64(pkt_ts - largest_pkt_sent_ts, NGTCP2_NANOSECONDS);

    rv = ngtcp2_conn_update_rtt(conn, cc_ack.rtt, fr->ack_delay_unscaled, ts);
    if (rv == 0 && cc->new_rtt_sample) {
      cc->new_rtt_sample(cc, cstat, ts);
    }
  }

  if (conn) {
    for (ent = acked_ent; ent; ent = acked_ent) {
      if (ent->hd.pkt_num >= pktns->tx.ecn.start_pkt_num &&
          (ent->flags & NGTCP2_RTB_ENTRY_FLAG_ECN)) {
        ++ecn_acked;
      }

      rv = process_acked_pkt(ent, conn, pktns);
      if (rv != 0) {
        goto fail;
      }

      if (ent->hd.pkt_num >= rtb->cc_pkt_num) {
        assert(cc_ack.pkt_delivered <= ent->rst.delivered);

        cc_ack.bytes_delivered += ent->pktlen;
        cc_ack.pkt_delivered = ent->rst.delivered;
      }

      rtb_on_pkt_acked(rtb, ent, cstat, pktns, ts);
      acked_ent = ent->next;
      ngtcp2_rtb_entry_objalloc_del(ent, rtb->rtb_entry_objalloc,
                                    rtb->frc_objalloc, rtb->mem);
    }

    if (verify_ecn) {
      conn_verify_ecn(conn, pktns, rtb->cc, cstat, fr, ecn_acked,
                      largest_pkt_sent_ts, ts);
    }
  } else {
    /* For unit tests */
    for (ent = acked_ent; ent; ent = acked_ent) {
      rtb_on_pkt_acked(rtb, ent, cstat, pktns, ts);
      acked_ent = ent->next;
      ngtcp2_rtb_entry_objalloc_del(ent, rtb->rtb_entry_objalloc,
                                    rtb->frc_objalloc, rtb->mem);
    }
  }

  if (rtb->cc->on_spurious_congestion && num_lost_pkts &&
      rtb->num_lost_pkts == rtb->num_lost_pmtud_pkts) {
    rtb->cc->on_spurious_congestion(cc, cstat, ts);
  }

  if (num_acked) {
    ngtcp2_rst_on_ack_recv(rtb->rst, cstat);

    if (conn) {
      rv = rtb_detect_lost_pkt(rtb, &cc_ack.bytes_lost, conn, pktns, cstat, ts);
      if (rv != 0) {
        return rv;
      }
    }
  }

  rtb->rst->lost += cc_ack.bytes_lost;

  cc_ack.largest_pkt_sent_ts = largest_pkt_sent_ts;
  if (num_acked && cc->on_ack_recv) {
    cc->on_ack_recv(cc, cstat, &cc_ack, ts);
  }

  return (ngtcp2_ssize)num_acked;

fail:
  for (ent = acked_ent; ent; ent = acked_ent) {
    acked_ent = ent->next;
    ngtcp2_rtb_entry_objalloc_del(ent, rtb->rtb_entry_objalloc,
                                  rtb->frc_objalloc, rtb->mem);
  }

  return rv;
}

static int rtb_pkt_lost(ngtcp2_rtb *rtb, ngtcp2_conn_stat *cstat,
                        const ngtcp2_rtb_entry *ent, ngtcp2_duration loss_delay,
                        size_t pkt_thres, const ngtcp2_pktns *pktns,
                        ngtcp2_tstamp ts) {
  ngtcp2_tstamp loss_time;

  if (ngtcp2_tstamp_elapsed(ent->ts, loss_delay, ts) ||
      rtb->largest_acked_tx_pkt_num >= ent->hd.pkt_num + (int64_t)pkt_thres) {
    return 1;
  }

  loss_time = cstat->loss_time[pktns->id];

  if (loss_time == UINT64_MAX) {
    loss_time = ent->ts + loss_delay;
  } else {
    loss_time = ngtcp2_min_uint64(loss_time, ent->ts + loss_delay);
  }

  cstat->loss_time[pktns->id] = loss_time;

  return 0;
}

/*
 * compute_pkt_loss_delay computes loss delay.
 */
static ngtcp2_duration compute_pkt_loss_delay(const ngtcp2_conn_stat *cstat) {
  /* 9/8 is kTimeThreshold */
  ngtcp2_duration loss_delay =
    ngtcp2_max_uint64(cstat->latest_rtt, cstat->smoothed_rtt) * 9 / 8;
  return ngtcp2_max_uint64(loss_delay, NGTCP2_GRANULARITY);
}

/*
 * conn_all_ecn_pkt_lost returns nonzero if all ECN QUIC packets are
 * lost during validation period.
 */
static int conn_all_ecn_pkt_lost(ngtcp2_conn *conn) {
  ngtcp2_pktns *in_pktns = conn->in_pktns;
  ngtcp2_pktns *hs_pktns = conn->hs_pktns;
  ngtcp2_pktns *pktns = &conn->pktns;

  return (!in_pktns || in_pktns->tx.ecn.validation_pkt_sent ==
                         in_pktns->tx.ecn.validation_pkt_lost) &&
         (!hs_pktns || hs_pktns->tx.ecn.validation_pkt_sent ==
                         hs_pktns->tx.ecn.validation_pkt_lost) &&
         pktns->tx.ecn.validation_pkt_sent == pktns->tx.ecn.validation_pkt_lost;
}

static int rtb_detect_lost_pkt(ngtcp2_rtb *rtb, uint64_t *ppkt_lost,
                               ngtcp2_conn *conn, ngtcp2_pktns *pktns,
                               ngtcp2_conn_stat *cstat, ngtcp2_tstamp ts) {
  ngtcp2_rtb_entry *ent;
  ngtcp2_duration loss_delay;
  ngtcp2_ksl_it it;
  ngtcp2_tstamp latest_ts, oldest_ts;
  int64_t last_lost_pkt_num;
  ngtcp2_duration loss_window, congestion_period;
  ngtcp2_cc *cc = rtb->cc;
  int rv;
  uint64_t pkt_thres =
    rtb->cc_bytes_in_flight / cstat->max_tx_udp_payload_size / 2;
  size_t ecn_pkt_lost = 0;
  ngtcp2_tstamp start_ts;
  ngtcp2_duration pto = ngtcp2_conn_compute_pto(conn, pktns);
  uint64_t bytes_lost = 0;
  ngtcp2_duration max_ack_delay;

  pkt_thres = ngtcp2_max_uint64(pkt_thres, NGTCP2_PKT_THRESHOLD);
  pkt_thres = ngtcp2_min_uint64(pkt_thres, 256);
  cstat->loss_time[pktns->id] = UINT64_MAX;
  loss_delay = compute_pkt_loss_delay(cstat);

  it = ngtcp2_ksl_lower_bound(&rtb->ents, &rtb->largest_acked_tx_pkt_num);
  for (; !ngtcp2_ksl_it_end(&it); ngtcp2_ksl_it_next(&it)) {
    ent = ngtcp2_ksl_it_get(&it);

    if (ent->flags & NGTCP2_RTB_ENTRY_FLAG_LOST_RETRANSMITTED) {
      break;
    }

    if (rtb_pkt_lost(rtb, cstat, ent, loss_delay, (size_t)pkt_thres, pktns,
                     ts)) {
      /* All entries from ent are considered to be lost. */
      latest_ts = oldest_ts = ent->ts;
      /* +1 to pick this packet for persistent congestion in the
         following loop. */
      last_lost_pkt_num = ent->hd.pkt_num + 1;
      max_ack_delay = conn->remote.transport_params
                        ? conn->remote.transport_params->max_ack_delay
                        : 0;

      congestion_period =
        (cstat->smoothed_rtt +
         ngtcp2_max_uint64(4 * cstat->rttvar, NGTCP2_GRANULARITY) +
         max_ack_delay) *
        NGTCP2_PERSISTENT_CONGESTION_THRESHOLD;

      start_ts = ngtcp2_max_uint64(rtb->persistent_congestion_start_ts,
                                   cstat->first_rtt_sample_ts);

      for (; !ngtcp2_ksl_it_end(&it); ngtcp2_ksl_it_next(&it)) {
        ent = ngtcp2_ksl_it_get(&it);

        if (last_lost_pkt_num == ent->hd.pkt_num + 1 && ent->ts >= start_ts) {
          last_lost_pkt_num = ent->hd.pkt_num;
          oldest_ts = ent->ts;
        } else {
          last_lost_pkt_num = -1;
        }

        if ((ent->flags & NGTCP2_RTB_ENTRY_FLAG_LOST_RETRANSMITTED)) {
          if (pktns->id != NGTCP2_PKTNS_ID_APPLICATION ||
              last_lost_pkt_num == -1 ||
              latest_ts - oldest_ts >= congestion_period) {
            break;
          }

          continue;
        }

        if (ent->hd.pkt_num >= pktns->tx.ecn.start_pkt_num &&
            (ent->flags & NGTCP2_RTB_ENTRY_FLAG_ECN)) {
          ++ecn_pkt_lost;
        }

        bytes_lost += rtb_on_remove(rtb, ent, cstat);
        rv = rtb_on_pkt_lost(rtb, ent, cstat, conn, pktns, ts);
        if (rv != 0) {
          return rv;
        }
      }

      /* If only PMTUD packets are lost, do not trigger congestion
         event. */
      if (bytes_lost == 0) {
        break;
      }

      switch (conn->tx.ecn.state) {
      case NGTCP2_ECN_STATE_TESTING:
        if (conn->tx.ecn.validation_start_ts == UINT64_MAX) {
          break;
        }

        if (ts - conn->tx.ecn.validation_start_ts < 3 * pto) {
          pktns->tx.ecn.validation_pkt_lost += ecn_pkt_lost;
          assert(pktns->tx.ecn.validation_pkt_sent >=
                 pktns->tx.ecn.validation_pkt_lost);
          break;
        }

        conn->tx.ecn.state = NGTCP2_ECN_STATE_UNKNOWN;

        /* fall through */
      case NGTCP2_ECN_STATE_UNKNOWN:
        pktns->tx.ecn.validation_pkt_lost += ecn_pkt_lost;
        assert(pktns->tx.ecn.validation_pkt_sent >=
               pktns->tx.ecn.validation_pkt_lost);
        if (conn_all_ecn_pkt_lost(conn)) {
          conn->tx.ecn.state = NGTCP2_ECN_STATE_FAILED;
        }
        break;
      default:
        break;
      }

      if (cc->congestion_event) {
        cc->congestion_event(cc, cstat, latest_ts, bytes_lost, ts);
      }

      loss_window = latest_ts - oldest_ts;
      /* Persistent congestion situation is only evaluated for app
       * packet number space and for the packets sent after handshake
       * is confirmed.  During handshake, there is not much packets
       * sent and also people seem to do lots of effort not to trigger
       * persistent congestion there, then it is a lot easier to just
       * not enable it during handshake.
       */
      if (pktns->id == NGTCP2_PKTNS_ID_APPLICATION && loss_window &&
          loss_window >= congestion_period) {
        ngtcp2_log_info(rtb->log, NGTCP2_LOG_EVENT_LDC,
                        "persistent congestion loss_window=%" PRIu64
                        " congestion_period=%" PRIu64,
                        loss_window, congestion_period);

        /* Reset min_rtt, srtt, and rttvar here.  Next new RTT
           sample will be used to recalculate these values. */
        cstat->min_rtt = UINT64_MAX;
        cstat->smoothed_rtt = conn->local.settings.initial_rtt;
        cstat->rttvar = conn->local.settings.initial_rtt / 2;
        cstat->first_rtt_sample_ts = UINT64_MAX;

        if (cc->on_persistent_congestion) {
          cc->on_persistent_congestion(cc, cstat, ts);
        }
      }

      break;
    }
  }

  ngtcp2_rtb_remove_excessive_lost_pkt(rtb, (size_t)pkt_thres);

  if (ppkt_lost) {
    *ppkt_lost = bytes_lost;
  }

  return 0;
}

int ngtcp2_rtb_detect_lost_pkt(ngtcp2_rtb *rtb, ngtcp2_conn *conn,
                               ngtcp2_pktns *pktns, ngtcp2_conn_stat *cstat,
                               ngtcp2_tstamp ts) {
  return rtb_detect_lost_pkt(rtb, /* ppkt_lost = */ NULL, conn, pktns, cstat,
                             ts);
}

void ngtcp2_rtb_remove_excessive_lost_pkt(ngtcp2_rtb *rtb, size_t n) {
  ngtcp2_ksl_it it = ngtcp2_ksl_end(&rtb->ents);
  ngtcp2_rtb_entry *ent;
  int rv;
  (void)rv;

  for (; rtb->num_lost_pkts > n;) {
    assert(ngtcp2_ksl_it_end(&it));
    ngtcp2_ksl_it_prev(&it);
    ent = ngtcp2_ksl_it_get(&it);

    assert(ent->flags & NGTCP2_RTB_ENTRY_FLAG_LOST_RETRANSMITTED);

    ngtcp2_log_info(rtb->log, NGTCP2_LOG_EVENT_LDC,
                    "removing stale lost pkn=%" PRId64, ent->hd.pkt_num);

    --rtb->num_lost_pkts;

    if (ent->flags & NGTCP2_RTB_ENTRY_FLAG_PMTUD_PROBE) {
      --rtb->num_lost_pmtud_pkts;
    }

    rv = ngtcp2_ksl_remove_hint(&rtb->ents, &it, &it, &ent->hd.pkt_num);
    assert(0 == rv);
    ngtcp2_rtb_entry_objalloc_del(ent, rtb->rtb_entry_objalloc,
                                  rtb->frc_objalloc, rtb->mem);
  }
}

void ngtcp2_rtb_remove_expired_lost_pkt(ngtcp2_rtb *rtb, ngtcp2_duration pto,
                                        ngtcp2_tstamp ts) {
  ngtcp2_ksl_it it;
  ngtcp2_rtb_entry *ent;
  int rv;
  (void)rv;

  if (ngtcp2_ksl_len(&rtb->ents) == 0) {
    return;
  }

  it = ngtcp2_ksl_end(&rtb->ents);

  for (;;) {
    assert(ngtcp2_ksl_it_end(&it));

    ngtcp2_ksl_it_prev(&it);
    ent = ngtcp2_ksl_it_get(&it);

    if (!(ent->flags & NGTCP2_RTB_ENTRY_FLAG_LOST_RETRANSMITTED) ||
        ts - ent->lost_ts < pto) {
      return;
    }

    ngtcp2_log_info(rtb->log, NGTCP2_LOG_EVENT_LDC,
                    "removing stale lost pkn=%" PRId64, ent->hd.pkt_num);

    --rtb->num_lost_pkts;

    if (ent->flags & NGTCP2_RTB_ENTRY_FLAG_PMTUD_PROBE) {
      --rtb->num_lost_pmtud_pkts;
    }

    rv = ngtcp2_ksl_remove_hint(&rtb->ents, &it, &it, &ent->hd.pkt_num);
    assert(0 == rv);
    ngtcp2_rtb_entry_objalloc_del(ent, rtb->rtb_entry_objalloc,
                                  rtb->frc_objalloc, rtb->mem);

    if (ngtcp2_ksl_len(&rtb->ents) == 0) {
      return;
    }
  }
}

ngtcp2_tstamp ngtcp2_rtb_lost_pkt_ts(const ngtcp2_rtb *rtb) {
  ngtcp2_ksl_it it;
  ngtcp2_rtb_entry *ent;

  if (ngtcp2_ksl_len(&rtb->ents) == 0) {
    return UINT64_MAX;
  }

  it = ngtcp2_ksl_end(&rtb->ents);
  ngtcp2_ksl_it_prev(&it);
  ent = ngtcp2_ksl_it_get(&it);

  if (!(ent->flags & NGTCP2_RTB_ENTRY_FLAG_LOST_RETRANSMITTED)) {
    return UINT64_MAX;
  }

  return ent->lost_ts;
}

static int rtb_on_pkt_lost_resched_move(ngtcp2_rtb *rtb, ngtcp2_conn *conn,
                                        ngtcp2_pktns *pktns,
                                        ngtcp2_rtb_entry *ent) {
  ngtcp2_frame_chain **pfrc, *frc;
  ngtcp2_stream *sfr;
  ngtcp2_strm *strm;
  int rv;

  ngtcp2_log_pkt_lost(rtb->log, ent->hd.pkt_num, ent->hd.type, ent->hd.flags,
                      ent->ts);

  if (rtb->qlog) {
    ngtcp2_qlog_pkt_lost(rtb->qlog, ent);
  }

  if (ent->flags & NGTCP2_RTB_ENTRY_FLAG_PROBE) {
    ngtcp2_log_info(rtb->log, NGTCP2_LOG_EVENT_LDC,
                    "pkn=%" PRId64
                    " is a probe packet, no retransmission is necessary",
                    ent->hd.pkt_num);
    return 0;
  }

  if (ent->flags & NGTCP2_RTB_ENTRY_FLAG_LOST_RETRANSMITTED) {
    --rtb->num_lost_pkts;

    if (ent->flags & NGTCP2_RTB_ENTRY_FLAG_PMTUD_PROBE) {
      --rtb->num_lost_pmtud_pkts;
    }

    ngtcp2_log_info(rtb->log, NGTCP2_LOG_EVENT_LDC,
                    "pkn=%" PRId64
                    " was declared lost and has already been retransmitted",
                    ent->hd.pkt_num);
    return 0;
  }

  if (ent->flags & NGTCP2_RTB_ENTRY_FLAG_PTO_RECLAIMED) {
    ngtcp2_log_info(rtb->log, NGTCP2_LOG_EVENT_LDC,
                    "pkn=%" PRId64 " has already been reclaimed on PTO",
                    ent->hd.pkt_num);
    return 0;
  }

  if (!(ent->flags & NGTCP2_RTB_ENTRY_FLAG_RETRANSMITTABLE) &&
      (!(ent->flags & NGTCP2_RTB_ENTRY_FLAG_DATAGRAM) ||
       !conn->callbacks.lost_datagram)) {
    /* PADDING only (or PADDING + ACK ) packets will have NULL
       ent->frc. */
    return 0;
  }

  pfrc = &ent->frc;

  for (; *pfrc;) {
    switch ((*pfrc)->fr.type) {
    case NGTCP2_FRAME_STREAM:
      frc = *pfrc;

      *pfrc = frc->next;
      frc->next = NULL;
      sfr = &frc->fr.stream;

      strm = ngtcp2_conn_find_stream(conn, sfr->stream_id);
      if (!strm) {
        ngtcp2_frame_chain_objalloc_del(frc, rtb->frc_objalloc, rtb->mem);
        break;
      }

      rv = ngtcp2_strm_streamfrq_push(strm, frc);
      if (rv != 0) {
        ngtcp2_frame_chain_objalloc_del(frc, rtb->frc_objalloc, rtb->mem);
        return rv;
      }

      if (!ngtcp2_strm_is_tx_queued(strm)) {
        strm->cycle = ngtcp2_conn_tx_strmq_first_cycle(conn);
        rv = ngtcp2_conn_tx_strmq_push(conn, strm);
        if (rv != 0) {
          return rv;
        }
      }

      break;
    case NGTCP2_FRAME_CRYPTO:
      frc = *pfrc;

      *pfrc = frc->next;
      frc->next = NULL;

      rv = ngtcp2_strm_streamfrq_push(&pktns->crypto.strm, frc);
      if (rv != 0) {
        assert(ngtcp2_err_is_fatal(rv));
        ngtcp2_frame_chain_objalloc_del(frc, rtb->frc_objalloc, rtb->mem);
        return rv;
      }

      break;
    case NGTCP2_FRAME_DATAGRAM:
    case NGTCP2_FRAME_DATAGRAM_LEN:
      frc = *pfrc;

      if (conn->callbacks.lost_datagram) {
        rv = conn->callbacks.lost_datagram(conn, frc->fr.datagram.dgram_id,
                                           conn->user_data);
        if (rv != 0) {
          return NGTCP2_ERR_CALLBACK_FAILURE;
        }
      }

      *pfrc = (*pfrc)->next;

      ngtcp2_frame_chain_objalloc_del(frc, rtb->frc_objalloc, rtb->mem);

      break;
    default:
      pfrc = &(*pfrc)->next;
    }
  }

  *pfrc = pktns->tx.frq;
  pktns->tx.frq = ent->frc;
  ent->frc = NULL;

  return 0;
}

int ngtcp2_rtb_remove_all(ngtcp2_rtb *rtb, ngtcp2_conn *conn,
                          ngtcp2_pktns *pktns, ngtcp2_conn_stat *cstat) {
  ngtcp2_rtb_entry *ent;
  ngtcp2_ksl_it it;
  int rv;

  it = ngtcp2_ksl_begin(&rtb->ents);

  for (; !ngtcp2_ksl_it_end(&it);) {
    ent = ngtcp2_ksl_it_get(&it);

    rtb_on_remove(rtb, ent, cstat);
    rv = ngtcp2_ksl_remove_hint(&rtb->ents, &it, &it, &ent->hd.pkt_num);
    assert(0 == rv);

    rv = rtb_on_pkt_lost_resched_move(rtb, conn, pktns, ent);

    ngtcp2_rtb_entry_objalloc_del(ent, rtb->rtb_entry_objalloc,
                                  rtb->frc_objalloc, rtb->mem);

    if (rv != 0) {
      return rv;
    }
  }

  return 0;
}

void ngtcp2_rtb_remove_early_data(ngtcp2_rtb *rtb, ngtcp2_conn_stat *cstat) {
  ngtcp2_rtb_entry *ent;
  ngtcp2_ksl_it it;
  int rv;
  (void)rv;

  it = ngtcp2_ksl_begin(&rtb->ents);

  for (; !ngtcp2_ksl_it_end(&it);) {
    ent = ngtcp2_ksl_it_get(&it);

    if (ent->hd.type != NGTCP2_PKT_0RTT) {
      ngtcp2_ksl_it_next(&it);
      continue;
    }

    rtb_on_remove(rtb, ent, cstat);
    rv = ngtcp2_ksl_remove_hint(&rtb->ents, &it, &it, &ent->hd.pkt_num);
    assert(0 == rv);

    ngtcp2_rtb_entry_objalloc_del(ent, rtb->rtb_entry_objalloc,
                                  rtb->frc_objalloc, rtb->mem);
  }
}

int ngtcp2_rtb_empty(const ngtcp2_rtb *rtb) {
  return ngtcp2_ksl_len(&rtb->ents) == 0;
}

void ngtcp2_rtb_reset_cc_state(ngtcp2_rtb *rtb, int64_t cc_pkt_num) {
  rtb->cc_pkt_num = cc_pkt_num;
  rtb->cc_bytes_in_flight = 0;
}

ngtcp2_ssize ngtcp2_rtb_reclaim_on_pto(ngtcp2_rtb *rtb, ngtcp2_conn *conn,
                                       ngtcp2_pktns *pktns, size_t num_pkts) {
  ngtcp2_ksl_it it;
  ngtcp2_rtb_entry *ent;
  ngtcp2_ssize reclaimed;
  size_t atmost = num_pkts;

  it = ngtcp2_ksl_end(&rtb->ents);
  for (; !ngtcp2_ksl_it_begin(&it) && num_pkts;) {
    ngtcp2_ksl_it_prev(&it);
    ent = ngtcp2_ksl_it_get(&it);

    if ((ent->flags & (NGTCP2_RTB_ENTRY_FLAG_LOST_RETRANSMITTED |
                       NGTCP2_RTB_ENTRY_FLAG_PTO_RECLAIMED)) ||
        !(ent->flags & NGTCP2_RTB_ENTRY_FLAG_RETRANSMITTABLE)) {
      continue;
    }

    assert(ent->frc);

    reclaimed =
      rtb_reclaim_frame(rtb, NGTCP2_RECLAIM_FLAG_NONE, conn, pktns, ent);
    if (reclaimed < 0) {
      return reclaimed;
    }

    /* Mark ent reclaimed even if reclaimed == 0 so that we can skip
       it in the next run. */
    ent->flags |= NGTCP2_RTB_ENTRY_FLAG_PTO_RECLAIMED;

    assert(rtb->num_retransmittable);
    --rtb->num_retransmittable;

    if (ent->flags & NGTCP2_RTB_ENTRY_FLAG_PTO_ELICITING) {
      ent->flags &= (uint16_t)~NGTCP2_RTB_ENTRY_FLAG_PTO_ELICITING;
      assert(rtb->num_pto_eliciting);
      --rtb->num_pto_eliciting;
    }

    if (reclaimed) {
      --num_pkts;
    }
  }

  return (ngtcp2_ssize)(atmost - num_pkts);
}
