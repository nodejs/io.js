/* Copyright 2015 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Algorithms for distributing the literals and commands of a metablock between
   block types and contexts. */

#include "metablock.h"

#include <brotli/types.h>

#include "../common/constants.h"
#include "../common/context.h"
#include "../common/platform.h"
#include "bit_cost.h"
#include "block_splitter.h"
#include "cluster.h"
#include "entropy_encode.h"
#include "histogram.h"
#include "memory.h"
#include "quality.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

void BrotliInitDistanceParams(BrotliDistanceParams* dist_params,
    uint32_t npostfix, uint32_t ndirect, BROTLI_BOOL large_window) {
  uint32_t alphabet_size_max;
  uint32_t alphabet_size_limit;
  uint32_t max_distance;

  dist_params->distance_postfix_bits = npostfix;
  dist_params->num_direct_distance_codes = ndirect;

  alphabet_size_max = BROTLI_DISTANCE_ALPHABET_SIZE(
      npostfix, ndirect, BROTLI_MAX_DISTANCE_BITS);
  alphabet_size_limit = alphabet_size_max;
  max_distance = ndirect + (1U << (BROTLI_MAX_DISTANCE_BITS + npostfix + 2)) -
      (1U << (npostfix + 2));

  if (large_window) {
    BrotliDistanceCodeLimit limit = BrotliCalculateDistanceCodeLimit(
        BROTLI_MAX_ALLOWED_DISTANCE, npostfix, ndirect);
    alphabet_size_max = BROTLI_DISTANCE_ALPHABET_SIZE(
        npostfix, ndirect, BROTLI_LARGE_MAX_DISTANCE_BITS);
    alphabet_size_limit = limit.max_alphabet_size;
    max_distance = limit.max_distance;
  }

  dist_params->alphabet_size_max = alphabet_size_max;
  dist_params->alphabet_size_limit = alphabet_size_limit;
  dist_params->max_distance = max_distance;
}

static void RecomputeDistancePrefixes(Command* cmds,
                                      size_t num_commands,
                                      const BrotliDistanceParams* orig_params,
                                      const BrotliDistanceParams* new_params) {
  size_t i;

  if (orig_params->distance_postfix_bits == new_params->distance_postfix_bits &&
      orig_params->num_direct_distance_codes ==
      new_params->num_direct_distance_codes) {
    return;
  }

  for (i = 0; i < num_commands; ++i) {
    Command* cmd = &cmds[i];
    if (CommandCopyLen(cmd) && cmd->cmd_prefix_ >= 128) {
      PrefixEncodeCopyDistance(CommandRestoreDistanceCode(cmd, orig_params),
                               new_params->num_direct_distance_codes,
                               new_params->distance_postfix_bits,
                               &cmd->dist_prefix_,
                               &cmd->dist_extra_);
    }
  }
}

static BROTLI_BOOL ComputeDistanceCost(const Command* cmds,
                                       size_t num_commands,
                                       const BrotliDistanceParams* orig_params,
                                       const BrotliDistanceParams* new_params,
                                       double* cost,
                                       HistogramDistance* tmp) {
  size_t i;
  BROTLI_BOOL equal_params = BROTLI_FALSE;
  uint16_t dist_prefix;
  uint32_t dist_extra;
  double extra_bits = 0.0;
  HistogramClearDistance(tmp);

  if (orig_params->distance_postfix_bits == new_params->distance_postfix_bits &&
      orig_params->num_direct_distance_codes ==
      new_params->num_direct_distance_codes) {
    equal_params = BROTLI_TRUE;
  }

  for (i = 0; i < num_commands; i++) {
    const Command* cmd = &cmds[i];
    if (CommandCopyLen(cmd) && cmd->cmd_prefix_ >= 128) {
      if (equal_params) {
        dist_prefix = cmd->dist_prefix_;
      } else {
        uint32_t distance = CommandRestoreDistanceCode(cmd, orig_params);
        if (distance > new_params->max_distance) {
          return BROTLI_FALSE;
        }
        PrefixEncodeCopyDistance(distance,
                                 new_params->num_direct_distance_codes,
                                 new_params->distance_postfix_bits,
                                 &dist_prefix,
                                 &dist_extra);
      }
      HistogramAddDistance(tmp, dist_prefix & 0x3FF);
      extra_bits += dist_prefix >> 10;
    }
  }

  *cost = BrotliPopulationCostDistance(tmp) + extra_bits;
  return BROTLI_TRUE;
}

void BrotliBuildMetaBlock(MemoryManager* m,
                          const uint8_t* ringbuffer,
                          const size_t pos,
                          const size_t mask,
                          BrotliEncoderParams* params,
                          uint8_t prev_byte,
                          uint8_t prev_byte2,
                          Command* cmds,
                          size_t num_commands,
                          ContextType literal_context_mode,
                          MetaBlockSplit* mb) {
  /* Histogram ids need to fit in one byte. */
  static const size_t kMaxNumberOfHistograms = 256;
  HistogramDistance* distance_histograms;
  HistogramLiteral* literal_histograms;
  ContextType* literal_context_modes = NULL;
  size_t literal_histograms_size;
  size_t distance_histograms_size;
  size_t i;
  size_t literal_context_multiplier = 1;
  uint32_t npostfix;
  uint32_t ndirect_msb = 0;
  BROTLI_BOOL check_orig = BROTLI_TRUE;
  double best_dist_cost = 1e99;
  BrotliDistanceParams orig_params = params->dist;
  BrotliDistanceParams new_params = params->dist;
  HistogramDistance* tmp = BROTLI_ALLOC(m, HistogramDistance, 1);

  if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(tmp)) return;

  for (npostfix = 0; npostfix <= BROTLI_MAX_NPOSTFIX; npostfix++) {
    for (; ndirect_msb < 16; ndirect_msb++) {
      uint32_t ndirect = ndirect_msb << npostfix;
      BROTLI_BOOL skip;
      double dist_cost;
      BrotliInitDistanceParams(&new_params, npostfix, ndirect,
                               params->large_window);
      if (npostfix == orig_params.distance_postfix_bits &&
          ndirect == orig_params.num_direct_distance_codes) {
        check_orig = BROTLI_FALSE;
      }
      skip = !ComputeDistanceCost(
          cmds, num_commands, &orig_params, &new_params, &dist_cost, tmp);
      if (skip || (dist_cost > best_dist_cost)) {
        break;
      }
      best_dist_cost = dist_cost;
      params->dist = new_params;
    }
    if (ndirect_msb > 0) ndirect_msb--;
    ndirect_msb /= 2;
  }
  if (check_orig) {
    double dist_cost;
    ComputeDistanceCost(cmds, num_commands, &orig_params, &orig_params,
                        &dist_cost, tmp);
    if (dist_cost < best_dist_cost) {
      /* NB: currently unused; uncomment when more param tuning is added. */
      /* best_dist_cost = dist_cost; */
      params->dist = orig_params;
    }
  }
  BROTLI_FREE(m, tmp);
  RecomputeDistancePrefixes(cmds, num_commands, &orig_params, &params->dist);

  BrotliSplitBlock(m, cmds, num_commands,
                   ringbuffer, pos, mask, params,
                   &mb->literal_split,
                   &mb->command_split,
                   &mb->distance_split);
  if (BROTLI_IS_OOM(m)) return;

  if (!params->disable_literal_context_modeling) {
    literal_context_multiplier = 1 << BROTLI_LITERAL_CONTEXT_BITS;
    literal_context_modes =
        BROTLI_ALLOC(m, ContextType, mb->literal_split.num_types);
    if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(literal_context_modes)) return;
    for (i = 0; i < mb->literal_split.num_types; ++i) {
      literal_context_modes[i] = literal_context_mode;
    }
  }

  literal_histograms_size =
      mb->literal_split.num_types * literal_context_multiplier;
  literal_histograms =
      BROTLI_ALLOC(m, HistogramLiteral, literal_histograms_size);
  if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(literal_histograms)) return;
  ClearHistogramsLiteral(literal_histograms, literal_histograms_size);

  distance_histograms_size =
      mb->distance_split.num_types << BROTLI_DISTANCE_CONTEXT_BITS;
  distance_histograms =
      BROTLI_ALLOC(m, HistogramDistance, distance_histograms_size);
  if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(distance_histograms)) return;
  ClearHistogramsDistance(distance_histograms, distance_histograms_size);

  BROTLI_DCHECK(mb->command_histograms == 0);
  mb->command_histograms_size = mb->command_split.num_types;
  mb->command_histograms =
      BROTLI_ALLOC(m, HistogramCommand, mb->command_histograms_size);
  if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(mb->command_histograms)) return;
  ClearHistogramsCommand(mb->command_histograms, mb->command_histograms_size);

  BrotliBuildHistogramsWithContext(cmds, num_commands,
      &mb->literal_split, &mb->command_split, &mb->distance_split,
      ringbuffer, pos, mask, prev_byte, prev_byte2, literal_context_modes,
      literal_histograms, mb->command_histograms, distance_histograms);
  BROTLI_FREE(m, literal_context_modes);

  BROTLI_DCHECK(mb->literal_context_map == 0);
  mb->literal_context_map_size =
      mb->literal_split.num_types << BROTLI_LITERAL_CONTEXT_BITS;
  mb->literal_context_map =
      BROTLI_ALLOC(m, uint32_t, mb->literal_context_map_size);
  if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(mb->literal_context_map)) return;

  BROTLI_DCHECK(mb->literal_histograms == 0);
  mb->literal_histograms_size = mb->literal_context_map_size;
  mb->literal_histograms =
      BROTLI_ALLOC(m, HistogramLiteral, mb->literal_histograms_size);
  if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(mb->literal_histograms)) return;

  BrotliClusterHistogramsLiteral(m, literal_histograms, literal_histograms_size,
      kMaxNumberOfHistograms, mb->literal_histograms,
      &mb->literal_histograms_size, mb->literal_context_map);
  if (BROTLI_IS_OOM(m)) return;
  BROTLI_FREE(m, literal_histograms);

  if (params->disable_literal_context_modeling) {
    /* Distribute assignment to all contexts. */
    for (i = mb->literal_split.num_types; i != 0;) {
      size_t j = 0;
      i--;
      for (; j < (1 << BROTLI_LITERAL_CONTEXT_BITS); j++) {
        mb->literal_context_map[(i << BROTLI_LITERAL_CONTEXT_BITS) + j] =
            mb->literal_context_map[i];
      }
    }
  }

  BROTLI_DCHECK(mb->distance_context_map == 0);
  mb->distance_context_map_size =
      mb->distance_split.num_types << BROTLI_DISTANCE_CONTEXT_BITS;
  mb->distance_context_map =
      BROTLI_ALLOC(m, uint32_t, mb->distance_context_map_size);
  if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(mb->distance_context_map)) return;

  BROTLI_DCHECK(mb->distance_histograms == 0);
  mb->distance_histograms_size = mb->distance_context_map_size;
  mb->distance_histograms =
      BROTLI_ALLOC(m, HistogramDistance, mb->distance_histograms_size);
  if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(mb->distance_histograms)) return;

  BrotliClusterHistogramsDistance(m, distance_histograms,
                                  mb->distance_context_map_size,
                                  kMaxNumberOfHistograms,
                                  mb->distance_histograms,
                                  &mb->distance_histograms_size,
                                  mb->distance_context_map);
  if (BROTLI_IS_OOM(m)) return;
  BROTLI_FREE(m, distance_histograms);
}

#define FN(X) X ## Literal
#include "metablock_inc.h"  /* NOLINT(build/include) */
#undef FN

#define FN(X) X ## Command
#include "metablock_inc.h"  /* NOLINT(build/include) */
#undef FN

#define FN(X) X ## Distance
#include "metablock_inc.h"  /* NOLINT(build/include) */
#undef FN

#define BROTLI_MAX_STATIC_CONTEXTS 13

/* Greedy block splitter for one block category (literal, command or distance).
   Gathers histograms for all context buckets. */
typedef struct ContextBlockSplitter {
  /* Alphabet size of particular block category. */
  size_t alphabet_size_;
  size_t num_contexts_;
  size_t max_block_types_;
  /* We collect at least this many symbols for each block. */
  size_t min_block_size_;
  /* We merge histograms A and B if
       entropy(A+B) < entropy(A) + entropy(B) + split_threshold_,
     where A is the current histogram and B is the histogram of the last or the
     second last block type. */
  double split_threshold_;

  size_t num_blocks_;
  BlockSplit* split_;  /* not owned */
  HistogramLiteral* histograms_;  /* not owned */
  size_t* histograms_size_;  /* not owned */

  /* The number of symbols that we want to collect before deciding on whether
     or not to merge the block with a previous one or emit a new block. */
  size_t target_block_size_;
  /* The number of symbols in the current histogram. */
  size_t block_size_;
  /* Offset of the current histogram. */
  size_t curr_histogram_ix_;
  /* Offset of the histograms of the previous two block types. */
  size_t last_histogram_ix_[2];
  /* Entropy of the previous two block types. */
  double last_entropy_[2 * BROTLI_MAX_STATIC_CONTEXTS];
  /* The number of times we merged the current block with the last one. */
  size_t merge_last_count_;
} ContextBlockSplitter;

static void InitContextBlockSplitter(
    MemoryManager* m, ContextBlockSplitter* self, size_t alphabet_size,
    size_t num_contexts, size_t min_block_size, double split_threshold,
    size_t num_symbols, BlockSplit* split, HistogramLiteral** histograms,
    size_t* histograms_size) {
  size_t max_num_blocks = num_symbols / min_block_size + 1;
  size_t max_num_types;
  BROTLI_DCHECK(num_contexts <= BROTLI_MAX_STATIC_CONTEXTS);

  self->alphabet_size_ = alphabet_size;
  self->num_contexts_ = num_contexts;
  self->max_block_types_ = BROTLI_MAX_NUMBER_OF_BLOCK_TYPES / num_contexts;
  self->min_block_size_ = min_block_size;
  self->split_threshold_ = split_threshold;
  self->num_blocks_ = 0;
  self->split_ = split;
  self->histograms_size_ = histograms_size;
  self->target_block_size_ = min_block_size;
  self->block_size_ = 0;
  self->curr_histogram_ix_ = 0;
  self->merge_last_count_ = 0;

  /* We have to allocate one more histogram than the maximum number of block
     types for the current histogram when the meta-block is too big. */
  max_num_types =
      BROTLI_MIN(size_t, max_num_blocks, self->max_block_types_ + 1);
  BROTLI_ENSURE_CAPACITY(m, uint8_t,
      split->types, split->types_alloc_size, max_num_blocks);
  BROTLI_ENSURE_CAPACITY(m, uint32_t,
      split->lengths, split->lengths_alloc_size, max_num_blocks);
  if (BROTLI_IS_OOM(m)) return;
  split->num_blocks = max_num_blocks;
  if (BROTLI_IS_OOM(m)) return;
  BROTLI_DCHECK(*histograms == 0);
  *histograms_size = max_num_types * num_contexts;
  *histograms = BROTLI_ALLOC(m, HistogramLiteral, *histograms_size);
  self->histograms_ = *histograms;
  if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(*histograms)) return;
  /* Clear only current histogram. */
  ClearHistogramsLiteral(&self->histograms_[0], num_contexts);
  self->last_histogram_ix_[0] = self->last_histogram_ix_[1] = 0;
}

/* Does either of three things:
     (1) emits the current block with a new block type;
     (2) emits the current block with the type of the second last block;
     (3) merges the current block with the last block. */
static void ContextBlockSplitterFinishBlock(
    ContextBlockSplitter* self, MemoryManager* m, BROTLI_BOOL is_final) {
  BlockSplit* split = self->split_;
  const size_t num_contexts = self->num_contexts_;
  double* last_entropy = self->last_entropy_;
  HistogramLiteral* histograms = self->histograms_;

  if (self->block_size_ < self->min_block_size_) {
    self->block_size_ = self->min_block_size_;
  }
  if (self->num_blocks_ == 0) {
    size_t i;
    /* Create first block. */
    split->lengths[0] = (uint32_t)self->block_size_;
    split->types[0] = 0;

    for (i = 0; i < num_contexts; ++i) {
      last_entropy[i] =
          BitsEntropy(histograms[i].data_, self->alphabet_size_);
      last_entropy[num_contexts + i] = last_entropy[i];
    }
    ++self->num_blocks_;
    ++split->num_types;
    self->curr_histogram_ix_ += num_contexts;
    if (self->curr_histogram_ix_ < *self->histograms_size_) {
      ClearHistogramsLiteral(
          &self->histograms_[self->curr_histogram_ix_], self->num_contexts_);
    }
    self->block_size_ = 0;
  } else if (self->block_size_ > 0) {
    /* Try merging the set of histograms for the current block type with the
       respective set of histograms for the last and second last block types.
       Decide over the split based on the total reduction of entropy across
       all contexts. */
    double entropy[BROTLI_MAX_STATIC_CONTEXTS];
    HistogramLiteral* combined_histo =
        BROTLI_ALLOC(m, HistogramLiteral, 2 * num_contexts);
    double combined_entropy[2 * BROTLI_MAX_STATIC_CONTEXTS];
    double diff[2] = { 0.0 };
    size_t i;
    if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(combined_histo)) return;
    for (i = 0; i < num_contexts; ++i) {
      size_t curr_histo_ix = self->curr_histogram_ix_ + i;
      size_t j;
      entropy[i] = BitsEntropy(histograms[curr_histo_ix].data_,
                               self->alphabet_size_);
      for (j = 0; j < 2; ++j) {
        size_t jx = j * num_contexts + i;
        size_t last_histogram_ix = self->last_histogram_ix_[j] + i;
        combined_histo[jx] = histograms[curr_histo_ix];
        HistogramAddHistogramLiteral(&combined_histo[jx],
            &histograms[last_histogram_ix]);
        combined_entropy[jx] = BitsEntropy(
            &combined_histo[jx].data_[0], self->alphabet_size_);
        diff[j] += combined_entropy[jx] - entropy[i] - last_entropy[jx];
      }
    }

    if (split->num_types < self->max_block_types_ &&
        diff[0] > self->split_threshold_ &&
        diff[1] > self->split_threshold_) {
      /* Create new block. */
      split->lengths[self->num_blocks_] = (uint32_t)self->block_size_;
      split->types[self->num_blocks_] = (uint8_t)split->num_types;
      self->last_histogram_ix_[1] = self->last_histogram_ix_[0];
      self->last_histogram_ix_[0] = split->num_types * num_contexts;
      for (i = 0; i < num_contexts; ++i) {
        last_entropy[num_contexts + i] = last_entropy[i];
        last_entropy[i] = entropy[i];
      }
      ++self->num_blocks_;
      ++split->num_types;
      self->curr_histogram_ix_ += num_contexts;
      if (self->curr_histogram_ix_ < *self->histograms_size_) {
        ClearHistogramsLiteral(
            &self->histograms_[self->curr_histogram_ix_], self->num_contexts_);
      }
      self->block_size_ = 0;
      self->merge_last_count_ = 0;
      self->target_block_size_ = self->min_block_size_;
    } else if (diff[1] < diff[0] - 20.0) {
      /* Combine this block with second last block. */
      split->lengths[self->num_blocks_] = (uint32_t)self->block_size_;
      split->types[self->num_blocks_] = split->types[self->num_blocks_ - 2];
      BROTLI_SWAP(size_t, self->last_histogram_ix_, 0, 1);
      for (i = 0; i < num_contexts; ++i) {
        histograms[self->last_histogram_ix_[0] + i] =
            combined_histo[num_contexts + i];
        last_entropy[num_contexts + i] = last_entropy[i];
        last_entropy[i] = combined_entropy[num_contexts + i];
        HistogramClearLiteral(&histograms[self->curr_histogram_ix_ + i]);
      }
      ++self->num_blocks_;
      self->block_size_ = 0;
      self->merge_last_count_ = 0;
      self->target_block_size_ = self->min_block_size_;
    } else {
      /* Combine this block with last block. */
      split->lengths[self->num_blocks_ - 1] += (uint32_t)self->block_size_;
      for (i = 0; i < num_contexts; ++i) {
        histograms[self->last_histogram_ix_[0] + i] = combined_histo[i];
        last_entropy[i] = combined_entropy[i];
        if (split->num_types == 1) {
          last_entropy[num_contexts + i] = last_entropy[i];
        }
        HistogramClearLiteral(&histograms[self->curr_histogram_ix_ + i]);
      }
      self->block_size_ = 0;
      if (++self->merge_last_count_ > 1) {
        self->target_block_size_ += self->min_block_size_;
      }
    }
    BROTLI_FREE(m, combined_histo);
  }
  if (is_final) {
    *self->histograms_size_ = split->num_types * num_contexts;
    split->num_blocks = self->num_blocks_;
  }
}

/* Adds the next symbol to the current block type and context. When the
   current block reaches the target size, decides on merging the block. */
static void ContextBlockSplitterAddSymbol(
    ContextBlockSplitter* self, MemoryManager* m,
    size_t symbol, size_t context) {
  HistogramAddLiteral(&self->histograms_[self->curr_histogram_ix_ + context],
      symbol);
  ++self->block_size_;
  if (self->block_size_ == self->target_block_size_) {
    ContextBlockSplitterFinishBlock(self, m, /* is_final = */ BROTLI_FALSE);
    if (BROTLI_IS_OOM(m)) return;
  }
}

static void MapStaticContexts(MemoryManager* m,
                              size_t num_contexts,
                              const uint32_t* static_context_map,
                              MetaBlockSplit* mb) {
  size_t i;
  BROTLI_DCHECK(mb->literal_context_map == 0);
  mb->literal_context_map_size =
      mb->literal_split.num_types << BROTLI_LITERAL_CONTEXT_BITS;
  mb->literal_context_map =
      BROTLI_ALLOC(m, uint32_t, mb->literal_context_map_size);
  if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(mb->literal_context_map)) return;

  for (i = 0; i < mb->literal_split.num_types; ++i) {
    uint32_t offset = (uint32_t)(i * num_contexts);
    size_t j;
    for (j = 0; j < (1u << BROTLI_LITERAL_CONTEXT_BITS); ++j) {
      mb->literal_context_map[(i << BROTLI_LITERAL_CONTEXT_BITS) + j] =
          offset + static_context_map[j];
    }
  }
}

typedef struct GreedyMetablockArena {
  union {
    BlockSplitterLiteral plain;
    ContextBlockSplitter ctx;
  } lit_blocks;
  BlockSplitterCommand cmd_blocks;
  BlockSplitterDistance dist_blocks;
} GreedyMetablockArena;

static BROTLI_INLINE void BrotliBuildMetaBlockGreedyInternal(
    MemoryManager* m, GreedyMetablockArena* arena, const uint8_t* ringbuffer,
    size_t pos, size_t mask, uint8_t prev_byte, uint8_t prev_byte2,
    ContextLut literal_context_lut, const size_t num_contexts,
    const uint32_t* static_context_map, const Command* commands,
    size_t n_commands, MetaBlockSplit* mb) {
  size_t num_literals = 0;
  size_t i;
  for (i = 0; i < n_commands; ++i) {
    num_literals += commands[i].insert_len_;
  }

  if (num_contexts == 1) {
    InitBlockSplitterLiteral(m, &arena->lit_blocks.plain, 256, 512, 400.0,
        num_literals, &mb->literal_split, &mb->literal_histograms,
        &mb->literal_histograms_size);
  } else {
    InitContextBlockSplitter(m, &arena->lit_blocks.ctx, 256, num_contexts, 512,
        400.0, num_literals, &mb->literal_split, &mb->literal_histograms,
        &mb->literal_histograms_size);
  }
  if (BROTLI_IS_OOM(m)) return;
  InitBlockSplitterCommand(m, &arena->cmd_blocks, BROTLI_NUM_COMMAND_SYMBOLS,
      1024, 500.0, n_commands, &mb->command_split, &mb->command_histograms,
      &mb->command_histograms_size);
  if (BROTLI_IS_OOM(m)) return;
  InitBlockSplitterDistance(m, &arena->dist_blocks, 64, 512, 100.0, n_commands,
      &mb->distance_split, &mb->distance_histograms,
      &mb->distance_histograms_size);
  if (BROTLI_IS_OOM(m)) return;

  for (i = 0; i < n_commands; ++i) {
    const Command cmd = commands[i];
    size_t j;
    BlockSplitterAddSymbolCommand(&arena->cmd_blocks, cmd.cmd_prefix_);
    for (j = cmd.insert_len_; j != 0; --j) {
      uint8_t literal = ringbuffer[pos & mask];
      if (num_contexts == 1) {
        BlockSplitterAddSymbolLiteral(&arena->lit_blocks.plain, literal);
      } else {
        size_t context =
            BROTLI_CONTEXT(prev_byte, prev_byte2, literal_context_lut);
        ContextBlockSplitterAddSymbol(&arena->lit_blocks.ctx, m, literal,
                                      static_context_map[context]);
        if (BROTLI_IS_OOM(m)) return;
      }
      prev_byte2 = prev_byte;
      prev_byte = literal;
      ++pos;
    }
    pos += CommandCopyLen(&cmd);
    if (CommandCopyLen(&cmd)) {
      prev_byte2 = ringbuffer[(pos - 2) & mask];
      prev_byte = ringbuffer[(pos - 1) & mask];
      if (cmd.cmd_prefix_ >= 128) {
        BlockSplitterAddSymbolDistance(
            &arena->dist_blocks, cmd.dist_prefix_ & 0x3FF);
      }
    }
  }

  if (num_contexts == 1) {
    BlockSplitterFinishBlockLiteral(
        &arena->lit_blocks.plain, /* is_final = */ BROTLI_TRUE);
  } else {
    ContextBlockSplitterFinishBlock(
        &arena->lit_blocks.ctx, m, /* is_final = */ BROTLI_TRUE);
    if (BROTLI_IS_OOM(m)) return;
  }
  BlockSplitterFinishBlockCommand(
      &arena->cmd_blocks, /* is_final = */ BROTLI_TRUE);
  BlockSplitterFinishBlockDistance(
      &arena->dist_blocks, /* is_final = */ BROTLI_TRUE);

  if (num_contexts > 1) {
    MapStaticContexts(m, num_contexts, static_context_map, mb);
  }
}

void BrotliBuildMetaBlockGreedy(MemoryManager* m,
                                const uint8_t* ringbuffer,
                                size_t pos,
                                size_t mask,
                                uint8_t prev_byte,
                                uint8_t prev_byte2,
                                ContextLut literal_context_lut,
                                size_t num_contexts,
                                const uint32_t* static_context_map,
                                const Command* commands,
                                size_t n_commands,
                                MetaBlockSplit* mb) {
  GreedyMetablockArena* arena = BROTLI_ALLOC(m, GreedyMetablockArena, 1);
  if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(arena)) return;
  if (num_contexts == 1) {
    BrotliBuildMetaBlockGreedyInternal(m, arena, ringbuffer, pos, mask,
        prev_byte, prev_byte2, literal_context_lut, 1, NULL, commands,
        n_commands, mb);
  } else {
    BrotliBuildMetaBlockGreedyInternal(m, arena, ringbuffer, pos, mask,
        prev_byte, prev_byte2, literal_context_lut, num_contexts,
        static_context_map, commands, n_commands, mb);
  }
  BROTLI_FREE(m, arena);
}

void BrotliOptimizeHistograms(uint32_t num_distance_codes,
                              MetaBlockSplit* mb) {
  uint8_t good_for_rle[BROTLI_NUM_COMMAND_SYMBOLS];
  size_t i;
  for (i = 0; i < mb->literal_histograms_size; ++i) {
    BrotliOptimizeHuffmanCountsForRle(256, mb->literal_histograms[i].data_,
                                      good_for_rle);
  }
  for (i = 0; i < mb->command_histograms_size; ++i) {
    BrotliOptimizeHuffmanCountsForRle(BROTLI_NUM_COMMAND_SYMBOLS,
                                      mb->command_histograms[i].data_,
                                      good_for_rle);
  }
  for (i = 0; i < mb->distance_histograms_size; ++i) {
    BrotliOptimizeHuffmanCountsForRle(num_distance_codes,
                                      mb->distance_histograms[i].data_,
                                      good_for_rle);
  }
}

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif
