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
#include "ares_llist.h"
#include "ares_htable.h"

#define ARES__HTABLE_MAX_BUCKETS    (1U << 24)
#define ARES__HTABLE_MIN_BUCKETS    (1U << 4)
#define ARES__HTABLE_EXPAND_PERCENT 75

struct ares_htable {
  ares_htable_hashfunc_t    hash;
  ares_htable_bucket_key_t  bucket_key;
  ares_htable_bucket_free_t bucket_free;
  ares_htable_key_eq_t      key_eq;
  unsigned int              seed;
  unsigned int              size;
  size_t                    num_keys;
  size_t                    num_collisions;
  /* NOTE: if we converted buckets into ares_slist_t we could guarantee on
   *       hash collisions we would have O(log n) worst case insert and search
   *       performance.  (We'd also need to make key_eq into a key_cmp to
   *       support sort).  That said, risk with a random hash seed is near zero,
   *       and ares_slist_t is heavier weight, so I think using ares_llist_t
   *       is an overall win. */
  ares_llist_t            **buckets;
};

static unsigned int ares_htable_generate_seed(ares_htable_t *htable)
{
#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
  /* Seed needs to be static for fuzzing */
  return 0;
#else
  unsigned int seed = 0;
  time_t       t    = time(NULL);

  /* Mix stack address, heap address, and time to generate a random seed, it
   * doesn't have to be super secure, just quick.  Likelihood of a hash
   * collision attack is very low with a small amount of effort */
  seed |= (unsigned int)((size_t)htable & 0xFFFFFFFF);
  seed |= (unsigned int)((size_t)&seed & 0xFFFFFFFF);
  seed |= (unsigned int)(((ares_uint64_t)t) & 0xFFFFFFFF);
  return seed;
#endif
}

static void ares_htable_buckets_destroy(ares_llist_t **buckets,
                                        unsigned int   size,
                                        ares_bool_t    destroy_vals)
{
  unsigned int i;

  if (buckets == NULL) {
    return;
  }

  for (i = 0; i < size; i++) {
    if (buckets[i] == NULL) {
      continue;
    }

    if (!destroy_vals) {
      ares_llist_replace_destructor(buckets[i], NULL);
    }

    ares_llist_destroy(buckets[i]);
  }

  ares_free(buckets);
}

void ares_htable_destroy(ares_htable_t *htable)
{
  if (htable == NULL) {
    return;
  }
  ares_htable_buckets_destroy(htable->buckets, htable->size, ARES_TRUE);
  ares_free(htable);
}

ares_htable_t *ares_htable_create(ares_htable_hashfunc_t    hash_func,
                                  ares_htable_bucket_key_t  bucket_key,
                                  ares_htable_bucket_free_t bucket_free,
                                  ares_htable_key_eq_t      key_eq)
{
  ares_htable_t *htable = NULL;

  if (hash_func == NULL || bucket_key == NULL || bucket_free == NULL ||
      key_eq == NULL) {
    goto fail;
  }

  htable = ares_malloc_zero(sizeof(*htable));
  if (htable == NULL) {
    goto fail;
  }

  htable->hash        = hash_func;
  htable->bucket_key  = bucket_key;
  htable->bucket_free = bucket_free;
  htable->key_eq      = key_eq;
  htable->seed        = ares_htable_generate_seed(htable);
  htable->size        = ARES__HTABLE_MIN_BUCKETS;
  htable->buckets = ares_malloc_zero(sizeof(*htable->buckets) * htable->size);

  if (htable->buckets == NULL) {
    goto fail;
  }

  return htable;

fail:
  ares_htable_destroy(htable);
  return NULL;
}

const void **ares_htable_all_buckets(const ares_htable_t *htable, size_t *num)
{
  const void **out = NULL;
  size_t       cnt = 0;
  size_t       i;

  if (htable == NULL || num == NULL) {
    return NULL; /* LCOV_EXCL_LINE */
  }

  *num = 0;

  out = ares_malloc_zero(sizeof(*out) * htable->num_keys);
  if (out == NULL) {
    return NULL; /* LCOV_EXCL_LINE */
  }

  for (i = 0; i < htable->size; i++) {
    ares_llist_node_t *node;
    for (node = ares_llist_node_first(htable->buckets[i]); node != NULL;
         node = ares_llist_node_next(node)) {
      out[cnt++] = ares_llist_node_val(node);
    }
  }

  *num = cnt;
  return out;
}

/*! Grabs the Hashtable index from the key and length.  The h index is
 *  the hash of the function reduced to the size of the bucket list.
 *  We are doing "hash & (size - 1)" since we are guaranteeing a power of
 *  2 for size. This is equivalent to "hash % size", but should be more
 * efficient */
#define HASH_IDX(h, key) h->hash(key, h->seed) & (h->size - 1)

static ares_llist_node_t *ares_htable_find(const ares_htable_t *htable,
                                           unsigned int idx, const void *key)
{
  ares_llist_node_t *node = NULL;

  for (node = ares_llist_node_first(htable->buckets[idx]); node != NULL;
       node = ares_llist_node_next(node)) {
    if (htable->key_eq(key, htable->bucket_key(ares_llist_node_val(node)))) {
      break;
    }
  }

  return node;
}

static ares_bool_t ares_htable_expand(ares_htable_t *htable)
{
  ares_llist_t **buckets  = NULL;
  unsigned int   old_size = htable->size;
  size_t         i;
  ares_llist_t **prealloc_llist     = NULL;
  size_t         prealloc_llist_len = 0;
  ares_bool_t    rv                 = ARES_FALSE;

  /* Not a failure, just won't expand */
  if (old_size == ARES__HTABLE_MAX_BUCKETS) {
    return ARES_TRUE; /* LCOV_EXCL_LINE */
  }

  htable->size <<= 1;

  /* We must pre-allocate all memory we'll need before moving entries to the
   * new hash array.  Otherwise if there's a memory allocation failure in the
   * middle, we wouldn't be able to recover. */
  buckets = ares_malloc_zero(sizeof(*buckets) * htable->size);
  if (buckets == NULL) {
    goto done; /* LCOV_EXCL_LINE */
  }

  /* The maximum number of new llists we'll need is the number of collisions
   * that were recorded */
  prealloc_llist_len = htable->num_collisions;
  if (prealloc_llist_len) {
    prealloc_llist =
      ares_malloc_zero(sizeof(*prealloc_llist) * prealloc_llist_len);
    if (prealloc_llist == NULL) {
      goto done; /* LCOV_EXCL_LINE */
    }
  }
  for (i = 0; i < prealloc_llist_len; i++) {
    prealloc_llist[i] = ares_llist_create(htable->bucket_free);
    if (prealloc_llist[i] == NULL) {
      goto done;
    }
  }

  /* Iterate across all buckets and move the entries to the new buckets */
  htable->num_collisions = 0;
  for (i = 0; i < old_size; i++) {
    ares_llist_node_t *node;

    /* Nothing in this bucket */
    if (htable->buckets[i] == NULL) {
      continue;
    }

    /* Fast path optimization (most likely case), there is likely only a single
     * entry in both the source and destination, check for this to confirm and
     * if so, just move the bucket over */
    if (ares_llist_len(htable->buckets[i]) == 1) {
      const void *val = ares_llist_first_val(htable->buckets[i]);
      size_t      idx = HASH_IDX(htable, htable->bucket_key(val));

      if (buckets[idx] == NULL) {
        /* Swap! */
        buckets[idx]       = htable->buckets[i];
        htable->buckets[i] = NULL;
        continue;
      }
    }

    /* Slow path, collisions */
    while ((node = ares_llist_node_first(htable->buckets[i])) != NULL) {
      const void *val = ares_llist_node_val(node);
      size_t      idx = HASH_IDX(htable, htable->bucket_key(val));

      /* Try fast path again as maybe we popped one collision off and the
       * next we can reuse the llist parent */
      if (buckets[idx] == NULL && ares_llist_len(htable->buckets[i]) == 1) {
        /* Swap! */
        buckets[idx]       = htable->buckets[i];
        htable->buckets[i] = NULL;
        break;
      }

      /* Grab one off our preallocated list */
      if (buckets[idx] == NULL) {
        /* Silence static analysis, this isn't possible but it doesn't know */
        if (prealloc_llist == NULL || prealloc_llist_len == 0) {
          goto done; /* LCOV_EXCL_LINE */
        }
        buckets[idx] = prealloc_llist[prealloc_llist_len - 1];
        prealloc_llist_len--;
      } else {
        /* Collision occurred since the bucket wasn't empty */
        htable->num_collisions++;
      }

      ares_llist_node_mvparent_first(node, buckets[idx]);
    }

    /* Abandoned bucket, destroy */
    if (htable->buckets[i] != NULL) {
      ares_llist_destroy(htable->buckets[i]);
      htable->buckets[i] = NULL;
    }
  }

  /* We have guaranteed all the buckets have either been moved or destroyed,
   * so we just call ares_free() on the array and swap out the pointer */
  ares_free(htable->buckets);
  htable->buckets = buckets;
  buckets         = NULL;
  rv              = ARES_TRUE;

done:
  ares_free(buckets);
  /* destroy any unused preallocated buckets */
  ares_htable_buckets_destroy(prealloc_llist, (unsigned int)prealloc_llist_len,
                              ARES_FALSE);

  /* On failure, we need to restore the htable size */
  if (rv != ARES_TRUE) {
    htable->size = old_size; /* LCOV_EXCL_LINE */
  }

  return rv;
}

ares_bool_t ares_htable_insert(ares_htable_t *htable, void *bucket)
{
  unsigned int       idx  = 0;
  ares_llist_node_t *node = NULL;
  const void        *key  = NULL;

  if (htable == NULL || bucket == NULL) {
    return ARES_FALSE;
  }


  key = htable->bucket_key(bucket);
  idx = HASH_IDX(htable, key);

  /* See if we have a matching bucket already, if so, replace it */
  node = ares_htable_find(htable, idx, key);
  if (node != NULL) {
    ares_llist_node_replace(node, bucket);
    return ARES_TRUE;
  }

  /* Check to see if we should rehash because likelihood of collisions has
   * increased beyond our threshold */
  if (htable->num_keys + 1 >
      (htable->size * ARES__HTABLE_EXPAND_PERCENT) / 100) {
    if (!ares_htable_expand(htable)) {
      return ARES_FALSE; /* LCOV_EXCL_LINE */
    }
    /* If we expanded, need to calculate a new index */
    idx = HASH_IDX(htable, key);
  }

  /* We lazily allocate the linked list */
  if (htable->buckets[idx] == NULL) {
    htable->buckets[idx] = ares_llist_create(htable->bucket_free);
    if (htable->buckets[idx] == NULL) {
      return ARES_FALSE;
    }
  }

  node = ares_llist_insert_first(htable->buckets[idx], bucket);
  if (node == NULL) {
    return ARES_FALSE;
  }

  /* Track collisions for rehash stability */
  if (ares_llist_len(htable->buckets[idx]) > 1) {
    htable->num_collisions++;
  }

  htable->num_keys++;

  return ARES_TRUE;
}

void *ares_htable_get(const ares_htable_t *htable, const void *key)
{
  unsigned int idx;

  if (htable == NULL || key == NULL) {
    return NULL;
  }

  idx = HASH_IDX(htable, key);

  return ares_llist_node_val(ares_htable_find(htable, idx, key));
}

ares_bool_t ares_htable_remove(ares_htable_t *htable, const void *key)
{
  ares_llist_node_t *node;
  unsigned int       idx;

  if (htable == NULL || key == NULL) {
    return ARES_FALSE;
  }

  idx  = HASH_IDX(htable, key);
  node = ares_htable_find(htable, idx, key);
  if (node == NULL) {
    return ARES_FALSE;
  }

  htable->num_keys--;

  /* Reduce collisions */
  if (ares_llist_len(ares_llist_node_parent(node)) > 1) {
    htable->num_collisions--;
  }

  ares_llist_node_destroy(node);
  return ARES_TRUE;
}

size_t ares_htable_num_keys(const ares_htable_t *htable)
{
  if (htable == NULL) {
    return 0;
  }
  return htable->num_keys;
}

unsigned int ares_htable_hash_FNV1a(const unsigned char *key, size_t key_len,
                                    unsigned int seed)
{
  unsigned int hv = seed ^ 2166136261U;
  size_t       i;

  for (i = 0; i < key_len; i++) {
    hv ^= (unsigned int)key[i];
    /* hv *= 16777619 (0x01000193) */
    hv += (hv << 1) + (hv << 4) + (hv << 7) + (hv << 8) + (hv << 24);
  }

  return hv;
}

/* Case insensitive version, meant for ASCII strings */
unsigned int ares_htable_hash_FNV1a_casecmp(const unsigned char *key,
                                            size_t key_len, unsigned int seed)
{
  unsigned int hv = seed ^ 2166136261U;
  size_t       i;

  for (i = 0; i < key_len; i++) {
    hv ^= (unsigned int)ares_tolower(key[i]);
    /* hv *= 16777619 (0x01000193) */
    hv += (hv << 1) + (hv << 4) + (hv << 7) + (hv << 8) + (hv << 24);
  }

  return hv;
}
