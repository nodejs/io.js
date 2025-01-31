/*
 * nghttp3
 *
 * Copyright (c) 2019 nghttp3 contributors
 * Copyright (c) 2015 nghttp2 contributors
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
#ifndef NGHTTP3_HTTP_H
#define NGHTTP3_HTTP_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <nghttp3/nghttp3.h>

typedef struct nghttp3_stream nghttp3_stream;

typedef struct nghttp3_http_state nghttp3_http_state;

/* HTTP related flags to enforce HTTP semantics */

/* NGHTTP3_HTTP_FLAG_NONE indicates that no flag is set. */
#define NGHTTP3_HTTP_FLAG_NONE 0x00u
/* header field seen so far */
#define NGHTTP3_HTTP_FLAG__AUTHORITY 0x01u
#define NGHTTP3_HTTP_FLAG__PATH 0x02u
#define NGHTTP3_HTTP_FLAG__METHOD 0x04u
#define NGHTTP3_HTTP_FLAG__SCHEME 0x08u
/* host is not pseudo header, but we require either host or
   :authority */
#define NGHTTP3_HTTP_FLAG_HOST 0x10u
#define NGHTTP3_HTTP_FLAG__STATUS 0x20u
/* required header fields for HTTP request except for CONNECT
   method. */
#define NGHTTP3_HTTP_FLAG_REQ_HEADERS                                          \
  (NGHTTP3_HTTP_FLAG__METHOD | NGHTTP3_HTTP_FLAG__PATH |                       \
   NGHTTP3_HTTP_FLAG__SCHEME)
#define NGHTTP3_HTTP_FLAG_PSEUDO_HEADER_DISALLOWED 0x40u
/* HTTP method flags */
#define NGHTTP3_HTTP_FLAG_METH_CONNECT 0x80u
#define NGHTTP3_HTTP_FLAG_METH_HEAD 0x0100u
#define NGHTTP3_HTTP_FLAG_METH_OPTIONS 0x0200u
#define NGHTTP3_HTTP_FLAG_METH_ALL                                             \
  (NGHTTP3_HTTP_FLAG_METH_CONNECT | NGHTTP3_HTTP_FLAG_METH_HEAD |              \
   NGHTTP3_HTTP_FLAG_METH_OPTIONS)
/* :path category */
/* path starts with "/" */
#define NGHTTP3_HTTP_FLAG_PATH_REGULAR 0x0400u
/* path "*" */
#define NGHTTP3_HTTP_FLAG_PATH_ASTERISK 0x0800u
/* scheme */
/* "http" or "https" scheme */
#define NGHTTP3_HTTP_FLAG_SCHEME_HTTP 0x1000u
/* set if final response is expected */
#define NGHTTP3_HTTP_FLAG_EXPECT_FINAL_RESPONSE 0x2000u
/* NGHTTP3_HTTP_FLAG__PROTOCOL is set when :protocol pseudo header
   field is seen. */
#define NGHTTP3_HTTP_FLAG__PROTOCOL 0x4000u
/* NGHTTP3_HTTP_FLAG_PRIORITY is set when priority header field is
   processed. */
#define NGHTTP3_HTTP_FLAG_PRIORITY 0x8000u
/* NGHTTP3_HTTP_FLAG_BAD_PRIORITY is set when an error is encountered
   while parsing priority header field. */
#define NGHTTP3_HTTP_FLAG_BAD_PRIORITY 0x010000u

/*
 * This function is called when HTTP header field |nv| received for
 * |http|.  This function will validate |nv| against the current state
 * of stream.  Pass nonzero if this is request headers. Pass nonzero
 * to |trailers| if |nv| is included in trailers.  |connect_protocol|
 * is nonzero if Extended CONNECT Method is enabled.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_MALFORMED_HTTP_HEADER
 *     Invalid HTTP header field was received.
 * NGHTTP3_ERR_REMOVE_HTTP_HEADER
 *     Invalid HTTP header field was received but it can be treated as
 *     if it was not received because of compatibility reasons.
 */
int nghttp3_http_on_header(nghttp3_http_state *http, nghttp3_qpack_nv *nv,
                           int request, int trailers, int connect_protocol);

/*
 * This function is called when request header is received.  This
 * function performs validation and returns 0 if it succeeds, or one
 * of the following negative error codes:
 *
 * NGHTTP3_ERR_MALFORMED_HTTP_HEADER
 *     Required HTTP header field was not received; or an invalid
 *     header field was received.
 */
int nghttp3_http_on_request_headers(nghttp3_http_state *http);

/*
 * This function is called when response header is received.  This
 * function performs validation and returns 0 if it succeeds, or one
 * of the following negative error codes:
 *
 * NGHTTP3_ERR_MALFORMED_HTTP_HEADER
 *     Required HTTP header field was not received; or an invalid
 *     header field was received.
 */
int nghttp3_http_on_response_headers(nghttp3_http_state *http);

/*
 * This function is called when read side stream is closed.  This
 *  function performs validation and returns 0 if it succeeds, or one
 *  of the following negative error codes:
 *
 * NGHTTP3_ERR_MALFORMED_HTTP_MESSAGING
 *     HTTP messaging is violated.
 */
int nghttp3_http_on_remote_end_stream(nghttp3_stream *stream);

/*
 * This function is called when chunk of data is received.  This
 * function performs validation and returns 0 if it succeeds, or one
 * of the following negative error codes:
 *
 * NGHTTP3_ERR_MALFORMED_HTTP_MESSAGING
 *     HTTP messaging is violated.
 */
int nghttp3_http_on_data_chunk(nghttp3_stream *stream, size_t n);

/*
 * This function inspects header fields in |nva| of length |nvlen| and
 * records its method in stream->http_flags.
 */
void nghttp3_http_record_request_method(nghttp3_stream *stream,
                                        const nghttp3_nv *nva, size_t nvlen);

/**
 * @function
 *
 * `nghttp3_http_parse_priority` parses priority HTTP header field
 * stored in the buffer pointed by |value| of length |len|.  If it
 * successfully processed header field value, it stores the result
 * into |*dest|.  This function just overwrites what it sees in the
 * header field value and does not initialize any field in |*dest|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * :macro:`NGHTTP3_ERR_INVALID_ARGUMENT`
 *     The function could not parse the provided value.
 */
int nghttp3_http_parse_priority(nghttp3_pri *dest, const uint8_t *value,
                                size_t len);

int nghttp3_pri_eq(const nghttp3_pri *a, const nghttp3_pri *b);

#endif /* !defined(NGHTTP3_HTTP_H) */
