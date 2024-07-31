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

const uint8_t *ngtcp2_get_uint64(uint64_t *dest, const uint8_t *p) {
  uint64_t n;
  memcpy(&n, p, sizeof(n));
  *dest = ngtcp2_ntohl64(n);
  return p + sizeof(n);
}

const uint8_t *ngtcp2_get_uint48(uint64_t *dest, const uint8_t *p) {
  uint64_t n = 0;
  memcpy(((uint8_t *)&n) + 2, p, 6);
  *dest = ngtcp2_ntohl64(n);
  return p + 6;
}

const uint8_t *ngtcp2_get_uint32(uint32_t *dest, const uint8_t *p) {
  uint32_t n;
  memcpy(&n, p, sizeof(n));
  *dest = ngtcp2_ntohl(n);
  return p + sizeof(n);
}

const uint8_t *ngtcp2_get_uint24(uint32_t *dest, const uint8_t *p) {
  uint32_t n = 0;
  memcpy(((uint8_t *)&n) + 1, p, 3);
  *dest = ngtcp2_ntohl(n);
  return p + 3;
}

const uint8_t *ngtcp2_get_uint16(uint16_t *dest, const uint8_t *p) {
  uint16_t n;
  memcpy(&n, p, sizeof(n));
  *dest = ngtcp2_ntohs(n);
  return p + sizeof(n);
}

const uint8_t *ngtcp2_get_uint16be(uint16_t *dest, const uint8_t *p) {
  memcpy(dest, p, sizeof(*dest));
  return p + sizeof(*dest);
}

static uint64_t get_uvarint(size_t *plen, const uint8_t *p) {
  union {
    uint8_t n8;
    uint16_t n16;
    uint32_t n32;
    uint64_t n64;
  } n;

  *plen = (size_t)(1u << (*p >> 6));

  switch (*plen) {
  case 1:
    return *p;
  case 2:
    memcpy(&n, p, 2);
    n.n8 &= 0x3f;
    return ngtcp2_ntohs(n.n16);
  case 4:
    memcpy(&n, p, 4);
    n.n8 &= 0x3f;
    return ngtcp2_ntohl(n.n32);
  case 8:
    memcpy(&n, p, 8);
    n.n8 &= 0x3f;
    return ngtcp2_ntohl64(n.n64);
  default:
    ngtcp2_unreachable();
  }
}

const uint8_t *ngtcp2_get_uvarint(uint64_t *dest, const uint8_t *p) {
  size_t len;

  *dest = get_uvarint(&len, p);

  return p + len;
}

const uint8_t *ngtcp2_get_varint(int64_t *dest, const uint8_t *p) {
  size_t len;

  *dest = (int64_t)get_uvarint(&len, p);

  return p + len;
}

int64_t ngtcp2_get_pkt_num(const uint8_t *p, size_t pkt_numlen) {
  uint32_t l;
  uint16_t s;

  switch (pkt_numlen) {
  case 1:
    return *p;
  case 2:
    ngtcp2_get_uint16(&s, p);
    return (int64_t)s;
  case 3:
    ngtcp2_get_uint24(&l, p);
    return (int64_t)l;
  case 4:
    ngtcp2_get_uint32(&l, p);
    return (int64_t)l;
  default:
    ngtcp2_unreachable();
  }
}

uint8_t *ngtcp2_put_uint64be(uint8_t *p, uint64_t n) {
  n = ngtcp2_htonl64(n);
  return ngtcp2_cpymem(p, (const uint8_t *)&n, sizeof(n));
}

uint8_t *ngtcp2_put_uint48be(uint8_t *p, uint64_t n) {
  n = ngtcp2_htonl64(n);
  return ngtcp2_cpymem(p, ((const uint8_t *)&n) + 2, 6);
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
    ngtcp2_put_uint16be(p, (uint16_t)pkt_num);
    return p + 2;
  case 3:
    ngtcp2_put_uint24be(p, (uint32_t)pkt_num);
    return p + 3;
  case 4:
    ngtcp2_put_uint32be(p, (uint32_t)pkt_num);
    return p + 4;
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

int64_t ngtcp2_nth_server_bidi_id(uint64_t n) {
  if (n == 0) {
    return 0;
  }

  if ((NGTCP2_MAX_VARINT >> 2) < n - 1) {
    return NGTCP2_MAX_SERVER_STREAM_ID_BIDI;
  }

  return (int64_t)(((n - 1) << 2) | 0x01);
}

int64_t ngtcp2_nth_client_bidi_id(uint64_t n) {
  if (n == 0) {
    return 0;
  }

  if ((NGTCP2_MAX_VARINT >> 2) < n - 1) {
    return NGTCP2_MAX_CLIENT_STREAM_ID_BIDI;
  }

  return (int64_t)((n - 1) << 2);
}

int64_t ngtcp2_nth_server_uni_id(uint64_t n) {
  if (n == 0) {
    return 0;
  }

  if ((NGTCP2_MAX_VARINT >> 2) < n - 1) {
    return NGTCP2_MAX_SERVER_STREAM_ID_UNI;
  }

  return (int64_t)(((n - 1) << 2) | 0x03);
}

int64_t ngtcp2_nth_client_uni_id(uint64_t n) {
  if (n == 0) {
    return 0;
  }

  if ((NGTCP2_MAX_VARINT >> 2) < n - 1) {
    return NGTCP2_MAX_CLIENT_STREAM_ID_UNI;
  }

  return (int64_t)(((n - 1) << 2) | 0x02);
}

uint64_t ngtcp2_ord_stream_id(int64_t stream_id) {
  return (uint64_t)(stream_id >> 2) + 1;
}
