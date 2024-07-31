/* MIT License
 *
 * Copyright (c) 1998, 2011, 2013 Massachusetts Institute of Technology
 * Copyright (c) The c-ares project and its contributors
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

#include "ares_private.h"

#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif
#ifdef HAVE_NETDB_H
#  include <netdb.h>
#endif
#ifdef HAVE_ARPA_INET_H
#  include <arpa/inet.h>
#endif

#include "ares_nameser.h"

#ifdef HAVE_STRINGS_H
#  include <strings.h>
#endif

#include "ares_inet_net_pton.h"
#include "ares_platform.h"

static void   sort_addresses(const struct hostent  *host,
                             const struct apattern *sortlist, size_t nsort);
static void   sort6_addresses(const struct hostent  *host,
                              const struct apattern *sortlist, size_t nsort);
static size_t get_address_index(const struct in_addr  *addr,
                                const struct apattern *sortlist, size_t nsort);
static size_t get6_address_index(const struct ares_in6_addr *addr,
                                 const struct apattern *sortlist, size_t nsort);

struct host_query {
  ares_host_callback callback;
  void              *arg;
  ares_channel_t    *channel;
};

static void ares_gethostbyname_callback(void *arg, int status, int timeouts,
                                        struct ares_addrinfo *result)
{
  struct hostent    *hostent  = NULL;
  struct host_query *ghbn_arg = arg;

  if (status == ARES_SUCCESS) {
    status = (int)ares__addrinfo2hostent(result, AF_UNSPEC, &hostent);
  }

  /* addrinfo2hostent will only return ENODATA if there are no addresses _and_
   * no cname/aliases.  However, gethostbyname will return ENODATA even if there
   * is cname/alias data */
  if (status == ARES_SUCCESS && hostent &&
      (!hostent->h_addr_list || !hostent->h_addr_list[0])) {
    status = ARES_ENODATA;
  }

  if (status == ARES_SUCCESS && ghbn_arg->channel->nsort && hostent) {
    if (hostent->h_addrtype == AF_INET6) {
      sort6_addresses(hostent, ghbn_arg->channel->sortlist,
                      ghbn_arg->channel->nsort);
    }
    if (hostent->h_addrtype == AF_INET) {
      sort_addresses(hostent, ghbn_arg->channel->sortlist,
                     ghbn_arg->channel->nsort);
    }
  }

  ghbn_arg->callback(ghbn_arg->arg, status, timeouts, hostent);

  ares_freeaddrinfo(result);
  ares_free(ghbn_arg);
  ares_free_hostent(hostent);
}

void ares_gethostbyname(ares_channel_t *channel, const char *name, int family,
                        ares_host_callback callback, void *arg)
{
  struct ares_addrinfo_hints hints;
  struct host_query         *ghbn_arg;

  if (!callback) {
    return;
  }

  memset(&hints, 0, sizeof(hints));
  hints.ai_flags  = ARES_AI_CANONNAME;
  hints.ai_family = family;

  ghbn_arg = ares_malloc(sizeof(*ghbn_arg));
  if (!ghbn_arg) {
    callback(arg, ARES_ENOMEM, 0, NULL);
    return;
  }

  ghbn_arg->callback = callback;
  ghbn_arg->arg      = arg;
  ghbn_arg->channel  = channel;

  /* NOTE: ares_getaddrinfo() locks the channel, we don't use the channel
   *       outside so no need to lock */
  ares_getaddrinfo(channel, name, NULL, &hints, ares_gethostbyname_callback,
                   ghbn_arg);
}

static void sort_addresses(const struct hostent  *host,
                           const struct apattern *sortlist, size_t nsort)
{
  struct in_addr a1;
  struct in_addr a2;
  int            i1;
  int            i2;
  size_t         ind1;
  size_t         ind2;

  /* This is a simple insertion sort, not optimized at all.  i1 walks
   * through the address list, with the loop invariant that everything
   * to the left of i1 is sorted.  In the loop body, the value at i1 is moved
   * back through the list (via i2) until it is in sorted order.
   */
  for (i1 = 0; host->h_addr_list[i1]; i1++) {
    memcpy(&a1, host->h_addr_list[i1], sizeof(struct in_addr));
    ind1 = get_address_index(&a1, sortlist, nsort);
    for (i2 = i1 - 1; i2 >= 0; i2--) {
      memcpy(&a2, host->h_addr_list[i2], sizeof(struct in_addr));
      ind2 = get_address_index(&a2, sortlist, nsort);
      if (ind2 <= ind1) {
        break;
      }
      memcpy(host->h_addr_list[i2 + 1], &a2, sizeof(struct in_addr));
    }
    memcpy(host->h_addr_list[i2 + 1], &a1, sizeof(struct in_addr));
  }
}

/* Find the first entry in sortlist which matches addr.  Return nsort
 * if none of them match.
 */
static size_t get_address_index(const struct in_addr  *addr,
                                const struct apattern *sortlist, size_t nsort)
{
  size_t           i;
  struct ares_addr aaddr;

  memset(&aaddr, 0, sizeof(aaddr));
  aaddr.family = AF_INET;
  memcpy(&aaddr.addr.addr4, addr, 4);

  for (i = 0; i < nsort; i++) {
    if (sortlist[i].addr.family != AF_INET) {
      continue;
    }

    if (ares__subnet_match(&aaddr, &sortlist[i].addr, sortlist[i].mask)) {
      break;
    }
  }

  return i;
}

static void sort6_addresses(const struct hostent  *host,
                            const struct apattern *sortlist, size_t nsort)
{
  struct ares_in6_addr a1;
  struct ares_in6_addr a2;
  int                  i1;
  int                  i2;
  size_t               ind1;
  size_t               ind2;

  /* This is a simple insertion sort, not optimized at all.  i1 walks
   * through the address list, with the loop invariant that everything
   * to the left of i1 is sorted.  In the loop body, the value at i1 is moved
   * back through the list (via i2) until it is in sorted order.
   */
  for (i1 = 0; host->h_addr_list[i1]; i1++) {
    memcpy(&a1, host->h_addr_list[i1], sizeof(struct ares_in6_addr));
    ind1 = get6_address_index(&a1, sortlist, nsort);
    for (i2 = i1 - 1; i2 >= 0; i2--) {
      memcpy(&a2, host->h_addr_list[i2], sizeof(struct ares_in6_addr));
      ind2 = get6_address_index(&a2, sortlist, nsort);
      if (ind2 <= ind1) {
        break;
      }
      memcpy(host->h_addr_list[i2 + 1], &a2, sizeof(struct ares_in6_addr));
    }
    memcpy(host->h_addr_list[i2 + 1], &a1, sizeof(struct ares_in6_addr));
  }
}

/* Find the first entry in sortlist which matches addr.  Return nsort
 * if none of them match.
 */
static size_t get6_address_index(const struct ares_in6_addr *addr,
                                 const struct apattern *sortlist, size_t nsort)
{
  size_t           i;
  struct ares_addr aaddr;

  memset(&aaddr, 0, sizeof(aaddr));
  aaddr.family = AF_INET6;
  memcpy(&aaddr.addr.addr6, addr, 16);

  for (i = 0; i < nsort; i++) {
    if (sortlist[i].addr.family != AF_INET6) {
      continue;
    }

    if (ares__subnet_match(&aaddr, &sortlist[i].addr, sortlist[i].mask)) {
      break;
    }
  }
  return i;
}

static ares_status_t ares__hostent_localhost(const char *name, int family,
                                             struct hostent **host_out)
{
  ares_status_t              status;
  struct ares_addrinfo      *ai = NULL;
  struct ares_addrinfo_hints hints;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = family;

  ai = ares_malloc_zero(sizeof(*ai));
  if (ai == NULL) {
    status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
    goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
  }

  status = ares__addrinfo_localhost(name, 0, &hints, ai);
  if (status != ARES_SUCCESS) {
    goto done; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  status = ares__addrinfo2hostent(ai, family, host_out);
  if (status != ARES_SUCCESS) {
    goto done; /* LCOV_EXCL_LINE: OutOfMemory */
  }

done:
  ares_freeaddrinfo(ai);
  return status;
}

/* I really have no idea why this is exposed as a public function, but since
 * it is, we can't kill this legacy function. */
static ares_status_t ares_gethostbyname_file_int(ares_channel_t *channel,
                                                 const char *name, int family,
                                                 struct hostent **host)
{
  const ares_hosts_entry_t *entry;
  ares_status_t             status;

  /* We only take the channel to ensure that ares_init() been called. */
  if (channel == NULL || name == NULL || host == NULL) {
    /* Anything will do, really.  This seems fine, and is consistent with
       other error cases. */
    if (host != NULL) {
      *host = NULL;
    }
    return ARES_ENOTFOUND;
  }

  /* Per RFC 7686, reject queries for ".onion" domain names with NXDOMAIN. */
  if (ares__is_onion_domain(name)) {
    return ARES_ENOTFOUND;
  }

  status = ares__hosts_search_host(channel, ARES_FALSE, name, &entry);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  status = ares__hosts_entry_to_hostent(entry, family, host);
  if (status != ARES_SUCCESS) {
    goto done; /* LCOV_EXCL_LINE: OutOfMemory */
  }

done:
  /* RFC6761 section 6.3 #3 states that "Name resolution APIs and libraries
   * SHOULD recognize localhost names as special and SHOULD always return the
   * IP loopback address for address queries".
   * We will also ignore ALL errors when trying to resolve localhost, such
   * as permissions errors reading /etc/hosts or a malformed /etc/hosts */
  if (status != ARES_SUCCESS && status != ARES_ENOMEM &&
      ares__is_localhost(name)) {
    return ares__hostent_localhost(name, family, host);
  }

  return status;
}

int ares_gethostbyname_file(ares_channel_t *channel, const char *name,
                            int family, struct hostent **host)
{
  ares_status_t status;
  if (channel == NULL) {
    return ARES_ENOTFOUND;
  }

  ares__channel_lock(channel);
  status = ares_gethostbyname_file_int(channel, name, family, host);
  ares__channel_unlock(channel);
  return (int)status;
}
