/* MIT License
 *
 * Copyright (c) 1998 Massachusetts Institute of Technology
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
#include "ares_inet_net_pton.h"

struct addr_query {
  /* Arguments passed to ares_gethostbyaddr() */
  ares_channel_t    *channel;
  struct ares_addr   addr;
  ares_host_callback callback;
  void              *arg;
  char       *lookups; /* duplicate memory from channel for ares_reinit() */
  const char *remaining_lookups;
  size_t      timeouts;
};

static void next_lookup(struct addr_query *aquery);
static void addr_callback(void *arg, ares_status_t status, size_t timeouts,
                          const ares_dns_record_t *dnsrec);
static void end_aquery(struct addr_query *aquery, ares_status_t status,
                       struct hostent *host);
static ares_status_t file_lookup(ares_channel_t         *channel,
                                 const struct ares_addr *addr,
                                 struct hostent        **host);

void ares_gethostbyaddr_nolock(ares_channel_t *channel, const void *addr,
                               int addrlen, int family,
                               ares_host_callback callback, void *arg)
{
  struct addr_query *aquery;

  if (family != AF_INET && family != AF_INET6) {
    callback(arg, ARES_ENOTIMP, 0, NULL);
    return;
  }

  if ((family == AF_INET && addrlen != sizeof(aquery->addr.addr.addr4)) ||
      (family == AF_INET6 && addrlen != sizeof(aquery->addr.addr.addr6))) {
    callback(arg, ARES_ENOTIMP, 0, NULL);
    return;
  }

  aquery = ares_malloc(sizeof(struct addr_query));
  if (!aquery) {
    callback(arg, ARES_ENOMEM, 0, NULL);
    return;
  }
  aquery->lookups = ares_strdup(channel->lookups);
  if (aquery->lookups == NULL) {
    /* LCOV_EXCL_START: OutOfMemory */
    ares_free(aquery);
    callback(arg, ARES_ENOMEM, 0, NULL);
    return;
    /* LCOV_EXCL_STOP */
  }
  aquery->channel = channel;
  if (family == AF_INET) {
    memcpy(&aquery->addr.addr.addr4, addr, sizeof(aquery->addr.addr.addr4));
  } else {
    memcpy(&aquery->addr.addr.addr6, addr, sizeof(aquery->addr.addr.addr6));
  }
  aquery->addr.family       = family;
  aquery->callback          = callback;
  aquery->arg               = arg;
  aquery->remaining_lookups = aquery->lookups;
  aquery->timeouts          = 0;

  next_lookup(aquery);
}

void ares_gethostbyaddr(ares_channel_t *channel, const void *addr, int addrlen,
                        int family, ares_host_callback callback, void *arg)
{
  if (channel == NULL) {
    return;
  }
  ares_channel_lock(channel);
  ares_gethostbyaddr_nolock(channel, addr, addrlen, family, callback, arg);
  ares_channel_unlock(channel);
}

static void next_lookup(struct addr_query *aquery)
{
  const char     *p;
  ares_status_t   status;
  struct hostent *host;
  char           *name;

  for (p = aquery->remaining_lookups; *p; p++) {
    switch (*p) {
      case 'b':
        name = ares_dns_addr_to_ptr(&aquery->addr);
        if (name == NULL) {
          end_aquery(aquery, ARES_ENOMEM,
                     NULL); /* LCOV_EXCL_LINE: OutOfMemory */
          return;           /* LCOV_EXCL_LINE: OutOfMemory */
        }
        aquery->remaining_lookups = p + 1;
        ares_query_nolock(aquery->channel, name, ARES_CLASS_IN,
                          ARES_REC_TYPE_PTR, addr_callback, aquery, NULL);
        ares_free(name);
        return;
      case 'f':
        status = file_lookup(aquery->channel, &aquery->addr, &host);

        /* this status check below previously checked for !ARES_ENOTFOUND,
           but we should not assume that this single error code is the one
           that can occur, as that is in fact no longer the case */
        if (status == ARES_SUCCESS) {
          end_aquery(aquery, status, host);
          return;
        }
        break;
      default:
        break;
    }
  }
  end_aquery(aquery, ARES_ENOTFOUND, NULL);
}

static void addr_callback(void *arg, ares_status_t status, size_t timeouts,
                          const ares_dns_record_t *dnsrec)
{
  struct addr_query *aquery = (struct addr_query *)arg;
  struct hostent    *host;
  size_t             addrlen;

  aquery->timeouts += timeouts;
  if (status == ARES_SUCCESS) {
    if (aquery->addr.family == AF_INET) {
      addrlen = sizeof(aquery->addr.addr.addr4);
      status  = ares_parse_ptr_reply_dnsrec(dnsrec, &aquery->addr.addr.addr4,
                                            (int)addrlen, AF_INET, &host);
    } else {
      addrlen = sizeof(aquery->addr.addr.addr6);
      status  = ares_parse_ptr_reply_dnsrec(dnsrec, &aquery->addr.addr.addr6,
                                            (int)addrlen, AF_INET6, &host);
    }
    end_aquery(aquery, status, host);
  } else if (status == ARES_EDESTRUCTION || status == ARES_ECANCELLED) {
    end_aquery(aquery, status, NULL);
  } else {
    next_lookup(aquery);
  }
}

static void end_aquery(struct addr_query *aquery, ares_status_t status,
                       struct hostent *host)
{
  aquery->callback(aquery->arg, (int)status, (int)aquery->timeouts, host);
  if (host) {
    ares_free_hostent(host);
  }
  ares_free(aquery->lookups);
  ares_free(aquery);
}

static ares_status_t file_lookup(ares_channel_t         *channel,
                                 const struct ares_addr *addr,
                                 struct hostent        **host)
{
  char                      ipaddr[INET6_ADDRSTRLEN];
  const void               *ptr = NULL;
  const ares_hosts_entry_t *entry;
  ares_status_t             status;

  if (addr->family == AF_INET) {
    ptr = &addr->addr.addr4;
  } else if (addr->family == AF_INET6) {
    ptr = &addr->addr.addr6;
  }

  if (ptr == NULL) {
    return ARES_ENOTFOUND;
  }

  if (!ares_inet_ntop(addr->family, ptr, ipaddr, sizeof(ipaddr))) {
    return ARES_ENOTFOUND;
  }

  status = ares_hosts_search_ipaddr(channel, ARES_FALSE, ipaddr, &entry);
  if (status != ARES_SUCCESS) {
    return status;
  }

  status = ares_hosts_entry_to_hostent(entry, addr->family, host);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  return ARES_SUCCESS;
}
