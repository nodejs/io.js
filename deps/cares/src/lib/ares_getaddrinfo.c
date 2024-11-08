/* MIT License
 *
 * Copyright (c) 1998, 2011, 2013 Massachusetts Institute of Technology
 * Copyright (c) 2017 Christian Ammer
 * Copyright (c) 2019 Andrew Selivanov
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

#ifdef HAVE_GETSERVBYNAME_R
#  if !defined(GETSERVBYNAME_R_ARGS) || (GETSERVBYNAME_R_ARGS < 4) || \
    (GETSERVBYNAME_R_ARGS > 6)
#    error "you MUST specify a valid number of arguments for getservbyname_r"
#  endif
#endif

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
#include <assert.h>

#ifdef HAVE_LIMITS_H
#  include <limits.h>
#endif

#include "ares_dns.h"

struct host_query {
  ares_channel_t            *channel;
  char                      *name;
  unsigned short             port; /* in host order */
  ares_addrinfo_callback     callback;
  void                      *arg;
  struct ares_addrinfo_hints hints;
  int    sent_family; /* this family is what was is being used */
  size_t timeouts;    /* number of timeouts we saw for this request */
  char  *lookups; /* Duplicate memory from channel because of ares_reinit() */
  const char *remaining_lookups; /* types of lookup we need to perform ("fb" by
                                    default, file and dns respectively) */

  /* Search order for names */
  char      **names;
  size_t      names_cnt;
  size_t      next_name_idx;       /* next name index being attempted */

  struct ares_addrinfo *ai;        /* store results between lookups */
  unsigned short        qid_a;     /* qid for A request */
  unsigned short        qid_aaaa;  /* qid for AAAA request */

  size_t                remaining; /* number of DNS answers waiting for */

  /* Track nodata responses to possibly override final result */
  size_t                nodata_cnt;
};

static const struct ares_addrinfo_hints default_hints = {
  0,         /* ai_flags */
  AF_UNSPEC, /* ai_family */
  0,         /* ai_socktype */
  0,         /* ai_protocol */
};

/* forward declarations */
static ares_bool_t next_dns_lookup(struct host_query *hquery);

struct ares_addrinfo_cname *
  ares_append_addrinfo_cname(struct ares_addrinfo_cname **head)
{
  struct ares_addrinfo_cname *tail = ares_malloc_zero(sizeof(*tail));
  struct ares_addrinfo_cname *last = *head;

  if (tail == NULL) {
    return NULL; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  if (!last) {
    *head = tail;
    return tail;
  }

  while (last->next) {
    last = last->next;
  }

  last->next = tail;
  return tail;
}

void ares_addrinfo_cat_cnames(struct ares_addrinfo_cname **head,
                              struct ares_addrinfo_cname  *tail)
{
  struct ares_addrinfo_cname *last = *head;
  if (!last) {
    *head = tail;
    return;
  }

  while (last->next) {
    last = last->next;
  }

  last->next = tail;
}

/* Allocate new addrinfo and append to the tail. */
struct ares_addrinfo_node *
  ares_append_addrinfo_node(struct ares_addrinfo_node **head)
{
  struct ares_addrinfo_node *tail = ares_malloc_zero(sizeof(*tail));
  struct ares_addrinfo_node *last = *head;

  if (tail == NULL) {
    return NULL; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  if (!last) {
    *head = tail;
    return tail;
  }

  while (last->ai_next) {
    last = last->ai_next;
  }

  last->ai_next = tail;
  return tail;
}

void ares_addrinfo_cat_nodes(struct ares_addrinfo_node **head,
                             struct ares_addrinfo_node  *tail)
{
  struct ares_addrinfo_node *last = *head;
  if (!last) {
    *head = tail;
    return;
  }

  while (last->ai_next) {
    last = last->ai_next;
  }

  last->ai_next = tail;
}

/* Resolve service name into port number given in host byte order.
 * If not resolved, return 0.
 */
static unsigned short lookup_service(const char *service, int flags)
{
  const char     *proto;
  struct servent *sep;
#ifdef HAVE_GETSERVBYNAME_R
  struct servent se;
  char           tmpbuf[4096];
#endif

  if (service) {
    if (flags & ARES_NI_UDP) {
      proto = "udp";
    } else if (flags & ARES_NI_SCTP) {
      proto = "sctp";
    } else if (flags & ARES_NI_DCCP) {
      proto = "dccp";
    } else {
      proto = "tcp";
    }
#ifdef HAVE_GETSERVBYNAME_R
    memset(&se, 0, sizeof(se));
    sep = &se;
    memset(tmpbuf, 0, sizeof(tmpbuf));
#  if GETSERVBYNAME_R_ARGS == 6
    if (getservbyname_r(service, proto, &se, (void *)tmpbuf, sizeof(tmpbuf),
                        &sep) != 0) {
      sep = NULL; /* LCOV_EXCL_LINE: buffer large so this never fails */
    }
#  elif GETSERVBYNAME_R_ARGS == 5
    sep = getservbyname_r(service, proto, &se, (void *)tmpbuf, sizeof(tmpbuf));
#  elif GETSERVBYNAME_R_ARGS == 4
    if (getservbyname_r(service, proto, &se, (void *)tmpbuf) != 0) {
      sep = NULL;
    }
#  else
    /* Lets just hope the OS uses TLS! */
    sep = getservbyname(service, proto);
#  endif
#else
    /* Lets just hope the OS uses TLS! */
#  if (defined(NETWARE) && !defined(__NOVELL_LIBC__))
    sep = getservbyname(service, (char *)proto);
#  else
    sep = getservbyname(service, proto);
#  endif
#endif
    return (sep ? ntohs((unsigned short)sep->s_port) : 0);
  }
  return 0;
}

/* If the name looks like an IP address or an error occurred,
 * fake up a host entry, end the query immediately, and return true.
 * Otherwise return false.
 */
static ares_bool_t fake_addrinfo(const char *name, unsigned short port,
                                 const struct ares_addrinfo_hints *hints,
                                 struct ares_addrinfo             *ai,
                                 ares_addrinfo_callback callback, void *arg)
{
  struct ares_addrinfo_cname *cname;
  ares_status_t               status = ARES_SUCCESS;
  ares_bool_t                 result = ARES_FALSE;
  int                         family = hints->ai_family;
  if (family == AF_INET || family == AF_INET6 || family == AF_UNSPEC) {
    /* It only looks like an IP address if it's all numbers and dots. */
    size_t      numdots = 0;
    ares_bool_t valid   = ARES_TRUE;
    const char *p;
    for (p = name; *p; p++) {
      if (!ares_isdigit(*p) && *p != '.') {
        valid = ARES_FALSE;
        break;
      } else if (*p == '.') {
        numdots++;
      }
    }

    /* if we don't have 3 dots, it is illegal
     * (although inet_pton doesn't think so).
     */
    if (numdots != 3 || !valid) {
      result = ARES_FALSE;
    } else {
      struct in_addr addr4;
      result =
        ares_inet_pton(AF_INET, name, &addr4) < 1 ? ARES_FALSE : ARES_TRUE;
      if (result) {
        status = ares_append_ai_node(AF_INET, port, 0, &addr4, &ai->nodes);
        if (status != ARES_SUCCESS) {
          callback(arg, (int)status, 0, NULL); /* LCOV_EXCL_LINE: OutOfMemory */
          return ARES_TRUE;                    /* LCOV_EXCL_LINE: OutOfMemory */
        }
      }
    }
  }

  if (!result && (family == AF_INET6 || family == AF_UNSPEC)) {
    struct ares_in6_addr addr6;
    result =
      ares_inet_pton(AF_INET6, name, &addr6) < 1 ? ARES_FALSE : ARES_TRUE;
    if (result) {
      status = ares_append_ai_node(AF_INET6, port, 0, &addr6, &ai->nodes);
      if (status != ARES_SUCCESS) {
        callback(arg, (int)status, 0, NULL); /* LCOV_EXCL_LINE: OutOfMemory */
        return ARES_TRUE;                    /* LCOV_EXCL_LINE: OutOfMemory */
      }
    }
  }

  if (!result) {
    return ARES_FALSE;
  }

  if (hints->ai_flags & ARES_AI_CANONNAME) {
    cname = ares_append_addrinfo_cname(&ai->cnames);
    if (!cname) {
      /* LCOV_EXCL_START: OutOfMemory */
      ares_freeaddrinfo(ai);
      callback(arg, ARES_ENOMEM, 0, NULL);
      return ARES_TRUE;
      /* LCOV_EXCL_STOP */
    }

    /* Duplicate the name, to avoid a constness violation. */
    cname->name = ares_strdup(name);
    if (!cname->name) {
      ares_freeaddrinfo(ai);
      callback(arg, ARES_ENOMEM, 0, NULL);
      return ARES_TRUE;
    }
  }

  ai->nodes->ai_socktype = hints->ai_socktype;
  ai->nodes->ai_protocol = hints->ai_protocol;

  callback(arg, ARES_SUCCESS, 0, ai);
  return ARES_TRUE;
}

static void hquery_free(struct host_query *hquery, ares_bool_t cleanup_ai)
{
  if (cleanup_ai) {
    ares_freeaddrinfo(hquery->ai);
  }
  ares_strsplit_free(hquery->names, hquery->names_cnt);
  ares_free(hquery->name);
  ares_free(hquery->lookups);
  ares_free(hquery);
}

static void end_hquery(struct host_query *hquery, ares_status_t status)
{
  struct ares_addrinfo_node  sentinel;
  struct ares_addrinfo_node *next;

  if (status == ARES_SUCCESS) {
    if (!(hquery->hints.ai_flags & ARES_AI_NOSORT) && hquery->ai->nodes) {
      sentinel.ai_next = hquery->ai->nodes;
      ares_sortaddrinfo(hquery->channel, &sentinel);
      hquery->ai->nodes = sentinel.ai_next;
    }
    next = hquery->ai->nodes;

    while (next) {
      next->ai_socktype = hquery->hints.ai_socktype;
      next->ai_protocol = hquery->hints.ai_protocol;
      next              = next->ai_next;
    }
  } else {
    /* Clean up what we have collected by so far. */
    ares_freeaddrinfo(hquery->ai);
    hquery->ai = NULL;
  }

  hquery->callback(hquery->arg, (int)status, (int)hquery->timeouts, hquery->ai);
  hquery_free(hquery, ARES_FALSE);
}

ares_bool_t ares_is_localhost(const char *name)
{
  /* RFC6761 6.3 says : The domain "localhost." and any names falling within
   * ".localhost." */
  size_t len;

  if (name == NULL) {
    return ARES_FALSE; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  if (ares_strcaseeq(name, "localhost")) {
    return ARES_TRUE;
  }

  len = ares_strlen(name);
  if (len < 10 /* strlen(".localhost") */) {
    return ARES_FALSE;
  }

  if (ares_strcaseeq(name + (len - 10 /* strlen(".localhost") */),
                     ".localhost")) {
    return ARES_TRUE;
  }

  return ARES_FALSE;
}

static ares_status_t file_lookup(struct host_query *hquery)
{
  const ares_hosts_entry_t *entry;
  ares_status_t             status;

  /* Per RFC 7686, reject queries for ".onion" domain names with NXDOMAIN. */
  if (ares_is_onion_domain(hquery->name)) {
    return ARES_ENOTFOUND;
  }

  status = ares_hosts_search_host(
    hquery->channel,
    (hquery->hints.ai_flags & ARES_AI_ENVHOSTS) ? ARES_TRUE : ARES_FALSE,
    hquery->name, &entry);

  if (status != ARES_SUCCESS) {
    goto done;
  }

  status = ares_hosts_entry_to_addrinfo(
    entry, hquery->name, hquery->hints.ai_family, hquery->port,
    (hquery->hints.ai_flags & ARES_AI_CANONNAME) ? ARES_TRUE : ARES_FALSE,
    hquery->ai);

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
      ares_is_localhost(hquery->name)) {
    return ares_addrinfo_localhost(hquery->name, hquery->port, &hquery->hints,
                                   hquery->ai);
  }

  return status;
}

static void next_lookup(struct host_query *hquery, ares_status_t status)
{
  switch (*hquery->remaining_lookups) {
    case 'b':
      /* RFC6761 section 6.3 #3 says "Name resolution APIs SHOULD NOT send
       * queries for localhost names to their configured caching DNS
       * server(s)."
       * Otherwise, DNS lookup. */
      if (!ares_is_localhost(hquery->name) && next_dns_lookup(hquery)) {
        break;
      }

      hquery->remaining_lookups++;
      next_lookup(hquery, status);
      break;

    case 'f':
      /* Host file lookup */
      if (file_lookup(hquery) == ARES_SUCCESS) {
        end_hquery(hquery, ARES_SUCCESS);
        break;
      }
      hquery->remaining_lookups++;
      next_lookup(hquery, status);
      break;
    default:
      /* No lookup left */
      end_hquery(hquery, status);
      break;
  }
}

static void terminate_retries(const struct host_query *hquery,
                              unsigned short           qid)
{
  unsigned short term_qid =
    (qid == hquery->qid_a) ? hquery->qid_aaaa : hquery->qid_a;
  const ares_channel_t *channel = hquery->channel;
  ares_query_t         *query   = NULL;

  /* No other outstanding queries, nothing to do */
  if (!hquery->remaining) {
    return;
  }

  query = ares_htable_szvp_get_direct(channel->queries_by_qid, term_qid);
  if (query == NULL) {
    return;
  }

  query->no_retries = ARES_TRUE;
}

static void host_callback(void *arg, ares_status_t status, size_t timeouts,
                          const ares_dns_record_t *dnsrec)
{
  struct host_query *hquery         = (struct host_query *)arg;
  ares_status_t      addinfostatus  = ARES_SUCCESS;
  hquery->timeouts                 += timeouts;
  hquery->remaining--;

  if (status == ARES_SUCCESS) {
    if (dnsrec == NULL) {
      addinfostatus = ARES_EBADRESP; /* LCOV_EXCL_LINE: DefensiveCoding */
    } else {
      addinfostatus =
        ares_parse_into_addrinfo(dnsrec, ARES_TRUE, hquery->port, hquery->ai);
    }
    if (addinfostatus == ARES_SUCCESS) {
      terminate_retries(hquery, ares_dns_record_get_id(dnsrec));
    }
  }

  if (!hquery->remaining) {
    if (status == ARES_EDESTRUCTION || status == ARES_ECANCELLED) {
      /* must make sure we don't do next_lookup() on destroy or cancel,
       * and return the appropriate status.  We won't return a partial
       * result in this case. */
      end_hquery(hquery, status);
    } else if (addinfostatus != ARES_SUCCESS && addinfostatus != ARES_ENODATA) {
      /* error in parsing result e.g. no memory */
      if (addinfostatus == ARES_EBADRESP && hquery->ai->nodes) {
        /* We got a bad response from server, but at least one query
         * ended with ARES_SUCCESS */
        end_hquery(hquery, ARES_SUCCESS);
      } else {
        end_hquery(hquery, addinfostatus);
      }
    } else if (hquery->ai->nodes) {
      /* at least one query ended with ARES_SUCCESS */
      end_hquery(hquery, ARES_SUCCESS);
    } else if (status == ARES_ENOTFOUND || status == ARES_ENODATA ||
               addinfostatus == ARES_ENODATA) {
      if (status == ARES_ENODATA || addinfostatus == ARES_ENODATA) {
        hquery->nodata_cnt++;
      }
      next_lookup(hquery, hquery->nodata_cnt ? ARES_ENODATA : status);
    } else if ((status == ARES_ESERVFAIL || status == ARES_EREFUSED) &&
               ares_name_label_cnt(hquery->names[hquery->next_name_idx - 1]) ==
                 1) {
      /* Issue #852, systemd-resolved may return SERVFAIL or REFUSED on a
       * single label domain name. */
      next_lookup(hquery, hquery->nodata_cnt ? ARES_ENODATA : status);
    } else {
      end_hquery(hquery, status);
    }
  }

  /* at this point we keep on waiting for the next query to finish */
}

static void ares_getaddrinfo_int(ares_channel_t *channel, const char *name,
                                 const char                       *service,
                                 const struct ares_addrinfo_hints *hints,
                                 ares_addrinfo_callback callback, void *arg)
{
  struct host_query    *hquery;
  unsigned short        port = 0;
  int                   family;
  struct ares_addrinfo *ai;
  ares_status_t         status;

  if (!hints) {
    hints = &default_hints;
  }

  family = hints->ai_family;

  /* Right now we only know how to look up Internet addresses
     and unspec means try both basically. */
  if (family != AF_INET && family != AF_INET6 && family != AF_UNSPEC) {
    callback(arg, ARES_ENOTIMP, 0, NULL);
    return;
  }

  if (ares_is_onion_domain(name)) {
    callback(arg, ARES_ENOTFOUND, 0, NULL);
    return;
  }

  if (service) {
    if (hints->ai_flags & ARES_AI_NUMERICSERV) {
      unsigned long val;
      errno = 0;
      val   = strtoul(service, NULL, 0);
      if ((val == 0 && errno != 0) || val > 65535) {
        callback(arg, ARES_ESERVICE, 0, NULL);
        return;
      }
      port = (unsigned short)val;
    } else {
      port = lookup_service(service, 0);
      if (!port) {
        unsigned long val;
        errno = 0;
        val   = strtoul(service, NULL, 0);
        if ((val == 0 && errno != 0) || val > 65535) {
          callback(arg, ARES_ESERVICE, 0, NULL);
          return;
        }
        port = (unsigned short)val;
      }
    }
  }

  ai = ares_malloc_zero(sizeof(*ai));
  if (!ai) {
    callback(arg, ARES_ENOMEM, 0, NULL);
    return;
  }

  if (fake_addrinfo(name, port, hints, ai, callback, arg)) {
    return;
  }

  /* Allocate and fill in the host query structure. */
  hquery = ares_malloc_zero(sizeof(*hquery));
  if (!hquery) {
    ares_freeaddrinfo(ai);
    callback(arg, ARES_ENOMEM, 0, NULL);
    return;
  }

  hquery->port        = port;
  hquery->channel     = channel;
  hquery->hints       = *hints;
  hquery->sent_family = -1; /* nothing is sent yet */
  hquery->callback    = callback;
  hquery->arg         = arg;
  hquery->ai          = ai;
  hquery->name        = ares_strdup(name);
  if (hquery->name == NULL) {
    hquery_free(hquery, ARES_TRUE);
    callback(arg, ARES_ENOMEM, 0, NULL);
    return;
  }

  status =
    ares_search_name_list(channel, name, &hquery->names, &hquery->names_cnt);
  if (status != ARES_SUCCESS) {
    hquery_free(hquery, ARES_TRUE);
    callback(arg, (int)status, 0, NULL);
    return;
  }
  hquery->next_name_idx = 0;


  hquery->lookups = ares_strdup(channel->lookups);
  if (hquery->lookups == NULL) {
    hquery_free(hquery, ARES_TRUE);
    callback(arg, ARES_ENOMEM, 0, NULL);
    return;
  }
  hquery->remaining_lookups = hquery->lookups;

  /* Start performing lookups according to channel->lookups. */
  next_lookup(hquery, ARES_ECONNREFUSED /* initial error code */);
}

void ares_getaddrinfo(ares_channel_t *channel, const char *name,
                      const char                       *service,
                      const struct ares_addrinfo_hints *hints,
                      ares_addrinfo_callback callback, void *arg)
{
  if (channel == NULL) {
    return;
  }
  ares_channel_lock(channel);
  ares_getaddrinfo_int(channel, name, service, hints, callback, arg);
  ares_channel_unlock(channel);
}

static ares_bool_t next_dns_lookup(struct host_query *hquery)
{
  const char *name = NULL;

  if (hquery->next_name_idx >= hquery->names_cnt) {
    return ARES_FALSE;
  }

  name = hquery->names[hquery->next_name_idx++];

  /* NOTE: hquery may be invalidated during the call to ares_query_qid(),
   *       so should not be referenced after this point */
  switch (hquery->hints.ai_family) {
    case AF_INET:
      hquery->remaining += 1;
      ares_query_nolock(hquery->channel, name, ARES_CLASS_IN, ARES_REC_TYPE_A,
                        host_callback, hquery, &hquery->qid_a);
      break;
    case AF_INET6:
      hquery->remaining += 1;
      ares_query_nolock(hquery->channel, name, ARES_CLASS_IN,
                        ARES_REC_TYPE_AAAA, host_callback, hquery,
                        &hquery->qid_aaaa);
      break;
    case AF_UNSPEC:
      hquery->remaining += 2;
      ares_query_nolock(hquery->channel, name, ARES_CLASS_IN, ARES_REC_TYPE_A,
                        host_callback, hquery, &hquery->qid_a);
      ares_query_nolock(hquery->channel, name, ARES_CLASS_IN,
                        ARES_REC_TYPE_AAAA, host_callback, hquery,
                        &hquery->qid_aaaa);
      break;
    default:
      break;
  }

  return ARES_TRUE;
}
