/* NOLINT(build/header_guard) */
/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* template parameters: EXPORT_FN, FN */

static BROTLI_NOINLINE void EXPORT_FN(CreateBackwardReferences)(
    size_t num_bytes, size_t position,
    const uint8_t* ringbuffer, size_t ringbuffer_mask,
    ContextLut literal_context_lut, const BrotliEncoderParams* params,
    Hasher* hasher, int* dist_cache, size_t* last_insert_len,
    Command* commands, size_t* num_commands, size_t* num_literals) {
  HASHER()* privat = &hasher->privat.FN(_);
  /* Set maximum distance, see section 9.1. of the spec. */
  const size_t max_backward_limit = BROTLI_MAX_BACKWARD_LIMIT(params->lgwin);
  const size_t position_offset = params->stream_offset;

  const Command* const orig_commands = commands;
  size_t insert_length = *last_insert_len;
  const size_t pos_end = position + num_bytes;
  const size_t store_end = num_bytes >= FN(StoreLookahead)() ?
      position + num_bytes - FN(StoreLookahead)() + 1 : position;

  /* For speed up heuristics for random data. */
  const size_t random_heuristics_window_size =
      LiteralSpreeLengthForSparseSearch(params);
  size_t apply_random_heuristics = position + random_heuristics_window_size;
  const size_t gap = params->dictionary.compound.total_size;

  /* Minimum score to accept a backward reference. */
  const score_t kMinScore = BROTLI_SCORE_BASE + 100;

  FN(PrepareDistanceCache)(privat, dist_cache);

  while (position + FN(HashTypeLength)() < pos_end) {
    size_t max_length = pos_end - position;
    size_t max_distance = BROTLI_MIN(size_t, position, max_backward_limit);
    size_t dictionary_start = BROTLI_MIN(size_t,
        position + position_offset, max_backward_limit);
    HasherSearchResult sr;
    int dict_id = 0;
    uint8_t p1 = 0;
    uint8_t p2 = 0;
    if (params->dictionary.contextual.context_based) {
      p1 = position >= 1 ?
          ringbuffer[(size_t)(position - 1) & ringbuffer_mask] : 0;
      p2 = position >= 2 ?
          ringbuffer[(size_t)(position - 2) & ringbuffer_mask] : 0;
      dict_id = params->dictionary.contextual.context_map[
          BROTLI_CONTEXT(p1, p2, literal_context_lut)];
    }
    sr.len = 0;
    sr.len_code_delta = 0;
    sr.distance = 0;
    sr.score = kMinScore;
    FN(FindLongestMatch)(privat, params->dictionary.contextual.dict[dict_id],
        ringbuffer, ringbuffer_mask, dist_cache, position, max_length,
        max_distance, dictionary_start + gap, params->dist.max_distance, &sr);
    if (ENABLE_COMPOUND_DICTIONARY) {
      LookupCompoundDictionaryMatch(&params->dictionary.compound, ringbuffer,
          ringbuffer_mask, dist_cache, position, max_length,
          dictionary_start, params->dist.max_distance, &sr);
    }
    if (sr.score > kMinScore) {
      /* Found a match. Let's look for something even better ahead. */
      int delayed_backward_references_in_row = 0;
      --max_length;
      for (;; --max_length) {
        const score_t cost_diff_lazy = 175;
        HasherSearchResult sr2;
        sr2.len = params->quality < MIN_QUALITY_FOR_EXTENSIVE_REFERENCE_SEARCH ?
            BROTLI_MIN(size_t, sr.len - 1, max_length) : 0;
        sr2.len_code_delta = 0;
        sr2.distance = 0;
        sr2.score = kMinScore;
        max_distance = BROTLI_MIN(size_t, position + 1, max_backward_limit);
        dictionary_start = BROTLI_MIN(size_t,
            position + 1 + position_offset, max_backward_limit);
        if (params->dictionary.contextual.context_based) {
          p2 = p1;
          p1 = ringbuffer[position & ringbuffer_mask];
          dict_id = params->dictionary.contextual.context_map[
              BROTLI_CONTEXT(p1, p2, literal_context_lut)];
        }
        FN(FindLongestMatch)(privat,
            params->dictionary.contextual.dict[dict_id],
            ringbuffer, ringbuffer_mask, dist_cache, position + 1, max_length,
            max_distance, dictionary_start + gap, params->dist.max_distance,
            &sr2);
        if (ENABLE_COMPOUND_DICTIONARY) {
          LookupCompoundDictionaryMatch(
              &params->dictionary.compound, ringbuffer,
              ringbuffer_mask, dist_cache, position + 1, max_length,
              dictionary_start, params->dist.max_distance, &sr2);
        }
        if (sr2.score >= sr.score + cost_diff_lazy) {
          /* Ok, let's just write one byte for now and start a match from the
             next byte. */
          ++position;
          ++insert_length;
          sr = sr2;
          if (++delayed_backward_references_in_row < 4 &&
              position + FN(HashTypeLength)() < pos_end) {
            continue;
          }
        }
        break;
      }
      apply_random_heuristics =
          position + 2 * sr.len + random_heuristics_window_size;
      dictionary_start = BROTLI_MIN(size_t,
          position + position_offset, max_backward_limit);
      {
        /* The first 16 codes are special short-codes,
           and the minimum offset is 1. */
        size_t distance_code = ComputeDistanceCode(
            sr.distance, dictionary_start + gap, dist_cache);
        if ((sr.distance <= (dictionary_start + gap)) && distance_code > 0) {
          dist_cache[3] = dist_cache[2];
          dist_cache[2] = dist_cache[1];
          dist_cache[1] = dist_cache[0];
          dist_cache[0] = (int)sr.distance;
          FN(PrepareDistanceCache)(privat, dist_cache);
        }
        InitCommand(commands++, &params->dist, insert_length,
            sr.len, sr.len_code_delta, distance_code);
      }
      *num_literals += insert_length;
      insert_length = 0;
      /* Put the hash keys into the table, if there are enough bytes left.
         Depending on the hasher implementation, it can push all positions
         in the given range or only a subset of them.
         Avoid hash poisoning with RLE data. */
      {
        size_t range_start = position + 2;
        size_t range_end = BROTLI_MIN(size_t, position + sr.len, store_end);
        if (sr.distance < (sr.len >> 2)) {
          range_start = BROTLI_MIN(size_t, range_end, BROTLI_MAX(size_t,
              range_start, position + sr.len - (sr.distance << 2)));
        }
        FN(StoreRange)(privat, ringbuffer, ringbuffer_mask, range_start,
                       range_end);
      }
      position += sr.len;
    } else {
      ++insert_length;
      ++position;
      /* If we have not seen matches for a long time, we can skip some
         match lookups. Unsuccessful match lookups are very very expensive
         and this kind of a heuristic speeds up compression quite
         a lot. */
      if (position > apply_random_heuristics) {
        /* Going through uncompressible data, jump. */
        if (position >
            apply_random_heuristics + 4 * random_heuristics_window_size) {
          /* It is quite a long time since we saw a copy, so we assume
             that this data is not compressible, and store hashes less
             often. Hashes of non compressible data are less likely to
             turn out to be useful in the future, too, so we store less of
             them to not to flood out the hash table of good compressible
             data. */
          const size_t kMargin =
              BROTLI_MAX(size_t, FN(StoreLookahead)() - 1, 4);
          size_t pos_jump =
              BROTLI_MIN(size_t, position + 16, pos_end - kMargin);
          for (; position < pos_jump; position += 4) {
            FN(Store)(privat, ringbuffer, ringbuffer_mask, position);
            insert_length += 4;
          }
        } else {
          const size_t kMargin =
              BROTLI_MAX(size_t, FN(StoreLookahead)() - 1, 2);
          size_t pos_jump =
              BROTLI_MIN(size_t, position + 8, pos_end - kMargin);
          for (; position < pos_jump; position += 2) {
            FN(Store)(privat, ringbuffer, ringbuffer_mask, position);
            insert_length += 2;
          }
        }
      }
    }
  }
  insert_length += pos_end - position;
  *last_insert_len = insert_length;
  *num_commands += (size_t)(commands - orig_commands);
}
