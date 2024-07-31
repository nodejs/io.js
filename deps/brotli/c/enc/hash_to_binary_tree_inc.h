/* NOLINT(build/header_guard) */
/* Copyright 2016 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* template parameters: FN, BUCKET_BITS, MAX_TREE_COMP_LENGTH,
                        MAX_TREE_SEARCH_DEPTH */

/* A (forgetful) hash table where each hash bucket contains a binary tree of
   sequences whose first 4 bytes share the same hash code.
   Each sequence is MAX_TREE_COMP_LENGTH long and is identified by its starting
   position in the input data. The binary tree is sorted by the lexicographic
   order of the sequences, and it is also a max-heap with respect to the
   starting positions. */

#define HashToBinaryTree HASHER()

#define BUCKET_SIZE (1 << BUCKET_BITS)

static BROTLI_INLINE size_t FN(HashTypeLength)(void) { return 4; }
static BROTLI_INLINE size_t FN(StoreLookahead)(void) {
  return MAX_TREE_COMP_LENGTH;
}

static uint32_t FN(HashBytes)(const uint8_t* BROTLI_RESTRICT data) {
  uint32_t h = BROTLI_UNALIGNED_LOAD32LE(data) * kHashMul32;
  /* The higher bits contain more mixture from the multiplication,
     so we take our results from there. */
  return h >> (32 - BUCKET_BITS);
}

typedef struct HashToBinaryTree {
  /* The window size minus 1 */
  size_t window_mask_;

  /* Hash table that maps the 4-byte hashes of the sequence to the last
     position where this hash was found, which is the root of the binary
     tree of sequences that share this hash bucket. */
  uint32_t* buckets_;  /* uint32_t[BUCKET_SIZE]; */

  /* A position used to mark a non-existent sequence, i.e. a tree is empty if
     its root is at invalid_pos_ and a node is a leaf if both its children
     are at invalid_pos_. */
  uint32_t invalid_pos_;

  /* --- Dynamic size members --- */

  /* The union of the binary trees of each hash bucket. The root of the tree
     corresponding to a hash is a sequence starting at buckets_[hash] and
     the left and right children of a sequence starting at pos are
     forest_[2 * pos] and forest_[2 * pos + 1]. */
  uint32_t* forest_;  /* uint32_t[2 * num_nodes] */
} HashToBinaryTree;

static void FN(Initialize)(
    HasherCommon* common, HashToBinaryTree* BROTLI_RESTRICT self,
    const BrotliEncoderParams* params) {
  self->buckets_ = (uint32_t*)common->extra[0];
  self->forest_ = (uint32_t*)common->extra[1];

  self->window_mask_ = (1u << params->lgwin) - 1u;
  self->invalid_pos_ = (uint32_t)(0 - self->window_mask_);
}

static void FN(Prepare)
    (HashToBinaryTree* BROTLI_RESTRICT self, BROTLI_BOOL one_shot,
    size_t input_size, const uint8_t* BROTLI_RESTRICT data) {
  uint32_t invalid_pos = self->invalid_pos_;
  uint32_t i;
  uint32_t* BROTLI_RESTRICT buckets = self->buckets_;
  BROTLI_UNUSED(data);
  BROTLI_UNUSED(one_shot);
  BROTLI_UNUSED(input_size);
  for (i = 0; i < BUCKET_SIZE; i++) {
    buckets[i] = invalid_pos;
  }
}

static BROTLI_INLINE void FN(HashMemAllocInBytes)(
    const BrotliEncoderParams* params, BROTLI_BOOL one_shot,
    size_t input_size, size_t* alloc_size) {
  size_t num_nodes = (size_t)1 << params->lgwin;
  if (one_shot && input_size < num_nodes) {
    num_nodes = input_size;
  }
  alloc_size[0] = sizeof(uint32_t) * BUCKET_SIZE;
  alloc_size[1] = 2 * sizeof(uint32_t) * num_nodes;
}

static BROTLI_INLINE size_t FN(LeftChildIndex)(
    HashToBinaryTree* BROTLI_RESTRICT self,
    const size_t pos) {
  return 2 * (pos & self->window_mask_);
}

static BROTLI_INLINE size_t FN(RightChildIndex)(
    HashToBinaryTree* BROTLI_RESTRICT self,
    const size_t pos) {
  return 2 * (pos & self->window_mask_) + 1;
}

/* Stores the hash of the next 4 bytes and in a single tree-traversal, the
   hash bucket's binary tree is searched for matches and is re-rooted at the
   current position.

   If less than MAX_TREE_COMP_LENGTH data is available, the hash bucket of the
   current position is searched for matches, but the state of the hash table
   is not changed, since we can not know the final sorting order of the
   current (incomplete) sequence.

   This function must be called with increasing cur_ix positions. */
static BROTLI_INLINE BackwardMatch* FN(StoreAndFindMatches)(
    HashToBinaryTree* BROTLI_RESTRICT self, const uint8_t* BROTLI_RESTRICT data,
    const size_t cur_ix, const size_t ring_buffer_mask, const size_t max_length,
    const size_t max_backward, size_t* const BROTLI_RESTRICT best_len,
    BackwardMatch* BROTLI_RESTRICT matches) {
  const size_t cur_ix_masked = cur_ix & ring_buffer_mask;
  const size_t max_comp_len =
      BROTLI_MIN(size_t, max_length, MAX_TREE_COMP_LENGTH);
  const BROTLI_BOOL should_reroot_tree =
      TO_BROTLI_BOOL(max_length >= MAX_TREE_COMP_LENGTH);
  const uint32_t key = FN(HashBytes)(&data[cur_ix_masked]);
  uint32_t* BROTLI_RESTRICT buckets = self->buckets_;
  uint32_t* BROTLI_RESTRICT forest = self->forest_;
  size_t prev_ix = buckets[key];
  /* The forest index of the rightmost node of the left subtree of the new
     root, updated as we traverse and re-root the tree of the hash bucket. */
  size_t node_left = FN(LeftChildIndex)(self, cur_ix);
  /* The forest index of the leftmost node of the right subtree of the new
     root, updated as we traverse and re-root the tree of the hash bucket. */
  size_t node_right = FN(RightChildIndex)(self, cur_ix);
  /* The match length of the rightmost node of the left subtree of the new
     root, updated as we traverse and re-root the tree of the hash bucket. */
  size_t best_len_left = 0;
  /* The match length of the leftmost node of the right subtree of the new
     root, updated as we traverse and re-root the tree of the hash bucket. */
  size_t best_len_right = 0;
  size_t depth_remaining;
  if (should_reroot_tree) {
    buckets[key] = (uint32_t)cur_ix;
  }
  for (depth_remaining = MAX_TREE_SEARCH_DEPTH; ; --depth_remaining) {
    const size_t backward = cur_ix - prev_ix;
    const size_t prev_ix_masked = prev_ix & ring_buffer_mask;
    if (backward == 0 || backward > max_backward || depth_remaining == 0) {
      if (should_reroot_tree) {
        forest[node_left] = self->invalid_pos_;
        forest[node_right] = self->invalid_pos_;
      }
      break;
    }
    {
      const size_t cur_len = BROTLI_MIN(size_t, best_len_left, best_len_right);
      size_t len;
      BROTLI_DCHECK(cur_len <= MAX_TREE_COMP_LENGTH);
      len = cur_len +
          FindMatchLengthWithLimit(&data[cur_ix_masked + cur_len],
                                   &data[prev_ix_masked + cur_len],
                                   max_length - cur_len);
      BROTLI_DCHECK(
          0 == memcmp(&data[cur_ix_masked], &data[prev_ix_masked], len));
      if (matches && len > *best_len) {
        *best_len = len;
        InitBackwardMatch(matches++, backward, len);
      }
      if (len >= max_comp_len) {
        if (should_reroot_tree) {
          forest[node_left] = forest[FN(LeftChildIndex)(self, prev_ix)];
          forest[node_right] = forest[FN(RightChildIndex)(self, prev_ix)];
        }
        break;
      }
      if (data[cur_ix_masked + len] > data[prev_ix_masked + len]) {
        best_len_left = len;
        if (should_reroot_tree) {
          forest[node_left] = (uint32_t)prev_ix;
        }
        node_left = FN(RightChildIndex)(self, prev_ix);
        prev_ix = forest[node_left];
      } else {
        best_len_right = len;
        if (should_reroot_tree) {
          forest[node_right] = (uint32_t)prev_ix;
        }
        node_right = FN(LeftChildIndex)(self, prev_ix);
        prev_ix = forest[node_right];
      }
    }
  }
  return matches;
}

/* Finds all backward matches of &data[cur_ix & ring_buffer_mask] up to the
   length of max_length and stores the position cur_ix in the hash table.

   Sets *num_matches to the number of matches found, and stores the found
   matches in matches[0] to matches[*num_matches - 1]. The matches will be
   sorted by strictly increasing length and (non-strictly) increasing
   distance. */
static BROTLI_INLINE size_t FN(FindAllMatches)(
    HashToBinaryTree* BROTLI_RESTRICT self,
    const BrotliEncoderDictionary* dictionary,
    const uint8_t* BROTLI_RESTRICT data,
    const size_t ring_buffer_mask, const size_t cur_ix,
    const size_t max_length, const size_t max_backward,
    const size_t dictionary_distance, const BrotliEncoderParams* params,
    BackwardMatch* matches) {
  BackwardMatch* const orig_matches = matches;
  const size_t cur_ix_masked = cur_ix & ring_buffer_mask;
  size_t best_len = 1;
  const size_t short_match_max_backward =
      params->quality != HQ_ZOPFLIFICATION_QUALITY ? 16 : 64;
  size_t stop = cur_ix - short_match_max_backward;
  uint32_t dict_matches[BROTLI_MAX_STATIC_DICTIONARY_MATCH_LEN + 1];
  size_t i;
  if (cur_ix < short_match_max_backward) { stop = 0; }
  for (i = cur_ix - 1; i > stop && best_len <= 2; --i) {
    size_t prev_ix = i;
    const size_t backward = cur_ix - prev_ix;
    if (BROTLI_PREDICT_FALSE(backward > max_backward)) {
      break;
    }
    prev_ix &= ring_buffer_mask;
    if (data[cur_ix_masked] != data[prev_ix] ||
        data[cur_ix_masked + 1] != data[prev_ix + 1]) {
      continue;
    }
    {
      const size_t len =
          FindMatchLengthWithLimit(&data[prev_ix], &data[cur_ix_masked],
                                   max_length);
      if (len > best_len) {
        best_len = len;
        InitBackwardMatch(matches++, backward, len);
      }
    }
  }
  if (best_len < max_length) {
    matches = FN(StoreAndFindMatches)(self, data, cur_ix,
        ring_buffer_mask, max_length, max_backward, &best_len, matches);
  }
  for (i = 0; i <= BROTLI_MAX_STATIC_DICTIONARY_MATCH_LEN; ++i) {
    dict_matches[i] = kInvalidMatch;
  }
  {
    size_t minlen = BROTLI_MAX(size_t, 4, best_len + 1);
    if (BrotliFindAllStaticDictionaryMatches(dictionary,
        &data[cur_ix_masked], minlen, max_length, &dict_matches[0])) {
      size_t maxlen = BROTLI_MIN(
          size_t, BROTLI_MAX_STATIC_DICTIONARY_MATCH_LEN, max_length);
      size_t l;
      for (l = minlen; l <= maxlen; ++l) {
        uint32_t dict_id = dict_matches[l];
        if (dict_id < kInvalidMatch) {
          size_t distance = dictionary_distance + (dict_id >> 5) + 1;
          if (distance <= params->dist.max_distance) {
            InitDictionaryBackwardMatch(matches++, distance, l, dict_id & 31);
          }
        }
      }
    }
  }
  return (size_t)(matches - orig_matches);
}

/* Stores the hash of the next 4 bytes and re-roots the binary tree at the
   current sequence, without returning any matches.
   REQUIRES: ix + MAX_TREE_COMP_LENGTH <= end-of-current-block */
static BROTLI_INLINE void FN(Store)(HashToBinaryTree* BROTLI_RESTRICT self,
    const uint8_t* BROTLI_RESTRICT data,
    const size_t mask, const size_t ix) {
  /* Maximum distance is window size - 16, see section 9.1. of the spec. */
  const size_t max_backward = self->window_mask_ - BROTLI_WINDOW_GAP + 1;
  FN(StoreAndFindMatches)(self, data, ix, mask, MAX_TREE_COMP_LENGTH,
      max_backward, NULL, NULL);
}

static BROTLI_INLINE void FN(StoreRange)(HashToBinaryTree* BROTLI_RESTRICT self,
    const uint8_t* BROTLI_RESTRICT data, const size_t mask,
    const size_t ix_start, const size_t ix_end) {
  size_t i = ix_start;
  size_t j = ix_start;
  if (ix_start + 63 <= ix_end) {
    i = ix_end - 63;
  }
  if (ix_start + 512 <= i) {
    for (; j < i; j += 8) {
      FN(Store)(self, data, mask, j);
    }
  }
  for (; i < ix_end; ++i) {
    FN(Store)(self, data, mask, i);
  }
}

static BROTLI_INLINE void FN(StitchToPreviousBlock)(
    HashToBinaryTree* BROTLI_RESTRICT self,
    size_t num_bytes, size_t position, const uint8_t* ringbuffer,
    size_t ringbuffer_mask) {
  if (num_bytes >= FN(HashTypeLength)() - 1 &&
      position >= MAX_TREE_COMP_LENGTH) {
    /* Store the last `MAX_TREE_COMP_LENGTH - 1` positions in the hasher.
       These could not be calculated before, since they require knowledge
       of both the previous and the current block. */
    const size_t i_start = position - MAX_TREE_COMP_LENGTH + 1;
    const size_t i_end = BROTLI_MIN(size_t, position, i_start + num_bytes);
    size_t i;
    for (i = i_start; i < i_end; ++i) {
      /* Maximum distance is window size - 16, see section 9.1. of the spec.
         Furthermore, we have to make sure that we don't look further back
         from the start of the next block than the window size, otherwise we
         could access already overwritten areas of the ring-buffer. */
      const size_t max_backward =
          self->window_mask_ - BROTLI_MAX(size_t,
                                          BROTLI_WINDOW_GAP - 1,
                                          position - i);
      /* We know that i + MAX_TREE_COMP_LENGTH <= position + num_bytes, i.e. the
         end of the current block and that we have at least
         MAX_TREE_COMP_LENGTH tail in the ring-buffer. */
      FN(StoreAndFindMatches)(self, ringbuffer, i, ringbuffer_mask,
          MAX_TREE_COMP_LENGTH, max_backward, NULL, NULL);
    }
  }
}

#undef BUCKET_SIZE

#undef HashToBinaryTree
