// Copyright 2019 Google LLC
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stddef.h>

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tests/swizzle_block_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

struct TestOddEvenBlocks {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const size_t N = Lanes(d);
    const auto even = Iota(d, 1);
    const auto odd = Iota(d, 1 + N);
    auto expected = AllocateAligned<T>(N);
    HWY_ASSERT(expected);
    for (size_t i = 0; i < N; ++i) {
      const size_t idx_block = i / (16 / sizeof(T));
      expected[i] = ConvertScalarTo<T>(1 + i + ((idx_block & 1) ? N : 0));
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), OddEvenBlocks(odd, even));
  }
};

HWY_NOINLINE void TestAllOddEvenBlocks() {
  ForAllTypes(ForGEVectors<128, TestOddEvenBlocks>());
}

struct TestSwapAdjacentBlocks {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const size_t N = Lanes(d);
    constexpr size_t kLanesPerBlock = 16 / sizeof(T);
    if (N < 2 * kLanesPerBlock) return;
    const auto vi = Iota(d, 1);
    auto expected = AllocateAligned<T>(N);
    HWY_ASSERT(expected);
    for (size_t i = 0; i < N; ++i) {
      const size_t idx_block = i / kLanesPerBlock;
      const size_t base = (idx_block ^ 1) * kLanesPerBlock;
      const size_t mod = i % kLanesPerBlock;
      expected[i] = ConvertScalarTo<T>(1 + base + mod);
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), SwapAdjacentBlocks(vi));
  }
};

HWY_NOINLINE void TestAllSwapAdjacentBlocks() {
  ForAllTypes(ForGEVectors<128, TestSwapAdjacentBlocks>());
}

class TestInsertBlock {
 private:
  template <int kBlock, class D,
            HWY_IF_V_SIZE_GT_D(D, static_cast<size_t>(kBlock) * 16)>
  static HWY_INLINE void DoTestInsertBlock(D d, const size_t N,
                                           TFromD<D>* HWY_RESTRICT expected) {
    // kBlock * 16 < D.MaxBytes() is true
    using T = TFromD<D>;
    using TI = MakeSigned<T>;
    using TU = MakeUnsigned<T>;

    const RebindToUnsigned<decltype(d)> du;
    const BlockDFromD<decltype(d)> d_block;
    const RebindToUnsigned<decltype(d_block)> du_block;
    using V = Vec<D>;
    using VB = Vec<decltype(d_block)>;
    constexpr TU kPositiveMask = static_cast<TU>(LimitsMax<TI>());
    constexpr TU kSignBit = static_cast<TU>(~kPositiveMask);

    for (size_t i = 0; i < N; i++) {
      const T val = ConvertScalarTo<T>(i);
      TU val_bits;
      CopySameSize(&val, &val_bits);
      val_bits &= kPositiveMask;
      CopySameSize(&val_bits, &expected[i]);
    }

    constexpr size_t kLanesPer16ByteBlk = 16 / sizeof(T);
    constexpr size_t kBlkLaneOffset =
        static_cast<size_t>(kBlock) * kLanesPer16ByteBlk;
    if (kBlkLaneOffset < N) {
      const size_t num_of_lanes_in_blk =
          HWY_MIN(N - kBlkLaneOffset, kLanesPer16ByteBlk);
      for (size_t i = 0; i < num_of_lanes_in_blk; i++) {
        const T val =
            ConvertScalarTo<T>(static_cast<TU>(i) + static_cast<TU>(kBlock));
        TU val_bits;
        CopySameSize(&val, &val_bits);
        val_bits |= kSignBit;
        CopySameSize(&val_bits, &expected[kBlkLaneOffset + i]);
      }
    }

    const V v = And(Iota(d, 0), BitCast(d, Set(du, kPositiveMask)));
    const VB blk_to_insert =
        Or(Iota(d_block, kBlock), BitCast(d_block, Set(du_block, kSignBit)));
    const V actual = InsertBlock<kBlock>(v, blk_to_insert);
    HWY_ASSERT_VEC_EQ(d, expected, actual);
  }
  template <int kBlock, class D,
            HWY_IF_V_SIZE_LE_D(D, static_cast<size_t>(kBlock) * 16)>
  static HWY_INLINE void DoTestInsertBlock(
      D /*d*/, const size_t /*N*/, TFromD<D>* HWY_RESTRICT /*expected*/) {
    // If kBlock * 16 >= D.MaxBytes() is true, do nothing
  }

 public:
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const size_t N = Lanes(d);
    auto expected = AllocateAligned<T>(N);
    HWY_ASSERT(expected);

    DoTestInsertBlock<0>(d, N, expected.get());
    DoTestInsertBlock<1>(d, N, expected.get());
    DoTestInsertBlock<2>(d, N, expected.get());
    DoTestInsertBlock<3>(d, N, expected.get());
  }
};

HWY_NOINLINE void TestAllInsertBlock() {
  ForAllTypes(ForPartialFixedOrFullScalableVectors<TestInsertBlock>());
}

class TestExtractBlock {
 private:
  template <int kBlock, class D,
            HWY_IF_V_SIZE_GT_D(D, static_cast<size_t>(kBlock) * 16)>
  static HWY_INLINE void DoTestExtractBlock(D d, const size_t N,
                                            TFromD<D>* HWY_RESTRICT expected) {
    // kBlock * 16 < D.MaxBytes() is true
    using T = TFromD<D>;

    constexpr size_t kLanesPer16ByteBlk = 16 / sizeof(T);
    constexpr size_t kBlkLaneOffset =
        static_cast<size_t>(kBlock) * kLanesPer16ByteBlk;
    if (kBlkLaneOffset >= N) return;

    const BlockDFromD<decltype(d)> d_block;
    static_assert(d_block.MaxLanes() <= kLanesPer16ByteBlk,
                  "d_block.MaxLanes() <= kLanesPer16ByteBlk must be true");

    for (size_t i = 0; i < kLanesPer16ByteBlk; i++) {
      expected[i] = ConvertScalarTo<T>(kBlkLaneOffset + i);
    }

    const auto v = Iota(d, 0);
    const Vec<BlockDFromD<decltype(d_block)>> actual = ExtractBlock<kBlock>(v);
    HWY_ASSERT_VEC_EQ(d_block, expected, actual);
  }
  template <int kBlock, class D,
            HWY_IF_V_SIZE_LE_D(D, static_cast<size_t>(kBlock) * 16)>
  static HWY_INLINE void DoTestExtractBlock(
      D /*d*/, const size_t /*N*/, TFromD<D>* HWY_RESTRICT /*expected*/) {
    // If kBlock * 16 >= D.MaxBytes() is true, do nothing
  }

 public:
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    constexpr size_t kLanesPer16ByteBlk = 16 / sizeof(T);
    const size_t N = Lanes(d);
    auto expected = AllocateAligned<T>(kLanesPer16ByteBlk);
    HWY_ASSERT(expected);

    DoTestExtractBlock<0>(d, N, expected.get());
    DoTestExtractBlock<1>(d, N, expected.get());
    DoTestExtractBlock<2>(d, N, expected.get());
    DoTestExtractBlock<3>(d, N, expected.get());
  }
};

HWY_NOINLINE void TestAllExtractBlock() {
  ForAllTypes(ForPartialFixedOrFullScalableVectors<TestExtractBlock>());
}

class TestBroadcastBlock {
 private:
  template <int kBlock, class D,
            HWY_IF_V_SIZE_GT_D(D, static_cast<size_t>(kBlock) * 16)>
  static HWY_INLINE void DoTestBroadcastBlock(
      D d, const size_t N, TFromD<D>* HWY_RESTRICT expected) {
    // kBlock * 16 < D.MaxBytes() is true
    using T = TFromD<D>;

    constexpr size_t kLanesPer16ByteBlk = 16 / sizeof(T);
    constexpr size_t kBlkLaneOffset =
        static_cast<size_t>(kBlock) * kLanesPer16ByteBlk;
    if (kBlkLaneOffset >= N) return;

    for (size_t i = 0; i < N; i++) {
      const size_t idx_in_blk = i & (kLanesPer16ByteBlk - 1);
      expected[i] =
          ConvertScalarTo<T>(kBlkLaneOffset + kLanesPer16ByteBlk + idx_in_blk);
    }

    const auto v = Iota(d, kLanesPer16ByteBlk);
    const auto actual = BroadcastBlock<kBlock>(v);
    HWY_ASSERT_VEC_EQ(d, expected, actual);
  }
  template <int kBlock, class D,
            HWY_IF_V_SIZE_LE_D(D, static_cast<size_t>(kBlock) * 16)>
  static HWY_INLINE void DoTestBroadcastBlock(
      D /*d*/, const size_t /*N*/, TFromD<D>* HWY_RESTRICT /*expected*/) {
    // If kBlock * 16 >= D.MaxBytes() is true, do nothing
  }

 public:
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const size_t N = Lanes(d);
    auto expected = AllocateAligned<T>(N);
    HWY_ASSERT(expected);

    DoTestBroadcastBlock<0>(d, N, expected.get());
    DoTestBroadcastBlock<1>(d, N, expected.get());
    DoTestBroadcastBlock<2>(d, N, expected.get());
    DoTestBroadcastBlock<3>(d, N, expected.get());
  }
};

HWY_NOINLINE void TestAllBroadcastBlock() {
  ForAllTypes(ForPartialFixedOrFullScalableVectors<TestBroadcastBlock>());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwySwizzleBlockTest);
HWY_EXPORT_AND_TEST_P(HwySwizzleBlockTest, TestAllOddEvenBlocks);
HWY_EXPORT_AND_TEST_P(HwySwizzleBlockTest, TestAllSwapAdjacentBlocks);
HWY_EXPORT_AND_TEST_P(HwySwizzleBlockTest, TestAllInsertBlock);
HWY_EXPORT_AND_TEST_P(HwySwizzleBlockTest, TestAllExtractBlock);
HWY_EXPORT_AND_TEST_P(HwySwizzleBlockTest, TestAllBroadcastBlock);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
