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
#include "ares_htable.h"
#include "ares_htable_dict.h"

struct ares_htable_dict {
  ares_htable_t *hash;
};

typedef struct {
  char               *key;
  char               *val;
  ares_htable_dict_t *parent;
} ares_htable_dict_bucket_t;

void ares_htable_dict_destroy(ares_htable_dict_t *htable)
{
  if (htable == NULL) {
    return; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  ares_htable_destroy(htable->hash);
  ares_free(htable);
}

static unsigned int hash_func(const void *key, unsigned int seed)
{
  return ares_htable_hash_FNV1a_casecmp(key, ares_strlen(key), seed);
}

static const void *bucket_key(const void *bucket)
{
  const ares_htable_dict_bucket_t *arg = bucket;
  return arg->key;
}

static void bucket_free(void *bucket)
{
  ares_htable_dict_bucket_t *arg = bucket;

  ares_free(arg->key);
  ares_free(arg->val);

  ares_free(arg);
}

static ares_bool_t key_eq(const void *key1, const void *key2)
{
  return ares_strcaseeq(key1, key2);
}

ares_htable_dict_t *ares_htable_dict_create(void)
{
  ares_htable_dict_t *htable = ares_malloc(sizeof(*htable));
  if (htable == NULL) {
    goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  htable->hash = ares_htable_create(hash_func, bucket_key, bucket_free, key_eq);
  if (htable->hash == NULL) {
    goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  return htable;

/* LCOV_EXCL_START: OutOfMemory */
fail:
  if (htable) {
    ares_htable_destroy(htable->hash);
    ares_free(htable);
  }
  return NULL;
  /* LCOV_EXCL_STOP */
}

ares_bool_t ares_htable_dict_insert(ares_htable_dict_t *htable, const char *key,
                                    const char *val)
{
  ares_htable_dict_bucket_t *bucket = NULL;

  if (htable == NULL || ares_strlen(key) == 0) {
    goto fail;
  }

  bucket = ares_malloc_zero(sizeof(*bucket));
  if (bucket == NULL) {
    goto fail;
  }

  bucket->parent = htable;
  bucket->key    = ares_strdup(key);
  if (bucket->key == NULL) {
    goto fail;
  }

  if (val != NULL) {
    bucket->val = ares_strdup(val);
    if (bucket->val == NULL) {
      goto fail;
    }
  }

  if (!ares_htable_insert(htable->hash, bucket)) {
    goto fail;
  }

  return ARES_TRUE;

fail:
  if (bucket) {
    ares_free(bucket->val);
    ares_free(bucket);
  }
  return ARES_FALSE;
}

ares_bool_t ares_htable_dict_get(const ares_htable_dict_t *htable,
                                 const char *key, const char **val)
{
  const ares_htable_dict_bucket_t *bucket = NULL;

  if (val) {
    *val = NULL;
  }

  if (htable == NULL) {
    return ARES_FALSE;
  }

  bucket = ares_htable_get(htable->hash, key);
  if (bucket == NULL) {
    return ARES_FALSE;
  }

  if (val) {
    *val = bucket->val;
  }
  return ARES_TRUE;
}

const char *ares_htable_dict_get_direct(const ares_htable_dict_t *htable,
                                        const char               *key)
{
  const char *val = NULL;
  ares_htable_dict_get(htable, key, &val);
  return val;
}

ares_bool_t ares_htable_dict_remove(ares_htable_dict_t *htable, const char *key)
{
  if (htable == NULL) {
    return ARES_FALSE;
  }

  return ares_htable_remove(htable->hash, key);
}

size_t ares_htable_dict_num_keys(const ares_htable_dict_t *htable)
{
  if (htable == NULL) {
    return 0;
  }
  return ares_htable_num_keys(htable->hash);
}

char **ares_htable_dict_keys(const ares_htable_dict_t *htable, size_t *num)
{
  const void **buckets = NULL;
  size_t       cnt     = 0;
  char       **out     = NULL;
  size_t       i;

  if (htable == NULL || num == NULL) {
    return NULL; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  *num = 0;

  buckets = ares_htable_all_buckets(htable->hash, &cnt);
  if (buckets == NULL || cnt == 0) {
    return NULL;
  }

  out = ares_malloc_zero(sizeof(*out) * cnt);
  if (out == NULL) {
    goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  for (i = 0; i < cnt; i++) {
    out[i] = ares_strdup(((const ares_htable_dict_bucket_t *)buckets[i])->key);
    if (out[i] == NULL) {
      goto fail;
    }
  }

  ares_free(buckets);
  *num = cnt;
  return out;

fail:
  *num = 0;
  ares_free_array(out, cnt, ares_free);
  return NULL;
}
