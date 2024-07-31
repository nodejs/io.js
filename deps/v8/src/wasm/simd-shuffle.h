// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_SIMD_SHUFFLE_H_
#define V8_WASM_SIMD_SHUFFLE_H_

#include "src/base/macros.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {
namespace wasm {

class V8_EXPORT_PRIVATE SimdShuffle {
 public:
  // is in the range [0 .. 15] (or [0 .. 31] if simd_size is kSimd256Size). Set
  // |inputs_equal| true if this is an explicit swizzle. Returns canonicalized
  // |shuffle|, |needs_swap|, and |is_swizzle|. If |needs_swap| is true, inputs
  // must be swapped. If |is_swizzle| is true, the second input can be ignored.
  template <const int simd_size = kSimd128Size,
            typename = std::enable_if_t<simd_size == kSimd128Size ||
                                        simd_size == kSimd256Size>>
  static void CanonicalizeShuffle(bool inputs_equal, uint8_t* shuffle,
                                  bool* needs_swap, bool* is_swizzle) {
    *needs_swap = false;
    // Inputs equal, then it's a swizzle.
    if (inputs_equal) {
      *is_swizzle = true;
    } else {
      // Inputs are distinct; check that both are required.
      bool src0_is_used = false;
      bool src1_is_used = false;
      for (int i = 0; i < simd_size; ++i) {
        if (shuffle[i] < simd_size) {
          src0_is_used = true;
        } else {
          src1_is_used = true;
        }
      }
      if (src0_is_used && !src1_is_used) {
        *is_swizzle = true;
      } else if (src1_is_used && !src0_is_used) {
        *needs_swap = true;
        *is_swizzle = true;
      } else {
        *is_swizzle = false;
        // Canonicalize general 2 input shuffles so that the first input lanes
        // are encountered first. This makes architectural shuffle pattern
        // matching easier, since we only need to consider 1 input ordering
        // instead of 2.
        if (shuffle[0] >= simd_size) {
          // The second operand is used first. Swap inputs and adjust the
          // shuffle.
          *needs_swap = true;
          for (int i = 0; i < simd_size; ++i) {
            shuffle[i] ^= simd_size;
          }
        }
      }
    }
    if (*is_swizzle) {
      for (int i = 0; i < simd_size; ++i) shuffle[i] &= simd_size - 1;
    }
  }

  // Tries to match an 8x16 byte shuffle to the identity shuffle, which is
  // [0 1 ... 15]. This should be called after canonicalizing the shuffle, so
  // the second identity shuffle, [16 17 .. 31] is converted to the first one.
  static bool TryMatchIdentity(const uint8_t* shuffle);

  // Tries to match a byte shuffle to a scalar splat operation. Returns the
  // index of the lane if successful.
  template <int LANES>
  static bool TryMatchSplat(const uint8_t* shuffle, int* index) {
    const int kBytesPerLane = kSimd128Size / LANES;
    // Get the first lane's worth of bytes and check that indices start at a
    // lane boundary and are consecutive.
    uint8_t lane0[kBytesPerLane];
    lane0[0] = shuffle[0];
    if (lane0[0] % kBytesPerLane != 0) return false;
    for (int i = 1; i < kBytesPerLane; ++i) {
      lane0[i] = shuffle[i];
      if (lane0[i] != lane0[0] + i) return false;
    }
    // Now check that the other lanes are identical to lane0.
    for (int i = 1; i < LANES; ++i) {
      for (int j = 0; j < kBytesPerLane; ++j) {
        if (lane0[j] != shuffle[i * kBytesPerLane + j]) return false;
      }
    }
    *index = lane0[0] / kBytesPerLane;
    return true;
  }

  // Tries to match a 32x4 rotate, only makes sense if the inputs are equal
  // (is_swizzle). A rotation is a shuffle like [1, 2, 3, 0]. This will always
  // match a Concat, but can have better codegen.
  static bool TryMatch32x4Rotate(const uint8_t* shuffle, uint8_t* shuffle32x4,
                                 bool is_swizzle);

  // Tries to match a 32x4 reverse shuffle: [3, 2, 1, 0].
  static bool TryMatch32x4Reverse(const uint8_t* shuffle32x4);

  // Tries to match a one lane copy of 4x32.
  static bool TryMatch32x4OneLaneSwizzle(const uint8_t* shuffle32x4,
                                         uint8_t* from, uint8_t* to);

  // Tries to match an 8x16 byte shuffle to an equivalent 32x4 shuffle. If
  // successful, it writes the 32x4 shuffle word indices. E.g.
  // [0 1 2 3 8 9 10 11 4 5 6 7 12 13 14 15] == [0 2 1 3]
  static bool TryMatch32x4Shuffle(const uint8_t* shuffle, uint8_t* shuffle32x4);

  // Tries to match an 8x32 byte shuffle to an equivalent 32x8 shuffle. If
  // successful, it writes the 32x8 shuffle word indices. E.g.
  // [0 1 2 3 8 9 10 11 4 5 6 7 12 13 14 15 16 17 18 19 24 25 26 27 20 21 22 23
  //  28 29 30 31 == [0 2 1 3 4 6 5 7]
  static bool TryMatch32x8Shuffle(const uint8_t* shuffle, uint8_t* shuffle32x8);

  // Tries to match an 8x16 byte shuffle to an equivalent 16x8 shuffle. If
  // successful, it writes the 16x8 shuffle word indices. E.g.
  // [0 1 8 9 2 3 10 11 4 5 12 13 6 7 14 15] == [0 4 1 5 2 6 3 7]
  static bool TryMatch16x8Shuffle(const uint8_t* shuffle, uint8_t* shuffle16x8);

  // Tries to match a byte shuffle to a concatenate operation, formed by taking
  // 16 bytes from the 32 byte concatenation of the inputs.  If successful, it
  // writes the byte offset. E.g. [4 5 6 7 .. 16 17 18 19] concatenates both
  // source vectors with offset 4. The shuffle should be canonicalized.
  static bool TryMatchConcat(const uint8_t* shuffle, uint8_t* offset);

  // Tries to match a byte shuffle to a blend operation, which is a shuffle
  // where no lanes change position. E.g. [0 9 2 11 .. 14 31] interleaves the
  // even lanes of the first source with the odd lanes of the second.  The
  // shuffle should be canonicalized.
  static bool TryMatchBlend(const uint8_t* shuffle);

  // Tries to match a byte shuffle to a packed byte to dword zero extend
  // operation. E.g. [8 x x x 9 x x x 10 x x x 11 x x x ] (x is arbitrary value
  // large than 15). The shuffle should be canonicalized. Its second input
  // should be zero.
  static bool TryMatchByteToDwordZeroExtend(const uint8_t* shuffle);

  // Packs a 4 lane shuffle into a single imm8 suitable for use by pshufd,
  // pshuflw, and pshufhw.
  static uint8_t PackShuffle4(uint8_t* shuffle);
  // Gets an 8 bit lane mask suitable for 16x8 pblendw.
  static uint8_t PackBlend8(const uint8_t* shuffle16x8);
  // Gets an 8 bit lane mask suitable for 32x4 pblendw.
  static uint8_t PackBlend4(const uint8_t* shuffle32x4);
  // Packs 4 bytes of shuffle into a 32 bit immediate.
  static int32_t Pack4Lanes(const uint8_t* shuffle);
  // Packs 16 bytes of shuffle into an array of 4 uint32_t.
  static void Pack16Lanes(uint32_t* dst, const uint8_t* shuffle);
};

class V8_EXPORT_PRIVATE SimdSwizzle {
 public:
  // Checks if all the immediates are in range (< kSimd128Size), and if they are
  // not, the top bit is set.
  static bool AllInRangeOrTopBitSet(std::array<uint8_t, kSimd128Size> shuffle);
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_SIMD_SHUFFLE_H_
