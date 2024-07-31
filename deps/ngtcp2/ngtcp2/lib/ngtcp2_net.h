/*
 * ngtcp2
 *
 * Copyright (c) 2022 ngtcp2 contributors
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
#ifndef NGTCP2_NET_H
#define NGTCP2_NET_H

/* This header file is explicitly allowed to be shared with
   ngtcp2_crypto library. */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#ifdef HAVE_ARPA_INET_H
#  include <arpa/inet.h>
#endif /* HAVE_ARPA_INET_H */

#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif /* HAVE_NETINET_IN_H */

#ifdef HAVE_BYTESWAP_H
#  include <byteswap.h>
#endif /* HAVE_BYTESWAP_H */

#ifdef HAVE_ENDIAN_H
#  include <endian.h>
#endif /* HAVE_ENDIAN_H */

#ifdef HAVE_SYS_ENDIAN_H
#  include <sys/endian.h>
#endif /* HAVE_SYS_ENDIAN_H */

#if defined(__APPLE__)
#  include <libkern/OSByteOrder.h>
#endif // __APPLE__

#include <ngtcp2/ngtcp2.h>

#if defined(HAVE_BE64TOH) ||                                                   \
    (defined(HAVE_DECL_BE64TOH) && HAVE_DECL_BE64TOH > 0)
#  define ngtcp2_ntohl64(N) be64toh(N)
#  define ngtcp2_htonl64(N) htobe64(N)
#else /* !HAVE_BE64TOH */
#  if defined(WORDS_BIGENDIAN)
#    define ngtcp2_ntohl64(N) (N)
#    define ngtcp2_htonl64(N) (N)
#  else /* !WORDS_BIGENDIAN */
#    if defined(HAVE_BSWAP_64) ||                                              \
        (defined(HAVE_DECL_BSWAP_64) && HAVE_DECL_BSWAP_64 > 0)
#      define ngtcp2_bswap64 bswap_64
#    elif defined(WIN32)
#      define ngtcp2_bswap64 _byteswap_uint64
#    elif defined(__APPLE__)
#      define ngtcp2_bswap64 OSSwapInt64
#    else /* !HAVE_BSWAP_64 && !WIN32 && !__APPLE__ */
#      define ngtcp2_bswap64(N)                                                \
        ((uint64_t)(ngtcp2_ntohl((uint32_t)(N))) << 32 |                       \
         ngtcp2_ntohl((uint32_t)((N) >> 32)))
#    endif /* !HAVE_BSWAP_64 && !WIN32 && !__APPLE__ */
#    define ngtcp2_ntohl64(N) ngtcp2_bswap64(N)
#    define ngtcp2_htonl64(N) ngtcp2_bswap64(N)
#  endif /* !WORDS_BIGENDIAN */
#endif   /* !HAVE_BE64TOH */

#if defined(WIN32)
/* Windows requires ws2_32 library for ntonl family functions.  We
   define inline functions for those function so that we don't have
   dependency on that lib. */

#  ifdef _MSC_VER
#    define STIN static __inline
#  else
#    define STIN static inline
#  endif

STIN uint32_t ngtcp2_htonl(uint32_t hostlong) {
  uint32_t res;
  unsigned char *p = (unsigned char *)&res;
  *p++ = (unsigned char)(hostlong >> 24);
  *p++ = (hostlong >> 16) & 0xffu;
  *p++ = (hostlong >> 8) & 0xffu;
  *p = hostlong & 0xffu;
  return res;
}

STIN uint16_t ngtcp2_htons(uint16_t hostshort) {
  uint16_t res;
  unsigned char *p = (unsigned char *)&res;
  *p++ = (unsigned char)(hostshort >> 8);
  *p = hostshort & 0xffu;
  return res;
}

STIN uint32_t ngtcp2_ntohl(uint32_t netlong) {
  uint32_t res;
  unsigned char *p = (unsigned char *)&netlong;
  res = (uint32_t)(*p++ << 24);
  res += (uint32_t)(*p++ << 16);
  res += (uint32_t)(*p++ << 8);
  res += *p;
  return res;
}

STIN uint16_t ngtcp2_ntohs(uint16_t netshort) {
  uint16_t res;
  unsigned char *p = (unsigned char *)&netshort;
  res = (uint16_t)(*p++ << 8);
  res += *p;
  return res;
}

#else /* !WIN32 */

#  define ngtcp2_htonl htonl
#  define ngtcp2_htons htons
#  define ngtcp2_ntohl ntohl
#  define ngtcp2_ntohs ntohs

#endif /* !WIN32 */

#endif /* NGTCP2_NET_H */
