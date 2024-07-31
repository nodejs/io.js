/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Models the histograms of literals, commands and distance codes. */

#ifndef BROTLI_ENC_HISTOGRAM_H_
#define BROTLI_ENC_HISTOGRAM_H_

#include <string.h>  /* memset */

#include <brotli/types.h>

#include "../common/constants.h"
#include "../common/context.h"
#include "../common/platform.h"
#include "block_splitter.h"
#include "command.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* The distance symbols effectively used by "Large Window Brotli" (32-bit). */
#define BROTLI_NUM_HISTOGRAM_DISTANCE_SYMBOLS 544

#define FN(X) X ## Literal
#define DATA_SIZE BROTLI_NUM_LITERAL_SYMBOLS
#define DataType uint8_t
#include "histogram_inc.h"  /* NOLINT(build/include) */
#undef DataType
#undef DATA_SIZE
#undef FN

#define FN(X) X ## Command
#define DataType uint16_t
#define DATA_SIZE BROTLI_NUM_COMMAND_SYMBOLS
#include "histogram_inc.h"  /* NOLINT(build/include) */
#undef DATA_SIZE
#undef FN

#define FN(X) X ## Distance
#define DATA_SIZE BROTLI_NUM_HISTOGRAM_DISTANCE_SYMBOLS
#include "histogram_inc.h"  /* NOLINT(build/include) */
#undef DataType
#undef DATA_SIZE
#undef FN

BROTLI_INTERNAL void BrotliBuildHistogramsWithContext(
    const Command* cmds, const size_t num_commands,
    const BlockSplit* literal_split, const BlockSplit* insert_and_copy_split,
    const BlockSplit* dist_split, const uint8_t* ringbuffer, size_t pos,
    size_t mask, uint8_t prev_byte, uint8_t prev_byte2,
    const ContextType* context_modes, HistogramLiteral* literal_histograms,
    HistogramCommand* insert_and_copy_histograms,
    HistogramDistance* copy_dist_histograms);

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_ENC_HISTOGRAM_H_ */
