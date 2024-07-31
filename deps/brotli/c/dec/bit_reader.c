/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Bit reading helpers */

#include "bit_reader.h"

#include <brotli/types.h>

#include "../common/platform.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

const brotli_reg_t kBrotliBitMask[33] = {   0x00000000,
    0x00000001, 0x00000003, 0x00000007, 0x0000000F,
    0x0000001F, 0x0000003F, 0x0000007F, 0x000000FF,
    0x000001FF, 0x000003FF, 0x000007FF, 0x00000FFF,
    0x00001FFF, 0x00003FFF, 0x00007FFF, 0x0000FFFF,
    0x0001FFFF, 0x0003FFFF, 0x0007FFFF, 0x000FFFFF,
    0x001FFFFF, 0x003FFFFF, 0x007FFFFF, 0x00FFFFFF,
    0x01FFFFFF, 0x03FFFFFF, 0x07FFFFFF, 0x0FFFFFFF,
    0x1FFFFFFF, 0x3FFFFFFF, 0x7FFFFFFF, 0xFFFFFFFF
};

void BrotliInitBitReader(BrotliBitReader* const br) {
  br->val_ = 0;
  br->bit_pos_ = 0;
}

BROTLI_BOOL BrotliWarmupBitReader(BrotliBitReader* const br) {
  size_t aligned_read_mask = (sizeof(br->val_) >> 1) - 1;
  /* Fixing alignment after unaligned BrotliFillWindow would result accumulator
     overflow. If unalignment is caused by BrotliSafeReadBits, then there is
     enough space in accumulator to fix alignment. */
  if (BROTLI_UNALIGNED_READ_FAST) {
    aligned_read_mask = 0;
  }
  if (BrotliGetAvailableBits(br) == 0) {
    br->val_ = 0;
    if (!BrotliPullByte(br)) {
      return BROTLI_FALSE;
    }
  }

  while ((((size_t)br->next_in) & aligned_read_mask) != 0) {
    if (!BrotliPullByte(br)) {
      /* If we consumed all the input, we don't care about the alignment. */
      return BROTLI_TRUE;
    }
  }
  return BROTLI_TRUE;
}

BROTLI_BOOL BrotliSafeReadBits32Slow(BrotliBitReader* const br,
    brotli_reg_t n_bits, brotli_reg_t* val) {
  brotli_reg_t low_val;
  brotli_reg_t high_val;
  BrotliBitReaderState memento;
  BROTLI_DCHECK(n_bits <= 32);
  BROTLI_DCHECK(n_bits > 24);
  BrotliBitReaderSaveState(br, &memento);
  if (!BrotliSafeReadBits(br, 16, &low_val) ||
      !BrotliSafeReadBits(br, n_bits - 16, &high_val)) {
    BrotliBitReaderRestoreState(br, &memento);
    return BROTLI_FALSE;
  }
  *val = low_val | (high_val << 16);
  return BROTLI_TRUE;
}

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif
