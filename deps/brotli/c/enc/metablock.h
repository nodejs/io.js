/* Copyright 2015 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Algorithms for distributing the literals and commands of a metablock between
   block types and contexts. */

#ifndef BROTLI_ENC_METABLOCK_H_
#define BROTLI_ENC_METABLOCK_H_

#include <brotli/types.h>

#include "../common/context.h"
#include "../common/platform.h"
#include "block_splitter.h"
#include "command.h"
#include "histogram.h"
#include "memory.h"
#include "quality.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

typedef struct MetaBlockSplit {
  BlockSplit literal_split;
  BlockSplit command_split;
  BlockSplit distance_split;
  uint32_t* literal_context_map;
  size_t literal_context_map_size;
  uint32_t* distance_context_map;
  size_t distance_context_map_size;
  HistogramLiteral* literal_histograms;
  size_t literal_histograms_size;
  HistogramCommand* command_histograms;
  size_t command_histograms_size;
  HistogramDistance* distance_histograms;
  size_t distance_histograms_size;
} MetaBlockSplit;

static BROTLI_INLINE void InitMetaBlockSplit(MetaBlockSplit* mb) {
  BrotliInitBlockSplit(&mb->literal_split);
  BrotliInitBlockSplit(&mb->command_split);
  BrotliInitBlockSplit(&mb->distance_split);
  mb->literal_context_map = 0;
  mb->literal_context_map_size = 0;
  mb->distance_context_map = 0;
  mb->distance_context_map_size = 0;
  mb->literal_histograms = 0;
  mb->literal_histograms_size = 0;
  mb->command_histograms = 0;
  mb->command_histograms_size = 0;
  mb->distance_histograms = 0;
  mb->distance_histograms_size = 0;
}

static BROTLI_INLINE void DestroyMetaBlockSplit(
    MemoryManager* m, MetaBlockSplit* mb) {
  BrotliDestroyBlockSplit(m, &mb->literal_split);
  BrotliDestroyBlockSplit(m, &mb->command_split);
  BrotliDestroyBlockSplit(m, &mb->distance_split);
  BROTLI_FREE(m, mb->literal_context_map);
  BROTLI_FREE(m, mb->distance_context_map);
  BROTLI_FREE(m, mb->literal_histograms);
  BROTLI_FREE(m, mb->command_histograms);
  BROTLI_FREE(m, mb->distance_histograms);
}

/* Uses the slow shortest-path block splitter and does context clustering.
   The distance parameters are dynamically selected based on the commands
   which get recomputed under the new distance parameters. The new distance
   parameters are stored into *params. */
BROTLI_INTERNAL void BrotliBuildMetaBlock(MemoryManager* m,
                                          const uint8_t* ringbuffer,
                                          const size_t pos,
                                          const size_t mask,
                                          BrotliEncoderParams* params,
                                          uint8_t prev_byte,
                                          uint8_t prev_byte2,
                                          Command* cmds,
                                          size_t num_commands,
                                          ContextType literal_context_mode,
                                          MetaBlockSplit* mb);

/* Uses a fast greedy block splitter that tries to merge current block with the
   last or the second last block and uses a static context clustering which
   is the same for all block types. */
BROTLI_INTERNAL void BrotliBuildMetaBlockGreedy(
    MemoryManager* m, const uint8_t* ringbuffer, size_t pos, size_t mask,
    uint8_t prev_byte, uint8_t prev_byte2, ContextLut literal_context_lut,
    size_t num_contexts, const uint32_t* static_context_map,
    const Command* commands, size_t n_commands, MetaBlockSplit* mb);

BROTLI_INTERNAL void BrotliOptimizeHistograms(uint32_t num_distance_codes,
                                              MetaBlockSplit* mb);

BROTLI_INTERNAL void BrotliInitDistanceParams(BrotliDistanceParams* params,
    uint32_t npostfix, uint32_t ndirect, BROTLI_BOOL large_window);

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_ENC_METABLOCK_H_ */
