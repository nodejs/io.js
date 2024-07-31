/* Copyright 2014 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Functions to convert brotli-related data structures into the
   brotli bit stream. The functions here operate under
   assumption that there is enough space in the storage, i.e., there are
   no out-of-range checks anywhere.

   These functions do bit addressing into a byte array. The byte array
   is called "storage" and the index to the bit is called storage_ix
   in function arguments. */

#ifndef BROTLI_ENC_BROTLI_BIT_STREAM_H_
#define BROTLI_ENC_BROTLI_BIT_STREAM_H_

#include <brotli/types.h>

#include "../common/context.h"
#include "../common/platform.h"
#include "command.h"
#include "entropy_encode.h"
#include "memory.h"
#include "metablock.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* All Store functions here will use a storage_ix, which is always the bit
   position for the current storage. */

BROTLI_INTERNAL void BrotliStoreHuffmanTree(const uint8_t* depths, size_t num,
    HuffmanTree* tree, size_t* storage_ix, uint8_t* storage);

BROTLI_INTERNAL void BrotliBuildAndStoreHuffmanTreeFast(
    HuffmanTree* tree, const uint32_t* histogram, const size_t histogram_total,
    const size_t max_bits, uint8_t* depth, uint16_t* bits, size_t* storage_ix,
    uint8_t* storage);

/* REQUIRES: length > 0 */
/* REQUIRES: length <= (1 << 24) */
BROTLI_INTERNAL void BrotliStoreMetaBlock(MemoryManager* m,
    const uint8_t* input, size_t start_pos, size_t length, size_t mask,
    uint8_t prev_byte, uint8_t prev_byte2, BROTLI_BOOL is_last,
    const BrotliEncoderParams* params, ContextType literal_context_mode,
    const Command* commands, size_t n_commands, const MetaBlockSplit* mb,
    size_t* storage_ix, uint8_t* storage);

/* Stores the meta-block without doing any block splitting, just collects
   one histogram per block category and uses that for entropy coding.
   REQUIRES: length > 0
   REQUIRES: length <= (1 << 24) */
BROTLI_INTERNAL void BrotliStoreMetaBlockTrivial(MemoryManager* m,
    const uint8_t* input, size_t start_pos, size_t length, size_t mask,
    BROTLI_BOOL is_last, const BrotliEncoderParams* params,
    const Command* commands, size_t n_commands,
    size_t* storage_ix, uint8_t* storage);

/* Same as above, but uses static prefix codes for histograms with a only a few
   symbols, and uses static code length prefix codes for all other histograms.
   REQUIRES: length > 0
   REQUIRES: length <= (1 << 24) */
BROTLI_INTERNAL void BrotliStoreMetaBlockFast(MemoryManager* m,
    const uint8_t* input, size_t start_pos, size_t length, size_t mask,
    BROTLI_BOOL is_last, const BrotliEncoderParams* params,
    const Command* commands, size_t n_commands,
    size_t* storage_ix, uint8_t* storage);

/* This is for storing uncompressed blocks (simple raw storage of
   bytes-as-bytes).
   REQUIRES: length > 0
   REQUIRES: length <= (1 << 24) */
BROTLI_INTERNAL void BrotliStoreUncompressedMetaBlock(
    BROTLI_BOOL is_final_block, const uint8_t* BROTLI_RESTRICT input,
    size_t position, size_t mask, size_t len,
    size_t* BROTLI_RESTRICT storage_ix, uint8_t* BROTLI_RESTRICT storage);

#if defined(BROTLI_TEST)
void GetBlockLengthPrefixCodeForTest(uint32_t, size_t*, uint32_t*, uint32_t*);
#endif

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_ENC_BROTLI_BIT_STREAM_H_ */
