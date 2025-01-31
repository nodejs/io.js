/*
 * nghttp3
 *
 * Copyright (c) 2019 nghttp3 contributors
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
#include "nghttp3_conv.h"

#include <string.h>
#include <assert.h>

#include "nghttp3_str.h"
#include "nghttp3_unreachable.h"

const uint8_t *nghttp3_get_varint(int64_t *dest, const uint8_t *p) {
  union {
    uint8_t n8;
    uint16_t n16;
    uint32_t n32;
    uint64_t n64;
  } n;

  switch (*p >> 6) {
  case 0:
    *dest = *p++;
    return p;
  case 1:
    memcpy(&n, p, 2);
    n.n8 &= 0x3f;
    *dest = ntohs(n.n16);

    return p + 2;
  case 2:
    memcpy(&n, p, 4);
    n.n8 &= 0x3f;
    *dest = ntohl(n.n32);

    return p + 4;
  case 3:
    memcpy(&n, p, 8);
    n.n8 &= 0x3f;
    *dest = (int64_t)nghttp3_ntohl64(n.n64);

    return p + 8;
  default:
    nghttp3_unreachable();
  }
}

int64_t nghttp3_get_varint_fb(const uint8_t *p) { return *p & 0x3f; }

size_t nghttp3_get_varintlen(const uint8_t *p) {
  return (size_t)(1u << (*p >> 6));
}

uint8_t *nghttp3_put_uint64be(uint8_t *p, uint64_t n) {
  n = nghttp3_htonl64(n);
  return nghttp3_cpymem(p, (const uint8_t *)&n, sizeof(n));
}

uint8_t *nghttp3_put_uint32be(uint8_t *p, uint32_t n) {
  n = htonl(n);
  return nghttp3_cpymem(p, (const uint8_t *)&n, sizeof(n));
}

uint8_t *nghttp3_put_uint16be(uint8_t *p, uint16_t n) {
  n = htons(n);
  return nghttp3_cpymem(p, (const uint8_t *)&n, sizeof(n));
}

uint8_t *nghttp3_put_varint(uint8_t *p, int64_t n) {
  uint8_t *rv;
  if (n < 64) {
    *p++ = (uint8_t)n;
    return p;
  }
  if (n < 16384) {
    rv = nghttp3_put_uint16be(p, (uint16_t)n);
    *p |= 0x40;
    return rv;
  }
  if (n < 1073741824) {
    rv = nghttp3_put_uint32be(p, (uint32_t)n);
    *p |= 0x80;
    return rv;
  }
  assert(n < 4611686018427387904LL);
  rv = nghttp3_put_uint64be(p, (uint64_t)n);
  *p |= 0xc0;
  return rv;
}

size_t nghttp3_put_varintlen(int64_t n) {
  if (n < 64) {
    return 1;
  }
  if (n < 16384) {
    return 2;
  }
  if (n < 1073741824) {
    return 4;
  }
  assert(n < 4611686018427387904LL);
  return 8;
}

uint64_t nghttp3_ord_stream_id(int64_t stream_id) {
  return (uint64_t)(stream_id >> 2) + 1;
}
