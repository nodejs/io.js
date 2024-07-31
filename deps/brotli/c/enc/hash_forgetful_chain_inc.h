/* NOLINT(build/header_guard) */
/* Copyright 2016 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* template parameters: FN, BUCKET_BITS, NUM_BANKS, BANK_BITS,
                        NUM_LAST_DISTANCES_TO_CHECK */

/* A (forgetful) hash table to the data seen by the compressor, to
   help create backward references to previous data.

   Hashes are stored in chains which are bucketed to groups. Group of chains
   share a storage "bank". When more than "bank size" chain nodes are added,
   oldest nodes are replaced; this way several chains may share a tail. */

#define HashForgetfulChain HASHER()

#define BANK_SIZE (1 << BANK_BITS)

/* Number of hash buckets. */
#define BUCKET_SIZE (1 << BUCKET_BITS)

#define CAPPED_CHAINS 0

static BROTLI_INLINE size_t FN(HashTypeLength)(void) { return 4; }
static BROTLI_INLINE size_t FN(StoreLookahead)(void) { return 4; }

/* HashBytes is the function that chooses the bucket to place the address in.*/
static BROTLI_INLINE size_t FN(HashBytes)(const uint8_t* BROTLI_RESTRICT data) {
  const uint32_t h = BROTLI_UNALIGNED_LOAD32LE(data) * kHashMul32;
  /* The higher bits contain more mixture from the multiplication,
     so we take our results from there. */
  return h >> (32 - BUCKET_BITS);
}

typedef struct FN(Slot) {
  uint16_t delta;
  uint16_t next;
} FN(Slot);

typedef struct FN(Bank) {
  FN(Slot) slots[BANK_SIZE];
} FN(Bank);

typedef struct HashForgetfulChain {
  uint16_t free_slot_idx[NUM_BANKS];  /* Up to 1KiB. Move to dynamic? */
  size_t max_hops;

  /* Shortcuts. */
  void* extra[2];
  HasherCommon* common;

  /* --- Dynamic size members --- */

  /* uint32_t addr[BUCKET_SIZE]; */

  /* uint16_t head[BUCKET_SIZE]; */

  /* Truncated hash used for quick rejection of "distance cache" candidates. */
  /* uint8_t tiny_hash[65536];*/

  /* FN(Bank) banks[NUM_BANKS]; */
} HashForgetfulChain;

static uint32_t* FN(Addr)(void* extra) {
  return (uint32_t*)extra;
}

static uint16_t* FN(Head)(void* extra) {
  return (uint16_t*)(&FN(Addr)(extra)[BUCKET_SIZE]);
}

static uint8_t* FN(TinyHash)(void* extra) {
  return (uint8_t*)(&FN(Head)(extra)[BUCKET_SIZE]);
}

static FN(Bank)* FN(Banks)(void* extra) {
  return (FN(Bank)*)(extra);
}

static void FN(Initialize)(
    HasherCommon* common, HashForgetfulChain* BROTLI_RESTRICT self,
    const BrotliEncoderParams* params) {
  self->common = common;
  self->extra[0] = common->extra[0];
  self->extra[1] = common->extra[1];

  self->max_hops = (params->quality > 6 ? 7u : 8u) << (params->quality - 4);
}

static void FN(Prepare)(
    HashForgetfulChain* BROTLI_RESTRICT self, BROTLI_BOOL one_shot,
    size_t input_size, const uint8_t* BROTLI_RESTRICT data) {
  uint32_t* BROTLI_RESTRICT addr = FN(Addr)(self->extra[0]);
  uint16_t* BROTLI_RESTRICT head = FN(Head)(self->extra[0]);
  uint8_t* BROTLI_RESTRICT tiny_hash = FN(TinyHash)(self->extra[0]);
  /* Partial preparation is 100 times slower (per socket). */
  size_t partial_prepare_threshold = BUCKET_SIZE >> 6;
  if (one_shot && input_size <= partial_prepare_threshold) {
    size_t i;
    for (i = 0; i < input_size; ++i) {
      size_t bucket = FN(HashBytes)(&data[i]);
      /* See InitEmpty comment. */
      addr[bucket] = 0xCCCCCCCC;
      head[bucket] = 0xCCCC;
    }
  } else {
    /* Fill |addr| array with 0xCCCCCCCC value. Because of wrapping, position
       processed by hasher never reaches 3GB + 64M; this makes all new chains
       to be terminated after the first node. */
    memset(addr, 0xCC, sizeof(uint32_t) * BUCKET_SIZE);
    memset(head, 0, sizeof(uint16_t) * BUCKET_SIZE);
  }
  memset(tiny_hash, 0, sizeof(uint8_t) * 65536);
  memset(self->free_slot_idx, 0, sizeof(self->free_slot_idx));
}

static BROTLI_INLINE void FN(HashMemAllocInBytes)(
    const BrotliEncoderParams* params, BROTLI_BOOL one_shot,
    size_t input_size, size_t* alloc_size) {
  BROTLI_UNUSED(params);
  BROTLI_UNUSED(one_shot);
  BROTLI_UNUSED(input_size);
  alloc_size[0] = sizeof(uint32_t) * BUCKET_SIZE +
                  sizeof(uint16_t) * BUCKET_SIZE + sizeof(uint8_t) * 65536;
  alloc_size[1] = sizeof(FN(Bank)) * NUM_BANKS;
}

/* Look at 4 bytes at &data[ix & mask]. Compute a hash from these, and prepend
   node to corresponding chain; also update tiny_hash for current position. */
static BROTLI_INLINE void FN(Store)(HashForgetfulChain* BROTLI_RESTRICT self,
    const uint8_t* BROTLI_RESTRICT data, const size_t mask, const size_t ix) {
  uint32_t* BROTLI_RESTRICT addr = FN(Addr)(self->extra[0]);
  uint16_t* BROTLI_RESTRICT head = FN(Head)(self->extra[0]);
  uint8_t* BROTLI_RESTRICT tiny_hash = FN(TinyHash)(self->extra[0]);
  FN(Bank)* BROTLI_RESTRICT banks = FN(Banks)(self->extra[1]);
  const size_t key = FN(HashBytes)(&data[ix & mask]);
  const size_t bank = key & (NUM_BANKS - 1);
  const size_t idx = self->free_slot_idx[bank]++ & (BANK_SIZE - 1);
  size_t delta = ix - addr[key];
  tiny_hash[(uint16_t)ix] = (uint8_t)key;
  if (delta > 0xFFFF) delta = CAPPED_CHAINS ? 0 : 0xFFFF;
  banks[bank].slots[idx].delta = (uint16_t)delta;
  banks[bank].slots[idx].next = head[key];
  addr[key] = (uint32_t)ix;
  head[key] = (uint16_t)idx;
}

static BROTLI_INLINE void FN(StoreRange)(
    HashForgetfulChain* BROTLI_RESTRICT self,
    const uint8_t* BROTLI_RESTRICT data, const size_t mask,
    const size_t ix_start, const size_t ix_end) {
  size_t i;
  for (i = ix_start; i < ix_end; ++i) {
    FN(Store)(self, data, mask, i);
  }
}

static BROTLI_INLINE void FN(StitchToPreviousBlock)(
    HashForgetfulChain* BROTLI_RESTRICT self,
    size_t num_bytes, size_t position, const uint8_t* ringbuffer,
    size_t ring_buffer_mask) {
  if (num_bytes >= FN(HashTypeLength)() - 1 && position >= 3) {
    /* Prepare the hashes for three last bytes of the last write.
       These could not be calculated before, since they require knowledge
       of both the previous and the current block. */
    FN(Store)(self, ringbuffer, ring_buffer_mask, position - 3);
    FN(Store)(self, ringbuffer, ring_buffer_mask, position - 2);
    FN(Store)(self, ringbuffer, ring_buffer_mask, position - 1);
  }
}

static BROTLI_INLINE void FN(PrepareDistanceCache)(
    HashForgetfulChain* BROTLI_RESTRICT self,
    int* BROTLI_RESTRICT distance_cache) {
  BROTLI_UNUSED(self);
  PrepareDistanceCache(distance_cache, NUM_LAST_DISTANCES_TO_CHECK);
}

/* Find a longest backward match of &data[cur_ix] up to the length of
   max_length and stores the position cur_ix in the hash table.

   REQUIRES: FN(PrepareDistanceCache) must be invoked for current distance cache
             values; if this method is invoked repeatedly with the same distance
             cache values, it is enough to invoke FN(PrepareDistanceCache) once.

   Does not look for matches longer than max_length.
   Does not look for matches further away than max_backward.
   Writes the best match into |out|.
   |out|->score is updated only if a better match is found. */
static BROTLI_INLINE void FN(FindLongestMatch)(
    HashForgetfulChain* BROTLI_RESTRICT self,
    const BrotliEncoderDictionary* dictionary,
    const uint8_t* BROTLI_RESTRICT data, const size_t ring_buffer_mask,
    const int* BROTLI_RESTRICT distance_cache,
    const size_t cur_ix, const size_t max_length, const size_t max_backward,
    const size_t dictionary_distance, const size_t max_distance,
    HasherSearchResult* BROTLI_RESTRICT out) {
  uint32_t* BROTLI_RESTRICT addr = FN(Addr)(self->extra[0]);
  uint16_t* BROTLI_RESTRICT head = FN(Head)(self->extra[0]);
  uint8_t* BROTLI_RESTRICT tiny_hashes = FN(TinyHash)(self->extra[0]);
  FN(Bank)* BROTLI_RESTRICT banks = FN(Banks)(self->extra[1]);
  const size_t cur_ix_masked = cur_ix & ring_buffer_mask;
  /* Don't accept a short copy from far away. */
  score_t min_score = out->score;
  score_t best_score = out->score;
  size_t best_len = out->len;
  size_t i;
  const size_t key = FN(HashBytes)(&data[cur_ix_masked]);
  const uint8_t tiny_hash = (uint8_t)(key);
  out->len = 0;
  out->len_code_delta = 0;
  /* Try last distance first. */
  for (i = 0; i < NUM_LAST_DISTANCES_TO_CHECK; ++i) {
    const size_t backward = (size_t)distance_cache[i];
    size_t prev_ix = (cur_ix - backward);
    /* For distance code 0 we want to consider 2-byte matches. */
    if (i > 0 && tiny_hashes[(uint16_t)prev_ix] != tiny_hash) continue;
    if (prev_ix >= cur_ix || backward > max_backward) {
      continue;
    }
    prev_ix &= ring_buffer_mask;
    {
      const size_t len = FindMatchLengthWithLimit(&data[prev_ix],
                                                  &data[cur_ix_masked],
                                                  max_length);
      if (len >= 2) {
        score_t score = BackwardReferenceScoreUsingLastDistance(len);
        if (best_score < score) {
          if (i != 0) score -= BackwardReferencePenaltyUsingLastDistance(i);
          if (best_score < score) {
            best_score = score;
            best_len = len;
            out->len = best_len;
            out->distance = backward;
            out->score = best_score;
          }
        }
      }
    }
  }
  {
    const size_t bank = key & (NUM_BANKS - 1);
    size_t backward = 0;
    size_t hops = self->max_hops;
    size_t delta = cur_ix - addr[key];
    size_t slot = head[key];
    while (hops--) {
      size_t prev_ix;
      size_t last = slot;
      backward += delta;
      if (backward > max_backward || (CAPPED_CHAINS && !delta)) break;
      prev_ix = (cur_ix - backward) & ring_buffer_mask;
      slot = banks[bank].slots[last].next;
      delta = banks[bank].slots[last].delta;
      if (cur_ix_masked + best_len > ring_buffer_mask ||
          prev_ix + best_len > ring_buffer_mask ||
          data[cur_ix_masked + best_len] != data[prev_ix + best_len]) {
        continue;
      }
      {
        const size_t len = FindMatchLengthWithLimit(&data[prev_ix],
                                                    &data[cur_ix_masked],
                                                    max_length);
        if (len >= 4) {
          /* Comparing for >= 3 does not change the semantics, but just saves
             for a few unnecessary binary logarithms in backward reference
             score, since we are not interested in such short matches. */
          score_t score = BackwardReferenceScore(len, backward);
          if (best_score < score) {
            best_score = score;
            best_len = len;
            out->len = best_len;
            out->distance = backward;
            out->score = best_score;
          }
        }
      }
    }
    FN(Store)(self, data, ring_buffer_mask, cur_ix);
  }
  if (out->score == min_score) {
    SearchInStaticDictionary(dictionary,
        self->common, &data[cur_ix_masked], max_length, dictionary_distance,
        max_distance, out, BROTLI_FALSE);
  }
}

#undef BANK_SIZE
#undef BUCKET_SIZE
#undef CAPPED_CHAINS

#undef HashForgetfulChain
