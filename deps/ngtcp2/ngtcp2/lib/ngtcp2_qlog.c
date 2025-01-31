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
#include "ngtcp2_qlog.h"

#include <assert.h>

#include "ngtcp2_str.h"
#include "ngtcp2_vec.h"
#include "ngtcp2_conv.h"
#include "ngtcp2_net.h"
#include "ngtcp2_unreachable.h"
#include "ngtcp2_conn_stat.h"

void ngtcp2_qlog_init(ngtcp2_qlog *qlog, ngtcp2_qlog_write write,
                      ngtcp2_tstamp ts, void *user_data) {
  qlog->write = write;
  qlog->ts = qlog->last_ts = ts;
  qlog->user_data = user_data;
}

#define write_verbatim(DEST, S) ngtcp2_cpymem((DEST), (S), sizeof(S) - 1)

static uint8_t *write_string_impl(uint8_t *p, const uint8_t *data,
                                  size_t datalen) {
  *p++ = '"';
  if (datalen) {
    p = ngtcp2_cpymem(p, data, datalen);
  }
  *p++ = '"';
  return p;
}

#define write_string(DEST, S)                                                  \
  write_string_impl((DEST), (const uint8_t *)(S), sizeof(S) - 1)

#define NGTCP2_LOWER_XDIGITS "0123456789abcdef"

static uint8_t *write_hex(uint8_t *p, const uint8_t *data, size_t datalen) {
  const uint8_t *b = data, *end = data + datalen;
  *p++ = '"';
  for (; b != end; ++b) {
    *p++ = (uint8_t)NGTCP2_LOWER_XDIGITS[*b >> 4];
    *p++ = (uint8_t)NGTCP2_LOWER_XDIGITS[*b & 0xf];
  }
  *p++ = '"';
  return p;
}

static uint8_t *write_cid(uint8_t *p, const ngtcp2_cid *cid) {
  return write_hex(p, cid->data, cid->datalen);
}

static uint8_t *write_number(uint8_t *p, uint64_t n) {
  size_t nlen = 0;
  uint64_t t;
  uint8_t *res;

  if (n == 0) {
    *p++ = '0';
    return p;
  }
  for (t = n; t; t /= 10, ++nlen)
    ;
  p += nlen;
  res = p;
  for (; n; n /= 10) {
    *--p = (uint8_t)((n % 10) + '0');
  }
  return res;
}

static uint8_t *write_tstamp(uint8_t *p, ngtcp2_tstamp ts) {
  return write_number(p, ts / NGTCP2_MILLISECONDS);
}

static uint8_t *write_duration(uint8_t *p, ngtcp2_duration duration) {
  return write_number(p, duration / NGTCP2_MILLISECONDS);
}

static uint8_t *write_bool(uint8_t *p, int b) {
  if (b) {
    return ngtcp2_cpymem(p, "true", sizeof("true") - 1);
  }
  return ngtcp2_cpymem(p, "false", sizeof("false") - 1);
}

static uint8_t *write_pair_impl(uint8_t *p, const uint8_t *name, size_t namelen,
                                const ngtcp2_vec *value) {
  p = write_string_impl(p, name, namelen);
  *p++ = ':';
  return write_string_impl(p, value->base, value->len);
}

#define write_pair(DEST, NAME, VALUE)                                          \
  write_pair_impl((DEST), (const uint8_t *)(NAME), sizeof(NAME) - 1, (VALUE))

static uint8_t *write_pair_hex_impl(uint8_t *p, const uint8_t *name,
                                    size_t namelen, const uint8_t *value,
                                    size_t valuelen) {
  p = write_string_impl(p, name, namelen);
  *p++ = ':';
  return write_hex(p, value, valuelen);
}

#define write_pair_hex(DEST, NAME, VALUE, VALUELEN)                            \
  write_pair_hex_impl((DEST), (const uint8_t *)(NAME), sizeof(NAME) - 1,       \
                      (VALUE), (VALUELEN))

static uint8_t *write_pair_number_impl(uint8_t *p, const uint8_t *name,
                                       size_t namelen, uint64_t value) {
  p = write_string_impl(p, name, namelen);
  *p++ = ':';
  return write_number(p, value);
}

#define write_pair_number(DEST, NAME, VALUE)                                   \
  write_pair_number_impl((DEST), (const uint8_t *)(NAME), sizeof(NAME) - 1,    \
                         (VALUE))

static uint8_t *write_pair_duration_impl(uint8_t *p, const uint8_t *name,
                                         size_t namelen,
                                         ngtcp2_duration duration) {
  p = write_string_impl(p, name, namelen);
  *p++ = ':';
  return write_duration(p, duration);
}

#define write_pair_duration(DEST, NAME, VALUE)                                 \
  write_pair_duration_impl((DEST), (const uint8_t *)(NAME), sizeof(NAME) - 1,  \
                           (VALUE))

static uint8_t *write_pair_tstamp_impl(uint8_t *p, const uint8_t *name,
                                       size_t namelen, ngtcp2_tstamp ts) {
  p = write_string_impl(p, name, namelen);
  *p++ = ':';
  return write_tstamp(p, ts);
}

#define write_pair_tstamp(DEST, NAME, VALUE)                                   \
  write_pair_tstamp_impl((DEST), (const uint8_t *)(NAME), sizeof(NAME) - 1,    \
                         (VALUE))

static uint8_t *write_pair_bool_impl(uint8_t *p, const uint8_t *name,
                                     size_t namelen, int b) {
  p = write_string_impl(p, name, namelen);
  *p++ = ':';
  return write_bool(p, b);
}

#define write_pair_bool(DEST, NAME, VALUE)                                     \
  write_pair_bool_impl((DEST), (const uint8_t *)(NAME), sizeof(NAME) - 1,      \
                       (VALUE))

static uint8_t *write_pair_cid_impl(uint8_t *p, const uint8_t *name,
                                    size_t namelen, const ngtcp2_cid *cid) {
  p = write_string_impl(p, name, namelen);
  *p++ = ':';
  return write_cid(p, cid);
}

#define write_pair_cid(DEST, NAME, VALUE)                                      \
  write_pair_cid_impl((DEST), (const uint8_t *)(NAME), sizeof(NAME) - 1,       \
                      (VALUE))

#define ngtcp2_make_vec_lit(S) {(uint8_t *)(S), sizeof((S)) - 1}

static uint8_t *write_common_fields(uint8_t *p, const ngtcp2_cid *odcid) {
  p = write_verbatim(
    p, "\"common_fields\":{\"protocol_type\":[\"QUIC\"],\"time_format\":"
       "\"relative\",\"reference_time\":0,\"group_id\":");
  p = write_cid(p, odcid);
  *p++ = '}';
  return p;
}

static uint8_t *write_trace(uint8_t *p, int server, const ngtcp2_cid *odcid) {
  p = write_verbatim(
    p, "\"trace\":{\"vantage_point\":{\"name\":\"ngtcp2\",\"type\":");
  if (server) {
    p = write_string(p, "server");
  } else {
    p = write_string(p, "client");
  }
  p = write_verbatim(p, "},");
  p = write_common_fields(p, odcid);
  *p++ = '}';
  return p;
}

void ngtcp2_qlog_start(ngtcp2_qlog *qlog, const ngtcp2_cid *odcid, int server) {
  uint8_t buf[1024];
  uint8_t *p = buf;

  if (!qlog->write) {
    return;
  }

  p = write_verbatim(
    p, "\x1e{\"qlog_format\":\"JSON-SEQ\",\"qlog_version\":\"0.3\",");
  p = write_trace(p, server, odcid);
  p = write_verbatim(p, "}\n");

  qlog->write(qlog->user_data, NGTCP2_QLOG_WRITE_FLAG_NONE, buf,
              (size_t)(p - buf));
}

void ngtcp2_qlog_end(ngtcp2_qlog *qlog) {
  uint8_t buf[1] = {0};

  if (!qlog->write) {
    return;
  }

  qlog->write(qlog->user_data, NGTCP2_QLOG_WRITE_FLAG_FIN, &buf, 0);
}

static ngtcp2_vec vec_pkt_type_initial = ngtcp2_make_vec_lit("initial");
static ngtcp2_vec vec_pkt_type_handshake = ngtcp2_make_vec_lit("handshake");
static ngtcp2_vec vec_pkt_type_0rtt = ngtcp2_make_vec_lit("0RTT");
static ngtcp2_vec vec_pkt_type_1rtt = ngtcp2_make_vec_lit("1RTT");
static ngtcp2_vec vec_pkt_type_retry = ngtcp2_make_vec_lit("retry");
static ngtcp2_vec vec_pkt_type_version_negotiation =
  ngtcp2_make_vec_lit("version_negotiation");
static ngtcp2_vec vec_pkt_type_stateless_reset =
  ngtcp2_make_vec_lit("stateless_reset");
static ngtcp2_vec vec_pkt_type_unknown = ngtcp2_make_vec_lit("unknown");

static const ngtcp2_vec *qlog_pkt_type(const ngtcp2_pkt_hd *hd) {
  if (hd->flags & NGTCP2_PKT_FLAG_LONG_FORM) {
    switch (hd->type) {
    case NGTCP2_PKT_INITIAL:
      return &vec_pkt_type_initial;
    case NGTCP2_PKT_HANDSHAKE:
      return &vec_pkt_type_handshake;
    case NGTCP2_PKT_0RTT:
      return &vec_pkt_type_0rtt;
    case NGTCP2_PKT_RETRY:
      return &vec_pkt_type_retry;
    default:
      return &vec_pkt_type_unknown;
    }
  }

  switch (hd->type) {
  case NGTCP2_PKT_VERSION_NEGOTIATION:
    return &vec_pkt_type_version_negotiation;
  case NGTCP2_PKT_STATELESS_RESET:
    return &vec_pkt_type_stateless_reset;
  case NGTCP2_PKT_1RTT:
    return &vec_pkt_type_1rtt;
  default:
    return &vec_pkt_type_unknown;
  }
}

static uint8_t *write_pkt_hd(uint8_t *p, const ngtcp2_pkt_hd *hd) {
  /*
   * {"packet_type":"version_negotiation","packet_number":"0000000000000000000","token":{"data":""}}
   */
#define NGTCP2_QLOG_PKT_HD_OVERHEAD 95

  *p++ = '{';
  p = write_pair(p, "packet_type", qlog_pkt_type(hd));
  *p++ = ',';
  p = write_pair_number(p, "packet_number", (uint64_t)hd->pkt_num);
  if (hd->type == NGTCP2_PKT_INITIAL && hd->tokenlen) {
    p = write_verbatim(p, ",\"token\":{");
    p = write_pair_hex(p, "data", hd->token, hd->tokenlen);
    *p++ = '}';
  }
  /* TODO Write DCIL and DCID */
  /* TODO Write SCIL and SCID */
  *p++ = '}';
  return p;
}

static uint8_t *write_padding_frame(uint8_t *p, const ngtcp2_padding *fr) {
  (void)fr;

  /* {"frame_type":"padding"} */
#define NGTCP2_QLOG_PADDING_FRAME_OVERHEAD 24

  return write_verbatim(p, "{\"frame_type\":\"padding\"}");
}

static uint8_t *write_ping_frame(uint8_t *p, const ngtcp2_ping *fr) {
  (void)fr;

  /* {"frame_type":"ping"} */
#define NGTCP2_QLOG_PING_FRAME_OVERHEAD 21

  return write_verbatim(p, "{\"frame_type\":\"ping\"}");
}

static uint8_t *write_ack_frame(uint8_t *p, const ngtcp2_ack *fr) {
  int64_t largest_ack, min_ack;
  size_t i;
  const ngtcp2_ack_range *range;

  /*
   * {"frame_type":"ack","ack_delay":0000000000000000000,"acked_ranges":[]}
   *
   * each range:
   * [0000000000000000000,0000000000000000000],
   *
   * ecn:
   * ,"ect1":0000000000000000000,"ect0":0000000000000000000,"ce":0000000000000000000
   */
#define NGTCP2_QLOG_ACK_FRAME_BASE_OVERHEAD 70
#define NGTCP2_QLOG_ACK_FRAME_RANGE_OVERHEAD 42
#define NGTCP2_QLOG_ACK_FRAME_ECN_OVERHEAD 79

  p = write_verbatim(p, "{\"frame_type\":\"ack\",");
  p = write_pair_duration(p, "ack_delay", fr->ack_delay_unscaled);
  p = write_verbatim(p, ",\"acked_ranges\":[");

  largest_ack = fr->largest_ack;
  min_ack = fr->largest_ack - (int64_t)fr->first_ack_range;

  *p++ = '[';
  p = write_number(p, (uint64_t)min_ack);
  if (largest_ack != min_ack) {
    *p++ = ',';
    p = write_number(p, (uint64_t)largest_ack);
  }
  *p++ = ']';

  for (i = 0; i < fr->rangecnt; ++i) {
    range = &fr->ranges[i];
    largest_ack = min_ack - (int64_t)range->gap - 2;
    min_ack = largest_ack - (int64_t)range->len;
    *p++ = ',';
    *p++ = '[';
    p = write_number(p, (uint64_t)min_ack);
    if (largest_ack != min_ack) {
      *p++ = ',';
      p = write_number(p, (uint64_t)largest_ack);
    }
    *p++ = ']';
  }

  *p++ = ']';

  if (fr->type == NGTCP2_FRAME_ACK_ECN) {
    *p++ = ',';
    p = write_pair_number(p, "ect1", fr->ecn.ect1);
    *p++ = ',';
    p = write_pair_number(p, "ect0", fr->ecn.ect0);
    *p++ = ',';
    p = write_pair_number(p, "ce", fr->ecn.ce);
  }

  *p++ = '}';

  return p;
}

static uint8_t *write_reset_stream_frame(uint8_t *p,
                                         const ngtcp2_reset_stream *fr) {
  /*
   * {"frame_type":"reset_stream","stream_id":0000000000000000000,"error_code":0000000000000000000,"final_size":0000000000000000000}
   */
#define NGTCP2_QLOG_RESET_STREAM_FRAME_OVERHEAD 127

  p = write_verbatim(p, "{\"frame_type\":\"reset_stream\",");
  p = write_pair_number(p, "stream_id", (uint64_t)fr->stream_id);
  *p++ = ',';
  p = write_pair_number(p, "error_code", fr->app_error_code);
  *p++ = ',';
  p = write_pair_number(p, "final_size", fr->final_size);
  *p++ = '}';

  return p;
}

static uint8_t *write_stop_sending_frame(uint8_t *p,
                                         const ngtcp2_stop_sending *fr) {
  /*
   * {"frame_type":"stop_sending","stream_id":0000000000000000000,"error_code":0000000000000000000}
   */
#define NGTCP2_QLOG_STOP_SENDING_FRAME_OVERHEAD 94

  p = write_verbatim(p, "{\"frame_type\":\"stop_sending\",");
  p = write_pair_number(p, "stream_id", (uint64_t)fr->stream_id);
  *p++ = ',';
  p = write_pair_number(p, "error_code", fr->app_error_code);
  *p++ = '}';

  return p;
}

static uint8_t *write_crypto_frame(uint8_t *p, const ngtcp2_stream *fr) {
  /*
   * {"frame_type":"crypto","offset":0000000000000000000,"length":0000000000000000000}
   */
#define NGTCP2_QLOG_CRYPTO_FRAME_OVERHEAD 81

  p = write_verbatim(p, "{\"frame_type\":\"crypto\",");
  p = write_pair_number(p, "offset", fr->offset);
  *p++ = ',';
  p = write_pair_number(p, "length", ngtcp2_vec_len(fr->data, fr->datacnt));
  *p++ = '}';

  return p;
}

static uint8_t *write_new_token_frame(uint8_t *p, const ngtcp2_new_token *fr) {
  /*
   * {"frame_type":"new_token","length":0000000000000000000,"token":{"data":""}}
   */
#define NGTCP2_QLOG_NEW_TOKEN_FRAME_OVERHEAD 75

  p = write_verbatim(p, "{\"frame_type\":\"new_token\",");
  p = write_pair_number(p, "length", fr->tokenlen);
  p = write_verbatim(p, ",\"token\":{");
  p = write_pair_hex(p, "data", fr->token, fr->tokenlen);
  *p++ = '}';
  *p++ = '}';

  return p;
}

static uint8_t *write_stream_frame(uint8_t *p, const ngtcp2_stream *fr) {
  /*
   * {"frame_type":"stream","stream_id":0000000000000000000,"offset":0000000000000000000,"length":0000000000000000000,"fin":true}
   */
#define NGTCP2_QLOG_STREAM_FRAME_OVERHEAD 124

  p = write_verbatim(p, "{\"frame_type\":\"stream\",");
  p = write_pair_number(p, "stream_id", (uint64_t)fr->stream_id);
  *p++ = ',';
  p = write_pair_number(p, "offset", fr->offset);
  *p++ = ',';
  p = write_pair_number(p, "length", ngtcp2_vec_len(fr->data, fr->datacnt));
  if (fr->fin) {
    *p++ = ',';
    p = write_pair_bool(p, "fin", 1);
  }
  *p++ = '}';

  return p;
}

static uint8_t *write_max_data_frame(uint8_t *p, const ngtcp2_max_data *fr) {
  /*
   * {"frame_type":"max_data","maximum":0000000000000000000}
   */
#define NGTCP2_QLOG_MAX_DATA_FRAME_OVERHEAD 55

  p = write_verbatim(p, "{\"frame_type\":\"max_data\",");
  p = write_pair_number(p, "maximum", fr->max_data);
  *p++ = '}';

  return p;
}

static uint8_t *write_max_stream_data_frame(uint8_t *p,
                                            const ngtcp2_max_stream_data *fr) {
  /*
   * {"frame_type":"max_stream_data","stream_id":0000000000000000000,"maximum":0000000000000000000}
   */
#define NGTCP2_QLOG_MAX_STREAM_DATA_FRAME_OVERHEAD 94

  p = write_verbatim(p, "{\"frame_type\":\"max_stream_data\",");
  p = write_pair_number(p, "stream_id", (uint64_t)fr->stream_id);
  *p++ = ',';
  p = write_pair_number(p, "maximum", fr->max_stream_data);
  *p++ = '}';

  return p;
}

static uint8_t *write_max_streams_frame(uint8_t *p,
                                        const ngtcp2_max_streams *fr) {
  /*
   * {"frame_type":"max_streams","stream_type":"unidirectional","maximum":0000000000000000000}
   */
#define NGTCP2_QLOG_MAX_STREAMS_FRAME_OVERHEAD 89

  p = write_verbatim(p, "{\"frame_type\":\"max_streams\",\"stream_type\":");
  if (fr->type == NGTCP2_FRAME_MAX_STREAMS_BIDI) {
    p = write_string(p, "bidirectional");
  } else {
    p = write_string(p, "unidirectional");
  }
  *p++ = ',';
  p = write_pair_number(p, "maximum", fr->max_streams);
  *p++ = '}';

  return p;
}

static uint8_t *write_data_blocked_frame(uint8_t *p,
                                         const ngtcp2_data_blocked *fr) {
  /*
   * {"frame_type":"data_blocked","limit":0000000000000000000}
   */
#define NGTCP2_QLOG_DATA_BLOCKED_FRAME_OVERHEAD 57

  p = write_verbatim(p, "{\"frame_type\":\"data_blocked\",");
  p = write_pair_number(p, "limit", fr->offset);
  *p++ = '}';

  return p;
}

static uint8_t *
write_stream_data_blocked_frame(uint8_t *p,
                                const ngtcp2_stream_data_blocked *fr) {
  /*
   * {"frame_type":"stream_data_blocked","stream_id":0000000000000000000,"limit":0000000000000000000}
   */
#define NGTCP2_QLOG_STREAM_DATA_BLOCKED_FRAME_OVERHEAD 96

  p = write_verbatim(p, "{\"frame_type\":\"stream_data_blocked\",");
  p = write_pair_number(p, "stream_id", (uint64_t)fr->stream_id);
  *p++ = ',';
  p = write_pair_number(p, "limit", fr->offset);
  *p++ = '}';

  return p;
}

static uint8_t *write_streams_blocked_frame(uint8_t *p,
                                            const ngtcp2_streams_blocked *fr) {
  /*
   * {"frame_type":"streams_blocked","stream_type":"unidirectional","limit":0000000000000000000}
   */
#define NGTCP2_QLOG_STREAMS_BLOCKED_FRAME_OVERHEAD 91

  p = write_verbatim(p, "{\"frame_type\":\"streams_blocked\",\"stream_type\":");
  if (fr->type == NGTCP2_FRAME_STREAMS_BLOCKED_BIDI) {
    p = write_string(p, "bidirectional");
  } else {
    p = write_string(p, "unidirectional");
  }
  *p++ = ',';
  p = write_pair_number(p, "limit", fr->max_streams);
  *p++ = '}';

  return p;
}

static uint8_t *
write_new_connection_id_frame(uint8_t *p, const ngtcp2_new_connection_id *fr) {
  /*
   * {"frame_type":"new_connection_id","sequence_number":0000000000000000000,"retire_prior_to":0000000000000000000,"connection_id_length":0000000000000000000,"connection_id":"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx","stateless_reset_token":{"data":"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"}}
   */
#define NGTCP2_QLOG_NEW_CONNECTION_ID_FRAME_OVERHEAD 280

  p = write_verbatim(p, "{\"frame_type\":\"new_connection_id\",");
  p = write_pair_number(p, "sequence_number", fr->seq);
  *p++ = ',';
  p = write_pair_number(p, "retire_prior_to", fr->retire_prior_to);
  *p++ = ',';
  p = write_pair_number(p, "connection_id_length", fr->cid.datalen);
  *p++ = ',';
  p = write_pair_cid(p, "connection_id", &fr->cid);
  p = write_verbatim(p, ",\"stateless_reset_token\":{");
  p = write_pair_hex(p, "data", fr->stateless_reset_token,
                     sizeof(fr->stateless_reset_token));
  *p++ = '}';
  *p++ = '}';

  return p;
}

static uint8_t *
write_retire_connection_id_frame(uint8_t *p,
                                 const ngtcp2_retire_connection_id *fr) {
  /*
   * {"frame_type":"retire_connection_id","sequence_number":0000000000000000000}
   */
#define NGTCP2_QLOG_RETIRE_CONNECTION_ID_FRAME_OVERHEAD 75

  p = write_verbatim(p, "{\"frame_type\":\"retire_connection_id\",");
  p = write_pair_number(p, "sequence_number", fr->seq);
  *p++ = '}';

  return p;
}

static uint8_t *write_path_challenge_frame(uint8_t *p,
                                           const ngtcp2_path_challenge *fr) {
  /*
   * {"frame_type":"path_challenge","data":"xxxxxxxxxxxxxxxx"}
   */
#define NGTCP2_QLOG_PATH_CHALLENGE_FRAME_OVERHEAD 57

  p = write_verbatim(p, "{\"frame_type\":\"path_challenge\",");
  p = write_pair_hex(p, "data", fr->data, sizeof(fr->data));
  *p++ = '}';

  return p;
}

static uint8_t *write_path_response_frame(uint8_t *p,
                                          const ngtcp2_path_response *fr) {
  /*
   * {"frame_type":"path_response","data":"xxxxxxxxxxxxxxxx"}
   */
#define NGTCP2_QLOG_PATH_RESPONSE_FRAME_OVERHEAD 56

  p = write_verbatim(p, "{\"frame_type\":\"path_response\",");
  p = write_pair_hex(p, "data", fr->data, sizeof(fr->data));
  *p++ = '}';

  return p;
}

static uint8_t *
write_connection_close_frame(uint8_t *p, const ngtcp2_connection_close *fr) {
  /*
   * {"frame_type":"connection_close","error_space":"application","error_code":0000000000000000000,"raw_error_code":0000000000000000000}
   */
#define NGTCP2_QLOG_CONNECTION_CLOSE_FRAME_OVERHEAD 131

  p =
    write_verbatim(p, "{\"frame_type\":\"connection_close\",\"error_space\":");
  if (fr->type == NGTCP2_FRAME_CONNECTION_CLOSE) {
    p = write_string(p, "transport");
  } else {
    p = write_string(p, "application");
  }
  *p++ = ',';
  p = write_pair_number(p, "error_code", fr->error_code);
  *p++ = ',';
  p = write_pair_number(p, "raw_error_code", fr->error_code);
  /* TODO Write reason by escaping non-printables */
  /* TODO Write trigger_frame_type */
  *p++ = '}';

  return p;
}

static uint8_t *write_handshake_done_frame(uint8_t *p,
                                           const ngtcp2_handshake_done *fr) {
  (void)fr;

  /*
   * {"frame_type":"handshake_done"}
   */
#define NGTCP2_QLOG_HANDSHAKE_DONE_FRAME_OVERHEAD 31

  return write_verbatim(p, "{\"frame_type\":\"handshake_done\"}");
}

static uint8_t *write_datagram_frame(uint8_t *p, const ngtcp2_datagram *fr) {
  /*
   * {"frame_type":"datagram","length":0000000000000000000}
   */
#define NGTCP2_QLOG_DATAGRAM_FRAME_OVERHEAD 54

  p = write_verbatim(p, "{\"frame_type\":\"datagram\",");
  p = write_pair_number(p, "length", ngtcp2_vec_len(fr->data, fr->datacnt));
  *p++ = '}';

  return p;
}

static uint8_t *qlog_write_time(ngtcp2_qlog *qlog, uint8_t *p) {
  return write_pair_tstamp(p, "time", qlog->last_ts - qlog->ts);
}

static void qlog_pkt_write_start(ngtcp2_qlog *qlog, int sent) {
  uint8_t *p;

  if (!qlog->write) {
    return;
  }

  ngtcp2_buf_reset(&qlog->buf);
  p = qlog->buf.last;

  *p++ = '\x1e';
  *p++ = '{';
  p = qlog_write_time(qlog, p);
  p = write_verbatim(p, ",\"name\":");
  if (sent) {
    p = write_string(p, "transport:packet_sent");
  } else {
    p = write_string(p, "transport:packet_received");
  }
  p = write_verbatim(p, ",\"data\":{\"frames\":[");
  qlog->buf.last = p;
}

static void qlog_pkt_write_end(ngtcp2_qlog *qlog, const ngtcp2_pkt_hd *hd,
                               size_t pktlen) {
  uint8_t *p = qlog->buf.last;

  if (!qlog->write) {
    return;
  }

  /*
   * ],"header":,"raw":{"length":0000000000000000000}}}
   *
   * plus, terminating LF
   */
#define NGTCP2_QLOG_PKT_WRITE_END_OVERHEAD                                     \
  (1 + 50 + NGTCP2_QLOG_PKT_HD_OVERHEAD)

  if (ngtcp2_buf_left(&qlog->buf) <
      NGTCP2_QLOG_PKT_WRITE_END_OVERHEAD + hd->tokenlen * 2) {
    return;
  }

  assert(ngtcp2_buf_len(&qlog->buf));

  /* Eat last ',' */
  if (*(p - 1) == ',') {
    --p;
  }

  p = write_verbatim(p, "],\"header\":");
  p = write_pkt_hd(p, hd);
  p = write_verbatim(p, ",\"raw\":{\"length\":");
  p = write_number(p, pktlen);
  p = write_verbatim(p, "}}}\n");

  qlog->buf.last = p;

  qlog->write(qlog->user_data, NGTCP2_QLOG_WRITE_FLAG_NONE, qlog->buf.pos,
              ngtcp2_buf_len(&qlog->buf));
}

void ngtcp2_qlog_write_frame(ngtcp2_qlog *qlog, const ngtcp2_frame *fr) {
  uint8_t *p = qlog->buf.last;

  if (!qlog->write) {
    return;
  }

  switch (fr->type) {
  case NGTCP2_FRAME_PADDING:
    if (ngtcp2_buf_left(&qlog->buf) < NGTCP2_QLOG_PADDING_FRAME_OVERHEAD + 1) {
      return;
    }
    p = write_padding_frame(p, &fr->padding);
    break;
  case NGTCP2_FRAME_PING:
    if (ngtcp2_buf_left(&qlog->buf) < NGTCP2_QLOG_PING_FRAME_OVERHEAD + 1) {
      return;
    }
    p = write_ping_frame(p, &fr->ping);
    break;
  case NGTCP2_FRAME_ACK:
  case NGTCP2_FRAME_ACK_ECN:
    if (ngtcp2_buf_left(&qlog->buf) <
        NGTCP2_QLOG_ACK_FRAME_BASE_OVERHEAD +
          (size_t)(fr->type == NGTCP2_FRAME_ACK_ECN
                     ? NGTCP2_QLOG_ACK_FRAME_ECN_OVERHEAD
                     : 0) +
          NGTCP2_QLOG_ACK_FRAME_RANGE_OVERHEAD * (1 + fr->ack.rangecnt) + 1) {
      return;
    }
    p = write_ack_frame(p, &fr->ack);
    break;
  case NGTCP2_FRAME_RESET_STREAM:
    if (ngtcp2_buf_left(&qlog->buf) <
        NGTCP2_QLOG_RESET_STREAM_FRAME_OVERHEAD + 1) {
      return;
    }
    p = write_reset_stream_frame(p, &fr->reset_stream);
    break;
  case NGTCP2_FRAME_STOP_SENDING:
    if (ngtcp2_buf_left(&qlog->buf) <
        NGTCP2_QLOG_STOP_SENDING_FRAME_OVERHEAD + 1) {
      return;
    }
    p = write_stop_sending_frame(p, &fr->stop_sending);
    break;
  case NGTCP2_FRAME_CRYPTO:
    if (ngtcp2_buf_left(&qlog->buf) < NGTCP2_QLOG_CRYPTO_FRAME_OVERHEAD + 1) {
      return;
    }
    p = write_crypto_frame(p, &fr->stream);
    break;
  case NGTCP2_FRAME_NEW_TOKEN:
    if (ngtcp2_buf_left(&qlog->buf) <
        NGTCP2_QLOG_NEW_TOKEN_FRAME_OVERHEAD + fr->new_token.tokenlen * 2 + 1) {
      return;
    }
    p = write_new_token_frame(p, &fr->new_token);
    break;
  case NGTCP2_FRAME_STREAM:
    if (ngtcp2_buf_left(&qlog->buf) < NGTCP2_QLOG_STREAM_FRAME_OVERHEAD + 1) {
      return;
    }
    p = write_stream_frame(p, &fr->stream);
    break;
  case NGTCP2_FRAME_MAX_DATA:
    if (ngtcp2_buf_left(&qlog->buf) < NGTCP2_QLOG_MAX_DATA_FRAME_OVERHEAD + 1) {
      return;
    }
    p = write_max_data_frame(p, &fr->max_data);
    break;
  case NGTCP2_FRAME_MAX_STREAM_DATA:
    if (ngtcp2_buf_left(&qlog->buf) <
        NGTCP2_QLOG_MAX_STREAM_DATA_FRAME_OVERHEAD + 1) {
      return;
    }
    p = write_max_stream_data_frame(p, &fr->max_stream_data);
    break;
  case NGTCP2_FRAME_MAX_STREAMS_BIDI:
  case NGTCP2_FRAME_MAX_STREAMS_UNI:
    if (ngtcp2_buf_left(&qlog->buf) <
        NGTCP2_QLOG_MAX_STREAMS_FRAME_OVERHEAD + 1) {
      return;
    }
    p = write_max_streams_frame(p, &fr->max_streams);
    break;
  case NGTCP2_FRAME_DATA_BLOCKED:
    if (ngtcp2_buf_left(&qlog->buf) <
        NGTCP2_QLOG_DATA_BLOCKED_FRAME_OVERHEAD + 1) {
      return;
    }
    p = write_data_blocked_frame(p, &fr->data_blocked);
    break;
  case NGTCP2_FRAME_STREAM_DATA_BLOCKED:
    if (ngtcp2_buf_left(&qlog->buf) <
        NGTCP2_QLOG_STREAM_DATA_BLOCKED_FRAME_OVERHEAD + 1) {
      return;
    }
    p = write_stream_data_blocked_frame(p, &fr->stream_data_blocked);
    break;
  case NGTCP2_FRAME_STREAMS_BLOCKED_BIDI:
  case NGTCP2_FRAME_STREAMS_BLOCKED_UNI:
    if (ngtcp2_buf_left(&qlog->buf) <
        NGTCP2_QLOG_STREAMS_BLOCKED_FRAME_OVERHEAD + 1) {
      return;
    }
    p = write_streams_blocked_frame(p, &fr->streams_blocked);
    break;
  case NGTCP2_FRAME_NEW_CONNECTION_ID:
    if (ngtcp2_buf_left(&qlog->buf) <
        NGTCP2_QLOG_NEW_CONNECTION_ID_FRAME_OVERHEAD + 1) {
      return;
    }
    p = write_new_connection_id_frame(p, &fr->new_connection_id);
    break;
  case NGTCP2_FRAME_RETIRE_CONNECTION_ID:
    if (ngtcp2_buf_left(&qlog->buf) <
        NGTCP2_QLOG_RETIRE_CONNECTION_ID_FRAME_OVERHEAD + 1) {
      return;
    }
    p = write_retire_connection_id_frame(p, &fr->retire_connection_id);
    break;
  case NGTCP2_FRAME_PATH_CHALLENGE:
    if (ngtcp2_buf_left(&qlog->buf) <
        NGTCP2_QLOG_PATH_CHALLENGE_FRAME_OVERHEAD + 1) {
      return;
    }
    p = write_path_challenge_frame(p, &fr->path_challenge);
    break;
  case NGTCP2_FRAME_PATH_RESPONSE:
    if (ngtcp2_buf_left(&qlog->buf) <
        NGTCP2_QLOG_PATH_RESPONSE_FRAME_OVERHEAD + 1) {
      return;
    }
    p = write_path_response_frame(p, &fr->path_response);
    break;
  case NGTCP2_FRAME_CONNECTION_CLOSE:
  case NGTCP2_FRAME_CONNECTION_CLOSE_APP:
    if (ngtcp2_buf_left(&qlog->buf) <
        NGTCP2_QLOG_CONNECTION_CLOSE_FRAME_OVERHEAD + 1) {
      return;
    }
    p = write_connection_close_frame(p, &fr->connection_close);
    break;
  case NGTCP2_FRAME_HANDSHAKE_DONE:
    if (ngtcp2_buf_left(&qlog->buf) <
        NGTCP2_QLOG_HANDSHAKE_DONE_FRAME_OVERHEAD + 1) {
      return;
    }
    p = write_handshake_done_frame(p, &fr->handshake_done);
    break;
  case NGTCP2_FRAME_DATAGRAM:
  case NGTCP2_FRAME_DATAGRAM_LEN:
    if (ngtcp2_buf_left(&qlog->buf) < NGTCP2_QLOG_DATAGRAM_FRAME_OVERHEAD + 1) {
      return;
    }
    p = write_datagram_frame(p, &fr->datagram);
    break;
  default:
    ngtcp2_unreachable();
  }

  *p++ = ',';

  qlog->buf.last = p;
}

void ngtcp2_qlog_pkt_received_start(ngtcp2_qlog *qlog) {
  qlog_pkt_write_start(qlog, /* sent = */ 0);
}

void ngtcp2_qlog_pkt_received_end(ngtcp2_qlog *qlog, const ngtcp2_pkt_hd *hd,
                                  size_t pktlen) {
  qlog_pkt_write_end(qlog, hd, pktlen);
}

void ngtcp2_qlog_pkt_sent_start(ngtcp2_qlog *qlog) {
  qlog_pkt_write_start(qlog, /* sent = */ 1);
}

void ngtcp2_qlog_pkt_sent_end(ngtcp2_qlog *qlog, const ngtcp2_pkt_hd *hd,
                              size_t pktlen) {
  qlog_pkt_write_end(qlog, hd, pktlen);
}

void ngtcp2_qlog_parameters_set_transport_params(
  ngtcp2_qlog *qlog, const ngtcp2_transport_params *params, int server,
  ngtcp2_qlog_side side) {
  uint8_t buf[1024];
  uint8_t *p = buf;
  const ngtcp2_preferred_addr *paddr;
  const ngtcp2_sockaddr_in *sa_in;
  const ngtcp2_sockaddr_in6 *sa_in6;

  if (!qlog->write) {
    return;
  }

  *p++ = '\x1e';
  *p++ = '{';
  p = qlog_write_time(qlog, p);
  p = write_verbatim(
    p, ",\"name\":\"transport:parameters_set\",\"data\":{\"owner\":");

  if (side == NGTCP2_QLOG_SIDE_LOCAL) {
    p = write_string(p, "local");
  } else {
    p = write_string(p, "remote");
  }

  *p++ = ',';
  p = write_pair_cid(p, "initial_source_connection_id", &params->initial_scid);
  *p++ = ',';
  if (side == (server ? NGTCP2_QLOG_SIDE_LOCAL : NGTCP2_QLOG_SIDE_REMOTE)) {
    p = write_pair_cid(p, "original_destination_connection_id",
                       &params->original_dcid);
    *p++ = ',';
  }
  if (params->retry_scid_present) {
    p = write_pair_cid(p, "retry_source_connection_id", &params->retry_scid);
    *p++ = ',';
  }
  if (params->stateless_reset_token_present) {
    p = write_verbatim(p, "\"stateless_reset_token\":{");
    p = write_pair_hex(p, "data", params->stateless_reset_token,
                       sizeof(params->stateless_reset_token));
    *p++ = '}';
    *p++ = ',';
  }
  p = write_pair_bool(p, "disable_active_migration",
                      params->disable_active_migration);
  *p++ = ',';
  p = write_pair_duration(p, "max_idle_timeout", params->max_idle_timeout);
  *p++ = ',';
  p =
    write_pair_number(p, "max_udp_payload_size", params->max_udp_payload_size);
  *p++ = ',';
  p = write_pair_number(p, "ack_delay_exponent", params->ack_delay_exponent);
  *p++ = ',';
  p = write_pair_duration(p, "max_ack_delay", params->max_ack_delay);
  *p++ = ',';
  p = write_pair_number(p, "active_connection_id_limit",
                        params->active_connection_id_limit);
  *p++ = ',';
  p = write_pair_number(p, "initial_max_data", params->initial_max_data);
  *p++ = ',';
  p = write_pair_number(p, "initial_max_stream_data_bidi_local",
                        params->initial_max_stream_data_bidi_local);
  *p++ = ',';
  p = write_pair_number(p, "initial_max_stream_data_bidi_remote",
                        params->initial_max_stream_data_bidi_remote);
  *p++ = ',';
  p = write_pair_number(p, "initial_max_stream_data_uni",
                        params->initial_max_stream_data_uni);
  *p++ = ',';
  p = write_pair_number(p, "initial_max_streams_bidi",
                        params->initial_max_streams_bidi);
  *p++ = ',';
  p = write_pair_number(p, "initial_max_streams_uni",
                        params->initial_max_streams_uni);
  if (params->preferred_addr_present) {
    *p++ = ',';
    paddr = &params->preferred_addr;
    p = write_string(p, "preferred_address");
    *p++ = ':';
    *p++ = '{';

    if (paddr->ipv4_present) {
      sa_in = &paddr->ipv4;

      p = write_pair_hex(p, "ip_v4", (const uint8_t *)&sa_in->sin_addr,
                         sizeof(sa_in->sin_addr));
      *p++ = ',';
      p = write_pair_number(p, "port_v4", ngtcp2_ntohs(sa_in->sin_port));
      *p++ = ',';
    }

    if (paddr->ipv6_present) {
      sa_in6 = &paddr->ipv6;

      p = write_pair_hex(p, "ip_v6", (const uint8_t *)&sa_in6->sin6_addr,
                         sizeof(sa_in6->sin6_addr));
      *p++ = ',';
      p = write_pair_number(p, "port_v6", ngtcp2_ntohs(sa_in6->sin6_port));
      *p++ = ',';
    }

    p = write_pair_cid(p, "connection_id", &paddr->cid);
    p = write_verbatim(p, ",\"stateless_reset_token\":{");
    p = write_pair_hex(p, "data", paddr->stateless_reset_token,
                       sizeof(paddr->stateless_reset_token));
    *p++ = '}';
    *p++ = '}';
  }
  *p++ = ',';
  p = write_pair_number(p, "max_datagram_frame_size",
                        params->max_datagram_frame_size);
  *p++ = ',';
  p = write_pair_bool(p, "grease_quic_bit", params->grease_quic_bit);
  p = write_verbatim(p, "}}\n");

  qlog->write(qlog->user_data, NGTCP2_QLOG_WRITE_FLAG_NONE, buf,
              (size_t)(p - buf));
}

void ngtcp2_qlog_metrics_updated(ngtcp2_qlog *qlog,
                                 const ngtcp2_conn_stat *cstat) {
  uint8_t buf[1024];
  uint8_t *p = buf;

  if (!qlog->write) {
    return;
  }

  *p++ = '\x1e';
  *p++ = '{';
  p = qlog_write_time(qlog, p);
  p = write_verbatim(p, ",\"name\":\"recovery:metrics_updated\",\"data\":{");

  if (cstat->min_rtt != UINT64_MAX) {
    p = write_pair_duration(p, "min_rtt", cstat->min_rtt);
    *p++ = ',';
  }
  p = write_pair_duration(p, "smoothed_rtt", cstat->smoothed_rtt);
  *p++ = ',';
  p = write_pair_duration(p, "latest_rtt", cstat->latest_rtt);
  *p++ = ',';
  p = write_pair_duration(p, "rtt_variance", cstat->rttvar);
  *p++ = ',';
  p = write_pair_number(p, "pto_count", cstat->pto_count);
  *p++ = ',';
  p = write_pair_number(p, "congestion_window", cstat->cwnd);
  *p++ = ',';
  p = write_pair_number(p, "bytes_in_flight", cstat->bytes_in_flight);
  if (cstat->ssthresh != UINT64_MAX) {
    *p++ = ',';
    p = write_pair_number(p, "ssthresh", cstat->ssthresh);
  }

  p = write_verbatim(p, "}}\n");

  qlog->write(qlog->user_data, NGTCP2_QLOG_WRITE_FLAG_NONE, buf,
              (size_t)(p - buf));
}

void ngtcp2_qlog_pkt_lost(ngtcp2_qlog *qlog, ngtcp2_rtb_entry *ent) {
  uint8_t buf[256];
  uint8_t *p = buf;
  ngtcp2_pkt_hd hd = {0};

  if (!qlog->write) {
    return;
  }

  *p++ = '\x1e';
  *p++ = '{';
  p = qlog_write_time(qlog, p);
  p = write_verbatim(
    p, ",\"name\":\"recovery:packet_lost\",\"data\":{\"header\":");

  hd.type = ent->hd.type;
  hd.flags = ent->hd.flags;
  hd.pkt_num = ent->hd.pkt_num;

  p = write_pkt_hd(p, &hd);
  p = write_verbatim(p, "}}\n");

  qlog->write(qlog->user_data, NGTCP2_QLOG_WRITE_FLAG_NONE, buf,
              (size_t)(p - buf));
}

void ngtcp2_qlog_retry_pkt_received(ngtcp2_qlog *qlog, const ngtcp2_pkt_hd *hd,
                                    const ngtcp2_pkt_retry *retry) {
  uint8_t rawbuf[1024];
  ngtcp2_buf buf;

  if (!qlog->write) {
    return;
  }

  ngtcp2_buf_init(&buf, rawbuf, sizeof(rawbuf));

  *buf.last++ = '\x1e';
  *buf.last++ = '{';
  buf.last = qlog_write_time(qlog, buf.last);
  buf.last = write_verbatim(
    buf.last, ",\"name\":\"transport:packet_received\",\"data\":{\"header\":");

  if (ngtcp2_buf_left(&buf) < NGTCP2_QLOG_PKT_HD_OVERHEAD + hd->tokenlen * 2 +
                                sizeof(",\"retry_token\":{\"data\":\"\"}}}\n") -
                                1 + retry->tokenlen * 2) {
    return;
  }

  buf.last = write_pkt_hd(buf.last, hd);
  buf.last = write_verbatim(buf.last, ",\"retry_token\":{");
  buf.last = write_pair_hex(buf.last, "data", retry->token, retry->tokenlen);
  buf.last = write_verbatim(buf.last, "}}}\n");

  qlog->write(qlog->user_data, NGTCP2_QLOG_WRITE_FLAG_NONE, buf.pos,
              ngtcp2_buf_len(&buf));
}

void ngtcp2_qlog_stateless_reset_pkt_received(
  ngtcp2_qlog *qlog, const ngtcp2_pkt_stateless_reset *sr) {
  uint8_t buf[256];
  uint8_t *p = buf;
  ngtcp2_pkt_hd hd = {0};

  if (!qlog->write) {
    return;
  }

  hd.type = NGTCP2_PKT_STATELESS_RESET;

  *p++ = '\x1e';
  *p++ = '{';
  p = qlog_write_time(qlog, p);
  p = write_verbatim(
    p, ",\"name\":\"transport:packet_received\",\"data\":{\"header\":");
  p = write_pkt_hd(p, &hd);
  *p++ = ',';
  p = write_pair_hex(p, "stateless_reset_token", sr->stateless_reset_token,
                     NGTCP2_STATELESS_RESET_TOKENLEN);
  p = write_verbatim(p, "}}\n");

  qlog->write(qlog->user_data, NGTCP2_QLOG_WRITE_FLAG_NONE, buf,
              (size_t)(p - buf));
}

void ngtcp2_qlog_version_negotiation_pkt_received(ngtcp2_qlog *qlog,
                                                  const ngtcp2_pkt_hd *hd,
                                                  const uint32_t *sv,
                                                  size_t nsv) {
  uint8_t rawbuf[512];
  ngtcp2_buf buf;
  size_t i;
  uint32_t v;

  if (!qlog->write) {
    return;
  }

  ngtcp2_buf_init(&buf, rawbuf, sizeof(rawbuf));

  *buf.last++ = '\x1e';
  *buf.last++ = '{';
  buf.last = qlog_write_time(qlog, buf.last);
  buf.last = write_verbatim(
    buf.last, ",\"name\":\"transport:packet_received\",\"data\":{\"header\":");
  buf.last = write_pkt_hd(buf.last, hd);
  buf.last = write_verbatim(buf.last, ",\"supported_versions\":[");

  if (nsv) {
    if (ngtcp2_buf_left(&buf) <
        (sizeof("\"xxxxxxxx\",") - 1) * nsv - 1 + sizeof("]}}\n") - 1) {
      return;
    }

    v = ngtcp2_htonl(sv[0]);
    buf.last = write_hex(buf.last, (const uint8_t *)&v, sizeof(v));

    for (i = 1; i < nsv; ++i) {
      *buf.last++ = ',';
      v = ngtcp2_htonl(sv[i]);
      buf.last = write_hex(buf.last, (const uint8_t *)&v, sizeof(v));
    }
  }

  buf.last = write_verbatim(buf.last, "]}}\n");

  qlog->write(qlog->user_data, NGTCP2_QLOG_WRITE_FLAG_NONE, buf.pos,
              ngtcp2_buf_len(&buf));
}
