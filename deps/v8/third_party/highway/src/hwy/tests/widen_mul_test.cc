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
#define HWY_TARGET_INCLUDE "tests/widen_mul_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/nanobenchmark.h"  // Unpredictable1
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

struct TestSatWidenMulAccumFixedPoint {
  template <class TN, class DN>
  HWY_INLINE void operator()(TN /*unused*/, DN dn) {
    static_assert(IsSigned<TN>() && !IsFloat<TN>() && !IsSpecialFloat<TN>(),
                  "TN must be a signed integer type");

    using TW = MakeWide<TN>;
    const Rebind<TW, DN> dw;
    using VW = Vec<decltype(dw)>;
    using VN = Vec<decltype(dn)>;

    const VN vn_min = Set(dn, LimitsMin<TN>());

    const VW vw_min = Set(dw, LimitsMin<TW>());
    const VW vw_kneg7 = Set(dw, static_cast<TW>(-7));
    const VW vw_k0 = Zero(dw);
    const VW vw_k1 = Set(dw, static_cast<TW>(1));
    const VW vw_k19 = Set(dw, static_cast<TW>(19));
    const VW vw_max = Set(dw, LimitsMax<TW>());

    const VN vn_p = Add(
        And(Iota(dn, TN{0}), Set(dn, static_cast<TN>(LimitsMax<TN>() >> 3))),
        Set(dn, TN{1}));
    const VW vw_p = PromoteTo(dw, vn_p);
    HWY_ASSERT(AllTrue(dw, Gt(vw_p, vw_k0)));

    const auto vw_n_minus7 = Add(Neg(vw_p), vw_kneg7);

    // As it is implementation-defined if vn_min[i] * vn_min[i] * 2 is first
    // saturated to TW before adding to vw_n_minus7[i], check that
    // actual_minsqr_sum[i] is equal to either LimitsMax<TW>() + vn_n_minus7[i]
    // or LimitsMax<TW>() + vn_n_minus7[i] + 1
    const VW actual_minsqr_sum =
        SatWidenMulAccumFixedPoint(dw, vn_min, vn_min, vw_n_minus7);
    const VW min_expected_minsqr_sum = Add(vw_max, vw_n_minus7);
    const VW max_expected_minsqr_sum = Add(min_expected_minsqr_sum, vw_k1);
    HWY_ASSERT(
        AllTrue(dw, And(Ge(actual_minsqr_sum, min_expected_minsqr_sum),
                        Le(actual_minsqr_sum, max_expected_minsqr_sum))));

    const VN vn_p_plus2 = Add(vn_p, Set(dn, TN{2}));
    const VN vn_p_plus3 = Add(vn_p, Set(dn, TN{3}));

    const VW vw_pp2_pp3 =
        Mul(PromoteTo(dw, vn_p_plus2), PromoteTo(dw, vn_p_plus3));
    const VW vw_pp2_pp3_2 = Add(vw_pp2_pp3, vw_pp2_pp3);

    const VW expected_p_sum = Add(vw_pp2_pp3_2, vw_kneg7);
    const VW actual_p_sum =
        SatWidenMulAccumFixedPoint(dw, vn_p_plus2, vn_p_plus3, vw_kneg7);
    HWY_ASSERT_VEC_EQ(dw, expected_p_sum, actual_p_sum);

    const VW expected_p_sum2 = Add(vw_pp2_pp3_2, vw_k19);
    const VW actual_p_sum2 =
        SatWidenMulAccumFixedPoint(dw, vn_p_plus2, vn_p_plus3, vw_k19);
    HWY_ASSERT_VEC_EQ(dw, expected_p_sum2, actual_p_sum2);

    HWY_ASSERT_VEC_EQ(
        dw, vw_max,
        SatWidenMulAccumFixedPoint(dw, vn_p_plus2, vn_p_plus3, vw_max));

    const VN vn_n_minus3 = Neg(vn_p_plus3);

    const VW expected_n_sum = Sub(vw_kneg7, vw_pp2_pp3_2);
    const VW actual_n_sum =
        SatWidenMulAccumFixedPoint(dw, vn_p_plus2, vn_n_minus3, vw_kneg7);
    HWY_ASSERT_VEC_EQ(dw, expected_n_sum, actual_n_sum);

    HWY_ASSERT_VEC_EQ(
        dw, vw_min,
        SatWidenMulAccumFixedPoint(dw, vn_p_plus2, vn_n_minus3, vw_min));
  }
};

HWY_NOINLINE void TestAllSatWidenMulAccumFixedPoint() {
  ForPromoteVectors<TestSatWidenMulAccumFixedPoint>()(int16_t());
}

#ifndef HWY_NATIVE_DOT_BF16
#error "Update set_macros-inl.h to set this required macro"
#endif

struct TestMulEvenAdd {
  // Must be inlined on aarch64 for bf16, else clang crashes.
  template <typename TN, class DN>
  HWY_INLINE void operator()(TN /*unused*/, DN dn) {
    using TW = MakeWide<TN>;
    const RepartitionToWide<DN> dw;
    using VW = Vec<decltype(dw)>;
    using VN = Vec<decltype(dn)>;
    const size_t NN = Lanes(dn);

    const VW f0 = Zero(dw);
    const VW f1 = Set(dw, TW{1});
    const VN bf0 = Zero(dn);
    // Cannot Set() bfloat16_t directly.
    const VN bf1 = ReorderDemote2To(dn, f1, f1);

    // Any input zero => both outputs zero
    HWY_ASSERT_VEC_EQ(dw, f0, MulEvenAdd(dw, bf0, bf0, f0));
    HWY_ASSERT_VEC_EQ(dw, f0, MulEvenAdd(dw, bf0, bf1, f0));
    HWY_ASSERT_VEC_EQ(dw, f0, MulEvenAdd(dw, bf1, bf0, f0));
    HWY_ASSERT_VEC_EQ(dw, f0, MulOddAdd(dw, bf0, bf0, f0));
    HWY_ASSERT_VEC_EQ(dw, f0, MulOddAdd(dw, bf0, bf1, f0));
    HWY_ASSERT_VEC_EQ(dw, f0, MulOddAdd(dw, bf1, bf0, f0));

    // delta[p] := 1, all others zero. For each p: Mul(delta, 1, 0) == 1.
    auto delta_w = AllocateAligned<TW>(NN);
    HWY_ASSERT(delta_w);
    for (size_t p = 0; p < NN; ++p) {
      // Workaround for incorrect Clang wasm codegen: re-initialize the entire
      // array rather than zero-initialize once and then toggle lane p.
      for (size_t i = 0; i < NN; ++i) {
        delta_w[i] = static_cast<TW>(i == p ? Unpredictable1() : 0);
      }
      const VW delta0 = Load(dw, delta_w.get());
      const VW delta1 = Load(dw, delta_w.get() + NN / 2);
      const VN delta = OrderedDemote2To(dn, delta0, delta1);

      {
        const VW sum_e = MulEvenAdd(dw, delta, bf1, f0);
        const VW sum_o = MulOddAdd(dw, delta, bf1, f0);
        HWY_ASSERT_VEC_EQ(dw, p & 1 ? f0 : f1, SumOfLanes(dw, sum_e));
        HWY_ASSERT_VEC_EQ(dw, p & 1 ? f1 : f0, SumOfLanes(dw, sum_o));
      }
      // Swapped arg order
      {
        const VW sum_e = MulEvenAdd(dw, bf1, delta, f0);
        const VW sum_o = MulOddAdd(dw, bf1, delta, f0);
        HWY_ASSERT_VEC_EQ(dw, p & 1 ? f0 : f1, SumOfLanes(dw, sum_e));
        HWY_ASSERT_VEC_EQ(dw, p & 1 ? f1 : f0, SumOfLanes(dw, sum_o));
      }
      // Start with nonzero sum
      {
        const VW sum_e = MulEvenAdd(dw, delta, bf1, Add(delta0, delta1));
        const VW sum_o = MulOddAdd(dw, delta, bf1, Add(delta0, delta1));
        HWY_ASSERT_EQ(TW{3}, ReduceSum(dw, Add(sum_e, sum_o)));
      }

      // Start with nonzero sum and swap arg order
      {
        const VW sum_e = MulEvenAdd(dw, bf1, delta, Add(delta0, delta1));
        const VW sum_o = MulOddAdd(dw, bf1, delta, Add(delta0, delta1));
        HWY_ASSERT_EQ(TW{3}, ReduceSum(dw, Add(sum_e, sum_o)));
      }
    }
  }
};

HWY_NOINLINE void TestAllMulEvenAdd() {
  ForShrinkableVectors<TestMulEvenAdd>()(bfloat16_t());
}

struct TestReorderWidenMulAccumulate {
  // Must be inlined on aarch64 for bf16, else clang crashes.
  template <typename TN, class DN>
  HWY_INLINE void operator()(TN /*unused*/, DN dn) {
    using TW = MakeWide<TN>;
    const RepartitionToWide<DN> dw;
    using VW = Vec<decltype(dw)>;
    using VN = Vec<decltype(dn)>;
    const size_t NN = Lanes(dn);

    const VW f0 = Zero(dw);
    const VW f1 = Set(dw, TW{1});
    const VN bf0 = Zero(dn);
    // Cannot Set() bfloat16_t directly.
    const VN bf1 = ReorderDemote2To(dn, f1, f1);

    // Any input zero => both outputs zero
    VW sum1 = f0;
    HWY_ASSERT_VEC_EQ(dw, f0,
                      ReorderWidenMulAccumulate(dw, bf0, bf0, f0, sum1));
    HWY_ASSERT_VEC_EQ(dw, f0, sum1);
    HWY_ASSERT_VEC_EQ(dw, f0,
                      ReorderWidenMulAccumulate(dw, bf0, bf1, f0, sum1));
    HWY_ASSERT_VEC_EQ(dw, f0, sum1);
    HWY_ASSERT_VEC_EQ(dw, f0,
                      ReorderWidenMulAccumulate(dw, bf1, bf0, f0, sum1));
    HWY_ASSERT_VEC_EQ(dw, f0, sum1);

    // delta[p] := 1, all others zero. For each p: Dot(delta, all-ones) == 1.
    auto delta_w = AllocateAligned<TW>(NN);
    HWY_ASSERT(delta_w);
    for (size_t p = 0; p < NN; ++p) {
      // Workaround for incorrect Clang wasm codegen: re-initialize the entire
      // array rather than zero-initialize once and then toggle lane p.
      for (size_t i = 0; i < NN; ++i) {
        delta_w[i] = static_cast<TW>(i == p);
      }
      const VW delta0 = Load(dw, delta_w.get());
      const VW delta1 = Load(dw, delta_w.get() + NN / 2);
      const VN delta = ReorderDemote2To(dn, delta0, delta1);

      {
        sum1 = f0;
        const VW sum0 = ReorderWidenMulAccumulate(dw, delta, bf1, f0, sum1);
        HWY_ASSERT_EQ(TW{1}, ReduceSum(dw, Add(sum0, sum1)));
      }
      // Swapped arg order
      {
        sum1 = f0;
        const VW sum0 = ReorderWidenMulAccumulate(dw, bf1, delta, f0, sum1);
        HWY_ASSERT_EQ(TW{1}, ReduceSum(dw, Add(sum0, sum1)));
      }
      // Start with nonzero sum0 or sum1
      {
        VW sum0 = delta0;
        sum1 = delta1;
        sum0 = ReorderWidenMulAccumulate(dw, delta, bf1, sum0, sum1);
        HWY_ASSERT_EQ(TW{2}, ReduceSum(dw, Add(sum0, sum1)));
      }
      // Start with nonzero sum0 or sum1, and swap arg order
      {
        VW sum0 = delta0;
        sum1 = delta1;
        sum0 = ReorderWidenMulAccumulate(dw, bf1, delta, sum0, sum1);
        HWY_ASSERT_EQ(TW{2}, ReduceSum(dw, Add(sum0, sum1)));
      }
    }
  }
};


struct TestWidenMulAccumulate {
  template <typename TN, class DN>
  HWY_INLINE void operator()(TN /*unused*/, DN dn) {
    using TW = MakeWide<TN>;
    const RepartitionToWide<DN> dw;
    const Half<DN> dnh;
    using VW = Vec<decltype(dw)>;
    using VN = Vec<decltype(dn)>;
    const size_t NN = Lanes(dn);

    VW f0 = Zero(dw);
    const VN bf0 = Zero(dn);
    const VN bf1 = Set(dn, TW{5});

    // Any input zero => both outputs zero
    VW sum1 = f0;
    HWY_ASSERT_VEC_EQ(dw, f0,
                      WidenMulAccumulate(dw, bf0, bf0, f0, sum1));
    HWY_ASSERT_VEC_EQ(dw, f0, sum1);
    HWY_ASSERT_VEC_EQ(dw, f0,
                      WidenMulAccumulate(dw, bf0, bf1, f0, sum1));
    HWY_ASSERT_VEC_EQ(dw, f0, sum1);
    HWY_ASSERT_VEC_EQ(dw, f0, WidenMulAccumulate(dw, bf1, bf0, f0, sum1));
    HWY_ASSERT_VEC_EQ(dw, f0, sum1);

    // delta[p] := 1, all others zero.
    auto delta_w = AllocateAligned<TW>(NN);
    HWY_ASSERT(delta_w);
    for (size_t p = 0; p < NN; ++p) {
      // Workaround for incorrect Clang wasm codegen: re-initialize the entire
      // array rather than zero-initialize once and then toggle lane p.
      for (size_t i = 0; i < NN; ++i) {
        delta_w[i] = static_cast<TW>(i == p);
      }
      const VW delta0 = Load(dw, delta_w.get());
      const VW delta1 = Load(dw, delta_w.get() + NN / 2);
      const VN delta = Combine(dn, DemoteTo(dnh, Add(delta0, delta1)),
                               DemoteTo(dnh, Sub(delta0, delta1)));
      VW highSumBefore;

      f0 = Set(dw, 6);

      {
        sum1 = f0;
        highSumBefore = sum1;
        const VW sum0 = WidenMulAccumulate(dw, delta, bf1, f0, sum1);
        const VW expectedLow =
            MulAdd(PromoteLowerTo(dw, delta), PromoteLowerTo(dw, bf1), f0);
        HWY_ASSERT_VEC_EQ(dw, expectedLow, sum0);
        const VW expectedHigh = MulAdd(PromoteUpperTo(dw, delta),
                                       PromoteUpperTo(dw, bf1), highSumBefore);
        HWY_ASSERT_VEC_EQ(dw, expectedHigh, sum1);
      }
      // Swapped arg order
      {
        sum1 = f0;
        highSumBefore = sum1;
        const VW sum0 = WidenMulAccumulate(dw, bf1, delta, f0, sum1);
        const VW expectedLow =
            MulAdd(PromoteLowerTo(dw, bf1), PromoteLowerTo(dw, delta), f0);
        HWY_ASSERT_VEC_EQ(dw, expectedLow, sum0);
        const VW expectedHigh = MulAdd(
            PromoteUpperTo(dw, bf1), PromoteUpperTo(dw, delta), highSumBefore);
        HWY_ASSERT_VEC_EQ(dw, expectedHigh, sum1);
      }
      // Start with nonzero sum0 or sum1
      {
        VW sum0 = PromoteTo(dw, LowerHalf(dnh, delta));
        VW lowSumBefore = sum0;
        sum1 = PromoteTo(dw, UpperHalf(dnh, delta));
        highSumBefore = sum1;
        sum0 = WidenMulAccumulate(dw, delta, bf1, sum0, sum1);
        const VW expectedLow = MulAdd(PromoteLowerTo(dw, delta),
                                      PromoteLowerTo(dw, bf1), lowSumBefore);
        HWY_ASSERT_VEC_EQ(dw, expectedLow, sum0);
        const VW expectedHigh = MulAdd(PromoteUpperTo(dw, delta),
                                       PromoteUpperTo(dw, bf1), highSumBefore);
        HWY_ASSERT_VEC_EQ(dw, expectedHigh, sum1);
      }
      // Start with nonzero sum0 or sum1, and swap arg order
      {
        VW sum0 = PromoteTo(dw, LowerHalf(dnh, delta));
        VW lowSumBefore = sum0;
        sum1 = PromoteTo(dw, UpperHalf(dnh, delta));
        highSumBefore = sum1;
        sum0 = WidenMulAccumulate(dw, bf1, delta, sum0, sum1);
        const VW expectedLow = MulAdd(PromoteLowerTo(dw, bf1),
                                      PromoteLowerTo(dw, delta), lowSumBefore);
        HWY_ASSERT_VEC_EQ(dw, expectedLow, sum0);
        const VW expectedHigh = MulAdd(
            PromoteUpperTo(dw, bf1), PromoteUpperTo(dw, delta), highSumBefore);
        HWY_ASSERT_VEC_EQ(dw, expectedHigh, sum1);
      }
    }
  }
};

HWY_NOINLINE void TestAllWidenMulAccumulate() {
  ForShrinkableVectors<TestWidenMulAccumulate>()(int32_t());
  ForShrinkableVectors<TestWidenMulAccumulate>()(uint32_t());
  ForShrinkableVectors<TestWidenMulAccumulate>()(int16_t());
  ForShrinkableVectors<TestWidenMulAccumulate>()(uint16_t());
  ForShrinkableVectors<TestWidenMulAccumulate>()(int8_t());
  ForShrinkableVectors<TestWidenMulAccumulate>()(uint8_t());
#if 0  // not yet implemented
#if HWY_HAVE_FLOAT16 && HWY_TARGET_IS_NEON
  ForShrinkableVectors<TestWidenMulAccumulate>()(float16_t());
#endif
#endif
}

HWY_NOINLINE void TestAllReorderWidenMulAccumulate() {
  ForShrinkableVectors<TestReorderWidenMulAccumulate>()(bfloat16_t());
  ForShrinkableVectors<TestReorderWidenMulAccumulate>()(int16_t());
  ForShrinkableVectors<TestReorderWidenMulAccumulate>()(uint16_t());
}

struct TestRearrangeToOddPlusEven {
  // Must be inlined on aarch64 for bf16, else clang crashes.
  template <typename TN, class DN>
  HWY_INLINE void operator()(TN /*unused*/, DN dn) {
    using TW = MakeWide<TN>;
    const RepartitionToWide<DN> dw;
    using VW = Vec<decltype(dw)>;
    using VN = Vec<decltype(dn)>;
    const size_t NW = Lanes(dw);

    const auto expected = AllocateAligned<TW>(NW);
    HWY_ASSERT(expected);
    for (size_t iw = 0; iw < NW; ++iw) {
      const size_t in = iw * 2;  // even, odd is +1
      const size_t a0 = 1 + in;
      const size_t b0 = 1 + 2 * NW - a0;
      const size_t a1 = a0 + 1;
      const size_t b1 = b0 - 1;
      expected[iw] = static_cast<TW>(a0 * b0 + a1 * b1);
    }

    const VW up0 = Iota(dw, 1);
    const VW up1 = Iota(dw, 1 + NW);
    // We will compute i * (N-i) to avoid per-lane overflow.
    const VW down0 = Reverse(dw, up1);
    const VW down1 = Reverse(dw, up0);

    const VN a = OrderedDemote2To(dn, up0, up1);
    const VN b = OrderedDemote2To(dn, down0, down1);

    VW sum0 = Zero(dw);
    VW sum1 = Zero(dw);
    sum0 = ReorderWidenMulAccumulate(dw, a, b, sum0, sum1);
    const VW sum_odd_even = RearrangeToOddPlusEven(sum0, sum1);
    HWY_ASSERT_VEC_EQ(dw, expected.get(), sum_odd_even);
  }
};

HWY_NOINLINE void TestAllRearrangeToOddPlusEven() {
// For reasons unknown, <128 bit crashes aarch64 clang.
#if HWY_ARCH_ARM_A64 && HWY_COMPILER_CLANG
  ForGEVectors<128, TestRearrangeToOddPlusEven>()(bfloat16_t());
#else
  ForShrinkableVectors<TestRearrangeToOddPlusEven>()(bfloat16_t());
#endif
  ForShrinkableVectors<TestRearrangeToOddPlusEven>()(int16_t());
  ForShrinkableVectors<TestRearrangeToOddPlusEven>()(uint16_t());
}

template <bool MixedSignedness>
struct TestSumOfMulQuadAccumulate {
  template <class DW2, class TN1, class TN2>
  static HWY_INLINE void TestConsecutiveSeqMulQuadAccum(DW2 dw2, TN1 a0,
                                                        TN2 b0) {
    using TW2 = TFromD<DW2>;
    const Repartition<TN1, DW2> dn1;
    const Repartition<TN2, DW2> dn2;

    const auto vn_iota0_mod4 = And(Iota(dn1, 0), Set(dn1, TN1{3}));

    const auto va = Add(vn_iota0_mod4, Set(dn1, a0));
    const auto vb = Add(BitCast(dn2, vn_iota0_mod4), Set(dn2, b0));
    const auto expected =
        Set(dw2,
            static_cast<TW2>((TW2{4} * static_cast<TW2>(a0) * b0) +
                             (TW2{6} * (static_cast<TW2>(a0) + b0)) + TW2{17}));

    HWY_ASSERT_VEC_EQ(dw2, expected,
                      SumOfMulQuadAccumulate(dw2, va, vb, Set(dw2, TW2{3})));
  }

  template <typename TN2, class DN2>
  HWY_INLINE void operator()(TN2 /*unused*/, DN2 dn2) {
    static_assert(!MixedSignedness || IsSigned<TN2>(),
                  "TN2 must be signed if MixedSignedness is true");
    using TN1 = If<MixedSignedness, MakeUnsigned<TN2>, TN2>;
    using TW2 = MakeWide<MakeWide<TN2>>;

    const Rebind<TN1, DN2> dn1;
    const Repartition<TW2, DN2> dw2;

    const auto vn1_k1 = Set(dn1, TN1{1});
    const auto vn2_k1 = BitCast(dn2, vn1_k1);
    const auto vn1_k4 = Set(dn1, TN1{4});
    const auto vn2_k4 = BitCast(dn2, vn1_k4);

    const auto vw2_k0 = Zero(dw2);
    const auto vw2_k1 = Set(dw2, TW2{1});
    const auto vw2_k4 = Set(dw2, TW2{4});
    const auto vw2_k5 = Set(dw2, TW2{5});
    const auto vw2_k21 = Set(dw2, TW2{21});

    HWY_ASSERT_VEC_EQ(dw2, vw2_k4,
                      SumOfMulQuadAccumulate(dw2, vn1_k1, vn2_k1, vw2_k0));
    HWY_ASSERT_VEC_EQ(dw2, vw2_k5,
                      SumOfMulQuadAccumulate(dw2, vn1_k1, vn2_k1, vw2_k1));
    HWY_ASSERT_VEC_EQ(dw2, vw2_k21,
                      SumOfMulQuadAccumulate(dw2, vn1_k1, vn2_k4, vw2_k5));
    HWY_ASSERT_VEC_EQ(dw2, vw2_k21,
                      SumOfMulQuadAccumulate(dw2, vn1_k4, vn2_k1, vw2_k5));

    constexpr TN1 kTN1ValWithMaxMag =
        static_cast<TN1>(IsSigned<TN1>() ? LimitsMin<TN1>() : LimitsMax<TN1>());
    constexpr TN2 kTN2ValWithMaxMag =
        static_cast<TN2>(IsSigned<TN2>() ? LimitsMin<TN2>() : LimitsMax<TN2>());
    HWY_ASSERT_VEC_EQ(
        dw2,
        Set(dw2, static_cast<TW2>(static_cast<TW2>(kTN1ValWithMaxMag) *
                                  kTN2ValWithMaxMag * TW2{4})),
        SumOfMulQuadAccumulate(dw2, Set(dn1, kTN1ValWithMaxMag),
                               Set(dn2, kTN2ValWithMaxMag), vw2_k0));

    TestConsecutiveSeqMulQuadAccum(dw2, static_cast<TN1>(27),
                                   static_cast<TN2>(34));
    TestConsecutiveSeqMulQuadAccum(dw2, static_cast<TN1>(13),
                                   static_cast<TN2>(-5));
    TestConsecutiveSeqMulQuadAccum(dw2, static_cast<TN1>(-29),
                                   static_cast<TN2>(2));
    TestConsecutiveSeqMulQuadAccum(dw2, static_cast<TN1>(-14),
                                   static_cast<TN2>(-35));
    TestConsecutiveSeqMulQuadAccum(dw2, static_cast<TN1>(LimitsMin<TN1>() + 5),
                                   static_cast<TN2>(LimitsMax<TN2>() - 4));
    TestConsecutiveSeqMulQuadAccum(dw2, static_cast<TN1>(LimitsMax<TN1>() - 4),
                                   static_cast<TN2>(LimitsMin<TN2>() + 11));
  }
};

HWY_NOINLINE void TestAllSumOfMulQuadAccumulate() {
  ForShrinkableVectors<TestSumOfMulQuadAccumulate<false>, 2>()(int8_t());
  ForShrinkableVectors<TestSumOfMulQuadAccumulate<false>, 2>()(uint8_t());
  ForShrinkableVectors<TestSumOfMulQuadAccumulate<true>, 2>()(int8_t());
#if HWY_HAVE_INTEGER64
  ForShrinkableVectors<TestSumOfMulQuadAccumulate<false>, 2>()(int16_t());
  ForShrinkableVectors<TestSumOfMulQuadAccumulate<false>, 2>()(uint16_t());
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
HWY_BEFORE_TEST(HwyWidenMulTest);
HWY_EXPORT_AND_TEST_P(HwyWidenMulTest, TestAllSatWidenMulAccumFixedPoint);
HWY_EXPORT_AND_TEST_P(HwyWidenMulTest, TestAllMulEvenAdd);
HWY_EXPORT_AND_TEST_P(HwyWidenMulTest, TestAllWidenMulAccumulate);
HWY_EXPORT_AND_TEST_P(HwyWidenMulTest, TestAllReorderWidenMulAccumulate);
HWY_EXPORT_AND_TEST_P(HwyWidenMulTest, TestAllRearrangeToOddPlusEven);
HWY_EXPORT_AND_TEST_P(HwyWidenMulTest, TestAllSumOfMulQuadAccumulate);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
