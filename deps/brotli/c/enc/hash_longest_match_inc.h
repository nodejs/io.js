/* NOLINT(build/header_guard) */
/* Copyright 2010 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* template parameters: FN */

/* A (forgetful) hash table to the data seen by the compressor, to
   help create backward references to previous data.

   This is a hash map of fixed size (bucket_size_) to a ring buffer of
   fixed size (block_size_). The ring buffer contains the last block_size_
   index positions of the given hash key in the compressed data. */

#define HashLongestMatch HASHER()

static BROTLI_INLINE size_t FN(HashTypeLength)(void) { return 4; }
static BROTLI_INLINE size_t FN(StoreLookahead)(void) { return 4; }

/* HashBytes is the function that chooses the bucket to place the address in. */
static uint32_t FN(HashBytes)(
    const uint8_t* BROTLI_RESTRICT data, const int shift) {
  uint32_t h = BROTLI_UNALIGNED_LOAD32LE(data) * kHashMul32;
  /* The higher bits contain more mixture from the multiplication,
     so we take our results from there. */
  return (uint32_t)(h >> shift);
}

typedef struct HashLongestMatch {
  /* Number of hash buckets. */
  size_t bucket_size_;
  /* Only block_size_ newest backward references are kept,
     and the older are forgotten. */
  size_t block_size_;
  /* Left-shift for computing hash bucket index from hash value. */
  int hash_shift_;
  /* Mask for accessing entries in a block (in a ring-buffer manner). */
  uint32_t block_mask_;

  int block_bits_;
  int num_last_distances_to_check_;

  /* Shortcuts. */
  HasherCommon* common_;

  /* --- Dynamic size members --- */

  /* Number of entries in a particular bucket. */
  uint16_t* num_;  /* uint16_t[bucket_size]; */

  /* Buckets containing block_size_ of backward references. */
  uint32_t* buckets_;  /* uint32_t[bucket_size * block_size]; */
} HashLongestMatch;

static void FN(Initialize)(
    HasherCommon* common, HashLongestMatch* BROTLI_RESTRICT self,
    const BrotliEncoderParams* params) {
  self->common_ = common;

  BROTLI_UNUSED(params);
  self->hash_shift_ = 32 - common->params.bucket_bits;
  self->bucket_size_ = (size_t)1 << common->params.bucket_bits;
  self->block_size_ = (size_t)1 << common->params.block_bits;
  self->block_mask_ = (uint32_t)(self->block_size_ - 1);
  self->num_ = (uint16_t*)common->extra[0];
  self->buckets_ = (uint32_t*)common->extra[1];
  self->block_bits_ = common->params.block_bits;
  self->num_last_distances_to_check_ =
      common->params.num_last_distances_to_check;
}

static void FN(Prepare)(
    HashLongestMatch* BROTLI_RESTRICT self, BROTLI_BOOL one_shot,
    size_t input_size, const uint8_t* BROTLI_RESTRICT data) {
  uint16_t* BROTLI_RESTRICT num = self->num_;
  /* Partial preparation is 100 times slower (per socket). */
  size_t partial_prepare_threshold = self->bucket_size_ >> 6;
  if (one_shot && input_size <= partial_prepare_threshold) {
    size_t i;
    for (i = 0; i < input_size; ++i) {
      const uint32_t key = FN(HashBytes)(&data[i], self->hash_shift_);
      num[key] = 0;
    }
  } else {
    memset(num, 0, self->bucket_size_ * sizeof(num[0]));
  }
}

static BROTLI_INLINE void FN(HashMemAllocInBytes)(
    const BrotliEncoderParams* params, BROTLI_BOOL one_shot,
    size_t input_size, size_t* alloc_size) {
  size_t bucket_size = (size_t)1 << params->hasher.bucket_bits;
  size_t block_size = (size_t)1 << params->hasher.block_bits;
  BROTLI_UNUSED(one_shot);
  BROTLI_UNUSED(input_size);
  alloc_size[0] = sizeof(uint16_t) * bucket_size;
  alloc_size[1] = sizeof(uint32_t) * bucket_size * block_size;
}

/* Look at 4 bytes at &data[ix & mask].
   Compute a hash from these, and store the value of ix at that position. */
static BROTLI_INLINE void FN(Store)(
    HashLongestMatch* BROTLI_RESTRICT self, const uint8_t* BROTLI_RESTRICT data,
    const size_t mask, const size_t ix) {
  const uint32_t key = FN(HashBytes)(&data[ix & mask], self->hash_shift_);
  const size_t minor_ix = self->num_[key] & self->block_mask_;
  const size_t offset = minor_ix + (key << self->block_bits_);
  self->buckets_[offset] = (uint32_t)ix;
  ++self->num_[key];
}

static BROTLI_INLINE void FN(StoreRange)(HashLongestMatch* BROTLI_RESTRICT self,
    const uint8_t* BROTLI_RESTRICT data, const size_t mask,
    const size_t ix_start, const size_t ix_end) {
  size_t i;
  for (i = ix_start; i < ix_end; ++i) {
    FN(Store)(self, data, mask, i);
  }
}

static BROTLI_INLINE void FN(StitchToPreviousBlock)(
    HashLongestMatch* BROTLI_RESTRICT self,
    size_t num_bytes, size_t position, const uint8_t* ringbuffer,
    size_t ringbuffer_mask) {
  if (num_bytes >= FN(HashTypeLength)() - 1 && position >= 3) {
    /* Prepare the hashes for three last bytes of the last write.
       These could not be calculated before, since they require knowledge
       of both the previous and the current block. */
    FN(Store)(self, ringbuffer, ringbuffer_mask, position - 3);
    FN(Store)(self, ringbuffer, ringbuffer_mask, position - 2);
    FN(Store)(self, ringbuffer, ringbuffer_mask, position - 1);
  }
}

static BROTLI_INLINE void FN(PrepareDistanceCache)(
    HashLongestMatch* BROTLI_RESTRICT self,
    int* BROTLI_RESTRICT distance_cache) {
  PrepareDistanceCache(distance_cache, self->num_last_distances_to_check_);
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
    HashLongestMatch* BROTLI_RESTRICT self,
    const BrotliEncoderDictionary* dictionary,
    const uint8_t* BROTLI_RESTRICT data, const size_t ring_buffer_mask,
    const int* BROTLI_RESTRICT distance_cache, const size_t cur_ix,
    const size_t max_length, const size_t max_backward,
    const size_t dictionary_distance, const size_t max_distance,
    HasherSearchResult* BROTLI_RESTRICT out) {
  uint16_t* BROTLI_RESTRICT num = self->num_;
  uint32_t* BROTLI_RESTRICT buckets = self->buckets_;
  const size_t cur_ix_masked = cur_ix & ring_buffer_mask;
  /* Don't accept a short copy from far away. */
  score_t min_score = out->score;
  score_t best_score = out->score;
  size_t best_len = out->len;
  size_t i;
  out->len = 0;
  out->len_code_delta = 0;
  /* Try last distance first. */
  for (i = 0; i < (size_t)self->num_last_distances_to_check_; ++i) {
    const size_t backward = (size_t)distance_cache[i];
    size_t prev_ix = (size_t)(cur_ix - backward);
    if (prev_ix >= cur_ix) {
      continue;
    }
    if (BROTLI_PREDICT_FALSE(backward > max_backward)) {
      continue;
    }
    prev_ix &= ring_buffer_mask;

    if (cur_ix_masked + best_len > ring_buffer_mask ||
        prev_ix + best_len > ring_buffer_mask ||
        data[cur_ix_masked + best_len] != data[prev_ix + best_len]) {
      continue;
    }
    {
      const size_t len = FindMatchLengthWithLimit(&data[prev_ix],
                                                  &data[cur_ix_masked],
                                                  max_length);
      if (len >= 3 || (len == 2 && i < 2)) {
        /* Comparing for >= 2 does not change the semantics, but just saves for
           a few unnecessary binary logarithms in backward reference score,
           since we are not interested in such short matches. */
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
    const uint32_t key =
        FN(HashBytes)(&data[cur_ix_masked], self->hash_shift_);
    uint32_t* BROTLI_RESTRICT bucket = &buckets[key << self->block_bits_];
    const size_t down =
        (num[key] > self->block_size_) ? (num[key] - self->block_size_) : 0u;
    for (i = num[key]; i > down;) {
      size_t prev_ix = bucket[--i & self->block_mask_];
      const size_t backward = cur_ix - prev_ix;
      if (BROTLI_PREDICT_FALSE(backward > max_backward)) {
        break;
      }
      prev_ix &= ring_buffer_mask;
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
    bucket[num[key] & self->block_mask_] = (uint32_t)cur_ix;
    ++num[key];
  }
  if (min_score == out->score) {
    SearchInStaticDictionary(dictionary,
        self->common_, &data[cur_ix_masked], max_length, dictionary_distance,
        max_distance, out, BROTLI_FALSE);
  }
}

#undef HashLongestMatch
