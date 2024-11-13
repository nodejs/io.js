// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/simd-shuffle.h"

#include <algorithm>

#include "src/common/globals.h"

namespace v8 {
namespace internal {
namespace wasm {

bool SimdShuffle::TryMatchIdentity(const uint8_t* shuffle) {
  for (int i = 0; i < kSimd128Size; ++i) {
    if (shuffle[i] != i) return false;
  }
  return true;
}

bool SimdShuffle::TryMatch32x4Rotate(const uint8_t* shuffle,
                                     uint8_t* shuffle32x4, bool is_swizzle) {
  uint8_t offset;
  bool is_concat = TryMatchConcat(shuffle, &offset);
  DCHECK_NE(offset, 0);  // 0 is identity, it should not be matched.
  // Since we already have a concat shuffle, we know that the indices goes from:
  // [ offset, ..., 15, 0, ... ], it suffices to check that the offset points
  // to the low byte of a 32x4 element.
  if (!is_concat || !is_swizzle || offset % 4 != 0) {
    return false;
  }

  uint8_t offset_32 = offset / 4;
  for (int i = 0; i < 4; i++) {
    shuffle32x4[i] = (offset_32 + i) % 4;
  }
  return true;
}

bool SimdShuffle::TryMatch32x4Reverse(const uint8_t* shuffle32x4) {
  return shuffle32x4[0] == 3 && shuffle32x4[1] == 2 && shuffle32x4[2] == 1 &&
         shuffle32x4[3] == 0;
}

bool SimdShuffle::TryMatch32x4OneLaneSwizzle(const uint8_t* shuffle32x4,
                                             uint8_t* from_lane,
                                             uint8_t* to_lane) {
  constexpr uint32_t patterns[12]{
      0x30200000,  // 0 -> 1
      0x30000100,  // 0 -> 2
      0x00020100,  // 0 -> 3
      0x03020101,  // 1 -> 0
      0x03010100,  // 1 -> 2
      0x01020100,  // 1 -> 3
      0x03020102,  // 2 -> 0
      0x03020200,  // 2 -> 1
      0x02020100,  // 2 -> 3
      0x03020103,  // 3 -> 0
      0x03020300,  // 3 -> 1
      0x03030100   // 3 -> 2
  };

  unsigned pattern_idx = 0;
  uint32_t shuffle = *reinterpret_cast<const uint32_t*>(shuffle32x4);
#ifdef V8_TARGET_BIG_ENDIAN
  shuffle = base::bits::ReverseBytes(shuffle);
#endif
  for (unsigned from = 0; from < 4; ++from) {
    for (unsigned to = 0; to < 4; ++to) {
      if (from == to) {
        continue;
      }
      if (shuffle == patterns[pattern_idx]) {
        *from_lane = from;
        *to_lane = to;
        return true;
      }
      ++pattern_idx;
    }
  }
  return false;
}

bool SimdShuffle::TryMatch64x2Shuffle(const uint8_t* shuffle,
                                      uint8_t* shuffle64x2) {
  constexpr std::array<uint64_t, 2> element_patterns = {
      0x0706050403020100,  // 0
      0x0f0e0d0c0b0a0908   // 1
  };
  uint64_t low_shuffle = reinterpret_cast<const uint64_t*>(shuffle)[0];
  uint64_t high_shuffle = reinterpret_cast<const uint64_t*>(shuffle)[1];
#ifdef V8_TARGET_BIG_ENDIAN
  low_shuffle = base::bits::ReverseBytes(low_shuffle);
  high_shuffle = base::bits::ReverseBytes(high_shuffle);
#endif
  if (element_patterns[0] == low_shuffle) {
    shuffle64x2[0] = 0;
  } else if (element_patterns[1] == low_shuffle) {
    shuffle64x2[0] = 1;
  } else {
    return false;
  }
  if (element_patterns[0] == high_shuffle) {
    shuffle64x2[1] = 0;
  } else if (element_patterns[1] == high_shuffle) {
    shuffle64x2[1] = 1;
  } else {
    return false;
  }
  return true;
}

bool SimdShuffle::TryMatch32x4Shuffle(const uint8_t* shuffle,
                                      uint8_t* shuffle32x4) {
  for (int i = 0; i < 4; ++i) {
    if (shuffle[i * 4] % 4 != 0) return false;
    for (int j = 1; j < 4; ++j) {
      if (shuffle[i * 4 + j] - shuffle[i * 4 + j - 1] != 1) return false;
    }
    shuffle32x4[i] = shuffle[i * 4] / 4;
  }
  return true;
}

bool SimdShuffle::TryMatch32x8Shuffle(const uint8_t* shuffle,
                                      uint8_t* shuffle32x8) {
  for (int i = 0; i < 8; ++i) {
    if (shuffle[i * 4] % 4 != 0) return false;
    for (int j = 1; j < 4; ++j) {
      if (shuffle[i * 4 + j] - shuffle[i * 4 + j - 1] != 1) return false;
    }
    shuffle32x8[i] = shuffle[i * 4] / 4;
  }
  return true;
}

bool SimdShuffle::TryMatch16x8Shuffle(const uint8_t* shuffle,
                                      uint8_t* shuffle16x8) {
  for (int i = 0; i < 8; ++i) {
    if (shuffle[i * 2] % 2 != 0) return false;
    for (int j = 1; j < 2; ++j) {
      if (shuffle[i * 2 + j] - shuffle[i * 2 + j - 1] != 1) return false;
    }
    shuffle16x8[i] = shuffle[i * 2] / 2;
  }
  return true;
}

bool SimdShuffle::TryMatchConcat(const uint8_t* shuffle, uint8_t* offset) {
  // Don't match the identity shuffle (e.g. [0 1 2 ... 15]).
  uint8_t start = shuffle[0];
  if (start == 0) return false;
  DCHECK_GT(kSimd128Size, start);  // The shuffle should be canonicalized.
  // A concatenation is a series of consecutive indices, with at most one jump
  // in the middle from the last lane to the first.
  for (int i = 1; i < kSimd128Size; ++i) {
    if ((shuffle[i]) != ((shuffle[i - 1] + 1))) {
      if (shuffle[i - 1] != 15) return false;
      if (shuffle[i] % kSimd128Size != 0) return false;
    }
  }
  *offset = start;
  return true;
}

bool SimdShuffle::TryMatchBlend(const uint8_t* shuffle) {
  for (int i = 0; i < 16; ++i) {
    if ((shuffle[i] & 0xF) != i) return false;
  }
  return true;
}

bool SimdShuffle::TryMatchByteToDwordZeroExtend(const uint8_t* shuffle) {
  for (int i = 0; i < 16; ++i) {
    if ((i % 4 != 0) && (shuffle[i] < 16)) return false;
    if ((i % 4 == 0) && (shuffle[i] > 15 || (shuffle[i] != shuffle[0] + i / 4)))
      return false;
  }
  return true;
}

namespace {
// Try to match the first step in a 32x4 pairwise shuffle.
bool TryMatch32x4Pairwise(const uint8_t* shuffle) {
  // Pattern to select 32-bit element 1.
  constexpr uint8_t low_pattern_arr[4] = {4, 5, 6, 7};
  // And we'll check that element 1 is shuffled into element 0.
  uint32_t low_shuffle = reinterpret_cast<const uint32_t*>(shuffle)[0];
  // Pattern to select 32-bit element 3.
  constexpr uint8_t high_pattern_arr[4] = {12, 13, 14, 15};
  // And we'll check that element 3 is shuffled into element 2.
  uint32_t high_shuffle = reinterpret_cast<const uint32_t*>(shuffle)[2];
  uint32_t low_pattern = *reinterpret_cast<const uint32_t*>(low_pattern_arr);
  uint32_t high_pattern = *reinterpret_cast<const uint32_t*>(high_pattern_arr);
  return low_shuffle == low_pattern && high_shuffle == high_pattern;
}

// Try to match the final step in a 32x4, now 32x2, pairwise shuffle.
bool TryMatch32x2Pairwise(const uint8_t* shuffle) {
  // Pattern to select 32-bit element 2.
  constexpr uint8_t pattern_arr[4] = {8, 9, 10, 11};
  // And we'll check that element 2 is shuffled to element 0.
  uint32_t low_shuffle = reinterpret_cast<const uint32_t*>(shuffle)[0];
  uint32_t pattern = *reinterpret_cast<const uint32_t*>(pattern_arr);
  return low_shuffle == pattern;
}

// Try to match the first step in a upper-to-lower half shuffle.
bool TryMatchUpperToLowerFirst(const uint8_t* shuffle) {
  // There's 16 'active' bytes, so the pattern to select the upper half starts
  // at byte 8.
  constexpr uint8_t low_pattern_arr[8] = {8, 9, 10, 11, 12, 13, 14, 15};
  // And we'll check that the top half is shuffled into the lower.
  uint64_t low_shuffle = reinterpret_cast<const uint64_t*>(shuffle)[0];
  uint64_t low_pattern = *reinterpret_cast<const uint64_t*>(low_pattern_arr);
  return low_shuffle == low_pattern;
}

// Try to match the second step in a upper-to-lower half shuffle.
bool TryMatchUpperToLowerSecond(const uint8_t* shuffle) {
  // There's 8 'active' bytes, so the pattern to select the upper half starts
  // at byte 4.
  constexpr uint8_t low_pattern_arr[4] = {4, 5, 6, 7};
  // And we'll check that the top half is shuffled into the lower.
  uint32_t low_shuffle = reinterpret_cast<const uint32_t*>(shuffle)[0];
  uint32_t low_pattern = *reinterpret_cast<const uint32_t*>(low_pattern_arr);
  return low_shuffle == low_pattern;
}

// Try to match the third step in a upper-to-lower half shuffle.
bool TryMatchUpperToLowerThird(const uint8_t* shuffle) {
  // The vector now has 4 'active' bytes, select the top two.
  constexpr uint8_t low_pattern_arr[2] = {2, 3};
  // And check they're shuffled to the lower half.
  uint16_t low_shuffle = reinterpret_cast<const uint16_t*>(shuffle)[0];
  uint16_t low_pattern = *reinterpret_cast<const uint16_t*>(low_pattern_arr);
  return low_shuffle == low_pattern;
}

// Try to match the fourth step in a upper-to-lower half shuffle.
bool TryMatchUpperToLowerFourth(const uint8_t* shuffle) {
  return shuffle[0] == 1;
}
}  // end namespace

bool SimdShuffle::TryMatch8x16UpperToLowerReduce(const uint8_t* shuffle1,
                                                 const uint8_t* shuffle2,
                                                 const uint8_t* shuffle3,
                                                 const uint8_t* shuffle4) {
  return TryMatchUpperToLowerFirst(shuffle1) &&
         TryMatchUpperToLowerSecond(shuffle2) &&
         TryMatchUpperToLowerThird(shuffle3) &&
         TryMatchUpperToLowerFourth(shuffle4);
}

bool SimdShuffle::TryMatch16x8UpperToLowerReduce(const uint8_t* shuffle1,
                                                 const uint8_t* shuffle2,
                                                 const uint8_t* shuffle3) {
  return TryMatchUpperToLowerFirst(shuffle1) &&
         TryMatchUpperToLowerSecond(shuffle2) &&
         TryMatchUpperToLowerThird(shuffle3);
}

bool SimdShuffle::TryMatch32x4UpperToLowerReduce(const uint8_t* shuffle1,
                                                 const uint8_t* shuffle2) {
  return TryMatchUpperToLowerFirst(shuffle1) &&
         TryMatchUpperToLowerSecond(shuffle2);
}

bool SimdShuffle::TryMatch32x4PairwiseReduce(const uint8_t* shuffle1,
                                             const uint8_t* shuffle2) {
  return TryMatch32x4Pairwise(shuffle1) && TryMatch32x2Pairwise(shuffle2);
}

bool SimdShuffle::TryMatch64x2Reduce(const uint8_t* shuffle64x2) {
  return shuffle64x2[0] == 1;
}

uint8_t SimdShuffle::PackShuffle4(uint8_t* shuffle) {
  return (shuffle[0] & 3) | ((shuffle[1] & 3) << 2) | ((shuffle[2] & 3) << 4) |
         ((shuffle[3] & 3) << 6);
}

uint8_t SimdShuffle::PackBlend8(const uint8_t* shuffle16x8) {
  int8_t result = 0;
  for (int i = 0; i < 8; ++i) {
    result |= (shuffle16x8[i] >= 8 ? 1 : 0) << i;
  }
  return result;
}

uint8_t SimdShuffle::PackBlend4(const uint8_t* shuffle32x4) {
  int8_t result = 0;
  for (int i = 0; i < 4; ++i) {
    result |= (shuffle32x4[i] >= 4 ? 0x3 : 0) << (i * 2);
  }
  return result;
}

int32_t SimdShuffle::Pack4Lanes(const uint8_t* shuffle) {
  int32_t result = 0;
  for (int i = 3; i >= 0; --i) {
    result <<= 8;
    result |= shuffle[i];
  }
  return result;
}

void SimdShuffle::Pack16Lanes(uint32_t* dst, const uint8_t* shuffle) {
  for (int i = 0; i < 4; i++) {
    dst[i] = wasm::SimdShuffle::Pack4Lanes(shuffle + (i * 4));
  }
}

#ifdef V8_TARGET_ARCH_X64
// static
bool SimdShuffle::TryMatchVpshufd(const uint8_t* shuffle32x8,
                                  uint8_t* control) {
  *control = 0;
  for (int i = 0; i < 4; ++i) {
    uint8_t mask;
    if (shuffle32x8[i] < 4 && shuffle32x8[i + 4] - shuffle32x8[i] == 4) {
      mask = shuffle32x8[i];
      *control |= mask << (2 * i);
      continue;
    }
    return false;
  }
  return true;
}

// static
bool SimdShuffle::TryMatchShufps256(const uint8_t* shuffle32x8,
                                    uint8_t* control) {
  *control = 0;
  for (int i = 0; i < 4; ++i) {
    // low 128-bits and high 128-bits should have the same shuffle order.
    if (shuffle32x8[i + 4] - shuffle32x8[i] == 4) {
      // [63:0]   bits select from SRC1,
      // [127:64] bits select from SRC2
      if ((i < 2 && shuffle32x8[i] < 4) ||
          (i >= 2 && shuffle32x8[i] >= 8 && shuffle32x8[i] < 12)) {
        *control |= (shuffle32x8[i] % 4) << (2 * i);
        continue;
      }
      return false;
    }
    return false;
  }
  return true;
}
#endif  // V8_TARGET_ARCH_X64

bool SimdSwizzle::AllInRangeOrTopBitSet(
    std::array<uint8_t, kSimd128Size> shuffle) {
  return std::all_of(shuffle.begin(), shuffle.end(),
                     [](auto i) { return (i < kSimd128Size) || (i & 0x80); });
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
