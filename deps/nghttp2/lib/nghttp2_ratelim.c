/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2023 nghttp2 contributors
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
#include "nghttp2_ratelim.h"
#include "nghttp2_helper.h"

void nghttp2_ratelim_init(nghttp2_ratelim *rl, uint64_t burst, uint64_t rate) {
  rl->val = rl->burst = burst;
  rl->rate = rate;
  rl->tstamp = 0;
}

void nghttp2_ratelim_update(nghttp2_ratelim *rl, uint64_t tstamp) {
  uint64_t d, gain;

  if (tstamp == rl->tstamp) {
    return;
  }

  if (tstamp > rl->tstamp) {
    d = tstamp - rl->tstamp;
  } else {
    d = 1;
  }

  rl->tstamp = tstamp;

  if (UINT64_MAX / d < rl->rate) {
    rl->val = rl->burst;

    return;
  }

  gain = rl->rate * d;

  if (UINT64_MAX - gain < rl->val) {
    rl->val = rl->burst;

    return;
  }

  rl->val += gain;
  rl->val = nghttp2_min_uint64(rl->val, rl->burst);
}

int nghttp2_ratelim_drain(nghttp2_ratelim *rl, uint64_t n) {
  if (rl->val < n) {
    return -1;
  }

  rl->val -= n;

  return 0;
}
