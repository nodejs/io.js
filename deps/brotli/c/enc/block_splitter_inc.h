/* NOLINT(build/header_guard) */
/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* template parameters: FN, DataType */

#define HistogramType FN(Histogram)

static void FN(InitialEntropyCodes)(const DataType* data, size_t length,
                                    size_t stride,
                                    size_t num_histograms,
                                    HistogramType* histograms) {
  uint32_t seed = 7;
  size_t block_length = length / num_histograms;
  size_t i;
  FN(ClearHistograms)(histograms, num_histograms);
  for (i = 0; i < num_histograms; ++i) {
    size_t pos = length * i / num_histograms;
    if (i != 0) {
      pos += MyRand(&seed) % block_length;
    }
    if (pos + stride >= length) {
      pos = length - stride - 1;
    }
    FN(HistogramAddVector)(&histograms[i], data + pos, stride);
  }
}

static void FN(RandomSample)(uint32_t* seed,
                             const DataType* data,
                             size_t length,
                             size_t stride,
                             HistogramType* sample) {
  size_t pos = 0;
  if (stride >= length) {
    stride = length;
  } else {
    pos = MyRand(seed) % (length - stride + 1);
  }
  FN(HistogramAddVector)(sample, data + pos, stride);
}

static void FN(RefineEntropyCodes)(const DataType* data, size_t length,
                                   size_t stride,
                                   size_t num_histograms,
                                   HistogramType* histograms,
                                   HistogramType* tmp) {
  size_t iters =
      kIterMulForRefining * length / stride + kMinItersForRefining;
  uint32_t seed = 7;
  size_t iter;
  iters = ((iters + num_histograms - 1) / num_histograms) * num_histograms;
  for (iter = 0; iter < iters; ++iter) {
    FN(HistogramClear)(tmp);
    FN(RandomSample)(&seed, data, length, stride, tmp);
    FN(HistogramAddHistogram)(&histograms[iter % num_histograms], tmp);
  }
}

/* Assigns a block id from the range [0, num_histograms) to each data element
   in data[0..length) and fills in block_id[0..length) with the assigned values.
   Returns the number of blocks, i.e. one plus the number of block switches. */
static size_t FN(FindBlocks)(const DataType* data, const size_t length,
                             const double block_switch_bitcost,
                             const size_t num_histograms,
                             const HistogramType* histograms,
                             double* insert_cost,
                             double* cost,
                             uint8_t* switch_signal,
                             uint8_t* block_id) {
  const size_t alphabet_size = FN(HistogramDataSize)();
  const size_t bitmap_len = (num_histograms + 7) >> 3;
  size_t num_blocks = 1;
  size_t byte_ix;
  size_t i;
  size_t j;
  BROTLI_DCHECK(num_histograms <= 256);

  /* Trivial case: single historgram -> single block type. */
  if (num_histograms <= 1) {
    for (i = 0; i < length; ++i) {
      block_id[i] = 0;
    }
    return 1;
  }

  /* Fill bitcost for each symbol of all histograms.
   * Non-existing symbol cost: 2 + log2(total_count).
   * Regular symbol cost: -log2(symbol_count / total_count). */
  memset(insert_cost, 0,
         sizeof(insert_cost[0]) * alphabet_size * num_histograms);
  for (i = 0; i < num_histograms; ++i) {
    insert_cost[i] = FastLog2((uint32_t)histograms[i].total_count_);
  }
  for (i = alphabet_size; i != 0;) {
    /* Reverse order to use the 0-th row as a temporary storage. */
    --i;
    for (j = 0; j < num_histograms; ++j) {
      insert_cost[i * num_histograms + j] =
          insert_cost[j] - BitCost(histograms[j].data_[i]);
    }
  }

  /* After each iteration of this loop, cost[k] will contain the difference
     between the minimum cost of arriving at the current byte position using
     entropy code k, and the minimum cost of arriving at the current byte
     position. This difference is capped at the block switch cost, and if it
     reaches block switch cost, it means that when we trace back from the last
     position, we need to switch here. */
  memset(cost, 0, sizeof(cost[0]) * num_histograms);
  memset(switch_signal, 0, sizeof(switch_signal[0]) * length * bitmap_len);
  for (byte_ix = 0; byte_ix < length; ++byte_ix) {
    size_t ix = byte_ix * bitmap_len;
    size_t symbol = data[byte_ix];
    size_t insert_cost_ix = symbol * num_histograms;
    double min_cost = 1e99;
    double block_switch_cost = block_switch_bitcost;
    size_t k;
    for (k = 0; k < num_histograms; ++k) {
      /* We are coding the symbol with entropy code k. */
      cost[k] += insert_cost[insert_cost_ix + k];
      if (cost[k] < min_cost) {
        min_cost = cost[k];
        block_id[byte_ix] = (uint8_t)k;
      }
    }
    /* More blocks for the beginning. */
    if (byte_ix < 2000) {
      block_switch_cost *= 0.77 + 0.07 * (double)byte_ix / 2000;
    }
    for (k = 0; k < num_histograms; ++k) {
      cost[k] -= min_cost;
      if (cost[k] >= block_switch_cost) {
        const uint8_t mask = (uint8_t)(1u << (k & 7));
        cost[k] = block_switch_cost;
        BROTLI_DCHECK((k >> 3) < bitmap_len);
        switch_signal[ix + (k >> 3)] |= mask;
      }
    }
  }

  byte_ix = length - 1;
  {  /* Trace back from the last position and switch at the marked places. */
    size_t ix = byte_ix * bitmap_len;
    uint8_t cur_id = block_id[byte_ix];
    while (byte_ix > 0) {
      const uint8_t mask = (uint8_t)(1u << (cur_id & 7));
      BROTLI_DCHECK(((size_t)cur_id >> 3) < bitmap_len);
      --byte_ix;
      ix -= bitmap_len;
      if (switch_signal[ix + (cur_id >> 3)] & mask) {
        if (cur_id != block_id[byte_ix]) {
          cur_id = block_id[byte_ix];
          ++num_blocks;
        }
      }
      block_id[byte_ix] = cur_id;
    }
  }
  return num_blocks;
}

static size_t FN(RemapBlockIds)(uint8_t* block_ids, const size_t length,
                                uint16_t* new_id, const size_t num_histograms) {
  static const uint16_t kInvalidId = 256;
  uint16_t next_id = 0;
  size_t i;
  for (i = 0; i < num_histograms; ++i) {
    new_id[i] = kInvalidId;
  }
  for (i = 0; i < length; ++i) {
    BROTLI_DCHECK(block_ids[i] < num_histograms);
    if (new_id[block_ids[i]] == kInvalidId) {
      new_id[block_ids[i]] = next_id++;
    }
  }
  for (i = 0; i < length; ++i) {
    block_ids[i] = (uint8_t)new_id[block_ids[i]];
    BROTLI_DCHECK(block_ids[i] < num_histograms);
  }
  BROTLI_DCHECK(next_id <= num_histograms);
  return next_id;
}

static void FN(BuildBlockHistograms)(const DataType* data, const size_t length,
                                     const uint8_t* block_ids,
                                     const size_t num_histograms,
                                     HistogramType* histograms) {
  size_t i;
  FN(ClearHistograms)(histograms, num_histograms);
  for (i = 0; i < length; ++i) {
    FN(HistogramAdd)(&histograms[block_ids[i]], data[i]);
  }
}

/* Given the initial partitioning build partitioning with limited number
 * of histograms (and block types). */
static void FN(ClusterBlocks)(MemoryManager* m,
                              const DataType* data, const size_t length,
                              const size_t num_blocks,
                              uint8_t* block_ids,
                              BlockSplit* split) {
  uint32_t* histogram_symbols = BROTLI_ALLOC(m, uint32_t, num_blocks);
  uint32_t* u32 =
      BROTLI_ALLOC(m, uint32_t, num_blocks + 4 * HISTOGRAMS_PER_BATCH);
  const size_t expected_num_clusters = CLUSTERS_PER_BATCH *
      (num_blocks + HISTOGRAMS_PER_BATCH - 1) / HISTOGRAMS_PER_BATCH;
  size_t all_histograms_size = 0;
  size_t all_histograms_capacity = expected_num_clusters;
  HistogramType* all_histograms =
      BROTLI_ALLOC(m, HistogramType, all_histograms_capacity);
  size_t cluster_size_size = 0;
  size_t cluster_size_capacity = expected_num_clusters;
  uint32_t* cluster_size = BROTLI_ALLOC(m, uint32_t, cluster_size_capacity);
  size_t num_clusters = 0;
  HistogramType* histograms = BROTLI_ALLOC(m, HistogramType,
      BROTLI_MIN(size_t, num_blocks, HISTOGRAMS_PER_BATCH));
  size_t max_num_pairs =
      HISTOGRAMS_PER_BATCH * HISTOGRAMS_PER_BATCH / 2;
  size_t pairs_capacity = max_num_pairs + 1;
  HistogramPair* pairs = BROTLI_ALLOC(m, HistogramPair, pairs_capacity);
  size_t pos = 0;
  uint32_t* clusters;
  size_t num_final_clusters;
  static const uint32_t kInvalidIndex = BROTLI_UINT32_MAX;
  uint32_t* new_index;
  size_t i;
  uint32_t* BROTLI_RESTRICT const sizes = u32 + 0 * HISTOGRAMS_PER_BATCH;
  uint32_t* BROTLI_RESTRICT const new_clusters = u32 + 1 * HISTOGRAMS_PER_BATCH;
  uint32_t* BROTLI_RESTRICT const symbols = u32 + 2 * HISTOGRAMS_PER_BATCH;
  uint32_t* BROTLI_RESTRICT const remap = u32 + 3 * HISTOGRAMS_PER_BATCH;
  uint32_t* BROTLI_RESTRICT const block_lengths =
      u32 + 4 * HISTOGRAMS_PER_BATCH;
  /* TODO(eustas): move to arena? */
  HistogramType* tmp = BROTLI_ALLOC(m, HistogramType, 2);

  if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(histogram_symbols) ||
      BROTLI_IS_NULL(u32) || BROTLI_IS_NULL(all_histograms) ||
      BROTLI_IS_NULL(cluster_size) || BROTLI_IS_NULL(histograms) ||
      BROTLI_IS_NULL(pairs) || BROTLI_IS_NULL(tmp)) {
    return;
  }

  memset(u32, 0, (num_blocks + 4 * HISTOGRAMS_PER_BATCH) * sizeof(uint32_t));

  /* Calculate block lengths (convert repeating values -> series length). */
  {
    size_t block_idx = 0;
    for (i = 0; i < length; ++i) {
      BROTLI_DCHECK(block_idx < num_blocks);
      ++block_lengths[block_idx];
      if (i + 1 == length || block_ids[i] != block_ids[i + 1]) {
        ++block_idx;
      }
    }
    BROTLI_DCHECK(block_idx == num_blocks);
  }

  /* Pre-cluster blocks (cluster batches). */
  for (i = 0; i < num_blocks; i += HISTOGRAMS_PER_BATCH) {
    const size_t num_to_combine =
        BROTLI_MIN(size_t, num_blocks - i, HISTOGRAMS_PER_BATCH);
    size_t num_new_clusters;
    size_t j;
    for (j = 0; j < num_to_combine; ++j) {
      size_t k;
      size_t block_length = block_lengths[i + j];
      FN(HistogramClear)(&histograms[j]);
      for (k = 0; k < block_length; ++k) {
        FN(HistogramAdd)(&histograms[j], data[pos++]);
      }
      histograms[j].bit_cost_ = FN(BrotliPopulationCost)(&histograms[j]);
      new_clusters[j] = (uint32_t)j;
      symbols[j] = (uint32_t)j;
      sizes[j] = 1;
    }
    num_new_clusters = FN(BrotliHistogramCombine)(
        histograms, tmp, sizes, symbols, new_clusters, pairs, num_to_combine,
        num_to_combine, HISTOGRAMS_PER_BATCH, max_num_pairs);
    BROTLI_ENSURE_CAPACITY(m, HistogramType, all_histograms,
        all_histograms_capacity, all_histograms_size + num_new_clusters);
    BROTLI_ENSURE_CAPACITY(m, uint32_t, cluster_size,
        cluster_size_capacity, cluster_size_size + num_new_clusters);
    if (BROTLI_IS_OOM(m)) return;
    for (j = 0; j < num_new_clusters; ++j) {
      all_histograms[all_histograms_size++] = histograms[new_clusters[j]];
      cluster_size[cluster_size_size++] = sizes[new_clusters[j]];
      remap[new_clusters[j]] = (uint32_t)j;
    }
    for (j = 0; j < num_to_combine; ++j) {
      histogram_symbols[i + j] = (uint32_t)num_clusters + remap[symbols[j]];
    }
    num_clusters += num_new_clusters;
    BROTLI_DCHECK(num_clusters == cluster_size_size);
    BROTLI_DCHECK(num_clusters == all_histograms_size);
  }
  BROTLI_FREE(m, histograms);

  /* Final clustering. */
  max_num_pairs =
      BROTLI_MIN(size_t, 64 * num_clusters, (num_clusters / 2) * num_clusters);
  if (pairs_capacity < max_num_pairs + 1) {
    BROTLI_FREE(m, pairs);
    pairs = BROTLI_ALLOC(m, HistogramPair, max_num_pairs + 1);
    if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(pairs)) return;
  }
  clusters = BROTLI_ALLOC(m, uint32_t, num_clusters);
  if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(clusters)) return;
  for (i = 0; i < num_clusters; ++i) {
    clusters[i] = (uint32_t)i;
  }
  num_final_clusters = FN(BrotliHistogramCombine)(
      all_histograms, tmp, cluster_size, histogram_symbols, clusters, pairs,
      num_clusters, num_blocks, BROTLI_MAX_NUMBER_OF_BLOCK_TYPES,
      max_num_pairs);
  BROTLI_FREE(m, pairs);
  BROTLI_FREE(m, cluster_size);

  /* Assign blocks to final histograms. */
  new_index = BROTLI_ALLOC(m, uint32_t, num_clusters);
  if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(new_index)) return;
  for (i = 0; i < num_clusters; ++i) new_index[i] = kInvalidIndex;
  pos = 0;
  {
    uint32_t next_index = 0;
    for (i = 0; i < num_blocks; ++i) {
      size_t j;
      uint32_t best_out;
      double best_bits;
      FN(HistogramClear)(tmp);
      for (j = 0; j < block_lengths[i]; ++j) {
        FN(HistogramAdd)(tmp, data[pos++]);
      }
      /* Among equally good histograms prefer last used. */
      /* TODO(eustas): should we give a block-switch discount here? */
      best_out = (i == 0) ? histogram_symbols[0] : histogram_symbols[i - 1];
      best_bits = FN(BrotliHistogramBitCostDistance)(
          tmp, &all_histograms[best_out], tmp + 1);
      for (j = 0; j < num_final_clusters; ++j) {
        const double cur_bits = FN(BrotliHistogramBitCostDistance)(
            tmp, &all_histograms[clusters[j]], tmp + 1);
        if (cur_bits < best_bits) {
          best_bits = cur_bits;
          best_out = clusters[j];
        }
      }
      histogram_symbols[i] = best_out;
      if (new_index[best_out] == kInvalidIndex) {
        new_index[best_out] = next_index++;
      }
    }
  }
  BROTLI_FREE(m, tmp);
  BROTLI_FREE(m, clusters);
  BROTLI_FREE(m, all_histograms);
  BROTLI_ENSURE_CAPACITY(
      m, uint8_t, split->types, split->types_alloc_size, num_blocks);
  BROTLI_ENSURE_CAPACITY(
      m, uint32_t, split->lengths, split->lengths_alloc_size, num_blocks);
  if (BROTLI_IS_OOM(m)) return;

  /* Rewrite final assignment to block-split. There might be less blocks
   * than |num_blocks| due to clustering. */
  {
    uint32_t cur_length = 0;
    size_t block_idx = 0;
    uint8_t max_type = 0;
    for (i = 0; i < num_blocks; ++i) {
      cur_length += block_lengths[i];
      if (i + 1 == num_blocks ||
          histogram_symbols[i] != histogram_symbols[i + 1]) {
        const uint8_t id = (uint8_t)new_index[histogram_symbols[i]];
        split->types[block_idx] = id;
        split->lengths[block_idx] = cur_length;
        max_type = BROTLI_MAX(uint8_t, max_type, id);
        cur_length = 0;
        ++block_idx;
      }
    }
    split->num_blocks = block_idx;
    split->num_types = (size_t)max_type + 1;
  }
  BROTLI_FREE(m, new_index);
  BROTLI_FREE(m, u32);
  BROTLI_FREE(m, histogram_symbols);
}

/* Create BlockSplit (partitioning) given the limits, estimates and "effort"
 * parameters.
 *
 * NB: max_histograms is often less than number of histograms allowed by format;
 *     this is done intentionally, to save some "space" for context-aware
 *     clustering (here entropy is estimated for context-free symbols). */
static void FN(SplitByteVector)(MemoryManager* m,
                                const DataType* data, const size_t length,
                                const size_t symbols_per_histogram,
                                const size_t max_histograms,
                                const size_t sampling_stride_length,
                                const double block_switch_cost,
                                const BrotliEncoderParams* params,
                                BlockSplit* split) {
  const size_t data_size = FN(HistogramDataSize)();
  HistogramType* histograms;
  HistogramType* tmp;
  /* Calculate number of histograms; initial estimate is one histogram per
   * specified amount of symbols; however, this value is capped. */
  size_t num_histograms = length / symbols_per_histogram + 1;
  if (num_histograms > max_histograms) {
    num_histograms = max_histograms;
  }

  /* Corner case: no input. */
  if (length == 0) {
    split->num_types = 1;
    return;
  }

  if (length < kMinLengthForBlockSplitting) {
    BROTLI_ENSURE_CAPACITY(m, uint8_t,
        split->types, split->types_alloc_size, split->num_blocks + 1);
    BROTLI_ENSURE_CAPACITY(m, uint32_t,
        split->lengths, split->lengths_alloc_size, split->num_blocks + 1);
    if (BROTLI_IS_OOM(m)) return;
    split->num_types = 1;
    split->types[split->num_blocks] = 0;
    split->lengths[split->num_blocks] = (uint32_t)length;
    split->num_blocks++;
    return;
  }
  histograms = BROTLI_ALLOC(m, HistogramType, num_histograms + 1);
  tmp = histograms + num_histograms;
  if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(histograms)) return;
  /* Find good entropy codes. */
  FN(InitialEntropyCodes)(data, length,
                          sampling_stride_length,
                          num_histograms, histograms);
  FN(RefineEntropyCodes)(data, length,
                         sampling_stride_length,
                         num_histograms, histograms, tmp);
  {
    /* Find a good path through literals with the good entropy codes. */
    uint8_t* block_ids = BROTLI_ALLOC(m, uint8_t, length);
    size_t num_blocks = 0;
    const size_t bitmaplen = (num_histograms + 7) >> 3;
    double* insert_cost = BROTLI_ALLOC(m, double, data_size * num_histograms);
    double* cost = BROTLI_ALLOC(m, double, num_histograms);
    uint8_t* switch_signal = BROTLI_ALLOC(m, uint8_t, length * bitmaplen);
    uint16_t* new_id = BROTLI_ALLOC(m, uint16_t, num_histograms);
    const size_t iters = params->quality < HQ_ZOPFLIFICATION_QUALITY ? 3 : 10;
    size_t i;
    if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(block_ids) ||
        BROTLI_IS_NULL(insert_cost) || BROTLI_IS_NULL(cost) ||
        BROTLI_IS_NULL(switch_signal) || BROTLI_IS_NULL(new_id)) {
      return;
    }
    for (i = 0; i < iters; ++i) {
      num_blocks = FN(FindBlocks)(data, length,
                                  block_switch_cost,
                                  num_histograms, histograms,
                                  insert_cost, cost, switch_signal,
                                  block_ids);
      num_histograms = FN(RemapBlockIds)(block_ids, length,
                                         new_id, num_histograms);
      FN(BuildBlockHistograms)(data, length, block_ids,
                               num_histograms, histograms);
    }
    BROTLI_FREE(m, insert_cost);
    BROTLI_FREE(m, cost);
    BROTLI_FREE(m, switch_signal);
    BROTLI_FREE(m, new_id);
    BROTLI_FREE(m, histograms);
    FN(ClusterBlocks)(m, data, length, num_blocks, block_ids, split);
    if (BROTLI_IS_OOM(m)) return;
    BROTLI_FREE(m, block_ids);
  }
}

#undef HistogramType
