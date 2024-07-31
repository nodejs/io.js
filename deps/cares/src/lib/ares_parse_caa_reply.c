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

int ares_parse_caa_reply(const unsigned char *abuf, int alen_int,
                         struct ares_caa_reply **caa_out)
{
  ares_status_t          status;
  size_t                 alen;
  struct ares_caa_reply *caa_head = NULL;
  struct ares_caa_reply *caa_last = NULL;
  struct ares_caa_reply *caa_curr;
  ares_dns_record_t     *dnsrec = NULL;
  size_t                 i;

  *caa_out = NULL;

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
    const unsigned char *ptr;
    size_t               ptr_len;
    const ares_dns_rr_t *rr =
      ares_dns_record_rr_get(dnsrec, ARES_SECTION_ANSWER, i);

    if (rr == NULL) {
      /* Shouldn't be possible */
      status = ARES_EBADRESP; /* LCOV_EXCL_LINE: DefensiveCoding */
      goto done;              /* LCOV_EXCL_LINE: DefensiveCoding */
    }

    /* XXX: Why do we allow Chaos class? */
    if (ares_dns_rr_get_class(rr) != ARES_CLASS_IN &&
        ares_dns_rr_get_class(rr) != ARES_CLASS_CHAOS) {
      continue;
    }

    /* Only looking for CAA records */
    if (ares_dns_rr_get_type(rr) != ARES_REC_TYPE_CAA) {
      continue;
    }

    /* Allocate storage for this CAA answer appending it to the list */
    caa_curr = ares_malloc_data(ARES_DATATYPE_CAA_REPLY);
    if (caa_curr == NULL) {
      status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
      goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
    }

    /* Link in the record */
    if (caa_last) {
      caa_last->next = caa_curr;
    } else {
      caa_head = caa_curr;
    }
    caa_last = caa_curr;

    caa_curr->critical = ares_dns_rr_get_u8(rr, ARES_RR_CAA_CRITICAL);
    caa_curr->property =
      (unsigned char *)ares_strdup(ares_dns_rr_get_str(rr, ARES_RR_CAA_TAG));
    if (caa_curr->property == NULL) {
      status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
      break;                /* LCOV_EXCL_LINE: OutOfMemory */
    }
    /* RFC6844 says this can only be ascii, so not sure why we're recording a
     * length */
    caa_curr->plength = ares_strlen((const char *)caa_curr->property);

    ptr = ares_dns_rr_get_bin(rr, ARES_RR_CAA_VALUE, &ptr_len);
    if (ptr == NULL) {
      status = ARES_EBADRESP; /* LCOV_EXCL_LINE: DefensiveCoding */
      goto done;              /* LCOV_EXCL_LINE: DefensiveCoding */
    }

    /* Wants NULL termination for some reason */
    caa_curr->value = ares_malloc(ptr_len + 1);
    if (caa_curr->value == NULL) {
      status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
      goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
    }
    memcpy(caa_curr->value, ptr, ptr_len);
    caa_curr->value[ptr_len] = 0;
    caa_curr->length         = ptr_len;
  }

done:
  /* clean up on error */
  if (status != ARES_SUCCESS) {
    if (caa_head) {
      ares_free_data(caa_head);
    }
  } else {
    /* everything looks fine, return the data */
    *caa_out = caa_head;
  }
  ares_dns_record_destroy(dnsrec);
  return (int)status;
}
