/* NOLINT(build/header_guard) */
/* Copyright 2018 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* template parameters: FN, HASHER_A, HASHER_B */

/* Composite hasher: This hasher allows to combine two other hashers, HASHER_A
   and HASHER_B. */

#define HashComposite HASHER()

#define FN_A(X) EXPAND_CAT(X, HASHER_A)
#define FN_B(X) EXPAND_CAT(X, HASHER_B)

static BROTLI_INLINE size_t FN(HashTypeLength)(void) {
  size_t a =  FN_A(HashTypeLength)();
  size_t b =  FN_B(HashTypeLength)();
  return a > b ? a : b;
}

static BROTLI_INLINE size_t FN(StoreLookahead)(void) {
  size_t a =  FN_A(StoreLookahead)();
  size_t b =  FN_B(StoreLookahead)();
  return a > b ? a : b;
}

typedef struct HashComposite {
  HASHER_A ha;
  HASHER_B hb;
  HasherCommon ha_common;
  HasherCommon hb_common;

  /* Shortcuts. */
  HasherCommon* common;

  BROTLI_BOOL fresh;
  const BrotliEncoderParams* params;
} HashComposite;

static void FN(Initialize)(HasherCommon* common,
    HashComposite* BROTLI_RESTRICT self, const BrotliEncoderParams* params) {
  self->common = common;

  self->ha_common = *self->common;
  self->hb_common = *self->common;
  self->fresh = BROTLI_TRUE;
  self->params = params;
  /* TODO(lode): Initialize of the hashers is deferred to Prepare (and params
     remembered here) because we don't get the one_shot and input_size params
     here that are needed to know the memory size of them. Instead provide
     those params to all hashers FN(Initialize) */
}

static void FN(Prepare)(
    HashComposite* BROTLI_RESTRICT self, BROTLI_BOOL one_shot,
    size_t input_size, const uint8_t* BROTLI_RESTRICT data) {
  if (self->fresh) {
    self->fresh = BROTLI_FALSE;
    self->ha_common.extra[0] = self->common->extra[0];
    self->ha_common.extra[1] = self->common->extra[1];
    self->ha_common.extra[2] = NULL;
    self->ha_common.extra[3] = NULL;
    self->hb_common.extra[0] = self->common->extra[2];
    self->hb_common.extra[1] = self->common->extra[3];
    self->hb_common.extra[2] = NULL;
    self->hb_common.extra[3] = NULL;

    FN_A(Initialize)(&self->ha_common, &self->ha, self->params);
    FN_B(Initialize)(&self->hb_common, &self->hb, self->params);
  }
  FN_A(Prepare)(&self->ha, one_shot, input_size, data);
  FN_B(Prepare)(&self->hb, one_shot, input_size, data);
}

static BROTLI_INLINE void FN(HashMemAllocInBytes)(
    const BrotliEncoderParams* params, BROTLI_BOOL one_shot,
    size_t input_size, size_t* alloc_size) {
  size_t alloc_size_a[4] = {0};
  size_t alloc_size_b[4] = {0};
  FN_A(HashMemAllocInBytes)(params, one_shot, input_size, alloc_size_a);
  FN_B(HashMemAllocInBytes)(params, one_shot, input_size, alloc_size_b);
  /* Should never happen. */
  if (alloc_size_a[2] != 0 || alloc_size_a[3] != 0) exit(EXIT_FAILURE);
  if (alloc_size_b[2] != 0 || alloc_size_b[3] != 0) exit(EXIT_FAILURE);
  alloc_size[0] = alloc_size_a[0];
  alloc_size[1] = alloc_size_a[1];
  alloc_size[2] = alloc_size_b[0];
  alloc_size[3] = alloc_size_b[1];
}

static BROTLI_INLINE void FN(Store)(HashComposite* BROTLI_RESTRICT self,
    const uint8_t* BROTLI_RESTRICT data, const size_t mask, const size_t ix) {
  FN_A(Store)(&self->ha, data, mask, ix);
  FN_B(Store)(&self->hb, data, mask, ix);
}

static BROTLI_INLINE void FN(StoreRange)(
    HashComposite* BROTLI_RESTRICT self, const uint8_t* BROTLI_RESTRICT data,
    const size_t mask, const size_t ix_start,
    const size_t ix_end) {
  FN_A(StoreRange)(&self->ha, data, mask, ix_start, ix_end);
  FN_B(StoreRange)(&self->hb, data, mask, ix_start, ix_end);
}

static BROTLI_INLINE void FN(StitchToPreviousBlock)(
    HashComposite* BROTLI_RESTRICT self,
    size_t num_bytes, size_t position, const uint8_t* ringbuffer,
    size_t ring_buffer_mask) {
  FN_A(StitchToPreviousBlock)(&self->ha, num_bytes, position,
      ringbuffer, ring_buffer_mask);
  FN_B(StitchToPreviousBlock)(&self->hb, num_bytes, position,
      ringbuffer, ring_buffer_mask);
}

static BROTLI_INLINE void FN(PrepareDistanceCache)(
    HashComposite* BROTLI_RESTRICT self, int* BROTLI_RESTRICT distance_cache) {
  FN_A(PrepareDistanceCache)(&self->ha, distance_cache);
  FN_B(PrepareDistanceCache)(&self->hb, distance_cache);
}

static BROTLI_INLINE void FN(FindLongestMatch)(
    HashComposite* BROTLI_RESTRICT self,
    const BrotliEncoderDictionary* dictionary,
    const uint8_t* BROTLI_RESTRICT data, const size_t ring_buffer_mask,
    const int* BROTLI_RESTRICT distance_cache, const size_t cur_ix,
    const size_t max_length, const size_t max_backward,
    const size_t dictionary_distance, const size_t max_distance,
    HasherSearchResult* BROTLI_RESTRICT out) {
  FN_A(FindLongestMatch)(&self->ha, dictionary, data, ring_buffer_mask,
      distance_cache, cur_ix, max_length, max_backward, dictionary_distance,
      max_distance, out);
  FN_B(FindLongestMatch)(&self->hb, dictionary, data, ring_buffer_mask,
      distance_cache, cur_ix, max_length, max_backward, dictionary_distance,
      max_distance, out);
}

#undef HashComposite
