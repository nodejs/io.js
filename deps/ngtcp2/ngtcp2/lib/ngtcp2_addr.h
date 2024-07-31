/*
 * ngtcp2
 *
 * Copyright (c) 2019 ngtcp2 contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef NGTCP2_ADDR_H
#define NGTCP2_ADDR_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <ngtcp2/ngtcp2.h>

/*
 * ngtcp2_addr_copy copies |src| to |dest|.  This function assumes
 * that dest->addr points to a buffer which have sufficient size to
 * store the copy.
 */
void ngtcp2_addr_copy(ngtcp2_addr *dest, const ngtcp2_addr *src);

/**
 * @function
 *
 * `ngtcp2_addr_eq` returns nonzero if |a| equals |b|.
 */
int ngtcp2_addr_eq(const ngtcp2_addr *a, const ngtcp2_addr *b);

/* NGTCP2_ADDR_COMPARE_FLAG_NONE indicates that no flag set. */
#define NGTCP2_ADDR_COMPARE_FLAG_NONE 0x0u
/* NGTCP2_ADDR_COMPARE_FLAG_ADDR indicates IP addresses do not
   match. */
#define NGTCP2_ADDR_COMPARE_FLAG_ADDR 0x1u
/* NGTCP2_ADDR_COMPARE_FLAG_PORT indicates ports do not match. */
#define NGTCP2_ADDR_COMPARE_FLAG_PORT 0x2u
/* NGTCP2_ADDR_COMPARE_FLAG_FAMILY indicates address families do not
   match. */
#define NGTCP2_ADDR_COMPARE_FLAG_FAMILY 0x4u

/*
 * ngtcp2_addr_compare compares address and port between |a| and |b|,
 * and returns zero or more of NGTCP2_ADDR_COMPARE_FLAG_*.
 */
uint32_t ngtcp2_addr_compare(const ngtcp2_addr *a, const ngtcp2_addr *b);

/*
 * ngtcp2_addr_empty returns nonzero if |addr| has zero length
 * address.
 */
int ngtcp2_addr_empty(const ngtcp2_addr *addr);

#endif /* NGTCP2_ADDR_H */
