/*
 * nghttp3
 *
 * Copyright (c) 2019 nghttp3 contributors
 * Copyright (c) 2017 ngtcp2 contributors
 * Copyright (c) 2012 nghttp2 contributors
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
#include "nghttp3_map.h"

#include <string.h>
#include <assert.h>
#include <stdio.h>

#include "nghttp3_conv.h"

#define NGHTTP3_INITIAL_TABLE_LENBITS 4

void nghttp3_map_init(nghttp3_map *map, const nghttp3_mem *mem) {
  map->mem = mem;
  map->tablelen = 0;
  map->tablelenbits = 0;
  map->table = NULL;
  map->size = 0;
}

void nghttp3_map_free(nghttp3_map *map) {
  if (!map) {
    return;
  }

  nghttp3_mem_free(map->mem, map->table);
}

void nghttp3_map_each_free(nghttp3_map *map, int (*func)(void *data, void *ptr),
                           void *ptr) {
  uint32_t i;
  nghttp3_map_bucket *bkt;

  for (i = 0; i < map->tablelen; ++i) {
    bkt = &map->table[i];

    if (bkt->data == NULL) {
      continue;
    }

    func(bkt->data, ptr);
  }
}

int nghttp3_map_each(nghttp3_map *map, int (*func)(void *data, void *ptr),
                     void *ptr) {
  int rv;
  uint32_t i;
  nghttp3_map_bucket *bkt;

  if (map->size == 0) {
    return 0;
  }

  for (i = 0; i < map->tablelen; ++i) {
    bkt = &map->table[i];

    if (bkt->data == NULL) {
      continue;
    }

    rv = func(bkt->data, ptr);
    if (rv != 0) {
      return rv;
    }
  }

  return 0;
}

static uint32_t hash(nghttp3_map_key_type key) {
  return (uint32_t)((key * 11400714819323198485llu) >> 32);
}

static size_t h2idx(uint32_t hash, uint32_t bits) {
  return hash >> (32 - bits);
}

static size_t distance(uint32_t tablelen, uint32_t tablelenbits,
                       nghttp3_map_bucket *bkt, size_t idx) {
  return (idx - h2idx(bkt->hash, tablelenbits)) & (tablelen - 1);
}

static void map_bucket_swap(nghttp3_map_bucket *bkt, uint32_t *phash,
                            nghttp3_map_key_type *pkey, void **pdata) {
  uint32_t h = bkt->hash;
  nghttp3_map_key_type key = bkt->key;
  void *data = bkt->data;

  bkt->hash = *phash;
  bkt->key = *pkey;
  bkt->data = *pdata;

  *phash = h;
  *pkey = key;
  *pdata = data;
}

static void map_bucket_set_data(nghttp3_map_bucket *bkt, uint32_t hash,
                                nghttp3_map_key_type key, void *data) {
  bkt->hash = hash;
  bkt->key = key;
  bkt->data = data;
}

#ifndef WIN32
void nghttp3_map_print_distance(nghttp3_map *map) {
  uint32_t i;
  size_t idx;
  nghttp3_map_bucket *bkt;

  for (i = 0; i < map->tablelen; ++i) {
    bkt = &map->table[i];

    if (bkt->data == NULL) {
      fprintf(stderr, "@%u <EMPTY>\n", i);
      continue;
    }

    idx = h2idx(bkt->hash, map->tablelenbits);
    fprintf(stderr, "@%u hash=%08x key=%" PRIu64 " base=%zu distance=%zu\n", i,
            bkt->hash, bkt->key, idx,
            distance(map->tablelen, map->tablelenbits, bkt, idx));
  }
}
#endif /* !WIN32 */

static int insert(nghttp3_map_bucket *table, uint32_t tablelen,
                  uint32_t tablelenbits, uint32_t hash,
                  nghttp3_map_key_type key, void *data) {
  size_t idx = h2idx(hash, tablelenbits);
  size_t d = 0, dd;
  nghttp3_map_bucket *bkt;

  for (;;) {
    bkt = &table[idx];

    if (bkt->data == NULL) {
      map_bucket_set_data(bkt, hash, key, data);
      return 0;
    }

    dd = distance(tablelen, tablelenbits, bkt, idx);
    if (d > dd) {
      map_bucket_swap(bkt, &hash, &key, &data);
      d = dd;
    } else if (bkt->key == key) {
      /* TODO This check is just a waste after first swap or if this
         function is called from map_resize.  That said, there is no
         difference with or without this conditional in performance
         wise. */
      return NGHTTP3_ERR_INVALID_ARGUMENT;
    }

    ++d;
    idx = (idx + 1) & (tablelen - 1);
  }
}

/* new_tablelen must be power of 2 and new_tablelen == (1 <<
   new_tablelenbits) must hold. */
static int map_resize(nghttp3_map *map, uint32_t new_tablelen,
                      uint32_t new_tablelenbits) {
  uint32_t i;
  nghttp3_map_bucket *new_table;
  nghttp3_map_bucket *bkt;
  int rv;
  (void)rv;

  new_table =
      nghttp3_mem_calloc(map->mem, new_tablelen, sizeof(nghttp3_map_bucket));
  if (new_table == NULL) {
    return NGHTTP3_ERR_NOMEM;
  }

  for (i = 0; i < map->tablelen; ++i) {
    bkt = &map->table[i];
    if (bkt->data == NULL) {
      continue;
    }
    rv = insert(new_table, new_tablelen, new_tablelenbits, bkt->hash, bkt->key,
                bkt->data);

    assert(0 == rv);
  }

  nghttp3_mem_free(map->mem, map->table);
  map->tablelen = new_tablelen;
  map->tablelenbits = new_tablelenbits;
  map->table = new_table;

  return 0;
}

int nghttp3_map_insert(nghttp3_map *map, nghttp3_map_key_type key, void *data) {
  int rv;

  assert(data);

  /* Load factor is 0.75 */
  if ((map->size + 1) * 4 > map->tablelen * 3) {
    if (map->tablelen) {
      rv = map_resize(map, map->tablelen * 2, map->tablelenbits + 1);
      if (rv != 0) {
        return rv;
      }
    } else {
      rv = map_resize(map, 1 << NGHTTP3_INITIAL_TABLE_LENBITS,
                      NGHTTP3_INITIAL_TABLE_LENBITS);
      if (rv != 0) {
        return rv;
      }
    }
  }

  rv = insert(map->table, map->tablelen, map->tablelenbits, hash(key), key,
              data);
  if (rv != 0) {
    return rv;
  }
  ++map->size;
  return 0;
}

void *nghttp3_map_find(nghttp3_map *map, nghttp3_map_key_type key) {
  uint32_t h;
  size_t idx;
  nghttp3_map_bucket *bkt;
  size_t d = 0;

  if (map->size == 0) {
    return NULL;
  }

  h = hash(key);
  idx = h2idx(h, map->tablelenbits);

  for (;;) {
    bkt = &map->table[idx];

    if (bkt->data == NULL ||
        d > distance(map->tablelen, map->tablelenbits, bkt, idx)) {
      return NULL;
    }

    if (bkt->key == key) {
      return bkt->data;
    }

    ++d;
    idx = (idx + 1) & (map->tablelen - 1);
  }
}

int nghttp3_map_remove(nghttp3_map *map, nghttp3_map_key_type key) {
  uint32_t h;
  size_t idx, didx;
  nghttp3_map_bucket *bkt;
  size_t d = 0;

  if (map->size == 0) {
    return NGHTTP3_ERR_INVALID_ARGUMENT;
  }

  h = hash(key);
  idx = h2idx(h, map->tablelenbits);

  for (;;) {
    bkt = &map->table[idx];

    if (bkt->data == NULL ||
        d > distance(map->tablelen, map->tablelenbits, bkt, idx)) {
      return NGHTTP3_ERR_INVALID_ARGUMENT;
    }

    if (bkt->key == key) {
      map_bucket_set_data(bkt, 0, 0, NULL);

      didx = idx;
      idx = (idx + 1) & (map->tablelen - 1);

      for (;;) {
        bkt = &map->table[idx];
        if (bkt->data == NULL ||
            distance(map->tablelen, map->tablelenbits, bkt, idx) == 0) {
          break;
        }

        map->table[didx] = *bkt;
        map_bucket_set_data(bkt, 0, 0, NULL);
        didx = idx;

        idx = (idx + 1) & (map->tablelen - 1);
      }

      --map->size;

      return 0;
    }

    ++d;
    idx = (idx + 1) & (map->tablelen - 1);
  }
}

void nghttp3_map_clear(nghttp3_map *map) {
  if (map->tablelen == 0) {
    return;
  }

  memset(map->table, 0, sizeof(*map->table) * map->tablelen);
  map->size = 0;
}

size_t nghttp3_map_size(nghttp3_map *map) { return map->size; }
