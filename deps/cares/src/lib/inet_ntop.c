/*
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Copyright (c) 1996-1999 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ares_private.h"

#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#  include <arpa/inet.h>
#endif

#include "ares_nameser.h"
#include "ares_ipv6.h"

/*
 * WARNING: Don't even consider trying to compile this on a system where
 * sizeof(int) < 4.  sizeof(int) > 4 is fine; all the world's not a VAX.
 */

static const char *inet_ntop4(const unsigned char *src, char *dst, size_t size);
static const char *inet_ntop6(const unsigned char *src, char *dst, size_t size);

/* char *
 * inet_ntop(af, src, dst, size)
 *     convert a network format address to presentation format.
 * return:
 *     pointer to presentation format address (`dst'), or NULL (see errno).
 * note:
 *     On Windows we store the error in the thread errno, not
 *     in the winsock error code. This is to avoid losing the
 *     actual last winsock error. So use macro ERRNO to fetch the
 *     errno this function sets when returning NULL, not SOCKERRNO.
 * author:
 *     Paul Vixie, 1996.
 */
const char        *ares_inet_ntop(int af, const void *src, char *dst,
                                  ares_socklen_t size)
{
  switch (af) {
    case AF_INET:
      return inet_ntop4(src, dst, (size_t)size);
    case AF_INET6:
      return inet_ntop6(src, dst, (size_t)size);
    default:
      break;
  }
  SET_ERRNO(EAFNOSUPPORT);
  return NULL;
}

/* const char *
 * inet_ntop4(src, dst, size)
 *     format an IPv4 address
 * return:
 *     `dst' (as a const)
 * notes:
 *     (1) uses no statics
 *     (2) takes a unsigned char* not an in_addr as input
 * author:
 *     Paul Vixie, 1996.
 */
static const char *inet_ntop4(const unsigned char *src, char *dst, size_t size)
{
  static const char fmt[] = "%u.%u.%u.%u";
  char              tmp[sizeof("255.255.255.255")];

  if (size < sizeof(tmp)) {
    SET_ERRNO(ENOSPC);
    return NULL;
  }

  if ((size_t)snprintf(tmp, sizeof(tmp), fmt, src[0], src[1], src[2], src[3]) >=
      size) {
    SET_ERRNO(ENOSPC);
    return NULL;
  }
  ares_strcpy(dst, tmp, size);
  return dst;
}

/* const char *
 * inet_ntop6(src, dst, size)
 *     convert IPv6 binary address into presentation (printable) format
 * author:
 *     Paul Vixie, 1996.
 */
static const char *inet_ntop6(const unsigned char *src, char *dst, size_t size)
{
  /*
   * Note that int32_t and int16_t need only be "at least" large enough
   * to contain a value of the specified size.  On some systems, like
   * Crays, there is no such thing as an integer variable with 16 bits.
   * Keep this in mind if you think this function should have been coded
   * to use pointer overlays.  All the world's not a VAX.
   */
  char  tmp[sizeof("ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255")];
  char *tp;

  struct {
    ares_ssize_t base;
    size_t       len;
  } best, cur;

  unsigned int words[NS_IN6ADDRSZ / NS_INT16SZ];
  size_t       i;

  /*
   * Preprocess:
   *  Copy the input (bytewise) array into a wordwise array.
   *  Find the longest run of 0x00's in src[] for :: shorthanding.
   */
  memset(words, '\0', sizeof(words));
  for (i = 0; i < NS_IN6ADDRSZ; i++) {
    words[i / 2] |= (unsigned int)(src[i] << ((1 - (i % 2)) << 3));
  }
  best.base = -1;
  best.len  = 0;
  cur.base  = -1;
  cur.len   = 0;
  for (i = 0; i < (NS_IN6ADDRSZ / NS_INT16SZ); i++) {
    if (words[i] == 0) {
      if (cur.base == -1) {
        cur.base = (ares_ssize_t)i;
        cur.len = 1;
      } else {
        cur.len++;
      }
    } else {
      if (cur.base != -1) {
        if (best.base == -1 || cur.len > best.len) {
          best = cur;
        }
        cur.base = -1;
      }
    }
  }
  if (cur.base != -1) {
    if (best.base == -1 || cur.len > best.len) {
      best = cur;
    }
  }
  if (best.base != -1 && best.len < 2) {
    best.base = -1;
  }

  /*
   * Format the result.
   */
  tp = tmp;
  for (i = 0; i < (NS_IN6ADDRSZ / NS_INT16SZ); i++) {
    /* Are we inside the best run of 0x00's? */
    if (best.base != -1 && i >= (size_t)best.base && i < ((size_t)best.base + best.len)) {
      if (i == (size_t)best.base) {
        *tp++ = ':';
      }
      continue;
    }
    /* Are we following an initial run of 0x00s or any real hex? */
    if (i != 0) {
      *tp++ = ':';
    }
    /* Is this address an encapsulated IPv4? */
    if (i == 6 && best.base == 0 &&
        (best.len == 6 || (best.len == 7 && words[7] != 0x0001) ||
         (best.len == 5 && words[5] == 0xffff))) {
      if (!inet_ntop4(src + 12, tp, sizeof(tmp) - (size_t)(tp - tmp))) {
        return (NULL);
      }
      tp += ares_strlen(tp);
      break;
    }
    tp += snprintf(tp, sizeof(tmp) - (size_t)(tp - tmp), "%x", words[i]);
  }
  /* Was it a trailing run of 0x00's? */
  if (best.base != -1 &&
      ((size_t)best.base + best.len) == (NS_IN6ADDRSZ / NS_INT16SZ)) {
    *tp++ = ':';
  }
  *tp++ = '\0';

  /*
   * Check for overflow, copy, and we're done.
   */
  if ((size_t)(tp - tmp) > size) {
    SET_ERRNO(ENOSPC);
    return NULL;
  }
  ares_strcpy(dst, tmp, size);
  return dst;
}

