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

#include "hwy/base.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tests/swizzle_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

struct TestGetLane {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const auto v = Iota(d, 1);
    HWY_ASSERT_EQ(T(1), GetLane(v));
  }
};

HWY_NOINLINE void TestAllGetLane() {
  ForAllTypes(ForPartialVectors<TestGetLane>());
}

struct TestExtractLane {
#if !HWY_HAVE_SCALABLE && HWY_TARGET < HWY_EMU128 && !HWY_TARGET_IS_SVE
  template <class D, HWY_IF_LANES_D(BlockDFromD<D>, 1)>
  static HWY_INLINE void DoTestExtractLaneWithConstAmt_0_7(D /*d*/, Vec<D> v) {
    HWY_ASSERT_EQ(ConvertScalarTo<TFromD<D>>(1), ExtractLane(v, 0));
  }

  template <class D, HWY_IF_LANES_D(BlockDFromD<D>, 2)>
  static HWY_INLINE void DoTestExtractLaneWithConstAmt_0_7(D /*d*/, Vec<D> v) {
    HWY_ASSERT_EQ(ConvertScalarTo<TFromD<D>>(1), ExtractLane(v, 0));
    HWY_ASSERT_EQ(ConvertScalarTo<TFromD<D>>(2), ExtractLane(v, 1));
  }

  template <class D, HWY_IF_LANES_D(BlockDFromD<D>, 4)>
  static HWY_INLINE void DoTestExtractLaneWithConstAmt_0_7(D /*d*/, Vec<D> v) {
    HWY_ASSERT_EQ(ConvertScalarTo<TFromD<D>>(1), ExtractLane(v, 0));
    HWY_ASSERT_EQ(ConvertScalarTo<TFromD<D>>(2), ExtractLane(v, 1));
    HWY_ASSERT_EQ(ConvertScalarTo<TFromD<D>>(3), ExtractLane(v, 2));
    HWY_ASSERT_EQ(ConvertScalarTo<TFromD<D>>(4), ExtractLane(v, 3));
  }

  template <class D, HWY_IF_LANES_GT_D(BlockDFromD<D>, 4)>
  static HWY_INLINE void DoTestExtractLaneWithConstAmt_0_7(D /*d*/, Vec<D> v) {
    HWY_ASSERT_EQ(ConvertScalarTo<TFromD<D>>(1), ExtractLane(v, 0));
    HWY_ASSERT_EQ(ConvertScalarTo<TFromD<D>>(2), ExtractLane(v, 1));
    HWY_ASSERT_EQ(ConvertScalarTo<TFromD<D>>(3), ExtractLane(v, 2));
    HWY_ASSERT_EQ(ConvertScalarTo<TFromD<D>>(4), ExtractLane(v, 3));
    HWY_ASSERT_EQ(ConvertScalarTo<TFromD<D>>(5), ExtractLane(v, 4));
    HWY_ASSERT_EQ(ConvertScalarTo<TFromD<D>>(6), ExtractLane(v, 5));
    HWY_ASSERT_EQ(ConvertScalarTo<TFromD<D>>(7), ExtractLane(v, 6));
    HWY_ASSERT_EQ(ConvertScalarTo<TFromD<D>>(8), ExtractLane(v, 7));
  }

  template <class D, HWY_IF_LANES_LE_D(BlockDFromD<D>, 8)>
  static HWY_INLINE void DoTestExtractLaneWithConstAmt_8_15(D /*d*/,
                                                            Vec<D> /*v*/) {}

  template <class D, HWY_IF_LANES_GT_D(BlockDFromD<D>, 8)>
  static HWY_INLINE void DoTestExtractLaneWithConstAmt_8_15(D /*d*/, Vec<D> v) {
    HWY_ASSERT_EQ(ConvertScalarTo<TFromD<D>>(9), ExtractLane(v, 8));
    HWY_ASSERT_EQ(ConvertScalarTo<TFromD<D>>(10), ExtractLane(v, 9));
    HWY_ASSERT_EQ(ConvertScalarTo<TFromD<D>>(11), ExtractLane(v, 10));
    HWY_ASSERT_EQ(ConvertScalarTo<TFromD<D>>(12), ExtractLane(v, 11));
    HWY_ASSERT_EQ(ConvertScalarTo<TFromD<D>>(13), ExtractLane(v, 12));
    HWY_ASSERT_EQ(ConvertScalarTo<TFromD<D>>(14), ExtractLane(v, 13));
    HWY_ASSERT_EQ(ConvertScalarTo<TFromD<D>>(15), ExtractLane(v, 14));
    HWY_ASSERT_EQ(ConvertScalarTo<TFromD<D>>(16), ExtractLane(v, 15));
  }
#endif  // !HWY_HAVE_SCALABLE && HWY_TARGET < HWY_EMU128 && !HWY_TARGET_IS_SVE

  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const auto v = Iota(d, 1);

#if !HWY_HAVE_SCALABLE && HWY_TARGET < HWY_EMU128 && !HWY_TARGET_IS_SVE
    DoTestExtractLaneWithConstAmt_0_7(d, v);
    DoTestExtractLaneWithConstAmt_8_15(d, v);
#endif  // !HWY_HAVE_SCALABLE && HWY_TARGET < HWY_EMU128 && !HWY_TARGET_IS_SVE

    for (size_t i = 0; i < Lanes(d); ++i) {
      const T actual = ExtractLane(v, i);
      HWY_ASSERT_EQ(ConvertScalarTo<T>(i + 1), actual);
    }
  }
};

HWY_NOINLINE void TestAllExtractLane() {
  ForAllTypes(ForPartialVectors<TestExtractLane>());
}

struct TestInsertLane {
#if !HWY_HAVE_SCALABLE
  template <class D, HWY_IF_LANES_D(BlockDFromD<D>, 1)>
  static HWY_INLINE void DoTestInsertLaneWithConstAmt_0_7(
      D d, TFromD<D>* HWY_RESTRICT lanes) {
    using T = TFromD<D>;

    lanes[0] = ConvertScalarTo<T>(1);
    Vec<D> v = InsertLane(Zero(d), 0, ConvertScalarTo<T>(1));
    HWY_ASSERT_VEC_EQ(d, lanes, v);
  }
  template <class D, HWY_IF_LANES_D(BlockDFromD<D>, 2)>
  static HWY_INLINE void DoTestInsertLaneWithConstAmt_0_7(
      D d, TFromD<D>* HWY_RESTRICT lanes) {
    using T = TFromD<D>;

    lanes[0] = ConvertScalarTo<T>(1);
    Vec<D> v = InsertLane(Zero(d), 0, ConvertScalarTo<T>(1));
    HWY_ASSERT_VEC_EQ(d, lanes, v);

    lanes[1] = ConvertScalarTo<T>(2);
    v = InsertLane(v, 1, ConvertScalarTo<T>(2));
    HWY_ASSERT_VEC_EQ(d, lanes, v);
  }
  template <class D, HWY_IF_LANES_D(BlockDFromD<D>, 4)>
  static HWY_INLINE void DoTestInsertLaneWithConstAmt_0_7(
      D d, TFromD<D>* HWY_RESTRICT lanes) {
    using T = TFromD<D>;

    lanes[0] = ConvertScalarTo<T>(1);
    Vec<D> v = InsertLane(Zero(d), 0, ConvertScalarTo<T>(1));
    HWY_ASSERT_VEC_EQ(d, lanes, v);

    lanes[1] = ConvertScalarTo<T>(2);
    v = InsertLane(v, 1, ConvertScalarTo<T>(2));
    HWY_ASSERT_VEC_EQ(d, lanes, v);

    lanes[2] = ConvertScalarTo<T>(3);
    v = InsertLane(v, 2, ConvertScalarTo<T>(3));
    HWY_ASSERT_VEC_EQ(d, lanes, v);

    lanes[3] = ConvertScalarTo<T>(4);
    v = InsertLane(v, 3, ConvertScalarTo<T>(4));
    HWY_ASSERT_VEC_EQ(d, lanes, v);
  }
  template <class D, HWY_IF_LANES_GT_D(BlockDFromD<D>, 4)>
  static HWY_INLINE void DoTestInsertLaneWithConstAmt_0_7(
      D d, TFromD<D>* HWY_RESTRICT lanes) {
    using T = TFromD<D>;

    lanes[0] = ConvertScalarTo<T>(1);
    Vec<D> v = InsertLane(Zero(d), 0, ConvertScalarTo<T>(1));
    HWY_ASSERT_VEC_EQ(d, lanes, v);

    lanes[1] = ConvertScalarTo<T>(2);
    v = InsertLane(v, 1, ConvertScalarTo<T>(2));
    HWY_ASSERT_VEC_EQ(d, lanes, v);

    lanes[2] = ConvertScalarTo<T>(3);
    v = InsertLane(v, 2, ConvertScalarTo<T>(3));
    HWY_ASSERT_VEC_EQ(d, lanes, v);

    lanes[3] = ConvertScalarTo<T>(4);
    v = InsertLane(v, 3, ConvertScalarTo<T>(4));
    HWY_ASSERT_VEC_EQ(d, lanes, v);

    lanes[4] = ConvertScalarTo<T>(5);
    v = InsertLane(v, 4, ConvertScalarTo<T>(5));
    HWY_ASSERT_VEC_EQ(d, lanes, v);

    lanes[5] = ConvertScalarTo<T>(6);
    v = InsertLane(v, 5, ConvertScalarTo<T>(6));
    HWY_ASSERT_VEC_EQ(d, lanes, v);

    lanes[6] = ConvertScalarTo<T>(7);
    v = InsertLane(v, 6, ConvertScalarTo<T>(7));
    HWY_ASSERT_VEC_EQ(d, lanes, v);

    lanes[7] = ConvertScalarTo<T>(8);
    v = InsertLane(v, 7, ConvertScalarTo<T>(8));
    HWY_ASSERT_VEC_EQ(d, lanes, v);
  }
  template <class D, HWY_IF_LANES_LE_D(BlockDFromD<D>, 8)>
  static HWY_INLINE void DoTestInsertLaneWithConstAmt_8_15(
      D, TFromD<D>* HWY_RESTRICT) {}
  template <class D, HWY_IF_LANES_GT_D(BlockDFromD<D>, 8)>
  static HWY_INLINE void DoTestInsertLaneWithConstAmt_8_15(
      D d, TFromD<D>* HWY_RESTRICT lanes) {
    using T = TFromD<D>;
    Vec<D> v = Load(d, lanes);

    lanes[8] = ConvertScalarTo<T>(9);
    v = InsertLane(v, 8, ConvertScalarTo<T>(9));
    HWY_ASSERT_VEC_EQ(d, lanes, v);

    lanes[9] = ConvertScalarTo<T>(10);
    v = InsertLane(v, 9, ConvertScalarTo<T>(10));
    HWY_ASSERT_VEC_EQ(d, lanes, v);

    lanes[10] = ConvertScalarTo<T>(11);
    v = InsertLane(v, 10, ConvertScalarTo<T>(11));
    HWY_ASSERT_VEC_EQ(d, lanes, v);

    lanes[11] = ConvertScalarTo<T>(12);
    v = InsertLane(v, 11, ConvertScalarTo<T>(12));
    HWY_ASSERT_VEC_EQ(d, lanes, v);

    lanes[12] = ConvertScalarTo<T>(13);
    v = InsertLane(v, 12, ConvertScalarTo<T>(13));
    HWY_ASSERT_VEC_EQ(d, lanes, v);

    lanes[13] = ConvertScalarTo<T>(14);
    v = InsertLane(v, 13, ConvertScalarTo<T>(14));
    HWY_ASSERT_VEC_EQ(d, lanes, v);

    lanes[14] = ConvertScalarTo<T>(15);
    v = InsertLane(v, 14, ConvertScalarTo<T>(15));
    HWY_ASSERT_VEC_EQ(d, lanes, v);

    lanes[15] = ConvertScalarTo<T>(16);
    v = InsertLane(v, 15, ConvertScalarTo<T>(16));
    HWY_ASSERT_VEC_EQ(d, lanes, v);
  }
  template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
  static HWY_INLINE void DoTestInsertLaneWithConstAmt(
      D d, TFromD<D>* HWY_RESTRICT lanes) {
    DoTestInsertLaneWithConstAmt_0_7(d, lanes);
    DoTestInsertLaneWithConstAmt_8_15(d, lanes);
    Store(Zero(d), d, lanes);
  }
  template <class D, HWY_IF_V_SIZE_GT_D(D, 16)>
  static HWY_INLINE void DoTestInsertLaneWithConstAmt(D,
                                                      TFromD<D>* HWY_RESTRICT) {
  }
#endif  // !HWY_HAVE_SCALABLE
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    using V = Vec<D>;
    const V v = IotaForSpecial(d, 1);
    const size_t N = Lanes(d);
    auto lanes = AllocateAligned<T>(N);
    HWY_ASSERT(lanes);
    Store(Zero(d), d, lanes.get());

#if !HWY_HAVE_SCALABLE
    DoTestInsertLaneWithConstAmt(d, lanes.get());
#endif

// TODO(janwas): file compiler bug report
#if HWY_COMPILER_CLANG && (HWY_COMPILER_CLANG < 1900) && HWY_ARCH_ARM
    if (IsSpecialFloat<T>()) return;
#endif

    V v2 = Zero(d);
    for (size_t i = 0; i < N; ++i) {
      lanes[i] = ConvertScalarTo<T>(i + 1);
      v2 = InsertLane(v2, i, ConvertScalarTo<T>(i + 1));
      HWY_ASSERT_VEC_EQ(d, lanes.get(), v2);
    }
    HWY_ASSERT_VEC_EQ(d, v, v2);

    for (size_t i = 0; i < N; ++i) {
      lanes[i] = ConvertScalarTo<T>(0);
      const V v3 = Load(d, lanes.get());
      const V actual = InsertLane(v3, i, ConvertScalarTo<T>(i + 1));
      HWY_ASSERT_VEC_EQ(d, v, actual);
      lanes[i] = ConvertScalarTo<T>(i + 1);  // restore lane i
    }
  }
};

HWY_NOINLINE void TestAllInsertLane() {
  ForAllTypesAndSpecial(ForPartialVectors<TestInsertLane>());
}

struct TestDupEven {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const size_t N = Lanes(d);
    auto expected = AllocateAligned<T>(N);
    HWY_ASSERT(expected);
    for (size_t i = 0; i < N; ++i) {
      expected[i] = ConvertScalarTo<T>((i & ~size_t{1}) + 1);
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), DupEven(Iota(d, 1)));
  }
};

HWY_NOINLINE void TestAllDupEven() {
  ForAllTypes(ForShrinkableVectors<TestDupEven>());
}

struct TestDupOdd {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
#if HWY_TARGET != HWY_SCALAR
    const size_t N = Lanes(d);
    auto expected = AllocateAligned<T>(N);
    HWY_ASSERT(expected);
    for (size_t i = 0; i < N; ++i) {
      expected[i] = ConvertScalarTo<T>((i & ~size_t{1}) + 2);
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), DupOdd(Iota(d, 1)));
#else
    (void)d;
#endif
  }
};

HWY_NOINLINE void TestAllDupOdd() {
  ForAllTypes(ForShrinkableVectors<TestDupOdd>());
}

struct TestOddEven {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const size_t N = Lanes(d);
    const auto even = Iota(d, 1);
    const auto odd = Iota(d, 1 + N);
    auto expected = AllocateAligned<T>(N);
    HWY_ASSERT(expected);
    for (size_t i = 0; i < N; ++i) {
      expected[i] = ConvertScalarTo<T>(1 + i + ((i & 1) ? N : 0));
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), OddEven(odd, even));
  }
};

HWY_NOINLINE void TestAllOddEven() {
  ForAllTypes(ForShrinkableVectors<TestOddEven>());
}

class TestBroadcastLane {
 private:
  template <int kLane, class D,
            HWY_IF_LANES_GT_D(D, static_cast<size_t>(kLane))>
  static HWY_INLINE void DoTestBroadcastLane(D d, const size_t N) {
    using T = TFromD<D>;
    // kLane < HWY_MAX_LANES_D(D) is true
    if (kLane >= N) return;

    const Vec<D> expected = Set(d, ConvertScalarTo<T>(kLane + 1));

    const BlockDFromD<decltype(d)> d_block;
    static_assert(d_block.MaxLanes() <= d.MaxLanes(),
                  "d_block.MaxLanes() <= d.MaxLanes() must be true");
    constexpr size_t kLanesPer16ByteBlk = 16 / sizeof(T);
    constexpr int kBlockIdx = kLane / static_cast<int>(kLanesPer16ByteBlk);
    constexpr int kLaneInBlkIdx =
        kLane & static_cast<int>(kLanesPer16ByteBlk - 1);

    const Vec<D> v = Iota(d, 1);
    const Vec<D> actual = BroadcastLane<kLane>(v);
    const Vec<decltype(d_block)> actual_block =
        ExtractBlock<kBlockIdx>(Broadcast<kLaneInBlkIdx>(v));

    HWY_ASSERT_VEC_EQ(d, expected, actual);
    HWY_ASSERT_VEC_EQ(d_block, ResizeBitCast(d_block, expected), actual_block);
  }
  template <int kLane, class D,
            HWY_IF_LANES_LE_D(D, static_cast<size_t>(kLane))>
  static HWY_INLINE void DoTestBroadcastLane(D /*d*/, const size_t /*N*/) {
    // If kLane >= HWY_MAX_LANES_D(D) is true, do nothing
  }

 public:
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const auto N = Lanes(d);

    DoTestBroadcastLane<0>(d, N);
    DoTestBroadcastLane<1>(d, N);
    DoTestBroadcastLane<2>(d, N);
    DoTestBroadcastLane<3>(d, N);
    DoTestBroadcastLane<6>(d, N);
    DoTestBroadcastLane<14>(d, N);
    DoTestBroadcastLane<29>(d, N);
    DoTestBroadcastLane<53>(d, N);
    DoTestBroadcastLane<115>(d, N);
    DoTestBroadcastLane<251>(d, N);
    DoTestBroadcastLane<257>(d, N);
  }
};

HWY_NOINLINE void TestAllBroadcastLane() {
  ForAllTypes(ForPartialFixedOrFullScalableVectors<TestBroadcastLane>());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwySwizzleTest);
HWY_EXPORT_AND_TEST_P(HwySwizzleTest, TestAllGetLane);
HWY_EXPORT_AND_TEST_P(HwySwizzleTest, TestAllExtractLane);
HWY_EXPORT_AND_TEST_P(HwySwizzleTest, TestAllInsertLane);
HWY_EXPORT_AND_TEST_P(HwySwizzleTest, TestAllDupEven);
HWY_EXPORT_AND_TEST_P(HwySwizzleTest, TestAllDupOdd);
HWY_EXPORT_AND_TEST_P(HwySwizzleTest, TestAllOddEven);
HWY_EXPORT_AND_TEST_P(HwySwizzleTest, TestAllBroadcastLane);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
