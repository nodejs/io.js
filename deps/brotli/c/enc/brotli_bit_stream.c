/* Copyright 2014 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Brotli bit stream functions to support the low level format. There are no
   compression algorithms here, just the right ordering of bits to match the
   specs. */

#include "brotli_bit_stream.h"

#include <string.h>  /* memcpy, memset */

#include <brotli/types.h>

#include "../common/constants.h"
#include "../common/context.h"
#include "../common/platform.h"
#include "entropy_encode.h"
#include "entropy_encode_static.h"
#include "fast_log.h"
#include "histogram.h"
#include "memory.h"
#include "write_bits.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define MAX_HUFFMAN_TREE_SIZE (2 * BROTLI_NUM_COMMAND_SYMBOLS + 1)
/* The maximum size of Huffman dictionary for distances assuming that
   NPOSTFIX = 0 and NDIRECT = 0. */
#define MAX_SIMPLE_DISTANCE_ALPHABET_SIZE \
  BROTLI_DISTANCE_ALPHABET_SIZE(0, 0, BROTLI_LARGE_MAX_DISTANCE_BITS)
/* MAX_SIMPLE_DISTANCE_ALPHABET_SIZE == 140 */

static BROTLI_INLINE uint32_t BlockLengthPrefixCode(uint32_t len) {
  uint32_t code = (len >= 177) ? (len >= 753 ? 20 : 14) : (len >= 41 ? 7 : 0);
  while (code < (BROTLI_NUM_BLOCK_LEN_SYMBOLS - 1) &&
      len >= _kBrotliPrefixCodeRanges[code + 1].offset) ++code;
  return code;
}

static BROTLI_INLINE void GetBlockLengthPrefixCode(uint32_t len, size_t* code,
    uint32_t* n_extra, uint32_t* extra) {
  *code = BlockLengthPrefixCode(len);
  *n_extra = _kBrotliPrefixCodeRanges[*code].nbits;
  *extra = len - _kBrotliPrefixCodeRanges[*code].offset;
}

typedef struct BlockTypeCodeCalculator {
  size_t last_type;
  size_t second_last_type;
} BlockTypeCodeCalculator;

static void InitBlockTypeCodeCalculator(BlockTypeCodeCalculator* self) {
  self->last_type = 1;
  self->second_last_type = 0;
}

static BROTLI_INLINE size_t NextBlockTypeCode(
    BlockTypeCodeCalculator* calculator, uint8_t type) {
  size_t type_code = (type == calculator->last_type + 1) ? 1u :
      (type == calculator->second_last_type) ? 0u : type + 2u;
  calculator->second_last_type = calculator->last_type;
  calculator->last_type = type;
  return type_code;
}

/* |nibblesbits| represents the 2 bits to encode MNIBBLES (0-3)
   REQUIRES: length > 0
   REQUIRES: length <= (1 << 24) */
static void BrotliEncodeMlen(size_t length, uint64_t* bits,
                             size_t* numbits, uint64_t* nibblesbits) {
  size_t lg = (length == 1) ? 1 : Log2FloorNonZero((uint32_t)(length - 1)) + 1;
  size_t mnibbles = (lg < 16 ? 16 : (lg + 3)) / 4;
  BROTLI_DCHECK(length > 0);
  BROTLI_DCHECK(length <= (1 << 24));
  BROTLI_DCHECK(lg <= 24);
  *nibblesbits = mnibbles - 4;
  *numbits = mnibbles * 4;
  *bits = length - 1;
}

static BROTLI_INLINE void StoreCommandExtra(
    const Command* cmd, size_t* storage_ix, uint8_t* storage) {
  uint32_t copylen_code = CommandCopyLenCode(cmd);
  uint16_t inscode = GetInsertLengthCode(cmd->insert_len_);
  uint16_t copycode = GetCopyLengthCode(copylen_code);
  uint32_t insnumextra = GetInsertExtra(inscode);
  uint64_t insextraval = cmd->insert_len_ - GetInsertBase(inscode);
  uint64_t copyextraval = copylen_code - GetCopyBase(copycode);
  uint64_t bits = (copyextraval << insnumextra) | insextraval;
  BrotliWriteBits(
      insnumextra + GetCopyExtra(copycode), bits, storage_ix, storage);
}

/* Data structure that stores almost everything that is needed to encode each
   block switch command. */
typedef struct BlockSplitCode {
  BlockTypeCodeCalculator type_code_calculator;
  uint8_t type_depths[BROTLI_MAX_BLOCK_TYPE_SYMBOLS];
  uint16_t type_bits[BROTLI_MAX_BLOCK_TYPE_SYMBOLS];
  uint8_t length_depths[BROTLI_NUM_BLOCK_LEN_SYMBOLS];
  uint16_t length_bits[BROTLI_NUM_BLOCK_LEN_SYMBOLS];
} BlockSplitCode;

/* Stores a number between 0 and 255. */
static void StoreVarLenUint8(size_t n, size_t* storage_ix, uint8_t* storage) {
  if (n == 0) {
    BrotliWriteBits(1, 0, storage_ix, storage);
  } else {
    size_t nbits = Log2FloorNonZero(n);
    BrotliWriteBits(1, 1, storage_ix, storage);
    BrotliWriteBits(3, nbits, storage_ix, storage);
    BrotliWriteBits(nbits, n - ((size_t)1 << nbits), storage_ix, storage);
  }
}

/* Stores the compressed meta-block header.
   REQUIRES: length > 0
   REQUIRES: length <= (1 << 24) */
static void StoreCompressedMetaBlockHeader(BROTLI_BOOL is_final_block,
                                           size_t length,
                                           size_t* storage_ix,
                                           uint8_t* storage) {
  uint64_t lenbits;
  size_t nlenbits;
  uint64_t nibblesbits;

  /* Write ISLAST bit. */
  BrotliWriteBits(1, (uint64_t)is_final_block, storage_ix, storage);
  /* Write ISEMPTY bit. */
  if (is_final_block) {
    BrotliWriteBits(1, 0, storage_ix, storage);
  }

  BrotliEncodeMlen(length, &lenbits, &nlenbits, &nibblesbits);
  BrotliWriteBits(2, nibblesbits, storage_ix, storage);
  BrotliWriteBits(nlenbits, lenbits, storage_ix, storage);

  if (!is_final_block) {
    /* Write ISUNCOMPRESSED bit. */
    BrotliWriteBits(1, 0, storage_ix, storage);
  }
}

/* Stores the uncompressed meta-block header.
   REQUIRES: length > 0
   REQUIRES: length <= (1 << 24) */
static void BrotliStoreUncompressedMetaBlockHeader(size_t length,
                                                   size_t* storage_ix,
                                                   uint8_t* storage) {
  uint64_t lenbits;
  size_t nlenbits;
  uint64_t nibblesbits;

  /* Write ISLAST bit.
     Uncompressed block cannot be the last one, so set to 0. */
  BrotliWriteBits(1, 0, storage_ix, storage);
  BrotliEncodeMlen(length, &lenbits, &nlenbits, &nibblesbits);
  BrotliWriteBits(2, nibblesbits, storage_ix, storage);
  BrotliWriteBits(nlenbits, lenbits, storage_ix, storage);
  /* Write ISUNCOMPRESSED bit. */
  BrotliWriteBits(1, 1, storage_ix, storage);
}

static void BrotliStoreHuffmanTreeOfHuffmanTreeToBitMask(
    const int num_codes, const uint8_t* code_length_bitdepth,
    size_t* storage_ix, uint8_t* storage) {
  static const uint8_t kStorageOrder[BROTLI_CODE_LENGTH_CODES] = {
    1, 2, 3, 4, 0, 5, 17, 6, 16, 7, 8, 9, 10, 11, 12, 13, 14, 15
  };
  /* The bit lengths of the Huffman code over the code length alphabet
     are compressed with the following static Huffman code:
       Symbol   Code
       ------   ----
       0          00
       1        1110
       2         110
       3          01
       4          10
       5        1111 */
  static const uint8_t kHuffmanBitLengthHuffmanCodeSymbols[6] = {
     0, 7, 3, 2, 1, 15
  };
  static const uint8_t kHuffmanBitLengthHuffmanCodeBitLengths[6] = {
    2, 4, 3, 2, 2, 4
  };

  size_t skip_some = 0;  /* skips none. */

  /* Throw away trailing zeros: */
  size_t codes_to_store = BROTLI_CODE_LENGTH_CODES;
  if (num_codes > 1) {
    for (; codes_to_store > 0; --codes_to_store) {
      if (code_length_bitdepth[kStorageOrder[codes_to_store - 1]] != 0) {
        break;
      }
    }
  }
  if (code_length_bitdepth[kStorageOrder[0]] == 0 &&
      code_length_bitdepth[kStorageOrder[1]] == 0) {
    skip_some = 2;  /* skips two. */
    if (code_length_bitdepth[kStorageOrder[2]] == 0) {
      skip_some = 3;  /* skips three. */
    }
  }
  BrotliWriteBits(2, skip_some, storage_ix, storage);
  {
    size_t i;
    for (i = skip_some; i < codes_to_store; ++i) {
      size_t l = code_length_bitdepth[kStorageOrder[i]];
      BrotliWriteBits(kHuffmanBitLengthHuffmanCodeBitLengths[l],
          kHuffmanBitLengthHuffmanCodeSymbols[l], storage_ix, storage);
    }
  }
}

static void BrotliStoreHuffmanTreeToBitMask(
    const size_t huffman_tree_size, const uint8_t* huffman_tree,
    const uint8_t* huffman_tree_extra_bits, const uint8_t* code_length_bitdepth,
    const uint16_t* code_length_bitdepth_symbols,
    size_t* BROTLI_RESTRICT storage_ix, uint8_t* BROTLI_RESTRICT storage) {
  size_t i;
  for (i = 0; i < huffman_tree_size; ++i) {
    size_t ix = huffman_tree[i];
    BrotliWriteBits(code_length_bitdepth[ix], code_length_bitdepth_symbols[ix],
                    storage_ix, storage);
    /* Extra bits */
    switch (ix) {
      case BROTLI_REPEAT_PREVIOUS_CODE_LENGTH:
        BrotliWriteBits(2, huffman_tree_extra_bits[i], storage_ix, storage);
        break;
      case BROTLI_REPEAT_ZERO_CODE_LENGTH:
        BrotliWriteBits(3, huffman_tree_extra_bits[i], storage_ix, storage);
        break;
    }
  }
}

static void StoreSimpleHuffmanTree(const uint8_t* depths,
                                   size_t symbols[4],
                                   size_t num_symbols,
                                   size_t max_bits,
                                   size_t* storage_ix, uint8_t* storage) {
  /* value of 1 indicates a simple Huffman code */
  BrotliWriteBits(2, 1, storage_ix, storage);
  BrotliWriteBits(2, num_symbols - 1, storage_ix, storage);  /* NSYM - 1 */

  {
    /* Sort */
    size_t i;
    for (i = 0; i < num_symbols; i++) {
      size_t j;
      for (j = i + 1; j < num_symbols; j++) {
        if (depths[symbols[j]] < depths[symbols[i]]) {
          BROTLI_SWAP(size_t, symbols, j, i);
        }
      }
    }
  }

  if (num_symbols == 2) {
    BrotliWriteBits(max_bits, symbols[0], storage_ix, storage);
    BrotliWriteBits(max_bits, symbols[1], storage_ix, storage);
  } else if (num_symbols == 3) {
    BrotliWriteBits(max_bits, symbols[0], storage_ix, storage);
    BrotliWriteBits(max_bits, symbols[1], storage_ix, storage);
    BrotliWriteBits(max_bits, symbols[2], storage_ix, storage);
  } else {
    BrotliWriteBits(max_bits, symbols[0], storage_ix, storage);
    BrotliWriteBits(max_bits, symbols[1], storage_ix, storage);
    BrotliWriteBits(max_bits, symbols[2], storage_ix, storage);
    BrotliWriteBits(max_bits, symbols[3], storage_ix, storage);
    /* tree-select */
    BrotliWriteBits(1, depths[symbols[0]] == 1 ? 1 : 0, storage_ix, storage);
  }
}

/* num = alphabet size
   depths = symbol depths */
void BrotliStoreHuffmanTree(const uint8_t* depths, size_t num,
                            HuffmanTree* tree,
                            size_t* storage_ix, uint8_t* storage) {
  /* Write the Huffman tree into the brotli-representation.
     The command alphabet is the largest, so this allocation will fit all
     alphabets. */
  /* TODO(eustas): fix me */
  uint8_t huffman_tree[BROTLI_NUM_COMMAND_SYMBOLS];
  uint8_t huffman_tree_extra_bits[BROTLI_NUM_COMMAND_SYMBOLS];
  size_t huffman_tree_size = 0;
  uint8_t code_length_bitdepth[BROTLI_CODE_LENGTH_CODES] = { 0 };
  uint16_t code_length_bitdepth_symbols[BROTLI_CODE_LENGTH_CODES];
  uint32_t huffman_tree_histogram[BROTLI_CODE_LENGTH_CODES] = { 0 };
  size_t i;
  int num_codes = 0;
  size_t code = 0;

  BROTLI_DCHECK(num <= BROTLI_NUM_COMMAND_SYMBOLS);

  BrotliWriteHuffmanTree(depths, num, &huffman_tree_size, huffman_tree,
                         huffman_tree_extra_bits);

  /* Calculate the statistics of the Huffman tree in brotli-representation. */
  for (i = 0; i < huffman_tree_size; ++i) {
    ++huffman_tree_histogram[huffman_tree[i]];
  }

  for (i = 0; i < BROTLI_CODE_LENGTH_CODES; ++i) {
    if (huffman_tree_histogram[i]) {
      if (num_codes == 0) {
        code = i;
        num_codes = 1;
      } else if (num_codes == 1) {
        num_codes = 2;
        break;
      }
    }
  }

  /* Calculate another Huffman tree to use for compressing both the
     earlier Huffman tree with. */
  BrotliCreateHuffmanTree(huffman_tree_histogram, BROTLI_CODE_LENGTH_CODES,
                          5, tree, code_length_bitdepth);
  BrotliConvertBitDepthsToSymbols(code_length_bitdepth,
                                  BROTLI_CODE_LENGTH_CODES,
                                  code_length_bitdepth_symbols);

  /* Now, we have all the data, let's start storing it */
  BrotliStoreHuffmanTreeOfHuffmanTreeToBitMask(num_codes, code_length_bitdepth,
                                               storage_ix, storage);

  if (num_codes == 1) {
    code_length_bitdepth[code] = 0;
  }

  /* Store the real Huffman tree now. */
  BrotliStoreHuffmanTreeToBitMask(huffman_tree_size,
                                  huffman_tree,
                                  huffman_tree_extra_bits,
                                  code_length_bitdepth,
                                  code_length_bitdepth_symbols,
                                  storage_ix, storage);
}

/* Builds a Huffman tree from histogram[0:length] into depth[0:length] and
   bits[0:length] and stores the encoded tree to the bit stream. */
static void BuildAndStoreHuffmanTree(const uint32_t* histogram,
                                     const size_t histogram_length,
                                     const size_t alphabet_size,
                                     HuffmanTree* tree,
                                     uint8_t* depth,
                                     uint16_t* bits,
                                     size_t* storage_ix,
                                     uint8_t* storage) {
  size_t count = 0;
  size_t s4[4] = { 0 };
  size_t i;
  size_t max_bits = 0;
  for (i = 0; i < histogram_length; i++) {
    if (histogram[i]) {
      if (count < 4) {
        s4[count] = i;
      } else if (count > 4) {
        break;
      }
      count++;
    }
  }

  {
    size_t max_bits_counter = alphabet_size - 1;
    while (max_bits_counter) {
      max_bits_counter >>= 1;
      ++max_bits;
    }
  }

  if (count <= 1) {
    BrotliWriteBits(4, 1, storage_ix, storage);
    BrotliWriteBits(max_bits, s4[0], storage_ix, storage);
    depth[s4[0]] = 0;
    bits[s4[0]] = 0;
    return;
  }

  memset(depth, 0, histogram_length * sizeof(depth[0]));
  BrotliCreateHuffmanTree(histogram, histogram_length, 15, tree, depth);
  BrotliConvertBitDepthsToSymbols(depth, histogram_length, bits);

  if (count <= 4) {
    StoreSimpleHuffmanTree(depth, s4, count, max_bits, storage_ix, storage);
  } else {
    BrotliStoreHuffmanTree(depth, histogram_length, tree, storage_ix, storage);
  }
}

static BROTLI_INLINE BROTLI_BOOL SortHuffmanTree(
    const HuffmanTree* v0, const HuffmanTree* v1) {
  return TO_BROTLI_BOOL(v0->total_count_ < v1->total_count_);
}

void BrotliBuildAndStoreHuffmanTreeFast(HuffmanTree* tree,
                                        const uint32_t* histogram,
                                        const size_t histogram_total,
                                        const size_t max_bits,
                                        uint8_t* depth, uint16_t* bits,
                                        size_t* storage_ix,
                                        uint8_t* storage) {
  size_t count = 0;
  size_t symbols[4] = { 0 };
  size_t length = 0;
  size_t total = histogram_total;
  while (total != 0) {
    if (histogram[length]) {
      if (count < 4) {
        symbols[count] = length;
      }
      ++count;
      total -= histogram[length];
    }
    ++length;
  }

  if (count <= 1) {
    BrotliWriteBits(4, 1, storage_ix, storage);
    BrotliWriteBits(max_bits, symbols[0], storage_ix, storage);
    depth[symbols[0]] = 0;
    bits[symbols[0]] = 0;
    return;
  }

  memset(depth, 0, length * sizeof(depth[0]));
  {
    uint32_t count_limit;
    for (count_limit = 1; ; count_limit *= 2) {
      HuffmanTree* node = tree;
      size_t l;
      for (l = length; l != 0;) {
        --l;
        if (histogram[l]) {
          if (BROTLI_PREDICT_TRUE(histogram[l] >= count_limit)) {
            InitHuffmanTree(node, histogram[l], -1, (int16_t)l);
          } else {
            InitHuffmanTree(node, count_limit, -1, (int16_t)l);
          }
          ++node;
        }
      }
      {
        const int n = (int)(node - tree);
        HuffmanTree sentinel;
        int i = 0;      /* Points to the next leaf node. */
        int j = n + 1;  /* Points to the next non-leaf node. */
        int k;

        SortHuffmanTreeItems(tree, (size_t)n, SortHuffmanTree);
        /* The nodes are:
           [0, n): the sorted leaf nodes that we start with.
           [n]: we add a sentinel here.
           [n + 1, 2n): new parent nodes are added here, starting from
                        (n+1). These are naturally in ascending order.
           [2n]: we add a sentinel at the end as well.
           There will be (2n+1) elements at the end. */
        InitHuffmanTree(&sentinel, BROTLI_UINT32_MAX, -1, -1);
        *node++ = sentinel;
        *node++ = sentinel;

        for (k = n - 1; k > 0; --k) {
          int left, right;
          if (tree[i].total_count_ <= tree[j].total_count_) {
            left = i;
            ++i;
          } else {
            left = j;
            ++j;
          }
          if (tree[i].total_count_ <= tree[j].total_count_) {
            right = i;
            ++i;
          } else {
            right = j;
            ++j;
          }
          /* The sentinel node becomes the parent node. */
          node[-1].total_count_ =
              tree[left].total_count_ + tree[right].total_count_;
          node[-1].index_left_ = (int16_t)left;
          node[-1].index_right_or_value_ = (int16_t)right;
          /* Add back the last sentinel node. */
          *node++ = sentinel;
        }
        if (BrotliSetDepth(2 * n - 1, tree, depth, 14)) {
          /* We need to pack the Huffman tree in 14 bits. If this was not
             successful, add fake entities to the lowest values and retry. */
          break;
        }
      }
    }
  }
  BrotliConvertBitDepthsToSymbols(depth, length, bits);
  if (count <= 4) {
    size_t i;
    /* value of 1 indicates a simple Huffman code */
    BrotliWriteBits(2, 1, storage_ix, storage);
    BrotliWriteBits(2, count - 1, storage_ix, storage);  /* NSYM - 1 */

    /* Sort */
    for (i = 0; i < count; i++) {
      size_t j;
      for (j = i + 1; j < count; j++) {
        if (depth[symbols[j]] < depth[symbols[i]]) {
          BROTLI_SWAP(size_t, symbols, j, i);
        }
      }
    }

    if (count == 2) {
      BrotliWriteBits(max_bits, symbols[0], storage_ix, storage);
      BrotliWriteBits(max_bits, symbols[1], storage_ix, storage);
    } else if (count == 3) {
      BrotliWriteBits(max_bits, symbols[0], storage_ix, storage);
      BrotliWriteBits(max_bits, symbols[1], storage_ix, storage);
      BrotliWriteBits(max_bits, symbols[2], storage_ix, storage);
    } else {
      BrotliWriteBits(max_bits, symbols[0], storage_ix, storage);
      BrotliWriteBits(max_bits, symbols[1], storage_ix, storage);
      BrotliWriteBits(max_bits, symbols[2], storage_ix, storage);
      BrotliWriteBits(max_bits, symbols[3], storage_ix, storage);
      /* tree-select */
      BrotliWriteBits(1, depth[symbols[0]] == 1 ? 1 : 0, storage_ix, storage);
    }
  } else {
    uint8_t previous_value = 8;
    size_t i;
    /* Complex Huffman Tree */
    StoreStaticCodeLengthCode(storage_ix, storage);

    /* Actual RLE coding. */
    for (i = 0; i < length;) {
      const uint8_t value = depth[i];
      size_t reps = 1;
      size_t k;
      for (k = i + 1; k < length && depth[k] == value; ++k) {
        ++reps;
      }
      i += reps;
      if (value == 0) {
        BrotliWriteBits(kZeroRepsDepth[reps], kZeroRepsBits[reps],
                        storage_ix, storage);
      } else {
        if (previous_value != value) {
          BrotliWriteBits(kCodeLengthDepth[value], kCodeLengthBits[value],
                          storage_ix, storage);
          --reps;
        }
        if (reps < 3) {
          while (reps != 0) {
            reps--;
            BrotliWriteBits(kCodeLengthDepth[value], kCodeLengthBits[value],
                            storage_ix, storage);
          }
        } else {
          reps -= 3;
          BrotliWriteBits(kNonZeroRepsDepth[reps], kNonZeroRepsBits[reps],
                          storage_ix, storage);
        }
        previous_value = value;
      }
    }
  }
}

static size_t IndexOf(const uint8_t* v, size_t v_size, uint8_t value) {
  size_t i = 0;
  for (; i < v_size; ++i) {
    if (v[i] == value) return i;
  }
  return i;
}

static void MoveToFront(uint8_t* v, size_t index) {
  uint8_t value = v[index];
  size_t i;
  for (i = index; i != 0; --i) {
    v[i] = v[i - 1];
  }
  v[0] = value;
}

static void MoveToFrontTransform(const uint32_t* BROTLI_RESTRICT v_in,
                                 const size_t v_size,
                                 uint32_t* v_out) {
  size_t i;
  uint8_t mtf[256];
  uint32_t max_value;
  if (v_size == 0) {
    return;
  }
  max_value = v_in[0];
  for (i = 1; i < v_size; ++i) {
    if (v_in[i] > max_value) max_value = v_in[i];
  }
  BROTLI_DCHECK(max_value < 256u);
  for (i = 0; i <= max_value; ++i) {
    mtf[i] = (uint8_t)i;
  }
  {
    size_t mtf_size = max_value + 1;
    for (i = 0; i < v_size; ++i) {
      size_t index = IndexOf(mtf, mtf_size, (uint8_t)v_in[i]);
      BROTLI_DCHECK(index < mtf_size);
      v_out[i] = (uint32_t)index;
      MoveToFront(mtf, index);
    }
  }
}

/* Finds runs of zeros in v[0..in_size) and replaces them with a prefix code of
   the run length plus extra bits (lower 9 bits is the prefix code and the rest
   are the extra bits). Non-zero values in v[] are shifted by
   *max_length_prefix. Will not create prefix codes bigger than the initial
   value of *max_run_length_prefix. The prefix code of run length L is simply
   Log2Floor(L) and the number of extra bits is the same as the prefix code. */
static void RunLengthCodeZeros(const size_t in_size,
    uint32_t* BROTLI_RESTRICT v, size_t* BROTLI_RESTRICT out_size,
    uint32_t* BROTLI_RESTRICT max_run_length_prefix) {
  uint32_t max_reps = 0;
  size_t i;
  uint32_t max_prefix;
  for (i = 0; i < in_size;) {
    uint32_t reps = 0;
    for (; i < in_size && v[i] != 0; ++i) ;
    for (; i < in_size && v[i] == 0; ++i) {
      ++reps;
    }
    max_reps = BROTLI_MAX(uint32_t, reps, max_reps);
  }
  max_prefix = max_reps > 0 ? Log2FloorNonZero(max_reps) : 0;
  max_prefix = BROTLI_MIN(uint32_t, max_prefix, *max_run_length_prefix);
  *max_run_length_prefix = max_prefix;
  *out_size = 0;
  for (i = 0; i < in_size;) {
    BROTLI_DCHECK(*out_size <= i);
    if (v[i] != 0) {
      v[*out_size] = v[i] + *max_run_length_prefix;
      ++i;
      ++(*out_size);
    } else {
      uint32_t reps = 1;
      size_t k;
      for (k = i + 1; k < in_size && v[k] == 0; ++k) {
        ++reps;
      }
      i += reps;
      while (reps != 0) {
        if (reps < (2u << max_prefix)) {
          uint32_t run_length_prefix = Log2FloorNonZero(reps);
          const uint32_t extra_bits = reps - (1u << run_length_prefix);
          v[*out_size] = run_length_prefix + (extra_bits << 9);
          ++(*out_size);
          break;
        } else {
          const uint32_t extra_bits = (1u << max_prefix) - 1u;
          v[*out_size] = max_prefix + (extra_bits << 9);
          reps -= (2u << max_prefix) - 1u;
          ++(*out_size);
        }
      }
    }
  }
}

#define SYMBOL_BITS 9

typedef struct EncodeContextMapArena {
  uint32_t histogram[BROTLI_MAX_CONTEXT_MAP_SYMBOLS];
  uint8_t depths[BROTLI_MAX_CONTEXT_MAP_SYMBOLS];
  uint16_t bits[BROTLI_MAX_CONTEXT_MAP_SYMBOLS];
} EncodeContextMapArena;

static void EncodeContextMap(MemoryManager* m,
                             EncodeContextMapArena* arena,
                             const uint32_t* context_map,
                             size_t context_map_size,
                             size_t num_clusters,
                             HuffmanTree* tree,
                             size_t* storage_ix, uint8_t* storage) {
  size_t i;
  uint32_t* rle_symbols;
  uint32_t max_run_length_prefix = 6;
  size_t num_rle_symbols = 0;
  uint32_t* BROTLI_RESTRICT const histogram = arena->histogram;
  static const uint32_t kSymbolMask = (1u << SYMBOL_BITS) - 1u;
  uint8_t* BROTLI_RESTRICT const depths = arena->depths;
  uint16_t* BROTLI_RESTRICT const bits = arena->bits;

  StoreVarLenUint8(num_clusters - 1, storage_ix, storage);

  if (num_clusters == 1) {
    return;
  }

  rle_symbols = BROTLI_ALLOC(m, uint32_t, context_map_size);
  if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(rle_symbols)) return;
  MoveToFrontTransform(context_map, context_map_size, rle_symbols);
  RunLengthCodeZeros(context_map_size, rle_symbols,
                     &num_rle_symbols, &max_run_length_prefix);
  memset(histogram, 0, sizeof(arena->histogram));
  for (i = 0; i < num_rle_symbols; ++i) {
    ++histogram[rle_symbols[i] & kSymbolMask];
  }
  {
    BROTLI_BOOL use_rle = TO_BROTLI_BOOL(max_run_length_prefix > 0);
    BrotliWriteBits(1, (uint64_t)use_rle, storage_ix, storage);
    if (use_rle) {
      BrotliWriteBits(4, max_run_length_prefix - 1, storage_ix, storage);
    }
  }
  BuildAndStoreHuffmanTree(histogram, num_clusters + max_run_length_prefix,
                           num_clusters + max_run_length_prefix,
                           tree, depths, bits, storage_ix, storage);
  for (i = 0; i < num_rle_symbols; ++i) {
    const uint32_t rle_symbol = rle_symbols[i] & kSymbolMask;
    const uint32_t extra_bits_val = rle_symbols[i] >> SYMBOL_BITS;
    BrotliWriteBits(depths[rle_symbol], bits[rle_symbol], storage_ix, storage);
    if (rle_symbol > 0 && rle_symbol <= max_run_length_prefix) {
      BrotliWriteBits(rle_symbol, extra_bits_val, storage_ix, storage);
    }
  }
  BrotliWriteBits(1, 1, storage_ix, storage);  /* use move-to-front */
  BROTLI_FREE(m, rle_symbols);
}

/* Stores the block switch command with index block_ix to the bit stream. */
static BROTLI_INLINE void StoreBlockSwitch(BlockSplitCode* code,
                                           const uint32_t block_len,
                                           const uint8_t block_type,
                                           BROTLI_BOOL is_first_block,
                                           size_t* storage_ix,
                                           uint8_t* storage) {
  size_t typecode = NextBlockTypeCode(&code->type_code_calculator, block_type);
  size_t lencode;
  uint32_t len_nextra;
  uint32_t len_extra;
  if (!is_first_block) {
    BrotliWriteBits(code->type_depths[typecode], code->type_bits[typecode],
                    storage_ix, storage);
  }
  GetBlockLengthPrefixCode(block_len, &lencode, &len_nextra, &len_extra);

  BrotliWriteBits(code->length_depths[lencode], code->length_bits[lencode],
                  storage_ix, storage);
  BrotliWriteBits(len_nextra, len_extra, storage_ix, storage);
}

/* Builds a BlockSplitCode data structure from the block split given by the
   vector of block types and block lengths and stores it to the bit stream. */
static void BuildAndStoreBlockSplitCode(const uint8_t* types,
                                        const uint32_t* lengths,
                                        const size_t num_blocks,
                                        const size_t num_types,
                                        HuffmanTree* tree,
                                        BlockSplitCode* code,
                                        size_t* storage_ix,
                                        uint8_t* storage) {
  uint32_t type_histo[BROTLI_MAX_BLOCK_TYPE_SYMBOLS];
  uint32_t length_histo[BROTLI_NUM_BLOCK_LEN_SYMBOLS];
  size_t i;
  BlockTypeCodeCalculator type_code_calculator;
  memset(type_histo, 0, (num_types + 2) * sizeof(type_histo[0]));
  memset(length_histo, 0, sizeof(length_histo));
  InitBlockTypeCodeCalculator(&type_code_calculator);
  for (i = 0; i < num_blocks; ++i) {
    size_t type_code = NextBlockTypeCode(&type_code_calculator, types[i]);
    if (i != 0) ++type_histo[type_code];
    ++length_histo[BlockLengthPrefixCode(lengths[i])];
  }
  StoreVarLenUint8(num_types - 1, storage_ix, storage);
  if (num_types > 1) {  /* TODO(eustas): else? could StoreBlockSwitch occur? */
    BuildAndStoreHuffmanTree(&type_histo[0], num_types + 2, num_types + 2, tree,
                             &code->type_depths[0], &code->type_bits[0],
                             storage_ix, storage);
    BuildAndStoreHuffmanTree(&length_histo[0], BROTLI_NUM_BLOCK_LEN_SYMBOLS,
                             BROTLI_NUM_BLOCK_LEN_SYMBOLS,
                             tree, &code->length_depths[0],
                             &code->length_bits[0], storage_ix, storage);
    StoreBlockSwitch(code, lengths[0], types[0], 1, storage_ix, storage);
  }
}

/* Stores a context map where the histogram type is always the block type. */
static void StoreTrivialContextMap(EncodeContextMapArena* arena,
                                   size_t num_types,
                                   size_t context_bits,
                                   HuffmanTree* tree,
                                   size_t* storage_ix,
                                   uint8_t* storage) {
  StoreVarLenUint8(num_types - 1, storage_ix, storage);
  if (num_types > 1) {
    size_t repeat_code = context_bits - 1u;
    size_t repeat_bits = (1u << repeat_code) - 1u;
    size_t alphabet_size = num_types + repeat_code;
    uint32_t* BROTLI_RESTRICT const histogram = arena->histogram;
    uint8_t* BROTLI_RESTRICT const depths = arena->depths;
    uint16_t* BROTLI_RESTRICT const bits = arena->bits;
    size_t i;
    memset(histogram, 0, alphabet_size * sizeof(histogram[0]));
    /* Write RLEMAX. */
    BrotliWriteBits(1, 1, storage_ix, storage);
    BrotliWriteBits(4, repeat_code - 1, storage_ix, storage);
    histogram[repeat_code] = (uint32_t)num_types;
    histogram[0] = 1;
    for (i = context_bits; i < alphabet_size; ++i) {
      histogram[i] = 1;
    }
    BuildAndStoreHuffmanTree(histogram, alphabet_size, alphabet_size,
                             tree, depths, bits, storage_ix, storage);
    for (i = 0; i < num_types; ++i) {
      size_t code = (i == 0 ? 0 : i + context_bits - 1);
      BrotliWriteBits(depths[code], bits[code], storage_ix, storage);
      BrotliWriteBits(
          depths[repeat_code], bits[repeat_code], storage_ix, storage);
      BrotliWriteBits(repeat_code, repeat_bits, storage_ix, storage);
    }
    /* Write IMTF (inverse-move-to-front) bit. */
    BrotliWriteBits(1, 1, storage_ix, storage);
  }
}

/* Manages the encoding of one block category (literal, command or distance). */
typedef struct BlockEncoder {
  size_t histogram_length_;
  size_t num_block_types_;
  const uint8_t* block_types_;  /* Not owned. */
  const uint32_t* block_lengths_;  /* Not owned. */
  size_t num_blocks_;
  BlockSplitCode block_split_code_;
  size_t block_ix_;
  size_t block_len_;
  size_t entropy_ix_;
  uint8_t* depths_;
  uint16_t* bits_;
} BlockEncoder;

static void InitBlockEncoder(BlockEncoder* self, size_t histogram_length,
    size_t num_block_types, const uint8_t* block_types,
    const uint32_t* block_lengths, const size_t num_blocks) {
  self->histogram_length_ = histogram_length;
  self->num_block_types_ = num_block_types;
  self->block_types_ = block_types;
  self->block_lengths_ = block_lengths;
  self->num_blocks_ = num_blocks;
  InitBlockTypeCodeCalculator(&self->block_split_code_.type_code_calculator);
  self->block_ix_ = 0;
  self->block_len_ = num_blocks == 0 ? 0 : block_lengths[0];
  self->entropy_ix_ = 0;
  self->depths_ = 0;
  self->bits_ = 0;
}

static void CleanupBlockEncoder(MemoryManager* m, BlockEncoder* self) {
  BROTLI_FREE(m, self->depths_);
  BROTLI_FREE(m, self->bits_);
}

/* Creates entropy codes of block lengths and block types and stores them
   to the bit stream. */
static void BuildAndStoreBlockSwitchEntropyCodes(BlockEncoder* self,
    HuffmanTree* tree, size_t* storage_ix, uint8_t* storage) {
  BuildAndStoreBlockSplitCode(self->block_types_, self->block_lengths_,
      self->num_blocks_, self->num_block_types_, tree, &self->block_split_code_,
      storage_ix, storage);
}

/* Stores the next symbol with the entropy code of the current block type.
   Updates the block type and block length at block boundaries. */
static void StoreSymbol(BlockEncoder* self, size_t symbol, size_t* storage_ix,
    uint8_t* storage) {
  if (self->block_len_ == 0) {
    size_t block_ix = ++self->block_ix_;
    uint32_t block_len = self->block_lengths_[block_ix];
    uint8_t block_type = self->block_types_[block_ix];
    self->block_len_ = block_len;
    self->entropy_ix_ = block_type * self->histogram_length_;
    StoreBlockSwitch(&self->block_split_code_, block_len, block_type, 0,
        storage_ix, storage);
  }
  --self->block_len_;
  {
    size_t ix = self->entropy_ix_ + symbol;
    BrotliWriteBits(self->depths_[ix], self->bits_[ix], storage_ix, storage);
  }
}

/* Stores the next symbol with the entropy code of the current block type and
   context value.
   Updates the block type and block length at block boundaries. */
static void StoreSymbolWithContext(BlockEncoder* self, size_t symbol,
    size_t context, const uint32_t* context_map, size_t* storage_ix,
    uint8_t* storage, const size_t context_bits) {
  if (self->block_len_ == 0) {
    size_t block_ix = ++self->block_ix_;
    uint32_t block_len = self->block_lengths_[block_ix];
    uint8_t block_type = self->block_types_[block_ix];
    self->block_len_ = block_len;
    self->entropy_ix_ = (size_t)block_type << context_bits;
    StoreBlockSwitch(&self->block_split_code_, block_len, block_type, 0,
        storage_ix, storage);
  }
  --self->block_len_;
  {
    size_t histo_ix = context_map[self->entropy_ix_ + context];
    size_t ix = histo_ix * self->histogram_length_ + symbol;
    BrotliWriteBits(self->depths_[ix], self->bits_[ix], storage_ix, storage);
  }
}

#define FN(X) X ## Literal
/* NOLINTNEXTLINE(build/include) */
#include "block_encoder_inc.h"
#undef FN

#define FN(X) X ## Command
/* NOLINTNEXTLINE(build/include) */
#include "block_encoder_inc.h"
#undef FN

#define FN(X) X ## Distance
/* NOLINTNEXTLINE(build/include) */
#include "block_encoder_inc.h"
#undef FN

static void JumpToByteBoundary(size_t* storage_ix, uint8_t* storage) {
  *storage_ix = (*storage_ix + 7u) & ~7u;
  storage[*storage_ix >> 3] = 0;
}

typedef struct StoreMetablockArena {
  BlockEncoder literal_enc;
  BlockEncoder command_enc;
  BlockEncoder distance_enc;
  EncodeContextMapArena context_map_arena;
} StoreMetablockArena;

void BrotliStoreMetaBlock(MemoryManager* m,
    const uint8_t* input, size_t start_pos, size_t length, size_t mask,
    uint8_t prev_byte, uint8_t prev_byte2, BROTLI_BOOL is_last,
    const BrotliEncoderParams* params, ContextType literal_context_mode,
    const Command* commands, size_t n_commands, const MetaBlockSplit* mb,
    size_t* storage_ix, uint8_t* storage) {

  size_t pos = start_pos;
  size_t i;
  uint32_t num_distance_symbols = params->dist.alphabet_size_max;
  uint32_t num_effective_distance_symbols = params->dist.alphabet_size_limit;
  HuffmanTree* tree;
  ContextLut literal_context_lut = BROTLI_CONTEXT_LUT(literal_context_mode);
  StoreMetablockArena* arena = NULL;
  BlockEncoder* literal_enc = NULL;
  BlockEncoder* command_enc = NULL;
  BlockEncoder* distance_enc = NULL;
  const BrotliDistanceParams* dist = &params->dist;
  BROTLI_DCHECK(
      num_effective_distance_symbols <= BROTLI_NUM_HISTOGRAM_DISTANCE_SYMBOLS);

  StoreCompressedMetaBlockHeader(is_last, length, storage_ix, storage);

  tree = BROTLI_ALLOC(m, HuffmanTree, MAX_HUFFMAN_TREE_SIZE);
  arena = BROTLI_ALLOC(m, StoreMetablockArena, 1);
  if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(tree) || BROTLI_IS_NULL(arena)) return;
  literal_enc = &arena->literal_enc;
  command_enc = &arena->command_enc;
  distance_enc = &arena->distance_enc;
  InitBlockEncoder(literal_enc, BROTLI_NUM_LITERAL_SYMBOLS,
      mb->literal_split.num_types, mb->literal_split.types,
      mb->literal_split.lengths, mb->literal_split.num_blocks);
  InitBlockEncoder(command_enc, BROTLI_NUM_COMMAND_SYMBOLS,
      mb->command_split.num_types, mb->command_split.types,
      mb->command_split.lengths, mb->command_split.num_blocks);
  InitBlockEncoder(distance_enc, num_effective_distance_symbols,
      mb->distance_split.num_types, mb->distance_split.types,
      mb->distance_split.lengths, mb->distance_split.num_blocks);

  BuildAndStoreBlockSwitchEntropyCodes(literal_enc, tree, storage_ix, storage);
  BuildAndStoreBlockSwitchEntropyCodes(command_enc, tree, storage_ix, storage);
  BuildAndStoreBlockSwitchEntropyCodes(distance_enc, tree, storage_ix, storage);

  BrotliWriteBits(2, dist->distance_postfix_bits, storage_ix, storage);
  BrotliWriteBits(
      4, dist->num_direct_distance_codes >> dist->distance_postfix_bits,
      storage_ix, storage);
  for (i = 0; i < mb->literal_split.num_types; ++i) {
    BrotliWriteBits(2, literal_context_mode, storage_ix, storage);
  }

  if (mb->literal_context_map_size == 0) {
    StoreTrivialContextMap(
        &arena->context_map_arena, mb->literal_histograms_size,
        BROTLI_LITERAL_CONTEXT_BITS, tree, storage_ix, storage);
  } else {
    EncodeContextMap(m, &arena->context_map_arena,
        mb->literal_context_map, mb->literal_context_map_size,
        mb->literal_histograms_size, tree, storage_ix, storage);
    if (BROTLI_IS_OOM(m)) return;
  }

  if (mb->distance_context_map_size == 0) {
    StoreTrivialContextMap(
        &arena->context_map_arena, mb->distance_histograms_size,
        BROTLI_DISTANCE_CONTEXT_BITS, tree, storage_ix, storage);
  } else {
    EncodeContextMap(m, &arena->context_map_arena,
        mb->distance_context_map, mb->distance_context_map_size,
        mb->distance_histograms_size, tree, storage_ix, storage);
    if (BROTLI_IS_OOM(m)) return;
  }

  BuildAndStoreEntropyCodesLiteral(m, literal_enc, mb->literal_histograms,
      mb->literal_histograms_size, BROTLI_NUM_LITERAL_SYMBOLS, tree,
      storage_ix, storage);
  if (BROTLI_IS_OOM(m)) return;
  BuildAndStoreEntropyCodesCommand(m, command_enc, mb->command_histograms,
      mb->command_histograms_size, BROTLI_NUM_COMMAND_SYMBOLS, tree,
      storage_ix, storage);
  if (BROTLI_IS_OOM(m)) return;
  BuildAndStoreEntropyCodesDistance(m, distance_enc, mb->distance_histograms,
      mb->distance_histograms_size, num_distance_symbols, tree,
      storage_ix, storage);
  if (BROTLI_IS_OOM(m)) return;
  BROTLI_FREE(m, tree);

  for (i = 0; i < n_commands; ++i) {
    const Command cmd = commands[i];
    size_t cmd_code = cmd.cmd_prefix_;
    StoreSymbol(command_enc, cmd_code, storage_ix, storage);
    StoreCommandExtra(&cmd, storage_ix, storage);
    if (mb->literal_context_map_size == 0) {
      size_t j;
      for (j = cmd.insert_len_; j != 0; --j) {
        StoreSymbol(literal_enc, input[pos & mask], storage_ix, storage);
        ++pos;
      }
    } else {
      size_t j;
      for (j = cmd.insert_len_; j != 0; --j) {
        size_t context =
            BROTLI_CONTEXT(prev_byte, prev_byte2, literal_context_lut);
        uint8_t literal = input[pos & mask];
        StoreSymbolWithContext(literal_enc, literal, context,
            mb->literal_context_map, storage_ix, storage,
            BROTLI_LITERAL_CONTEXT_BITS);
        prev_byte2 = prev_byte;
        prev_byte = literal;
        ++pos;
      }
    }
    pos += CommandCopyLen(&cmd);
    if (CommandCopyLen(&cmd)) {
      prev_byte2 = input[(pos - 2) & mask];
      prev_byte = input[(pos - 1) & mask];
      if (cmd.cmd_prefix_ >= 128) {
        size_t dist_code = cmd.dist_prefix_ & 0x3FF;
        uint32_t distnumextra = cmd.dist_prefix_ >> 10;
        uint64_t distextra = cmd.dist_extra_;
        if (mb->distance_context_map_size == 0) {
          StoreSymbol(distance_enc, dist_code, storage_ix, storage);
        } else {
          size_t context = CommandDistanceContext(&cmd);
          StoreSymbolWithContext(distance_enc, dist_code, context,
              mb->distance_context_map, storage_ix, storage,
              BROTLI_DISTANCE_CONTEXT_BITS);
        }
        BrotliWriteBits(distnumextra, distextra, storage_ix, storage);
      }
    }
  }
  CleanupBlockEncoder(m, distance_enc);
  CleanupBlockEncoder(m, command_enc);
  CleanupBlockEncoder(m, literal_enc);
  BROTLI_FREE(m, arena);
  if (is_last) {
    JumpToByteBoundary(storage_ix, storage);
  }
}

static void BuildHistograms(const uint8_t* input,
                            size_t start_pos,
                            size_t mask,
                            const Command* commands,
                            size_t n_commands,
                            HistogramLiteral* lit_histo,
                            HistogramCommand* cmd_histo,
                            HistogramDistance* dist_histo) {
  size_t pos = start_pos;
  size_t i;
  for (i = 0; i < n_commands; ++i) {
    const Command cmd = commands[i];
    size_t j;
    HistogramAddCommand(cmd_histo, cmd.cmd_prefix_);
    for (j = cmd.insert_len_; j != 0; --j) {
      HistogramAddLiteral(lit_histo, input[pos & mask]);
      ++pos;
    }
    pos += CommandCopyLen(&cmd);
    if (CommandCopyLen(&cmd) && cmd.cmd_prefix_ >= 128) {
      HistogramAddDistance(dist_histo, cmd.dist_prefix_ & 0x3FF);
    }
  }
}

static void StoreDataWithHuffmanCodes(const uint8_t* input,
                                      size_t start_pos,
                                      size_t mask,
                                      const Command* commands,
                                      size_t n_commands,
                                      const uint8_t* lit_depth,
                                      const uint16_t* lit_bits,
                                      const uint8_t* cmd_depth,
                                      const uint16_t* cmd_bits,
                                      const uint8_t* dist_depth,
                                      const uint16_t* dist_bits,
                                      size_t* storage_ix,
                                      uint8_t* storage) {
  size_t pos = start_pos;
  size_t i;
  for (i = 0; i < n_commands; ++i) {
    const Command cmd = commands[i];
    const size_t cmd_code = cmd.cmd_prefix_;
    size_t j;
    BrotliWriteBits(
        cmd_depth[cmd_code], cmd_bits[cmd_code], storage_ix, storage);
    StoreCommandExtra(&cmd, storage_ix, storage);
    for (j = cmd.insert_len_; j != 0; --j) {
      const uint8_t literal = input[pos & mask];
      BrotliWriteBits(
          lit_depth[literal], lit_bits[literal], storage_ix, storage);
      ++pos;
    }
    pos += CommandCopyLen(&cmd);
    if (CommandCopyLen(&cmd) && cmd.cmd_prefix_ >= 128) {
      const size_t dist_code = cmd.dist_prefix_ & 0x3FF;
      const uint32_t distnumextra = cmd.dist_prefix_ >> 10;
      const uint32_t distextra = cmd.dist_extra_;
      BrotliWriteBits(dist_depth[dist_code], dist_bits[dist_code],
                      storage_ix, storage);
      BrotliWriteBits(distnumextra, distextra, storage_ix, storage);
    }
  }
}

/* TODO(eustas): pull alloc/dealloc to caller? */
typedef struct MetablockArena {
  HistogramLiteral lit_histo;
  HistogramCommand cmd_histo;
  HistogramDistance dist_histo;
  /* TODO(eustas): merge bits and depth? */
  uint8_t lit_depth[BROTLI_NUM_LITERAL_SYMBOLS];
  uint16_t lit_bits[BROTLI_NUM_LITERAL_SYMBOLS];
  uint8_t cmd_depth[BROTLI_NUM_COMMAND_SYMBOLS];
  uint16_t cmd_bits[BROTLI_NUM_COMMAND_SYMBOLS];
  uint8_t dist_depth[MAX_SIMPLE_DISTANCE_ALPHABET_SIZE];
  uint16_t dist_bits[MAX_SIMPLE_DISTANCE_ALPHABET_SIZE];
  HuffmanTree tree[MAX_HUFFMAN_TREE_SIZE];
} MetablockArena;

void BrotliStoreMetaBlockTrivial(MemoryManager* m,
    const uint8_t* input, size_t start_pos, size_t length, size_t mask,
    BROTLI_BOOL is_last, const BrotliEncoderParams* params,
    const Command* commands, size_t n_commands,
    size_t* storage_ix, uint8_t* storage) {
  MetablockArena* arena = BROTLI_ALLOC(m, MetablockArena, 1);
  uint32_t num_distance_symbols = params->dist.alphabet_size_max;
  if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(arena)) return;

  StoreCompressedMetaBlockHeader(is_last, length, storage_ix, storage);

  HistogramClearLiteral(&arena->lit_histo);
  HistogramClearCommand(&arena->cmd_histo);
  HistogramClearDistance(&arena->dist_histo);

  BuildHistograms(input, start_pos, mask, commands, n_commands,
                  &arena->lit_histo, &arena->cmd_histo, &arena->dist_histo);

  BrotliWriteBits(13, 0, storage_ix, storage);

  BuildAndStoreHuffmanTree(arena->lit_histo.data_, BROTLI_NUM_LITERAL_SYMBOLS,
                           BROTLI_NUM_LITERAL_SYMBOLS, arena->tree,
                           arena->lit_depth, arena->lit_bits,
                           storage_ix, storage);
  BuildAndStoreHuffmanTree(arena->cmd_histo.data_, BROTLI_NUM_COMMAND_SYMBOLS,
                           BROTLI_NUM_COMMAND_SYMBOLS, arena->tree,
                           arena->cmd_depth, arena->cmd_bits,
                           storage_ix, storage);
  BuildAndStoreHuffmanTree(arena->dist_histo.data_,
                           MAX_SIMPLE_DISTANCE_ALPHABET_SIZE,
                           num_distance_symbols, arena->tree,
                           arena->dist_depth, arena->dist_bits,
                           storage_ix, storage);
  StoreDataWithHuffmanCodes(input, start_pos, mask, commands,
                            n_commands, arena->lit_depth, arena->lit_bits,
                            arena->cmd_depth, arena->cmd_bits,
                            arena->dist_depth, arena->dist_bits,
                            storage_ix, storage);
  BROTLI_FREE(m, arena);
  if (is_last) {
    JumpToByteBoundary(storage_ix, storage);
  }
}

void BrotliStoreMetaBlockFast(MemoryManager* m,
    const uint8_t* input, size_t start_pos, size_t length, size_t mask,
    BROTLI_BOOL is_last, const BrotliEncoderParams* params,
    const Command* commands, size_t n_commands,
    size_t* storage_ix, uint8_t* storage) {
  MetablockArena* arena = BROTLI_ALLOC(m, MetablockArena, 1);
  uint32_t num_distance_symbols = params->dist.alphabet_size_max;
  uint32_t distance_alphabet_bits =
      Log2FloorNonZero(num_distance_symbols - 1) + 1;
  if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(arena)) return;

  StoreCompressedMetaBlockHeader(is_last, length, storage_ix, storage);

  BrotliWriteBits(13, 0, storage_ix, storage);

  if (n_commands <= 128) {
    uint32_t histogram[BROTLI_NUM_LITERAL_SYMBOLS] = { 0 };
    size_t pos = start_pos;
    size_t num_literals = 0;
    size_t i;
    for (i = 0; i < n_commands; ++i) {
      const Command cmd = commands[i];
      size_t j;
      for (j = cmd.insert_len_; j != 0; --j) {
        ++histogram[input[pos & mask]];
        ++pos;
      }
      num_literals += cmd.insert_len_;
      pos += CommandCopyLen(&cmd);
    }
    BrotliBuildAndStoreHuffmanTreeFast(arena->tree, histogram, num_literals,
                                       /* max_bits = */ 8,
                                       arena->lit_depth, arena->lit_bits,
                                       storage_ix, storage);
    StoreStaticCommandHuffmanTree(storage_ix, storage);
    StoreStaticDistanceHuffmanTree(storage_ix, storage);
    StoreDataWithHuffmanCodes(input, start_pos, mask, commands,
                              n_commands, arena->lit_depth, arena->lit_bits,
                              kStaticCommandCodeDepth,
                              kStaticCommandCodeBits,
                              kStaticDistanceCodeDepth,
                              kStaticDistanceCodeBits,
                              storage_ix, storage);
  } else {
    HistogramClearLiteral(&arena->lit_histo);
    HistogramClearCommand(&arena->cmd_histo);
    HistogramClearDistance(&arena->dist_histo);
    BuildHistograms(input, start_pos, mask, commands, n_commands,
                    &arena->lit_histo, &arena->cmd_histo, &arena->dist_histo);
    BrotliBuildAndStoreHuffmanTreeFast(arena->tree, arena->lit_histo.data_,
                                       arena->lit_histo.total_count_,
                                       /* max_bits = */ 8,
                                       arena->lit_depth, arena->lit_bits,
                                       storage_ix, storage);
    BrotliBuildAndStoreHuffmanTreeFast(arena->tree, arena->cmd_histo.data_,
                                       arena->cmd_histo.total_count_,
                                       /* max_bits = */ 10,
                                       arena->cmd_depth, arena->cmd_bits,
                                       storage_ix, storage);
    BrotliBuildAndStoreHuffmanTreeFast(arena->tree, arena->dist_histo.data_,
                                       arena->dist_histo.total_count_,
                                       /* max_bits = */
                                       distance_alphabet_bits,
                                       arena->dist_depth, arena->dist_bits,
                                       storage_ix, storage);
    StoreDataWithHuffmanCodes(input, start_pos, mask, commands,
                              n_commands, arena->lit_depth, arena->lit_bits,
                              arena->cmd_depth, arena->cmd_bits,
                              arena->dist_depth, arena->dist_bits,
                              storage_ix, storage);
  }

  BROTLI_FREE(m, arena);

  if (is_last) {
    JumpToByteBoundary(storage_ix, storage);
  }
}

/* This is for storing uncompressed blocks (simple raw storage of
   bytes-as-bytes). */
void BrotliStoreUncompressedMetaBlock(BROTLI_BOOL is_final_block,
                                      const uint8_t* BROTLI_RESTRICT input,
                                      size_t position, size_t mask,
                                      size_t len,
                                      size_t* BROTLI_RESTRICT storage_ix,
                                      uint8_t* BROTLI_RESTRICT storage) {
  size_t masked_pos = position & mask;
  BrotliStoreUncompressedMetaBlockHeader(len, storage_ix, storage);
  JumpToByteBoundary(storage_ix, storage);

  if (masked_pos + len > mask + 1) {
    size_t len1 = mask + 1 - masked_pos;
    memcpy(&storage[*storage_ix >> 3], &input[masked_pos], len1);
    *storage_ix += len1 << 3;
    len -= len1;
    masked_pos = 0;
  }
  memcpy(&storage[*storage_ix >> 3], &input[masked_pos], len);
  *storage_ix += len << 3;

  /* We need to clear the next 4 bytes to continue to be
     compatible with BrotliWriteBits. */
  BrotliWriteBitsPrepareStorage(*storage_ix, storage);

  /* Since the uncompressed block itself may not be the final block, add an
     empty one after this. */
  if (is_final_block) {
    BrotliWriteBits(1, 1, storage_ix, storage);  /* islast */
    BrotliWriteBits(1, 1, storage_ix, storage);  /* isempty */
    JumpToByteBoundary(storage_ix, storage);
  }
}

#if defined(BROTLI_TEST)
void GetBlockLengthPrefixCodeForTest(uint32_t len, size_t* code,
                                     uint32_t* n_extra, uint32_t* extra) {
  GetBlockLengthPrefixCode(len, code, n_extra, extra);
}
#endif

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif
