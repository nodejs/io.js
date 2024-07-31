/* Copyright 2017 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

#include "compound_dictionary.h"

#include <brotli/types.h>

#include "../common/platform.h"
#include "memory.h"
#include "quality.h"

static PreparedDictionary* CreatePreparedDictionaryWithParams(MemoryManager* m,
    const uint8_t* source, size_t source_size, uint32_t bucket_bits,
    uint32_t slot_bits, uint32_t hash_bits, uint16_t bucket_limit) {
  /* Step 1: create "bloated" hasher. */
  uint32_t num_slots = 1u << slot_bits;
  uint32_t num_buckets = 1u << bucket_bits;
  uint32_t hash_shift = 64u - bucket_bits;
  uint64_t hash_mask = (~((uint64_t)0U)) >> (64 - hash_bits);
  uint32_t slot_mask = num_slots - 1;
  size_t alloc_size = (sizeof(uint32_t) << slot_bits) +
      (sizeof(uint32_t) << slot_bits) +
      (sizeof(uint16_t) << bucket_bits) +
      (sizeof(uint32_t) << bucket_bits) +
      (sizeof(uint32_t) * source_size);
  uint8_t* flat = NULL;
  PreparedDictionary* result = NULL;
  uint16_t* num = NULL;
  uint32_t* bucket_heads = NULL;
  uint32_t* next_bucket = NULL;
  uint32_t* slot_offsets = NULL;
  uint16_t* heads = NULL;
  uint32_t* items = NULL;
  uint8_t** source_ref = NULL;
  uint32_t i;
  uint32_t* slot_size = NULL;
  uint32_t* slot_limit = NULL;
  uint32_t total_items = 0;
  if (slot_bits > 16) return NULL;
  if (slot_bits > bucket_bits) return NULL;
  if (bucket_bits - slot_bits >= 16) return NULL;

  flat = BROTLI_ALLOC(m, uint8_t, alloc_size);
  if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(flat)) return NULL;

  slot_size = (uint32_t*)flat;
  slot_limit = (uint32_t*)(&slot_size[num_slots]);
  num = (uint16_t*)(&slot_limit[num_slots]);
  bucket_heads = (uint32_t*)(&num[num_buckets]);
  next_bucket = (uint32_t*)(&bucket_heads[num_buckets]);
  memset(num, 0, num_buckets * sizeof(num[0]));

  /* TODO(eustas): apply custom "store" order. */
  for (i = 0; i + 7 < source_size; ++i) {
    const uint64_t h = (BROTLI_UNALIGNED_LOAD64LE(&source[i]) & hash_mask) *
        kPreparedDictionaryHashMul64Long;
    const uint32_t key = (uint32_t)(h >> hash_shift);
    uint16_t count = num[key];
    next_bucket[i] = (count == 0) ? ((uint32_t)(-1)) : bucket_heads[key];
    bucket_heads[key] = i;
    count++;
    if (count > bucket_limit) count = bucket_limit;
    num[key] = count;
  }

  /* Step 2: find slot limits. */
  for (i = 0; i < num_slots; ++i) {
    BROTLI_BOOL overflow = BROTLI_FALSE;
    slot_limit[i] = bucket_limit;
    while (BROTLI_TRUE) {
      uint32_t limit = slot_limit[i];
      size_t j;
      uint32_t count = 0;
      overflow = BROTLI_FALSE;
      for (j = i; j < num_buckets; j += num_slots) {
        uint32_t size = num[j];
        /* Last chain may span behind 64K limit; overflow happens only if
           we are about to use 0xFFFF+ as item offset. */
        if (count >= 0xFFFF) {
          overflow = BROTLI_TRUE;
          break;
        }
        if (size > limit) size = limit;
        count += size;
      }
      if (!overflow) {
        slot_size[i] = count;
        total_items += count;
        break;
      }
      slot_limit[i]--;
    }
  }

  /* Step 3: transfer data to "slim" hasher. */
  alloc_size = sizeof(PreparedDictionary) + (sizeof(uint32_t) << slot_bits) +
      (sizeof(uint16_t) << bucket_bits) + (sizeof(uint32_t) * total_items) +
      sizeof(uint8_t*);

  result = (PreparedDictionary*)BROTLI_ALLOC(m, uint8_t, alloc_size);
  if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(result)) {
    BROTLI_FREE(m, flat);
    return NULL;
  }
  slot_offsets = (uint32_t*)(&result[1]);
  heads = (uint16_t*)(&slot_offsets[num_slots]);
  items = (uint32_t*)(&heads[num_buckets]);
  source_ref = (uint8_t**)(&items[total_items]);

  result->magic = kLeanPreparedDictionaryMagic;
  result->num_items = total_items;
  result->source_size = (uint32_t)source_size;
  result->hash_bits = hash_bits;
  result->bucket_bits = bucket_bits;
  result->slot_bits = slot_bits;
  BROTLI_UNALIGNED_STORE_PTR(source_ref, source);

  total_items = 0;
  for (i = 0; i < num_slots; ++i) {
    slot_offsets[i] = total_items;
    total_items += slot_size[i];
    slot_size[i] = 0;
  }
  for (i = 0; i < num_buckets; ++i) {
    uint32_t slot = i & slot_mask;
    uint32_t count = num[i];
    uint32_t pos;
    size_t j;
    size_t cursor = slot_size[slot];
    if (count > slot_limit[slot]) count = slot_limit[slot];
    if (count == 0) {
      heads[i] = 0xFFFF;
      continue;
    }
    heads[i] = (uint16_t)cursor;
    cursor += slot_offsets[slot];
    slot_size[slot] += count;
    pos = bucket_heads[i];
    for (j = 0; j < count; j++) {
      items[cursor++] = pos;
      pos = next_bucket[pos];
    }
    items[cursor - 1] |= 0x80000000;
  }

  BROTLI_FREE(m, flat);
  return result;
}

PreparedDictionary* CreatePreparedDictionary(MemoryManager* m,
    const uint8_t* source, size_t source_size) {
  uint32_t bucket_bits = 17;
  uint32_t slot_bits = 7;
  uint32_t hash_bits = 40;
  uint16_t bucket_limit = 32;
  size_t volume = 16u << bucket_bits;
  /* Tune parameters to fit dictionary size. */
  while (volume < source_size && bucket_bits < 22) {
    bucket_bits++;
    slot_bits++;
    volume <<= 1;
  }
  return CreatePreparedDictionaryWithParams(m,
      source, source_size, bucket_bits, slot_bits, hash_bits, bucket_limit);
}

void DestroyPreparedDictionary(MemoryManager* m,
    PreparedDictionary* dictionary) {
  if (!dictionary) return;
  BROTLI_FREE(m, dictionary);
}

BROTLI_BOOL AttachPreparedDictionary(
    CompoundDictionary* compound, const PreparedDictionary* dictionary) {
  size_t length = 0;
  size_t index = 0;

  if (compound->num_chunks == SHARED_BROTLI_MAX_COMPOUND_DICTS) {
    return BROTLI_FALSE;
  }

  if (!dictionary) return BROTLI_FALSE;

  length = dictionary->source_size;
  index = compound->num_chunks;
  compound->total_size += length;
  compound->chunks[index] = dictionary;
  compound->chunk_offsets[index + 1] = compound->total_size;
  {
    uint32_t* slot_offsets = (uint32_t*)(&dictionary[1]);
    uint16_t* heads = (uint16_t*)(&slot_offsets[1u << dictionary->slot_bits]);
    uint32_t* items = (uint32_t*)(&heads[1u << dictionary->bucket_bits]);
    const void* tail = (void*)&items[dictionary->num_items];
    if (dictionary->magic == kPreparedDictionaryMagic) {
      compound->chunk_source[index] = (const uint8_t*)tail;
    } else {
      /* dictionary->magic == kLeanPreparedDictionaryMagic */
      compound->chunk_source[index] =
          (const uint8_t*)BROTLI_UNALIGNED_LOAD_PTR((const uint8_t**)tail);
    }
  }
  compound->num_chunks++;
  return BROTLI_TRUE;
}
