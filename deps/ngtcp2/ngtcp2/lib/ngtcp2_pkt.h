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
#ifndef NGTCP2_PKT_H
#define NGTCP2_PKT_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <ngtcp2/ngtcp2.h>

/* QUIC header macros */
#define NGTCP2_HEADER_FORM_BIT 0x80
#define NGTCP2_FIXED_BIT_MASK 0x40
#define NGTCP2_PKT_NUMLEN_MASK 0x03

/* Long header specific macros */
#define NGTCP2_LONG_TYPE_MASK 0x30
#define NGTCP2_LONG_RESERVED_BIT_MASK 0x0c

/* Short header specific macros */
#define NGTCP2_SHORT_RESERVED_BIT_MASK 0x18
#define NGTCP2_SHORT_KEY_PHASE_BIT 0x04

/* NGTCP2_SR_TYPE is a Type field of Stateless Reset. */
#define NGTCP2_SR_TYPE 0x1f

/* NGTCP2_MIN_LONG_HEADERLEN is the minimum length of long header.
   That is (1|1|TT|RR|PP)<1> + VERSION<4> + DCIL<1> + SCIL<1> +
   LENGTH<1> + PKN<1> */
#define NGTCP2_MIN_LONG_HEADERLEN (1 + 4 + 1 + 1 + 1 + 1)

/* STREAM frame specific macros */
#define NGTCP2_STREAM_FIN_BIT 0x01
#define NGTCP2_STREAM_LEN_BIT 0x02
#define NGTCP2_STREAM_OFF_BIT 0x04

/* NGTCP2_STREAM_OVERHEAD is the maximum number of bytes required
   other than payload for STREAM frame.  That is from type field to
   the beginning of the payload. */
#define NGTCP2_STREAM_OVERHEAD (1 + 8 + 8 + 8)

/* NGTCP2_CRYPTO_OVERHEAD is the maximum number of bytes required
   other than payload for CRYPTO frame.  That is from type field to
   the beginning of the payload. */
#define NGTCP2_CRYPTO_OVERHEAD (1 + 8 + 8)

/* NGTCP2_DATAGRAM_OVERHEAD is the maximum number of bytes required
   other than payload for DATAGRAM frame.  That is from type field to
   the beginning of the payload. */
#define NGTCP2_DATAGRAM_OVERHEAD (1 + 8)

/* NGTCP2_MAX_SERVER_STREAM_ID_BIDI is the maximum bidirectional
   server stream ID. */
#define NGTCP2_MAX_SERVER_STREAM_ID_BIDI ((int64_t)0x3ffffffffffffffdll)
/* NGTCP2_MAX_CLIENT_STREAM_ID_BIDI is the maximum bidirectional
   client stream ID. */
#define NGTCP2_MAX_CLIENT_STREAM_ID_BIDI ((int64_t)0x3ffffffffffffffcll)
/* NGTCP2_MAX_SERVER_STREAM_ID_UNI is the maximum unidirectional
   server stream ID. */
#define NGTCP2_MAX_SERVER_STREAM_ID_UNI ((int64_t)0x3fffffffffffffffll)
/* NGTCP2_MAX_CLIENT_STREAM_ID_UNI is the maximum unidirectional
   client stream ID. */
#define NGTCP2_MAX_CLIENT_STREAM_ID_UNI ((int64_t)0x3ffffffffffffffell)

/* NGTCP2_MAX_NUM_ACK_RANGES is the maximum number of Additional ACK
   ranges which this library can create, or decode. */
#define NGTCP2_MAX_ACK_RANGES 32

/* NGTCP2_MAX_PKT_NUM is the maximum packet number. */
#define NGTCP2_MAX_PKT_NUM ((int64_t)((1ll << 62) - 1))

/* NGTCP2_MIN_PKT_EXPANDLEN is the minimum packet size expansion to
   hide/trigger Stateless Reset. */
#define NGTCP2_MIN_PKT_EXPANDLEN 22

/* NGTCP2_RETRY_TAGLEN is the length of Retry packet integrity tag. */
#define NGTCP2_RETRY_TAGLEN 16

/* NGTCP2_HARD_MAX_UDP_PAYLOAD_SIZE is the maximum UDP datagram
   payload size that this library can write. */
#define NGTCP2_HARD_MAX_UDP_PAYLOAD_SIZE ((1 << 24) - 1)

/* NGTCP2_PKT_LENGTHLEN is the number of bytes that is occupied by
   Length field in Long packet header. */
#define NGTCP2_PKT_LENGTHLEN 4

/* NGTCP2_PKT_TYPE_INITIAL_V1 is Initial long header packet type for
   QUIC v1. */
#define NGTCP2_PKT_TYPE_INITIAL_V1 0x0
/* NGTCP2_PKT_TYPE_0RTT_V1 is 0RTT long header packet type for QUIC
   v1. */
#define NGTCP2_PKT_TYPE_0RTT_V1 0x1
/* NGTCP2_PKT_TYPE_HANDSHAKE_V1 is Handshake long header packet type
   for QUIC v1. */
#define NGTCP2_PKT_TYPE_HANDSHAKE_V1 0x2
/* NGTCP2_PKT_TYPE_RETRY_V1 is Retry long header packet type for QUIC
   v1. */
#define NGTCP2_PKT_TYPE_RETRY_V1 0x3

/* NGTCP2_PKT_TYPE_INITIAL_V2 is Initial long header packet type for
   QUIC v2. */
#define NGTCP2_PKT_TYPE_INITIAL_V2 0x1
/* NGTCP2_PKT_TYPE_0RTT_V2 is 0RTT long header packet type for QUIC
   v2. */
#define NGTCP2_PKT_TYPE_0RTT_V2 0x2
/* NGTCP2_PKT_TYPE_HANDSHAKE_V2 is Handshake long header packet type
   for QUIC v2. */
#define NGTCP2_PKT_TYPE_HANDSHAKE_V2 0x3
/* NGTCP2_PKT_TYPE_RETRY_V2 is Retry long header packet type for QUIC
   v2. */
#define NGTCP2_PKT_TYPE_RETRY_V2 0x0

typedef struct ngtcp2_pkt_retry {
  ngtcp2_cid odcid;
  uint8_t *token;
  size_t tokenlen;
  uint8_t tag[NGTCP2_RETRY_TAGLEN];
} ngtcp2_pkt_retry;

#define NGTCP2_FRAME_PADDING 0x00
#define NGTCP2_FRAME_PING 0x01
#define NGTCP2_FRAME_ACK 0x02
#define NGTCP2_FRAME_ACK_ECN 0x03
#define NGTCP2_FRAME_RESET_STREAM 0x04
#define NGTCP2_FRAME_STOP_SENDING 0x05
#define NGTCP2_FRAME_CRYPTO 0x06
#define NGTCP2_FRAME_NEW_TOKEN 0x07
#define NGTCP2_FRAME_STREAM 0x08
#define NGTCP2_FRAME_MAX_DATA 0x10
#define NGTCP2_FRAME_MAX_STREAM_DATA 0x11
#define NGTCP2_FRAME_MAX_STREAMS_BIDI 0x12
#define NGTCP2_FRAME_MAX_STREAMS_UNI 0x13
#define NGTCP2_FRAME_DATA_BLOCKED 0x14
#define NGTCP2_FRAME_STREAM_DATA_BLOCKED 0x15
#define NGTCP2_FRAME_STREAMS_BLOCKED_BIDI 0x16
#define NGTCP2_FRAME_STREAMS_BLOCKED_UNI 0x17
#define NGTCP2_FRAME_NEW_CONNECTION_ID 0x18
#define NGTCP2_FRAME_RETIRE_CONNECTION_ID 0x19
#define NGTCP2_FRAME_PATH_CHALLENGE 0x1a
#define NGTCP2_FRAME_PATH_RESPONSE 0x1b
#define NGTCP2_FRAME_CONNECTION_CLOSE 0x1c
#define NGTCP2_FRAME_CONNECTION_CLOSE_APP 0x1d
#define NGTCP2_FRAME_HANDSHAKE_DONE 0x1e
#define NGTCP2_FRAME_DATAGRAM 0x30
#define NGTCP2_FRAME_DATAGRAM_LEN 0x31

/* ngtcp2_stream represents STREAM and CRYPTO frames. */
typedef struct ngtcp2_stream {
  uint64_t type;
  /**
   * flags of decoded STREAM frame.  This gets ignored when encoding
   * STREAM frame.  CRYPTO frame does not include this field, and must
   * set it to 0.
   */
  uint8_t flags;
  /* CRYPTO frame does not include this field, and must set it to
     0. */
  uint8_t fin;
  /* CRYPTO frame does not include this field, and must set it to
     0. */
  int64_t stream_id;
  uint64_t offset;
  /* datacnt is the number of elements that data contains.  Although
     the length of data is 1 in this definition, the library may
     allocate extra bytes to hold more elements. */
  size_t datacnt;
  /* data is the array of ngtcp2_vec which references data. */
  ngtcp2_vec data[1];
} ngtcp2_stream;

typedef struct ngtcp2_ack_range {
  uint64_t gap;
  uint64_t len;
} ngtcp2_ack_range;

typedef struct ngtcp2_ack {
  uint64_t type;
  int64_t largest_ack;
  uint64_t ack_delay;
  /**
   * ack_delay_unscaled is an ack_delay multiplied by
   * 2**ack_delay_component * NGTCP2_MICROSECONDS.
   */
  ngtcp2_duration ack_delay_unscaled;
  struct {
    uint64_t ect0;
    uint64_t ect1;
    uint64_t ce;
  } ecn;
  uint64_t first_ack_range;
  size_t rangecnt;
  ngtcp2_ack_range ranges[1];
} ngtcp2_ack;

typedef struct ngtcp2_padding {
  uint64_t type;
  /**
   * The length of contiguous PADDING frames.
   */
  size_t len;
} ngtcp2_padding;

typedef struct ngtcp2_reset_stream {
  uint64_t type;
  int64_t stream_id;
  uint64_t app_error_code;
  uint64_t final_size;
} ngtcp2_reset_stream;

typedef struct ngtcp2_connection_close {
  uint64_t type;
  uint64_t error_code;
  uint64_t frame_type;
  size_t reasonlen;
  uint8_t *reason;
} ngtcp2_connection_close;

typedef struct ngtcp2_max_data {
  uint64_t type;
  /**
   * max_data is Maximum Data.
   */
  uint64_t max_data;
} ngtcp2_max_data;

typedef struct ngtcp2_max_stream_data {
  uint64_t type;
  int64_t stream_id;
  uint64_t max_stream_data;
} ngtcp2_max_stream_data;

typedef struct ngtcp2_max_streams {
  uint64_t type;
  uint64_t max_streams;
} ngtcp2_max_streams;

typedef struct ngtcp2_ping {
  uint64_t type;
} ngtcp2_ping;

typedef struct ngtcp2_data_blocked {
  uint64_t type;
  uint64_t offset;
} ngtcp2_data_blocked;

typedef struct ngtcp2_stream_data_blocked {
  uint64_t type;
  int64_t stream_id;
  uint64_t offset;
} ngtcp2_stream_data_blocked;

typedef struct ngtcp2_streams_blocked {
  uint64_t type;
  uint64_t max_streams;
} ngtcp2_streams_blocked;

typedef struct ngtcp2_new_connection_id {
  uint64_t type;
  uint64_t seq;
  uint64_t retire_prior_to;
  ngtcp2_cid cid;
  uint8_t stateless_reset_token[NGTCP2_STATELESS_RESET_TOKENLEN];
} ngtcp2_new_connection_id;

typedef struct ngtcp2_stop_sending {
  uint64_t type;
  int64_t stream_id;
  uint64_t app_error_code;
} ngtcp2_stop_sending;

typedef struct ngtcp2_path_challenge {
  uint64_t type;
  uint8_t data[NGTCP2_PATH_CHALLENGE_DATALEN];
} ngtcp2_path_challenge;

typedef struct ngtcp2_path_response {
  uint64_t type;
  uint8_t data[NGTCP2_PATH_CHALLENGE_DATALEN];
} ngtcp2_path_response;

typedef struct ngtcp2_new_token {
  uint64_t type;
  uint8_t *token;
  size_t tokenlen;
} ngtcp2_new_token;

typedef struct ngtcp2_retire_connection_id {
  uint64_t type;
  uint64_t seq;
} ngtcp2_retire_connection_id;

typedef struct ngtcp2_handshake_done {
  uint64_t type;
} ngtcp2_handshake_done;

typedef struct ngtcp2_datagram {
  uint64_t type;
  /* dgram_id is an opaque identifier chosen by an application. */
  uint64_t dgram_id;
  /* datacnt is the number of elements that data contains. */
  size_t datacnt;
  /* data is a pointer to ngtcp2_vec array that stores data. */
  ngtcp2_vec *data;
  /* rdata is conveniently embedded to ngtcp2_datagram, so that data
     field can just point to the address of this field to store a
     single vector which is the case when DATAGRAM is received from a
     remote endpoint. */
  ngtcp2_vec rdata[1];
} ngtcp2_datagram;

typedef union ngtcp2_frame {
  uint64_t type;
  ngtcp2_stream stream;
  ngtcp2_ack ack;
  ngtcp2_padding padding;
  ngtcp2_reset_stream reset_stream;
  ngtcp2_connection_close connection_close;
  ngtcp2_max_data max_data;
  ngtcp2_max_stream_data max_stream_data;
  ngtcp2_max_streams max_streams;
  ngtcp2_ping ping;
  ngtcp2_data_blocked data_blocked;
  ngtcp2_stream_data_blocked stream_data_blocked;
  ngtcp2_streams_blocked streams_blocked;
  ngtcp2_new_connection_id new_connection_id;
  ngtcp2_stop_sending stop_sending;
  ngtcp2_path_challenge path_challenge;
  ngtcp2_path_response path_response;
  ngtcp2_new_token new_token;
  ngtcp2_retire_connection_id retire_connection_id;
  ngtcp2_handshake_done handshake_done;
  ngtcp2_datagram datagram;
  /* Extend ngtcp2_frame so that ngtcp2_stream has at least additional
     3 ngtcp2_vec, totaling 4 slots, which can store HEADERS header,
     HEADERS payload, DATA header, and DATA payload in the standard
     sized ngtcp2_frame_chain. */
  uint8_t pad[sizeof(ngtcp2_stream) + sizeof(ngtcp2_vec) * 3];
} ngtcp2_frame;

typedef struct ngtcp2_pkt_chain ngtcp2_pkt_chain;

/*
 * ngtcp2_pkt_chain is the chain of incoming packets buffered.
 */
struct ngtcp2_pkt_chain {
  ngtcp2_path_storage path;
  ngtcp2_pkt_info pi;
  ngtcp2_pkt_chain *next;
  uint8_t *pkt;
  /* pktlen is length of a QUIC packet. */
  size_t pktlen;
  /* dgramlen is length of UDP datagram payload that a QUIC packet is
     included. */
  size_t dgramlen;
  ngtcp2_tstamp ts;
};

/*
 * ngtcp2_pkt_chain_new allocates ngtcp2_pkt_chain objects, and
 * assigns its pointer to |*ppc|.  The content of buffer pointed by
 * |pkt| of length |pktlen| is copied into |*ppc|.  The packet is
 * obtained via the network |path|.  The values of path->local and
 * path->remote are copied into |*ppc|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 */
int ngtcp2_pkt_chain_new(ngtcp2_pkt_chain **ppc, const ngtcp2_path *path,
                         const ngtcp2_pkt_info *pi, const uint8_t *pkt,
                         size_t pktlen, size_t dgramlen, ngtcp2_tstamp ts,
                         const ngtcp2_mem *mem);

/*
 * ngtcp2_pkt_chain_del deallocates |pc|.  It also frees the memory
 * pointed by |pc|.
 */
void ngtcp2_pkt_chain_del(ngtcp2_pkt_chain *pc, const ngtcp2_mem *mem);

/*
 * ngtcp2_pkt_hd_init initializes |hd| with the given values.  If
 * |dcid| and/or |scid| is NULL, Destination Connection ID and/or
 * Source Connection ID of |hd| is empty respectively.  |pkt_numlen|
 * is the number of bytes used to encode |pkt_num| and either 1, 2, or
 * 4.  |version| is QUIC version for long header.  |len| is the length
 * field of Initial, 0RTT, and Handshake packets.
 */
void ngtcp2_pkt_hd_init(ngtcp2_pkt_hd *hd, uint8_t flags, uint8_t type,
                        const ngtcp2_cid *dcid, const ngtcp2_cid *scid,
                        int64_t pkt_num, size_t pkt_numlen, uint32_t version,
                        size_t len);

/*
 * ngtcp2_pkt_encode_hd_long encodes |hd| as QUIC long header into
 * |out| which has length |outlen|.  It returns the number of bytes
 * written into |out| if it succeeds, or one of the following negative
 * error codes:
 *
 * NGTCP2_ERR_NOBUF
 *     Buffer is too short
 */
ngtcp2_ssize ngtcp2_pkt_encode_hd_long(uint8_t *out, size_t outlen,
                                       const ngtcp2_pkt_hd *hd);

/*
 * ngtcp2_pkt_encode_hd_short encodes |hd| as QUIC short header into
 * |out| which has length |outlen|.  It returns the number of bytes
 * written into |out| if it succeeds, or one of the following negative
 * error codes:
 *
 * NGTCP2_ERR_NOBUF
 *     Buffer is too short
 */
ngtcp2_ssize ngtcp2_pkt_encode_hd_short(uint8_t *out, size_t outlen,
                                        const ngtcp2_pkt_hd *hd);

/**
 * @function
 *
 * `ngtcp2_pkt_decode_frame` decodes a QUIC frame from the buffer
 * pointed by |payload| whose length is |payloadlen|.
 *
 * This function returns the number of bytes read to decode a single
 * frame if it succeeds, or one of the following negative error codes:
 *
 * :enum:`NGTCP2_ERR_FRAME_ENCODING`
 *     Frame is badly formatted; or frame type is unknown; or
 *     |payloadlen| is 0.
 */
ngtcp2_ssize ngtcp2_pkt_decode_frame(ngtcp2_frame *dest, const uint8_t *payload,
                                     size_t payloadlen);

/**
 * @function
 *
 * `ngtcp2_pkt_encode_frame` encodes a frame |fr| into the buffer
 * pointed by |out| of length |outlen|.
 *
 * This function returns the number of bytes written to the buffer, or
 * one of the following negative error codes:
 *
 * :enum:`NGTCP2_ERR_NOBUF`
 *     Buffer does not have enough capacity to write a frame.
 */
ngtcp2_ssize ngtcp2_pkt_encode_frame(uint8_t *out, size_t outlen,
                                     ngtcp2_frame *fr);

/*
 * ngtcp2_pkt_decode_version_negotiation decodes Version Negotiation
 * packet payload |payload| of length |payloadlen|, and stores the
 * result in |dest|.  |dest| must have enough capacity to store the
 * result.  |payloadlen| also must be a multiple of sizeof(uint32_t).
 *
 * This function returns the number of versions written in |dest|.
 */
size_t ngtcp2_pkt_decode_version_negotiation(uint32_t *dest,
                                             const uint8_t *payload,
                                             size_t payloadlen);

/*
 * ngtcp2_pkt_decode_stateless_reset decodes Stateless Reset payload
 * |payload| of length |payloadlen|.  The |payload| must start with
 * Stateless Reset Token.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_INVALID_ARGUMENT
 *     Payloadlen is too short.
 */
int ngtcp2_pkt_decode_stateless_reset(ngtcp2_pkt_stateless_reset *sr,
                                      const uint8_t *payload,
                                      size_t payloadlen);

/*
 * ngtcp2_pkt_decode_retry decodes Retry packet payload |payload| of
 * length |payloadlen|.  The |payload| must start with Retry token
 * field.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_INVALID_ARGUMENT
 *     Payloadlen is too short.
 */
int ngtcp2_pkt_decode_retry(ngtcp2_pkt_retry *dest, const uint8_t *payload,
                            size_t payloadlen);

/*
 * ngtcp2_pkt_decode_stream_frame decodes STREAM frame from |payload|
 * of length |payloadlen|.  The result is stored in the object pointed
 * by |dest|.  STREAM frame must start at payload[0].  This function
 * finishes when it decodes one STREAM frame, and returns the exact
 * number of bytes read to decode a frame if it succeeds, or one of
 * the following negative error codes:
 *
 * NGTCP2_ERR_FRAME_ENCODING
 *     Payload is too short to include STREAM frame.
 */
ngtcp2_ssize ngtcp2_pkt_decode_stream_frame(ngtcp2_stream *dest,
                                            const uint8_t *payload,
                                            size_t payloadlen);

/*
 * ngtcp2_pkt_decode_ack_frame decodes ACK frame from |payload| of
 * length |payloadlen|.  The result is stored in the object pointed by
 * |dest|.  ACK frame must start at payload[0].  This function
 * finishes when it decodes one ACK frame, and returns the exact
 * number of bytes read to decode a frame if it succeeds, or one of
 * the following negative error codes:
 *
 * NGTCP2_ERR_FRAME_ENCODING
 *     Payload is too short to include ACK frame.
 */
ngtcp2_ssize ngtcp2_pkt_decode_ack_frame(ngtcp2_ack *dest,
                                         const uint8_t *payload,
                                         size_t payloadlen);

/*
 * ngtcp2_pkt_decode_padding_frame decodes contiguous PADDING frames
 * from |payload| of length |payloadlen|.  It continues to parse
 * frames as long as the frame type is PADDING.  It finishes when it
 * encounters the frame type which is not PADDING, or all input data
 * is read.  The first byte (payload[0]) must be NGTCP2_FRAME_PADDING.
 * This function returns the exact number of bytes read to decode
 * PADDING frames.
 */
ngtcp2_ssize ngtcp2_pkt_decode_padding_frame(ngtcp2_padding *dest,
                                             const uint8_t *payload,
                                             size_t payloadlen);

/*
 * ngtcp2_pkt_decode_reset_stream_frame decodes RESET_STREAM frame
 * from |payload| of length |payloadlen|.  The result is stored in the
 * object pointed by |dest|.  RESET_STREAM frame must start at
 * payload[0].  This function finishes when it decodes one
 * RESET_STREAM frame, and returns the exact number of bytes read to
 * decode a frame if it succeeds, or one of the following negative
 * error codes:
 *
 * NGTCP2_ERR_FRAME_ENCODING
 *     Payload is too short to include RESET_STREAM frame.
 */
ngtcp2_ssize ngtcp2_pkt_decode_reset_stream_frame(ngtcp2_reset_stream *dest,
                                                  const uint8_t *payload,
                                                  size_t payloadlen);

/*
 * ngtcp2_pkt_decode_connection_close_frame decodes CONNECTION_CLOSE
 * frame from |payload| of length |payloadlen|.  The result is stored
 * in the object pointed by |dest|.  CONNECTION_CLOSE frame must start
 * at payload[0].  This function finishes when it decodes one
 * CONNECTION_CLOSE frame, and returns the exact number of bytes read
 * to decode a frame if it succeeds, or one of the following negative
 * error codes:
 *
 * NGTCP2_ERR_FRAME_ENCODING
 *     Payload is too short to include CONNECTION_CLOSE frame.
 */
ngtcp2_ssize ngtcp2_pkt_decode_connection_close_frame(
  ngtcp2_connection_close *dest, const uint8_t *payload, size_t payloadlen);

/*
 * ngtcp2_pkt_decode_max_data_frame decodes MAX_DATA frame from
 * |payload| of length |payloadlen|.  The result is stored in the
 * object pointed by |dest|.  MAX_DATA frame must start at payload[0].
 * This function finishes when it decodes one MAX_DATA frame, and
 * returns the exact number of bytes read to decode a frame if it
 * succeeds, or one of the following negative error codes:
 *
 * NGTCP2_ERR_FRAME_ENCODING
 *     Payload is too short to include MAX_DATA frame.
 */
ngtcp2_ssize ngtcp2_pkt_decode_max_data_frame(ngtcp2_max_data *dest,
                                              const uint8_t *payload,
                                              size_t payloadlen);

/*
 * ngtcp2_pkt_decode_max_stream_data_frame decodes MAX_STREAM_DATA
 * frame from |payload| of length |payloadlen|.  The result is stored
 * in the object pointed by |dest|.  MAX_STREAM_DATA frame must start
 * at payload[0].  This function finishes when it decodes one
 * MAX_STREAM_DATA frame, and returns the exact number of bytes read
 * to decode a frame if it succeeds, or one of the following negative
 * error codes:
 *
 * NGTCP2_ERR_FRAME_ENCODING
 *     Payload is too short to include MAX_STREAM_DATA frame.
 */
ngtcp2_ssize ngtcp2_pkt_decode_max_stream_data_frame(
  ngtcp2_max_stream_data *dest, const uint8_t *payload, size_t payloadlen);

/*
 * ngtcp2_pkt_decode_max_streams_frame decodes MAX_STREAMS frame from
 * |payload| of length |payloadlen|.  The result is stored in the
 * object pointed by |dest|.  MAX_STREAMS frame must start at
 * payload[0].  This function finishes when it decodes one MAX_STREAMS
 * frame, and returns the exact number of bytes read to decode a frame
 * if it succeeds, or one of the following negative error codes:
 *
 * NGTCP2_ERR_FRAME_ENCODING
 *     Payload is too short to include MAX_STREAMS frame.
 */
ngtcp2_ssize ngtcp2_pkt_decode_max_streams_frame(ngtcp2_max_streams *dest,
                                                 const uint8_t *payload,
                                                 size_t payloadlen);

/*
 * ngtcp2_pkt_decode_ping_frame decodes PING frame from |payload| of
 * length |payloadlen|.  The result is stored in the object pointed by
 * |dest|.  PING frame must start at payload[0].  This function
 * finishes when it decodes one PING frame, and returns the exact
 * number of bytes read to decode a frame.
 */
ngtcp2_ssize ngtcp2_pkt_decode_ping_frame(ngtcp2_ping *dest,
                                          const uint8_t *payload,
                                          size_t payloadlen);

/*
 * ngtcp2_pkt_decode_data_blocked_frame decodes DATA_BLOCKED frame
 * from |payload| of length |payloadlen|.  The result is stored in the
 * object pointed by |dest|.  DATA_BLOCKED frame must start at
 * payload[0].  This function finishes when it decodes one
 * DATA_BLOCKED frame, and returns the exact number of bytes read to
 * decode a frame if it succeeds, or one of the following negative
 * error codes:
 *
 * NGTCP2_ERR_FRAME_ENCODING
 *     Payload is too short to include DATA_BLOCKED frame.
 */
ngtcp2_ssize ngtcp2_pkt_decode_data_blocked_frame(ngtcp2_data_blocked *dest,
                                                  const uint8_t *payload,
                                                  size_t payloadlen);

/*
 * ngtcp2_pkt_decode_stream_data_blocked_frame decodes
 * STREAM_DATA_BLOCKED frame from |payload| of length |payloadlen|.
 * The result is stored in the object pointed by |dest|.
 * STREAM_DATA_BLOCKED frame must start at payload[0].  This function
 * finishes when it decodes one STREAM_DATA_BLOCKED frame, and returns
 * the exact number of bytes read to decode a frame if it succeeds, or
 * one of the following negative error codes:
 *
 * NGTCP2_ERR_FRAME_ENCODING
 *     Payload is too short to include STREAM_DATA_BLOCKED frame.
 */
ngtcp2_ssize ngtcp2_pkt_decode_stream_data_blocked_frame(
  ngtcp2_stream_data_blocked *dest, const uint8_t *payload, size_t payloadlen);

/*
 * ngtcp2_pkt_decode_streams_blocked_frame decodes STREAMS_BLOCKED
 * frame from |payload| of length |payloadlen|.  The result is stored
 * in the object pointed by |dest|.  STREAMS_BLOCKED frame must start
 * at payload[0].  This function finishes when it decodes one
 * STREAMS_BLOCKED frame, and returns the exact number of bytes read
 * to decode a frame if it succeeds, or one of the following negative
 * error codes:
 *
 * NGTCP2_ERR_FRAME_ENCODING
 *     Payload is too short to include STREAMS_BLOCKED frame.
 */
ngtcp2_ssize ngtcp2_pkt_decode_streams_blocked_frame(
  ngtcp2_streams_blocked *dest, const uint8_t *payload, size_t payloadlen);

/*
 * ngtcp2_pkt_decode_new_connection_id_frame decodes NEW_CONNECTION_ID
 * frame from |payload| of length |payloadlen|.  The result is stored
 * in the object pointed by |dest|.  NEW_CONNECTION_ID frame must
 * start at payload[0].  This function finishes when it decodes one
 * NEW_CONNECTION_ID frame, and returns the exact number of bytes read
 * to decode a frame if it succeeds, or one of the following negative
 * error codes:
 *
 * NGTCP2_ERR_FRAME_ENCODING
 *     Payload is too short to include NEW_CONNECTION_ID frame; or the
 *     length of Connection ID is strictly less than NGTCP2_MIN_CIDLEN
 *     or greater than NGTCP2_MAX_CIDLEN.
 */
ngtcp2_ssize ngtcp2_pkt_decode_new_connection_id_frame(
  ngtcp2_new_connection_id *dest, const uint8_t *payload, size_t payloadlen);

/*
 * ngtcp2_pkt_decode_stop_sending_frame decodes STOP_SENDING frame
 * from |payload| of length |payloadlen|.  The result is stored in the
 * object pointed by |dest|.  STOP_SENDING frame must start at
 * payload[0].  This function finishes when it decodes one
 * STOP_SENDING frame, and returns the exact number of bytes read to
 * decode a frame if it succeeds, or one of the following negative
 * error codes:
 *
 * NGTCP2_ERR_FRAME_ENCODING
 *     Payload is too short to include STOP_SENDING frame.
 */
ngtcp2_ssize ngtcp2_pkt_decode_stop_sending_frame(ngtcp2_stop_sending *dest,
                                                  const uint8_t *payload,
                                                  size_t payloadlen);

/*
 * ngtcp2_pkt_decode_path_challenge_frame decodes PATH_CHALLENGE frame
 * from |payload| of length |payloadlen|.  The result is stored in the
 * object pointed by |dest|.  PATH_CHALLENGE frame must start at
 * payload[0].  This function finishes when it decodes one
 * PATH_CHALLENGE frame, and returns the exact number of bytes read to
 * decode a frame if it succeeds, or one of the following negative
 * error codes:
 *
 * NGTCP2_ERR_FRAME_ENCODING
 *     Payload is too short to include PATH_CHALLENGE frame.
 */
ngtcp2_ssize ngtcp2_pkt_decode_path_challenge_frame(ngtcp2_path_challenge *dest,
                                                    const uint8_t *payload,
                                                    size_t payloadlen);

/*
 * ngtcp2_pkt_decode_path_response_frame decodes PATH_RESPONSE frame
 * from |payload| of length |payloadlen|.  The result is stored in the
 * object pointed by |dest|.  PATH_RESPONSE frame must start at
 * payload[0].  This function finishes when it decodes one
 * PATH_RESPONSE frame, and returns the exact number of bytes read to
 * decode a frame if it succeeds, or one of the following negative
 * error codes:
 *
 * NGTCP2_ERR_FRAME_ENCODING
 *     Payload is too short to include PATH_RESPONSE frame.
 */
ngtcp2_ssize ngtcp2_pkt_decode_path_response_frame(ngtcp2_path_response *dest,
                                                   const uint8_t *payload,
                                                   size_t payloadlen);

/*
 * ngtcp2_pkt_decode_crypto_frame decodes CRYPTO frame from |payload|
 * of length |payloadlen|.  The result is stored in the object pointed
 * by |dest|.  CRYPTO frame must start at payload[0].  This function
 * finishes when it decodes one CRYPTO frame, and returns the exact
 * number of bytes read to decode a frame if it succeeds, or one of
 * the following negative error codes:
 *
 * NGTCP2_ERR_FRAME_ENCODING
 *     Payload is too short to include CRYPTO frame.
 */
ngtcp2_ssize ngtcp2_pkt_decode_crypto_frame(ngtcp2_stream *dest,
                                            const uint8_t *payload,
                                            size_t payloadlen);

/*
 * ngtcp2_pkt_decode_new_token_frame decodes NEW_TOKEN frame from
 * |payload| of length |payloadlen|.  The result is stored in the
 * object pointed by |dest|.  NEW_TOKEN frame must start at
 * payload[0].  This function finishes when it decodes one NEW_TOKEN
 * frame, and returns the exact number of bytes read to decode a frame
 * if it succeeds, or one of the following negative error codes:
 *
 * NGTCP2_ERR_FRAME_ENCODING
 *     Payload is too short to include NEW_TOKEN frame.
 */
ngtcp2_ssize ngtcp2_pkt_decode_new_token_frame(ngtcp2_new_token *dest,
                                               const uint8_t *payload,
                                               size_t payloadlen);

/*
 * ngtcp2_pkt_decode_retire_connection_id_frame decodes
 * RETIRE_CONNECTION_ID frame from |payload| of length |payloadlen|.
 * The result is stored in the object pointed by |dest|.
 * RETIRE_CONNECTION_ID frame must start at payload[0].  This function
 * finishes when it decodes one RETIRE_CONNECTION_ID frame, and
 * returns the exact number of bytes read to decode a frame if it
 * succeeds, or one of the following negative error codes:
 *
 * NGTCP2_ERR_FRAME_ENCODING
 *     Payload is too short to include RETIRE_CONNECTION_ID frame.
 */
ngtcp2_ssize ngtcp2_pkt_decode_retire_connection_id_frame(
  ngtcp2_retire_connection_id *dest, const uint8_t *payload, size_t payloadlen);

/*
 * ngtcp2_pkt_decode_handshake_done_frame decodes HANDSHAKE_DONE frame
 * from |payload| of length |payloadlen|.  The result is stored in the
 * object pointed by |dest|.  HANDSHAKE_DONE frame must start at
 * payload[0].  This function finishes when it decodes one
 * HANDSHAKE_DONE frame, and returns the exact number of bytes read to
 * decode a frame.
 */
ngtcp2_ssize ngtcp2_pkt_decode_handshake_done_frame(ngtcp2_handshake_done *dest,
                                                    const uint8_t *payload,
                                                    size_t payloadlen);

/*
 * ngtcp2_pkt_decode_datagram_frame decodes DATAGRAM frame from
 * |payload| of length |payloadlen|.  The result is stored in the
 * object pointed by |dest|.  DATAGRAM frame must start at payload[0].
 * This function finishes when it decodes one DATAGRAM frame, and
 * returns the exact number of bytes read to decode a frame if it
 * succeeds, or one of the following negative error codes:
 *
 * NGTCP2_ERR_FRAME_ENCODING
 *     Payload is too short to include DATAGRAM frame.
 */
ngtcp2_ssize ngtcp2_pkt_decode_datagram_frame(ngtcp2_datagram *dest,
                                              const uint8_t *payload,
                                              size_t payloadlen);

/*
 * ngtcp2_pkt_encode_stream_frame encodes STREAM frame |fr| into the
 * buffer pointed by |out| of length |outlen|.
 *
 * This function assigns <the serialized frame type> &
 * ~NGTCP2_FRAME_STREAM to fr->flags.
 *
 * This function returns the number of bytes written if it succeeds,
 * or one of the following negative error codes:
 *
 * NGTCP2_ERR_NOBUF
 *     Buffer does not have enough capacity to write a frame.
 */
ngtcp2_ssize ngtcp2_pkt_encode_stream_frame(uint8_t *out, size_t outlen,
                                            ngtcp2_stream *fr);

/*
 * ngtcp2_pkt_encode_ack_frame encodes ACK frame |fr| into the buffer
 * pointed by |out| of length |outlen|.
 *
 * This function returns the number of bytes written if it succeeds,
 * or one of the following negative error codes:
 *
 * NGTCP2_ERR_NOBUF
 *     Buffer does not have enough capacity to write a frame.
 */
ngtcp2_ssize ngtcp2_pkt_encode_ack_frame(uint8_t *out, size_t outlen,
                                         const ngtcp2_ack *fr);

/*
 * ngtcp2_pkt_encode_padding_frame encodes PADDING frame |fr| into the
 * buffer pointed by |out| of length |outlen|.
 *
 * This function encodes consecutive fr->len PADDING frames.
 *
 * This function returns the number of bytes written if it succeeds,
 * or one of the following negative error codes:
 *
 * NGTCP2_ERR_NOBUF
 *     Buffer does not have enough capacity to write frame(s).
 */
ngtcp2_ssize ngtcp2_pkt_encode_padding_frame(uint8_t *out, size_t outlen,
                                             const ngtcp2_padding *fr);

/*
 * ngtcp2_pkt_encode_reset_stream_frame encodes RESET_STREAM frame
 * |fr| into the buffer pointed by |out| of length |buflen|.
 *
 * This function returns the number of bytes written if it succeeds,
 * or one of the following negative error codes:
 *
 * NGTCP2_ERR_NOBUF
 *     Buffer does not have enough capacity to write a frame.
 */
ngtcp2_ssize
ngtcp2_pkt_encode_reset_stream_frame(uint8_t *out, size_t outlen,
                                     const ngtcp2_reset_stream *fr);

/*
 * ngtcp2_pkt_encode_connection_close_frame encodes CONNECTION_CLOSE
 * frame |fr| into the buffer pointed by |out| of length |outlen|.
 *
 * This function returns the number of bytes written if it succeeds,
 * or one of the following negative error codes:
 *
 * NGTCP2_ERR_NOBUF
 *     Buffer does not have enough capacity to write a frame.
 */
ngtcp2_ssize
ngtcp2_pkt_encode_connection_close_frame(uint8_t *out, size_t outlen,
                                         const ngtcp2_connection_close *fr);

/*
 * ngtcp2_pkt_encode_max_data_frame encodes MAX_DATA frame |fr| into
 * the buffer pointed by |out| of length |outlen|.
 *
 * This function returns the number of bytes written if it succeeds,
 * or one of the following negative error codes:
 *
 * NGTCP2_ERR_NOBUF
 *     Buffer does not have enough capacity to write a frame.
 */
ngtcp2_ssize ngtcp2_pkt_encode_max_data_frame(uint8_t *out, size_t outlen,
                                              const ngtcp2_max_data *fr);

/*
 * ngtcp2_pkt_encode_max_stream_data_frame encodes MAX_STREAM_DATA
 * frame |fr| into the buffer pointed by |out| of length |outlen|.
 *
 * This function returns the number of bytes written if it succeeds,
 * or one of the following negative error codes:
 *
 * NGTCP2_ERR_NOBUF
 *     Buffer does not have enough capacity to write a frame.
 */
ngtcp2_ssize
ngtcp2_pkt_encode_max_stream_data_frame(uint8_t *out, size_t outlen,
                                        const ngtcp2_max_stream_data *fr);

/*
 * ngtcp2_pkt_encode_max_streams_frame encodes MAX_STREAMS
 * frame |fr| into the buffer pointed by |out| of length |outlen|.
 *
 * This function returns the number of bytes written if it succeeds,
 * or one of the following negative error codes:
 *
 * NGTCP2_ERR_NOBUF
 *     Buffer does not have enough capacity to write a frame.
 */
ngtcp2_ssize ngtcp2_pkt_encode_max_streams_frame(uint8_t *out, size_t outlen,
                                                 const ngtcp2_max_streams *fr);

/*
 * ngtcp2_pkt_encode_ping_frame encodes PING frame |fr| into the
 * buffer pointed by |out| of length |outlen|.
 *
 * This function returns the number of bytes written if it succeeds,
 * or one of the following negative error codes:
 *
 * NGTCP2_ERR_NOBUF
 *     Buffer does not have enough capacity to write a frame.
 */
ngtcp2_ssize ngtcp2_pkt_encode_ping_frame(uint8_t *out, size_t outlen,
                                          const ngtcp2_ping *fr);

/*
 * ngtcp2_pkt_encode_data_blocked_frame encodes DATA_BLOCKED frame
 * |fr| into the buffer pointed by |out| of length |outlen|.
 *
 * This function returns the number of bytes written if it succeeds,
 * or one of the following negative error codes:
 *
 * NGTCP2_ERR_NOBUF
 *     Buffer does not have enough capacity to write a frame.
 */
ngtcp2_ssize
ngtcp2_pkt_encode_data_blocked_frame(uint8_t *out, size_t outlen,
                                     const ngtcp2_data_blocked *fr);

/*
 * ngtcp2_pkt_encode_stream_data_blocked_frame encodes
 * STREAM_DATA_BLOCKED frame |fr| into the buffer pointed by |out| of
 * length |outlen|.
 *
 * This function returns the number of bytes written if it succeeds,
 * or one of the following negative error codes:
 *
 * NGTCP2_ERR_NOBUF
 *     Buffer does not have enough capacity to write a frame.
 */
ngtcp2_ssize ngtcp2_pkt_encode_stream_data_blocked_frame(
  uint8_t *out, size_t outlen, const ngtcp2_stream_data_blocked *fr);

/*
 * ngtcp2_pkt_encode_streams_blocked_frame encodes STREAMS_BLOCKED
 * frame |fr| into the buffer pointed by |out| of length |outlen|.
 *
 * This function returns the number of bytes written if it succeeds,
 * or one of the following negative error codes:
 *
 * NGTCP2_ERR_NOBUF
 *     Buffer does not have enough capacity to write a frame.
 */
ngtcp2_ssize
ngtcp2_pkt_encode_streams_blocked_frame(uint8_t *out, size_t outlen,
                                        const ngtcp2_streams_blocked *fr);

/*
 * ngtcp2_pkt_encode_new_connection_id_frame encodes NEW_CONNECTION_ID
 * frame |fr| into the buffer pointed by |out| of length |outlen|.
 *
 * This function returns the number of bytes written if it succeeds,
 * or one of the following negative error codes:
 *
 * NGTCP2_ERR_NOBUF
 *     Buffer does not have enough capacity to write a frame.
 */
ngtcp2_ssize
ngtcp2_pkt_encode_new_connection_id_frame(uint8_t *out, size_t outlen,
                                          const ngtcp2_new_connection_id *fr);

/*
 * ngtcp2_pkt_encode_stop_sending_frame encodes STOP_SENDING frame
 * |fr| into the buffer pointed by |out| of length |outlen|.
 *
 * This function returns the number of bytes written if it succeeds,
 * or one of the following negative error codes:
 *
 * NGTCP2_ERR_NOBUF
 *     Buffer does not have enough capacity to write a frame.
 */
ngtcp2_ssize
ngtcp2_pkt_encode_stop_sending_frame(uint8_t *out, size_t outlen,
                                     const ngtcp2_stop_sending *fr);

/*
 * ngtcp2_pkt_encode_path_challenge_frame encodes PATH_CHALLENGE frame
 * |fr| into the buffer pointed by |out| of length |outlen|.
 *
 * This function returns the number of bytes written if it succeeds,
 * or one of the following negative error codes:
 *
 * NGTCP2_ERR_NOBUF
 *     Buffer does not have enough capacity to write a frame.
 */
ngtcp2_ssize
ngtcp2_pkt_encode_path_challenge_frame(uint8_t *out, size_t outlen,
                                       const ngtcp2_path_challenge *fr);

/*
 * ngtcp2_pkt_encode_path_response_frame encodes PATH_RESPONSE frame
 * |fr| into the buffer pointed by |out| of length |outlen|.
 *
 * This function returns the number of bytes written if it succeeds,
 * or one of the following negative error codes:
 *
 * NGTCP2_ERR_NOBUF
 *     Buffer does not have enough capacity to write a frame.
 */
ngtcp2_ssize
ngtcp2_pkt_encode_path_response_frame(uint8_t *out, size_t outlen,
                                      const ngtcp2_path_response *fr);

/*
 * ngtcp2_pkt_encode_crypto_frame encodes CRYPTO frame |fr| into the
 * buffer pointed by |out| of length |outlen|.
 *
 * This function returns the number of bytes written if it succeeds,
 * or one of the following negative error codes:
 *
 * NGTCP2_ERR_NOBUF
 *     Buffer does not have enough capacity to write a frame.
 */
ngtcp2_ssize ngtcp2_pkt_encode_crypto_frame(uint8_t *out, size_t outlen,
                                            const ngtcp2_stream *fr);

/*
 * ngtcp2_pkt_encode_new_token_frame encodes NEW_TOKEN frame |fr| into
 * the buffer pointed by |out| of length |outlen|.
 *
 * This function returns the number of bytes written if it succeeds,
 * or one of the following negative error codes:
 *
 * NGTCP2_ERR_NOBUF
 *     Buffer does not have enough capacity to write a frame.
 */
ngtcp2_ssize ngtcp2_pkt_encode_new_token_frame(uint8_t *out, size_t outlen,
                                               const ngtcp2_new_token *fr);

/*
 * ngtcp2_pkt_encode_retire_connection_id_frame encodes
 * RETIRE_CONNECTION_ID frame |fr| into the buffer pointed by |out| of
 * length |outlen|.
 *
 * This function returns the number of bytes written if it succeeds,
 * or one of the following negative error codes:
 *
 * NGTCP2_ERR_NOBUF
 *     Buffer does not have enough capacity to write a frame.
 */
ngtcp2_ssize ngtcp2_pkt_encode_retire_connection_id_frame(
  uint8_t *out, size_t outlen, const ngtcp2_retire_connection_id *fr);

/*
 * ngtcp2_pkt_encode_handshake_done_frame encodes HANDSHAKE_DONE frame
 * |fr| into the buffer pointed by |out| of length |outlen|.
 *
 * This function returns the number of bytes written if it succeeds,
 * or one of the following negative error codes:
 *
 * NGTCP2_ERR_NOBUF
 *     Buffer does not have enough capacity to write a frame.
 */
ngtcp2_ssize
ngtcp2_pkt_encode_handshake_done_frame(uint8_t *out, size_t outlen,
                                       const ngtcp2_handshake_done *fr);

/*
 * ngtcp2_pkt_encode_datagram_frame encodes DATAGRAM frame |fr| into
 * the buffer pointed by |out| of length |outlen|.
 *
 * This function returns the number of bytes written if it succeeds,
 * or one of the following negative error codes:
 *
 * NGTCP2_ERR_NOBUF
 *     Buffer does not have enough capacity to write a frame.
 */
ngtcp2_ssize ngtcp2_pkt_encode_datagram_frame(uint8_t *out, size_t outlen,
                                              const ngtcp2_datagram *fr);

/*
 * ngtcp2_pkt_adjust_pkt_num finds the full 62 bits packet number for
 * |pkt_num|, which is encoded in |pkt_numlen| bytes.  The
 * |max_pkt_num| is the highest successfully authenticated packet
 * number.
 */
int64_t ngtcp2_pkt_adjust_pkt_num(int64_t max_pkt_num, int64_t pkt_num,
                                  size_t pkt_numlen);

/*
 * ngtcp2_pkt_validate_ack verifies whether |fr| is malformed or not.
 * |min_pkt_num| is the minimum packet number that a local endpoint
 * sends.  It is an error to receive acknowledgements for a packet
 * less than |min_pkt_num|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_ACK_FRAME
 *     ACK frame is malformed
 * NGTCP2_ERR_PROTO
 *     |fr| contains a packet number less than |min_pkt_num|.
 */
int ngtcp2_pkt_validate_ack(const ngtcp2_ack *fr, int64_t min_pkt_num);

/*
 * ngtcp2_pkt_stream_max_datalen returns the maximum number of bytes
 * which can be sent for stream denoted by |stream_id|.  |offset| is
 * an offset within the stream.  |len| is the estimated number of
 * bytes to send.  |left| is the size of buffer.  If |left| is too
 * small to write STREAM frame, this function returns (size_t)-1.
 */
size_t ngtcp2_pkt_stream_max_datalen(int64_t stream_id, uint64_t offset,
                                     uint64_t len, size_t left);

/*
 * ngtcp2_pkt_crypto_max_datalen returns the maximum number of bytes
 * which can be sent for crypto stream.  |offset| is an offset within
 * the crypto stream.  |len| is the estimated number of bytes to send.
 * |left| is the size of buffer.  If |left| is too small to write
 * CRYPTO frame, this function returns (size_t)-1.
 */
size_t ngtcp2_pkt_crypto_max_datalen(uint64_t offset, size_t len, size_t left);

/*
 * ngtcp2_pkt_datagram_framelen returns the length of DATAGRAM frame
 * to encode |len| bytes of data.
 */
size_t ngtcp2_pkt_datagram_framelen(size_t len);

/*
 * ngtcp2_pkt_verify_reserved_bits verifies that the first byte |c| of
 * the packet header has the correct reserved bits.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_PROTO
 *     Reserved bits has wrong value.
 */
int ngtcp2_pkt_verify_reserved_bits(uint8_t c);

/*
 * ngtcp2_pkt_encode_pseudo_retry encodes Retry pseudo-packet in the
 * buffer pointed by |dest| of length |destlen|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_BUF
 *     Buffer is too short.
 */
ngtcp2_ssize ngtcp2_pkt_encode_pseudo_retry(
  uint8_t *dest, size_t destlen, const ngtcp2_pkt_hd *hd, uint8_t unused,
  const ngtcp2_cid *odcid, const uint8_t *token, size_t tokenlen);

/*
 * ngtcp2_pkt_verify_retry_tag verifies Retry packet.  The buffer
 * pointed by |pkt| of length |pktlen| must contain Retry packet
 * including packet header.  The odcid and tag fields of |retry| must
 * be specified.  |aead| must be AEAD_AES_128_GCM.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_PROTO
 *     Verification failed.
 */
int ngtcp2_pkt_verify_retry_tag(uint32_t version, const ngtcp2_pkt_retry *retry,
                                const uint8_t *pkt, size_t pktlen,
                                ngtcp2_encrypt encrypt,
                                const ngtcp2_crypto_aead *aead,
                                const ngtcp2_crypto_aead_ctx *aead_ctx);

/*
 * ngtcp2_pkt_versioned_type returns versioned packet type for a
 * version |version| that corresponds to the version-independent
 * |pkt_type|.
 */
uint8_t ngtcp2_pkt_versioned_type(uint32_t version, uint32_t pkt_type);

/**
 * @function
 *
 * `ngtcp2_pkt_get_type_long` returns the version-independent long
 * packet type.  |version| is the QUIC version.  |c| is the first byte
 * of Long packet header.  If |version| is not supported by the
 * library, it returns 0.
 */
uint8_t ngtcp2_pkt_get_type_long(uint32_t version, uint8_t c);

#endif /* !defined(NGTCP2_PKT_H) */
