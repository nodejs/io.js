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
#define HWY_TARGET_INCLUDE "tests/mask_slide_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

struct TestSlideMaskDownLanes {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
#if HWY_TARGET != HWY_SCALAR
    using TI = MakeSigned<T>;

    const RebindToSigned<decltype(d)> di;

    const size_t N = Lanes(d);
    if (N < 2) {
      return;
    }

    auto bool_lanes = AllocateAligned<TI>(N);
    auto expected = AllocateAligned<TI>(N);
    HWY_ASSERT(bool_lanes && expected);

    // For all combinations of zero/nonzero state of subset of lanes:
    const size_t max_lanes = AdjustedLog2Reps(HWY_MIN(N, size_t(6)));

    ZeroBytes(bool_lanes.get(), max_lanes * sizeof(TI));
    for (size_t i = max_lanes; i < N; i++) {
      bool_lanes[i] = TI(-1);
    }

    for (size_t code = 0; code < (1ull << max_lanes); ++code) {
      for (size_t i = 0; i < max_lanes; ++i) {
        bool_lanes[i] = (code & (1ull << i)) ? TI(-1) : TI(0);
      }

      for (size_t i = 0; i < max_lanes; i++) {
        ZeroBytes(expected.get() + N - i, i * sizeof(TI));
        for (size_t j = 0; j < N - i; j++) {
          expected[j] = bool_lanes[j + i];
        }

        const auto src_mask =
            MaskFromVec(BitCast(d, Load(di, bool_lanes.get())));
        const auto expected_mask =
            MaskFromVec(BitCast(d, Load(di, expected.get())));
        const auto actual_mask = SlideMaskDownLanes(d, src_mask, i);
        HWY_ASSERT_MASK_EQ(d, expected_mask, actual_mask);

        if (i == 1) {
          HWY_ASSERT_MASK_EQ(d, expected_mask, SlideMask1Down(d, src_mask));
        }
      }
    }
#else
    (void)d;
#endif
  }
};

HWY_NOINLINE void TestAllSlideMaskDownLanes() {
  ForAllTypes(ForPartialVectors<TestSlideMaskDownLanes>());
}

struct TestSlideMaskUpLanes {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
#if HWY_TARGET != HWY_SCALAR
    using TI = MakeSigned<T>;

    const RebindToSigned<decltype(d)> di;

    const size_t N = Lanes(d);
    if (N < 2) {
      return;
    }

    auto bool_lanes = AllocateAligned<TI>(N);
    auto expected = AllocateAligned<TI>(N);
    HWY_ASSERT(bool_lanes && expected);

    // For all combinations of zero/nonzero state of subset of lanes:
    const size_t max_lanes = AdjustedLog2Reps(HWY_MIN(N, size_t(6)));

    ZeroBytes(bool_lanes.get(), max_lanes * sizeof(TI));
    for (size_t i = max_lanes; i < N; i++) {
      bool_lanes[i] = TI(-1);
    }

    for (size_t code = 0; code < (1ull << max_lanes); ++code) {
      for (size_t i = 0; i < max_lanes; ++i) {
        bool_lanes[i] = (code & (1ull << i)) ? TI(-1) : TI(0);
      }

      for (size_t i = 0; i < max_lanes; i++) {
        ZeroBytes(expected.get(), i * sizeof(TI));
        for (size_t j = 0; j < N - i; j++) {
          expected[j + i] = bool_lanes[j];
        }

        const auto src_mask =
            MaskFromVec(BitCast(d, Load(di, bool_lanes.get())));
        const auto expected_mask =
            MaskFromVec(BitCast(d, Load(di, expected.get())));
        const auto actual_mask = SlideMaskUpLanes(d, src_mask, i);
        HWY_ASSERT_MASK_EQ(d, expected_mask, actual_mask);

        if (i == 1) {
          HWY_ASSERT_MASK_EQ(d, expected_mask, SlideMask1Up(d, src_mask));
        }
      }
    }
#else
    (void)d;
#endif
  }
};

HWY_NOINLINE void TestAllSlideMaskUpLanes() {
  ForAllTypes(ForPartialVectors<TestSlideMaskUpLanes>());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyMaskSlideTest);
HWY_EXPORT_AND_TEST_P(HwyMaskSlideTest, TestAllSlideMaskDownLanes);
HWY_EXPORT_AND_TEST_P(HwyMaskSlideTest, TestAllSlideMaskUpLanes);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
