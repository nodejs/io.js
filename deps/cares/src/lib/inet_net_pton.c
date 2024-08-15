/*
 * Copyright (c) 2012 by Gilles Chehade <gilles@openbsd.org>
 * Copyright (c) 1996,1999 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM DISCLAIMS
 * ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL INTERNET SOFTWARE
 * CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
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
#include "ares_inet_net_pton.h"

const struct ares_in6_addr ares_in6addr_any = { { { 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                                    0, 0, 0, 0, 0, 0, 0 } } };

/*
 * static int
 * inet_net_pton_ipv4(src, dst, size)
 *      convert IPv4 network number from presentation to network format.
 *      accepts hex octets, hex strings, decimal octets, and /CIDR.
 *      "size" is in bytes and describes "dst".
 * return:
 *      number of bits, either imputed classfully or specified with /CIDR,
 *      or -1 if some failure occurred (check errno).  ENOENT means it was
 *      not an IPv4 network specification.
 * note:
 *      network byte order assumed.  this means 192.5.5.240/28 has
 *      0b11110000 in its fourth octet.
 * note:
 *      On Windows we store the error in the thread errno, not
 *      in the winsock error code. This is to avoid losing the
 *      actual last winsock error. So use macro ERRNO to fetch the
 *      errno this function sets when returning (-1), not SOCKERRNO.
 * author:
 *      Paul Vixie (ISC), June 1996
 */
static int ares_inet_net_pton_ipv4(const char *src, unsigned char *dst,
                                   size_t size)
{
  static const char    xdigits[] = "0123456789abcdef";
  static const char    digits[]  = "0123456789";
  int                  n;
  int                  ch;
  int                  tmp = 0;
  int                  dirty;
  int                  bits;
  const unsigned char *odst = dst;

  ch = *src++;
  if (ch == '0' && (src[0] == 'x' || src[0] == 'X') && ares__isascii(src[1]) &&
      ares__isxdigit(src[1])) {
    /* Hexadecimal: Eat nybble string. */
    if (!size) {
      goto emsgsize;
    }
    dirty = 0;
    src++; /* skip x or X. */
    while ((ch = *src++) != '\0' && ares__isascii(ch) && ares__isxdigit(ch)) {
      if (ares__isupper(ch)) {
        ch = ares__tolower((unsigned char)ch);
      }
      n = (int)(strchr(xdigits, ch) - xdigits);
      if (dirty == 0) {
        tmp = n;
      } else {
        tmp = (tmp << 4) | n;
      }
      if (++dirty == 2) {
        if (!size--) {
          goto emsgsize;
        }
        *dst++ = (unsigned char)tmp;
        dirty  = 0;
      }
    }
    if (dirty) { /* Odd trailing nybble? */
      if (!size--) {
        goto emsgsize;
      }
      *dst++ = (unsigned char)(tmp << 4);
    }
  } else if (ares__isascii(ch) && ares__isdigit(ch)) {
    /* Decimal: eat dotted digit string. */
    for (;;) {
      tmp = 0;
      do {
        n    = (int)(strchr(digits, ch) - digits);
        tmp *= 10;
        tmp += n;
        if (tmp > 255) {
          goto enoent;
        }
      } while ((ch = *src++) != '\0' && ares__isascii(ch) && ares__isdigit(ch));
      if (!size--) {
        goto emsgsize;
      }
      *dst++ = (unsigned char)tmp;
      if (ch == '\0' || ch == '/') {
        break;
      }
      if (ch != '.') {
        goto enoent;
      }
      ch = *src++;
      if (!ares__isascii(ch) || !ares__isdigit(ch)) {
        goto enoent;
      }
    }
  } else {
    goto enoent;
  }

  bits = -1;
  if (ch == '/' && ares__isascii(src[0]) && ares__isdigit(src[0]) &&
      dst > odst) {
    /* CIDR width specifier.  Nothing can follow it. */
    ch   = *src++; /* Skip over the /. */
    bits = 0;
    do {
      n     = (int)(strchr(digits, ch) - digits);
      bits *= 10;
      bits += n;
      if (bits > 32) {
        goto enoent;
      }
    } while ((ch = *src++) != '\0' && ares__isascii(ch) && ares__isdigit(ch));
    if (ch != '\0') {
      goto enoent;
    }
  }

  /* Firey death and destruction unless we prefetched EOS. */
  if (ch != '\0') {
    goto enoent;
  }

  /* If nothing was written to the destination, we found no address. */
  if (dst == odst) {
    goto enoent; /* LCOV_EXCL_LINE: all valid paths above increment dst */
  }
  /* If no CIDR spec was given, infer width from net class. */
  if (bits == -1) {
    if (*odst >= 240) {        /* Class E */
      bits = 32;
    } else if (*odst >= 224) { /* Class D */
      bits = 8;
    } else if (*odst >= 192) { /* Class C */
      bits = 24;
    } else if (*odst >= 128) { /* Class B */
      bits = 16;
    } else {                   /* Class A */
      bits = 8;
    }
    /* If imputed mask is narrower than specified octets, widen. */
    if (bits < ((dst - odst) * 8)) {
      bits = (int)(dst - odst) * 8;
    }
    /*
     * If there are no additional bits specified for a class D
     * address adjust bits to 4.
     */
    if (bits == 8 && *odst == 224) {
      bits = 4;
    }
  }
  /* Extend network to cover the actual mask. */
  while (bits > ((dst - odst) * 8)) {
    if (!size--) {
      goto emsgsize;
    }
    *dst++ = '\0';
  }
  return bits;

enoent:
  SET_ERRNO(ENOENT);
  return -1;

emsgsize:
  SET_ERRNO(EMSGSIZE);
  return -1;
}

static int getbits(const char *src, size_t *bitsp)
{
  static const char digits[] = "0123456789";
  size_t            n;
  size_t            val;
  char              ch;

  val = 0;
  n   = 0;
  while ((ch = *src++) != '\0') {
    const char *pch;

    pch = strchr(digits, ch);
    if (pch != NULL) {
      if (n++ != 0 && val == 0) { /* no leading zeros */
        return 0;
      }
      val *= 10;
      val += (size_t)(pch - digits);
      if (val > 128) { /* range */
        return 0;
      }
      continue;
    }
    return 0;
  }
  if (n == 0) {
    return 0;
  }
  *bitsp = val;
  return 1;
}

static int ares_inet_pton6(const char *src, unsigned char *dst)
{
  static const char xdigits_l[] = "0123456789abcdef";
  static const char xdigits_u[] = "0123456789ABCDEF";
  unsigned char     tmp[NS_IN6ADDRSZ];
  unsigned char    *tp;
  unsigned char    *endp;
  unsigned char    *colonp;
  const char       *xdigits;
  const char       *curtok;
  int               ch;
  int               saw_xdigit;
  int               count_xdigit;
  unsigned int      val;

  memset((tp = tmp), '\0', NS_IN6ADDRSZ);
  endp   = tp + NS_IN6ADDRSZ;
  colonp = NULL;
  /* Leading :: requires some special handling. */
  if (*src == ':') {
    if (*++src != ':') {
      goto enoent;
    }
  }
  curtok     = src;
  saw_xdigit = count_xdigit = 0;
  val                       = 0;
  while ((ch = *src++) != '\0') {
    const char *pch;

    if ((pch = strchr((xdigits = xdigits_l), ch)) == NULL) {
      pch = strchr((xdigits = xdigits_u), ch);
    }
    if (pch != NULL) {
      if (count_xdigit >= 4) {
        goto enoent;
      }
      val <<= 4;
      val  |= (unsigned int)(pch - xdigits);
      if (val > 0xffff) {
        goto enoent;
      }
      saw_xdigit = 1;
      count_xdigit++;
      continue;
    }
    if (ch == ':') {
      curtok = src;
      if (!saw_xdigit) {
        if (colonp) {
          goto enoent;
        }
        colonp = tp;
        continue;
      } else if (*src == '\0') {
        goto enoent;
      }
      if (tp + NS_INT16SZ > endp) {
        goto enoent;
      }
      *tp++        = (unsigned char)(val >> 8) & 0xff;
      *tp++        = (unsigned char)val & 0xff;
      saw_xdigit   = 0;
      count_xdigit = 0;
      val          = 0;
      continue;
    }
    if (ch == '.' && ((tp + NS_INADDRSZ) <= endp) &&
        ares_inet_net_pton_ipv4(curtok, tp, NS_INADDRSZ) > 0) {
      tp         += NS_INADDRSZ;
      saw_xdigit  = 0;
      break; /* '\0' was seen by inet_pton4(). */
    }
    goto enoent;
  }
  if (saw_xdigit) {
    if (tp + NS_INT16SZ > endp) {
      goto enoent;
    }
    *tp++ = (unsigned char)(val >> 8) & 0xff;
    *tp++ = (unsigned char)val & 0xff;
  }
  if (colonp != NULL) {
    /*
     * Since some memmove()'s erroneously fail to handle
     * overlapping regions, we'll do the shift by hand.
     */
    const int n = (int)(tp - colonp);
    int       i;

    if (tp == endp) {
      goto enoent;
    }
    for (i = 1; i <= n; i++) {
      endp[-i]      = colonp[n - i];
      colonp[n - i] = 0;
    }
    tp = endp;
  }
  if (tp != endp) {
    goto enoent;
  }

  memcpy(dst, tmp, NS_IN6ADDRSZ);
  return 1;

enoent:
  SET_ERRNO(ENOENT);
  return -1;
}

static int ares_inet_net_pton_ipv6(const char *src, unsigned char *dst,
                                   size_t size)
{
  struct ares_in6_addr in6;
  int                  ret;
  size_t               bits;
  size_t               bytes;
  char                 buf[INET6_ADDRSTRLEN + sizeof("/128")];
  char                *sep;

  if (ares_strlen(src) >= sizeof buf) {
    SET_ERRNO(EMSGSIZE);
    return -1;
  }
  ares_strcpy(buf, src, sizeof buf);

  sep = strchr(buf, '/');
  if (sep != NULL) {
    *sep++ = '\0';
  }

  ret = ares_inet_pton6(buf, (unsigned char *)&in6);
  if (ret != 1) {
    return -1;
  }

  if (sep == NULL) {
    bits = 128;
  } else {
    if (!getbits(sep, &bits)) {
      SET_ERRNO(ENOENT);
      return -1;
    }
  }

  bytes = (bits + 7) / 8;
  if (bytes > size) {
    SET_ERRNO(EMSGSIZE);
    return -1;
  }
  memcpy(dst, &in6, bytes);
  return (int)bits;
}

/*
 * int
 * inet_net_pton(af, src, dst, size)
 *      convert network number from presentation to network format.
 *      accepts hex octets, hex strings, decimal octets, and /CIDR.
 *      "size" is in bytes and describes "dst".
 * return:
 *      number of bits, either imputed classfully or specified with /CIDR,
 *      or -1 if some failure occurred (check errno).  ENOENT means it was
 *      not a valid network specification.
 * note:
 *      On Windows we store the error in the thread errno, not
 *      in the winsock error code. This is to avoid losing the
 *      actual last winsock error. So use macro ERRNO to fetch the
 *      errno this function sets when returning (-1), not SOCKERRNO.
 * author:
 *      Paul Vixie (ISC), June 1996
 */
int ares_inet_net_pton(int af, const char *src, void *dst, size_t size)
{
  switch (af) {
    case AF_INET:
      return ares_inet_net_pton_ipv4(src, dst, size);
    case AF_INET6:
      return ares_inet_net_pton_ipv6(src, dst, size);
    default:
      SET_ERRNO(EAFNOSUPPORT);
      return -1;
  }
}

int ares_inet_pton(int af, const char *src, void *dst)
{
  int    result;
  size_t size;

  if (af == AF_INET) {
    size = sizeof(struct in_addr);
  } else if (af == AF_INET6) {
    size = sizeof(struct ares_in6_addr);
  } else {
    SET_ERRNO(EAFNOSUPPORT);
    return -1;
  }
  result = ares_inet_net_pton(af, src, dst, size);
  if (result == -1 && ERRNO == ENOENT) {
    return 0;
  }
  return (result > -1) ? 1 : -1;
}
