/* NOLINT(build/header_guard) */
/* Copyright 2018 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* template parameters: FN, JUMP, NUMBUCKETS, MASK, CHUNKLEN */
/* NUMBUCKETS / (MASK + 1) = probability of storing and using hash code. */
/* JUMP = skip bytes for speedup */

/* Rolling hash for long distance long string matches. Stores one position
   per bucket, bucket key is computed over a long region. */

#define HashRolling HASHER()

static const uint32_t FN(kRollingHashMul32) = 69069;
static const uint32_t FN(kInvalidPos) = 0xffffffff;

/* This hasher uses a longer forward length, but returning a higher value here
   will hurt compression by the main hasher when combined with a composite
   hasher. The hasher tests for forward itself instead. */
static BROTLI_INLINE size_t FN(HashTypeLength)(void) { return 4; }
static BROTLI_INLINE size_t FN(StoreLookahead)(void) { return 4; }

/* Computes a code from a single byte. A lookup table of 256 values could be
   used, but simply adding 1 works about as good. */
static uint32_t FN(HashByte)(uint8_t byte) {
  return (uint32_t)byte + 1u;
}

static uint32_t FN(HashRollingFunctionInitial)(uint32_t state, uint8_t add,
                                               uint32_t factor) {
  return (uint32_t)(factor * state + FN(HashByte)(add));
}

static uint32_t FN(HashRollingFunction)(uint32_t state, uint8_t add,
                                        uint8_t rem, uint32_t factor,
                                        uint32_t factor_remove) {
  return (uint32_t)(factor * state +
      FN(HashByte)(add) - factor_remove * FN(HashByte)(rem));
}

typedef struct HashRolling {
  uint32_t state;
  uint32_t* table;
  size_t next_ix;

  uint32_t chunk_len;
  uint32_t factor;
  uint32_t factor_remove;
} HashRolling;

static void FN(Initialize)(
    HasherCommon* common, HashRolling* BROTLI_RESTRICT self,
    const BrotliEncoderParams* params) {
  size_t i;
  self->state = 0;
  self->next_ix = 0;

  self->factor = FN(kRollingHashMul32);

  /* Compute the factor of the oldest byte to remove: factor**steps modulo
     0xffffffff (the multiplications rely on 32-bit overflow) */
  self->factor_remove = 1;
  for (i = 0; i < CHUNKLEN; i += JUMP) {
    self->factor_remove *= self->factor;
  }

  self->table = (uint32_t*)common->extra[0];
  for (i = 0; i < NUMBUCKETS; i++) {
    self->table[i] = FN(kInvalidPos);
  }

  BROTLI_UNUSED(params);
}

static void FN(Prepare)(HashRolling* BROTLI_RESTRICT self, BROTLI_BOOL one_shot,
    size_t input_size, const uint8_t* BROTLI_RESTRICT data) {
  size_t i;
  /* Too small size, cannot use this hasher. */
  if (input_size < CHUNKLEN) return;
  self->state = 0;
  for (i = 0; i < CHUNKLEN; i += JUMP) {
    self->state = FN(HashRollingFunctionInitial)(
        self->state, data[i], self->factor);
  }
  BROTLI_UNUSED(one_shot);
}

static BROTLI_INLINE void FN(HashMemAllocInBytes)(
    const BrotliEncoderParams* params, BROTLI_BOOL one_shot,
    size_t input_size, size_t* alloc_size) {
  BROTLI_UNUSED(params);
  BROTLI_UNUSED(one_shot);
  BROTLI_UNUSED(input_size);
  alloc_size[0] = NUMBUCKETS * sizeof(uint32_t);
}

static BROTLI_INLINE void FN(Store)(HashRolling* BROTLI_RESTRICT self,
    const uint8_t* BROTLI_RESTRICT data, const size_t mask, const size_t ix) {
  BROTLI_UNUSED(self);
  BROTLI_UNUSED(data);
  BROTLI_UNUSED(mask);
  BROTLI_UNUSED(ix);
}

static BROTLI_INLINE void FN(StoreRange)(HashRolling* BROTLI_RESTRICT self,
    const uint8_t* BROTLI_RESTRICT data, const size_t mask,
    const size_t ix_start, const size_t ix_end) {
  BROTLI_UNUSED(self);
  BROTLI_UNUSED(data);
  BROTLI_UNUSED(mask);
  BROTLI_UNUSED(ix_start);
  BROTLI_UNUSED(ix_end);
}

static BROTLI_INLINE void FN(StitchToPreviousBlock)(
    HashRolling* BROTLI_RESTRICT self,
    size_t num_bytes, size_t position, const uint8_t* ringbuffer,
    size_t ring_buffer_mask) {
  /* In this case we must re-initialize the hasher from scratch from the
     current position. */
  size_t position_masked;
  size_t available = num_bytes;
  if ((position & (JUMP - 1)) != 0) {
    size_t diff = JUMP - (position & (JUMP - 1));
    available = (diff > available) ? 0 : (available - diff);
    position += diff;
  }
  position_masked = position & ring_buffer_mask;
  /* wrapping around ringbuffer not handled. */
  if (available > ring_buffer_mask - position_masked) {
    available = ring_buffer_mask - position_masked;
  }

  FN(Prepare)(self, BROTLI_FALSE, available,
      ringbuffer + (position & ring_buffer_mask));
  self->next_ix = position;
  BROTLI_UNUSED(num_bytes);
}

static BROTLI_INLINE void FN(PrepareDistanceCache)(
    HashRolling* BROTLI_RESTRICT self,
    int* BROTLI_RESTRICT distance_cache) {
  BROTLI_UNUSED(self);
  BROTLI_UNUSED(distance_cache);
}

static BROTLI_INLINE void FN(FindLongestMatch)(
    HashRolling* BROTLI_RESTRICT self,
    const BrotliEncoderDictionary* dictionary,
    const uint8_t* BROTLI_RESTRICT data, const size_t ring_buffer_mask,
    const int* BROTLI_RESTRICT distance_cache, const size_t cur_ix,
    const size_t max_length, const size_t max_backward,
    const size_t dictionary_distance, const size_t max_distance,
    HasherSearchResult* BROTLI_RESTRICT out) {
  const size_t cur_ix_masked = cur_ix & ring_buffer_mask;
  size_t pos;

  if ((cur_ix & (JUMP - 1)) != 0) return;

  /* Not enough lookahead */
  if (max_length < CHUNKLEN) return;

  for (pos = self->next_ix; pos <= cur_ix; pos += JUMP) {
    uint32_t code = self->state & MASK;

    uint8_t rem = data[pos & ring_buffer_mask];
    uint8_t add = data[(pos + CHUNKLEN) & ring_buffer_mask];
    size_t found_ix = FN(kInvalidPos);

    self->state = FN(HashRollingFunction)(
        self->state, add, rem, self->factor, self->factor_remove);

    if (code < NUMBUCKETS) {
      found_ix = self->table[code];
      self->table[code] = (uint32_t)pos;
      if (pos == cur_ix && found_ix != FN(kInvalidPos)) {
        /* The cast to 32-bit makes backward distances up to 4GB work even
           if cur_ix is above 4GB, despite using 32-bit values in the table. */
        size_t backward = (uint32_t)(cur_ix - found_ix);
        if (backward <= max_backward) {
          const size_t found_ix_masked = found_ix & ring_buffer_mask;
          const size_t len = FindMatchLengthWithLimit(&data[found_ix_masked],
                                                      &data[cur_ix_masked],
                                                      max_length);
          if (len >= 4 && len > out->len) {
            score_t score = BackwardReferenceScore(len, backward);
            if (score > out->score) {
              out->len = len;
              out->distance = backward;
              out->score = score;
              out->len_code_delta = 0;
            }
          }
        }
      }
    }
  }

  self->next_ix = cur_ix + JUMP;

  /* NOTE: this hasher does not search in the dictionary. It is used as
     backup-hasher, the main hasher already searches in it. */
  BROTLI_UNUSED(dictionary);
  BROTLI_UNUSED(distance_cache);
  BROTLI_UNUSED(dictionary_distance);
  BROTLI_UNUSED(max_distance);
}

#undef HashRolling
