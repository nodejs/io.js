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
#define HWY_TARGET_INCLUDE "tests/div_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/nanobenchmark.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

struct TestIntegerDiv {
  template <class D, typename T = TFromD<D>>
  static HWY_NOINLINE void DoDiv(D d, const T* HWY_RESTRICT a_lanes,
                                 const T* HWY_RESTRICT b_lanes, bool neg_a,
                                 bool neg_b) {
    const RebindToSigned<decltype(d)> di;
    using TI = TFromD<decltype(di)>;
    const size_t N = Lanes(d);
    using V = VFromD<D>;
    auto expected = AllocateAligned<T>(N);
    auto expected_even = AllocateAligned<T>(N);
    auto expected_odd = AllocateAligned<T>(N);
    HWY_ASSERT(expected && expected_even && expected_odd);

    V a = Load(d, a_lanes);
    V b = Load(d, b_lanes);
    if (neg_a) a = BitCast(d, Neg(BitCast(di, a)));
    if (neg_b) b = BitCast(d, Neg(BitCast(di, b)));

    for (size_t i = 0; i < N; i++) {
      const T a1 =
          neg_a ? static_cast<T>(-static_cast<TI>(a_lanes[i])) : a_lanes[i];
      const T b1 =
          neg_b ? static_cast<T>(-static_cast<TI>(b_lanes[i])) : b_lanes[i];
      HWY_ASSERT(b1 != 0);
      expected[i] = static_cast<T>(a1 / b1);
      if ((i & 1) == 0) {
        expected_even[i] = expected[i];
        expected_odd[i] = static_cast<T>(0);
      } else {
        expected_even[i] = static_cast<T>(0);
        expected_odd[i] = expected[i];
      }
    }

    HWY_ASSERT_VEC_EQ(d, expected.get(), Div(a, b));

    const V vmin = Set(d, LimitsMin<T>());
    const V zero = Zero(d);
    const V all_ones = Set(d, static_cast<T>(-1));

    HWY_ASSERT_VEC_EQ(d, expected_even.get(),
                      OddEven(zero, Div(a, OddEven(zero, b))));
    HWY_ASSERT_VEC_EQ(d, expected_odd.get(),
                      OddEven(Div(a, OddEven(b, zero)), zero));

    HWY_ASSERT_VEC_EQ(
        d, expected_even.get(),
        OddEven(zero, Div(OddEven(vmin, a), OddEven(all_ones, b))));
    HWY_ASSERT_VEC_EQ(
        d, expected_odd.get(),
        OddEven(Div(OddEven(a, vmin), OddEven(b, all_ones)), zero));
  }

  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    using TU = MakeUnsigned<T>;
    using TI = MakeSigned<T>;
    using V = VFromD<D>;

    const size_t N = Lanes(d);

#if HWY_TARGET <= HWY_AVX3 && HWY_IS_MSAN
    // Workaround for MSAN bug on AVX3
    if (sizeof(T) <= 2 && N >= 16) return;
#endif

#if HWY_COMPILER_CLANG && HWY_ARCH_RISCV && HWY_TARGET == HWY_EMU128
    // Workaround for incorrect codegen. The implementation splits vectors
    // into halves and then combines them; the upper half is incorrect.
    if (sizeof(T) == 4 && N == 4) return;
#endif

    const T k1 = static_cast<T>(Unpredictable1());
    const T kMin = static_cast<T>(LimitsMin<T>() * k1);
    const T kMax = static_cast<T>(LimitsMax<T>() * k1);
    const V vmin = Set(d, kMin);
    const V vmax = Set(d, kMax);
    const V v1 = Set(d, static_cast<T>(k1));
    const V v2 = Set(d, static_cast<T>(k1 + 1));
    const V v3 = Set(d, static_cast<T>(k1 + 2));

    HWY_ASSERT_VEC_EQ(d, vmin, Div(vmin, v1));
    HWY_ASSERT_VEC_EQ(d, vmax, Div(vmax, v1));
    HWY_ASSERT_VEC_EQ(d, Set(d, static_cast<T>(kMin / 2)), Div(vmin, v2));
    HWY_ASSERT_VEC_EQ(d, Set(d, static_cast<T>(kMin / 3)), Div(vmin, v3));
    HWY_ASSERT_VEC_EQ(d, Set(d, static_cast<T>(kMax / 2)), Div(vmax, v2));
    HWY_ASSERT_VEC_EQ(d, Set(d, static_cast<T>(kMax / 3)), Div(vmax, v3));

    auto in1 = AllocateAligned<T>(N);
    auto in2 = AllocateAligned<T>(N);
    HWY_ASSERT(in1 && in2);

    // Random inputs in each lane
    RandomState rng;
    for (size_t rep = 0; rep < AdjustedReps(1000); ++rep) {
      for (size_t i = 0; i < N; ++i) {
        const T rnd_a0 = static_cast<T>(Random64(&rng) &
                                        static_cast<uint64_t>(LimitsMax<TU>()));
        const T rnd_b0 = static_cast<T>(Random64(&rng) &
                                        static_cast<uint64_t>(LimitsMax<TI>()));

        const T rnd_b = static_cast<T>(rnd_b0 | static_cast<T>(rnd_b0 == 0));
        const T rnd_a = static_cast<T>(
            rnd_a0 + static_cast<T>(IsSigned<T>() && rnd_a0 == LimitsMin<T>() &&
                                    ScalarAbs(rnd_b) == static_cast<T>(1)));

        in1[i] = rnd_a;
        in2[i] = rnd_b;
      }

      const bool neg_a = true;
      const bool neg_b = true;
      DoDiv(d, in1.get(), in2.get(), false, false);
      DoDiv(d, in1.get(), in2.get(), false, neg_b);
      DoDiv(d, in1.get(), in2.get(), neg_a, false);
      DoDiv(d, in1.get(), in2.get(), neg_a, neg_b);
    }
  }
};

HWY_NOINLINE void TestAllIntegerDiv() {
  ForIntegerTypes(ForPartialVectors<TestIntegerDiv>());
}

struct TestIntegerMod {
  template <class D>
  static HWY_NOINLINE void DoTestIntegerMod(D d, const VecArg<VFromD<D>> a,
                                            const VecArg<VFromD<D>> b) {
    using T = TFromD<D>;

    const size_t N = Lanes(d);

#if HWY_TARGET <= HWY_AVX3 && HWY_IS_MSAN
    // Workaround for MSAN bug on AVX3
    if (sizeof(T) <= 2 && N >= 16) {
      return;
    }
#endif

#if HWY_COMPILER_CLANG && HWY_ARCH_RISCV && HWY_TARGET == HWY_EMU128
    // Workaround for incorrect codegen. The implementation splits vectors
    // into halves and then combines them; the lower half is incorrect.
    if (sizeof(T) == 4 && N == 4) return;
#endif

    auto a_lanes = AllocateAligned<T>(N);
    auto b_lanes = AllocateAligned<T>(N);
    auto expected = AllocateAligned<T>(N);
    auto expected_even = AllocateAligned<T>(N);
    auto expected_odd = AllocateAligned<T>(N);
    HWY_ASSERT(a_lanes && b_lanes && expected && expected_even && expected_odd);

    Store(a, d, a_lanes.get());
    Store(b, d, b_lanes.get());

    for (size_t i = 0; i < N; i++) {
      expected[i] = static_cast<T>(a_lanes[i] % b_lanes[i]);
      if ((i & 1) == 0) {
        expected_even[i] = expected[i];
        expected_odd[i] = static_cast<T>(0);
      } else {
        expected_even[i] = static_cast<T>(0);
        expected_odd[i] = expected[i];
      }
    }

    HWY_ASSERT_VEC_EQ(d, expected.get(), Mod(a, b));

    const auto vmin = Set(d, LimitsMin<T>());
    const auto zero = Zero(d);
    const auto all_ones = Set(d, static_cast<T>(-1));

    HWY_ASSERT_VEC_EQ(d, expected_even.get(),
                      OddEven(zero, Mod(a, OddEven(zero, b))));
    HWY_ASSERT_VEC_EQ(d, expected_odd.get(),
                      OddEven(Mod(a, OddEven(b, zero)), zero));

    HWY_ASSERT_VEC_EQ(
        d, expected_even.get(),
        OddEven(zero, Mod(OddEven(vmin, a), OddEven(all_ones, b))));
    HWY_ASSERT_VEC_EQ(
        d, expected_odd.get(),
        OddEven(Mod(OddEven(a, vmin), OddEven(b, all_ones)), zero));
  }

  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    using TU = MakeUnsigned<T>;
    using TI = MakeSigned<T>;

    const size_t N = Lanes(d);
    auto in1 = AllocateAligned<T>(N);
    auto in2 = AllocateAligned<T>(N);
    HWY_ASSERT(in1 && in2);

    const RebindToSigned<decltype(d)> di;

    // Random inputs in each lane
    RandomState rng;
    for (size_t rep = 0; rep < AdjustedReps(1000); ++rep) {
      for (size_t i = 0; i < N; ++i) {
        const T rnd_a0 = static_cast<T>(Random64(&rng) &
                                        static_cast<uint64_t>(LimitsMax<TU>()));
        const T rnd_b0 = static_cast<T>(Random64(&rng) &
                                        static_cast<uint64_t>(LimitsMax<TI>()));

        const T rnd_b = static_cast<T>(rnd_b0 | static_cast<T>(rnd_b0 == 0));
        const T rnd_a = static_cast<T>(
            rnd_a0 + static_cast<T>(IsSigned<T>() && rnd_a0 == LimitsMin<T>() &&
                                    ScalarAbs(rnd_b) == static_cast<T>(1)));

        in1[i] = rnd_a;
        in2[i] = rnd_b;
      }

      const auto a = Load(d, in1.get());
      const auto b = Load(d, in2.get());

      const auto neg_a = BitCast(d, Neg(BitCast(di, a)));
      const auto neg_b = BitCast(d, Neg(BitCast(di, b)));

      DoTestIntegerMod(d, a, b);
      DoTestIntegerMod(d, a, neg_b);
      DoTestIntegerMod(d, neg_a, b);
      DoTestIntegerMod(d, neg_a, neg_b);
    }
  }
};

HWY_NOINLINE void TestAllIntegerMod() {
  ForIntegerTypes(ForPartialVectors<TestIntegerMod>());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyDivTest);
HWY_EXPORT_AND_TEST_P(HwyDivTest, TestAllIntegerDiv);
HWY_EXPORT_AND_TEST_P(HwyDivTest, TestAllIntegerMod);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
