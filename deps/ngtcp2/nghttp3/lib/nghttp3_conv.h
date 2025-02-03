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
#ifndef NGHTTP3_CONV_H
#define NGHTTP3_CONV_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#ifdef HAVE_ARPA_INET_H
#  include <arpa/inet.h>
#endif /* defined(HAVE_ARPA_INET_H) */

#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif /* defined(HAVE_NETINET_IN_H) */

#ifdef HAVE_BYTESWAP_H
#  include <byteswap.h>
#endif /* defined(HAVE_BYTESWAP_H) */

#ifdef HAVE_ENDIAN_H
#  include <endian.h>
#endif /* defined(HAVE_ENDIAN_H) */

#ifdef HAVE_SYS_ENDIAN_H
#  include <sys/endian.h>
#endif /* defined(HAVE_SYS_ENDIAN_H) */

#ifdef __APPLE__
#  include <libkern/OSByteOrder.h>
#endif /* defined(__APPLE__) */

#include <nghttp3/nghttp3.h>

#if HAVE_DECL_BE64TOH
#  define nghttp3_ntohl64(N) be64toh(N)
#  define nghttp3_htonl64(N) htobe64(N)
#else /* !HAVE_DECL_BE64TOH */
#  ifdef WORDS_BIGENDIAN
#    define nghttp3_ntohl64(N) (N)
#    define nghttp3_htonl64(N) (N)
#  else /* !defined(WORDS_BIGENDIAN) */
#    if HAVE_DECL_BSWAP_64
#      define nghttp3_bswap64 bswap_64
#    elif defined(WIN32)
#      define nghttp3_bswap64 _byteswap_uint64
#    elif defined(__APPLE__)
#      define nghttp3_bswap64 OSSwapInt64
#    else /* !(HAVE_DECL_BSWAP_64 || defined(WIN32) || defined(__APPLE__)) */
#      define nghttp3_bswap64(N)                                               \
        ((uint64_t)(ntohl((uint32_t)(N))) << 32 | ntohl((uint32_t)((N) >> 32)))
#    endif /* !(HAVE_DECL_BSWAP_64 || defined(WIN32) || defined(__APPLE__)) */
#    define nghttp3_ntohl64(N) nghttp3_bswap64(N)
#    define nghttp3_htonl64(N) nghttp3_bswap64(N)
#  endif /* !defined(WORDS_BIGENDIAN) */
#endif   /* !HAVE_DECL_BE64TOH */

#ifdef WIN32
/* Windows requires ws2_32 library for ntonl family of functions.  We
   define inline functions for those functions so that we don't have
   dependency on that lib. */

#  ifdef _MSC_VER
#    define STIN static __inline
#  else /* !defined(_MSC_VER) */
#    define STIN static inline
#  endif /* !defined(_MSC_VER) */

STIN uint32_t htonl(uint32_t hostlong) {
  uint32_t res;
  unsigned char *p = (unsigned char *)&res;
  *p++ = (unsigned char)(hostlong >> 24);
  *p++ = (hostlong >> 16) & 0xffu;
  *p++ = (hostlong >> 8) & 0xffu;
  *p = hostlong & 0xffu;
  return res;
}

STIN uint16_t htons(uint16_t hostshort) {
  uint16_t res;
  unsigned char *p = (unsigned char *)&res;
  *p++ = (unsigned char)(hostshort >> 8);
  *p = hostshort & 0xffu;
  return res;
}

STIN uint32_t ntohl(uint32_t netlong) {
  uint32_t res;
  unsigned char *p = (unsigned char *)&netlong;
  res = (uint32_t)(*p++ << 24);
  res += (uint32_t)(*p++ << 16);
  res += (uint32_t)(*p++ << 8);
  res += *p;
  return res;
}

STIN uint16_t ntohs(uint16_t netshort) {
  uint16_t res;
  unsigned char *p = (unsigned char *)&netshort;
  res = (uint16_t)(*p++ << 8);
  res += *p;
  return res;
}

#endif /* defined(WIN32) */

/*
 * nghttp3_get_varint reads variable-length unsigned integer from |p|,
 * and stores it in the buffer pointed by |dest| in host byte order.
 * It returns |p| plus the number of bytes read from |p|.
 */
const uint8_t *nghttp3_get_varint(int64_t *dest, const uint8_t *p);

/*
 * nghttp3_get_varint_fb reads first byte of encoded variable-length
 * integer from |p|.
 */
int64_t nghttp3_get_varint_fb(const uint8_t *p);

/*
 * nghttp3_get_varintlen returns the required number of bytes to read
 * variable-length integer starting at |p|.
 */
size_t nghttp3_get_varintlen(const uint8_t *p);

/*
 * nghttp3_put_uint64be writes |n| in host byte order in |p| in
 * network byte order.  It returns the one beyond of the last written
 * position.
 */
uint8_t *nghttp3_put_uint64be(uint8_t *p, uint64_t n);

/*
 * nghttp3_put_uint32be writes |n| in host byte order in |p| in
 * network byte order.  It returns the one beyond of the last written
 * position.
 */
uint8_t *nghttp3_put_uint32be(uint8_t *p, uint32_t n);

/*
 * nghttp3_put_uint16be writes |n| in host byte order in |p| in
 * network byte order.  It returns the one beyond of the last written
 * position.
 */
uint8_t *nghttp3_put_uint16be(uint8_t *p, uint16_t n);

/*
 * nghttp3_put_varint writes |n| in |p| using variable-length integer
 * encoding.  It returns the one beyond of the last written position.
 */
uint8_t *nghttp3_put_varint(uint8_t *p, int64_t n);

/*
 * nghttp3_put_varintlen returns the required number of bytes to
 * encode |n|.
 */
size_t nghttp3_put_varintlen(int64_t n);

/*
 * nghttp3_ord_stream_id returns the ordinal number of |stream_id|.
 */
uint64_t nghttp3_ord_stream_id(int64_t stream_id);

/*
 * NGHTTP3_PRI_INC_MASK is a bit mask to retrieve incremental bit from
 * a value produced by nghttp3_pri_to_uint8.
 */
#define NGHTTP3_PRI_INC_MASK (1 << 7)

#endif /* !defined(NGHTTP3_CONV_H) */
