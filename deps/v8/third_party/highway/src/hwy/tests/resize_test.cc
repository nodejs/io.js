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
#define HWY_TARGET_INCLUDE "tests/resize_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

#if HWY_TARGET != HWY_SCALAR

template <class DTo, class DFrom>
HWY_INLINE void DoTruncResizeBitCastTest(DTo d_to, DFrom d_from) {
  if (Lanes(d_to) == 0) return;

  const VFromD<DFrom> v = Iota(d_from, 1);
  const VFromD<DTo> expected = Iota(d_to, 1);

  const VFromD<DTo> actual_1 = ResizeBitCast(d_to, v);
  HWY_ASSERT_VEC_EQ(d_to, expected, actual_1);

  const VFromD<DTo> actual_2 = ZeroExtendResizeBitCast(d_to, d_from, v);
  HWY_ASSERT_VEC_EQ(d_to, expected, actual_2);
}

struct TestTruncatingResizeBitCastHalf {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const Half<D> dh;
    DoTruncResizeBitCastTest(dh, d);

    const auto v_full = Iota(d, 1);
    const VFromD<decltype(dh)> expected_full_to_half = LowerHalf(dh, v_full);
    HWY_ASSERT_VEC_EQ(dh, expected_full_to_half, ResizeBitCast(dh, v_full));
    HWY_ASSERT_VEC_EQ(dh, expected_full_to_half,
                      ZeroExtendResizeBitCast(dh, d, v_full));
  }
};

struct TestTruncatingResizeBitCastQuarter {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const Half<Half<decltype(d)>> d_quarter;
    if (MaxLanes(d_quarter) == MaxLanes(d) / 4) {
      DoTruncResizeBitCastTest(d_quarter, d);
    }
  }
};

struct TestTruncatingResizeBitCastEighth {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const Half<Half<Half<decltype(d)>>> d_eighth;
    if (MaxLanes(d_eighth) == MaxLanes(d) / 8) {
      DoTruncResizeBitCastTest(d_eighth, d);
    }
  }
};

#endif  // HWY_TARGET != HWY_SCALAR

HWY_NOINLINE void TestAllTruncatingResizeBitCast() {
#if HWY_TARGET != HWY_SCALAR
  ForAllTypes(ForShrinkableVectors<TestTruncatingResizeBitCastHalf, 1>());
  ForAllTypes(ForShrinkableVectors<TestTruncatingResizeBitCastQuarter, 2>());
  ForAllTypes(ForShrinkableVectors<TestTruncatingResizeBitCastEighth, 3>());
#endif
}

class TestExtendingResizeBitCast {
#if HWY_TARGET != HWY_SCALAR

 private:
  template <class DTo, class DFrom>
  static HWY_INLINE void DoExtResizeBitCastTest(DTo d_to, DFrom d_from) {
    const size_t N = Lanes(d_from);
    const auto active_elements_mask = FirstN(d_to, N);

    const VFromD<DTo> expected =
        IfThenElseZero(active_elements_mask, Iota(d_to, 1));
    const VFromD<DFrom> v = Iota(d_from, 1);

    const VFromD<DTo> actual_1 = ResizeBitCast(d_to, v);
    const VFromD<DTo> actual_2 = ZeroExtendResizeBitCast(d_to, d_from, v);

    HWY_ASSERT_VEC_EQ(d_to, expected,
                      IfThenElseZero(active_elements_mask, actual_1));
    HWY_ASSERT_VEC_EQ(d_to, expected, actual_2);
  }

  template <class DFrom>
  static HWY_INLINE void DoExtResizeBitCastToTwiceDTest(DFrom d_from) {
    using DTo = Twice<DFrom>;
    const DTo d_to;
    DoExtResizeBitCastTest(d_to, d_from);

    const VFromD<DFrom> v = Iota(d_from, 1);
    const VFromD<DTo> expected = ZeroExtendVector(d_to, v);

    const VFromD<DTo> actual_1 = ResizeBitCast(d_to, v);
    const VFromD<DTo> actual_2 = ZeroExtendResizeBitCast(d_to, d_from, v);

    HWY_ASSERT_VEC_EQ(d_from, v, LowerHalf(d_from, actual_1));
    HWY_ASSERT_VEC_EQ(d_from, v, LowerHalf(d_from, actual_2));
    HWY_ASSERT_VEC_EQ(d_to, expected, actual_2);
  }
#endif  // HWY_TARGET != HWY_SCALAR

 public:
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
#if HWY_TARGET != HWY_SCALAR
    DoExtResizeBitCastToTwiceDTest(d);

    constexpr size_t kMaxLanes = MaxLanes(d);
#if HWY_TARGET == HWY_RVV
    constexpr int kFromVectPow2 = DFromV<VFromD<D>>().Pow2();
    static_assert(kFromVectPow2 >= -3 && kFromVectPow2 <= 3,
                  "kFromVectPow2 must be between -3 and 3");

    constexpr size_t kScaledMaxLanes =
        HWY_MAX((kMaxLanes << 3) >> (kFromVectPow2 + 3), 1);
    constexpr size_t kQuadrupleScaledLimit = kScaledMaxLanes;
    constexpr size_t kOctupleScaledLimit = kScaledMaxLanes;
    constexpr int kQuadruplePow2 = HWY_MIN(kFromVectPow2 + 2, 3);
    constexpr int kOctuplePow2 = HWY_MIN(kFromVectPow2 + 3, 3);
#else
    constexpr size_t kQuadrupleScaledLimit = kMaxLanes * 4;
    constexpr size_t kOctupleScaledLimit = kMaxLanes * 8;
    constexpr int kQuadruplePow2 = 0;
    constexpr int kOctuplePow2 = 0;
#endif

    const CappedTag<T, kQuadrupleScaledLimit, kQuadruplePow2> d_quadruple;
    const CappedTag<T, kOctupleScaledLimit, kOctuplePow2> d_octuple;

    if (MaxLanes(d_quadruple) == kMaxLanes * 4) {
      DoExtResizeBitCastTest(d_quadruple, d);
      if (MaxLanes(d_octuple) == kMaxLanes * 8) {
        DoExtResizeBitCastTest(d_octuple, d);
      }
    }
#else
    (void)d;
#endif  // HWY_TARGET != HWY_SCALAR
  }
};

HWY_NOINLINE void TestAllExtendingResizeBitCast() {
  ForAllTypes(ForExtendableVectors<TestExtendingResizeBitCast>());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyResizeTest);
HWY_EXPORT_AND_TEST_P(HwyResizeTest, TestAllTruncatingResizeBitCast);
HWY_EXPORT_AND_TEST_P(HwyResizeTest, TestAllExtendingResizeBitCast);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
