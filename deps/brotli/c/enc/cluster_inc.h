/* NOLINT(build/header_guard) */
/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* template parameters: FN, CODE */

#define HistogramType FN(Histogram)

/* Computes the bit cost reduction by combining out[idx1] and out[idx2] and if
   it is below a threshold, stores the pair (idx1, idx2) in the *pairs queue. */
BROTLI_INTERNAL void FN(BrotliCompareAndPushToQueue)(
    const HistogramType* out, HistogramType* tmp, const uint32_t* cluster_size,
    uint32_t idx1, uint32_t idx2, size_t max_num_pairs, HistogramPair* pairs,
    size_t* num_pairs) CODE({
  BROTLI_BOOL is_good_pair = BROTLI_FALSE;
  HistogramPair p;
  p.idx1 = p.idx2 = 0;
  p.cost_diff = p.cost_combo = 0;
  if (idx1 == idx2) {
    return;
  }
  if (idx2 < idx1) {
    uint32_t t = idx2;
    idx2 = idx1;
    idx1 = t;
  }
  p.idx1 = idx1;
  p.idx2 = idx2;
  p.cost_diff = 0.5 * ClusterCostDiff(cluster_size[idx1], cluster_size[idx2]);
  p.cost_diff -= out[idx1].bit_cost_;
  p.cost_diff -= out[idx2].bit_cost_;

  if (out[idx1].total_count_ == 0) {
    p.cost_combo = out[idx2].bit_cost_;
    is_good_pair = BROTLI_TRUE;
  } else if (out[idx2].total_count_ == 0) {
    p.cost_combo = out[idx1].bit_cost_;
    is_good_pair = BROTLI_TRUE;
  } else {
    double threshold = *num_pairs == 0 ? 1e99 :
        BROTLI_MAX(double, 0.0, pairs[0].cost_diff);
    double cost_combo;
    *tmp = out[idx1];
    FN(HistogramAddHistogram)(tmp, &out[idx2]);
    cost_combo = FN(BrotliPopulationCost)(tmp);
    if (cost_combo < threshold - p.cost_diff) {
      p.cost_combo = cost_combo;
      is_good_pair = BROTLI_TRUE;
    }
  }
  if (is_good_pair) {
    p.cost_diff += p.cost_combo;
    if (*num_pairs > 0 && HistogramPairIsLess(&pairs[0], &p)) {
      /* Replace the top of the queue if needed. */
      if (*num_pairs < max_num_pairs) {
        pairs[*num_pairs] = pairs[0];
        ++(*num_pairs);
      }
      pairs[0] = p;
    } else if (*num_pairs < max_num_pairs) {
      pairs[*num_pairs] = p;
      ++(*num_pairs);
    }
  }
})

BROTLI_INTERNAL size_t FN(BrotliHistogramCombine)(HistogramType* out,
                                                  HistogramType* tmp,
                                                  uint32_t* cluster_size,
                                                  uint32_t* symbols,
                                                  uint32_t* clusters,
                                                  HistogramPair* pairs,
                                                  size_t num_clusters,
                                                  size_t symbols_size,
                                                  size_t max_clusters,
                                                  size_t max_num_pairs) CODE({
  double cost_diff_threshold = 0.0;
  size_t min_cluster_size = 1;
  size_t num_pairs = 0;

  {
    /* We maintain a vector of histogram pairs, with the property that the pair
       with the maximum bit cost reduction is the first. */
    size_t idx1;
    for (idx1 = 0; idx1 < num_clusters; ++idx1) {
      size_t idx2;
      for (idx2 = idx1 + 1; idx2 < num_clusters; ++idx2) {
        FN(BrotliCompareAndPushToQueue)(out, tmp, cluster_size, clusters[idx1],
            clusters[idx2], max_num_pairs, &pairs[0], &num_pairs);
      }
    }
  }

  while (num_clusters > min_cluster_size) {
    uint32_t best_idx1;
    uint32_t best_idx2;
    size_t i;
    if (pairs[0].cost_diff >= cost_diff_threshold) {
      cost_diff_threshold = 1e99;
      min_cluster_size = max_clusters;
      continue;
    }
    /* Take the best pair from the top of heap. */
    best_idx1 = pairs[0].idx1;
    best_idx2 = pairs[0].idx2;
    FN(HistogramAddHistogram)(&out[best_idx1], &out[best_idx2]);
    out[best_idx1].bit_cost_ = pairs[0].cost_combo;
    cluster_size[best_idx1] += cluster_size[best_idx2];
    for (i = 0; i < symbols_size; ++i) {
      if (symbols[i] == best_idx2) {
        symbols[i] = best_idx1;
      }
    }
    for (i = 0; i < num_clusters; ++i) {
      if (clusters[i] == best_idx2) {
        memmove(&clusters[i], &clusters[i + 1],
                (num_clusters - i - 1) * sizeof(clusters[0]));
        break;
      }
    }
    --num_clusters;
    {
      /* Remove pairs intersecting the just combined best pair. */
      size_t copy_to_idx = 0;
      for (i = 0; i < num_pairs; ++i) {
        HistogramPair* p = &pairs[i];
        if (p->idx1 == best_idx1 || p->idx2 == best_idx1 ||
            p->idx1 == best_idx2 || p->idx2 == best_idx2) {
          /* Remove invalid pair from the queue. */
          continue;
        }
        if (HistogramPairIsLess(&pairs[0], p)) {
          /* Replace the top of the queue if needed. */
          HistogramPair front = pairs[0];
          pairs[0] = *p;
          pairs[copy_to_idx] = front;
        } else {
          pairs[copy_to_idx] = *p;
        }
        ++copy_to_idx;
      }
      num_pairs = copy_to_idx;
    }

    /* Push new pairs formed with the combined histogram to the heap. */
    for (i = 0; i < num_clusters; ++i) {
      FN(BrotliCompareAndPushToQueue)(out, tmp, cluster_size, best_idx1,
          clusters[i], max_num_pairs, &pairs[0], &num_pairs);
    }
  }
  return num_clusters;
})

/* What is the bit cost of moving histogram from cur_symbol to candidate. */
BROTLI_INTERNAL double FN(BrotliHistogramBitCostDistance)(
    const HistogramType* histogram, const HistogramType* candidate,
    HistogramType* tmp) CODE({
  if (histogram->total_count_ == 0) {
    return 0.0;
  } else {
    *tmp = *histogram;
    FN(HistogramAddHistogram)(tmp, candidate);
    return FN(BrotliPopulationCost)(tmp) - candidate->bit_cost_;
  }
})

/* Find the best 'out' histogram for each of the 'in' histograms.
   When called, clusters[0..num_clusters) contains the unique values from
   symbols[0..in_size), but this property is not preserved in this function.
   Note: we assume that out[]->bit_cost_ is already up-to-date. */
BROTLI_INTERNAL void FN(BrotliHistogramRemap)(const HistogramType* in,
    size_t in_size, const uint32_t* clusters, size_t num_clusters,
    HistogramType* out, HistogramType* tmp, uint32_t* symbols) CODE({
  size_t i;
  for (i = 0; i < in_size; ++i) {
    uint32_t best_out = i == 0 ? symbols[0] : symbols[i - 1];
    double best_bits =
        FN(BrotliHistogramBitCostDistance)(&in[i], &out[best_out], tmp);
    size_t j;
    for (j = 0; j < num_clusters; ++j) {
      const double cur_bits =
          FN(BrotliHistogramBitCostDistance)(&in[i], &out[clusters[j]], tmp);
      if (cur_bits < best_bits) {
        best_bits = cur_bits;
        best_out = clusters[j];
      }
    }
    symbols[i] = best_out;
  }

  /* Recompute each out based on raw and symbols. */
  for (i = 0; i < num_clusters; ++i) {
    FN(HistogramClear)(&out[clusters[i]]);
  }
  for (i = 0; i < in_size; ++i) {
    FN(HistogramAddHistogram)(&out[symbols[i]], &in[i]);
  }
})

/* Reorders elements of the out[0..length) array and changes values in
   symbols[0..length) array in the following way:
     * when called, symbols[] contains indexes into out[], and has N unique
       values (possibly N < length)
     * on return, symbols'[i] = f(symbols[i]) and
                  out'[symbols'[i]] = out[symbols[i]], for each 0 <= i < length,
       where f is a bijection between the range of symbols[] and [0..N), and
       the first occurrences of values in symbols'[i] come in consecutive
       increasing order.
   Returns N, the number of unique values in symbols[]. */
BROTLI_INTERNAL size_t FN(BrotliHistogramReindex)(MemoryManager* m,
    HistogramType* out, uint32_t* symbols, size_t length) CODE({
  static const uint32_t kInvalidIndex = BROTLI_UINT32_MAX;
  uint32_t* new_index = BROTLI_ALLOC(m, uint32_t, length);
  uint32_t next_index;
  HistogramType* tmp;
  size_t i;
  if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(new_index)) return 0;
  for (i = 0; i < length; ++i) {
      new_index[i] = kInvalidIndex;
  }
  next_index = 0;
  for (i = 0; i < length; ++i) {
    if (new_index[symbols[i]] == kInvalidIndex) {
      new_index[symbols[i]] = next_index;
      ++next_index;
    }
  }
  /* TODO(eustas): by using idea of "cycle-sort" we can avoid allocation of
     tmp and reduce the number of copying by the factor of 2. */
  tmp = BROTLI_ALLOC(m, HistogramType, next_index);
  if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(tmp)) return 0;
  next_index = 0;
  for (i = 0; i < length; ++i) {
    if (new_index[symbols[i]] == next_index) {
      tmp[next_index] = out[symbols[i]];
      ++next_index;
    }
    symbols[i] = new_index[symbols[i]];
  }
  BROTLI_FREE(m, new_index);
  for (i = 0; i < next_index; ++i) {
    out[i] = tmp[i];
  }
  BROTLI_FREE(m, tmp);
  return next_index;
})

BROTLI_INTERNAL void FN(BrotliClusterHistograms)(
    MemoryManager* m, const HistogramType* in, const size_t in_size,
    size_t max_histograms, HistogramType* out, size_t* out_size,
    uint32_t* histogram_symbols) CODE({
  uint32_t* cluster_size = BROTLI_ALLOC(m, uint32_t, in_size);
  uint32_t* clusters = BROTLI_ALLOC(m, uint32_t, in_size);
  size_t num_clusters = 0;
  const size_t max_input_histograms = 64;
  size_t pairs_capacity = max_input_histograms * max_input_histograms / 2;
  /* For the first pass of clustering, we allow all pairs. */
  HistogramPair* pairs = BROTLI_ALLOC(m, HistogramPair, pairs_capacity + 1);
  /* TODO(eustas): move to "persistent" arena? */
  HistogramType* tmp = BROTLI_ALLOC(m, HistogramType, 1);
  size_t i;

  if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(cluster_size) ||
      BROTLI_IS_NULL(clusters) || BROTLI_IS_NULL(pairs)|| BROTLI_IS_NULL(tmp)) {
    return;
  }

  for (i = 0; i < in_size; ++i) {
    cluster_size[i] = 1;
  }

  for (i = 0; i < in_size; ++i) {
    out[i] = in[i];
    out[i].bit_cost_ = FN(BrotliPopulationCost)(&in[i]);
    histogram_symbols[i] = (uint32_t)i;
  }

  for (i = 0; i < in_size; i += max_input_histograms) {
    size_t num_to_combine =
        BROTLI_MIN(size_t, in_size - i, max_input_histograms);
    size_t num_new_clusters;
    size_t j;
    for (j = 0; j < num_to_combine; ++j) {
      clusters[num_clusters + j] = (uint32_t)(i + j);
    }
    num_new_clusters =
        FN(BrotliHistogramCombine)(out, tmp, cluster_size,
                                   &histogram_symbols[i],
                                   &clusters[num_clusters], pairs,
                                   num_to_combine, num_to_combine,
                                   max_histograms, pairs_capacity);
    num_clusters += num_new_clusters;
  }

  {
    /* For the second pass, we limit the total number of histogram pairs.
       After this limit is reached, we only keep searching for the best pair. */
    size_t max_num_pairs = BROTLI_MIN(size_t,
        64 * num_clusters, (num_clusters / 2) * num_clusters);
    BROTLI_ENSURE_CAPACITY(
        m, HistogramPair, pairs, pairs_capacity, max_num_pairs + 1);
    if (BROTLI_IS_OOM(m)) return;

    /* Collapse similar histograms. */
    num_clusters = FN(BrotliHistogramCombine)(out, tmp, cluster_size,
                                              histogram_symbols, clusters,
                                              pairs, num_clusters, in_size,
                                              max_histograms, max_num_pairs);
  }
  BROTLI_FREE(m, pairs);
  BROTLI_FREE(m, cluster_size);
  /* Find the optimal map from original histograms to the final ones. */
  FN(BrotliHistogramRemap)(in, in_size, clusters, num_clusters,
                           out, tmp, histogram_symbols);
  BROTLI_FREE(m, tmp);
  BROTLI_FREE(m, clusters);
  /* Convert the context map to a canonical form. */
  *out_size = FN(BrotliHistogramReindex)(m, out, histogram_symbols, in_size);
  if (BROTLI_IS_OOM(m)) return;
})

#undef HistogramType
