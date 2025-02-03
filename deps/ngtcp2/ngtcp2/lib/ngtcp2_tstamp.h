/*
 * ngtcp2
 *
 * Copyright (c) 2023 ngtcp2 contributors
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
#ifndef NGTCP2_TSTAMP_H
#define NGTCP2_TSTAMP_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <ngtcp2/ngtcp2.h>

/*
 * ngtcp2_tstamp_elapsed returns nonzero if at least |d| has passed
 * since |base|.  |ts| expresses a current time, and must not be
 * UINT64_MAX.
 *
 * If |base| is UINT64_MAX, this function returns 0 because UINT64_MAX
 * is an invalid timestamp.  Otherwise, if |base| + |d| >= UINT64_MAX,
 * this function returns 0.
 *
 * !ngtcp2_tstamp_elapsed() == ngtcp2_tstamp_not_elapsed() does not
 * hold when |base| is UINT64_MAX.  If you need nonzero if |base| is
 * UINT64_MAX, use !ngtcp2_tstamp_elapsed.  Otherwise, use
 * ngtcp2_tstamp_not_elapsed.
 */
static inline int ngtcp2_tstamp_elapsed(ngtcp2_tstamp base, ngtcp2_duration d,
                                        ngtcp2_tstamp ts) {
  return base != UINT64_MAX && base < UINT64_MAX - d && base + d <= ts;
}

/*
 * ngtcp2_tstamp_not_elapsed returns nonzero if |d| has not passed
 * since |base|.  |ts| expresses a current time, and must not be
 * UINT64_MAX.
 *
 * If |base| is UINT64_MAX, this function returns 0 because UINT64_MAX
 * is an invalid timestamp.  Otherwise, if |base| + |d| >= UINT64_MAX,
 * this function returns nonzero.
 */
static inline int ngtcp2_tstamp_not_elapsed(ngtcp2_tstamp base,
                                            ngtcp2_duration d,
                                            ngtcp2_tstamp ts) {
  return base != UINT64_MAX && (base >= UINT64_MAX - d || base + d > ts);
}

#endif /* !defined(NGTCP2_TSTAMP_H) */
