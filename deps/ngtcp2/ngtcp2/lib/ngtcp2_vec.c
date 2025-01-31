/*
 * ngtcp2
 *
 * Copyright (c) 2018 ngtcp2 contributors
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
#include "ngtcp2_vec.h"

#include <string.h>
#include <assert.h>

#include "ngtcp2_str.h"

ngtcp2_vec *ngtcp2_vec_init(ngtcp2_vec *vec, const uint8_t *base, size_t len) {
  vec->base = (uint8_t *)base;
  vec->len = len;
  return vec;
}

int ngtcp2_vec_new(ngtcp2_vec **pvec, const uint8_t *data, size_t datalen,
                   const ngtcp2_mem *mem) {
  size_t len;
  uint8_t *p;

  len = sizeof(ngtcp2_vec) + datalen;

  *pvec = ngtcp2_mem_malloc(mem, len);
  if (*pvec == NULL) {
    return NGTCP2_ERR_NOMEM;
  }

  p = (uint8_t *)(*pvec) + sizeof(ngtcp2_vec);
  (*pvec)->base = p;
  (*pvec)->len = datalen;

  if (datalen) {
    /* p = */ ngtcp2_cpymem(p, data, datalen);
  }

  return 0;
}

void ngtcp2_vec_del(ngtcp2_vec *vec, const ngtcp2_mem *mem) {
  ngtcp2_mem_free(mem, vec);
}

uint64_t ngtcp2_vec_len(const ngtcp2_vec *vec, size_t n) {
  size_t i;
  size_t res = 0;

  for (i = 0; i < n; ++i) {
    res += vec[i].len;
  }

  return res;
}

int64_t ngtcp2_vec_len_varint(const ngtcp2_vec *vec, size_t n) {
  uint64_t res = 0;
  size_t len;
  size_t i;

  for (i = 0; i < n; ++i) {
    len = vec[i].len;
    if (len > NGTCP2_MAX_VARINT - res) {
      return -1;
    }

    res += len;
  }

  return (int64_t)res;
}

ngtcp2_ssize ngtcp2_vec_split(ngtcp2_vec *dst, size_t *pdstcnt, ngtcp2_vec *src,
                              size_t *psrccnt, size_t left, size_t maxcnt) {
  size_t i;
  size_t srccnt = *psrccnt;
  size_t nmove;
  size_t extra = 0;

  for (i = 0; i < srccnt; ++i) {
    if (left >= src[i].len) {
      left -= src[i].len;
      continue;
    }

    if (*pdstcnt && src[srccnt - 1].base + src[srccnt - 1].len == dst[0].base) {
      if (*pdstcnt + srccnt - i - 1 > maxcnt) {
        return -1;
      }

      dst[0].len += src[srccnt - 1].len;
      dst[0].base = src[srccnt - 1].base;
      extra = src[srccnt - 1].len;
      --srccnt;
    } else if (*pdstcnt + srccnt - i > maxcnt) {
      return -1;
    }

    if (left == 0) {
      *psrccnt = i;
    } else {
      *psrccnt = i + 1;
    }

    nmove = srccnt - i;
    if (nmove) {
      memmove(dst + nmove, dst, sizeof(ngtcp2_vec) * (*pdstcnt));
      *pdstcnt += nmove;
      memcpy(dst, src + i, sizeof(ngtcp2_vec) * nmove);
    }

    dst[0].len -= left;
    dst[0].base += left;
    src[i].len = left;

    if (nmove == 0) {
      extra -= left;
    }

    return (ngtcp2_ssize)(ngtcp2_vec_len(dst, nmove) + extra);
  }

  return 0;
}

size_t ngtcp2_vec_merge(ngtcp2_vec *dst, size_t *pdstcnt, ngtcp2_vec *src,
                        size_t *psrccnt, size_t left, size_t maxcnt) {
  size_t orig_left = left;
  size_t i = 0;
  ngtcp2_vec *a, *b;

  assert(maxcnt);

  if (*pdstcnt == 0) {
    if (*psrccnt == 0) {
      return 0;
    }

    a = &dst[0];
    b = &src[0];

    if (left < b->len) {
      a->len = left;
      a->base = b->base;

      b->len -= left;
      b->base += left;

      return left;
    }

    *a = *b;
    ++*pdstcnt;
    left -= b->len;
    i = 1;
  }

  for (; left && i < *psrccnt; ++i) {
    a = &dst[*pdstcnt - 1];
    b = &src[i];

    if (left < b->len) {
      if (a->base + a->len == b->base) {
        a->len += left;
      } else if (*pdstcnt == maxcnt) {
        break;
      } else {
        dst[*pdstcnt].len = left;
        dst[*pdstcnt].base = b->base;
        ++*pdstcnt;
      }

      b->len -= left;
      b->base += left;
      left = 0;

      break;
    }

    if (a->base + a->len == b->base) {
      a->len += b->len;
    } else if (*pdstcnt == maxcnt) {
      break;
    } else {
      dst[(*pdstcnt)++] = *b;
    }

    left -= b->len;
  }

  memmove(src, src + i, sizeof(ngtcp2_vec) * (*psrccnt - i));
  *psrccnt -= i;

  return orig_left - left;
}

size_t ngtcp2_vec_copy_at_most(ngtcp2_vec *dst, size_t dstcnt,
                               const ngtcp2_vec *src, size_t srccnt,
                               size_t left) {
  size_t i, j;

  for (i = 0, j = 0; left > 0 && i < srccnt && j < dstcnt;) {
    if (src[i].len == 0) {
      ++i;
      continue;
    }

    dst[j] = src[i];

    if (dst[j].len > left) {
      dst[j].len = left;
      return j + 1;
    }

    left -= dst[j].len;
    ++i;
    ++j;
  }

  return j;
}

void ngtcp2_vec_copy(ngtcp2_vec *dst, const ngtcp2_vec *src, size_t cnt) {
  memcpy(dst, src, sizeof(ngtcp2_vec) * cnt);
}
