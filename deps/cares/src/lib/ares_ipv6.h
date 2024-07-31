/* MIT License
 *
 * Copyright (c) 2005 Dominick Meglio
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef ARES_IPV6_H
#define ARES_IPV6_H

#ifdef HAVE_NETINET6_IN6_H
#  include <netinet6/in6.h>
#endif

#if defined(USE_WINSOCK)
#  if defined(HAVE_IPHLPAPI_H)
#    include <iphlpapi.h>
#  endif
#  if defined(HAVE_NETIOAPI_H)
#    include <netioapi.h>
#  endif
#endif

#ifndef HAVE_PF_INET6
#  define PF_INET6 AF_INET6
#endif

#ifndef HAVE_STRUCT_SOCKADDR_IN6
struct sockaddr_in6 {
  unsigned short       sin6_family;
  unsigned short       sin6_port;
  unsigned long        sin6_flowinfo;
  struct ares_in6_addr sin6_addr;
  unsigned int         sin6_scope_id;
};
#endif

typedef union {
  struct sockaddr     sa;
  struct sockaddr_in  sa4;
  struct sockaddr_in6 sa6;
} ares_sockaddr;

#ifndef HAVE_STRUCT_ADDRINFO
struct addrinfo {
  int              ai_flags;
  int              ai_family;
  int              ai_socktype;
  int              ai_protocol;
  ares_socklen_t   ai_addrlen; /* Follow rfc3493 struct addrinfo */
  char            *ai_canonname;
  struct sockaddr *ai_addr;
  struct addrinfo *ai_next;
};
#endif

#ifndef NS_IN6ADDRSZ
#  ifndef HAVE_STRUCT_IN6_ADDR
/* We cannot have it set to zero, so we pick a fixed value here */
#    define NS_IN6ADDRSZ 16
#  else
#    define NS_IN6ADDRSZ sizeof(struct in6_addr)
#  endif
#endif

#ifndef NS_INADDRSZ
#  define NS_INADDRSZ sizeof(struct in_addr)
#endif

#ifndef NS_INT16SZ
#  define NS_INT16SZ 2
#endif

#ifndef IF_NAMESIZE
#  ifdef IFNAMSIZ
#    define IF_NAMESIZE IFNAMSIZ
#  else
#    define IF_NAMESIZE 256
#  endif
#endif

/* Defined in inet_net_pton.c for no particular reason. */
extern const struct ares_in6_addr ares_in6addr_any; /* :: */


#endif /* ARES_IPV6_H */
