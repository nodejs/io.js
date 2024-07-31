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
#include <limits.h>
#ifdef HAVE_STDINT_H
#  include <stdint.h>
#endif


ares_status_t ares_dns_record_create(ares_dns_record_t **dnsrec,
                                     unsigned short id, unsigned short flags,
                                     ares_dns_opcode_t opcode,
                                     ares_dns_rcode_t  rcode)
{
  if (dnsrec == NULL) {
    return ARES_EFORMERR;
  }

  *dnsrec = NULL;

  if (!ares_dns_opcode_isvalid(opcode) || !ares_dns_rcode_isvalid(rcode) ||
      !ares_dns_flags_arevalid(flags)) {
    return ARES_EFORMERR;
  }

  *dnsrec = ares_malloc_zero(sizeof(**dnsrec));
  if (*dnsrec == NULL) {
    return ARES_ENOMEM;
  }

  (*dnsrec)->id     = id;
  (*dnsrec)->flags  = flags;
  (*dnsrec)->opcode = opcode;
  (*dnsrec)->rcode  = rcode;
  return ARES_SUCCESS;
}

unsigned short ares_dns_record_get_id(const ares_dns_record_t *dnsrec)
{
  if (dnsrec == NULL) {
    return 0;
  }
  return dnsrec->id;
}

ares_bool_t ares_dns_record_set_id(ares_dns_record_t *dnsrec, unsigned short id)
{
  if (dnsrec == NULL) {
    return ARES_FALSE;
  }
  dnsrec->id = id;
  return ARES_TRUE;
}

unsigned short ares_dns_record_get_flags(const ares_dns_record_t *dnsrec)
{
  if (dnsrec == NULL) {
    return 0;
  }
  return dnsrec->flags;
}

ares_dns_opcode_t ares_dns_record_get_opcode(const ares_dns_record_t *dnsrec)
{
  if (dnsrec == NULL) {
    return 0;
  }
  return dnsrec->opcode;
}

ares_dns_rcode_t ares_dns_record_get_rcode(const ares_dns_record_t *dnsrec)
{
  if (dnsrec == NULL) {
    return 0;
  }
  return dnsrec->rcode;
}

static void ares__dns_options_free(ares__dns_options_t *options)
{
  size_t i;

  if (options == NULL) {
    return;
  }

  for (i = 0; i < options->cnt; i++) {
    ares_free(options->optval[i].val);
  }
  ares_free(options->optval);
  ares_free(options);
}

static void ares__dns_rr_free(ares_dns_rr_t *rr)
{
  ares_free(rr->name);

  switch (rr->type) {
    case ARES_REC_TYPE_A:
    case ARES_REC_TYPE_AAAA:
    case ARES_REC_TYPE_ANY:
      /* Nothing to free */
      break;

    case ARES_REC_TYPE_NS:
      ares_free(rr->r.ns.nsdname);
      break;

    case ARES_REC_TYPE_CNAME:
      ares_free(rr->r.cname.cname);
      break;

    case ARES_REC_TYPE_SOA:
      ares_free(rr->r.soa.mname);
      ares_free(rr->r.soa.rname);
      break;

    case ARES_REC_TYPE_PTR:
      ares_free(rr->r.ptr.dname);
      break;

    case ARES_REC_TYPE_HINFO:
      ares_free(rr->r.hinfo.cpu);
      ares_free(rr->r.hinfo.os);
      break;

    case ARES_REC_TYPE_MX:
      ares_free(rr->r.mx.exchange);
      break;

    case ARES_REC_TYPE_TXT:
      ares__dns_multistring_destroy(rr->r.txt.strs);
      break;

    case ARES_REC_TYPE_SIG:
      ares_free(rr->r.sig.signers_name);
      ares_free(rr->r.sig.signature);
      break;

    case ARES_REC_TYPE_SRV:
      ares_free(rr->r.srv.target);
      break;

    case ARES_REC_TYPE_NAPTR:
      ares_free(rr->r.naptr.flags);
      ares_free(rr->r.naptr.services);
      ares_free(rr->r.naptr.regexp);
      ares_free(rr->r.naptr.replacement);
      break;

    case ARES_REC_TYPE_OPT:
      ares__dns_options_free(rr->r.opt.options);
      break;

    case ARES_REC_TYPE_TLSA:
      ares_free(rr->r.tlsa.data);
      break;

    case ARES_REC_TYPE_SVCB:
      ares_free(rr->r.svcb.target);
      ares__dns_options_free(rr->r.svcb.params);
      break;

    case ARES_REC_TYPE_HTTPS:
      ares_free(rr->r.https.target);
      ares__dns_options_free(rr->r.https.params);
      break;

    case ARES_REC_TYPE_URI:
      ares_free(rr->r.uri.target);
      break;

    case ARES_REC_TYPE_CAA:
      ares_free(rr->r.caa.tag);
      ares_free(rr->r.caa.value);
      break;

    case ARES_REC_TYPE_RAW_RR:
      ares_free(rr->r.raw_rr.data);
      break;
  }
}

void ares_dns_record_destroy(ares_dns_record_t *dnsrec)
{
  size_t i;

  if (dnsrec == NULL) {
    return;
  }

  /* Free questions */
  for (i = 0; i < dnsrec->qdcount; i++) {
    ares_free(dnsrec->qd[i].name);
  }
  ares_free(dnsrec->qd);

  /* Free answers */
  for (i = 0; i < dnsrec->ancount; i++) {
    ares__dns_rr_free(&dnsrec->an[i]);
  }
  ares_free(dnsrec->an);

  /* Free authority */
  for (i = 0; i < dnsrec->nscount; i++) {
    ares__dns_rr_free(&dnsrec->ns[i]);
  }
  ares_free(dnsrec->ns);

  /* Free additional */
  for (i = 0; i < dnsrec->arcount; i++) {
    ares__dns_rr_free(&dnsrec->ar[i]);
  }
  ares_free(dnsrec->ar);

  ares_free(dnsrec);
}

size_t ares_dns_record_query_cnt(const ares_dns_record_t *dnsrec)
{
  if (dnsrec == NULL) {
    return 0;
  }
  return dnsrec->qdcount;
}

ares_status_t ares_dns_record_query_add(ares_dns_record_t  *dnsrec,
                                        const char         *name,
                                        ares_dns_rec_type_t qtype,
                                        ares_dns_class_t    qclass)
{
  ares_dns_qd_t *temp = NULL;
  size_t         idx;

  if (dnsrec == NULL || name == NULL ||
      !ares_dns_rec_type_isvalid(qtype, ARES_TRUE) ||
      !ares_dns_class_isvalid(qclass, qtype, ARES_TRUE)) {
    return ARES_EFORMERR;
  }

  if (dnsrec->qdcount >= dnsrec->qdalloc) {
    size_t alloc_cnt = ares__round_up_pow2(dnsrec->qdcount + 1);

    temp = ares_realloc_zero(dnsrec->qd, sizeof(*temp) * (dnsrec->qdalloc),
                             sizeof(*temp) * alloc_cnt);
    if (temp == NULL) {
      return ARES_ENOMEM;
    }

    dnsrec->qdalloc = alloc_cnt;
    dnsrec->qd      = temp;
  }

  idx = dnsrec->qdcount;

  dnsrec->qd[idx].name = ares_strdup(name);
  if (dnsrec->qd[idx].name == NULL) {
    /* No need to clean up anything */
    return ARES_ENOMEM;
  }

  dnsrec->qd[idx].qtype  = qtype;
  dnsrec->qd[idx].qclass = qclass;
  dnsrec->qdcount++;
  return ARES_SUCCESS;
}

ares_status_t ares_dns_record_query_set_name(ares_dns_record_t *dnsrec,
                                             size_t idx, const char *name)
{
  char *orig_name = NULL;

  if (dnsrec == NULL || idx >= dnsrec->qdcount || name == NULL) {
    return ARES_EFORMERR;
  }

  orig_name            = dnsrec->qd[idx].name;
  dnsrec->qd[idx].name = ares_strdup(name);
  if (dnsrec->qd[idx].name == NULL) {
    dnsrec->qd[idx].name = orig_name; /* LCOV_EXCL_LINE: OutOfMemory */
    return ARES_ENOMEM;               /* LCOV_EXCL_LINE: OutOfMemory */
  }

  ares_free(orig_name);
  return ARES_SUCCESS;
}

ares_status_t ares_dns_record_query_set_type(ares_dns_record_t  *dnsrec,
                                             size_t              idx,
                                             ares_dns_rec_type_t qtype)
{
  if (dnsrec == NULL || idx >= dnsrec->qdcount ||
      !ares_dns_rec_type_isvalid(qtype, ARES_TRUE)) {
    return ARES_EFORMERR;
  }

  dnsrec->qd[idx].qtype = qtype;

  return ARES_SUCCESS;
}

ares_status_t ares_dns_record_query_get(const ares_dns_record_t *dnsrec,
                                        size_t idx, const char **name,
                                        ares_dns_rec_type_t *qtype,
                                        ares_dns_class_t    *qclass)
{
  if (dnsrec == NULL || idx >= dnsrec->qdcount) {
    return ARES_EFORMERR;
  }

  if (name != NULL) {
    *name = dnsrec->qd[idx].name;
  }

  if (qtype != NULL) {
    *qtype = dnsrec->qd[idx].qtype;
  }

  if (qclass != NULL) {
    *qclass = dnsrec->qd[idx].qclass;
  }

  return ARES_SUCCESS;
}

size_t ares_dns_record_rr_cnt(const ares_dns_record_t *dnsrec,
                              ares_dns_section_t       sect)
{
  if (dnsrec == NULL || !ares_dns_section_isvalid(sect)) {
    return 0;
  }

  switch (sect) {
    case ARES_SECTION_ANSWER:
      return dnsrec->ancount;
    case ARES_SECTION_AUTHORITY:
      return dnsrec->nscount;
    case ARES_SECTION_ADDITIONAL:
      return dnsrec->arcount;
  }

  return 0; /* LCOV_EXCL_LINE: DefensiveCoding */
}

ares_status_t ares_dns_record_rr_prealloc(ares_dns_record_t *dnsrec,
                                          ares_dns_section_t sect, size_t cnt)
{
  ares_dns_rr_t **rr_ptr   = NULL;
  size_t         *rr_alloc = NULL;
  ares_dns_rr_t  *temp     = NULL;

  if (dnsrec == NULL || cnt == 0 || !ares_dns_section_isvalid(sect)) {
    return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  switch (sect) {
    case ARES_SECTION_ANSWER:
      rr_ptr   = &dnsrec->an;
      rr_alloc = &dnsrec->analloc;
      break;
    case ARES_SECTION_AUTHORITY:
      rr_ptr   = &dnsrec->ns;
      rr_alloc = &dnsrec->nsalloc;
      break;
    case ARES_SECTION_ADDITIONAL:
      rr_ptr   = &dnsrec->ar;
      rr_alloc = &dnsrec->aralloc;
      break;
  }

  /* Round up cnt to a power of 2 */
  cnt = ares__round_up_pow2(cnt);

  /* Already have that */
  if (cnt <= *rr_alloc) {
    return ARES_SUCCESS;
  }

  temp = ares_realloc_zero(*rr_ptr, sizeof(*temp) * (*rr_alloc),
                           sizeof(*temp) * cnt);
  if (temp == NULL) {
    return ARES_ENOMEM;
  }

  *rr_alloc = cnt;
  *rr_ptr   = temp;
  return ARES_SUCCESS;
}

ares_status_t ares_dns_record_rr_add(ares_dns_rr_t    **rr_out,
                                     ares_dns_record_t *dnsrec,
                                     ares_dns_section_t sect, const char *name,
                                     ares_dns_rec_type_t type,
                                     ares_dns_class_t rclass, unsigned int ttl)
{
  ares_dns_rr_t **rr_ptr = NULL;
  ares_dns_rr_t  *rr     = NULL;
  size_t         *rr_len = NULL;
  ares_status_t   status;
  size_t          idx;

  if (dnsrec == NULL || name == NULL || rr_out == NULL ||
      !ares_dns_section_isvalid(sect) ||
      !ares_dns_rec_type_isvalid(type, ARES_FALSE) ||
      !ares_dns_class_isvalid(rclass, type, ARES_FALSE)) {
    return ARES_EFORMERR;
  }

  *rr_out = NULL;

  switch (sect) {
    case ARES_SECTION_ANSWER:
      rr_ptr = &dnsrec->an;
      rr_len = &dnsrec->ancount;
      break;
    case ARES_SECTION_AUTHORITY:
      rr_ptr = &dnsrec->ns;
      rr_len = &dnsrec->nscount;
      break;
    case ARES_SECTION_ADDITIONAL:
      rr_ptr = &dnsrec->ar;
      rr_len = &dnsrec->arcount;
      break;
  }

  status = ares_dns_record_rr_prealloc(dnsrec, sect, *rr_len + 1);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  idx = *rr_len;
  rr  = &(*rr_ptr)[idx];

  rr->name = ares_strdup(name);
  if (rr->name == NULL) {
    /* No need to clean up anything */
    return ARES_ENOMEM;
  }

  rr->parent = dnsrec;
  rr->type   = type;
  rr->rclass = rclass;
  rr->ttl    = ttl;
  (*rr_len)++;

  *rr_out = rr;

  return ARES_SUCCESS;
}

ares_status_t ares_dns_record_rr_del(ares_dns_record_t *dnsrec,
                                     ares_dns_section_t sect, size_t idx)
{
  ares_dns_rr_t *rr_ptr = NULL;
  size_t        *rr_len = NULL;
  size_t         cnt_after;

  if (dnsrec == NULL || !ares_dns_section_isvalid(sect)) {
    return ARES_EFORMERR;
  }

  switch (sect) {
    case ARES_SECTION_ANSWER:
      rr_ptr = dnsrec->an;
      rr_len = &dnsrec->ancount;
      break;
    case ARES_SECTION_AUTHORITY:
      rr_ptr = dnsrec->ns;
      rr_len = &dnsrec->nscount;
      break;
    case ARES_SECTION_ADDITIONAL:
      rr_ptr = dnsrec->ar;
      rr_len = &dnsrec->arcount;
      break;
  }

  if (idx >= *rr_len) {
    return ARES_EFORMERR;
  }

  ares__dns_rr_free(&rr_ptr[idx]);

  cnt_after = *rr_len - idx - 1;

  if (cnt_after) {
    memmove(&rr_ptr[idx], &rr_ptr[idx + 1], sizeof(*rr_ptr) * cnt_after);
  }

  (*rr_len)--;
  return ARES_SUCCESS;
}

ares_dns_rr_t *ares_dns_record_rr_get(ares_dns_record_t *dnsrec,
                                      ares_dns_section_t sect, size_t idx)
{
  ares_dns_rr_t *rr_ptr = NULL;
  size_t         rr_len = 0;

  if (dnsrec == NULL || !ares_dns_section_isvalid(sect)) {
    return NULL;
  }

  switch (sect) {
    case ARES_SECTION_ANSWER:
      rr_ptr = dnsrec->an;
      rr_len = dnsrec->ancount;
      break;
    case ARES_SECTION_AUTHORITY:
      rr_ptr = dnsrec->ns;
      rr_len = dnsrec->nscount;
      break;
    case ARES_SECTION_ADDITIONAL:
      rr_ptr = dnsrec->ar;
      rr_len = dnsrec->arcount;
      break;
  }

  if (idx >= rr_len) {
    return NULL;
  }

  return &rr_ptr[idx];
}

const ares_dns_rr_t *
  ares_dns_record_rr_get_const(const ares_dns_record_t *dnsrec,
                               ares_dns_section_t sect, size_t idx)
{
  return ares_dns_record_rr_get((void *)((size_t)dnsrec), sect, idx);
}

const char *ares_dns_rr_get_name(const ares_dns_rr_t *rr)
{
  if (rr == NULL) {
    return NULL;
  }
  return rr->name;
}

ares_dns_rec_type_t ares_dns_rr_get_type(const ares_dns_rr_t *rr)
{
  if (rr == NULL) {
    return 0;
  }
  return rr->type;
}

ares_dns_class_t ares_dns_rr_get_class(const ares_dns_rr_t *rr)
{
  if (rr == NULL) {
    return 0;
  }
  return rr->rclass;
}

unsigned int ares_dns_rr_get_ttl(const ares_dns_rr_t *rr)
{
  if (rr == NULL) {
    return 0;
  }
  return rr->ttl;
}

static void *ares_dns_rr_data_ptr(ares_dns_rr_t *dns_rr, ares_dns_rr_key_t key,
                                  size_t **lenptr)
{
  if (dns_rr == NULL || dns_rr->type != ares_dns_rr_key_to_rec_type(key)) {
    return NULL; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  switch (key) {
    case ARES_RR_A_ADDR:
      return &dns_rr->r.a.addr;

    case ARES_RR_NS_NSDNAME:
      return &dns_rr->r.ns.nsdname;

    case ARES_RR_CNAME_CNAME:
      return &dns_rr->r.cname.cname;

    case ARES_RR_SOA_MNAME:
      return &dns_rr->r.soa.mname;

    case ARES_RR_SOA_RNAME:
      return &dns_rr->r.soa.rname;

    case ARES_RR_SOA_SERIAL:
      return &dns_rr->r.soa.serial;

    case ARES_RR_SOA_REFRESH:
      return &dns_rr->r.soa.refresh;

    case ARES_RR_SOA_RETRY:
      return &dns_rr->r.soa.retry;

    case ARES_RR_SOA_EXPIRE:
      return &dns_rr->r.soa.expire;

    case ARES_RR_SOA_MINIMUM:
      return &dns_rr->r.soa.minimum;

    case ARES_RR_PTR_DNAME:
      return &dns_rr->r.ptr.dname;

    case ARES_RR_AAAA_ADDR:
      return &dns_rr->r.aaaa.addr;

    case ARES_RR_HINFO_CPU:
      return &dns_rr->r.hinfo.cpu;

    case ARES_RR_HINFO_OS:
      return &dns_rr->r.hinfo.os;

    case ARES_RR_MX_PREFERENCE:
      return &dns_rr->r.mx.preference;

    case ARES_RR_MX_EXCHANGE:
      return &dns_rr->r.mx.exchange;

    case ARES_RR_SIG_TYPE_COVERED:
      return &dns_rr->r.sig.type_covered;

    case ARES_RR_SIG_ALGORITHM:
      return &dns_rr->r.sig.algorithm;

    case ARES_RR_SIG_LABELS:
      return &dns_rr->r.sig.labels;

    case ARES_RR_SIG_ORIGINAL_TTL:
      return &dns_rr->r.sig.original_ttl;

    case ARES_RR_SIG_EXPIRATION:
      return &dns_rr->r.sig.expiration;

    case ARES_RR_SIG_INCEPTION:
      return &dns_rr->r.sig.inception;

    case ARES_RR_SIG_KEY_TAG:
      return &dns_rr->r.sig.key_tag;

    case ARES_RR_SIG_SIGNERS_NAME:
      return &dns_rr->r.sig.signers_name;

    case ARES_RR_SIG_SIGNATURE:
      if (lenptr == NULL) {
        return NULL;
      }
      *lenptr = &dns_rr->r.sig.signature_len;
      return &dns_rr->r.sig.signature;

    case ARES_RR_TXT_DATA:
      return &dns_rr->r.txt.strs;

    case ARES_RR_SRV_PRIORITY:
      return &dns_rr->r.srv.priority;

    case ARES_RR_SRV_WEIGHT:
      return &dns_rr->r.srv.weight;

    case ARES_RR_SRV_PORT:
      return &dns_rr->r.srv.port;

    case ARES_RR_SRV_TARGET:
      return &dns_rr->r.srv.target;

    case ARES_RR_NAPTR_ORDER:
      return &dns_rr->r.naptr.order;

    case ARES_RR_NAPTR_PREFERENCE:
      return &dns_rr->r.naptr.preference;

    case ARES_RR_NAPTR_FLAGS:
      return &dns_rr->r.naptr.flags;

    case ARES_RR_NAPTR_SERVICES:
      return &dns_rr->r.naptr.services;

    case ARES_RR_NAPTR_REGEXP:
      return &dns_rr->r.naptr.regexp;

    case ARES_RR_NAPTR_REPLACEMENT:
      return &dns_rr->r.naptr.replacement;

    case ARES_RR_OPT_UDP_SIZE:
      return &dns_rr->r.opt.udp_size;

    case ARES_RR_OPT_VERSION:
      return &dns_rr->r.opt.version;

    case ARES_RR_OPT_FLAGS:
      return &dns_rr->r.opt.flags;

    case ARES_RR_OPT_OPTIONS:
      return &dns_rr->r.opt.options;

    case ARES_RR_TLSA_CERT_USAGE:
      return &dns_rr->r.tlsa.cert_usage;

    case ARES_RR_TLSA_SELECTOR:
      return &dns_rr->r.tlsa.selector;

    case ARES_RR_TLSA_MATCH:
      return &dns_rr->r.tlsa.match;

    case ARES_RR_TLSA_DATA:
      if (lenptr == NULL) {
        return NULL;
      }
      *lenptr = &dns_rr->r.tlsa.data_len;
      return &dns_rr->r.tlsa.data;

    case ARES_RR_SVCB_PRIORITY:
      return &dns_rr->r.svcb.priority;

    case ARES_RR_SVCB_TARGET:
      return &dns_rr->r.svcb.target;

    case ARES_RR_SVCB_PARAMS:
      return &dns_rr->r.svcb.params;

    case ARES_RR_HTTPS_PRIORITY:
      return &dns_rr->r.https.priority;

    case ARES_RR_HTTPS_TARGET:
      return &dns_rr->r.https.target;

    case ARES_RR_HTTPS_PARAMS:
      return &dns_rr->r.https.params;

    case ARES_RR_URI_PRIORITY:
      return &dns_rr->r.uri.priority;

    case ARES_RR_URI_WEIGHT:
      return &dns_rr->r.uri.weight;

    case ARES_RR_URI_TARGET:
      return &dns_rr->r.uri.target;

    case ARES_RR_CAA_CRITICAL:
      return &dns_rr->r.caa.critical;

    case ARES_RR_CAA_TAG:
      return &dns_rr->r.caa.tag;

    case ARES_RR_CAA_VALUE:
      if (lenptr == NULL) {
        return NULL;
      }
      *lenptr = &dns_rr->r.caa.value_len;
      return &dns_rr->r.caa.value;

    case ARES_RR_RAW_RR_TYPE:
      return &dns_rr->r.raw_rr.type;

    case ARES_RR_RAW_RR_DATA:
      if (lenptr == NULL) {
        return NULL;
      }
      *lenptr = &dns_rr->r.raw_rr.length;
      return &dns_rr->r.raw_rr.data;
  }

  return NULL;
}

static const void *ares_dns_rr_data_ptr_const(const ares_dns_rr_t *dns_rr,
                                              ares_dns_rr_key_t    key,
                                              const size_t       **lenptr)
{
  /* We're going to cast off the const */
  return ares_dns_rr_data_ptr((void *)((size_t)dns_rr), key,
                              (void *)((size_t)lenptr));
}

const struct in_addr *ares_dns_rr_get_addr(const ares_dns_rr_t *dns_rr,
                                           ares_dns_rr_key_t    key)
{
  const struct in_addr *addr;

  if (ares_dns_rr_key_datatype(key) != ARES_DATATYPE_INADDR) {
    return NULL;
  }

  addr = ares_dns_rr_data_ptr_const(dns_rr, key, NULL);
  if (addr == NULL) {
    return NULL;
  }

  return addr;
}

const struct ares_in6_addr *ares_dns_rr_get_addr6(const ares_dns_rr_t *dns_rr,
                                                  ares_dns_rr_key_t    key)
{
  const struct ares_in6_addr *addr;

  if (ares_dns_rr_key_datatype(key) != ARES_DATATYPE_INADDR6) {
    return NULL;
  }

  addr = ares_dns_rr_data_ptr_const(dns_rr, key, NULL);
  if (addr == NULL) {
    return NULL;
  }

  return addr;
}

unsigned char ares_dns_rr_get_u8(const ares_dns_rr_t *dns_rr,
                                 ares_dns_rr_key_t    key)
{
  const unsigned char *u8;

  if (ares_dns_rr_key_datatype(key) != ARES_DATATYPE_U8) {
    return 0;
  }

  u8 = ares_dns_rr_data_ptr_const(dns_rr, key, NULL);
  if (u8 == NULL) {
    return 0;
  }

  return *u8;
}

unsigned short ares_dns_rr_get_u16(const ares_dns_rr_t *dns_rr,
                                   ares_dns_rr_key_t    key)
{
  const unsigned short *u16;

  if (ares_dns_rr_key_datatype(key) != ARES_DATATYPE_U16) {
    return 0;
  }

  u16 = ares_dns_rr_data_ptr_const(dns_rr, key, NULL);
  if (u16 == NULL) {
    return 0;
  }

  return *u16;
}

unsigned int ares_dns_rr_get_u32(const ares_dns_rr_t *dns_rr,
                                 ares_dns_rr_key_t    key)
{
  const unsigned int *u32;

  if (ares_dns_rr_key_datatype(key) != ARES_DATATYPE_U32) {
    return 0;
  }

  u32 = ares_dns_rr_data_ptr_const(dns_rr, key, NULL);
  if (u32 == NULL) {
    return 0;
  }

  return *u32;
}

const unsigned char *ares_dns_rr_get_bin(const ares_dns_rr_t *dns_rr,
                                         ares_dns_rr_key_t key, size_t *len)
{
  unsigned char * const *bin     = NULL;
  size_t const          *bin_len = NULL;

  if ((ares_dns_rr_key_datatype(key) != ARES_DATATYPE_BIN &&
       ares_dns_rr_key_datatype(key) != ARES_DATATYPE_BINP &&
       ares_dns_rr_key_datatype(key) != ARES_DATATYPE_ABINP) ||
      len == NULL) {
    return NULL;
  }

  /* Array of strings, return concatenated version */
  if (ares_dns_rr_key_datatype(key) == ARES_DATATYPE_ABINP) {
    ares__dns_multistring_t * const *strs =
      ares_dns_rr_data_ptr_const(dns_rr, key, NULL);

    if (strs == NULL) {
      return NULL;
    }

    return ares__dns_multistring_get_combined(*strs, len);
  }

  /* Not a multi-string, just straight binary data */
  bin = ares_dns_rr_data_ptr_const(dns_rr, key, &bin_len);
  if (bin == NULL) {
    return NULL;
  }

  /* Shouldn't be possible */
  if (bin_len == NULL) {
    return NULL;
  }
  *len = *bin_len;

  return *bin;
}

size_t ares_dns_rr_get_abin_cnt(const ares_dns_rr_t *dns_rr,
                                ares_dns_rr_key_t    key)
{
  ares__dns_multistring_t * const *strs;

  if (ares_dns_rr_key_datatype(key) != ARES_DATATYPE_ABINP) {
    return 0;
  }

  strs = ares_dns_rr_data_ptr_const(dns_rr, key, NULL);
  if (strs == NULL) {
    return 0;
  }

  return ares__dns_multistring_cnt(*strs);
}

const unsigned char *ares_dns_rr_get_abin(const ares_dns_rr_t *dns_rr,
                                          ares_dns_rr_key_t key, size_t idx,
                                          size_t *len)
{
  ares__dns_multistring_t * const *strs;

  if (ares_dns_rr_key_datatype(key) != ARES_DATATYPE_ABINP) {
    return NULL;
  }

  strs = ares_dns_rr_data_ptr_const(dns_rr, key, NULL);
  if (strs == NULL) {
    return NULL;
  }

  return ares__dns_multistring_get(*strs, idx, len);
}

ares_status_t ares_dns_rr_del_abin(ares_dns_rr_t *dns_rr, ares_dns_rr_key_t key,
                                   size_t idx)
{
  ares__dns_multistring_t **strs;

  if (ares_dns_rr_key_datatype(key) != ARES_DATATYPE_ABINP) {
    return ARES_EFORMERR;
  }

  strs = ares_dns_rr_data_ptr(dns_rr, key, NULL);
  if (strs == NULL) {
    return ARES_EFORMERR;
  }

  return ares__dns_multistring_del(*strs, idx);
}

ares_status_t ares_dns_rr_add_abin(ares_dns_rr_t *dns_rr, ares_dns_rr_key_t key,
                                   const unsigned char *val, size_t len)
{
  ares_status_t       status;
  ares_dns_datatype_t datatype = ares_dns_rr_key_datatype(key);
  ares_bool_t         is_nullterm =
    (datatype == ARES_DATATYPE_ABINP) ? ARES_TRUE : ARES_FALSE;
  size_t                    alloclen = is_nullterm ? len + 1 : len;
  unsigned char            *temp;
  ares__dns_multistring_t **strs;

  if (ares_dns_rr_key_datatype(key) != ARES_DATATYPE_ABINP) {
    return ARES_EFORMERR;
  }

  strs = ares_dns_rr_data_ptr(dns_rr, key, NULL);
  if (strs == NULL) {
    return ARES_EFORMERR;
  }

  if (*strs == NULL) {
    *strs = ares__dns_multistring_create();
    if (*strs == NULL) {
      return ARES_ENOMEM;
    }
  }

  temp = ares_malloc(alloclen);
  if (temp == NULL) {
    return ARES_ENOMEM;
  }

  memcpy(temp, val, len);

  /* NULL-term ABINP */
  if (is_nullterm) {
    temp[len] = 0;
  }

  status = ares__dns_multistring_add_own(*strs, temp, len);
  if (status != ARES_SUCCESS) {
    ares_free(temp);
  }

  return status;
}

const char *ares_dns_rr_get_str(const ares_dns_rr_t *dns_rr,
                                ares_dns_rr_key_t    key)
{
  char * const *str;

  if (ares_dns_rr_key_datatype(key) != ARES_DATATYPE_STR &&
      ares_dns_rr_key_datatype(key) != ARES_DATATYPE_NAME) {
    return NULL;
  }

  str = ares_dns_rr_data_ptr_const(dns_rr, key, NULL);
  if (str == NULL) {
    return NULL;
  }

  return *str;
}

size_t ares_dns_rr_get_opt_cnt(const ares_dns_rr_t *dns_rr,
                               ares_dns_rr_key_t    key)
{
  ares__dns_options_t * const *opts;

  if (ares_dns_rr_key_datatype(key) != ARES_DATATYPE_OPT) {
    return 0;
  }

  opts = ares_dns_rr_data_ptr_const(dns_rr, key, NULL);
  if (opts == NULL || *opts == NULL) {
    return 0;
  }

  return (*opts)->cnt;
}

unsigned short ares_dns_rr_get_opt(const ares_dns_rr_t *dns_rr,
                                   ares_dns_rr_key_t key, size_t idx,
                                   const unsigned char **val, size_t *val_len)
{
  ares__dns_options_t * const *opts;

  if (val) {
    *val = NULL;
  }
  if (val_len) {
    *val_len = 0;
  }

  if (ares_dns_rr_key_datatype(key) != ARES_DATATYPE_OPT) {
    return 65535;
  }

  opts = ares_dns_rr_data_ptr_const(dns_rr, key, NULL);
  if (opts == NULL || *opts == NULL) {
    return 65535;
  }

  if (idx >= (*opts)->cnt) {
    return 65535;
  }

  if (val) {
    *val = (*opts)->optval[idx].val;
  }
  if (val_len) {
    *val_len = (*opts)->optval[idx].val_len;
  }

  return (*opts)->optval[idx].opt;
}

ares_bool_t ares_dns_rr_get_opt_byid(const ares_dns_rr_t *dns_rr,
                                     ares_dns_rr_key_t key, unsigned short opt,
                                     const unsigned char **val, size_t *val_len)
{
  ares__dns_options_t * const *opts;
  size_t                       i;

  if (val) {
    *val = NULL;
  }
  if (val_len) {
    *val_len = 0;
  }

  if (ares_dns_rr_key_datatype(key) != ARES_DATATYPE_OPT) {
    return ARES_FALSE;
  }

  opts = ares_dns_rr_data_ptr_const(dns_rr, key, NULL);
  if (opts == NULL || *opts == NULL) {
    return ARES_FALSE;
  }

  for (i = 0; i < (*opts)->cnt; i++) {
    if ((*opts)->optval[i].opt == opt) {
      break;
    }
  }

  if (i >= (*opts)->cnt) {
    return ARES_FALSE;
  }

  if (val) {
    *val = (*opts)->optval[i].val;
  }
  if (val_len) {
    *val_len = (*opts)->optval[i].val_len;
  }
  return ARES_TRUE;
}

ares_status_t ares_dns_rr_set_addr(ares_dns_rr_t *dns_rr, ares_dns_rr_key_t key,
                                   const struct in_addr *addr)
{
  struct in_addr *a;

  if (ares_dns_rr_key_datatype(key) != ARES_DATATYPE_INADDR || addr == NULL) {
    return ARES_EFORMERR;
  }

  a = ares_dns_rr_data_ptr(dns_rr, key, NULL);
  if (a == NULL) {
    return ARES_EFORMERR;
  }

  memcpy(a, addr, sizeof(*a));
  return ARES_SUCCESS;
}

ares_status_t ares_dns_rr_set_addr6(ares_dns_rr_t              *dns_rr,
                                    ares_dns_rr_key_t           key,
                                    const struct ares_in6_addr *addr)
{
  struct ares_in6_addr *a;

  if (ares_dns_rr_key_datatype(key) != ARES_DATATYPE_INADDR6 || addr == NULL) {
    return ARES_EFORMERR;
  }

  a = ares_dns_rr_data_ptr(dns_rr, key, NULL);
  if (a == NULL) {
    return ARES_EFORMERR;
  }

  memcpy(a, addr, sizeof(*a));
  return ARES_SUCCESS;
}

ares_status_t ares_dns_rr_set_u8(ares_dns_rr_t *dns_rr, ares_dns_rr_key_t key,
                                 unsigned char val)
{
  unsigned char *u8;

  if (ares_dns_rr_key_datatype(key) != ARES_DATATYPE_U8) {
    return ARES_EFORMERR;
  }

  u8 = ares_dns_rr_data_ptr(dns_rr, key, NULL);
  if (u8 == NULL) {
    return ARES_EFORMERR;
  }

  *u8 = val;
  return ARES_SUCCESS;
}

ares_status_t ares_dns_rr_set_u16(ares_dns_rr_t *dns_rr, ares_dns_rr_key_t key,
                                  unsigned short val)
{
  unsigned short *u16;

  if (ares_dns_rr_key_datatype(key) != ARES_DATATYPE_U16) {
    return ARES_EFORMERR;
  }

  u16 = ares_dns_rr_data_ptr(dns_rr, key, NULL);
  if (u16 == NULL) {
    return ARES_EFORMERR;
  }

  *u16 = val;
  return ARES_SUCCESS;
}

ares_status_t ares_dns_rr_set_u32(ares_dns_rr_t *dns_rr, ares_dns_rr_key_t key,
                                  unsigned int val)
{
  unsigned int *u32;

  if (ares_dns_rr_key_datatype(key) != ARES_DATATYPE_U32) {
    return ARES_EFORMERR;
  }

  u32 = ares_dns_rr_data_ptr(dns_rr, key, NULL);
  if (u32 == NULL) {
    return ARES_EFORMERR;
  }

  *u32 = val;
  return ARES_SUCCESS;
}

ares_status_t ares_dns_rr_set_bin_own(ares_dns_rr_t    *dns_rr,
                                      ares_dns_rr_key_t key, unsigned char *val,
                                      size_t len)
{
  unsigned char **bin;
  size_t         *bin_len = NULL;

  if (ares_dns_rr_key_datatype(key) != ARES_DATATYPE_BIN &&
      ares_dns_rr_key_datatype(key) != ARES_DATATYPE_BINP &&
      ares_dns_rr_key_datatype(key) != ARES_DATATYPE_ABINP) {
    return ARES_EFORMERR;
  }

  if (ares_dns_rr_key_datatype(key) == ARES_DATATYPE_ABINP) {
    ares__dns_multistring_t **strs = ares_dns_rr_data_ptr(dns_rr, key, NULL);
    if (strs == NULL) {
      return ARES_EFORMERR;
    }

    if (*strs == NULL) {
      *strs = ares__dns_multistring_create();
      if (*strs == NULL) {
        return ARES_ENOMEM;
      }
    }

    /* Clear all existing entries as this is an override */
    ares__dns_multistring_clear(*strs);

    return ares__dns_multistring_add_own(*strs, val, len);
  }

  bin = ares_dns_rr_data_ptr(dns_rr, key, &bin_len);
  if (bin == NULL || bin_len == NULL) {
    return ARES_EFORMERR;
  }

  if (*bin) {
    ares_free(*bin);
  }
  *bin     = val;
  *bin_len = len;

  return ARES_SUCCESS;
}

ares_status_t ares_dns_rr_set_bin(ares_dns_rr_t *dns_rr, ares_dns_rr_key_t key,
                                  const unsigned char *val, size_t len)
{
  ares_status_t       status;
  ares_dns_datatype_t datatype = ares_dns_rr_key_datatype(key);
  ares_bool_t         is_nullterm =
    (datatype == ARES_DATATYPE_BINP || datatype == ARES_DATATYPE_ABINP)
              ? ARES_TRUE
              : ARES_FALSE;
  size_t         alloclen = is_nullterm ? len + 1 : len;
  unsigned char *temp     = ares_malloc(alloclen);

  if (temp == NULL) {
    return ARES_ENOMEM;
  }

  memcpy(temp, val, len);

  /* NULL-term BINP */
  if (is_nullterm) {
    temp[len] = 0;
  }

  status = ares_dns_rr_set_bin_own(dns_rr, key, temp, len);
  if (status != ARES_SUCCESS) {
    ares_free(temp);
  }

  return status;
}

ares_status_t ares_dns_rr_set_str_own(ares_dns_rr_t    *dns_rr,
                                      ares_dns_rr_key_t key, char *val)
{
  char **str;

  if (ares_dns_rr_key_datatype(key) != ARES_DATATYPE_STR &&
      ares_dns_rr_key_datatype(key) != ARES_DATATYPE_NAME) {
    return ARES_EFORMERR;
  }

  str = ares_dns_rr_data_ptr(dns_rr, key, NULL);
  if (str == NULL) {
    return ARES_EFORMERR;
  }

  if (*str) {
    ares_free(*str);
  }
  *str = val;

  return ARES_SUCCESS;
}

ares_status_t ares_dns_rr_set_str(ares_dns_rr_t *dns_rr, ares_dns_rr_key_t key,
                                  const char *val)
{
  ares_status_t status;
  char         *temp = NULL;

  if (val != NULL) {
    temp = ares_strdup(val);
    if (temp == NULL) {
      return ARES_ENOMEM;
    }
  }

  status = ares_dns_rr_set_str_own(dns_rr, key, temp);
  if (status != ARES_SUCCESS) {
    ares_free(temp);
  }

  return status;
}

ares_status_t ares_dns_rr_set_abin_own(ares_dns_rr_t           *dns_rr,
                                       ares_dns_rr_key_t        key,
                                       ares__dns_multistring_t *strs)
{
  ares__dns_multistring_t **strs_ptr;

  if (ares_dns_rr_key_datatype(key) != ARES_DATATYPE_ABINP) {
    return ARES_EFORMERR;
  }

  strs_ptr = ares_dns_rr_data_ptr(dns_rr, key, NULL);
  if (strs_ptr == NULL) {
    return ARES_EFORMERR;
  }

  if (*strs_ptr != NULL) {
    ares__dns_multistring_destroy(*strs_ptr);
  }
  *strs_ptr = strs;

  return ARES_SUCCESS;
}

ares_status_t ares_dns_rr_set_opt_own(ares_dns_rr_t    *dns_rr,
                                      ares_dns_rr_key_t key, unsigned short opt,
                                      unsigned char *val, size_t val_len)
{
  ares__dns_options_t **options;
  size_t                idx;

  if (ares_dns_rr_key_datatype(key) != ARES_DATATYPE_OPT) {
    return ARES_EFORMERR;
  }

  options = ares_dns_rr_data_ptr(dns_rr, key, NULL);
  if (options == NULL) {
    return ARES_EFORMERR;
  }

  if (*options == NULL) {
    *options = ares_malloc_zero(sizeof(**options));
  }
  if (*options == NULL) {
    return ARES_ENOMEM;
  }

  for (idx = 0; idx < (*options)->cnt; idx++) {
    if ((*options)->optval[idx].opt == opt) {
      break;
    }
  }

  /* Duplicate entry, replace */
  if (idx != (*options)->cnt) {
    goto done;
  }

  idx = (*options)->cnt;

  /* Expand by powers of 2 */
  if (idx >= (*options)->alloc) {
    size_t alloc_size = (*options)->alloc;
    void  *temp;

    if (alloc_size == 0) {
      alloc_size = 1;
    } else {
      alloc_size <<= 1;
    }

    temp = ares_realloc_zero((*options)->optval,
                             (*options)->alloc * sizeof(*(*options)->optval),
                             alloc_size * sizeof(*(*options)->optval));
    if (temp == NULL) {
      return ARES_ENOMEM;
    }

    (*options)->optval = temp;
    (*options)->alloc  = alloc_size;
  }

  (*options)->cnt++;

done:
  ares_free((*options)->optval[idx].val);
  (*options)->optval[idx].opt     = opt;
  (*options)->optval[idx].val     = val;
  (*options)->optval[idx].val_len = val_len;

  return ARES_SUCCESS;
}

ares_status_t ares_dns_rr_set_opt(ares_dns_rr_t *dns_rr, ares_dns_rr_key_t key,
                                  unsigned short opt, const unsigned char *val,
                                  size_t val_len)
{
  unsigned char *temp = NULL;
  ares_status_t  status;

  if (val != NULL) {
    temp = ares_malloc(val_len + 1);
    if (temp == NULL) {
      return ARES_ENOMEM;
    }
    memcpy(temp, val, val_len);
    temp[val_len] = 0;
  }

  status = ares_dns_rr_set_opt_own(dns_rr, key, opt, temp, val_len);
  if (status != ARES_SUCCESS) {
    ares_free(temp);
  }

  return status;
}

char *ares_dns_addr_to_ptr(const struct ares_addr *addr)
{
  ares__buf_t               *buf     = NULL;
  const unsigned char       *ptr     = NULL;
  size_t                     ptr_len = 0;
  size_t                     i;
  ares_status_t              status;
  static const unsigned char hexbytes[] = "0123456789abcdef";

  if (addr->family != AF_INET && addr->family != AF_INET6) {
    goto fail;
  }

  buf = ares__buf_create();
  if (buf == NULL) {
    goto fail;
  }

  if (addr->family == AF_INET) {
    ptr     = (const unsigned char *)&addr->addr.addr4;
    ptr_len = 4;
  } else {
    ptr     = (const unsigned char *)&addr->addr.addr6;
    ptr_len = 16;
  }

  for (i = ptr_len; i > 0; i--) {
    if (addr->family == AF_INET) {
      status = ares__buf_append_num_dec(buf, (size_t)ptr[i - 1], 0);
    } else {
      unsigned char c;

      c      = ptr[i - 1] & 0xF;
      status = ares__buf_append_byte(buf, hexbytes[c]);
      if (status != ARES_SUCCESS) {
        goto fail;
      }

      status = ares__buf_append_byte(buf, '.');
      if (status != ARES_SUCCESS) {
        goto fail;
      }

      c      = (ptr[i - 1] >> 4) & 0xF;
      status = ares__buf_append_byte(buf, hexbytes[c]);
    }
    if (status != ARES_SUCCESS) {
      goto fail;
    }

    status = ares__buf_append_byte(buf, '.');
    if (status != ARES_SUCCESS) {
      goto fail;
    }
  }

  if (addr->family == AF_INET) {
    status = ares__buf_append(buf, (const unsigned char *)"in-addr.arpa", 12);
  } else {
    status = ares__buf_append(buf, (const unsigned char *)"ip6.arpa", 8);
  }
  if (status != ARES_SUCCESS) {
    goto fail;
  }

  return ares__buf_finish_str(buf, NULL);

fail:
  ares__buf_destroy(buf);
  return NULL;
}

/* search for an OPT RR in the response */
ares_bool_t ares_dns_has_opt_rr(const ares_dns_record_t *rec)
{
  size_t i;
  for (i = 0; i < ares_dns_record_rr_cnt(rec, ARES_SECTION_ADDITIONAL); i++) {
    const ares_dns_rr_t *rr =
      ares_dns_record_rr_get_const(rec, ARES_SECTION_ADDITIONAL, i);

    if (ares_dns_rr_get_type(rr) == ARES_REC_TYPE_OPT) {
      return ARES_TRUE;
    }
  }
  return ARES_FALSE;
}

/* Construct a DNS record for a name with given class and type. Used internally
 * by ares_search() and ares_create_query().
 */
ares_status_t
  ares_dns_record_create_query(ares_dns_record_t **dnsrec, const char *name,
                               ares_dns_class_t    dnsclass,
                               ares_dns_rec_type_t type, unsigned short id,
                               ares_dns_flags_t flags, size_t max_udp_size)
{
  ares_status_t  status;
  ares_dns_rr_t *rr = NULL;

  if (dnsrec == NULL) {
    return ARES_EFORMERR;
  }

  *dnsrec = NULL;

  /* Per RFC 7686, reject queries for ".onion" domain names with NXDOMAIN */
  if (ares__is_onion_domain(name)) {
    status = ARES_ENOTFOUND;
    goto done;
  }

  status = ares_dns_record_create(dnsrec, id, (unsigned short)flags,
                                  ARES_OPCODE_QUERY, ARES_RCODE_NOERROR);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  status = ares_dns_record_query_add(*dnsrec, name, type, dnsclass);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  /* max_udp_size > 0 indicates EDNS, so send OPT RR as an additional record */
  if (max_udp_size > 0) {
    /* max_udp_size must fit into a 16 bit unsigned integer field on the OPT
     * RR, so check here that it fits
     */
    if (max_udp_size > 65535) {
      status = ARES_EFORMERR;
      goto done;
    }

    status = ares_dns_record_rr_add(&rr, *dnsrec, ARES_SECTION_ADDITIONAL, "",
                                    ARES_REC_TYPE_OPT, ARES_CLASS_IN, 0);
    if (status != ARES_SUCCESS) {
      goto done;
    }

    status = ares_dns_rr_set_u16(rr, ARES_RR_OPT_UDP_SIZE,
                                 (unsigned short)max_udp_size);
    if (status != ARES_SUCCESS) {
      goto done;
    }

    status = ares_dns_rr_set_u8(rr, ARES_RR_OPT_VERSION, 0);
    if (status != ARES_SUCCESS) {
      goto done;
    }

    status = ares_dns_rr_set_u16(rr, ARES_RR_OPT_FLAGS, 0);
    if (status != ARES_SUCCESS) {
      goto done;
    }
  }

done:
  if (status != ARES_SUCCESS) {
    ares_dns_record_destroy(*dnsrec);
    *dnsrec = NULL;
  }
  return status;
}

ares_status_t ares_dns_record_duplicate_ex(ares_dns_record_t **dest,
                                           const ares_dns_record_t *src)
{
  unsigned char     *data     = NULL;
  size_t             data_len = 0;
  ares_status_t      status;

  if (dest == NULL || src == NULL) {
    return ARES_EFORMERR;
  }

  *dest = NULL;

  status = ares_dns_write(src, &data, &data_len);
  if (status != ARES_SUCCESS) {
    return status;
  }

  status = ares_dns_parse(data, data_len, 0, dest);
  ares_free(data);

  return status;
}

ares_dns_record_t *ares_dns_record_duplicate(const ares_dns_record_t *dnsrec)
{
  ares_dns_record_t *dest = NULL;

  ares_dns_record_duplicate_ex(&dest, dnsrec);
  return dest;
}
