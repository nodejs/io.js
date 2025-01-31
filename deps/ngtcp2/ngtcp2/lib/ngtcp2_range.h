/*
 * ngtcp2
 *
 * Copyright (c) 2017 ngtcp2 contributors
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
#ifndef NGTCP2_RANGE_H
#define NGTCP2_RANGE_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <ngtcp2/ngtcp2.h>

/*
 * ngtcp2_range represents half-closed range [begin, end).
 */
typedef struct ngtcp2_range {
  uint64_t begin;
  uint64_t end;
} ngtcp2_range;

/*
 * ngtcp2_range_init initializes |r| with the range [|begin|, |end|).
 */
void ngtcp2_range_init(ngtcp2_range *r, uint64_t begin, uint64_t end);

/*
 * ngtcp2_range_intersect returns the intersection of |a| and |b|.  If
 * they do not overlap, it returns empty range.
 */
ngtcp2_range ngtcp2_range_intersect(const ngtcp2_range *a,
                                    const ngtcp2_range *b);

/*
 * ngtcp2_range_len returns the length of |r|.
 */
uint64_t ngtcp2_range_len(const ngtcp2_range *r);

/*
 * ngtcp2_range_eq returns nonzero if |a| equals |b|, such that
 * a->begin == b->begin and a->end == b->end hold.
 */
int ngtcp2_range_eq(const ngtcp2_range *a, const ngtcp2_range *b);

/*
 * ngtcp2_range_cut returns the left and right range after removing
 * |b| from |a|.  This function assumes that |a| completely includes
 * |b|.  In other words, a->begin <= b->begin and b->end <= a->end
 * hold.
 */
void ngtcp2_range_cut(ngtcp2_range *left, ngtcp2_range *right,
                      const ngtcp2_range *a, const ngtcp2_range *b);

/*
 * ngtcp2_range_not_after returns nonzero if the right edge of |a|
 * does not go beyond of the right edge of |b|.
 */
int ngtcp2_range_not_after(const ngtcp2_range *a, const ngtcp2_range *b);

#endif /* !defined(NGTCP2_RANGE_H) */
