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

#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif

typedef struct {
  ares_callback_dnsrec callback;
  void                *arg;
} ares_query_dnsrec_arg_t;

static void ares_query_dnsrec_cb(void *arg, ares_status_t status,
                                 size_t                   timeouts,
                                 const ares_dns_record_t *dnsrec)
{
  ares_query_dnsrec_arg_t *qquery = arg;

  if (status != ARES_SUCCESS) {
    qquery->callback(qquery->arg, status, timeouts, dnsrec);
  } else {
    size_t           ancount;
    ares_dns_rcode_t rcode;
    /* Pull the response code and answer count from the packet and convert any
     * errors.
     */
    rcode   = ares_dns_record_get_rcode(dnsrec);
    ancount = ares_dns_record_rr_cnt(dnsrec, ARES_SECTION_ANSWER);
    status  = ares_dns_query_reply_tostatus(rcode, ancount);
    qquery->callback(qquery->arg, status, timeouts, dnsrec);
  }
  ares_free(qquery);
}

ares_status_t ares_query_nolock(ares_channel_t *channel, const char *name,
                                ares_dns_class_t     dnsclass,
                                ares_dns_rec_type_t  type,
                                ares_callback_dnsrec callback, void *arg,
                                unsigned short *qid)
{
  ares_status_t            status;
  ares_dns_record_t       *dnsrec = NULL;
  ares_dns_flags_t         flags  = 0;
  ares_query_dnsrec_arg_t *qquery = NULL;

  if (channel == NULL || name == NULL || callback == NULL) {
    /* LCOV_EXCL_START: DefensiveCoding */
    status = ARES_EFORMERR;
    if (callback != NULL) {
      callback(arg, status, 0, NULL);
    }
    return status;
    /* LCOV_EXCL_STOP */
  }

  if (!(channel->flags & ARES_FLAG_NORECURSE)) {
    flags |= ARES_FLAG_RD;
  }

  status = ares_dns_record_create_query(
    &dnsrec, name, dnsclass, type, 0, flags,
    (size_t)(channel->flags & ARES_FLAG_EDNS) ? channel->ednspsz : 0);
  if (status != ARES_SUCCESS) {
    callback(arg, status, 0, NULL); /* LCOV_EXCL_LINE: OutOfMemory */
    return status;                  /* LCOV_EXCL_LINE: OutOfMemory */
  }

  qquery = ares_malloc(sizeof(*qquery));
  if (qquery == NULL) {
    /* LCOV_EXCL_START: OutOfMemory */
    status = ARES_ENOMEM;
    callback(arg, status, 0, NULL);
    ares_dns_record_destroy(dnsrec);
    return status;
    /* LCOV_EXCL_STOP */
  }

  qquery->callback = callback;
  qquery->arg      = arg;

  /* Send it off.  qcallback will be called when we get an answer. */
  status = ares_send_nolock(channel, NULL, 0, dnsrec, ares_query_dnsrec_cb,
                            qquery, qid);

  ares_dns_record_destroy(dnsrec);
  return status;
}

ares_status_t ares_query_dnsrec(ares_channel_t *channel, const char *name,
                                ares_dns_class_t     dnsclass,
                                ares_dns_rec_type_t  type,
                                ares_callback_dnsrec callback, void *arg,
                                unsigned short *qid)
{
  ares_status_t status;

  if (channel == NULL) {
    return ARES_EFORMERR;
  }

  ares_channel_lock(channel);
  status = ares_query_nolock(channel, name, dnsclass, type, callback, arg, qid);
  ares_channel_unlock(channel);
  return status;
}

void ares_query(ares_channel_t *channel, const char *name, int dnsclass,
                int type, ares_callback callback, void *arg)
{
  void *carg = NULL;

  if (channel == NULL) {
    return;
  }

  carg = ares_dnsrec_convert_arg(callback, arg);
  if (carg == NULL) {
    callback(arg, ARES_ENOMEM, 0, NULL, 0); /* LCOV_EXCL_LINE: OutOfMemory */
    return;                                 /* LCOV_EXCL_LINE: OutOfMemory */
  }

  ares_query_dnsrec(channel, name, (ares_dns_class_t)dnsclass,
                    (ares_dns_rec_type_t)type, ares_dnsrec_convert_cb, carg,
                    NULL);
}
