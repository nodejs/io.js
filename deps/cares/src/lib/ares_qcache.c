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

struct ares__qcache {
  ares__htable_strvp_t *cache;
  ares__slist_t        *expire;
  unsigned int          max_ttl;
};

typedef struct {
  char              *key;
  ares_dns_record_t *dnsrec;
  time_t             expire_ts;
  time_t             insert_ts;
} ares__qcache_entry_t;

static char *ares__qcache_calc_key(const ares_dns_record_t *dnsrec)
{
  ares__buf_t     *buf = ares__buf_create();
  size_t           i;
  ares_status_t    status;
  ares_dns_flags_t flags;

  if (dnsrec == NULL || buf == NULL) {
    return NULL; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  /* Format is OPCODE|FLAGS[|QTYPE1|QCLASS1|QNAME1]... */

  status = ares__buf_append_str(
    buf, ares_dns_opcode_tostr(ares_dns_record_get_opcode(dnsrec)));
  if (status != ARES_SUCCESS) {
    goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  status = ares__buf_append_byte(buf, '|');
  if (status != ARES_SUCCESS) {
    goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  flags = ares_dns_record_get_flags(dnsrec);
  /* Only care about RD and CD */
  if (flags & ARES_FLAG_RD) {
    status = ares__buf_append_str(buf, "rd");
    if (status != ARES_SUCCESS) {
      goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }
  if (flags & ARES_FLAG_CD) {
    status = ares__buf_append_str(buf, "cd");
    if (status != ARES_SUCCESS) {
      goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  for (i = 0; i < ares_dns_record_query_cnt(dnsrec); i++) {
    const char         *name;
    size_t              name_len;
    ares_dns_rec_type_t qtype;
    ares_dns_class_t    qclass;

    status = ares_dns_record_query_get(dnsrec, i, &name, &qtype, &qclass);
    if (status != ARES_SUCCESS) {
      goto fail; /* LCOV_EXCL_LINE: DefensiveCoding */
    }

    status = ares__buf_append_byte(buf, '|');
    if (status != ARES_SUCCESS) {
      goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
    }

    status = ares__buf_append_str(buf, ares_dns_rec_type_tostr(qtype));
    if (status != ARES_SUCCESS) {
      goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
    }

    status = ares__buf_append_byte(buf, '|');
    if (status != ARES_SUCCESS) {
      goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
    }

    status = ares__buf_append_str(buf, ares_dns_class_tostr(qclass));
    if (status != ARES_SUCCESS) {
      goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
    }

    status = ares__buf_append_byte(buf, '|');
    if (status != ARES_SUCCESS) {
      goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
    }

    /* On queries, a '.' may be appended to the name to indicate an explicit
     * name lookup without performing a search.  Strip this since its not part
     * of a cached response. */
    name_len = ares_strlen(name);
    if (name_len && name[name_len - 1] == '.') {
      name_len--;
    }

    status = ares__buf_append(buf, (const unsigned char *)name, name_len);
    if (status != ARES_SUCCESS) {
      goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  return ares__buf_finish_str(buf, NULL);

/* LCOV_EXCL_START: OutOfMemory */
fail:
  ares__buf_destroy(buf);
  return NULL;
  /* LCOV_EXCL_STOP */
}

static void ares__qcache_expire(ares__qcache_t       *cache,
                                const ares_timeval_t *now)
{
  ares__slist_node_t *node;

  if (cache == NULL) {
    return;
  }

  while ((node = ares__slist_node_first(cache->expire)) != NULL) {
    const ares__qcache_entry_t *entry = ares__slist_node_val(node);

    /* If now is NULL, we're flushing everything, so don't break */
    if (now != NULL && entry->expire_ts > now->sec) {
      break;
    }

    ares__htable_strvp_remove(cache->cache, entry->key);
    ares__slist_node_destroy(node);
  }
}

void ares__qcache_flush(ares__qcache_t *cache)
{
  ares__qcache_expire(cache, NULL /* flush all */);
}

void ares__qcache_destroy(ares__qcache_t *cache)
{
  if (cache == NULL) {
    return;
  }

  ares__htable_strvp_destroy(cache->cache);
  ares__slist_destroy(cache->expire);
  ares_free(cache);
}

static int ares__qcache_entry_sort_cb(const void *arg1, const void *arg2)
{
  const ares__qcache_entry_t *entry1 = arg1;
  const ares__qcache_entry_t *entry2 = arg2;

  if (entry1->expire_ts > entry2->expire_ts) {
    return 1;
  }

  if (entry1->expire_ts < entry2->expire_ts) {
    return -1;
  }

  return 0;
}

static void ares__qcache_entry_destroy_cb(void *arg)
{
  ares__qcache_entry_t *entry = arg;
  if (entry == NULL) {
    return; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  ares_free(entry->key);
  ares_dns_record_destroy(entry->dnsrec);
  ares_free(entry);
}

ares_status_t ares__qcache_create(ares_rand_state *rand_state,
                                  unsigned int     max_ttl,
                                  ares__qcache_t **cache_out)
{
  ares_status_t   status = ARES_SUCCESS;
  ares__qcache_t *cache;

  cache = ares_malloc_zero(sizeof(*cache));
  if (cache == NULL) {
    status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
    goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
  }

  cache->cache = ares__htable_strvp_create(NULL);
  if (cache->cache == NULL) {
    status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
    goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
  }

  cache->expire = ares__slist_create(rand_state, ares__qcache_entry_sort_cb,
                                     ares__qcache_entry_destroy_cb);
  if (cache->expire == NULL) {
    status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
    goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
  }

  cache->max_ttl = max_ttl;

done:
  if (status != ARES_SUCCESS) {
    *cache_out = NULL;
    ares__qcache_destroy(cache);
    return status;
  }

  *cache_out = cache;
  return status;
}

static unsigned int ares__qcache_calc_minttl(ares_dns_record_t *dnsrec)
{
  unsigned int minttl = 0xFFFFFFFF;
  size_t       sect;

  for (sect = ARES_SECTION_ANSWER; sect <= ARES_SECTION_ADDITIONAL; sect++) {
    size_t i;
    for (i = 0; i < ares_dns_record_rr_cnt(dnsrec, (ares_dns_section_t)sect);
         i++) {
      const ares_dns_rr_t *rr =
        ares_dns_record_rr_get(dnsrec, (ares_dns_section_t)sect, i);
      ares_dns_rec_type_t type = ares_dns_rr_get_type(rr);
      unsigned int        ttl  = ares_dns_rr_get_ttl(rr);

      /* TTL is meaningless on these record types */
      if (type == ARES_REC_TYPE_OPT || type == ARES_REC_TYPE_SOA ||
          type == ARES_REC_TYPE_SIG) {
        continue;
      }

      if (ttl < minttl) {
        minttl = ttl;
      }
    }
  }

  return minttl;
}

static unsigned int ares__qcache_soa_minimum(ares_dns_record_t *dnsrec)
{
  size_t i;

  /* RFC 2308 Section 5 says its the minimum of MINIMUM and the TTL of the
   * record. */
  for (i = 0; i < ares_dns_record_rr_cnt(dnsrec, ARES_SECTION_AUTHORITY); i++) {
    const ares_dns_rr_t *rr =
      ares_dns_record_rr_get(dnsrec, ARES_SECTION_AUTHORITY, i);
    ares_dns_rec_type_t type = ares_dns_rr_get_type(rr);
    unsigned int        ttl;
    unsigned int        minimum;

    if (type != ARES_REC_TYPE_SOA) {
      continue;
    }

    minimum = ares_dns_rr_get_u32(rr, ARES_RR_SOA_MINIMUM);
    ttl     = ares_dns_rr_get_ttl(rr);

    if (ttl > minimum) {
      return minimum;
    }
    return ttl;
  }

  return 0;
}

/* On success, takes ownership of dnsrec */
static ares_status_t ares__qcache_insert(ares__qcache_t            *qcache,
                                         ares_dns_record_t         *qresp,
                                         const ares_dns_record_t   *qreq,
                                         const ares_timeval_t      *now)
{
  ares__qcache_entry_t *entry;
  unsigned int          ttl;
  ares_dns_rcode_t      rcode = ares_dns_record_get_rcode(qresp);
  ares_dns_flags_t      flags = ares_dns_record_get_flags(qresp);

  if (qcache == NULL || qresp == NULL) {
    return ARES_EFORMERR;
  }

  /* Only save NOERROR or NXDOMAIN */
  if (rcode != ARES_RCODE_NOERROR && rcode != ARES_RCODE_NXDOMAIN) {
    return ARES_ENOTIMP;
  }

  /* Don't save truncated queries */
  if (flags & ARES_FLAG_TC) {
    return ARES_ENOTIMP;
  }

  /* Look at SOA for NXDOMAIN for minimum */
  if (rcode == ARES_RCODE_NXDOMAIN) {
    ttl = ares__qcache_soa_minimum(qresp);
  } else {
    ttl = ares__qcache_calc_minttl(qresp);
  }

  if (ttl > qcache->max_ttl) {
    ttl = qcache->max_ttl;
  }

  /* Don't cache something that is already expired */
  if (ttl == 0) {
    return ARES_EREFUSED;
  }

  entry = ares_malloc_zero(sizeof(*entry));
  if (entry == NULL) {
    goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  entry->dnsrec    = qresp;
  entry->expire_ts = now->sec + (time_t)ttl;
  entry->insert_ts = now->sec;

  /* We can't guarantee the server responded with the same flags as the
   * request had, so we have to re-parse the request in order to generate the
   * key for caching, but we'll only do this once we know for sure we really
   * want to cache it */
  entry->key = ares__qcache_calc_key(qreq);
  if (entry->key == NULL) {
    goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  if (!ares__htable_strvp_insert(qcache->cache, entry->key, entry)) {
    goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  if (ares__slist_insert(qcache->expire, entry) == NULL) {
    goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  return ARES_SUCCESS;

/* LCOV_EXCL_START: OutOfMemory */
fail:
  if (entry != NULL && entry->key != NULL) {
    ares__htable_strvp_remove(qcache->cache, entry->key);
    ares_free(entry->key);
    ares_free(entry);
  }
  return ARES_ENOMEM;
  /* LCOV_EXCL_STOP */
}

ares_status_t ares_qcache_fetch(ares_channel_t           *channel,
                                const ares_timeval_t     *now,
                                const ares_dns_record_t  *dnsrec,
                                const ares_dns_record_t **dnsrec_resp)
{
  char                 *key = NULL;
  ares__qcache_entry_t *entry;
  ares_status_t         status = ARES_SUCCESS;

  if (channel == NULL || dnsrec == NULL || dnsrec_resp == NULL) {
    return ARES_EFORMERR;
  }

  if (channel->qcache == NULL) {
    return ARES_ENOTFOUND;
  }

  ares__qcache_expire(channel->qcache, now);

  key = ares__qcache_calc_key(dnsrec);
  if (key == NULL) {
    status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
    goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
  }

  entry = ares__htable_strvp_get_direct(channel->qcache->cache, key);
  if (entry == NULL) {
    status = ARES_ENOTFOUND;
    goto done;
  }

  ares_dns_record_write_ttl_decrement(
    entry->dnsrec, (unsigned int)(now->sec - entry->insert_ts));

  *dnsrec_resp = entry->dnsrec;

done:
  ares_free(key);
  return status;
}

ares_status_t ares_qcache_insert(ares_channel_t       *channel,
                                 const ares_timeval_t *now,
                                 const struct query   *query,
                                 ares_dns_record_t    *dnsrec)
{
  return ares__qcache_insert(channel->qcache, dnsrec, query->query,
                             now);
}
