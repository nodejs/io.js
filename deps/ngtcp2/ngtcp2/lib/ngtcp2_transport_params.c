/*
 * ngtcp2
 *
 * Copyright (c) 2023 ngtcp2 contributors
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
#include "ngtcp2_transport_params.h"

#include <string.h>
#include <assert.h>

#include "ngtcp2_conv.h"
#include "ngtcp2_str.h"
#include "ngtcp2_mem.h"
#include "ngtcp2_unreachable.h"

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

/*
 * varint_paramlen returns the length of a single transport parameter
 * which has variable integer in its parameter.
 */
static size_t varint_paramlen(ngtcp2_transport_param_id id, uint64_t param) {
  size_t valuelen = ngtcp2_put_uvarintlen(param);
  return ngtcp2_put_uvarintlen(id) + ngtcp2_put_uvarintlen(valuelen) + valuelen;
}

/*
 * write_varint_param writes parameter |id| of the given |value| in
 * varint encoding.  It returns p + the number of bytes written.
 */
static uint8_t *write_varint_param(uint8_t *p, ngtcp2_transport_param_id id,
                                   uint64_t value) {
  p = ngtcp2_put_uvarint(p, id);
  p = ngtcp2_put_uvarint(p, ngtcp2_put_uvarintlen(value));
  return ngtcp2_put_uvarint(p, value);
}

/*
 * zero_paramlen returns the length of a single transport parameter
 * which has zero length value in its parameter.
 */
static size_t zero_paramlen(ngtcp2_transport_param_id id) {
  return ngtcp2_put_uvarintlen(id) + 1;
}

/*
 * write_zero_param writes parameter |id| that has zero length value.
 * It returns p + the number of bytes written.
 */
static uint8_t *write_zero_param(uint8_t *p, ngtcp2_transport_param_id id) {
  p = ngtcp2_put_uvarint(p, id);
  *p++ = 0;

  return p;
}

/*
 * cid_paramlen returns the length of a single transport parameter
 * which has |cid| as value.
 */
static size_t cid_paramlen(ngtcp2_transport_param_id id,
                           const ngtcp2_cid *cid) {
  return ngtcp2_put_uvarintlen(id) + ngtcp2_put_uvarintlen(cid->datalen) +
         cid->datalen;
}

/*
 * write_cid_param writes parameter |id| of the given |cid|.  It
 * returns p + the number of bytes written.
 */
static uint8_t *write_cid_param(uint8_t *p, ngtcp2_transport_param_id id,
                                const ngtcp2_cid *cid) {
  assert(cid->datalen == 0 || cid->datalen >= NGTCP2_MIN_CIDLEN);
  assert(cid->datalen <= NGTCP2_MAX_CIDLEN);

  p = ngtcp2_put_uvarint(p, id);
  p = ngtcp2_put_uvarint(p, cid->datalen);
  if (cid->datalen) {
    p = ngtcp2_cpymem(p, cid->data, cid->datalen);
  }
  return p;
}

static const uint8_t empty_address[16];

ngtcp2_ssize ngtcp2_transport_params_encode_versioned(
  uint8_t *dest, size_t destlen, int transport_params_version,
  const ngtcp2_transport_params *params) {
  uint8_t *p;
  size_t len = 0;
  /* For some reason, gcc 7.3.0 requires this initialization. */
  size_t preferred_addrlen = 0;
  size_t version_infolen = 0;
  const ngtcp2_sockaddr_in *sa_in;
  const ngtcp2_sockaddr_in6 *sa_in6;
  ngtcp2_transport_params paramsbuf;

  params = ngtcp2_transport_params_convert_to_latest(
    &paramsbuf, transport_params_version, params);

  if (params->original_dcid_present) {
    len +=
      cid_paramlen(NGTCP2_TRANSPORT_PARAM_ORIGINAL_DESTINATION_CONNECTION_ID,
                   &params->original_dcid);
  }

  if (params->stateless_reset_token_present) {
    len += ngtcp2_put_uvarintlen(NGTCP2_TRANSPORT_PARAM_STATELESS_RESET_TOKEN) +
           ngtcp2_put_uvarintlen(NGTCP2_STATELESS_RESET_TOKENLEN) +
           NGTCP2_STATELESS_RESET_TOKENLEN;
  }

  if (params->preferred_addr_present) {
    assert(params->preferred_addr.cid.datalen >= NGTCP2_MIN_CIDLEN);
    assert(params->preferred_addr.cid.datalen <= NGTCP2_MAX_CIDLEN);
    preferred_addrlen = 4 /* ipv4Address */ + 2 /* ipv4Port */ +
                        16 /* ipv6Address */ + 2 /* ipv6Port */
                        + 1 + params->preferred_addr.cid.datalen /* CID */ +
                        NGTCP2_STATELESS_RESET_TOKENLEN;
    len += ngtcp2_put_uvarintlen(NGTCP2_TRANSPORT_PARAM_PREFERRED_ADDRESS) +
           ngtcp2_put_uvarintlen(preferred_addrlen) + preferred_addrlen;
  }
  if (params->retry_scid_present) {
    len += cid_paramlen(NGTCP2_TRANSPORT_PARAM_RETRY_SOURCE_CONNECTION_ID,
                        &params->retry_scid);
  }

  if (params->initial_scid_present) {
    len += cid_paramlen(NGTCP2_TRANSPORT_PARAM_INITIAL_SOURCE_CONNECTION_ID,
                        &params->initial_scid);
  }

  if (params->initial_max_stream_data_bidi_local) {
    len +=
      varint_paramlen(NGTCP2_TRANSPORT_PARAM_INITIAL_MAX_STREAM_DATA_BIDI_LOCAL,
                      params->initial_max_stream_data_bidi_local);
  }
  if (params->initial_max_stream_data_bidi_remote) {
    len += varint_paramlen(
      NGTCP2_TRANSPORT_PARAM_INITIAL_MAX_STREAM_DATA_BIDI_REMOTE,
      params->initial_max_stream_data_bidi_remote);
  }
  if (params->initial_max_stream_data_uni) {
    len += varint_paramlen(NGTCP2_TRANSPORT_PARAM_INITIAL_MAX_STREAM_DATA_UNI,
                           params->initial_max_stream_data_uni);
  }
  if (params->initial_max_data) {
    len += varint_paramlen(NGTCP2_TRANSPORT_PARAM_INITIAL_MAX_DATA,
                           params->initial_max_data);
  }
  if (params->initial_max_streams_bidi) {
    len += varint_paramlen(NGTCP2_TRANSPORT_PARAM_INITIAL_MAX_STREAMS_BIDI,
                           params->initial_max_streams_bidi);
  }
  if (params->initial_max_streams_uni) {
    len += varint_paramlen(NGTCP2_TRANSPORT_PARAM_INITIAL_MAX_STREAMS_UNI,
                           params->initial_max_streams_uni);
  }
  if (params->max_udp_payload_size !=
      NGTCP2_DEFAULT_MAX_RECV_UDP_PAYLOAD_SIZE) {
    len += varint_paramlen(NGTCP2_TRANSPORT_PARAM_MAX_UDP_PAYLOAD_SIZE,
                           params->max_udp_payload_size);
  }
  if (params->ack_delay_exponent != NGTCP2_DEFAULT_ACK_DELAY_EXPONENT) {
    len += varint_paramlen(NGTCP2_TRANSPORT_PARAM_ACK_DELAY_EXPONENT,
                           params->ack_delay_exponent);
  }
  if (params->disable_active_migration) {
    len += zero_paramlen(NGTCP2_TRANSPORT_PARAM_DISABLE_ACTIVE_MIGRATION);
  }
  if (params->max_ack_delay != NGTCP2_DEFAULT_MAX_ACK_DELAY) {
    len += varint_paramlen(NGTCP2_TRANSPORT_PARAM_MAX_ACK_DELAY,
                           params->max_ack_delay / NGTCP2_MILLISECONDS);
  }
  if (params->max_idle_timeout) {
    len += varint_paramlen(NGTCP2_TRANSPORT_PARAM_MAX_IDLE_TIMEOUT,
                           params->max_idle_timeout / NGTCP2_MILLISECONDS);
  }
  if (params->active_connection_id_limit &&
      params->active_connection_id_limit !=
        NGTCP2_DEFAULT_ACTIVE_CONNECTION_ID_LIMIT) {
    len += varint_paramlen(NGTCP2_TRANSPORT_PARAM_ACTIVE_CONNECTION_ID_LIMIT,
                           params->active_connection_id_limit);
  }
  if (params->max_datagram_frame_size) {
    len += varint_paramlen(NGTCP2_TRANSPORT_PARAM_MAX_DATAGRAM_FRAME_SIZE,
                           params->max_datagram_frame_size);
  }
  if (params->grease_quic_bit) {
    len += zero_paramlen(NGTCP2_TRANSPORT_PARAM_GREASE_QUIC_BIT);
  }
  if (params->version_info_present) {
    version_infolen =
      sizeof(uint32_t) + params->version_info.available_versionslen;
    len += ngtcp2_put_uvarintlen(NGTCP2_TRANSPORT_PARAM_VERSION_INFORMATION) +
           ngtcp2_put_uvarintlen(version_infolen) + version_infolen;
  }

  if (dest == NULL && destlen == 0) {
    return (ngtcp2_ssize)len;
  }

  if (destlen < len) {
    return NGTCP2_ERR_NOBUF;
  }

  p = dest;

  if (params->original_dcid_present) {
    p = write_cid_param(
      p, NGTCP2_TRANSPORT_PARAM_ORIGINAL_DESTINATION_CONNECTION_ID,
      &params->original_dcid);
  }

  if (params->stateless_reset_token_present) {
    p = ngtcp2_put_uvarint(p, NGTCP2_TRANSPORT_PARAM_STATELESS_RESET_TOKEN);
    p = ngtcp2_put_uvarint(p, sizeof(params->stateless_reset_token));
    p = ngtcp2_cpymem(p, params->stateless_reset_token,
                      sizeof(params->stateless_reset_token));
  }

  if (params->preferred_addr_present) {
    p = ngtcp2_put_uvarint(p, NGTCP2_TRANSPORT_PARAM_PREFERRED_ADDRESS);
    p = ngtcp2_put_uvarint(p, preferred_addrlen);

    if (params->preferred_addr.ipv4_present) {
      sa_in = &params->preferred_addr.ipv4;
      p = ngtcp2_cpymem(p, &sa_in->sin_addr, sizeof(sa_in->sin_addr));
      p = ngtcp2_put_uint16(p, sa_in->sin_port);
    } else {
      p = ngtcp2_cpymem(p, empty_address, sizeof(sa_in->sin_addr));
      p = ngtcp2_put_uint16(p, 0);
    }

    if (params->preferred_addr.ipv6_present) {
      sa_in6 = &params->preferred_addr.ipv6;
      p = ngtcp2_cpymem(p, &sa_in6->sin6_addr, sizeof(sa_in6->sin6_addr));
      p = ngtcp2_put_uint16(p, sa_in6->sin6_port);
    } else {
      p = ngtcp2_cpymem(p, empty_address, sizeof(sa_in6->sin6_addr));
      p = ngtcp2_put_uint16(p, 0);
    }

    *p++ = (uint8_t)params->preferred_addr.cid.datalen;
    if (params->preferred_addr.cid.datalen) {
      p = ngtcp2_cpymem(p, params->preferred_addr.cid.data,
                        params->preferred_addr.cid.datalen);
    }
    p = ngtcp2_cpymem(p, params->preferred_addr.stateless_reset_token,
                      sizeof(params->preferred_addr.stateless_reset_token));
  }

  if (params->retry_scid_present) {
    p = write_cid_param(p, NGTCP2_TRANSPORT_PARAM_RETRY_SOURCE_CONNECTION_ID,
                        &params->retry_scid);
  }

  if (params->initial_scid_present) {
    p = write_cid_param(p, NGTCP2_TRANSPORT_PARAM_INITIAL_SOURCE_CONNECTION_ID,
                        &params->initial_scid);
  }

  if (params->initial_max_stream_data_bidi_local) {
    p = write_varint_param(
      p, NGTCP2_TRANSPORT_PARAM_INITIAL_MAX_STREAM_DATA_BIDI_LOCAL,
      params->initial_max_stream_data_bidi_local);
  }

  if (params->initial_max_stream_data_bidi_remote) {
    p = write_varint_param(
      p, NGTCP2_TRANSPORT_PARAM_INITIAL_MAX_STREAM_DATA_BIDI_REMOTE,
      params->initial_max_stream_data_bidi_remote);
  }

  if (params->initial_max_stream_data_uni) {
    p =
      write_varint_param(p, NGTCP2_TRANSPORT_PARAM_INITIAL_MAX_STREAM_DATA_UNI,
                         params->initial_max_stream_data_uni);
  }

  if (params->initial_max_data) {
    p = write_varint_param(p, NGTCP2_TRANSPORT_PARAM_INITIAL_MAX_DATA,
                           params->initial_max_data);
  }

  if (params->initial_max_streams_bidi) {
    p = write_varint_param(p, NGTCP2_TRANSPORT_PARAM_INITIAL_MAX_STREAMS_BIDI,
                           params->initial_max_streams_bidi);
  }

  if (params->initial_max_streams_uni) {
    p = write_varint_param(p, NGTCP2_TRANSPORT_PARAM_INITIAL_MAX_STREAMS_UNI,
                           params->initial_max_streams_uni);
  }

  if (params->max_udp_payload_size !=
      NGTCP2_DEFAULT_MAX_RECV_UDP_PAYLOAD_SIZE) {
    p = write_varint_param(p, NGTCP2_TRANSPORT_PARAM_MAX_UDP_PAYLOAD_SIZE,
                           params->max_udp_payload_size);
  }

  if (params->ack_delay_exponent != NGTCP2_DEFAULT_ACK_DELAY_EXPONENT) {
    p = write_varint_param(p, NGTCP2_TRANSPORT_PARAM_ACK_DELAY_EXPONENT,
                           params->ack_delay_exponent);
  }

  if (params->disable_active_migration) {
    p = write_zero_param(p, NGTCP2_TRANSPORT_PARAM_DISABLE_ACTIVE_MIGRATION);
  }

  if (params->max_ack_delay != NGTCP2_DEFAULT_MAX_ACK_DELAY) {
    p = write_varint_param(p, NGTCP2_TRANSPORT_PARAM_MAX_ACK_DELAY,
                           params->max_ack_delay / NGTCP2_MILLISECONDS);
  }

  if (params->max_idle_timeout) {
    p = write_varint_param(p, NGTCP2_TRANSPORT_PARAM_MAX_IDLE_TIMEOUT,
                           params->max_idle_timeout / NGTCP2_MILLISECONDS);
  }

  if (params->active_connection_id_limit &&
      params->active_connection_id_limit !=
        NGTCP2_DEFAULT_ACTIVE_CONNECTION_ID_LIMIT) {
    p = write_varint_param(p, NGTCP2_TRANSPORT_PARAM_ACTIVE_CONNECTION_ID_LIMIT,
                           params->active_connection_id_limit);
  }

  if (params->max_datagram_frame_size) {
    p = write_varint_param(p, NGTCP2_TRANSPORT_PARAM_MAX_DATAGRAM_FRAME_SIZE,
                           params->max_datagram_frame_size);
  }

  if (params->grease_quic_bit) {
    p = write_zero_param(p, NGTCP2_TRANSPORT_PARAM_GREASE_QUIC_BIT);
  }

  if (params->version_info_present) {
    p = ngtcp2_put_uvarint(p, NGTCP2_TRANSPORT_PARAM_VERSION_INFORMATION);
    p = ngtcp2_put_uvarint(p, version_infolen);
    p = ngtcp2_put_uint32be(p, params->version_info.chosen_version);
    if (params->version_info.available_versionslen) {
      p = ngtcp2_cpymem(p, params->version_info.available_versions,
                        params->version_info.available_versionslen);
    }
  }

  assert((size_t)(p - dest) == len);

  return (ngtcp2_ssize)len;
}

/*
 * decode_varint decodes a single varint from the buffer pointed by
 * |*pp| of length |end - *pp|.  If it decodes an integer
 * successfully, it stores the integer in |*pdest|, increment |*pp| by
 * the number of bytes read from |*pp|, and returns 0.  Otherwise it
 * returns -1.
 */
static int decode_varint(uint64_t *pdest, const uint8_t **pp,
                         const uint8_t *end) {
  const uint8_t *p = *pp;
  size_t len;

  if (p == end) {
    return -1;
  }

  len = ngtcp2_get_uvarintlen(p);
  if ((uint64_t)(end - p) < len) {
    return -1;
  }

  *pp = ngtcp2_get_uvarint(pdest, p);

  return 0;
}

/*
 * decode_varint_param decodes length prefixed value from the buffer
 * pointed by |*pp| of length |end - *pp|.  The length and value are
 * encoded in varint form.  If it decodes a value successfully, it
 * stores the value in |*pdest|, increment |*pp| by the number of
 * bytes read from |*pp|, and returns 0.  Otherwise it returns -1.
 */
static int decode_varint_param(uint64_t *pdest, const uint8_t **pp,
                               const uint8_t *end) {
  const uint8_t *p = *pp;
  uint64_t valuelen;

  if (decode_varint(&valuelen, &p, end) != 0) {
    return -1;
  }

  if (p == end) {
    return -1;
  }

  if ((uint64_t)(end - p) < valuelen) {
    return -1;
  }

  if (ngtcp2_get_uvarintlen(p) != valuelen) {
    return -1;
  }

  *pp = ngtcp2_get_uvarint(pdest, p);

  return 0;
}

/*
 * decode_zero_param decodes zero length value from the buffer pointed
 * by |*pp| of length |end - *pp|.  The length is encoded in varint
 * form.  If it decodes zero length value successfully, it increments
 * |*pp| by 1, and returns 0.  Otherwise it returns -1.
 */
static int decode_zero_param(const uint8_t **pp, const uint8_t *end) {
  if (*pp == end || **pp != 0) {
    return -1;
  }

  ++*pp;

  return 0;
}

/*
 * decode_cid_param decodes length prefixed ngtcp2_cid from the buffer
 * pointed by |*pp| of length |end - *pp|.  The length is encoded in
 * varint form.  If it decodes a value successfully, it stores the
 * value in |*pdest|, increment |*pp| by the number of read from
 * |*pp|, and returns the number of bytes read.  Otherwise it returns
 * the one of the negative error code:
 *
 * NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM
 *     Could not decode Connection ID.
 */
static int decode_cid_param(ngtcp2_cid *pdest, const uint8_t **pp,
                            const uint8_t *end) {
  const uint8_t *p = *pp;
  uint64_t valuelen;

  if (decode_varint(&valuelen, &p, end) != 0) {
    return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
  }

  if ((valuelen != 0 && valuelen < NGTCP2_MIN_CIDLEN) ||
      valuelen > NGTCP2_MAX_CIDLEN || (size_t)(end - p) < valuelen) {
    return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
  }

  ngtcp2_cid_init(pdest, p, (size_t)valuelen);

  p += valuelen;

  *pp = p;

  return 0;
}

int ngtcp2_transport_params_decode_versioned(int transport_params_version,
                                             ngtcp2_transport_params *dest,
                                             const uint8_t *data,
                                             size_t datalen) {
  const uint8_t *p, *end, *lend;
  size_t len;
  uint64_t param_type;
  uint64_t valuelen;
  int rv;
  ngtcp2_sockaddr_in *sa_in;
  ngtcp2_sockaddr_in6 *sa_in6;
  uint32_t version;
  ngtcp2_transport_params *params, paramsbuf;

  if (transport_params_version == NGTCP2_TRANSPORT_PARAMS_VERSION) {
    params = dest;
  } else {
    params = &paramsbuf;
  }

  /* Set default values */
  memset(params, 0, sizeof(*params));
  params->max_udp_payload_size = NGTCP2_DEFAULT_MAX_RECV_UDP_PAYLOAD_SIZE;
  params->ack_delay_exponent = NGTCP2_DEFAULT_ACK_DELAY_EXPONENT;
  params->max_ack_delay = NGTCP2_DEFAULT_MAX_ACK_DELAY;
  params->active_connection_id_limit =
    NGTCP2_DEFAULT_ACTIVE_CONNECTION_ID_LIMIT;

  p = data;
  end = data + datalen;

  for (; (size_t)(end - p) >= 2;) {
    if (decode_varint(&param_type, &p, end) != 0) {
      return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
    }

    switch (param_type) {
    case NGTCP2_TRANSPORT_PARAM_INITIAL_MAX_STREAM_DATA_BIDI_LOCAL:
      if (decode_varint_param(&params->initial_max_stream_data_bidi_local, &p,
                              end) != 0) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }
      break;
    case NGTCP2_TRANSPORT_PARAM_INITIAL_MAX_STREAM_DATA_BIDI_REMOTE:
      if (decode_varint_param(&params->initial_max_stream_data_bidi_remote, &p,
                              end) != 0) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }
      break;
    case NGTCP2_TRANSPORT_PARAM_INITIAL_MAX_STREAM_DATA_UNI:
      if (decode_varint_param(&params->initial_max_stream_data_uni, &p, end) !=
          0) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }
      break;
    case NGTCP2_TRANSPORT_PARAM_INITIAL_MAX_DATA:
      if (decode_varint_param(&params->initial_max_data, &p, end) != 0) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }
      break;
    case NGTCP2_TRANSPORT_PARAM_INITIAL_MAX_STREAMS_BIDI:
      if (decode_varint_param(&params->initial_max_streams_bidi, &p, end) !=
          0) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }
      if (params->initial_max_streams_bidi > NGTCP2_MAX_STREAMS) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }
      break;
    case NGTCP2_TRANSPORT_PARAM_INITIAL_MAX_STREAMS_UNI:
      if (decode_varint_param(&params->initial_max_streams_uni, &p, end) != 0) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }
      if (params->initial_max_streams_uni > NGTCP2_MAX_STREAMS) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }
      break;
    case NGTCP2_TRANSPORT_PARAM_MAX_IDLE_TIMEOUT:
      if (decode_varint_param(&params->max_idle_timeout, &p, end) != 0) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }
      params->max_idle_timeout *= NGTCP2_MILLISECONDS;
      break;
    case NGTCP2_TRANSPORT_PARAM_MAX_UDP_PAYLOAD_SIZE:
      if (decode_varint_param(&params->max_udp_payload_size, &p, end) != 0) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }
      break;
    case NGTCP2_TRANSPORT_PARAM_STATELESS_RESET_TOKEN:
      if (decode_varint(&valuelen, &p, end) != 0) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }
      if ((size_t)valuelen != sizeof(params->stateless_reset_token)) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }
      if ((size_t)(end - p) < sizeof(params->stateless_reset_token)) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }

      p = ngtcp2_get_bytes(params->stateless_reset_token, p,
                           sizeof(params->stateless_reset_token));
      params->stateless_reset_token_present = 1;

      break;
    case NGTCP2_TRANSPORT_PARAM_ACK_DELAY_EXPONENT:
      if (decode_varint_param(&params->ack_delay_exponent, &p, end) != 0) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }
      if (params->ack_delay_exponent > 20) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }
      break;
    case NGTCP2_TRANSPORT_PARAM_PREFERRED_ADDRESS:
      if (decode_varint(&valuelen, &p, end) != 0) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }
      if ((size_t)(end - p) < valuelen) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }
      len = 4 /* ipv4Address */ + 2 /* ipv4Port */ + 16 /* ipv6Address */ +
            2 /* ipv6Port */
            + 1 /* cid length */ + NGTCP2_STATELESS_RESET_TOKENLEN;
      if (valuelen < len) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }

      sa_in = &params->preferred_addr.ipv4;

      p = ngtcp2_get_bytes(&sa_in->sin_addr, p, sizeof(sa_in->sin_addr));
      p = ngtcp2_get_uint16(&sa_in->sin_port, p);

      if (sa_in->sin_port || memcmp(empty_address, &sa_in->sin_addr,
                                    sizeof(sa_in->sin_addr)) != 0) {
        sa_in->sin_family = NGTCP2_AF_INET;
        params->preferred_addr.ipv4_present = 1;
      }

      sa_in6 = &params->preferred_addr.ipv6;

      p = ngtcp2_get_bytes(&sa_in6->sin6_addr, p, sizeof(sa_in6->sin6_addr));
      p = ngtcp2_get_uint16(&sa_in6->sin6_port, p);

      if (sa_in6->sin6_port || memcmp(empty_address, &sa_in6->sin6_addr,
                                      sizeof(sa_in6->sin6_addr)) != 0) {
        sa_in6->sin6_family = NGTCP2_AF_INET6;
        params->preferred_addr.ipv6_present = 1;
      }

      /* cid */
      params->preferred_addr.cid.datalen = *p++;
      len += params->preferred_addr.cid.datalen;
      if (valuelen != len ||
          params->preferred_addr.cid.datalen > NGTCP2_MAX_CIDLEN ||
          params->preferred_addr.cid.datalen < NGTCP2_MIN_CIDLEN) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }
      if (params->preferred_addr.cid.datalen) {
        p = ngtcp2_get_bytes(params->preferred_addr.cid.data, p,
                             params->preferred_addr.cid.datalen);
      }

      /* stateless reset token */
      p =
        ngtcp2_get_bytes(params->preferred_addr.stateless_reset_token, p,
                         sizeof(params->preferred_addr.stateless_reset_token));
      params->preferred_addr_present = 1;
      break;
    case NGTCP2_TRANSPORT_PARAM_DISABLE_ACTIVE_MIGRATION:
      if (decode_zero_param(&p, end) != 0) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }
      params->disable_active_migration = 1;
      break;
    case NGTCP2_TRANSPORT_PARAM_ORIGINAL_DESTINATION_CONNECTION_ID:
      rv = decode_cid_param(&params->original_dcid, &p, end);
      if (rv != 0) {
        return rv;
      }
      params->original_dcid_present = 1;
      break;
    case NGTCP2_TRANSPORT_PARAM_RETRY_SOURCE_CONNECTION_ID:
      rv = decode_cid_param(&params->retry_scid, &p, end);
      if (rv != 0) {
        return rv;
      }
      params->retry_scid_present = 1;
      break;
    case NGTCP2_TRANSPORT_PARAM_INITIAL_SOURCE_CONNECTION_ID:
      rv = decode_cid_param(&params->initial_scid, &p, end);
      if (rv != 0) {
        return rv;
      }
      params->initial_scid_present = 1;
      break;
    case NGTCP2_TRANSPORT_PARAM_MAX_ACK_DELAY:
      if (decode_varint_param(&params->max_ack_delay, &p, end) != 0) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }
      if (params->max_ack_delay >= 16384) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }
      params->max_ack_delay *= NGTCP2_MILLISECONDS;
      break;
    case NGTCP2_TRANSPORT_PARAM_ACTIVE_CONNECTION_ID_LIMIT:
      if (decode_varint_param(&params->active_connection_id_limit, &p, end) !=
          0) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }
      break;
    case NGTCP2_TRANSPORT_PARAM_MAX_DATAGRAM_FRAME_SIZE:
      if (decode_varint_param(&params->max_datagram_frame_size, &p, end) != 0) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }
      break;
    case NGTCP2_TRANSPORT_PARAM_GREASE_QUIC_BIT:
      if (decode_zero_param(&p, end) != 0) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }
      params->grease_quic_bit = 1;
      break;
    case NGTCP2_TRANSPORT_PARAM_VERSION_INFORMATION:
      if (decode_varint(&valuelen, &p, end) != 0) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }
      if ((size_t)(end - p) < valuelen) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }
      if (valuelen < sizeof(uint32_t) || (valuelen & 0x3)) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }
      p = ngtcp2_get_uint32be(&params->version_info.chosen_version, p);
      if (params->version_info.chosen_version == 0) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }
      if (valuelen > sizeof(uint32_t)) {
        params->version_info.available_versions = (uint8_t *)p;
        params->version_info.available_versionslen =
          (size_t)valuelen - sizeof(uint32_t);

        for (lend = p + (valuelen - sizeof(uint32_t)); p != lend;) {
          p = ngtcp2_get_uint32be(&version, p);
          if (version == 0) {
            return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
          }
        }
      }
      params->version_info_present = 1;
      break;
    default:
      /* Ignore unknown parameter */
      if (decode_varint(&valuelen, &p, end) != 0) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }
      if ((size_t)(end - p) < valuelen) {
        return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
      }
      p += valuelen;
      break;
    }
  }

  if (end - p != 0) {
    return NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM;
  }

  if (transport_params_version != NGTCP2_TRANSPORT_PARAMS_VERSION) {
    ngtcp2_transport_params_convert_to_old(transport_params_version, dest,
                                           params);
  }

  return 0;
}

static int transport_params_copy_new(ngtcp2_transport_params **pdest,
                                     const ngtcp2_transport_params *src,
                                     const ngtcp2_mem *mem) {
  size_t len = sizeof(**pdest);
  ngtcp2_transport_params *dest;
  uint8_t *p;

  if (src->version_info_present) {
    len += src->version_info.available_versionslen;
  }

  dest = ngtcp2_mem_malloc(mem, len);
  if (dest == NULL) {
    return NGTCP2_ERR_NOMEM;
  }

  *dest = *src;

  if (src->version_info_present && src->version_info.available_versionslen) {
    p = (uint8_t *)dest + sizeof(*dest);
    memcpy(p, src->version_info.available_versions,
           src->version_info.available_versionslen);
    dest->version_info.available_versions = p;
  }

  *pdest = dest;

  return 0;
}

int ngtcp2_transport_params_decode_new(ngtcp2_transport_params **pparams,
                                       const uint8_t *data, size_t datalen,
                                       const ngtcp2_mem *mem) {
  int rv;
  ngtcp2_transport_params params;

  rv = ngtcp2_transport_params_decode(&params, data, datalen);
  if (rv < 0) {
    return rv;
  }

  if (mem == NULL) {
    mem = ngtcp2_mem_default();
  }

  return transport_params_copy_new(pparams, &params, mem);
}

void ngtcp2_transport_params_del(ngtcp2_transport_params *params,
                                 const ngtcp2_mem *mem) {
  if (params == NULL) {
    return;
  }

  if (mem == NULL) {
    mem = ngtcp2_mem_default();
  }

  ngtcp2_mem_free(mem, params);
}

int ngtcp2_transport_params_copy_new(ngtcp2_transport_params **pdest,
                                     const ngtcp2_transport_params *src,
                                     const ngtcp2_mem *mem) {
  if (src == NULL) {
    *pdest = NULL;
    return 0;
  }

  return transport_params_copy_new(pdest, src, mem);
}

static void transport_params_copy(ngtcp2_transport_params *dest,
                                  const ngtcp2_transport_params *src,
                                  int transport_params_version) {
  assert(transport_params_version != NGTCP2_TRANSPORT_PARAMS_VERSION);

  switch (transport_params_version) {
  case NGTCP2_TRANSPORT_PARAMS_V1:
    memcpy(dest, src,
           offsetof(ngtcp2_transport_params, version_info_present) +
             sizeof(src->version_info_present));

    break;
  }
}

const ngtcp2_transport_params *
ngtcp2_transport_params_convert_to_latest(ngtcp2_transport_params *dest,
                                          int transport_params_version,
                                          const ngtcp2_transport_params *src) {
  if (transport_params_version == NGTCP2_TRANSPORT_PARAMS_VERSION) {
    return src;
  }

  ngtcp2_transport_params_default(dest);

  transport_params_copy(dest, src, transport_params_version);

  return dest;
}

void ngtcp2_transport_params_convert_to_old(
  int transport_params_version, ngtcp2_transport_params *dest,
  const ngtcp2_transport_params *src) {
  assert(transport_params_version != NGTCP2_TRANSPORT_PARAMS_VERSION);

  transport_params_copy(dest, src, transport_params_version);
}
