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
#ifndef NGTCP2_TRANSPORT_PARAMS_H
#define NGTCP2_TRANSPORT_PARAMS_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <ngtcp2/ngtcp2.h>

/* ngtcp2_transport_param_id is the registry of QUIC transport
   parameter ID. */
typedef uint64_t ngtcp2_transport_param_id;

#define NGTCP2_TRANSPORT_PARAM_ORIGINAL_DESTINATION_CONNECTION_ID 0x00
#define NGTCP2_TRANSPORT_PARAM_MAX_IDLE_TIMEOUT 0x01
#define NGTCP2_TRANSPORT_PARAM_STATELESS_RESET_TOKEN 0x02
#define NGTCP2_TRANSPORT_PARAM_MAX_UDP_PAYLOAD_SIZE 0x03
#define NGTCP2_TRANSPORT_PARAM_INITIAL_MAX_DATA 0x04
#define NGTCP2_TRANSPORT_PARAM_INITIAL_MAX_STREAM_DATA_BIDI_LOCAL 0x05
#define NGTCP2_TRANSPORT_PARAM_INITIAL_MAX_STREAM_DATA_BIDI_REMOTE 0x06
#define NGTCP2_TRANSPORT_PARAM_INITIAL_MAX_STREAM_DATA_UNI 0x07
#define NGTCP2_TRANSPORT_PARAM_INITIAL_MAX_STREAMS_BIDI 0x08
#define NGTCP2_TRANSPORT_PARAM_INITIAL_MAX_STREAMS_UNI 0x09
#define NGTCP2_TRANSPORT_PARAM_ACK_DELAY_EXPONENT 0x0a
#define NGTCP2_TRANSPORT_PARAM_MAX_ACK_DELAY 0x0b
#define NGTCP2_TRANSPORT_PARAM_DISABLE_ACTIVE_MIGRATION 0x0c
#define NGTCP2_TRANSPORT_PARAM_PREFERRED_ADDRESS 0x0d
#define NGTCP2_TRANSPORT_PARAM_ACTIVE_CONNECTION_ID_LIMIT 0x0e
#define NGTCP2_TRANSPORT_PARAM_INITIAL_SOURCE_CONNECTION_ID 0x0f
#define NGTCP2_TRANSPORT_PARAM_RETRY_SOURCE_CONNECTION_ID 0x10
/* https://datatracker.ietf.org/doc/html/rfc9221 */
#define NGTCP2_TRANSPORT_PARAM_MAX_DATAGRAM_FRAME_SIZE 0x20
#define NGTCP2_TRANSPORT_PARAM_GREASE_QUIC_BIT 0x2ab2
/* https://datatracker.ietf.org/doc/html/rfc9368 */
#define NGTCP2_TRANSPORT_PARAM_VERSION_INFORMATION 0x11

/* NGTCP2_MAX_STREAMS is the maximum number of streams. */
#define NGTCP2_MAX_STREAMS (1LL << 60)

/*
 * ngtcp2_transport_params_copy_new makes a copy of |src|, and assigns
 * it to |*pdest|.  If |src| is NULL, NULL is assigned to |*pdest|.
 *
 * Caller is responsible to call ngtcp2_transport_params_del to free
 * the memory assigned to |*pdest|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 */
int ngtcp2_transport_params_copy_new(ngtcp2_transport_params **pdest,
                                     const ngtcp2_transport_params *src,
                                     const ngtcp2_mem *mem);

/*
 * ngtcp2_transport_params_convert_to_latest converts |src| of version
 * |transport_params_version| to the latest version
 * NGTCP2_TRANSPORT_PARAMS_VERSION.
 *
 * |dest| must point to the latest version.  |src| may be the older
 * version, and if so, it may have fewer fields.  Accessing those
 * fields causes undefined behavior.
 *
 * If |transport_params_version| == NGTCP2_TRANSPORT_PARAMS_VERSION,
 * no conversion is made, and |src| is returned.  Otherwise, first
 * |dest| is initialized via ngtcp2_transport_params_default, and then
 * all valid fields in |src| are copied into |dest|.  Finally, |dest|
 * is returned.
 */
const ngtcp2_transport_params *
ngtcp2_transport_params_convert_to_latest(ngtcp2_transport_params *dest,
                                          int transport_params_version,
                                          const ngtcp2_transport_params *src);

/*
 * ngtcp2_transport_params_convert_to_old converts |src| of the latest
 * version to |dest| of version |transport_params_version|.
 *
 * |transport_params_version| must not be the latest version
 *  NGTCP2_TRANSPORT_PARAMS_VERSION.
 *
 * |dest| points to the older version, and it may have fewer fields.
 * Accessing those fields causes undefined behavior.
 *
 * This function copies all valid fields in version
 * |transport_params_version| from |src| to |dest|.
 */
void ngtcp2_transport_params_convert_to_old(int transport_params_version,
                                            ngtcp2_transport_params *dest,
                                            const ngtcp2_transport_params *src);

#endif /* !defined(NGTCP2_TRANSPORT_PARAMS_H) */
