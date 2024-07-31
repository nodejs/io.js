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
#include "ngtcp2_conn.h"

#include <string.h>
#include <assert.h>

#include "ngtcp2_macro.h"
#include "ngtcp2_log.h"
#include "ngtcp2_cid.h"
#include "ngtcp2_conv.h"
#include "ngtcp2_vec.h"
#include "ngtcp2_addr.h"
#include "ngtcp2_path.h"
#include "ngtcp2_rcvry.h"
#include "ngtcp2_unreachable.h"
#include "ngtcp2_net.h"
#include "ngtcp2_conversion.h"
#include "ngtcp2_tstamp.h"
#include "ngtcp2_frame_chain.h"

/* NGTCP2_FLOW_WINDOW_RTT_FACTOR is the factor of RTT when flow
   control window auto-tuning is triggered. */
#define NGTCP2_FLOW_WINDOW_RTT_FACTOR 2
/* NGTCP2_FLOW_WINDOW_SCALING_FACTOR is the growth factor of flow
   control window. */
#define NGTCP2_FLOW_WINDOW_SCALING_FACTOR 2
/* NGTCP2_MIN_COALESCED_PAYLOADLEN is the minimum length of QUIC
   packet payload that should be coalesced to a long packet. */
#define NGTCP2_MIN_COALESCED_PAYLOADLEN 128

ngtcp2_objalloc_def(strm, ngtcp2_strm, oplent);

/*
 * conn_local_stream returns nonzero if |stream_id| indicates that it
 * is the stream initiated by local endpoint.
 */
static int conn_local_stream(ngtcp2_conn *conn, int64_t stream_id) {
  return (uint8_t)(stream_id & 1) == conn->server;
}

/*
 * bidi_stream returns nonzero if |stream_id| is a bidirectional
 * stream ID.
 */
static int bidi_stream(int64_t stream_id) { return (stream_id & 0x2) == 0; }

static void conn_update_timestamp(ngtcp2_conn *conn, ngtcp2_tstamp ts) {
  assert(conn->log.last_ts <= ts);
  assert(conn->qlog.last_ts <= ts);

  conn->log.last_ts = ts;
  conn->qlog.last_ts = ts;
}

/*
 * conn_is_tls_handshake_completed returns nonzero if TLS handshake
 * has completed and 1 RTT keys are available.
 */
static int conn_is_tls_handshake_completed(ngtcp2_conn *conn) {
  return (conn->flags & NGTCP2_CONN_FLAG_TLS_HANDSHAKE_COMPLETED) &&
         conn->pktns.crypto.rx.ckm && conn->pktns.crypto.tx.ckm;
}

static int conn_call_recv_client_initial(ngtcp2_conn *conn,
                                         const ngtcp2_cid *dcid) {
  int rv;

  assert(conn->callbacks.recv_client_initial);

  rv = conn->callbacks.recv_client_initial(conn, dcid, conn->user_data);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int conn_call_handshake_completed(ngtcp2_conn *conn) {
  int rv;

  if (!conn->callbacks.handshake_completed) {
    return 0;
  }

  rv = conn->callbacks.handshake_completed(conn, conn->user_data);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int conn_call_recv_stream_data(ngtcp2_conn *conn, ngtcp2_strm *strm,
                                      uint32_t flags, uint64_t offset,
                                      const uint8_t *data, size_t datalen) {
  int rv;

  if (!conn->callbacks.recv_stream_data) {
    return 0;
  }

  rv = conn->callbacks.recv_stream_data(conn, flags, strm->stream_id, offset,
                                        data, datalen, conn->user_data,
                                        strm->stream_user_data);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int conn_call_recv_crypto_data(ngtcp2_conn *conn,
                                      ngtcp2_encryption_level encryption_level,
                                      uint64_t offset, const uint8_t *data,
                                      size_t datalen) {
  int rv;

  assert(conn->callbacks.recv_crypto_data);

  rv = conn->callbacks.recv_crypto_data(conn, encryption_level, offset, data,
                                        datalen, conn->user_data);
  switch (rv) {
  case 0:
  case NGTCP2_ERR_CRYPTO:
  case NGTCP2_ERR_REQUIRED_TRANSPORT_PARAM:
  case NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM:
  case NGTCP2_ERR_TRANSPORT_PARAM:
  case NGTCP2_ERR_PROTO:
  case NGTCP2_ERR_VERSION_NEGOTIATION_FAILURE:
  case NGTCP2_ERR_NOMEM:
  case NGTCP2_ERR_CALLBACK_FAILURE:
    return rv;
  default:
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }
}

static int conn_call_stream_open(ngtcp2_conn *conn, ngtcp2_strm *strm) {
  int rv;

  if (!conn->callbacks.stream_open) {
    return 0;
  }

  rv = conn->callbacks.stream_open(conn, strm->stream_id, conn->user_data);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int conn_call_stream_close(ngtcp2_conn *conn, ngtcp2_strm *strm) {
  int rv;
  uint32_t flags = NGTCP2_STREAM_CLOSE_FLAG_NONE;

  if (!conn->callbacks.stream_close) {
    return 0;
  }

  if (strm->flags & NGTCP2_STRM_FLAG_APP_ERROR_CODE_SET) {
    flags |= NGTCP2_STREAM_CLOSE_FLAG_APP_ERROR_CODE_SET;
  }

  rv = conn->callbacks.stream_close(conn, flags, strm->stream_id,
                                    strm->app_error_code, conn->user_data,
                                    strm->stream_user_data);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int conn_call_stream_reset(ngtcp2_conn *conn, int64_t stream_id,
                                  uint64_t final_size, uint64_t app_error_code,
                                  void *stream_user_data) {
  int rv;

  if (!conn->callbacks.stream_reset) {
    return 0;
  }

  rv = conn->callbacks.stream_reset(conn, stream_id, final_size, app_error_code,
                                    conn->user_data, stream_user_data);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int conn_call_extend_max_local_streams_bidi(ngtcp2_conn *conn,
                                                   uint64_t max_streams) {
  int rv;

  if (!conn->callbacks.extend_max_local_streams_bidi) {
    return 0;
  }

  rv = conn->callbacks.extend_max_local_streams_bidi(conn, max_streams,
                                                     conn->user_data);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int conn_call_extend_max_local_streams_uni(ngtcp2_conn *conn,
                                                  uint64_t max_streams) {
  int rv;

  if (!conn->callbacks.extend_max_local_streams_uni) {
    return 0;
  }

  rv = conn->callbacks.extend_max_local_streams_uni(conn, max_streams,
                                                    conn->user_data);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int conn_call_get_new_connection_id(ngtcp2_conn *conn, ngtcp2_cid *cid,
                                           uint8_t *token, size_t cidlen) {
  int rv;

  assert(conn->callbacks.get_new_connection_id);

  rv = conn->callbacks.get_new_connection_id(conn, cid, token, cidlen,
                                             conn->user_data);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int conn_call_remove_connection_id(ngtcp2_conn *conn,
                                          const ngtcp2_cid *cid) {
  int rv;

  if (!conn->callbacks.remove_connection_id) {
    return 0;
  }

  rv = conn->callbacks.remove_connection_id(conn, cid, conn->user_data);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int conn_call_path_validation(ngtcp2_conn *conn, const ngtcp2_pv *pv,
                                     ngtcp2_path_validation_result res) {
  int rv;
  uint32_t flags = NGTCP2_PATH_VALIDATION_FLAG_NONE;
  const ngtcp2_path *old_path = NULL;

  if (!conn->callbacks.path_validation) {
    return 0;
  }

  if (pv->flags & NGTCP2_PV_FLAG_PREFERRED_ADDR) {
    flags |= NGTCP2_PATH_VALIDATION_FLAG_PREFERRED_ADDR;
  }

  if (pv->flags & NGTCP2_PV_FLAG_FALLBACK_ON_FAILURE) {
    old_path = &pv->fallback_dcid.ps.path;
  }

  if (conn->server && old_path &&
      (ngtcp2_addr_compare(&pv->dcid.ps.path.remote, &old_path->remote) &
       (NGTCP2_ADDR_COMPARE_FLAG_ADDR | NGTCP2_ADDR_COMPARE_FLAG_FAMILY))) {
    flags |= NGTCP2_PATH_VALIDATION_FLAG_NEW_TOKEN;
  }

  rv = conn->callbacks.path_validation(conn, flags, &pv->dcid.ps.path, old_path,
                                       res, conn->user_data);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int conn_call_select_preferred_addr(ngtcp2_conn *conn,
                                           ngtcp2_path *dest) {
  int rv;

  if (!conn->callbacks.select_preferred_addr) {
    return 0;
  }

  assert(conn->remote.transport_params);
  assert(conn->remote.transport_params->preferred_addr_present);

  rv = conn->callbacks.select_preferred_addr(
      conn, dest, &conn->remote.transport_params->preferred_addr,
      conn->user_data);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int conn_call_extend_max_remote_streams_bidi(ngtcp2_conn *conn,
                                                    uint64_t max_streams) {
  int rv;

  if (!conn->callbacks.extend_max_remote_streams_bidi) {
    return 0;
  }

  rv = conn->callbacks.extend_max_remote_streams_bidi(conn, max_streams,
                                                      conn->user_data);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int conn_call_extend_max_remote_streams_uni(ngtcp2_conn *conn,
                                                   uint64_t max_streams) {
  int rv;

  if (!conn->callbacks.extend_max_remote_streams_uni) {
    return 0;
  }

  rv = conn->callbacks.extend_max_remote_streams_uni(conn, max_streams,
                                                     conn->user_data);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int conn_call_extend_max_stream_data(ngtcp2_conn *conn,
                                            ngtcp2_strm *strm,
                                            int64_t stream_id,
                                            uint64_t datalen) {
  int rv;

  if (!conn->callbacks.extend_max_stream_data) {
    return 0;
  }

  rv = conn->callbacks.extend_max_stream_data(
      conn, stream_id, datalen, conn->user_data, strm->stream_user_data);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int conn_call_dcid_status(ngtcp2_conn *conn,
                                 ngtcp2_connection_id_status_type type,
                                 const ngtcp2_dcid *dcid) {
  int rv;

  if (!conn->callbacks.dcid_status) {
    return 0;
  }

  rv = conn->callbacks.dcid_status(
      conn, type, dcid->seq, &dcid->cid,
      (dcid->flags & NGTCP2_DCID_FLAG_TOKEN_PRESENT) ? dcid->token : NULL,
      conn->user_data);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int conn_call_activate_dcid(ngtcp2_conn *conn, const ngtcp2_dcid *dcid) {
  return conn_call_dcid_status(conn, NGTCP2_CONNECTION_ID_STATUS_TYPE_ACTIVATE,
                               dcid);
}

static int conn_call_deactivate_dcid(ngtcp2_conn *conn,
                                     const ngtcp2_dcid *dcid) {
  return conn_call_dcid_status(
      conn, NGTCP2_CONNECTION_ID_STATUS_TYPE_DEACTIVATE, dcid);
}

static int conn_call_stream_stop_sending(ngtcp2_conn *conn, int64_t stream_id,
                                         uint64_t app_error_code,
                                         void *stream_user_data) {
  int rv;

  if (!conn->callbacks.stream_stop_sending) {
    return 0;
  }

  rv = conn->callbacks.stream_stop_sending(conn, stream_id, app_error_code,
                                           conn->user_data, stream_user_data);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static void conn_call_delete_crypto_aead_ctx(ngtcp2_conn *conn,
                                             ngtcp2_crypto_aead_ctx *aead_ctx) {
  if (!aead_ctx->native_handle) {
    return;
  }

  assert(conn->callbacks.delete_crypto_aead_ctx);

  conn->callbacks.delete_crypto_aead_ctx(conn, aead_ctx, conn->user_data);
}

static void
conn_call_delete_crypto_cipher_ctx(ngtcp2_conn *conn,
                                   ngtcp2_crypto_cipher_ctx *cipher_ctx) {
  if (!cipher_ctx->native_handle) {
    return;
  }

  assert(conn->callbacks.delete_crypto_cipher_ctx);

  conn->callbacks.delete_crypto_cipher_ctx(conn, cipher_ctx, conn->user_data);
}

static int conn_call_client_initial(ngtcp2_conn *conn) {
  int rv;

  assert(conn->callbacks.client_initial);

  rv = conn->callbacks.client_initial(conn, conn->user_data);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int conn_call_get_path_challenge_data(ngtcp2_conn *conn, uint8_t *data) {
  int rv;

  assert(conn->callbacks.get_path_challenge_data);

  rv = conn->callbacks.get_path_challenge_data(conn, data, conn->user_data);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int conn_call_recv_version_negotiation(ngtcp2_conn *conn,
                                              const ngtcp2_pkt_hd *hd,
                                              const uint32_t *sv, size_t nsv) {
  int rv;

  if (!conn->callbacks.recv_version_negotiation) {
    return 0;
  }

  rv = conn->callbacks.recv_version_negotiation(conn, hd, sv, nsv,
                                                conn->user_data);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int conn_call_recv_retry(ngtcp2_conn *conn, const ngtcp2_pkt_hd *hd) {
  int rv;

  assert(conn->callbacks.recv_retry);

  rv = conn->callbacks.recv_retry(conn, hd, conn->user_data);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int
conn_call_recv_stateless_reset(ngtcp2_conn *conn,
                               const ngtcp2_pkt_stateless_reset *sr) {
  int rv;

  if (!conn->callbacks.recv_stateless_reset) {
    return 0;
  }

  rv = conn->callbacks.recv_stateless_reset(conn, sr, conn->user_data);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int conn_call_recv_new_token(ngtcp2_conn *conn, const uint8_t *token,
                                    size_t tokenlen) {
  int rv;

  if (!conn->callbacks.recv_new_token) {
    return 0;
  }

  rv = conn->callbacks.recv_new_token(conn, token, tokenlen, conn->user_data);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int conn_call_handshake_confirmed(ngtcp2_conn *conn) {
  int rv;

  if (!conn->callbacks.handshake_confirmed) {
    return 0;
  }

  rv = conn->callbacks.handshake_confirmed(conn, conn->user_data);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int conn_call_recv_datagram(ngtcp2_conn *conn,
                                   const ngtcp2_datagram *fr) {
  const uint8_t *data;
  size_t datalen;
  int rv;
  uint32_t flags = NGTCP2_DATAGRAM_FLAG_NONE;

  if (!conn->callbacks.recv_datagram) {
    return 0;
  }

  if (fr->datacnt) {
    assert(fr->datacnt == 1);

    data = fr->data->base;
    datalen = fr->data->len;
  } else {
    data = NULL;
    datalen = 0;
  }

  if (!conn_is_tls_handshake_completed(conn)) {
    flags |= NGTCP2_DATAGRAM_FLAG_0RTT;
  }

  rv = conn->callbacks.recv_datagram(conn, flags, data, datalen,
                                     conn->user_data);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int
conn_call_update_key(ngtcp2_conn *conn, uint8_t *rx_secret, uint8_t *tx_secret,
                     ngtcp2_crypto_aead_ctx *rx_aead_ctx, uint8_t *rx_iv,
                     ngtcp2_crypto_aead_ctx *tx_aead_ctx, uint8_t *tx_iv,
                     const uint8_t *current_rx_secret,
                     const uint8_t *current_tx_secret, size_t secretlen) {
  int rv;

  assert(conn->callbacks.update_key);

  rv = conn->callbacks.update_key(
      conn, rx_secret, tx_secret, rx_aead_ctx, rx_iv, tx_aead_ctx, tx_iv,
      current_rx_secret, current_tx_secret, secretlen, conn->user_data);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int conn_call_version_negotiation(ngtcp2_conn *conn, uint32_t version,
                                         const ngtcp2_cid *dcid) {
  int rv;

  assert(conn->callbacks.version_negotiation);

  rv =
      conn->callbacks.version_negotiation(conn, version, dcid, conn->user_data);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int conn_call_recv_rx_key(ngtcp2_conn *conn,
                                 ngtcp2_encryption_level level) {
  int rv;

  if (!conn->callbacks.recv_rx_key) {
    return 0;
  }

  rv = conn->callbacks.recv_rx_key(conn, level, conn->user_data);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int conn_call_recv_tx_key(ngtcp2_conn *conn,
                                 ngtcp2_encryption_level level) {
  int rv;

  if (!conn->callbacks.recv_tx_key) {
    return 0;
  }

  rv = conn->callbacks.recv_tx_key(conn, level, conn->user_data);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int pktns_init(ngtcp2_pktns *pktns, ngtcp2_pktns_id pktns_id,
                      ngtcp2_rst *rst, ngtcp2_cc *cc, int64_t initial_pkt_num,
                      ngtcp2_log *log, ngtcp2_qlog *qlog,
                      ngtcp2_objalloc *rtb_entry_objalloc,
                      ngtcp2_objalloc *frc_objalloc, const ngtcp2_mem *mem) {
  int rv;

  memset(pktns, 0, sizeof(*pktns));

  ngtcp2_gaptr_init(&pktns->rx.pngap, mem);

  pktns->tx.last_pkt_num = initial_pkt_num - 1;
  pktns->tx.non_ack_pkt_start_ts = UINT64_MAX;
  pktns->rx.max_pkt_num = -1;
  pktns->rx.max_ack_eliciting_pkt_num = -1;

  rv = ngtcp2_acktr_init(&pktns->acktr, log, mem);
  if (rv != 0) {
    goto fail_acktr_init;
  }

  ngtcp2_strm_init(&pktns->crypto.strm, 0, NGTCP2_STRM_FLAG_NONE, 0, 0, NULL,
                   frc_objalloc, mem);

  ngtcp2_rtb_init(&pktns->rtb, pktns_id, &pktns->crypto.strm, rst, cc,
                  initial_pkt_num, log, qlog, rtb_entry_objalloc, frc_objalloc,
                  mem);

  return 0;

fail_acktr_init:
  ngtcp2_gaptr_free(&pktns->rx.pngap);

  return rv;
}

static int pktns_new(ngtcp2_pktns **ppktns, ngtcp2_pktns_id pktns_id,
                     ngtcp2_rst *rst, ngtcp2_cc *cc, int64_t initial_pkt_num,
                     ngtcp2_log *log, ngtcp2_qlog *qlog,
                     ngtcp2_objalloc *rtb_entry_objalloc,
                     ngtcp2_objalloc *frc_objalloc, const ngtcp2_mem *mem) {
  int rv;

  *ppktns = ngtcp2_mem_malloc(mem, sizeof(ngtcp2_pktns));
  if (*ppktns == NULL) {
    return NGTCP2_ERR_NOMEM;
  }

  rv = pktns_init(*ppktns, pktns_id, rst, cc, initial_pkt_num, log, qlog,
                  rtb_entry_objalloc, frc_objalloc, mem);
  if (rv != 0) {
    ngtcp2_mem_free(mem, *ppktns);
  }

  return rv;
}

static int cycle_less(const ngtcp2_pq_entry *lhs, const ngtcp2_pq_entry *rhs) {
  ngtcp2_strm *ls = ngtcp2_struct_of(lhs, ngtcp2_strm, pe);
  ngtcp2_strm *rs = ngtcp2_struct_of(rhs, ngtcp2_strm, pe);

  if (ls->cycle == rs->cycle) {
    return ls->stream_id < rs->stream_id;
  }

  return rs->cycle - ls->cycle <= 1;
}

static void delete_buffed_pkts(ngtcp2_pkt_chain *pc, const ngtcp2_mem *mem) {
  ngtcp2_pkt_chain *next;

  for (; pc;) {
    next = pc->next;
    ngtcp2_pkt_chain_del(pc, mem);
    pc = next;
  }
}

static void delete_buf_chain(ngtcp2_buf_chain *bufchain,
                             const ngtcp2_mem *mem) {
  ngtcp2_buf_chain *next;

  for (; bufchain;) {
    next = bufchain->next;
    ngtcp2_buf_chain_del(bufchain, mem);
    bufchain = next;
  }
}

static void pktns_free(ngtcp2_pktns *pktns, const ngtcp2_mem *mem) {
  delete_buf_chain(pktns->crypto.tx.data, mem);

  delete_buffed_pkts(pktns->rx.buffed_pkts, mem);

  ngtcp2_frame_chain_list_objalloc_del(pktns->tx.frq, pktns->rtb.frc_objalloc,
                                       mem);

  ngtcp2_crypto_km_del(pktns->crypto.rx.ckm, mem);
  ngtcp2_crypto_km_del(pktns->crypto.tx.ckm, mem);

  ngtcp2_rtb_free(&pktns->rtb);
  ngtcp2_strm_free(&pktns->crypto.strm);
  ngtcp2_acktr_free(&pktns->acktr);
  ngtcp2_gaptr_free(&pktns->rx.pngap);
}

static void pktns_del(ngtcp2_pktns *pktns, const ngtcp2_mem *mem) {
  if (pktns == NULL) {
    return;
  }

  pktns_free(pktns, mem);

  ngtcp2_mem_free(mem, pktns);
}

static int cid_less(const ngtcp2_ksl_key *lhs, const ngtcp2_ksl_key *rhs) {
  return ngtcp2_cid_less(lhs, rhs);
}

static int retired_ts_less(const ngtcp2_pq_entry *lhs,
                           const ngtcp2_pq_entry *rhs) {
  const ngtcp2_scid *a = ngtcp2_struct_of(lhs, ngtcp2_scid, pe);
  const ngtcp2_scid *b = ngtcp2_struct_of(rhs, ngtcp2_scid, pe);

  return a->retired_ts < b->retired_ts;
}

/*
 * conn_reset_conn_stat_cc resets congestion state in |cstat|.
 */
static void conn_reset_conn_stat_cc(ngtcp2_conn *conn,
                                    ngtcp2_conn_stat *cstat) {
  cstat->latest_rtt = 0;
  cstat->min_rtt = UINT64_MAX;
  cstat->smoothed_rtt = conn->local.settings.initial_rtt;
  cstat->rttvar = conn->local.settings.initial_rtt / 2;
  cstat->first_rtt_sample_ts = UINT64_MAX;
  cstat->pto_count = 0;
  cstat->loss_detection_timer = UINT64_MAX;
  cstat->cwnd =
      ngtcp2_cc_compute_initcwnd(conn->local.settings.max_tx_udp_payload_size);
  cstat->ssthresh = UINT64_MAX;
  cstat->congestion_recovery_start_ts = UINT64_MAX;
  cstat->bytes_in_flight = 0;
  cstat->delivery_rate_sec = 0;
  cstat->pacing_interval = 0;
  cstat->send_quantum = 64 * 1024;
}

/*
 * reset_conn_stat_recovery resets the fields related to the recovery
 * function
 */
static void reset_conn_stat_recovery(ngtcp2_conn_stat *cstat) {
  /* Initializes them with UINT64_MAX. */
  memset(cstat->loss_time, 0xff, sizeof(cstat->loss_time));
  memset(cstat->last_tx_pkt_ts, 0xff, sizeof(cstat->last_tx_pkt_ts));
}

/*
 * conn_reset_conn_stat resets |cstat|.  The following fields are not
 * reset: initial_rtt and max_udp_payload_size.
 */
static void conn_reset_conn_stat(ngtcp2_conn *conn, ngtcp2_conn_stat *cstat) {
  conn_reset_conn_stat_cc(conn, cstat);
  reset_conn_stat_recovery(cstat);
}

static void delete_scid(ngtcp2_ksl *scids, const ngtcp2_mem *mem) {
  ngtcp2_ksl_it it;

  for (it = ngtcp2_ksl_begin(scids); !ngtcp2_ksl_it_end(&it);
       ngtcp2_ksl_it_next(&it)) {
    ngtcp2_mem_free(mem, ngtcp2_ksl_it_get(&it));
  }
}

/*
 * compute_pto computes PTO.
 */
static ngtcp2_duration compute_pto(ngtcp2_duration smoothed_rtt,
                                   ngtcp2_duration rttvar,
                                   ngtcp2_duration max_ack_delay) {
  ngtcp2_duration var = ngtcp2_max(4 * rttvar, NGTCP2_GRANULARITY);
  return smoothed_rtt + var + max_ack_delay;
}

/*
 * conn_compute_initial_pto computes PTO using the initial RTT.
 */
static ngtcp2_duration conn_compute_initial_pto(ngtcp2_conn *conn,
                                                ngtcp2_pktns *pktns) {
  ngtcp2_duration initial_rtt = conn->local.settings.initial_rtt;
  ngtcp2_duration max_ack_delay;

  if (pktns->rtb.pktns_id == NGTCP2_PKTNS_ID_APPLICATION &&
      conn->remote.transport_params) {
    max_ack_delay = conn->remote.transport_params->max_ack_delay;
  } else {
    max_ack_delay = 0;
  }
  return compute_pto(initial_rtt, initial_rtt / 2, max_ack_delay);
}

/*
 * conn_compute_pto computes the current PTO.
 */
static ngtcp2_duration conn_compute_pto(ngtcp2_conn *conn,
                                        ngtcp2_pktns *pktns) {
  ngtcp2_conn_stat *cstat = &conn->cstat;
  ngtcp2_duration max_ack_delay;

  if (pktns->rtb.pktns_id == NGTCP2_PKTNS_ID_APPLICATION &&
      conn->remote.transport_params) {
    max_ack_delay = conn->remote.transport_params->max_ack_delay;
  } else {
    max_ack_delay = 0;
  }
  return compute_pto(cstat->smoothed_rtt, cstat->rttvar, max_ack_delay);
}

ngtcp2_duration ngtcp2_conn_compute_pto(ngtcp2_conn *conn,
                                        ngtcp2_pktns *pktns) {
  return conn_compute_pto(conn, pktns);
}

/*
 * conn_compute_pv_timeout_pto returns path validation timeout using
 * the given |pto|.
 */
static ngtcp2_duration conn_compute_pv_timeout_pto(ngtcp2_conn *conn,
                                                   ngtcp2_duration pto) {
  ngtcp2_duration initial_pto = conn_compute_initial_pto(conn, &conn->pktns);

  return 3 * ngtcp2_max(pto, initial_pto);
}

/*
 * conn_compute_pv_timeout returns path validation timeout.
 */
static ngtcp2_duration conn_compute_pv_timeout(ngtcp2_conn *conn) {
  return conn_compute_pv_timeout_pto(conn,
                                     conn_compute_pto(conn, &conn->pktns));
}

static void conn_handle_tx_ecn(ngtcp2_conn *conn, ngtcp2_pkt_info *pi,
                               uint16_t *prtb_entry_flags, ngtcp2_pktns *pktns,
                               const ngtcp2_pkt_hd *hd, ngtcp2_tstamp ts) {
  assert(pi);

  if (pi->ecn != NGTCP2_ECN_NOT_ECT) {
    /* We have already made a transition of validation state and
      deceided to send UDP datagram with ECN bit set.  Coalesced QUIC
      packets also bear ECN bits set. */
    if (pktns->tx.ecn.start_pkt_num == INT64_MAX) {
      pktns->tx.ecn.start_pkt_num = hd->pkt_num;
    }

    ++pktns->tx.ecn.validation_pkt_sent;

    if (prtb_entry_flags) {
      *prtb_entry_flags |= NGTCP2_RTB_ENTRY_FLAG_ECN;
    }

    ++pktns->tx.ecn.ect0;

    return;
  }

  switch (conn->tx.ecn.state) {
  case NGTCP2_ECN_STATE_TESTING:
    if (conn->tx.ecn.validation_start_ts == UINT64_MAX) {
      assert(0 == pktns->tx.ecn.validation_pkt_sent);
      assert(0 == pktns->tx.ecn.validation_pkt_lost);

      conn->tx.ecn.validation_start_ts = ts;
    } else if (ts - conn->tx.ecn.validation_start_ts >=
               3 * conn_compute_pto(conn, pktns)) {
      conn->tx.ecn.state = NGTCP2_ECN_STATE_UNKNOWN;
      break;
    }

    if (pktns->tx.ecn.start_pkt_num == INT64_MAX) {
      pktns->tx.ecn.start_pkt_num = hd->pkt_num;
    }

    ++pktns->tx.ecn.validation_pkt_sent;

    if (++conn->tx.ecn.dgram_sent == NGTCP2_ECN_MAX_NUM_VALIDATION_PKTS) {
      conn->tx.ecn.state = NGTCP2_ECN_STATE_UNKNOWN;
    }
    /* fall through */
  case NGTCP2_ECN_STATE_CAPABLE:
    /* pi is provided per UDP datagram. */
    assert(NGTCP2_ECN_NOT_ECT == pi->ecn);

    pi->ecn = NGTCP2_ECN_ECT_0;

    if (prtb_entry_flags) {
      *prtb_entry_flags |= NGTCP2_RTB_ENTRY_FLAG_ECN;
    }

    ++pktns->tx.ecn.ect0;
    break;
  case NGTCP2_ECN_STATE_UNKNOWN:
  case NGTCP2_ECN_STATE_FAILED:
    break;
  default:
    ngtcp2_unreachable();
  }
}

static void conn_reset_ecn_validation_state(ngtcp2_conn *conn) {
  ngtcp2_pktns *in_pktns = conn->in_pktns;
  ngtcp2_pktns *hs_pktns = conn->hs_pktns;
  ngtcp2_pktns *pktns = &conn->pktns;

  conn->tx.ecn.state = NGTCP2_ECN_STATE_TESTING;
  conn->tx.ecn.validation_start_ts = UINT64_MAX;
  conn->tx.ecn.dgram_sent = 0;

  if (in_pktns) {
    in_pktns->tx.ecn.start_pkt_num = INT64_MAX;
    in_pktns->tx.ecn.validation_pkt_sent = 0;
    in_pktns->tx.ecn.validation_pkt_lost = 0;
  }

  if (hs_pktns) {
    hs_pktns->tx.ecn.start_pkt_num = INT64_MAX;
    hs_pktns->tx.ecn.validation_pkt_sent = 0;
    hs_pktns->tx.ecn.validation_pkt_lost = 0;
  }

  pktns->tx.ecn.start_pkt_num = INT64_MAX;
  pktns->tx.ecn.validation_pkt_sent = 0;
  pktns->tx.ecn.validation_pkt_lost = 0;
}

/* server_default_available_versions is the default available_versions
   field sent by server. */
static uint8_t server_default_available_versions[] = {0, 0, 0, 1};

/*
 * available_versions_new allocates new buffer, and writes |versions|
 * of length |versionslen| in network byte order, suitable for sending
 * in available_versions field of version_information QUIC transport
 * parameter.  The pointer to the allocated buffer is assigned to
 * |*pbuf|.
 *
 * This function returns 0 if it succeeds, or one of the negative
 * error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 */
static int available_versions_new(uint8_t **pbuf, const uint32_t *versions,
                                  size_t versionslen, const ngtcp2_mem *mem) {
  size_t i;
  uint8_t *buf = ngtcp2_mem_malloc(mem, sizeof(uint32_t) * versionslen);

  if (buf == NULL) {
    return NGTCP2_ERR_NOMEM;
  }

  *pbuf = buf;

  for (i = 0; i < versionslen; ++i) {
    buf = ngtcp2_put_uint32be(buf, versions[i]);
  }

  return 0;
}

static void
conn_set_local_transport_params(ngtcp2_conn *conn,
                                const ngtcp2_transport_params *params) {
  ngtcp2_transport_params *p = &conn->local.transport_params;
  uint32_t chosen_version = p->version_info.chosen_version;

  *p = *params;

  if (conn->server) {
    p->version_info.chosen_version = chosen_version;
  } else {
    p->version_info.chosen_version = conn->client_chosen_version;
  }
  p->version_info.available_versions = conn->vneg.available_versions;
  p->version_info.available_versionslen = conn->vneg.available_versionslen;
  p->version_info_present = 1;
}

static int conn_new(ngtcp2_conn **pconn, const ngtcp2_cid *dcid,
                    const ngtcp2_cid *scid, const ngtcp2_path *path,
                    uint32_t client_chosen_version, int callbacks_version,
                    const ngtcp2_callbacks *callbacks, int settings_version,
                    const ngtcp2_settings *settings,
                    int transport_params_version,
                    const ngtcp2_transport_params *params,
                    const ngtcp2_mem *mem, void *user_data, int server) {
  int rv;
  ngtcp2_scid *scident;
  uint8_t *buf;
  uint8_t fixed_bit_byte;
  size_t i;
  uint32_t *preferred_versions;
  ngtcp2_transport_params paramsbuf;
  (void)callbacks_version;
  (void)settings_version;

  params = ngtcp2_transport_params_convert_to_latest(
      &paramsbuf, transport_params_version, params);

  assert(settings->max_window <= NGTCP2_MAX_VARINT);
  assert(settings->max_stream_window <= NGTCP2_MAX_VARINT);
  assert(settings->max_tx_udp_payload_size);
  assert(settings->max_tx_udp_payload_size <= NGTCP2_HARD_MAX_UDP_PAYLOAD_SIZE);
  assert(settings->initial_pkt_num <= INT32_MAX);
  assert(params->active_connection_id_limit >=
         NGTCP2_DEFAULT_ACTIVE_CONNECTION_ID_LIMIT);
  assert(params->active_connection_id_limit <= NGTCP2_MAX_DCID_POOL_SIZE);
  assert(params->initial_max_data <= NGTCP2_MAX_VARINT);
  assert(params->initial_max_stream_data_bidi_local <= NGTCP2_MAX_VARINT);
  assert(params->initial_max_stream_data_bidi_remote <= NGTCP2_MAX_VARINT);
  assert(params->initial_max_stream_data_uni <= NGTCP2_MAX_VARINT);
  assert((server && params->original_dcid_present) ||
         (!server && !params->original_dcid_present));
  assert(!params->initial_scid_present);
  assert(server || !params->stateless_reset_token_present);
  assert(server || !params->preferred_addr_present);
  assert(server || !params->retry_scid_present);
  assert(params->max_idle_timeout != UINT64_MAX);
  assert(params->max_ack_delay < (1 << 14) * NGTCP2_MILLISECONDS);
  assert(server || callbacks->client_initial);
  assert(!server || callbacks->recv_client_initial);
  assert(callbacks->recv_crypto_data);
  assert(callbacks->encrypt);
  assert(callbacks->decrypt);
  assert(callbacks->hp_mask);
  assert(server || callbacks->recv_retry);
  assert(callbacks->rand);
  assert(callbacks->get_new_connection_id);
  assert(callbacks->update_key);
  assert(callbacks->delete_crypto_aead_ctx);
  assert(callbacks->delete_crypto_cipher_ctx);
  assert(callbacks->get_path_challenge_data);
  assert(!server || !ngtcp2_is_reserved_version(client_chosen_version));

  if (mem == NULL) {
    mem = ngtcp2_mem_default();
  }

  *pconn = ngtcp2_mem_calloc(mem, 1, sizeof(ngtcp2_conn));
  if (*pconn == NULL) {
    rv = NGTCP2_ERR_NOMEM;
    goto fail_conn;
  }

  (*pconn)->server = server;

  ngtcp2_objalloc_frame_chain_init(&(*pconn)->frc_objalloc, 64, mem);
  ngtcp2_objalloc_rtb_entry_init(&(*pconn)->rtb_entry_objalloc, 64, mem);
  ngtcp2_objalloc_strm_init(&(*pconn)->strm_objalloc, 64, mem);

  ngtcp2_static_ringbuf_dcid_bound_init(&(*pconn)->dcid.bound);

  ngtcp2_static_ringbuf_dcid_unused_init(&(*pconn)->dcid.unused);

  ngtcp2_static_ringbuf_dcid_retired_init(&(*pconn)->dcid.retired);

  ngtcp2_gaptr_init(&(*pconn)->dcid.seqgap, mem);

  ngtcp2_ksl_init(&(*pconn)->scid.set, cid_less, sizeof(ngtcp2_cid), mem);

  ngtcp2_pq_init(&(*pconn)->scid.used, retired_ts_less, mem);

  ngtcp2_map_init(&(*pconn)->strms, mem);

  ngtcp2_pq_init(&(*pconn)->tx.strmq, cycle_less, mem);

  ngtcp2_idtr_init(&(*pconn)->remote.bidi.idtr, !server, mem);

  ngtcp2_idtr_init(&(*pconn)->remote.uni.idtr, !server, mem);

  ngtcp2_static_ringbuf_path_challenge_init(&(*pconn)->rx.path_challenge);

  ngtcp2_log_init(&(*pconn)->log, scid, settings->log_printf,
                  settings->initial_ts, user_data);
  ngtcp2_qlog_init(&(*pconn)->qlog, settings->qlog_write, settings->initial_ts,
                   user_data);
  if ((*pconn)->qlog.write) {
    buf = ngtcp2_mem_malloc(mem, NGTCP2_QLOG_BUFLEN);
    if (buf == NULL) {
      rv = NGTCP2_ERR_NOMEM;
      goto fail_qlog_buf;
    }
    ngtcp2_buf_init(&(*pconn)->qlog.buf, buf, NGTCP2_QLOG_BUFLEN);
  }

  (*pconn)->local.settings = *settings;

  if (settings->tokenlen) {
    buf = ngtcp2_mem_malloc(mem, settings->tokenlen);
    if (buf == NULL) {
      rv = NGTCP2_ERR_NOMEM;
      goto fail_token;
    }
    memcpy(buf, settings->token, settings->tokenlen);
    (*pconn)->local.settings.token = buf;
  } else {
    (*pconn)->local.settings.token = NULL;
  }

  if (!(*pconn)->local.settings.original_version) {
    (*pconn)->local.settings.original_version = client_chosen_version;
  }

  conn_reset_conn_stat(*pconn, &(*pconn)->cstat);
  (*pconn)->cstat.initial_rtt = settings->initial_rtt;
  (*pconn)->cstat.max_tx_udp_payload_size =
      (*pconn)->local.settings.max_tx_udp_payload_size;

  ngtcp2_rst_init(&(*pconn)->rst);

  (*pconn)->cc_algo = settings->cc_algo;

  switch (settings->cc_algo) {
  case NGTCP2_CC_ALGO_RENO:
    ngtcp2_cc_reno_init(&(*pconn)->reno, &(*pconn)->log);

    break;
  case NGTCP2_CC_ALGO_CUBIC:
    ngtcp2_cc_cubic_init(&(*pconn)->cubic, &(*pconn)->log);

    break;
  case NGTCP2_CC_ALGO_BBR:
    ngtcp2_cc_bbr_init(&(*pconn)->bbr, &(*pconn)->log, &(*pconn)->cstat,
                       &(*pconn)->rst, settings->initial_ts, callbacks->rand,
                       &settings->rand_ctx);

    break;
  default:
    ngtcp2_unreachable();
  }

  rv = pktns_new(&(*pconn)->in_pktns, NGTCP2_PKTNS_ID_INITIAL, &(*pconn)->rst,
                 &(*pconn)->cc, settings->initial_pkt_num, &(*pconn)->log,
                 &(*pconn)->qlog, &(*pconn)->rtb_entry_objalloc,
                 &(*pconn)->frc_objalloc, mem);
  if (rv != 0) {
    goto fail_in_pktns_init;
  }

  rv = pktns_new(&(*pconn)->hs_pktns, NGTCP2_PKTNS_ID_HANDSHAKE, &(*pconn)->rst,
                 &(*pconn)->cc, settings->initial_pkt_num, &(*pconn)->log,
                 &(*pconn)->qlog, &(*pconn)->rtb_entry_objalloc,
                 &(*pconn)->frc_objalloc, mem);
  if (rv != 0) {
    goto fail_hs_pktns_init;
  }

  rv = pktns_init(&(*pconn)->pktns, NGTCP2_PKTNS_ID_APPLICATION, &(*pconn)->rst,
                  &(*pconn)->cc, settings->initial_pkt_num, &(*pconn)->log,
                  &(*pconn)->qlog, &(*pconn)->rtb_entry_objalloc,
                  &(*pconn)->frc_objalloc, mem);
  if (rv != 0) {
    goto fail_pktns_init;
  }

  scident = ngtcp2_mem_malloc(mem, sizeof(*scident));
  if (scident == NULL) {
    rv = NGTCP2_ERR_NOMEM;
    goto fail_scident;
  }

  /* Set stateless reset token later if it is available in the local
     transport parameters */
  ngtcp2_scid_init(scident, 0, scid);

  rv = ngtcp2_ksl_insert(&(*pconn)->scid.set, NULL, &scident->cid, scident);
  if (rv != 0) {
    goto fail_scid_set_insert;
  }

  scident = NULL;

  ngtcp2_dcid_init(&(*pconn)->dcid.current, 0, dcid, NULL);
  ngtcp2_dcid_set_path(&(*pconn)->dcid.current, path);

  rv = ngtcp2_gaptr_push(&(*pconn)->dcid.seqgap, 0, 1);
  if (rv != 0) {
    goto fail_seqgap_push;
  }

  if (settings->preferred_versionslen) {
    if (!server && !ngtcp2_is_reserved_version(client_chosen_version)) {
      for (i = 0; i < settings->preferred_versionslen; ++i) {
        if (settings->preferred_versions[i] == client_chosen_version) {
          break;
        }
      }

      assert(i < settings->preferred_versionslen);
    }

    preferred_versions = ngtcp2_mem_malloc(
        mem, sizeof(uint32_t) * settings->preferred_versionslen);
    if (preferred_versions == NULL) {
      rv = NGTCP2_ERR_NOMEM;
      goto fail_preferred_versions;
    }

    for (i = 0; i < settings->preferred_versionslen; ++i) {
      assert(ngtcp2_is_supported_version(settings->preferred_versions[i]));

      preferred_versions[i] = settings->preferred_versions[i];
    }

    (*pconn)->vneg.preferred_versions = preferred_versions;
    (*pconn)->vneg.preferred_versionslen = settings->preferred_versionslen;
  }

  (*pconn)->local.settings.preferred_versions = NULL;
  (*pconn)->local.settings.preferred_versionslen = 0;

  if (settings->available_versionslen) {
    if (!server && !ngtcp2_is_reserved_version(client_chosen_version)) {
      for (i = 0; i < settings->available_versionslen; ++i) {
        if (settings->available_versions[i] == client_chosen_version) {
          break;
        }
      }

      assert(i < settings->available_versionslen);
    }

    for (i = 0; i < settings->available_versionslen; ++i) {
      assert(ngtcp2_is_reserved_version(settings->available_versions[i]) ||
             ngtcp2_is_supported_version(settings->available_versions[i]));
    }

    rv = available_versions_new(&buf, settings->available_versions,
                                settings->available_versionslen, mem);
    if (rv != 0) {
      goto fail_available_versions;
    }

    (*pconn)->vneg.available_versions = buf;
    (*pconn)->vneg.available_versionslen =
        sizeof(uint32_t) * settings->available_versionslen;
  } else if (server) {
    if (settings->preferred_versionslen) {
      rv = available_versions_new(&buf, settings->preferred_versions,
                                  settings->preferred_versionslen, mem);
      if (rv != 0) {
        goto fail_available_versions;
      }

      (*pconn)->vneg.available_versions = buf;
      (*pconn)->vneg.available_versionslen =
          sizeof(uint32_t) * settings->preferred_versionslen;
    } else {
      (*pconn)->vneg.available_versions = server_default_available_versions;
      (*pconn)->vneg.available_versionslen =
          sizeof(server_default_available_versions);
    }
  } else if (!server && !ngtcp2_is_reserved_version(client_chosen_version)) {
    rv = available_versions_new(&buf, &client_chosen_version, 1, mem);
    if (rv != 0) {
      goto fail_available_versions;
    }

    (*pconn)->vneg.available_versions = buf;
    (*pconn)->vneg.available_versionslen = sizeof(uint32_t);
  }

  (*pconn)->local.settings.available_versions = NULL;
  (*pconn)->local.settings.available_versionslen = 0;

  (*pconn)->client_chosen_version = client_chosen_version;

  conn_set_local_transport_params(*pconn, params);

  callbacks->rand(&fixed_bit_byte, 1, &settings->rand_ctx);
  if (fixed_bit_byte & 1) {
    (*pconn)->flags |= NGTCP2_CONN_FLAG_CLEAR_FIXED_BIT;
  }

  (*pconn)->keep_alive.last_ts = UINT64_MAX;
  (*pconn)->keep_alive.timeout = UINT64_MAX;

  (*pconn)->oscid = *scid;
  (*pconn)->callbacks = *callbacks;
  (*pconn)->mem = mem;
  (*pconn)->user_data = user_data;
  (*pconn)->idle_ts = settings->initial_ts;
  (*pconn)->crypto.key_update.confirmed_ts = UINT64_MAX;
  (*pconn)->tx.last_max_data_ts = UINT64_MAX;
  (*pconn)->tx.pacing.next_ts = UINT64_MAX;
  (*pconn)->tx.last_blocked_offset = UINT64_MAX;
  (*pconn)->early.discard_started_ts = UINT64_MAX;

  conn_reset_ecn_validation_state(*pconn);

  ngtcp2_qlog_start(
      &(*pconn)->qlog,
      server ? ((*pconn)->local.transport_params.retry_scid_present
                    ? &(*pconn)->local.transport_params.retry_scid
                    : &(*pconn)->local.transport_params.original_dcid)
             : dcid,
      server);

  return 0;

fail_available_versions:
  ngtcp2_mem_free(mem, (*pconn)->vneg.preferred_versions);
fail_preferred_versions:
fail_seqgap_push:
fail_scid_set_insert:
  ngtcp2_mem_free(mem, scident);
fail_scident:
  pktns_free(&(*pconn)->pktns, mem);
fail_pktns_init:
  pktns_del((*pconn)->hs_pktns, mem);
fail_hs_pktns_init:
  pktns_del((*pconn)->in_pktns, mem);
fail_in_pktns_init:
  ngtcp2_mem_free(mem, (uint8_t *)(*pconn)->local.settings.token);
fail_token:
  ngtcp2_mem_free(mem, (*pconn)->qlog.buf.begin);
fail_qlog_buf:
  ngtcp2_idtr_free(&(*pconn)->remote.uni.idtr);
  ngtcp2_idtr_free(&(*pconn)->remote.bidi.idtr);
  ngtcp2_map_free(&(*pconn)->strms);
  delete_scid(&(*pconn)->scid.set, mem);
  ngtcp2_ksl_free(&(*pconn)->scid.set);
  ngtcp2_gaptr_free(&(*pconn)->dcid.seqgap);
  ngtcp2_objalloc_free(&(*pconn)->strm_objalloc);
  ngtcp2_objalloc_free(&(*pconn)->rtb_entry_objalloc);
  ngtcp2_objalloc_free(&(*pconn)->frc_objalloc);
  ngtcp2_mem_free(mem, *pconn);
fail_conn:
  return rv;
}

int ngtcp2_conn_client_new_versioned(
    ngtcp2_conn **pconn, const ngtcp2_cid *dcid, const ngtcp2_cid *scid,
    const ngtcp2_path *path, uint32_t client_chosen_version,
    int callbacks_version, const ngtcp2_callbacks *callbacks,
    int settings_version, const ngtcp2_settings *settings,
    int transport_params_version, const ngtcp2_transport_params *params,
    const ngtcp2_mem *mem, void *user_data) {
  int rv;

  rv = conn_new(pconn, dcid, scid, path, client_chosen_version,
                callbacks_version, callbacks, settings_version, settings,
                transport_params_version, params, mem, user_data, 0);
  if (rv != 0) {
    return rv;
  }
  (*pconn)->rcid = *dcid;
  (*pconn)->state = NGTCP2_CS_CLIENT_INITIAL;
  (*pconn)->local.bidi.next_stream_id = 0;
  (*pconn)->local.uni.next_stream_id = 2;

  rv = ngtcp2_conn_commit_local_transport_params(*pconn);
  if (rv != 0) {
    ngtcp2_conn_del(*pconn);
    return rv;
  }

  return 0;
}

int ngtcp2_conn_server_new_versioned(
    ngtcp2_conn **pconn, const ngtcp2_cid *dcid, const ngtcp2_cid *scid,
    const ngtcp2_path *path, uint32_t client_chosen_version,
    int callbacks_version, const ngtcp2_callbacks *callbacks,
    int settings_version, const ngtcp2_settings *settings,
    int transport_params_version, const ngtcp2_transport_params *params,
    const ngtcp2_mem *mem, void *user_data) {
  int rv;

  rv = conn_new(pconn, dcid, scid, path, client_chosen_version,
                callbacks_version, callbacks, settings_version, settings,
                transport_params_version, params, mem, user_data, 1);
  if (rv != 0) {
    return rv;
  }

  (*pconn)->state = NGTCP2_CS_SERVER_INITIAL;
  (*pconn)->local.bidi.next_stream_id = 1;
  (*pconn)->local.uni.next_stream_id = 3;

  if ((*pconn)->local.settings.tokenlen) {
    /* Usage of token lifts amplification limit */
    (*pconn)->dcid.current.flags |= NGTCP2_DCID_FLAG_PATH_VALIDATED;
  }

  return 0;
}

/*
 * conn_fc_credits returns the number of bytes allowed to be sent to
 * the given stream.  Both connection and stream level flow control
 * credits are considered.
 */
static uint64_t conn_fc_credits(ngtcp2_conn *conn, ngtcp2_strm *strm) {
  return ngtcp2_min(strm->tx.max_offset - strm->tx.offset,
                    conn->tx.max_offset - conn->tx.offset);
}

/*
 * conn_enforce_flow_control returns the number of bytes allowed to be
 * sent to the given stream.  |len| might be shorted because of
 * available flow control credits.
 */
static uint64_t conn_enforce_flow_control(ngtcp2_conn *conn, ngtcp2_strm *strm,
                                          uint64_t len) {
  uint64_t fc_credits = conn_fc_credits(conn, strm);
  return ngtcp2_min(len, fc_credits);
}

static int delete_strms_each(void *data, void *ptr) {
  ngtcp2_conn *conn = ptr;
  ngtcp2_strm *s = data;

  ngtcp2_strm_free(s);
  ngtcp2_objalloc_strm_release(&conn->strm_objalloc, s);

  return 0;
}

static void conn_vneg_crypto_free(ngtcp2_conn *conn) {
  if (conn->vneg.rx.ckm) {
    conn_call_delete_crypto_aead_ctx(conn, &conn->vneg.rx.ckm->aead_ctx);
  }
  conn_call_delete_crypto_cipher_ctx(conn, &conn->vneg.rx.hp_ctx);

  if (conn->vneg.tx.ckm) {
    conn_call_delete_crypto_aead_ctx(conn, &conn->vneg.tx.ckm->aead_ctx);
  }
  conn_call_delete_crypto_cipher_ctx(conn, &conn->vneg.tx.hp_ctx);

  ngtcp2_crypto_km_del(conn->vneg.rx.ckm, conn->mem);
  ngtcp2_crypto_km_del(conn->vneg.tx.ckm, conn->mem);
}

void ngtcp2_conn_del(ngtcp2_conn *conn) {
  if (conn == NULL) {
    return;
  }

  ngtcp2_qlog_end(&conn->qlog);

  if (conn->early.ckm) {
    conn_call_delete_crypto_aead_ctx(conn, &conn->early.ckm->aead_ctx);
  }
  conn_call_delete_crypto_cipher_ctx(conn, &conn->early.hp_ctx);

  if (conn->crypto.key_update.old_rx_ckm) {
    conn_call_delete_crypto_aead_ctx(
        conn, &conn->crypto.key_update.old_rx_ckm->aead_ctx);
  }
  if (conn->crypto.key_update.new_rx_ckm) {
    conn_call_delete_crypto_aead_ctx(
        conn, &conn->crypto.key_update.new_rx_ckm->aead_ctx);
  }
  if (conn->crypto.key_update.new_tx_ckm) {
    conn_call_delete_crypto_aead_ctx(
        conn, &conn->crypto.key_update.new_tx_ckm->aead_ctx);
  }

  if (conn->pktns.crypto.rx.ckm) {
    conn_call_delete_crypto_aead_ctx(conn,
                                     &conn->pktns.crypto.rx.ckm->aead_ctx);
  }
  conn_call_delete_crypto_cipher_ctx(conn, &conn->pktns.crypto.rx.hp_ctx);

  if (conn->pktns.crypto.tx.ckm) {
    conn_call_delete_crypto_aead_ctx(conn,
                                     &conn->pktns.crypto.tx.ckm->aead_ctx);
  }
  conn_call_delete_crypto_cipher_ctx(conn, &conn->pktns.crypto.tx.hp_ctx);

  if (conn->hs_pktns) {
    if (conn->hs_pktns->crypto.rx.ckm) {
      conn_call_delete_crypto_aead_ctx(
          conn, &conn->hs_pktns->crypto.rx.ckm->aead_ctx);
    }
    conn_call_delete_crypto_cipher_ctx(conn, &conn->hs_pktns->crypto.rx.hp_ctx);

    if (conn->hs_pktns->crypto.tx.ckm) {
      conn_call_delete_crypto_aead_ctx(
          conn, &conn->hs_pktns->crypto.tx.ckm->aead_ctx);
    }
    conn_call_delete_crypto_cipher_ctx(conn, &conn->hs_pktns->crypto.tx.hp_ctx);
  }
  if (conn->in_pktns) {
    if (conn->in_pktns->crypto.rx.ckm) {
      conn_call_delete_crypto_aead_ctx(
          conn, &conn->in_pktns->crypto.rx.ckm->aead_ctx);
    }
    conn_call_delete_crypto_cipher_ctx(conn, &conn->in_pktns->crypto.rx.hp_ctx);

    if (conn->in_pktns->crypto.tx.ckm) {
      conn_call_delete_crypto_aead_ctx(
          conn, &conn->in_pktns->crypto.tx.ckm->aead_ctx);
    }
    conn_call_delete_crypto_cipher_ctx(conn, &conn->in_pktns->crypto.tx.hp_ctx);
  }

  conn_call_delete_crypto_aead_ctx(conn, &conn->crypto.retry_aead_ctx);

  ngtcp2_transport_params_del(conn->remote.transport_params, conn->mem);
  ngtcp2_transport_params_del(conn->remote.pending_transport_params, conn->mem);

  conn_vneg_crypto_free(conn);

  ngtcp2_mem_free(conn->mem, conn->vneg.preferred_versions);
  if (conn->vneg.available_versions != server_default_available_versions) {
    ngtcp2_mem_free(conn->mem, conn->vneg.available_versions);
  }

  ngtcp2_mem_free(conn->mem, conn->crypto.decrypt_buf.base);
  ngtcp2_mem_free(conn->mem, conn->crypto.decrypt_hp_buf.base);
  ngtcp2_mem_free(conn->mem, (uint8_t *)conn->local.settings.token);

  ngtcp2_crypto_km_del(conn->crypto.key_update.old_rx_ckm, conn->mem);
  ngtcp2_crypto_km_del(conn->crypto.key_update.new_rx_ckm, conn->mem);
  ngtcp2_crypto_km_del(conn->crypto.key_update.new_tx_ckm, conn->mem);
  ngtcp2_crypto_km_del(conn->early.ckm, conn->mem);

  pktns_free(&conn->pktns, conn->mem);
  pktns_del(conn->hs_pktns, conn->mem);
  pktns_del(conn->in_pktns, conn->mem);

  ngtcp2_mem_free(conn->mem, conn->qlog.buf.begin);

  ngtcp2_pmtud_del(conn->pmtud);
  ngtcp2_pv_del(conn->pv);

  ngtcp2_mem_free(conn->mem, (uint8_t *)conn->rx.ccerr.reason);

  ngtcp2_idtr_free(&conn->remote.uni.idtr);
  ngtcp2_idtr_free(&conn->remote.bidi.idtr);
  ngtcp2_mem_free(conn->mem, conn->tx.ack);
  ngtcp2_pq_free(&conn->tx.strmq);
  ngtcp2_map_each_free(&conn->strms, delete_strms_each, (void *)conn);
  ngtcp2_map_free(&conn->strms);

  ngtcp2_pq_free(&conn->scid.used);
  delete_scid(&conn->scid.set, conn->mem);
  ngtcp2_ksl_free(&conn->scid.set);
  ngtcp2_gaptr_free(&conn->dcid.seqgap);

  ngtcp2_objalloc_free(&conn->strm_objalloc);
  ngtcp2_objalloc_free(&conn->rtb_entry_objalloc);
  ngtcp2_objalloc_free(&conn->frc_objalloc);

  ngtcp2_mem_free(conn->mem, conn);
}

/*
 * conn_ensure_ack_ranges makes sure that conn->tx.ack->ack.ranges can
 * contain at least |n| additional ngtcp2_ack_range.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 */
static int conn_ensure_ack_ranges(ngtcp2_conn *conn, size_t n) {
  ngtcp2_frame *fr;
  size_t max = conn->tx.max_ack_ranges;

  if (n <= max) {
    return 0;
  }

  max *= 2;

  assert(max >= n);

  fr = ngtcp2_mem_realloc(conn->mem, conn->tx.ack,
                          sizeof(ngtcp2_ack) + sizeof(ngtcp2_ack_range) * max);
  if (fr == NULL) {
    return NGTCP2_ERR_NOMEM;
  }

  conn->tx.ack = fr;
  conn->tx.max_ack_ranges = max;

  return 0;
}

/*
 * conn_compute_ack_delay computes ACK delay for outgoing protected
 * ACK.
 */
static ngtcp2_duration conn_compute_ack_delay(ngtcp2_conn *conn) {
  return ngtcp2_min(conn->local.transport_params.max_ack_delay,
                    conn->cstat.smoothed_rtt / 8);
}

int ngtcp2_conn_create_ack_frame(ngtcp2_conn *conn, ngtcp2_frame **pfr,
                                 ngtcp2_pktns *pktns, uint8_t type,
                                 ngtcp2_tstamp ts, ngtcp2_duration ack_delay,
                                 uint64_t ack_delay_exponent) {
  /* TODO Measure an actual size of ACK blocks to find the best
     default value. */
  const size_t initial_max_ack_ranges = 8;
  int64_t last_pkt_num;
  ngtcp2_acktr *acktr = &pktns->acktr;
  ngtcp2_ack_range *range;
  ngtcp2_ksl_it it;
  ngtcp2_acktr_entry *rpkt;
  ngtcp2_ack *ack;
  size_t range_idx;
  ngtcp2_tstamp largest_ack_ts;
  int rv;

  if (acktr->flags & NGTCP2_ACKTR_FLAG_IMMEDIATE_ACK) {
    ack_delay = 0;
  }

  if (!ngtcp2_acktr_require_active_ack(acktr, ack_delay, ts)) {
    return 0;
  }

  it = ngtcp2_acktr_get(acktr);
  if (ngtcp2_ksl_it_end(&it)) {
    ngtcp2_acktr_commit_ack(acktr);
    return 0;
  }

  if (conn->tx.ack == NULL) {
    conn->tx.ack = ngtcp2_mem_malloc(
        conn->mem,
        sizeof(ngtcp2_ack) + sizeof(ngtcp2_ack_range) * initial_max_ack_ranges);
    if (conn->tx.ack == NULL) {
      return NGTCP2_ERR_NOMEM;
    }
    conn->tx.max_ack_ranges = initial_max_ack_ranges;
  }

  ack = &conn->tx.ack->ack;

  if (pktns->rx.ecn.ect0 || pktns->rx.ecn.ect1 || pktns->rx.ecn.ce) {
    ack->type = NGTCP2_FRAME_ACK_ECN;
    ack->ecn.ect0 = pktns->rx.ecn.ect0;
    ack->ecn.ect1 = pktns->rx.ecn.ect1;
    ack->ecn.ce = pktns->rx.ecn.ce;
  } else {
    ack->type = NGTCP2_FRAME_ACK;
  }
  ack->rangecnt = 0;

  rpkt = ngtcp2_ksl_it_get(&it);

  if (rpkt->pkt_num == pktns->rx.max_pkt_num) {
    last_pkt_num = rpkt->pkt_num - (int64_t)(rpkt->len - 1);
    largest_ack_ts = rpkt->tstamp;
    ack->largest_ack = rpkt->pkt_num;
    ack->first_ack_range = rpkt->len - 1;

    ngtcp2_ksl_it_next(&it);
  } else if (rpkt->pkt_num + 1 == pktns->rx.max_pkt_num) {
    last_pkt_num = rpkt->pkt_num - (int64_t)(rpkt->len - 1);
    largest_ack_ts = pktns->rx.max_pkt_ts;
    ack->largest_ack = pktns->rx.max_pkt_num;
    ack->first_ack_range = rpkt->len;

    ngtcp2_ksl_it_next(&it);
  } else {
    assert(rpkt->pkt_num < pktns->rx.max_pkt_num);

    last_pkt_num = pktns->rx.max_pkt_num;
    largest_ack_ts = pktns->rx.max_pkt_ts;
    ack->largest_ack = pktns->rx.max_pkt_num;
    ack->first_ack_range = 0;
  }

  if (type == NGTCP2_PKT_1RTT) {
    ack->ack_delay_unscaled = ts - largest_ack_ts;
    ack->ack_delay = ack->ack_delay_unscaled / NGTCP2_MICROSECONDS /
                     (1ULL << ack_delay_exponent);
  } else {
    ack->ack_delay_unscaled = 0;
    ack->ack_delay = 0;
  }

  for (; !ngtcp2_ksl_it_end(&it); ngtcp2_ksl_it_next(&it)) {
    if (ack->rangecnt == NGTCP2_MAX_ACK_RANGES) {
      break;
    }

    rpkt = ngtcp2_ksl_it_get(&it);

    range_idx = ack->rangecnt++;
    rv = conn_ensure_ack_ranges(conn, ack->rangecnt);
    if (rv != 0) {
      return rv;
    }
    ack = &conn->tx.ack->ack;
    range = &ack->ranges[range_idx];
    range->gap = (uint64_t)(last_pkt_num - rpkt->pkt_num - 2);
    range->len = rpkt->len - 1;

    last_pkt_num = rpkt->pkt_num - (int64_t)(rpkt->len - 1);
  }

  /* TODO Just remove entries which cannot fit into a single ACK frame
     for now. */
  if (!ngtcp2_ksl_it_end(&it)) {
    ngtcp2_acktr_forget(acktr, ngtcp2_ksl_it_get(&it));
  }

  *pfr = conn->tx.ack;

  return 0;
}

/*
 * conn_ppe_write_frame writes |fr| to |ppe|.  If |hd_logged| is not
 * NULL and |*hd_logged| is zero, packet header is logged, and 1 is
 * assigned to |*hd_logged|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOBUF
 *     Buffer is too small.
 */
static int conn_ppe_write_frame_hd_log(ngtcp2_conn *conn, ngtcp2_ppe *ppe,
                                       int *hd_logged, const ngtcp2_pkt_hd *hd,
                                       ngtcp2_frame *fr) {
  int rv;

  rv = ngtcp2_ppe_encode_frame(ppe, fr);
  if (rv != 0) {
    assert(NGTCP2_ERR_NOBUF == rv);
    return rv;
  }

  if (hd_logged && !*hd_logged) {
    *hd_logged = 1;
    ngtcp2_log_tx_pkt_hd(&conn->log, hd);
    ngtcp2_qlog_pkt_sent_start(&conn->qlog);
  }

  ngtcp2_log_tx_fr(&conn->log, hd, fr);
  ngtcp2_qlog_write_frame(&conn->qlog, fr);

  return 0;
}

/*
 * conn_ppe_write_frame writes |fr| to |ppe|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOBUF
 *     Buffer is too small.
 */
static int conn_ppe_write_frame(ngtcp2_conn *conn, ngtcp2_ppe *ppe,
                                const ngtcp2_pkt_hd *hd, ngtcp2_frame *fr) {
  return conn_ppe_write_frame_hd_log(conn, ppe, NULL, hd, fr);
}

/*
 * conn_on_pkt_sent is called when new non-ACK-only packet is sent.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory
 */
static int conn_on_pkt_sent(ngtcp2_conn *conn, ngtcp2_rtb *rtb,
                            ngtcp2_rtb_entry *ent) {
  int rv;

  /* This function implements OnPacketSent, but it handles only
     non-ACK-only packet. */
  rv = ngtcp2_rtb_add(rtb, ent, &conn->cstat);
  if (rv != 0) {
    return rv;
  }

  if (ent->flags & NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING) {
    conn->cstat.last_tx_pkt_ts[rtb->pktns_id] = ent->ts;
  }

  ngtcp2_conn_set_loss_detection_timer(conn, ent->ts);

  return 0;
}

/*
 * pktns_select_pkt_numlen selects shortest packet number encoding for
 * the next packet number based on the largest acknowledged packet
 * number.  It returns the number of bytes to encode the packet
 * number.
 */
static size_t pktns_select_pkt_numlen(ngtcp2_pktns *pktns) {
  int64_t pkt_num = pktns->tx.last_pkt_num + 1;
  ngtcp2_rtb *rtb = &pktns->rtb;
  int64_t n = pkt_num - rtb->largest_acked_tx_pkt_num;

  if (NGTCP2_MAX_PKT_NUM / 2 < n) {
    return 4;
  }

  n = n * 2 - 1;

  if (n > 0xffffff) {
    return 4;
  }
  if (n > 0xffff) {
    return 3;
  }
  if (n > 0xff) {
    return 2;
  }
  return 1;
}

/*
 * conn_get_cwnd returns cwnd for the current path.
 */
static uint64_t conn_get_cwnd(ngtcp2_conn *conn) {
  return conn->pv && (conn->pv->flags & NGTCP2_PV_FLAG_FALLBACK_ON_FAILURE)
             ? ngtcp2_cc_compute_initcwnd(conn->cstat.max_tx_udp_payload_size)
             : conn->cstat.cwnd;
}

/*
 * conn_cwnd_is_zero returns nonzero if the number of bytes the local
 * endpoint can sent at this time is zero.
 */
static int conn_cwnd_is_zero(ngtcp2_conn *conn) {
  uint64_t bytes_in_flight = conn->cstat.bytes_in_flight;
  uint64_t cwnd = conn_get_cwnd(conn);

  if (bytes_in_flight >= cwnd) {
    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_LDC,
                    "cwnd limited bytes_in_flight=%lu cwnd=%lu",
                    bytes_in_flight, cwnd);
  }

  return bytes_in_flight >= cwnd;
}

/*
 * conn_retry_early_payloadlen returns the estimated wire length of
 * the first STREAM frame of 0-RTT packet which should be
 * retransmitted due to Retry packet.
 */
static uint64_t conn_retry_early_payloadlen(ngtcp2_conn *conn) {
  ngtcp2_frame_chain *frc;
  ngtcp2_strm *strm;
  uint64_t len;

  if (conn->flags & NGTCP2_CONN_FLAG_EARLY_DATA_REJECTED) {
    return 0;
  }

  for (; !ngtcp2_pq_empty(&conn->tx.strmq);) {
    strm = ngtcp2_conn_tx_strmq_top(conn);
    if (ngtcp2_strm_streamfrq_empty(strm)) {
      ngtcp2_conn_tx_strmq_pop(conn);
      continue;
    }

    frc = ngtcp2_strm_streamfrq_top(strm);

    len = ngtcp2_vec_len(frc->fr.stream.data, frc->fr.stream.datacnt) +
          NGTCP2_STREAM_OVERHEAD;

    /* Take the min because in conn_should_pad_pkt we take max in
       order to deal with unbreakable DATAGRAM. */
    return ngtcp2_min(len, NGTCP2_MIN_COALESCED_PAYLOADLEN);
  }

  return 0;
}

/*
 * conn_verify_dcid verifies that destination connection ID in |hd| is
 * valid for the connection.  If it is successfully verified and the
 * remote endpoint uses new DCID in the packet, nonzero value is
 * assigned to |*pnew_cid_used| if it is not NULL.  Otherwise 0 is
 * assigned to it.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 * NGTCP2_ERR_INVALID_ARGUMENT
 *     |dcid| is not known to the local endpoint.
 */
static int conn_verify_dcid(ngtcp2_conn *conn, int *pnew_cid_used,
                            const ngtcp2_pkt_hd *hd) {
  ngtcp2_ksl_it it;
  ngtcp2_scid *scid;
  int rv;

  it = ngtcp2_ksl_lower_bound(&conn->scid.set, &hd->dcid);
  if (ngtcp2_ksl_it_end(&it)) {
    return NGTCP2_ERR_INVALID_ARGUMENT;
  }

  scid = ngtcp2_ksl_it_get(&it);
  if (!ngtcp2_cid_eq(&scid->cid, &hd->dcid)) {
    return NGTCP2_ERR_INVALID_ARGUMENT;
  }

  if (!(scid->flags & NGTCP2_SCID_FLAG_USED)) {
    scid->flags |= NGTCP2_SCID_FLAG_USED;

    if (scid->pe.index == NGTCP2_PQ_BAD_INDEX) {
      rv = ngtcp2_pq_push(&conn->scid.used, &scid->pe);
      if (rv != 0) {
        return rv;
      }
    }

    if (pnew_cid_used) {
      *pnew_cid_used = 1;
    }
  } else if (pnew_cid_used) {
    *pnew_cid_used = 0;
  }

  return 0;
}

/*
 * conn_should_pad_pkt returns nonzero if the packet should be padded.
 * |type| is the type of packet.  |left| is the space left in packet
 * buffer.  |write_datalen| is the number of bytes which will be sent
 * in the next, coalesced 0-RTT packet.
 */
static int conn_should_pad_pkt(ngtcp2_conn *conn, uint8_t type, size_t left,
                               uint64_t write_datalen, int ack_eliciting,
                               int require_padding) {
  uint64_t min_payloadlen;

  if (type == NGTCP2_PKT_INITIAL) {
    if (conn->server) {
      if (!ack_eliciting) {
        return 0;
      }

      if (conn->hs_pktns->crypto.tx.ckm &&
          (conn->hs_pktns->rtb.probe_pkt_left ||
           !ngtcp2_strm_streamfrq_empty(&conn->hs_pktns->crypto.strm) ||
           !ngtcp2_acktr_empty(&conn->hs_pktns->acktr))) {
        /* If we have something to send in Handshake packet, then add
           PADDING in Handshake packet. */
        min_payloadlen = NGTCP2_MIN_COALESCED_PAYLOADLEN;
      } else {
        return 1;
      }
    } else {
      if (conn->hs_pktns->crypto.tx.ckm &&
          (conn->hs_pktns->rtb.probe_pkt_left ||
           !ngtcp2_strm_streamfrq_empty(&conn->hs_pktns->crypto.strm) ||
           !ngtcp2_acktr_empty(&conn->hs_pktns->acktr))) {
        /* If we have something to send in Handshake packet, then add
           PADDING in Handshake packet. */
        min_payloadlen = NGTCP2_MIN_COALESCED_PAYLOADLEN;
      } else if (conn->early.ckm && write_datalen > 0) {
        /* If we have something to send in 0RTT packet, then add
           PADDING in that packet.  Take maximum in case that
           write_datalen includes DATAGRAM which cannot be split. */
        min_payloadlen =
            ngtcp2_max(write_datalen, NGTCP2_MIN_COALESCED_PAYLOADLEN);
      } else {
        return 1;
      }
    }
  } else {
    assert(type == NGTCP2_PKT_HANDSHAKE);

    if (!require_padding) {
      return 0;
    }

    if (!conn->pktns.crypto.tx.ckm) {
      return 1;
    }

    min_payloadlen = NGTCP2_MIN_COALESCED_PAYLOADLEN;
  }

  /* TODO the next packet type should be taken into account */
  return left <
         /* TODO Assuming that pkt_num is encoded in 1 byte. */
         NGTCP2_MIN_LONG_HEADERLEN + conn->dcid.current.cid.datalen +
             conn->oscid.datalen + NGTCP2_PKT_LENGTHLEN - 1 + min_payloadlen +
             NGTCP2_MAX_AEAD_OVERHEAD;
}

static void conn_restart_timer_on_write(ngtcp2_conn *conn, ngtcp2_tstamp ts) {
  conn->idle_ts = ts;
  conn->flags &= (uint32_t)~NGTCP2_CONN_FLAG_RESTART_IDLE_TIMER_ON_WRITE;
}

static void conn_restart_timer_on_read(ngtcp2_conn *conn, ngtcp2_tstamp ts) {
  conn->idle_ts = ts;
  conn->flags |= NGTCP2_CONN_FLAG_RESTART_IDLE_TIMER_ON_WRITE;
}

/*
 * conn_keep_alive_enabled returns nonzero if keep-alive is enabled.
 */
static int conn_keep_alive_enabled(ngtcp2_conn *conn) {
  return conn->keep_alive.last_ts != UINT64_MAX &&
         conn->keep_alive.timeout != UINT64_MAX;
}

/*
 * conn_keep_alive_expired returns nonzero if keep-alive timer has
 * expired.
 */
static int conn_keep_alive_expired(ngtcp2_conn *conn, ngtcp2_tstamp ts) {
  return ngtcp2_tstamp_elapsed(conn->keep_alive.last_ts,
                               conn->keep_alive.timeout, ts);
}

/*
 * conn_keep_alive_expiry returns the expiry time of keep-alive timer.
 */
static ngtcp2_tstamp conn_keep_alive_expiry(ngtcp2_conn *conn) {
  if ((conn->flags & NGTCP2_CONN_FLAG_KEEP_ALIVE_CANCELLED) ||
      !(conn->flags & NGTCP2_CONN_FLAG_HANDSHAKE_COMPLETED) ||
      !conn_keep_alive_enabled(conn) ||
      conn->keep_alive.last_ts >= UINT64_MAX - conn->keep_alive.timeout) {
    return UINT64_MAX;
  }

  return conn->keep_alive.last_ts + conn->keep_alive.timeout;
}

/*
 * conn_cancel_expired_keep_alive_timer cancels the expired keep-alive
 * timer.
 */
static void conn_cancel_expired_keep_alive_timer(ngtcp2_conn *conn,
                                                 ngtcp2_tstamp ts) {
  if (!(conn->flags & NGTCP2_CONN_FLAG_KEEP_ALIVE_CANCELLED) &&
      conn_keep_alive_expired(conn, ts)) {
    conn->flags |= NGTCP2_CONN_FLAG_KEEP_ALIVE_CANCELLED;
  }
}

/*
 * conn_update_keep_alive_last_ts updates the base time point of
 * keep-alive timer.
 */
static void conn_update_keep_alive_last_ts(ngtcp2_conn *conn,
                                           ngtcp2_tstamp ts) {
  conn->keep_alive.last_ts = ts;
  conn->flags &= (uint32_t)~NGTCP2_CONN_FLAG_KEEP_ALIVE_CANCELLED;
}

void ngtcp2_conn_set_keep_alive_timeout(ngtcp2_conn *conn,
                                        ngtcp2_duration timeout) {
  if (timeout == 0) {
    timeout = UINT64_MAX;
  }

  conn->keep_alive.timeout = timeout;
}

/*
 * NGTCP2_PKT_PACING_OVERHEAD defines overhead of userspace event
 * loop.  Packet pacing might require sub milliseconds packet spacing,
 * but userspace event loop might not offer such precision.
 * Typically, if delay is 0.5 microseconds, the actual delay after
 * which we can send packet is well over 1 millisecond when event loop
 * is involved (which includes other stuff, like reading packets etc
 * in a typical single threaded use case).
 */
#define NGTCP2_PKT_PACING_OVERHEAD NGTCP2_MILLISECONDS

static void conn_cancel_expired_pkt_tx_timer(ngtcp2_conn *conn,
                                             ngtcp2_tstamp ts) {
  if (conn->tx.pacing.next_ts == UINT64_MAX) {
    return;
  }

  if (conn->tx.pacing.next_ts > ts + NGTCP2_PKT_PACING_OVERHEAD) {
    return;
  }

  conn->tx.pacing.next_ts = UINT64_MAX;
}

static int conn_pacing_pkt_tx_allowed(ngtcp2_conn *conn, ngtcp2_tstamp ts) {
  return conn->tx.pacing.next_ts == UINT64_MAX ||
         conn->tx.pacing.next_ts <= ts + NGTCP2_PKT_PACING_OVERHEAD;
}

static uint8_t conn_pkt_flags(ngtcp2_conn *conn) {
  if (conn->remote.transport_params &&
      conn->remote.transport_params->grease_quic_bit &&
      (conn->flags & NGTCP2_CONN_FLAG_CLEAR_FIXED_BIT)) {
    return NGTCP2_PKT_FLAG_FIXED_BIT_CLEAR;
  }

  return NGTCP2_PKT_FLAG_NONE;
}

static uint8_t conn_pkt_flags_long(ngtcp2_conn *conn) {
  return NGTCP2_PKT_FLAG_LONG_FORM | conn_pkt_flags(conn);
}

static uint8_t conn_pkt_flags_short(ngtcp2_conn *conn) {
  return (uint8_t)(conn_pkt_flags(conn) | ((conn->pktns.crypto.tx.ckm->flags &
                                            NGTCP2_CRYPTO_KM_FLAG_KEY_PHASE_ONE)
                                               ? NGTCP2_PKT_FLAG_KEY_PHASE
                                               : NGTCP2_PKT_FLAG_NONE));
}

/*
 * conn_write_handshake_pkt writes handshake packet in the buffer
 * pointed by |dest| whose length is |destlen|.  |type| specifies long
 * packet type.  It should be either NGTCP2_PKT_INITIAL or
 * NGTCP2_PKT_HANDSHAKE_PKT.
 *
 * |write_datalen| is the minimum length of application data ready to
 * send in subsequent 0RTT packet.
 *
 * This function returns the number of bytes written in |dest| if it
 * succeeds, or one of the following negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User-defined callback function failed.
 */
static ngtcp2_ssize
conn_write_handshake_pkt(ngtcp2_conn *conn, ngtcp2_pkt_info *pi, uint8_t *dest,
                         size_t destlen, uint8_t type, uint8_t flags,
                         uint64_t write_datalen, ngtcp2_tstamp ts) {
  int rv;
  ngtcp2_ppe ppe;
  ngtcp2_pkt_hd hd;
  ngtcp2_frame_chain *frq = NULL, **pfrc = &frq;
  ngtcp2_frame_chain *nfrc;
  ngtcp2_frame *ackfr = NULL, lfr;
  ngtcp2_ssize spktlen;
  ngtcp2_crypto_cc cc;
  ngtcp2_rtb_entry *rtbent;
  ngtcp2_pktns *pktns;
  size_t left;
  uint16_t rtb_entry_flags = NGTCP2_RTB_ENTRY_FLAG_NONE;
  int require_padding = (flags & NGTCP2_WRITE_PKT_FLAG_REQUIRE_PADDING) != 0;
  int pkt_empty = 1;
  int padded = 0;
  int hd_logged = 0;
  uint64_t crypto_offset;
  ngtcp2_ssize num_reclaimed;
  uint32_t version;

  switch (type) {
  case NGTCP2_PKT_INITIAL:
    if (!conn->in_pktns) {
      return 0;
    }
    assert(conn->in_pktns->crypto.tx.ckm);
    pktns = conn->in_pktns;
    version = conn->negotiated_version ? conn->negotiated_version
                                       : conn->client_chosen_version;
    if (version == conn->client_chosen_version) {
      cc.ckm = pktns->crypto.tx.ckm;
      cc.hp_ctx = pktns->crypto.tx.hp_ctx;
    } else {
      assert(conn->vneg.version == version);

      cc.ckm = conn->vneg.tx.ckm;
      cc.hp_ctx = conn->vneg.tx.hp_ctx;
    }
    break;
  case NGTCP2_PKT_HANDSHAKE:
    if (!conn->hs_pktns || !conn->hs_pktns->crypto.tx.ckm) {
      return 0;
    }
    pktns = conn->hs_pktns;
    version = conn->negotiated_version;
    cc.ckm = pktns->crypto.tx.ckm;
    cc.hp_ctx = pktns->crypto.tx.hp_ctx;
    break;
  default:
    ngtcp2_unreachable();
  }

  cc.aead = pktns->crypto.ctx.aead;
  cc.hp = pktns->crypto.ctx.hp;
  cc.encrypt = conn->callbacks.encrypt;
  cc.hp_mask = conn->callbacks.hp_mask;

  ngtcp2_pkt_hd_init(&hd, conn_pkt_flags_long(conn), type,
                     &conn->dcid.current.cid, &conn->oscid,
                     pktns->tx.last_pkt_num + 1, pktns_select_pkt_numlen(pktns),
                     version, 0);

  if (!conn->server && type == NGTCP2_PKT_INITIAL &&
      conn->local.settings.tokenlen) {
    hd.token = conn->local.settings.token;
    hd.tokenlen = conn->local.settings.tokenlen;
  }

  ngtcp2_ppe_init(&ppe, dest, destlen, &cc);

  rv = ngtcp2_ppe_encode_hd(&ppe, &hd);
  if (rv != 0) {
    assert(NGTCP2_ERR_NOBUF == rv);
    return 0;
  }

  if (!ngtcp2_ppe_ensure_hp_sample(&ppe)) {
    return 0;
  }

  rv = ngtcp2_conn_create_ack_frame(conn, &ackfr, pktns, type, ts,
                                    /* ack_delay = */ 0,
                                    NGTCP2_DEFAULT_ACK_DELAY_EXPONENT);
  if (rv != 0) {
    ngtcp2_frame_chain_list_objalloc_del(frq, &conn->frc_objalloc, conn->mem);
    return rv;
  }

  if (ackfr) {
    rv = conn_ppe_write_frame_hd_log(conn, &ppe, &hd_logged, &hd, ackfr);
    if (rv != 0) {
      assert(NGTCP2_ERR_NOBUF == rv);
    } else {
      ngtcp2_acktr_commit_ack(&pktns->acktr);
      ngtcp2_acktr_add_ack(&pktns->acktr, hd.pkt_num, ackfr->ack.largest_ack);
      pkt_empty = 0;
    }
  }

  /* Server requires at least NGTCP2_MAX_UDP_PAYLOAD_SIZE bytes in
     order to send ack-eliciting Initial packet. */
  if (!conn->server || type != NGTCP2_PKT_INITIAL ||
      destlen >= NGTCP2_MAX_UDP_PAYLOAD_SIZE) {
  build_pkt:
    for (; !ngtcp2_strm_streamfrq_empty(&pktns->crypto.strm);) {
      left = ngtcp2_ppe_left(&ppe);

      crypto_offset = ngtcp2_strm_streamfrq_unacked_offset(&pktns->crypto.strm);
      if (crypto_offset == (uint64_t)-1) {
        ngtcp2_strm_streamfrq_clear(&pktns->crypto.strm);
        break;
      }

      left = ngtcp2_pkt_crypto_max_datalen(crypto_offset, left, left);
      if (left == (size_t)-1) {
        break;
      }

      rv = ngtcp2_strm_streamfrq_pop(&pktns->crypto.strm, &nfrc, left);
      if (rv != 0) {
        assert(ngtcp2_err_is_fatal(rv));
        ngtcp2_frame_chain_list_objalloc_del(frq, &conn->frc_objalloc,
                                             conn->mem);
        return rv;
      }

      if (nfrc == NULL) {
        break;
      }

      rv = conn_ppe_write_frame_hd_log(conn, &ppe, &hd_logged, &hd, &nfrc->fr);
      if (rv != 0) {
        ngtcp2_unreachable();
      }

      *pfrc = nfrc;
      pfrc = &(*pfrc)->next;

      pkt_empty = 0;
      rtb_entry_flags |= NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING |
                         NGTCP2_RTB_ENTRY_FLAG_PTO_ELICITING |
                         NGTCP2_RTB_ENTRY_FLAG_RETRANSMITTABLE;
    }

    if (!(rtb_entry_flags & NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING) &&
        pktns->rtb.num_retransmittable && pktns->rtb.probe_pkt_left) {
      num_reclaimed = ngtcp2_rtb_reclaim_on_pto(&pktns->rtb, conn, pktns, 1);
      if (num_reclaimed < 0) {
        ngtcp2_frame_chain_list_objalloc_del(frq, &conn->frc_objalloc,
                                             conn->mem);
        return rv;
      }
      if (num_reclaimed) {
        goto build_pkt;
      }
      /* We had pktns->rtb.num_retransmittable > 0 but the contents of
         those packets have been acknowledged (i.e., retransmission in
         another packet).  For server, in this case, we don't have to
         send any probe packet.  Client needs to send probe packets
         until it knows that server has completed address validation or
         handshake has been confirmed. */
      if (pktns->rtb.num_pto_eliciting == 0 &&
          (conn->server ||
           (conn->flags & (NGTCP2_CONN_FLAG_SERVER_ADDR_VERIFIED |
                           NGTCP2_CONN_FLAG_HANDSHAKE_CONFIRMED)))) {
        pktns->rtb.probe_pkt_left = 0;
        ngtcp2_conn_set_loss_detection_timer(conn, ts);
        /* TODO If packet is empty, we should return now if cwnd is
           zero. */
      }
    }

    if (!(rtb_entry_flags & NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING) &&
        pktns->rtb.probe_pkt_left) {
      lfr.type = NGTCP2_FRAME_PING;

      rv = conn_ppe_write_frame_hd_log(conn, &ppe, &hd_logged, &hd, &lfr);
      if (rv != 0) {
        assert(rv == NGTCP2_ERR_NOBUF);
      } else {
        rtb_entry_flags |=
            NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING | NGTCP2_RTB_ENTRY_FLAG_PROBE;
        pkt_empty = 0;
      }
    }

    if (!pkt_empty) {
      if (!(rtb_entry_flags & NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING)) {
        if (ngtcp2_tstamp_elapsed(pktns->tx.non_ack_pkt_start_ts,
                                  conn->cstat.smoothed_rtt, ts)) {
          lfr.type = NGTCP2_FRAME_PING;

          rv = conn_ppe_write_frame_hd_log(conn, &ppe, &hd_logged, &hd, &lfr);
          if (rv != 0) {
            assert(rv == NGTCP2_ERR_NOBUF);
          } else {
            rtb_entry_flags |= NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING;
            pktns->tx.non_ack_pkt_start_ts = UINT64_MAX;
          }
        } else if (pktns->tx.non_ack_pkt_start_ts == UINT64_MAX) {
          pktns->tx.non_ack_pkt_start_ts = ts;
        }
      } else {
        pktns->tx.non_ack_pkt_start_ts = UINT64_MAX;
      }
    }
  }

  if (pkt_empty && !require_padding) {
    return 0;
  }

  /* If we cannot write another packet, then we need to add padding to
     Initial here. */
  if (conn_should_pad_pkt(
          conn, type, ngtcp2_ppe_left(&ppe), write_datalen,
          (rtb_entry_flags & NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING) != 0,
          require_padding)) {
    lfr.type = NGTCP2_FRAME_PADDING;
    lfr.padding.len = ngtcp2_ppe_padding(&ppe);
  } else if (pkt_empty) {
    return 0;
  } else {
    lfr.type = NGTCP2_FRAME_PADDING;
    lfr.padding.len = ngtcp2_ppe_padding_hp_sample(&ppe);
  }

  if (lfr.padding.len) {
    padded = 1;
    ngtcp2_log_tx_fr(&conn->log, &hd, &lfr);
    ngtcp2_qlog_write_frame(&conn->qlog, &lfr);
  }

  spktlen = ngtcp2_ppe_final(&ppe, NULL);
  if (spktlen < 0) {
    assert(ngtcp2_err_is_fatal((int)spktlen));
    ngtcp2_frame_chain_list_objalloc_del(frq, &conn->frc_objalloc, conn->mem);
    return spktlen;
  }

  ngtcp2_qlog_pkt_sent_end(&conn->qlog, &hd, (size_t)spktlen);

  if ((rtb_entry_flags & NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING) || padded) {
    if (pi) {
      conn_handle_tx_ecn(conn, pi, &rtb_entry_flags, pktns, &hd, ts);
    }

    rv = ngtcp2_rtb_entry_objalloc_new(&rtbent, &hd, frq, ts, (size_t)spktlen,
                                       rtb_entry_flags,
                                       &conn->rtb_entry_objalloc);
    if (rv != 0) {
      assert(ngtcp2_err_is_fatal(rv));
      ngtcp2_frame_chain_list_objalloc_del(frq, &conn->frc_objalloc, conn->mem);
      return rv;
    }

    rv = conn_on_pkt_sent(conn, &pktns->rtb, rtbent);
    if (rv != 0) {
      ngtcp2_rtb_entry_objalloc_del(rtbent, &conn->rtb_entry_objalloc,
                                    &conn->frc_objalloc, conn->mem);
      return rv;
    }

    if ((rtb_entry_flags & NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING) &&
        (conn->flags & NGTCP2_CONN_FLAG_RESTART_IDLE_TIMER_ON_WRITE)) {
      conn_restart_timer_on_write(conn, ts);
    }
  } else if (pi && conn->tx.ecn.state == NGTCP2_ECN_STATE_CAPABLE) {
    conn_handle_tx_ecn(conn, pi, NULL, pktns, &hd, ts);
  }

  if (pktns->rtb.probe_pkt_left &&
      (rtb_entry_flags & NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING)) {
    --pktns->rtb.probe_pkt_left;
  }

  conn_update_keep_alive_last_ts(conn, ts);

  conn->dcid.current.bytes_sent += (uint64_t)spktlen;

  conn->tx.pacing.pktlen += (size_t)spktlen;

  ngtcp2_qlog_metrics_updated(&conn->qlog, &conn->cstat);

  ++pktns->tx.last_pkt_num;

  return spktlen;
}

/*
 * conn_write_ack_pkt writes QUIC packet for type |type| which only
 * includes ACK frame in the buffer pointed by |dest| whose length is
 * |destlen|.
 *
 * This function returns the number of bytes written in |dest| if it
 * succeeds, or one of the following negative error codes:
 *
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User-defined callback function failed.
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 */
static ngtcp2_ssize conn_write_ack_pkt(ngtcp2_conn *conn, ngtcp2_pkt_info *pi,
                                       uint8_t *dest, size_t destlen,
                                       uint8_t type, ngtcp2_tstamp ts) {
  int rv;
  ngtcp2_frame *ackfr;
  ngtcp2_pktns *pktns;
  ngtcp2_duration ack_delay;
  uint64_t ack_delay_exponent;
  ngtcp2_ssize spktlen;

  assert(!(conn->flags & NGTCP2_CONN_FLAG_PPE_PENDING));

  switch (type) {
  case NGTCP2_PKT_INITIAL:
    assert(conn->server);
    pktns = conn->in_pktns;
    ack_delay = 0;
    ack_delay_exponent = NGTCP2_DEFAULT_ACK_DELAY_EXPONENT;
    break;
  case NGTCP2_PKT_HANDSHAKE:
    pktns = conn->hs_pktns;
    ack_delay = 0;
    ack_delay_exponent = NGTCP2_DEFAULT_ACK_DELAY_EXPONENT;
    break;
  case NGTCP2_PKT_1RTT:
    pktns = &conn->pktns;
    ack_delay = conn_compute_ack_delay(conn);
    ack_delay_exponent = conn->local.transport_params.ack_delay_exponent;
    break;
  default:
    ngtcp2_unreachable();
  }

  if (!pktns->crypto.tx.ckm) {
    return 0;
  }

  ackfr = NULL;
  rv = ngtcp2_conn_create_ack_frame(conn, &ackfr, pktns, type, ts, ack_delay,
                                    ack_delay_exponent);
  if (rv != 0) {
    return rv;
  }

  if (!ackfr) {
    return 0;
  }

  spktlen = ngtcp2_conn_write_single_frame_pkt(
      conn, pi, dest, destlen, type, NGTCP2_WRITE_PKT_FLAG_NONE,
      &conn->dcid.current.cid, ackfr, NGTCP2_RTB_ENTRY_FLAG_NONE, NULL, ts);

  if (spktlen <= 0) {
    return spktlen;
  }

  conn->dcid.current.bytes_sent += (uint64_t)spktlen;

  return spktlen;
}

static void conn_discard_pktns(ngtcp2_conn *conn, ngtcp2_pktns **ppktns,
                               ngtcp2_tstamp ts) {
  ngtcp2_pktns *pktns = *ppktns;
  uint64_t bytes_in_flight;

  bytes_in_flight = pktns->rtb.cc_bytes_in_flight;

  assert(conn->cstat.bytes_in_flight >= bytes_in_flight);

  conn->cstat.bytes_in_flight -= bytes_in_flight;
  conn->cstat.pto_count = 0;
  conn->cstat.last_tx_pkt_ts[pktns->rtb.pktns_id] = UINT64_MAX;
  conn->cstat.loss_time[pktns->rtb.pktns_id] = UINT64_MAX;

  conn_call_delete_crypto_aead_ctx(conn, &pktns->crypto.rx.ckm->aead_ctx);
  conn_call_delete_crypto_cipher_ctx(conn, &pktns->crypto.rx.hp_ctx);
  conn_call_delete_crypto_aead_ctx(conn, &pktns->crypto.tx.ckm->aead_ctx);
  conn_call_delete_crypto_cipher_ctx(conn, &pktns->crypto.tx.hp_ctx);

  pktns_del(pktns, conn->mem);
  *ppktns = NULL;

  ngtcp2_conn_set_loss_detection_timer(conn, ts);
}

/*
 * conn_discard_initial_state discards state for Initial packet number
 * space.
 */
static void conn_discard_initial_state(ngtcp2_conn *conn, ngtcp2_tstamp ts) {
  if (!conn->in_pktns) {
    return;
  }

  ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_CON,
                  "discarding Initial packet number space");

  conn_discard_pktns(conn, &conn->in_pktns, ts);

  conn_vneg_crypto_free(conn);

  memset(&conn->vneg.rx, 0, sizeof(conn->vneg.rx));
  memset(&conn->vneg.tx, 0, sizeof(conn->vneg.tx));
}

/*
 * conn_discard_handshake_state discards state for Handshake packet
 * number space.
 */
static void conn_discard_handshake_state(ngtcp2_conn *conn, ngtcp2_tstamp ts) {
  if (!conn->hs_pktns) {
    return;
  }

  ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_CON,
                  "discarding Handshake packet number space");

  conn_discard_pktns(conn, &conn->hs_pktns, ts);
}

/*
 * conn_discard_early_key discards early key.
 */
static void conn_discard_early_key(ngtcp2_conn *conn) {
  assert(conn->early.ckm);

  ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_CON, "discarding early key");

  conn_call_delete_crypto_aead_ctx(conn, &conn->early.ckm->aead_ctx);
  conn_call_delete_crypto_cipher_ctx(conn, &conn->early.hp_ctx);
  memset(&conn->early.hp_ctx, 0, sizeof(conn->early.hp_ctx));

  ngtcp2_crypto_km_del(conn->early.ckm, conn->mem);
  conn->early.ckm = NULL;
}

/*
 * conn_write_handshake_ack_pkts writes packets which contain ACK
 * frame only.  This function writes at most 2 packets for each
 * Initial and Handshake packet.
 */
static ngtcp2_ssize conn_write_handshake_ack_pkts(ngtcp2_conn *conn,
                                                  ngtcp2_pkt_info *pi,
                                                  uint8_t *dest, size_t destlen,
                                                  ngtcp2_tstamp ts) {
  ngtcp2_ssize res = 0, nwrite = 0;

  /* In the most cases, client sends ACK in conn_write_handshake_pkt.
     This function is only called when it is CWND limited or pacing
     limited.  It is not required for client to send ACK for server
     Initial.  This is because once it gets server Initial, it gets
     Handshake tx key and discards Initial key.  The only good reason
     to send ACK is give server RTT measurement early. */
  if (conn->server && conn->in_pktns) {
    nwrite =
        conn_write_ack_pkt(conn, pi, dest, destlen, NGTCP2_PKT_INITIAL, ts);
    if (nwrite < 0) {
      assert(nwrite != NGTCP2_ERR_NOBUF);
      return nwrite;
    }

    res += nwrite;
    dest += nwrite;
    destlen -= (size_t)nwrite;
  }

  if (conn->hs_pktns->crypto.tx.ckm) {
    nwrite =
        conn_write_ack_pkt(conn, pi, dest, destlen, NGTCP2_PKT_HANDSHAKE, ts);
    if (nwrite < 0) {
      assert(nwrite != NGTCP2_ERR_NOBUF);
      return nwrite;
    }

    res += nwrite;

    if (!conn->server && nwrite) {
      conn_discard_initial_state(conn, ts);
    }
  }

  return res;
}

/*
 * conn_write_client_initial writes Initial packet in the buffer
 * pointed by |dest| whose length is |destlen|.
 *
 * This function returns the number of bytes written in |dest| if it
 * succeeds, or one of the following negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User-defined callback function failed.
 */
static ngtcp2_ssize conn_write_client_initial(ngtcp2_conn *conn,
                                              ngtcp2_pkt_info *pi,
                                              uint8_t *dest, size_t destlen,
                                              uint64_t early_datalen,
                                              ngtcp2_tstamp ts) {
  int rv;

  rv = conn_call_client_initial(conn);
  if (rv != 0) {
    return rv;
  }

  return conn_write_handshake_pkt(conn, pi, dest, destlen, NGTCP2_PKT_INITIAL,
                                  NGTCP2_WRITE_PKT_FLAG_NONE, early_datalen,
                                  ts);
}

/*
 * dcid_tx_left returns the maximum number of bytes that server is
 * allowed to send to an unvalidated path associated to |dcid|.
 */
static uint64_t dcid_tx_left(ngtcp2_dcid *dcid) {
  if (dcid->flags & NGTCP2_DCID_FLAG_PATH_VALIDATED) {
    return SIZE_MAX;
  }
  /* From QUIC spec: Prior to validating the client address, servers
     MUST NOT send more than three times as many bytes as the number
     of bytes they have received. */
  assert(dcid->bytes_recv * 3 >= dcid->bytes_sent);

  return dcid->bytes_recv * 3 - dcid->bytes_sent;
}

/*
 * conn_server_tx_left returns the maximum number of bytes that server
 * is allowed to send to an unvalidated path.
 */
static uint64_t conn_server_tx_left(ngtcp2_conn *conn, ngtcp2_dcid *dcid) {
  assert(conn->server);

  /* If pv->dcid has the current path, use conn->dcid.current.  This
     is because conn->dcid.current gets update for bytes_recv and
     bytes_sent. */
  if (ngtcp2_path_eq(&dcid->ps.path, &conn->dcid.current.ps.path)) {
    return dcid_tx_left(&conn->dcid.current);
  }

  return dcid_tx_left(dcid);
}

/*
 * conn_write_handshake_pkts writes Initial and Handshake packets in
 * the buffer pointed by |dest| whose length is |destlen|.
 *
 * This function returns the number of bytes written in |dest| if it
 * succeeds, or one of the following negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User-defined callback function failed.
 */
static ngtcp2_ssize conn_write_handshake_pkts(ngtcp2_conn *conn,
                                              ngtcp2_pkt_info *pi,
                                              uint8_t *dest, size_t destlen,
                                              uint64_t write_datalen,
                                              ngtcp2_tstamp ts) {
  ngtcp2_ssize nwrite;
  ngtcp2_ssize res = 0;
  ngtcp2_rtb_entry *rtbent;
  uint8_t wflags = NGTCP2_WRITE_PKT_FLAG_NONE;
  ngtcp2_conn_stat *cstat = &conn->cstat;
  ngtcp2_ksl_it it;

  /* As a client, we would like to discard Initial packet number space
     when sending the first Handshake packet.  When sending Handshake
     packet, it should be one of 1) sending ACK, 2) sending PTO probe
     packet, or 3) sending CRYPTO.  If we have pending acknowledgement
     for Initial, then do not discard Initial packet number space.
     Otherwise, if either 1) or 2) is satisfied, discard Initial
     packet number space.  When sending Handshake CRYPTO, it indicates
     that client has received Handshake CRYPTO from server.  Initial
     packet number space is discarded because 1) is met.  If there is
     pending Initial ACK, Initial packet number space is discarded
     after writing the first Handshake packet.
   */
  if (!conn->server && conn->hs_pktns->crypto.tx.ckm && conn->in_pktns &&
      !ngtcp2_acktr_require_active_ack(&conn->in_pktns->acktr,
                                       /* max_ack_delay = */ 0, ts) &&
      (ngtcp2_acktr_require_active_ack(&conn->hs_pktns->acktr,
                                       /* max_ack_delay = */ 0, ts) ||
       conn->hs_pktns->rtb.probe_pkt_left)) {
    /* Discard Initial state here so that Handshake packet is not
       padded. */
    conn_discard_initial_state(conn, ts);
  } else if (conn->in_pktns) {
    nwrite =
        conn_write_handshake_pkt(conn, pi, dest, destlen, NGTCP2_PKT_INITIAL,
                                 NGTCP2_WRITE_PKT_FLAG_NONE, write_datalen, ts);
    if (nwrite < 0) {
      assert(nwrite != NGTCP2_ERR_NOBUF);
      return nwrite;
    }

    if (nwrite == 0) {
      if (conn->server &&
          (conn->in_pktns->rtb.probe_pkt_left ||
           !ngtcp2_strm_streamfrq_empty(&conn->in_pktns->crypto.strm))) {
        if (cstat->loss_detection_timer != UINT64_MAX &&
            conn_server_tx_left(conn, &conn->dcid.current) <
                NGTCP2_MAX_UDP_PAYLOAD_SIZE) {
          ngtcp2_log_info(
              &conn->log, NGTCP2_LOG_EVENT_LDC,
              "loss detection timer canceled due to amplification limit");
          cstat->loss_detection_timer = UINT64_MAX;
        }

        return 0;
      }
    } else {
      res += nwrite;
      dest += nwrite;
      destlen -= (size_t)nwrite;

      if (destlen) {
        /* We might have already added padding to Initial, but in that
           case, we should have destlen == 0 and no Handshake packet
           will be written. */
        if (conn->server) {
          it = ngtcp2_rtb_head(&conn->in_pktns->rtb);
          if (!ngtcp2_ksl_it_end(&it)) {
            rtbent = ngtcp2_ksl_it_get(&it);
            if (rtbent->flags & NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING) {
              wflags |= NGTCP2_WRITE_PKT_FLAG_REQUIRE_PADDING;
            }
          }
        } else {
          wflags |= NGTCP2_WRITE_PKT_FLAG_REQUIRE_PADDING;
        }
      }
    }
  }

  nwrite = conn_write_handshake_pkt(
      conn, pi, dest, destlen, NGTCP2_PKT_HANDSHAKE, wflags, write_datalen, ts);
  if (nwrite < 0) {
    assert(nwrite != NGTCP2_ERR_NOBUF);
    return nwrite;
  }

  res += nwrite;

  if (!conn->server && conn->hs_pktns->crypto.tx.ckm && nwrite) {
    /* We don't need to send further Initial packet if we have
       Handshake key and sent something with it.  So discard initial
       state here. */
    conn_discard_initial_state(conn, ts);
  }

  return res;
}

/*
 * conn_initial_stream_rx_offset returns the initial maximum offset of
 * data for a stream denoted by |stream_id|.
 */
static uint64_t conn_initial_stream_rx_offset(ngtcp2_conn *conn,
                                              int64_t stream_id) {
  int local_stream = conn_local_stream(conn, stream_id);

  if (bidi_stream(stream_id)) {
    if (local_stream) {
      return conn->local.transport_params.initial_max_stream_data_bidi_local;
    }
    return conn->local.transport_params.initial_max_stream_data_bidi_remote;
  }

  if (local_stream) {
    return 0;
  }
  return conn->local.transport_params.initial_max_stream_data_uni;
}

/*
 * conn_should_send_max_stream_data returns nonzero if MAX_STREAM_DATA
 * frame should be send for |strm|.
 */
static int conn_should_send_max_stream_data(ngtcp2_conn *conn,
                                            ngtcp2_strm *strm) {
  uint64_t inc = strm->rx.unsent_max_offset - strm->rx.max_offset;
  (void)conn;

  return strm->rx.window < 2 * inc;
}

/*
 * conn_should_send_max_data returns nonzero if MAX_DATA frame should
 * be sent.
 */
static int conn_should_send_max_data(ngtcp2_conn *conn) {
  uint64_t inc = conn->rx.unsent_max_offset - conn->rx.max_offset;

  return conn->rx.window < 2 * inc;
}

/*
 * conn_required_num_new_connection_id returns the number of
 * additional connection ID the local endpoint has to provide to the
 * remote endpoint.
 */
static size_t conn_required_num_new_connection_id(ngtcp2_conn *conn) {
  uint64_t n;
  size_t len = ngtcp2_ksl_len(&conn->scid.set);
  size_t lim;

  if (len >= NGTCP2_MAX_SCID_POOL_SIZE) {
    return 0;
  }

  assert(NGTCP2_MAX_SCID_POOL_SIZE >= conn->scid.num_in_flight);

  lim = NGTCP2_MAX_SCID_POOL_SIZE - conn->scid.num_in_flight;
  if (lim == 0) {
    return 0;
  }

  assert(conn->remote.transport_params);
  assert(conn->remote.transport_params->active_connection_id_limit);

  /* len includes retired CID.  We don't provide extra CID if doing so
     exceeds NGTCP2_MAX_SCID_POOL_SIZE. */

  n = conn->remote.transport_params->active_connection_id_limit +
      conn->scid.num_retired;

  n = ngtcp2_min(NGTCP2_MAX_SCID_POOL_SIZE, n) - len;

  return (size_t)ngtcp2_min(lim, n);
}

/*
 * conn_enqueue_new_connection_id generates additional connection IDs
 * and prepares to send them to the remote endpoint.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User-defined callback function failed.
 */
static int conn_enqueue_new_connection_id(ngtcp2_conn *conn) {
  size_t i, need = conn_required_num_new_connection_id(conn);
  size_t cidlen = conn->oscid.datalen;
  ngtcp2_cid cid;
  uint64_t seq;
  int rv;
  uint8_t token[NGTCP2_STATELESS_RESET_TOKENLEN];
  ngtcp2_frame_chain *nfrc;
  ngtcp2_pktns *pktns = &conn->pktns;
  ngtcp2_scid *scid;
  ngtcp2_ksl_it it;

  for (i = 0; i < need; ++i) {
    rv = conn_call_get_new_connection_id(conn, &cid, token, cidlen);
    if (rv != 0) {
      return rv;
    }

    if (cid.datalen != cidlen) {
      return NGTCP2_ERR_CALLBACK_FAILURE;
    }

    /* Assert uniqueness */
    it = ngtcp2_ksl_lower_bound(&conn->scid.set, &cid);
    if (!ngtcp2_ksl_it_end(&it) &&
        ngtcp2_cid_eq(ngtcp2_ksl_it_key(&it), &cid)) {
      return NGTCP2_ERR_CALLBACK_FAILURE;
    }

    seq = ++conn->scid.last_seq;

    scid = ngtcp2_mem_malloc(conn->mem, sizeof(*scid));
    if (scid == NULL) {
      return NGTCP2_ERR_NOMEM;
    }

    ngtcp2_scid_init(scid, seq, &cid);

    rv = ngtcp2_ksl_insert(&conn->scid.set, NULL, &scid->cid, scid);
    if (rv != 0) {
      ngtcp2_mem_free(conn->mem, scid);
      return rv;
    }

    rv = ngtcp2_frame_chain_objalloc_new(&nfrc, &conn->frc_objalloc);
    if (rv != 0) {
      return rv;
    }

    nfrc->fr.type = NGTCP2_FRAME_NEW_CONNECTION_ID;
    nfrc->fr.new_connection_id.seq = seq;
    nfrc->fr.new_connection_id.retire_prior_to = 0;
    nfrc->fr.new_connection_id.cid = cid;
    memcpy(nfrc->fr.new_connection_id.stateless_reset_token, token,
           sizeof(token));
    nfrc->next = pktns->tx.frq;
    pktns->tx.frq = nfrc;

    assert(NGTCP2_MAX_SCID_POOL_SIZE > conn->scid.num_in_flight);

    ++conn->scid.num_in_flight;
  }

  return 0;
}

/*
 * conn_remove_retired_connection_id removes the already retired
 * connection ID.  It waits PTO before actually removing a connection
 * ID after it receives RETIRE_CONNECTION_ID from peer to catch
 * reordered packets.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User-defined callback function failed.
 */
static int conn_remove_retired_connection_id(ngtcp2_conn *conn,
                                             ngtcp2_duration pto,
                                             ngtcp2_tstamp ts) {
  ngtcp2_duration timeout = pto;
  ngtcp2_scid *scid;
  ngtcp2_dcid *dcid;
  int rv;

  for (; !ngtcp2_pq_empty(&conn->scid.used);) {
    scid = ngtcp2_struct_of(ngtcp2_pq_top(&conn->scid.used), ngtcp2_scid, pe);

    if (!ngtcp2_tstamp_elapsed(scid->retired_ts, timeout, ts)) {
      break;
    }

    assert(scid->flags & NGTCP2_SCID_FLAG_RETIRED);

    rv = conn_call_remove_connection_id(conn, &scid->cid);
    if (rv != 0) {
      return rv;
    }

    ngtcp2_ksl_remove(&conn->scid.set, NULL, &scid->cid);
    ngtcp2_pq_pop(&conn->scid.used);
    ngtcp2_mem_free(conn->mem, scid);

    assert(conn->scid.num_retired);
    --conn->scid.num_retired;
  }

  for (; ngtcp2_ringbuf_len(&conn->dcid.retired.rb);) {
    dcid = ngtcp2_ringbuf_get(&conn->dcid.retired.rb, 0);
    if (dcid->retired_ts + timeout >= ts) {
      break;
    }

    rv = conn_call_deactivate_dcid(conn, dcid);
    if (rv != 0) {
      return rv;
    }

    ngtcp2_ringbuf_pop_front(&conn->dcid.retired.rb);
  }

  return 0;
}

/*
 * conn_min_short_pktlen returns the minimum length of Short packet
 * this endpoint sends.
 */
static size_t conn_min_short_pktlen(ngtcp2_conn *conn) {
  return conn->dcid.current.cid.datalen + NGTCP2_MIN_PKT_EXPANDLEN;
}

/*
 * conn_handle_unconfirmed_key_update_from_remote deals with key
 * update which has not been confirmed yet and initiated by the remote
 * endpoint.
 *
 * If key update was initiated by the remote endpoint, acknowledging a
 * packet encrypted with the new key completes key update procedure.
 */
static void conn_handle_unconfirmed_key_update_from_remote(ngtcp2_conn *conn,
                                                           int64_t largest_ack,
                                                           ngtcp2_tstamp ts) {
  if (!(conn->flags & NGTCP2_CONN_FLAG_KEY_UPDATE_NOT_CONFIRMED) ||
      (conn->flags & NGTCP2_CONN_FLAG_KEY_UPDATE_INITIATOR) ||
      largest_ack < conn->pktns.crypto.rx.ckm->pkt_num) {
    return;
  }

  conn->flags &= (uint32_t)~NGTCP2_CONN_FLAG_KEY_UPDATE_NOT_CONFIRMED;
  conn->crypto.key_update.confirmed_ts = ts;

  ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_CRY, "key update confirmed");
}

static uint64_t conn_tx_strmq_first_cycle(ngtcp2_conn *conn);

/*
 * strm_should_send_stream_data_blocked returns nonzero if
 * STREAM_DATA_BLOCKED frame should be sent to |strm|.
 */
static int strm_should_send_stream_data_blocked(ngtcp2_strm *strm) {
  return strm->tx.offset == strm->tx.max_offset &&
         strm->tx.last_blocked_offset != strm->tx.max_offset;
}

/*
 * conn_should_send_data_blocked returns nonzero if DATA_BLOCKED frame
 * should be sent.
 */
static int conn_should_send_data_blocked(ngtcp2_conn *conn) {
  return conn->tx.offset == conn->tx.max_offset &&
         conn->tx.last_blocked_offset != conn->tx.max_offset;
}

/*
 * conn_reset_ppe_pending clears NGTCP2_CONN_FLAG_PPE_PENDING flag and
 * nullifies conn->pkt.
 */
static void conn_reset_ppe_pending(ngtcp2_conn *conn) {
  conn->flags &= (uint32_t)~NGTCP2_CONN_FLAG_PPE_PENDING;

  memset(&conn->pkt, 0, sizeof(conn->pkt));
}

/*
 * conn_write_pkt writes a protected packet in the buffer pointed by
 * |dest| whose length if |destlen|.  |type| specifies the type of
 * packet.  It can be NGTCP2_PKT_1RTT or NGTCP2_PKT_0RTT.
 *
 * This function can send new stream data.  In order to send stream
 * data, specify the underlying stream and parameters to
 * |vmsg|->stream.  If |vmsg|->stream.fin is set to nonzero, it
 * signals that the given data is the final portion of the stream.
 * |vmsg|->stream.data vector of length |vmsg|->stream.datacnt
 * specifies stream data to send.  The number of bytes sent to the
 * stream is assigned to *|vmsg|->stream.pdatalen.  If 0 length STREAM
 * data is sent, 0 is assigned to it.  The caller should initialize
 * *|vmsg|->stream.pdatalen to -1.
 *
 * If |require_padding| is nonzero, padding bytes are added to occupy
 * the remaining packet payload.
 *
 * This function returns the number of bytes written in |dest| if it
 * succeeds, or one of the following negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User-defined callback function failed.
 * NGTCP2_ERR_STREAM_DATA_BLOCKED
 *     Stream data could not be written because of flow control.
 */
static ngtcp2_ssize conn_write_pkt(ngtcp2_conn *conn, ngtcp2_pkt_info *pi,
                                   uint8_t *dest, size_t destlen,
                                   ngtcp2_vmsg *vmsg, uint8_t type,
                                   uint8_t flags, ngtcp2_tstamp ts) {
  int rv = 0;
  ngtcp2_crypto_cc *cc = &conn->pkt.cc;
  ngtcp2_ppe *ppe = &conn->pkt.ppe;
  ngtcp2_pkt_hd *hd = &conn->pkt.hd;
  ngtcp2_frame *ackfr = NULL, lfr;
  ngtcp2_ssize nwrite;
  ngtcp2_frame_chain **pfrc, *nfrc, *frc;
  ngtcp2_rtb_entry *ent;
  ngtcp2_strm *strm;
  int pkt_empty = 1;
  uint64_t ndatalen = 0;
  int send_stream = 0;
  int stream_blocked = 0;
  int send_datagram = 0;
  ngtcp2_pktns *pktns = &conn->pktns;
  size_t left;
  uint64_t datalen = 0;
  ngtcp2_vec data[NGTCP2_MAX_STREAM_DATACNT];
  size_t datacnt;
  uint16_t rtb_entry_flags = NGTCP2_RTB_ENTRY_FLAG_NONE;
  int hd_logged = 0;
  ngtcp2_path_challenge_entry *pcent;
  uint8_t hd_flags = NGTCP2_PKT_FLAG_NONE;
  int require_padding = (flags & NGTCP2_WRITE_PKT_FLAG_REQUIRE_PADDING) != 0;
  int write_more = (flags & NGTCP2_WRITE_PKT_FLAG_MORE) != 0;
  int ppe_pending = (conn->flags & NGTCP2_CONN_FLAG_PPE_PENDING) != 0;
  size_t min_pktlen = conn_min_short_pktlen(conn);
  int padded = 0;
  ngtcp2_cc_pkt cc_pkt;
  uint64_t crypto_offset;
  uint64_t stream_offset;
  ngtcp2_ssize num_reclaimed;
  int fin;
  uint64_t target_max_data;
  ngtcp2_conn_stat *cstat = &conn->cstat;
  uint64_t delta;
  const ngtcp2_cid *scid = NULL;
  int keep_alive_expired = 0;
  uint32_t version = 0;

  /* Return 0 if destlen is less than minimum packet length which can
     trigger Stateless Reset */
  if (destlen < min_pktlen) {
    return 0;
  }

  if (vmsg) {
    switch (vmsg->type) {
    case NGTCP2_VMSG_TYPE_STREAM:
      datalen = ngtcp2_vec_len(vmsg->stream.data, vmsg->stream.datacnt);
      ndatalen = conn_enforce_flow_control(conn, vmsg->stream.strm, datalen);
      /* 0 length STREAM frame is allowed */
      if (ndatalen || datalen == 0) {
        send_stream = 1;
      } else {
        stream_blocked = 1;
      }
      break;
    case NGTCP2_VMSG_TYPE_DATAGRAM:
      datalen = ngtcp2_vec_len(vmsg->datagram.data, vmsg->datagram.datacnt);
      send_datagram = 1;
      break;
    default:
      break;
    }
  }

  if (!ppe_pending) {
    switch (type) {
    case NGTCP2_PKT_1RTT:
      hd_flags = conn_pkt_flags_short(conn);
      scid = NULL;
      cc->aead = pktns->crypto.ctx.aead;
      cc->hp = pktns->crypto.ctx.hp;
      cc->ckm = pktns->crypto.tx.ckm;
      cc->hp_ctx = pktns->crypto.tx.hp_ctx;

      assert(conn->negotiated_version);

      version = conn->negotiated_version;

      /* transport parameter is only valid after handshake completion
         which means we don't know how many connection ID that remote
         peer can accept before handshake completion.  Because server
         can use remote transport parameters sending stream data in
         0.5 RTT, it is also allowed to use remote transport
         parameters here.  */
      if (conn->oscid.datalen &&
          (conn->server || conn_is_tls_handshake_completed(conn))) {
        rv = conn_enqueue_new_connection_id(conn);
        if (rv != 0) {
          return rv;
        }
      }

      break;
    case NGTCP2_PKT_0RTT:
      assert(!conn->server);
      if (!conn->early.ckm) {
        return 0;
      }
      hd_flags = conn_pkt_flags_long(conn);
      scid = &conn->oscid;
      cc->aead = conn->early.ctx.aead;
      cc->hp = conn->early.ctx.hp;
      cc->ckm = conn->early.ckm;
      cc->hp_ctx = conn->early.hp_ctx;
      version = conn->client_chosen_version;
      break;
    default:
      /* Unreachable */
      ngtcp2_unreachable();
    }

    cc->encrypt = conn->callbacks.encrypt;
    cc->hp_mask = conn->callbacks.hp_mask;

    if (conn_should_send_max_data(conn)) {
      rv = ngtcp2_frame_chain_objalloc_new(&nfrc, &conn->frc_objalloc);
      if (rv != 0) {
        return rv;
      }

      if (conn->local.settings.max_window &&
          conn->tx.last_max_data_ts != UINT64_MAX &&
          ts - conn->tx.last_max_data_ts <
              NGTCP2_FLOW_WINDOW_RTT_FACTOR * cstat->smoothed_rtt &&
          conn->local.settings.max_window > conn->rx.window) {
        target_max_data = NGTCP2_FLOW_WINDOW_SCALING_FACTOR * conn->rx.window;
        if (target_max_data > conn->local.settings.max_window) {
          target_max_data = conn->local.settings.max_window;
        }

        delta = target_max_data - conn->rx.window;
        if (conn->rx.unsent_max_offset + delta > NGTCP2_MAX_VARINT) {
          delta = NGTCP2_MAX_VARINT - conn->rx.unsent_max_offset;
        }

        conn->rx.window = target_max_data;
      } else {
        delta = 0;
      }

      conn->tx.last_max_data_ts = ts;

      nfrc->fr.type = NGTCP2_FRAME_MAX_DATA;
      nfrc->fr.max_data.max_data = conn->rx.unsent_max_offset + delta;
      nfrc->next = pktns->tx.frq;
      pktns->tx.frq = nfrc;

      conn->rx.max_offset = conn->rx.unsent_max_offset =
          nfrc->fr.max_data.max_data;
    }

    if (stream_blocked && conn_should_send_max_data(conn)) {
      rv = ngtcp2_frame_chain_objalloc_new(&nfrc, &conn->frc_objalloc);
      if (rv != 0) {
        return rv;
      }

      nfrc->fr.type = NGTCP2_FRAME_DATA_BLOCKED;
      nfrc->fr.data_blocked.offset = conn->tx.max_offset;
      nfrc->next = pktns->tx.frq;
      pktns->tx.frq = nfrc;

      conn->tx.last_blocked_offset = conn->tx.max_offset;
    }

    if (stream_blocked && !ngtcp2_strm_is_tx_queued(vmsg->stream.strm) &&
        strm_should_send_stream_data_blocked(vmsg->stream.strm)) {
      assert(vmsg);
      assert(vmsg->type == NGTCP2_VMSG_TYPE_STREAM);

      vmsg->stream.strm->cycle = conn_tx_strmq_first_cycle(conn);
      rv = ngtcp2_conn_tx_strmq_push(conn, vmsg->stream.strm);
      if (rv != 0) {
        return rv;
      }
    }

    ngtcp2_pkt_hd_init(hd, hd_flags, type, &conn->dcid.current.cid, scid,
                       pktns->tx.last_pkt_num + 1,
                       pktns_select_pkt_numlen(pktns), version, 0);

    ngtcp2_ppe_init(ppe, dest, destlen, cc);

    rv = ngtcp2_ppe_encode_hd(ppe, hd);
    if (rv != 0) {
      assert(NGTCP2_ERR_NOBUF == rv);
      return 0;
    }

    if (!ngtcp2_ppe_ensure_hp_sample(ppe)) {
      return 0;
    }

    if (ngtcp2_ringbuf_len(&conn->rx.path_challenge.rb)) {
      pcent = ngtcp2_ringbuf_get(&conn->rx.path_challenge.rb, 0);

      /* PATH_RESPONSE is bound to the path that the corresponding
         PATH_CHALLENGE is received. */
      if (ngtcp2_path_eq(&conn->dcid.current.ps.path, &pcent->ps.path)) {
        lfr.type = NGTCP2_FRAME_PATH_RESPONSE;
        memcpy(lfr.path_response.data, pcent->data,
               sizeof(lfr.path_response.data));

        rv = conn_ppe_write_frame_hd_log(conn, ppe, &hd_logged, hd, &lfr);
        if (rv != 0) {
          assert(NGTCP2_ERR_NOBUF == rv);
        } else {
          ngtcp2_ringbuf_pop_front(&conn->rx.path_challenge.rb);

          pkt_empty = 0;
          rtb_entry_flags |= NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING;
          require_padding = require_padding || !conn->server ||
                            destlen >= NGTCP2_MAX_UDP_PAYLOAD_SIZE;
          /* We don't retransmit PATH_RESPONSE. */
        }
      }
    }

    rv = ngtcp2_conn_create_ack_frame(
        conn, &ackfr, pktns, type, ts, conn_compute_ack_delay(conn),
        conn->local.transport_params.ack_delay_exponent);
    if (rv != 0) {
      assert(ngtcp2_err_is_fatal(rv));
      return rv;
    }

    if (ackfr) {
      rv = conn_ppe_write_frame_hd_log(conn, ppe, &hd_logged, hd, ackfr);
      if (rv != 0) {
        assert(NGTCP2_ERR_NOBUF == rv);
      } else {
        ngtcp2_acktr_commit_ack(&pktns->acktr);
        ngtcp2_acktr_add_ack(&pktns->acktr, hd->pkt_num,
                             ackfr->ack.largest_ack);
        if (type == NGTCP2_PKT_1RTT) {
          conn_handle_unconfirmed_key_update_from_remote(
              conn, ackfr->ack.largest_ack, ts);
        }
        pkt_empty = 0;
      }
    }

  build_pkt:
    for (pfrc = &pktns->tx.frq; *pfrc;) {
      if ((*pfrc)->binder &&
          ((*pfrc)->binder->flags & NGTCP2_FRAME_CHAIN_BINDER_FLAG_ACK)) {
        frc = *pfrc;
        *pfrc = (*pfrc)->next;
        ngtcp2_frame_chain_objalloc_del(frc, &conn->frc_objalloc, conn->mem);
        continue;
      }

      switch ((*pfrc)->fr.type) {
      case NGTCP2_FRAME_RESET_STREAM:
        strm =
            ngtcp2_conn_find_stream(conn, (*pfrc)->fr.reset_stream.stream_id);
        if (strm == NULL ||
            !ngtcp2_strm_require_retransmit_reset_stream(strm)) {
          frc = *pfrc;
          *pfrc = (*pfrc)->next;
          ngtcp2_frame_chain_objalloc_del(frc, &conn->frc_objalloc, conn->mem);
          continue;
        }
        break;
      case NGTCP2_FRAME_STOP_SENDING:
        strm =
            ngtcp2_conn_find_stream(conn, (*pfrc)->fr.stop_sending.stream_id);
        if (strm == NULL ||
            !ngtcp2_strm_require_retransmit_stop_sending(strm)) {
          frc = *pfrc;
          *pfrc = (*pfrc)->next;
          ngtcp2_frame_chain_objalloc_del(frc, &conn->frc_objalloc, conn->mem);
          continue;
        }
        break;
      case NGTCP2_FRAME_STREAM:
        ngtcp2_unreachable();
      case NGTCP2_FRAME_MAX_STREAMS_BIDI:
        if ((*pfrc)->fr.max_streams.max_streams <
            conn->remote.bidi.max_streams) {
          frc = *pfrc;
          *pfrc = (*pfrc)->next;
          ngtcp2_frame_chain_objalloc_del(frc, &conn->frc_objalloc, conn->mem);
          continue;
        }
        break;
      case NGTCP2_FRAME_MAX_STREAMS_UNI:
        if ((*pfrc)->fr.max_streams.max_streams <
            conn->remote.uni.max_streams) {
          frc = *pfrc;
          *pfrc = (*pfrc)->next;
          ngtcp2_frame_chain_objalloc_del(frc, &conn->frc_objalloc, conn->mem);
          continue;
        }
        break;
      case NGTCP2_FRAME_MAX_STREAM_DATA:
        strm = ngtcp2_conn_find_stream(conn,
                                       (*pfrc)->fr.max_stream_data.stream_id);
        if (strm == NULL || !ngtcp2_strm_require_retransmit_max_stream_data(
                                strm, &(*pfrc)->fr.max_stream_data)) {
          frc = *pfrc;
          *pfrc = (*pfrc)->next;
          ngtcp2_frame_chain_objalloc_del(frc, &conn->frc_objalloc, conn->mem);
          continue;
        }
        break;
      case NGTCP2_FRAME_MAX_DATA:
        if ((*pfrc)->fr.max_data.max_data < conn->rx.max_offset) {
          frc = *pfrc;
          *pfrc = (*pfrc)->next;
          ngtcp2_frame_chain_objalloc_del(frc, &conn->frc_objalloc, conn->mem);
          continue;
        }
        break;
      case NGTCP2_FRAME_STREAM_DATA_BLOCKED:
        strm = ngtcp2_conn_find_stream(
            conn, (*pfrc)->fr.stream_data_blocked.stream_id);
        if (strm == NULL || !ngtcp2_strm_require_retransmit_stream_data_blocked(
                                strm, &(*pfrc)->fr.stream_data_blocked)) {
          frc = *pfrc;
          *pfrc = (*pfrc)->next;
          ngtcp2_frame_chain_objalloc_del(frc, &conn->frc_objalloc, conn->mem);
          continue;
        }
        break;
      case NGTCP2_FRAME_DATA_BLOCKED:
        if ((*pfrc)->fr.data_blocked.offset != conn->tx.max_offset) {
          frc = *pfrc;
          *pfrc = (*pfrc)->next;
          ngtcp2_frame_chain_objalloc_del(frc, &conn->frc_objalloc, conn->mem);
          continue;
        }
        break;
      case NGTCP2_FRAME_CRYPTO:
        ngtcp2_unreachable();
      }

      rv = conn_ppe_write_frame_hd_log(conn, ppe, &hd_logged, hd, &(*pfrc)->fr);
      if (rv != 0) {
        assert(NGTCP2_ERR_NOBUF == rv);
        break;
      }

      pkt_empty = 0;
      rtb_entry_flags |= NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING |
                         NGTCP2_RTB_ENTRY_FLAG_PTO_ELICITING |
                         NGTCP2_RTB_ENTRY_FLAG_RETRANSMITTABLE;
      pfrc = &(*pfrc)->next;
    }

    if (*pfrc == NULL) {
      for (; !ngtcp2_strm_streamfrq_empty(&pktns->crypto.strm);) {
        left = ngtcp2_ppe_left(ppe);

        crypto_offset =
            ngtcp2_strm_streamfrq_unacked_offset(&pktns->crypto.strm);
        if (crypto_offset == (uint64_t)-1) {
          ngtcp2_strm_streamfrq_clear(&pktns->crypto.strm);
          break;
        }

        left = ngtcp2_pkt_crypto_max_datalen(crypto_offset, left, left);

        if (left == (size_t)-1) {
          break;
        }

        rv = ngtcp2_strm_streamfrq_pop(&pktns->crypto.strm, &nfrc, left);
        if (rv != 0) {
          assert(ngtcp2_err_is_fatal(rv));
          return rv;
        }

        if (nfrc == NULL) {
          break;
        }

        rv = conn_ppe_write_frame_hd_log(conn, ppe, &hd_logged, hd, &nfrc->fr);
        if (rv != 0) {
          ngtcp2_unreachable();
        }

        *pfrc = nfrc;
        pfrc = &(*pfrc)->next;

        pkt_empty = 0;
        rtb_entry_flags |= NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING |
                           NGTCP2_RTB_ENTRY_FLAG_PTO_ELICITING |
                           NGTCP2_RTB_ENTRY_FLAG_RETRANSMITTABLE;
      }
    }

    if (*pfrc == NULL) {
      for (; !ngtcp2_pq_empty(&conn->tx.strmq);) {
        strm = ngtcp2_conn_tx_strmq_top(conn);

        if (strm->flags & NGTCP2_STRM_FLAG_SEND_RESET_STREAM) {
          rv = ngtcp2_frame_chain_objalloc_new(&nfrc, &conn->frc_objalloc);
          if (rv != 0) {
            return rv;
          }

          nfrc->fr.type = NGTCP2_FRAME_RESET_STREAM;
          nfrc->fr.reset_stream.stream_id = strm->stream_id;
          nfrc->fr.reset_stream.app_error_code =
              strm->tx.reset_stream_app_error_code;
          nfrc->fr.reset_stream.final_size = strm->tx.offset;
          *pfrc = nfrc;

          strm->flags &= ~NGTCP2_STRM_FLAG_SEND_RESET_STREAM;

          rv =
              conn_ppe_write_frame_hd_log(conn, ppe, &hd_logged, hd, &nfrc->fr);
          if (rv != 0) {
            assert(NGTCP2_ERR_NOBUF == rv);

            break;
          }

          pkt_empty = 0;
          rtb_entry_flags |= NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING |
                             NGTCP2_RTB_ENTRY_FLAG_PTO_ELICITING |
                             NGTCP2_RTB_ENTRY_FLAG_RETRANSMITTABLE;
          pfrc = &(*pfrc)->next;
        }

        if (strm->flags & NGTCP2_STRM_FLAG_SEND_STOP_SENDING) {
          if ((strm->flags & NGTCP2_STRM_FLAG_SHUT_RD) &&
              ngtcp2_strm_rx_offset(strm) == strm->rx.last_offset) {
            strm->flags &= ~NGTCP2_STRM_FLAG_SEND_STOP_SENDING;
          } else {
            rv = conn_call_stream_stop_sending(
                conn, strm->stream_id, strm->tx.stop_sending_app_error_code,
                strm->stream_user_data);
            if (rv != 0) {
              assert(ngtcp2_err_is_fatal(rv));

              return rv;
            }

            rv = ngtcp2_frame_chain_objalloc_new(&nfrc, &conn->frc_objalloc);
            if (rv != 0) {
              return rv;
            }

            nfrc->fr.type = NGTCP2_FRAME_STOP_SENDING;
            nfrc->fr.stop_sending.stream_id = strm->stream_id;
            nfrc->fr.stop_sending.app_error_code =
                strm->tx.stop_sending_app_error_code;
            *pfrc = nfrc;

            strm->flags &= ~NGTCP2_STRM_FLAG_SEND_STOP_SENDING;

            rv = conn_ppe_write_frame_hd_log(conn, ppe, &hd_logged, hd,
                                             &nfrc->fr);
            if (rv != 0) {
              assert(NGTCP2_ERR_NOBUF == rv);

              break;
            }

            pkt_empty = 0;
            rtb_entry_flags |= NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING |
                               NGTCP2_RTB_ENTRY_FLAG_PTO_ELICITING |
                               NGTCP2_RTB_ENTRY_FLAG_RETRANSMITTABLE;
            pfrc = &(*pfrc)->next;
          }
        }

        if (!(strm->flags & NGTCP2_STRM_FLAG_SHUT_WR) &&
            strm_should_send_stream_data_blocked(strm)) {
          rv = ngtcp2_frame_chain_objalloc_new(&nfrc, &conn->frc_objalloc);
          if (rv != 0) {
            return rv;
          }

          nfrc->fr.type = NGTCP2_FRAME_STREAM_DATA_BLOCKED;
          nfrc->fr.stream_data_blocked.stream_id = strm->stream_id;
          nfrc->fr.stream_data_blocked.offset = strm->tx.max_offset;
          *pfrc = nfrc;

          strm->tx.last_blocked_offset = strm->tx.max_offset;

          rv =
              conn_ppe_write_frame_hd_log(conn, ppe, &hd_logged, hd, &nfrc->fr);
          if (rv != 0) {
            assert(NGTCP2_ERR_NOBUF == rv);

            break;
          }

          pkt_empty = 0;
          rtb_entry_flags |= NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING |
                             NGTCP2_RTB_ENTRY_FLAG_PTO_ELICITING |
                             NGTCP2_RTB_ENTRY_FLAG_RETRANSMITTABLE;
          pfrc = &(*pfrc)->next;
        }

        if (!(strm->flags &
              (NGTCP2_STRM_FLAG_SHUT_RD | NGTCP2_STRM_FLAG_STOP_SENDING)) &&
            conn_should_send_max_stream_data(conn, strm)) {
          rv = ngtcp2_frame_chain_objalloc_new(&nfrc, &conn->frc_objalloc);
          if (rv != 0) {
            assert(ngtcp2_err_is_fatal(rv));
            return rv;
          }

          if (conn->local.settings.max_stream_window &&
              strm->tx.last_max_stream_data_ts != UINT64_MAX &&
              ts - strm->tx.last_max_stream_data_ts <
                  NGTCP2_FLOW_WINDOW_RTT_FACTOR * cstat->smoothed_rtt &&
              conn->local.settings.max_stream_window > strm->rx.window) {
            target_max_data =
                NGTCP2_FLOW_WINDOW_SCALING_FACTOR * strm->rx.window;
            if (target_max_data > conn->local.settings.max_stream_window) {
              target_max_data = conn->local.settings.max_stream_window;
            }

            delta = target_max_data - strm->rx.window;
            if (strm->rx.unsent_max_offset + delta > NGTCP2_MAX_VARINT) {
              delta = NGTCP2_MAX_VARINT - strm->rx.unsent_max_offset;
            }

            strm->rx.window = target_max_data;
          } else {
            delta = 0;
          }

          strm->tx.last_max_stream_data_ts = ts;

          nfrc->fr.type = NGTCP2_FRAME_MAX_STREAM_DATA;
          nfrc->fr.max_stream_data.stream_id = strm->stream_id;
          nfrc->fr.max_stream_data.max_stream_data =
              strm->rx.unsent_max_offset + delta;
          *pfrc = nfrc;

          strm->rx.max_offset = strm->rx.unsent_max_offset =
              nfrc->fr.max_stream_data.max_stream_data;

          rv =
              conn_ppe_write_frame_hd_log(conn, ppe, &hd_logged, hd, &nfrc->fr);
          if (rv != 0) {
            assert(NGTCP2_ERR_NOBUF == rv);
            break;
          }

          pkt_empty = 0;
          rtb_entry_flags |= NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING |
                             NGTCP2_RTB_ENTRY_FLAG_PTO_ELICITING |
                             NGTCP2_RTB_ENTRY_FLAG_RETRANSMITTABLE;
          pfrc = &(*pfrc)->next;
        }

        if (ngtcp2_strm_streamfrq_empty(strm)) {
          ngtcp2_conn_tx_strmq_pop(conn);
          continue;
        }

        stream_offset = ngtcp2_strm_streamfrq_unacked_offset(strm);
        if (stream_offset == (uint64_t)-1) {
          ngtcp2_strm_streamfrq_clear(strm);
          ngtcp2_conn_tx_strmq_pop(conn);
          continue;
        }

        left = ngtcp2_ppe_left(ppe);

        left = ngtcp2_pkt_stream_max_datalen(strm->stream_id, stream_offset,
                                             left, left);

        if (left == (size_t)-1) {
          break;
        }

        rv = ngtcp2_strm_streamfrq_pop(strm, &nfrc, left);
        if (rv != 0) {
          assert(ngtcp2_err_is_fatal(rv));
          return rv;
        }

        if (nfrc == NULL) {
          /* TODO Why? */
          break;
        }

        rv = conn_ppe_write_frame_hd_log(conn, ppe, &hd_logged, hd, &nfrc->fr);
        if (rv != 0) {
          ngtcp2_unreachable();
        }

        *pfrc = nfrc;
        pfrc = &(*pfrc)->next;

        pkt_empty = 0;
        rtb_entry_flags |= NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING |
                           NGTCP2_RTB_ENTRY_FLAG_PTO_ELICITING |
                           NGTCP2_RTB_ENTRY_FLAG_RETRANSMITTABLE;

        if (ngtcp2_strm_streamfrq_empty(strm)) {
          ngtcp2_conn_tx_strmq_pop(conn);
          continue;
        }

        ngtcp2_conn_tx_strmq_pop(conn);
        ++strm->cycle;
        rv = ngtcp2_conn_tx_strmq_push(conn, strm);
        if (rv != 0) {
          assert(ngtcp2_err_is_fatal(rv));
          return rv;
        }
      }
    }

    /* Write MAX_STREAMS after RESET_STREAM so that we can extend
       stream ID space in one packet. */
    if (*pfrc == NULL &&
        conn->remote.bidi.unsent_max_streams > conn->remote.bidi.max_streams) {
      rv = conn_call_extend_max_remote_streams_bidi(
          conn, conn->remote.bidi.unsent_max_streams);
      if (rv != 0) {
        assert(ngtcp2_err_is_fatal(rv));
        return rv;
      }

      rv = ngtcp2_frame_chain_objalloc_new(&nfrc, &conn->frc_objalloc);
      if (rv != 0) {
        assert(ngtcp2_err_is_fatal(rv));
        return rv;
      }
      nfrc->fr.type = NGTCP2_FRAME_MAX_STREAMS_BIDI;
      nfrc->fr.max_streams.max_streams = conn->remote.bidi.unsent_max_streams;
      *pfrc = nfrc;

      conn->remote.bidi.max_streams = conn->remote.bidi.unsent_max_streams;

      rv = conn_ppe_write_frame_hd_log(conn, ppe, &hd_logged, hd, &(*pfrc)->fr);
      if (rv != 0) {
        assert(NGTCP2_ERR_NOBUF == rv);
      } else {
        pkt_empty = 0;
        rtb_entry_flags |= NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING |
                           NGTCP2_RTB_ENTRY_FLAG_PTO_ELICITING |
                           NGTCP2_RTB_ENTRY_FLAG_RETRANSMITTABLE;
        pfrc = &(*pfrc)->next;
      }
    }

    if (*pfrc == NULL &&
        conn->remote.uni.unsent_max_streams > conn->remote.uni.max_streams) {
      rv = conn_call_extend_max_remote_streams_uni(
          conn, conn->remote.uni.unsent_max_streams);
      if (rv != 0) {
        assert(ngtcp2_err_is_fatal(rv));
        return rv;
      }

      rv = ngtcp2_frame_chain_objalloc_new(&nfrc, &conn->frc_objalloc);
      if (rv != 0) {
        assert(ngtcp2_err_is_fatal(rv));
        return rv;
      }
      nfrc->fr.type = NGTCP2_FRAME_MAX_STREAMS_UNI;
      nfrc->fr.max_streams.max_streams = conn->remote.uni.unsent_max_streams;
      *pfrc = nfrc;

      conn->remote.uni.max_streams = conn->remote.uni.unsent_max_streams;

      rv = conn_ppe_write_frame_hd_log(conn, ppe, &hd_logged, hd, &(*pfrc)->fr);
      if (rv != 0) {
        assert(NGTCP2_ERR_NOBUF == rv);
      } else {
        pkt_empty = 0;
        rtb_entry_flags |= NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING |
                           NGTCP2_RTB_ENTRY_FLAG_PTO_ELICITING |
                           NGTCP2_RTB_ENTRY_FLAG_RETRANSMITTABLE;
        pfrc = &(*pfrc)->next;
      }
    }

    if (pktns->tx.frq == NULL && !send_stream && !send_datagram &&
        !(rtb_entry_flags & NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING) &&
        pktns->rtb.num_retransmittable && pktns->rtb.probe_pkt_left) {
      num_reclaimed = ngtcp2_rtb_reclaim_on_pto(&pktns->rtb, conn, pktns, 1);
      if (num_reclaimed < 0) {
        return rv;
      }
      if (num_reclaimed) {
        goto build_pkt;
      }

      /* We had pktns->rtb.num_retransmittable > 0 but we were unable
         to reclaim any frame.  In this case, we do not have to send
         any probe packet. */
      if (pktns->rtb.num_pto_eliciting == 0) {
        pktns->rtb.probe_pkt_left = 0;
        ngtcp2_conn_set_loss_detection_timer(conn, ts);

        if (pkt_empty && conn_cwnd_is_zero(conn) && !require_padding) {
          return 0;
        }
      }
    }
  } else {
    pfrc = conn->pkt.pfrc;
    rtb_entry_flags |= conn->pkt.rtb_entry_flags;
    pkt_empty = conn->pkt.pkt_empty;
    hd_logged = conn->pkt.hd_logged;
  }

  left = ngtcp2_ppe_left(ppe);

  if (*pfrc == NULL && send_stream &&
      (ndatalen = ngtcp2_pkt_stream_max_datalen(
           vmsg->stream.strm->stream_id, vmsg->stream.strm->tx.offset, ndatalen,
           left)) != (size_t)-1 &&
      (ndatalen || datalen == 0)) {
    datacnt = ngtcp2_vec_copy_at_most(data, NGTCP2_MAX_STREAM_DATACNT,
                                      vmsg->stream.data, vmsg->stream.datacnt,
                                      (size_t)ndatalen);
    ndatalen = ngtcp2_vec_len(data, datacnt);

    assert((datacnt == 0 && datalen == 0) || (datacnt && datalen));

    rv = ngtcp2_frame_chain_stream_datacnt_objalloc_new(
        &nfrc, datacnt, &conn->frc_objalloc, conn->mem);
    if (rv != 0) {
      assert(ngtcp2_err_is_fatal(rv));
      return rv;
    }

    nfrc->fr.stream.type = NGTCP2_FRAME_STREAM;
    nfrc->fr.stream.flags = 0;
    nfrc->fr.stream.stream_id = vmsg->stream.strm->stream_id;
    nfrc->fr.stream.offset = vmsg->stream.strm->tx.offset;
    nfrc->fr.stream.datacnt = datacnt;
    ngtcp2_vec_copy(nfrc->fr.stream.data, data, datacnt);

    fin = (vmsg->stream.flags & NGTCP2_WRITE_STREAM_FLAG_FIN) &&
          ndatalen == datalen;
    nfrc->fr.stream.fin = (uint8_t)fin;

    rv = conn_ppe_write_frame_hd_log(conn, ppe, &hd_logged, hd, &nfrc->fr);
    if (rv != 0) {
      ngtcp2_unreachable();
    }

    *pfrc = nfrc;
    pfrc = &(*pfrc)->next;

    pkt_empty = 0;
    rtb_entry_flags |= NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING |
                       NGTCP2_RTB_ENTRY_FLAG_PTO_ELICITING |
                       NGTCP2_RTB_ENTRY_FLAG_RETRANSMITTABLE;

    vmsg->stream.strm->tx.offset += ndatalen;
    conn->tx.offset += ndatalen;

    if (fin) {
      ngtcp2_strm_shutdown(vmsg->stream.strm, NGTCP2_STRM_FLAG_SHUT_WR);
    }

    if (vmsg->stream.pdatalen) {
      *vmsg->stream.pdatalen = (ngtcp2_ssize)ndatalen;
    }
  } else {
    send_stream = 0;
  }

  if (vmsg && vmsg->type == NGTCP2_VMSG_TYPE_STREAM &&
      ((stream_blocked && *pfrc == NULL) ||
       (send_stream &&
        !(vmsg->stream.strm->flags & NGTCP2_STRM_FLAG_SHUT_WR)))) {
    if (conn_should_send_data_blocked(conn)) {
      rv = ngtcp2_frame_chain_objalloc_new(&nfrc, &conn->frc_objalloc);
      if (rv != 0) {
        assert(ngtcp2_err_is_fatal(rv));

        return rv;
      }

      nfrc->fr.type = NGTCP2_FRAME_DATA_BLOCKED;
      nfrc->fr.data_blocked.offset = conn->tx.offset;

      rv = conn_ppe_write_frame_hd_log(conn, ppe, &hd_logged, hd, &nfrc->fr);
      if (rv != 0) {
        assert(NGTCP2_ERR_NOBUF == rv);

        /* We cannot add nfrc to pktns->tx.frq here. */
        ngtcp2_frame_chain_objalloc_del(nfrc, &conn->frc_objalloc, conn->mem);
      } else {
        *pfrc = nfrc;
        pfrc = &(*pfrc)->next;

        pkt_empty = 0;
        rtb_entry_flags |= NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING |
                           NGTCP2_RTB_ENTRY_FLAG_PTO_ELICITING |
                           NGTCP2_RTB_ENTRY_FLAG_RETRANSMITTABLE;

        conn->tx.last_blocked_offset = conn->tx.max_offset;
      }
    }

    strm = vmsg->stream.strm;

    if (strm_should_send_stream_data_blocked(strm)) {
      rv = ngtcp2_frame_chain_objalloc_new(&nfrc, &conn->frc_objalloc);
      if (rv != 0) {
        assert(ngtcp2_err_is_fatal(rv));

        return rv;
      }

      nfrc->fr.type = NGTCP2_FRAME_STREAM_DATA_BLOCKED;
      nfrc->fr.stream_data_blocked.stream_id = strm->stream_id;
      nfrc->fr.stream_data_blocked.offset = strm->tx.max_offset;

      rv = conn_ppe_write_frame_hd_log(conn, ppe, &hd_logged, hd, &nfrc->fr);
      if (rv != 0) {
        assert(NGTCP2_ERR_NOBUF == rv);

        /* We cannot add nfrc to pktns->tx.frq here. */
        ngtcp2_frame_chain_objalloc_del(nfrc, &conn->frc_objalloc, conn->mem);

        if (!ngtcp2_strm_is_tx_queued(strm)) {
          strm->cycle = conn_tx_strmq_first_cycle(conn);
          rv = ngtcp2_conn_tx_strmq_push(conn, strm);
          if (rv != 0) {
            return rv;
          }
        }
      } else {
        *pfrc = nfrc;
        pfrc = &(*pfrc)->next;

        pkt_empty = 0;
        rtb_entry_flags |= NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING |
                           NGTCP2_RTB_ENTRY_FLAG_PTO_ELICITING |
                           NGTCP2_RTB_ENTRY_FLAG_RETRANSMITTABLE;

        strm->tx.last_blocked_offset = strm->tx.max_offset;
      }
    }
  }

  if (*pfrc == NULL && send_datagram &&
      left >= ngtcp2_pkt_datagram_framelen((size_t)datalen)) {
    if (conn->callbacks.ack_datagram || conn->callbacks.lost_datagram) {
      rv = ngtcp2_frame_chain_objalloc_new(&nfrc, &conn->frc_objalloc);
      if (rv != 0) {
        assert(ngtcp2_err_is_fatal(rv));
        return rv;
      }

      nfrc->fr.datagram.type = NGTCP2_FRAME_DATAGRAM_LEN;
      nfrc->fr.datagram.dgram_id = vmsg->datagram.dgram_id;
      nfrc->fr.datagram.datacnt = vmsg->datagram.datacnt;
      nfrc->fr.datagram.data = (ngtcp2_vec *)vmsg->datagram.data;

      rv = conn_ppe_write_frame_hd_log(conn, ppe, &hd_logged, hd, &nfrc->fr);
      assert(rv == 0);

      /* Because DATAGRAM will not be retransmitted, we do not use
         data anymore.  Just nullify it.  The only reason to keep
         track a frame is keep dgram_id to pass it to
         ngtcp2_ack_datagram or ngtcp2_lost_datagram callbacks. */
      nfrc->fr.datagram.datacnt = 0;
      nfrc->fr.datagram.data = NULL;

      *pfrc = nfrc;
      pfrc = &(*pfrc)->next;
    } else {
      lfr.datagram.type = NGTCP2_FRAME_DATAGRAM_LEN;
      lfr.datagram.datacnt = vmsg->datagram.datacnt;
      lfr.datagram.data = (ngtcp2_vec *)vmsg->datagram.data;

      rv = conn_ppe_write_frame_hd_log(conn, ppe, &hd_logged, hd, &lfr);
      assert(rv == 0);
    }

    pkt_empty = 0;
    rtb_entry_flags |= NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING |
                       NGTCP2_RTB_ENTRY_FLAG_PTO_ELICITING |
                       NGTCP2_RTB_ENTRY_FLAG_DATAGRAM;

    if (vmsg->datagram.paccepted) {
      *vmsg->datagram.paccepted = 1;
    }
  } else {
    send_datagram = 0;
  }

  if (pkt_empty) {
    if (*pfrc == NULL && rv == 0 && stream_blocked &&
        (write_more || !require_padding) &&
        ngtcp2_conn_get_max_data_left(conn)) {
      if (write_more) {
        conn->pkt.pfrc = pfrc;
        conn->pkt.pkt_empty = pkt_empty;
        conn->pkt.rtb_entry_flags = rtb_entry_flags;
        conn->pkt.hd_logged = hd_logged;
        conn->flags |= NGTCP2_CONN_FLAG_PPE_PENDING;
      }

      return NGTCP2_ERR_STREAM_DATA_BLOCKED;
    }

    keep_alive_expired =
        type == NGTCP2_PKT_1RTT && conn_keep_alive_expired(conn, ts);

    if (conn->pktns.rtb.probe_pkt_left == 0 && !keep_alive_expired &&
        !require_padding) {
      conn_reset_ppe_pending(conn);

      return 0;
    }
  } else if (write_more) {
    conn->pkt.pfrc = pfrc;
    conn->pkt.pkt_empty = pkt_empty;
    conn->pkt.rtb_entry_flags = rtb_entry_flags;
    conn->pkt.hd_logged = hd_logged;
    conn->flags |= NGTCP2_CONN_FLAG_PPE_PENDING;

    assert(vmsg);

    switch (vmsg->type) {
    case NGTCP2_VMSG_TYPE_STREAM:
      if (send_stream) {
        if (ngtcp2_ppe_left(ppe)) {
          return NGTCP2_ERR_WRITE_MORE;
        }
        break;
      }

      if (*pfrc == NULL && ngtcp2_conn_get_max_data_left(conn) &&
          stream_blocked) {
        return NGTCP2_ERR_STREAM_DATA_BLOCKED;
      }
      break;
    case NGTCP2_VMSG_TYPE_DATAGRAM:
      if (send_datagram && ngtcp2_ppe_left(ppe)) {
        return NGTCP2_ERR_WRITE_MORE;
      }
      /* If DATAGRAM cannot be written due to insufficient space,
         continue to create a packet with the hope that application
         calls ngtcp2_conn_writev_datagram again. */
      break;
    default:
      ngtcp2_unreachable();
    }
  }

  if (!(rtb_entry_flags & NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING)) {
    if (ngtcp2_tstamp_elapsed(pktns->tx.non_ack_pkt_start_ts,
                              cstat->smoothed_rtt, ts) ||
        keep_alive_expired || conn->pktns.rtb.probe_pkt_left) {
      lfr.type = NGTCP2_FRAME_PING;

      rv = conn_ppe_write_frame_hd_log(conn, ppe, &hd_logged, hd, &lfr);
      if (rv != 0) {
        assert(rv == NGTCP2_ERR_NOBUF);
        /* TODO If buffer is too small, PING cannot be written if
           packet is still empty. */
      } else {
        rtb_entry_flags |= NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING;
        if (conn->pktns.rtb.probe_pkt_left) {
          rtb_entry_flags |= NGTCP2_RTB_ENTRY_FLAG_PROBE;
        }
        pktns->tx.non_ack_pkt_start_ts = UINT64_MAX;
      }
    } else if (pktns->tx.non_ack_pkt_start_ts == UINT64_MAX) {
      pktns->tx.non_ack_pkt_start_ts = ts;
    }
  } else {
    pktns->tx.non_ack_pkt_start_ts = UINT64_MAX;
  }

  /* TODO Push STREAM frame back to ngtcp2_strm if there is an error
     before ngtcp2_rtb_entry is safely created and added. */
  if (require_padding ||
      /* Making full sized packet will help GSO a bit */
      ngtcp2_ppe_left(ppe) < 10) {
    lfr.padding.len = ngtcp2_ppe_padding(ppe);
  } else if (type == NGTCP2_PKT_1RTT) {
    lfr.padding.len = ngtcp2_ppe_padding_size(ppe, min_pktlen);
  } else {
    lfr.padding.len = ngtcp2_ppe_padding_hp_sample(ppe);
  }

  if (lfr.padding.len) {
    lfr.type = NGTCP2_FRAME_PADDING;
    padded = 1;
    ngtcp2_log_tx_fr(&conn->log, hd, &lfr);
    ngtcp2_qlog_write_frame(&conn->qlog, &lfr);
  }

  nwrite = ngtcp2_ppe_final(ppe, NULL);
  if (nwrite < 0) {
    assert(ngtcp2_err_is_fatal((int)nwrite));
    return nwrite;
  }

  ++cc->ckm->use_count;

  ngtcp2_qlog_pkt_sent_end(&conn->qlog, hd, (size_t)nwrite);

  /* TODO ack-eliciting vs needs-tracking */
  /* probe packet needs tracking but it does not need ACK, could be lost. */
  if ((rtb_entry_flags & NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING) || padded) {
    if (pi) {
      conn_handle_tx_ecn(conn, pi, &rtb_entry_flags, pktns, hd, ts);
    }

    rv = ngtcp2_rtb_entry_objalloc_new(&ent, hd, NULL, ts, (size_t)nwrite,
                                       rtb_entry_flags,
                                       &conn->rtb_entry_objalloc);
    if (rv != 0) {
      assert(ngtcp2_err_is_fatal((int)nwrite));
      return rv;
    }

    if (*pfrc != pktns->tx.frq) {
      ent->frc = pktns->tx.frq;
      pktns->tx.frq = *pfrc;
      *pfrc = NULL;
    }

    if ((rtb_entry_flags & NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING) &&
        pktns->rtb.num_ack_eliciting == 0 && conn->cc.event) {
      conn->cc.event(&conn->cc, &conn->cstat, NGTCP2_CC_EVENT_TYPE_TX_START,
                     ts);
    }

    rv = conn_on_pkt_sent(conn, &pktns->rtb, ent);
    if (rv != 0) {
      assert(ngtcp2_err_is_fatal(rv));
      ngtcp2_rtb_entry_objalloc_del(ent, &conn->rtb_entry_objalloc,
                                    &conn->frc_objalloc, conn->mem);
      return rv;
    }

    if (rtb_entry_flags & NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING) {
      if (conn->cc.on_pkt_sent) {
        conn->cc.on_pkt_sent(
            &conn->cc, &conn->cstat,
            ngtcp2_cc_pkt_init(&cc_pkt, hd->pkt_num, (size_t)nwrite,
                               NGTCP2_PKTNS_ID_APPLICATION, ts, ent->rst.lost,
                               ent->rst.tx_in_flight, ent->rst.is_app_limited));
      }

      if (conn->flags & NGTCP2_CONN_FLAG_RESTART_IDLE_TIMER_ON_WRITE) {
        conn_restart_timer_on_write(conn, ts);
      }
    }
  } else if (pi && conn->tx.ecn.state == NGTCP2_ECN_STATE_CAPABLE) {
    conn_handle_tx_ecn(conn, pi, NULL, pktns, hd, ts);
  }

  conn_reset_ppe_pending(conn);

  if (pktns->rtb.probe_pkt_left &&
      (rtb_entry_flags & NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING)) {
    --pktns->rtb.probe_pkt_left;

    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_CON, "probe pkt size=%td",
                    nwrite);
  }

  conn_update_keep_alive_last_ts(conn, ts);

  conn->dcid.current.bytes_sent += (uint64_t)nwrite;

  conn->tx.pacing.pktlen += (size_t)nwrite;

  ngtcp2_qlog_metrics_updated(&conn->qlog, &conn->cstat);

  ++pktns->tx.last_pkt_num;

  return nwrite;
}

ngtcp2_ssize ngtcp2_conn_write_single_frame_pkt(
    ngtcp2_conn *conn, ngtcp2_pkt_info *pi, uint8_t *dest, size_t destlen,
    uint8_t type, uint8_t flags, const ngtcp2_cid *dcid, ngtcp2_frame *fr,
    uint16_t rtb_entry_flags, const ngtcp2_path *path, ngtcp2_tstamp ts) {
  int rv;
  ngtcp2_ppe ppe;
  ngtcp2_pkt_hd hd;
  ngtcp2_frame lfr;
  ngtcp2_ssize nwrite;
  ngtcp2_crypto_cc cc;
  ngtcp2_pktns *pktns;
  uint8_t hd_flags;
  ngtcp2_rtb_entry *rtbent;
  int padded = 0;
  const ngtcp2_cid *scid;
  uint32_t version;

  switch (type) {
  case NGTCP2_PKT_INITIAL:
    pktns = conn->in_pktns;
    hd_flags = conn_pkt_flags_long(conn);
    scid = &conn->oscid;
    version = conn->negotiated_version ? conn->negotiated_version
                                       : conn->client_chosen_version;
    if (version == conn->client_chosen_version) {
      cc.ckm = pktns->crypto.tx.ckm;
      cc.hp_ctx = pktns->crypto.tx.hp_ctx;
    } else {
      assert(version == conn->vneg.version);

      cc.ckm = conn->vneg.tx.ckm;
      cc.hp_ctx = conn->vneg.tx.hp_ctx;
    }
    break;
  case NGTCP2_PKT_HANDSHAKE:
    pktns = conn->hs_pktns;
    hd_flags = conn_pkt_flags_long(conn);
    scid = &conn->oscid;
    version = conn->negotiated_version;
    cc.ckm = pktns->crypto.tx.ckm;
    cc.hp_ctx = pktns->crypto.tx.hp_ctx;
    break;
  case NGTCP2_PKT_1RTT:
    pktns = &conn->pktns;
    hd_flags = conn_pkt_flags_short(conn);
    scid = NULL;
    version = conn->negotiated_version;
    cc.ckm = pktns->crypto.tx.ckm;
    cc.hp_ctx = pktns->crypto.tx.hp_ctx;
    break;
  default:
    /* We don't support 0-RTT packet in this function. */
    ngtcp2_unreachable();
  }

  cc.aead = pktns->crypto.ctx.aead;
  cc.hp = pktns->crypto.ctx.hp;
  cc.encrypt = conn->callbacks.encrypt;
  cc.hp_mask = conn->callbacks.hp_mask;

  ngtcp2_pkt_hd_init(&hd, hd_flags, type, dcid, scid,
                     pktns->tx.last_pkt_num + 1, pktns_select_pkt_numlen(pktns),
                     version, 0);

  ngtcp2_ppe_init(&ppe, dest, destlen, &cc);

  rv = ngtcp2_ppe_encode_hd(&ppe, &hd);
  if (rv != 0) {
    assert(NGTCP2_ERR_NOBUF == rv);
    return 0;
  }

  if (!ngtcp2_ppe_ensure_hp_sample(&ppe)) {
    return 0;
  }

  ngtcp2_log_tx_pkt_hd(&conn->log, &hd);
  ngtcp2_qlog_pkt_sent_start(&conn->qlog);

  rv = conn_ppe_write_frame(conn, &ppe, &hd, fr);
  if (rv != 0) {
    assert(NGTCP2_ERR_NOBUF == rv);
    return 0;
  }

  lfr.type = NGTCP2_FRAME_PADDING;
  if (flags & NGTCP2_WRITE_PKT_FLAG_REQUIRE_PADDING) {
    lfr.padding.len = ngtcp2_ppe_padding(&ppe);
  } else {
    switch (fr->type) {
    case NGTCP2_FRAME_PATH_CHALLENGE:
    case NGTCP2_FRAME_PATH_RESPONSE:
      if (!conn->server || destlen >= NGTCP2_MAX_UDP_PAYLOAD_SIZE) {
        lfr.padding.len = ngtcp2_ppe_padding(&ppe);
      } else {
        lfr.padding.len = 0;
      }
      break;
    default:
      if (type == NGTCP2_PKT_1RTT) {
        lfr.padding.len =
            ngtcp2_ppe_padding_size(&ppe, conn_min_short_pktlen(conn));
      } else {
        lfr.padding.len = ngtcp2_ppe_padding_hp_sample(&ppe);
      }
    }
  }
  if (lfr.padding.len) {
    padded = 1;
    ngtcp2_log_tx_fr(&conn->log, &hd, &lfr);
    ngtcp2_qlog_write_frame(&conn->qlog, &lfr);
  }

  nwrite = ngtcp2_ppe_final(&ppe, NULL);
  if (nwrite < 0) {
    return nwrite;
  }

  if (type == NGTCP2_PKT_1RTT) {
    ++cc.ckm->use_count;
  }

  ngtcp2_qlog_pkt_sent_end(&conn->qlog, &hd, (size_t)nwrite);

  /* Do this when we are sure that there is no error. */
  switch (fr->type) {
  case NGTCP2_FRAME_ACK:
  case NGTCP2_FRAME_ACK_ECN:
    ngtcp2_acktr_commit_ack(&pktns->acktr);
    ngtcp2_acktr_add_ack(&pktns->acktr, hd.pkt_num, fr->ack.largest_ack);
    if (type == NGTCP2_PKT_1RTT) {
      conn_handle_unconfirmed_key_update_from_remote(conn, fr->ack.largest_ack,
                                                     ts);
    }
    break;
  }

  if (((rtb_entry_flags & NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING) || padded) &&
      (!path || ngtcp2_path_eq(&conn->dcid.current.ps.path, path))) {
    if (pi && (conn->tx.ecn.state == NGTCP2_ECN_STATE_CAPABLE ||
               !(rtb_entry_flags & NGTCP2_RTB_ENTRY_FLAG_PMTUD_PROBE))) {
      conn_handle_tx_ecn(conn, pi, &rtb_entry_flags, pktns, &hd, ts);
    }

    rv = ngtcp2_rtb_entry_objalloc_new(&rtbent, &hd, NULL, ts, (size_t)nwrite,
                                       rtb_entry_flags,
                                       &conn->rtb_entry_objalloc);
    if (rv != 0) {
      return rv;
    }

    rv = conn_on_pkt_sent(conn, &pktns->rtb, rtbent);
    if (rv != 0) {
      ngtcp2_rtb_entry_objalloc_del(rtbent, &conn->rtb_entry_objalloc,
                                    &conn->frc_objalloc, conn->mem);
      return rv;
    }

    if (rtb_entry_flags & NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING) {
      if (conn->flags & NGTCP2_CONN_FLAG_RESTART_IDLE_TIMER_ON_WRITE) {
        conn_restart_timer_on_write(conn, ts);
      }

      if (pktns->rtb.probe_pkt_left && path &&
          ngtcp2_path_eq(&conn->dcid.current.ps.path, path)) {
        --pktns->rtb.probe_pkt_left;

        ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_CON, "probe pkt size=%td",
                        nwrite);
      }
    }
  } else if (pi && conn->tx.ecn.state == NGTCP2_ECN_STATE_CAPABLE) {
    conn_handle_tx_ecn(conn, pi, NULL, pktns, &hd, ts);
  }

  if (path && ngtcp2_path_eq(&conn->dcid.current.ps.path, path)) {
    conn_update_keep_alive_last_ts(conn, ts);
  }

  if (!padded) {
    switch (fr->type) {
    case NGTCP2_FRAME_ACK:
    case NGTCP2_FRAME_ACK_ECN:
      break;
    default:
      conn->tx.pacing.pktlen += (size_t)nwrite;
    }
  } else {
    conn->tx.pacing.pktlen += (size_t)nwrite;
  }

  ngtcp2_qlog_metrics_updated(&conn->qlog, &conn->cstat);

  ++pktns->tx.last_pkt_num;

  return nwrite;
}

/*
 * conn_process_early_rtb makes any pending 0RTT packet 1RTT packet.
 */
static void conn_process_early_rtb(ngtcp2_conn *conn) {
  ngtcp2_rtb_entry *ent;
  ngtcp2_rtb *rtb = &conn->pktns.rtb;
  ngtcp2_ksl_it it;

  for (it = ngtcp2_rtb_head(rtb); !ngtcp2_ksl_it_end(&it);
       ngtcp2_ksl_it_next(&it)) {
    ent = ngtcp2_ksl_it_get(&it);

    if ((ent->hd.flags & NGTCP2_PKT_FLAG_LONG_FORM) == 0 ||
        ent->hd.type != NGTCP2_PKT_0RTT) {
      continue;
    }

    /*  0-RTT packet is retransmitted as a 1RTT packet. */
    ent->hd.flags &= (uint8_t)~NGTCP2_PKT_FLAG_LONG_FORM;
    ent->hd.type = NGTCP2_PKT_1RTT;
  }
}

/*
 * conn_handshake_remnants_left returns nonzero if there may be
 * handshake packets the local endpoint has to send, including new
 * packets and lost ones.
 */
static int conn_handshake_remnants_left(ngtcp2_conn *conn) {
  ngtcp2_pktns *in_pktns = conn->in_pktns;
  ngtcp2_pktns *hs_pktns = conn->hs_pktns;

  return !conn_is_tls_handshake_completed(conn) ||
         (in_pktns && (in_pktns->rtb.num_pto_eliciting ||
                       !ngtcp2_strm_streamfrq_empty(&in_pktns->crypto.strm))) ||
         (hs_pktns && (hs_pktns->rtb.num_pto_eliciting ||
                       !ngtcp2_strm_streamfrq_empty(&hs_pktns->crypto.strm)));
}

/*
 * conn_retire_dcid_seq retires destination connection ID denoted by
 * |seq|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 * NGTCP2_ERR_CONNECTION_ID_LIMIT
 *     The number of unacknowledged retirement exceeds the limit.
 */
static int conn_retire_dcid_seq(ngtcp2_conn *conn, uint64_t seq) {
  ngtcp2_pktns *pktns = &conn->pktns;
  ngtcp2_frame_chain *nfrc;
  int rv;

  rv = ngtcp2_conn_track_retired_dcid_seq(conn, seq);
  if (rv != 0) {
    return rv;
  }

  rv = ngtcp2_frame_chain_objalloc_new(&nfrc, &conn->frc_objalloc);
  if (rv != 0) {
    return rv;
  }

  nfrc->fr.type = NGTCP2_FRAME_RETIRE_CONNECTION_ID;
  nfrc->fr.retire_connection_id.seq = seq;
  nfrc->next = pktns->tx.frq;
  pktns->tx.frq = nfrc;

  return 0;
}

/*
 * conn_retire_dcid retires |dcid|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory
 */
static int conn_retire_dcid(ngtcp2_conn *conn, const ngtcp2_dcid *dcid,
                            ngtcp2_tstamp ts) {
  ngtcp2_ringbuf *rb = &conn->dcid.retired.rb;
  ngtcp2_dcid *dest, *stale_dcid;
  int rv;

  assert(dcid->cid.datalen);

  if (ngtcp2_ringbuf_full(rb)) {
    stale_dcid = ngtcp2_ringbuf_get(rb, 0);
    rv = conn_call_deactivate_dcid(conn, stale_dcid);
    if (rv != 0) {
      return rv;
    }

    ngtcp2_ringbuf_pop_front(rb);
  }

  dest = ngtcp2_ringbuf_push_back(rb);
  ngtcp2_dcid_copy(dest, dcid);
  dest->retired_ts = ts;

  return conn_retire_dcid_seq(conn, dcid->seq);
}

/*
 * conn_bind_dcid stores the DCID to |*pdcid| bound to |path|.  If
 * such DCID is not found, bind the new DCID to |path| and stores it
 * to |*pdcid|.  If a remote endpoint uses zero-length connection ID,
 * the pointer to conn->dcid.current is assigned to |*pdcid|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_CONN_ID_BLOCKED
 *     No unused DCID is available
 * NGTCP2_ERR_NOMEM
 *     Out of memory
 */
static int conn_bind_dcid(ngtcp2_conn *conn, ngtcp2_dcid **pdcid,
                          const ngtcp2_path *path, ngtcp2_tstamp ts) {
  ngtcp2_dcid *dcid, *ndcid;
  ngtcp2_cid cid;
  size_t i, len;
  int rv;

  assert(!ngtcp2_path_eq(&conn->dcid.current.ps.path, path));
  assert(!conn->pv || !ngtcp2_path_eq(&conn->pv->dcid.ps.path, path));
  assert(!conn->pv || !(conn->pv->flags & NGTCP2_PV_FLAG_FALLBACK_ON_FAILURE) ||
         !ngtcp2_path_eq(&conn->pv->fallback_dcid.ps.path, path));

  len = ngtcp2_ringbuf_len(&conn->dcid.bound.rb);
  for (i = 0; i < len; ++i) {
    dcid = ngtcp2_ringbuf_get(&conn->dcid.bound.rb, i);

    if (ngtcp2_path_eq(&dcid->ps.path, path)) {
      *pdcid = dcid;
      return 0;
    }
  }

  if (conn->dcid.current.cid.datalen == 0) {
    ndcid = ngtcp2_ringbuf_push_back(&conn->dcid.bound.rb);
    ngtcp2_cid_zero(&cid);
    ngtcp2_dcid_init(ndcid, ++conn->dcid.zerolen_seq, &cid, NULL);
    ngtcp2_dcid_set_path(ndcid, path);

    *pdcid = ndcid;

    return 0;
  }

  if (ngtcp2_ringbuf_len(&conn->dcid.unused.rb) == 0) {
    return NGTCP2_ERR_CONN_ID_BLOCKED;
  }

  if (ngtcp2_ringbuf_full(&conn->dcid.bound.rb)) {
    dcid = ngtcp2_ringbuf_get(&conn->dcid.bound.rb, 0);
    rv = conn_retire_dcid(conn, dcid, ts);
    if (rv != 0) {
      return rv;
    }
  }

  dcid = ngtcp2_ringbuf_get(&conn->dcid.unused.rb, 0);
  ndcid = ngtcp2_ringbuf_push_back(&conn->dcid.bound.rb);

  ngtcp2_dcid_copy(ndcid, dcid);
  ndcid->bound_ts = ts;
  ngtcp2_dcid_set_path(ndcid, path);

  ngtcp2_ringbuf_pop_front(&conn->dcid.unused.rb);

  *pdcid = ndcid;

  return 0;
}

static int conn_start_pmtud(ngtcp2_conn *conn) {
  int rv;
  size_t hard_max_udp_payload_size;

  assert(!conn->local.settings.no_pmtud);
  assert(!conn->pmtud);
  assert(conn_is_tls_handshake_completed(conn));
  assert(conn->remote.transport_params);
  assert(conn->remote.transport_params->max_udp_payload_size >=
         NGTCP2_MAX_UDP_PAYLOAD_SIZE);

  hard_max_udp_payload_size = (size_t)ngtcp2_min(
      conn->remote.transport_params->max_udp_payload_size,
      (uint64_t)conn->local.settings.max_tx_udp_payload_size);

  rv = ngtcp2_pmtud_new(&conn->pmtud, conn->dcid.current.max_udp_payload_size,
                        hard_max_udp_payload_size,
                        conn->pktns.tx.last_pkt_num + 1, conn->mem);
  if (rv != 0) {
    return rv;
  }

  if (ngtcp2_pmtud_finished(conn->pmtud)) {
    ngtcp2_conn_stop_pmtud(conn);
  }

  return 0;
}

int ngtcp2_conn_start_pmtud(ngtcp2_conn *conn) {
  return conn_start_pmtud(conn);
}

void ngtcp2_conn_stop_pmtud(ngtcp2_conn *conn) {
  if (!conn->pmtud) {
    return;
  }

  ngtcp2_pmtud_del(conn->pmtud);

  conn->pmtud = NULL;
}

static ngtcp2_ssize conn_write_pmtud_probe(ngtcp2_conn *conn,
                                           ngtcp2_pkt_info *pi, uint8_t *dest,
                                           size_t destlen, ngtcp2_tstamp ts) {
  size_t probelen;
  ngtcp2_ssize nwrite;
  ngtcp2_frame lfr;

  assert(conn->pmtud);
  assert(!ngtcp2_pmtud_finished(conn->pmtud));

  if (!ngtcp2_pmtud_require_probe(conn->pmtud)) {
    return 0;
  }

  probelen = ngtcp2_pmtud_probelen(conn->pmtud);
  if (probelen > destlen) {
    return 0;
  }

  ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_CON,
                  "sending PMTUD probe packet len=%zu", probelen);

  lfr.type = NGTCP2_FRAME_PING;

  nwrite = ngtcp2_conn_write_single_frame_pkt(
      conn, pi, dest, probelen, NGTCP2_PKT_1RTT,
      NGTCP2_WRITE_PKT_FLAG_REQUIRE_PADDING, &conn->dcid.current.cid, &lfr,
      NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING |
          NGTCP2_RTB_ENTRY_FLAG_PTO_ELICITING |
          NGTCP2_RTB_ENTRY_FLAG_PMTUD_PROBE,
      NULL, ts);
  if (nwrite < 0) {
    return nwrite;
  }

  assert(nwrite);

  ngtcp2_pmtud_probe_sent(conn->pmtud, conn_compute_pto(conn, &conn->pktns),
                          ts);

  return nwrite;
}

/*
 * conn_stop_pv stops the path validation which is currently running.
 * This function does nothing if no path validation is currently being
 * performed.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory
 */
static int conn_stop_pv(ngtcp2_conn *conn, ngtcp2_tstamp ts) {
  int rv = 0;
  ngtcp2_pv *pv = conn->pv;

  if (pv == NULL) {
    return 0;
  }

  if (pv->dcid.cid.datalen && pv->dcid.seq != conn->dcid.current.seq) {
    rv = conn_retire_dcid(conn, &pv->dcid, ts);
    if (rv != 0) {
      goto fin;
    }
  }

  if ((pv->flags & NGTCP2_PV_FLAG_FALLBACK_ON_FAILURE) &&
      pv->fallback_dcid.cid.datalen &&
      pv->fallback_dcid.seq != conn->dcid.current.seq &&
      pv->fallback_dcid.seq != pv->dcid.seq) {
    rv = conn_retire_dcid(conn, &pv->fallback_dcid, ts);
    if (rv != 0) {
      goto fin;
    }
  }

fin:
  ngtcp2_pv_del(pv);
  conn->pv = NULL;

  return rv;
}

/*
 * conn_abort_pv aborts the current path validation and frees
 * resources allocated for it.  This function assumes that conn->pv is
 * not NULL.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User-defined callback function failed.
 */
static int conn_abort_pv(ngtcp2_conn *conn, ngtcp2_tstamp ts) {
  ngtcp2_pv *pv = conn->pv;
  int rv;

  assert(pv);

  if (!(pv->flags & NGTCP2_PV_FLAG_DONT_CARE)) {
    rv = conn_call_path_validation(conn, pv,
                                   NGTCP2_PATH_VALIDATION_RESULT_ABORTED);
    if (rv != 0) {
      return rv;
    }
  }

  return conn_stop_pv(conn, ts);
}

static size_t conn_shape_udp_payload(ngtcp2_conn *conn, const ngtcp2_dcid *dcid,
                                     size_t payloadlen) {
  if (conn->remote.transport_params &&
      conn->remote.transport_params->max_udp_payload_size) {
    assert(conn->remote.transport_params->max_udp_payload_size >=
           NGTCP2_MAX_UDP_PAYLOAD_SIZE);

    payloadlen =
        (size_t)ngtcp2_min((uint64_t)payloadlen,
                           conn->remote.transport_params->max_udp_payload_size);
  }

  payloadlen =
      ngtcp2_min(payloadlen, conn->local.settings.max_tx_udp_payload_size);

  if (conn->local.settings.no_tx_udp_payload_size_shaping) {
    return payloadlen;
  }

  return ngtcp2_min(payloadlen, dcid->max_udp_payload_size);
}

static void conn_reset_congestion_state(ngtcp2_conn *conn, ngtcp2_tstamp ts);

/*
 * conn_on_path_validation_failed is called when path validation
 * fails.  This function may delete |pv|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User-defined callback function failed.
 */
static int conn_on_path_validation_failed(ngtcp2_conn *conn, ngtcp2_pv *pv,
                                          ngtcp2_tstamp ts) {
  int rv;

  if (!(pv->flags & NGTCP2_PV_FLAG_DONT_CARE)) {
    rv = conn_call_path_validation(conn, pv,
                                   NGTCP2_PATH_VALIDATION_RESULT_FAILURE);
    if (rv != 0) {
      return rv;
    }
  }

  if (pv->flags & NGTCP2_PV_FLAG_FALLBACK_ON_FAILURE) {
    ngtcp2_dcid_copy(&conn->dcid.current, &pv->fallback_dcid);
    conn_reset_congestion_state(conn, ts);
  }

  return conn_stop_pv(conn, ts);
}

/*
 * conn_write_path_challenge writes a packet which includes
 * PATH_CHALLENGE frame into |dest| of length |destlen|.
 *
 * This function returns the number of bytes written to |dest|, or one
 * of the following negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User-defined callback function failed.
 */
static ngtcp2_ssize conn_write_path_challenge(ngtcp2_conn *conn,
                                              ngtcp2_path *path,
                                              ngtcp2_pkt_info *pi,
                                              uint8_t *dest, size_t destlen,
                                              ngtcp2_tstamp ts) {
  ngtcp2_ssize nwrite;
  ngtcp2_tstamp expiry;
  ngtcp2_pv *pv = conn->pv;
  ngtcp2_frame lfr;
  ngtcp2_duration timeout, initial_pto;
  uint8_t flags;
  uint64_t tx_left;
  int rv;

  if (ngtcp2_pv_validation_timed_out(pv, ts)) {
    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PTV,
                    "path validation was timed out");
    rv = conn_on_path_validation_failed(conn, pv, ts);
    if (rv != 0) {
      return rv;
    }

    /* We might set path to the one which we just failed validate.
       Set it to the current path here. */
    if (path) {
      ngtcp2_path_copy(path, &conn->dcid.current.ps.path);
    }

    return 0;
  }

  ngtcp2_pv_handle_entry_expiry(pv, ts);

  if (!ngtcp2_pv_should_send_probe(pv)) {
    return 0;
  }

  rv = conn_call_get_path_challenge_data(conn, lfr.path_challenge.data);
  if (rv != 0) {
    return rv;
  }

  lfr.type = NGTCP2_FRAME_PATH_CHALLENGE;

  initial_pto = conn_compute_initial_pto(conn, &conn->pktns);
  timeout = conn_compute_pto(conn, &conn->pktns);
  timeout = ngtcp2_max(timeout, initial_pto);
  expiry = ts + timeout * (1ULL << pv->round);

  destlen = ngtcp2_min(destlen, NGTCP2_MAX_UDP_PAYLOAD_SIZE);

  if (conn->server) {
    if (!(pv->dcid.flags & NGTCP2_DCID_FLAG_PATH_VALIDATED)) {
      tx_left = conn_server_tx_left(conn, &pv->dcid);
      destlen = (size_t)ngtcp2_min((uint64_t)destlen, tx_left);
      if (destlen == 0) {
        return 0;
      }
    }

    if (destlen < NGTCP2_MAX_UDP_PAYLOAD_SIZE) {
      flags = NGTCP2_PV_ENTRY_FLAG_UNDERSIZED;
    } else {
      flags = NGTCP2_PV_ENTRY_FLAG_NONE;
    }
  } else {
    flags = NGTCP2_PV_ENTRY_FLAG_NONE;
  }

  ngtcp2_pv_add_entry(pv, lfr.path_challenge.data, expiry, flags, ts);

  nwrite = ngtcp2_conn_write_single_frame_pkt(
      conn, pi, dest, destlen, NGTCP2_PKT_1RTT, NGTCP2_WRITE_PKT_FLAG_NONE,
      &pv->dcid.cid, &lfr,
      NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING | NGTCP2_RTB_ENTRY_FLAG_PTO_ELICITING,
      &pv->dcid.ps.path, ts);
  if (nwrite <= 0) {
    return nwrite;
  }

  if (path) {
    ngtcp2_path_copy(path, &pv->dcid.ps.path);
  }

  if (ngtcp2_path_eq(&pv->dcid.ps.path, &conn->dcid.current.ps.path)) {
    conn->dcid.current.bytes_sent += (uint64_t)nwrite;
  } else {
    pv->dcid.bytes_sent += (uint64_t)nwrite;
  }

  return nwrite;
}

/*
 * conn_write_path_response writes a packet which includes
 * PATH_RESPONSE frame into |dest| of length |destlen|.
 *
 * This function returns the number of bytes written to |dest|, or one
 * of the following negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User-defined callback function failed.
 */
static ngtcp2_ssize conn_write_path_response(ngtcp2_conn *conn,
                                             ngtcp2_path *path,
                                             ngtcp2_pkt_info *pi, uint8_t *dest,
                                             size_t destlen, ngtcp2_tstamp ts) {
  ngtcp2_pv *pv = conn->pv;
  ngtcp2_path_challenge_entry *pcent = NULL;
  ngtcp2_dcid *dcid = NULL;
  ngtcp2_frame lfr;
  ngtcp2_ssize nwrite;
  int rv;
  uint64_t tx_left;

  for (; ngtcp2_ringbuf_len(&conn->rx.path_challenge.rb);) {
    pcent = ngtcp2_ringbuf_get(&conn->rx.path_challenge.rb, 0);

    if (ngtcp2_path_eq(&conn->dcid.current.ps.path, &pcent->ps.path)) {
      /* Send PATH_RESPONSE from conn_write_pkt. */
      return 0;
    }

    if (pv) {
      if (ngtcp2_path_eq(&pv->dcid.ps.path, &pcent->ps.path)) {
        dcid = &pv->dcid;
        break;
      }
      if ((pv->flags & NGTCP2_PV_FLAG_FALLBACK_ON_FAILURE) &&
          ngtcp2_path_eq(&pv->fallback_dcid.ps.path, &pcent->ps.path)) {
        dcid = &pv->fallback_dcid;
        break;
      }
    }

    if (conn->server) {
      break;
    }

    /* Client does not expect to respond to path validation against
       unknown path */
    ngtcp2_ringbuf_pop_front(&conn->rx.path_challenge.rb);
    pcent = NULL;
  }

  if (pcent == NULL) {
    return 0;
  }

  if (dcid == NULL) {
    /* client is expected to have |path| in conn->dcid.current or
       conn->pv. */
    assert(conn->server);

    rv = conn_bind_dcid(conn, &dcid, &pcent->ps.path, ts);
    if (rv != 0) {
      if (ngtcp2_err_is_fatal(rv)) {
        return rv;
      }
      return 0;
    }
  }

  destlen = ngtcp2_min(destlen, NGTCP2_MAX_UDP_PAYLOAD_SIZE);

  if (conn->server && !(dcid->flags & NGTCP2_DCID_FLAG_PATH_VALIDATED)) {
    tx_left = conn_server_tx_left(conn, dcid);
    destlen = (size_t)ngtcp2_min((uint64_t)destlen, tx_left);
    if (destlen == 0) {
      return 0;
    }
  }

  lfr.type = NGTCP2_FRAME_PATH_RESPONSE;
  memcpy(lfr.path_response.data, pcent->data, sizeof(lfr.path_response.data));

  nwrite = ngtcp2_conn_write_single_frame_pkt(
      conn, pi, dest, destlen, NGTCP2_PKT_1RTT, NGTCP2_WRITE_PKT_FLAG_NONE,
      &dcid->cid, &lfr, NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING, &pcent->ps.path,
      ts);
  if (nwrite <= 0) {
    return nwrite;
  }

  if (path) {
    ngtcp2_path_copy(path, &pcent->ps.path);
  }

  ngtcp2_ringbuf_pop_front(&conn->rx.path_challenge.rb);

  dcid->bytes_sent += (uint64_t)nwrite;

  return nwrite;
}

ngtcp2_ssize ngtcp2_conn_write_pkt_versioned(ngtcp2_conn *conn,
                                             ngtcp2_path *path,
                                             int pkt_info_version,
                                             ngtcp2_pkt_info *pi, uint8_t *dest,
                                             size_t destlen, ngtcp2_tstamp ts) {
  return ngtcp2_conn_writev_stream_versioned(
      conn, path, pkt_info_version, pi, dest, destlen,
      /* pdatalen = */ NULL, NGTCP2_WRITE_STREAM_FLAG_NONE,
      /* stream_id = */ -1,
      /* datav = */ NULL, /* datavcnt = */ 0, ts);
}

/*
 * conn_on_version_negotiation is called when Version Negotiation
 * packet is received.  The function decodes the data in the buffer
 * pointed by |payload| whose length is |payloadlen| as Version
 * Negotiation packet payload.  The packet header is given in |hd|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User-defined callback function failed.
 * NGTCP2_ERR_INVALID_ARGUMENT
 *     Packet payload is badly formatted.
 */
static int conn_on_version_negotiation(ngtcp2_conn *conn,
                                       const ngtcp2_pkt_hd *hd,
                                       const uint8_t *payload,
                                       size_t payloadlen) {
  uint32_t sv[16];
  uint32_t *p;
  int rv = 0;
  size_t nsv;
  size_t i;

  if (payloadlen % sizeof(uint32_t)) {
    return NGTCP2_ERR_INVALID_ARGUMENT;
  }

  /* Version Negotiation packet is ignored if client has reacted upon
     Version Negotiation packet. */
  if (conn->local.settings.original_version != conn->client_chosen_version) {
    return NGTCP2_ERR_INVALID_ARGUMENT;
  }

  if (payloadlen > sizeof(sv)) {
    p = ngtcp2_mem_malloc(conn->mem, payloadlen);
    if (p == NULL) {
      return NGTCP2_ERR_NOMEM;
    }
  } else {
    p = sv;
  }

  nsv = ngtcp2_pkt_decode_version_negotiation(p, payload, payloadlen);

  ngtcp2_log_rx_vn(&conn->log, hd, p, nsv);

  ngtcp2_qlog_version_negotiation_pkt_received(&conn->qlog, hd, p, nsv);

  if (!ngtcp2_is_reserved_version(conn->local.settings.original_version)) {
    for (i = 0; i < nsv; ++i) {
      if (p[i] == conn->local.settings.original_version) {
        ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                        "ignore Version Negotiation because it contains the "
                        "original version");

        rv = NGTCP2_ERR_INVALID_ARGUMENT;
        goto fin;
      }
    }
  }

  rv = conn_call_recv_version_negotiation(conn, hd, p, nsv);
  if (rv != 0) {
    goto fin;
  }

fin:
  if (p != sv) {
    ngtcp2_mem_free(conn->mem, p);
  }

  return rv;
}

static uint64_t conn_tx_strmq_first_cycle(ngtcp2_conn *conn) {
  ngtcp2_strm *strm;

  if (ngtcp2_pq_empty(&conn->tx.strmq)) {
    return 0;
  }

  strm = ngtcp2_struct_of(ngtcp2_pq_top(&conn->tx.strmq), ngtcp2_strm, pe);
  return strm->cycle;
}

uint64_t ngtcp2_conn_tx_strmq_first_cycle(ngtcp2_conn *conn) {
  ngtcp2_strm *strm;

  if (ngtcp2_pq_empty(&conn->tx.strmq)) {
    return 0;
  }

  strm = ngtcp2_struct_of(ngtcp2_pq_top(&conn->tx.strmq), ngtcp2_strm, pe);
  return strm->cycle;
}

/*
 * conn_on_retry is called when Retry packet is received.  The
 * function decodes the data in the buffer pointed by |pkt| whose
 * length is |pktlen| as Retry packet.  The length of long packet
 * header is given in |hdpktlen|.  |pkt| includes packet header.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User-defined callback function failed.
 * NGTCP2_ERR_INVALID_ARGUMENT
 *     Packet payload is badly formatted.
 * NGTCP2_ERR_PROTO
 *     ODCID does not match; or Token is empty.
 */
static int conn_on_retry(ngtcp2_conn *conn, const ngtcp2_pkt_hd *hd,
                         size_t hdpktlen, const uint8_t *pkt, size_t pktlen,
                         ngtcp2_tstamp ts) {
  int rv;
  ngtcp2_pkt_retry retry;
  ngtcp2_pktns *in_pktns = conn->in_pktns;
  ngtcp2_rtb *rtb = &conn->pktns.rtb;
  ngtcp2_rtb *in_rtb;
  uint8_t cidbuf[sizeof(retry.odcid.data) * 2 + 1];
  uint8_t *token;

  if (!in_pktns || conn->flags & NGTCP2_CONN_FLAG_RECV_RETRY) {
    return 0;
  }

  in_rtb = &in_pktns->rtb;

  rv = ngtcp2_pkt_decode_retry(&retry, pkt + hdpktlen, pktlen - hdpktlen);
  if (rv != 0) {
    return rv;
  }

  retry.odcid = conn->dcid.current.cid;

  rv = ngtcp2_pkt_verify_retry_tag(
      conn->client_chosen_version, &retry, pkt, pktlen, conn->callbacks.encrypt,
      &conn->crypto.retry_aead, &conn->crypto.retry_aead_ctx);
  if (rv != 0) {
    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                    "unable to verify Retry packet integrity");
    return rv;
  }

  ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT, "odcid=0x%s",
                  (const char *)ngtcp2_encode_hex(cidbuf, retry.odcid.data,
                                                  retry.odcid.datalen));

  if (retry.tokenlen == 0) {
    return NGTCP2_ERR_PROTO;
  }

  if (ngtcp2_cid_eq(&conn->dcid.current.cid, &hd->scid)) {
    return 0;
  }

  ngtcp2_qlog_retry_pkt_received(&conn->qlog, hd, &retry);

  /* DCID must be updated before invoking callback because client
     generates new initial keys there. */
  conn->dcid.current.cid = hd->scid;
  conn->retry_scid = hd->scid;

  conn->flags |= NGTCP2_CONN_FLAG_RECV_RETRY;

  rv = conn_call_recv_retry(conn, hd);
  if (rv != 0) {
    return rv;
  }

  conn->state = NGTCP2_CS_CLIENT_INITIAL;

  /* Just freeing memory is dangerous because we might free twice. */

  rv = ngtcp2_rtb_remove_all(rtb, conn, &conn->pktns, &conn->cstat);
  if (rv != 0) {
    return rv;
  }

  rv = ngtcp2_rtb_remove_all(in_rtb, conn, in_pktns, &conn->cstat);
  if (rv != 0) {
    return rv;
  }

  ngtcp2_mem_free(conn->mem, (uint8_t *)conn->local.settings.token);
  conn->local.settings.token = NULL;
  conn->local.settings.tokenlen = 0;

  token = ngtcp2_mem_malloc(conn->mem, retry.tokenlen);
  if (token == NULL) {
    return NGTCP2_ERR_NOMEM;
  }

  ngtcp2_cpymem(token, retry.token, retry.tokenlen);

  conn->local.settings.token = token;
  conn->local.settings.tokenlen = retry.tokenlen;

  reset_conn_stat_recovery(&conn->cstat);
  conn_reset_congestion_state(conn, ts);
  conn_reset_ecn_validation_state(conn);

  return 0;
}

int ngtcp2_conn_detect_lost_pkt(ngtcp2_conn *conn, ngtcp2_pktns *pktns,
                                ngtcp2_conn_stat *cstat, ngtcp2_tstamp ts) {
  return ngtcp2_rtb_detect_lost_pkt(&pktns->rtb, conn, pktns, cstat, ts);
}

/*
 * conn_recv_ack processes received ACK frame |fr|.  |pkt_ts| is the
 * timestamp when packet is received.  |ts| should be the current
 * time.  Usually they are the same, but for buffered packets,
 * |pkt_ts| would be earlier than |ts|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory
 * NGTCP2_ERR_ACK_FRAME
 *     ACK frame is malformed.
 * NGTCP2_ERR_PROTO
 *     |fr| acknowledges a packet this endpoint has not sent.
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User callback failed.
 */
static int conn_recv_ack(ngtcp2_conn *conn, ngtcp2_pktns *pktns, ngtcp2_ack *fr,
                         ngtcp2_tstamp pkt_ts, ngtcp2_tstamp ts) {
  int rv;
  ngtcp2_ssize num_acked;
  ngtcp2_conn_stat *cstat = &conn->cstat;

  if (pktns->tx.last_pkt_num < fr->largest_ack) {
    return NGTCP2_ERR_PROTO;
  }

  rv = ngtcp2_pkt_validate_ack(fr, conn->local.settings.initial_pkt_num);
  if (rv != 0) {
    return rv;
  }

  ngtcp2_acktr_recv_ack(&pktns->acktr, fr);

  num_acked = ngtcp2_rtb_recv_ack(&pktns->rtb, fr, &conn->cstat, conn, pktns,
                                  pkt_ts, ts);
  if (num_acked < 0) {
    assert(ngtcp2_err_is_fatal((int)num_acked));
    return (int)num_acked;
  }

  if (num_acked == 0) {
    return 0;
  }

  pktns->rtb.probe_pkt_left = 0;

  if (cstat->pto_count &&
      (conn->server || (conn->flags & NGTCP2_CONN_FLAG_SERVER_ADDR_VERIFIED))) {
    /* Reset PTO count but no less than 2 to avoid frequent probe
       packet transmission. */
    cstat->pto_count = ngtcp2_min(cstat->pto_count, 2);
  }

  ngtcp2_conn_set_loss_detection_timer(conn, ts);

  return 0;
}

/*
 * conn_assign_recved_ack_delay_unscaled assigns
 * fr->ack_delay_unscaled.
 */
static void assign_recved_ack_delay_unscaled(ngtcp2_ack *fr,
                                             uint64_t ack_delay_exponent) {
  fr->ack_delay_unscaled =
      fr->ack_delay * (1ULL << ack_delay_exponent) * NGTCP2_MICROSECONDS;
}

/*
 * conn_recv_max_stream_data processes received MAX_STREAM_DATA frame
 * |fr|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_STREAM_STATE
 *     Stream ID indicates that it is a local stream, and the local
 *     endpoint has not initiated it; or stream is peer initiated
 *     unidirectional stream.
 * NGTCP2_ERR_STREAM_LIMIT
 *     Stream ID exceeds allowed limit.
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 */
static int conn_recv_max_stream_data(ngtcp2_conn *conn,
                                     const ngtcp2_max_stream_data *fr) {
  ngtcp2_strm *strm;
  ngtcp2_idtr *idtr;
  int local_stream = conn_local_stream(conn, fr->stream_id);
  int bidi = bidi_stream(fr->stream_id);
  int rv;

  if (bidi) {
    if (local_stream) {
      if (conn->local.bidi.next_stream_id <= fr->stream_id) {
        return NGTCP2_ERR_STREAM_STATE;
      }
    } else if (conn->remote.bidi.max_streams <
               ngtcp2_ord_stream_id(fr->stream_id)) {
      return NGTCP2_ERR_STREAM_LIMIT;
    }

    idtr = &conn->remote.bidi.idtr;
  } else {
    if (!local_stream || conn->local.uni.next_stream_id <= fr->stream_id) {
      return NGTCP2_ERR_STREAM_STATE;
    }

    idtr = &conn->remote.uni.idtr;
  }

  strm = ngtcp2_conn_find_stream(conn, fr->stream_id);
  if (strm == NULL) {
    if (local_stream) {
      /* Stream has been closed. */
      return 0;
    }

    rv = ngtcp2_idtr_open(idtr, fr->stream_id);
    if (rv != 0) {
      if (ngtcp2_err_is_fatal(rv)) {
        return rv;
      }
      assert(rv == NGTCP2_ERR_STREAM_IN_USE);
      /* Stream has been closed. */
      return 0;
    }

    strm = ngtcp2_objalloc_strm_get(&conn->strm_objalloc);
    if (strm == NULL) {
      return NGTCP2_ERR_NOMEM;
    }
    rv = ngtcp2_conn_init_stream(conn, strm, fr->stream_id, NULL);
    if (rv != 0) {
      ngtcp2_objalloc_strm_release(&conn->strm_objalloc, strm);
      return rv;
    }

    rv = conn_call_stream_open(conn, strm);
    if (rv != 0) {
      return rv;
    }
  }

  if (strm->tx.max_offset < fr->max_stream_data) {
    strm->tx.max_offset = fr->max_stream_data;

    /* Don't call callback if stream is half-closed local */
    if (strm->flags & NGTCP2_STRM_FLAG_SHUT_WR) {
      return 0;
    }

    rv = conn_call_extend_max_stream_data(conn, strm, fr->stream_id,
                                          fr->max_stream_data);
    if (rv != 0) {
      return rv;
    }
  }

  return 0;
}

/*
 * conn_recv_max_data processes received MAX_DATA frame |fr|.
 */
static void conn_recv_max_data(ngtcp2_conn *conn, const ngtcp2_max_data *fr) {
  conn->tx.max_offset = ngtcp2_max(conn->tx.max_offset, fr->max_data);
}

/*
 * conn_buffer_pkt buffers |pkt| of length |pktlen|, chaining it from
 * |*ppc|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 */
static int conn_buffer_pkt(ngtcp2_conn *conn, ngtcp2_pktns *pktns,
                           const ngtcp2_path *path, const ngtcp2_pkt_info *pi,
                           const uint8_t *pkt, size_t pktlen, size_t dgramlen,
                           ngtcp2_tstamp ts) {
  int rv;
  ngtcp2_pkt_chain **ppc = &pktns->rx.buffed_pkts, *pc;
  size_t i;
  for (i = 0; *ppc && i < NGTCP2_MAX_NUM_BUFFED_RX_PKTS;
       ppc = &(*ppc)->next, ++i)
    ;

  if (i == NGTCP2_MAX_NUM_BUFFED_RX_PKTS) {
    return 0;
  }

  rv =
      ngtcp2_pkt_chain_new(&pc, path, pi, pkt, pktlen, dgramlen, ts, conn->mem);
  if (rv != 0) {
    return rv;
  }

  *ppc = pc;

  return 0;
}

static int ensure_decrypt_buffer(ngtcp2_vec *vec, size_t n, size_t initial,
                                 const ngtcp2_mem *mem) {
  uint8_t *nbuf;
  size_t len;

  if (vec->len >= n) {
    return 0;
  }

  len = vec->len == 0 ? initial : vec->len * 2;
  for (; len < n; len *= 2)
    ;
  nbuf = ngtcp2_mem_realloc(mem, vec->base, len);
  if (nbuf == NULL) {
    return NGTCP2_ERR_NOMEM;
  }
  vec->base = nbuf;
  vec->len = len;

  return 0;
}

/*
 * conn_ensure_decrypt_hp_buffer ensures that
 * conn->crypto.decrypt_hp_buf has at least |n| bytes space.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 */
static int conn_ensure_decrypt_hp_buffer(ngtcp2_conn *conn, size_t n) {
  return ensure_decrypt_buffer(&conn->crypto.decrypt_hp_buf, n, 256, conn->mem);
}

/*
 * conn_ensure_decrypt_buffer ensures that conn->crypto.decrypt_buf
 * has at least |n| bytes space.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 */
static int conn_ensure_decrypt_buffer(ngtcp2_conn *conn, size_t n) {
  return ensure_decrypt_buffer(&conn->crypto.decrypt_buf, n, 2048, conn->mem);
}

/*
 * decrypt_pkt decrypts the data pointed by |payload| whose length is
 * |payloadlen|, and writes plaintext data to the buffer pointed by
 * |dest|.  The buffer pointed by |aad| is the Additional
 * Authenticated Data, and its length is |aadlen|.  |pkt_num| is used
 * to create a nonce.  |ckm| is the cryptographic key, and iv to use.
 * |decrypt| is a callback function which actually decrypts a packet.
 *
 * This function returns the number of bytes written in |dest| if it
 * succeeds, or one of the following negative error codes:
 *
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User callback failed.
 * NGTCP2_ERR_DECRYPT
 *     Failed to decrypt a packet.
 */
static ngtcp2_ssize decrypt_pkt(uint8_t *dest, const ngtcp2_crypto_aead *aead,
                                const uint8_t *payload, size_t payloadlen,
                                const uint8_t *aad, size_t aadlen,
                                int64_t pkt_num, ngtcp2_crypto_km *ckm,
                                ngtcp2_decrypt decrypt) {
  /* TODO nonce is limited to 64 bytes. */
  uint8_t nonce[64];
  int rv;

  assert(sizeof(nonce) >= ckm->iv.len);

  ngtcp2_crypto_create_nonce(nonce, ckm->iv.base, ckm->iv.len, pkt_num);

  rv = decrypt(dest, aead, &ckm->aead_ctx, payload, payloadlen, nonce,
               ckm->iv.len, aad, aadlen);

  if (rv != 0) {
    if (rv == NGTCP2_ERR_DECRYPT) {
      return rv;
    }
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  assert(payloadlen >= aead->max_overhead);

  return (ngtcp2_ssize)(payloadlen - aead->max_overhead);
}

/*
 * decrypt_hp decryptes packet header.  The packet number starts at
 * |pkt| + |pkt_num_offset|.  The entire plaintext QUIC packet header
 * will be written to the buffer pointed by |dest| whose capacity is
 * |destlen|.
 *
 * This function returns the number of bytes written to |dest|, or one
 * of the following negative error codes:
 *
 * NGTCP2_ERR_PROTO
 *     Packet is badly formatted
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User-defined callback function failed; or it does not return
 *     expected result.
 */
static ngtcp2_ssize
decrypt_hp(ngtcp2_pkt_hd *hd, uint8_t *dest, const ngtcp2_crypto_cipher *hp,
           const uint8_t *pkt, size_t pktlen, size_t pkt_num_offset,
           const ngtcp2_crypto_cipher_ctx *hp_ctx, ngtcp2_hp_mask hp_mask) {
  size_t sample_offset;
  uint8_t *p = dest;
  uint8_t mask[NGTCP2_HP_SAMPLELEN];
  size_t i;
  int rv;

  assert(hp_mask);

  if (pkt_num_offset + 4 + NGTCP2_HP_SAMPLELEN > pktlen) {
    return NGTCP2_ERR_PROTO;
  }

  p = ngtcp2_cpymem(p, pkt, pkt_num_offset);

  sample_offset = pkt_num_offset + 4;

  rv = hp_mask(mask, hp, hp_ctx, pkt + sample_offset);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  if (hd->flags & NGTCP2_PKT_FLAG_LONG_FORM) {
    dest[0] = (uint8_t)(dest[0] ^ (mask[0] & 0x0f));
  } else {
    dest[0] = (uint8_t)(dest[0] ^ (mask[0] & 0x1f));
    if (dest[0] & NGTCP2_SHORT_KEY_PHASE_BIT) {
      hd->flags |= NGTCP2_PKT_FLAG_KEY_PHASE;
    }
  }

  hd->pkt_numlen = (size_t)((dest[0] & NGTCP2_PKT_NUMLEN_MASK) + 1);

  for (i = 0; i < hd->pkt_numlen; ++i) {
    *p++ = *(pkt + pkt_num_offset + i) ^ mask[i + 1];
  }

  hd->pkt_num = ngtcp2_get_pkt_num(p - hd->pkt_numlen, hd->pkt_numlen);

  return p - dest;
}

/*
 * conn_emit_pending_crypto_data delivers pending stream data to the
 * application due to packet reordering.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User callback failed
 * NGTCP2_ERR_CRYPTO
 *     TLS backend reported error
 */
static int
conn_emit_pending_crypto_data(ngtcp2_conn *conn,
                              ngtcp2_encryption_level encryption_level,
                              ngtcp2_strm *strm, uint64_t rx_offset) {
  size_t datalen;
  const uint8_t *data;
  int rv;
  uint64_t offset;

  if (!strm->rx.rob) {
    return 0;
  }

  for (;;) {
    datalen = ngtcp2_rob_data_at(strm->rx.rob, &data, rx_offset);
    if (datalen == 0) {
      assert(rx_offset == ngtcp2_strm_rx_offset(strm));
      return 0;
    }

    offset = rx_offset;
    rx_offset += datalen;

    rv = conn_call_recv_crypto_data(conn, encryption_level, offset, data,
                                    datalen);
    if (rv != 0) {
      return rv;
    }

    ngtcp2_rob_pop(strm->rx.rob, rx_offset - datalen, datalen);
  }
}

/*
 * conn_recv_connection_close is called when CONNECTION_CLOSE or
 * APPLICATION_CLOSE frame is received.
 */
static int conn_recv_connection_close(ngtcp2_conn *conn,
                                      ngtcp2_connection_close *fr) {
  ngtcp2_ccerr *ccerr = &conn->rx.ccerr;

  conn->state = NGTCP2_CS_DRAINING;
  if (fr->type == NGTCP2_FRAME_CONNECTION_CLOSE) {
    ccerr->type = NGTCP2_CCERR_TYPE_TRANSPORT;
  } else {
    ccerr->type = NGTCP2_CCERR_TYPE_APPLICATION;
  }
  ccerr->error_code = fr->error_code;
  ccerr->frame_type = fr->frame_type;

  if (!fr->reasonlen) {
    ccerr->reasonlen = 0;

    return 0;
  }

  if (ccerr->reason == NULL) {
    ccerr->reason = ngtcp2_mem_malloc(conn->mem, NGTCP2_CCERR_MAX_REASONLEN);
    if (ccerr->reason == NULL) {
      return NGTCP2_ERR_NOMEM;
    }
  }

  ccerr->reasonlen = ngtcp2_min(fr->reasonlen, NGTCP2_CCERR_MAX_REASONLEN);
  ngtcp2_cpymem((uint8_t *)ccerr->reason, fr->reason, ccerr->reasonlen);

  return 0;
}

static void conn_recv_path_challenge(ngtcp2_conn *conn, const ngtcp2_path *path,
                                     ngtcp2_path_challenge *fr) {
  ngtcp2_path_challenge_entry *ent;

  /* client only responds to PATH_CHALLENGE from the current path or
     path which client is migrating to. */
  if (!conn->server && !ngtcp2_path_eq(&conn->dcid.current.ps.path, path) &&
      (!conn->pv || !ngtcp2_path_eq(&conn->pv->dcid.ps.path, path))) {
    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_CON,
                    "discard PATH_CHALLENGE from the path which is not current "
                    "or endpoint is migrating to");
    return;
  }

  ent = ngtcp2_ringbuf_push_front(&conn->rx.path_challenge.rb);
  ngtcp2_path_challenge_entry_init(ent, path, fr->data);
}

/*
 * conn_reset_congestion_state resets congestion state.
 */
static void conn_reset_congestion_state(ngtcp2_conn *conn, ngtcp2_tstamp ts) {
  conn_reset_conn_stat_cc(conn, &conn->cstat);

  if (conn->cc.reset) {
    conn->cc.reset(&conn->cc, &conn->cstat, ts);
  }

  if (conn->hs_pktns) {
    ngtcp2_rtb_reset_cc_state(&conn->hs_pktns->rtb,
                              conn->hs_pktns->tx.last_pkt_num + 1);
  }
  ngtcp2_rtb_reset_cc_state(&conn->pktns.rtb, conn->pktns.tx.last_pkt_num + 1);
  ngtcp2_rst_init(&conn->rst);

  conn->tx.pacing.next_ts = UINT64_MAX;
}

static int conn_recv_path_response(ngtcp2_conn *conn, ngtcp2_path_response *fr,
                                   ngtcp2_tstamp ts) {
  int rv;
  ngtcp2_pv *pv = conn->pv, *npv;
  uint8_t ent_flags;

  if (!pv) {
    return 0;
  }

  rv = ngtcp2_pv_validate(pv, &ent_flags, fr->data);
  if (rv != 0) {
    assert(!ngtcp2_err_is_fatal(rv));

    return 0;
  }

  if (!(pv->flags & NGTCP2_PV_FLAG_DONT_CARE)) {
    if (pv->dcid.seq != conn->dcid.current.seq) {
      assert(!conn->server);
      assert(conn->dcid.current.cid.datalen);

      rv = conn_retire_dcid(conn, &conn->dcid.current, ts);
      if (rv != 0) {
        return rv;
      }
      ngtcp2_dcid_copy(&conn->dcid.current, &pv->dcid);

      conn_reset_congestion_state(conn, ts);
      conn_reset_ecn_validation_state(conn);
    }

    assert(ngtcp2_path_eq(&pv->dcid.ps.path, &conn->dcid.current.ps.path));

    conn->dcid.current.flags |= NGTCP2_DCID_FLAG_PATH_VALIDATED;

    if (!conn->local.settings.no_pmtud) {
      ngtcp2_conn_stop_pmtud(conn);

      if (!(ent_flags & NGTCP2_PV_ENTRY_FLAG_UNDERSIZED)) {
        rv = conn_start_pmtud(conn);
        if (rv != 0) {
          return rv;
        }
      }
    }

    if (!(ent_flags & NGTCP2_PV_ENTRY_FLAG_UNDERSIZED)) {
      rv = conn_call_path_validation(conn, pv,
                                     NGTCP2_PATH_VALIDATION_RESULT_SUCCESS);
      if (rv != 0) {
        return rv;
      }
    }
  }

  if (pv->flags & NGTCP2_PV_FLAG_FALLBACK_ON_FAILURE) {
    if (ent_flags & NGTCP2_PV_ENTRY_FLAG_UNDERSIZED) {
      assert(conn->server);

      /* Validate path again */
      rv = ngtcp2_pv_new(&npv, &pv->dcid, conn_compute_pv_timeout(conn),
                         NGTCP2_PV_FLAG_FALLBACK_ON_FAILURE, &conn->log,
                         conn->mem);
      if (rv != 0) {
        return rv;
      }

      npv->dcid.flags |= NGTCP2_DCID_FLAG_PATH_VALIDATED;
      ngtcp2_dcid_copy(&npv->fallback_dcid, &pv->fallback_dcid);
      npv->fallback_pto = pv->fallback_pto;
    } else {
      rv = ngtcp2_pv_new(&npv, &pv->fallback_dcid,
                         conn_compute_pv_timeout_pto(conn, pv->fallback_pto),
                         NGTCP2_PV_FLAG_DONT_CARE, &conn->log, conn->mem);
      if (rv != 0) {
        return rv;
      }
    }

    /* Unset the flag bit so that conn_stop_pv does not retire
       DCID. */
    pv->flags &= (uint8_t)~NGTCP2_PV_FLAG_FALLBACK_ON_FAILURE;

    rv = conn_stop_pv(conn, ts);
    if (rv != 0) {
      ngtcp2_pv_del(npv);
      return rv;
    }

    conn->pv = npv;

    return 0;
  }

  return conn_stop_pv(conn, ts);
}

/*
 * pktns_pkt_num_is_duplicate returns nonzero if |pkt_num| is
 * duplicated packet number.
 */
static int pktns_pkt_num_is_duplicate(ngtcp2_pktns *pktns, int64_t pkt_num) {
  return ngtcp2_gaptr_is_pushed(&pktns->rx.pngap, (uint64_t)pkt_num, 1);
}

/*
 * pktns_commit_recv_pkt_num marks packet number |pkt_num| as
 * received.
 */
static int pktns_commit_recv_pkt_num(ngtcp2_pktns *pktns, int64_t pkt_num,
                                     int ack_eliciting, ngtcp2_tstamp ts) {
  int rv;
  ngtcp2_range r;

  rv = ngtcp2_gaptr_push(&pktns->rx.pngap, (uint64_t)pkt_num, 1);
  if (rv != 0) {
    return rv;
  }

  if (ngtcp2_ksl_len(&pktns->rx.pngap.gap) > 256) {
    ngtcp2_gaptr_drop_first_gap(&pktns->rx.pngap);
  }

  if (ack_eliciting) {
    if (pktns->rx.max_ack_eliciting_pkt_num != -1) {
      if (pkt_num < pktns->rx.max_ack_eliciting_pkt_num) {
        ngtcp2_acktr_immediate_ack(&pktns->acktr);
      } else if (pkt_num > pktns->rx.max_ack_eliciting_pkt_num) {
        r = ngtcp2_gaptr_get_first_gap_after(
            &pktns->rx.pngap, (uint64_t)pktns->rx.max_ack_eliciting_pkt_num);

        if (r.begin < (uint64_t)pkt_num) {
          ngtcp2_acktr_immediate_ack(&pktns->acktr);
        }
      }
    }

    if (pktns->rx.max_ack_eliciting_pkt_num < pkt_num) {
      pktns->rx.max_ack_eliciting_pkt_num = pkt_num;
    }
  }

  if (pktns->rx.max_pkt_num < pkt_num) {
    pktns->rx.max_pkt_num = pkt_num;
    pktns->rx.max_pkt_ts = ts;
  }

  return 0;
}

/*
 * verify_token verifies |hd| contains |token| in its token field.  It
 * returns 0 if it succeeds, or NGTCP2_ERR_PROTO.
 */
static int verify_token(const uint8_t *token, size_t tokenlen,
                        const ngtcp2_pkt_hd *hd) {
  if (tokenlen == hd->tokenlen && ngtcp2_cmemeq(token, hd->token, tokenlen)) {
    return 0;
  }
  return NGTCP2_ERR_PROTO;
}

static void pktns_increase_ecn_counts(ngtcp2_pktns *pktns,
                                      const ngtcp2_pkt_info *pi) {
  switch (pi->ecn & NGTCP2_ECN_MASK) {
  case NGTCP2_ECN_ECT_0:
    ++pktns->rx.ecn.ect0;
    break;
  case NGTCP2_ECN_ECT_1:
    ++pktns->rx.ecn.ect1;
    break;
  case NGTCP2_ECN_CE:
    ++pktns->rx.ecn.ce;
    break;
  }
}

/*
 * vneg_available_versions_includes returns nonzero if
 * |available_versions| of length |available_versionslen| includes
 * |version|.  |available_versions| is the wire image of
 * available_versions field of version_information transport
 * parameter, and each version is encoded in network byte order.
 */
static int vneg_available_versions_includes(const uint8_t *available_versions,
                                            size_t available_versionslen,
                                            uint32_t version) {
  size_t i;
  uint32_t v;

  assert(!(available_versionslen & 0x3));

  if (available_versionslen == 0) {
    return 0;
  }

  for (i = 0; i < available_versionslen; i += sizeof(uint32_t)) {
    available_versions = ngtcp2_get_uint32(&v, available_versions);

    if (version == v) {
      return 1;
    }
  }

  return 0;
}

/*
 * conn_verify_fixed_bit verifies that fixed bit in |hd| is
 * acceptable.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_INVALID_ARGUMENT
 *     Clearing fixed bit is not permitted.
 */
static int conn_verify_fixed_bit(ngtcp2_conn *conn, ngtcp2_pkt_hd *hd) {
  if (!(hd->flags & NGTCP2_PKT_FLAG_FIXED_BIT_CLEAR)) {
    return 0;
  }

  if (conn->server) {
    switch (hd->type) {
    case NGTCP2_PKT_INITIAL:
    case NGTCP2_PKT_0RTT:
    case NGTCP2_PKT_HANDSHAKE:
      /* RFC 9287 requires that a token from NEW_TOKEN. */
      if (!(conn->flags & NGTCP2_CONN_FLAG_INITIAL_PKT_PROCESSED) &&
          (conn->local.settings.token_type != NGTCP2_TOKEN_TYPE_NEW_TOKEN ||
           !conn->local.settings.tokenlen)) {
        return NGTCP2_ERR_INVALID_ARGUMENT;
      }

      break;
    }
  }

  /* TODO we have no information that we enabled grease_quic_bit in
     the previous connection. */
  if (!conn->local.transport_params.grease_quic_bit) {
    return NGTCP2_ERR_INVALID_ARGUMENT;
  }

  return 0;
}

static int conn_recv_crypto(ngtcp2_conn *conn,
                            ngtcp2_encryption_level encryption_level,
                            ngtcp2_strm *strm, const ngtcp2_stream *fr);

static ngtcp2_ssize conn_recv_pkt(ngtcp2_conn *conn, const ngtcp2_path *path,
                                  const ngtcp2_pkt_info *pi, const uint8_t *pkt,
                                  size_t pktlen, size_t dgramlen,
                                  ngtcp2_tstamp pkt_ts, ngtcp2_tstamp ts);

static int conn_process_buffered_protected_pkt(ngtcp2_conn *conn,
                                               ngtcp2_pktns *pktns,
                                               ngtcp2_tstamp ts);

/*
 * conn_recv_handshake_pkt processes received packet |pkt| whose
 * length is |pktlen| during handshake period.  The buffer pointed by
 * |pkt| might contain multiple packets.  This function only processes
 * one packet.  |pkt_ts| is the timestamp when packet is received.
 * |ts| should be the current time.  Usually they are the same, but
 * for buffered packets, |pkt_ts| would be earlier than |ts|.
 *
 * This function returns the number of bytes it reads if it succeeds,
 * or one of the following negative error codes:
 *
 * NGTCP2_ERR_RECV_VERSION_NEGOTIATION
 *     Version Negotiation packet is received.
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User-defined callback function failed.
 * NGTCP2_ERR_DISCARD_PKT
 *     Packet was discarded because plain text header was malformed;
 *     or its payload could not be decrypted.
 * NGTCP2_ERR_FRAME_FORMAT
 *     Frame is badly formatted
 * NGTCP2_ERR_ACK_FRAME
 *     ACK frame is malformed.
 * NGTCP2_ERR_CRYPTO
 *     TLS stack reported error.
 * NGTCP2_ERR_PROTO
 *     Generic QUIC protocol error.
 *
 * In addition to the above error codes, error codes returned from
 * conn_recv_pkt are also returned.
 */
static ngtcp2_ssize
conn_recv_handshake_pkt(ngtcp2_conn *conn, const ngtcp2_path *path,
                        const ngtcp2_pkt_info *pi, const uint8_t *pkt,
                        size_t pktlen, size_t dgramlen, ngtcp2_tstamp pkt_ts,
                        ngtcp2_tstamp ts) {
  ngtcp2_ssize nread;
  ngtcp2_pkt_hd hd;
  ngtcp2_max_frame mfr;
  ngtcp2_frame *fr = &mfr.fr;
  int rv;
  int require_ack = 0;
  size_t hdpktlen;
  const uint8_t *payload;
  size_t payloadlen;
  ngtcp2_ssize nwrite;
  ngtcp2_crypto_aead *aead;
  ngtcp2_crypto_cipher *hp;
  ngtcp2_crypto_km *ckm;
  ngtcp2_crypto_cipher_ctx *hp_ctx;
  ngtcp2_hp_mask hp_mask;
  ngtcp2_decrypt decrypt;
  ngtcp2_pktns *pktns;
  ngtcp2_strm *crypto;
  ngtcp2_encryption_level encryption_level;
  int invalid_reserved_bits = 0;

  if (pktlen == 0) {
    return 0;
  }

  if (!(pkt[0] & NGTCP2_HEADER_FORM_BIT)) {
    if (conn->state == NGTCP2_CS_SERVER_INITIAL) {
      /* Ignore 1RTT packet unless server's first Handshake packet has
         been transmitted. */
      return (ngtcp2_ssize)pktlen;
    }

    if (conn->pktns.crypto.rx.ckm) {
      return 0;
    }

    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_CON,
                    "buffering 1RTT packet len=%zu", pktlen);

    rv = conn_buffer_pkt(conn, &conn->pktns, path, pi, pkt, pktlen, dgramlen,
                         ts);
    if (rv != 0) {
      assert(ngtcp2_err_is_fatal(rv));
      return rv;
    }
    return (ngtcp2_ssize)pktlen;
  }

  nread = ngtcp2_pkt_decode_hd_long(&hd, pkt, pktlen);
  if (nread < 0) {
    return NGTCP2_ERR_DISCARD_PKT;
  }

  switch (hd.type) {
  case NGTCP2_PKT_VERSION_NEGOTIATION:
    hdpktlen = (size_t)nread;

    ngtcp2_log_rx_pkt_hd(&conn->log, &hd);

    if (conn->server) {
      return NGTCP2_ERR_DISCARD_PKT;
    }

    /* Receiving Version Negotiation packet after getting Handshake
       packet from server is invalid. */
    if (conn->flags & NGTCP2_CONN_FLAG_INITIAL_PKT_PROCESSED) {
      return NGTCP2_ERR_DISCARD_PKT;
    }

    if (!ngtcp2_cid_eq(&conn->oscid, &hd.dcid)) {
      ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                      "packet was ignored because of mismatched DCID");
      return NGTCP2_ERR_DISCARD_PKT;
    }

    if (!ngtcp2_cid_eq(&conn->dcid.current.cid, &hd.scid)) {
      /* Just discard invalid Version Negotiation packet */
      ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                      "packet was ignored because of mismatched SCID");
      return NGTCP2_ERR_DISCARD_PKT;
    }
    rv = conn_on_version_negotiation(conn, &hd, pkt + hdpktlen,
                                     pktlen - hdpktlen);
    if (rv != 0) {
      if (ngtcp2_err_is_fatal(rv)) {
        return rv;
      }
      return NGTCP2_ERR_DISCARD_PKT;
    }
    return NGTCP2_ERR_RECV_VERSION_NEGOTIATION;
  case NGTCP2_PKT_RETRY:
    hdpktlen = (size_t)nread;

    ngtcp2_log_rx_pkt_hd(&conn->log, &hd);

    if (conn->server) {
      return NGTCP2_ERR_DISCARD_PKT;
    }

    if (conn_verify_fixed_bit(conn, &hd) != 0) {
      return NGTCP2_ERR_DISCARD_PKT;
    }

    /* Receiving Retry packet after getting Initial packet from server
       is invalid. */
    if (conn->flags & NGTCP2_CONN_FLAG_INITIAL_PKT_PROCESSED) {
      return NGTCP2_ERR_DISCARD_PKT;
    }

    if (conn->client_chosen_version != hd.version) {
      return NGTCP2_ERR_DISCARD_PKT;
    }

    rv = conn_on_retry(conn, &hd, hdpktlen, pkt, pktlen, ts);
    if (rv != 0) {
      if (ngtcp2_err_is_fatal(rv)) {
        return rv;
      }
      return NGTCP2_ERR_DISCARD_PKT;
    }
    return (ngtcp2_ssize)pktlen;
  }

  if (pktlen < (size_t)nread + hd.len) {
    return NGTCP2_ERR_DISCARD_PKT;
  }

  pktlen = (size_t)nread + hd.len;

  if (!ngtcp2_is_supported_version(hd.version)) {
    return NGTCP2_ERR_DISCARD_PKT;
  }

  if (conn->server) {
    if (hd.version != conn->client_chosen_version &&
        (!conn->negotiated_version || hd.version != conn->negotiated_version)) {
      return NGTCP2_ERR_DISCARD_PKT;
    }
  } else if (hd.version != conn->client_chosen_version &&
             conn->negotiated_version &&
             hd.version != conn->negotiated_version) {
    return NGTCP2_ERR_DISCARD_PKT;
  }

  if (conn_verify_fixed_bit(conn, &hd) != 0) {
    return NGTCP2_ERR_DISCARD_PKT;
  }

  /* Quoted from spec: if subsequent packets of those types include a
     different Source Connection ID, they MUST be discarded. */
  if ((conn->flags & NGTCP2_CONN_FLAG_INITIAL_PKT_PROCESSED) &&
      !ngtcp2_cid_eq(&conn->dcid.current.cid, &hd.scid)) {
    ngtcp2_log_rx_pkt_hd(&conn->log, &hd);
    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                    "packet was ignored because of mismatched SCID");
    return NGTCP2_ERR_DISCARD_PKT;
  }

  switch (hd.type) {
  case NGTCP2_PKT_0RTT:
    if (!conn->server) {
      return NGTCP2_ERR_DISCARD_PKT;
    }

    if (hd.version != conn->client_chosen_version) {
      return NGTCP2_ERR_DISCARD_PKT;
    }

    if (conn->flags & NGTCP2_CONN_FLAG_INITIAL_PKT_PROCESSED) {
      if (conn->early.ckm) {
        ngtcp2_ssize nread2;
        /* TODO Avoid to parse header twice. */
        nread2 =
            conn_recv_pkt(conn, path, pi, pkt, pktlen, dgramlen, pkt_ts, ts);
        if (nread2 < 0) {
          return nread2;
        }
      }

      /* Discard 0-RTT packet if we don't have a key to decrypt it. */
      return (ngtcp2_ssize)pktlen;
    }

    /* Buffer re-ordered 0-RTT packet. */
    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_CON,
                    "buffering 0-RTT packet len=%zu", pktlen);

    rv = conn_buffer_pkt(conn, conn->in_pktns, path, pi, pkt, pktlen, dgramlen,
                         ts);
    if (rv != 0) {
      assert(ngtcp2_err_is_fatal(rv));
      return rv;
    }

    return (ngtcp2_ssize)pktlen;
  case NGTCP2_PKT_INITIAL:
    if (!conn->in_pktns) {
      ngtcp2_log_info(
          &conn->log, NGTCP2_LOG_EVENT_PKT,
          "Initial packet is discarded because keys have been discarded");
      return (ngtcp2_ssize)pktlen;
    }

    assert(conn->in_pktns);

    if (conn->server) {
      if (dgramlen < NGTCP2_MAX_UDP_PAYLOAD_SIZE) {
        ngtcp2_log_info(
            &conn->log, NGTCP2_LOG_EVENT_PKT,
            "Initial packet was ignored because it is included in UDP datagram "
            "less than %zu bytes: %zu bytes",
            NGTCP2_MAX_UDP_PAYLOAD_SIZE, dgramlen);
        return NGTCP2_ERR_DISCARD_PKT;
      }
      if (conn->local.settings.tokenlen) {
        rv = verify_token(conn->local.settings.token,
                          conn->local.settings.tokenlen, &hd);
        if (rv != 0) {
          ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                          "packet was ignored because token is invalid");
          return NGTCP2_ERR_DISCARD_PKT;
        }
      }
      if ((conn->flags & NGTCP2_CONN_FLAG_INITIAL_PKT_PROCESSED) == 0) {
        /* Set rcid here so that it is available to callback.  If this
           packet is discarded later in this function and no packet is
           processed in this connection attempt so far, connection
           will be dropped. */
        conn->rcid = hd.dcid;

        rv = conn_call_recv_client_initial(conn, &hd.dcid);
        if (rv != 0) {
          return rv;
        }
      }
    } else {
      if (hd.tokenlen != 0) {
        ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                        "packet was ignored because token is not empty");
        return NGTCP2_ERR_DISCARD_PKT;
      }

      if (hd.version != conn->client_chosen_version &&
          !conn->negotiated_version && conn->vneg.version != hd.version) {
        if (!vneg_available_versions_includes(conn->vneg.available_versions,
                                              conn->vneg.available_versionslen,
                                              hd.version)) {
          return NGTCP2_ERR_DISCARD_PKT;
        }

        /* Install new Initial keys using QUIC version = hd.version */
        rv = conn_call_version_negotiation(
            conn, hd.version,
            (conn->flags & NGTCP2_CONN_FLAG_RECV_RETRY)
                ? &conn->dcid.current.cid
                : &conn->rcid);
        if (rv != 0) {
          return rv;
        }

        assert(conn->vneg.version == hd.version);
      }
    }

    pktns = conn->in_pktns;
    crypto = &pktns->crypto.strm;
    encryption_level = NGTCP2_ENCRYPTION_LEVEL_INITIAL;

    if (hd.version == conn->client_chosen_version) {
      ckm = pktns->crypto.rx.ckm;
      hp_ctx = &pktns->crypto.rx.hp_ctx;
    } else {
      assert(conn->vneg.version == hd.version);

      ckm = conn->vneg.rx.ckm;
      hp_ctx = &conn->vneg.rx.hp_ctx;
    }

    break;
  case NGTCP2_PKT_HANDSHAKE:
    if (hd.version != conn->negotiated_version) {
      return NGTCP2_ERR_DISCARD_PKT;
    }

    if (!conn->hs_pktns->crypto.rx.ckm) {
      if (conn->server) {
        ngtcp2_log_info(
            &conn->log, NGTCP2_LOG_EVENT_PKT,
            "Handshake packet at this point is unexpected and discarded");
        return (ngtcp2_ssize)pktlen;
      }
      ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_CON,
                      "buffering Handshake packet len=%zu", pktlen);

      rv = conn_buffer_pkt(conn, conn->hs_pktns, path, pi, pkt, pktlen,
                           dgramlen, ts);
      if (rv != 0) {
        assert(ngtcp2_err_is_fatal(rv));
        return rv;
      }
      return (ngtcp2_ssize)pktlen;
    }

    pktns = conn->hs_pktns;
    crypto = &pktns->crypto.strm;
    encryption_level = NGTCP2_ENCRYPTION_LEVEL_HANDSHAKE;
    ckm = pktns->crypto.rx.ckm;
    hp_ctx = &pktns->crypto.rx.hp_ctx;

    break;
  default:
    ngtcp2_unreachable();
  }

  hp_mask = conn->callbacks.hp_mask;
  decrypt = conn->callbacks.decrypt;
  aead = &pktns->crypto.ctx.aead;
  hp = &pktns->crypto.ctx.hp;

  assert(ckm);
  assert(hp_mask);
  assert(decrypt);

  rv = conn_ensure_decrypt_hp_buffer(conn, (size_t)nread + 4);
  if (rv != 0) {
    return rv;
  }

  nwrite = decrypt_hp(&hd, conn->crypto.decrypt_hp_buf.base, hp, pkt, pktlen,
                      (size_t)nread, hp_ctx, hp_mask);
  if (nwrite < 0) {
    if (ngtcp2_err_is_fatal((int)nwrite)) {
      return nwrite;
    }
    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                    "could not decrypt packet number");
    return NGTCP2_ERR_DISCARD_PKT;
  }

  hdpktlen = (size_t)nwrite;
  payload = pkt + hdpktlen;
  payloadlen = hd.len - hd.pkt_numlen;

  hd.pkt_num = ngtcp2_pkt_adjust_pkt_num(pktns->rx.max_pkt_num, hd.pkt_num,
                                         hd.pkt_numlen);
  if (hd.pkt_num > NGTCP2_MAX_PKT_NUM) {
    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                    "pkn=%" PRId64 " is greater than maximum pkn", hd.pkt_num);
    return NGTCP2_ERR_DISCARD_PKT;
  }

  ngtcp2_log_rx_pkt_hd(&conn->log, &hd);

  rv = ngtcp2_pkt_verify_reserved_bits(conn->crypto.decrypt_hp_buf.base[0]);
  if (rv != 0) {
    invalid_reserved_bits = 1;

    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                    "packet has incorrect reserved bits");

    /* Will return error after decrypting payload */
  }

  if (pktns_pkt_num_is_duplicate(pktns, hd.pkt_num)) {
    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                    "packet was discarded because of duplicated packet number");
    return NGTCP2_ERR_DISCARD_PKT;
  }

  rv = conn_ensure_decrypt_buffer(conn, payloadlen);
  if (rv != 0) {
    return rv;
  }

  nwrite = decrypt_pkt(conn->crypto.decrypt_buf.base, aead, payload, payloadlen,
                       conn->crypto.decrypt_hp_buf.base, hdpktlen, hd.pkt_num,
                       ckm, decrypt);
  if (nwrite < 0) {
    if (ngtcp2_err_is_fatal((int)nwrite)) {
      return nwrite;
    }
    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                    "could not decrypt packet payload");
    return NGTCP2_ERR_DISCARD_PKT;
  }

  if (invalid_reserved_bits) {
    return NGTCP2_ERR_PROTO;
  }

  if (!conn->server && hd.version != conn->client_chosen_version &&
      !conn->negotiated_version) {
    conn->negotiated_version = hd.version;

    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_CON,
                    "the negotiated version is 0x%08x",
                    conn->negotiated_version);
  }

  payload = conn->crypto.decrypt_buf.base;
  payloadlen = (size_t)nwrite;

  switch (hd.type) {
  case NGTCP2_PKT_INITIAL:
    if (!conn->server ||
        ((conn->flags & NGTCP2_CONN_FLAG_INITIAL_PKT_PROCESSED) &&
         !ngtcp2_cid_eq(&conn->rcid, &hd.dcid))) {
      rv = conn_verify_dcid(conn, NULL, &hd);
      if (rv != 0) {
        if (ngtcp2_err_is_fatal(rv)) {
          return rv;
        }
        ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                        "packet was ignored because of mismatched DCID");
        return NGTCP2_ERR_DISCARD_PKT;
      }
    }
    break;
  case NGTCP2_PKT_HANDSHAKE:
    rv = conn_verify_dcid(conn, NULL, &hd);
    if (rv != 0) {
      if (ngtcp2_err_is_fatal(rv)) {
        return rv;
      }
      ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                      "packet was ignored because of mismatched DCID");
      return NGTCP2_ERR_DISCARD_PKT;
    }
    break;
  default:
    ngtcp2_unreachable();
  }

  if (payloadlen == 0) {
    /* QUIC packet must contain at least one frame */
    if (hd.type == NGTCP2_PKT_INITIAL) {
      return NGTCP2_ERR_DISCARD_PKT;
    }
    return NGTCP2_ERR_PROTO;
  }

  if (hd.type == NGTCP2_PKT_INITIAL &&
      !(conn->flags & NGTCP2_CONN_FLAG_INITIAL_PKT_PROCESSED)) {
    conn->flags |= NGTCP2_CONN_FLAG_INITIAL_PKT_PROCESSED;
    if (!conn->server) {
      conn->dcid.current.cid = hd.scid;
    }
  }

  ngtcp2_qlog_pkt_received_start(&conn->qlog);

  for (; payloadlen;) {
    nread = ngtcp2_pkt_decode_frame(fr, payload, payloadlen);
    if (nread < 0) {
      return nread;
    }

    payload += nread;
    payloadlen -= (size_t)nread;

    switch (fr->type) {
    case NGTCP2_FRAME_ACK:
    case NGTCP2_FRAME_ACK_ECN:
      fr->ack.ack_delay = 0;
      fr->ack.ack_delay_unscaled = 0;
      break;
    }

    ngtcp2_log_rx_fr(&conn->log, &hd, fr);

    switch (fr->type) {
    case NGTCP2_FRAME_ACK:
    case NGTCP2_FRAME_ACK_ECN:
      if (!conn->server && hd.type == NGTCP2_PKT_HANDSHAKE) {
        conn->flags |= NGTCP2_CONN_FLAG_SERVER_ADDR_VERIFIED;
      }
      rv = conn_recv_ack(conn, pktns, &fr->ack, pkt_ts, ts);
      if (rv != 0) {
        return rv;
      }
      break;
    case NGTCP2_FRAME_PADDING:
      break;
    case NGTCP2_FRAME_CRYPTO:
      if (!conn->server && !conn->negotiated_version &&
          ngtcp2_vec_len(fr->stream.data, fr->stream.datacnt)) {
        conn->negotiated_version = hd.version;

        ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_CON,
                        "the negotiated version is 0x%08x",
                        conn->negotiated_version);
      }

      rv = conn_recv_crypto(conn, encryption_level, crypto, &fr->stream);
      if (rv != 0) {
        return rv;
      }
      require_ack = 1;
      break;
    case NGTCP2_FRAME_CONNECTION_CLOSE:
      rv = conn_recv_connection_close(conn, &fr->connection_close);
      if (rv != 0) {
        return rv;
      }
      break;
    case NGTCP2_FRAME_PING:
      require_ack = 1;
      break;
    default:
      return NGTCP2_ERR_PROTO;
    }

    ngtcp2_qlog_write_frame(&conn->qlog, fr);
  }

  if (hd.type == NGTCP2_PKT_HANDSHAKE) {
    /* Successful processing of Handshake packet from a remote
       endpoint validates its source address. */
    conn->dcid.current.flags |= NGTCP2_DCID_FLAG_PATH_VALIDATED;
  }

  ngtcp2_qlog_pkt_received_end(&conn->qlog, &hd, pktlen);

  rv = pktns_commit_recv_pkt_num(pktns, hd.pkt_num, require_ack, pkt_ts);
  if (rv != 0) {
    return rv;
  }

  pktns_increase_ecn_counts(pktns, pi);

  /* Initial and Handshake are always acknowledged without delay.  No
     need to call ngtcp2_acktr_immediate_ack(). */
  rv = ngtcp2_conn_sched_ack(conn, &pktns->acktr, hd.pkt_num, require_ack,
                             pkt_ts);
  if (rv != 0) {
    return rv;
  }

  conn_restart_timer_on_read(conn, ts);

  ngtcp2_qlog_metrics_updated(&conn->qlog, &conn->cstat);

  return conn->state == NGTCP2_CS_DRAINING ? NGTCP2_ERR_DRAINING
                                           : (ngtcp2_ssize)pktlen;
}

static int is_unrecoverable_error(int liberr) {
  switch (liberr) {
  case NGTCP2_ERR_CRYPTO:
  case NGTCP2_ERR_REQUIRED_TRANSPORT_PARAM:
  case NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM:
  case NGTCP2_ERR_TRANSPORT_PARAM:
  case NGTCP2_ERR_VERSION_NEGOTIATION_FAILURE:
    return 1;
  }

  return 0;
}

/*
 * conn_recv_handshake_cpkt processes compound packet during
 * handshake.  The buffer pointed by |pkt| might contain multiple
 * packets.  The 1RTT packet must be the last one because it does not
 * have payload length field.
 *
 * This function returns the same error code returned by
 * conn_recv_handshake_pkt.
 */
static ngtcp2_ssize conn_recv_handshake_cpkt(ngtcp2_conn *conn,
                                             const ngtcp2_path *path,
                                             const ngtcp2_pkt_info *pi,
                                             const uint8_t *pkt, size_t pktlen,
                                             ngtcp2_tstamp ts) {
  ngtcp2_ssize nread;
  size_t dgramlen = pktlen;
  const uint8_t *origpkt = pkt;
  uint32_t version;

  if (ngtcp2_path_eq(&conn->dcid.current.ps.path, path)) {
    conn->dcid.current.bytes_recv += dgramlen;
  }

  while (pktlen) {
    nread =
        conn_recv_handshake_pkt(conn, path, pi, pkt, pktlen, dgramlen, ts, ts);
    if (nread < 0) {
      if (ngtcp2_err_is_fatal((int)nread)) {
        return nread;
      }

      if (nread == NGTCP2_ERR_DRAINING) {
        return NGTCP2_ERR_DRAINING;
      }

      if ((pkt[0] & NGTCP2_HEADER_FORM_BIT) && pktlen > 4) {
        /* Not a Version Negotiation packet */
        ngtcp2_get_uint32(&version, &pkt[1]);
        if (ngtcp2_pkt_get_type_long(version, pkt[0]) == NGTCP2_PKT_INITIAL) {
          if (conn->server) {
            if (is_unrecoverable_error((int)nread)) {
              /* If server gets crypto error from TLS stack, it is
                 unrecoverable, therefore drop connection. */
              return nread;
            }

            /* If server discards first Initial, then drop connection
               state.  This is because SCID in packet might be corrupted
               and the current connection state might wrongly discard
               valid packet and prevent the handshake from
               completing. */
            if (conn->in_pktns && conn->in_pktns->rx.max_pkt_num == -1) {
              return NGTCP2_ERR_DROP_CONN;
            }

            return (ngtcp2_ssize)dgramlen;
          }
          /* client */
          if (is_unrecoverable_error((int)nread)) {
            /* If client gets crypto error from TLS stack, it is
               unrecoverable, therefore drop connection. */
            return nread;
          }
          return (ngtcp2_ssize)dgramlen;
        }
      }

      if (nread == NGTCP2_ERR_DISCARD_PKT) {
        return (ngtcp2_ssize)dgramlen;
      }

      return nread;
    }

    if (nread == 0) {
      assert(!(pkt[0] & NGTCP2_HEADER_FORM_BIT));
      return pkt - origpkt;
    }

    assert(pktlen >= (size_t)nread);
    pkt += nread;
    pktlen -= (size_t)nread;

    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                    "read packet %td left %zu", nread, pktlen);
  }

  return (ngtcp2_ssize)dgramlen;
}

int ngtcp2_conn_init_stream(ngtcp2_conn *conn, ngtcp2_strm *strm,
                            int64_t stream_id, void *stream_user_data) {
  int rv;
  uint64_t max_rx_offset;
  uint64_t max_tx_offset;
  int local_stream = conn_local_stream(conn, stream_id);

  assert(conn->remote.transport_params);

  if (bidi_stream(stream_id)) {
    if (local_stream) {
      max_rx_offset =
          conn->local.transport_params.initial_max_stream_data_bidi_local;
      max_tx_offset =
          conn->remote.transport_params->initial_max_stream_data_bidi_remote;
    } else {
      max_rx_offset =
          conn->local.transport_params.initial_max_stream_data_bidi_remote;
      max_tx_offset =
          conn->remote.transport_params->initial_max_stream_data_bidi_local;
    }
  } else if (local_stream) {
    max_rx_offset = 0;
    max_tx_offset = conn->remote.transport_params->initial_max_stream_data_uni;
  } else {
    max_rx_offset = conn->local.transport_params.initial_max_stream_data_uni;
    max_tx_offset = 0;
  }

  ngtcp2_strm_init(strm, stream_id, NGTCP2_STRM_FLAG_NONE, max_rx_offset,
                   max_tx_offset, stream_user_data, &conn->frc_objalloc,
                   conn->mem);

  rv = ngtcp2_map_insert(&conn->strms, (ngtcp2_map_key_type)strm->stream_id,
                         strm);
  if (rv != 0) {
    assert(rv != NGTCP2_ERR_INVALID_ARGUMENT);
    goto fail;
  }

  return 0;

fail:
  ngtcp2_strm_free(strm);
  return rv;
}

/*
 * conn_emit_pending_stream_data passes buffered ordered stream data
 * to the application.  |rx_offset| is the first offset to deliver to
 * the application.  This function assumes that the data up to
 * |rx_offset| has been delivered already.  This function only passes
 * the ordered data without any gap.  If there is a gap, it stops
 * providing the data to the application, and returns.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User callback failed.
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 */
static int conn_emit_pending_stream_data(ngtcp2_conn *conn, ngtcp2_strm *strm,
                                         uint64_t rx_offset) {
  size_t datalen;
  const uint8_t *data;
  int rv;
  uint64_t offset;
  uint32_t sdflags;
  int handshake_completed = conn_is_tls_handshake_completed(conn);

  if (!strm->rx.rob) {
    return 0;
  }

  for (;;) {
    /* Stop calling callback if application has called
       ngtcp2_conn_shutdown_stream_read() inside the callback.
       Because it doubly counts connection window. */
    if (strm->flags & NGTCP2_STRM_FLAG_STOP_SENDING) {
      return 0;
    }

    datalen = ngtcp2_rob_data_at(strm->rx.rob, &data, rx_offset);
    if (datalen == 0) {
      assert(rx_offset == ngtcp2_strm_rx_offset(strm));
      return 0;
    }

    offset = rx_offset;
    rx_offset += datalen;

    sdflags = NGTCP2_STREAM_DATA_FLAG_NONE;
    if ((strm->flags & NGTCP2_STRM_FLAG_SHUT_RD) &&
        rx_offset == strm->rx.last_offset) {
      sdflags |= NGTCP2_STREAM_DATA_FLAG_FIN;
    }
    if (!handshake_completed) {
      sdflags |= NGTCP2_STREAM_DATA_FLAG_0RTT;
    }

    rv = conn_call_recv_stream_data(conn, strm, sdflags, offset, data, datalen);
    if (rv != 0) {
      return rv;
    }

    ngtcp2_rob_pop(strm->rx.rob, rx_offset - datalen, datalen);
  }
}

/*
 * conn_recv_crypto is called when CRYPTO frame |fr| is received.
 * |rx_offset_base| is the offset in the entire TLS handshake stream.
 * fr->offset specifies the offset in each encryption level.
 * |max_rx_offset| is, if it is nonzero, the maximum offset in the
 * entire TLS handshake stream that |fr| can carry.
 * |encryption_level| is the encryption level where this data is
 * received.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_PROTO
 *     CRYPTO frame has invalid offset.
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 * NGTCP2_ERR_CRYPTO
 *     TLS stack reported error.
 * NGTCP2_ERR_FRAME_ENCODING
 *     The end offset exceeds the maximum value.
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User-defined callback function failed.
 */
static int conn_recv_crypto(ngtcp2_conn *conn,
                            ngtcp2_encryption_level encryption_level,
                            ngtcp2_strm *crypto, const ngtcp2_stream *fr) {
  uint64_t fr_end_offset;
  uint64_t rx_offset;
  int rv;

  if (fr->datacnt == 0) {
    return 0;
  }

  fr_end_offset = fr->offset + fr->data[0].len;

  if (NGTCP2_MAX_VARINT < fr_end_offset) {
    return NGTCP2_ERR_FRAME_ENCODING;
  }

  rx_offset = ngtcp2_strm_rx_offset(crypto);

  if (fr_end_offset <= rx_offset) {
    if (conn->server &&
        !(conn->flags & NGTCP2_CONN_FLAG_HANDSHAKE_EARLY_RETRANSMIT) &&
        encryption_level == NGTCP2_ENCRYPTION_LEVEL_INITIAL) {
      /* https://datatracker.ietf.org/doc/html/rfc9002#section-6.2.3:
         Speeding Up Handshake Completion

         When a server receives an Initial packet containing duplicate
         CRYPTO data, it can assume the client did not receive all of
         the server's CRYPTO data sent in Initial packets, or the
         client's estimated RTT is too small.  ...  To speed up
         handshake completion under these conditions, an endpoint MAY
         send a packet containing unacknowledged CRYPTO data earlier
         than the PTO expiry, subject to address validation limits;
         ... */
      conn->flags |= NGTCP2_CONN_FLAG_HANDSHAKE_EARLY_RETRANSMIT;
      conn->in_pktns->rtb.probe_pkt_left = 1;
      conn->hs_pktns->rtb.probe_pkt_left = 1;
    }
    return 0;
  }

  crypto->rx.last_offset = ngtcp2_max(crypto->rx.last_offset, fr_end_offset);

  /* TODO Before dispatching incoming data to TLS stack, make sure
     that previous data in previous encryption level has been
     completely sent to TLS stack.  Usually, if data is left, it is an
     error because key is generated after consuming all data in the
     previous encryption level. */
  if (fr->offset <= rx_offset) {
    size_t ncut = (size_t)(rx_offset - fr->offset);
    const uint8_t *data = fr->data[0].base + ncut;
    size_t datalen = fr->data[0].len - ncut;
    uint64_t offset = rx_offset;

    rx_offset += datalen;
    ngtcp2_strm_update_rx_offset(crypto, rx_offset);

    rv = conn_call_recv_crypto_data(conn, encryption_level, offset, data,
                                    datalen);
    if (rv != 0) {
      return rv;
    }

    rv = conn_emit_pending_crypto_data(conn, encryption_level, crypto,
                                       rx_offset);
    if (rv != 0) {
      return rv;
    }

    return 0;
  }

  if (fr_end_offset - rx_offset > NGTCP2_MAX_REORDERED_CRYPTO_DATA) {
    return NGTCP2_ERR_CRYPTO_BUFFER_EXCEEDED;
  }

  return ngtcp2_strm_recv_reordering(crypto, fr->data[0].base, fr->data[0].len,
                                     fr->offset);
}

/*
 * conn_max_data_violated returns nonzero if receiving |datalen|
 * violates connection flow control on local endpoint.
 */
static int conn_max_data_violated(ngtcp2_conn *conn, uint64_t datalen) {
  return conn->rx.max_offset - conn->rx.offset < datalen;
}

/*
 * conn_recv_stream is called when STREAM frame |fr| is received.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_STREAM_STATE
 *     STREAM frame is received for a local stream which is not
 *     initiated; or STREAM frame is received for a local
 *     unidirectional stream
 * NGTCP2_ERR_STREAM_LIMIT
 *     STREAM frame has remote stream ID which is strictly greater
 *     than the allowed limit.
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User-defined callback function failed.
 * NGTCP2_ERR_FLOW_CONTROL
 *     Flow control limit is violated; or the end offset of stream
 *     data is beyond the NGTCP2_MAX_VARINT.
 * NGTCP2_ERR_FINAL_SIZE
 *     STREAM frame has strictly larger end offset than it is
 *     permitted.
 */
static int conn_recv_stream(ngtcp2_conn *conn, const ngtcp2_stream *fr) {
  int rv;
  ngtcp2_strm *strm;
  ngtcp2_idtr *idtr;
  uint64_t rx_offset, fr_end_offset;
  int local_stream;
  int bidi;
  uint64_t datalen = ngtcp2_vec_len(fr->data, fr->datacnt);
  uint32_t sdflags = NGTCP2_STREAM_DATA_FLAG_NONE;

  local_stream = conn_local_stream(conn, fr->stream_id);
  bidi = bidi_stream(fr->stream_id);

  if (bidi) {
    if (local_stream) {
      if (conn->local.bidi.next_stream_id <= fr->stream_id) {
        return NGTCP2_ERR_STREAM_STATE;
      }
    } else if (conn->remote.bidi.max_streams <
               ngtcp2_ord_stream_id(fr->stream_id)) {
      return NGTCP2_ERR_STREAM_LIMIT;
    }

    idtr = &conn->remote.bidi.idtr;
  } else {
    if (local_stream) {
      return NGTCP2_ERR_STREAM_STATE;
    }
    if (conn->remote.uni.max_streams < ngtcp2_ord_stream_id(fr->stream_id)) {
      return NGTCP2_ERR_STREAM_LIMIT;
    }

    idtr = &conn->remote.uni.idtr;
  }

  if (NGTCP2_MAX_VARINT - datalen < fr->offset) {
    return NGTCP2_ERR_FLOW_CONTROL;
  }

  strm = ngtcp2_conn_find_stream(conn, fr->stream_id);
  if (strm == NULL) {
    if (local_stream) {
      /* TODO The stream has been closed.  This should be responded
         with RESET_STREAM, or simply ignored. */
      return 0;
    }

    rv = ngtcp2_idtr_open(idtr, fr->stream_id);
    if (rv != 0) {
      if (ngtcp2_err_is_fatal(rv)) {
        return rv;
      }
      assert(rv == NGTCP2_ERR_STREAM_IN_USE);
      /* TODO The stream has been closed.  This should be responded
         with RESET_STREAM, or simply ignored. */
      return 0;
    }

    strm = ngtcp2_objalloc_strm_get(&conn->strm_objalloc);
    if (strm == NULL) {
      return NGTCP2_ERR_NOMEM;
    }

    rv = ngtcp2_conn_init_stream(conn, strm, fr->stream_id, NULL);
    if (rv != 0) {
      ngtcp2_objalloc_strm_release(&conn->strm_objalloc, strm);
      return rv;
    }

    if (!bidi) {
      ngtcp2_strm_shutdown(strm, NGTCP2_STRM_FLAG_SHUT_WR);
      strm->flags |= NGTCP2_STRM_FLAG_FIN_ACKED;
    }

    rv = conn_call_stream_open(conn, strm);
    if (rv != 0) {
      return rv;
    }
  }

  fr_end_offset = fr->offset + datalen;

  if (strm->rx.max_offset < fr_end_offset) {
    return NGTCP2_ERR_FLOW_CONTROL;
  }

  if (strm->rx.last_offset < fr_end_offset) {
    uint64_t len = fr_end_offset - strm->rx.last_offset;

    if (conn_max_data_violated(conn, len)) {
      return NGTCP2_ERR_FLOW_CONTROL;
    }

    conn->rx.offset += len;

    if (strm->flags & NGTCP2_STRM_FLAG_STOP_SENDING) {
      ngtcp2_conn_extend_max_offset(conn, len);
    }
  }

  rx_offset = ngtcp2_strm_rx_offset(strm);

  if (fr->fin) {
    if (strm->flags & NGTCP2_STRM_FLAG_SHUT_RD) {
      if (strm->rx.last_offset != fr_end_offset) {
        return NGTCP2_ERR_FINAL_SIZE;
      }

      if (strm->flags & NGTCP2_STRM_FLAG_RESET_STREAM_RECVED) {
        return 0;
      }

      if (rx_offset == fr_end_offset) {
        return 0;
      }
    } else if (strm->rx.last_offset > fr_end_offset) {
      return NGTCP2_ERR_FINAL_SIZE;
    } else {
      strm->rx.last_offset = fr_end_offset;

      ngtcp2_strm_shutdown(strm, NGTCP2_STRM_FLAG_SHUT_RD);
    }
  } else {
    if ((strm->flags & NGTCP2_STRM_FLAG_SHUT_RD) &&
        strm->rx.last_offset < fr_end_offset) {
      return NGTCP2_ERR_FINAL_SIZE;
    }

    strm->rx.last_offset = ngtcp2_max(strm->rx.last_offset, fr_end_offset);

    if (fr_end_offset <= rx_offset) {
      return 0;
    }

    if (strm->flags & NGTCP2_STRM_FLAG_RESET_STREAM_RECVED) {
      return 0;
    }
  }

  if (fr->offset <= rx_offset) {
    size_t ncut = (size_t)(rx_offset - fr->offset);
    uint64_t offset = rx_offset;
    const uint8_t *data;
    int fin;

    if (fr->datacnt) {
      data = fr->data[0].base + ncut;
      datalen -= ncut;

      rx_offset += datalen;
      ngtcp2_strm_update_rx_offset(strm, rx_offset);
    } else {
      data = NULL;
      datalen = 0;
    }

    if (strm->flags & NGTCP2_STRM_FLAG_STOP_SENDING) {
      return ngtcp2_conn_close_stream_if_shut_rdwr(conn, strm);
    }

    fin = (strm->flags & NGTCP2_STRM_FLAG_SHUT_RD) &&
          rx_offset == strm->rx.last_offset;

    if (fin || datalen) {
      if (fin) {
        sdflags |= NGTCP2_STREAM_DATA_FLAG_FIN;
      }
      if (!conn_is_tls_handshake_completed(conn)) {
        sdflags |= NGTCP2_STREAM_DATA_FLAG_0RTT;
      }
      rv = conn_call_recv_stream_data(conn, strm, sdflags, offset, data,
                                      (size_t)datalen);
      if (rv != 0) {
        return rv;
      }

      rv = conn_emit_pending_stream_data(conn, strm, rx_offset);
      if (rv != 0) {
        return rv;
      }
    }
  } else if (fr->datacnt && !(strm->flags & NGTCP2_STRM_FLAG_STOP_SENDING)) {
    rv = ngtcp2_strm_recv_reordering(strm, fr->data[0].base, fr->data[0].len,
                                     fr->offset);
    if (rv != 0) {
      return rv;
    }
  }
  return ngtcp2_conn_close_stream_if_shut_rdwr(conn, strm);
}

/*
 * conn_reset_stream adds RESET_STREAM frame to the transmission
 * queue.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 */
static int conn_reset_stream(ngtcp2_conn *conn, ngtcp2_strm *strm,
                             uint64_t app_error_code) {
  strm->flags |= NGTCP2_STRM_FLAG_SEND_RESET_STREAM;
  strm->tx.reset_stream_app_error_code = app_error_code;

  if (ngtcp2_strm_is_tx_queued(strm)) {
    return 0;
  }

  strm->cycle = conn_tx_strmq_first_cycle(conn);

  return ngtcp2_conn_tx_strmq_push(conn, strm);
}

/*
 * conn_stop_sending adds STOP_SENDING frame to the transmission
 * queue.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 */
static int conn_stop_sending(ngtcp2_conn *conn, ngtcp2_strm *strm,
                             uint64_t app_error_code) {
  strm->flags |= NGTCP2_STRM_FLAG_SEND_STOP_SENDING;
  strm->tx.stop_sending_app_error_code = app_error_code;

  if (ngtcp2_strm_is_tx_queued(strm)) {
    return 0;
  }

  strm->cycle = conn_tx_strmq_first_cycle(conn);

  return ngtcp2_conn_tx_strmq_push(conn, strm);
}

/*
 * handle_max_remote_streams_extension extends
 * |*punsent_max_remote_streams| by |n| if a condition allows it.
 */
static void
handle_max_remote_streams_extension(uint64_t *punsent_max_remote_streams,
                                    size_t n) {
  if (
#if SIZE_MAX > UINT32_MAX
      NGTCP2_MAX_STREAMS < n ||
#endif /* SIZE_MAX > UINT32_MAX */
      *punsent_max_remote_streams > (uint64_t)(NGTCP2_MAX_STREAMS - n)) {
    *punsent_max_remote_streams = NGTCP2_MAX_STREAMS;
  } else {
    *punsent_max_remote_streams += n;
  }
}

/*
 * conn_recv_reset_stream is called when RESET_STREAM |fr| is
 * received.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_STREAM_STATE
 *     RESET_STREAM frame is received to the local stream which is not
 *     initiated.
 * NGTCP2_ERR_STREAM_LIMIT
 *     RESET_STREAM frame has remote stream ID which is strictly
 *     greater than the allowed limit.
 * NGTCP2_ERR_PROTO
 *     RESET_STREAM frame is received to the local unidirectional
 *     stream
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User-defined callback function failed.
 * NGTCP2_ERR_FLOW_CONTROL
 *     Flow control limit is violated; or the final size is beyond the
 *     NGTCP2_MAX_VARINT.
 * NGTCP2_ERR_FINAL_SIZE
 *     The final offset is strictly larger than it is permitted.
 */
static int conn_recv_reset_stream(ngtcp2_conn *conn,
                                  const ngtcp2_reset_stream *fr) {
  ngtcp2_strm *strm;
  int local_stream = conn_local_stream(conn, fr->stream_id);
  int bidi = bidi_stream(fr->stream_id);
  uint64_t datalen;
  ngtcp2_idtr *idtr;
  int rv;

  /* TODO share this piece of code */
  if (bidi) {
    if (local_stream) {
      if (conn->local.bidi.next_stream_id <= fr->stream_id) {
        return NGTCP2_ERR_STREAM_STATE;
      }
    } else if (conn->remote.bidi.max_streams <
               ngtcp2_ord_stream_id(fr->stream_id)) {
      return NGTCP2_ERR_STREAM_LIMIT;
    }

    idtr = &conn->remote.bidi.idtr;
  } else {
    if (local_stream) {
      return NGTCP2_ERR_PROTO;
    }
    if (conn->remote.uni.max_streams < ngtcp2_ord_stream_id(fr->stream_id)) {
      return NGTCP2_ERR_STREAM_LIMIT;
    }

    idtr = &conn->remote.uni.idtr;
  }

  if (NGTCP2_MAX_VARINT < fr->final_size) {
    return NGTCP2_ERR_FLOW_CONTROL;
  }

  strm = ngtcp2_conn_find_stream(conn, fr->stream_id);
  if (strm == NULL) {
    if (local_stream) {
      return 0;
    }

    rv = ngtcp2_idtr_open(idtr, fr->stream_id);
    if (rv != 0) {
      if (ngtcp2_err_is_fatal(rv)) {
        return rv;
      }
      assert(rv == NGTCP2_ERR_STREAM_IN_USE);
      return 0;
    }

    if (conn_initial_stream_rx_offset(conn, fr->stream_id) < fr->final_size ||
        conn_max_data_violated(conn, fr->final_size)) {
      return NGTCP2_ERR_FLOW_CONTROL;
    }

    /* Stream is reset before we create ngtcp2_strm object. */
    strm = ngtcp2_objalloc_strm_get(&conn->strm_objalloc);
    if (strm == NULL) {
      return NGTCP2_ERR_NOMEM;
    }
    rv = ngtcp2_conn_init_stream(conn, strm, fr->stream_id, NULL);
    if (rv != 0) {
      ngtcp2_objalloc_strm_release(&conn->strm_objalloc, strm);
      return rv;
    }

    rv = conn_call_stream_open(conn, strm);
    if (rv != 0) {
      return rv;
    }
  }

  if ((strm->flags & NGTCP2_STRM_FLAG_SHUT_RD)) {
    if (strm->rx.last_offset != fr->final_size) {
      return NGTCP2_ERR_FINAL_SIZE;
    }
  } else if (strm->rx.last_offset > fr->final_size) {
    return NGTCP2_ERR_FINAL_SIZE;
  }

  if (strm->flags & NGTCP2_STRM_FLAG_RESET_STREAM_RECVED) {
    return 0;
  }

  if (strm->rx.max_offset < fr->final_size) {
    return NGTCP2_ERR_FLOW_CONTROL;
  }

  datalen = fr->final_size - strm->rx.last_offset;

  if (conn_max_data_violated(conn, datalen)) {
    return NGTCP2_ERR_FLOW_CONTROL;
  }

  rv = conn_call_stream_reset(conn, fr->stream_id, fr->final_size,
                              fr->app_error_code, strm->stream_user_data);
  if (rv != 0) {
    return rv;
  }

  /* Extend connection flow control window for the amount of data
     which are not passed to application. */
  if (!(strm->flags & NGTCP2_STRM_FLAG_STOP_SENDING)) {
    ngtcp2_conn_extend_max_offset(conn, strm->rx.last_offset -
                                            ngtcp2_strm_rx_offset(strm));
  }

  conn->rx.offset += datalen;
  ngtcp2_conn_extend_max_offset(conn, datalen);

  strm->rx.last_offset = fr->final_size;
  strm->flags |=
      NGTCP2_STRM_FLAG_SHUT_RD | NGTCP2_STRM_FLAG_RESET_STREAM_RECVED;

  ngtcp2_strm_set_app_error_code(strm, fr->app_error_code);

  return ngtcp2_conn_close_stream_if_shut_rdwr(conn, strm);
}

/*
 * conn_recv_stop_sending is called when STOP_SENDING |fr| is received.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_STREAM_STATE
 *     STOP_SENDING frame is received for a local stream which is not
 *     initiated; or STOP_SENDING frame is received for a local
 *     unidirectional stream.
 * NGTCP2_ERR_STREAM_LIMIT
 *     STOP_SENDING frame has remote stream ID which is strictly
 *     greater than the allowed limit.
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User-defined callback function failed.
 */
static int conn_recv_stop_sending(ngtcp2_conn *conn,
                                  const ngtcp2_stop_sending *fr) {
  int rv;
  ngtcp2_strm *strm;
  ngtcp2_idtr *idtr;
  int local_stream = conn_local_stream(conn, fr->stream_id);
  int bidi = bidi_stream(fr->stream_id);

  if (bidi) {
    if (local_stream) {
      if (conn->local.bidi.next_stream_id <= fr->stream_id) {
        return NGTCP2_ERR_STREAM_STATE;
      }
    } else if (conn->remote.bidi.max_streams <
               ngtcp2_ord_stream_id(fr->stream_id)) {
      return NGTCP2_ERR_STREAM_LIMIT;
    }

    idtr = &conn->remote.bidi.idtr;
  } else {
    if (!local_stream || conn->local.uni.next_stream_id <= fr->stream_id) {
      return NGTCP2_ERR_STREAM_STATE;
    }

    idtr = &conn->remote.uni.idtr;
  }

  strm = ngtcp2_conn_find_stream(conn, fr->stream_id);
  if (strm == NULL) {
    if (local_stream) {
      return 0;
    }
    rv = ngtcp2_idtr_open(idtr, fr->stream_id);
    if (rv != 0) {
      if (ngtcp2_err_is_fatal(rv)) {
        return rv;
      }
      assert(rv == NGTCP2_ERR_STREAM_IN_USE);
      return 0;
    }

    /* STOP_SENDING frame is received before we create ngtcp2_strm
       object. */
    strm = ngtcp2_objalloc_strm_get(&conn->strm_objalloc);
    if (strm == NULL) {
      return NGTCP2_ERR_NOMEM;
    }
    rv = ngtcp2_conn_init_stream(conn, strm, fr->stream_id, NULL);
    if (rv != 0) {
      ngtcp2_objalloc_strm_release(&conn->strm_objalloc, strm);
      return rv;
    }

    rv = conn_call_stream_open(conn, strm);
    if (rv != 0) {
      return rv;
    }
  }

  if (strm->flags & NGTCP2_STRM_FLAG_STOP_SENDING_RECVED) {
    return 0;
  }

  ngtcp2_strm_set_app_error_code(strm, fr->app_error_code);

  /* No RESET_STREAM is required if we have sent FIN and all data have
     been acknowledged. */
  if (!ngtcp2_strm_is_all_tx_data_fin_acked(strm) &&
      !(strm->flags & NGTCP2_STRM_FLAG_RESET_STREAM)) {
    strm->flags |= NGTCP2_STRM_FLAG_RESET_STREAM;

    rv = conn_reset_stream(conn, strm, fr->app_error_code);
    if (rv != 0) {
      return rv;
    }
  }

  strm->flags |=
      NGTCP2_STRM_FLAG_SHUT_WR | NGTCP2_STRM_FLAG_STOP_SENDING_RECVED;

  ngtcp2_strm_streamfrq_clear(strm);

  return ngtcp2_conn_close_stream_if_shut_rdwr(conn, strm);
}

/*
 * check_stateless_reset returns nonzero if Stateless Reset |sr|
 * coming via |path| is valid against |dcid|.
 */
static int check_stateless_reset(const ngtcp2_dcid *dcid,
                                 const ngtcp2_path *path,
                                 const ngtcp2_pkt_stateless_reset *sr) {
  return ngtcp2_path_eq(&dcid->ps.path, path) &&
         ngtcp2_dcid_verify_stateless_reset_token(
             dcid, sr->stateless_reset_token) == 0;
}

/*
 * conn_on_stateless_reset decodes Stateless Reset from the buffer
 * pointed by |payload| whose length is |payloadlen|.  |payload|
 * should start after first byte of packet.
 *
 * If Stateless Reset is decoded, and the Stateless Reset Token is
 * validated, the connection is closed.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_INVALID_ARGUMENT
 *     Could not decode Stateless Reset; or Stateless Reset Token does
 *     not match; or No stateless reset token is available.
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User callback failed.
 */
static int conn_on_stateless_reset(ngtcp2_conn *conn, const ngtcp2_path *path,
                                   const uint8_t *payload, size_t payloadlen) {
  int rv = 1;
  ngtcp2_pv *pv = conn->pv;
  ngtcp2_dcid *dcid;
  ngtcp2_pkt_stateless_reset sr;
  size_t len, i;

  rv = ngtcp2_pkt_decode_stateless_reset(&sr, payload, payloadlen);
  if (rv != 0) {
    return rv;
  }

  if (!check_stateless_reset(&conn->dcid.current, path, &sr) &&
      (!pv || (!check_stateless_reset(&pv->dcid, path, &sr) &&
               (!(pv->flags & NGTCP2_PV_FLAG_FALLBACK_ON_FAILURE) ||
                !check_stateless_reset(&pv->fallback_dcid, path, &sr))))) {
    len = ngtcp2_ringbuf_len(&conn->dcid.retired.rb);
    for (i = 0; i < len; ++i) {
      dcid = ngtcp2_ringbuf_get(&conn->dcid.retired.rb, i);
      if (check_stateless_reset(dcid, path, &sr)) {
        break;
      }
    }

    if (i == len) {
      len = ngtcp2_ringbuf_len(&conn->dcid.bound.rb);
      for (i = 0; i < len; ++i) {
        dcid = ngtcp2_ringbuf_get(&conn->dcid.bound.rb, i);
        if (check_stateless_reset(dcid, path, &sr)) {
          break;
        }
      }

      if (i == len) {
        return NGTCP2_ERR_INVALID_ARGUMENT;
      }
    }
  }

  conn->state = NGTCP2_CS_DRAINING;

  ngtcp2_log_rx_sr(&conn->log, &sr);

  ngtcp2_qlog_stateless_reset_pkt_received(&conn->qlog, &sr);

  return conn_call_recv_stateless_reset(conn, &sr);
}

/*
 * conn_recv_max_streams processes the incoming MAX_STREAMS frame
 * |fr|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User callback failed.
 * NGTCP2_ERR_FRAME_ENCODING
 *     The maximum streams field exceeds the maximum value.
 */
static int conn_recv_max_streams(ngtcp2_conn *conn,
                                 const ngtcp2_max_streams *fr) {
  uint64_t n;

  if (fr->max_streams > NGTCP2_MAX_STREAMS) {
    return NGTCP2_ERR_FRAME_ENCODING;
  }

  n = ngtcp2_min(fr->max_streams, NGTCP2_MAX_STREAMS);

  if (fr->type == NGTCP2_FRAME_MAX_STREAMS_BIDI) {
    if (conn->local.bidi.max_streams < n) {
      conn->local.bidi.max_streams = n;
      return conn_call_extend_max_local_streams_bidi(conn, n);
    }
    return 0;
  }

  if (conn->local.uni.max_streams < n) {
    conn->local.uni.max_streams = n;
    return conn_call_extend_max_local_streams_uni(conn, n);
  }
  return 0;
}

static int conn_retire_dcid_prior_to(ngtcp2_conn *conn, ngtcp2_ringbuf *rb,
                                     uint64_t retire_prior_to) {
  size_t i;
  ngtcp2_dcid *dcid, *last;
  int rv;

  for (i = 0; i < ngtcp2_ringbuf_len(rb);) {
    dcid = ngtcp2_ringbuf_get(rb, i);
    if (dcid->seq >= retire_prior_to) {
      ++i;
      continue;
    }

    rv = conn_retire_dcid_seq(conn, dcid->seq);
    if (rv != 0) {
      return rv;
    }

    if (i == 0) {
      ngtcp2_ringbuf_pop_front(rb);
      continue;
    }

    if (i == ngtcp2_ringbuf_len(rb) - 1) {
      ngtcp2_ringbuf_pop_back(rb);
      break;
    }

    last = ngtcp2_ringbuf_get(rb, ngtcp2_ringbuf_len(rb) - 1);
    ngtcp2_dcid_copy(dcid, last);
    ngtcp2_ringbuf_pop_back(rb);
  }

  return 0;
}

/*
 * conn_recv_new_connection_id processes the incoming
 * NEW_CONNECTION_ID frame |fr|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_PROTO
 *     |fr| has the duplicated sequence number with different CID or
 *     token; or DCID is zero-length.
 */
static int conn_recv_new_connection_id(ngtcp2_conn *conn,
                                       const ngtcp2_new_connection_id *fr) {
  size_t i, len;
  ngtcp2_dcid *dcid;
  ngtcp2_pv *pv = conn->pv;
  int rv;
  int found = 0;
  size_t extra_dcid = 0;

  if (conn->dcid.current.cid.datalen == 0) {
    return NGTCP2_ERR_PROTO;
  }

  if (fr->retire_prior_to > fr->seq) {
    return NGTCP2_ERR_FRAME_ENCODING;
  }

  rv = ngtcp2_dcid_verify_uniqueness(&conn->dcid.current, fr->seq, &fr->cid,
                                     fr->stateless_reset_token);
  if (rv != 0) {
    return rv;
  }
  if (ngtcp2_cid_eq(&conn->dcid.current.cid, &fr->cid)) {
    found = 1;
  }

  if (pv) {
    rv = ngtcp2_dcid_verify_uniqueness(&pv->dcid, fr->seq, &fr->cid,
                                       fr->stateless_reset_token);
    if (rv != 0) {
      return rv;
    }
    if (ngtcp2_cid_eq(&pv->dcid.cid, &fr->cid)) {
      found = 1;
    }
  }

  len = ngtcp2_ringbuf_len(&conn->dcid.bound.rb);

  for (i = 0; i < len; ++i) {
    dcid = ngtcp2_ringbuf_get(&conn->dcid.bound.rb, i);
    rv = ngtcp2_dcid_verify_uniqueness(dcid, fr->seq, &fr->cid,
                                       fr->stateless_reset_token);
    if (rv != 0) {
      return NGTCP2_ERR_PROTO;
    }
    if (ngtcp2_cid_eq(&dcid->cid, &fr->cid)) {
      found = 1;
    }
  }

  len = ngtcp2_ringbuf_len(&conn->dcid.unused.rb);

  for (i = 0; i < len; ++i) {
    dcid = ngtcp2_ringbuf_get(&conn->dcid.unused.rb, i);
    rv = ngtcp2_dcid_verify_uniqueness(dcid, fr->seq, &fr->cid,
                                       fr->stateless_reset_token);
    if (rv != 0) {
      return NGTCP2_ERR_PROTO;
    }
    if (ngtcp2_cid_eq(&dcid->cid, &fr->cid)) {
      found = 1;
    }
  }

  if (conn->dcid.retire_prior_to < fr->retire_prior_to) {
    conn->dcid.retire_prior_to = fr->retire_prior_to;

    rv = conn_retire_dcid_prior_to(conn, &conn->dcid.bound.rb,
                                   fr->retire_prior_to);
    if (rv != 0) {
      return rv;
    }

    rv = conn_retire_dcid_prior_to(conn, &conn->dcid.unused.rb,
                                   conn->dcid.retire_prior_to);
    if (rv != 0) {
      return rv;
    }
  } else if (fr->seq < conn->dcid.retire_prior_to) {
    /* If packets are reordered, we might have retire_prior_to which
       is larger than fr->seq.

       A malicious peer might send crafted NEW_CONNECTION_ID to force
       local endpoint to create lots of RETIRE_CONNECTION_ID frames.
       For example, a peer might send seq = 50000 and retire_prior_to
       = 50000.  Then send NEW_CONNECTION_ID frames with seq <
       50000. */
    return conn_retire_dcid_seq(conn, fr->seq);
  }

  if (found) {
    return 0;
  }

  if (ngtcp2_gaptr_is_pushed(&conn->dcid.seqgap, fr->seq, 1)) {
    return 0;
  }

  rv = ngtcp2_gaptr_push(&conn->dcid.seqgap, fr->seq, 1);
  if (rv != 0) {
    return rv;
  }

  if (ngtcp2_ksl_len(&conn->dcid.seqgap.gap) > 32) {
    ngtcp2_gaptr_drop_first_gap(&conn->dcid.seqgap);
  }

  len = ngtcp2_ringbuf_len(&conn->dcid.unused.rb);

  if (conn->dcid.current.seq >= conn->dcid.retire_prior_to) {
    ++extra_dcid;
  }
  if (pv) {
    if (pv->dcid.seq != conn->dcid.current.seq &&
        pv->dcid.seq >= conn->dcid.retire_prior_to) {
      ++extra_dcid;
    }
    if ((pv->flags & NGTCP2_PV_FLAG_FALLBACK_ON_FAILURE) &&
        pv->fallback_dcid.seq != conn->dcid.current.seq &&
        pv->fallback_dcid.seq >= conn->dcid.retire_prior_to) {
      ++extra_dcid;
    }
  }

  if (conn->local.transport_params.active_connection_id_limit <=
      len + extra_dcid) {
    return NGTCP2_ERR_CONNECTION_ID_LIMIT;
  }

  if (len >= NGTCP2_MAX_DCID_POOL_SIZE) {
    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_CON, "too many connection ID");
    return 0;
  }

  dcid = ngtcp2_ringbuf_push_back(&conn->dcid.unused.rb);
  ngtcp2_dcid_init(dcid, fr->seq, &fr->cid, fr->stateless_reset_token);

  return 0;
}

/*
 * conn_post_process_recv_new_connection_id handles retirement request
 * of active DCIDs.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User-defined callback function failed.
 */
static int conn_post_process_recv_new_connection_id(ngtcp2_conn *conn,
                                                    ngtcp2_tstamp ts) {
  ngtcp2_pv *pv = conn->pv;
  ngtcp2_dcid *dcid;
  int rv;

  if (conn->dcid.current.seq < conn->dcid.retire_prior_to) {
    if (ngtcp2_ringbuf_len(&conn->dcid.unused.rb) == 0) {
      return 0;
    }

    rv = conn_retire_dcid(conn, &conn->dcid.current, ts);
    if (rv != 0) {
      return rv;
    }

    dcid = ngtcp2_ringbuf_get(&conn->dcid.unused.rb, 0);
    if (pv) {
      if (conn->dcid.current.seq == pv->dcid.seq) {
        ngtcp2_dcid_copy_cid_token(&pv->dcid, dcid);
      }
      if ((pv->flags & NGTCP2_PV_FLAG_FALLBACK_ON_FAILURE) &&
          conn->dcid.current.seq == pv->fallback_dcid.seq) {
        ngtcp2_dcid_copy_cid_token(&pv->fallback_dcid, dcid);
      }
    }

    ngtcp2_dcid_copy_cid_token(&conn->dcid.current, dcid);
    ngtcp2_ringbuf_pop_front(&conn->dcid.unused.rb);

    rv = conn_call_activate_dcid(conn, &conn->dcid.current);
    if (rv != 0) {
      return rv;
    }
  }

  if (pv) {
    if (pv->dcid.seq < conn->dcid.retire_prior_to) {
      if (ngtcp2_ringbuf_len(&conn->dcid.unused.rb)) {
        rv = conn_retire_dcid(conn, &pv->dcid, ts);
        if (rv != 0) {
          return rv;
        }

        dcid = ngtcp2_ringbuf_get(&conn->dcid.unused.rb, 0);

        if ((pv->flags & NGTCP2_PV_FLAG_FALLBACK_ON_FAILURE) &&
            pv->dcid.seq == pv->fallback_dcid.seq) {
          ngtcp2_dcid_copy_cid_token(&pv->fallback_dcid, dcid);
        }

        ngtcp2_dcid_copy_cid_token(&pv->dcid, dcid);
        ngtcp2_ringbuf_pop_front(&conn->dcid.unused.rb);

        rv = conn_call_activate_dcid(conn, &pv->dcid);
        if (rv != 0) {
          return rv;
        }
      } else {
        ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PTV,
                        "path migration is aborted because connection ID is"
                        "retired and no unused connection ID is available");

        return conn_abort_pv(conn, ts);
      }
    }
    if ((pv->flags & NGTCP2_PV_FLAG_FALLBACK_ON_FAILURE) &&
        pv->fallback_dcid.seq < conn->dcid.retire_prior_to) {
      if (ngtcp2_ringbuf_len(&conn->dcid.unused.rb)) {
        rv = conn_retire_dcid(conn, &pv->fallback_dcid, ts);
        if (rv != 0) {
          return rv;
        }

        dcid = ngtcp2_ringbuf_get(&conn->dcid.unused.rb, 0);
        ngtcp2_dcid_copy_cid_token(&pv->fallback_dcid, dcid);
        ngtcp2_ringbuf_pop_front(&conn->dcid.unused.rb);

        rv = conn_call_activate_dcid(conn, &pv->fallback_dcid);
        if (rv != 0) {
          return rv;
        }
      } else {
        /* Now we have no fallback dcid. */
        return conn_abort_pv(conn, ts);
      }
    }
  }

  return 0;
}

/*
 * conn_recv_retire_connection_id processes the incoming
 * RETIRE_CONNECTION_ID frame |fr|.  |hd| is a packet header which
 * |fr| is included.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 * NGTCP2_ERR_PROTO
 *     SCID is zero-length.
 * NGTCP2_ERR_FRAME_ENCODING
 *     Attempt to retire CID which is used as DCID to send this frame.
 */
static int conn_recv_retire_connection_id(ngtcp2_conn *conn,
                                          const ngtcp2_pkt_hd *hd,
                                          const ngtcp2_retire_connection_id *fr,
                                          ngtcp2_tstamp ts) {
  ngtcp2_ksl_it it;
  ngtcp2_scid *scid;

  if (conn->oscid.datalen == 0 || conn->scid.last_seq < fr->seq) {
    return NGTCP2_ERR_PROTO;
  }

  for (it = ngtcp2_ksl_begin(&conn->scid.set); !ngtcp2_ksl_it_end(&it);
       ngtcp2_ksl_it_next(&it)) {
    scid = ngtcp2_ksl_it_get(&it);
    if (scid->seq == fr->seq) {
      if (ngtcp2_cid_eq(&scid->cid, &hd->dcid)) {
        return NGTCP2_ERR_PROTO;
      }

      if (!(scid->flags & NGTCP2_SCID_FLAG_RETIRED)) {
        scid->flags |= NGTCP2_SCID_FLAG_RETIRED;
        ++conn->scid.num_retired;
      }

      if (scid->pe.index != NGTCP2_PQ_BAD_INDEX) {
        ngtcp2_pq_remove(&conn->scid.used, &scid->pe);
        scid->pe.index = NGTCP2_PQ_BAD_INDEX;
      }

      scid->retired_ts = ts;

      return ngtcp2_pq_push(&conn->scid.used, &scid->pe);
    }
  }

  return 0;
}

/*
 * conn_recv_new_token processes the incoming NEW_TOKEN frame |fr|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_FRAME_ENCODING
 *     Token is empty
 * NGTCP2_ERR_PROTO:
 *     Server received NEW_TOKEN.
 */
static int conn_recv_new_token(ngtcp2_conn *conn, const ngtcp2_new_token *fr) {
  if (conn->server) {
    return NGTCP2_ERR_PROTO;
  }

  if (fr->tokenlen == 0) {
    return NGTCP2_ERR_FRAME_ENCODING;
  }

  return conn_call_recv_new_token(conn, fr->token, fr->tokenlen);
}

/*
 * conn_recv_streams_blocked_bidi processes the incoming
 * STREAMS_BLOCKED (0x16).
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_FRAME_ENCODING
 *     Maximum Streams is larger than advertised value.
 */
static int conn_recv_streams_blocked_bidi(ngtcp2_conn *conn,
                                          ngtcp2_streams_blocked *fr) {
  if (fr->max_streams > conn->remote.bidi.max_streams) {
    return NGTCP2_ERR_FRAME_ENCODING;
  }

  return 0;
}

/*
 * conn_recv_streams_blocked_uni processes the incoming
 * STREAMS_BLOCKED (0x17).
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_FRAME_ENCODING
 *     Maximum Streams is larger than advertised value.
 */
static int conn_recv_streams_blocked_uni(ngtcp2_conn *conn,
                                         ngtcp2_streams_blocked *fr) {
  if (fr->max_streams > conn->remote.uni.max_streams) {
    return NGTCP2_ERR_FRAME_ENCODING;
  }

  return 0;
}

/*
 * conn_recv_stream_data_blocked processes the incoming
 * STREAM_DATA_BLOCKED frame |fr|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_STREAM_STATE
 *     STREAM_DATA_BLOCKED is received for a local stream which is not
 *     initiated; or it is received for a local unidirectional stream.
 * NGTCP2_ERR_STREAM_LIMIT
 *     STREAM_DATA_BLOCKED has remote stream ID which is strictly
 *     greater than the allowed limit.
 * NGTCP2_ERR_FLOW_CONTROL
 *     STREAM_DATA_BLOCKED frame violates flow control limit.
 * NGTCP2_ERR_FINAL_SIZE
 *     The offset is strictly larger than it is permitted.
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User-defined callback function failed.
 */
static int conn_recv_stream_data_blocked(ngtcp2_conn *conn,
                                         ngtcp2_stream_data_blocked *fr) {
  int rv;
  ngtcp2_strm *strm;
  ngtcp2_idtr *idtr;
  int local_stream = conn_local_stream(conn, fr->stream_id);
  int bidi = bidi_stream(fr->stream_id);
  uint64_t datalen;

  if (bidi) {
    if (local_stream) {
      if (conn->local.bidi.next_stream_id <= fr->stream_id) {
        return NGTCP2_ERR_STREAM_STATE;
      }
    } else if (conn->remote.bidi.max_streams <
               ngtcp2_ord_stream_id(fr->stream_id)) {
      return NGTCP2_ERR_STREAM_LIMIT;
    }

    idtr = &conn->remote.bidi.idtr;
  } else {
    if (local_stream) {
      return NGTCP2_ERR_STREAM_STATE;
    }
    if (conn->remote.uni.max_streams < ngtcp2_ord_stream_id(fr->stream_id)) {
      return NGTCP2_ERR_STREAM_LIMIT;
    }

    idtr = &conn->remote.uni.idtr;
  }

  strm = ngtcp2_conn_find_stream(conn, fr->stream_id);
  if (strm == NULL) {
    if (local_stream) {
      return 0;
    }

    rv = ngtcp2_idtr_open(idtr, fr->stream_id);
    if (rv != 0) {
      if (ngtcp2_err_is_fatal(rv)) {
        return rv;
      }
      assert(rv == NGTCP2_ERR_STREAM_IN_USE);
      return 0;
    }

    /* Frame is received before we create ngtcp2_strm object. */
    strm = ngtcp2_objalloc_strm_get(&conn->strm_objalloc);
    if (strm == NULL) {
      return NGTCP2_ERR_NOMEM;
    }
    rv = ngtcp2_conn_init_stream(conn, strm, fr->stream_id, NULL);
    if (rv != 0) {
      ngtcp2_objalloc_strm_release(&conn->strm_objalloc, strm);
      return rv;
    }

    if (!bidi) {
      ngtcp2_strm_shutdown(strm, NGTCP2_STRM_FLAG_SHUT_WR);
      strm->flags |= NGTCP2_STRM_FLAG_FIN_ACKED;
    }

    rv = conn_call_stream_open(conn, strm);
    if (rv != 0) {
      return rv;
    }
  }

  if (strm->rx.max_offset < fr->offset) {
    return NGTCP2_ERR_FLOW_CONTROL;
  }

  if (fr->offset <= strm->rx.last_offset) {
    return 0;
  }

  if (strm->flags & NGTCP2_STRM_FLAG_SHUT_RD) {
    return NGTCP2_ERR_FINAL_SIZE;
  }

  datalen = fr->offset - strm->rx.last_offset;
  if (datalen) {
    if (conn_max_data_violated(conn, datalen)) {
      return NGTCP2_ERR_FLOW_CONTROL;
    }

    conn->rx.offset += datalen;

    if (strm->flags & NGTCP2_STRM_FLAG_STOP_SENDING) {
      ngtcp2_conn_extend_max_offset(conn, datalen);
    }
  }

  strm->rx.last_offset = fr->offset;

  return 0;
}

/*
 * conn_recv_data_blocked processes the incoming DATA_BLOCKED frame
 * |fr|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_FLOW_CONTROL
 *     It violates connection-level flow control limit.
 */
static int conn_recv_data_blocked(ngtcp2_conn *conn, ngtcp2_data_blocked *fr) {
  if (conn->rx.max_offset < fr->offset) {
    return NGTCP2_ERR_FLOW_CONTROL;
  }

  return 0;
}

/*
 * conn_select_preferred_addr asks a client application to select a
 * server address from preferred addresses received from server.  If a
 * client chooses the address, path validation will start.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User-defined callback function failed.
 */
static int conn_select_preferred_addr(ngtcp2_conn *conn) {
  ngtcp2_path_storage ps;
  int rv;
  ngtcp2_pv *pv;
  ngtcp2_dcid *dcid;

  if (ngtcp2_ringbuf_len(&conn->dcid.unused.rb) == 0) {
    return 0;
  }

  ngtcp2_path_storage_zero(&ps);
  ngtcp2_addr_copy(&ps.path.local, &conn->dcid.current.ps.path.local);

  rv = conn_call_select_preferred_addr(conn, &ps.path);
  if (rv != 0) {
    return rv;
  }

  if (ps.path.remote.addrlen == 0 ||
      ngtcp2_addr_eq(&conn->dcid.current.ps.path.remote, &ps.path.remote)) {
    return 0;
  }

  assert(conn->pv == NULL);

  dcid = ngtcp2_ringbuf_get(&conn->dcid.unused.rb, 0);
  ngtcp2_dcid_set_path(dcid, &ps.path);

  rv = ngtcp2_pv_new(&pv, dcid, conn_compute_pv_timeout(conn),
                     NGTCP2_PV_FLAG_PREFERRED_ADDR, &conn->log, conn->mem);
  if (rv != 0) {
    /* TODO Call ngtcp2_dcid_free here if it is introduced */
    return rv;
  }

  ngtcp2_ringbuf_pop_front(&conn->dcid.unused.rb);
  conn->pv = pv;

  return conn_call_activate_dcid(conn, &pv->dcid);
}

/*
 * conn_recv_handshake_done processes the incoming HANDSHAKE_DONE
 * frame |fr|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_PROTO
 *     Server received HANDSHAKE_DONE frame.
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User-defined callback function failed.
 */
static int conn_recv_handshake_done(ngtcp2_conn *conn, ngtcp2_tstamp ts) {
  int rv;

  if (conn->server) {
    return NGTCP2_ERR_PROTO;
  }

  if (conn->flags & NGTCP2_CONN_FLAG_HANDSHAKE_CONFIRMED) {
    return 0;
  }

  conn->flags |= NGTCP2_CONN_FLAG_HANDSHAKE_CONFIRMED |
                 NGTCP2_CONN_FLAG_SERVER_ADDR_VERIFIED;

  conn->pktns.rtb.persistent_congestion_start_ts = ts;

  conn_discard_handshake_state(conn, ts);

  assert(conn->remote.transport_params);

  if (conn->remote.transport_params->preferred_addr_present) {
    rv = conn_select_preferred_addr(conn);
    if (rv != 0) {
      return rv;
    }
  }

  rv = conn_call_handshake_confirmed(conn);
  if (rv != 0) {
    return rv;
  }

  /* Re-arm loss detection timer after handshake has been
     confirmed. */
  ngtcp2_conn_set_loss_detection_timer(conn, ts);

  return 0;
}

/*
 * conn_recv_datagram processes the incoming DATAGRAM frame |fr|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User-defined callback function failed.
 */
static int conn_recv_datagram(ngtcp2_conn *conn, ngtcp2_datagram *fr) {
  assert(conn->local.transport_params.max_datagram_frame_size);

  return conn_call_recv_datagram(conn, fr);
}

/*
 * conn_key_phase_changed returns nonzero if |hd| indicates that the
 * key phase has unexpected value.
 */
static int conn_key_phase_changed(ngtcp2_conn *conn, const ngtcp2_pkt_hd *hd) {
  ngtcp2_pktns *pktns = &conn->pktns;

  return !(pktns->crypto.rx.ckm->flags & NGTCP2_CRYPTO_KM_FLAG_KEY_PHASE_ONE) ^
         !(hd->flags & NGTCP2_PKT_FLAG_KEY_PHASE);
}

static int conn_initiate_key_update(ngtcp2_conn *conn, ngtcp2_tstamp ts);

/*
 * conn_prepare_key_update installs new updated keys.
 */
static int conn_prepare_key_update(ngtcp2_conn *conn, ngtcp2_tstamp ts) {
  int rv;
  ngtcp2_tstamp confirmed_ts = conn->crypto.key_update.confirmed_ts;
  ngtcp2_duration pto = conn_compute_pto(conn, &conn->pktns);
  ngtcp2_pktns *pktns = &conn->pktns;
  ngtcp2_crypto_km *rx_ckm = pktns->crypto.rx.ckm;
  ngtcp2_crypto_km *tx_ckm = pktns->crypto.tx.ckm;
  ngtcp2_crypto_km *new_rx_ckm, *new_tx_ckm;
  ngtcp2_crypto_aead_ctx rx_aead_ctx = {0}, tx_aead_ctx = {0};
  size_t secretlen, ivlen;

  if ((conn->flags & NGTCP2_CONN_FLAG_HANDSHAKE_CONFIRMED) &&
      tx_ckm->use_count >= pktns->crypto.ctx.max_encryption &&
      conn_initiate_key_update(conn, ts) != 0) {
    return NGTCP2_ERR_AEAD_LIMIT_REACHED;
  }

  if ((conn->flags & NGTCP2_CONN_FLAG_KEY_UPDATE_NOT_CONFIRMED) ||
      ngtcp2_tstamp_not_elapsed(confirmed_ts, pto, ts)) {
    return 0;
  }

  if (conn->crypto.key_update.new_rx_ckm ||
      conn->crypto.key_update.new_tx_ckm) {
    assert(conn->crypto.key_update.new_rx_ckm);
    assert(conn->crypto.key_update.new_tx_ckm);
    return 0;
  }

  secretlen = rx_ckm->secret.len;
  ivlen = rx_ckm->iv.len;

  rv = ngtcp2_crypto_km_nocopy_new(&conn->crypto.key_update.new_rx_ckm,
                                   secretlen, ivlen, conn->mem);
  if (rv != 0) {
    return rv;
  }

  rv = ngtcp2_crypto_km_nocopy_new(&conn->crypto.key_update.new_tx_ckm,
                                   secretlen, ivlen, conn->mem);
  if (rv != 0) {
    return rv;
  }

  new_rx_ckm = conn->crypto.key_update.new_rx_ckm;
  new_tx_ckm = conn->crypto.key_update.new_tx_ckm;

  rv = conn_call_update_key(
      conn, new_rx_ckm->secret.base, new_tx_ckm->secret.base, &rx_aead_ctx,
      new_rx_ckm->iv.base, &tx_aead_ctx, new_tx_ckm->iv.base,
      rx_ckm->secret.base, tx_ckm->secret.base, secretlen);
  if (rv != 0) {
    return rv;
  }

  new_rx_ckm->aead_ctx = rx_aead_ctx;
  new_tx_ckm->aead_ctx = tx_aead_ctx;

  if (!(rx_ckm->flags & NGTCP2_CRYPTO_KM_FLAG_KEY_PHASE_ONE)) {
    new_rx_ckm->flags |= NGTCP2_CRYPTO_KM_FLAG_KEY_PHASE_ONE;
    new_tx_ckm->flags |= NGTCP2_CRYPTO_KM_FLAG_KEY_PHASE_ONE;
  }

  if (conn->crypto.key_update.old_rx_ckm) {
    conn_call_delete_crypto_aead_ctx(
        conn, &conn->crypto.key_update.old_rx_ckm->aead_ctx);
    ngtcp2_crypto_km_del(conn->crypto.key_update.old_rx_ckm, conn->mem);
    conn->crypto.key_update.old_rx_ckm = NULL;
  }

  return 0;
}

/*
 * conn_rotate_keys rotates keys.  The current key moves to old key,
 * and new key moves to the current key.  If the local endpoint
 * initiated this key update, pass nonzero as |initiator|.
 */
static void conn_rotate_keys(ngtcp2_conn *conn, int64_t pkt_num,
                             int initiator) {
  ngtcp2_pktns *pktns = &conn->pktns;

  assert(conn->crypto.key_update.new_rx_ckm);
  assert(conn->crypto.key_update.new_tx_ckm);
  assert(!conn->crypto.key_update.old_rx_ckm);
  assert(!(conn->flags & NGTCP2_CONN_FLAG_PPE_PENDING));

  conn->crypto.key_update.old_rx_ckm = pktns->crypto.rx.ckm;

  pktns->crypto.rx.ckm = conn->crypto.key_update.new_rx_ckm;
  conn->crypto.key_update.new_rx_ckm = NULL;
  pktns->crypto.rx.ckm->pkt_num = pkt_num;

  assert(pktns->crypto.tx.ckm);

  conn_call_delete_crypto_aead_ctx(conn, &pktns->crypto.tx.ckm->aead_ctx);
  ngtcp2_crypto_km_del(pktns->crypto.tx.ckm, conn->mem);

  pktns->crypto.tx.ckm = conn->crypto.key_update.new_tx_ckm;
  conn->crypto.key_update.new_tx_ckm = NULL;
  pktns->crypto.tx.ckm->pkt_num = pktns->tx.last_pkt_num + 1;

  conn->flags |= NGTCP2_CONN_FLAG_KEY_UPDATE_NOT_CONFIRMED;
  if (initiator) {
    conn->flags |= NGTCP2_CONN_FLAG_KEY_UPDATE_INITIATOR;
  }
}

/*
 * conn_path_validation_in_progress returns nonzero if path validation
 * against |path| is underway.
 */
static int conn_path_validation_in_progress(ngtcp2_conn *conn,
                                            const ngtcp2_path *path) {
  ngtcp2_pv *pv = conn->pv;

  return pv && ngtcp2_path_eq(&pv->dcid.ps.path, path);
}

/*
 * conn_recv_non_probing_pkt_on_new_path is called when non-probing
 * packet is received via new path.  It starts path validation against
 * the new path.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_CONN_ID_BLOCKED
 *     No DCID is available
 * NGTCP2_ERR_NOMEM
 *     Out of memory
 */
static int conn_recv_non_probing_pkt_on_new_path(ngtcp2_conn *conn,
                                                 const ngtcp2_path *path,
                                                 size_t dgramlen,
                                                 int new_cid_used,
                                                 ngtcp2_tstamp ts) {

  ngtcp2_dcid dcid, *bound_dcid, *last;
  ngtcp2_pv *pv;
  int rv;
  ngtcp2_duration pto;
  int require_new_cid;
  int local_addr_eq;
  uint32_t remote_addr_cmp;
  size_t len, i;

  assert(conn->server);

  if (conn->pv && (conn->pv->flags & NGTCP2_PV_FLAG_FALLBACK_ON_FAILURE) &&
      ngtcp2_path_eq(&conn->pv->fallback_dcid.ps.path, path)) {
    /* If new path equals fallback path, that means connection
       migrated back to the original path.  Fallback path is
       considered to be validated. */
    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PTV,
                    "path is migrated back to the original path");
    ngtcp2_dcid_copy(&conn->dcid.current, &conn->pv->fallback_dcid);
    conn_reset_congestion_state(conn, ts);
    conn->dcid.current.bytes_recv += dgramlen;
    conn_reset_ecn_validation_state(conn);

    rv = conn_abort_pv(conn, ts);
    if (rv != 0) {
      return rv;
    }

    /* Run PMTUD just in case if it is prematurely aborted */
    assert(!conn->pmtud);

    return conn_start_pmtud(conn);
  }

  remote_addr_cmp =
      ngtcp2_addr_compare(&conn->dcid.current.ps.path.remote, &path->remote);
  local_addr_eq =
      ngtcp2_addr_eq(&conn->dcid.current.ps.path.local, &path->local);

  /*
   * When to change DCID?  RFC 9002 section 9.5 says:
   *
   * An endpoint MUST NOT reuse a connection ID when sending from more
   * than one local address -- for example, when initiating connection
   * migration as described in Section 9.2 or when probing a new
   * network path as described in Section 9.1.
   *
   * Similarly, an endpoint MUST NOT reuse a connection ID when
   * sending to more than one destination address.  Due to network
   * changes outside the control of its peer, an endpoint might
   * receive packets from a new source address with the same
   * Destination Connection ID field value, in which case it MAY
   * continue to use the current connection ID with the new remote
   * address while still sending from the same local address.
   */
  require_new_cid = conn->dcid.current.cid.datalen &&
                    ((new_cid_used && remote_addr_cmp) || !local_addr_eq);

  ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_CON,
                  "non-probing packet was received from new remote address");

  len = ngtcp2_ringbuf_len(&conn->dcid.bound.rb);

  for (i = 0; i < len; ++i) {
    bound_dcid = ngtcp2_ringbuf_get(&conn->dcid.bound.rb, i);
    if (ngtcp2_path_eq(&bound_dcid->ps.path, path)) {
      ngtcp2_log_info(
          &conn->log, NGTCP2_LOG_EVENT_CON,
          "Found DCID which has already been bound to the new path");

      ngtcp2_dcid_copy(&dcid, bound_dcid);
      if (i == 0) {
        ngtcp2_ringbuf_pop_front(&conn->dcid.bound.rb);
      } else if (i == ngtcp2_ringbuf_len(&conn->dcid.bound.rb) - 1) {
        ngtcp2_ringbuf_pop_back(&conn->dcid.bound.rb);
      } else {
        last = ngtcp2_ringbuf_get(&conn->dcid.bound.rb, len - 1);
        ngtcp2_dcid_copy(bound_dcid, last);
        ngtcp2_ringbuf_pop_back(&conn->dcid.bound.rb);
      }
      require_new_cid = 0;

      if (dcid.cid.datalen) {
        rv = conn_call_activate_dcid(conn, &dcid);
        if (rv != 0) {
          return rv;
        }
      }
      break;
    }
  }

  if (i == len) {
    if (require_new_cid) {
      if (ngtcp2_ringbuf_len(&conn->dcid.unused.rb) == 0) {
        return NGTCP2_ERR_CONN_ID_BLOCKED;
      }
      ngtcp2_dcid_copy(&dcid, ngtcp2_ringbuf_get(&conn->dcid.unused.rb, 0));
      ngtcp2_ringbuf_pop_front(&conn->dcid.unused.rb);

      rv = conn_call_activate_dcid(conn, &dcid);
      if (rv != 0) {
        return rv;
      }
    } else {
      /* Use the current DCID if a remote endpoint does not change
         DCID. */
      ngtcp2_dcid_copy(&dcid, &conn->dcid.current);
      dcid.bytes_sent = 0;
      dcid.bytes_recv = 0;
      dcid.flags &= (uint8_t)~NGTCP2_DCID_FLAG_PATH_VALIDATED;
    }

    ngtcp2_dcid_set_path(&dcid, path);
  }

  dcid.bytes_recv += dgramlen;

  pto = conn_compute_pto(conn, &conn->pktns);

  rv = ngtcp2_pv_new(&pv, &dcid, conn_compute_pv_timeout_pto(conn, pto),
                     NGTCP2_PV_FLAG_FALLBACK_ON_FAILURE, &conn->log, conn->mem);
  if (rv != 0) {
    return rv;
  }

  if (conn->pv && (conn->pv->flags & NGTCP2_PV_FLAG_FALLBACK_ON_FAILURE)) {
    ngtcp2_dcid_copy(&pv->fallback_dcid, &conn->pv->fallback_dcid);
    pv->fallback_pto = conn->pv->fallback_pto;
    /* Unset the flag bit so that conn_stop_pv does not retire
       DCID. */
    conn->pv->flags &= (uint8_t)~NGTCP2_PV_FLAG_FALLBACK_ON_FAILURE;
  } else {
    ngtcp2_dcid_copy(&pv->fallback_dcid, &conn->dcid.current);
    pv->fallback_pto = pto;
  }

  if (!local_addr_eq || (remote_addr_cmp & (NGTCP2_ADDR_COMPARE_FLAG_ADDR |
                                            NGTCP2_ADDR_COMPARE_FLAG_FAMILY))) {
    conn_reset_congestion_state(conn, ts);
  }

  ngtcp2_dcid_copy(&conn->dcid.current, &dcid);

  conn_reset_ecn_validation_state(conn);

  ngtcp2_conn_stop_pmtud(conn);

  if (conn->pv) {
    ngtcp2_log_info(
        &conn->log, NGTCP2_LOG_EVENT_PTV,
        "path migration is aborted because new migration has started");
    rv = conn_abort_pv(conn, ts);
    if (rv != 0) {
      return rv;
    }
  }

  conn->pv = pv;

  return 0;
}

/*
 * conn_recv_pkt_from_new_path is called when a 1RTT packet is
 * received from new path (not current path).  This packet would be a
 * packet which only contains probing frame, or reordered packet, or a
 * path is being validated.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_CONN_ID_BLOCKED
 *     No unused DCID is available
 * NGTCP2_ERR_NOMEM
 *     Out of memory
 */
static int conn_recv_pkt_from_new_path(ngtcp2_conn *conn,
                                       const ngtcp2_path *path, size_t dgramlen,
                                       int path_challenge_recved,
                                       ngtcp2_tstamp ts) {
  ngtcp2_pv *pv = conn->pv;
  ngtcp2_dcid *bound_dcid;
  int rv;

  if (pv) {
    if (ngtcp2_path_eq(&pv->dcid.ps.path, path)) {
      pv->dcid.bytes_recv += dgramlen;
      return 0;
    }

    if ((pv->flags & NGTCP2_PV_FLAG_FALLBACK_ON_FAILURE) &&
        ngtcp2_path_eq(&pv->fallback_dcid.ps.path, path)) {
      pv->fallback_dcid.bytes_recv += dgramlen;
      return 0;
    }
  }

  if (!path_challenge_recved) {
    return 0;
  }

  rv = conn_bind_dcid(conn, &bound_dcid, path, ts);
  if (rv != 0) {
    return rv;
  }

  ngtcp2_dcid_set_path(bound_dcid, path);
  bound_dcid->bytes_recv += dgramlen;

  return 0;
}

/*
 * conn_recv_delayed_handshake_pkt processes the received Handshake
 * packet which is received after handshake completed.  This function
 * does the minimal job, and its purpose is send acknowledgement of
 * this packet to the peer.  We assume that hd->type ==
 * NGTCP2_PKT_HANDSHAKE.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_FRAME_ENCODING
 *     Frame is badly formatted; or frame type is unknown.
 * NGTCP2_ERR_NOMEM
 *     Out of memory
 * NGTCP2_ERR_DISCARD_PKT
 *     Packet was discarded.
 * NGTCP2_ERR_ACK_FRAME
 *     ACK frame is malformed.
 * NGTCP2_ERR_PROTO
 *     Frame that is not allowed in Handshake packet is received.
 */
static int
conn_recv_delayed_handshake_pkt(ngtcp2_conn *conn, const ngtcp2_pkt_info *pi,
                                const ngtcp2_pkt_hd *hd, size_t pktlen,
                                const uint8_t *payload, size_t payloadlen,
                                ngtcp2_tstamp pkt_ts, ngtcp2_tstamp ts) {
  ngtcp2_ssize nread;
  ngtcp2_max_frame mfr;
  ngtcp2_frame *fr = &mfr.fr;
  int rv;
  int require_ack = 0;
  ngtcp2_pktns *pktns;

  assert(hd->type == NGTCP2_PKT_HANDSHAKE);

  pktns = conn->hs_pktns;

  if (payloadlen == 0) {
    /* QUIC packet must contain at least one frame */
    return NGTCP2_ERR_PROTO;
  }

  ngtcp2_qlog_pkt_received_start(&conn->qlog);

  for (; payloadlen;) {
    nread = ngtcp2_pkt_decode_frame(fr, payload, payloadlen);
    if (nread < 0) {
      return (int)nread;
    }

    payload += nread;
    payloadlen -= (size_t)nread;

    switch (fr->type) {
    case NGTCP2_FRAME_ACK:
    case NGTCP2_FRAME_ACK_ECN:
      fr->ack.ack_delay = 0;
      fr->ack.ack_delay_unscaled = 0;
      break;
    }

    ngtcp2_log_rx_fr(&conn->log, hd, fr);

    switch (fr->type) {
    case NGTCP2_FRAME_ACK:
    case NGTCP2_FRAME_ACK_ECN:
      if (!conn->server) {
        conn->flags |= NGTCP2_CONN_FLAG_SERVER_ADDR_VERIFIED;
      }
      rv = conn_recv_ack(conn, pktns, &fr->ack, pkt_ts, ts);
      if (rv != 0) {
        return rv;
      }
      break;
    case NGTCP2_FRAME_PADDING:
      break;
    case NGTCP2_FRAME_CONNECTION_CLOSE:
      rv = conn_recv_connection_close(conn, &fr->connection_close);
      if (rv != 0) {
        return rv;
      }
      break;
    case NGTCP2_FRAME_CRYPTO:
    case NGTCP2_FRAME_PING:
      require_ack = 1;
      break;
    default:
      return NGTCP2_ERR_PROTO;
    }

    ngtcp2_qlog_write_frame(&conn->qlog, fr);
  }

  ngtcp2_qlog_pkt_received_end(&conn->qlog, hd, pktlen);

  rv = pktns_commit_recv_pkt_num(pktns, hd->pkt_num, require_ack, pkt_ts);
  if (rv != 0) {
    return rv;
  }

  pktns_increase_ecn_counts(pktns, pi);

  /* Initial and Handshake are always acknowledged without delay.  No
     need to call ngtcp2_acktr_immediate_ack(). */
  rv = ngtcp2_conn_sched_ack(conn, &pktns->acktr, hd->pkt_num, require_ack,
                             pkt_ts);
  if (rv != 0) {
    return rv;
  }

  conn_restart_timer_on_read(conn, ts);

  ngtcp2_qlog_metrics_updated(&conn->qlog, &conn->cstat);

  return 0;
}

/*
 * conn_allow_path_change_under_disable_active_migration returns
 * nonzero if a packet from |path| is acceptable under
 * disable_active_migration is on.
 */
static int
conn_allow_path_change_under_disable_active_migration(ngtcp2_conn *conn,
                                                      const ngtcp2_path *path) {
  uint32_t remote_addr_cmp;
  const ngtcp2_preferred_addr *paddr;
  ngtcp2_addr addr;

  assert(conn->server);
  assert(conn->local.transport_params.disable_active_migration);

  /* If local address does not change, it must be passive migration
     (NAT rebinding). */
  if (ngtcp2_addr_eq(&conn->dcid.current.ps.path.local, &path->local)) {
    remote_addr_cmp =
        ngtcp2_addr_compare(&conn->dcid.current.ps.path.remote, &path->remote);

    return (remote_addr_cmp | NGTCP2_ADDR_COMPARE_FLAG_PORT) ==
           NGTCP2_ADDR_COMPARE_FLAG_PORT;
  }

  /* If local address changes, it must be one of the preferred
     addresses. */

  if (!conn->local.transport_params.preferred_addr_present) {
    return 0;
  }

  paddr = &conn->local.transport_params.preferred_addr;

  if (paddr->ipv4_present) {
    ngtcp2_addr_init(&addr, (const ngtcp2_sockaddr *)&paddr->ipv4,
                     sizeof(paddr->ipv4));

    if (ngtcp2_addr_eq(&addr, &path->local)) {
      return 1;
    }
  }

  if (paddr->ipv6_present) {
    ngtcp2_addr_init(&addr, (const ngtcp2_sockaddr *)&paddr->ipv6,
                     sizeof(paddr->ipv6));

    if (ngtcp2_addr_eq(&addr, &path->local)) {
      return 1;
    }
  }

  return 0;
}

/*
 * conn_recv_pkt processes a packet contained in the buffer pointed by
 * |pkt| of length |pktlen|.  |pkt| may contain multiple QUIC packets.
 * This function only processes the first packet.  |pkt_ts| is the
 * timestamp when packet is received.  |ts| should be the current
 * time.  Usually they are the same, but for buffered packets,
 * |pkt_ts| would be earlier than |ts|.
 *
 * This function returns the number of bytes processed if it succeeds,
 * or one of the following negative error codes:
 *
 * NGTCP2_ERR_DISCARD_PKT
 *     Packet was discarded because plain text header was malformed;
 *     or its payload could not be decrypted.
 * NGTCP2_ERR_PROTO
 *     Packet is badly formatted; or 0RTT packet contains other than
 *     PADDING or STREAM frames; or other QUIC protocol violation is
 *     found.
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User-defined callback function failed.
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 * NGTCP2_ERR_FRAME_ENCODING
 *     Frame is badly formatted; or frame type is unknown.
 * NGTCP2_ERR_ACK_FRAME
 *     ACK frame is malformed.
 * NGTCP2_ERR_STREAM_STATE
 *     Frame is received to the local stream which is not initiated.
 * NGTCP2_ERR_STREAM_LIMIT
 *     Frame has remote stream ID which is strictly greater than the
 *     allowed limit.
 * NGTCP2_ERR_FLOW_CONTROL
 *     Flow control limit is violated.
 * NGTCP2_ERR_FINAL_SIZE
 *     Frame has strictly larger end offset than it is permitted.
 */
static ngtcp2_ssize conn_recv_pkt(ngtcp2_conn *conn, const ngtcp2_path *path,
                                  const ngtcp2_pkt_info *pi, const uint8_t *pkt,
                                  size_t pktlen, size_t dgramlen,
                                  ngtcp2_tstamp pkt_ts, ngtcp2_tstamp ts) {
  ngtcp2_pkt_hd hd;
  int rv = 0;
  size_t hdpktlen;
  const uint8_t *payload;
  size_t payloadlen;
  ngtcp2_ssize nread, nwrite;
  ngtcp2_max_frame mfr;
  ngtcp2_frame *fr = &mfr.fr;
  int require_ack = 0;
  ngtcp2_crypto_aead *aead;
  ngtcp2_crypto_cipher *hp;
  ngtcp2_crypto_km *ckm;
  ngtcp2_crypto_cipher_ctx *hp_ctx;
  ngtcp2_hp_mask hp_mask;
  ngtcp2_decrypt decrypt;
  ngtcp2_pktns *pktns;
  int non_probing_pkt = 0;
  int key_phase_bit_changed = 0;
  int force_decrypt_failure = 0;
  int recv_ncid = 0;
  int new_cid_used = 0;
  int path_challenge_recved = 0;

  if (conn->server && conn->local.transport_params.disable_active_migration &&
      !ngtcp2_path_eq(&conn->dcid.current.ps.path, path) &&
      !conn_allow_path_change_under_disable_active_migration(conn, path)) {
    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                    "packet is discarded because active migration is disabled");

    return NGTCP2_ERR_DISCARD_PKT;
  }

  if (pkt[0] & NGTCP2_HEADER_FORM_BIT) {
    nread = ngtcp2_pkt_decode_hd_long(&hd, pkt, pktlen);
    if (nread < 0) {
      ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                      "could not decode long header");
      return NGTCP2_ERR_DISCARD_PKT;
    }

    if (pktlen < (size_t)nread + hd.len) {
      return NGTCP2_ERR_DISCARD_PKT;
    }

    assert(conn->negotiated_version);

    if (hd.version != conn->client_chosen_version &&
        hd.version != conn->negotiated_version) {
      return NGTCP2_ERR_DISCARD_PKT;
    }

    if (conn_verify_fixed_bit(conn, &hd) != 0) {
      return NGTCP2_ERR_DISCARD_PKT;
    }

    pktlen = (size_t)nread + hd.len;

    /* Quoted from spec: if subsequent packets of those types include
       a different Source Connection ID, they MUST be discarded. */
    if (!ngtcp2_cid_eq(&conn->dcid.current.cid, &hd.scid)) {
      ngtcp2_log_rx_pkt_hd(&conn->log, &hd);
      ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                      "packet was ignored because of mismatched SCID");
      return NGTCP2_ERR_DISCARD_PKT;
    }

    switch (hd.type) {
    case NGTCP2_PKT_INITIAL:
      ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                      "delayed Initial packet was discarded");
      return (ngtcp2_ssize)pktlen;
    case NGTCP2_PKT_HANDSHAKE:
      if (hd.version != conn->negotiated_version) {
        return NGTCP2_ERR_DISCARD_PKT;
      }

      if (!conn->hs_pktns) {
        ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                        "delayed Handshake packet was discarded");
        return (ngtcp2_ssize)pktlen;
      }

      pktns = conn->hs_pktns;
      aead = &pktns->crypto.ctx.aead;
      hp = &pktns->crypto.ctx.hp;
      ckm = pktns->crypto.rx.ckm;
      hp_ctx = &pktns->crypto.rx.hp_ctx;
      hp_mask = conn->callbacks.hp_mask;
      decrypt = conn->callbacks.decrypt;
      break;
    case NGTCP2_PKT_0RTT:
      if (!conn->server || hd.version != conn->client_chosen_version) {
        return NGTCP2_ERR_DISCARD_PKT;
      }

      if (!conn->early.ckm) {
        return (ngtcp2_ssize)pktlen;
      }

      pktns = &conn->pktns;
      aead = &conn->early.ctx.aead;
      hp = &conn->early.ctx.hp;
      ckm = conn->early.ckm;
      hp_ctx = &conn->early.hp_ctx;
      hp_mask = conn->callbacks.hp_mask;
      decrypt = conn->callbacks.decrypt;
      break;
    default:
      ngtcp2_log_rx_pkt_hd(&conn->log, &hd);
      ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                      "packet type 0x%02x was ignored", hd.type);
      return (ngtcp2_ssize)pktlen;
    }
  } else {
    nread = ngtcp2_pkt_decode_hd_short(&hd, pkt, pktlen, conn->oscid.datalen);
    if (nread < 0) {
      ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                      "could not decode short header");
      return NGTCP2_ERR_DISCARD_PKT;
    }

    if (conn_verify_fixed_bit(conn, &hd) != 0) {
      return NGTCP2_ERR_DISCARD_PKT;
    }

    pktns = &conn->pktns;
    aead = &pktns->crypto.ctx.aead;
    hp = &pktns->crypto.ctx.hp;
    ckm = pktns->crypto.rx.ckm;
    hp_ctx = &pktns->crypto.rx.hp_ctx;
    hp_mask = conn->callbacks.hp_mask;
    decrypt = conn->callbacks.decrypt;
  }

  rv = conn_ensure_decrypt_hp_buffer(conn, (size_t)nread + 4);
  if (rv != 0) {
    return rv;
  }

  nwrite = decrypt_hp(&hd, conn->crypto.decrypt_hp_buf.base, hp, pkt, pktlen,
                      (size_t)nread, hp_ctx, hp_mask);
  if (nwrite < 0) {
    if (ngtcp2_err_is_fatal((int)nwrite)) {
      return nwrite;
    }
    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                    "could not decrypt packet number");
    return NGTCP2_ERR_DISCARD_PKT;
  }

  hdpktlen = (size_t)nwrite;
  payload = pkt + hdpktlen;
  payloadlen = pktlen - hdpktlen;

  hd.pkt_num = ngtcp2_pkt_adjust_pkt_num(pktns->rx.max_pkt_num, hd.pkt_num,
                                         hd.pkt_numlen);
  if (hd.pkt_num > NGTCP2_MAX_PKT_NUM) {
    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                    "pkn=%" PRId64 " is greater than maximum pkn", hd.pkt_num);
    return NGTCP2_ERR_DISCARD_PKT;
  }

  ngtcp2_log_rx_pkt_hd(&conn->log, &hd);

  if (hd.type == NGTCP2_PKT_1RTT) {
    key_phase_bit_changed = conn_key_phase_changed(conn, &hd);
  }

  rv = conn_ensure_decrypt_buffer(conn, payloadlen);
  if (rv != 0) {
    return rv;
  }

  if (key_phase_bit_changed) {
    assert(hd.type == NGTCP2_PKT_1RTT);

    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT, "unexpected KEY_PHASE");

    if (ckm->pkt_num > hd.pkt_num) {
      if (conn->crypto.key_update.old_rx_ckm) {
        ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                        "decrypting with old key");
        ckm = conn->crypto.key_update.old_rx_ckm;
      } else {
        force_decrypt_failure = 1;
      }
    } else if (pktns->rx.max_pkt_num < hd.pkt_num) {
      assert(ckm->pkt_num < hd.pkt_num);
      if (!conn->crypto.key_update.new_rx_ckm) {
        ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                        "new key is not available");
        force_decrypt_failure = 1;
      } else {
        ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                        "decrypting with new key");
        ckm = conn->crypto.key_update.new_rx_ckm;
      }
    } else {
      force_decrypt_failure = 1;
    }
  }

  nwrite = decrypt_pkt(conn->crypto.decrypt_buf.base, aead, payload, payloadlen,
                       conn->crypto.decrypt_hp_buf.base, hdpktlen, hd.pkt_num,
                       ckm, decrypt);

  if (force_decrypt_failure) {
    nwrite = NGTCP2_ERR_DECRYPT;
  }

  if (nwrite < 0) {
    if (ngtcp2_err_is_fatal((int)nwrite)) {
      return nwrite;
    }

    assert(NGTCP2_ERR_DECRYPT == nwrite);

    if (hd.type == NGTCP2_PKT_1RTT &&
        ++conn->crypto.decryption_failure_count >=
            pktns->crypto.ctx.max_decryption_failure) {
      return NGTCP2_ERR_AEAD_LIMIT_REACHED;
    }

    if (hd.flags & NGTCP2_PKT_FLAG_LONG_FORM) {
      ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                      "could not decrypt packet payload");
      return NGTCP2_ERR_DISCARD_PKT;
    }

    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                    "could not decrypt packet payload");
    return NGTCP2_ERR_DISCARD_PKT;
  }

  rv = ngtcp2_pkt_verify_reserved_bits(conn->crypto.decrypt_hp_buf.base[0]);
  if (rv != 0) {
    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                    "packet has incorrect reserved bits");

    return NGTCP2_ERR_PROTO;
  }

  if (pktns_pkt_num_is_duplicate(pktns, hd.pkt_num)) {
    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                    "packet was discarded because of duplicated packet number");
    return NGTCP2_ERR_DISCARD_PKT;
  }

  payload = conn->crypto.decrypt_buf.base;
  payloadlen = (size_t)nwrite;

  if (payloadlen == 0) {
    /* QUIC packet must contain at least one frame */
    return NGTCP2_ERR_PROTO;
  }

  if (hd.flags & NGTCP2_PKT_FLAG_LONG_FORM) {
    switch (hd.type) {
    case NGTCP2_PKT_HANDSHAKE:
      rv = conn_verify_dcid(conn, NULL, &hd);
      if (rv != 0) {
        if (ngtcp2_err_is_fatal(rv)) {
          return rv;
        }
        ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                        "packet was ignored because of mismatched DCID");
        return NGTCP2_ERR_DISCARD_PKT;
      }

      rv = conn_recv_delayed_handshake_pkt(conn, pi, &hd, pktlen, payload,
                                           payloadlen, pkt_ts, ts);
      if (rv < 0) {
        return (ngtcp2_ssize)rv;
      }

      return (ngtcp2_ssize)pktlen;
    case NGTCP2_PKT_0RTT:
      if (!ngtcp2_cid_eq(&conn->rcid, &hd.dcid)) {
        rv = conn_verify_dcid(conn, NULL, &hd);
        if (rv != 0) {
          if (ngtcp2_err_is_fatal(rv)) {
            return rv;
          }
          ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                          "packet was ignored because of mismatched DCID");
          return NGTCP2_ERR_DISCARD_PKT;
        }
      }
      break;
    default:
      /* Unreachable */
      ngtcp2_unreachable();
    }
  } else {
    rv = conn_verify_dcid(conn, &new_cid_used, &hd);
    if (rv != 0) {
      if (ngtcp2_err_is_fatal(rv)) {
        return rv;
      }
      ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                      "packet was ignored because of mismatched DCID");
      return NGTCP2_ERR_DISCARD_PKT;
    }
  }

  ngtcp2_qlog_pkt_received_start(&conn->qlog);

  for (; payloadlen;) {
    nread = ngtcp2_pkt_decode_frame(fr, payload, payloadlen);
    if (nread < 0) {
      return nread;
    }

    payload += nread;
    payloadlen -= (size_t)nread;

    switch (fr->type) {
    case NGTCP2_FRAME_ACK:
    case NGTCP2_FRAME_ACK_ECN:
      if ((hd.flags & NGTCP2_PKT_FLAG_LONG_FORM) &&
          hd.type == NGTCP2_PKT_0RTT) {
        return NGTCP2_ERR_PROTO;
      }
      assert(conn->remote.transport_params);
      assign_recved_ack_delay_unscaled(
          &fr->ack, conn->remote.transport_params->ack_delay_exponent);
      break;
    }

    ngtcp2_log_rx_fr(&conn->log, &hd, fr);

    if (hd.type == NGTCP2_PKT_0RTT) {
      switch (fr->type) {
      case NGTCP2_FRAME_PADDING:
      case NGTCP2_FRAME_PING:
      case NGTCP2_FRAME_RESET_STREAM:
      case NGTCP2_FRAME_STOP_SENDING:
      case NGTCP2_FRAME_STREAM:
      case NGTCP2_FRAME_MAX_DATA:
      case NGTCP2_FRAME_MAX_STREAM_DATA:
      case NGTCP2_FRAME_MAX_STREAMS_BIDI:
      case NGTCP2_FRAME_MAX_STREAMS_UNI:
      case NGTCP2_FRAME_DATA_BLOCKED:
      case NGTCP2_FRAME_STREAM_DATA_BLOCKED:
      case NGTCP2_FRAME_STREAMS_BLOCKED_BIDI:
      case NGTCP2_FRAME_STREAMS_BLOCKED_UNI:
      case NGTCP2_FRAME_NEW_CONNECTION_ID:
      case NGTCP2_FRAME_PATH_CHALLENGE:
      case NGTCP2_FRAME_CONNECTION_CLOSE:
      case NGTCP2_FRAME_CONNECTION_CLOSE_APP:
      case NGTCP2_FRAME_DATAGRAM:
      case NGTCP2_FRAME_DATAGRAM_LEN:
        break;
      default:
        return NGTCP2_ERR_PROTO;
      }
    }

    switch (fr->type) {
    case NGTCP2_FRAME_ACK:
    case NGTCP2_FRAME_ACK_ECN:
    case NGTCP2_FRAME_PADDING:
    case NGTCP2_FRAME_CONNECTION_CLOSE:
    case NGTCP2_FRAME_CONNECTION_CLOSE_APP:
      break;
    default:
      require_ack = 1;
    }

    switch (fr->type) {
    case NGTCP2_FRAME_ACK:
    case NGTCP2_FRAME_ACK_ECN:
      if (!conn->server) {
        conn->flags |= NGTCP2_CONN_FLAG_SERVER_ADDR_VERIFIED;
      }
      rv = conn_recv_ack(conn, pktns, &fr->ack, pkt_ts, ts);
      if (rv != 0) {
        return rv;
      }
      non_probing_pkt = 1;
      break;
    case NGTCP2_FRAME_STREAM:
      rv = conn_recv_stream(conn, &fr->stream);
      if (rv != 0) {
        return rv;
      }
      non_probing_pkt = 1;
      break;
    case NGTCP2_FRAME_CRYPTO:
      rv = conn_recv_crypto(conn, NGTCP2_ENCRYPTION_LEVEL_1RTT,
                            &pktns->crypto.strm, &fr->stream);
      if (rv != 0) {
        return rv;
      }
      non_probing_pkt = 1;
      break;
    case NGTCP2_FRAME_RESET_STREAM:
      rv = conn_recv_reset_stream(conn, &fr->reset_stream);
      if (rv != 0) {
        return rv;
      }
      non_probing_pkt = 1;
      break;
    case NGTCP2_FRAME_STOP_SENDING:
      rv = conn_recv_stop_sending(conn, &fr->stop_sending);
      if (rv != 0) {
        return rv;
      }
      non_probing_pkt = 1;
      break;
    case NGTCP2_FRAME_MAX_STREAM_DATA:
      rv = conn_recv_max_stream_data(conn, &fr->max_stream_data);
      if (rv != 0) {
        return rv;
      }
      non_probing_pkt = 1;
      break;
    case NGTCP2_FRAME_MAX_DATA:
      conn_recv_max_data(conn, &fr->max_data);
      non_probing_pkt = 1;
      break;
    case NGTCP2_FRAME_MAX_STREAMS_BIDI:
    case NGTCP2_FRAME_MAX_STREAMS_UNI:
      rv = conn_recv_max_streams(conn, &fr->max_streams);
      if (rv != 0) {
        return rv;
      }
      non_probing_pkt = 1;
      break;
    case NGTCP2_FRAME_CONNECTION_CLOSE:
    case NGTCP2_FRAME_CONNECTION_CLOSE_APP:
      rv = conn_recv_connection_close(conn, &fr->connection_close);
      if (rv != 0) {
        return rv;
      }
      break;
    case NGTCP2_FRAME_PING:
      non_probing_pkt = 1;
      break;
    case NGTCP2_FRAME_PATH_CHALLENGE:
      conn_recv_path_challenge(conn, path, &fr->path_challenge);
      path_challenge_recved = 1;
      break;
    case NGTCP2_FRAME_PATH_RESPONSE:
      rv = conn_recv_path_response(conn, &fr->path_response, ts);
      if (rv != 0) {
        return rv;
      }
      break;
    case NGTCP2_FRAME_NEW_CONNECTION_ID:
      rv = conn_recv_new_connection_id(conn, &fr->new_connection_id);
      if (rv != 0) {
        return rv;
      }
      recv_ncid = 1;
      break;
    case NGTCP2_FRAME_RETIRE_CONNECTION_ID:
      rv = conn_recv_retire_connection_id(conn, &hd, &fr->retire_connection_id,
                                          ts);
      if (rv != 0) {
        return rv;
      }
      non_probing_pkt = 1;
      break;
    case NGTCP2_FRAME_NEW_TOKEN:
      rv = conn_recv_new_token(conn, &fr->new_token);
      if (rv != 0) {
        return rv;
      }
      non_probing_pkt = 1;
      break;
    case NGTCP2_FRAME_HANDSHAKE_DONE:
      rv = conn_recv_handshake_done(conn, ts);
      if (rv != 0) {
        return rv;
      }
      non_probing_pkt = 1;
      break;
    case NGTCP2_FRAME_STREAMS_BLOCKED_BIDI:
      rv = conn_recv_streams_blocked_bidi(conn, &fr->streams_blocked);
      if (rv != 0) {
        return rv;
      }
      non_probing_pkt = 1;
      break;
    case NGTCP2_FRAME_STREAMS_BLOCKED_UNI:
      rv = conn_recv_streams_blocked_uni(conn, &fr->streams_blocked);
      if (rv != 0) {
        return rv;
      }
      non_probing_pkt = 1;
      break;
    case NGTCP2_FRAME_STREAM_DATA_BLOCKED:
      rv = conn_recv_stream_data_blocked(conn, &fr->stream_data_blocked);
      if (rv != 0) {
        return rv;
      }
      non_probing_pkt = 1;
      break;
    case NGTCP2_FRAME_DATA_BLOCKED:
      rv = conn_recv_data_blocked(conn, &fr->data_blocked);
      if (rv != 0) {
        return rv;
      }
      non_probing_pkt = 1;
      break;
    case NGTCP2_FRAME_DATAGRAM:
    case NGTCP2_FRAME_DATAGRAM_LEN:
      if ((uint64_t)nread >
          conn->local.transport_params.max_datagram_frame_size) {
        return NGTCP2_ERR_PROTO;
      }
      rv = conn_recv_datagram(conn, &fr->datagram);
      if (rv != 0) {
        return rv;
      }
      non_probing_pkt = 1;
      break;
    }

    ngtcp2_qlog_write_frame(&conn->qlog, fr);
  }

  ngtcp2_qlog_pkt_received_end(&conn->qlog, &hd, pktlen);

  if (recv_ncid) {
    rv = conn_post_process_recv_new_connection_id(conn, ts);
    if (rv != 0) {
      return rv;
    }
  }

  if (conn->server && hd.type == NGTCP2_PKT_1RTT &&
      !ngtcp2_path_eq(&conn->dcid.current.ps.path, path)) {
    if (non_probing_pkt && pktns->rx.max_pkt_num < hd.pkt_num &&
        !conn_path_validation_in_progress(conn, path)) {
      rv = conn_recv_non_probing_pkt_on_new_path(conn, path, dgramlen,
                                                 new_cid_used, ts);
      if (rv != 0) {
        if (ngtcp2_err_is_fatal(rv)) {
          return rv;
        }

        /* DCID is not available.  Just continue. */
        assert(NGTCP2_ERR_CONN_ID_BLOCKED == rv);
      }
    } else {
      rv = conn_recv_pkt_from_new_path(conn, path, dgramlen,
                                       path_challenge_recved, ts);
      if (rv != 0) {
        if (ngtcp2_err_is_fatal(rv)) {
          return rv;
        }

        /* DCID is not available.  Just continue. */
        assert(NGTCP2_ERR_CONN_ID_BLOCKED == rv);
      }
    }
  }

  if (hd.type == NGTCP2_PKT_1RTT) {
    if (ckm == conn->crypto.key_update.new_rx_ckm) {
      ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_CON, "rotate keys");
      conn_rotate_keys(conn, hd.pkt_num, /* initiator = */ 0);
    } else if (ckm->pkt_num > hd.pkt_num) {
      ckm->pkt_num = hd.pkt_num;
    }

    if (conn->server && conn->early.ckm &&
        conn->early.discard_started_ts == UINT64_MAX) {
      conn->early.discard_started_ts = ts;
    }

    if (ngtcp2_path_eq(&conn->dcid.current.ps.path, path)) {
      conn_update_keep_alive_last_ts(conn, ts);
    }
  }

  rv = pktns_commit_recv_pkt_num(pktns, hd.pkt_num, require_ack, pkt_ts);
  if (rv != 0) {
    return rv;
  }

  pktns_increase_ecn_counts(pktns, pi);

  if (require_ack &&
      (++pktns->acktr.rx_npkt >= conn->local.settings.ack_thresh ||
       (pi->ecn & NGTCP2_ECN_MASK) == NGTCP2_ECN_CE)) {
    ngtcp2_acktr_immediate_ack(&pktns->acktr);
  }

  rv = ngtcp2_conn_sched_ack(conn, &pktns->acktr, hd.pkt_num, require_ack,
                             pkt_ts);
  if (rv != 0) {
    return rv;
  }

  conn_restart_timer_on_read(conn, ts);

  ngtcp2_qlog_metrics_updated(&conn->qlog, &conn->cstat);

  return conn->state == NGTCP2_CS_DRAINING ? NGTCP2_ERR_DRAINING
                                           : (ngtcp2_ssize)pktlen;
}

/*
 * conn_process_buffered_protected_pkt processes buffered 0RTT or 1RTT
 * packets.
 *
 * This function returns 0 if it succeeds, or the same negative error
 * codes from conn_recv_pkt.
 */
static int conn_process_buffered_protected_pkt(ngtcp2_conn *conn,
                                               ngtcp2_pktns *pktns,
                                               ngtcp2_tstamp ts) {
  ngtcp2_ssize nread;
  ngtcp2_pkt_chain **ppc, *next;
  int rv;

  ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_CON,
                  "processing buffered protected packet");

  for (ppc = &pktns->rx.buffed_pkts; *ppc;) {
    next = (*ppc)->next;
    nread = conn_recv_pkt(conn, &(*ppc)->path.path, &(*ppc)->pi, (*ppc)->pkt,
                          (*ppc)->pktlen, (*ppc)->dgramlen, (*ppc)->ts, ts);
    if (nread < 0 && !ngtcp2_err_is_fatal((int)nread) &&
        nread != NGTCP2_ERR_DRAINING) {
      /* TODO We don't know this is the first QUIC packet in a
         datagram. */
      rv = conn_on_stateless_reset(conn, &(*ppc)->path.path, (*ppc)->pkt,
                                   (*ppc)->pktlen);
      if (rv == 0) {
        ngtcp2_pkt_chain_del(*ppc, conn->mem);
        *ppc = next;
        return NGTCP2_ERR_DRAINING;
      }
    }

    ngtcp2_pkt_chain_del(*ppc, conn->mem);
    *ppc = next;
    if (nread < 0) {
      if (nread == NGTCP2_ERR_DISCARD_PKT) {
        continue;
      }
      return (int)nread;
    }
  }

  return 0;
}

/*
 * conn_process_buffered_handshake_pkt processes buffered Handshake
 * packets.
 *
 * This function returns 0 if it succeeds, or the same negative error
 * codes from conn_recv_handshake_pkt.
 */
static int conn_process_buffered_handshake_pkt(ngtcp2_conn *conn,
                                               ngtcp2_tstamp ts) {
  ngtcp2_pktns *pktns = conn->hs_pktns;
  ngtcp2_ssize nread;
  ngtcp2_pkt_chain **ppc, *next;

  ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_CON,
                  "processing buffered handshake packet");

  for (ppc = &pktns->rx.buffed_pkts; *ppc;) {
    next = (*ppc)->next;
    nread = conn_recv_handshake_pkt(conn, &(*ppc)->path.path, &(*ppc)->pi,
                                    (*ppc)->pkt, (*ppc)->pktlen,
                                    (*ppc)->dgramlen, (*ppc)->ts, ts);
    ngtcp2_pkt_chain_del(*ppc, conn->mem);
    *ppc = next;
    if (nread < 0) {
      if (nread == NGTCP2_ERR_DISCARD_PKT) {
        continue;
      }
      return (int)nread;
    }
  }

  return 0;
}

static void conn_sync_stream_id_limit(ngtcp2_conn *conn) {
  ngtcp2_transport_params *params = conn->remote.transport_params;

  assert(params);

  conn->local.bidi.max_streams = params->initial_max_streams_bidi;
  conn->local.uni.max_streams = params->initial_max_streams_uni;
}

static int strm_set_max_offset(void *data, void *ptr) {
  ngtcp2_conn *conn = ptr;
  ngtcp2_transport_params *params = conn->remote.transport_params;
  ngtcp2_strm *strm = data;
  uint64_t max_offset;
  int rv;

  assert(params);

  if (!conn_local_stream(conn, strm->stream_id)) {
    return 0;
  }

  if (bidi_stream(strm->stream_id)) {
    max_offset = params->initial_max_stream_data_bidi_remote;
  } else {
    max_offset = params->initial_max_stream_data_uni;
  }

  if (strm->tx.max_offset < max_offset) {
    strm->tx.max_offset = max_offset;

    /* Don't call callback if stream is half-closed local */
    if (strm->flags & NGTCP2_STRM_FLAG_SHUT_WR) {
      return 0;
    }

    rv = conn_call_extend_max_stream_data(conn, strm, strm->stream_id,
                                          strm->tx.max_offset);
    if (rv != 0) {
      return rv;
    }
  }

  return 0;
}

static int conn_sync_stream_data_limit(ngtcp2_conn *conn) {
  return ngtcp2_map_each(&conn->strms, strm_set_max_offset, conn);
}

/*
 * conn_handshake_completed is called once cryptographic handshake has
 * completed.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User callback failed.
 */
static int conn_handshake_completed(ngtcp2_conn *conn) {
  int rv;

  conn->flags |= NGTCP2_CONN_FLAG_HANDSHAKE_COMPLETED;

  rv = conn_call_handshake_completed(conn);
  if (rv != 0) {
    return rv;
  }

  if (conn->local.bidi.max_streams > 0) {
    rv = conn_call_extend_max_local_streams_bidi(conn,
                                                 conn->local.bidi.max_streams);
    if (rv != 0) {
      return rv;
    }
  }
  if (conn->local.uni.max_streams > 0) {
    rv = conn_call_extend_max_local_streams_uni(conn,
                                                conn->local.uni.max_streams);
    if (rv != 0) {
      return rv;
    }
  }

  return 0;
}

/*
 * conn_recv_cpkt processes compound packet after handshake.  The
 * buffer pointed by |pkt| might contain multiple packets.  The 1RTT
 * packet must be the last one because it does not have payload length
 * field.
 *
 * This function returns 0 if it succeeds, or the same negative error
 * codes from conn_recv_pkt except for NGTCP2_ERR_DISCARD_PKT.
 */
static int conn_recv_cpkt(ngtcp2_conn *conn, const ngtcp2_path *path,
                          const ngtcp2_pkt_info *pi, const uint8_t *pkt,
                          size_t pktlen, ngtcp2_tstamp ts) {
  ngtcp2_ssize nread;
  int rv;
  const uint8_t *origpkt = pkt;
  size_t dgramlen = pktlen;

  if (ngtcp2_path_eq(&conn->dcid.current.ps.path, path)) {
    conn->dcid.current.bytes_recv += dgramlen;
  }

  while (pktlen) {
    nread = conn_recv_pkt(conn, path, pi, pkt, pktlen, dgramlen, ts, ts);
    if (nread < 0) {
      if (ngtcp2_err_is_fatal((int)nread)) {
        return (int)nread;
      }

      if (nread == NGTCP2_ERR_DRAINING) {
        return NGTCP2_ERR_DRAINING;
      }

      if (origpkt == pkt) {
        rv = conn_on_stateless_reset(conn, path, origpkt, dgramlen);
        if (rv == 0) {
          return NGTCP2_ERR_DRAINING;
        }
      }
      if (nread == NGTCP2_ERR_DISCARD_PKT) {
        return 0;
      }
      return (int)nread;
    }

    assert(pktlen >= (size_t)nread);
    pkt += nread;
    pktlen -= (size_t)nread;

    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_PKT,
                    "read packet %td left %zu", nread, pktlen);
  }

  return 0;
}

/*
 * conn_is_retired_path returns nonzero if |path| is included in
 * retired path list.
 */
static int conn_is_retired_path(ngtcp2_conn *conn, const ngtcp2_path *path) {
  size_t i, len = ngtcp2_ringbuf_len(&conn->dcid.retired.rb);
  ngtcp2_dcid *dcid;

  for (i = 0; i < len; ++i) {
    dcid = ngtcp2_ringbuf_get(&conn->dcid.retired.rb, i);
    if (ngtcp2_path_eq(&dcid->ps.path, path)) {
      return 1;
    }
  }

  return 0;
}

/*
 * conn_enqueue_handshake_done enqueues HANDSHAKE_DONE frame for
 * transmission.
 */
static int conn_enqueue_handshake_done(ngtcp2_conn *conn) {
  ngtcp2_pktns *pktns = &conn->pktns;
  ngtcp2_frame_chain *nfrc;
  int rv;

  assert(conn->server);

  rv = ngtcp2_frame_chain_objalloc_new(&nfrc, &conn->frc_objalloc);
  if (rv != 0) {
    return rv;
  }

  nfrc->fr.type = NGTCP2_FRAME_HANDSHAKE_DONE;
  nfrc->next = pktns->tx.frq;
  pktns->tx.frq = nfrc;

  return 0;
}

/**
 * @function
 *
 * `conn_read_handshake` performs QUIC cryptographic handshake by
 * reading given data.  |pkt| points to the buffer to read and
 * |pktlen| is the length of the buffer.  |path| is the network path.
 *
 * This function returns the number of bytes processed.  Unless the
 * last packet is 1RTT packet and an application decryption key has
 * been installed, it returns |pktlen| if it succeeds.  If it finds
 * 1RTT packet and an application decryption key has been installed,
 * it returns the number of bytes just before 1RTT packet begins.
 *
 * This function returns the number of bytes processed if it succeeds,
 * or one of the following negative error codes: (TBD).
 */
static ngtcp2_ssize conn_read_handshake(ngtcp2_conn *conn,
                                        const ngtcp2_path *path,
                                        const ngtcp2_pkt_info *pi,
                                        const uint8_t *pkt, size_t pktlen,
                                        ngtcp2_tstamp ts) {
  int rv;
  ngtcp2_ssize nread;

  switch (conn->state) {
  case NGTCP2_CS_CLIENT_INITIAL:
    /* TODO Better to log something when we ignore input */
    return (ngtcp2_ssize)pktlen;
  case NGTCP2_CS_CLIENT_WAIT_HANDSHAKE:
    nread = conn_recv_handshake_cpkt(conn, path, pi, pkt, pktlen, ts);
    if (nread < 0) {
      return nread;
    }

    if (conn->state == NGTCP2_CS_CLIENT_INITIAL) {
      /* Retry packet was received */
      return (ngtcp2_ssize)pktlen;
    }

    assert(conn->hs_pktns);

    if (conn->hs_pktns->crypto.rx.ckm && conn->in_pktns) {
      rv = conn_process_buffered_handshake_pkt(conn, ts);
      if (rv != 0) {
        return rv;
      }
    }

    if (conn_is_tls_handshake_completed(conn) &&
        !(conn->flags & NGTCP2_CONN_FLAG_HANDSHAKE_COMPLETED)) {
      rv = conn_handshake_completed(conn);
      if (rv != 0) {
        return rv;
      }

      rv = conn_process_buffered_protected_pkt(conn, &conn->pktns, ts);
      if (rv != 0) {
        return rv;
      }
    }

    return nread;
  case NGTCP2_CS_SERVER_INITIAL:
    nread = conn_recv_handshake_cpkt(conn, path, pi, pkt, pktlen, ts);
    if (nread < 0) {
      return nread;
    }

    /*
     * Client Hello might not fit into single Initial packet (e.g.,
     * resuming session with client authentication).  If we get Client
     * Initial which does not increase offset or it is 0RTT packet
     * buffered, perform address validation in order to buffer
     * validated data only.
     */
    if (ngtcp2_strm_rx_offset(&conn->in_pktns->crypto.strm) == 0) {
      if (conn->in_pktns->crypto.strm.rx.rob &&
          ngtcp2_rob_data_buffered(conn->in_pktns->crypto.strm.rx.rob)) {
        /* Address has been validated with token */
        if (conn->local.settings.tokenlen) {
          return nread;
        }
        return NGTCP2_ERR_RETRY;
      }
      /* If CRYPTO frame is not processed, just drop connection. */
      return NGTCP2_ERR_DROP_CONN;
    }

    /* Process re-ordered 0-RTT packets which arrived before Initial
       packet. */
    if (conn->early.ckm) {
      assert(conn->in_pktns);

      rv = conn_process_buffered_protected_pkt(conn, conn->in_pktns, ts);
      if (rv != 0) {
        return rv;
      }
    }

    return nread;
  case NGTCP2_CS_SERVER_WAIT_HANDSHAKE:
    nread = conn_recv_handshake_cpkt(conn, path, pi, pkt, pktlen, ts);
    if (nread < 0) {
      return nread;
    }

    if (conn->hs_pktns->crypto.rx.ckm) {
      rv = conn_process_buffered_handshake_pkt(conn, ts);
      if (rv != 0) {
        return rv;
      }
    }

    if (conn->hs_pktns->rx.max_pkt_num != -1) {
      conn_discard_initial_state(conn, ts);
    }

    if (!conn_is_tls_handshake_completed(conn)) {
      /* If server hits amplification limit, it cancels loss detection
         timer.  If server receives a packet from client, the limit is
         increased and server can send more.  If server has
         ack-eliciting Initial or Handshake packets, it should resend
         it if timer fired but timer is not armed in this case.  So
         instead of resending Initial/Handshake packets, if server has
         1RTT data to send, it might send them and then might hit
         amplification limit again until it hits stream data limit.
         Initial/Handshake data is not resent.  In order to avoid this
         situation, try to arm loss detection and check the expiry
         here so that on next write call, we can resend
         Initial/Handshake first. */
      if (conn->cstat.loss_detection_timer == UINT64_MAX) {
        ngtcp2_conn_set_loss_detection_timer(conn, ts);
        if (ngtcp2_conn_loss_detection_expiry(conn) <= ts) {
          rv = ngtcp2_conn_on_loss_detection_timer(conn, ts);
          if (rv != 0) {
            return rv;
          }
        }
      }

      if ((size_t)nread < pktlen) {
        /* We have 1RTT packet and application rx key, but the
           handshake has not completed yet. */
        ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_CON,
                        "buffering 1RTT packet len=%zu",
                        pktlen - (size_t)nread);

        rv = conn_buffer_pkt(conn, &conn->pktns, path, pi, pkt + nread,
                             pktlen - (size_t)nread, pktlen, ts);
        if (rv != 0) {
          assert(ngtcp2_err_is_fatal(rv));
          return rv;
        }

        return (ngtcp2_ssize)pktlen;
      }

      return nread;
    }

    if (!(conn->flags & NGTCP2_CONN_FLAG_TRANSPORT_PARAM_RECVED)) {
      return NGTCP2_ERR_REQUIRED_TRANSPORT_PARAM;
    }

    rv = conn_handshake_completed(conn);
    if (rv != 0) {
      return rv;
    }
    conn->state = NGTCP2_CS_POST_HANDSHAKE;

    rv = conn_call_activate_dcid(conn, &conn->dcid.current);
    if (rv != 0) {
      return rv;
    }

    rv = conn_process_buffered_protected_pkt(conn, &conn->pktns, ts);
    if (rv != 0) {
      return rv;
    }

    conn_discard_handshake_state(conn, ts);

    rv = conn_enqueue_handshake_done(conn);
    if (rv != 0) {
      return rv;
    }

    if (!conn->local.settings.no_pmtud) {
      rv = conn_start_pmtud(conn);
      if (rv != 0) {
        return rv;
      }
    }

    conn->pktns.rtb.persistent_congestion_start_ts = ts;

    /* Re-arm loss detection timer here after handshake has been
       confirmed. */
    ngtcp2_conn_set_loss_detection_timer(conn, ts);

    return nread;
  case NGTCP2_CS_CLOSING:
    return NGTCP2_ERR_CLOSING;
  case NGTCP2_CS_DRAINING:
    return NGTCP2_ERR_DRAINING;
  default:
    return (ngtcp2_ssize)pktlen;
  }
}

int ngtcp2_conn_read_pkt_versioned(ngtcp2_conn *conn, const ngtcp2_path *path,
                                   int pkt_info_version,
                                   const ngtcp2_pkt_info *pi,
                                   const uint8_t *pkt, size_t pktlen,
                                   ngtcp2_tstamp ts) {
  int rv = 0;
  ngtcp2_ssize nread = 0;
  const ngtcp2_pkt_info zero_pi = {0};
  (void)pkt_info_version;

  assert(!(conn->flags & NGTCP2_CONN_FLAG_PPE_PENDING));

  conn_update_timestamp(conn, ts);

  ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_CON, "recv packet len=%zu",
                  pktlen);

  if (pktlen == 0) {
    return NGTCP2_ERR_INVALID_ARGUMENT;
  }

  /* client does not expect a packet from unknown path. */
  if (!conn->server && !ngtcp2_path_eq(&conn->dcid.current.ps.path, path) &&
      (!conn->pv || !ngtcp2_path_eq(&conn->pv->dcid.ps.path, path)) &&
      !conn_is_retired_path(conn, path)) {
    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_CON,
                    "ignore packet from unknown path");
    return 0;
  }

  if (!pi) {
    pi = &zero_pi;
  }

  switch (conn->state) {
  case NGTCP2_CS_CLIENT_INITIAL:
  case NGTCP2_CS_CLIENT_WAIT_HANDSHAKE:
    nread = conn_read_handshake(conn, path, pi, pkt, pktlen, ts);
    if (nread < 0) {
      return (int)nread;
    }

    if ((size_t)nread == pktlen) {
      return 0;
    }

    assert(conn->pktns.crypto.rx.ckm);

    pkt += nread;
    pktlen -= (size_t)nread;

    break;
  case NGTCP2_CS_SERVER_INITIAL:
  case NGTCP2_CS_SERVER_WAIT_HANDSHAKE:
    if (!ngtcp2_path_eq(&conn->dcid.current.ps.path, path)) {
      ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_CON,
                      "ignore packet from unknown path during handshake");

      if (conn->state == NGTCP2_CS_SERVER_INITIAL &&
          ngtcp2_strm_rx_offset(&conn->in_pktns->crypto.strm) == 0 &&
          (!conn->in_pktns->crypto.strm.rx.rob ||
           !ngtcp2_rob_data_buffered(conn->in_pktns->crypto.strm.rx.rob))) {
        return NGTCP2_ERR_DROP_CONN;
      }

      return 0;
    }

    nread = conn_read_handshake(conn, path, pi, pkt, pktlen, ts);
    if (nread < 0) {
      return (int)nread;
    }

    if ((size_t)nread == pktlen) {
      return 0;
    }

    assert(conn->pktns.crypto.rx.ckm);

    pkt += nread;
    pktlen -= (size_t)nread;

    break;
  case NGTCP2_CS_CLOSING:
    return NGTCP2_ERR_CLOSING;
  case NGTCP2_CS_DRAINING:
    return NGTCP2_ERR_DRAINING;
  case NGTCP2_CS_POST_HANDSHAKE:
    rv = conn_prepare_key_update(conn, ts);
    if (rv != 0) {
      return rv;
    }
    break;
  default:
    ngtcp2_unreachable();
  }

  return conn_recv_cpkt(conn, path, pi, pkt, pktlen, ts);
}

/*
 * conn_check_pkt_num_exhausted returns nonzero if packet number is
 * exhausted in at least one of packet number space.
 */
static int conn_check_pkt_num_exhausted(ngtcp2_conn *conn) {
  ngtcp2_pktns *in_pktns = conn->in_pktns;
  ngtcp2_pktns *hs_pktns = conn->hs_pktns;

  return (in_pktns && in_pktns->tx.last_pkt_num == NGTCP2_MAX_PKT_NUM) ||
         (hs_pktns && hs_pktns->tx.last_pkt_num == NGTCP2_MAX_PKT_NUM) ||
         conn->pktns.tx.last_pkt_num == NGTCP2_MAX_PKT_NUM;
}

/*
 * conn_retransmit_retry_early retransmits 0RTT packet after Retry is
 * received from server.
 */
static ngtcp2_ssize conn_retransmit_retry_early(ngtcp2_conn *conn,
                                                ngtcp2_pkt_info *pi,
                                                uint8_t *dest, size_t destlen,
                                                uint8_t flags,
                                                ngtcp2_tstamp ts) {
  return conn_write_pkt(conn, pi, dest, destlen, NULL, NGTCP2_PKT_0RTT, flags,
                        ts);
}

/*
 * conn_handshake_probe_left returns nonzero if there are probe
 * packets to be sent for Initial or Handshake packet number space
 * left.
 */
static int conn_handshake_probe_left(ngtcp2_conn *conn) {
  return (conn->in_pktns && conn->in_pktns->rtb.probe_pkt_left) ||
         conn->hs_pktns->rtb.probe_pkt_left;
}

/*
 * conn_validate_early_transport_params_limits validates that the
 * limits in transport parameters remembered by client for early data
 * are not reduced.  This function is only used by client and should
 * only be called when early data is accepted by server.
 */
static int conn_validate_early_transport_params_limits(ngtcp2_conn *conn) {
  const ngtcp2_transport_params *params = conn->remote.transport_params;

  assert(!conn->server);
  assert(params);

  if (conn->early.transport_params.active_connection_id_limit >
          params->active_connection_id_limit ||
      conn->early.transport_params.initial_max_data >
          params->initial_max_data ||
      conn->early.transport_params.initial_max_stream_data_bidi_local >
          params->initial_max_stream_data_bidi_local ||
      conn->early.transport_params.initial_max_stream_data_bidi_remote >
          params->initial_max_stream_data_bidi_remote ||
      conn->early.transport_params.initial_max_stream_data_uni >
          params->initial_max_stream_data_uni ||
      conn->early.transport_params.initial_max_streams_bidi >
          params->initial_max_streams_bidi ||
      conn->early.transport_params.initial_max_streams_uni >
          params->initial_max_streams_uni ||
      conn->early.transport_params.max_datagram_frame_size >
          params->max_datagram_frame_size) {
    return NGTCP2_ERR_PROTO;
  }

  return 0;
}

/*
 * conn_write_handshake writes QUIC handshake packets to the buffer
 * pointed by |dest| of length |destlen|.  |write_datalen| specifies
 * the expected length of 0RTT packet payload.  Specify 0 to
 * |write_datalen| if there is no such data.
 *
 * This function returns the number of bytes written to the buffer, or
 * one of the following negative error codes:
 *
 * NGTCP2_ERR_PKT_NUM_EXHAUSTED
 *     Packet number is exhausted.
 * NGTCP2_ERR_NOMEM
 *     Out of memory
 * NGTCP2_ERR_REQUIRED_TRANSPORT_PARAM
 *     Required transport parameter is missing.
 * NGTCP2_CS_CLOSING
 *     Connection is in closing state.
 * NGTCP2_CS_DRAINING
 *     Connection is in draining state.
 *
 * In addition to the above negative error codes, the same error codes
 * from conn_recv_pkt may also be returned.
 */
static ngtcp2_ssize conn_write_handshake(ngtcp2_conn *conn, ngtcp2_pkt_info *pi,
                                         uint8_t *dest, size_t destlen,
                                         uint64_t write_datalen,
                                         ngtcp2_tstamp ts) {
  int rv;
  ngtcp2_ssize res = 0, nwrite = 0, early_spktlen = 0;
  size_t origlen = destlen;
  uint64_t pending_early_datalen;
  ngtcp2_dcid *dcid;
  ngtcp2_preferred_addr *paddr;

  switch (conn->state) {
  case NGTCP2_CS_CLIENT_INITIAL:
    pending_early_datalen = conn_retry_early_payloadlen(conn);
    if (pending_early_datalen) {
      write_datalen = pending_early_datalen;
    }

    if (!(conn->flags & NGTCP2_CONN_FLAG_RECV_RETRY)) {
      nwrite =
          conn_write_client_initial(conn, pi, dest, destlen, write_datalen, ts);
      if (nwrite <= 0) {
        return nwrite;
      }
    } else {
      nwrite = conn_write_handshake_pkt(
          conn, pi, dest, destlen, NGTCP2_PKT_INITIAL,
          NGTCP2_WRITE_PKT_FLAG_NONE, write_datalen, ts);
      if (nwrite < 0) {
        return nwrite;
      }
    }

    if (pending_early_datalen) {
      early_spktlen = conn_retransmit_retry_early(
          conn, pi, dest + nwrite, destlen - (size_t)nwrite,
          nwrite ? NGTCP2_WRITE_PKT_FLAG_REQUIRE_PADDING
                 : NGTCP2_WRITE_PKT_FLAG_NONE,
          ts);

      if (early_spktlen < 0) {
        assert(ngtcp2_err_is_fatal((int)early_spktlen));
        return early_spktlen;
      }
    }

    conn->state = NGTCP2_CS_CLIENT_WAIT_HANDSHAKE;

    res = nwrite + early_spktlen;

    return res;
  case NGTCP2_CS_CLIENT_WAIT_HANDSHAKE:
    if (!conn_handshake_probe_left(conn) && conn_cwnd_is_zero(conn)) {
      destlen = 0;
    } else {
      if (!(conn->flags & NGTCP2_CONN_FLAG_HANDSHAKE_COMPLETED)) {
        pending_early_datalen = conn_retry_early_payloadlen(conn);
        if (pending_early_datalen) {
          write_datalen = pending_early_datalen;
        }
      }

      nwrite =
          conn_write_handshake_pkts(conn, pi, dest, destlen, write_datalen, ts);
      if (nwrite < 0) {
        return nwrite;
      }

      res += nwrite;
      dest += nwrite;
      destlen -= (size_t)nwrite;
    }

    if (!conn_is_tls_handshake_completed(conn)) {
      if (!(conn->flags & NGTCP2_CONN_FLAG_EARLY_DATA_REJECTED)) {
        nwrite = conn_retransmit_retry_early(conn, pi, dest, destlen,
                                             NGTCP2_WRITE_PKT_FLAG_NONE, ts);
        if (nwrite < 0) {
          return nwrite;
        }

        res += nwrite;
      }

      if (res == 0) {
        nwrite = conn_write_handshake_ack_pkts(conn, pi, dest, origlen, ts);
        if (nwrite < 0) {
          return nwrite;
        }
        res = nwrite;
      }

      return res;
    }

    if (!(conn->flags & NGTCP2_CONN_FLAG_HANDSHAKE_COMPLETED)) {
      return res;
    }

    if (!(conn->flags & NGTCP2_CONN_FLAG_TRANSPORT_PARAM_RECVED)) {
      return NGTCP2_ERR_REQUIRED_TRANSPORT_PARAM;
    }

    if ((conn->flags & NGTCP2_CONN_FLAG_EARLY_KEY_INSTALLED) &&
        !(conn->flags & NGTCP2_CONN_FLAG_EARLY_DATA_REJECTED)) {
      rv = conn_validate_early_transport_params_limits(conn);
      if (rv != 0) {
        return rv;
      }
    }

    /* Server might increase stream data limits.  Extend it if we have
       streams created for early data. */
    rv = conn_sync_stream_data_limit(conn);
    if (rv != 0) {
      return rv;
    }

    conn->state = NGTCP2_CS_POST_HANDSHAKE;

    assert(conn->remote.transport_params);

    if (conn->remote.transport_params->preferred_addr_present) {
      assert(!ngtcp2_ringbuf_full(&conn->dcid.unused.rb));

      paddr = &conn->remote.transport_params->preferred_addr;
      dcid = ngtcp2_ringbuf_push_back(&conn->dcid.unused.rb);
      ngtcp2_dcid_init(dcid, 1, &paddr->cid, paddr->stateless_reset_token);

      rv = ngtcp2_gaptr_push(&conn->dcid.seqgap, 1, 1);
      if (rv != 0) {
        return (ngtcp2_ssize)rv;
      }
    }

    if (conn->remote.transport_params->stateless_reset_token_present) {
      assert(conn->dcid.current.seq == 0);
      assert(!(conn->dcid.current.flags & NGTCP2_DCID_FLAG_TOKEN_PRESENT));
      ngtcp2_dcid_set_token(
          &conn->dcid.current,
          conn->remote.transport_params->stateless_reset_token);
    }

    rv = conn_call_activate_dcid(conn, &conn->dcid.current);
    if (rv != 0) {
      return rv;
    }

    conn_process_early_rtb(conn);

    if (!conn->local.settings.no_pmtud) {
      rv = conn_start_pmtud(conn);
      if (rv != 0) {
        return rv;
      }
    }

    return res;
  case NGTCP2_CS_SERVER_INITIAL:
    nwrite =
        conn_write_handshake_pkts(conn, pi, dest, destlen, write_datalen, ts);
    if (nwrite < 0) {
      return nwrite;
    }

    if (nwrite) {
      conn->state = NGTCP2_CS_SERVER_WAIT_HANDSHAKE;
    }

    return nwrite;
  case NGTCP2_CS_SERVER_WAIT_HANDSHAKE:
    if (conn_handshake_probe_left(conn) || !conn_cwnd_is_zero(conn)) {
      nwrite =
          conn_write_handshake_pkts(conn, pi, dest, destlen, write_datalen, ts);
      if (nwrite < 0) {
        return nwrite;
      }

      res += nwrite;
      dest += nwrite;
      destlen -= (size_t)nwrite;
    }

    if (res == 0) {
      nwrite = conn_write_handshake_ack_pkts(conn, pi, dest, origlen, ts);
      if (nwrite < 0) {
        return nwrite;
      }

      res += nwrite;
      dest += nwrite;
      origlen -= (size_t)nwrite;
    }

    return res;
  case NGTCP2_CS_CLOSING:
    return NGTCP2_ERR_CLOSING;
  case NGTCP2_CS_DRAINING:
    return NGTCP2_ERR_DRAINING;
  default:
    return 0;
  }
}

/**
 * @function
 *
 * `conn_client_write_handshake` writes client side handshake data and
 * 0RTT packet.
 *
 * In order to send STREAM data in 0RTT packet, specify
 * |vmsg|->stream.  |vmsg|->stream.strm, |vmsg|->stream.fin,
 * |vmsg|->stream.data, and |vmsg|->stream.datacnt are stream to which
 * 0-RTT data is sent, whether it is a last data chunk in this stream,
 * a vector of 0-RTT data, and its number of elements respectively.
 * The amount of 0RTT data sent is assigned to
 * *|vmsg|->stream.pdatalen.  If no data is sent, -1 is assigned.
 * Note that 0 length STREAM frame is allowed in QUIC, so 0 might be
 * assigned to *|vmsg|->stream.pdatalen.
 *
 * This function returns 0 if it cannot write any frame because buffer
 * is too small, or packet is congestion limited.  Application should
 * keep reading and wait for congestion window to grow.
 *
 * This function returns the number of bytes written to the buffer
 * pointed by |dest| if it succeeds, or one of the following negative
 * error codes: (TBD).
 */
static ngtcp2_ssize conn_client_write_handshake(ngtcp2_conn *conn,
                                                ngtcp2_pkt_info *pi,
                                                uint8_t *dest, size_t destlen,
                                                ngtcp2_vmsg *vmsg,
                                                ngtcp2_tstamp ts) {
  int send_stream = 0;
  int send_datagram = 0;
  ngtcp2_ssize spktlen, early_spktlen;
  uint64_t datalen;
  uint64_t write_datalen = 0;
  uint8_t wflags = NGTCP2_WRITE_PKT_FLAG_NONE;
  int ppe_pending = (conn->flags & NGTCP2_CONN_FLAG_PPE_PENDING) != 0;
  uint32_t version;

  assert(!conn->server);

  /* conn->early.ckm might be created in the first call of
     conn_handshake().  Check it later. */
  if (vmsg) {
    switch (vmsg->type) {
    case NGTCP2_VMSG_TYPE_STREAM:
      datalen = ngtcp2_vec_len(vmsg->stream.data, vmsg->stream.datacnt);
      send_stream = conn_retry_early_payloadlen(conn) == 0;
      if (send_stream) {
        write_datalen = ngtcp2_min(datalen + NGTCP2_STREAM_OVERHEAD,
                                   NGTCP2_MIN_COALESCED_PAYLOADLEN);

        if (vmsg->stream.flags & NGTCP2_WRITE_STREAM_FLAG_MORE) {
          wflags |= NGTCP2_WRITE_PKT_FLAG_MORE;
        }
      } else {
        vmsg = NULL;
      }
      break;
    case NGTCP2_VMSG_TYPE_DATAGRAM:
      datalen = ngtcp2_vec_len(vmsg->datagram.data, vmsg->datagram.datacnt);
      send_datagram = conn_retry_early_payloadlen(conn) == 0;
      if (send_datagram) {
        write_datalen = datalen + NGTCP2_DATAGRAM_OVERHEAD;

        if (vmsg->datagram.flags & NGTCP2_WRITE_DATAGRAM_FLAG_MORE) {
          wflags |= NGTCP2_WRITE_PKT_FLAG_MORE;
        }
      } else {
        vmsg = NULL;
      }
      break;
    }
  }

  if (!ppe_pending) {
    spktlen = conn_write_handshake(conn, pi, dest, destlen, write_datalen, ts);

    if (spktlen < 0) {
      return spktlen;
    }

    if ((conn->flags & NGTCP2_CONN_FLAG_EARLY_DATA_REJECTED) ||
        !conn->early.ckm || (!send_stream && !send_datagram)) {
      return spktlen;
    }

    /* If spktlen > 0, we are making a compound packet.  If Initial
       packet is written, we have to pad bytes to 0-RTT packet. */
    version = conn->negotiated_version ? conn->negotiated_version
                                       : conn->client_chosen_version;
    if (spktlen > 0 &&
        ngtcp2_pkt_get_type_long(version, dest[0]) == NGTCP2_PKT_INITIAL) {
      wflags |= NGTCP2_WRITE_PKT_FLAG_REQUIRE_PADDING;
      conn->pkt.require_padding = 1;
    }
  } else {
    assert(!conn->pktns.crypto.rx.ckm);
    assert(!conn->pktns.crypto.tx.ckm);
    assert(conn->early.ckm);

    if (conn->pkt.require_padding) {
      wflags |= NGTCP2_WRITE_PKT_FLAG_REQUIRE_PADDING;
    }
    spktlen = conn->pkt.hs_spktlen;
  }

  dest += spktlen;
  destlen -= (size_t)spktlen;

  if (conn_cwnd_is_zero(conn)) {
    return spktlen;
  }

  early_spktlen = conn_write_pkt(conn, pi, dest, destlen, vmsg, NGTCP2_PKT_0RTT,
                                 wflags, ts);
  if (early_spktlen < 0) {
    switch (early_spktlen) {
    case NGTCP2_ERR_STREAM_DATA_BLOCKED:
      if (!(wflags & NGTCP2_WRITE_PKT_FLAG_MORE)) {
        if (spktlen) {
          return spktlen;
        }

        break;
      }
      /* fall through */
    case NGTCP2_ERR_WRITE_MORE:
      conn->pkt.hs_spktlen = spktlen;
      break;
    }
    return early_spktlen;
  }

  return spktlen + early_spktlen;
}

void ngtcp2_conn_tls_handshake_completed(ngtcp2_conn *conn) {
  conn->flags |= NGTCP2_CONN_FLAG_TLS_HANDSHAKE_COMPLETED;
  if (conn->server) {
    conn->flags |= NGTCP2_CONN_FLAG_HANDSHAKE_CONFIRMED;
  }
}

int ngtcp2_conn_get_handshake_completed(ngtcp2_conn *conn) {
  return conn_is_tls_handshake_completed(conn) &&
         (conn->flags & NGTCP2_CONN_FLAG_HANDSHAKE_COMPLETED);
}

int ngtcp2_conn_sched_ack(ngtcp2_conn *conn, ngtcp2_acktr *acktr,
                          int64_t pkt_num, int active_ack, ngtcp2_tstamp ts) {
  int rv;
  (void)conn;

  rv = ngtcp2_acktr_add(acktr, pkt_num, active_ack, ts);
  if (rv != 0) {
    assert(rv != NGTCP2_ERR_INVALID_ARGUMENT);
    return rv;
  }

  return 0;
}

int ngtcp2_accept(ngtcp2_pkt_hd *dest, const uint8_t *pkt, size_t pktlen) {
  ngtcp2_ssize nread;
  ngtcp2_pkt_hd hd, *p;

  if (dest) {
    p = dest;
  } else {
    p = &hd;
  }

  if (pktlen == 0 || (pkt[0] & NGTCP2_HEADER_FORM_BIT) == 0) {
    return NGTCP2_ERR_INVALID_ARGUMENT;
  }

  nread = ngtcp2_pkt_decode_hd_long(p, pkt, pktlen);
  if (nread < 0) {
    return (int)nread;
  }

  switch (p->type) {
  case NGTCP2_PKT_INITIAL:
    break;
  case NGTCP2_PKT_0RTT:
    /* 0-RTT packet may arrive before Initial packet due to
       re-ordering.  ngtcp2 does not buffer 0RTT packet unless the
       very first Initial packet is received or token is received.
       Previously, we returned NGTCP2_ERR_RETRY here, so that client
       can resend 0RTT data.  But it incurs 1RTT already and
       diminishes the value of 0RTT.  Therefore, we just discard the
       packet here for now. */
  default:
    return NGTCP2_ERR_INVALID_ARGUMENT;
  }

  if (pktlen < NGTCP2_MAX_UDP_PAYLOAD_SIZE ||
      (p->tokenlen == 0 && p->dcid.datalen < NGTCP2_MIN_INITIAL_DCIDLEN)) {
    return NGTCP2_ERR_INVALID_ARGUMENT;
  }

  return 0;
}

int ngtcp2_conn_install_initial_key(
    ngtcp2_conn *conn, const ngtcp2_crypto_aead_ctx *rx_aead_ctx,
    const uint8_t *rx_iv, const ngtcp2_crypto_cipher_ctx *rx_hp_ctx,
    const ngtcp2_crypto_aead_ctx *tx_aead_ctx, const uint8_t *tx_iv,
    const ngtcp2_crypto_cipher_ctx *tx_hp_ctx, size_t ivlen) {
  ngtcp2_pktns *pktns = conn->in_pktns;
  int rv;

  assert(ivlen >= 8);
  assert(pktns);

  conn_call_delete_crypto_cipher_ctx(conn, &pktns->crypto.rx.hp_ctx);
  pktns->crypto.rx.hp_ctx.native_handle = NULL;

  if (pktns->crypto.rx.ckm) {
    conn_call_delete_crypto_aead_ctx(conn, &pktns->crypto.rx.ckm->aead_ctx);
    ngtcp2_crypto_km_del(pktns->crypto.rx.ckm, conn->mem);
    pktns->crypto.rx.ckm = NULL;
  }

  conn_call_delete_crypto_cipher_ctx(conn, &pktns->crypto.tx.hp_ctx);
  pktns->crypto.tx.hp_ctx.native_handle = NULL;

  if (pktns->crypto.tx.ckm) {
    conn_call_delete_crypto_aead_ctx(conn, &pktns->crypto.tx.ckm->aead_ctx);
    ngtcp2_crypto_km_del(pktns->crypto.tx.ckm, conn->mem);
    pktns->crypto.tx.ckm = NULL;
  }

  rv = ngtcp2_crypto_km_new(&pktns->crypto.rx.ckm, NULL, 0, NULL, rx_iv, ivlen,
                            conn->mem);
  if (rv != 0) {
    return rv;
  }

  rv = ngtcp2_crypto_km_new(&pktns->crypto.tx.ckm, NULL, 0, NULL, tx_iv, ivlen,
                            conn->mem);
  if (rv != 0) {
    return rv;
  }

  /* Take owner ship after we are sure that no failure occurs, so that
     caller can delete these contexts on failure. */
  pktns->crypto.rx.ckm->aead_ctx = *rx_aead_ctx;
  pktns->crypto.rx.hp_ctx = *rx_hp_ctx;
  pktns->crypto.tx.ckm->aead_ctx = *tx_aead_ctx;
  pktns->crypto.tx.hp_ctx = *tx_hp_ctx;

  return 0;
}

int ngtcp2_conn_install_vneg_initial_key(
    ngtcp2_conn *conn, uint32_t version,
    const ngtcp2_crypto_aead_ctx *rx_aead_ctx, const uint8_t *rx_iv,
    const ngtcp2_crypto_cipher_ctx *rx_hp_ctx,
    const ngtcp2_crypto_aead_ctx *tx_aead_ctx, const uint8_t *tx_iv,
    const ngtcp2_crypto_cipher_ctx *tx_hp_ctx, size_t ivlen) {
  int rv;

  assert(ivlen >= 8);

  conn_call_delete_crypto_cipher_ctx(conn, &conn->vneg.rx.hp_ctx);
  conn->vneg.rx.hp_ctx.native_handle = NULL;

  if (conn->vneg.rx.ckm) {
    conn_call_delete_crypto_aead_ctx(conn, &conn->vneg.rx.ckm->aead_ctx);
    ngtcp2_crypto_km_del(conn->vneg.rx.ckm, conn->mem);
    conn->vneg.rx.ckm = NULL;
  }

  conn_call_delete_crypto_cipher_ctx(conn, &conn->vneg.tx.hp_ctx);
  conn->vneg.tx.hp_ctx.native_handle = NULL;

  if (conn->vneg.tx.ckm) {
    conn_call_delete_crypto_aead_ctx(conn, &conn->vneg.tx.ckm->aead_ctx);
    ngtcp2_crypto_km_del(conn->vneg.tx.ckm, conn->mem);
    conn->vneg.tx.ckm = NULL;
  }

  rv = ngtcp2_crypto_km_new(&conn->vneg.rx.ckm, NULL, 0, NULL, rx_iv, ivlen,
                            conn->mem);
  if (rv != 0) {
    return rv;
  }

  rv = ngtcp2_crypto_km_new(&conn->vneg.tx.ckm, NULL, 0, NULL, tx_iv, ivlen,
                            conn->mem);
  if (rv != 0) {
    return rv;
  }

  /* Take owner ship after we are sure that no failure occurs, so that
     caller can delete these contexts on failure. */
  conn->vneg.rx.ckm->aead_ctx = *rx_aead_ctx;
  conn->vneg.rx.hp_ctx = *rx_hp_ctx;
  conn->vneg.tx.ckm->aead_ctx = *tx_aead_ctx;
  conn->vneg.tx.hp_ctx = *tx_hp_ctx;
  conn->vneg.version = version;

  return 0;
}

int ngtcp2_conn_install_rx_handshake_key(
    ngtcp2_conn *conn, const ngtcp2_crypto_aead_ctx *aead_ctx,
    const uint8_t *iv, size_t ivlen, const ngtcp2_crypto_cipher_ctx *hp_ctx) {
  ngtcp2_pktns *pktns = conn->hs_pktns;
  int rv;

  assert(ivlen >= 8);
  assert(pktns);
  assert(!pktns->crypto.rx.hp_ctx.native_handle);
  assert(!pktns->crypto.rx.ckm);

  rv = ngtcp2_crypto_km_new(&pktns->crypto.rx.ckm, NULL, 0, aead_ctx, iv, ivlen,
                            conn->mem);
  if (rv != 0) {
    return rv;
  }

  pktns->crypto.rx.hp_ctx = *hp_ctx;

  rv = conn_call_recv_rx_key(conn, NGTCP2_ENCRYPTION_LEVEL_HANDSHAKE);
  if (rv != 0) {
    ngtcp2_crypto_km_del(pktns->crypto.rx.ckm, conn->mem);
    pktns->crypto.rx.ckm = NULL;

    memset(&pktns->crypto.rx.hp_ctx, 0, sizeof(pktns->crypto.rx.hp_ctx));

    return rv;
  }

  return 0;
}

int ngtcp2_conn_install_tx_handshake_key(
    ngtcp2_conn *conn, const ngtcp2_crypto_aead_ctx *aead_ctx,
    const uint8_t *iv, size_t ivlen, const ngtcp2_crypto_cipher_ctx *hp_ctx) {
  ngtcp2_pktns *pktns = conn->hs_pktns;
  int rv;

  assert(ivlen >= 8);
  assert(pktns);
  assert(!pktns->crypto.tx.hp_ctx.native_handle);
  assert(!pktns->crypto.tx.ckm);

  rv = ngtcp2_crypto_km_new(&pktns->crypto.tx.ckm, NULL, 0, aead_ctx, iv, ivlen,
                            conn->mem);
  if (rv != 0) {
    return rv;
  }

  pktns->crypto.tx.hp_ctx = *hp_ctx;

  if (conn->server) {
    rv = ngtcp2_conn_commit_local_transport_params(conn);
    if (rv != 0) {
      return rv;
    }
  }

  rv = conn_call_recv_tx_key(conn, NGTCP2_ENCRYPTION_LEVEL_HANDSHAKE);
  if (rv != 0) {
    ngtcp2_crypto_km_del(pktns->crypto.tx.ckm, conn->mem);
    pktns->crypto.tx.ckm = NULL;

    memset(&pktns->crypto.tx.hp_ctx, 0, sizeof(pktns->crypto.tx.hp_ctx));

    return rv;
  }

  return 0;
}

int ngtcp2_conn_install_0rtt_key(ngtcp2_conn *conn,
                                 const ngtcp2_crypto_aead_ctx *aead_ctx,
                                 const uint8_t *iv, size_t ivlen,
                                 const ngtcp2_crypto_cipher_ctx *hp_ctx) {
  int rv;

  assert(ivlen >= 8);
  assert(!conn->early.hp_ctx.native_handle);
  assert(!conn->early.ckm);

  rv = ngtcp2_crypto_km_new(&conn->early.ckm, NULL, 0, aead_ctx, iv, ivlen,
                            conn->mem);
  if (rv != 0) {
    return rv;
  }

  conn->early.hp_ctx = *hp_ctx;

  conn->flags |= NGTCP2_CONN_FLAG_EARLY_KEY_INSTALLED;

  if (conn->server) {
    rv = conn_call_recv_rx_key(conn, NGTCP2_ENCRYPTION_LEVEL_0RTT);
  } else {
    rv = conn_call_recv_tx_key(conn, NGTCP2_ENCRYPTION_LEVEL_0RTT);
  }
  if (rv != 0) {
    ngtcp2_crypto_km_del(conn->early.ckm, conn->mem);
    conn->early.ckm = NULL;

    memset(&conn->early.hp_ctx, 0, sizeof(conn->early.hp_ctx));

    return rv;
  }

  return 0;
}

int ngtcp2_conn_install_rx_key(ngtcp2_conn *conn, const uint8_t *secret,
                               size_t secretlen,
                               const ngtcp2_crypto_aead_ctx *aead_ctx,
                               const uint8_t *iv, size_t ivlen,
                               const ngtcp2_crypto_cipher_ctx *hp_ctx) {
  ngtcp2_pktns *pktns = &conn->pktns;
  int rv;

  assert(ivlen >= 8);
  assert(!pktns->crypto.rx.hp_ctx.native_handle);
  assert(!pktns->crypto.rx.ckm);

  rv = ngtcp2_crypto_km_new(&pktns->crypto.rx.ckm, secret, secretlen, aead_ctx,
                            iv, ivlen, conn->mem);
  if (rv != 0) {
    return rv;
  }

  pktns->crypto.rx.hp_ctx = *hp_ctx;

  if (!conn->server) {
    if (conn->remote.pending_transport_params) {
      ngtcp2_transport_params_del(conn->remote.transport_params, conn->mem);

      conn->remote.transport_params = conn->remote.pending_transport_params;
      conn->remote.pending_transport_params = NULL;
      conn_sync_stream_id_limit(conn);
      conn->tx.max_offset = conn->remote.transport_params->initial_max_data;
    }

    if (conn->early.ckm) {
      conn_discard_early_key(conn);
    }
  }

  rv = conn_call_recv_rx_key(conn, NGTCP2_ENCRYPTION_LEVEL_1RTT);
  if (rv != 0) {
    ngtcp2_crypto_km_del(pktns->crypto.rx.ckm, conn->mem);
    pktns->crypto.rx.ckm = NULL;

    memset(&pktns->crypto.rx.hp_ctx, 0, sizeof(pktns->crypto.rx.hp_ctx));

    return rv;
  }

  return 0;
}

int ngtcp2_conn_install_tx_key(ngtcp2_conn *conn, const uint8_t *secret,
                               size_t secretlen,
                               const ngtcp2_crypto_aead_ctx *aead_ctx,
                               const uint8_t *iv, size_t ivlen,
                               const ngtcp2_crypto_cipher_ctx *hp_ctx) {
  ngtcp2_pktns *pktns = &conn->pktns;
  int rv;

  assert(ivlen >= 8);
  assert(!pktns->crypto.tx.hp_ctx.native_handle);
  assert(!pktns->crypto.tx.ckm);

  rv = ngtcp2_crypto_km_new(&pktns->crypto.tx.ckm, secret, secretlen, aead_ctx,
                            iv, ivlen, conn->mem);
  if (rv != 0) {
    return rv;
  }

  pktns->crypto.tx.hp_ctx = *hp_ctx;

  if (conn->server) {
    if (conn->remote.pending_transport_params) {
      ngtcp2_transport_params_del(conn->remote.transport_params, conn->mem);

      conn->remote.transport_params = conn->remote.pending_transport_params;
      conn->remote.pending_transport_params = NULL;
      conn_sync_stream_id_limit(conn);
      conn->tx.max_offset = conn->remote.transport_params->initial_max_data;
    }
  } else if (conn->early.ckm) {
    conn_discard_early_key(conn);
  }

  rv = conn_call_recv_tx_key(conn, NGTCP2_ENCRYPTION_LEVEL_1RTT);
  if (rv != 0) {
    ngtcp2_crypto_km_del(pktns->crypto.tx.ckm, conn->mem);
    pktns->crypto.tx.ckm = NULL;

    memset(&pktns->crypto.tx.hp_ctx, 0, sizeof(pktns->crypto.tx.hp_ctx));

    return rv;
  }

  return 0;
}

static int conn_initiate_key_update(ngtcp2_conn *conn, ngtcp2_tstamp ts) {
  ngtcp2_tstamp confirmed_ts = conn->crypto.key_update.confirmed_ts;
  ngtcp2_duration pto = conn_compute_pto(conn, &conn->pktns);

  assert(conn->state == NGTCP2_CS_POST_HANDSHAKE);

  if (!(conn->flags & NGTCP2_CONN_FLAG_HANDSHAKE_CONFIRMED) ||
      (conn->flags & NGTCP2_CONN_FLAG_KEY_UPDATE_NOT_CONFIRMED) ||
      !conn->crypto.key_update.new_tx_ckm ||
      !conn->crypto.key_update.new_rx_ckm ||
      ngtcp2_tstamp_not_elapsed(confirmed_ts, 3 * pto, ts)) {
    return NGTCP2_ERR_INVALID_STATE;
  }

  conn_rotate_keys(conn, NGTCP2_MAX_PKT_NUM, /* initiator = */ 1);

  return 0;
}

int ngtcp2_conn_initiate_key_update(ngtcp2_conn *conn, ngtcp2_tstamp ts) {
  conn_update_timestamp(conn, ts);

  return conn_initiate_key_update(conn, ts);
}

/*
 * conn_retire_stale_bound_dcid retires stale destination connection
 * ID in conn->dcid.bound to keep some unused destination connection
 * IDs available.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 */
static int conn_retire_stale_bound_dcid(ngtcp2_conn *conn,
                                        ngtcp2_duration timeout,
                                        ngtcp2_tstamp ts) {
  size_t i;
  ngtcp2_dcid *dcid, *last;
  int rv;

  for (i = 0; i < ngtcp2_ringbuf_len(&conn->dcid.bound.rb);) {
    dcid = ngtcp2_ringbuf_get(&conn->dcid.bound.rb, i);

    assert(dcid->cid.datalen);

    if (ngtcp2_tstamp_not_elapsed(dcid->bound_ts, timeout, ts)) {
      ++i;
      continue;
    }

    rv = conn_retire_dcid_seq(conn, dcid->seq);
    if (rv != 0) {
      return rv;
    }

    if (i == 0) {
      ngtcp2_ringbuf_pop_front(&conn->dcid.bound.rb);
      continue;
    }

    if (i == ngtcp2_ringbuf_len(&conn->dcid.bound.rb) - 1) {
      ngtcp2_ringbuf_pop_back(&conn->dcid.bound.rb);
      break;
    }

    last = ngtcp2_ringbuf_get(&conn->dcid.bound.rb,
                              ngtcp2_ringbuf_len(&conn->dcid.bound.rb) - 1);
    ngtcp2_dcid_copy(dcid, last);
    ngtcp2_ringbuf_pop_back(&conn->dcid.bound.rb);
  }

  return 0;
}

ngtcp2_tstamp ngtcp2_conn_loss_detection_expiry(ngtcp2_conn *conn) {
  return conn->cstat.loss_detection_timer;
}

ngtcp2_tstamp ngtcp2_conn_internal_expiry(ngtcp2_conn *conn) {
  ngtcp2_tstamp res = UINT64_MAX, t;
  ngtcp2_duration pto = conn_compute_pto(conn, &conn->pktns);
  ngtcp2_scid *scid;
  ngtcp2_dcid *dcid;
  size_t i, len;

  if (conn->pv) {
    res = ngtcp2_pv_next_expiry(conn->pv);
  }

  if (conn->pmtud) {
    res = ngtcp2_min(res, conn->pmtud->expiry);
  }

  if (!ngtcp2_pq_empty(&conn->scid.used)) {
    scid = ngtcp2_struct_of(ngtcp2_pq_top(&conn->scid.used), ngtcp2_scid, pe);
    if (scid->retired_ts != UINT64_MAX) {
      t = scid->retired_ts + pto;
      res = ngtcp2_min(res, t);
    }
  }

  if (ngtcp2_ringbuf_len(&conn->dcid.retired.rb)) {
    dcid = ngtcp2_ringbuf_get(&conn->dcid.retired.rb, 0);
    t = dcid->retired_ts + pto;
    res = ngtcp2_min(res, t);
  }

  if (conn->dcid.current.cid.datalen) {
    len = ngtcp2_ringbuf_len(&conn->dcid.bound.rb);
    for (i = 0; i < len; ++i) {
      dcid = ngtcp2_ringbuf_get(&conn->dcid.bound.rb, i);

      assert(dcid->cid.datalen);
      assert(dcid->bound_ts != UINT64_MAX);

      t = dcid->bound_ts + 3 * pto;
      res = ngtcp2_min(res, t);
    }
  }

  if (conn->server && conn->early.ckm &&
      conn->early.discard_started_ts != UINT64_MAX) {
    t = conn->early.discard_started_ts + 3 * pto;
    res = ngtcp2_min(res, t);
  }

  return res;
}

ngtcp2_tstamp ngtcp2_conn_ack_delay_expiry(ngtcp2_conn *conn) {
  ngtcp2_acktr *acktr = &conn->pktns.acktr;

  if (!(acktr->flags & NGTCP2_ACKTR_FLAG_CANCEL_TIMER) &&
      acktr->first_unacked_ts != UINT64_MAX) {
    return acktr->first_unacked_ts + conn_compute_ack_delay(conn);
  }
  return UINT64_MAX;
}

static ngtcp2_tstamp conn_handshake_expiry(ngtcp2_conn *conn) {
  if (conn_is_tls_handshake_completed(conn) ||
      conn->local.settings.handshake_timeout == UINT64_MAX ||
      conn->local.settings.initial_ts >=
          UINT64_MAX - conn->local.settings.handshake_timeout) {
    return UINT64_MAX;
  }

  return conn->local.settings.initial_ts +
         conn->local.settings.handshake_timeout;
}

ngtcp2_tstamp ngtcp2_conn_get_expiry(ngtcp2_conn *conn) {
  ngtcp2_tstamp t1 = ngtcp2_conn_loss_detection_expiry(conn);
  ngtcp2_tstamp t2 = ngtcp2_conn_ack_delay_expiry(conn);
  ngtcp2_tstamp t3 = ngtcp2_conn_internal_expiry(conn);
  ngtcp2_tstamp t4 = ngtcp2_conn_lost_pkt_expiry(conn);
  ngtcp2_tstamp t5 = conn_keep_alive_expiry(conn);
  ngtcp2_tstamp t6 = conn_handshake_expiry(conn);
  ngtcp2_tstamp t7 = ngtcp2_conn_get_idle_expiry(conn);
  ngtcp2_tstamp res = ngtcp2_min(t1, t2);
  res = ngtcp2_min(res, t3);
  res = ngtcp2_min(res, t4);
  res = ngtcp2_min(res, t5);
  res = ngtcp2_min(res, t6);
  res = ngtcp2_min(res, t7);
  return ngtcp2_min(res, conn->tx.pacing.next_ts);
}

int ngtcp2_conn_handle_expiry(ngtcp2_conn *conn, ngtcp2_tstamp ts) {
  int rv;
  ngtcp2_duration pto;

  conn_update_timestamp(conn, ts);

  pto = conn_compute_pto(conn, &conn->pktns);

  assert(!(conn->flags & NGTCP2_CONN_FLAG_PPE_PENDING));

  if (ngtcp2_conn_get_idle_expiry(conn) <= ts) {
    return NGTCP2_ERR_IDLE_CLOSE;
  }

  ngtcp2_conn_cancel_expired_ack_delay_timer(conn, ts);

  conn_cancel_expired_keep_alive_timer(conn, ts);

  conn_cancel_expired_pkt_tx_timer(conn, ts);

  ngtcp2_conn_remove_lost_pkt(conn, ts);

  if (conn->pv) {
    ngtcp2_pv_cancel_expired_timer(conn->pv, ts);
  }

  if (conn->pmtud) {
    ngtcp2_pmtud_handle_expiry(conn->pmtud, ts);
    if (ngtcp2_pmtud_finished(conn->pmtud)) {
      ngtcp2_conn_stop_pmtud(conn);
    }
  }

  if (ngtcp2_conn_loss_detection_expiry(conn) <= ts) {
    rv = ngtcp2_conn_on_loss_detection_timer(conn, ts);
    if (rv != 0) {
      return rv;
    }
  }

  if (conn->dcid.current.cid.datalen) {
    rv = conn_retire_stale_bound_dcid(conn, 3 * pto, ts);
    if (rv != 0) {
      return rv;
    }
  }

  rv = conn_remove_retired_connection_id(conn, pto, ts);
  if (rv != 0) {
    return rv;
  }

  if (conn->server && conn->early.ckm &&
      ngtcp2_tstamp_elapsed(conn->early.discard_started_ts, 3 * pto, ts)) {
    conn_discard_early_key(conn);
  }

  if (!conn_is_tls_handshake_completed(conn) &&
      ngtcp2_tstamp_elapsed(conn->local.settings.initial_ts,
                            conn->local.settings.handshake_timeout, ts)) {
    return NGTCP2_ERR_HANDSHAKE_TIMEOUT;
  }

  return 0;
}

static void acktr_cancel_expired_ack_delay_timer(ngtcp2_acktr *acktr,
                                                 ngtcp2_duration max_ack_delay,
                                                 ngtcp2_tstamp ts) {
  if (!(acktr->flags & NGTCP2_ACKTR_FLAG_CANCEL_TIMER) &&
      ngtcp2_tstamp_elapsed(acktr->first_unacked_ts, max_ack_delay, ts)) {
    acktr->flags |= NGTCP2_ACKTR_FLAG_CANCEL_TIMER;
  }
}

void ngtcp2_conn_cancel_expired_ack_delay_timer(ngtcp2_conn *conn,
                                                ngtcp2_tstamp ts) {
  ngtcp2_duration ack_delay = conn_compute_ack_delay(conn);

  if (conn->in_pktns) {
    acktr_cancel_expired_ack_delay_timer(&conn->in_pktns->acktr, 0, ts);
  }
  if (conn->hs_pktns) {
    acktr_cancel_expired_ack_delay_timer(&conn->hs_pktns->acktr, 0, ts);
  }
  acktr_cancel_expired_ack_delay_timer(&conn->pktns.acktr, ack_delay, ts);
}

ngtcp2_tstamp ngtcp2_conn_lost_pkt_expiry(ngtcp2_conn *conn) {
  ngtcp2_tstamp res = UINT64_MAX, ts;

  if (conn->in_pktns) {
    ts = ngtcp2_rtb_lost_pkt_ts(&conn->in_pktns->rtb);
    if (ts != UINT64_MAX) {
      ts += conn_compute_pto(conn, conn->in_pktns);
      res = ngtcp2_min(res, ts);
    }
  }

  if (conn->hs_pktns) {
    ts = ngtcp2_rtb_lost_pkt_ts(&conn->hs_pktns->rtb);
    if (ts != UINT64_MAX) {
      ts += conn_compute_pto(conn, conn->hs_pktns);
      res = ngtcp2_min(res, ts);
    }
  }

  ts = ngtcp2_rtb_lost_pkt_ts(&conn->pktns.rtb);
  if (ts != UINT64_MAX) {
    ts += conn_compute_pto(conn, &conn->pktns);
    res = ngtcp2_min(res, ts);
  }

  return res;
}

void ngtcp2_conn_remove_lost_pkt(ngtcp2_conn *conn, ngtcp2_tstamp ts) {
  ngtcp2_duration pto;

  if (conn->in_pktns) {
    pto = conn_compute_pto(conn, conn->in_pktns);
    ngtcp2_rtb_remove_expired_lost_pkt(&conn->in_pktns->rtb, pto, ts);
  }
  if (conn->hs_pktns) {
    pto = conn_compute_pto(conn, conn->hs_pktns);
    ngtcp2_rtb_remove_expired_lost_pkt(&conn->hs_pktns->rtb, pto, ts);
  }
  pto = conn_compute_pto(conn, &conn->pktns);
  ngtcp2_rtb_remove_expired_lost_pkt(&conn->pktns.rtb, pto, ts);
}

/*
 * select_preferred_version selects the most preferred version.
 * |fallback_version| is chosen if no preference is made, or
 * |preferred_versions| does not include any of |chosen_version| or
 * |available_versions|.  |chosen_version| is treated as an extra
 * other version.
 */
static uint32_t select_preferred_version(const uint32_t *preferred_versions,
                                         size_t preferred_versionslen,
                                         uint32_t chosen_version,
                                         const uint8_t *available_versions,
                                         size_t available_versionslen,
                                         uint32_t fallback_version) {
  size_t i, j;
  const uint8_t *p;
  uint32_t v;

  if (!preferred_versionslen ||
      (!available_versionslen && chosen_version == fallback_version)) {
    return fallback_version;
  }

  for (i = 0; i < preferred_versionslen; ++i) {
    if (preferred_versions[i] == chosen_version) {
      return chosen_version;
    }
    for (j = 0, p = available_versions; j < available_versionslen;
         j += sizeof(uint32_t)) {
      p = ngtcp2_get_uint32(&v, p);

      if (preferred_versions[i] == v) {
        return v;
      }
    }
  }

  return fallback_version;
}

/*
 * conn_client_validate_transport_params validates |params| as client.
 * |params| must be sent with Encrypted Extensions.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_TRANSPORT_PARAM
 *     params contains preferred address but server chose zero-length
 *     connection ID.
 * NGTCP2_ERR_VERSION_NEGOTIATION_FAILURE
 *     Validation against version negotiation parameters failed.
 */
static int
conn_client_validate_transport_params(ngtcp2_conn *conn,
                                      const ngtcp2_transport_params *params) {
  if (!params->original_dcid_present) {
    return NGTCP2_ERR_REQUIRED_TRANSPORT_PARAM;
  }

  if (!ngtcp2_cid_eq(&conn->rcid, &params->original_dcid)) {
    return NGTCP2_ERR_TRANSPORT_PARAM;
  }

  if (conn->flags & NGTCP2_CONN_FLAG_RECV_RETRY) {
    if (!params->retry_scid_present) {
      return NGTCP2_ERR_TRANSPORT_PARAM;
    }
    if (!ngtcp2_cid_eq(&conn->retry_scid, &params->retry_scid)) {
      return NGTCP2_ERR_TRANSPORT_PARAM;
    }
  } else if (params->retry_scid_present) {
    return NGTCP2_ERR_TRANSPORT_PARAM;
  }

  if (params->preferred_addr_present && conn->dcid.current.cid.datalen == 0) {
    return NGTCP2_ERR_TRANSPORT_PARAM;
  }

  if (params->version_info_present) {
    if (conn->negotiated_version != params->version_info.chosen_version) {
      return NGTCP2_ERR_VERSION_NEGOTIATION_FAILURE;
    }

    assert(vneg_available_versions_includes(conn->vneg.available_versions,
                                            conn->vneg.available_versionslen,
                                            conn->negotiated_version));
  } else if (conn->client_chosen_version != conn->negotiated_version) {
    return NGTCP2_ERR_VERSION_NEGOTIATION_FAILURE;
  }

  /* When client reacted upon Version Negotiation */
  if (conn->local.settings.original_version != conn->client_chosen_version) {
    if (!params->version_info_present) {
      assert(conn->client_chosen_version == conn->negotiated_version);

      /* QUIC v1 is treated specially.  If version_info is missing, no
         further validation is necessary.  See
         https://datatracker.ietf.org/doc/html/rfc9368#section-8
       */
      if (conn->client_chosen_version == NGTCP2_PROTO_VER_V1) {
        return 0;
      }

      return NGTCP2_ERR_VERSION_NEGOTIATION_FAILURE;
    }

    /* Server choose original version after Version Negotiation.  RFC
       9368 does not say this particular case, but this smells like
       misbehaved server because server should accept original_version
       in the original connection. */
    if (conn->local.settings.original_version ==
        params->version_info.chosen_version) {
      return NGTCP2_ERR_VERSION_NEGOTIATION_FAILURE;
    }

    /* Check version downgrade on incompatible version negotiation. */
    if (params->version_info.available_versionslen == 0) {
      return NGTCP2_ERR_VERSION_NEGOTIATION_FAILURE;
    }

    if (conn->client_chosen_version !=
        select_preferred_version(conn->vneg.preferred_versions,
                                 conn->vneg.preferred_versionslen,
                                 params->version_info.chosen_version,
                                 params->version_info.available_versions,
                                 params->version_info.available_versionslen,
                                 /* fallback_version = */ 0)) {
      return NGTCP2_ERR_VERSION_NEGOTIATION_FAILURE;
    }
  }

  return 0;
}

uint32_t
ngtcp2_conn_server_negotiate_version(ngtcp2_conn *conn,
                                     const ngtcp2_version_info *version_info) {
  assert(conn->server);
  assert(conn->client_chosen_version == version_info->chosen_version);

  return select_preferred_version(
      conn->vneg.preferred_versions, conn->vneg.preferred_versionslen,
      version_info->chosen_version, version_info->available_versions,
      version_info->available_versionslen, version_info->chosen_version);
}

int ngtcp2_conn_set_remote_transport_params(
    ngtcp2_conn *conn, const ngtcp2_transport_params *params) {
  int rv;

  /* We expect this function is called once per QUIC connection, but
     GnuTLS server seems to call TLS extension callback twice if it
     sends HelloRetryRequest.  In practice, same QUIC transport
     parameters are sent in the 2nd client flight, just returning 0
     would cause no harm. */
  if (conn->flags & NGTCP2_CONN_FLAG_TRANSPORT_PARAM_RECVED) {
    return 0;
  }

  if (!params->initial_scid_present) {
    return NGTCP2_ERR_REQUIRED_TRANSPORT_PARAM;
  }

  /* Assume that ngtcp2_transport_params_decode sets default value if
     active_connection_id_limit is omitted. */
  if (params->active_connection_id_limit <
      NGTCP2_DEFAULT_ACTIVE_CONNECTION_ID_LIMIT) {
    return NGTCP2_ERR_TRANSPORT_PARAM;
  }

  /* We assume that conn->dcid.current.cid is still the initial one.
     This requires that transport parameter must be fed into
     ngtcp2_conn as early as possible. */
  if (!ngtcp2_cid_eq(&conn->dcid.current.cid, &params->initial_scid)) {
    return NGTCP2_ERR_TRANSPORT_PARAM;
  }

  if (params->max_udp_payload_size < NGTCP2_MAX_UDP_PAYLOAD_SIZE) {
    return NGTCP2_ERR_TRANSPORT_PARAM;
  }

  if (conn->server) {
    if (params->original_dcid_present ||
        params->stateless_reset_token_present ||
        params->preferred_addr_present || params->retry_scid_present) {
      return NGTCP2_ERR_TRANSPORT_PARAM;
    }

    if (params->version_info_present) {
      if (!vneg_available_versions_includes(
              params->version_info.available_versions,
              params->version_info.available_versionslen,
              params->version_info.chosen_version)) {
        return NGTCP2_ERR_TRANSPORT_PARAM;
      }

      if (params->version_info.chosen_version != conn->client_chosen_version) {
        return NGTCP2_ERR_VERSION_NEGOTIATION_FAILURE;
      }

      conn->negotiated_version =
          ngtcp2_conn_server_negotiate_version(conn, &params->version_info);
      if (conn->negotiated_version != conn->client_chosen_version) {
        rv = conn_call_version_negotiation(conn, conn->negotiated_version,
                                           &conn->rcid);
        if (rv != 0) {
          return rv;
        }
      }
    } else {
      conn->negotiated_version = conn->client_chosen_version;
    }

    conn->local.transport_params.version_info.chosen_version =
        conn->negotiated_version;

    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_CON,
                    "the negotiated version is 0x%08x",
                    conn->negotiated_version);
  } else {
    rv = conn_client_validate_transport_params(conn, params);
    if (rv != 0) {
      return rv;
    }
  }

  ngtcp2_log_remote_tp(&conn->log, params);

  ngtcp2_qlog_parameters_set_transport_params(&conn->qlog, params, conn->server,
                                              NGTCP2_QLOG_SIDE_REMOTE);

  if ((conn->server && conn->pktns.crypto.tx.ckm) ||
      (!conn->server && conn->pktns.crypto.rx.ckm)) {
    ngtcp2_transport_params_del(conn->remote.transport_params, conn->mem);
    conn->remote.transport_params = NULL;

    rv = ngtcp2_transport_params_copy_new(&conn->remote.transport_params,
                                          params, conn->mem);
    if (rv != 0) {
      return rv;
    }
    conn_sync_stream_id_limit(conn);
    conn->tx.max_offset = conn->remote.transport_params->initial_max_data;
  } else {
    assert(!conn->remote.pending_transport_params);

    rv = ngtcp2_transport_params_copy_new(
        &conn->remote.pending_transport_params, params, conn->mem);
    if (rv != 0) {
      return rv;
    }
  }

  conn->flags |= NGTCP2_CONN_FLAG_TRANSPORT_PARAM_RECVED;

  return 0;
}

int ngtcp2_conn_decode_and_set_remote_transport_params(ngtcp2_conn *conn,
                                                       const uint8_t *data,
                                                       size_t datalen) {
  ngtcp2_transport_params params;
  int rv;

  rv = ngtcp2_transport_params_decode(&params, data, datalen);
  if (rv != 0) {
    return rv;
  }

  return ngtcp2_conn_set_remote_transport_params(conn, &params);
}

const ngtcp2_transport_params *
ngtcp2_conn_get_remote_transport_params(ngtcp2_conn *conn) {
  if (conn->remote.pending_transport_params) {
    return conn->remote.pending_transport_params;
  }

  return conn->remote.transport_params;
}

ngtcp2_ssize ngtcp2_conn_encode_0rtt_transport_params(ngtcp2_conn *conn,
                                                      uint8_t *dest,
                                                      size_t destlen) {
  ngtcp2_transport_params params, *src;

  if (conn->server) {
    src = &conn->local.transport_params;
  } else {
    assert(conn->remote.transport_params);

    src = conn->remote.transport_params;
  }

  ngtcp2_transport_params_default(&params);

  params.initial_max_streams_bidi = src->initial_max_streams_bidi;
  params.initial_max_streams_uni = src->initial_max_streams_uni;
  params.initial_max_stream_data_bidi_local =
      src->initial_max_stream_data_bidi_local;
  params.initial_max_stream_data_bidi_remote =
      src->initial_max_stream_data_bidi_remote;
  params.initial_max_stream_data_uni = src->initial_max_stream_data_uni;
  params.initial_max_data = src->initial_max_data;
  params.active_connection_id_limit = src->active_connection_id_limit;
  params.max_datagram_frame_size = src->max_datagram_frame_size;

  if (conn->server) {
    params.max_idle_timeout = src->max_idle_timeout;
    params.max_udp_payload_size = src->max_udp_payload_size;
    params.disable_active_migration = src->disable_active_migration;
  }

  return ngtcp2_transport_params_encode(dest, destlen, &params);
}

int ngtcp2_conn_decode_and_set_0rtt_transport_params(ngtcp2_conn *conn,
                                                     const uint8_t *data,
                                                     size_t datalen) {
  ngtcp2_transport_params params;
  int rv;

  rv = ngtcp2_transport_params_decode(&params, data, datalen);
  if (rv != 0) {
    return rv;
  }

  return ngtcp2_conn_set_0rtt_remote_transport_params(conn, &params);
}

int ngtcp2_conn_set_0rtt_remote_transport_params(
    ngtcp2_conn *conn, const ngtcp2_transport_params *params) {
  ngtcp2_transport_params *p;

  assert(!conn->server);
  assert(!conn->remote.transport_params);

  /* Assume that all pointer fields in p are NULL */
  p = ngtcp2_mem_calloc(conn->mem, 1, sizeof(*p));
  if (p == NULL) {
    return NGTCP2_ERR_NOMEM;
  }

  conn->remote.transport_params = p;

  ngtcp2_transport_params_default(conn->remote.transport_params);

  p->initial_max_streams_bidi = params->initial_max_streams_bidi;
  p->initial_max_streams_uni = params->initial_max_streams_uni;
  p->initial_max_stream_data_bidi_local =
      params->initial_max_stream_data_bidi_local;
  p->initial_max_stream_data_bidi_remote =
      params->initial_max_stream_data_bidi_remote;
  p->initial_max_stream_data_uni = params->initial_max_stream_data_uni;
  p->initial_max_data = params->initial_max_data;
  /* we might hit garbage, then set the sane default. */
  p->active_connection_id_limit =
      ngtcp2_max(NGTCP2_DEFAULT_ACTIVE_CONNECTION_ID_LIMIT,
                 params->active_connection_id_limit);
  p->max_datagram_frame_size = params->max_datagram_frame_size;

  /* we might hit garbage, then set the sane default. */
  if (params->max_udp_payload_size) {
    p->max_udp_payload_size =
        ngtcp2_max(NGTCP2_MAX_UDP_PAYLOAD_SIZE, params->max_udp_payload_size);
  }

  /* These parameters are treated specially.  If server accepts early
     data, it must not set values for these parameters that are
     smaller than these remembered values. */
  conn->early.transport_params.initial_max_streams_bidi =
      params->initial_max_streams_bidi;
  conn->early.transport_params.initial_max_streams_uni =
      params->initial_max_streams_uni;
  conn->early.transport_params.initial_max_stream_data_bidi_local =
      params->initial_max_stream_data_bidi_local;
  conn->early.transport_params.initial_max_stream_data_bidi_remote =
      params->initial_max_stream_data_bidi_remote;
  conn->early.transport_params.initial_max_stream_data_uni =
      params->initial_max_stream_data_uni;
  conn->early.transport_params.initial_max_data = params->initial_max_data;
  conn->early.transport_params.active_connection_id_limit =
      params->active_connection_id_limit;
  conn->early.transport_params.max_datagram_frame_size =
      params->max_datagram_frame_size;

  conn_sync_stream_id_limit(conn);

  conn->tx.max_offset = p->initial_max_data;

  ngtcp2_qlog_parameters_set_transport_params(&conn->qlog, p, conn->server,
                                              NGTCP2_QLOG_SIDE_REMOTE);

  return 0;
}

int ngtcp2_conn_set_local_transport_params_versioned(
    ngtcp2_conn *conn, int transport_params_version,
    const ngtcp2_transport_params *params) {
  ngtcp2_transport_params paramsbuf;

  params = ngtcp2_transport_params_convert_to_latest(
      &paramsbuf, transport_params_version, params);

  assert(conn->server);
  assert(params->active_connection_id_limit >=
         NGTCP2_DEFAULT_ACTIVE_CONNECTION_ID_LIMIT);
  assert(params->active_connection_id_limit <= NGTCP2_MAX_DCID_POOL_SIZE);

  if (conn->hs_pktns == NULL || conn->hs_pktns->crypto.tx.ckm) {
    return NGTCP2_ERR_INVALID_STATE;
  }

  conn_set_local_transport_params(conn, params);

  return 0;
}

int ngtcp2_conn_commit_local_transport_params(ngtcp2_conn *conn) {
  const ngtcp2_mem *mem = conn->mem;
  ngtcp2_transport_params *params = &conn->local.transport_params;
  ngtcp2_scid *scident;
  int rv;

  assert(1 == ngtcp2_ksl_len(&conn->scid.set));

  params->initial_scid = conn->oscid;
  params->initial_scid_present = 1;

  if (conn->oscid.datalen == 0) {
    params->preferred_addr_present = 0;
  }

  if (conn->server && params->preferred_addr_present) {
    scident = ngtcp2_mem_malloc(mem, sizeof(*scident));
    if (scident == NULL) {
      return NGTCP2_ERR_NOMEM;
    }

    ngtcp2_scid_init(scident, 1, &params->preferred_addr.cid);

    rv = ngtcp2_ksl_insert(&conn->scid.set, NULL, &scident->cid, scident);
    if (rv != 0) {
      ngtcp2_mem_free(mem, scident);
      return rv;
    }

    conn->scid.last_seq = 1;
  }

  conn->rx.window = conn->rx.unsent_max_offset = conn->rx.max_offset =
      params->initial_max_data;
  conn->remote.bidi.unsent_max_streams = params->initial_max_streams_bidi;
  conn->remote.bidi.max_streams = params->initial_max_streams_bidi;
  conn->remote.uni.unsent_max_streams = params->initial_max_streams_uni;
  conn->remote.uni.max_streams = params->initial_max_streams_uni;

  conn->flags |= NGTCP2_CONN_FLAG_LOCAL_TRANSPORT_PARAMS_COMMITTED;

  ngtcp2_qlog_parameters_set_transport_params(&conn->qlog, params, conn->server,
                                              NGTCP2_QLOG_SIDE_LOCAL);

  return 0;
}

const ngtcp2_transport_params *
ngtcp2_conn_get_local_transport_params(ngtcp2_conn *conn) {
  return &conn->local.transport_params;
}

ngtcp2_ssize ngtcp2_conn_encode_local_transport_params(ngtcp2_conn *conn,
                                                       uint8_t *dest,
                                                       size_t destlen) {
  return ngtcp2_transport_params_encode(dest, destlen,
                                        &conn->local.transport_params);
}

int ngtcp2_conn_open_bidi_stream(ngtcp2_conn *conn, int64_t *pstream_id,
                                 void *stream_user_data) {
  int rv;
  ngtcp2_strm *strm;

  if (ngtcp2_conn_get_streams_bidi_left(conn) == 0) {
    return NGTCP2_ERR_STREAM_ID_BLOCKED;
  }

  strm = ngtcp2_objalloc_strm_get(&conn->strm_objalloc);
  if (strm == NULL) {
    return NGTCP2_ERR_NOMEM;
  }

  rv = ngtcp2_conn_init_stream(conn, strm, conn->local.bidi.next_stream_id,
                               stream_user_data);
  if (rv != 0) {
    ngtcp2_objalloc_strm_release(&conn->strm_objalloc, strm);
    return rv;
  }

  *pstream_id = conn->local.bidi.next_stream_id;
  conn->local.bidi.next_stream_id += 4;

  return 0;
}

int ngtcp2_conn_open_uni_stream(ngtcp2_conn *conn, int64_t *pstream_id,
                                void *stream_user_data) {
  int rv;
  ngtcp2_strm *strm;

  if (ngtcp2_conn_get_streams_uni_left(conn) == 0) {
    return NGTCP2_ERR_STREAM_ID_BLOCKED;
  }

  strm = ngtcp2_objalloc_strm_get(&conn->strm_objalloc);
  if (strm == NULL) {
    return NGTCP2_ERR_NOMEM;
  }

  rv = ngtcp2_conn_init_stream(conn, strm, conn->local.uni.next_stream_id,
                               stream_user_data);
  if (rv != 0) {
    ngtcp2_objalloc_strm_release(&conn->strm_objalloc, strm);
    return rv;
  }
  ngtcp2_strm_shutdown(strm, NGTCP2_STRM_FLAG_SHUT_RD);

  *pstream_id = conn->local.uni.next_stream_id;
  conn->local.uni.next_stream_id += 4;

  return 0;
}

ngtcp2_strm *ngtcp2_conn_find_stream(ngtcp2_conn *conn, int64_t stream_id) {
  return ngtcp2_map_find(&conn->strms, (uint64_t)stream_id);
}

ngtcp2_ssize ngtcp2_conn_write_stream_versioned(
    ngtcp2_conn *conn, ngtcp2_path *path, int pkt_info_version,
    ngtcp2_pkt_info *pi, uint8_t *dest, size_t destlen, ngtcp2_ssize *pdatalen,
    uint32_t flags, int64_t stream_id, const uint8_t *data, size_t datalen,
    ngtcp2_tstamp ts) {
  ngtcp2_vec datav;

  datav.len = datalen;
  datav.base = (uint8_t *)data;

  return ngtcp2_conn_writev_stream_versioned(conn, path, pkt_info_version, pi,
                                             dest, destlen, pdatalen, flags,
                                             stream_id, &datav, 1, ts);
}

static ngtcp2_ssize conn_write_vmsg_wrapper(ngtcp2_conn *conn,
                                            ngtcp2_path *path,
                                            int pkt_info_version,
                                            ngtcp2_pkt_info *pi, uint8_t *dest,
                                            size_t destlen, ngtcp2_vmsg *vmsg,
                                            ngtcp2_tstamp ts) {
  ngtcp2_conn_stat *cstat = &conn->cstat;
  ngtcp2_ssize nwrite;

  nwrite = ngtcp2_conn_write_vmsg(conn, path, pkt_info_version, pi, dest,
                                  destlen, vmsg, ts);
  if (nwrite < 0) {
    return nwrite;
  }

  if (cstat->bytes_in_flight >= cstat->cwnd) {
    conn->rst.is_cwnd_limited = 1;
  }

  if (nwrite == 0 && cstat->bytes_in_flight < cstat->cwnd) {
    conn->rst.app_limited = conn->rst.delivered + cstat->bytes_in_flight;

    if (conn->rst.app_limited == 0) {
      conn->rst.app_limited = cstat->max_tx_udp_payload_size;
    }
  }

  return nwrite;
}

ngtcp2_ssize ngtcp2_conn_writev_stream_versioned(
    ngtcp2_conn *conn, ngtcp2_path *path, int pkt_info_version,
    ngtcp2_pkt_info *pi, uint8_t *dest, size_t destlen, ngtcp2_ssize *pdatalen,
    uint32_t flags, int64_t stream_id, const ngtcp2_vec *datav, size_t datavcnt,
    ngtcp2_tstamp ts) {
  ngtcp2_vmsg vmsg, *pvmsg;
  ngtcp2_strm *strm;
  int64_t datalen;

  if (pdatalen) {
    *pdatalen = -1;
  }

  if (stream_id != -1) {
    strm = ngtcp2_conn_find_stream(conn, stream_id);
    if (strm == NULL) {
      return NGTCP2_ERR_STREAM_NOT_FOUND;
    }

    if (strm->flags & NGTCP2_STRM_FLAG_SHUT_WR) {
      return NGTCP2_ERR_STREAM_SHUT_WR;
    }

    datalen = ngtcp2_vec_len_varint(datav, datavcnt);
    if (datalen == -1) {
      return NGTCP2_ERR_INVALID_ARGUMENT;
    }

    if ((uint64_t)datalen > NGTCP2_MAX_VARINT - strm->tx.offset ||
        (uint64_t)datalen > NGTCP2_MAX_VARINT - conn->tx.offset) {
      return NGTCP2_ERR_INVALID_ARGUMENT;
    }

    vmsg.type = NGTCP2_VMSG_TYPE_STREAM;
    vmsg.stream.strm = strm;
    vmsg.stream.flags = flags;
    vmsg.stream.data = datav;
    vmsg.stream.datacnt = datavcnt;
    vmsg.stream.pdatalen = pdatalen;

    pvmsg = &vmsg;
  } else {
    pvmsg = NULL;
  }

  return conn_write_vmsg_wrapper(conn, path, pkt_info_version, pi, dest,
                                 destlen, pvmsg, ts);
}

ngtcp2_ssize ngtcp2_conn_write_datagram_versioned(
    ngtcp2_conn *conn, ngtcp2_path *path, int pkt_info_version,
    ngtcp2_pkt_info *pi, uint8_t *dest, size_t destlen, int *paccepted,
    uint32_t flags, uint64_t dgram_id, const uint8_t *data, size_t datalen,
    ngtcp2_tstamp ts) {
  ngtcp2_vec datav;

  datav.len = datalen;
  datav.base = (uint8_t *)data;

  return ngtcp2_conn_writev_datagram_versioned(conn, path, pkt_info_version, pi,
                                               dest, destlen, paccepted, flags,
                                               dgram_id, &datav, 1, ts);
}

ngtcp2_ssize ngtcp2_conn_writev_datagram_versioned(
    ngtcp2_conn *conn, ngtcp2_path *path, int pkt_info_version,
    ngtcp2_pkt_info *pi, uint8_t *dest, size_t destlen, int *paccepted,
    uint32_t flags, uint64_t dgram_id, const ngtcp2_vec *datav, size_t datavcnt,
    ngtcp2_tstamp ts) {
  ngtcp2_vmsg vmsg;
  int64_t datalen;

  if (paccepted) {
    *paccepted = 0;
  }

  if (conn->remote.transport_params == NULL ||
      conn->remote.transport_params->max_datagram_frame_size == 0) {
    return NGTCP2_ERR_INVALID_STATE;
  }

  datalen = ngtcp2_vec_len_varint(datav, datavcnt);
  if (datalen == -1
#if UINT64_MAX > SIZE_MAX
      || (uint64_t)datalen > SIZE_MAX
#endif /* UINT64_MAX > SIZE_MAX */
  ) {
    return NGTCP2_ERR_INVALID_ARGUMENT;
  }

  if (conn->remote.transport_params->max_datagram_frame_size <
      ngtcp2_pkt_datagram_framelen((size_t)datalen)) {
    return NGTCP2_ERR_INVALID_ARGUMENT;
  }

  vmsg.type = NGTCP2_VMSG_TYPE_DATAGRAM;
  vmsg.datagram.dgram_id = dgram_id;
  vmsg.datagram.flags = flags;
  vmsg.datagram.data = datav;
  vmsg.datagram.datacnt = datavcnt;
  vmsg.datagram.paccepted = paccepted;

  return conn_write_vmsg_wrapper(conn, path, pkt_info_version, pi, dest,
                                 destlen, &vmsg, ts);
}

ngtcp2_ssize ngtcp2_conn_write_vmsg(ngtcp2_conn *conn, ngtcp2_path *path,
                                    int pkt_info_version, ngtcp2_pkt_info *pi,
                                    uint8_t *dest, size_t destlen,
                                    ngtcp2_vmsg *vmsg, ngtcp2_tstamp ts) {
  ngtcp2_ssize nwrite;
  size_t origlen;
  size_t origdestlen = destlen;
  int rv;
  uint8_t wflags = NGTCP2_WRITE_PKT_FLAG_NONE;
  int ppe_pending = (conn->flags & NGTCP2_CONN_FLAG_PPE_PENDING) != 0;
  ngtcp2_conn_stat *cstat = &conn->cstat;
  ngtcp2_ssize res = 0;
  uint64_t server_tx_left;
  int64_t prev_in_pkt_num = -1;
  ngtcp2_ksl_it it;
  ngtcp2_rtb_entry *rtbent;
  (void)pkt_info_version;

  conn_update_timestamp(conn, ts);

  if (path) {
    ngtcp2_path_copy(path, &conn->dcid.current.ps.path);
  }

  origlen = destlen =
      conn_shape_udp_payload(conn, &conn->dcid.current, destlen);

  if (!ppe_pending && pi) {
    pi->ecn = NGTCP2_ECN_NOT_ECT;
  }

  switch (conn->state) {
  case NGTCP2_CS_CLIENT_INITIAL:
  case NGTCP2_CS_CLIENT_WAIT_HANDSHAKE:
    if (!conn_pacing_pkt_tx_allowed(conn, ts)) {
      assert(!ppe_pending);

      return conn_write_handshake_ack_pkts(conn, pi, dest, origlen, ts);
    }

    nwrite = conn_client_write_handshake(conn, pi, dest, destlen, vmsg, ts);
    /* We might be unable to write a packet because of depletion of
       congestion window budget, perhaps due to packet loss that
       shrinks the window drastically. */
    if (nwrite <= 0) {
      return nwrite;
    }
    if (conn->state != NGTCP2_CS_POST_HANDSHAKE) {
      return nwrite;
    }

    assert(nwrite);
    assert(dest[0] & NGTCP2_HEADER_FORM_BIT);
    assert(conn->negotiated_version);

    if (ngtcp2_pkt_get_type_long(conn->negotiated_version, dest[0]) ==
        NGTCP2_PKT_INITIAL) {
      /* We have added padding already, but in that case, there is no
         space left to write 1RTT packet. */
      wflags |= NGTCP2_WRITE_PKT_FLAG_REQUIRE_PADDING;
    }

    res = nwrite;
    dest += nwrite;
    destlen -= (size_t)nwrite;
    /* Break here so that we can coalesces 1RTT packet. */
    break;
  case NGTCP2_CS_SERVER_INITIAL:
  case NGTCP2_CS_SERVER_WAIT_HANDSHAKE:
    if (!conn_pacing_pkt_tx_allowed(conn, ts)) {
      assert(!ppe_pending);

      if (!(conn->dcid.current.flags & NGTCP2_DCID_FLAG_PATH_VALIDATED)) {
        server_tx_left = conn_server_tx_left(conn, &conn->dcid.current);
        if (server_tx_left == 0) {
          return 0;
        }

        origlen = (size_t)ngtcp2_min((uint64_t)origlen, server_tx_left);
      }

      return conn_write_handshake_ack_pkts(conn, pi, dest, origlen, ts);
    }

    if (!ppe_pending) {
      if (!(conn->dcid.current.flags & NGTCP2_DCID_FLAG_PATH_VALIDATED)) {
        server_tx_left = conn_server_tx_left(conn, &conn->dcid.current);
        if (server_tx_left == 0) {
          if (cstat->loss_detection_timer != UINT64_MAX) {
            ngtcp2_log_info(
                &conn->log, NGTCP2_LOG_EVENT_LDC,
                "loss detection timer canceled due to amplification limit");
            cstat->loss_detection_timer = UINT64_MAX;
          }

          return 0;
        }

        destlen = (size_t)ngtcp2_min((uint64_t)destlen, server_tx_left);
      }

      if (conn->in_pktns) {
        it = ngtcp2_rtb_head(&conn->in_pktns->rtb);
        if (!ngtcp2_ksl_it_end(&it)) {
          rtbent = ngtcp2_ksl_it_get(&it);
          prev_in_pkt_num = rtbent->hd.pkt_num;
        }
      }

      nwrite = conn_write_handshake(conn, pi, dest, destlen,
                                    /* write_datalen = */ 0, ts);
      if (nwrite < 0) {
        return nwrite;
      }

      res = nwrite;
      dest += nwrite;
      destlen -= (size_t)nwrite;

      if (conn->in_pktns && nwrite > 0) {
        it = ngtcp2_rtb_head(&conn->in_pktns->rtb);
        if (!ngtcp2_ksl_it_end(&it)) {
          rtbent = ngtcp2_ksl_it_get(&it);
          if (rtbent->hd.pkt_num != prev_in_pkt_num &&
              (rtbent->flags & NGTCP2_RTB_ENTRY_FLAG_ACK_ELICITING)) {
            /* We have added padding already, but in that case, there
               is no space left to write 1RTT packet. */
            wflags |= NGTCP2_WRITE_PKT_FLAG_REQUIRE_PADDING;
          }
        }
      }
    }
    if (conn->pktns.crypto.tx.ckm == NULL) {
      return res;
    }
    break;
  case NGTCP2_CS_POST_HANDSHAKE:
    if (!conn_pacing_pkt_tx_allowed(conn, ts)) {
      assert(!ppe_pending);

      if (conn->server &&
          !(conn->dcid.current.flags & NGTCP2_DCID_FLAG_PATH_VALIDATED)) {
        server_tx_left = conn_server_tx_left(conn, &conn->dcid.current);
        if (server_tx_left == 0) {
          return 0;
        }

        origlen = (size_t)ngtcp2_min((uint64_t)origlen, server_tx_left);
      }

      return conn_write_ack_pkt(conn, pi, dest, origlen, NGTCP2_PKT_1RTT, ts);
    }

    break;
  case NGTCP2_CS_CLOSING:
    return NGTCP2_ERR_CLOSING;
  case NGTCP2_CS_DRAINING:
    return NGTCP2_ERR_DRAINING;
  default:
    return 0;
  }

  assert(conn->pktns.crypto.tx.ckm);

  if (conn_check_pkt_num_exhausted(conn)) {
    return NGTCP2_ERR_PKT_NUM_EXHAUSTED;
  }

  if (vmsg) {
    switch (vmsg->type) {
    case NGTCP2_VMSG_TYPE_STREAM:
      if (vmsg->stream.flags & NGTCP2_WRITE_STREAM_FLAG_MORE) {
        wflags |= NGTCP2_WRITE_PKT_FLAG_MORE;
      }
      break;
    case NGTCP2_VMSG_TYPE_DATAGRAM:
      if (vmsg->datagram.flags & NGTCP2_WRITE_DATAGRAM_FLAG_MORE) {
        wflags |= NGTCP2_WRITE_PKT_FLAG_MORE;
      }
      break;
    default:
      break;
    }
  }

  if (ppe_pending) {
    res = conn->pkt.hs_spktlen;
    if (conn->pkt.require_padding) {
      wflags |= NGTCP2_WRITE_PKT_FLAG_REQUIRE_PADDING;
    }
    /* dest and destlen have already been adjusted in ppe in the first
       run.  They are adjusted for probe packet later. */
    nwrite = conn_write_pkt(conn, pi, dest, destlen, vmsg, NGTCP2_PKT_1RTT,
                            wflags, ts);
    goto fin;
  } else {
    conn->pkt.require_padding =
        (wflags & NGTCP2_WRITE_PKT_FLAG_REQUIRE_PADDING);

    if (conn->state == NGTCP2_CS_POST_HANDSHAKE) {
      rv = conn_prepare_key_update(conn, ts);
      if (rv != 0) {
        return rv;
      }
    }

    if (!conn->pktns.rtb.probe_pkt_left && conn_cwnd_is_zero(conn)) {
      destlen = 0;
    } else {
      if (res == 0) {
        nwrite =
            conn_write_path_response(conn, path, pi, dest, origdestlen, ts);
        if (nwrite) {
          goto fin;
        }

        if (conn->pv) {
          nwrite =
              conn_write_path_challenge(conn, path, pi, dest, origdestlen, ts);
          if (nwrite) {
            goto fin;
          }
        }

        if (conn->pmtud &&
            (conn->dcid.current.flags & NGTCP2_DCID_FLAG_PATH_VALIDATED) &&
            (!conn->hs_pktns ||
             ngtcp2_strm_streamfrq_empty(&conn->hs_pktns->crypto.strm))) {
          nwrite = conn_write_pmtud_probe(conn, pi, dest, origdestlen, ts);
          if (nwrite) {
            goto fin;
          }
        }
      }
    }

    if (conn->server &&
        !(conn->dcid.current.flags & NGTCP2_DCID_FLAG_PATH_VALIDATED)) {
      server_tx_left = conn_server_tx_left(conn, &conn->dcid.current);
      origlen = (size_t)ngtcp2_min((uint64_t)origlen, server_tx_left);
      destlen = (size_t)ngtcp2_min((uint64_t)destlen, server_tx_left);

      if (server_tx_left == 0 &&
          conn->cstat.loss_detection_timer != UINT64_MAX) {
        ngtcp2_log_info(
            &conn->log, NGTCP2_LOG_EVENT_LDC,
            "loss detection timer canceled due to amplification limit");
        conn->cstat.loss_detection_timer = UINT64_MAX;
      }
    }
  }

  if (res == 0) {
    if (conn_handshake_remnants_left(conn)) {
      if (conn_handshake_probe_left(conn) ||
          /* Allow exceeding CWND if an Handshake packet needs to be
             sent in order to avoid dead lock.  In some situation,
             typically for client, 1 RTT packets may occupy in-flight
             bytes (e.g., some large requests and PMTUD), and
             Handshake packet loss shrinks CWND, and we may get in the
             situation that we are unable to send Handshake packet. */
          (conn->hs_pktns->rtb.num_pto_eliciting == 0 &&
           !ngtcp2_strm_streamfrq_empty(&conn->hs_pktns->crypto.strm))) {
        destlen = origlen;
      }
      nwrite = conn_write_handshake_pkts(conn, pi, dest, destlen,
                                         /* write_datalen = */ 0, ts);
      if (nwrite < 0) {
        return nwrite;
      }
      if (nwrite > 0) {
        res = nwrite;
        dest += nwrite;
        destlen -= (size_t)nwrite;
      } else if (destlen == 0) {
        res = conn_write_handshake_ack_pkts(conn, pi, dest, origlen, ts);
        if (res) {
          return res;
        }
      }
    }
  }

  if (conn->pktns.rtb.probe_pkt_left) {
    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_CON,
                    "transmit probe pkt left=%zu",
                    conn->pktns.rtb.probe_pkt_left);

    nwrite = conn_write_pkt(conn, pi, dest, destlen, vmsg, NGTCP2_PKT_1RTT,
                            wflags, ts);

    goto fin;
  }

  nwrite = conn_write_pkt(conn, pi, dest, destlen, vmsg, NGTCP2_PKT_1RTT,
                          wflags, ts);
  if (nwrite) {
    assert(nwrite != NGTCP2_ERR_NOBUF);
    goto fin;
  }

  if (res == 0) {
    nwrite = conn_write_ack_pkt(conn, pi, dest, origlen, NGTCP2_PKT_1RTT, ts);
  }

fin:
  if (nwrite >= 0) {
    res += nwrite;
    return res;
  }

  switch (nwrite) {
  case NGTCP2_ERR_STREAM_DATA_BLOCKED:
    if (!(wflags & NGTCP2_WRITE_PKT_FLAG_MORE)) {
      if (res) {
        return res;
      }

      break;
    }
    /* fall through */
  case NGTCP2_ERR_WRITE_MORE:
    conn->pkt.hs_spktlen = res;
    break;
  }

  return nwrite;
}

static ngtcp2_ssize
conn_write_connection_close(ngtcp2_conn *conn, ngtcp2_pkt_info *pi,
                            uint8_t *dest, size_t destlen, uint8_t pkt_type,
                            uint64_t error_code, const uint8_t *reason,
                            size_t reasonlen, ngtcp2_tstamp ts) {
  ngtcp2_pktns *in_pktns = conn->in_pktns;
  ngtcp2_pktns *hs_pktns = conn->hs_pktns;
  ngtcp2_ssize res = 0, nwrite;
  ngtcp2_frame fr;
  uint8_t flags = NGTCP2_WRITE_PKT_FLAG_NONE;

  fr.type = NGTCP2_FRAME_CONNECTION_CLOSE;
  fr.connection_close.error_code = error_code;
  fr.connection_close.frame_type = 0;
  fr.connection_close.reasonlen = reasonlen;
  fr.connection_close.reason = (uint8_t *)reason;

  if (!(conn->flags & NGTCP2_CONN_FLAG_HANDSHAKE_CONFIRMED) &&
      pkt_type != NGTCP2_PKT_INITIAL) {
    if (in_pktns && conn->server) {
      nwrite = ngtcp2_conn_write_single_frame_pkt(
          conn, pi, dest, destlen, NGTCP2_PKT_INITIAL,
          NGTCP2_WRITE_PKT_FLAG_NONE, &conn->dcid.current.cid, &fr,
          NGTCP2_RTB_ENTRY_FLAG_NONE, NULL, ts);
      if (nwrite < 0) {
        return nwrite;
      }

      dest += nwrite;
      destlen -= (size_t)nwrite;
      res += nwrite;
    }

    if (pkt_type != NGTCP2_PKT_HANDSHAKE && hs_pktns &&
        hs_pktns->crypto.tx.ckm) {
      nwrite = ngtcp2_conn_write_single_frame_pkt(
          conn, pi, dest, destlen, NGTCP2_PKT_HANDSHAKE,
          NGTCP2_WRITE_PKT_FLAG_NONE, &conn->dcid.current.cid, &fr,
          NGTCP2_RTB_ENTRY_FLAG_NONE, NULL, ts);
      if (nwrite < 0) {
        return nwrite;
      }

      dest += nwrite;
      destlen -= (size_t)nwrite;
      res += nwrite;
    }
  }

  if (!conn->server && pkt_type == NGTCP2_PKT_INITIAL) {
    flags = NGTCP2_WRITE_PKT_FLAG_REQUIRE_PADDING;
  }

  nwrite = ngtcp2_conn_write_single_frame_pkt(
      conn, pi, dest, destlen, pkt_type, flags, &conn->dcid.current.cid, &fr,
      NGTCP2_RTB_ENTRY_FLAG_NONE, NULL, ts);

  if (nwrite < 0) {
    return nwrite;
  }

  res += nwrite;

  if (res == 0) {
    return NGTCP2_ERR_NOBUF;
  }

  return res;
}

ngtcp2_ssize ngtcp2_conn_write_connection_close_pkt(
    ngtcp2_conn *conn, ngtcp2_path *path, ngtcp2_pkt_info *pi, uint8_t *dest,
    size_t destlen, uint64_t error_code, const uint8_t *reason,
    size_t reasonlen, ngtcp2_tstamp ts) {
  ngtcp2_pktns *in_pktns = conn->in_pktns;
  ngtcp2_pktns *hs_pktns = conn->hs_pktns;
  uint8_t pkt_type;
  ngtcp2_ssize nwrite;
  uint64_t server_tx_left;

  if (conn_check_pkt_num_exhausted(conn)) {
    return NGTCP2_ERR_PKT_NUM_EXHAUSTED;
  }

  switch (conn->state) {
  case NGTCP2_CS_CLIENT_INITIAL:
    return NGTCP2_ERR_INVALID_STATE;
  case NGTCP2_CS_CLOSING:
  case NGTCP2_CS_DRAINING:
    return 0;
  default:
    break;
  }

  if (path) {
    ngtcp2_path_copy(path, &conn->dcid.current.ps.path);
  }

  destlen = conn_shape_udp_payload(conn, &conn->dcid.current, destlen);

  if (pi) {
    pi->ecn = NGTCP2_ECN_NOT_ECT;
  }

  if (conn->server) {
    server_tx_left = conn_server_tx_left(conn, &conn->dcid.current);
    destlen = (size_t)ngtcp2_min((uint64_t)destlen, server_tx_left);
  }

  if (conn->state == NGTCP2_CS_POST_HANDSHAKE ||
      (conn->server && conn->pktns.crypto.tx.ckm)) {
    pkt_type = NGTCP2_PKT_1RTT;
  } else if (hs_pktns && hs_pktns->crypto.tx.ckm) {
    pkt_type = NGTCP2_PKT_HANDSHAKE;
  } else if (in_pktns && in_pktns->crypto.tx.ckm) {
    pkt_type = NGTCP2_PKT_INITIAL;
  } else {
    /* This branch is taken if server has not read any Initial packet
       from client. */
    return NGTCP2_ERR_INVALID_STATE;
  }

  nwrite = conn_write_connection_close(conn, pi, dest, destlen, pkt_type,
                                       error_code, reason, reasonlen, ts);
  if (nwrite < 0) {
    return nwrite;
  }

  conn->state = NGTCP2_CS_CLOSING;

  return nwrite;
}

ngtcp2_ssize ngtcp2_conn_write_application_close_pkt(
    ngtcp2_conn *conn, ngtcp2_path *path, ngtcp2_pkt_info *pi, uint8_t *dest,
    size_t destlen, uint64_t app_error_code, const uint8_t *reason,
    size_t reasonlen, ngtcp2_tstamp ts) {
  ngtcp2_ssize nwrite;
  ngtcp2_ssize res = 0;
  ngtcp2_frame fr;
  uint64_t server_tx_left;

  if (conn_check_pkt_num_exhausted(conn)) {
    return NGTCP2_ERR_PKT_NUM_EXHAUSTED;
  }

  switch (conn->state) {
  case NGTCP2_CS_CLIENT_INITIAL:
    return NGTCP2_ERR_INVALID_STATE;
  case NGTCP2_CS_CLOSING:
  case NGTCP2_CS_DRAINING:
    return 0;
  default:
    break;
  }

  if (path) {
    ngtcp2_path_copy(path, &conn->dcid.current.ps.path);
  }

  destlen = conn_shape_udp_payload(conn, &conn->dcid.current, destlen);

  if (pi) {
    pi->ecn = NGTCP2_ECN_NOT_ECT;
  }

  if (conn->server) {
    server_tx_left = conn_server_tx_left(conn, &conn->dcid.current);
    destlen = (size_t)ngtcp2_min((uint64_t)destlen, server_tx_left);
  }

  if (!(conn->flags & NGTCP2_CONN_FLAG_HANDSHAKE_CONFIRMED)) {
    nwrite = conn_write_connection_close(conn, pi, dest, destlen,
                                         conn->hs_pktns->crypto.tx.ckm
                                             ? NGTCP2_PKT_HANDSHAKE
                                             : NGTCP2_PKT_INITIAL,
                                         NGTCP2_APPLICATION_ERROR, NULL, 0, ts);
    if (nwrite < 0) {
      return nwrite;
    }
    res = nwrite;
    dest += nwrite;
    destlen -= (size_t)nwrite;
  }

  if (conn->state != NGTCP2_CS_POST_HANDSHAKE) {
    assert(res);

    if (!conn->server || !conn->pktns.crypto.tx.ckm) {
      return res;
    }
  }

  assert(conn->pktns.crypto.tx.ckm);

  fr.type = NGTCP2_FRAME_CONNECTION_CLOSE_APP;
  fr.connection_close.error_code = app_error_code;
  fr.connection_close.frame_type = 0;
  fr.connection_close.reasonlen = reasonlen;
  fr.connection_close.reason = (uint8_t *)reason;

  nwrite = ngtcp2_conn_write_single_frame_pkt(
      conn, pi, dest, destlen, NGTCP2_PKT_1RTT, NGTCP2_WRITE_PKT_FLAG_NONE,
      &conn->dcid.current.cid, &fr, NGTCP2_RTB_ENTRY_FLAG_NONE, NULL, ts);

  if (nwrite < 0) {
    return nwrite;
  }

  res += nwrite;

  if (res == 0) {
    return NGTCP2_ERR_NOBUF;
  }

  conn->state = NGTCP2_CS_CLOSING;

  return res;
}

static void ccerr_init(ngtcp2_ccerr *ccerr, ngtcp2_ccerr_type type,
                       uint64_t error_code, const uint8_t *reason,
                       size_t reasonlen) {
  ccerr->type = type;
  ccerr->error_code = error_code;
  ccerr->frame_type = 0;
  ccerr->reason = (uint8_t *)reason;
  ccerr->reasonlen = reasonlen;
}

void ngtcp2_ccerr_default(ngtcp2_ccerr *ccerr) {
  ccerr_init(ccerr, NGTCP2_CCERR_TYPE_TRANSPORT, NGTCP2_NO_ERROR, NULL, 0);
}

void ngtcp2_ccerr_set_transport_error(ngtcp2_ccerr *ccerr, uint64_t error_code,
                                      const uint8_t *reason, size_t reasonlen) {
  ccerr_init(ccerr, NGTCP2_CCERR_TYPE_TRANSPORT, error_code, reason, reasonlen);
}

void ngtcp2_ccerr_set_liberr(ngtcp2_ccerr *ccerr, int liberr,
                             const uint8_t *reason, size_t reasonlen) {
  switch (liberr) {
  case NGTCP2_ERR_RECV_VERSION_NEGOTIATION:
    ccerr_init(ccerr, NGTCP2_CCERR_TYPE_VERSION_NEGOTIATION, NGTCP2_NO_ERROR,
               reason, reasonlen);

    return;
  case NGTCP2_ERR_IDLE_CLOSE:
    ccerr_init(ccerr, NGTCP2_CCERR_TYPE_IDLE_CLOSE, NGTCP2_NO_ERROR, reason,
               reasonlen);

    return;
  };

  ngtcp2_ccerr_set_transport_error(
      ccerr, ngtcp2_err_infer_quic_transport_error_code(liberr), reason,
      reasonlen);
}

void ngtcp2_ccerr_set_tls_alert(ngtcp2_ccerr *ccerr, uint8_t tls_alert,
                                const uint8_t *reason, size_t reasonlen) {
  ngtcp2_ccerr_set_transport_error(ccerr, NGTCP2_CRYPTO_ERROR | tls_alert,
                                   reason, reasonlen);
}

void ngtcp2_ccerr_set_application_error(ngtcp2_ccerr *ccerr,
                                        uint64_t error_code,
                                        const uint8_t *reason,
                                        size_t reasonlen) {
  ccerr_init(ccerr, NGTCP2_CCERR_TYPE_APPLICATION, error_code, reason,
             reasonlen);
}

ngtcp2_ssize ngtcp2_conn_write_connection_close_versioned(
    ngtcp2_conn *conn, ngtcp2_path *path, int pkt_info_version,
    ngtcp2_pkt_info *pi, uint8_t *dest, size_t destlen,
    const ngtcp2_ccerr *ccerr, ngtcp2_tstamp ts) {
  (void)pkt_info_version;

  conn_update_timestamp(conn, ts);

  switch (ccerr->type) {
  case NGTCP2_CCERR_TYPE_TRANSPORT:
    return ngtcp2_conn_write_connection_close_pkt(
        conn, path, pi, dest, destlen, ccerr->error_code, ccerr->reason,
        ccerr->reasonlen, ts);
  case NGTCP2_CCERR_TYPE_APPLICATION:
    return ngtcp2_conn_write_application_close_pkt(
        conn, path, pi, dest, destlen, ccerr->error_code, ccerr->reason,
        ccerr->reasonlen, ts);
  default:
    return 0;
  }
}

int ngtcp2_conn_in_closing_period(ngtcp2_conn *conn) {
  return conn->state == NGTCP2_CS_CLOSING;
}

int ngtcp2_conn_in_draining_period(ngtcp2_conn *conn) {
  return conn->state == NGTCP2_CS_DRAINING;
}

int ngtcp2_conn_close_stream(ngtcp2_conn *conn, ngtcp2_strm *strm) {
  int rv;

  rv = conn_call_stream_close(conn, strm);
  if (rv != 0) {
    return rv;
  }

  rv = ngtcp2_map_remove(&conn->strms, (ngtcp2_map_key_type)strm->stream_id);
  if (rv != 0) {
    assert(rv != NGTCP2_ERR_INVALID_ARGUMENT);
    return rv;
  }

  if (ngtcp2_strm_is_tx_queued(strm)) {
    ngtcp2_pq_remove(&conn->tx.strmq, &strm->pe);
  }

  ngtcp2_strm_free(strm);
  ngtcp2_objalloc_strm_release(&conn->strm_objalloc, strm);

  return 0;
}

int ngtcp2_conn_close_stream_if_shut_rdwr(ngtcp2_conn *conn,
                                          ngtcp2_strm *strm) {
  if ((strm->flags & NGTCP2_STRM_FLAG_SHUT_RDWR) ==
          NGTCP2_STRM_FLAG_SHUT_RDWR &&
      ((strm->flags & NGTCP2_STRM_FLAG_RESET_STREAM_RECVED) ||
       ngtcp2_strm_rx_offset(strm) == strm->rx.last_offset) &&
      (((strm->flags & NGTCP2_STRM_FLAG_RESET_STREAM) &&
        (strm->flags & NGTCP2_STRM_FLAG_RESET_STREAM_ACKED)) ||
       ngtcp2_strm_is_all_tx_data_fin_acked(strm))) {
    return ngtcp2_conn_close_stream(conn, strm);
  }
  return 0;
}

/*
 * conn_shutdown_stream_write closes send stream with error code
 * |app_error_code|.  RESET_STREAM frame is scheduled.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 */
static int conn_shutdown_stream_write(ngtcp2_conn *conn, ngtcp2_strm *strm,
                                      uint64_t app_error_code) {
  ngtcp2_strm_set_app_error_code(strm, app_error_code);

  if ((strm->flags & NGTCP2_STRM_FLAG_RESET_STREAM) ||
      ngtcp2_strm_is_all_tx_data_fin_acked(strm)) {
    return 0;
  }

  /* Set this flag so that we don't accidentally send DATA to this
     stream. */
  strm->flags |= NGTCP2_STRM_FLAG_SHUT_WR | NGTCP2_STRM_FLAG_RESET_STREAM;

  ngtcp2_strm_streamfrq_clear(strm);

  return conn_reset_stream(conn, strm, app_error_code);
}

/*
 * conn_shutdown_stream_read closes read stream with error code
 * |app_error_code|.  STOP_SENDING frame is scheduled.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 */
static int conn_shutdown_stream_read(ngtcp2_conn *conn, ngtcp2_strm *strm,
                                     uint64_t app_error_code) {
  ngtcp2_strm_set_app_error_code(strm, app_error_code);

  if (strm->flags &
      (NGTCP2_STRM_FLAG_STOP_SENDING | NGTCP2_STRM_FLAG_RESET_STREAM_RECVED)) {
    return 0;
  }
  if ((strm->flags & NGTCP2_STRM_FLAG_SHUT_RD) &&
      ngtcp2_strm_rx_offset(strm) == strm->rx.last_offset) {
    return 0;
  }

  /* Extend connection flow control window for the amount of data
     which are not passed to application. */
  if (!(strm->flags & NGTCP2_STRM_FLAG_RESET_STREAM_RECVED)) {
    ngtcp2_conn_extend_max_offset(conn, strm->rx.last_offset -
                                            ngtcp2_strm_rx_offset(strm));
  }

  strm->flags |= NGTCP2_STRM_FLAG_STOP_SENDING;

  ngtcp2_strm_discard_reordered_data(strm);

  return conn_stop_sending(conn, strm, app_error_code);
}

int ngtcp2_conn_shutdown_stream(ngtcp2_conn *conn, uint32_t flags,
                                int64_t stream_id, uint64_t app_error_code) {
  int rv;
  ngtcp2_strm *strm;
  (void)flags;

  strm = ngtcp2_conn_find_stream(conn, stream_id);
  if (strm == NULL) {
    return 0;
  }

  if (bidi_stream(stream_id) || !conn_local_stream(conn, stream_id)) {
    rv = conn_shutdown_stream_read(conn, strm, app_error_code);
    if (rv != 0) {
      return rv;
    }
  }

  if (bidi_stream(stream_id) || conn_local_stream(conn, stream_id)) {
    rv = conn_shutdown_stream_write(conn, strm, app_error_code);
    if (rv != 0) {
      return rv;
    }
  }

  return 0;
}

int ngtcp2_conn_shutdown_stream_write(ngtcp2_conn *conn, uint32_t flags,
                                      int64_t stream_id,
                                      uint64_t app_error_code) {
  ngtcp2_strm *strm;
  (void)flags;

  if (!bidi_stream(stream_id) && !conn_local_stream(conn, stream_id)) {
    return NGTCP2_ERR_INVALID_ARGUMENT;
  }

  strm = ngtcp2_conn_find_stream(conn, stream_id);
  if (strm == NULL) {
    return 0;
  }

  return conn_shutdown_stream_write(conn, strm, app_error_code);
}

int ngtcp2_conn_shutdown_stream_read(ngtcp2_conn *conn, uint32_t flags,
                                     int64_t stream_id,
                                     uint64_t app_error_code) {
  ngtcp2_strm *strm;
  (void)flags;

  if (!bidi_stream(stream_id) && conn_local_stream(conn, stream_id)) {
    return NGTCP2_ERR_INVALID_ARGUMENT;
  }

  strm = ngtcp2_conn_find_stream(conn, stream_id);
  if (strm == NULL) {
    return 0;
  }

  return conn_shutdown_stream_read(conn, strm, app_error_code);
}

/*
 * conn_extend_max_stream_offset extends stream level flow control
 * window by |datalen| of the stream denoted by |strm|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 */
static int conn_extend_max_stream_offset(ngtcp2_conn *conn, ngtcp2_strm *strm,
                                         uint64_t datalen) {
  ngtcp2_strm *top;

  if (datalen > NGTCP2_MAX_VARINT ||
      strm->rx.unsent_max_offset > NGTCP2_MAX_VARINT - datalen) {
    strm->rx.unsent_max_offset = NGTCP2_MAX_VARINT;
  } else {
    strm->rx.unsent_max_offset += datalen;
  }

  if (!(strm->flags &
        (NGTCP2_STRM_FLAG_SHUT_RD | NGTCP2_STRM_FLAG_STOP_SENDING)) &&
      !ngtcp2_strm_is_tx_queued(strm) &&
      conn_should_send_max_stream_data(conn, strm)) {
    if (!ngtcp2_pq_empty(&conn->tx.strmq)) {
      top = ngtcp2_conn_tx_strmq_top(conn);
      strm->cycle = top->cycle;
    }
    strm->cycle = conn_tx_strmq_first_cycle(conn);
    return ngtcp2_conn_tx_strmq_push(conn, strm);
  }

  return 0;
}

int ngtcp2_conn_extend_max_stream_offset(ngtcp2_conn *conn, int64_t stream_id,
                                         uint64_t datalen) {
  ngtcp2_strm *strm;

  if (!bidi_stream(stream_id) && conn_local_stream(conn, stream_id)) {
    return NGTCP2_ERR_INVALID_ARGUMENT;
  }

  strm = ngtcp2_conn_find_stream(conn, stream_id);
  if (strm == NULL) {
    return 0;
  }

  return conn_extend_max_stream_offset(conn, strm, datalen);
}

void ngtcp2_conn_extend_max_offset(ngtcp2_conn *conn, uint64_t datalen) {
  if (NGTCP2_MAX_VARINT < datalen ||
      conn->rx.unsent_max_offset > NGTCP2_MAX_VARINT - datalen) {
    conn->rx.unsent_max_offset = NGTCP2_MAX_VARINT;
    return;
  }

  conn->rx.unsent_max_offset += datalen;
}

void ngtcp2_conn_extend_max_streams_bidi(ngtcp2_conn *conn, size_t n) {
  handle_max_remote_streams_extension(&conn->remote.bidi.unsent_max_streams, n);
}

void ngtcp2_conn_extend_max_streams_uni(ngtcp2_conn *conn, size_t n) {
  handle_max_remote_streams_extension(&conn->remote.uni.unsent_max_streams, n);
}

const ngtcp2_cid *ngtcp2_conn_get_dcid(ngtcp2_conn *conn) {
  return &conn->dcid.current.cid;
}

const ngtcp2_cid *ngtcp2_conn_get_client_initial_dcid(ngtcp2_conn *conn) {
  return &conn->rcid;
}

uint32_t ngtcp2_conn_get_client_chosen_version(ngtcp2_conn *conn) {
  return conn->client_chosen_version;
}

uint32_t ngtcp2_conn_get_negotiated_version(ngtcp2_conn *conn) {
  return conn->negotiated_version;
}

static int delete_strms_pq_each(void *data, void *ptr) {
  ngtcp2_conn *conn = ptr;
  ngtcp2_strm *s = data;

  if (ngtcp2_strm_is_tx_queued(s)) {
    ngtcp2_pq_remove(&conn->tx.strmq, &s->pe);
  }

  ngtcp2_strm_free(s);
  ngtcp2_objalloc_strm_release(&conn->strm_objalloc, s);

  return 0;
}

/*
 * conn_discard_early_data_state discards any connection states which
 * are altered by any operations during early data transfer.
 */
static void conn_discard_early_data_state(ngtcp2_conn *conn) {
  ngtcp2_frame_chain **pfrc, *frc;

  ngtcp2_rtb_remove_early_data(&conn->pktns.rtb, &conn->cstat);

  ngtcp2_map_each_free(&conn->strms, delete_strms_pq_each, conn);
  ngtcp2_map_clear(&conn->strms);

  conn->tx.offset = 0;
  conn->tx.last_blocked_offset = UINT64_MAX;

  conn->rx.unsent_max_offset = conn->rx.max_offset =
      conn->local.transport_params.initial_max_data;

  conn->remote.bidi.unsent_max_streams = conn->remote.bidi.max_streams =
      conn->local.transport_params.initial_max_streams_bidi;

  conn->remote.uni.unsent_max_streams = conn->remote.uni.max_streams =
      conn->local.transport_params.initial_max_streams_uni;

  if (conn->server) {
    conn->local.bidi.next_stream_id = 1;
    conn->local.uni.next_stream_id = 3;
  } else {
    conn->local.bidi.next_stream_id = 0;
    conn->local.uni.next_stream_id = 2;
  }

  for (pfrc = &conn->pktns.tx.frq; *pfrc;) {
    frc = *pfrc;
    *pfrc = (*pfrc)->next;
    ngtcp2_frame_chain_objalloc_del(frc, &conn->frc_objalloc, conn->mem);
  }
}

int ngtcp2_conn_tls_early_data_rejected(ngtcp2_conn *conn) {
  if (conn->flags & NGTCP2_CONN_FLAG_EARLY_DATA_REJECTED) {
    return 0;
  }

  conn->flags |= NGTCP2_CONN_FLAG_EARLY_DATA_REJECTED;

  conn_discard_early_data_state(conn);

  if (conn->callbacks.tls_early_data_rejected) {
    return conn->callbacks.tls_early_data_rejected(conn, conn->user_data);
  }

  if (conn->early.ckm) {
    conn_discard_early_key(conn);
  }

  return 0;
}

int ngtcp2_conn_get_tls_early_data_rejected(ngtcp2_conn *conn) {
  return (conn->flags & NGTCP2_CONN_FLAG_EARLY_DATA_REJECTED) != 0;
}

int ngtcp2_conn_update_rtt(ngtcp2_conn *conn, ngtcp2_duration rtt,
                           ngtcp2_duration ack_delay, ngtcp2_tstamp ts) {
  ngtcp2_conn_stat *cstat = &conn->cstat;

  if (cstat->min_rtt == UINT64_MAX) {
    cstat->latest_rtt = rtt;
    cstat->min_rtt = rtt;
    cstat->smoothed_rtt = rtt;
    cstat->rttvar = rtt / 2;
    cstat->first_rtt_sample_ts = ts;
  } else {
    if (conn->flags & NGTCP2_CONN_FLAG_HANDSHAKE_CONFIRMED) {
      assert(conn->remote.transport_params);

      ack_delay =
          ngtcp2_min(ack_delay, conn->remote.transport_params->max_ack_delay);
    } else if (ack_delay > 0 && rtt >= cstat->min_rtt &&
               rtt < cstat->min_rtt + ack_delay) {
      /* Ignore RTT sample if adjusting ack_delay causes the sample
         less than min_rtt before handshake confirmation. */
      ngtcp2_log_info(
          &conn->log, NGTCP2_LOG_EVENT_LDC,
          "ignore rtt sample because ack_delay is too large latest_rtt=%" PRIu64
          " min_rtt=%" PRIu64 " ack_delay=%" PRIu64,
          rtt / NGTCP2_MILLISECONDS, cstat->min_rtt / NGTCP2_MILLISECONDS,
          ack_delay / NGTCP2_MILLISECONDS);
      return NGTCP2_ERR_INVALID_ARGUMENT;
    }

    cstat->latest_rtt = rtt;
    cstat->min_rtt = ngtcp2_min(cstat->min_rtt, rtt);

    if (rtt >= cstat->min_rtt + ack_delay) {
      rtt -= ack_delay;
    }

    cstat->rttvar = (cstat->rttvar * 3 + (cstat->smoothed_rtt < rtt
                                              ? rtt - cstat->smoothed_rtt
                                              : cstat->smoothed_rtt - rtt)) /
                    4;
    cstat->smoothed_rtt = (cstat->smoothed_rtt * 7 + rtt) / 8;
  }

  ngtcp2_log_info(
      &conn->log, NGTCP2_LOG_EVENT_LDC,
      "latest_rtt=%" PRIu64 " min_rtt=%" PRIu64 " smoothed_rtt=%" PRIu64
      " rttvar=%" PRIu64 " ack_delay=%" PRIu64,
      cstat->latest_rtt / NGTCP2_MILLISECONDS,
      cstat->min_rtt / NGTCP2_MILLISECONDS,
      cstat->smoothed_rtt / NGTCP2_MILLISECONDS,
      cstat->rttvar / NGTCP2_MILLISECONDS, ack_delay / NGTCP2_MILLISECONDS);

  return 0;
}

void ngtcp2_conn_get_conn_info_versioned(ngtcp2_conn *conn,
                                         int conn_info_version,
                                         ngtcp2_conn_info *cinfo) {
  const ngtcp2_conn_stat *cstat = &conn->cstat;
  (void)conn_info_version;

  cinfo->latest_rtt = cstat->latest_rtt;
  cinfo->min_rtt = cstat->min_rtt;
  cinfo->smoothed_rtt = cstat->smoothed_rtt;
  cinfo->rttvar = cstat->rttvar;
  cinfo->cwnd = cstat->cwnd;
  cinfo->ssthresh = cstat->ssthresh;
  cinfo->bytes_in_flight = cstat->bytes_in_flight;
}

static void conn_get_loss_time_and_pktns(ngtcp2_conn *conn,
                                         ngtcp2_tstamp *ploss_time,
                                         ngtcp2_pktns **ppktns) {
  ngtcp2_pktns *const ns[] = {conn->hs_pktns, &conn->pktns};
  ngtcp2_conn_stat *cstat = &conn->cstat;
  ngtcp2_duration *loss_time = cstat->loss_time + 1;
  ngtcp2_tstamp earliest_loss_time = cstat->loss_time[NGTCP2_PKTNS_ID_INITIAL];
  ngtcp2_pktns *pktns = conn->in_pktns;
  size_t i;

  for (i = 0; i < ngtcp2_arraylen(ns); ++i) {
    if (ns[i] == NULL || loss_time[i] >= earliest_loss_time) {
      continue;
    }

    earliest_loss_time = loss_time[i];
    pktns = ns[i];
  }

  if (ploss_time) {
    *ploss_time = earliest_loss_time;
  }
  if (ppktns) {
    *ppktns = pktns;
  }
}

static ngtcp2_tstamp conn_get_earliest_pto_expiry(ngtcp2_conn *conn,
                                                  ngtcp2_tstamp ts) {
  ngtcp2_pktns *ns[] = {conn->in_pktns, conn->hs_pktns, &conn->pktns};
  size_t i;
  ngtcp2_tstamp earliest_ts = UINT64_MAX, t;
  ngtcp2_conn_stat *cstat = &conn->cstat;
  ngtcp2_tstamp *times = cstat->last_tx_pkt_ts;
  ngtcp2_duration duration =
      compute_pto(cstat->smoothed_rtt, cstat->rttvar, /* max_ack_delay = */ 0) *
      (1ULL << cstat->pto_count);

  for (i = NGTCP2_PKTNS_ID_INITIAL; i < NGTCP2_PKTNS_ID_MAX; ++i) {
    if (ns[i] == NULL || ns[i]->rtb.num_pto_eliciting == 0 ||
        (times[i] == UINT64_MAX ||
         (i == NGTCP2_PKTNS_ID_APPLICATION &&
          !(conn->flags & NGTCP2_CONN_FLAG_HANDSHAKE_CONFIRMED)))) {
      continue;
    }

    t = times[i] + duration;

    if (i == NGTCP2_PKTNS_ID_APPLICATION) {
      assert(conn->remote.transport_params);
      t += conn->remote.transport_params->max_ack_delay *
           (1ULL << cstat->pto_count);
    }

    if (t < earliest_ts) {
      earliest_ts = t;
    }
  }

  if (earliest_ts == UINT64_MAX) {
    return ts + duration;
  }

  return earliest_ts;
}

void ngtcp2_conn_set_loss_detection_timer(ngtcp2_conn *conn, ngtcp2_tstamp ts) {
  ngtcp2_conn_stat *cstat = &conn->cstat;
  ngtcp2_duration timeout;
  ngtcp2_pktns *in_pktns = conn->in_pktns;
  ngtcp2_pktns *hs_pktns = conn->hs_pktns;
  ngtcp2_pktns *pktns = &conn->pktns;
  ngtcp2_tstamp earliest_loss_time;

  conn_get_loss_time_and_pktns(conn, &earliest_loss_time, NULL);

  if (earliest_loss_time != UINT64_MAX) {
    cstat->loss_detection_timer = earliest_loss_time;

    ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_LDC,
                    "loss_detection_timer=%" PRIu64 " nonzero crypto loss time",
                    cstat->loss_detection_timer);
    return;
  }

  if ((!in_pktns || in_pktns->rtb.num_pto_eliciting == 0) &&
      (!hs_pktns || hs_pktns->rtb.num_pto_eliciting == 0) &&
      (pktns->rtb.num_pto_eliciting == 0 ||
       !(conn->flags & NGTCP2_CONN_FLAG_HANDSHAKE_CONFIRMED)) &&
      (conn->server ||
       (conn->flags & (NGTCP2_CONN_FLAG_SERVER_ADDR_VERIFIED |
                       NGTCP2_CONN_FLAG_HANDSHAKE_CONFIRMED)))) {
    if (cstat->loss_detection_timer != UINT64_MAX) {
      ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_LDC,
                      "loss detection timer canceled");
      cstat->loss_detection_timer = UINT64_MAX;
      cstat->pto_count = 0;
    }
    return;
  }

  cstat->loss_detection_timer = conn_get_earliest_pto_expiry(conn, ts);

  timeout =
      cstat->loss_detection_timer > ts ? cstat->loss_detection_timer - ts : 0;

  ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_LDC,
                  "loss_detection_timer=%" PRIu64 " timeout=%" PRIu64,
                  cstat->loss_detection_timer, timeout / NGTCP2_MILLISECONDS);
}

int ngtcp2_conn_on_loss_detection_timer(ngtcp2_conn *conn, ngtcp2_tstamp ts) {
  ngtcp2_conn_stat *cstat = &conn->cstat;
  int rv;
  ngtcp2_pktns *in_pktns = conn->in_pktns;
  ngtcp2_pktns *hs_pktns = conn->hs_pktns;
  ngtcp2_tstamp earliest_loss_time;
  ngtcp2_pktns *loss_pktns = NULL;

  switch (conn->state) {
  case NGTCP2_CS_CLOSING:
  case NGTCP2_CS_DRAINING:
    cstat->loss_detection_timer = UINT64_MAX;
    cstat->pto_count = 0;
    return 0;
  default:
    break;
  }

  if (cstat->loss_detection_timer == UINT64_MAX) {
    return 0;
  }

  conn_get_loss_time_and_pktns(conn, &earliest_loss_time, &loss_pktns);

  ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_LDC,
                  "loss detection timer fired");

  if (earliest_loss_time != UINT64_MAX) {
    assert(loss_pktns);

    rv = ngtcp2_conn_detect_lost_pkt(conn, loss_pktns, cstat, ts);
    if (rv != 0) {
      return rv;
    }
    ngtcp2_conn_set_loss_detection_timer(conn, ts);
    return 0;
  }

  if (!conn->server && !conn_is_tls_handshake_completed(conn)) {
    if (hs_pktns->crypto.tx.ckm) {
      hs_pktns->rtb.probe_pkt_left = 1;
    } else {
      in_pktns->rtb.probe_pkt_left = 1;
    }
  } else {
    if (in_pktns && in_pktns->rtb.num_pto_eliciting) {
      in_pktns->rtb.probe_pkt_left = 1;

      assert(hs_pktns);

      if (conn->server && hs_pktns->rtb.num_pto_eliciting) {
        /* let server coalesce packets */
        hs_pktns->rtb.probe_pkt_left = 1;
      }
    } else if (hs_pktns && hs_pktns->rtb.num_pto_eliciting) {
      hs_pktns->rtb.probe_pkt_left = 2;
    } else {
      conn->pktns.rtb.probe_pkt_left = 2;
    }
  }

  ++cstat->pto_count;

  ngtcp2_log_info(&conn->log, NGTCP2_LOG_EVENT_LDC, "pto_count=%zu",
                  cstat->pto_count);

  ngtcp2_conn_set_loss_detection_timer(conn, ts);

  return 0;
}

static int conn_buffer_crypto_data(ngtcp2_conn *conn, const uint8_t **pdata,
                                   ngtcp2_pktns *pktns, const uint8_t *data,
                                   size_t datalen) {
  int rv;
  ngtcp2_buf_chain **pbufchain = &pktns->crypto.tx.data;

  if (*pbufchain) {
    for (; (*pbufchain)->next; pbufchain = &(*pbufchain)->next)
      ;

    if (ngtcp2_buf_left(&(*pbufchain)->buf) < datalen) {
      pbufchain = &(*pbufchain)->next;
    }
  }

  if (!*pbufchain) {
    rv = ngtcp2_buf_chain_new(pbufchain, ngtcp2_max(1024, datalen), conn->mem);
    if (rv != 0) {
      return rv;
    }
  }

  *pdata = (*pbufchain)->buf.last;
  (*pbufchain)->buf.last = ngtcp2_cpymem((*pbufchain)->buf.last, data, datalen);

  return 0;
}

int ngtcp2_conn_submit_crypto_data(ngtcp2_conn *conn,
                                   ngtcp2_encryption_level encryption_level,
                                   const uint8_t *data, const size_t datalen) {
  ngtcp2_pktns *pktns;
  ngtcp2_frame_chain *frc;
  ngtcp2_stream *fr;
  int rv;

  if (datalen == 0) {
    return 0;
  }

  switch (encryption_level) {
  case NGTCP2_ENCRYPTION_LEVEL_INITIAL:
    assert(conn->in_pktns);
    pktns = conn->in_pktns;
    break;
  case NGTCP2_ENCRYPTION_LEVEL_HANDSHAKE:
    assert(conn->hs_pktns);
    pktns = conn->hs_pktns;
    break;
  case NGTCP2_ENCRYPTION_LEVEL_1RTT:
    pktns = &conn->pktns;
    break;
  default:
    return NGTCP2_ERR_INVALID_ARGUMENT;
  }

  rv = conn_buffer_crypto_data(conn, &data, pktns, data, datalen);
  if (rv != 0) {
    return rv;
  }

  rv = ngtcp2_frame_chain_objalloc_new(&frc, &conn->frc_objalloc);
  if (rv != 0) {
    return rv;
  }

  fr = &frc->fr.stream;

  fr->type = NGTCP2_FRAME_CRYPTO;
  fr->flags = 0;
  fr->fin = 0;
  fr->stream_id = 0;
  fr->offset = pktns->crypto.tx.offset;
  fr->datacnt = 1;
  fr->data[0].len = datalen;
  fr->data[0].base = (uint8_t *)data;

  rv = ngtcp2_strm_streamfrq_push(&pktns->crypto.strm, frc);
  if (rv != 0) {
    ngtcp2_frame_chain_objalloc_del(frc, &conn->frc_objalloc, conn->mem);
    return rv;
  }

  pktns->crypto.strm.tx.offset += datalen;
  pktns->crypto.tx.offset += datalen;

  return 0;
}

int ngtcp2_conn_submit_new_token(ngtcp2_conn *conn, const uint8_t *token,
                                 size_t tokenlen) {
  int rv;
  ngtcp2_frame_chain *nfrc;

  assert(conn->server);
  assert(token);
  assert(tokenlen);

  rv = ngtcp2_frame_chain_new_token_objalloc_new(
      &nfrc, token, tokenlen, &conn->frc_objalloc, conn->mem);
  if (rv != 0) {
    return rv;
  }

  nfrc->next = conn->pktns.tx.frq;
  conn->pktns.tx.frq = nfrc;

  return 0;
}

ngtcp2_strm *ngtcp2_conn_tx_strmq_top(ngtcp2_conn *conn) {
  assert(!ngtcp2_pq_empty(&conn->tx.strmq));
  return ngtcp2_struct_of(ngtcp2_pq_top(&conn->tx.strmq), ngtcp2_strm, pe);
}

void ngtcp2_conn_tx_strmq_pop(ngtcp2_conn *conn) {
  ngtcp2_strm *strm = ngtcp2_conn_tx_strmq_top(conn);
  assert(strm);
  ngtcp2_pq_pop(&conn->tx.strmq);
  strm->pe.index = NGTCP2_PQ_BAD_INDEX;
}

int ngtcp2_conn_tx_strmq_push(ngtcp2_conn *conn, ngtcp2_strm *strm) {
  return ngtcp2_pq_push(&conn->tx.strmq, &strm->pe);
}

static int conn_has_uncommitted_preferred_addr_cid(ngtcp2_conn *conn) {
  return conn->server &&
         !(conn->flags & NGTCP2_CONN_FLAG_LOCAL_TRANSPORT_PARAMS_COMMITTED) &&
         conn->oscid.datalen &&
         conn->local.transport_params.preferred_addr_present;
}

size_t ngtcp2_conn_get_scid(ngtcp2_conn *conn, ngtcp2_cid *dest) {
  ngtcp2_cid *origdest = dest;
  ngtcp2_ksl_it it;
  ngtcp2_scid *scid;

  if (dest == NULL) {
    return ngtcp2_ksl_len(&conn->scid.set) +
           (size_t)conn_has_uncommitted_preferred_addr_cid(conn);
  }

  for (it = ngtcp2_ksl_begin(&conn->scid.set); !ngtcp2_ksl_it_end(&it);
       ngtcp2_ksl_it_next(&it)) {
    scid = ngtcp2_ksl_it_get(&it);
    *dest++ = scid->cid;
  }

  if (conn_has_uncommitted_preferred_addr_cid(conn)) {
    *dest++ = conn->local.transport_params.preferred_addr.cid;
  }

  return (size_t)(dest - origdest);
}

static size_t conn_get_num_active_dcid(ngtcp2_conn *conn) {
  size_t n = 1; /* for conn->dcid.current */
  ngtcp2_pv *pv = conn->pv;

  if (pv) {
    if (pv->dcid.seq != conn->dcid.current.seq) {
      ++n;
    }
    if ((pv->flags & NGTCP2_PV_FLAG_FALLBACK_ON_FAILURE) &&
        pv->fallback_dcid.seq != conn->dcid.current.seq &&
        pv->fallback_dcid.seq != pv->dcid.seq) {
      ++n;
    }
  }

  n += ngtcp2_ringbuf_len(&conn->dcid.retired.rb);

  return n;
}

static void copy_dcid_to_cid_token(ngtcp2_cid_token *dest,
                                   const ngtcp2_dcid *src) {
  dest->seq = src->seq;
  dest->cid = src->cid;
  ngtcp2_path_storage_init2(&dest->ps, &src->ps.path);
  if ((dest->token_present =
           (src->flags & NGTCP2_DCID_FLAG_TOKEN_PRESENT) != 0)) {
    memcpy(dest->token, src->token, NGTCP2_STATELESS_RESET_TOKENLEN);
  }
}

size_t ngtcp2_conn_get_active_dcid(ngtcp2_conn *conn, ngtcp2_cid_token *dest) {
  ngtcp2_pv *pv = conn->pv;
  ngtcp2_cid_token *orig = dest;
  ngtcp2_dcid *dcid;
  size_t len, i;

  if (!(conn->flags & NGTCP2_CONN_FLAG_HANDSHAKE_COMPLETED)) {
    return 0;
  }

  if (dest == NULL) {
    return conn_get_num_active_dcid(conn);
  }

  copy_dcid_to_cid_token(dest, &conn->dcid.current);
  ++dest;

  if (pv) {
    if (pv->dcid.seq != conn->dcid.current.seq) {
      copy_dcid_to_cid_token(dest, &pv->dcid);
      ++dest;
    }
    if ((pv->flags & NGTCP2_PV_FLAG_FALLBACK_ON_FAILURE) &&
        pv->fallback_dcid.seq != conn->dcid.current.seq &&
        pv->fallback_dcid.seq != pv->dcid.seq) {
      copy_dcid_to_cid_token(dest, &pv->fallback_dcid);
      ++dest;
    }
  }

  len = ngtcp2_ringbuf_len(&conn->dcid.retired.rb);
  for (i = 0; i < len; ++i) {
    dcid = ngtcp2_ringbuf_get(&conn->dcid.retired.rb, i);
    copy_dcid_to_cid_token(dest, dcid);
    ++dest;
  }

  return (size_t)(dest - orig);
}

void ngtcp2_conn_set_local_addr(ngtcp2_conn *conn, const ngtcp2_addr *addr) {
  ngtcp2_addr *dest = &conn->dcid.current.ps.path.local;

  assert(addr->addrlen <=
         (ngtcp2_socklen)sizeof(conn->dcid.current.ps.local_addrbuf));
  ngtcp2_addr_copy(dest, addr);
}

void ngtcp2_conn_set_path_user_data(ngtcp2_conn *conn, void *path_user_data) {
  conn->dcid.current.ps.path.user_data = path_user_data;
}

const ngtcp2_path *ngtcp2_conn_get_path(ngtcp2_conn *conn) {
  return &conn->dcid.current.ps.path;
}

size_t ngtcp2_conn_get_max_tx_udp_payload_size(ngtcp2_conn *conn) {
  return conn->local.settings.max_tx_udp_payload_size;
}

size_t ngtcp2_conn_get_path_max_tx_udp_payload_size(ngtcp2_conn *conn) {
  if (conn->local.settings.no_tx_udp_payload_size_shaping) {
    return ngtcp2_conn_get_max_tx_udp_payload_size(conn);
  }

  return conn->dcid.current.max_udp_payload_size;
}

static int conn_initiate_migration_precheck(ngtcp2_conn *conn,
                                            const ngtcp2_addr *local_addr) {
  if (!(conn->flags & NGTCP2_CONN_FLAG_HANDSHAKE_CONFIRMED) ||
      conn->remote.transport_params->disable_active_migration ||
      conn->dcid.current.cid.datalen == 0 ||
      (conn->pv && (conn->pv->flags & NGTCP2_PV_FLAG_PREFERRED_ADDR))) {
    return NGTCP2_ERR_INVALID_STATE;
  }

  if (ngtcp2_ringbuf_len(&conn->dcid.unused.rb) == 0) {
    return NGTCP2_ERR_CONN_ID_BLOCKED;
  }

  if (ngtcp2_addr_eq(&conn->dcid.current.ps.path.local, local_addr)) {
    return NGTCP2_ERR_INVALID_ARGUMENT;
  }

  return 0;
}

int ngtcp2_conn_initiate_immediate_migration(ngtcp2_conn *conn,
                                             const ngtcp2_path *path,
                                             ngtcp2_tstamp ts) {
  int rv;
  ngtcp2_dcid *dcid;
  ngtcp2_pv *pv;

  assert(!conn->server);

  conn_update_timestamp(conn, ts);

  rv = conn_initiate_migration_precheck(conn, &path->local);
  if (rv != 0) {
    return rv;
  }

  ngtcp2_conn_stop_pmtud(conn);

  if (conn->pv) {
    rv = conn_abort_pv(conn, ts);
    if (rv != 0) {
      return rv;
    }
  }

  rv = conn_retire_dcid(conn, &conn->dcid.current, ts);
  if (rv != 0) {
    return rv;
  }

  dcid = ngtcp2_ringbuf_get(&conn->dcid.unused.rb, 0);
  ngtcp2_dcid_set_path(dcid, path);

  ngtcp2_dcid_copy(&conn->dcid.current, dcid);
  ngtcp2_ringbuf_pop_front(&conn->dcid.unused.rb);

  conn_reset_congestion_state(conn, ts);
  conn_reset_ecn_validation_state(conn);

  /* TODO It might be better to add a new flag which indicates that a
     connection should be closed if this path validation failed.  The
     current design allows an application to continue, by migrating
     into yet another path. */
  rv = ngtcp2_pv_new(&pv, dcid, conn_compute_pv_timeout(conn),
                     NGTCP2_PV_FLAG_NONE, &conn->log, conn->mem);
  if (rv != 0) {
    return rv;
  }

  conn->pv = pv;

  return conn_call_activate_dcid(conn, &conn->dcid.current);
}

int ngtcp2_conn_initiate_migration(ngtcp2_conn *conn, const ngtcp2_path *path,
                                   ngtcp2_tstamp ts) {
  int rv;
  ngtcp2_dcid *dcid;
  ngtcp2_pv *pv;

  assert(!conn->server);

  conn_update_timestamp(conn, ts);

  rv = conn_initiate_migration_precheck(conn, &path->local);
  if (rv != 0) {
    return rv;
  }

  if (conn->pv) {
    rv = conn_abort_pv(conn, ts);
    if (rv != 0) {
      return rv;
    }
  }

  dcid = ngtcp2_ringbuf_get(&conn->dcid.unused.rb, 0);
  ngtcp2_dcid_set_path(dcid, path);

  rv = ngtcp2_pv_new(&pv, dcid, conn_compute_pv_timeout(conn),
                     NGTCP2_PV_FLAG_NONE, &conn->log, conn->mem);
  if (rv != 0) {
    return rv;
  }

  ngtcp2_ringbuf_pop_front(&conn->dcid.unused.rb);
  conn->pv = pv;

  return conn_call_activate_dcid(conn, &pv->dcid);
}

uint64_t ngtcp2_conn_get_max_data_left(ngtcp2_conn *conn) {
  return conn->tx.max_offset - conn->tx.offset;
}

uint64_t ngtcp2_conn_get_max_stream_data_left(ngtcp2_conn *conn,
                                              int64_t stream_id) {
  ngtcp2_strm *strm = ngtcp2_conn_find_stream(conn, stream_id);

  if (strm == NULL) {
    return 0;
  }

  return strm->tx.max_offset - strm->tx.offset;
}

uint64_t ngtcp2_conn_get_streams_bidi_left(ngtcp2_conn *conn) {
  uint64_t n = ngtcp2_ord_stream_id(conn->local.bidi.next_stream_id);

  return n > conn->local.bidi.max_streams
             ? 0
             : conn->local.bidi.max_streams - n + 1;
}

uint64_t ngtcp2_conn_get_streams_uni_left(ngtcp2_conn *conn) {
  uint64_t n = ngtcp2_ord_stream_id(conn->local.uni.next_stream_id);

  return n > conn->local.uni.max_streams ? 0
                                         : conn->local.uni.max_streams - n + 1;
}

uint64_t ngtcp2_conn_get_cwnd_left(ngtcp2_conn *conn) {
  uint64_t bytes_in_flight = conn->cstat.bytes_in_flight;
  uint64_t cwnd = conn_get_cwnd(conn);

  if (cwnd > bytes_in_flight) {
    return cwnd - bytes_in_flight;
  }

  return 0;
}

ngtcp2_tstamp ngtcp2_conn_get_idle_expiry(ngtcp2_conn *conn) {
  ngtcp2_duration trpto;
  ngtcp2_duration idle_timeout;

  /* TODO Remote max_idle_timeout becomes effective after handshake
     completion. */

  if (!conn_is_tls_handshake_completed(conn) ||
      conn->remote.transport_params->max_idle_timeout == 0 ||
      (conn->local.transport_params.max_idle_timeout &&
       conn->local.transport_params.max_idle_timeout <
           conn->remote.transport_params->max_idle_timeout)) {
    idle_timeout = conn->local.transport_params.max_idle_timeout;
  } else {
    idle_timeout = conn->remote.transport_params->max_idle_timeout;
  }

  if (idle_timeout == 0) {
    return UINT64_MAX;
  }

  trpto = 3 * conn_compute_pto(conn, conn_is_tls_handshake_completed(conn)
                                         ? &conn->pktns
                                         : conn->hs_pktns);

  idle_timeout = ngtcp2_max(idle_timeout, trpto);

  if (conn->idle_ts >= UINT64_MAX - idle_timeout) {
    return UINT64_MAX;
  }

  return conn->idle_ts + idle_timeout;
}

ngtcp2_duration ngtcp2_conn_get_pto(ngtcp2_conn *conn) {
  return conn_compute_pto(conn, conn_is_tls_handshake_completed(conn)
                                    ? &conn->pktns
                                    : conn->hs_pktns);
}

void ngtcp2_conn_set_initial_crypto_ctx(ngtcp2_conn *conn,
                                        const ngtcp2_crypto_ctx *ctx) {
  assert(conn->in_pktns);
  conn->in_pktns->crypto.ctx = *ctx;
}

const ngtcp2_crypto_ctx *ngtcp2_conn_get_initial_crypto_ctx(ngtcp2_conn *conn) {
  assert(conn->in_pktns);
  return &conn->in_pktns->crypto.ctx;
}

void ngtcp2_conn_set_retry_aead(ngtcp2_conn *conn,
                                const ngtcp2_crypto_aead *aead,
                                const ngtcp2_crypto_aead_ctx *aead_ctx) {
  assert(!conn->crypto.retry_aead_ctx.native_handle);

  conn->crypto.retry_aead = *aead;
  conn->crypto.retry_aead_ctx = *aead_ctx;
}

void ngtcp2_conn_set_crypto_ctx(ngtcp2_conn *conn,
                                const ngtcp2_crypto_ctx *ctx) {
  assert(conn->hs_pktns);
  conn->hs_pktns->crypto.ctx = *ctx;
  conn->pktns.crypto.ctx = *ctx;
}

const ngtcp2_crypto_ctx *ngtcp2_conn_get_crypto_ctx(ngtcp2_conn *conn) {
  return &conn->pktns.crypto.ctx;
}

void ngtcp2_conn_set_0rtt_crypto_ctx(ngtcp2_conn *conn,
                                     const ngtcp2_crypto_ctx *ctx) {
  conn->early.ctx = *ctx;
}

const ngtcp2_crypto_ctx *ngtcp2_conn_get_0rtt_crypto_ctx(ngtcp2_conn *conn) {
  return &conn->early.ctx;
}

void *ngtcp2_conn_get_tls_native_handle(ngtcp2_conn *conn) {
  return conn->crypto.tls_native_handle;
}

void ngtcp2_conn_set_tls_native_handle(ngtcp2_conn *conn,
                                       void *tls_native_handle) {
  conn->crypto.tls_native_handle = tls_native_handle;
}

const ngtcp2_ccerr *ngtcp2_conn_get_ccerr(ngtcp2_conn *conn) {
  return &conn->rx.ccerr;
}

void ngtcp2_conn_set_tls_error(ngtcp2_conn *conn, int liberr) {
  conn->crypto.tls_error = liberr;
}

int ngtcp2_conn_get_tls_error(ngtcp2_conn *conn) {
  return conn->crypto.tls_error;
}

void ngtcp2_conn_set_tls_alert(ngtcp2_conn *conn, uint8_t alert) {
  conn->crypto.tls_alert = alert;
}

uint8_t ngtcp2_conn_get_tls_alert(ngtcp2_conn *conn) {
  return conn->crypto.tls_alert;
}

int ngtcp2_conn_is_local_stream(ngtcp2_conn *conn, int64_t stream_id) {
  return conn_local_stream(conn, stream_id);
}

int ngtcp2_conn_is_server(ngtcp2_conn *conn) { return conn->server; }

int ngtcp2_conn_after_retry(ngtcp2_conn *conn) {
  return (conn->flags & NGTCP2_CONN_FLAG_RECV_RETRY) != 0;
}

int ngtcp2_conn_set_stream_user_data(ngtcp2_conn *conn, int64_t stream_id,
                                     void *stream_user_data) {
  ngtcp2_strm *strm = ngtcp2_conn_find_stream(conn, stream_id);

  if (strm == NULL) {
    return NGTCP2_ERR_STREAM_NOT_FOUND;
  }

  strm->stream_user_data = stream_user_data;

  return 0;
}

void ngtcp2_conn_update_pkt_tx_time(ngtcp2_conn *conn, ngtcp2_tstamp ts) {
  ngtcp2_duration pacing_interval;
  ngtcp2_duration wait;

  conn_update_timestamp(conn, ts);

  if (conn->tx.pacing.pktlen == 0) {
    return;
  }

  if (conn->cstat.pacing_interval) {
    pacing_interval = conn->cstat.pacing_interval;
  } else {
    /* 1.25 is the under-utilization avoidance factor described in
       https://datatracker.ietf.org/doc/html/rfc9002#section-7.7 */
    pacing_interval = (conn->cstat.first_rtt_sample_ts == UINT64_MAX
                           ? NGTCP2_MILLISECONDS
                           : conn->cstat.smoothed_rtt) *
                      100 / 125 / conn->cstat.cwnd;
  }

  wait = (ngtcp2_duration)(conn->tx.pacing.pktlen * pacing_interval);

  conn->tx.pacing.next_ts = ts + wait;
  conn->tx.pacing.pktlen = 0;
}

size_t ngtcp2_conn_get_send_quantum(ngtcp2_conn *conn) {
  return conn->cstat.send_quantum;
}

int ngtcp2_conn_track_retired_dcid_seq(ngtcp2_conn *conn, uint64_t seq) {
  size_t i;

  if (conn->dcid.retire_unacked.len >=
      ngtcp2_arraylen(conn->dcid.retire_unacked.seqs)) {
    return NGTCP2_ERR_CONNECTION_ID_LIMIT;
  }

  /* Make sure that we do not have a duplicate */
  for (i = 0; i < conn->dcid.retire_unacked.len; ++i) {
    if (conn->dcid.retire_unacked.seqs[i] == seq) {
      ngtcp2_unreachable();
    }
  }

  conn->dcid.retire_unacked.seqs[conn->dcid.retire_unacked.len++] = seq;

  return 0;
}

void ngtcp2_conn_untrack_retired_dcid_seq(ngtcp2_conn *conn, uint64_t seq) {
  size_t i;

  for (i = 0; i < conn->dcid.retire_unacked.len; ++i) {
    if (conn->dcid.retire_unacked.seqs[i] != seq) {
      continue;
    }

    if (i != conn->dcid.retire_unacked.len - 1) {
      conn->dcid.retire_unacked.seqs[i] =
          conn->dcid.retire_unacked.seqs[conn->dcid.retire_unacked.len - 1];
    }

    --conn->dcid.retire_unacked.len;

    return;
  }
}

size_t ngtcp2_conn_get_stream_loss_count(ngtcp2_conn *conn, int64_t stream_id) {
  ngtcp2_strm *strm = ngtcp2_conn_find_stream(conn, stream_id);

  if (strm == NULL) {
    return 0;
  }

  return strm->tx.loss_count;
}

void ngtcp2_path_challenge_entry_init(ngtcp2_path_challenge_entry *pcent,
                                      const ngtcp2_path *path,
                                      const uint8_t *data) {
  ngtcp2_path_storage_init2(&pcent->ps, path);
  memcpy(pcent->data, data, sizeof(pcent->data));
}

void ngtcp2_settings_default_versioned(int settings_version,
                                       ngtcp2_settings *settings) {
  (void)settings_version;

  memset(settings, 0, sizeof(*settings));
  settings->cc_algo = NGTCP2_CC_ALGO_CUBIC;
  settings->initial_rtt = NGTCP2_DEFAULT_INITIAL_RTT;
  settings->ack_thresh = 2;
  settings->max_tx_udp_payload_size = 1500 - 48;
  settings->handshake_timeout = UINT64_MAX;
}

void ngtcp2_transport_params_default_versioned(
    int transport_params_version, ngtcp2_transport_params *params) {
  size_t len;

  switch (transport_params_version) {
  case NGTCP2_TRANSPORT_PARAMS_VERSION:
    len = sizeof(*params);

    break;
  default:
    ngtcp2_unreachable();
  }

  memset(params, 0, len);

  switch (transport_params_version) {
  case NGTCP2_TRANSPORT_PARAMS_VERSION:
    params->max_udp_payload_size = NGTCP2_DEFAULT_MAX_RECV_UDP_PAYLOAD_SIZE;
    params->active_connection_id_limit =
        NGTCP2_DEFAULT_ACTIVE_CONNECTION_ID_LIMIT;
    params->ack_delay_exponent = NGTCP2_DEFAULT_ACK_DELAY_EXPONENT;
    params->max_ack_delay = NGTCP2_DEFAULT_MAX_ACK_DELAY;

    break;
  }
}

/* The functions prefixed with ngtcp2_pkt_ are usually put inside
   ngtcp2_pkt.c.  This function uses encryption construct and uses
   test data defined only in ngtcp2_conn_test.c, so it is written
   here. */
ngtcp2_ssize ngtcp2_pkt_write_connection_close(
    uint8_t *dest, size_t destlen, uint32_t version, const ngtcp2_cid *dcid,
    const ngtcp2_cid *scid, uint64_t error_code, const uint8_t *reason,
    size_t reasonlen, ngtcp2_encrypt encrypt, const ngtcp2_crypto_aead *aead,
    const ngtcp2_crypto_aead_ctx *aead_ctx, const uint8_t *iv,
    ngtcp2_hp_mask hp_mask, const ngtcp2_crypto_cipher *hp,
    const ngtcp2_crypto_cipher_ctx *hp_ctx) {
  ngtcp2_pkt_hd hd;
  ngtcp2_crypto_km ckm;
  ngtcp2_crypto_cc cc;
  ngtcp2_ppe ppe;
  ngtcp2_frame fr = {0};
  int rv;

  ngtcp2_pkt_hd_init(&hd, NGTCP2_PKT_FLAG_LONG_FORM, NGTCP2_PKT_INITIAL, dcid,
                     scid, /* pkt_num = */ 0, /* pkt_numlen = */ 1, version,
                     /* len = */ 0);

  ngtcp2_vec_init(&ckm.secret, NULL, 0);
  ngtcp2_vec_init(&ckm.iv, iv, 12);
  ckm.aead_ctx = *aead_ctx;
  ckm.pkt_num = 0;
  ckm.flags = NGTCP2_CRYPTO_KM_FLAG_NONE;

  cc.aead = *aead;
  cc.hp = *hp;
  cc.ckm = &ckm;
  cc.hp_ctx = *hp_ctx;
  cc.encrypt = encrypt;
  cc.hp_mask = hp_mask;

  ngtcp2_ppe_init(&ppe, dest, destlen, &cc);

  rv = ngtcp2_ppe_encode_hd(&ppe, &hd);
  if (rv != 0) {
    assert(NGTCP2_ERR_NOBUF == rv);
    return rv;
  }

  if (!ngtcp2_ppe_ensure_hp_sample(&ppe)) {
    return NGTCP2_ERR_NOBUF;
  }

  fr.type = NGTCP2_FRAME_CONNECTION_CLOSE;
  fr.connection_close.error_code = error_code;
  fr.connection_close.reasonlen = reasonlen;
  fr.connection_close.reason = (uint8_t *)reason;

  rv = ngtcp2_ppe_encode_frame(&ppe, &fr);
  if (rv != 0) {
    assert(NGTCP2_ERR_NOBUF == rv);
    return rv;
  }

  return ngtcp2_ppe_final(&ppe, NULL);
}

int ngtcp2_is_bidi_stream(int64_t stream_id) { return bidi_stream(stream_id); }

uint32_t ngtcp2_select_version(const uint32_t *preferred_versions,
                               size_t preferred_versionslen,
                               const uint32_t *offered_versions,
                               size_t offered_versionslen) {
  size_t i, j;

  if (!preferred_versionslen || !offered_versionslen) {
    return 0;
  }

  for (i = 0; i < preferred_versionslen; ++i) {
    assert(ngtcp2_is_supported_version(preferred_versions[i]));

    for (j = 0; j < offered_versionslen; ++j) {
      if (preferred_versions[i] == offered_versions[j]) {
        return preferred_versions[i];
      }
    }
  }

  return 0;
}
