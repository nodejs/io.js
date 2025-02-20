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
#define HWY_TARGET_INCLUDE "tests/saturated_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

struct TestUnsignedSaturatedAddSub {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const auto v0 = Zero(d);
    const auto vi = Iota(d, 1);
    const auto vm = Set(d, LimitsMax<T>());

    HWY_ASSERT_VEC_EQ(d, Add(v0, v0), SaturatedAdd(v0, v0));
    HWY_ASSERT_VEC_EQ(d, Add(v0, vi), SaturatedAdd(v0, vi));
    HWY_ASSERT_VEC_EQ(d, Add(v0, vm), SaturatedAdd(v0, vm));
    HWY_ASSERT_VEC_EQ(d, vm, SaturatedAdd(vi, vm));
    HWY_ASSERT_VEC_EQ(d, vm, SaturatedAdd(vm, vm));

    HWY_ASSERT_VEC_EQ(d, v0, SaturatedSub(v0, v0));
    HWY_ASSERT_VEC_EQ(d, v0, SaturatedSub(v0, vi));
    HWY_ASSERT_VEC_EQ(d, v0, SaturatedSub(vi, vi));
    HWY_ASSERT_VEC_EQ(d, v0, SaturatedSub(vi, vm));
    HWY_ASSERT_VEC_EQ(d, Sub(vm, vi), SaturatedSub(vm, vi));
  }
};

struct TestSignedSaturatedAddSub {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const Vec<D> v0 = Zero(d);
    const Vec<D> vpm = Set(d, LimitsMax<T>());
    const Vec<D> vi = PositiveIota(d);
    const Vec<D> vn = Sub(v0, vi);
    const Vec<D> vnm = Set(d, LimitsMin<T>());
    HWY_ASSERT_MASK_EQ(d, MaskTrue(d), Gt(vi, v0));
    HWY_ASSERT_MASK_EQ(d, MaskTrue(d), Lt(vn, v0));

    HWY_ASSERT_VEC_EQ(d, v0, SaturatedAdd(v0, v0));
    HWY_ASSERT_VEC_EQ(d, vi, SaturatedAdd(v0, vi));
    HWY_ASSERT_VEC_EQ(d, vpm, SaturatedAdd(v0, vpm));
    HWY_ASSERT_VEC_EQ(d, vpm, SaturatedAdd(vi, vpm));
    HWY_ASSERT_VEC_EQ(d, vpm, SaturatedAdd(vpm, vpm));

    HWY_ASSERT_VEC_EQ(d, v0, SaturatedSub(v0, v0));
    HWY_ASSERT_VEC_EQ(d, Sub(v0, vi), SaturatedSub(v0, vi));
    HWY_ASSERT_VEC_EQ(d, vn, SaturatedSub(vn, v0));
    HWY_ASSERT_VEC_EQ(d, vnm, SaturatedSub(vnm, vi));
    HWY_ASSERT_VEC_EQ(d, vnm, SaturatedSub(vnm, vpm));
  }
};

struct TestSaturatedAddSubOverflow {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const auto v1 = Iota(d, 1);
    const auto vMax = Iota(d, LimitsMax<T>());
    const auto vMin = Iota(d, LimitsMin<T>());

    // Check that no UB triggered.
    // "assert" here is formal - to avoid compiler dropping calculations
    HWY_ASSERT_VEC_EQ(d, SaturatedAdd(v1, vMax), SaturatedAdd(vMax, v1));
    HWY_ASSERT_VEC_EQ(d, SaturatedAdd(vMax, vMax), SaturatedAdd(vMax, vMax));
    HWY_ASSERT_VEC_EQ(d, SaturatedAdd(vMin, vMax), SaturatedAdd(vMin, vMax));
    HWY_ASSERT_VEC_EQ(d, SaturatedAdd(vMin, vMin), SaturatedAdd(vMin, vMin));
    HWY_ASSERT_VEC_EQ(d, SaturatedSub(vMin, v1), SaturatedSub(vMin, v1));
    HWY_ASSERT_VEC_EQ(d, SaturatedSub(vMin, vMax), SaturatedSub(vMin, vMax));
    HWY_ASSERT_VEC_EQ(d, SaturatedSub(vMax, vMin), SaturatedSub(vMax, vMin));
    HWY_ASSERT_VEC_EQ(d, SaturatedSub(vMin, vMin), SaturatedSub(vMin, vMin));
  }
};

HWY_NOINLINE void TestAllSaturatedAddSub() {
  ForUnsignedTypes(ForPartialVectors<TestUnsignedSaturatedAddSub>());
  ForSignedTypes(ForPartialVectors<TestSignedSaturatedAddSub>());
  ForIntegerTypes(ForPartialVectors<TestSaturatedAddSubOverflow>());
}

struct TestSaturatedAbs {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const Vec<D> v0 = Zero(d);
    const Vec<D> vp1 = Set(d, static_cast<T>(1));
    const Vec<D> vn1 = Set(d, static_cast<T>(-1));
    const Vec<D> vpm = Set(d, LimitsMax<T>());
    const Vec<D> vnm = Set(d, LimitsMin<T>());

    HWY_ASSERT_VEC_EQ(d, v0, SaturatedAbs(v0));
    HWY_ASSERT_VEC_EQ(d, vp1, SaturatedAbs(vp1));
    HWY_ASSERT_VEC_EQ(d, vp1, SaturatedAbs(vn1));
    HWY_ASSERT_VEC_EQ(d, vpm, SaturatedAbs(vpm));
    HWY_ASSERT_VEC_EQ(d, vpm, SaturatedAbs(vnm));
  }
};

HWY_NOINLINE void TestAllSaturatedAbs() {
  ForSignedTypes(ForPartialVectors<TestSaturatedAbs>());
}

struct TestSaturatedNeg {
  template <class D>
  static HWY_NOINLINE void VerifySatNegOverflow(D d) {
    using T = TFromD<D>;
    HWY_ASSERT_VEC_EQ(d, Set(d, LimitsMax<T>()),
                      SaturatedNeg(Set(d, LimitsMin<T>())));
  }

  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    VerifySatNegOverflow(d);

    const RebindToUnsigned<D> du;
    using TU = TFromD<decltype(du)>;
    const Vec<D> v0 = Zero(d);
    const Vec<D> v1 = BitCast(d, Set(du, TU{1}));
    const Vec<D> vp = BitCast(d, Set(du, TU{3}));
    const Vec<D> vn = Add(Not(vp), v1);  // 2's complement
    HWY_ASSERT_VEC_EQ(d, v0, SaturatedNeg(v0));
    HWY_ASSERT_VEC_EQ(d, vp, SaturatedNeg(vn));
    HWY_ASSERT_VEC_EQ(d, vn, SaturatedNeg(vp));
  }
};

HWY_NOINLINE void TestAllSaturatedNeg() {
  ForSignedTypes(ForPartialVectors<TestSaturatedNeg>());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwySaturatedTest);
HWY_EXPORT_AND_TEST_P(HwySaturatedTest, TestAllSaturatedAddSub);
HWY_EXPORT_AND_TEST_P(HwySaturatedTest, TestAllSaturatedAbs);
HWY_EXPORT_AND_TEST_P(HwySaturatedTest, TestAllSaturatedNeg);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
