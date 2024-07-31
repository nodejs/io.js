/* Copyright 2010 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Write bits into a byte array. */

#ifndef BROTLI_ENC_WRITE_BITS_H_
#define BROTLI_ENC_WRITE_BITS_H_

#include <brotli/types.h>

#include "../common/platform.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* This function writes bits into bytes in increasing addresses, and within
   a byte least-significant-bit first.

   The function can write up to 56 bits in one go with WriteBits
   Example: let's assume that 3 bits (Rs below) have been written already:

   BYTE-0     BYTE+1       BYTE+2

   0000 0RRR    0000 0000    0000 0000

   Now, we could write 5 or less bits in MSB by just shifting by 3
   and OR'ing to BYTE-0.

   For n bits, we take the last 5 bits, OR that with high bits in BYTE-0,
   and locate the rest in BYTE+1, BYTE+2, etc. */
static BROTLI_INLINE void BrotliWriteBits(size_t n_bits,
                                          uint64_t bits,
                                          size_t* BROTLI_RESTRICT pos,
                                          uint8_t* BROTLI_RESTRICT array) {
  BROTLI_LOG(("WriteBits  %2d  0x%08x%08x  %10d\n", (int)n_bits,
      (uint32_t)(bits >> 32), (uint32_t)(bits & 0xFFFFFFFF),
      (int)*pos));
  BROTLI_DCHECK((bits >> n_bits) == 0);
  BROTLI_DCHECK(n_bits <= 56);
#if defined(BROTLI_LITTLE_ENDIAN)
  /* This branch of the code can write up to 56 bits at a time,
     7 bits are lost by being perhaps already in *p and at least
     1 bit is needed to initialize the bit-stream ahead (i.e. if 7
     bits are in *p and we write 57 bits, then the next write will
     access a byte that was never initialized). */
  {
    uint8_t* p = &array[*pos >> 3];
    uint64_t v = (uint64_t)(*p);  /* Zero-extend 8 to 64 bits. */
    v |= bits << (*pos & 7);
    BROTLI_UNALIGNED_STORE64LE(p, v);  /* Set some bits. */
    *pos += n_bits;
  }
#else
  /* implicit & 0xFF is assumed for uint8_t arithmetics */
  {
    uint8_t* array_pos = &array[*pos >> 3];
    const size_t bits_reserved_in_first_byte = (*pos & 7);
    size_t bits_left_to_write;
    bits <<= bits_reserved_in_first_byte;
    *array_pos++ |= (uint8_t)bits;
    for (bits_left_to_write = n_bits + bits_reserved_in_first_byte;
         bits_left_to_write >= 9;
         bits_left_to_write -= 8) {
      bits >>= 8;
      *array_pos++ = (uint8_t)bits;
    }
    *array_pos = 0;
    *pos += n_bits;
  }
#endif
}

static BROTLI_INLINE void BrotliWriteBitsPrepareStorage(
    size_t pos, uint8_t* array) {
  BROTLI_LOG(("WriteBitsPrepareStorage            %10d\n", (int)pos));
  BROTLI_DCHECK((pos & 7) == 0);
  array[pos >> 3] = 0;
}

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_ENC_WRITE_BITS_H_ */
