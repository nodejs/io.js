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

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tests/mask_combine_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

struct TestLowerAndUpperHalvesOfMask {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
#if HWY_TARGET != HWY_SCALAR
    using TI = MakeSigned<T>;

    const Half<decltype(d)> dh;
    const RebindToSigned<decltype(d)> di;
    const RebindToSigned<decltype(dh)> dh_i;

    const size_t N = Lanes(d);
    auto bool_lanes = AllocateAligned<TI>(N);
    auto expected = AllocateAligned<TI>(N);
    HWY_ASSERT(bool_lanes && expected);

    ZeroBytes(bool_lanes.get(), N * sizeof(TI));

    // For all combinations of zero/nonzero state of subset of lanes:
    const size_t max_lanes = AdjustedLog2Reps(HWY_MIN(N, size_t(6)));
    for (size_t code = 0; code < (1ull << max_lanes); ++code) {
      for (size_t i = 0; i < max_lanes; ++i) {
        bool_lanes[i] = (code & (1ull << i)) ? TI(1) : TI(0);
      }

      for (size_t i = 0; i < N; ++i) {
        expected[i] = static_cast<TI>(-static_cast<TI>(bool_lanes[i]));
      }

      const auto in_mask =
          RebindMask(d, Gt(Load(di, bool_lanes.get()), Zero(di)));

      const auto expected_vec = Load(di, expected.get());
      const auto expected_lo_mask =
          RebindMask(dh, MaskFromVec(LowerHalf(dh_i, expected_vec)));
      const auto expected_hi_mask =
          RebindMask(dh, MaskFromVec(UpperHalf(dh_i, expected_vec)));

      HWY_ASSERT_MASK_EQ(dh, expected_lo_mask, LowerHalfOfMask(dh, in_mask));
      HWY_ASSERT_MASK_EQ(dh, expected_hi_mask, UpperHalfOfMask(dh, in_mask));
    }

    HWY_ASSERT_MASK_EQ(dh, FirstN(dh, 1), LowerHalfOfMask(dh, FirstN(d, 1)));
    HWY_ASSERT_MASK_EQ(dh, FirstN(dh, (N / 2) - 1),
                       LowerHalfOfMask(dh, FirstN(d, (N / 2) - 1)));

    const size_t hi_N = HWY_MAX(HWY_MIN((N / 2) - 1, 5), 1);
    HWY_ASSERT_MASK_EQ(dh, Not(FirstN(dh, (N / 2) - hi_N)),
                       UpperHalfOfMask(dh, Not(FirstN(d, N - hi_N))));
#else
    (void)d;
#endif
  }
};

HWY_NOINLINE void TestAllLowerAndUpperHalvesOfMask() {
  ForAllTypes(ForShrinkableVectors<TestLowerAndUpperHalvesOfMask>());
}

struct TestCombineMasks {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
#if HWY_TARGET != HWY_SCALAR
    using TI = MakeSigned<T>;

    const Twice<decltype(d)> dt;
    const RebindToSigned<decltype(d)> di;
    const RebindToSigned<decltype(dt)> dt_i;

    const size_t N = Lanes(di);
    auto bool_lanes = AllocateAligned<TI>(N * 2);
    auto expected = AllocateAligned<TI>(N * 2);
    HWY_ASSERT(bool_lanes && expected);

    ZeroBytes(bool_lanes.get(), N * 2 * sizeof(TI));

    // For all combinations of zero/nonzero state of subset of lanes:
    const size_t max_lanes = AdjustedLog2Reps(HWY_MIN(N * 2, size_t(6)));
    for (size_t code = 0; code < (1ull << max_lanes); ++code) {
      for (size_t i = 0; i < max_lanes; ++i) {
        bool_lanes[i] = (code & (1ull << i)) ? TI(1) : TI(0);
      }

      for (size_t i = 0; i < N * 2; ++i) {
        expected[i] = static_cast<TI>(-static_cast<TI>(bool_lanes[i]));
      }

      const auto m0 = RebindMask(d, Gt(Load(di, bool_lanes.get()), Zero(di)));
      const auto m1 =
          RebindMask(d, Gt(Load(di, bool_lanes.get() + N), Zero(di)));

      const auto combined_mask = CombineMasks(dt, m1, m0);
      const auto expected_mask =
          RebindMask(dt, MaskFromVec(Load(dt_i, expected.get())));

      HWY_ASSERT_VEC_EQ(dt_i, expected.get(),
                        BitCast(dt_i, VecFromMask(dt, combined_mask)));
      HWY_ASSERT_VEC_EQ(dt_i, expected.get(),
                        Combine(dt_i, BitCast(di, VecFromMask(d, m1)),
                                BitCast(di, VecFromMask(d, m0))));
      HWY_ASSERT_MASK_EQ(dt, expected_mask, combined_mask);
    }

    const size_t max_hi_lanes = HWY_MIN(max_lanes, N);
    for (size_t code = 0; code < (1ull << max_hi_lanes); ++code) {
      for (size_t i = 0; i < max_hi_lanes; ++i) {
        bool_lanes[i] = (code & (1ull << i)) ? TI(1) : TI(0);
      }

      const size_t lo_lane_count = (code + 1) & (N - 1);

      for (size_t i = 0; i < N; ++i) {
        expected[i] = static_cast<TI>((i < lo_lane_count) ? TI(-1) : TI(0));
        expected[N + i] = static_cast<TI>(-static_cast<TI>(bool_lanes[i]));
      }

      const auto m0 = FirstN(d, lo_lane_count);
      const auto m1 = RebindMask(d, Gt(Load(di, bool_lanes.get()), Zero(di)));

      const auto combined_mask = CombineMasks(dt, m1, m0);
      const auto expected_mask =
          RebindMask(dt, MaskFromVec(Load(dt_i, expected.get())));

      HWY_ASSERT_VEC_EQ(dt_i, expected.get(),
                        BitCast(dt_i, VecFromMask(dt, combined_mask)));
      HWY_ASSERT_VEC_EQ(dt_i, expected.get(),
                        Combine(dt_i, BitCast(di, VecFromMask(d, m1)),
                                BitCast(di, VecFromMask(d, m0))));
      HWY_ASSERT_MASK_EQ(dt, expected_mask, combined_mask);
    }

    HWY_ASSERT_MASK_EQ(dt, FirstN(dt, N - 1),
                       CombineMasks(dt, FirstN(d, 0), FirstN(d, N - 1)));
    HWY_ASSERT_MASK_EQ(dt, FirstN(dt, N),
                       CombineMasks(dt, FirstN(d, 0), FirstN(d, N)));
    HWY_ASSERT_MASK_EQ(dt, FirstN(dt, 2 * N - 1),
                       CombineMasks(dt, FirstN(d, N - 1), FirstN(d, N)));
    HWY_ASSERT_MASK_EQ(dt, FirstN(dt, 2 * N),
                       CombineMasks(dt, FirstN(d, N), FirstN(d, N)));
#else
    (void)d;
#endif
  }
};

HWY_NOINLINE void TestAllCombineMasks() {
  ForAllTypes(ForExtendableVectors<TestCombineMasks>());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyMaskCombineTest);
HWY_EXPORT_AND_TEST_P(HwyMaskCombineTest, TestAllLowerAndUpperHalvesOfMask);
HWY_EXPORT_AND_TEST_P(HwyMaskCombineTest, TestAllCombineMasks);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
