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

static int ares_create_query_int(const char *name, int dnsclass, int type,
                                 unsigned short id, int rd,
                                 unsigned char **bufp, int *buflenp,
                                 int max_udp_size)
{
  ares_status_t      status;
  ares_dns_record_t *dnsrec = NULL;
  size_t             len;
  ares_dns_flags_t   rd_flag = rd ? ARES_FLAG_RD : 0;

  if (name == NULL || bufp == NULL || buflenp == NULL) {
    status = ARES_EFORMERR;
    goto done;
  }

  *bufp    = NULL;
  *buflenp = 0;

  status = ares_dns_record_create_query(
    &dnsrec, name, (ares_dns_class_t)dnsclass, (ares_dns_rec_type_t)type, id,
    rd_flag, (size_t)max_udp_size);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  status = ares_dns_write(dnsrec, bufp, &len);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  *buflenp = (int)len;

done:
  ares_dns_record_destroy(dnsrec);
  return (int)status;
}

int ares_create_query(const char *name, int dnsclass, int type,
                      unsigned short id, int rd, unsigned char **bufp,
                      int *buflenp, int max_udp_size)
{
  return ares_create_query_int(name, dnsclass, type, id, rd, bufp, buflenp,
                               max_udp_size);
}

int ares_mkquery(const char *name, int dnsclass, int type, unsigned short id,
                 int rd, unsigned char **buf, int *buflen)
{
  return ares_create_query_int(name, dnsclass, type, id, rd, buf, buflen, 0);
}
