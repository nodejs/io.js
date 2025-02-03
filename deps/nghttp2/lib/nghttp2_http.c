/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2015 Tatsuhiro Tsujikawa
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
#include "nghttp2_http.h"

#include <string.h>
#include <assert.h>
#include <stdio.h>

#include "nghttp2_hd.h"
#include "nghttp2_helper.h"
#include "nghttp2_extpri.h"
#include "sfparse.h"

static uint8_t downcase(uint8_t c) {
  return 'A' <= c && c <= 'Z' ? (uint8_t)(c - 'A' + 'a') : c;
}

static int memieq(const void *a, const void *b, size_t n) {
  size_t i;
  const uint8_t *aa = a, *bb = b;

  for (i = 0; i < n; ++i) {
    if (downcase(aa[i]) != downcase(bb[i])) {
      return 0;
    }
  }
  return 1;
}

#define lstrieq(A, B, N) ((sizeof((A)) - 1) == (N) && memieq((A), (B), (N)))

static int64_t parse_uint(const uint8_t *s, size_t len) {
  int64_t n = 0;
  size_t i;
  if (len == 0) {
    return -1;
  }
  for (i = 0; i < len; ++i) {
    if ('0' <= s[i] && s[i] <= '9') {
      if (n > INT64_MAX / 10) {
        return -1;
      }
      n *= 10;
      if (n > INT64_MAX - (s[i] - '0')) {
        return -1;
      }
      n += s[i] - '0';
      continue;
    }
    return -1;
  }
  return n;
}

static int check_pseudo_header(nghttp2_stream *stream, const nghttp2_hd_nv *nv,
                               uint32_t flag) {
  if ((stream->http_flags & flag) || nv->value->len == 0) {
    return 0;
  }
  stream->http_flags = stream->http_flags | flag;
  return 1;
}

static int expect_response_body(nghttp2_stream *stream) {
  return (stream->http_flags & NGHTTP2_HTTP_FLAG_METH_HEAD) == 0 &&
         stream->status_code / 100 != 1 && stream->status_code != 304 &&
         stream->status_code != 204;
}

/* For "http" or "https" URIs, OPTIONS request may have "*" in :path
   header field to represent system-wide OPTIONS request.  Otherwise,
   :path header field value must start with "/".  This function must
   be called after ":method" header field was received.  This function
   returns nonzero if path is valid.*/
static int check_path(nghttp2_stream *stream) {
  return (stream->http_flags & NGHTTP2_HTTP_FLAG_SCHEME_HTTP) == 0 ||
         ((stream->http_flags & NGHTTP2_HTTP_FLAG_PATH_REGULAR) ||
          ((stream->http_flags & NGHTTP2_HTTP_FLAG_METH_OPTIONS) &&
           (stream->http_flags & NGHTTP2_HTTP_FLAG_PATH_ASTERISK)));
}

static int http_request_on_header(nghttp2_stream *stream, nghttp2_hd_nv *nv,
                                  int trailer, int connect_protocol) {
  nghttp2_extpri extpri;

  if (nv->name->base[0] == ':') {
    if (trailer ||
        (stream->http_flags & NGHTTP2_HTTP_FLAG_PSEUDO_HEADER_DISALLOWED)) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
  }

  switch (nv->token) {
  case NGHTTP2_TOKEN__AUTHORITY:
    if (!check_pseudo_header(stream, nv, NGHTTP2_HTTP_FLAG__AUTHORITY)) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    break;
  case NGHTTP2_TOKEN__METHOD:
    if (!check_pseudo_header(stream, nv, NGHTTP2_HTTP_FLAG__METHOD)) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    switch (nv->value->len) {
    case 4:
      if (lstreq("HEAD", nv->value->base, nv->value->len)) {
        stream->http_flags |= NGHTTP2_HTTP_FLAG_METH_HEAD;
      }
      break;
    case 7:
      switch (nv->value->base[6]) {
      case 'T':
        if (lstreq("CONNECT", nv->value->base, nv->value->len)) {
          if (stream->stream_id % 2 == 0) {
            /* we won't allow CONNECT for push */
            return NGHTTP2_ERR_HTTP_HEADER;
          }
          stream->http_flags |= NGHTTP2_HTTP_FLAG_METH_CONNECT;
        }
        break;
      case 'S':
        if (lstreq("OPTIONS", nv->value->base, nv->value->len)) {
          stream->http_flags |= NGHTTP2_HTTP_FLAG_METH_OPTIONS;
        }
        break;
      }
      break;
    }
    break;
  case NGHTTP2_TOKEN__PATH:
    if (!check_pseudo_header(stream, nv, NGHTTP2_HTTP_FLAG__PATH)) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    if (nv->value->base[0] == '/') {
      stream->http_flags |= NGHTTP2_HTTP_FLAG_PATH_REGULAR;
    } else if (nv->value->len == 1 && nv->value->base[0] == '*') {
      stream->http_flags |= NGHTTP2_HTTP_FLAG_PATH_ASTERISK;
    }
    break;
  case NGHTTP2_TOKEN__SCHEME:
    if (!check_pseudo_header(stream, nv, NGHTTP2_HTTP_FLAG__SCHEME)) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    if ((nv->value->len == 4 && memieq("http", nv->value->base, 4)) ||
        (nv->value->len == 5 && memieq("https", nv->value->base, 5))) {
      stream->http_flags |= NGHTTP2_HTTP_FLAG_SCHEME_HTTP;
    }
    break;
  case NGHTTP2_TOKEN__PROTOCOL:
    if (!connect_protocol) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }

    if (!check_pseudo_header(stream, nv, NGHTTP2_HTTP_FLAG__PROTOCOL)) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    break;
  case NGHTTP2_TOKEN_HOST:
    if (!check_pseudo_header(stream, nv, NGHTTP2_HTTP_FLAG_HOST)) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    break;
  case NGHTTP2_TOKEN_CONTENT_LENGTH: {
    if (stream->content_length != -1) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    stream->content_length = parse_uint(nv->value->base, nv->value->len);
    if (stream->content_length == -1) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    break;
  }
  /* disallowed header fields */
  case NGHTTP2_TOKEN_CONNECTION:
  case NGHTTP2_TOKEN_KEEP_ALIVE:
  case NGHTTP2_TOKEN_PROXY_CONNECTION:
  case NGHTTP2_TOKEN_TRANSFER_ENCODING:
  case NGHTTP2_TOKEN_UPGRADE:
    return NGHTTP2_ERR_HTTP_HEADER;
  case NGHTTP2_TOKEN_TE:
    if (!lstrieq("trailers", nv->value->base, nv->value->len)) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    break;
  case NGHTTP2_TOKEN_PRIORITY:
    if (!trailer &&
        /* Do not parse the header field in PUSH_PROMISE. */
        (stream->stream_id & 1) &&
        (stream->flags & NGHTTP2_STREAM_FLAG_NO_RFC7540_PRIORITIES) &&
        !(stream->http_flags & NGHTTP2_HTTP_FLAG_BAD_PRIORITY)) {
      nghttp2_extpri_from_uint8(&extpri, stream->http_extpri);
      if (nghttp2_http_parse_priority(&extpri, nv->value->base,
                                      nv->value->len) == 0) {
        stream->http_extpri = nghttp2_extpri_to_uint8(&extpri);
        stream->http_flags |= NGHTTP2_HTTP_FLAG_PRIORITY;
      } else {
        stream->http_flags &= (uint32_t)~NGHTTP2_HTTP_FLAG_PRIORITY;
        stream->http_flags |= NGHTTP2_HTTP_FLAG_BAD_PRIORITY;
      }
    }
    break;
  default:
    if (nv->name->base[0] == ':') {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
  }

  if (nv->name->base[0] != ':') {
    stream->http_flags |= NGHTTP2_HTTP_FLAG_PSEUDO_HEADER_DISALLOWED;
  }

  return 0;
}

static int http_response_on_header(nghttp2_stream *stream, nghttp2_hd_nv *nv,
                                   int trailer) {
  if (nv->name->base[0] == ':') {
    if (trailer ||
        (stream->http_flags & NGHTTP2_HTTP_FLAG_PSEUDO_HEADER_DISALLOWED)) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
  }

  switch (nv->token) {
  case NGHTTP2_TOKEN__STATUS: {
    if (!check_pseudo_header(stream, nv, NGHTTP2_HTTP_FLAG__STATUS)) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    if (nv->value->len != 3) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    stream->status_code = (int16_t)parse_uint(nv->value->base, nv->value->len);
    if (stream->status_code == -1 || stream->status_code == 101) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    break;
  }
  case NGHTTP2_TOKEN_CONTENT_LENGTH: {
    if (stream->status_code == 204) {
      /* content-length header field in 204 response is prohibited by
         RFC 7230.  But some widely used servers send content-length:
         0.  Until they get fixed, we ignore it. */
      if (stream->content_length != -1) {
        /* Found multiple content-length field */
        return NGHTTP2_ERR_HTTP_HEADER;
      }
      if (!lstrieq("0", nv->value->base, nv->value->len)) {
        return NGHTTP2_ERR_HTTP_HEADER;
      }
      stream->content_length = 0;
      return NGHTTP2_ERR_REMOVE_HTTP_HEADER;
    }
    if (stream->status_code / 100 == 1) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    /* https://tools.ietf.org/html/rfc7230#section-3.3.3 */
    if (stream->status_code / 100 == 2 &&
        (stream->http_flags & NGHTTP2_HTTP_FLAG_METH_CONNECT)) {
      return NGHTTP2_ERR_REMOVE_HTTP_HEADER;
    }
    if (stream->content_length != -1) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    stream->content_length = parse_uint(nv->value->base, nv->value->len);
    if (stream->content_length == -1) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    break;
  }
  /* disallowed header fields */
  case NGHTTP2_TOKEN_CONNECTION:
  case NGHTTP2_TOKEN_KEEP_ALIVE:
  case NGHTTP2_TOKEN_PROXY_CONNECTION:
  case NGHTTP2_TOKEN_TRANSFER_ENCODING:
  case NGHTTP2_TOKEN_UPGRADE:
    return NGHTTP2_ERR_HTTP_HEADER;
  case NGHTTP2_TOKEN_TE:
    if (!lstrieq("trailers", nv->value->base, nv->value->len)) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    break;
  default:
    if (nv->name->base[0] == ':') {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
  }

  if (nv->name->base[0] != ':') {
    stream->http_flags |= NGHTTP2_HTTP_FLAG_PSEUDO_HEADER_DISALLOWED;
  }

  return 0;
}

static int check_scheme(const uint8_t *value, size_t len) {
  const uint8_t *last;
  if (len == 0) {
    return 0;
  }

  if (!(('A' <= *value && *value <= 'Z') || ('a' <= *value && *value <= 'z'))) {
    return 0;
  }

  last = value + len;
  ++value;

  for (; value != last; ++value) {
    if (!(('A' <= *value && *value <= 'Z') ||
          ('a' <= *value && *value <= 'z') ||
          ('0' <= *value && *value <= '9') || *value == '+' || *value == '-' ||
          *value == '.')) {
      return 0;
    }
  }
  return 1;
}

static int lws(const uint8_t *s, size_t n) {
  size_t i;
  for (i = 0; i < n; ++i) {
    if (s[i] != ' ' && s[i] != '\t') {
      return 0;
    }
  }
  return 1;
}

/* Generated by genauthoritychartbl.py, but '@' is not allowed */
static char VALID_AUTHORITY_CHARS[] = {
  0 /* NUL  */, 0 /* SOH  */, 0 /* STX  */, 0 /* ETX  */,
  0 /* EOT  */, 0 /* ENQ  */, 0 /* ACK  */, 0 /* BEL  */,
  0 /* BS   */, 0 /* HT   */, 0 /* LF   */, 0 /* VT   */,
  0 /* FF   */, 0 /* CR   */, 0 /* SO   */, 0 /* SI   */,
  0 /* DLE  */, 0 /* DC1  */, 0 /* DC2  */, 0 /* DC3  */,
  0 /* DC4  */, 0 /* NAK  */, 0 /* SYN  */, 0 /* ETB  */,
  0 /* CAN  */, 0 /* EM   */, 0 /* SUB  */, 0 /* ESC  */,
  0 /* FS   */, 0 /* GS   */, 0 /* RS   */, 0 /* US   */,
  0 /* SPC  */, 1 /* !    */, 0 /* "    */, 0 /* #    */,
  1 /* $    */, 1 /* %    */, 1 /* &    */, 1 /* '    */,
  1 /* (    */, 1 /* )    */, 1 /* *    */, 1 /* +    */,
  1 /* ,    */, 1 /* -    */, 1 /* .    */, 0 /* /    */,
  1 /* 0    */, 1 /* 1    */, 1 /* 2    */, 1 /* 3    */,
  1 /* 4    */, 1 /* 5    */, 1 /* 6    */, 1 /* 7    */,
  1 /* 8    */, 1 /* 9    */, 1 /* :    */, 1 /* ;    */,
  0 /* <    */, 1 /* =    */, 0 /* >    */, 0 /* ?    */,
  0 /* @    */, 1 /* A    */, 1 /* B    */, 1 /* C    */,
  1 /* D    */, 1 /* E    */, 1 /* F    */, 1 /* G    */,
  1 /* H    */, 1 /* I    */, 1 /* J    */, 1 /* K    */,
  1 /* L    */, 1 /* M    */, 1 /* N    */, 1 /* O    */,
  1 /* P    */, 1 /* Q    */, 1 /* R    */, 1 /* S    */,
  1 /* T    */, 1 /* U    */, 1 /* V    */, 1 /* W    */,
  1 /* X    */, 1 /* Y    */, 1 /* Z    */, 1 /* [    */,
  0 /* \    */, 1 /* ]    */, 0 /* ^    */, 1 /* _    */,
  0 /* `    */, 1 /* a    */, 1 /* b    */, 1 /* c    */,
  1 /* d    */, 1 /* e    */, 1 /* f    */, 1 /* g    */,
  1 /* h    */, 1 /* i    */, 1 /* j    */, 1 /* k    */,
  1 /* l    */, 1 /* m    */, 1 /* n    */, 1 /* o    */,
  1 /* p    */, 1 /* q    */, 1 /* r    */, 1 /* s    */,
  1 /* t    */, 1 /* u    */, 1 /* v    */, 1 /* w    */,
  1 /* x    */, 1 /* y    */, 1 /* z    */, 0 /* {    */,
  0 /* |    */, 0 /* }    */, 1 /* ~    */, 0 /* DEL  */,
  0 /* 0x80 */, 0 /* 0x81 */, 0 /* 0x82 */, 0 /* 0x83 */,
  0 /* 0x84 */, 0 /* 0x85 */, 0 /* 0x86 */, 0 /* 0x87 */,
  0 /* 0x88 */, 0 /* 0x89 */, 0 /* 0x8a */, 0 /* 0x8b */,
  0 /* 0x8c */, 0 /* 0x8d */, 0 /* 0x8e */, 0 /* 0x8f */,
  0 /* 0x90 */, 0 /* 0x91 */, 0 /* 0x92 */, 0 /* 0x93 */,
  0 /* 0x94 */, 0 /* 0x95 */, 0 /* 0x96 */, 0 /* 0x97 */,
  0 /* 0x98 */, 0 /* 0x99 */, 0 /* 0x9a */, 0 /* 0x9b */,
  0 /* 0x9c */, 0 /* 0x9d */, 0 /* 0x9e */, 0 /* 0x9f */,
  0 /* 0xa0 */, 0 /* 0xa1 */, 0 /* 0xa2 */, 0 /* 0xa3 */,
  0 /* 0xa4 */, 0 /* 0xa5 */, 0 /* 0xa6 */, 0 /* 0xa7 */,
  0 /* 0xa8 */, 0 /* 0xa9 */, 0 /* 0xaa */, 0 /* 0xab */,
  0 /* 0xac */, 0 /* 0xad */, 0 /* 0xae */, 0 /* 0xaf */,
  0 /* 0xb0 */, 0 /* 0xb1 */, 0 /* 0xb2 */, 0 /* 0xb3 */,
  0 /* 0xb4 */, 0 /* 0xb5 */, 0 /* 0xb6 */, 0 /* 0xb7 */,
  0 /* 0xb8 */, 0 /* 0xb9 */, 0 /* 0xba */, 0 /* 0xbb */,
  0 /* 0xbc */, 0 /* 0xbd */, 0 /* 0xbe */, 0 /* 0xbf */,
  0 /* 0xc0 */, 0 /* 0xc1 */, 0 /* 0xc2 */, 0 /* 0xc3 */,
  0 /* 0xc4 */, 0 /* 0xc5 */, 0 /* 0xc6 */, 0 /* 0xc7 */,
  0 /* 0xc8 */, 0 /* 0xc9 */, 0 /* 0xca */, 0 /* 0xcb */,
  0 /* 0xcc */, 0 /* 0xcd */, 0 /* 0xce */, 0 /* 0xcf */,
  0 /* 0xd0 */, 0 /* 0xd1 */, 0 /* 0xd2 */, 0 /* 0xd3 */,
  0 /* 0xd4 */, 0 /* 0xd5 */, 0 /* 0xd6 */, 0 /* 0xd7 */,
  0 /* 0xd8 */, 0 /* 0xd9 */, 0 /* 0xda */, 0 /* 0xdb */,
  0 /* 0xdc */, 0 /* 0xdd */, 0 /* 0xde */, 0 /* 0xdf */,
  0 /* 0xe0 */, 0 /* 0xe1 */, 0 /* 0xe2 */, 0 /* 0xe3 */,
  0 /* 0xe4 */, 0 /* 0xe5 */, 0 /* 0xe6 */, 0 /* 0xe7 */,
  0 /* 0xe8 */, 0 /* 0xe9 */, 0 /* 0xea */, 0 /* 0xeb */,
  0 /* 0xec */, 0 /* 0xed */, 0 /* 0xee */, 0 /* 0xef */,
  0 /* 0xf0 */, 0 /* 0xf1 */, 0 /* 0xf2 */, 0 /* 0xf3 */,
  0 /* 0xf4 */, 0 /* 0xf5 */, 0 /* 0xf6 */, 0 /* 0xf7 */,
  0 /* 0xf8 */, 0 /* 0xf9 */, 0 /* 0xfa */, 0 /* 0xfb */,
  0 /* 0xfc */, 0 /* 0xfd */, 0 /* 0xfe */, 0 /* 0xff */
};

static int check_authority(const uint8_t *value, size_t len) {
  const uint8_t *last;
  for (last = value + len; value != last; ++value) {
    if (!VALID_AUTHORITY_CHARS[*value]) {
      return 0;
    }
  }
  return 1;
}

int nghttp2_http_on_header(nghttp2_session *session, nghttp2_stream *stream,
                           nghttp2_frame *frame, nghttp2_hd_nv *nv,
                           int trailer) {
  int rv;

  /* We are strict for pseudo header field.  One bad character should
     lead to fail.  OTOH, we should be a bit forgiving for regular
     headers, since existing public internet has so much illegal
     headers floating around and if we kill the stream because of
     this, we may disrupt many web sites and/or libraries.  So we
     become conservative here, and just ignore those illegal regular
     headers. */
  if (!nghttp2_check_header_name(nv->name->base, nv->name->len)) {
    size_t i;
    if (nv->name->len > 0 && nv->name->base[0] == ':') {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    /* header field name must be lower-cased without exception */
    for (i = 0; i < nv->name->len; ++i) {
      uint8_t c = nv->name->base[i];
      if ('A' <= c && c <= 'Z') {
        return NGHTTP2_ERR_HTTP_HEADER;
      }
    }
    /* When ignoring regular headers, we set this flag so that we
       still enforce header field ordering rule for pseudo header
       fields. */
    stream->http_flags |= NGHTTP2_HTTP_FLAG_PSEUDO_HEADER_DISALLOWED;
    return NGHTTP2_ERR_IGN_HTTP_HEADER;
  }

  switch (nv->token) {
  case NGHTTP2_TOKEN__METHOD:
    rv = nghttp2_check_method(nv->value->base, nv->value->len);
    break;
  case NGHTTP2_TOKEN__PATH:
    rv = nghttp2_check_path(nv->value->base, nv->value->len);
    break;
  case NGHTTP2_TOKEN__AUTHORITY:
  case NGHTTP2_TOKEN_HOST:
    if (session->server || frame->hd.type == NGHTTP2_PUSH_PROMISE) {
      rv = check_authority(nv->value->base, nv->value->len);
    } else if (
      stream->flags &
      NGHTTP2_STREAM_FLAG_NO_RFC9113_LEADING_AND_TRAILING_WS_VALIDATION) {
      rv = nghttp2_check_header_value(nv->value->base, nv->value->len);
    } else {
      rv = nghttp2_check_header_value_rfc9113(nv->value->base, nv->value->len);
    }
    break;
  case NGHTTP2_TOKEN__SCHEME:
    rv = check_scheme(nv->value->base, nv->value->len);
    break;
  case NGHTTP2_TOKEN__PROTOCOL:
    /* Check the value consists of just white spaces, which was done
       in check_pseudo_header before
       nghttp2_check_header_value_rfc9113 has been introduced. */
    if ((stream->flags &
         NGHTTP2_STREAM_FLAG_NO_RFC9113_LEADING_AND_TRAILING_WS_VALIDATION) &&
        lws(nv->value->base, nv->value->len)) {
      rv = 0;
      break;
    }
    /* fall through */
  default:
    if (stream->flags &
        NGHTTP2_STREAM_FLAG_NO_RFC9113_LEADING_AND_TRAILING_WS_VALIDATION) {
      rv = nghttp2_check_header_value(nv->value->base, nv->value->len);
    } else {
      rv = nghttp2_check_header_value_rfc9113(nv->value->base, nv->value->len);
    }
  }

  if (rv == 0) {
    assert(nv->name->len > 0);
    if (nv->name->base[0] == ':') {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    /* When ignoring regular headers, we set this flag so that we
       still enforce header field ordering rule for pseudo header
       fields. */
    stream->http_flags |= NGHTTP2_HTTP_FLAG_PSEUDO_HEADER_DISALLOWED;
    return NGHTTP2_ERR_IGN_HTTP_HEADER;
  }

  if (session->server || frame->hd.type == NGHTTP2_PUSH_PROMISE) {
    return http_request_on_header(stream, nv, trailer,
                                  session->server &&
                                    session->pending_enable_connect_protocol);
  }

  return http_response_on_header(stream, nv, trailer);
}

int nghttp2_http_on_request_headers(nghttp2_stream *stream,
                                    nghttp2_frame *frame) {
  if (!(stream->http_flags & NGHTTP2_HTTP_FLAG__PROTOCOL) &&
      (stream->http_flags & NGHTTP2_HTTP_FLAG_METH_CONNECT)) {
    if ((stream->http_flags &
         (NGHTTP2_HTTP_FLAG__SCHEME | NGHTTP2_HTTP_FLAG__PATH)) ||
        (stream->http_flags & NGHTTP2_HTTP_FLAG__AUTHORITY) == 0) {
      return -1;
    }
    stream->content_length = -1;
  } else {
    if ((stream->http_flags & NGHTTP2_HTTP_FLAG_REQ_HEADERS) !=
          NGHTTP2_HTTP_FLAG_REQ_HEADERS ||
        (stream->http_flags &
         (NGHTTP2_HTTP_FLAG__AUTHORITY | NGHTTP2_HTTP_FLAG_HOST)) == 0) {
      return -1;
    }
    if ((stream->http_flags & NGHTTP2_HTTP_FLAG__PROTOCOL) &&
        ((stream->http_flags & NGHTTP2_HTTP_FLAG_METH_CONNECT) == 0 ||
         (stream->http_flags & NGHTTP2_HTTP_FLAG__AUTHORITY) == 0)) {
      return -1;
    }
    if (!check_path(stream)) {
      return -1;
    }
  }

  if (frame->hd.type == NGHTTP2_PUSH_PROMISE) {
    /* we are going to reuse data fields for upcoming response.  Clear
       them now, except for method flags. */
    stream->http_flags &= NGHTTP2_HTTP_FLAG_METH_ALL;
    stream->content_length = -1;
  }

  return 0;
}

int nghttp2_http_on_response_headers(nghttp2_stream *stream) {
  if ((stream->http_flags & NGHTTP2_HTTP_FLAG__STATUS) == 0) {
    return -1;
  }

  if (stream->status_code / 100 == 1) {
    /* non-final response */
    stream->http_flags = (stream->http_flags & NGHTTP2_HTTP_FLAG_METH_ALL) |
                         NGHTTP2_HTTP_FLAG_EXPECT_FINAL_RESPONSE;
    stream->content_length = -1;
    stream->status_code = -1;
    return 0;
  }

  stream->http_flags =
    stream->http_flags & (uint32_t)~NGHTTP2_HTTP_FLAG_EXPECT_FINAL_RESPONSE;

  if (!expect_response_body(stream)) {
    stream->content_length = 0;
  } else if (stream->http_flags & (NGHTTP2_HTTP_FLAG_METH_CONNECT |
                                   NGHTTP2_HTTP_FLAG_METH_UPGRADE_WORKAROUND)) {
    stream->content_length = -1;
  }

  return 0;
}

int nghttp2_http_on_trailer_headers(nghttp2_stream *stream,
                                    nghttp2_frame *frame) {
  (void)stream;

  if ((frame->hd.flags & NGHTTP2_FLAG_END_STREAM) == 0) {
    return -1;
  }

  return 0;
}

int nghttp2_http_on_remote_end_stream(nghttp2_stream *stream) {
  if (stream->http_flags & NGHTTP2_HTTP_FLAG_EXPECT_FINAL_RESPONSE) {
    return -1;
  }

  if (stream->content_length != -1 &&
      stream->content_length != stream->recv_content_length) {
    return -1;
  }

  return 0;
}

int nghttp2_http_on_data_chunk(nghttp2_stream *stream, size_t n) {
  stream->recv_content_length += (int64_t)n;

  if ((stream->http_flags & NGHTTP2_HTTP_FLAG_EXPECT_FINAL_RESPONSE) ||
      (stream->content_length != -1 &&
       stream->recv_content_length > stream->content_length)) {
    return -1;
  }

  return 0;
}

void nghttp2_http_record_request_method(nghttp2_stream *stream,
                                        nghttp2_frame *frame) {
  const nghttp2_nv *nva;
  size_t nvlen;
  size_t i;

  switch (frame->hd.type) {
  case NGHTTP2_HEADERS:
    nva = frame->headers.nva;
    nvlen = frame->headers.nvlen;
    break;
  case NGHTTP2_PUSH_PROMISE:
    nva = frame->push_promise.nva;
    nvlen = frame->push_promise.nvlen;
    break;
  default:
    return;
  }

  /* TODO we should do this strictly. */
  for (i = 0; i < nvlen; ++i) {
    const nghttp2_nv *nv = &nva[i];
    if (!(nv->namelen == 7 && nv->name[6] == 'd' &&
          memcmp(":metho", nv->name, nv->namelen - 1) == 0)) {
      continue;
    }
    if (lstreq("CONNECT", nv->value, nv->valuelen)) {
      stream->http_flags |= NGHTTP2_HTTP_FLAG_METH_CONNECT;
      return;
    }
    if (lstreq("HEAD", nv->value, nv->valuelen)) {
      stream->http_flags |= NGHTTP2_HTTP_FLAG_METH_HEAD;
      return;
    }
    return;
  }
}

int nghttp2_http_parse_priority(nghttp2_extpri *dest, const uint8_t *value,
                                size_t valuelen) {
  nghttp2_extpri pri = *dest;
  sf_parser sfp;
  sf_vec key;
  sf_value val;
  int rv;

  sf_parser_init(&sfp, value, valuelen);

  for (;;) {
    rv = sf_parser_dict(&sfp, &key, &val);
    if (rv != 0) {
      if (rv == SF_ERR_EOF) {
        break;
      }

      return NGHTTP2_ERR_INVALID_ARGUMENT;
    }

    if (key.len != 1) {
      continue;
    }

    switch (key.base[0]) {
    case 'i':
      if (val.type != SF_TYPE_BOOLEAN) {
        return NGHTTP2_ERR_INVALID_ARGUMENT;
      }

      pri.inc = val.boolean;

      break;
    case 'u':
      if (val.type != SF_TYPE_INTEGER ||
          val.integer < NGHTTP2_EXTPRI_URGENCY_HIGH ||
          NGHTTP2_EXTPRI_URGENCY_LOW < val.integer) {
        return NGHTTP2_ERR_INVALID_ARGUMENT;
      }

      pri.urgency = (uint32_t)val.integer;

      break;
    }
  }

  *dest = pri;

  return 0;
}
