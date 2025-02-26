// Copyright 2023 Google LLC
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

#include "hwy/base.h"
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tests/masked_minmax_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/nanobenchmark.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

struct TestUnsignedMinMax {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    RandomState rng;
    const Vec<D> v2 = Iota(d, hwy::Unpredictable1() + 1);
    const Vec<D> v3 = Iota(d, hwy::Unpredictable1() + 2);
    const Vec<D> v4 = Iota(d, hwy::Unpredictable1() + 3);
    const Vec<D> k0 = Zero(d);
    const Vec<D> vm = Set(d, LimitsMax<T>());

    using TI = MakeSigned<T>;  // For mask > 0 comparison
    const Rebind<TI, D> di;
    using VI = Vec<decltype(di)>;
    const size_t N = Lanes(d);
    auto bool_lanes = AllocateAligned<TI>(N);
    auto expected_min = AllocateAligned<T>(N);
    auto expected_max = AllocateAligned<T>(N);
    HWY_ASSERT(bool_lanes && expected_min && expected_max);

    // Ensure unsigned 0 < max.
    HWY_ASSERT_VEC_EQ(d, k0, MaskedMinOr(v2, MaskTrue(d), k0, vm));
    HWY_ASSERT_VEC_EQ(d, k0, MaskedMinOr(v2, MaskTrue(d), vm, k0));
    HWY_ASSERT_VEC_EQ(d, vm, MaskedMaxOr(v2, MaskTrue(d), k0, vm));
    HWY_ASSERT_VEC_EQ(d, vm, MaskedMaxOr(v2, MaskTrue(d), vm, k0));

    // Each lane should have a chance of having mask=true.
    for (size_t rep = 0; rep < AdjustedReps(200); ++rep) {
      for (size_t i = 0; i < N; ++i) {
        bool_lanes[i] = (Random32(&rng) & 1024) ? TI(1) : TI(0);
        const T t2 = static_cast<T>(AddWithWraparound(static_cast<T>(i), 2));
        const T t3 = static_cast<T>(AddWithWraparound(static_cast<T>(i), 3));
        const T t4 = static_cast<T>(AddWithWraparound(static_cast<T>(i), 4));
        if (bool_lanes[i]) {
          expected_min[i] = HWY_MIN(t3, t4);
          expected_max[i] = HWY_MAX(t3, t4);
        } else {
          expected_min[i] = expected_max[i] = t2;
        }
      }

      const VI mask_i = Load(di, bool_lanes.get());
      const Mask<D> mask = RebindMask(d, Gt(mask_i, Zero(di)));

      HWY_ASSERT_VEC_EQ(d, expected_min.get(), MaskedMinOr(v2, mask, v3, v4));
      HWY_ASSERT_VEC_EQ(d, expected_min.get(), MaskedMinOr(v2, mask, v4, v3));
      HWY_ASSERT_VEC_EQ(d, expected_max.get(), MaskedMaxOr(v2, mask, v3, v4));
      HWY_ASSERT_VEC_EQ(d, expected_max.get(), MaskedMaxOr(v2, mask, v4, v3));
    }
  }
};

HWY_NOINLINE void TestAllUnsignedMinMax() {
  ForUnsignedTypes(ForPartialVectors<TestUnsignedMinMax>());
}

struct TestSignedMinMax {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    RandomState rng;
    const Vec<D> v2 = Iota(d, hwy::Unpredictable1() + 1);
    const Vec<D> v3 = Iota(d, hwy::Unpredictable1() + 2);
    const Vec<D> v4 = Iota(d, hwy::Unpredictable1() + 3);
    const Vec<D> k0 = Zero(d);
    const Vec<D> vm = Set(d, LowestValue<T>());

    using TI = MakeSigned<T>;  // For mask > 0 comparison
    const Rebind<TI, D> di;
    using VI = Vec<decltype(di)>;
    const size_t N = Lanes(d);
    auto bool_lanes = AllocateAligned<TI>(N);
    auto expected_min = AllocateAligned<T>(N);
    auto expected_max = AllocateAligned<T>(N);
    HWY_ASSERT(bool_lanes && expected_min && expected_max);

    // Ensure signed min < 0.
    HWY_ASSERT_VEC_EQ(d, vm, MaskedMinOr(v2, MaskTrue(d), k0, vm));
    HWY_ASSERT_VEC_EQ(d, vm, MaskedMinOr(v2, MaskTrue(d), vm, k0));
    HWY_ASSERT_VEC_EQ(d, k0, MaskedMaxOr(v2, MaskTrue(d), k0, vm));
    HWY_ASSERT_VEC_EQ(d, k0, MaskedMaxOr(v2, MaskTrue(d), vm, k0));

    // Each lane should have a chance of having mask=true.
    for (size_t rep = 0; rep < AdjustedReps(200); ++rep) {
      for (size_t i = 0; i < N; ++i) {
        bool_lanes[i] = (Random32(&rng) & 1024) ? TI(1) : TI(0);
        const T t2 = AddWithWraparound(ConvertScalarTo<T>(i), 2);
        const T t3 = AddWithWraparound(ConvertScalarTo<T>(i), 3);
        const T t4 = AddWithWraparound(ConvertScalarTo<T>(i), 4);
        if (bool_lanes[i]) {
          expected_min[i] = HWY_MIN(t3, t4);
          expected_max[i] = HWY_MAX(t3, t4);
        } else {
          expected_min[i] = expected_max[i] = t2;
        }
      }

      const VI mask_i = Load(di, bool_lanes.get());
      const Mask<D> mask = RebindMask(d, Gt(mask_i, Zero(di)));

      HWY_ASSERT_VEC_EQ(d, expected_min.get(), MaskedMinOr(v2, mask, v3, v4));
      HWY_ASSERT_VEC_EQ(d, expected_min.get(), MaskedMinOr(v2, mask, v4, v3));
      HWY_ASSERT_VEC_EQ(d, expected_max.get(), MaskedMaxOr(v2, mask, v3, v4));
      HWY_ASSERT_VEC_EQ(d, expected_max.get(), MaskedMaxOr(v2, mask, v4, v3));
    }
  }
};

HWY_NOINLINE void TestAllSignedMinMax() {
  ForSignedTypes(ForPartialVectors<TestSignedMinMax>());
  ForFloatTypes(ForPartialVectors<TestSignedMinMax>());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyMaskedMinMaxTest);
HWY_EXPORT_AND_TEST_P(HwyMaskedMinMaxTest, TestAllUnsignedMinMax);
HWY_EXPORT_AND_TEST_P(HwyMaskedMinMaxTest, TestAllSignedMinMax);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
