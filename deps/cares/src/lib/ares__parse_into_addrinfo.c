/* MIT License
 *
 * Copyright (c) 2019 Andrew Selivanov
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

#ifdef HAVE_STRINGS_H
#  include <strings.h>
#endif

#ifdef HAVE_LIMITS_H
#  include <limits.h>
#endif


ares_status_t ares__parse_into_addrinfo(const ares_dns_record_t *dnsrec,
                                        ares_bool_t    cname_only_is_enodata,
                                        unsigned short port,
                                        struct ares_addrinfo *ai)
{
  ares_status_t               status;
  size_t                      i;
  size_t                      ancount;
  const char                 *hostname  = NULL;
  ares_bool_t                 got_a     = ARES_FALSE;
  ares_bool_t                 got_aaaa  = ARES_FALSE;
  ares_bool_t                 got_cname = ARES_FALSE;
  struct ares_addrinfo_cname *cnames    = NULL;
  struct ares_addrinfo_node  *nodes     = NULL;

  /* Save question hostname */
  status = ares_dns_record_query_get(dnsrec, 0, &hostname, NULL, NULL);
  if (status != ARES_SUCCESS) {
    goto done; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  ancount = ares_dns_record_rr_cnt(dnsrec, ARES_SECTION_ANSWER);
  if (ancount == 0) {
    status = ARES_ENODATA;
    goto done;
  }

  for (i = 0; i < ancount; i++) {
    ares_dns_rec_type_t  rtype;
    const ares_dns_rr_t *rr =
      ares_dns_record_rr_get_const(dnsrec, ARES_SECTION_ANSWER, i);

    if (ares_dns_rr_get_class(rr) != ARES_CLASS_IN) {
      continue;
    }

    rtype = ares_dns_rr_get_type(rr);

    /* Issue #683
     * Old code did this hostname sanity check, however it appears this is
     * flawed logic.  Other resolvers don't do this sanity check.  Leaving
     * this code commented out for future reference.
     *
     * rname = ares_dns_rr_get_name(rr);
     * if ((rtype == ARES_REC_TYPE_A || rtype == ARES_REC_TYPE_AAAA) &&
     *     strcasecmp(rname, hostname) != 0) {
     *   continue;
     * }
     */

    if (rtype == ARES_REC_TYPE_CNAME) {
      struct ares_addrinfo_cname *cname;

      got_cname = ARES_TRUE;
      /* replace hostname with data from cname
       * SA: Seems wrong as it introduces order dependency. */
      hostname = ares_dns_rr_get_str(rr, ARES_RR_CNAME_CNAME);

      cname = ares__append_addrinfo_cname(&cnames);
      if (cname == NULL) {
        status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
        goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
      }
      cname->ttl   = (int)ares_dns_rr_get_ttl(rr);
      cname->alias = ares_strdup(ares_dns_rr_get_name(rr));
      if (cname->alias == NULL) {
        status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
        goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
      }
      cname->name = ares_strdup(hostname);
      if (cname->name == NULL) {
        status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
        goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
      }
    } else if (rtype == ARES_REC_TYPE_A) {
      got_a = ARES_TRUE;
      status =
        ares_append_ai_node(AF_INET, port, ares_dns_rr_get_ttl(rr),
                            ares_dns_rr_get_addr(rr, ARES_RR_A_ADDR), &nodes);
      if (status != ARES_SUCCESS) {
        goto done; /* LCOV_EXCL_LINE: OutOfMemory */
      }
    } else if (rtype == ARES_REC_TYPE_AAAA) {
      got_aaaa = ARES_TRUE;
      status   = ares_append_ai_node(AF_INET6, port, ares_dns_rr_get_ttl(rr),
                                     ares_dns_rr_get_addr6(rr, ARES_RR_AAAA_ADDR),
                                     &nodes);
      if (status != ARES_SUCCESS) {
        goto done; /* LCOV_EXCL_LINE: OutOfMemory */
      }
    } else {
      continue;
    }
  }

  if (!got_a && !got_aaaa &&
      (!got_cname || (got_cname && cname_only_is_enodata))) {
    status = ARES_ENODATA;
    goto done;
  }

  /* save the hostname as ai->name */
  if (ai->name == NULL || strcasecmp(ai->name, hostname) != 0) {
    ares_free(ai->name);
    ai->name = ares_strdup(hostname);
    if (ai->name == NULL) {
      status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
      goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  if (got_a || got_aaaa) {
    ares__addrinfo_cat_nodes(&ai->nodes, nodes);
    nodes = NULL;
  }

  if (got_cname) {
    ares__addrinfo_cat_cnames(&ai->cnames, cnames);
    cnames = NULL;
  }

done:
  ares__freeaddrinfo_cnames(cnames);
  ares__freeaddrinfo_nodes(nodes);

  /* compatibility */
  if (status == ARES_EBADNAME) {
    status = ARES_EBADRESP;
  }

  return status;
}
