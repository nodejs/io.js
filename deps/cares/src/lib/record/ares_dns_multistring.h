/* MIT License
 *
 * Copyright (c) 2024 Brad House
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
#ifndef __ARES_DNS_MULTISTRING_H
#define __ARES_DNS_MULTISTRING_H

#include "ares_buf.h"

struct ares_dns_multistring;
typedef struct ares_dns_multistring ares_dns_multistring_t;

ares_dns_multistring_t             *ares_dns_multistring_create(void);
void          ares_dns_multistring_clear(ares_dns_multistring_t *strs);
void          ares_dns_multistring_destroy(ares_dns_multistring_t *strs);
ares_status_t ares_dns_multistring_swap_own(ares_dns_multistring_t *strs,
                                            size_t idx, unsigned char *str,
                                            size_t len);
ares_status_t ares_dns_multistring_del(ares_dns_multistring_t *strs,
                                       size_t                  idx);
ares_status_t ares_dns_multistring_add_own(ares_dns_multistring_t *strs,
                                           unsigned char *str, size_t len);
size_t        ares_dns_multistring_cnt(const ares_dns_multistring_t *strs);
const unsigned char *
  ares_dns_multistring_get(const ares_dns_multistring_t *strs, size_t idx,
                           size_t *len);
const unsigned char *ares_dns_multistring_combined(ares_dns_multistring_t *strs,
                                                   size_t                 *len);

/*! Parse an array of character strings as defined in RFC1035, as binary,
 *  however, for convenience this does guarantee a NULL terminator (that is
 *  not included in the length for each value).
 *
 *  \param[in]  buf                initialized buffer object
 *  \param[in]  remaining_len      maximum length that should be used for
 *                                 parsing the string, this is often less than
 *                                 the remaining buffer and is based on the RR
 *                                 record length.
 *  \param[out] strs               Pointer passed by reference to be filled in
 *                                 with
 *                                 the array of values.
 *  \param[out] validate_printable Validate the strings contain only printable
 *                                 data.
 *  \return ARES_SUCCESS on success
 */
ares_status_t        ares_dns_multistring_parse_buf(ares_buf_t *buf,
                                                    size_t      remaining_len,
                                                    ares_dns_multistring_t **strs,
                                                    ares_bool_t validate_printable);

#endif
