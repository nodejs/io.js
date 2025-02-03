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
#include "ares_data.h"

int ares_parse_soa_reply(const unsigned char *abuf, int alen_int,
                         struct ares_soa_reply **soa_out)
{
  ares_status_t          status;
  size_t                 alen;
  struct ares_soa_reply *soa    = NULL;
  ares_dns_record_t     *dnsrec = NULL;
  size_t                 i;

  *soa_out = NULL;

  if (alen_int < 0) {
    return ARES_EBADRESP;
  }

  alen = (size_t)alen_int;

  status = ares_dns_parse(abuf, alen, 0, &dnsrec);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  if (ares_dns_record_rr_cnt(dnsrec, ARES_SECTION_ANSWER) == 0) {
    status = ARES_EBADRESP; /* ENODATA might make more sense */
    goto done;
  }

  for (i = 0; i < ares_dns_record_rr_cnt(dnsrec, ARES_SECTION_ANSWER); i++) {
    const ares_dns_rr_t *rr =
      ares_dns_record_rr_get(dnsrec, ARES_SECTION_ANSWER, i);

    if (rr == NULL) {
      /* Shouldn't be possible */
      status = ARES_EBADRESP; /* LCOV_EXCL_LINE: DefensiveCoding */
      goto done;              /* LCOV_EXCL_LINE: DefensiveCoding */
    }

    if (ares_dns_rr_get_class(rr) != ARES_CLASS_IN ||
        ares_dns_rr_get_type(rr) != ARES_REC_TYPE_SOA) {
      continue;
    }

    /* allocate result struct */
    soa = ares_malloc_data(ARES_DATATYPE_SOA_REPLY);
    if (soa == NULL) {
      status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
      goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
    }

    soa->serial  = ares_dns_rr_get_u32(rr, ARES_RR_SOA_SERIAL);
    soa->refresh = ares_dns_rr_get_u32(rr, ARES_RR_SOA_REFRESH);
    soa->retry   = ares_dns_rr_get_u32(rr, ARES_RR_SOA_RETRY);
    soa->expire  = ares_dns_rr_get_u32(rr, ARES_RR_SOA_EXPIRE);
    soa->minttl  = ares_dns_rr_get_u32(rr, ARES_RR_SOA_MINIMUM);
    soa->nsname  = ares_strdup(ares_dns_rr_get_str(rr, ARES_RR_SOA_MNAME));
    if (soa->nsname == NULL) {
      status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
      goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
    }
    soa->hostmaster = ares_strdup(ares_dns_rr_get_str(rr, ARES_RR_SOA_RNAME));
    if (soa->hostmaster == NULL) {
      status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
      goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
    }
    break;
  }

  if (soa == NULL) {
    status = ARES_EBADRESP;
  }

done:
  /* clean up on error */
  if (status != ARES_SUCCESS) {
    ares_free_data(soa);
    /* Compatibility */
    if (status == ARES_EBADNAME) {
      status = ARES_EBADRESP;
    }
  } else {
    /* everything looks fine, return the data */
    *soa_out = soa;
  }
  ares_dns_record_destroy(dnsrec);
  return (int)status;
}
