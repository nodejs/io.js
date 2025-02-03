/* MIT License
 *
 * Copyright (c) 2023 Brad House
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

int ares_parse_ns_reply(const unsigned char *abuf, int alen_int,
                        struct hostent **host)
{
  ares_status_t      status;
  size_t             alen;
  size_t             nscount  = 0;
  struct hostent    *hostent  = NULL;
  const char        *hostname = NULL;
  ares_dns_record_t *dnsrec   = NULL;
  size_t             i;
  size_t             ancount;

  *host = NULL;

  if (alen_int < 0) {
    return ARES_EBADRESP;
  }

  alen = (size_t)alen_int;

  status = ares_dns_parse(abuf, alen, 0, &dnsrec);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  ancount = ares_dns_record_rr_cnt(dnsrec, ARES_SECTION_ANSWER);
  if (ancount == 0) {
    status = ARES_ENODATA;
    goto done;
  }

  /* Response structure */
  hostent = ares_malloc(sizeof(*hostent));
  if (hostent == NULL) {
    status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
    goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
  }

  memset(hostent, 0, sizeof(*hostent));

  hostent->h_addr_list = ares_malloc(sizeof(*hostent->h_addr_list));
  if (hostent->h_addr_list == NULL) {
    status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
    goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
  }
  hostent->h_addr_list[0] = NULL;
  hostent->h_addrtype     = AF_INET;
  hostent->h_length       = sizeof(struct in_addr);

  /* Fill in hostname */
  status = ares_dns_record_query_get(dnsrec, 0, &hostname, NULL, NULL);
  if (status != ARES_SUCCESS) {
    goto done; /* LCOV_EXCL_LINE: DefensiveCoding */
  }
  hostent->h_name = ares_strdup(hostname);
  if (hostent->h_name == NULL) {
    status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
    goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* Preallocate the maximum number + 1 */
  hostent->h_aliases = ares_malloc((ancount + 1) * sizeof(*hostent->h_aliases));
  if (hostent->h_aliases == NULL) {
    status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
    goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
  }
  memset(hostent->h_aliases, 0, (ancount + 1) * sizeof(*hostent->h_aliases));

  for (i = 0; i < ancount; i++) {
    const ares_dns_rr_t *rr =
      ares_dns_record_rr_get(dnsrec, ARES_SECTION_ANSWER, i);

    if (rr == NULL) {
      /* Shouldn't be possible */
      status = ARES_EBADRESP; /* LCOV_EXCL_LINE: DefensiveCoding */
      goto done;              /* LCOV_EXCL_LINE: DefensiveCoding */
    }

    if (ares_dns_rr_get_class(rr) != ARES_CLASS_IN ||
        ares_dns_rr_get_type(rr) != ARES_REC_TYPE_NS) {
      continue;
    }

    hostname = ares_dns_rr_get_str(rr, ARES_RR_NS_NSDNAME);
    if (hostname == NULL) {
      status = ARES_EBADRESP; /* LCOV_EXCL_LINE: DefensiveCoding */
      goto done;              /* LCOV_EXCL_LINE: DefensiveCoding */
    }

    hostent->h_aliases[nscount] = ares_strdup(hostname);
    if (hostent->h_aliases[nscount] == NULL) {
      status = ARES_ENOMEM;
      goto done;
    }
    nscount++;
  }

  if (nscount == 0) {
    status = ARES_ENODATA;
  } else {
    status = ARES_SUCCESS;
  }

done:
  if (status != ARES_SUCCESS) {
    ares_free_hostent(hostent);
    /* Compatibility */
    if (status == ARES_EBADNAME) {
      status = ARES_EBADRESP;
    }
  } else {
    *host = hostent;
  }
  ares_dns_record_destroy(dnsrec);
  return (int)status;
}
