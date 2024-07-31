/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2012 Tatsuhiro Tsujikawa
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
#include "nghttp2_alpn.h"

#include <string.h>

static int select_alpn(const unsigned char **out, unsigned char *outlen,
                       const unsigned char *in, unsigned int inlen,
                       const char *key, unsigned int keylen) {
  unsigned int i;
  for (i = 0; i + keylen <= inlen; i += (unsigned int)(in[i] + 1)) {
    if (memcmp(&in[i], key, keylen) == 0) {
      *out = (unsigned char *)&in[i + 1];
      *outlen = in[i];
      return 0;
    }
  }
  return -1;
}

#define NGHTTP2_HTTP_1_1_ALPN "\x8http/1.1"
#define NGHTTP2_HTTP_1_1_ALPN_LEN (sizeof(NGHTTP2_HTTP_1_1_ALPN) - 1)

int nghttp2_select_next_protocol(unsigned char **out, unsigned char *outlen,
                                 const unsigned char *in, unsigned int inlen) {
  if (select_alpn((const unsigned char **)out, outlen, in, inlen,
                  NGHTTP2_PROTO_ALPN, NGHTTP2_PROTO_ALPN_LEN) == 0) {
    return 1;
  }
  if (select_alpn((const unsigned char **)out, outlen, in, inlen,
                  NGHTTP2_HTTP_1_1_ALPN, NGHTTP2_HTTP_1_1_ALPN_LEN) == 0) {
    return 0;
  }
  return -1;
}

int nghttp2_select_alpn(const unsigned char **out, unsigned char *outlen,
                        const unsigned char *in, unsigned int inlen) {
  if (select_alpn(out, outlen, in, inlen, NGHTTP2_PROTO_ALPN,
                  NGHTTP2_PROTO_ALPN_LEN) == 0) {
    return 1;
  }
  if (select_alpn(out, outlen, in, inlen, NGHTTP2_HTTP_1_1_ALPN,
                  NGHTTP2_HTTP_1_1_ALPN_LEN) == 0) {
    return 0;
  }
  return -1;
}
