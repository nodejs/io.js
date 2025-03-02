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

#include <stdint.h>

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tests/truncate_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

template <typename From, typename To, class D>
constexpr bool IsSupportedTruncation() {
  return (sizeof(To) < sizeof(From) && Rebind<To, D>().Pow2() >= -3 &&
          Rebind<To, D>().Pow2() + 4 >= static_cast<int>(CeilLog2(sizeof(To))));
}

struct TestTruncateTo {
  template <typename From, typename To, class D,
            hwy::EnableIf<!IsSupportedTruncation<From, To, D>()>* = nullptr>
  HWY_NOINLINE void testTo(From, To, const D) {
    // do nothing
  }

  template <typename From, typename To, class D,
            hwy::EnableIf<IsSupportedTruncation<From, To, D>()>* = nullptr>
  HWY_NOINLINE void testTo(From, To, const D d) {
    constexpr uint32_t base = 0xFA578D00;
    const Rebind<To, D> dTo;
    const Vec<D> src = Iota(d, base & hwy::LimitsMax<From>());
    const Vec<decltype(dTo)> expected = Iota(dTo, base & hwy::LimitsMax<To>());
    const VFromD<decltype(dTo)> actual = TruncateTo(dTo, src);
    HWY_ASSERT_VEC_EQ(dTo, expected, actual);
  }

  template <typename T, class D>
  HWY_NOINLINE void operator()(T from, const D d) {
    testTo<T, uint8_t, D>(from, uint8_t(), d);
    testTo<T, uint16_t, D>(from, uint16_t(), d);
    testTo<T, uint32_t, D>(from, uint32_t(), d);
  }
};

HWY_NOINLINE void TestAllTruncate() {
  ForU163264(ForDemoteVectors<TestTruncateTo>());
}

struct TestOrderedTruncate2To {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*t*/, D d) {
#if HWY_TARGET != HWY_SCALAR
    const Repartition<MakeNarrow<T>, decltype(d)> dn;
    using TN = TFromD<decltype(dn)>;

    const size_t N = Lanes(d);
    const size_t twiceN = N * 2;
    auto from = AllocateAligned<T>(twiceN);
    auto expected = AllocateAligned<TN>(twiceN);
    HWY_ASSERT(from && expected);

    const T max = LimitsMax<TN>();

    constexpr uint32_t iota_base = 0xFA578D00;
    const auto src_iota_a = Iota(d, iota_base);
    const auto src_iota_b = Iota(d, iota_base + N);
    const auto expected_iota_trunc_result = Iota(dn, iota_base);
    const auto actual_iota_trunc_result =
        OrderedTruncate2To(dn, src_iota_a, src_iota_b);
    HWY_ASSERT_VEC_EQ(dn, expected_iota_trunc_result, actual_iota_trunc_result);

    RandomState rng;
    for (size_t rep = 0; rep < AdjustedReps(1000); ++rep) {
      for (size_t i = 0; i < twiceN; ++i) {
        const uint64_t bits = rng();
        CopyBytes<sizeof(T)>(&bits, &from[i]);  // not same size
        expected[i] = static_cast<TN>(from[i] & max);
      }

      const auto in_1 = Load(d, from.get());
      const auto in_2 = Load(d, from.get() + N);
      const auto actual = OrderedTruncate2To(dn, in_1, in_2);
      HWY_ASSERT_VEC_EQ(dn, expected.get(), actual);
    }
#else
    (void)d;
#endif
  }
};

HWY_NOINLINE void TestAllOrderedTruncate2To() {
  ForU163264(ForShrinkableVectors<TestOrderedTruncate2To>());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyTruncateTest);
HWY_EXPORT_AND_TEST_P(HwyTruncateTest, TestAllTruncate);
HWY_EXPORT_AND_TEST_P(HwyTruncateTest, TestAllOrderedTruncate2To);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
