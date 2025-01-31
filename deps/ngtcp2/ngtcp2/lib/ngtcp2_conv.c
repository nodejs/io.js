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
#include "ngtcp2_conv.h"

#include <string.h>
#include <assert.h>

#include "ngtcp2_str.h"
#include "ngtcp2_pkt.h"
#include "ngtcp2_net.h"
#include "ngtcp2_unreachable.h"

const uint8_t *ngtcp2_get_uint64be(uint64_t *dest, const uint8_t *p) {
  memcpy(dest, p, sizeof(*dest));
  *dest = ngtcp2_ntohl64(*dest);
  return p + sizeof(*dest);
}

const uint8_t *ngtcp2_get_uint32be(uint32_t *dest, const uint8_t *p) {
  memcpy(dest, p, sizeof(*dest));
  *dest = ngtcp2_ntohl(*dest);
  return p + sizeof(*dest);
}

const uint8_t *ngtcp2_get_uint24be(uint32_t *dest, const uint8_t *p) {
  *dest = 0;
  memcpy(((uint8_t *)dest) + 1, p, 3);
  *dest = ngtcp2_ntohl(*dest);
  return p + 3;
}

const uint8_t *ngtcp2_get_uint16be(uint16_t *dest, const uint8_t *p) {
  memcpy(dest, p, sizeof(*dest));
  *dest = ngtcp2_ntohs(*dest);
  return p + sizeof(*dest);
}

const uint8_t *ngtcp2_get_uint16(uint16_t *dest, const uint8_t *p) {
  memcpy(dest, p, sizeof(*dest));
  return p + sizeof(*dest);
}

static const uint8_t *get_uvarint(uint64_t *dest, const uint8_t *p) {
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
    *dest = ngtcp2_ntohs(n.n16);

    return p + 2;
  case 2:
    memcpy(&n, p, 4);
    n.n8 &= 0x3f;
    *dest = ngtcp2_ntohl(n.n32);

    return p + 4;
  case 3:
    memcpy(&n, p, 8);
    n.n8 &= 0x3f;
    *dest = ngtcp2_ntohl64(n.n64);

    return p + 8;
  default:
    ngtcp2_unreachable();
  }
}

const uint8_t *ngtcp2_get_uvarint(uint64_t *dest, const uint8_t *p) {
  return get_uvarint(dest, p);
}

const uint8_t *ngtcp2_get_varint(int64_t *dest, const uint8_t *p) {
  return get_uvarint((uint64_t *)dest, p);
}

int64_t ngtcp2_get_pkt_num(const uint8_t *p, size_t pkt_numlen) {
  uint32_t l;
  uint16_t s;

  switch (pkt_numlen) {
  case 1:
    return *p;
  case 2:
    ngtcp2_get_uint16be(&s, p);
    return (int64_t)s;
  case 3:
    ngtcp2_get_uint24be(&l, p);
    return (int64_t)l;
  case 4:
    ngtcp2_get_uint32be(&l, p);
    return (int64_t)l;
  default:
    ngtcp2_unreachable();
  }
}

uint8_t *ngtcp2_put_uint64be(uint8_t *p, uint64_t n) {
  n = ngtcp2_htonl64(n);
  return ngtcp2_cpymem(p, (const uint8_t *)&n, sizeof(n));
}

uint8_t *ngtcp2_put_uint32be(uint8_t *p, uint32_t n) {
  n = ngtcp2_htonl(n);
  return ngtcp2_cpymem(p, (const uint8_t *)&n, sizeof(n));
}

uint8_t *ngtcp2_put_uint24be(uint8_t *p, uint32_t n) {
  n = ngtcp2_htonl(n);
  return ngtcp2_cpymem(p, ((const uint8_t *)&n) + 1, 3);
}

uint8_t *ngtcp2_put_uint16be(uint8_t *p, uint16_t n) {
  n = ngtcp2_htons(n);
  return ngtcp2_cpymem(p, (const uint8_t *)&n, sizeof(n));
}

uint8_t *ngtcp2_put_uint16(uint8_t *p, uint16_t n) {
  return ngtcp2_cpymem(p, (const uint8_t *)&n, sizeof(n));
}

uint8_t *ngtcp2_put_uvarint(uint8_t *p, uint64_t n) {
  uint8_t *rv;
  if (n < 64) {
    *p++ = (uint8_t)n;
    return p;
  }
  if (n < 16384) {
    rv = ngtcp2_put_uint16be(p, (uint16_t)n);
    *p |= 0x40;
    return rv;
  }
  if (n < 1073741824) {
    rv = ngtcp2_put_uint32be(p, (uint32_t)n);
    *p |= 0x80;
    return rv;
  }
  assert(n < 4611686018427387904ULL);
  rv = ngtcp2_put_uint64be(p, n);
  *p |= 0xc0;
  return rv;
}

uint8_t *ngtcp2_put_uvarint30(uint8_t *p, uint32_t n) {
  uint8_t *rv;

  assert(n < 1073741824);

  rv = ngtcp2_put_uint32be(p, n);
  *p |= 0x80;

  return rv;
}

uint8_t *ngtcp2_put_pkt_num(uint8_t *p, int64_t pkt_num, size_t len) {
  switch (len) {
  case 1:
    *p++ = (uint8_t)pkt_num;
    return p;
  case 2:
    return ngtcp2_put_uint16be(p, (uint16_t)pkt_num);
  case 3:
    return ngtcp2_put_uint24be(p, (uint32_t)pkt_num);
  case 4:
    return ngtcp2_put_uint32be(p, (uint32_t)pkt_num);
  default:
    ngtcp2_unreachable();
  }
}

size_t ngtcp2_get_uvarintlen(const uint8_t *p) {
  return (size_t)(1u << (*p >> 6));
}

size_t ngtcp2_put_uvarintlen(uint64_t n) {
  if (n < 64) {
    return 1;
  }
  if (n < 16384) {
    return 2;
  }
  if (n < 1073741824) {
    return 4;
  }
  assert(n < 4611686018427387904ULL);
  return 8;
}

uint64_t ngtcp2_ord_stream_id(int64_t stream_id) {
  return (uint64_t)(stream_id >> 2) + 1;
}
