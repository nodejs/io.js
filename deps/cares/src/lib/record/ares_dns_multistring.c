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
  ares_bool_t    cache_invalidated;
  /*! combined/concatenated string cache */
  unsigned char *cache_str;
  /*! length of combined/concatenated string */
  size_t         cache_str_len;
  /*! Data making up strings */
  ares__array_t *strs; /*!< multistring_data_t type */
};

static void ares__dns_multistring_free_cb(void *arg)
{
  multistring_data_t *data = arg;
  if (data == NULL) {
    return;
  }
  ares_free(data->data);
}

ares__dns_multistring_t *ares__dns_multistring_create(void)
{
  ares__dns_multistring_t *strs = ares_malloc_zero(sizeof(*strs));
  if (strs == NULL) {
    return NULL;
  }

  strs->strs = ares__array_create(sizeof(multistring_data_t),
                                  ares__dns_multistring_free_cb);
  if (strs->strs == NULL) {
    ares_free(strs);
    return NULL;
  }

  return strs;
}

void ares__dns_multistring_clear(ares__dns_multistring_t *strs)
{
  if (strs == NULL) {
    return;
  }

  while (ares__array_len(strs->strs)) {
    ares__array_remove_last(strs->strs);
  }
}

void ares__dns_multistring_destroy(ares__dns_multistring_t *strs)
{
  if (strs == NULL) {
    return;
  }
  ares__dns_multistring_clear(strs);
  ares__array_destroy(strs->strs);
  ares_free(strs->cache_str);
  ares_free(strs);
}

ares_status_t ares__dns_multistring_replace_own(ares__dns_multistring_t *strs,
                                                size_t idx, unsigned char *str,
                                                size_t len)
{
  multistring_data_t *data;

  if (strs == NULL || str == NULL || len == 0) {
    return ARES_EFORMERR;
  }

  strs->cache_invalidated = ARES_TRUE;

  data = ares__array_at(strs->strs, idx);
  if (data == NULL) {
    return ARES_EFORMERR;
  }

  ares_free(data->data);
  data->data = str;
  data->len  = len;
  return ARES_SUCCESS;
}

ares_status_t ares__dns_multistring_del(ares__dns_multistring_t *strs,
                                        size_t                   idx)
{
  if (strs == NULL) {
    return ARES_EFORMERR;
  }

  strs->cache_invalidated = ARES_TRUE;

  return ares__array_remove_at(strs->strs, idx);
}

ares_status_t ares__dns_multistring_add_own(ares__dns_multistring_t *strs,
                                            unsigned char *str, size_t len)
{
  multistring_data_t *data;
  ares_status_t       status;

  if (strs == NULL) {
    return ARES_EFORMERR;
  }

  strs->cache_invalidated = ARES_TRUE;

  /* NOTE: its ok to have an empty string added */
  if (str == NULL && len != 0) {
    return ARES_EFORMERR;
  }

  status = ares__array_insert_last((void **)&data, strs->strs);
  if (status != ARES_SUCCESS) {
    return status;
  }

  data->data = str;
  data->len  = len;

  return ARES_SUCCESS;
}

size_t ares__dns_multistring_cnt(const ares__dns_multistring_t *strs)
{
  if (strs == NULL) {
    return 0;
  }
  return ares__array_len(strs->strs);
}

const unsigned char *
  ares__dns_multistring_get(const ares__dns_multistring_t *strs, size_t idx,
                            size_t *len)
{
  const multistring_data_t *data;

  if (strs == NULL || len == NULL) {
    return NULL;
  }

  data = ares__array_at_const(strs->strs, idx);
  if (data == NULL) {
    return NULL;
  }

  *len = data->len;
  return data->data;
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

  for (i = 0; i < ares__array_len(strs->strs); i++) {
    const multistring_data_t *data = ares__array_at_const(strs->strs, i);
    if (data == NULL ||
        ares__buf_append(buf, data->data, data->len) != ARES_SUCCESS) {
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
