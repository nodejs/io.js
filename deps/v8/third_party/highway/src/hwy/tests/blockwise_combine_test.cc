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
#include <stdint.h>

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tests/blockwise_combine_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

// Scalar does not define CombineShiftRightBytes.
#if HWY_TARGET != HWY_SCALAR || HWY_IDE

template <int kBytes>
struct TestCombineShiftRightBytes {
  template <class T, class D>
  HWY_NOINLINE void operator()(T, D d) {
    constexpr size_t kBlockSize = 16;
    static_assert(kBytes < kBlockSize, "Shift count is per block");
    const Repartition<uint8_t, D> d8;
    const size_t N8 = Lanes(d8);
    if (N8 < 16) return;
    auto hi_bytes = AllocateAligned<uint8_t>(N8);
    auto lo_bytes = AllocateAligned<uint8_t>(N8);
    auto expected_bytes = AllocateAligned<uint8_t>(N8);
    HWY_ASSERT(hi_bytes && lo_bytes && expected_bytes);
    uint8_t combined[2 * kBlockSize];

    // Random inputs in each lane
    RandomState rng;
    for (size_t rep = 0; rep < AdjustedReps(100); ++rep) {
      for (size_t i = 0; i < N8; ++i) {
        hi_bytes[i] = static_cast<uint8_t>(Random64(&rng) & 0xFF);
        lo_bytes[i] = static_cast<uint8_t>(Random64(&rng) & 0xFF);
      }
      for (size_t i = 0; i < N8; i += kBlockSize) {
        // Arguments are not the same size.
        CopyBytes<kBlockSize>(&lo_bytes[i], combined);
        CopyBytes<kBlockSize>(&hi_bytes[i], combined + kBlockSize);
        CopyBytes<kBlockSize>(combined + kBytes, &expected_bytes[i]);
      }

      const auto hi = BitCast(d, Load(d8, hi_bytes.get()));
      const auto lo = BitCast(d, Load(d8, lo_bytes.get()));
      const auto expected = BitCast(d, Load(d8, expected_bytes.get()));
      HWY_ASSERT_VEC_EQ(d, expected, CombineShiftRightBytes<kBytes>(d, hi, lo));
    }
  }
};

template <int kLanes>
struct TestCombineShiftRightLanes {
  template <class T, class D>
  HWY_NOINLINE void operator()(T, D d) {
    const Repartition<uint8_t, D> d8;
    const size_t N8 = Lanes(d8);
    if (N8 < 16) return;

    auto hi_bytes = AllocateAligned<uint8_t>(N8);
    auto lo_bytes = AllocateAligned<uint8_t>(N8);
    auto expected_bytes = AllocateAligned<uint8_t>(N8);
    HWY_ASSERT(hi_bytes && lo_bytes && expected_bytes);
    constexpr size_t kBlockSize = 16;
    uint8_t combined[2 * kBlockSize];

    // Random inputs in each lane
    RandomState rng;
    for (size_t rep = 0; rep < AdjustedReps(100); ++rep) {
      for (size_t i = 0; i < N8; ++i) {
        hi_bytes[i] = static_cast<uint8_t>(Random64(&rng) & 0xFF);
        lo_bytes[i] = static_cast<uint8_t>(Random64(&rng) & 0xFF);
      }
      for (size_t i = 0; i < N8; i += kBlockSize) {
        // Arguments are not the same size.
        CopyBytes<kBlockSize>(&lo_bytes[i], combined);
        CopyBytes<kBlockSize>(&hi_bytes[i], combined + kBlockSize);
        CopyBytes<kBlockSize>(combined + kLanes * sizeof(T),
                              &expected_bytes[i]);
      }

      const auto hi = BitCast(d, Load(d8, hi_bytes.get()));
      const auto lo = BitCast(d, Load(d8, lo_bytes.get()));
      const auto expected = BitCast(d, Load(d8, expected_bytes.get()));
      HWY_ASSERT_VEC_EQ(d, expected, CombineShiftRightLanes<kLanes>(d, hi, lo));
    }
  }
};

#endif  // #if HWY_TARGET != HWY_SCALAR

struct TestCombineShiftRight {
  template <class T, class D>
  HWY_NOINLINE void operator()(T t, D d) {
// Scalar does not define CombineShiftRightBytes.
#if HWY_TARGET != HWY_SCALAR || HWY_IDE
    constexpr int kMaxBytes =
        HWY_MIN(16, static_cast<int>(MaxLanes(d) * sizeof(T)));
    constexpr int kMaxLanes = kMaxBytes / static_cast<int>(sizeof(T));
    TestCombineShiftRightBytes<kMaxBytes - 1>()(t, d);
    TestCombineShiftRightBytes<HWY_MAX(kMaxBytes / 2, 1)>()(t, d);
    TestCombineShiftRightBytes<1>()(t, d);

    TestCombineShiftRightLanes<kMaxLanes - 1>()(t, d);
    TestCombineShiftRightLanes<HWY_MAX(kMaxLanes / 2, -1)>()(t, d);
    TestCombineShiftRightLanes<1>()(t, d);
#else
    (void)t;
    (void)d;
#endif
  }
};

HWY_NOINLINE void TestAllCombineShiftRight() {
  // Need at least 2 lanes.
  ForAllTypes(ForShrinkableVectors<TestCombineShiftRight>());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyBlockwiseCombineTest);
HWY_EXPORT_AND_TEST_P(HwyBlockwiseCombineTest, TestAllCombineShiftRight);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
