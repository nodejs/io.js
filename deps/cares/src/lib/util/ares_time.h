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
#ifndef __ARES_TIME_H
#define __ARES_TIME_H

/*! struct timeval on some systems like Windows doesn't support 64bit time so
 *  therefore can't be used due to Y2K38 issues.  Make our own that does have
 *  64bit time. */
typedef struct {
  ares_int64_t sec;  /*!< Seconds */
  unsigned int usec; /*!< Microseconds. Can't be negative. */
} ares_timeval_t;

/* return true if now is exactly check time or later */
ares_bool_t ares_timedout(const ares_timeval_t *now,
                          const ares_timeval_t *check);

void        ares_tvnow(ares_timeval_t *now);
void        ares_timeval_remaining(ares_timeval_t       *remaining,
                                   const ares_timeval_t *now,
                                   const ares_timeval_t *tout);
void ares_timeval_diff(ares_timeval_t *tvdiff, const ares_timeval_t *tvstart,
                       const ares_timeval_t *tvstop);

#endif
