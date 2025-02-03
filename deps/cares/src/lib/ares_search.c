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

#ifdef HAVE_STRINGS_H
#  include <strings.h>
#endif

struct search_query {
  /* Arguments passed to ares_search_dnsrec() */
  ares_channel_t      *channel;
  ares_callback_dnsrec callback;
  void                *arg;

  /* Duplicate of DNS record passed to ares_search_dnsrec() */
  ares_dns_record_t   *dnsrec;

  /* Search order for names */
  char               **names;
  size_t               names_cnt;

  /* State tracking progress through the search query */
  size_t               next_name_idx; /* next name index being attempted */
  size_t      timeouts;        /* number of timeouts we saw for this request */
  ares_bool_t ever_got_nodata; /* did we ever get ARES_ENODATA along the way? */
};

static void squery_free(struct search_query *squery)
{
  if (squery == NULL) {
    return; /* LCOV_EXCL_LINE: DefensiveCoding */
  }
  ares_strsplit_free(squery->names, squery->names_cnt);
  ares_dns_record_destroy(squery->dnsrec);
  ares_free(squery);
}

/* End a search query by invoking the user callback and freeing the
 * search_query structure.
 */
static void end_squery(struct search_query *squery, ares_status_t status,
                       const ares_dns_record_t *dnsrec)
{
  squery->callback(squery->arg, status, squery->timeouts, dnsrec);
  squery_free(squery);
}

static void search_callback(void *arg, ares_status_t status, size_t timeouts,
                            const ares_dns_record_t *dnsrec);

static ares_status_t ares_search_next(ares_channel_t      *channel,
                                      struct search_query *squery,
                                      ares_bool_t         *skip_cleanup)
{
  ares_status_t status;

  *skip_cleanup = ARES_FALSE;

  /* Misuse check */
  if (squery->next_name_idx >= squery->names_cnt) {
    return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  status = ares_dns_record_query_set_name(
    squery->dnsrec, 0, squery->names[squery->next_name_idx++]);
  if (status != ARES_SUCCESS) {
    return status;
  }

  status = ares_send_nolock(channel, NULL, 0, squery->dnsrec, search_callback,
                            squery, NULL);

  if (status != ARES_EFORMERR) {
    *skip_cleanup = ARES_TRUE;
  }

  return status;
}

static void search_callback(void *arg, ares_status_t status, size_t timeouts,
                            const ares_dns_record_t *dnsrec)
{
  struct search_query *squery  = (struct search_query *)arg;
  ares_channel_t      *channel = squery->channel;

  ares_status_t        mystatus;
  ares_bool_t          skip_cleanup = ARES_FALSE;

  squery->timeouts += timeouts;

  if (dnsrec) {
    ares_dns_rcode_t rcode = ares_dns_record_get_rcode(dnsrec);
    size_t ancount = ares_dns_record_rr_cnt(dnsrec, ARES_SECTION_ANSWER);
    mystatus       = ares_dns_query_reply_tostatus(rcode, ancount);
  } else {
    mystatus = status;
  }

  switch (mystatus) {
    case ARES_ENODATA:
    case ARES_ENOTFOUND:
      break;
    case ARES_ESERVFAIL:
    case ARES_EREFUSED:
      /* Issue #852, systemd-resolved may return SERVFAIL or REFUSED on a
       * single label domain name. */
      if (ares_name_label_cnt(squery->names[squery->next_name_idx - 1]) != 1) {
        end_squery(squery, mystatus, dnsrec);
        return;
      }
      break;
    default:
      end_squery(squery, mystatus, dnsrec);
      return;
  }

  /* If we ever get ARES_ENODATA along the way, record that; if the search
   * should run to the very end and we got at least one ARES_ENODATA,
   * then callers like ares_gethostbyname() may want to try a T_A search
   * even if the last domain we queried for T_AAAA resource records
   * returned ARES_ENOTFOUND.
   */
  if (mystatus == ARES_ENODATA) {
    squery->ever_got_nodata = ARES_TRUE;
  }

  if (squery->next_name_idx < squery->names_cnt) {
    mystatus = ares_search_next(channel, squery, &skip_cleanup);
    if (mystatus != ARES_SUCCESS && !skip_cleanup) {
      end_squery(squery, mystatus, NULL);
    }
    return;
  }

  /* We have no more domains to search, return an appropriate response. */
  if (mystatus == ARES_ENOTFOUND && squery->ever_got_nodata) {
    end_squery(squery, ARES_ENODATA, NULL);
    return;
  }

  end_squery(squery, mystatus, NULL);
}

/* Determine if the domain should be looked up as-is, or if it is eligible
 * for search by appending domains */
static ares_bool_t ares_search_eligible(const ares_channel_t *channel,
                                        const char           *name)
{
  size_t len = ares_strlen(name);

  /* Name ends in '.', cannot search */
  if (len && name[len - 1] == '.') {
    return ARES_FALSE;
  }

  if (channel->flags & ARES_FLAG_NOSEARCH) {
    return ARES_FALSE;
  }

  return ARES_TRUE;
}

size_t ares_name_label_cnt(const char *name)
{
  const char *p;
  size_t      ndots = 0;

  if (name == NULL) {
    return 0;
  }

  for (p = name; p != NULL && *p != 0; p++) {
    if (*p == '.') {
      ndots++;
    }
  }

  /* Label count is 1 greater than ndots */
  return ndots + 1;
}

ares_status_t ares_search_name_list(const ares_channel_t *channel,
                                    const char *name, char ***names,
                                    size_t *names_len)
{
  ares_status_t status;
  char        **list     = NULL;
  size_t        list_len = 0;
  char         *alias    = NULL;
  size_t        ndots    = 0;
  size_t        idx      = 0;
  size_t        i;

  /* Perform HOSTALIASES resolution */
  status = ares_lookup_hostaliases(channel, name, &alias);
  if (status == ARES_SUCCESS) {
    /* If hostalias succeeds, there is no searching, it is used as-is */
    list_len = 1;
    list     = ares_malloc_zero(sizeof(*list) * list_len);
    if (list == NULL) {
      status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
      goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
    }
    list[0] = alias;
    alias   = NULL;
    goto done;
  } else if (status != ARES_ENOTFOUND) {
    goto done;
  }

  /* See if searching is eligible at all, if not, look up as-is only */
  if (!ares_search_eligible(channel, name)) {
    list_len = 1;
    list     = ares_malloc_zero(sizeof(*list) * list_len);
    if (list == NULL) {
      status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
      goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
    }
    list[0] = ares_strdup(name);
    if (list[0] == NULL) {
      status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
    } else {
      status = ARES_SUCCESS;
    }
    goto done;
  }

  /* Count the number of dots in name, 1 less than label count */
  ndots = ares_name_label_cnt(name);
  if (ndots > 0) {
    ndots--;
  }

  /* Allocate an entry for each search domain, plus one for as-is */
  list_len = channel->ndomains + 1;
  list     = ares_malloc_zero(sizeof(*list) * list_len);
  if (list == NULL) {
    status = ARES_ENOMEM;
    goto done;
  }

  /* Set status here, its possible there are no search domains at all, so
   * status may be ARES_ENOTFOUND from ares_lookup_hostaliases(). */
  status = ARES_SUCCESS;

  /* Try as-is first */
  if (ndots >= channel->ndots) {
    list[idx] = ares_strdup(name);
    if (list[idx] == NULL) {
      status = ARES_ENOMEM;
      goto done;
    }
    idx++;
  }

  /* Append each search suffix to the name */
  for (i = 0; i < channel->ndomains; i++) {
    status = ares_cat_domain(name, channel->domains[i], &list[idx]);
    if (status != ARES_SUCCESS) {
      goto done;
    }
    idx++;
  }

  /* Try as-is last */
  if (ndots < channel->ndots) {
    list[idx] = ares_strdup(name);
    if (list[idx] == NULL) {
      status = ARES_ENOMEM;
      goto done;
    }
    idx++;
  }


done:
  if (status == ARES_SUCCESS) {
    *names     = list;
    *names_len = list_len;
  } else {
    ares_strsplit_free(list, list_len);
  }

  ares_free(alias);
  return status;
}

static ares_status_t ares_search_int(ares_channel_t          *channel,
                                     const ares_dns_record_t *dnsrec,
                                     ares_callback_dnsrec callback, void *arg)
{
  struct search_query *squery = NULL;
  const char          *name;
  ares_status_t        status       = ARES_SUCCESS;
  ares_bool_t          skip_cleanup = ARES_FALSE;

  /* Extract the name for the search. Note that searches are only supported for
   * DNS records containing a single query.
   */
  if (ares_dns_record_query_cnt(dnsrec) != 1) {
    status = ARES_EBADQUERY;
    goto fail;
  }

  status = ares_dns_record_query_get(dnsrec, 0, &name, NULL, NULL);
  if (status != ARES_SUCCESS) {
    goto fail;
  }

  /* Per RFC 7686, reject queries for ".onion" domain names with NXDOMAIN. */
  if (ares_is_onion_domain(name)) {
    status = ARES_ENOTFOUND;
    goto fail;
  }

  /* Allocate a search_query structure to hold the state necessary for
   * doing multiple lookups.
   */
  squery = ares_malloc_zero(sizeof(*squery));
  if (squery == NULL) {
    status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
    goto fail;            /* LCOV_EXCL_LINE: OutOfMemory */
  }

  squery->channel = channel;

  /* Duplicate DNS record since, name will need to be rewritten */
  squery->dnsrec = ares_dns_record_duplicate(dnsrec);
  if (squery->dnsrec == NULL) {
    status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
    goto fail;            /* LCOV_EXCL_LINE: OutOfMemory */
  }

  squery->callback        = callback;
  squery->arg             = arg;
  squery->timeouts        = 0;
  squery->ever_got_nodata = ARES_FALSE;

  status =
    ares_search_name_list(channel, name, &squery->names, &squery->names_cnt);
  if (status != ARES_SUCCESS) {
    goto fail;
  }

  status = ares_search_next(channel, squery, &skip_cleanup);
  if (status != ARES_SUCCESS) {
    goto fail;
  }

  return status;

fail:
  if (!skip_cleanup) {
    squery_free(squery);
    callback(arg, status, 0, NULL);
  }
  return status;
}

/* Callback argument structure passed to ares_dnsrec_convert_cb(). */
typedef struct {
  ares_callback callback;
  void         *arg;
} dnsrec_convert_arg_t;

/*! Function to create callback arg for converting from ares_callback_dnsrec
 *  to ares_calback */
void *ares_dnsrec_convert_arg(ares_callback callback, void *arg)
{
  dnsrec_convert_arg_t *carg = ares_malloc_zero(sizeof(*carg));
  if (carg == NULL) {
    return NULL;
  }
  carg->callback = callback;
  carg->arg      = arg;
  return carg;
}

/*! Callback function used to convert from the ares_callback_dnsrec prototype to
 *  the ares_callback prototype, by writing the result and passing that to
 *  the inner callback.
 */
void ares_dnsrec_convert_cb(void *arg, ares_status_t status, size_t timeouts,
                            const ares_dns_record_t *dnsrec)
{
  dnsrec_convert_arg_t *carg = arg;
  unsigned char        *abuf = NULL;
  size_t                alen = 0;

  if (dnsrec != NULL) {
    ares_status_t mystatus = ares_dns_write(dnsrec, &abuf, &alen);
    if (mystatus != ARES_SUCCESS) {
      status = mystatus;
    }
  }

  carg->callback(carg->arg, (int)status, (int)timeouts, abuf, (int)alen);

  ares_free(abuf);
  ares_free(carg);
}

/* Search for a DNS name with given class and type. Wrapper around
 * ares_search_int() where the DNS record to search is first constructed.
 */
void ares_search(ares_channel_t *channel, const char *name, int dnsclass,
                 int type, ares_callback callback, void *arg)
{
  ares_status_t      status;
  ares_dns_record_t *dnsrec = NULL;
  size_t             max_udp_size;
  ares_dns_flags_t   rd_flag;
  void              *carg = NULL;
  if (channel == NULL || name == NULL) {
    return;
  }

  /* For now, ares_search_int() uses the ares_callback prototype. We need to
   * wrap the callback passed to this function in ares_dnsrec_convert_cb, to
   * convert from ares_callback_dnsrec to ares_callback. Allocate the convert
   * arg structure here.
   */
  carg = ares_dnsrec_convert_arg(callback, arg);
  if (carg == NULL) {
    callback(arg, ARES_ENOMEM, 0, NULL, 0);
    return;
  }

  rd_flag      = !(channel->flags & ARES_FLAG_NORECURSE) ? ARES_FLAG_RD : 0;
  max_udp_size = (channel->flags & ARES_FLAG_EDNS) ? channel->ednspsz : 0;
  status       = ares_dns_record_create_query(
    &dnsrec, name, (ares_dns_class_t)dnsclass, (ares_dns_rec_type_t)type, 0,
    rd_flag, max_udp_size);
  if (status != ARES_SUCCESS) {
    callback(arg, (int)status, 0, NULL, 0);
    ares_free(carg);
    return;
  }

  ares_channel_lock(channel);
  ares_search_int(channel, dnsrec, ares_dnsrec_convert_cb, carg);
  ares_channel_unlock(channel);

  ares_dns_record_destroy(dnsrec);
}

/* Search for a DNS record. Wrapper around ares_search_int(). */
ares_status_t ares_search_dnsrec(ares_channel_t          *channel,
                                 const ares_dns_record_t *dnsrec,
                                 ares_callback_dnsrec callback, void *arg)
{
  ares_status_t status;

  if (channel == NULL || dnsrec == NULL || callback == NULL) {
    return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  ares_channel_lock(channel);
  status = ares_search_int(channel, dnsrec, callback, arg);
  ares_channel_unlock(channel);

  return status;
}

/* Concatenate two domains. */
ares_status_t ares_cat_domain(const char *name, const char *domain, char **s)
{
  size_t nlen = ares_strlen(name);
  size_t dlen = ares_strlen(domain);

  *s = ares_malloc(nlen + 1 + dlen + 1);
  if (!*s) {
    return ARES_ENOMEM;
  }
  memcpy(*s, name, nlen);
  (*s)[nlen] = '.';
  if (ares_streq(domain, ".")) {
    /* Avoid appending the root domain to the separator, which would set *s to
       an ill-formed value (ending in two consecutive dots). */
    dlen = 0;
  }
  memcpy(*s + nlen + 1, domain, dlen);
  (*s)[nlen + 1 + dlen] = 0;
  return ARES_SUCCESS;
}

ares_status_t ares_lookup_hostaliases(const ares_channel_t *channel,
                                      const char *name, char **alias)
{
  ares_status_t status      = ARES_SUCCESS;
  const char   *hostaliases = NULL;
  ares_buf_t   *buf         = NULL;
  ares_array_t *lines       = NULL;
  size_t        num;
  size_t        i;

  if (channel == NULL || name == NULL || alias == NULL) {
    return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  *alias = NULL;

  /* Configuration says to not perform alias lookup */
  if (channel->flags & ARES_FLAG_NOALIASES) {
    return ARES_ENOTFOUND;
  }

  /* If a domain has a '.', its not allowed to perform an alias lookup */
  if (strchr(name, '.') != NULL) {
    return ARES_ENOTFOUND;
  }

  hostaliases = getenv("HOSTALIASES");
  if (hostaliases == NULL) {
    status = ARES_ENOTFOUND;
    goto done;
  }

  buf = ares_buf_create();
  if (buf == NULL) {
    status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
    goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
  }

  status = ares_buf_load_file(hostaliases, buf);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  /* The HOSTALIASES file is structured as one alias per line.  The first
   * field in the line is the simple hostname with no periods, followed by
   * whitespace, then the full domain name, e.g.:
   *
   * c-ares  www.c-ares.org
   * curl    www.curl.se
   */

  status = ares_buf_split(buf, (const unsigned char *)"\n", 1,
                          ARES_BUF_SPLIT_TRIM, 0, &lines);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  num = ares_array_len(lines);
  for (i = 0; i < num; i++) {
    ares_buf_t **bufptr       = ares_array_at(lines, i);
    ares_buf_t  *line         = *bufptr;
    char         hostname[64] = "";
    char         fqdn[256]    = "";

    /* Pull off hostname */
    ares_buf_tag(line);
    ares_buf_consume_nonwhitespace(line);
    if (ares_buf_tag_fetch_string(line, hostname, sizeof(hostname)) !=
        ARES_SUCCESS) {
      continue;
    }

    /* Match hostname */
    if (!ares_strcaseeq(hostname, name)) {
      continue;
    }

    /* consume whitespace */
    ares_buf_consume_whitespace(line, ARES_TRUE);

    /* pull off fqdn */
    ares_buf_tag(line);
    ares_buf_consume_nonwhitespace(line);
    if (ares_buf_tag_fetch_string(line, fqdn, sizeof(fqdn)) != ARES_SUCCESS ||
        ares_strlen(fqdn) == 0) {
      continue;
    }

    /* Validate characterset */
    if (!ares_is_hostname(fqdn)) {
      continue;
    }

    *alias = ares_strdup(fqdn);
    if (*alias == NULL) {
      status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
      goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
    }

    /* Good! */
    status = ARES_SUCCESS;
    goto done;
  }

  status = ARES_ENOTFOUND;

done:
  ares_buf_destroy(buf);
  ares_array_destroy(lines);

  return status;
}
