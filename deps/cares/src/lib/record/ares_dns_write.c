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


static ares_status_t ares_dns_write_header(const ares_dns_record_t *dnsrec,
                                           ares_buf_t              *buf)
{
  unsigned short u16;
  unsigned short opcode;
  unsigned short rcode;

  ares_status_t  status;

  /* ID */
  status = ares_buf_append_be16(buf, dnsrec->id);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* Flags */
  u16 = 0;

  /* QR */
  if (dnsrec->flags & ARES_FLAG_QR) {
    u16 |= 0x8000;
  }

  /* OPCODE */
  opcode   = (unsigned short)(dnsrec->opcode & 0xF);
  opcode <<= 11;
  u16     |= opcode;

  /* AA */
  if (dnsrec->flags & ARES_FLAG_AA) {
    u16 |= 0x400;
  }

  /* TC */
  if (dnsrec->flags & ARES_FLAG_TC) {
    u16 |= 0x200;
  }

  /* RD */
  if (dnsrec->flags & ARES_FLAG_RD) {
    u16 |= 0x100;
  }

  /* RA */
  if (dnsrec->flags & ARES_FLAG_RA) {
    u16 |= 0x80;
  }

  /* Z -- unused */

  /* AD */
  if (dnsrec->flags & ARES_FLAG_AD) {
    u16 |= 0x20;
  }

  /* CD */
  if (dnsrec->flags & ARES_FLAG_CD) {
    u16 |= 0x10;
  }

  /* RCODE */
  if (dnsrec->rcode > 15 && ares_dns_get_opt_rr_const(dnsrec) == NULL) {
    /* Must have OPT RR in order to write extended error codes */
    rcode = ARES_RCODE_SERVFAIL;
  } else {
    rcode = (unsigned short)(dnsrec->rcode & 0xF);
  }
  u16 |= rcode;

  status = ares_buf_append_be16(buf, u16);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* QDCOUNT */
  status = ares_buf_append_be16(
    buf, (unsigned short)ares_dns_record_query_cnt(dnsrec));
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* ANCOUNT */
  status = ares_buf_append_be16(
    buf, (unsigned short)ares_dns_record_rr_cnt(dnsrec, ARES_SECTION_ANSWER));
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* NSCOUNT */
  status = ares_buf_append_be16(buf, (unsigned short)ares_dns_record_rr_cnt(
                                       dnsrec, ARES_SECTION_AUTHORITY));
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* ARCOUNT */
  status = ares_buf_append_be16(buf, (unsigned short)ares_dns_record_rr_cnt(
                                       dnsrec, ARES_SECTION_ADDITIONAL));
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  return ARES_SUCCESS;
}

static ares_status_t ares_dns_write_questions(const ares_dns_record_t *dnsrec,
                                              ares_llist_t           **namelist,
                                              ares_buf_t              *buf)
{
  size_t i;

  for (i = 0; i < ares_dns_record_query_cnt(dnsrec); i++) {
    ares_status_t       status;
    const char         *name = NULL;
    ares_dns_rec_type_t qtype;
    ares_dns_class_t    qclass;

    status = ares_dns_record_query_get(dnsrec, i, &name, &qtype, &qclass);
    if (status != ARES_SUCCESS) {
      return status;
    }

    /* Name */
    status = ares_dns_name_write(buf, namelist, ARES_TRUE, name);
    if (status != ARES_SUCCESS) {
      return status;
    }

    /* Type */
    status = ares_buf_append_be16(buf, (unsigned short)qtype);
    if (status != ARES_SUCCESS) {
      return status; /* LCOV_EXCL_LINE: OutOfMemory */
    }

    /* Class */
    status = ares_buf_append_be16(buf, (unsigned short)qclass);
    if (status != ARES_SUCCESS) {
      return status; /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  return ARES_SUCCESS;
}

static ares_status_t ares_dns_write_rr_name(ares_buf_t          *buf,
                                            const ares_dns_rr_t *rr,
                                            ares_llist_t       **namelist,
                                            ares_bool_t       validate_hostname,
                                            ares_dns_rr_key_t key)
{
  const char *name;

  name = ares_dns_rr_get_str(rr, key);
  if (name == NULL) {
    return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  return ares_dns_name_write(buf, namelist, validate_hostname, name);
}

static ares_status_t ares_dns_write_rr_str(ares_buf_t          *buf,
                                           const ares_dns_rr_t *rr,
                                           ares_dns_rr_key_t    key)
{
  const char   *str;
  size_t        len;
  ares_status_t status;

  str = ares_dns_rr_get_str(rr, key);
  if (str == NULL) {
    return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  len = ares_strlen(str);
  if (len > 255) {
    return ARES_EFORMERR;
  }

  /* Write 1 byte length */
  status = ares_buf_append_byte(buf, (unsigned char)(len & 0xFF));
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  if (len == 0) {
    return ARES_SUCCESS;
  }

  /* Write string */
  return ares_buf_append(buf, (const unsigned char *)str, len);
}

static ares_status_t ares_dns_write_binstr(ares_buf_t          *buf,
                                           const unsigned char *bin,
                                           size_t               bin_len)
{
  const unsigned char *ptr;
  size_t               ptr_len;
  ares_status_t        status;

  /* split into possible multiple 255-byte or less length strings */
  ptr     = bin;
  ptr_len = bin_len;
  do {
    size_t len = ptr_len;
    if (len > 255) {
      len = 255;
    }

    /* Length */
    status = ares_buf_append_byte(buf, (unsigned char)(len & 0xFF));
    if (status != ARES_SUCCESS) {
      return status; /* LCOV_EXCL_LINE: OutOfMemory */
    }

    /* String */
    if (len) {
      status = ares_buf_append(buf, ptr, len);
      if (status != ARES_SUCCESS) {
        return status; /* LCOV_EXCL_LINE: OutOfMemory */
      }
    }

    ptr     += len;
    ptr_len -= len;
  } while (ptr_len > 0);

  return ARES_SUCCESS;
}

static ares_status_t ares_dns_write_rr_abin(ares_buf_t          *buf,
                                            const ares_dns_rr_t *rr,
                                            ares_dns_rr_key_t    key)
{
  ares_status_t status = ARES_EFORMERR;
  size_t        i;
  size_t        cnt = ares_dns_rr_get_abin_cnt(rr, key);

  if (cnt == 0) {
    return ARES_EFORMERR;
  }

  for (i = 0; i < cnt; i++) {
    const unsigned char *bin;
    size_t               bin_len;

    bin = ares_dns_rr_get_abin(rr, key, i, &bin_len);

    status = ares_dns_write_binstr(buf, bin, bin_len);
    if (status != ARES_SUCCESS) {
      break;
    }
  }

  return status;
}

static ares_status_t ares_dns_write_rr_be32(ares_buf_t          *buf,
                                            const ares_dns_rr_t *rr,
                                            ares_dns_rr_key_t    key)
{
  if (ares_dns_rr_key_datatype(key) != ARES_DATATYPE_U32) {
    return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
  }
  return ares_buf_append_be32(buf, ares_dns_rr_get_u32(rr, key));
}

static ares_status_t ares_dns_write_rr_be16(ares_buf_t          *buf,
                                            const ares_dns_rr_t *rr,
                                            ares_dns_rr_key_t    key)
{
  if (ares_dns_rr_key_datatype(key) != ARES_DATATYPE_U16) {
    return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
  }
  return ares_buf_append_be16(buf, ares_dns_rr_get_u16(rr, key));
}

static ares_status_t ares_dns_write_rr_u8(ares_buf_t          *buf,
                                          const ares_dns_rr_t *rr,
                                          ares_dns_rr_key_t    key)
{
  if (ares_dns_rr_key_datatype(key) != ARES_DATATYPE_U8) {
    return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
  }
  return ares_buf_append_byte(buf, ares_dns_rr_get_u8(rr, key));
}

static ares_status_t ares_dns_write_rr_a(ares_buf_t          *buf,
                                         const ares_dns_rr_t *rr,
                                         ares_llist_t       **namelist)
{
  const struct in_addr *addr;
  (void)namelist;

  addr = ares_dns_rr_get_addr(rr, ARES_RR_A_ADDR);
  if (addr == NULL) {
    return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  return ares_buf_append(buf, (const unsigned char *)addr, sizeof(*addr));
}

static ares_status_t ares_dns_write_rr_ns(ares_buf_t          *buf,
                                          const ares_dns_rr_t *rr,
                                          ares_llist_t       **namelist)
{
  return ares_dns_write_rr_name(buf, rr, namelist, ARES_FALSE,
                                ARES_RR_NS_NSDNAME);
}

static ares_status_t ares_dns_write_rr_cname(ares_buf_t          *buf,
                                             const ares_dns_rr_t *rr,
                                             ares_llist_t       **namelist)
{
  return ares_dns_write_rr_name(buf, rr, namelist, ARES_FALSE,
                                ARES_RR_CNAME_CNAME);
}

static ares_status_t ares_dns_write_rr_soa(ares_buf_t          *buf,
                                           const ares_dns_rr_t *rr,
                                           ares_llist_t       **namelist)
{
  ares_status_t status;

  /* MNAME */
  status =
    ares_dns_write_rr_name(buf, rr, namelist, ARES_FALSE, ARES_RR_SOA_MNAME);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* RNAME */
  status =
    ares_dns_write_rr_name(buf, rr, namelist, ARES_FALSE, ARES_RR_SOA_RNAME);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* SERIAL */
  status = ares_dns_write_rr_be32(buf, rr, ARES_RR_SOA_SERIAL);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* REFRESH */
  status = ares_dns_write_rr_be32(buf, rr, ARES_RR_SOA_REFRESH);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* RETRY */
  status = ares_dns_write_rr_be32(buf, rr, ARES_RR_SOA_RETRY);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* EXPIRE */
  status = ares_dns_write_rr_be32(buf, rr, ARES_RR_SOA_EXPIRE);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* MINIMUM */
  return ares_dns_write_rr_be32(buf, rr, ARES_RR_SOA_MINIMUM);
}

static ares_status_t ares_dns_write_rr_ptr(ares_buf_t          *buf,
                                           const ares_dns_rr_t *rr,
                                           ares_llist_t       **namelist)
{
  return ares_dns_write_rr_name(buf, rr, namelist, ARES_FALSE,
                                ARES_RR_PTR_DNAME);
}

static ares_status_t ares_dns_write_rr_hinfo(ares_buf_t          *buf,
                                             const ares_dns_rr_t *rr,
                                             ares_llist_t       **namelist)
{
  ares_status_t status;

  (void)namelist;

  /* CPU */
  status = ares_dns_write_rr_str(buf, rr, ARES_RR_HINFO_CPU);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* OS */
  return ares_dns_write_rr_str(buf, rr, ARES_RR_HINFO_OS);
}

static ares_status_t ares_dns_write_rr_mx(ares_buf_t          *buf,
                                          const ares_dns_rr_t *rr,
                                          ares_llist_t       **namelist)
{
  ares_status_t status;

  /* PREFERENCE */
  status = ares_dns_write_rr_be16(buf, rr, ARES_RR_MX_PREFERENCE);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* EXCHANGE */
  return ares_dns_write_rr_name(buf, rr, namelist, ARES_FALSE,
                                ARES_RR_MX_EXCHANGE);
}

static ares_status_t ares_dns_write_rr_txt(ares_buf_t          *buf,
                                           const ares_dns_rr_t *rr,
                                           ares_llist_t       **namelist)
{
  (void)namelist;
  return ares_dns_write_rr_abin(buf, rr, ARES_RR_TXT_DATA);
}

static ares_status_t ares_dns_write_rr_sig(ares_buf_t          *buf,
                                           const ares_dns_rr_t *rr,
                                           ares_llist_t       **namelist)
{
  ares_status_t        status;
  const unsigned char *data;
  size_t               len = 0;

  (void)namelist;

  /* TYPE COVERED */
  status = ares_dns_write_rr_be16(buf, rr, ARES_RR_SIG_TYPE_COVERED);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* ALGORITHM */
  status = ares_dns_write_rr_u8(buf, rr, ARES_RR_SIG_ALGORITHM);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* LABELS */
  status = ares_dns_write_rr_u8(buf, rr, ARES_RR_SIG_LABELS);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* ORIGINAL TTL */
  status = ares_dns_write_rr_be32(buf, rr, ARES_RR_SIG_ORIGINAL_TTL);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* EXPIRATION */
  status = ares_dns_write_rr_be32(buf, rr, ARES_RR_SIG_EXPIRATION);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* INCEPTION */
  status = ares_dns_write_rr_be32(buf, rr, ARES_RR_SIG_INCEPTION);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* KEY TAG */
  status = ares_dns_write_rr_be16(buf, rr, ARES_RR_SIG_KEY_TAG);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* SIGNERS NAME */
  status = ares_dns_write_rr_name(buf, rr, namelist, ARES_FALSE,
                                  ARES_RR_SIG_SIGNERS_NAME);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* SIGNATURE -- binary, rest of buffer, required to be non-zero length */
  data = ares_dns_rr_get_bin(rr, ARES_RR_SIG_SIGNATURE, &len);
  if (data == NULL || len == 0) {
    return ARES_EFORMERR;
  }

  return ares_buf_append(buf, data, len);
}

static ares_status_t ares_dns_write_rr_aaaa(ares_buf_t          *buf,
                                            const ares_dns_rr_t *rr,
                                            ares_llist_t       **namelist)
{
  const struct ares_in6_addr *addr;
  (void)namelist;

  addr = ares_dns_rr_get_addr6(rr, ARES_RR_AAAA_ADDR);
  if (addr == NULL) {
    return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  return ares_buf_append(buf, (const unsigned char *)addr, sizeof(*addr));
}

static ares_status_t ares_dns_write_rr_srv(ares_buf_t          *buf,
                                           const ares_dns_rr_t *rr,
                                           ares_llist_t       **namelist)
{
  ares_status_t status;

  /* PRIORITY */
  status = ares_dns_write_rr_be16(buf, rr, ARES_RR_SRV_PRIORITY);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* WEIGHT */
  status = ares_dns_write_rr_be16(buf, rr, ARES_RR_SRV_WEIGHT);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* PORT */
  status = ares_dns_write_rr_be16(buf, rr, ARES_RR_SRV_PORT);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* TARGET */
  return ares_dns_write_rr_name(buf, rr, namelist, ARES_FALSE,
                                ARES_RR_SRV_TARGET);
}

static ares_status_t ares_dns_write_rr_naptr(ares_buf_t          *buf,
                                             const ares_dns_rr_t *rr,
                                             ares_llist_t       **namelist)
{
  ares_status_t status;

  /* ORDER */
  status = ares_dns_write_rr_be16(buf, rr, ARES_RR_NAPTR_ORDER);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* PREFERENCE */
  status = ares_dns_write_rr_be16(buf, rr, ARES_RR_NAPTR_PREFERENCE);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* FLAGS */
  status = ares_dns_write_rr_str(buf, rr, ARES_RR_NAPTR_FLAGS);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* SERVICES */
  status = ares_dns_write_rr_str(buf, rr, ARES_RR_NAPTR_SERVICES);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* REGEXP */
  status = ares_dns_write_rr_str(buf, rr, ARES_RR_NAPTR_REGEXP);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* REPLACEMENT */
  return ares_dns_write_rr_name(buf, rr, namelist, ARES_FALSE,
                                ARES_RR_NAPTR_REPLACEMENT);
}

static ares_status_t ares_dns_write_rr_opt(ares_buf_t          *buf,
                                           const ares_dns_rr_t *rr,
                                           ares_llist_t       **namelist)
{
  size_t         len = ares_buf_len(buf);
  ares_status_t  status;
  unsigned int   ttl = 0;
  size_t         i;
  unsigned short rcode = (unsigned short)((rr->parent->rcode >> 4) & 0xFF);

  (void)namelist;

  /* Coverity reports on this even though its not possible when taken
   * into context */
  if (len == 0) {
    return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  /* We need to go back and overwrite the class and ttl that were emitted as
   * the OPT record overloads them for its own use (yes, very strange!) */
  status = ares_buf_set_length(buf, len - 2 /* RDLENGTH */
                                      - 4   /* TTL */
                                      - 2 /* CLASS */);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* Class -> UDP Size */
  status = ares_dns_write_rr_be16(buf, rr, ARES_RR_OPT_UDP_SIZE);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* TTL -> rcode (u8) << 24 | version (u8) << 16 | flags (u16) */
  ttl |= (unsigned int)rcode << 24;
  ttl |= (unsigned int)ares_dns_rr_get_u8(rr, ARES_RR_OPT_VERSION) << 16;
  ttl |= (unsigned int)ares_dns_rr_get_u16(rr, ARES_RR_OPT_FLAGS);

  status = ares_buf_append_be32(buf, ttl);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* Now go back to real end */
  status = ares_buf_set_length(buf, len);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* Append Options */
  for (i = 0; i < ares_dns_rr_get_opt_cnt(rr, ARES_RR_OPT_OPTIONS); i++) {
    unsigned short       opt;
    size_t               val_len;
    const unsigned char *val;

    opt = ares_dns_rr_get_opt(rr, ARES_RR_OPT_OPTIONS, i, &val, &val_len);

    /* BE16 option */
    status = ares_buf_append_be16(buf, opt);
    if (status != ARES_SUCCESS) {
      return status; /* LCOV_EXCL_LINE: OutOfMemory */
    }

    /* BE16 length */
    status = ares_buf_append_be16(buf, (unsigned short)(val_len & 0xFFFF));
    if (status != ARES_SUCCESS) {
      return status; /* LCOV_EXCL_LINE: OutOfMemory */
    }

    /* Value */
    if (val && val_len) {
      status = ares_buf_append(buf, val, val_len);
      if (status != ARES_SUCCESS) {
        return status; /* LCOV_EXCL_LINE: OutOfMemory */
      }
    }
  }

  return ARES_SUCCESS;
}

static ares_status_t ares_dns_write_rr_tlsa(ares_buf_t          *buf,
                                            const ares_dns_rr_t *rr,
                                            ares_llist_t       **namelist)
{
  ares_status_t        status;
  const unsigned char *data;
  size_t               len = 0;

  (void)namelist;

  /* CERT_USAGE */
  status = ares_dns_write_rr_u8(buf, rr, ARES_RR_TLSA_CERT_USAGE);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* SELECTOR */
  status = ares_dns_write_rr_u8(buf, rr, ARES_RR_TLSA_SELECTOR);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* MATCH */
  status = ares_dns_write_rr_u8(buf, rr, ARES_RR_TLSA_MATCH);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* DATA -- binary, rest of buffer, required to be non-zero length */
  data = ares_dns_rr_get_bin(rr, ARES_RR_TLSA_DATA, &len);
  if (data == NULL || len == 0) {
    return ARES_EFORMERR;
  }

  return ares_buf_append(buf, data, len);
}

static ares_status_t ares_dns_write_rr_svcb(ares_buf_t          *buf,
                                            const ares_dns_rr_t *rr,
                                            ares_llist_t       **namelist)
{
  ares_status_t status;
  size_t        i;

  /* PRIORITY */
  status = ares_dns_write_rr_be16(buf, rr, ARES_RR_SVCB_PRIORITY);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* TARGET */
  status =
    ares_dns_write_rr_name(buf, rr, namelist, ARES_FALSE, ARES_RR_SVCB_TARGET);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* Append Params */
  for (i = 0; i < ares_dns_rr_get_opt_cnt(rr, ARES_RR_SVCB_PARAMS); i++) {
    unsigned short       opt;
    size_t               val_len;
    const unsigned char *val;

    opt = ares_dns_rr_get_opt(rr, ARES_RR_SVCB_PARAMS, i, &val, &val_len);

    /* BE16 option */
    status = ares_buf_append_be16(buf, opt);
    if (status != ARES_SUCCESS) {
      return status; /* LCOV_EXCL_LINE: OutOfMemory */
    }

    /* BE16 length */
    status = ares_buf_append_be16(buf, (unsigned short)(val_len & 0xFFFF));
    if (status != ARES_SUCCESS) {
      return status; /* LCOV_EXCL_LINE: OutOfMemory */
    }

    /* Value */
    if (val && val_len) {
      status = ares_buf_append(buf, val, val_len);
      if (status != ARES_SUCCESS) {
        return status; /* LCOV_EXCL_LINE: OutOfMemory */
      }
    }
  }
  return ARES_SUCCESS;
}

static ares_status_t ares_dns_write_rr_https(ares_buf_t          *buf,
                                             const ares_dns_rr_t *rr,
                                             ares_llist_t       **namelist)
{
  ares_status_t status;
  size_t        i;

  /* PRIORITY */
  status = ares_dns_write_rr_be16(buf, rr, ARES_RR_HTTPS_PRIORITY);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* TARGET */
  status =
    ares_dns_write_rr_name(buf, rr, namelist, ARES_FALSE, ARES_RR_HTTPS_TARGET);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* Append Params */
  for (i = 0; i < ares_dns_rr_get_opt_cnt(rr, ARES_RR_HTTPS_PARAMS); i++) {
    unsigned short       opt;
    size_t               val_len;
    const unsigned char *val;

    opt = ares_dns_rr_get_opt(rr, ARES_RR_HTTPS_PARAMS, i, &val, &val_len);

    /* BE16 option */
    status = ares_buf_append_be16(buf, opt);
    if (status != ARES_SUCCESS) {
      return status; /* LCOV_EXCL_LINE: OutOfMemory */
    }

    /* BE16 length */
    status = ares_buf_append_be16(buf, (unsigned short)(val_len & 0xFFFF));
    if (status != ARES_SUCCESS) {
      return status; /* LCOV_EXCL_LINE: OutOfMemory */
    }

    /* Value */
    if (val && val_len) {
      status = ares_buf_append(buf, val, val_len);
      if (status != ARES_SUCCESS) {
        return status; /* LCOV_EXCL_LINE: OutOfMemory */
      }
    }
  }
  return ARES_SUCCESS;
}

static ares_status_t ares_dns_write_rr_uri(ares_buf_t          *buf,
                                           const ares_dns_rr_t *rr,
                                           ares_llist_t       **namelist)
{
  ares_status_t status;
  const char   *target;

  (void)namelist;

  /* PRIORITY */
  status = ares_dns_write_rr_be16(buf, rr, ARES_RR_URI_PRIORITY);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* WEIGHT */
  status = ares_dns_write_rr_be16(buf, rr, ARES_RR_URI_WEIGHT);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* TARGET -- not in DNS string format, rest of buffer, required to be
   * non-zero length */
  target = ares_dns_rr_get_str(rr, ARES_RR_URI_TARGET);
  if (target == NULL || ares_strlen(target) == 0) {
    return ARES_EFORMERR;
  }

  return ares_buf_append(buf, (const unsigned char *)target,
                         ares_strlen(target));
}

static ares_status_t ares_dns_write_rr_caa(ares_buf_t          *buf,
                                           const ares_dns_rr_t *rr,
                                           ares_llist_t       **namelist)
{
  const unsigned char *data     = NULL;
  size_t               data_len = 0;
  ares_status_t        status;

  (void)namelist;

  /* CRITICAL */
  status = ares_dns_write_rr_u8(buf, rr, ARES_RR_CAA_CRITICAL);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* Tag */
  status = ares_dns_write_rr_str(buf, rr, ARES_RR_CAA_TAG);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* Value - binary! (remaining buffer */
  data = ares_dns_rr_get_bin(rr, ARES_RR_CAA_VALUE, &data_len);
  if (data == NULL || data_len == 0) {
    return ARES_EFORMERR;
  }

  return ares_buf_append(buf, data, data_len);
}

static ares_status_t ares_dns_write_rr_raw_rr(ares_buf_t          *buf,
                                              const ares_dns_rr_t *rr,
                                              ares_llist_t       **namelist)
{
  size_t               len = ares_buf_len(buf);
  ares_status_t        status;
  const unsigned char *data     = NULL;
  size_t               data_len = 0;

  (void)namelist;

  /* Coverity reports on this even though its not possible when taken
   * into context */
  if (len == 0) {
    return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  /* We need to go back and overwrite the type that was emitted by the parent
   * function */
  status = ares_buf_set_length(buf, len - 2 /* RDLENGTH */
                                      - 4   /* TTL */
                                      - 2   /* CLASS */
                                      - 2 /* TYPE */);
  if (status != ARES_SUCCESS) {
    return status;
  }

  status = ares_dns_write_rr_be16(buf, rr, ARES_RR_RAW_RR_TYPE);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* Now go back to real end */
  status = ares_buf_set_length(buf, len);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* Output raw data */
  data = ares_dns_rr_get_bin(rr, ARES_RR_RAW_RR_DATA, &data_len);
  if (data == NULL) {
    return ARES_EFORMERR;
  }

  if (data_len == 0) {
    return ARES_SUCCESS;
  }

  return ares_buf_append(buf, data, data_len);
}

static ares_status_t ares_dns_write_rr(const ares_dns_record_t *dnsrec,
                                       ares_llist_t           **namelist,
                                       ares_dns_section_t       section,
                                       ares_buf_t              *buf)
{
  size_t i;

  for (i = 0; i < ares_dns_record_rr_cnt(dnsrec, section); i++) {
    const ares_dns_rr_t *rr;
    ares_dns_rec_type_t  type;
    ares_bool_t          allow_compress;
    ares_llist_t       **namelistptr = NULL;
    size_t               pos_len;
    ares_status_t        status;
    size_t               rdlength;
    size_t               end_length;
    unsigned int         ttl;

    rr = ares_dns_record_rr_get_const(dnsrec, section, i);
    if (rr == NULL) {
      return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
    }

    type           = ares_dns_rr_get_type(rr);
    allow_compress = ares_dns_rec_allow_name_comp(type);
    if (allow_compress) {
      namelistptr = namelist;
    }

    /* Name */
    status =
      ares_dns_name_write(buf, namelist, ARES_TRUE, ares_dns_rr_get_name(rr));
    if (status != ARES_SUCCESS) {
      return status;
    }

    /* Type */
    status = ares_buf_append_be16(buf, (unsigned short)type);
    if (status != ARES_SUCCESS) {
      return status; /* LCOV_EXCL_LINE: OutOfMemory */
    }

    /* Class */
    status =
      ares_buf_append_be16(buf, (unsigned short)ares_dns_rr_get_class(rr));
    if (status != ARES_SUCCESS) {
      return status; /* LCOV_EXCL_LINE: OutOfMemory */
    }

    /* TTL */
    ttl = ares_dns_rr_get_ttl(rr);
    if (rr->parent->ttl_decrement > ttl) {
      ttl = 0;
    } else {
      ttl -= rr->parent->ttl_decrement;
    }
    status = ares_buf_append_be32(buf, ttl);
    if (status != ARES_SUCCESS) {
      return status; /* LCOV_EXCL_LINE: OutOfMemory */
    }

    /* Length */
    pos_len = ares_buf_len(buf); /* Save to write real length later */
    status  = ares_buf_append_be16(buf, 0);
    if (status != ARES_SUCCESS) {
      return status; /* LCOV_EXCL_LINE: OutOfMemory */
    }

    /* Data */
    switch (type) {
      case ARES_REC_TYPE_A:
        status = ares_dns_write_rr_a(buf, rr, namelistptr);
        break;
      case ARES_REC_TYPE_NS:
        status = ares_dns_write_rr_ns(buf, rr, namelistptr);
        break;
      case ARES_REC_TYPE_CNAME:
        status = ares_dns_write_rr_cname(buf, rr, namelistptr);
        break;
      case ARES_REC_TYPE_SOA:
        status = ares_dns_write_rr_soa(buf, rr, namelistptr);
        break;
      case ARES_REC_TYPE_PTR:
        status = ares_dns_write_rr_ptr(buf, rr, namelistptr);
        break;
      case ARES_REC_TYPE_HINFO:
        status = ares_dns_write_rr_hinfo(buf, rr, namelistptr);
        break;
      case ARES_REC_TYPE_MX:
        status = ares_dns_write_rr_mx(buf, rr, namelistptr);
        break;
      case ARES_REC_TYPE_TXT:
        status = ares_dns_write_rr_txt(buf, rr, namelistptr);
        break;
      case ARES_REC_TYPE_SIG:
        status = ares_dns_write_rr_sig(buf, rr, namelistptr);
        break;
      case ARES_REC_TYPE_AAAA:
        status = ares_dns_write_rr_aaaa(buf, rr, namelistptr);
        break;
      case ARES_REC_TYPE_SRV:
        status = ares_dns_write_rr_srv(buf, rr, namelistptr);
        break;
      case ARES_REC_TYPE_NAPTR:
        status = ares_dns_write_rr_naptr(buf, rr, namelistptr);
        break;
      case ARES_REC_TYPE_ANY:
        status = ARES_EFORMERR;
        break;
      case ARES_REC_TYPE_OPT:
        status = ares_dns_write_rr_opt(buf, rr, namelistptr);
        break;
      case ARES_REC_TYPE_TLSA:
        status = ares_dns_write_rr_tlsa(buf, rr, namelistptr);
        break;
      case ARES_REC_TYPE_SVCB:
        status = ares_dns_write_rr_svcb(buf, rr, namelistptr);
        break;
      case ARES_REC_TYPE_HTTPS:
        status = ares_dns_write_rr_https(buf, rr, namelistptr);
        break;
      case ARES_REC_TYPE_URI:
        status = ares_dns_write_rr_uri(buf, rr, namelistptr);
        break;
      case ARES_REC_TYPE_CAA:
        status = ares_dns_write_rr_caa(buf, rr, namelistptr);
        break;
      case ARES_REC_TYPE_RAW_RR:
        status = ares_dns_write_rr_raw_rr(buf, rr, namelistptr);
        break;
    }

    if (status != ARES_SUCCESS) {
      return status;
    }

    /* Back off write pointer, write real length, then go back to proper
     * position */
    end_length = ares_buf_len(buf);
    rdlength   = end_length - pos_len - 2;

    status = ares_buf_set_length(buf, pos_len);
    if (status != ARES_SUCCESS) {
      return status;
    }

    status = ares_buf_append_be16(buf, (unsigned short)(rdlength & 0xFFFF));
    if (status != ARES_SUCCESS) {
      return status; /* LCOV_EXCL_LINE: OutOfMemory */
    }

    status = ares_buf_set_length(buf, end_length);
    if (status != ARES_SUCCESS) {
      return status;
    }
  }

  return ARES_SUCCESS;
}

ares_status_t ares_dns_write_buf(const ares_dns_record_t *dnsrec,
                                 ares_buf_t              *buf)
{
  ares_llist_t *namelist = NULL;
  size_t        orig_len;
  ares_status_t status;

  if (dnsrec == NULL || buf == NULL) {
    return ARES_EFORMERR;
  }

  orig_len = ares_buf_len(buf);

  status = ares_dns_write_header(dnsrec, buf);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  status = ares_dns_write_questions(dnsrec, &namelist, buf);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  status = ares_dns_write_rr(dnsrec, &namelist, ARES_SECTION_ANSWER, buf);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  status = ares_dns_write_rr(dnsrec, &namelist, ARES_SECTION_AUTHORITY, buf);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  status = ares_dns_write_rr(dnsrec, &namelist, ARES_SECTION_ADDITIONAL, buf);
  if (status != ARES_SUCCESS) {
    goto done;
  }

done:
  ares_llist_destroy(namelist);
  if (status != ARES_SUCCESS) {
    ares_buf_set_length(buf, orig_len);
  }

  return status;
}

ares_status_t ares_dns_write_buf_tcp(const ares_dns_record_t *dnsrec,
                                     ares_buf_t              *buf)
{
  ares_status_t status;
  size_t        orig_len;
  size_t        msg_len;
  size_t        len;

  if (dnsrec == NULL || buf == NULL) {
    return ARES_EFORMERR;
  }

  orig_len = ares_buf_len(buf);

  /* Write placeholder for length */
  status = ares_buf_append_be16(buf, 0);
  if (status != ARES_SUCCESS) {
    goto done; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* Write message */
  status = ares_dns_write_buf(dnsrec, buf);
  if (status != ARES_SUCCESS) {
    goto done; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  len     = ares_buf_len(buf);
  msg_len = len - orig_len - 2;
  if (msg_len > 65535) {
    status = ARES_EBADQUERY;
    goto done;
  }

  /* Now we need to overwrite the length, so we jump back to the original
   * message length, overwrite the section and jump back */
  ares_buf_set_length(buf, orig_len);
  status = ares_buf_append_be16(buf, (unsigned short)(msg_len & 0xFFFF));
  if (status != ARES_SUCCESS) {
    goto done; /* LCOV_EXCL_LINE: UntestablePath */
  }
  ares_buf_set_length(buf, len);

done:
  if (status != ARES_SUCCESS) {
    ares_buf_set_length(buf, orig_len);
  }
  return status;
}

ares_status_t ares_dns_write(const ares_dns_record_t *dnsrec,
                             unsigned char **buf, size_t *buf_len)
{
  ares_buf_t   *b = NULL;
  ares_status_t status;

  if (buf == NULL || buf_len == NULL || dnsrec == NULL) {
    return ARES_EFORMERR;
  }

  *buf     = NULL;
  *buf_len = 0;

  b = ares_buf_create();
  if (b == NULL) {
    return ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  status = ares_dns_write_buf(dnsrec, b);

  if (status != ARES_SUCCESS) {
    ares_buf_destroy(b);
    return status;
  }

  *buf = ares_buf_finish_bin(b, buf_len);
  return status;
}

void ares_dns_record_ttl_decrement(ares_dns_record_t *dnsrec,
                                   unsigned int       ttl_decrement)
{
  if (dnsrec == NULL) {
    return;
  }
  dnsrec->ttl_decrement = ttl_decrement;
}
