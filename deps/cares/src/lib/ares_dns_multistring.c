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
#include "ares_private.h"
#include "ares_dns_private.h"

typedef struct {
  unsigned char *data;
  size_t         len;
} multistring_data_t;

struct ares__dns_multistring {
  /*! whether or not cached concatenated string is valid */
  ares_bool_t         cache_invalidated;
  /*! combined/concatenated string cache */
  unsigned char      *cache_str;
  /*! length of combined/concatenated string */
  size_t              cache_str_len;
  /*! Data making up strings */
  multistring_data_t *strs;
  size_t              cnt;
  size_t              alloc;
};

ares__dns_multistring_t *ares__dns_multistring_create(void)
{
  return ares_malloc_zero(sizeof(ares__dns_multistring_t));
}

void ares__dns_multistring_clear(ares__dns_multistring_t *strs)
{
  size_t i;

  if (strs == NULL) {
    return;
  }

  for (i = 0; i < strs->cnt; i++) {
    ares_free(strs->strs[i].data);
    memset(&strs->strs[i], 0, sizeof(strs->strs[i]));
  }
  strs->cnt = 0;
}

void ares__dns_multistring_destroy(ares__dns_multistring_t *strs)
{
  if (strs == NULL) {
    return;
  }
  ares__dns_multistring_clear(strs);
  ares_free(strs->strs);
  ares_free(strs->cache_str);
  ares_free(strs);
}

ares_status_t ares__dns_multistring_replace_own(ares__dns_multistring_t *strs,
                                                size_t idx, unsigned char *str,
                                                size_t len)
{
  if (strs == NULL || str == NULL || len == 0 || idx >= strs->cnt) {
    return ARES_EFORMERR;
  }

  strs->cache_invalidated = ARES_TRUE;
  ares_free(strs->strs[idx].data);
  strs->strs[idx].data = str;
  strs->strs[idx].len  = len;
  return ARES_SUCCESS;
}

ares_status_t ares__dns_multistring_del(ares__dns_multistring_t *strs,
                                        size_t                   idx)
{
  size_t move_cnt;

  if (strs == NULL || idx >= strs->cnt) {
    return ARES_EFORMERR;
  }

  strs->cache_invalidated = ARES_TRUE;

  ares_free(strs->strs[idx].data);

  move_cnt = strs->cnt - idx - 1;
  if (move_cnt) {
    memmove(&strs->strs[idx], &strs->strs[idx + 1],
            sizeof(*strs->strs) * move_cnt);
  }

  strs->cnt--;
  return ARES_SUCCESS;
}

ares_status_t ares__dns_multistring_add_own(ares__dns_multistring_t *strs,
                                            unsigned char *str, size_t len)
{
  if (strs == NULL) {
    return ARES_EFORMERR;
  }

  strs->cache_invalidated = ARES_TRUE;

  /* NOTE: its ok to have an empty string added */
  if (str == NULL && len != 0) {
    return ARES_EFORMERR;
  }

  if (strs->alloc < strs->cnt + 1) {
    size_t newalloc = (strs->alloc == 0) ? 1 : (strs->alloc << 1);
    void *ptr = ares_realloc_zero(strs->strs, strs->alloc * sizeof(*strs->strs),
                                  (newalloc) * sizeof(*strs->strs));
    if (ptr == NULL) {
      return ARES_ENOMEM;
    }
    strs->strs  = ptr;
    strs->alloc = newalloc;
  }

  strs->strs[strs->cnt].data = str;
  strs->strs[strs->cnt].len  = len;
  strs->cnt++;

  return ARES_SUCCESS;
}

size_t ares__dns_multistring_cnt(const ares__dns_multistring_t *strs)
{
  if (strs == NULL) {
    return 0;
  }
  return strs->cnt;
}

const unsigned char *ares__dns_multistring_get(
  const ares__dns_multistring_t *strs, size_t idx, size_t *len)
{
  if (strs == NULL || idx >= strs->cnt || len == NULL) {
    return NULL;
  }

  *len = strs->strs[idx].len;
  return strs->strs[idx].data;
}

const unsigned char *
  ares__dns_multistring_get_combined(ares__dns_multistring_t *strs, size_t *len)
{
  ares__buf_t *buf = NULL;
  size_t       i;

  if (strs == NULL || len == NULL) {
    return NULL;
  }

  *len = 0;

  /* Return cache if possible */
  if (!strs->cache_invalidated) {
    *len = strs->cache_str_len;
    return strs->cache_str;
  }

  /* Clear cache */
  ares_free(strs->cache_str);
  strs->cache_str     = NULL;
  strs->cache_str_len = 0;

  buf = ares__buf_create();

  for (i = 0; i < strs->cnt; i++) {
    if (ares__buf_append(buf, strs->strs[i].data, strs->strs[i].len) !=
        ARES_SUCCESS) {
      ares__buf_destroy(buf);
      return NULL;
    }
  }

  strs->cache_str =
    (unsigned char *)ares__buf_finish_str(buf, &strs->cache_str_len);
  if (strs->cache_str != NULL) {
    strs->cache_invalidated = ARES_FALSE;
  }
  *len = strs->cache_str_len;
  return strs->cache_str;
}
