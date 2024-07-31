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

static size_t ares_dns_rr_remaining_len(const ares__buf_t *buf, size_t orig_len,
                                        size_t rdlength)
{
  size_t used_len = orig_len - ares__buf_len(buf);
  if (used_len >= rdlength) {
    return 0;
  }
  return rdlength - used_len;
}

static ares_status_t ares_dns_parse_and_set_dns_name(ares__buf_t   *buf,
                                                     ares_bool_t    is_hostname,
                                                     ares_dns_rr_t *rr,
                                                     ares_dns_rr_key_t key)
{
  ares_status_t status;
  char         *name = NULL;

  status = ares__dns_name_parse(buf, &name, is_hostname);
  if (status != ARES_SUCCESS) {
    return status;
  }

  status = ares_dns_rr_set_str_own(rr, key, name);
  if (status != ARES_SUCCESS) {
    ares_free(name);
    return status;
  }
  return ARES_SUCCESS;
}

static ares_status_t ares_dns_parse_and_set_dns_str(ares__buf_t      *buf,
                                                    size_t            max_len,
                                                    ares_dns_rr_t    *rr,
                                                    ares_dns_rr_key_t key,
                                                    ares_bool_t blank_allowed)
{
  ares_status_t status;
  char         *str = NULL;

  status = ares__buf_parse_dns_str(buf, max_len, &str);
  if (status != ARES_SUCCESS) {
    return status;
  }

  if (!blank_allowed && ares_strlen(str) == 0) {
    ares_free(str);
    return ARES_EBADRESP;
  }

  status = ares_dns_rr_set_str_own(rr, key, str);
  if (status != ARES_SUCCESS) {
    ares_free(str);
    return status;
  }
  return ARES_SUCCESS;
}

static ares_status_t
  ares_dns_parse_and_set_dns_abin(ares__buf_t *buf, size_t max_len,
                                  ares_dns_rr_t *rr, ares_dns_rr_key_t key,
                                  ares_bool_t validate_printable)
{
  ares_status_t            status;
  ares__dns_multistring_t *strs = NULL;

  status = ares__buf_parse_dns_abinstr(buf, max_len, &strs, validate_printable);
  if (status != ARES_SUCCESS) {
    return status;
  }

  status = ares_dns_rr_set_abin_own(rr, key, strs);
  if (status != ARES_SUCCESS) {
    ares__dns_multistring_destroy(strs);
    return status;
  }
  return ARES_SUCCESS;
}

static ares_status_t ares_dns_parse_and_set_be32(ares__buf_t      *buf,
                                                 ares_dns_rr_t    *rr,
                                                 ares_dns_rr_key_t key)
{
  ares_status_t status;
  unsigned int  u32;

  status = ares__buf_fetch_be32(buf, &u32);
  if (status != ARES_SUCCESS) {
    return status;
  }

  return ares_dns_rr_set_u32(rr, key, u32);
}

static ares_status_t ares_dns_parse_and_set_be16(ares__buf_t      *buf,
                                                 ares_dns_rr_t    *rr,
                                                 ares_dns_rr_key_t key)
{
  ares_status_t  status;
  unsigned short u16;

  status = ares__buf_fetch_be16(buf, &u16);
  if (status != ARES_SUCCESS) {
    return status;
  }

  return ares_dns_rr_set_u16(rr, key, u16);
}

static ares_status_t ares_dns_parse_and_set_u8(ares__buf_t      *buf,
                                               ares_dns_rr_t    *rr,
                                               ares_dns_rr_key_t key)
{
  ares_status_t status;
  unsigned char u8;

  status = ares__buf_fetch_bytes(buf, &u8, 1);
  if (status != ARES_SUCCESS) {
    return status;
  }

  return ares_dns_rr_set_u8(rr, key, u8);
}

static ares_status_t ares_dns_parse_rr_a(ares__buf_t *buf, ares_dns_rr_t *rr,
                                         size_t rdlength)
{
  struct in_addr addr;
  ares_status_t  status;

  (void)rdlength; /* Not needed */

  status = ares__buf_fetch_bytes(buf, (unsigned char *)&addr, sizeof(addr));
  if (status != ARES_SUCCESS) {
    return status;
  }

  return ares_dns_rr_set_addr(rr, ARES_RR_A_ADDR, &addr);
}

static ares_status_t ares_dns_parse_rr_ns(ares__buf_t *buf, ares_dns_rr_t *rr,
                                          size_t rdlength)
{
  (void)rdlength; /* Not needed */

  return ares_dns_parse_and_set_dns_name(buf, ARES_FALSE, rr,
                                         ARES_RR_NS_NSDNAME);
}

static ares_status_t ares_dns_parse_rr_cname(ares__buf_t   *buf,
                                             ares_dns_rr_t *rr, size_t rdlength)
{
  (void)rdlength; /* Not needed */

  return ares_dns_parse_and_set_dns_name(buf, ARES_FALSE, rr,
                                         ARES_RR_CNAME_CNAME);
}

static ares_status_t ares_dns_parse_rr_soa(ares__buf_t *buf, ares_dns_rr_t *rr,
                                           size_t rdlength)
{
  ares_status_t status;

  (void)rdlength; /* Not needed */

  /* MNAME */
  status =
    ares_dns_parse_and_set_dns_name(buf, ARES_FALSE, rr, ARES_RR_SOA_MNAME);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* RNAME */
  status =
    ares_dns_parse_and_set_dns_name(buf, ARES_FALSE, rr, ARES_RR_SOA_RNAME);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* SERIAL */
  status = ares_dns_parse_and_set_be32(buf, rr, ARES_RR_SOA_SERIAL);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* REFRESH */
  status = ares_dns_parse_and_set_be32(buf, rr, ARES_RR_SOA_REFRESH);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* RETRY */
  status = ares_dns_parse_and_set_be32(buf, rr, ARES_RR_SOA_RETRY);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* EXPIRE */
  status = ares_dns_parse_and_set_be32(buf, rr, ARES_RR_SOA_EXPIRE);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* MINIMUM */
  return ares_dns_parse_and_set_be32(buf, rr, ARES_RR_SOA_MINIMUM);
}

static ares_status_t ares_dns_parse_rr_ptr(ares__buf_t *buf, ares_dns_rr_t *rr,
                                           size_t rdlength)
{
  (void)rdlength; /* Not needed */

  return ares_dns_parse_and_set_dns_name(buf, ARES_FALSE, rr,
                                         ARES_RR_PTR_DNAME);
}

static ares_status_t ares_dns_parse_rr_hinfo(ares__buf_t   *buf,
                                             ares_dns_rr_t *rr, size_t rdlength)
{
  ares_status_t status;
  size_t        orig_len = ares__buf_len(buf);

  (void)rdlength; /* Not needed */

  /* CPU */
  status = ares_dns_parse_and_set_dns_str(
    buf, ares_dns_rr_remaining_len(buf, orig_len, rdlength), rr,
    ARES_RR_HINFO_CPU, ARES_TRUE);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* OS */
  status = ares_dns_parse_and_set_dns_str(
    buf, ares_dns_rr_remaining_len(buf, orig_len, rdlength), rr,
    ARES_RR_HINFO_OS, ARES_TRUE);

  return status;
}

static ares_status_t ares_dns_parse_rr_mx(ares__buf_t *buf, ares_dns_rr_t *rr,
                                          size_t rdlength)
{
  ares_status_t status;

  (void)rdlength; /* Not needed */

  /* PREFERENCE */
  status = ares_dns_parse_and_set_be16(buf, rr, ARES_RR_MX_PREFERENCE);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* EXCHANGE */
  return ares_dns_parse_and_set_dns_name(buf, ARES_FALSE, rr,
                                         ARES_RR_MX_EXCHANGE);
}

static ares_status_t ares_dns_parse_rr_txt(ares__buf_t *buf, ares_dns_rr_t *rr,
                                           size_t rdlength)
{
  return ares_dns_parse_and_set_dns_abin(buf, rdlength, rr, ARES_RR_TXT_DATA,
                                         ARES_FALSE);
}

static ares_status_t ares_dns_parse_rr_sig(ares__buf_t *buf, ares_dns_rr_t *rr,
                                           size_t rdlength)
{
  ares_status_t  status;
  size_t         orig_len = ares__buf_len(buf);
  size_t         len;
  unsigned char *data;

  status = ares_dns_parse_and_set_be16(buf, rr, ARES_RR_SIG_TYPE_COVERED);
  if (status != ARES_SUCCESS) {
    return status;
  }

  status = ares_dns_parse_and_set_u8(buf, rr, ARES_RR_SIG_ALGORITHM);
  if (status != ARES_SUCCESS) {
    return status;
  }

  status = ares_dns_parse_and_set_u8(buf, rr, ARES_RR_SIG_LABELS);
  if (status != ARES_SUCCESS) {
    return status;
  }

  status = ares_dns_parse_and_set_be32(buf, rr, ARES_RR_SIG_ORIGINAL_TTL);
  if (status != ARES_SUCCESS) {
    return status;
  }

  status = ares_dns_parse_and_set_be32(buf, rr, ARES_RR_SIG_EXPIRATION);
  if (status != ARES_SUCCESS) {
    return status;
  }

  status = ares_dns_parse_and_set_be32(buf, rr, ARES_RR_SIG_INCEPTION);
  if (status != ARES_SUCCESS) {
    return status;
  }

  status = ares_dns_parse_and_set_be16(buf, rr, ARES_RR_SIG_KEY_TAG);
  if (status != ARES_SUCCESS) {
    return status;
  }

  status = ares_dns_parse_and_set_dns_name(buf, ARES_FALSE, rr,
                                           ARES_RR_SIG_SIGNERS_NAME);
  if (status != ARES_SUCCESS) {
    return status;
  }

  len = ares_dns_rr_remaining_len(buf, orig_len, rdlength);
  if (len == 0) {
    return ARES_EBADRESP;
  }

  status = ares__buf_fetch_bytes_dup(buf, len, ARES_FALSE, &data);
  if (status != ARES_SUCCESS) {
    return status;
  }

  status = ares_dns_rr_set_bin_own(rr, ARES_RR_SIG_SIGNATURE, data, len);
  if (status != ARES_SUCCESS) {
    ares_free(data);
    return status;
  }

  return ARES_SUCCESS;
}

static ares_status_t ares_dns_parse_rr_aaaa(ares__buf_t *buf, ares_dns_rr_t *rr,
                                            size_t rdlength)
{
  struct ares_in6_addr addr;
  ares_status_t        status;

  (void)rdlength; /* Not needed */

  status = ares__buf_fetch_bytes(buf, (unsigned char *)&addr, sizeof(addr));
  if (status != ARES_SUCCESS) {
    return status;
  }

  return ares_dns_rr_set_addr6(rr, ARES_RR_AAAA_ADDR, &addr);
}

static ares_status_t ares_dns_parse_rr_srv(ares__buf_t *buf, ares_dns_rr_t *rr,
                                           size_t rdlength)
{
  ares_status_t status;

  (void)rdlength; /* Not needed */

  /* PRIORITY */
  status = ares_dns_parse_and_set_be16(buf, rr, ARES_RR_SRV_PRIORITY);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* WEIGHT */
  status = ares_dns_parse_and_set_be16(buf, rr, ARES_RR_SRV_WEIGHT);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* PORT */
  status = ares_dns_parse_and_set_be16(buf, rr, ARES_RR_SRV_PORT);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* TARGET */
  return ares_dns_parse_and_set_dns_name(buf, ARES_FALSE, rr,
                                         ARES_RR_SRV_TARGET);
}

static ares_status_t ares_dns_parse_rr_naptr(ares__buf_t   *buf,
                                             ares_dns_rr_t *rr, size_t rdlength)
{
  ares_status_t status;
  size_t        orig_len = ares__buf_len(buf);

  /* ORDER */
  status = ares_dns_parse_and_set_be16(buf, rr, ARES_RR_NAPTR_ORDER);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* PREFERENCE */
  status = ares_dns_parse_and_set_be16(buf, rr, ARES_RR_NAPTR_PREFERENCE);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* FLAGS */
  status = ares_dns_parse_and_set_dns_str(
    buf, ares_dns_rr_remaining_len(buf, orig_len, rdlength), rr,
    ARES_RR_NAPTR_FLAGS, ARES_TRUE);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* SERVICES */
  status = ares_dns_parse_and_set_dns_str(
    buf, ares_dns_rr_remaining_len(buf, orig_len, rdlength), rr,
    ARES_RR_NAPTR_SERVICES, ARES_TRUE);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* REGEXP */
  status = ares_dns_parse_and_set_dns_str(
    buf, ares_dns_rr_remaining_len(buf, orig_len, rdlength), rr,
    ARES_RR_NAPTR_REGEXP, ARES_TRUE);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* REPLACEMENT */
  return ares_dns_parse_and_set_dns_name(buf, ARES_FALSE, rr,
                                         ARES_RR_NAPTR_REPLACEMENT);
}

static ares_status_t ares_dns_parse_rr_opt(ares__buf_t *buf, ares_dns_rr_t *rr,
                                           size_t         rdlength,
                                           unsigned short raw_class,
                                           unsigned int   raw_ttl)
{
  ares_status_t  status;
  size_t         orig_len = ares__buf_len(buf);
  unsigned short rcode_high;

  status = ares_dns_rr_set_u16(rr, ARES_RR_OPT_UDP_SIZE, raw_class);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* First 8 bits of TTL are an extended RCODE, and they go in the higher order
   * after the original 4-bit rcode */
  rcode_high             = (unsigned short)((raw_ttl >> 20) & 0x0FF0);
  rr->parent->raw_rcode |= rcode_high;

  status = ares_dns_rr_set_u8(rr, ARES_RR_OPT_VERSION,
                              (unsigned char)(raw_ttl >> 16) & 0xFF);
  if (status != ARES_SUCCESS) {
    return status;
  }

  status = ares_dns_rr_set_u16(rr, ARES_RR_OPT_FLAGS,
                               (unsigned short)(raw_ttl & 0xFFFF));
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* Parse options */
  while (ares_dns_rr_remaining_len(buf, orig_len, rdlength)) {
    unsigned short opt = 0;
    unsigned short len = 0;
    unsigned char *val = NULL;

    /* Fetch be16 option */
    status = ares__buf_fetch_be16(buf, &opt);
    if (status != ARES_SUCCESS) {
      return status;
    }

    /* Fetch be16 length */
    status = ares__buf_fetch_be16(buf, &len);
    if (status != ARES_SUCCESS) {
      return status;
    }

    if (len) {
      status = ares__buf_fetch_bytes_dup(buf, len, ARES_TRUE, &val);
      if (status != ARES_SUCCESS) {
        return status;
      }
    }

    status = ares_dns_rr_set_opt_own(rr, ARES_RR_OPT_OPTIONS, opt, val, len);
    if (status != ARES_SUCCESS) {
      return status;
    }
  }

  return ARES_SUCCESS;
}

static ares_status_t ares_dns_parse_rr_tlsa(ares__buf_t *buf, ares_dns_rr_t *rr,
                                            size_t rdlength)
{
  ares_status_t  status;
  size_t         orig_len = ares__buf_len(buf);
  size_t         len;
  unsigned char *data;

  status = ares_dns_parse_and_set_u8(buf, rr, ARES_RR_TLSA_CERT_USAGE);
  if (status != ARES_SUCCESS) {
    return status;
  }

  status = ares_dns_parse_and_set_u8(buf, rr, ARES_RR_TLSA_SELECTOR);
  if (status != ARES_SUCCESS) {
    return status;
  }

  status = ares_dns_parse_and_set_u8(buf, rr, ARES_RR_TLSA_MATCH);
  if (status != ARES_SUCCESS) {
    return status;
  }

  len = ares_dns_rr_remaining_len(buf, orig_len, rdlength);
  if (len == 0) {
    return ARES_EBADRESP;
  }

  status = ares__buf_fetch_bytes_dup(buf, len, ARES_FALSE, &data);
  if (status != ARES_SUCCESS) {
    return status;
  }

  status = ares_dns_rr_set_bin_own(rr, ARES_RR_TLSA_DATA, data, len);
  if (status != ARES_SUCCESS) {
    ares_free(data);
    return status;
  }

  return ARES_SUCCESS;
}

static ares_status_t ares_dns_parse_rr_svcb(ares__buf_t *buf, ares_dns_rr_t *rr,
                                            size_t rdlength)
{
  ares_status_t status;
  size_t        orig_len = ares__buf_len(buf);

  status = ares_dns_parse_and_set_be16(buf, rr, ARES_RR_SVCB_PRIORITY);
  if (status != ARES_SUCCESS) {
    return status;
  }

  status =
    ares_dns_parse_and_set_dns_name(buf, ARES_FALSE, rr, ARES_RR_SVCB_TARGET);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* Parse params */
  while (ares_dns_rr_remaining_len(buf, orig_len, rdlength)) {
    unsigned short opt = 0;
    unsigned short len = 0;
    unsigned char *val = NULL;

    /* Fetch be16 option */
    status = ares__buf_fetch_be16(buf, &opt);
    if (status != ARES_SUCCESS) {
      return status;
    }

    /* Fetch be16 length */
    status = ares__buf_fetch_be16(buf, &len);
    if (status != ARES_SUCCESS) {
      return status;
    }

    if (len) {
      status = ares__buf_fetch_bytes_dup(buf, len, ARES_TRUE, &val);
      if (status != ARES_SUCCESS) {
        return status;
      }
    }

    status = ares_dns_rr_set_opt_own(rr, ARES_RR_SVCB_PARAMS, opt, val, len);
    if (status != ARES_SUCCESS) {
      return status;
    }
  }

  return ARES_SUCCESS;
}

static ares_status_t ares_dns_parse_rr_https(ares__buf_t   *buf,
                                             ares_dns_rr_t *rr, size_t rdlength)
{
  ares_status_t status;
  size_t        orig_len = ares__buf_len(buf);

  status = ares_dns_parse_and_set_be16(buf, rr, ARES_RR_HTTPS_PRIORITY);
  if (status != ARES_SUCCESS) {
    return status;
  }

  status =
    ares_dns_parse_and_set_dns_name(buf, ARES_FALSE, rr, ARES_RR_HTTPS_TARGET);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* Parse params */
  while (ares_dns_rr_remaining_len(buf, orig_len, rdlength)) {
    unsigned short opt = 0;
    unsigned short len = 0;
    unsigned char *val = NULL;

    /* Fetch be16 option */
    status = ares__buf_fetch_be16(buf, &opt);
    if (status != ARES_SUCCESS) {
      return status;
    }

    /* Fetch be16 length */
    status = ares__buf_fetch_be16(buf, &len);
    if (status != ARES_SUCCESS) {
      return status;
    }

    if (len) {
      status = ares__buf_fetch_bytes_dup(buf, len, ARES_TRUE, &val);
      if (status != ARES_SUCCESS) {
        return status;
      }
    }

    status = ares_dns_rr_set_opt_own(rr, ARES_RR_HTTPS_PARAMS, opt, val, len);
    if (status != ARES_SUCCESS) {
      return status;
    }
  }

  return ARES_SUCCESS;
}

static ares_status_t ares_dns_parse_rr_uri(ares__buf_t *buf, ares_dns_rr_t *rr,
                                           size_t rdlength)
{
  char         *name = NULL;
  ares_status_t status;
  size_t        orig_len = ares__buf_len(buf);
  size_t        remaining_len;

  /* PRIORITY */
  status = ares_dns_parse_and_set_be16(buf, rr, ARES_RR_URI_PRIORITY);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* WEIGHT */
  status = ares_dns_parse_and_set_be16(buf, rr, ARES_RR_URI_WEIGHT);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* TARGET -- not in string format, rest of buffer, required to be
   * non-zero length */
  remaining_len = ares_dns_rr_remaining_len(buf, orig_len, rdlength);
  if (remaining_len == 0) {
    status = ARES_EBADRESP;
    return status;
  }

  /* NOTE: Not in DNS string format */
  status = ares__buf_fetch_str_dup(buf, remaining_len, &name);
  if (status != ARES_SUCCESS) {
    return status;
  }

  if (!ares__str_isprint(name, remaining_len)) {
    ares_free(name);
    return ARES_EBADRESP;
  }

  status = ares_dns_rr_set_str_own(rr, ARES_RR_URI_TARGET, name);
  if (status != ARES_SUCCESS) {
    ares_free(name);
    return status;
  }
  name = NULL;

  return ARES_SUCCESS;
}

static ares_status_t ares_dns_parse_rr_caa(ares__buf_t *buf, ares_dns_rr_t *rr,
                                           size_t rdlength)
{
  unsigned char *data     = NULL;
  size_t         data_len = 0;
  ares_status_t  status;
  size_t         orig_len = ares__buf_len(buf);

  /* CRITICAL */
  status = ares_dns_parse_and_set_u8(buf, rr, ARES_RR_CAA_CRITICAL);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* Tag */
  status = ares_dns_parse_and_set_dns_str(
    buf, ares_dns_rr_remaining_len(buf, orig_len, rdlength), rr,
    ARES_RR_CAA_TAG, ARES_FALSE);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* Value - binary! (remaining buffer */
  data_len = ares_dns_rr_remaining_len(buf, orig_len, rdlength);
  if (data_len == 0) {
    status = ARES_EBADRESP;
    return status;
  }
  status = ares__buf_fetch_bytes_dup(buf, data_len, ARES_TRUE, &data);
  if (status != ARES_SUCCESS) {
    return status;
  }

  status = ares_dns_rr_set_bin_own(rr, ARES_RR_CAA_VALUE, data, data_len);
  if (status != ARES_SUCCESS) {
    ares_free(data);
    return status;
  }
  data = NULL;

  return ARES_SUCCESS;
}

static ares_status_t ares_dns_parse_rr_raw_rr(ares__buf_t   *buf,
                                              ares_dns_rr_t *rr,
                                              size_t         rdlength,
                                              unsigned short raw_type)
{
  ares_status_t  status;
  unsigned char *bytes = NULL;

  if (rdlength == 0) {
    return ARES_SUCCESS;
  }

  status = ares__buf_fetch_bytes_dup(buf, rdlength, ARES_FALSE, &bytes);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* Can't fail */
  status = ares_dns_rr_set_u16(rr, ARES_RR_RAW_RR_TYPE, raw_type);
  if (status != ARES_SUCCESS) {
    ares_free(bytes);
    return status;
  }

  status = ares_dns_rr_set_bin_own(rr, ARES_RR_RAW_RR_DATA, bytes, rdlength);
  if (status != ARES_SUCCESS) {
    ares_free(bytes);
    return status;
  }

  return ARES_SUCCESS;
}

static ares_status_t ares_dns_parse_header(ares__buf_t *buf, unsigned int flags,
                                           ares_dns_record_t **dnsrec,
                                           unsigned short     *qdcount,
                                           unsigned short     *ancount,
                                           unsigned short     *nscount,
                                           unsigned short     *arcount)
{
  ares_status_t     status = ARES_EBADRESP;
  unsigned short    u16;
  unsigned short    id;
  unsigned short    dns_flags = 0;
  ares_dns_opcode_t opcode;
  unsigned short    rcode;

  (void)flags; /* currently unused */

  if (buf == NULL || dnsrec == NULL || qdcount == NULL || ancount == NULL ||
      nscount == NULL || arcount == NULL) {
    return ARES_EFORMERR;
  }

  *dnsrec = NULL;

  /*
   *  RFC 1035 4.1.1. Header section format.
   *  and Updated by RFC 2065 to add AD and CD bits.
   *                                  1  1  1  1  1  1
   *    0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
   *  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   *  |                      ID                       |
   *  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   *  |QR|   Opcode  |AA|TC|RD|RA| Z|AD|CD|   RCODE   |
   *  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   *  |                    QDCOUNT                    |
   *  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   *  |                    ANCOUNT                    |
   *  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   *  |                    NSCOUNT                    |
   *  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   *  |                    ARCOUNT                    |
   *  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   */

  /* ID */
  status = ares__buf_fetch_be16(buf, &id);
  if (status != ARES_SUCCESS) {
    goto fail;
  }

  /* Flags */
  status = ares__buf_fetch_be16(buf, &u16);
  if (status != ARES_SUCCESS) {
    goto fail;
  }

  /* QR */
  if (u16 & 0x8000) {
    dns_flags |= ARES_FLAG_QR;
  }

  /* OPCODE */
  opcode = (u16 >> 11) & 0xf;

  /* AA */
  if (u16 & 0x400) {
    dns_flags |= ARES_FLAG_AA;
  }

  /* TC */
  if (u16 & 0x200) {
    dns_flags |= ARES_FLAG_TC;
  }

  /* RD */
  if (u16 & 0x100) {
    dns_flags |= ARES_FLAG_RD;
  }

  /* RA */
  if (u16 & 0x80) {
    dns_flags |= ARES_FLAG_RA;
  }

  /* Z -- unused */

  /* AD */
  if (u16 & 0x20) {
    dns_flags |= ARES_FLAG_AD;
  }

  /* CD */
  if (u16 & 0x10) {
    dns_flags |= ARES_FLAG_CD;
  }

  /* RCODE */
  rcode = u16 & 0xf;

  /* QDCOUNT */
  status = ares__buf_fetch_be16(buf, qdcount);
  if (status != ARES_SUCCESS) {
    goto fail;
  }

  /* ANCOUNT */
  status = ares__buf_fetch_be16(buf, ancount);
  if (status != ARES_SUCCESS) {
    goto fail;
  }

  /* NSCOUNT */
  status = ares__buf_fetch_be16(buf, nscount);
  if (status != ARES_SUCCESS) {
    goto fail;
  }

  /* ARCOUNT */
  status = ares__buf_fetch_be16(buf, arcount);
  if (status != ARES_SUCCESS) {
    goto fail;
  }

  status = ares_dns_record_create(dnsrec, id, dns_flags, opcode,
                                  ARES_RCODE_NOERROR /* Temporary */);
  if (status != ARES_SUCCESS) {
    goto fail;
  }

  (*dnsrec)->raw_rcode = rcode;

  if (*ancount > 0) {
    status =
      ares_dns_record_rr_prealloc(*dnsrec, ARES_SECTION_ANSWER, *ancount);
    if (status != ARES_SUCCESS) {
      goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  if (*nscount > 0) {
    status =
      ares_dns_record_rr_prealloc(*dnsrec, ARES_SECTION_AUTHORITY, *nscount);
    if (status != ARES_SUCCESS) {
      goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  if (*arcount > 0) {
    status =
      ares_dns_record_rr_prealloc(*dnsrec, ARES_SECTION_ADDITIONAL, *arcount);
    if (status != ARES_SUCCESS) {
      goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  return ARES_SUCCESS;

fail:
  ares_dns_record_destroy(*dnsrec);
  *dnsrec  = NULL;
  *qdcount = 0;
  *ancount = 0;
  *nscount = 0;
  *arcount = 0;

  return status;
}

static ares_status_t
  ares_dns_parse_rr_data(ares__buf_t *buf, size_t rdlength, ares_dns_rr_t *rr,
                         ares_dns_rec_type_t type, unsigned short raw_type,
                         unsigned short raw_class, unsigned int raw_ttl)
{
  switch (type) {
    case ARES_REC_TYPE_A:
      return ares_dns_parse_rr_a(buf, rr, rdlength);
    case ARES_REC_TYPE_NS:
      return ares_dns_parse_rr_ns(buf, rr, rdlength);
    case ARES_REC_TYPE_CNAME:
      return ares_dns_parse_rr_cname(buf, rr, rdlength);
    case ARES_REC_TYPE_SOA:
      return ares_dns_parse_rr_soa(buf, rr, rdlength);
    case ARES_REC_TYPE_PTR:
      return ares_dns_parse_rr_ptr(buf, rr, rdlength);
    case ARES_REC_TYPE_HINFO:
      return ares_dns_parse_rr_hinfo(buf, rr, rdlength);
    case ARES_REC_TYPE_MX:
      return ares_dns_parse_rr_mx(buf, rr, rdlength);
    case ARES_REC_TYPE_TXT:
      return ares_dns_parse_rr_txt(buf, rr, rdlength);
    case ARES_REC_TYPE_SIG:
      return ares_dns_parse_rr_sig(buf, rr, rdlength);
    case ARES_REC_TYPE_AAAA:
      return ares_dns_parse_rr_aaaa(buf, rr, rdlength);
    case ARES_REC_TYPE_SRV:
      return ares_dns_parse_rr_srv(buf, rr, rdlength);
    case ARES_REC_TYPE_NAPTR:
      return ares_dns_parse_rr_naptr(buf, rr, rdlength);
    case ARES_REC_TYPE_ANY:
      return ARES_EBADRESP;
    case ARES_REC_TYPE_OPT:
      return ares_dns_parse_rr_opt(buf, rr, rdlength, raw_class, raw_ttl);
    case ARES_REC_TYPE_TLSA:
      return ares_dns_parse_rr_tlsa(buf, rr, rdlength);
    case ARES_REC_TYPE_SVCB:
      return ares_dns_parse_rr_svcb(buf, rr, rdlength);
    case ARES_REC_TYPE_HTTPS:
      return ares_dns_parse_rr_https(buf, rr, rdlength);
    case ARES_REC_TYPE_URI:
      return ares_dns_parse_rr_uri(buf, rr, rdlength);
    case ARES_REC_TYPE_CAA:
      return ares_dns_parse_rr_caa(buf, rr, rdlength);
    case ARES_REC_TYPE_RAW_RR:
      return ares_dns_parse_rr_raw_rr(buf, rr, rdlength, raw_type);
  }
  return ARES_EFORMERR;
}

static ares_status_t ares_dns_parse_qd(ares__buf_t       *buf,
                                       ares_dns_record_t *dnsrec)
{
  char               *name = NULL;
  unsigned short      u16;
  ares_status_t       status;
  ares_dns_rec_type_t type;
  ares_dns_class_t    qclass;
  /* The question section is used to carry the "question" in most queries,
   * i.e., the parameters that define what is being asked.  The section
   * contains QDCOUNT (usually 1) entries, each of the following format:
   *                                 1  1  1  1  1  1
   *   0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
   * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   * |                                               |
   * /                     QNAME                     /
   * /                                               /
   * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   * |                     QTYPE                     |
   * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   * |                     QCLASS                    |
   * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   */

  /* Name */
  status = ares__dns_name_parse(buf, &name, ARES_FALSE);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  /* Type */
  status = ares__buf_fetch_be16(buf, &u16);
  if (status != ARES_SUCCESS) {
    goto done;
  }
  type = u16;

  /* Class */
  status = ares__buf_fetch_be16(buf, &u16);
  if (status != ARES_SUCCESS) {
    goto done;
  }
  qclass = u16;

  /* Add question */
  status = ares_dns_record_query_add(dnsrec, name, type, qclass);
  if (status != ARES_SUCCESS) {
    goto done;
  }

done:
  ares_free(name);
  return status;
}

static ares_status_t ares_dns_parse_rr(ares__buf_t *buf, unsigned int flags,
                                       ares_dns_section_t sect,
                                       ares_dns_record_t *dnsrec)
{
  char               *name = NULL;
  unsigned short      u16;
  unsigned short      raw_type;
  ares_status_t       status;
  ares_dns_rec_type_t type;
  ares_dns_class_t    qclass;
  unsigned int        ttl;
  size_t              rdlength;
  ares_dns_rr_t      *rr            = NULL;
  size_t              remaining_len = 0;
  size_t              processed_len = 0;
  ares_bool_t         namecomp;

  /* All RRs have the same top level format shown below:
   *                                 1  1  1  1  1  1
   *   0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
   * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   * |                                               |
   * /                                               /
   * /                      NAME                     /
   * |                                               |
   * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   * |                      TYPE                     |
   * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   * |                     CLASS                     |
   * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   * |                      TTL                      |
   * |                                               |
   * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   * |                   RDLENGTH                    |
   * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--|
   * /                     RDATA                     /
   * /                                               /
   * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   */

  /* Name */
  status = ares__dns_name_parse(buf, &name, ARES_FALSE);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  /* Type */
  status = ares__buf_fetch_be16(buf, &u16);
  if (status != ARES_SUCCESS) {
    goto done;
  }
  type     = u16;
  raw_type = u16; /* Only used for raw rr data */

  /* Class */
  status = ares__buf_fetch_be16(buf, &u16);
  if (status != ARES_SUCCESS) {
    goto done;
  }
  qclass = u16;

  /* TTL */
  status = ares__buf_fetch_be32(buf, &ttl);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  /* Length */
  status = ares__buf_fetch_be16(buf, &u16);
  if (status != ARES_SUCCESS) {
    goto done;
  }
  rdlength = u16;

  if (!ares_dns_rec_type_isvalid(type, ARES_FALSE)) {
    type = ARES_REC_TYPE_RAW_RR;
  }

  namecomp = ares_dns_rec_type_allow_name_compression(type);
  if (sect == ARES_SECTION_ANSWER &&
      (flags &
       (namecomp ? ARES_DNS_PARSE_AN_BASE_RAW : ARES_DNS_PARSE_AN_EXT_RAW))) {
    type = ARES_REC_TYPE_RAW_RR;
  }
  if (sect == ARES_SECTION_AUTHORITY &&
      (flags &
       (namecomp ? ARES_DNS_PARSE_NS_BASE_RAW : ARES_DNS_PARSE_NS_EXT_RAW))) {
    type = ARES_REC_TYPE_RAW_RR;
  }
  if (sect == ARES_SECTION_ADDITIONAL &&
      (flags &
       (namecomp ? ARES_DNS_PARSE_AR_BASE_RAW : ARES_DNS_PARSE_AR_EXT_RAW))) {
    type = ARES_REC_TYPE_RAW_RR;
  }

  /* Pull into another buffer for safety */
  if (rdlength > ares__buf_len(buf)) {
    status = ARES_EBADRESP;
    goto done;
  }

  /* Add the base rr */
  status =
    ares_dns_record_rr_add(&rr, dnsrec, sect, name, type,
                           type == ARES_REC_TYPE_OPT ? ARES_CLASS_IN : qclass,
                           type == ARES_REC_TYPE_OPT ? 0 : ttl);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  /* Record the current remaining length in the buffer so we can tell how
   * much was processed */
  remaining_len = ares__buf_len(buf);

  /* Fill in the data for the rr */
  status = ares_dns_parse_rr_data(buf, rdlength, rr, type, raw_type,
                                  (unsigned short)qclass, ttl);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  /* Determine how many bytes were processed */
  processed_len = remaining_len - ares__buf_len(buf);

  /* If too many bytes were processed, error! */
  if (processed_len > rdlength) {
    status = ARES_EBADRESP;
    goto done;
  }

  /* If too few bytes were processed, consume the unprocessed data for this
   * record as the parser may not have wanted/needed to use it */
  if (processed_len < rdlength) {
    ares__buf_consume(buf, rdlength - processed_len);
  }


done:
  ares_free(name);
  return status;
}

static ares_status_t ares_dns_parse_buf(ares__buf_t *buf, unsigned int flags,
                                        ares_dns_record_t **dnsrec)
{
  ares_status_t  status;
  unsigned short qdcount;
  unsigned short ancount;
  unsigned short nscount;
  unsigned short arcount;
  unsigned short i;

  if (buf == NULL || dnsrec == NULL) {
    return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  /* Maximum DNS packet size is 64k, even over TCP */
  if (ares__buf_len(buf) > 0xFFFF) {
    return ARES_EFORMERR;
  }

  /* All communications inside of the domain protocol are carried in a single
   * format called a message.  The top level format of message is divided
   * into 5 sections (some of which are empty in certain cases) shown below:
   *
   * +---------------------+
   * |        Header       |
   * +---------------------+
   * |       Question      | the question for the name server
   * +---------------------+
   * |        Answer       | RRs answering the question
   * +---------------------+
   * |      Authority      | RRs pointing toward an authority
   * +---------------------+
   * |      Additional     | RRs holding additional information
   * +---------------------+
   */

  /* Parse header */
  status = ares_dns_parse_header(buf, flags, dnsrec, &qdcount, &ancount,
                                 &nscount, &arcount);
  if (status != ARES_SUCCESS) {
    goto fail;
  }

  /* Must have questions */
  if (qdcount == 0) {
    status = ARES_EBADRESP;
    goto fail;
  }

  /* XXX: this should be controlled by a flag in case we want to allow
   *      multiple questions.  I think mDNS allows this */
  if (qdcount > 1) {
    status = ARES_EBADRESP;
    goto fail;
  }

  /* Parse questions */
  for (i = 0; i < qdcount; i++) {
    status = ares_dns_parse_qd(buf, *dnsrec);
    if (status != ARES_SUCCESS) {
      goto fail;
    }
  }

  /* Parse Answers */
  for (i = 0; i < ancount; i++) {
    status = ares_dns_parse_rr(buf, flags, ARES_SECTION_ANSWER, *dnsrec);
    if (status != ARES_SUCCESS) {
      goto fail;
    }
  }

  /* Parse Authority */
  for (i = 0; i < nscount; i++) {
    status = ares_dns_parse_rr(buf, flags, ARES_SECTION_AUTHORITY, *dnsrec);
    if (status != ARES_SUCCESS) {
      goto fail;
    }
  }

  /* Parse Additional */
  for (i = 0; i < arcount; i++) {
    status = ares_dns_parse_rr(buf, flags, ARES_SECTION_ADDITIONAL, *dnsrec);
    if (status != ARES_SUCCESS) {
      goto fail;
    }
  }

  /* Finalize rcode now that if we have OPT it is processed */
  if (!ares_dns_rcode_isvalid((*dnsrec)->raw_rcode)) {
    (*dnsrec)->rcode = ARES_RCODE_SERVFAIL;
  } else {
    (*dnsrec)->rcode = (ares_dns_rcode_t)(*dnsrec)->raw_rcode;
  }

  return ARES_SUCCESS;

fail:
  ares_dns_record_destroy(*dnsrec);
  *dnsrec = NULL;
  return status;
}

ares_status_t ares_dns_parse(const unsigned char *buf, size_t buf_len,
                             unsigned int flags, ares_dns_record_t **dnsrec)
{
  ares__buf_t  *parser = NULL;
  ares_status_t status;

  if (buf == NULL || buf_len == 0 || dnsrec == NULL) {
    return ARES_EFORMERR;
  }

  parser = ares__buf_create_const(buf, buf_len);
  if (parser == NULL) {
    return ARES_ENOMEM;
  }

  status = ares_dns_parse_buf(parser, flags, dnsrec);
  ares__buf_destroy(parser);

  return status;
}
