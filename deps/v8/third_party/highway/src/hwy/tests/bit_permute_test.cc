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
#define HWY_TARGET_INCLUDE "tests/bit_permute_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

struct TestBitShuffle {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
#if HWY_TARGET == HWY_SCALAR
    (void)d;
#else   // HWY_TARGET != HWY_SCALAR
    using TU = MakeUnsigned<T>;

    const size_t N = Lanes(d);

    auto in1_lanes = AllocateAligned<T>(N);
    auto in2_lanes = AllocateAligned<uint8_t>(N * sizeof(T));
    auto expected = AllocateAligned<T>(N);
    HWY_ASSERT(in1_lanes && in2_lanes && expected);

    constexpr uint8_t kBitIdxMask = static_cast<uint8_t>((sizeof(T) * 8) - 1);

    const Repartition<uint8_t, decltype(d)> du8;
    const RebindToSigned<decltype(du8)> di8;

    RandomState rng;
    for (size_t rep = 0; rep < AdjustedReps(1000); ++rep) {
      for (size_t i = 0; i < N; i++) {
        TU src_val = static_cast<TU>(rng());
        TU expected_result = static_cast<TU>(0);
        for (size_t j = 0; j < sizeof(T); j++) {
          const uint8_t bit_idx = static_cast<uint8_t>(rng() & kBitIdxMask);

          in2_lanes[i * sizeof(T) + j] = bit_idx;
          expected_result = static_cast<TU>(expected_result |
                                            (((src_val >> bit_idx) & 1) << j));
        }

        in1_lanes[i] = static_cast<T>(src_val);
        expected[i] = static_cast<T>(expected_result);
      }

      const auto in1 = Load(d, in1_lanes.get());
      const auto in2 = Load(du8, in2_lanes.get());
      HWY_ASSERT_VEC_EQ(d, expected.get(), BitShuffle(in1, in2));
      HWY_ASSERT_VEC_EQ(d, expected.get(), BitShuffle(in1, BitCast(di8, in2)));
    }
#endif  // HWY_TARGET == HWY_SCALAR
  }
};

HWY_NOINLINE void TestAllBitShuffle() {
#if HWY_HAVE_INTEGER64
  ForPartialFixedOrFullScalableVectors<TestBitShuffle>()(int64_t());
  ForPartialFixedOrFullScalableVectors<TestBitShuffle>()(uint64_t());
#endif
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyBitPermuteTest);
HWY_EXPORT_AND_TEST_P(HwyBitPermuteTest, TestAllBitShuffle);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
