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
#define HWY_TARGET_INCLUDE "tests/combine_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

struct TestLowerHalf {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const Half<D> d2;

    const size_t N = Lanes(d);
    auto lanes = AllocateAligned<T>(N);
    auto lanes2 = AllocateAligned<T>(N);
    HWY_ASSERT(lanes && lanes2);
    ZeroBytes(lanes.get(), N * sizeof(T));
    ZeroBytes(lanes2.get(), N * sizeof(T));
    const auto v = Iota(d, 1);
    Store(LowerHalf(d2, v), d2, lanes.get());
    Store(LowerHalf(v), d2, lanes2.get());  // optionally without D
    size_t i = 0;
    for (; i < Lanes(d2); ++i) {
      HWY_ASSERT_EQ(ConvertScalarTo<T>(1 + i), lanes[i]);
      HWY_ASSERT_EQ(ConvertScalarTo<T>(1 + i), lanes2[i]);
    }
    // Other half remains unchanged
    for (; i < N; ++i) {
      HWY_ASSERT_EQ(ConvertScalarTo<T>(0), lanes[i]);
      HWY_ASSERT_EQ(ConvertScalarTo<T>(0), lanes2[i]);
    }
  }
};

struct TestLowerQuarter {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const Half<D> d2;
    const Half<decltype(d2)> d4;

    const size_t N = Lanes(d);
    auto lanes = AllocateAligned<T>(N);
    auto lanes2 = AllocateAligned<T>(N);
    HWY_ASSERT(lanes && lanes2);
    ZeroBytes(lanes.get(), N * sizeof(T));
    ZeroBytes(lanes2.get(), N * sizeof(T));
    const auto v = Iota(d, 1);
    const auto lo = LowerHalf(d4, LowerHalf(d2, v));
    const auto lo2 = LowerHalf(LowerHalf(v));  // optionally without D
    Store(lo, d4, lanes.get());
    Store(lo2, d4, lanes2.get());
    size_t i = 0;
    for (; i < Lanes(d4); ++i) {
      HWY_ASSERT_EQ(ConvertScalarTo<T>(i + 1), lanes[i]);
      HWY_ASSERT_EQ(ConvertScalarTo<T>(i + 1), lanes2[i]);
    }
    // Upper 3/4 remain unchanged
    for (; i < N; ++i) {
      HWY_ASSERT_EQ(ConvertScalarTo<T>(0), lanes[i]);
      HWY_ASSERT_EQ(ConvertScalarTo<T>(0), lanes2[i]);
    }
  }
};

HWY_NOINLINE void TestAllLowerHalf() {
  ForAllTypes(ForHalfVectors<TestLowerHalf>());

  // The minimum vector size is 128 bits, so there's no guarantee we can have
  // quarters of 64-bit lanes, hence test 'all' other types.
  ForHalfVectors<TestLowerQuarter, 2> test_quarter;
  ForUI8(test_quarter);
  ForUI16(test_quarter);  // exclude float16_t - cannot compare
  ForUIF32(test_quarter);
}

struct TestUpperHalf {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    // Scalar does not define UpperHalf.
#if HWY_TARGET != HWY_SCALAR
    const Half<D> d2;
    const size_t N2 = Lanes(d2);
    if (N2 < 2) return;
    HWY_ASSERT_EQ(N2 * 2, Lanes(d));
    auto expected = AllocateAligned<T>(N2);
    HWY_ASSERT(expected);
    size_t i = 0;
    for (; i < N2; ++i) {
      expected[i] = ConvertScalarTo<T>(N2 + 1 + i);
    }
    HWY_ASSERT_VEC_EQ(d2, expected.get(), UpperHalf(d2, Iota(d, 1)));
#else
    (void)d;
#endif
  }
};

HWY_NOINLINE void TestAllUpperHalf() {
  ForAllTypes(ForHalfVectors<TestUpperHalf>());
}

struct TestZeroExtendVector {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const Twice<D> d2;

    const Vec<D> v = IotaForSpecial(d, 1);
    const size_t N = Lanes(d);
    const size_t N2 = Lanes(d2);
    // If equal, then N was already MaxLanes(d) and it's not clear what
    // Combine or ZeroExtendVector should return.
    if (N2 == N) return;
    HWY_ASSERT(N2 == 2 * N);
    auto lanes = AllocateAligned<T>(N2);
    HWY_ASSERT(lanes);
    Store(v, d, &lanes[0]);
    Store(v, d, &lanes[N]);

    const VFromD<decltype(d2)> ext = ZeroExtendVector(d2, v);
    Store(ext, d2, lanes.get());

    // Lower half is unchanged
    HWY_ASSERT_VEC_EQ(d, v, Load(d, &lanes[0]));
    // Upper half is zero
    HWY_ASSERT_VEC_EQ(d, Zero(d), Load(d, &lanes[N]));
  }
};

HWY_NOINLINE void TestAllZeroExtendVector() {
  ForAllTypesAndSpecial(ForExtendableVectors<TestZeroExtendVector>());
}

struct TestCombine {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const Twice<D> d2;
    const size_t N2 = Lanes(d2);
    if (N2 < 2) return;
    auto lanes = AllocateAligned<T>(N2);
    HWY_ASSERT(lanes);

    const Vec<D> lo = Iota(d, 1);
    const Vec<D> hi = Iota(d, N2 / 2 + 1);
    const Vec<decltype(d2)> combined = Combine(d2, hi, lo);
    Store(combined, d2, lanes.get());

    const Vec<decltype(d2)> expected = Iota(d2, 1);
    HWY_ASSERT_VEC_EQ(d2, expected, combined);
  }
};

HWY_NOINLINE void TestAllCombine() {
  ForAllTypes(ForExtendableVectors<TestCombine>());
}

struct TestInterleaveWholeHalves {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
#if HWY_TARGET != HWY_SCALAR
    const size_t N = Lanes(d);
    using TU = MakeUnsigned<T>;

    constexpr TU kMsb = SignMask<T>();
    const TU hi_bit = (!IsFloat<T>() && !IsSpecialFloat<T>() && N < kMsb)
                          ? static_cast<TU>(N)
                          : kMsb;
    const TU lo_mask = static_cast<TU>(hi_bit - 1u);

    const RebindToUnsigned<decltype(d)> du;
    const auto v0 = And(Iota(d, 0), BitCast(d, Set(du, lo_mask)));
    const auto v1 = Or(v0, BitCast(d, Set(du, hi_bit)));

    auto v0_lanes = AllocateAligned<T>(N);
    auto v1_lanes = AllocateAligned<T>(N);
    auto expected = AllocateAligned<T>(N);
    HWY_ASSERT(v0_lanes && v1_lanes && expected);

    Store(v0, d, v0_lanes.get());
    Store(v1, d, v1_lanes.get());

    const size_t half_N = N / 2;
    for (size_t i = 0; i < half_N; i++) {
      expected[2 * i] = v0_lanes[i];
      expected[2 * i + 1] = v1_lanes[i];
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), InterleaveWholeLower(d, v0, v1));
    HWY_ASSERT_VEC_EQ(d, expected.get(), InterleaveWholeLower(v0, v1));

    for (size_t i = 0; i < half_N; i++) {
      expected[2 * i] = v1_lanes[i];
      expected[2 * i + 1] = v0_lanes[i];
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), InterleaveWholeLower(d, v1, v0));
    HWY_ASSERT_VEC_EQ(d, expected.get(), InterleaveWholeLower(v1, v0));

    for (size_t i = 0; i < half_N; i++) {
      expected[2 * i] = v0_lanes[i + half_N];
      expected[2 * i + 1] = v1_lanes[i + half_N];
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), InterleaveWholeUpper(d, v0, v1));

    for (size_t i = 0; i < half_N; i++) {
      expected[2 * i] = v1_lanes[i + half_N];
      expected[2 * i + 1] = v0_lanes[i + half_N];
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), InterleaveWholeUpper(d, v1, v0));
#else
    (void)d;
#endif  // HWY_TARGET != HWY_SCALAR
  }
};

HWY_NOINLINE void TestAllInterleaveWholeHalves() {
  ForAllTypes(ForShrinkableVectors<TestInterleaveWholeHalves>());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyCombineTest);
HWY_EXPORT_AND_TEST_P(HwyCombineTest, TestAllLowerHalf);
HWY_EXPORT_AND_TEST_P(HwyCombineTest, TestAllUpperHalf);
HWY_EXPORT_AND_TEST_P(HwyCombineTest, TestAllZeroExtendVector);
HWY_EXPORT_AND_TEST_P(HwyCombineTest, TestAllCombine);
HWY_EXPORT_AND_TEST_P(HwyCombineTest, TestAllInterleaveWholeHalves);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
