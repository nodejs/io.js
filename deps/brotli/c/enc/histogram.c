/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Build per-context histograms of literals, commands and distance codes. */

#include "histogram.h"

#include "../common/context.h"
#include "block_splitter.h"
#include "command.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

typedef struct BlockSplitIterator {
  const BlockSplit* split_;  /* Not owned. */
  size_t idx_;
  size_t type_;
  size_t length_;
} BlockSplitIterator;

static void InitBlockSplitIterator(BlockSplitIterator* self,
    const BlockSplit* split) {
  self->split_ = split;
  self->idx_ = 0;
  self->type_ = 0;
  self->length_ = split->lengths ? split->lengths[0] : 0;
}

static void BlockSplitIteratorNext(BlockSplitIterator* self) {
  if (self->length_ == 0) {
    ++self->idx_;
    self->type_ = self->split_->types[self->idx_];
    self->length_ = self->split_->lengths[self->idx_];
  }
  --self->length_;
}

void BrotliBuildHistogramsWithContext(
    const Command* cmds, const size_t num_commands,
    const BlockSplit* literal_split, const BlockSplit* insert_and_copy_split,
    const BlockSplit* dist_split, const uint8_t* ringbuffer, size_t start_pos,
    size_t mask, uint8_t prev_byte, uint8_t prev_byte2,
    const ContextType* context_modes, HistogramLiteral* literal_histograms,
    HistogramCommand* insert_and_copy_histograms,
    HistogramDistance* copy_dist_histograms) {
  size_t pos = start_pos;
  BlockSplitIterator literal_it;
  BlockSplitIterator insert_and_copy_it;
  BlockSplitIterator dist_it;
  size_t i;

  InitBlockSplitIterator(&literal_it, literal_split);
  InitBlockSplitIterator(&insert_and_copy_it, insert_and_copy_split);
  InitBlockSplitIterator(&dist_it, dist_split);
  for (i = 0; i < num_commands; ++i) {
    const Command* cmd = &cmds[i];
    size_t j;
    BlockSplitIteratorNext(&insert_and_copy_it);
    HistogramAddCommand(&insert_and_copy_histograms[insert_and_copy_it.type_],
        cmd->cmd_prefix_);
    /* TODO(eustas): unwrap iterator blocks. */
    for (j = cmd->insert_len_; j != 0; --j) {
      size_t context;
      BlockSplitIteratorNext(&literal_it);
      context = literal_it.type_;
      if (context_modes) {
        ContextLut lut = BROTLI_CONTEXT_LUT(context_modes[context]);
        context = (context << BROTLI_LITERAL_CONTEXT_BITS) +
            BROTLI_CONTEXT(prev_byte, prev_byte2, lut);
      }
      HistogramAddLiteral(&literal_histograms[context],
          ringbuffer[pos & mask]);
      prev_byte2 = prev_byte;
      prev_byte = ringbuffer[pos & mask];
      ++pos;
    }
    pos += CommandCopyLen(cmd);
    if (CommandCopyLen(cmd)) {
      prev_byte2 = ringbuffer[(pos - 2) & mask];
      prev_byte = ringbuffer[(pos - 1) & mask];
      if (cmd->cmd_prefix_ >= 128) {
        size_t context;
        BlockSplitIteratorNext(&dist_it);
        context = (dist_it.type_ << BROTLI_DISTANCE_CONTEXT_BITS) +
            CommandDistanceContext(cmd);
        HistogramAddDistance(&copy_dist_histograms[context],
            cmd->dist_prefix_ & 0x3FF);
      }
    }
  }
}

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif
