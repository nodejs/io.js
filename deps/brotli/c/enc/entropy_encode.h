/* Copyright 2010 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Entropy encoding (Huffman) utilities. */

#ifndef BROTLI_ENC_ENTROPY_ENCODE_H_
#define BROTLI_ENC_ENTROPY_ENCODE_H_

#include <brotli/types.h>

#include "../common/platform.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* A node of a Huffman tree. */
typedef struct HuffmanTree {
  uint32_t total_count_;
  int16_t index_left_;
  int16_t index_right_or_value_;
} HuffmanTree;

static BROTLI_INLINE void InitHuffmanTree(HuffmanTree* self, uint32_t count,
    int16_t left, int16_t right) {
  self->total_count_ = count;
  self->index_left_ = left;
  self->index_right_or_value_ = right;
}

/* Returns 1 is assignment of depths succeeded, otherwise 0. */
BROTLI_INTERNAL BROTLI_BOOL BrotliSetDepth(
    int p, HuffmanTree* pool, uint8_t* depth, int max_depth);

/* This function will create a Huffman tree.

   The (data,length) contains the population counts.
   The tree_limit is the maximum bit depth of the Huffman codes.

   The depth contains the tree, i.e., how many bits are used for
   the symbol.

   The actual Huffman tree is constructed in the tree[] array, which has to
   be at least 2 * length + 1 long.

   See http://en.wikipedia.org/wiki/Huffman_coding */
BROTLI_INTERNAL void BrotliCreateHuffmanTree(const uint32_t* data,
                                             const size_t length,
                                             const int tree_limit,
                                             HuffmanTree* tree,
                                             uint8_t* depth);

/* Change the population counts in a way that the consequent
   Huffman tree compression, especially its RLE-part will be more
   likely to compress this data more efficiently.

   length contains the size of the histogram.
   counts contains the population counts.
   good_for_rle is a buffer of at least length size */
BROTLI_INTERNAL void BrotliOptimizeHuffmanCountsForRle(
    size_t length, uint32_t* counts, uint8_t* good_for_rle);

/* Write a Huffman tree from bit depths into the bit-stream representation
   of a Huffman tree. The generated Huffman tree is to be compressed once
   more using a Huffman tree */
BROTLI_INTERNAL void BrotliWriteHuffmanTree(const uint8_t* depth,
                                            size_t num,
                                            size_t* tree_size,
                                            uint8_t* tree,
                                            uint8_t* extra_bits_data);

/* Get the actual bit values for a tree of bit depths. */
BROTLI_INTERNAL void BrotliConvertBitDepthsToSymbols(const uint8_t* depth,
                                                     size_t len,
                                                     uint16_t* bits);

BROTLI_INTERNAL extern const size_t kBrotliShellGaps[6];
/* Input size optimized Shell sort. */
typedef BROTLI_BOOL (*HuffmanTreeComparator)(
    const HuffmanTree*, const HuffmanTree*);
static BROTLI_INLINE void SortHuffmanTreeItems(HuffmanTree* items,
    const size_t n, HuffmanTreeComparator comparator) {
  if (n < 13) {
    /* Insertion sort. */
    size_t i;
    for (i = 1; i < n; ++i) {
      HuffmanTree tmp = items[i];
      size_t k = i;
      size_t j = i - 1;
      while (comparator(&tmp, &items[j])) {
        items[k] = items[j];
        k = j;
        if (!j--) break;
      }
      items[k] = tmp;
    }
    return;
  } else {
    /* Shell sort. */
    int g = n < 57 ? 2 : 0;
    for (; g < 6; ++g) {
      size_t gap = kBrotliShellGaps[g];
      size_t i;
      for (i = gap; i < n; ++i) {
        size_t j = i;
        HuffmanTree tmp = items[i];
        for (; j >= gap && comparator(&tmp, &items[j - gap]); j -= gap) {
          items[j] = items[j - gap];
        }
        items[j] = tmp;
      }
    }
  }
}

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_ENC_ENTROPY_ENCODE_H_ */
