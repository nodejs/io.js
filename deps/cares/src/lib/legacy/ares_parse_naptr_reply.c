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

int ares_parse_naptr_reply(const unsigned char *abuf, int alen_int,
                           struct ares_naptr_reply **naptr_out)
{
  ares_status_t            status;
  size_t                   alen;
  struct ares_naptr_reply *naptr_head = NULL;
  struct ares_naptr_reply *naptr_last = NULL;
  struct ares_naptr_reply *naptr_curr;
  ares_dns_record_t       *dnsrec = NULL;
  size_t                   i;

  *naptr_out = NULL;

  if (alen_int < 0) {
    return ARES_EBADRESP;
  }

  alen = (size_t)alen_int;

  status = ares_dns_parse(abuf, alen, 0, &dnsrec);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  if (ares_dns_record_rr_cnt(dnsrec, ARES_SECTION_ANSWER) == 0) {
    status = ARES_ENODATA;
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
        ares_dns_rr_get_type(rr) != ARES_REC_TYPE_NAPTR) {
      continue;
    }

    /* Allocate storage for this NAPTR answer appending it to the list */
    naptr_curr = ares_malloc_data(ARES_DATATYPE_NAPTR_REPLY);
    if (naptr_curr == NULL) {
      status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
      goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
    }

    /* Link in the record */
    if (naptr_last) {
      naptr_last->next = naptr_curr;
    } else {
      naptr_head = naptr_curr;
    }
    naptr_last = naptr_curr;

    naptr_curr->order      = ares_dns_rr_get_u16(rr, ARES_RR_NAPTR_ORDER);
    naptr_curr->preference = ares_dns_rr_get_u16(rr, ARES_RR_NAPTR_PREFERENCE);

    /* XXX: Why is this unsigned char * ? */
    naptr_curr->flags = (unsigned char *)ares_strdup(
      ares_dns_rr_get_str(rr, ARES_RR_NAPTR_FLAGS));
    if (naptr_curr->flags == NULL) {
      status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
      goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
    }
    /* XXX: Why is this unsigned char * ? */
    naptr_curr->service = (unsigned char *)ares_strdup(
      ares_dns_rr_get_str(rr, ARES_RR_NAPTR_SERVICES));
    if (naptr_curr->service == NULL) {
      status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
      goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
    }
    /* XXX: Why is this unsigned char * ? */
    naptr_curr->regexp = (unsigned char *)ares_strdup(
      ares_dns_rr_get_str(rr, ARES_RR_NAPTR_REGEXP));
    if (naptr_curr->regexp == NULL) {
      status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
      goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
    }
    naptr_curr->replacement =
      ares_strdup(ares_dns_rr_get_str(rr, ARES_RR_NAPTR_REPLACEMENT));
    if (naptr_curr->replacement == NULL) {
      status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
      goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

done:
  /* clean up on error */
  if (status != ARES_SUCCESS) {
    if (naptr_head) {
      ares_free_data(naptr_head);
    }
  } else {
    /* everything looks fine, return the data */
    *naptr_out = naptr_head;
  }
  ares_dns_record_destroy(dnsrec);
  return (int)status;
}
