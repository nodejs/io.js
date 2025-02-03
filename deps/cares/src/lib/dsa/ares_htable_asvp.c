/* MIT License
 *
 * Copyright (c) 2023 Brad House
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
#include "ares_htable_asvp.h"

struct ares_htable_asvp {
  ares_htable_asvp_val_free_t free_val;
  ares_htable_t              *hash;
};

typedef struct {
  ares_socket_t       key;
  void               *val;
  ares_htable_asvp_t *parent;
} ares_htable_asvp_bucket_t;

void ares_htable_asvp_destroy(ares_htable_asvp_t *htable)
{
  if (htable == NULL) {
    return;
  }

  ares_htable_destroy(htable->hash);
  ares_free(htable);
}

static unsigned int hash_func(const void *key, unsigned int seed)
{
  const ares_socket_t *arg = key;
  return ares_htable_hash_FNV1a((const unsigned char *)arg, sizeof(*arg), seed);
}

static const void *bucket_key(const void *bucket)
{
  const ares_htable_asvp_bucket_t *arg = bucket;
  return &arg->key;
}

static void bucket_free(void *bucket)
{
  ares_htable_asvp_bucket_t *arg = bucket;

  if (arg->parent->free_val) {
    arg->parent->free_val(arg->val);
  }

  ares_free(arg);
}

static ares_bool_t key_eq(const void *key1, const void *key2)
{
  const ares_socket_t *k1 = key1;
  const ares_socket_t *k2 = key2;

  if (*k1 == *k2) {
    return ARES_TRUE;
  }

  return ARES_FALSE;
}

ares_htable_asvp_t *
  ares_htable_asvp_create(ares_htable_asvp_val_free_t val_free)
{
  ares_htable_asvp_t *htable = ares_malloc(sizeof(*htable));
  if (htable == NULL) {
    goto fail;
  }

  htable->hash = ares_htable_create(hash_func, bucket_key, bucket_free, key_eq);
  if (htable->hash == NULL) {
    goto fail;
  }

  htable->free_val = val_free;

  return htable;

fail:
  if (htable) {
    ares_htable_destroy(htable->hash);
    ares_free(htable);
  }
  return NULL;
}

ares_socket_t *ares_htable_asvp_keys(const ares_htable_asvp_t *htable,
                                     size_t                   *num)
{
  const void   **buckets = NULL;
  size_t         cnt     = 0;
  ares_socket_t *out     = NULL;
  size_t         i;

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
    ares_free(buckets); /* LCOV_EXCL_LINE: OutOfMemory */
    return NULL;        /* LCOV_EXCL_LINE: OutOfMemory */
  }

  for (i = 0; i < cnt; i++) {
    out[i] = ((const ares_htable_asvp_bucket_t *)buckets[i])->key;
  }

  ares_free(buckets);
  *num = cnt;
  return out;
}

ares_bool_t ares_htable_asvp_insert(ares_htable_asvp_t *htable,
                                    ares_socket_t key, void *val)
{
  ares_htable_asvp_bucket_t *bucket = NULL;

  if (htable == NULL) {
    goto fail;
  }

  bucket = ares_malloc(sizeof(*bucket));
  if (bucket == NULL) {
    goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  bucket->parent = htable;
  bucket->key    = key;
  bucket->val    = val;

  if (!ares_htable_insert(htable->hash, bucket)) {
    goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  return ARES_TRUE;

fail:
  if (bucket) {
    ares_free(bucket); /* LCOV_EXCL_LINE: OutOfMemory */
  }
  return ARES_FALSE;
}

ares_bool_t ares_htable_asvp_get(const ares_htable_asvp_t *htable,
                                 ares_socket_t key, void **val)
{
  ares_htable_asvp_bucket_t *bucket = NULL;

  if (val) {
    *val = NULL;
  }

  if (htable == NULL) {
    return ARES_FALSE;
  }

  bucket = ares_htable_get(htable->hash, &key);
  if (bucket == NULL) {
    return ARES_FALSE;
  }

  if (val) {
    *val = bucket->val;
  }
  return ARES_TRUE;
}

void *ares_htable_asvp_get_direct(const ares_htable_asvp_t *htable,
                                  ares_socket_t             key)
{
  void *val = NULL;
  ares_htable_asvp_get(htable, key, &val);
  return val;
}

ares_bool_t ares_htable_asvp_remove(ares_htable_asvp_t *htable,
                                    ares_socket_t       key)
{
  if (htable == NULL) {
    return ARES_FALSE;
  }

  return ares_htable_remove(htable->hash, &key);
}

size_t ares_htable_asvp_num_keys(const ares_htable_asvp_t *htable)
{
  if (htable == NULL) {
    return 0;
  }
  return ares_htable_num_keys(htable->hash);
}
