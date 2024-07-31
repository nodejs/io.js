/*
 * Original file name getaddrinfo.c
 * Lifted from the 'Android Bionic' project with the BSD license.
 */

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
 * Copyright (C) 2018 The Android Open Source Project
 * Copyright (C) 2019 Andrew Selivanov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ares_private.h"

#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif
#ifdef HAVE_NETDB_H
#  include <netdb.h>
#endif
#ifdef HAVE_STRINGS_H
#  include <strings.h>
#endif

#include <assert.h>
#include <limits.h>


struct addrinfo_sort_elem {
  struct ares_addrinfo_node *ai;
  ares_bool_t                has_src_addr;
  ares_sockaddr              src_addr;
  size_t                     original_order;
};

#define ARES_IPV6_ADDR_MC_SCOPE(a) ((a)->s6_addr[1] & 0x0f)

#define ARES_IPV6_ADDR_SCOPE_NODELOCAL    0x01
#define ARES_IPV6_ADDR_SCOPE_INTFACELOCAL 0x01
#define ARES_IPV6_ADDR_SCOPE_LINKLOCAL    0x02
#define ARES_IPV6_ADDR_SCOPE_SITELOCAL    0x05
#define ARES_IPV6_ADDR_SCOPE_ORGLOCAL     0x08
#define ARES_IPV6_ADDR_SCOPE_GLOBAL       0x0e

#define ARES_IN_LOOPBACK(a) \
  ((((long unsigned int)(a)) & 0xff000000) == 0x7f000000)

/* RFC 4193. */
#define ARES_IN6_IS_ADDR_ULA(a) (((a)->s6_addr[0] & 0xfe) == 0xfc)

/* These macros are modelled after the ones in <netinet/in6.h>. */
/* RFC 4380, section 2.6 */
#define ARES_IN6_IS_ADDR_TEREDO(a)                             \
  ((*(const unsigned int *)(const void *)(&(a)->s6_addr[0]) == \
    ntohl(0x20010000)))
/* RFC 3056, section 2. */
#define ARES_IN6_IS_ADDR_6TO4(a) \
  (((a)->s6_addr[0] == 0x20) && ((a)->s6_addr[1] == 0x02))
/* 6bone testing address area (3ffe::/16), deprecated in RFC 3701. */
#define ARES_IN6_IS_ADDR_6BONE(a) \
  (((a)->s6_addr[0] == 0x3f) && ((a)->s6_addr[1] == 0xfe))

static int get_scope(const struct sockaddr *addr)
{
  if (addr->sa_family == AF_INET6) {
    const struct sockaddr_in6 *addr6 =
      CARES_INADDR_CAST(const struct sockaddr_in6 *, addr);
    if (IN6_IS_ADDR_MULTICAST(&addr6->sin6_addr)) {
      return ARES_IPV6_ADDR_MC_SCOPE(&addr6->sin6_addr);
    } else if (IN6_IS_ADDR_LOOPBACK(&addr6->sin6_addr) ||
               IN6_IS_ADDR_LINKLOCAL(&addr6->sin6_addr)) {
      /*
       * RFC 4291 section 2.5.3 says loopback is to be treated as having
       * link-local scope.
       */
      return ARES_IPV6_ADDR_SCOPE_LINKLOCAL;
    } else if (IN6_IS_ADDR_SITELOCAL(&addr6->sin6_addr)) {
      return ARES_IPV6_ADDR_SCOPE_SITELOCAL;
    } else {
      return ARES_IPV6_ADDR_SCOPE_GLOBAL;
    }
  } else if (addr->sa_family == AF_INET) {
    const struct sockaddr_in *addr4 =
      CARES_INADDR_CAST(const struct sockaddr_in *, addr);
    unsigned long int na = ntohl(addr4->sin_addr.s_addr);
    if (ARES_IN_LOOPBACK(na) ||          /* 127.0.0.0/8 */
        (na & 0xffff0000) == 0xa9fe0000) /* 169.254.0.0/16 */
    {
      return ARES_IPV6_ADDR_SCOPE_LINKLOCAL;
    } else {
      /*
       * RFC 6724 section 3.2. Other IPv4 addresses, including private
       * addresses and shared addresses (100.64.0.0/10), are assigned global
       * scope.
       */
      return ARES_IPV6_ADDR_SCOPE_GLOBAL;
    }
  } else {
    /*
     * This should never happen.
     * Return a scope with low priority as a last resort.
     */
    return ARES_IPV6_ADDR_SCOPE_NODELOCAL;
  }
}

static int get_label(const struct sockaddr *addr)
{
  if (addr->sa_family == AF_INET) {
    return 4;
  } else if (addr->sa_family == AF_INET6) {
    const struct sockaddr_in6 *addr6 =
      CARES_INADDR_CAST(const struct sockaddr_in6 *, addr);
    if (IN6_IS_ADDR_LOOPBACK(&addr6->sin6_addr)) {
      return 0;
    } else if (IN6_IS_ADDR_V4MAPPED(&addr6->sin6_addr)) {
      return 4;
    } else if (ARES_IN6_IS_ADDR_6TO4(&addr6->sin6_addr)) {
      return 2;
    } else if (ARES_IN6_IS_ADDR_TEREDO(&addr6->sin6_addr)) {
      return 5;
    } else if (ARES_IN6_IS_ADDR_ULA(&addr6->sin6_addr)) {
      return 13;
    } else if (IN6_IS_ADDR_V4COMPAT(&addr6->sin6_addr)) {
      return 3;
    } else if (IN6_IS_ADDR_SITELOCAL(&addr6->sin6_addr)) {
      return 11;
    } else if (ARES_IN6_IS_ADDR_6BONE(&addr6->sin6_addr)) {
      return 12;
    } else {
      /* All other IPv6 addresses, including global unicast addresses. */
      return 1;
    }
  } else {
    /*
     * This should never happen.
     * Return a semi-random label as a last resort.
     */
    return 1;
  }
}

/*
 * Get the precedence for a given IPv4/IPv6 address.
 * RFC 6724, section 2.1.
 */
static int get_precedence(const struct sockaddr *addr)
{
  if (addr->sa_family == AF_INET) {
    return 35;
  } else if (addr->sa_family == AF_INET6) {
    const struct sockaddr_in6 *addr6 =
      CARES_INADDR_CAST(const struct sockaddr_in6 *, addr);
    if (IN6_IS_ADDR_LOOPBACK(&addr6->sin6_addr)) {
      return 50;
    } else if (IN6_IS_ADDR_V4MAPPED(&addr6->sin6_addr)) {
      return 35;
    } else if (ARES_IN6_IS_ADDR_6TO4(&addr6->sin6_addr)) {
      return 30;
    } else if (ARES_IN6_IS_ADDR_TEREDO(&addr6->sin6_addr)) {
      return 5;
    } else if (ARES_IN6_IS_ADDR_ULA(&addr6->sin6_addr)) {
      return 3;
    } else if (IN6_IS_ADDR_V4COMPAT(&addr6->sin6_addr) ||
               IN6_IS_ADDR_SITELOCAL(&addr6->sin6_addr) ||
               ARES_IN6_IS_ADDR_6BONE(&addr6->sin6_addr)) {
      return 1;
    } else {
      /* All other IPv6 addresses, including global unicast addresses. */
      return 40;
    }
  } else {
    return 1;
  }
}

/*
 * Find number of matching initial bits between the two addresses a1 and a2.
 */
static size_t common_prefix_len(const struct in6_addr *a1,
                                const struct in6_addr *a2)
{
  const unsigned char *p1 = (const unsigned char *)a1;
  const unsigned char *p2 = (const unsigned char *)a2;
  size_t               i;
  for (i = 0; i < sizeof(*a1); ++i) {
    unsigned char x;
    size_t        j;
    if (p1[i] == p2[i]) {
      continue;
    }
    x = p1[i] ^ p2[i];
    for (j = 0; j < CHAR_BIT; ++j) {
      if (x & (1 << (CHAR_BIT - 1))) {
        return i * CHAR_BIT + j;
      }
      x <<= 1;
    }
  }
  return sizeof(*a1) * CHAR_BIT;
}

/*
 * Compare two source/destination address pairs.
 * RFC 6724, section 6.
 */
static int rfc6724_compare(const void *ptr1, const void *ptr2)
{
  const struct addrinfo_sort_elem *a1 = (const struct addrinfo_sort_elem *)ptr1;
  const struct addrinfo_sort_elem *a2 = (const struct addrinfo_sort_elem *)ptr2;
  int                              scope_src1;
  int                              scope_dst1;
  int                              scope_match1;
  int                              scope_src2;
  int                              scope_dst2;
  int                              scope_match2;
  int                              label_src1;
  int                              label_dst1;
  int                              label_match1;
  int                              label_src2;
  int                              label_dst2;
  int                              label_match2;
  int                              precedence1;
  int                              precedence2;
  size_t                           prefixlen1;
  size_t                           prefixlen2;

  /* Rule 1: Avoid unusable destinations. */
  if (a1->has_src_addr != a2->has_src_addr) {
    return ((int)a2->has_src_addr) - ((int)a1->has_src_addr);
  }

  /* Rule 2: Prefer matching scope. */
  scope_src1 = ARES_IPV6_ADDR_SCOPE_NODELOCAL;
  if (a1->has_src_addr) {
    scope_src1 = get_scope(&a1->src_addr.sa);
  }
  scope_dst1   = get_scope(a1->ai->ai_addr);
  scope_match1 = (scope_src1 == scope_dst1);

  scope_src2 = ARES_IPV6_ADDR_SCOPE_NODELOCAL;
  if (a2->has_src_addr) {
    scope_src2 = get_scope(&a2->src_addr.sa);
  }
  scope_dst2   = get_scope(a2->ai->ai_addr);
  scope_match2 = (scope_src2 == scope_dst2);

  if (scope_match1 != scope_match2) {
    return scope_match2 - scope_match1;
  }

  /* Rule 3: Avoid deprecated addresses.  */

  /* Rule 4: Prefer home addresses.  */

  /* Rule 5: Prefer matching label. */
  label_src1 = 1;
  if (a1->has_src_addr) {
    label_src1 = get_label(&a1->src_addr.sa);
  }
  label_dst1   = get_label(a1->ai->ai_addr);
  label_match1 = (label_src1 == label_dst1);

  label_src2 = 1;
  if (a2->has_src_addr) {
    label_src2 = get_label(&a2->src_addr.sa);
  }
  label_dst2   = get_label(a2->ai->ai_addr);
  label_match2 = (label_src2 == label_dst2);

  if (label_match1 != label_match2) {
    return label_match2 - label_match1;
  }

  /* Rule 6: Prefer higher precedence. */
  precedence1 = get_precedence(a1->ai->ai_addr);
  precedence2 = get_precedence(a2->ai->ai_addr);
  if (precedence1 != precedence2) {
    return precedence2 - precedence1;
  }

  /* Rule 7: Prefer native transport.  */

  /* Rule 8: Prefer smaller scope. */
  if (scope_dst1 != scope_dst2) {
    return scope_dst1 - scope_dst2;
  }

  /* Rule 9: Use longest matching prefix. */
  if (a1->has_src_addr && a1->ai->ai_addr->sa_family == AF_INET6 &&
      a2->has_src_addr && a2->ai->ai_addr->sa_family == AF_INET6) {
    const struct sockaddr_in6 *a1_src = &a1->src_addr.sa6;
    const struct sockaddr_in6 *a1_dst =
      CARES_INADDR_CAST(const struct sockaddr_in6 *, a1->ai->ai_addr);
    const struct sockaddr_in6 *a2_src = &a2->src_addr.sa6;
    const struct sockaddr_in6 *a2_dst =
      CARES_INADDR_CAST(const struct sockaddr_in6 *, a2->ai->ai_addr);
    prefixlen1 = common_prefix_len(&a1_src->sin6_addr, &a1_dst->sin6_addr);
    prefixlen2 = common_prefix_len(&a2_src->sin6_addr, &a2_dst->sin6_addr);
    if (prefixlen1 != prefixlen2) {
      return (int)prefixlen2 - (int)prefixlen1;
    }
  }

  /*
   * Rule 10: Leave the order unchanged.
   * We need this since qsort() is not necessarily stable.
   */
  return ((int)a1->original_order) - ((int)a2->original_order);
}

/*
 * Find the source address that will be used if trying to connect to the given
 * address.
 *
 * Returns 1 if a source address was found, 0 if the address is unreachable
 * and -1 if a fatal error occurred. If 0 or 1, the contents of src_addr are
 * undefined.
 */
static int find_src_addr(ares_channel_t *channel, const struct sockaddr *addr,
                         struct sockaddr *src_addr)
{
  ares_socket_t  sock;
  int            ret;
  ares_socklen_t len;

  switch (addr->sa_family) {
    case AF_INET:
      len = sizeof(struct sockaddr_in);
      break;
    case AF_INET6:
      len = sizeof(struct sockaddr_in6);
      break;
    default:
      /* No known usable source address for non-INET families. */
      return 0;
  }

  sock = ares__open_socket(channel, addr->sa_family, SOCK_DGRAM, IPPROTO_UDP);
  if (sock == ARES_SOCKET_BAD) {
    if (errno == EAFNOSUPPORT) {
      return 0;
    } else {
      return -1;
    }
  }

  do {
    ret = ares__connect_socket(channel, sock, addr, len);
  } while (ret == -1 && errno == EINTR);

  if (ret == -1) {
    ares__close_socket(channel, sock);
    return 0;
  }

  if (getsockname(sock, src_addr, &len) != 0) {
    ares__close_socket(channel, sock);
    return -1;
  }
  ares__close_socket(channel, sock);
  return 1;
}

/*
 * Sort the linked list starting at sentinel->ai_next in RFC6724 order.
 * Will leave the list unchanged if an error occurs.
 */
ares_status_t ares__sortaddrinfo(ares_channel_t            *channel,
                                 struct ares_addrinfo_node *list_sentinel)
{
  struct ares_addrinfo_node *cur;
  size_t                     nelem = 0;
  size_t                     i;
  int                        has_src_addr;
  struct addrinfo_sort_elem *elems;

  cur = list_sentinel->ai_next;
  while (cur) {
    ++nelem;
    cur = cur->ai_next;
  }

  if (!nelem) {
    return ARES_ENODATA;
  }

  elems = (struct addrinfo_sort_elem *)ares_malloc(
    nelem * sizeof(struct addrinfo_sort_elem));
  if (!elems) {
    return ARES_ENOMEM;
  }

  /*
   * Convert the linked list to an array that also contains the candidate
   * source address for each destination address.
   */
  for (i = 0, cur = list_sentinel->ai_next; i < nelem;
       ++i, cur   = cur->ai_next) {
    assert(cur != NULL);
    elems[i].ai             = cur;
    elems[i].original_order = i;
    has_src_addr = find_src_addr(channel, cur->ai_addr, &elems[i].src_addr.sa);
    if (has_src_addr == -1) {
      ares_free(elems);
      return ARES_ENOTFOUND;
    }
    elems[i].has_src_addr = (has_src_addr == 1) ? ARES_TRUE : ARES_FALSE;
  }

  /* Sort the addresses, and rearrange the linked list so it matches the sorted
   * order. */
  qsort((void *)elems, nelem, sizeof(struct addrinfo_sort_elem),
        rfc6724_compare);

  list_sentinel->ai_next = elems[0].ai;
  for (i = 0; i < nelem - 1; ++i) {
    elems[i].ai->ai_next = elems[i + 1].ai;
  }
  elems[nelem - 1].ai->ai_next = NULL;

  ares_free(elems);
  return ARES_SUCCESS;
}
