/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Bit reading helpers */

#ifndef BROTLI_DEC_BIT_READER_H_
#define BROTLI_DEC_BIT_READER_H_

#include <string.h>  /* memcpy */

#include <brotli/types.h>

#include "../common/constants.h"
#include "../common/platform.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define BROTLI_SHORT_FILL_BIT_WINDOW_READ (sizeof(brotli_reg_t) >> 1)

/* 162 bits + 7 bytes */
#define BROTLI_FAST_INPUT_SLACK 28

BROTLI_INTERNAL extern const brotli_reg_t kBrotliBitMask[33];

static BROTLI_INLINE brotli_reg_t BitMask(brotli_reg_t n) {
  if (BROTLI_IS_CONSTANT(n) || BROTLI_HAS_UBFX) {
    /* Masking with this expression turns to a single
       "Unsigned Bit Field Extract" UBFX instruction on ARM. */
    return ~(~((brotli_reg_t)0) << n);
  } else {
    return kBrotliBitMask[n];
  }
}

typedef struct {
  brotli_reg_t val_;       /* pre-fetched bits */
  brotli_reg_t bit_pos_;   /* current bit-reading position in val_ */
  const uint8_t* next_in;  /* the byte we're reading from */
  const uint8_t* guard_in; /* position from which "fast-path" is prohibited */
  const uint8_t* last_in;  /* == next_in + avail_in */
} BrotliBitReader;

typedef struct {
  brotli_reg_t val_;
  brotli_reg_t bit_pos_;
  const uint8_t* next_in;
  size_t avail_in;
} BrotliBitReaderState;

/* Initializes the BrotliBitReader fields. */
BROTLI_INTERNAL void BrotliInitBitReader(BrotliBitReader* br);

/* Ensures that accumulator is not empty.
   May consume up to sizeof(brotli_reg_t) - 1 bytes of input.
   Returns BROTLI_FALSE if data is required but there is no input available.
   For !BROTLI_UNALIGNED_READ_FAST this function also prepares bit reader for
   aligned reading. */
BROTLI_INTERNAL BROTLI_BOOL BrotliWarmupBitReader(BrotliBitReader* br);

/* Fallback for BrotliSafeReadBits32. Extracted as noninlined method to unburden
   the main code-path. Never called for RFC brotli streams, required only for
   "large-window" mode and other extensions. */
BROTLI_INTERNAL BROTLI_NOINLINE BROTLI_BOOL BrotliSafeReadBits32Slow(
    BrotliBitReader* br, brotli_reg_t n_bits, brotli_reg_t* val);

static BROTLI_INLINE size_t
BrotliBitReaderGetAvailIn(BrotliBitReader* const br) {
  return (size_t)(br->last_in - br->next_in);
}

static BROTLI_INLINE void BrotliBitReaderSaveState(
    BrotliBitReader* const from, BrotliBitReaderState* to) {
  to->val_ = from->val_;
  to->bit_pos_ = from->bit_pos_;
  to->next_in = from->next_in;
  to->avail_in = BrotliBitReaderGetAvailIn(from);
}

static BROTLI_INLINE void BrotliBitReaderSetInput(
    BrotliBitReader* const br, const uint8_t* next_in, size_t avail_in) {
  br->next_in = next_in;
  br->last_in = (avail_in == 0) ? next_in : (next_in + avail_in);
  if (avail_in + 1 > BROTLI_FAST_INPUT_SLACK) {
    br->guard_in = next_in + (avail_in + 1 - BROTLI_FAST_INPUT_SLACK);
  } else {
    br->guard_in = next_in;
  }
}

static BROTLI_INLINE void BrotliBitReaderRestoreState(
    BrotliBitReader* const to, BrotliBitReaderState* from) {
  to->val_ = from->val_;
  to->bit_pos_ = from->bit_pos_;
  to->next_in = from->next_in;
  BrotliBitReaderSetInput(to, from->next_in, from->avail_in);
}

static BROTLI_INLINE brotli_reg_t BrotliGetAvailableBits(
    const BrotliBitReader* br) {
  return br->bit_pos_;
}

/* Returns amount of unread bytes the bit reader still has buffered from the
   BrotliInput, including whole bytes in br->val_. Result is capped with
   maximal ring-buffer size (larger number won't be utilized anyway). */
static BROTLI_INLINE size_t BrotliGetRemainingBytes(BrotliBitReader* br) {
  static const size_t kCap = (size_t)1 << BROTLI_LARGE_MAX_WBITS;
  size_t avail_in = BrotliBitReaderGetAvailIn(br);
  if (avail_in > kCap) return kCap;
  return avail_in + (BrotliGetAvailableBits(br) >> 3);
}

/* Checks if there is at least |num| bytes left in the input ring-buffer
   (excluding the bits remaining in br->val_). */
static BROTLI_INLINE BROTLI_BOOL BrotliCheckInputAmount(
    BrotliBitReader* const br) {
  return TO_BROTLI_BOOL(br->next_in < br->guard_in);
}

/* Load more bits into accumulator. */
static BROTLI_INLINE brotli_reg_t BrotliBitReaderLoadBits(brotli_reg_t val,
                                                          brotli_reg_t new_bits,
                                                          brotli_reg_t count,
                                                          brotli_reg_t offset) {
  BROTLI_DCHECK(
      !((val >> offset) & ~new_bits & ~(~((brotli_reg_t)0) << count)));
  (void)count;
  return val | (new_bits << offset);
}

/* Guarantees that there are at least |n_bits| + 1 bits in accumulator.
   Precondition: accumulator contains at least 1 bit.
   |n_bits| should be in the range [1..24] for regular build. For portable
   non-64-bit little-endian build only 16 bits are safe to request. */
static BROTLI_INLINE void BrotliFillBitWindow(
    BrotliBitReader* const br, brotli_reg_t n_bits) {
#if (BROTLI_64_BITS)
  if (BROTLI_UNALIGNED_READ_FAST && BROTLI_IS_CONSTANT(n_bits) &&
      (n_bits <= 8)) {
    brotli_reg_t bit_pos = br->bit_pos_;
    if (bit_pos <= 8) {
      br->val_ = BrotliBitReaderLoadBits(br->val_,
          BROTLI_UNALIGNED_LOAD64LE(br->next_in), 56, bit_pos);
      br->bit_pos_ = bit_pos + 56;
      br->next_in += 7;
    }
  } else if (BROTLI_UNALIGNED_READ_FAST && BROTLI_IS_CONSTANT(n_bits) &&
             (n_bits <= 16)) {
    brotli_reg_t bit_pos = br->bit_pos_;
    if (bit_pos <= 16) {
      br->val_ = BrotliBitReaderLoadBits(br->val_,
          BROTLI_UNALIGNED_LOAD64LE(br->next_in), 48, bit_pos);
      br->bit_pos_ = bit_pos + 48;
      br->next_in += 6;
    }
  } else {
    brotli_reg_t bit_pos = br->bit_pos_;
    if (bit_pos <= 32) {
      br->val_ = BrotliBitReaderLoadBits(br->val_,
          (uint64_t)BROTLI_UNALIGNED_LOAD32LE(br->next_in), 32, bit_pos);
      br->bit_pos_ = bit_pos + 32;
      br->next_in += BROTLI_SHORT_FILL_BIT_WINDOW_READ;
    }
  }
#else
  if (BROTLI_UNALIGNED_READ_FAST && BROTLI_IS_CONSTANT(n_bits) &&
      (n_bits <= 8)) {
    brotli_reg_t bit_pos = br->bit_pos_;
    if (bit_pos <= 8) {
      br->val_ = BrotliBitReaderLoadBits(br->val_,
          BROTLI_UNALIGNED_LOAD32LE(br->next_in), 24, bit_pos);
      br->bit_pos_ = bit_pos + 24;
      br->next_in += 3;
    }
  } else {
    brotli_reg_t bit_pos = br->bit_pos_;
    if (bit_pos <= 16) {
      br->val_ = BrotliBitReaderLoadBits(br->val_,
          (uint32_t)BROTLI_UNALIGNED_LOAD16LE(br->next_in), 16, bit_pos);
      br->bit_pos_ = bit_pos + 16;
      br->next_in += BROTLI_SHORT_FILL_BIT_WINDOW_READ;
    }
  }
#endif
}

/* Mostly like BrotliFillBitWindow, but guarantees only 16 bits and reads no
   more than BROTLI_SHORT_FILL_BIT_WINDOW_READ bytes of input. */
static BROTLI_INLINE void BrotliFillBitWindow16(BrotliBitReader* const br) {
  BrotliFillBitWindow(br, 17);
}

/* Tries to pull one byte of input to accumulator.
   Returns BROTLI_FALSE if there is no input available. */
static BROTLI_INLINE BROTLI_BOOL BrotliPullByte(BrotliBitReader* const br) {
  if (br->next_in == br->last_in) {
    return BROTLI_FALSE;
  }
  br->val_ = BrotliBitReaderLoadBits(br->val_,
      (brotli_reg_t)*br->next_in, 8, br->bit_pos_);
  br->bit_pos_ += 8;
  ++br->next_in;
  return BROTLI_TRUE;
}

/* Returns currently available bits.
   The number of valid bits could be calculated by BrotliGetAvailableBits. */
static BROTLI_INLINE brotli_reg_t BrotliGetBitsUnmasked(
    BrotliBitReader* const br) {
  return br->val_;
}

/* Like BrotliGetBits, but does not mask the result.
   The result contains at least 16 valid bits. */
static BROTLI_INLINE brotli_reg_t BrotliGet16BitsUnmasked(
    BrotliBitReader* const br) {
  BrotliFillBitWindow(br, 16);
  return (brotli_reg_t)BrotliGetBitsUnmasked(br);
}

/* Returns the specified number of bits from |br| without advancing bit
   position. */
static BROTLI_INLINE brotli_reg_t BrotliGetBits(
    BrotliBitReader* const br, brotli_reg_t n_bits) {
  BrotliFillBitWindow(br, n_bits);
  return BrotliGetBitsUnmasked(br) & BitMask(n_bits);
}

/* Tries to peek the specified amount of bits. Returns BROTLI_FALSE, if there
   is not enough input. */
static BROTLI_INLINE BROTLI_BOOL BrotliSafeGetBits(
    BrotliBitReader* const br, brotli_reg_t n_bits, brotli_reg_t* val) {
  while (BrotliGetAvailableBits(br) < n_bits) {
    if (!BrotliPullByte(br)) {
      return BROTLI_FALSE;
    }
  }
  *val = BrotliGetBitsUnmasked(br) & BitMask(n_bits);
  return BROTLI_TRUE;
}

/* Advances the bit pos by |n_bits|. */
static BROTLI_INLINE void BrotliDropBits(
    BrotliBitReader* const br, brotli_reg_t n_bits) {
  br->bit_pos_ -= n_bits;
  br->val_ >>= n_bits;
}

/* Make sure that there are no spectre bits in accumulator.
   This is important for the cases when some bytes are skipped
   (i.e. never placed into accumulator). */
static BROTLI_INLINE void BrotliBitReaderNormalize(BrotliBitReader* br) {
  /* Actually, it is enough to normalize when br->bit_pos_ == 0 */
  if (br->bit_pos_ < (sizeof(brotli_reg_t) << 3u)) {
    br->val_ &= (((brotli_reg_t)1) << br->bit_pos_) - 1;
  }
}

static BROTLI_INLINE void BrotliBitReaderUnload(BrotliBitReader* br) {
  brotli_reg_t unused_bytes = BrotliGetAvailableBits(br) >> 3;
  brotli_reg_t unused_bits = unused_bytes << 3;
  br->next_in =
      (unused_bytes == 0) ? br->next_in : (br->next_in - unused_bytes);
  br->bit_pos_ -= unused_bits;
  BrotliBitReaderNormalize(br);
}

/* Reads the specified number of bits from |br| and advances the bit pos.
   Precondition: accumulator MUST contain at least |n_bits|. */
static BROTLI_INLINE void BrotliTakeBits(BrotliBitReader* const br,
                                         brotli_reg_t n_bits,
                                         brotli_reg_t* val) {
  *val = BrotliGetBitsUnmasked(br) & BitMask(n_bits);
  BROTLI_LOG(("[BrotliTakeBits]  %d %d %d val: %6x\n",
              (int)BrotliBitReaderGetAvailIn(br), (int)br->bit_pos_,
              (int)n_bits, (int)*val));
  BrotliDropBits(br, n_bits);
}

/* Reads the specified number of bits from |br| and advances the bit pos.
   Assumes that there is enough input to perform BrotliFillBitWindow.
   Up to 24 bits are allowed to be requested from this method. */
static BROTLI_INLINE brotli_reg_t BrotliReadBits24(
    BrotliBitReader* const br, brotli_reg_t n_bits) {
  BROTLI_DCHECK(n_bits <= 24);
  if (BROTLI_64_BITS || (n_bits <= 16)) {
    brotli_reg_t val;
    BrotliFillBitWindow(br, n_bits);
    BrotliTakeBits(br, n_bits, &val);
    return val;
  } else {
    brotli_reg_t low_val;
    brotli_reg_t high_val;
    BrotliFillBitWindow(br, 16);
    BrotliTakeBits(br, 16, &low_val);
    BrotliFillBitWindow(br, 8);
    BrotliTakeBits(br, n_bits - 16, &high_val);
    return low_val | (high_val << 16);
  }
}

/* Same as BrotliReadBits24, but allows reading up to 32 bits. */
static BROTLI_INLINE brotli_reg_t BrotliReadBits32(
    BrotliBitReader* const br, brotli_reg_t n_bits) {
  BROTLI_DCHECK(n_bits <= 32);
  if (BROTLI_64_BITS || (n_bits <= 16)) {
    brotli_reg_t val;
    BrotliFillBitWindow(br, n_bits);
    BrotliTakeBits(br, n_bits, &val);
    return val;
  } else {
    brotli_reg_t low_val;
    brotli_reg_t high_val;
    BrotliFillBitWindow(br, 16);
    BrotliTakeBits(br, 16, &low_val);
    BrotliFillBitWindow(br, 16);
    BrotliTakeBits(br, n_bits - 16, &high_val);
    return low_val | (high_val << 16);
  }
}

/* Tries to read the specified amount of bits. Returns BROTLI_FALSE, if there
   is not enough input. |n_bits| MUST be positive.
   Up to 24 bits are allowed to be requested from this method. */
static BROTLI_INLINE BROTLI_BOOL BrotliSafeReadBits(
    BrotliBitReader* const br, brotli_reg_t n_bits, brotli_reg_t* val) {
  BROTLI_DCHECK(n_bits <= 24);
  while (BrotliGetAvailableBits(br) < n_bits) {
    if (!BrotliPullByte(br)) {
      return BROTLI_FALSE;
    }
  }
  BrotliTakeBits(br, n_bits, val);
  return BROTLI_TRUE;
}

/* Same as BrotliSafeReadBits, but allows reading up to 32 bits. */
static BROTLI_INLINE BROTLI_BOOL BrotliSafeReadBits32(
    BrotliBitReader* const br, brotli_reg_t n_bits, brotli_reg_t* val) {
  BROTLI_DCHECK(n_bits <= 32);
  if (BROTLI_64_BITS || (n_bits <= 24)) {
    while (BrotliGetAvailableBits(br) < n_bits) {
      if (!BrotliPullByte(br)) {
        return BROTLI_FALSE;
      }
    }
    BrotliTakeBits(br, n_bits, val);
    return BROTLI_TRUE;
  } else {
    return BrotliSafeReadBits32Slow(br, n_bits, val);
  }
}

/* Advances the bit reader position to the next byte boundary and verifies
   that any skipped bits are set to zero. */
static BROTLI_INLINE BROTLI_BOOL BrotliJumpToByteBoundary(BrotliBitReader* br) {
  brotli_reg_t pad_bits_count = BrotliGetAvailableBits(br) & 0x7;
  brotli_reg_t pad_bits = 0;
  if (pad_bits_count != 0) {
    BrotliTakeBits(br, pad_bits_count, &pad_bits);
  }
  BrotliBitReaderNormalize(br);
  return TO_BROTLI_BOOL(pad_bits == 0);
}

static BROTLI_INLINE void BrotliDropBytes(BrotliBitReader* br, size_t num) {
  /* Check detour is legal: accumulator must to be empty. */
  BROTLI_DCHECK(br->bit_pos_ == 0);
  BROTLI_DCHECK(br->val_ == 0);
  br->next_in += num;
}

/* Copies remaining input bytes stored in the bit reader to the output. Value
   |num| may not be larger than BrotliGetRemainingBytes. The bit reader must be
   warmed up again after this. */
static BROTLI_INLINE void BrotliCopyBytes(uint8_t* dest,
                                          BrotliBitReader* br, size_t num) {
  while (BrotliGetAvailableBits(br) >= 8 && num > 0) {
    *dest = (uint8_t)BrotliGetBitsUnmasked(br);
    BrotliDropBits(br, 8);
    ++dest;
    --num;
  }
  BrotliBitReaderNormalize(br);
  if (num > 0) {
    memcpy(dest, br->next_in, num);
    BrotliDropBytes(br, num);
  }
}

BROTLI_UNUSED_FUNCTION void BrotliBitReaderSuppressUnusedFunctions(void) {
  BROTLI_UNUSED(&BrotliBitReaderSuppressUnusedFunctions);

  BROTLI_UNUSED(&BrotliBitReaderGetAvailIn);
  BROTLI_UNUSED(&BrotliBitReaderLoadBits);
  BROTLI_UNUSED(&BrotliBitReaderRestoreState);
  BROTLI_UNUSED(&BrotliBitReaderSaveState);
  BROTLI_UNUSED(&BrotliBitReaderSetInput);
  BROTLI_UNUSED(&BrotliBitReaderUnload);
  BROTLI_UNUSED(&BrotliCheckInputAmount);
  BROTLI_UNUSED(&BrotliCopyBytes);
  BROTLI_UNUSED(&BrotliFillBitWindow16);
  BROTLI_UNUSED(&BrotliGet16BitsUnmasked);
  BROTLI_UNUSED(&BrotliGetBits);
  BROTLI_UNUSED(&BrotliGetRemainingBytes);
  BROTLI_UNUSED(&BrotliJumpToByteBoundary);
  BROTLI_UNUSED(&BrotliReadBits24);
  BROTLI_UNUSED(&BrotliReadBits32);
  BROTLI_UNUSED(&BrotliSafeGetBits);
  BROTLI_UNUSED(&BrotliSafeReadBits);
  BROTLI_UNUSED(&BrotliSafeReadBits32);
}

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_DEC_BIT_READER_H_ */
