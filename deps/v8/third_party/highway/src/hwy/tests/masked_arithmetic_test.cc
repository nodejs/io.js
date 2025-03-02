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
#define HWY_TARGET_INCLUDE "tests/masked_arithmetic_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/nanobenchmark.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

struct TestAddSubMul {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    RandomState rng;
    const Vec<D> v2 = Iota(d, hwy::Unpredictable1() + 1);
    const Vec<D> v3 = Iota(d, hwy::Unpredictable1() + 2);
    const Vec<D> v4 = Iota(d, hwy::Unpredictable1() + 3);
    // So that we can subtract two iotas without resulting in a constant.
    const Vec<D> tv4 = Add(v4, v4);
    // For range-limited (so mul does not overflow), non-constant inputs.
    // We cannot just And() because T might be floating-point.
    alignas(16) static const T mod_lanes[16] = {
        ConvertScalarTo<T>(0), ConvertScalarTo<T>(1), ConvertScalarTo<T>(2),
        ConvertScalarTo<T>(hwy::Unpredictable1() + 2)};
    const Vec<D> in_mul = LoadDup128(d, mod_lanes);

    using TI = MakeSigned<T>;  // For mask > 0 comparison
    const Rebind<TI, D> di;
    using VI = Vec<decltype(di)>;
    const size_t N = Lanes(d);
    auto bool_lanes = AllocateAligned<TI>(N);
    auto expected_add = AllocateAligned<T>(N);
    auto expected_sub = AllocateAligned<T>(N);
    auto expected_mul = AllocateAligned<T>(N);
    HWY_ASSERT(bool_lanes && expected_add && expected_sub && expected_mul);

    // Each lane should have a chance of having mask=true.
    for (size_t rep = 0; rep < AdjustedReps(200); ++rep) {
      for (size_t i = 0; i < N; ++i) {
        bool_lanes[i] = (Random32(&rng) & 1024) ? TI(1) : TI(0);
        if (bool_lanes[i]) {
          expected_add[i] = ConvertScalarTo<T>(2 * i + 7);
          expected_sub[i] = ConvertScalarTo<T>(i + 5);
          const size_t mod_i = i & ((16 / sizeof(T)) - 1);
          expected_mul[i] =
              ConvertScalarTo<T>(mod_lanes[mod_i] * mod_lanes[mod_i]);
        } else {
          expected_add[i] = ConvertScalarTo<T>(i + 2);
          expected_sub[i] = ConvertScalarTo<T>(i + 2);
          expected_mul[i] = ConvertScalarTo<T>(i + 2);
        }
      }

      const VI mask_i = Load(di, bool_lanes.get());
      const Mask<D> mask = RebindMask(d, Gt(mask_i, Zero(di)));

      HWY_ASSERT_VEC_EQ(d, expected_add.get(), MaskedAddOr(v2, mask, v3, v4));
      HWY_ASSERT_VEC_EQ(d, expected_sub.get(), MaskedSubOr(v2, mask, tv4, v3));
      HWY_ASSERT_VEC_EQ(d, expected_mul.get(),
                        MaskedMulOr(v2, mask, in_mul, in_mul));
    }
  }
};

HWY_NOINLINE void TestAllAddSubMul() {
  ForAllTypes(ForPartialVectors<TestAddSubMul>());
}

struct TestUnsignedSatAddSub {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    RandomState rng;

    using TI = MakeSigned<T>;  // For mask > 0 comparison
    const Rebind<TI, D> di;
    using VI = Vec<decltype(di)>;
    const size_t N = Lanes(d);
    auto bool_lanes = AllocateAligned<TI>(N);
    HWY_ASSERT(bool_lanes);

    const Vec<D> v2 = Iota(d, hwy::Unpredictable1() + 1);

    const Vec<D> v0 = Zero(d);
    const Vec<D> vi = Iota(d, 1);
    const Vec<D> vm = Set(d, LimitsMax<T>());

    // Each lane should have a chance of having mask=true.
    for (size_t rep = 0; rep < AdjustedReps(200); ++rep) {
      for (size_t i = 0; i < N; ++i) {
        bool_lanes[i] = (Random32(&rng) & 1024) ? TI(1) : TI(0);
      }
      const VI mask_i = Load(di, bool_lanes.get());
      const Mask<D> mask = RebindMask(d, Gt(mask_i, Zero(di)));

      const Vec<D> disabled_lane_val = Iota(d, 2);

      Vec<D> expected_add =
          IfThenElse(mask, Set(d, static_cast<T>(0)), disabled_lane_val);
      HWY_ASSERT_VEC_EQ(d, expected_add, MaskedSatAddOr(v2, mask, v0, v0));
      expected_add = IfThenElse(mask, vi, disabled_lane_val);
      HWY_ASSERT_VEC_EQ(d, expected_add, MaskedSatAddOr(v2, mask, v0, vi));
      expected_add = IfThenElse(mask, Set(d, static_cast<T>(LimitsMax<T>())),
                                disabled_lane_val);
      HWY_ASSERT_VEC_EQ(d, expected_add, MaskedSatAddOr(v2, mask, v0, vm));
      HWY_ASSERT_VEC_EQ(d, expected_add, MaskedSatAddOr(v2, mask, vi, vm));
      HWY_ASSERT_VEC_EQ(d, expected_add, MaskedSatAddOr(v2, mask, vm, vm));

      Vec<D> expected_sub =
          IfThenElse(mask, Set(d, static_cast<T>(0)), disabled_lane_val);
      HWY_ASSERT_VEC_EQ(d, expected_sub, MaskedSatSubOr(v2, mask, v0, v0));
      HWY_ASSERT_VEC_EQ(d, expected_sub, MaskedSatSubOr(v2, mask, v0, vi));
      HWY_ASSERT_VEC_EQ(d, expected_sub, MaskedSatSubOr(v2, mask, vi, vi));
      HWY_ASSERT_VEC_EQ(d, expected_sub, MaskedSatSubOr(v2, mask, vi, vm));
      expected_sub = IfThenElse(mask, Sub(vm, vi), disabled_lane_val);
      HWY_ASSERT_VEC_EQ(d, expected_sub, MaskedSatSubOr(v2, mask, vm, vi));
    }
  }
};

struct TestSignedSatAddSub {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    RandomState rng;

    using TI = MakeSigned<T>;  // For mask > 0 comparison
    const Rebind<TI, D> di;
    using VI = Vec<decltype(di)>;
    const size_t N = Lanes(d);
    auto bool_lanes = AllocateAligned<TI>(N);
    HWY_ASSERT(bool_lanes);

    const Vec<D> v2 = Iota(d, hwy::Unpredictable1() + 1);

    const Vec<D> v0 = Zero(d);
    const Vec<D> vpm = Set(d, LimitsMax<T>());
    const Vec<D> vi = PositiveIota(d);
    const Vec<D> vn = Sub(v0, vi);
    const Vec<D> vnm = Set(d, LimitsMin<T>());

    // Each lane should have a chance of having mask=true.
    for (size_t rep = 0; rep < AdjustedReps(200); ++rep) {
      for (size_t i = 0; i < N; ++i) {
        bool_lanes[i] = (Random32(&rng) & 1024) ? TI(1) : TI(0);
      }
      const VI mask_i = Load(di, bool_lanes.get());
      const Mask<D> mask = RebindMask(d, Gt(mask_i, Zero(di)));

      const Vec<D> disabled_lane_val = Iota(d, 2);

      Vec<D> expected_add = IfThenElse(mask, v0, disabled_lane_val);
      HWY_ASSERT_VEC_EQ(d, expected_add, MaskedSatAddOr(v2, mask, v0, v0));
      expected_add = IfThenElse(mask, vi, disabled_lane_val);
      HWY_ASSERT_VEC_EQ(d, expected_add, MaskedSatAddOr(v2, mask, v0, vi));
      expected_add = IfThenElse(mask, vpm, disabled_lane_val);
      HWY_ASSERT_VEC_EQ(d, expected_add, MaskedSatAddOr(v2, mask, v0, vpm));
      HWY_ASSERT_VEC_EQ(d, expected_add, MaskedSatAddOr(v2, mask, vi, vpm));
      HWY_ASSERT_VEC_EQ(d, expected_add, MaskedSatAddOr(v2, mask, vpm, vpm));

      Vec<D> expected_sub = IfThenElse(mask, v0, disabled_lane_val);
      HWY_ASSERT_VEC_EQ(d, expected_sub, MaskedSatSubOr(v2, mask, v0, v0));
      expected_sub = IfThenElse(mask, Sub(v0, vi), disabled_lane_val);
      HWY_ASSERT_VEC_EQ(d, expected_sub, MaskedSatSubOr(v2, mask, v0, vi));
      expected_sub = IfThenElse(mask, vn, disabled_lane_val);
      HWY_ASSERT_VEC_EQ(d, expected_sub, MaskedSatSubOr(v2, mask, vn, v0));
      expected_sub = IfThenElse(mask, vnm, disabled_lane_val);
      HWY_ASSERT_VEC_EQ(d, expected_sub, MaskedSatSubOr(v2, mask, vnm, vi));
      HWY_ASSERT_VEC_EQ(d, expected_sub, MaskedSatSubOr(v2, mask, vnm, vpm));
    }
  }
};

HWY_NOINLINE void TestAllSatAddSub() {
  ForU816(ForPartialVectors<TestUnsignedSatAddSub>());
  ForI816(ForPartialVectors<TestSignedSatAddSub>());
}

struct TestDiv {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    RandomState rng;

    using TI = MakeSigned<T>;  // For mask > 0 comparison
    const Rebind<TI, D> di;
    using VI = Vec<decltype(di)>;

    // Wrap after 7 so that even float16_t can represent 1 << iota1.
    const VI viota1 = And(Iota(di, hwy::Unpredictable1()), Set(di, 7));
    const Vec<D> pows = ConvertTo(d, Shl(Set(di, 1), viota1));
    const Vec<D> no = ConvertTo(d, viota1);

    const size_t N = Lanes(d);
    auto bool_lanes = AllocateAligned<TI>(N);
    auto expected = AllocateAligned<T>(N);
    HWY_ASSERT(bool_lanes && expected);

    // Each lane should have a chance of having mask=true.
    for (size_t rep = 0; rep < AdjustedReps(200); ++rep) {
      ZeroBytes(expected.get(), N * sizeof(T));
      for (size_t i = 0; i < N; ++i) {
        bool_lanes[i] = (Random32(&rng) & 1024) ? TI(1) : TI(0);
        const size_t iota1 = (i + 1) & 7;
        expected[i] = ConvertScalarTo<T>(iota1);
        if (bool_lanes[i]) {
          expected[i] =
              ConvertScalarTo<T>(static_cast<double>(1 << iota1) / 2.0);
        }
      }

      const VI mask_i = Load(di, bool_lanes.get());
      const Mask<D> mask = RebindMask(d, Gt(mask_i, Zero(di)));

      const Vec<D> div = Set(d, ConvertScalarTo<T>(2));
      HWY_ASSERT_VEC_EQ(d, expected.get(), MaskedDivOr(no, mask, pows, div));
    }
  }
};

HWY_NOINLINE void TestAllDiv() { ForFloatTypes(ForPartialVectors<TestDiv>()); }

struct TestIntegerDivMod {
  template <class D, HWY_IF_SIGNED_D(D)>
  static HWY_INLINE void DoSignedDivModTests(
      D d, const TFromD<D>* HWY_RESTRICT expected_quot,
      const TFromD<D>* HWY_RESTRICT expected_mod,
      const TFromD<D>* HWY_RESTRICT neg_expected_quot,
      const TFromD<D>* HWY_RESTRICT neg_expected_mod, Mask<D> mask, Vec<D> va,
      Vec<D> vb) {
    using T = TFromD<D>;

    const auto v1 = Set(d, static_cast<T>(1));
    const auto vneg1 = Set(d, static_cast<T>(-1));

    const auto neg_a = Neg(va);
    const auto neg_b = Neg(vb);

    HWY_ASSERT_VEC_EQ(d, neg_expected_quot,
                      MaskedDivOr(vneg1, mask, neg_a, vb));
    HWY_ASSERT_VEC_EQ(d, neg_expected_quot,
                      MaskedDivOr(vneg1, mask, va, neg_b));
    HWY_ASSERT_VEC_EQ(d, expected_quot, MaskedDivOr(v1, mask, neg_a, neg_b));

    HWY_ASSERT_VEC_EQ(d, neg_expected_mod, MaskedModOr(neg_b, mask, neg_a, vb));
    HWY_ASSERT_VEC_EQ(d, expected_mod, MaskedModOr(vb, mask, va, neg_b));
    HWY_ASSERT_VEC_EQ(d, neg_expected_mod,
                      MaskedModOr(neg_b, mask, neg_a, neg_b));
  }

  template <class D, HWY_IF_UNSIGNED_D(D)>
  static HWY_INLINE void DoSignedDivModTests(
      D /*d*/, const TFromD<D>* HWY_RESTRICT /*expected_quot*/,
      const TFromD<D>* HWY_RESTRICT /*expected_mod*/,
      const TFromD<D>* HWY_RESTRICT /*neg_expected_quot*/,
      const TFromD<D>* HWY_RESTRICT /*neg_expected_mod*/, Mask<D> /*mask*/,
      Vec<D> /*va*/, Vec<D> /*vb*/) {}

  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    RandomState rng;

    const auto v1 = Set(d, static_cast<T>(1));
    const auto vmax = Set(d, LimitsMax<T>());

    const auto vb = Max(And(Iota(d, hwy::Unpredictable1() + 1),
                            Set(d, static_cast<T>(LimitsMax<T>() >> 1))),
                        Set(d, static_cast<T>(2)));
    const auto va =
        Max(And(Sub(vmax, Iota(d, static_cast<T>(hwy::Unpredictable1() - 1))),
                vmax),
            Add(vb, vb));

    using TI = MakeSigned<T>;  // For mask > 0 comparison
    using TU = MakeUnsigned<T>;
    const Rebind<TI, D> di;
    using VI = Vec<decltype(di)>;
    const size_t N = Lanes(d);

#if HWY_TARGET <= HWY_AVX3 && HWY_IS_MSAN
    // Workaround for MSAN bug on AVX3
    if (sizeof(T) <= 2 && N >= 16) {
      return;
    }
#endif
#if HWY_COMPILER_CLANG && HWY_ARCH_RISCV && HWY_TARGET == HWY_EMU128
    // Workaround for incorrect codegen. Off by one in the lowest lane.
    if (sizeof(T) == 4) return;
#endif

    auto bool_lanes = AllocateAligned<TI>(N);
    auto expected_quot = AllocateAligned<T>(N);
    auto expected_mod = AllocateAligned<T>(N);
    auto neg_expected_quot = AllocateAligned<T>(N);
    auto neg_expected_mod = AllocateAligned<T>(N);
    HWY_ASSERT(bool_lanes && expected_quot && expected_mod &&
               neg_expected_quot && neg_expected_mod);

    // Each lane should have a chance of having mask=true.
    for (size_t rep = 0; rep < AdjustedReps(200); ++rep) {
      for (size_t i = 0; i < N; ++i) {
        const auto a0 = static_cast<T>((static_cast<TU>(LimitsMax<T>()) - i) &
                                       LimitsMax<T>());
        const auto b0 =
            static_cast<T>((i + 2u) & static_cast<TU>(LimitsMax<T>() >> 1));

        const auto b = static_cast<T>(HWY_MAX(b0, 2));
        const auto a = static_cast<T>(HWY_MAX(a0, b + b));

        bool_lanes[i] = (Random32(&rng) & 1024) ? TI(1) : TI(0);
        if (bool_lanes[i]) {
          expected_quot[i] = static_cast<T>(a / b);
          expected_mod[i] = static_cast<T>(a % b);
        } else {
          expected_quot[i] = static_cast<T>(1);
          expected_mod[i] = b;
        }

        neg_expected_quot[i] =
            static_cast<T>(static_cast<T>(0) - expected_quot[i]);
        neg_expected_mod[i] =
            static_cast<T>(static_cast<T>(0) - expected_mod[i]);
      }

      const VI mask_i = Load(di, bool_lanes.get());
      const Mask<D> mask = RebindMask(d, Gt(mask_i, Zero(di)));

      HWY_ASSERT_VEC_EQ(d, expected_quot.get(), MaskedDivOr(v1, mask, va, vb));
      HWY_ASSERT_VEC_EQ(d, expected_mod.get(), MaskedModOr(vb, mask, va, vb));
      DoSignedDivModTests(d, expected_quot.get(), expected_mod.get(),
                          neg_expected_quot.get(), neg_expected_mod.get(), mask,
                          va, vb);
    }
  }
};

HWY_NOINLINE void TestAllIntegerDivMod() {
  ForIntegerTypes(ForPartialVectors<TestIntegerDivMod>());
}

struct TestFloatExceptions {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const Vec<D> v4 = Iota(d, hwy::Unpredictable1() + 3);
    const Mask<D> m0 = MaskFalse(d);

    // No overflow
    const Vec<D> inf = Inf(d);
    HWY_ASSERT_VEC_EQ(d, v4, MaskedAddOr(v4, m0, inf, inf));
    HWY_ASSERT_VEC_EQ(d, v4, MaskedSubOr(v4, m0, Neg(inf), Neg(inf)));

    // No underflow
    const Vec<D> eps = Set(d, Epsilon<T>());
    const Vec<D> half = Set(d, static_cast<T>(0.5f));
    HWY_ASSERT_VEC_EQ(d, v4, MaskedMulOr(v4, m0, eps, half));

    // Division by zero
    const Vec<D> v0 = Set(d, ConvertScalarTo<T>(0));
    HWY_ASSERT_VEC_EQ(d, v4, MaskedDivOr(v4, m0, v4, v0));
  }
};

HWY_NOINLINE void TestAllFloatExceptions() {
  ForFloatTypes(ForPartialVectors<TestFloatExceptions>());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyMaskedArithmeticTest);
HWY_EXPORT_AND_TEST_P(HwyMaskedArithmeticTest, TestAllAddSubMul);
HWY_EXPORT_AND_TEST_P(HwyMaskedArithmeticTest, TestAllSatAddSub);
HWY_EXPORT_AND_TEST_P(HwyMaskedArithmeticTest, TestAllDiv);
HWY_EXPORT_AND_TEST_P(HwyMaskedArithmeticTest, TestAllIntegerDivMod);
HWY_EXPORT_AND_TEST_P(HwyMaskedArithmeticTest, TestAllFloatExceptions);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
