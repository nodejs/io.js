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
#define HWY_TARGET_INCLUDE "tests/mul_pairwise_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

struct TestWidenMulPairwiseAdd {
  // Must be inlined on aarch64 for bf16, else clang crashes.
  template <typename TN, class DN>
  HWY_INLINE void operator()(TN /*unused*/, DN dn) {
    using TW = MakeWide<TN>;
    const RepartitionToWide<DN> dw;
    using VW = Vec<decltype(dw)>;
    using VN = Vec<decltype(dn)>;
    const size_t NN = Lanes(dn);

    const VW f0 = Zero(dw);
    const VW f1 = Set(dw, ConvertScalarTo<TW>(1));
    const VN bf0 = Zero(dn);
    // Cannot Set() bfloat16_t directly.
    const VN bf1 = ReorderDemote2To(dn, f1, f1);

    // Any input zero => both outputs zero
    HWY_ASSERT_VEC_EQ(dw, f0, WidenMulPairwiseAdd(dw, bf0, bf0));
    HWY_ASSERT_VEC_EQ(dw, f0, WidenMulPairwiseAdd(dw, bf0, bf1));
    HWY_ASSERT_VEC_EQ(dw, f0, WidenMulPairwiseAdd(dw, bf1, bf0));

    // delta[p] := p all others zero.
    auto delta_w = AllocateAligned<TW>(NN);
    HWY_ASSERT(delta_w);
    for (size_t p = 0; p < NN; ++p) {
      // Workaround for incorrect Clang wasm codegen: re-initialize the entire
      // array rather than zero-initialize once and then set lane p to p.
      for (size_t i = 0; i < NN; ++i) {
        delta_w[i] = static_cast<TW>((i == p) ? p : 0);
      }
      const VW delta0 = Load(dw, delta_w.get() + 0);
      const VW delta1 = Load(dw, delta_w.get() + NN / 2);
      const VN delta = OrderedDemote2To(dn, delta0, delta1);

      const VW expected = InsertLane(f0, p / 2, static_cast<TW>(p));
      {
        const VW actual = WidenMulPairwiseAdd(dw, delta, bf1);
        HWY_ASSERT_VEC_EQ(dw, expected, actual);
      }
      // Swapped arg order
      {
        const VW actual = WidenMulPairwiseAdd(dw, bf1, delta);
        HWY_ASSERT_VEC_EQ(dw, expected, actual);
      }
    }
  }
};

HWY_NOINLINE void TestAllWidenMulPairwiseAdd() {
  ForShrinkableVectors<TestWidenMulPairwiseAdd>()(bfloat16_t());
  ForShrinkableVectors<TestWidenMulPairwiseAdd>()(int16_t());
  ForShrinkableVectors<TestWidenMulPairwiseAdd>()(uint16_t());
}

struct TestSatWidenMulPairwiseAdd {
  template <typename TN, class DN>
  HWY_NOINLINE void operator()(TN /*unused*/, DN dn) {
    static_assert(IsSame<TN, int8_t>(), "TN should be int8_t");

    using TN_U = MakeUnsigned<TN>;
    using TW = MakeWide<TN>;
    const RepartitionToWide<DN> dw;
    using VW = Vec<decltype(dw)>;
    using VN = Vec<decltype(dn)>;
    const size_t NN = Lanes(dn);
    const size_t NW = Lanes(dw);
    HWY_ASSERT(NN == NW * 2);

    const RebindToUnsigned<decltype(dn)> dn_u;

    const VW f0 = Zero(dw);
    const VN nf0 = Zero(dn);
    const VN nf1 = Set(dn, TN{1});

    // Any input zero => both outputs zero
    HWY_ASSERT_VEC_EQ(dw, f0,
                      SatWidenMulPairwiseAdd(dw, BitCast(dn_u, nf0), nf0));
    HWY_ASSERT_VEC_EQ(dw, f0,
                      SatWidenMulPairwiseAdd(dw, BitCast(dn_u, nf0), nf1));
    HWY_ASSERT_VEC_EQ(dw, f0,
                      SatWidenMulPairwiseAdd(dw, BitCast(dn_u, nf1), nf0));

    // delta[p] := p all others zero.
    auto delta_w = AllocateAligned<TW>(NN);
    HWY_ASSERT(delta_w);

    auto expected = AllocateAligned<TW>(NW);
    HWY_ASSERT(expected);
    Store(f0, dw, expected.get());

    for (size_t p = 0; p < NN; ++p) {
      // Workaround for incorrect Clang wasm codegen: re-initialize the entire
      // array rather than zero-initialize once and then set lane p to p.

      const TN pn = static_cast<TN>(p);
      const TN_U pn_u = static_cast<TN_U>(pn);
      for (size_t i = 0; i < NN; ++i) {
        delta_w[i] = static_cast<TW>((i == p) ? pn : 0);
      }
      const VW delta0 = Load(dw, delta_w.get() + 0);
      const VW delta1 = Load(dw, delta_w.get() + NN / 2);
      const VN delta = OrderedDemote2To(dn, delta0, delta1);

      expected[p / 2] = static_cast<TW>(pn_u);
      const VW actual_1 = SatWidenMulPairwiseAdd(dw, BitCast(dn_u, delta), nf1);
      HWY_ASSERT_VEC_EQ(dw, expected.get(), actual_1);

      // Swapped arg order
      expected[p / 2] = static_cast<TW>(pn);
      const VW actual_2 = SatWidenMulPairwiseAdd(dw, BitCast(dn_u, nf1), delta);
      HWY_ASSERT_VEC_EQ(dw, expected.get(), actual_2);

      expected[p / 2] = TW{0};
    }

    const auto vn_signed_min = Set(dn, LimitsMin<TN>());
    const auto vn_signed_max = Set(dn, LimitsMax<TN>());
    const auto vn_unsigned_max = Set(dn_u, LimitsMax<TN_U>());
    const auto vw_signed_min = Set(dw, LimitsMin<TW>());
    const auto vw_signed_max = Set(dw, LimitsMax<TW>());
    const auto vw_neg_tn_unsigned_max =
        Set(dw, static_cast<TW>(-static_cast<TW>(LimitsMax<TN_U>())));

    HWY_ASSERT_VEC_EQ(
        dw, vw_signed_max,
        SatWidenMulPairwiseAdd(dw, vn_unsigned_max, vn_signed_max));
    HWY_ASSERT_VEC_EQ(
        dw, vw_signed_min,
        SatWidenMulPairwiseAdd(dw, vn_unsigned_max, vn_signed_min));
    HWY_ASSERT_VEC_EQ(dw, vw_neg_tn_unsigned_max,
                      SatWidenMulPairwiseAdd(
                          dw, vn_unsigned_max,
                          InterleaveLower(dn, vn_signed_max, vn_signed_min)));
    HWY_ASSERT_VEC_EQ(dw, vw_neg_tn_unsigned_max,
                      SatWidenMulPairwiseAdd(
                          dw, vn_unsigned_max,
                          InterleaveLower(dn, vn_signed_min, vn_signed_max)));

    constexpr TN kSignedMax = LimitsMax<TN>();
    constexpr TN kZeroIotaRepl = static_cast<TN>(LimitsMax<TN>() - 16);

    auto in_a = AllocateAligned<TN>(NN);
    auto in_b = AllocateAligned<TN>(NN);
    auto in_neg_b = AllocateAligned<TN>(NN);
    HWY_ASSERT(in_a && in_b && in_neg_b);

    for (size_t i = 0; i < NN; i++) {
      const auto val = ((i + 1) & kSignedMax);
      const auto a_val = static_cast<TN>((val != 0) ? val : kZeroIotaRepl);
      const auto b_val = static_cast<TN>((a_val & 63) + 20);
      in_a[i] = a_val;
      in_b[i] = static_cast<TN>(b_val);
      in_neg_b[i] = static_cast<TN>(-b_val);
    }

    for (size_t i = 0; i < NW; i++) {
      const TW a0 = static_cast<TW>(in_a[2 * i]);
      const TW a1 = static_cast<TW>(in_a[2 * i + 1]);
      expected[i] = static_cast<TW>(a0 * a0 + a1 * a1);
    }

    auto vn_a = Load(dn, in_a.get());
    HWY_ASSERT_VEC_EQ(dw, expected.get(),
                      SatWidenMulPairwiseAdd(dw, BitCast(dn_u, vn_a), vn_a));

    for (size_t i = 0; i < NW; i++) {
      expected[i] = static_cast<TW>(-expected[i]);
    }

    HWY_ASSERT_VEC_EQ(
        dw, expected.get(),
        SatWidenMulPairwiseAdd(dw, BitCast(dn_u, vn_a), Neg(vn_a)));

    auto vn_b = Load(dn, in_b.get());

    HWY_ASSERT_VEC_EQ(
        dw, vw_signed_max,
        SatWidenMulPairwiseAdd(
            dw, InterleaveLower(dn_u, BitCast(dn_u, vn_b), vn_unsigned_max),
            InterleaveLower(dn, vn_b, vn_signed_max)));
    HWY_ASSERT_VEC_EQ(
        dw, vw_signed_max,
        SatWidenMulPairwiseAdd(
            dw, InterleaveUpper(dn_u, BitCast(dn_u, vn_b), vn_unsigned_max),
            InterleaveUpper(dn, vn_b, vn_signed_max)));
    HWY_ASSERT_VEC_EQ(
        dw, vw_signed_max,
        SatWidenMulPairwiseAdd(
            dw, InterleaveLower(dn_u, vn_unsigned_max, BitCast(dn_u, vn_b)),
            InterleaveLower(dn, vn_signed_max, vn_b)));
    HWY_ASSERT_VEC_EQ(
        dw, vw_signed_max,
        SatWidenMulPairwiseAdd(
            dw, InterleaveUpper(dn_u, vn_unsigned_max, BitCast(dn_u, vn_b)),
            InterleaveUpper(dn, vn_signed_max, vn_b)));

    const auto vn_neg_b = Load(dn, in_neg_b.get());
    HWY_ASSERT_VEC_EQ(
        dw, vw_signed_min,
        SatWidenMulPairwiseAdd(
            dw, InterleaveLower(dn_u, BitCast(dn_u, vn_b), vn_unsigned_max),
            InterleaveLower(dn, vn_neg_b, vn_signed_min)));
    HWY_ASSERT_VEC_EQ(
        dw, vw_signed_min,
        SatWidenMulPairwiseAdd(
            dw, InterleaveUpper(dn_u, BitCast(dn_u, vn_b), vn_unsigned_max),
            InterleaveUpper(dn, vn_neg_b, vn_signed_min)));
    HWY_ASSERT_VEC_EQ(
        dw, vw_signed_min,
        SatWidenMulPairwiseAdd(
            dw, InterleaveLower(dn_u, vn_unsigned_max, BitCast(dn_u, vn_b)),
            InterleaveLower(dn, vn_signed_min, vn_neg_b)));
    HWY_ASSERT_VEC_EQ(
        dw, vw_signed_min,
        SatWidenMulPairwiseAdd(
            dw, InterleaveUpper(dn_u, vn_unsigned_max, BitCast(dn_u, vn_b)),
            InterleaveUpper(dn, vn_signed_min, vn_neg_b)));

    constexpr size_t kMaxLanesPerNBlock = 16 / sizeof(TN);
    constexpr size_t kMaxLanesPerWBlock = 16 / sizeof(TW);

    for (size_t i = 0; i < NW; i++) {
      const size_t blk_idx = i / kMaxLanesPerWBlock;
      const TW b = static_cast<TW>(
          in_b[blk_idx * kMaxLanesPerNBlock + (i & (kMaxLanesPerWBlock - 1))]);
      expected[i] =
          static_cast<TW>(b * b + static_cast<TW>(LimitsMax<TN_U>()) *
                                      static_cast<TW>(LimitsMin<TN>()));
    }
    HWY_ASSERT_VEC_EQ(
        dw, expected.get(),
        SatWidenMulPairwiseAdd(
            dw, InterleaveLower(dn_u, vn_unsigned_max, BitCast(dn_u, vn_b)),
            InterleaveLower(dn, vn_signed_min, vn_b)));
    HWY_ASSERT_VEC_EQ(
        dw, expected.get(),
        SatWidenMulPairwiseAdd(
            dw, InterleaveLower(dn_u, BitCast(dn_u, vn_b), vn_unsigned_max),
            InterleaveLower(dn, vn_b, vn_signed_min)));
  }
};

HWY_NOINLINE void TestAllSatWidenMulPairwiseAdd() {
  ForShrinkableVectors<TestSatWidenMulPairwiseAdd>()(int8_t());
}

struct TestSatWidenMulPairwiseAccumulate {
  template <class TN, class DN>
  HWY_INLINE void operator()(TN /*unused*/, DN dn) {
    static_assert(IsSigned<TN>() && !IsFloat<TN>() && !IsSpecialFloat<TN>(),
                  "TN must be a signed integer type");

    using TW = MakeWide<TN>;
    const RepartitionToWide<DN> dw;
    using VW = Vec<decltype(dw)>;
    using VN = Vec<decltype(dn)>;
    const size_t NN = Lanes(dn);
    const size_t NW = Lanes(dw);
    HWY_ASSERT(NN == NW * 2);

    const VN vn_min = Set(dn, LimitsMin<TN>());
    const VN vn_kneg1 = Set(dn, static_cast<TN>(-1));
    const VN vn_k1 = Set(dn, static_cast<TN>(1));
    const VN vn_max = Set(dn, LimitsMax<TN>());

    const VW vw_min = Set(dw, LimitsMin<TW>());
    const VW vw_kneg7 = Set(dw, static_cast<TW>(-7));
    const VW vw_kneg1 = Set(dw, static_cast<TW>(-1));
    const VW vw_k0 = Zero(dw);
    const VW vw_k1 = Set(dw, static_cast<TW>(1));
    const VW vw_k5 = Set(dw, static_cast<TW>(5));
    const VW vw_max = Set(dw, LimitsMax<TW>());

    HWY_ASSERT_VEC_EQ(
        dw, vw_min, SatWidenMulPairwiseAccumulate(dw, vn_max, vn_min, vw_min));
    HWY_ASSERT_VEC_EQ(
        dw, vw_max, SatWidenMulPairwiseAccumulate(dw, vn_max, vn_max, vw_max));
    HWY_ASSERT_VEC_EQ(
        dw, vw_max,
        SatWidenMulPairwiseAccumulate(dw, vn_min, vn_min, vw_kneg1));

    const VN vn_p = PositiveIota(dn);
    const VN vn_n = Neg(vn_p);

    const VW vw_sum2_p = SumsOf2(vn_p);
    const VW vw_sum2_n = SumsOf2(vn_n);

    const VW vw_p2 = Add(MulEven(vn_p, vn_p), MulOdd(vn_p, vn_p));
    const VW vw_n2 = Add(MulEven(vn_p, vn_n), MulOdd(vn_p, vn_n));

    HWY_ASSERT_VEC_EQ(dw, vw_p2,
                      SatWidenMulPairwiseAccumulate(dw, vn_p, vn_p, vw_k0));
    HWY_ASSERT_VEC_EQ(dw, vw_n2,
                      SatWidenMulPairwiseAccumulate(dw, vn_p, vn_n, vw_k0));
    HWY_ASSERT_VEC_EQ(dw, Add(vw_p2, vw_k1),
                      SatWidenMulPairwiseAccumulate(dw, vn_p, vn_p, vw_k1));
    HWY_ASSERT_VEC_EQ(dw, Add(vw_n2, vw_k1),
                      SatWidenMulPairwiseAccumulate(dw, vn_n, vn_p, vw_k1));
    HWY_ASSERT_VEC_EQ(dw, Add(vw_sum2_p, vw_k5),
                      SatWidenMulPairwiseAccumulate(dw, vn_p, vn_k1, vw_k5));
    HWY_ASSERT_VEC_EQ(dw, Add(vw_sum2_n, vw_kneg7),
                      SatWidenMulPairwiseAccumulate(dw, vn_n, vn_k1, vw_kneg7));
    HWY_ASSERT_VEC_EQ(
        dw, Add(vw_sum2_p, vw_kneg7),
        SatWidenMulPairwiseAccumulate(dw, vn_kneg1, vn_n, vw_kneg7));
    HWY_ASSERT_VEC_EQ(dw, Add(vw_sum2_n, vw_kneg7),
                      SatWidenMulPairwiseAccumulate(dw, vn_k1, vn_n, vw_kneg7));
  }
};

HWY_NOINLINE void TestAllSatWidenMulPairwiseAccumulate() {
  ForShrinkableVectors<TestSatWidenMulPairwiseAccumulate>()(int16_t());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyMulPairwiseTest);
HWY_EXPORT_AND_TEST_P(HwyMulPairwiseTest, TestAllWidenMulPairwiseAdd);
HWY_EXPORT_AND_TEST_P(HwyMulPairwiseTest, TestAllSatWidenMulPairwiseAdd);
HWY_EXPORT_AND_TEST_P(HwyMulPairwiseTest, TestAllSatWidenMulPairwiseAccumulate);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
