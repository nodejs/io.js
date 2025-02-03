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
#include "ngtcp2_gaptr.h"

#include <string.h>
#include <assert.h>

void ngtcp2_gaptr_init(ngtcp2_gaptr *gaptr, const ngtcp2_mem *mem) {
  ngtcp2_ksl_init(&gaptr->gap, ngtcp2_ksl_range_compar, ngtcp2_ksl_range_search,
                  sizeof(ngtcp2_range), mem);

  gaptr->mem = mem;
}

static int gaptr_gap_init(ngtcp2_gaptr *gaptr) {
  ngtcp2_range range = {0, UINT64_MAX};

  return ngtcp2_ksl_insert(&gaptr->gap, NULL, &range, NULL);
}

void ngtcp2_gaptr_free(ngtcp2_gaptr *gaptr) {
  if (gaptr == NULL) {
    return;
  }

  ngtcp2_ksl_free(&gaptr->gap);
}

int ngtcp2_gaptr_push(ngtcp2_gaptr *gaptr, uint64_t offset, uint64_t datalen) {
  int rv;
  ngtcp2_range k, m, l, r, q = {offset, offset + datalen};
  ngtcp2_ksl_it it;

  if (ngtcp2_ksl_len(&gaptr->gap) == 0) {
    rv = gaptr_gap_init(gaptr);
    if (rv != 0) {
      return rv;
    }
  }

  it = ngtcp2_ksl_lower_bound_search(&gaptr->gap, &q,
                                     ngtcp2_ksl_range_exclusive_search);

  for (; !ngtcp2_ksl_it_end(&it);) {
    k = *(ngtcp2_range *)ngtcp2_ksl_it_key(&it);
    m = ngtcp2_range_intersect(&q, &k);
    if (!ngtcp2_range_len(&m)) {
      break;
    }

    if (ngtcp2_range_eq(&k, &m)) {
      ngtcp2_ksl_remove_hint(&gaptr->gap, &it, &it, &k);
      continue;
    }

    ngtcp2_range_cut(&l, &r, &k, &m);

    if (ngtcp2_range_len(&l)) {
      ngtcp2_ksl_update_key(&gaptr->gap, &k, &l);

      if (ngtcp2_range_len(&r)) {
        rv = ngtcp2_ksl_insert(&gaptr->gap, &it, &r, NULL);
        if (rv != 0) {
          return rv;
        }
      }
    } else if (ngtcp2_range_len(&r)) {
      ngtcp2_ksl_update_key(&gaptr->gap, &k, &r);
    }

    ngtcp2_ksl_it_next(&it);
  }

  return 0;
}

uint64_t ngtcp2_gaptr_first_gap_offset(const ngtcp2_gaptr *gaptr) {
  ngtcp2_ksl_it it;

  if (ngtcp2_ksl_len(&gaptr->gap) == 0) {
    return 0;
  }

  it = ngtcp2_ksl_begin(&gaptr->gap);

  return ((ngtcp2_range *)ngtcp2_ksl_it_key(&it))->begin;
}

ngtcp2_range ngtcp2_gaptr_get_first_gap_after(const ngtcp2_gaptr *gaptr,
                                              uint64_t offset) {
  ngtcp2_range q = {offset, offset + 1};
  ngtcp2_ksl_it it;

  if (ngtcp2_ksl_len(&gaptr->gap) == 0) {
    ngtcp2_range r = {0, UINT64_MAX};
    return r;
  }

  it = ngtcp2_ksl_lower_bound_search(&gaptr->gap, &q,
                                     ngtcp2_ksl_range_exclusive_search);

  assert(!ngtcp2_ksl_it_end(&it));

  return *(ngtcp2_range *)ngtcp2_ksl_it_key(&it);
}

int ngtcp2_gaptr_is_pushed(const ngtcp2_gaptr *gaptr, uint64_t offset,
                           uint64_t datalen) {
  ngtcp2_range q = {offset, offset + datalen};
  ngtcp2_ksl_it it;
  ngtcp2_range m;

  if (ngtcp2_ksl_len(&gaptr->gap) == 0) {
    return 0;
  }

  it = ngtcp2_ksl_lower_bound_search(&gaptr->gap, &q,
                                     ngtcp2_ksl_range_exclusive_search);
  m = ngtcp2_range_intersect(&q, (ngtcp2_range *)ngtcp2_ksl_it_key(&it));

  return ngtcp2_range_len(&m) == 0;
}

void ngtcp2_gaptr_drop_first_gap(ngtcp2_gaptr *gaptr) {
  ngtcp2_ksl_it it;
  ngtcp2_range r;

  if (ngtcp2_ksl_len(&gaptr->gap) == 0) {
    return;
  }

  it = ngtcp2_ksl_begin(&gaptr->gap);

  assert(!ngtcp2_ksl_it_end(&it));

  r = *(ngtcp2_range *)ngtcp2_ksl_it_key(&it);

  ngtcp2_ksl_remove_hint(&gaptr->gap, NULL, &it, &r);
}
