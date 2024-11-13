// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/simd-shuffle.h"

#include "test/unittests/test-utils.h"
#include "testing/gmock-support.h"

using ::testing::ElementsAre;

namespace v8 {
namespace internal {
namespace wasm {
// Helper to make calls to private wasm shuffle functions.
class SimdShuffleTest : public ::testing::Test {
 public:
  template <int Size, typename = std::enable_if_t<Size == kSimd128Size ||
                                                  Size == kSimd256Size>>
  using Shuffle = std::array<uint8_t, Size>;

  template <int Size, typename = std::enable_if_t<Size == kSimd128Size ||
                                                  Size == kSimd256Size>>
  struct TestShuffle {
    Shuffle<Size> non_canonical;
    Shuffle<Size> canonical;
    bool needs_swap;
    bool is_swizzle;
  };

  // Call testing members in wasm.
  static void CanonicalizeShuffle(bool inputs_equal,
                                  Shuffle<kSimd128Size>* shuffle,
                                  bool* needs_swap, bool* is_swizzle) {
    SimdShuffle::CanonicalizeShuffle(inputs_equal, &(*shuffle)[0], needs_swap,
                                     is_swizzle);
  }

  static bool TryMatchIdentity(const Shuffle<kSimd128Size>& shuffle) {
    return SimdShuffle::TryMatchIdentity(&shuffle[0]);
  }
  template <int LANES>
  static bool TryMatchSplat(const Shuffle<kSimd128Size>& shuffle, int* index) {
    return SimdShuffle::TryMatchSplat<LANES>(&shuffle[0], index);
  }
  static bool TryMatch64x2Shuffle(const Shuffle<kSimd128Size>& shuffle,
                                  uint8_t* shuffle64x2) {
    return SimdShuffle::TryMatch64x2Shuffle(&shuffle[0], shuffle64x2);
  }
  static bool TryMatch32x4Shuffle(const Shuffle<kSimd128Size>& shuffle,
                                  uint8_t* shuffle32x4) {
    return SimdShuffle::TryMatch32x4Shuffle(&shuffle[0], shuffle32x4);
  }
  static bool TryMatch32x8Shuffle(const Shuffle<kSimd256Size>& shuffle,
                                  uint8_t* shuffle32x8) {
    return SimdShuffle::TryMatch32x8Shuffle(&shuffle[0], shuffle32x8);
  }
  static bool TryMatch32x4Reverse(const uint8_t* shuffle32x4) {
    return SimdShuffle::TryMatch32x4Reverse(shuffle32x4);
  }
  static bool TryMatch32x4OneLaneSwizzle(const uint8_t* shuffle32x4,
                                         uint8_t* from, uint8_t* to) {
    return SimdShuffle::TryMatch32x4OneLaneSwizzle(shuffle32x4, from, to);
  }
  static bool TryMatch16x8Shuffle(const Shuffle<kSimd128Size>& shuffle,
                                  uint8_t* shuffle16x8) {
    return SimdShuffle::TryMatch16x8Shuffle(&shuffle[0], shuffle16x8);
  }
  static bool TryMatchConcat(const Shuffle<kSimd128Size>& shuffle,
                             uint8_t* offset) {
    return SimdShuffle::TryMatchConcat(&shuffle[0], offset);
  }
  static bool TryMatchBlend(const Shuffle<kSimd128Size>& shuffle) {
    return SimdShuffle::TryMatchBlend(&shuffle[0]);
  }
#ifdef V8_TARGET_ARCH_X64
  static bool TryMatchVpshufd(const uint8_t* shuffle32x8, uint8_t* control) {
    return SimdShuffle::TryMatchVpshufd(shuffle32x8, control);
  }
  static bool TryMatchShufps256(const uint8_t* shuffle32x8, uint8_t* control) {
    return SimdShuffle::TryMatchShufps256(shuffle32x8, control);
  }
#endif  // V8_TARGET_ARCH_X64
};

template <int Size, typename = std::enable_if_t<Size == kSimd128Size ||
                                                Size == kSimd256Size>>
bool operator==(const SimdShuffleTest::Shuffle<Size>& a,
                const SimdShuffleTest::Shuffle<Size>& b) {
  for (int i = 0; i < Size; ++i) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

TEST_F(SimdShuffleTest, CanonicalizeShuffle) {
  const bool kInputsEqual = true;
  const bool kNeedsSwap = true;
  const bool kIsSwizzle = true;

  bool needs_swap;
  bool is_swizzle;

  // Test canonicalization driven by input shuffle.
  TestShuffle<kSimd128Size> test_shuffles[] = {
      // Identity is canonical.
      {{{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}},
       {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}},
       !kNeedsSwap,
       kIsSwizzle},
      // Non-canonical identity requires a swap.
      {{{16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31}},
       {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}},
       kNeedsSwap,
       kIsSwizzle},
      // General shuffle, canonical is unchanged.
      {{{0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23}},
       {{0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23}},
       !kNeedsSwap,
       !kIsSwizzle},
      // Non-canonical shuffle requires a swap.
      {{{16, 0, 17, 1, 18, 2, 19, 3, 20, 4, 21, 5, 22, 6, 23, 7}},
       {{0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23}},
       kNeedsSwap,
       !kIsSwizzle},
  };
  for (size_t i = 0; i < arraysize(test_shuffles); ++i) {
    Shuffle<kSimd128Size> shuffle = test_shuffles[i].non_canonical;
    CanonicalizeShuffle(!kInputsEqual, &shuffle, &needs_swap, &is_swizzle);
    EXPECT_EQ(shuffle, test_shuffles[i].canonical);
    EXPECT_EQ(needs_swap, test_shuffles[i].needs_swap);
    EXPECT_EQ(is_swizzle, test_shuffles[i].is_swizzle);
  }

  // Test canonicalization when inputs are equal (explicit swizzle).
  TestShuffle<kSimd128Size> test_swizzles[] = {
      // Identity is canonical.
      {{{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}},
       {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}},
       !kNeedsSwap,
       kIsSwizzle},
      // Non-canonical identity requires a swap.
      {{{16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31}},
       {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}},
       !kNeedsSwap,
       kIsSwizzle},
      // Canonicalized to swizzle.
      {{{0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23}},
       {{0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7}},
       !kNeedsSwap,
       kIsSwizzle},
      // Canonicalized to swizzle.
      {{{16, 0, 17, 1, 18, 2, 19, 3, 20, 4, 21, 5, 22, 6, 23, 7}},
       {{0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7}},
       !kNeedsSwap,
       kIsSwizzle},
  };
  for (size_t i = 0; i < arraysize(test_swizzles); ++i) {
    Shuffle<kSimd128Size> shuffle = test_swizzles[i].non_canonical;
    CanonicalizeShuffle(kInputsEqual, &shuffle, &needs_swap, &is_swizzle);
    EXPECT_EQ(shuffle, test_swizzles[i].canonical);
    EXPECT_EQ(needs_swap, test_swizzles[i].needs_swap);
    EXPECT_EQ(is_swizzle, test_swizzles[i].is_swizzle);
  }
}

TEST_F(SimdShuffleTest, TryMatchIdentity) {
  // Match shuffle that returns first source operand.
  EXPECT_TRUE(TryMatchIdentity(
      {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}}));
  // The non-canonicalized identity shuffle doesn't match.
  EXPECT_FALSE(TryMatchIdentity(
      {{16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31}}));
  // Even one lane out of place is not an identity shuffle.
  EXPECT_FALSE(TryMatchIdentity(
      {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 31}}));
}

TEST_F(SimdShuffleTest, TryMatchSplat) {
  int index;
  // All lanes from the same 32 bit source lane.
  EXPECT_TRUE(TryMatchSplat<4>(
      {{4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7}}, &index));
  EXPECT_EQ(1, index);
  // It shouldn't match for other vector shapes.
  EXPECT_FALSE(TryMatchSplat<8>(
      {{4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7}}, &index));
  EXPECT_FALSE(TryMatchSplat<16>(
      {{4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7}}, &index));
  // All lanes from the same 16 bit source lane.
  EXPECT_TRUE(TryMatchSplat<8>(
      {{16, 17, 16, 17, 16, 17, 16, 17, 16, 17, 16, 17, 16, 17, 16, 17}},
      &index));
  EXPECT_EQ(8, index);
  // It shouldn't match for other vector shapes.
  EXPECT_FALSE(TryMatchSplat<4>(
      {{16, 17, 16, 17, 16, 17, 16, 17, 16, 17, 16, 17, 16, 17, 16, 17}},
      &index));
  EXPECT_FALSE(TryMatchSplat<16>(
      {{16, 17, 16, 17, 16, 17, 16, 17, 16, 17, 16, 17, 16, 17, 16, 17}},
      &index));
  // All lanes from the same 8 bit source lane.
  EXPECT_TRUE(TryMatchSplat<16>(
      {{7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7}}, &index));
  EXPECT_EQ(7, index);
  // It shouldn't match for other vector shapes.
  EXPECT_FALSE(TryMatchSplat<4>(
      {{7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7}}, &index));
  EXPECT_FALSE(TryMatchSplat<8>(
      {{7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7}}, &index));
}

TEST_F(SimdShuffleTest, TryMatchConcat) {
  uint8_t offset;
  // Ascending indices, jump at end to same input (concatenating swizzle).
  EXPECT_TRUE(TryMatchConcat(
      {{3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2}}, &offset));
  EXPECT_EQ(3, offset);
  // Ascending indices, jump at end to other input (concatenating shuffle).
  EXPECT_TRUE(TryMatchConcat(
      {{4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19}}, &offset));
  EXPECT_EQ(4, offset);

  // Shuffles that should not match:
  // Ascending indices, but jump isn't at end/beginning.
  EXPECT_FALSE(TryMatchConcat(
      {{3, 4, 5, 6, 7, 8, 9, 10, 11, 0, 1, 2, 3, 4, 5, 6}}, &offset));
  // Ascending indices, but multiple jumps.
  EXPECT_FALSE(TryMatchConcat(
      {{0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3}}, &offset));
}

TEST_F(SimdShuffleTest, TryMatch32x4Shuffle) {
  uint8_t shuffle32x4[4];
  // Match if each group of 4 bytes is from the same 32 bit lane.
  EXPECT_TRUE(TryMatch32x4Shuffle(
      {{12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 16, 17, 18, 19}},
      shuffle32x4));
  EXPECT_EQ(3, shuffle32x4[0]);
  EXPECT_EQ(2, shuffle32x4[1]);
  EXPECT_EQ(1, shuffle32x4[2]);
  EXPECT_EQ(4, shuffle32x4[3]);
  // Bytes must be in order in the 32 bit lane.
  EXPECT_FALSE(TryMatch32x4Shuffle(
      {{12, 13, 14, 14, 8, 9, 10, 11, 4, 5, 6, 7, 16, 17, 18, 19}},
      shuffle32x4));
  // Each group must start with the first byte in the 32 bit lane.
  EXPECT_FALSE(TryMatch32x4Shuffle(
      {{13, 14, 15, 12, 8, 9, 10, 11, 4, 5, 6, 7, 16, 17, 18, 19}},
      shuffle32x4));
}

TEST_F(SimdShuffleTest, TryMatch32x8Shuffle) {
  uint8_t shuffle32x8[8];
  // Match if each group of 4 bytes is from the same 32 bit lane.
  EXPECT_TRUE(TryMatch32x8Shuffle(
      {{12, 13, 14, 15, 8,  9,  10, 11, 4,  5,  6,  7,  16, 17, 18, 19,
        20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 0,  1,  2,  3}},
      shuffle32x8));
  EXPECT_EQ(3, shuffle32x8[0]);
  EXPECT_EQ(2, shuffle32x8[1]);
  EXPECT_EQ(1, shuffle32x8[2]);
  EXPECT_EQ(4, shuffle32x8[3]);
  EXPECT_EQ(5, shuffle32x8[4]);
  EXPECT_EQ(6, shuffle32x8[5]);
  EXPECT_EQ(7, shuffle32x8[6]);
  EXPECT_EQ(0, shuffle32x8[7]);
  // Bytes must be in order in the 32 bit lane.
  EXPECT_FALSE(TryMatch32x8Shuffle(
      {{12, 13, 14, 14, 8,  9,  10, 11, 4,  5,  6,  7,  16, 17, 18, 19,
        20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 0,  1,  2,  3}},
      shuffle32x8));
  // Each group must start with the first byte in the 32 bit lane.
  EXPECT_FALSE(TryMatch32x8Shuffle(
      {{13, 14, 15, 12, 8,  9,  10, 11, 4,  5,  6,  7,  16, 17, 18, 19,
        20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 0,  1,  2,  3}},
      shuffle32x8));
}

TEST_F(SimdShuffleTest, TryMatch32x4Reverse) {
  uint8_t shuffle32x4[4];
  // low
  EXPECT_TRUE(TryMatch32x4Shuffle(
      {{12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3}}, shuffle32x4));
  EXPECT_EQ(3, shuffle32x4[0]);
  EXPECT_EQ(2, shuffle32x4[1]);
  EXPECT_EQ(1, shuffle32x4[2]);
  EXPECT_EQ(0, shuffle32x4[3]);
  EXPECT_TRUE(TryMatch32x4Reverse(shuffle32x4));

  // high
  Shuffle<kSimd128Size> high_rev = {28, 29, 30, 31, 24, 25, 26, 27,
                                    20, 21, 22, 23, 16, 17, 18, 19};
  EXPECT_TRUE(TryMatch32x4Shuffle(high_rev, shuffle32x4));
  EXPECT_EQ(7, shuffle32x4[0]);
  EXPECT_EQ(6, shuffle32x4[1]);
  EXPECT_EQ(5, shuffle32x4[2]);
  EXPECT_EQ(4, shuffle32x4[3]);

  bool needs_swap = false;
  bool is_swizzle = false;
  CanonicalizeShuffle(false, &high_rev, &needs_swap, &is_swizzle);
  EXPECT_TRUE(needs_swap);
  EXPECT_TRUE(is_swizzle);
  EXPECT_TRUE(TryMatch32x4Shuffle(high_rev, shuffle32x4));
  EXPECT_TRUE(TryMatch32x4Reverse(shuffle32x4));
}

TEST_F(SimdShuffleTest, TryMatch32x4OneLaneSwizzle) {
  uint8_t shuffle32x4[4];
  uint8_t from = 0;
  uint8_t to = 0;
  // low
  EXPECT_TRUE(TryMatch32x4Shuffle(
      {{12, 13, 14, 15, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}},
      shuffle32x4));
  EXPECT_EQ(3, shuffle32x4[0]);
  EXPECT_EQ(1, shuffle32x4[1]);
  EXPECT_EQ(2, shuffle32x4[2]);
  EXPECT_EQ(3, shuffle32x4[3]);
  EXPECT_TRUE(TryMatch32x4OneLaneSwizzle(shuffle32x4, &from, &to));
  EXPECT_EQ(from, 3);
  EXPECT_EQ(to, 0);

  // high
  Shuffle<kSimd128Size> high_one = {16, 17, 18, 19, 20, 21, 22, 23,
                                    20, 21, 22, 23, 28, 29, 30, 31};
  EXPECT_TRUE(TryMatch32x4Shuffle(high_one, shuffle32x4));
  EXPECT_EQ(4, shuffle32x4[0]);
  EXPECT_EQ(5, shuffle32x4[1]);
  EXPECT_EQ(5, shuffle32x4[2]);
  EXPECT_EQ(7, shuffle32x4[3]);

  bool needs_swap = false;
  bool is_swizzle = false;
  CanonicalizeShuffle(false, &high_one, &needs_swap, &is_swizzle);
  EXPECT_TRUE(needs_swap);
  EXPECT_TRUE(is_swizzle);
  EXPECT_TRUE(TryMatch32x4Shuffle(high_one, shuffle32x4));
  EXPECT_TRUE(TryMatch32x4OneLaneSwizzle(shuffle32x4, &from, &to));
  EXPECT_EQ(from, 1);
  EXPECT_EQ(to, 2);
}

TEST_F(SimdShuffleTest, TryMatch16x8Shuffle) {
  uint8_t shuffle16x8[8];
  // Match if each group of 2 bytes is from the same 16 bit lane.
  EXPECT_TRUE(TryMatch16x8Shuffle(
      {{12, 13, 30, 31, 8, 9, 26, 27, 4, 5, 22, 23, 16, 17, 2, 3}},
      shuffle16x8));
  EXPECT_EQ(6, shuffle16x8[0]);
  EXPECT_EQ(15, shuffle16x8[1]);
  EXPECT_EQ(4, shuffle16x8[2]);
  EXPECT_EQ(13, shuffle16x8[3]);
  EXPECT_EQ(2, shuffle16x8[4]);
  EXPECT_EQ(11, shuffle16x8[5]);
  EXPECT_EQ(8, shuffle16x8[6]);
  EXPECT_EQ(1, shuffle16x8[7]);
  // Bytes must be in order in the 16 bit lane.
  EXPECT_FALSE(TryMatch16x8Shuffle(
      {{12, 13, 30, 30, 8, 9, 26, 27, 4, 5, 22, 23, 16, 17, 2, 3}},
      shuffle16x8));
  // Each group must start with the first byte in the 16 bit lane.
  EXPECT_FALSE(TryMatch16x8Shuffle(
      {{12, 13, 31, 30, 8, 9, 26, 27, 4, 5, 22, 23, 16, 17, 2, 3}},
      shuffle16x8));
}

TEST_F(SimdShuffleTest, TryMatchBlend) {
  // Match if each byte remains in place.
  EXPECT_TRUE(TryMatchBlend(
      {{0, 17, 2, 19, 4, 21, 6, 23, 8, 25, 10, 27, 12, 29, 14, 31}}));
  // Identity is a blend.
  EXPECT_TRUE(
      TryMatchBlend({{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}}));
  // Even one lane out of place is not a blend.
  EXPECT_FALSE(TryMatchBlend(
      {{1, 17, 2, 19, 4, 21, 6, 23, 8, 25, 10, 27, 12, 29, 14, 31}}));
}

TEST_F(SimdShuffleTest, PairwiseReduce) {
  uint8_t shuffle64x2[2];
  EXPECT_TRUE(TryMatch64x2Shuffle(
      {{8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7}}, shuffle64x2));
  EXPECT_TRUE(SimdShuffle::TryMatch64x2Reduce(shuffle64x2));

  constexpr uint8_t pairwise_32x4[] = {4,  5,  6,  7,  0, 1, 2, 3,
                                       12, 13, 14, 15, 0, 1, 2, 3};
  constexpr uint8_t pairwise_32x2[] = {8, 9, 10, 11, 0, 1, 2, 3,
                                       0, 1, 2,  3,  0, 1, 2, 3};
  EXPECT_TRUE(
      SimdShuffle::TryMatch32x4PairwiseReduce(pairwise_32x4, pairwise_32x2));
}

TEST_F(SimdShuffleTest, UpperToLowerReduce) {
  constexpr uint8_t upper_to_lower_32x4[] = {8, 9, 10, 11, 12, 13, 14, 15,
                                             0, 1, 2,  3,  0,  1,  2,  3};
  constexpr uint8_t upper_to_lower_32x2[] = {4, 5, 6, 7, 0, 1, 2, 3,
                                             0, 1, 2, 3, 0, 1, 2, 3};
  EXPECT_TRUE(SimdShuffle::TryMatch32x4UpperToLowerReduce(upper_to_lower_32x4,
                                                          upper_to_lower_32x2));

  constexpr uint8_t upper_to_lower_16x8[] = {8, 9, 10, 11, 12, 13, 14, 15, 0,
                                             1, 0, 1,  0,  1,  0,  1,  0};
  constexpr uint8_t upper_to_lower_16x4[] = {4, 5, 6, 7, 0, 1, 0, 1,
                                             0, 1, 0, 1, 0, 1, 0, 1};
  constexpr uint8_t upper_to_lower_16x2[] = {2, 3, 0, 1, 0, 1, 0, 1,
                                             0, 1, 0, 1, 0, 1, 0, 1};
  EXPECT_TRUE(SimdShuffle::TryMatch16x8UpperToLowerReduce(
      upper_to_lower_16x8, upper_to_lower_16x4, upper_to_lower_16x2));

  constexpr uint8_t upper_to_lower_8x16[] = {8, 9, 10, 11, 12, 13, 14, 15, 0,
                                             1, 0, 1,  0,  1,  0,  1,  0};
  constexpr uint8_t upper_to_lower_8x8[] = {4, 5, 6, 7, 0, 1, 0, 1,
                                            0, 1, 0, 1, 0, 1, 0, 1};
  constexpr uint8_t upper_to_lower_8x4[] = {2, 3, 0, 1, 0, 1, 0, 1,
                                            0, 1, 0, 1, 0, 1, 0, 1};
  constexpr uint8_t upper_to_lower_8x2[] = {1, 0, 0, 1, 0, 1, 0, 1,
                                            0, 1, 0, 1, 0, 1, 0, 1};
  EXPECT_TRUE(SimdShuffle::TryMatch8x16UpperToLowerReduce(
      upper_to_lower_8x16, upper_to_lower_8x8, upper_to_lower_8x4,
      upper_to_lower_8x2));
}

TEST_F(SimdShuffleTest, Shuffle64x2) {
  constexpr uint8_t identity_64x2[] = {0, 1, 2,  3,  4,  5,  6,  7,
                                       8, 9, 10, 11, 12, 13, 14, 15};
  uint8_t shuffle64x2[2];
  EXPECT_TRUE(SimdShuffle::TryMatch64x2Shuffle(identity_64x2, shuffle64x2));
  EXPECT_EQ(shuffle64x2[0], 0);
  EXPECT_EQ(shuffle64x2[1], 1);

  constexpr uint8_t rev_64x2[] = {8, 9, 10, 11, 12, 13, 14, 15,
                                  0, 1, 2,  3,  4,  5,  6,  7};
  EXPECT_TRUE(SimdShuffle::TryMatch64x2Shuffle(rev_64x2, shuffle64x2));
  EXPECT_EQ(shuffle64x2[0], 1);
  EXPECT_EQ(shuffle64x2[1], 0);

  constexpr uint8_t dup0_64x2[] = {0, 1, 2, 3, 4, 5, 6, 7,
                                   0, 1, 2, 3, 4, 5, 6, 7};
  EXPECT_TRUE(SimdShuffle::TryMatch64x2Shuffle(dup0_64x2, shuffle64x2));
  EXPECT_EQ(shuffle64x2[0], 0);
  EXPECT_EQ(shuffle64x2[1], 0);

  constexpr uint8_t dup1_64x2[] = {8, 9, 10, 11, 12, 13, 14, 15,
                                   8, 9, 10, 11, 12, 13, 14, 15};
  EXPECT_TRUE(SimdShuffle::TryMatch64x2Shuffle(dup1_64x2, shuffle64x2));
  EXPECT_EQ(shuffle64x2[0], 1);
  EXPECT_EQ(shuffle64x2[1], 1);
}

TEST(SimdShufflePackTest, PackShuffle4) {
  uint8_t arr[4]{0b0001, 0b0010, 0b0100, 0b1000};
  EXPECT_EQ(0b00001001, SimdShuffle::PackShuffle4(arr));
}

TEST(SimdShufflePackTest, PackBlend8) {
  uint8_t arr[8]{0, 2, 4, 6, 8, 10, 12, 14};
  EXPECT_EQ(0b11110000, SimdShuffle::PackBlend8(arr));
}

TEST(SimdShufflePackTest, PackBlend4) {
  uint8_t arr[4]{0, 2, 4, 6};
  EXPECT_EQ(0b11110000, SimdShuffle::PackBlend4(arr));
}

TEST(SimdShufflePackTest, Pack4Lanes) {
  uint8_t arr[4]{0x01, 0x08, 0xa0, 0x7c};
  EXPECT_EQ(0x7ca00801, SimdShuffle::Pack4Lanes(arr));
}

TEST(SimdShufflePackTest, Pack16Lanes) {
  uint8_t arr[16]{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
  uint32_t imms[4]{0};
  SimdShuffle::Pack16Lanes(imms, arr);
  EXPECT_THAT(imms,
              ElementsAre(0x03020100, 0x07060504, 0x0b0a0908, 0x0f0e0d0c));
}

#ifdef V8_TARGET_ARCH_X64
TEST_F(SimdShuffleTest, TryMatchVpshufd) {
  uint8_t shuffle32x8[8];
  EXPECT_TRUE(TryMatch32x8Shuffle(
      {{12, 13, 14, 15, 8,  9,  10, 11, 4,  5,  6,  7,  0,  1,  2,  3,
        28, 29, 30, 31, 24, 25, 26, 27, 20, 21, 22, 23, 16, 17, 18, 19}},
      shuffle32x8));

  EXPECT_EQ(shuffle32x8[0], 3);
  EXPECT_EQ(shuffle32x8[1], 2);
  EXPECT_EQ(shuffle32x8[2], 1);
  EXPECT_EQ(shuffle32x8[3], 0);
  EXPECT_EQ(shuffle32x8[4], 7);
  EXPECT_EQ(shuffle32x8[5], 6);
  EXPECT_EQ(shuffle32x8[6], 5);
  EXPECT_EQ(shuffle32x8[7], 4);

  uint8_t control;
  EXPECT_TRUE(TryMatchVpshufd(shuffle32x8, &control));
  EXPECT_EQ(control, 0b00'01'10'11);
}

TEST_F(SimdShuffleTest, TryMatchShufps256) {
  uint8_t shuffle32x8[8];
  EXPECT_TRUE(TryMatch32x8Shuffle(
      {{12, 13, 14, 15, 8,  9,  10, 11, 36, 37, 38, 39, 32, 33, 34, 35,
        28, 29, 30, 31, 24, 25, 26, 27, 52, 53, 54, 55, 48, 49, 50, 51}},
      shuffle32x8));
  EXPECT_EQ(shuffle32x8[0], 3);
  EXPECT_EQ(shuffle32x8[1], 2);
  EXPECT_EQ(shuffle32x8[2], 9);
  EXPECT_EQ(shuffle32x8[3], 8);
  EXPECT_EQ(shuffle32x8[4], 7);
  EXPECT_EQ(shuffle32x8[5], 6);
  EXPECT_EQ(shuffle32x8[6], 13);
  EXPECT_EQ(shuffle32x8[7], 12);

  uint8_t control;
  EXPECT_TRUE(TryMatchShufps256(shuffle32x8, &control));
  EXPECT_EQ(control, 0b00'01'10'11);
}

#endif  // V8_TARGET_ARCH_X64

}  // namespace wasm
}  // namespace internal
}  // namespace v8
