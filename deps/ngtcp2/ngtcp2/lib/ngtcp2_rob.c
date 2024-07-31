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
#include "ngtcp2_rob.h"

#include <string.h>
#include <assert.h>

#include "ngtcp2_macro.h"

int ngtcp2_rob_gap_new(ngtcp2_rob_gap **pg, uint64_t begin, uint64_t end,
                       const ngtcp2_mem *mem) {
  *pg = ngtcp2_mem_malloc(mem, sizeof(ngtcp2_rob_gap));
  if (*pg == NULL) {
    return NGTCP2_ERR_NOMEM;
  }

  (*pg)->range.begin = begin;
  (*pg)->range.end = end;

  return 0;
}

void ngtcp2_rob_gap_del(ngtcp2_rob_gap *g, const ngtcp2_mem *mem) {
  ngtcp2_mem_free(mem, g);
}

int ngtcp2_rob_data_new(ngtcp2_rob_data **pd, uint64_t offset, size_t chunk,
                        const ngtcp2_mem *mem) {
  *pd = ngtcp2_mem_malloc(mem, sizeof(ngtcp2_rob_data) + chunk);
  if (*pd == NULL) {
    return NGTCP2_ERR_NOMEM;
  }

  (*pd)->range.begin = offset;
  (*pd)->range.end = offset + chunk;
  (*pd)->begin = (uint8_t *)(*pd) + sizeof(ngtcp2_rob_data);
  (*pd)->end = (*pd)->begin + chunk;

  return 0;
}

void ngtcp2_rob_data_del(ngtcp2_rob_data *d, const ngtcp2_mem *mem) {
  ngtcp2_mem_free(mem, d);
}

int ngtcp2_rob_init(ngtcp2_rob *rob, size_t chunk, const ngtcp2_mem *mem) {
  int rv;
  ngtcp2_rob_gap *g;

  ngtcp2_ksl_init(&rob->gapksl, ngtcp2_ksl_range_compar, sizeof(ngtcp2_range),
                  mem);

  rv = ngtcp2_rob_gap_new(&g, 0, UINT64_MAX, mem);
  if (rv != 0) {
    goto fail_rob_gap_new;
  }

  rv = ngtcp2_ksl_insert(&rob->gapksl, NULL, &g->range, g);
  if (rv != 0) {
    goto fail_gapksl_ksl_insert;
  }

  ngtcp2_ksl_init(&rob->dataksl, ngtcp2_ksl_range_compar, sizeof(ngtcp2_range),
                  mem);

  rob->chunk = chunk;
  rob->mem = mem;

  return 0;

fail_gapksl_ksl_insert:
  ngtcp2_rob_gap_del(g, mem);
fail_rob_gap_new:
  ngtcp2_ksl_free(&rob->gapksl);
  return rv;
}

void ngtcp2_rob_free(ngtcp2_rob *rob) {
  ngtcp2_ksl_it it;

  if (rob == NULL) {
    return;
  }

  for (it = ngtcp2_ksl_begin(&rob->dataksl); !ngtcp2_ksl_it_end(&it);
       ngtcp2_ksl_it_next(&it)) {
    ngtcp2_rob_data_del(ngtcp2_ksl_it_get(&it), rob->mem);
  }

  for (it = ngtcp2_ksl_begin(&rob->gapksl); !ngtcp2_ksl_it_end(&it);
       ngtcp2_ksl_it_next(&it)) {
    ngtcp2_rob_gap_del(ngtcp2_ksl_it_get(&it), rob->mem);
  }

  ngtcp2_ksl_free(&rob->dataksl);
  ngtcp2_ksl_free(&rob->gapksl);
}

static int rob_write_data(ngtcp2_rob *rob, uint64_t offset, const uint8_t *data,
                          size_t len) {
  size_t n;
  int rv;
  ngtcp2_rob_data *d;
  ngtcp2_range range = {offset, offset + len};
  ngtcp2_ksl_it it;

  for (it = ngtcp2_ksl_lower_bound_compar(&rob->dataksl, &range,
                                          ngtcp2_ksl_range_exclusive_compar);
       len; ngtcp2_ksl_it_next(&it)) {
    if (ngtcp2_ksl_it_end(&it)) {
      d = NULL;
    } else {
      d = ngtcp2_ksl_it_get(&it);
    }

    if (d == NULL || offset < d->range.begin) {
      rv = ngtcp2_rob_data_new(&d, (offset / rob->chunk) * rob->chunk,
                               rob->chunk, rob->mem);
      if (rv != 0) {
        return rv;
      }

      rv = ngtcp2_ksl_insert(&rob->dataksl, &it, &d->range, d);
      if (rv != 0) {
        ngtcp2_rob_data_del(d, rob->mem);
        return rv;
      }
    }

    n = (size_t)ngtcp2_min((uint64_t)len, d->range.begin + rob->chunk - offset);
    memcpy(d->begin + (offset - d->range.begin), data, n);
    offset += n;
    data += n;
    len -= n;
  }

  return 0;
}

int ngtcp2_rob_push(ngtcp2_rob *rob, uint64_t offset, const uint8_t *data,
                    size_t datalen) {
  int rv;
  ngtcp2_rob_gap *g;
  ngtcp2_range m, l, r, q = {offset, offset + datalen};
  ngtcp2_ksl_it it;

  it = ngtcp2_ksl_lower_bound_compar(&rob->gapksl, &q,
                                     ngtcp2_ksl_range_exclusive_compar);

  for (; !ngtcp2_ksl_it_end(&it);) {
    g = ngtcp2_ksl_it_get(&it);

    m = ngtcp2_range_intersect(&q, &g->range);
    if (!ngtcp2_range_len(&m)) {
      break;
    }
    if (ngtcp2_range_eq(&g->range, &m)) {
      ngtcp2_ksl_remove_hint(&rob->gapksl, &it, &it, &g->range);
      ngtcp2_rob_gap_del(g, rob->mem);
      rv = rob_write_data(rob, m.begin, data + (m.begin - offset),
                          (size_t)ngtcp2_range_len(&m));
      if (rv != 0) {
        return rv;
      }

      continue;
    }
    ngtcp2_range_cut(&l, &r, &g->range, &m);
    if (ngtcp2_range_len(&l)) {
      ngtcp2_ksl_update_key(&rob->gapksl, &g->range, &l);
      g->range = l;

      if (ngtcp2_range_len(&r)) {
        ngtcp2_rob_gap *ng;
        rv = ngtcp2_rob_gap_new(&ng, r.begin, r.end, rob->mem);
        if (rv != 0) {
          return rv;
        }
        rv = ngtcp2_ksl_insert(&rob->gapksl, &it, &ng->range, ng);
        if (rv != 0) {
          ngtcp2_rob_gap_del(ng, rob->mem);
          return rv;
        }
      }
    } else if (ngtcp2_range_len(&r)) {
      ngtcp2_ksl_update_key(&rob->gapksl, &g->range, &r);
      g->range = r;
    }
    rv = rob_write_data(rob, m.begin, data + (m.begin - offset),
                        (size_t)ngtcp2_range_len(&m));
    if (rv != 0) {
      return rv;
    }
    ngtcp2_ksl_it_next(&it);
  }
  return 0;
}

void ngtcp2_rob_remove_prefix(ngtcp2_rob *rob, uint64_t offset) {
  ngtcp2_rob_gap *g;
  ngtcp2_rob_data *d;
  ngtcp2_ksl_it it;

  it = ngtcp2_ksl_begin(&rob->gapksl);

  for (; !ngtcp2_ksl_it_end(&it);) {
    g = ngtcp2_ksl_it_get(&it);
    if (offset <= g->range.begin) {
      break;
    }
    if (offset < g->range.end) {
      ngtcp2_range r = {offset, g->range.end};
      ngtcp2_ksl_update_key(&rob->gapksl, &g->range, &r);
      g->range.begin = offset;
      break;
    }
    ngtcp2_ksl_remove_hint(&rob->gapksl, &it, &it, &g->range);
    ngtcp2_rob_gap_del(g, rob->mem);
  }

  it = ngtcp2_ksl_begin(&rob->dataksl);

  for (; !ngtcp2_ksl_it_end(&it);) {
    d = ngtcp2_ksl_it_get(&it);
    if (offset < d->range.begin + rob->chunk) {
      return;
    }
    ngtcp2_ksl_remove_hint(&rob->dataksl, &it, &it, &d->range);
    ngtcp2_rob_data_del(d, rob->mem);
  }
}

size_t ngtcp2_rob_data_at(ngtcp2_rob *rob, const uint8_t **pdest,
                          uint64_t offset) {
  ngtcp2_rob_gap *g;
  ngtcp2_rob_data *d;
  ngtcp2_ksl_it it;

  it = ngtcp2_ksl_begin(&rob->gapksl);
  if (ngtcp2_ksl_it_end(&it)) {
    return 0;
  }

  g = ngtcp2_ksl_it_get(&it);

  if (g->range.begin <= offset) {
    return 0;
  }

  it = ngtcp2_ksl_begin(&rob->dataksl);
  d = ngtcp2_ksl_it_get(&it);

  assert(d);
  assert(d->range.begin <= offset);
  assert(offset < d->range.begin + rob->chunk);

  *pdest = d->begin + (offset - d->range.begin);

  return (size_t)(ngtcp2_min(g->range.begin, d->range.begin + rob->chunk) -
                  offset);
}

void ngtcp2_rob_pop(ngtcp2_rob *rob, uint64_t offset, size_t len) {
  ngtcp2_ksl_it it;
  ngtcp2_rob_data *d;

  it = ngtcp2_ksl_begin(&rob->dataksl);
  d = ngtcp2_ksl_it_get(&it);

  assert(d);

  if (offset + len < d->range.begin + rob->chunk) {
    return;
  }

  ngtcp2_ksl_remove_hint(&rob->dataksl, NULL, &it, &d->range);
  ngtcp2_rob_data_del(d, rob->mem);
}

uint64_t ngtcp2_rob_first_gap_offset(ngtcp2_rob *rob) {
  ngtcp2_ksl_it it = ngtcp2_ksl_begin(&rob->gapksl);
  ngtcp2_rob_gap *g;

  if (ngtcp2_ksl_it_end(&it)) {
    return UINT64_MAX;
  }

  g = ngtcp2_ksl_it_get(&it);

  return g->range.begin;
}

int ngtcp2_rob_data_buffered(ngtcp2_rob *rob) {
  return ngtcp2_ksl_len(&rob->dataksl) != 0;
}
