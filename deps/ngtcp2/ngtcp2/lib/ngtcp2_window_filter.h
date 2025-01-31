/*
 * ngtcp2
 *
 * Copyright (c) 2021 ngtcp2 contributors
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

/*
 * Translated to C from the original C++ code
 * https://quiche.googlesource.com/quiche/+/5be974e29f7e71a196e726d6e2272676d33ab77d/quic/core/congestion_control/windowed_filter.h
 * with the following license:
 *
 * // Copyright (c) 2016 The Chromium Authors. All rights reserved.
 * // Use of this source code is governed by a BSD-style license that can be
 * // found in the LICENSE file.
 */
#ifndef NGTCP2_WINDOW_FILTER_H
#define NGTCP2_WINDOW_FILTER_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <ngtcp2/ngtcp2.h>

typedef struct ngtcp2_window_filter_sample {
  uint64_t sample;
  uint64_t time;
} ngtcp2_window_filter_sample;

typedef struct ngtcp2_window_filter {
  uint64_t window_length;
  ngtcp2_window_filter_sample estimates[3];
} ngtcp2_window_filter;

void ngtcp2_window_filter_init(ngtcp2_window_filter *wf,
                               uint64_t window_length);

void ngtcp2_window_filter_update(ngtcp2_window_filter *wf, uint64_t new_sample,
                                 uint64_t new_time);

void ngtcp2_window_filter_reset(ngtcp2_window_filter *wf, uint64_t new_sample,
                                uint64_t new_time);

uint64_t ngtcp2_window_filter_get_best(ngtcp2_window_filter *wf);

#endif /* !defined(NGTCP2_WINDOW_FILTER_H) */
