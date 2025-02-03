/*
 * nghttp3
 *
 * Copyright (c) 2019 nghttp3 contributors
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
#include "nghttp3_range.h"
#include "nghttp3_macro.h"

void nghttp3_range_init(nghttp3_range *r, uint64_t begin, uint64_t end) {
  r->begin = begin;
  r->end = end;
}

nghttp3_range nghttp3_range_intersect(const nghttp3_range *a,
                                      const nghttp3_range *b) {
  nghttp3_range r = {0, 0};
  uint64_t begin = nghttp3_max_uint64(a->begin, b->begin);
  uint64_t end = nghttp3_min_uint64(a->end, b->end);

  if (begin < end) {
    nghttp3_range_init(&r, begin, end);
  }

  return r;
}

uint64_t nghttp3_range_len(const nghttp3_range *r) { return r->end - r->begin; }

int nghttp3_range_eq(const nghttp3_range *a, const nghttp3_range *b) {
  return a->begin == b->begin && a->end == b->end;
}

void nghttp3_range_cut(nghttp3_range *left, nghttp3_range *right,
                       const nghttp3_range *a, const nghttp3_range *b) {
  /* Assume that b is included in a */
  left->begin = a->begin;
  left->end = b->begin;
  right->begin = b->end;
  right->end = a->end;
}

int nghttp3_range_not_after(const nghttp3_range *a, const nghttp3_range *b) {
  return a->end <= b->end;
}
