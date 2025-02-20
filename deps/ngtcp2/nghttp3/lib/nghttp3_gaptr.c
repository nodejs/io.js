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
#include "nghttp3_gaptr.h"

#include <string.h>
#include <assert.h>

void nghttp3_gaptr_init(nghttp3_gaptr *gaptr, const nghttp3_mem *mem) {
  nghttp3_ksl_init(&gaptr->gap, nghttp3_ksl_range_compar,
                   nghttp3_ksl_range_search, sizeof(nghttp3_range), mem);

  gaptr->mem = mem;
}

static int gaptr_gap_init(nghttp3_gaptr *gaptr) {
  nghttp3_range range = {0, UINT64_MAX};

  return nghttp3_ksl_insert(&gaptr->gap, NULL, &range, NULL);
}

void nghttp3_gaptr_free(nghttp3_gaptr *gaptr) {
  if (gaptr == NULL) {
    return;
  }

  nghttp3_ksl_free(&gaptr->gap);
}

int nghttp3_gaptr_push(nghttp3_gaptr *gaptr, uint64_t offset,
                       uint64_t datalen) {
  int rv;
  nghttp3_range k, m, l, r, q = {offset, offset + datalen};
  nghttp3_ksl_it it;

  if (nghttp3_ksl_len(&gaptr->gap) == 0) {
    rv = gaptr_gap_init(gaptr);
    if (rv != 0) {
      return rv;
    }
  }

  it = nghttp3_ksl_lower_bound_search(&gaptr->gap, &q,
                                      nghttp3_ksl_range_exclusive_search);

  for (; !nghttp3_ksl_it_end(&it);) {
    k = *(nghttp3_range *)nghttp3_ksl_it_key(&it);
    m = nghttp3_range_intersect(&q, &k);
    if (!nghttp3_range_len(&m)) {
      break;
    }

    if (nghttp3_range_eq(&k, &m)) {
      nghttp3_ksl_remove_hint(&gaptr->gap, &it, &it, &k);
      continue;
    }

    nghttp3_range_cut(&l, &r, &k, &m);

    if (nghttp3_range_len(&l)) {
      nghttp3_ksl_update_key(&gaptr->gap, &k, &l);

      if (nghttp3_range_len(&r)) {
        rv = nghttp3_ksl_insert(&gaptr->gap, &it, &r, NULL);
        if (rv != 0) {
          return rv;
        }
      }
    } else if (nghttp3_range_len(&r)) {
      nghttp3_ksl_update_key(&gaptr->gap, &k, &r);
    }

    nghttp3_ksl_it_next(&it);
  }

  return 0;
}

uint64_t nghttp3_gaptr_first_gap_offset(nghttp3_gaptr *gaptr) {
  nghttp3_ksl_it it;

  if (nghttp3_ksl_len(&gaptr->gap) == 0) {
    return 0;
  }

  it = nghttp3_ksl_begin(&gaptr->gap);

  return ((nghttp3_range *)nghttp3_ksl_it_key(&it))->begin;
}

nghttp3_range nghttp3_gaptr_get_first_gap_after(nghttp3_gaptr *gaptr,
                                                uint64_t offset) {
  nghttp3_range q = {offset, offset + 1};
  nghttp3_ksl_it it;

  if (nghttp3_ksl_len(&gaptr->gap) == 0) {
    nghttp3_range r = {0, UINT64_MAX};
    return r;
  }

  it = nghttp3_ksl_lower_bound_search(&gaptr->gap, &q,
                                      nghttp3_ksl_range_exclusive_search);

  assert(!nghttp3_ksl_it_end(&it));

  return *(nghttp3_range *)nghttp3_ksl_it_key(&it);
}

int nghttp3_gaptr_is_pushed(nghttp3_gaptr *gaptr, uint64_t offset,
                            uint64_t datalen) {
  nghttp3_range q = {offset, offset + datalen};
  nghttp3_ksl_it it;
  nghttp3_range m;

  if (nghttp3_ksl_len(&gaptr->gap) == 0) {
    return 0;
  }

  it = nghttp3_ksl_lower_bound_search(&gaptr->gap, &q,
                                      nghttp3_ksl_range_exclusive_search);
  m = nghttp3_range_intersect(&q, (nghttp3_range *)nghttp3_ksl_it_key(&it));

  return nghttp3_range_len(&m) == 0;
}

void nghttp3_gaptr_drop_first_gap(nghttp3_gaptr *gaptr) {
  nghttp3_ksl_it it;
  nghttp3_range r;

  if (nghttp3_ksl_len(&gaptr->gap) == 0) {
    return;
  }

  it = nghttp3_ksl_begin(&gaptr->gap);

  assert(!nghttp3_ksl_it_end(&it));

  r = *(nghttp3_range *)nghttp3_ksl_it_key(&it);

  nghttp3_ksl_remove_hint(&gaptr->gap, NULL, &it, &r);
}
