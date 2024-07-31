/* Copyright 2015 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Function for fast encoding of an input fragment, independently from the input
   history. This function uses one-pass processing: when we find a backward
   match, we immediately emit the corresponding command and literal codes to
   the bit stream. */

#ifndef BROTLI_ENC_COMPRESS_FRAGMENT_H_
#define BROTLI_ENC_COMPRESS_FRAGMENT_H_

#include <brotli/types.h>

#include "../common/constants.h"
#include "../common/platform.h"
#include "entropy_encode.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

typedef struct BrotliOnePassArena {
  uint8_t lit_depth[256];
  uint16_t lit_bits[256];

  /* Command and distance prefix codes (each 64 symbols, stored back-to-back)
     used for the next block. The command prefix code is over a smaller alphabet
     with the following 64 symbols:
        0 - 15: insert length code 0, copy length code 0 - 15, same distance
       16 - 39: insert length code 0, copy length code 0 - 23
       40 - 63: insert length code 0 - 23, copy length code 0
     Note that symbols 16 and 40 represent the same code in the full alphabet,
     but we do not use either of them. */
  uint8_t cmd_depth[128];
  uint16_t cmd_bits[128];
  uint32_t cmd_histo[128];

  /* The compressed form of the command and distance prefix codes for the next
     block. */
  uint8_t cmd_code[512];
  size_t cmd_code_numbits;

  HuffmanTree tree[2 * BROTLI_NUM_LITERAL_SYMBOLS + 1];
  uint32_t histogram[256];
  uint8_t tmp_depth[BROTLI_NUM_COMMAND_SYMBOLS];
  uint16_t tmp_bits[64];
} BrotliOnePassArena;

/* Compresses "input" string to the "*storage" buffer as one or more complete
   meta-blocks, and updates the "*storage_ix" bit position.

   If "is_last" is 1, emits an additional empty last meta-block.

   "cmd_depth" and "cmd_bits" contain the command and distance prefix codes
   (see comment in encode.h) used for the encoding of this input fragment.
   If "is_last" is 0, they are updated to reflect the statistics
   of this input fragment, to be used for the encoding of the next fragment.

   "*cmd_code_numbits" is the number of bits of the compressed representation
   of the command and distance prefix codes, and "cmd_code" is an array of
   at least "(*cmd_code_numbits + 7) >> 3" size that contains the compressed
   command and distance prefix codes. If "is_last" is 0, these are also
   updated to represent the updated "cmd_depth" and "cmd_bits".

   REQUIRES: "input_size" is greater than zero, or "is_last" is 1.
   REQUIRES: "input_size" is less or equal to maximal metablock size (1 << 24).
   REQUIRES: All elements in "table[0..table_size-1]" are initialized to zero.
   REQUIRES: "table_size" is an odd (9, 11, 13, 15) power of two
   OUTPUT: maximal copy distance <= |input_size|
   OUTPUT: maximal copy distance <= BROTLI_MAX_BACKWARD_LIMIT(18) */
BROTLI_INTERNAL void BrotliCompressFragmentFast(BrotliOnePassArena* s,
                                                const uint8_t* input,
                                                size_t input_size,
                                                BROTLI_BOOL is_last,
                                                int* table, size_t table_size,
                                                size_t* storage_ix,
                                                uint8_t* storage);

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_ENC_COMPRESS_FRAGMENT_H_ */
