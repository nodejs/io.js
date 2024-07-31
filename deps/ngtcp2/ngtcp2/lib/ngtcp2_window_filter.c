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
#include "ngtcp2_window_filter.h"

#include <string.h>

void ngtcp2_window_filter_init(ngtcp2_window_filter *wf,
                               uint64_t window_length) {
  wf->window_length = window_length;
  memset(wf->estimates, 0xff, sizeof(wf->estimates));
}

void ngtcp2_window_filter_update(ngtcp2_window_filter *wf, uint64_t new_sample,
                                 uint64_t new_time) {
  /* Reset all estimates if they have not yet been initialized, if new
     sample is a new best, or if the newest recorded estimate is too
     old. */
  if (wf->estimates[0].sample == UINT64_MAX ||
      new_sample > wf->estimates[0].sample ||
      new_time - wf->estimates[2].time > wf->window_length) {
    ngtcp2_window_filter_reset(wf, new_sample, new_time);
    return;
  }

  if (new_sample > wf->estimates[1].sample) {
    wf->estimates[1].sample = new_sample;
    wf->estimates[1].time = new_time;
    wf->estimates[2] = wf->estimates[1];
  } else if (new_sample > wf->estimates[2].sample) {
    wf->estimates[2].sample = new_sample;
    wf->estimates[2].time = new_time;
  }

  /* Expire and update estimates as necessary. */
  if (new_time - wf->estimates[0].time > wf->window_length) {
    /* The best estimate hasn't been updated for an entire window, so
       promote second and third best estimates. */
    wf->estimates[0] = wf->estimates[1];
    wf->estimates[1] = wf->estimates[2];
    wf->estimates[2].sample = new_sample;
    wf->estimates[2].time = new_time;

    /* Need to iterate one more time.  Check if the new best estimate
       is outside the window as well, since it may also have been
       recorded a long time ago.  Don't need to iterate once more
       since we cover that case at the beginning of the method. */
    if (new_time - wf->estimates[0].time > wf->window_length) {
      wf->estimates[0] = wf->estimates[1];
      wf->estimates[1] = wf->estimates[2];
    }
    return;
  }

  if (wf->estimates[1].sample == wf->estimates[0].sample &&
      new_time - wf->estimates[1].time > wf->window_length >> 2) {
    /* A quarter of the window has passed without a better sample, so
       the second-best estimate is taken from the second quarter of
       the window. */
    wf->estimates[2].sample = new_sample;
    wf->estimates[2].time = new_time;
    wf->estimates[1] = wf->estimates[2];
    return;
  }

  if (wf->estimates[2].sample == wf->estimates[1].sample &&
      new_time - wf->estimates[2].time > wf->window_length >> 1) {
    /* We've passed a half of the window without a better estimate, so
       take a third-best estimate from the second half of the
       window. */
    wf->estimates[2].sample = new_sample;
    wf->estimates[2].time = new_time;
  }
}

void ngtcp2_window_filter_reset(ngtcp2_window_filter *wf, uint64_t new_sample,
                                uint64_t new_time) {
  wf->estimates[0].sample = new_sample;
  wf->estimates[0].time = new_time;
  wf->estimates[1] = wf->estimates[2] = wf->estimates[0];
}

uint64_t ngtcp2_window_filter_get_best(ngtcp2_window_filter *wf) {
  return wf->estimates[0].sample;
}
