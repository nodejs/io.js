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

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tests/mask_convert_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

template <class TTo>
struct TestPromoteMaskTo {
  using TTo_I = MakeSigned<TTo>;

  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    using TI = MakeSigned<T>;

    const Rebind<TTo, decltype(d)> d_to;
    const RebindToSigned<decltype(d)> di;
    const RebindToSigned<decltype(d_to)> di_to;

    const size_t N = Lanes(di);
    auto bool_lanes = AllocateAligned<TI>(N);
    auto expected = AllocateAligned<TTo_I>(N);
    HWY_ASSERT(bool_lanes && expected);

    ZeroBytes(bool_lanes.get(), N * sizeof(TI));

    // For all combinations of zero/nonzero state of subset of lanes:
    const size_t max_lanes = AdjustedLog2Reps(HWY_MIN(N, size_t(6)));
    for (size_t code = 0; code < (1ull << max_lanes); ++code) {
      for (size_t i = 0; i < max_lanes; ++i) {
        bool_lanes[i] = (code & (1ull << i)) ? TI(1) : TI(0);
      }

      for (size_t i = 0; i < N; ++i) {
        expected[i] = static_cast<TTo_I>(-static_cast<TI>(bool_lanes[i]));
      }

      const auto m = RebindMask(d, Gt(Load(di, bool_lanes.get()), Zero(di)));
      const auto promoted_mask = PromoteMaskTo(d_to, d, m);

      const auto expected_mask =
          RebindMask(d_to, MaskFromVec(Load(di_to, expected.get())));

      HWY_ASSERT_VEC_EQ(di_to, expected.get(),
                        BitCast(di_to, VecFromMask(d_to, promoted_mask)));
      HWY_ASSERT_MASK_EQ(d_to, expected_mask, promoted_mask);
    }
  }
};

HWY_NOINLINE void TestAllPromoteMaskTo() {
  const ForPromoteVectors<TestPromoteMaskTo<int16_t>, 1> to_i16div2;
  to_i16div2(int8_t());
  to_i16div2(uint8_t());

  const ForPromoteVectors<TestPromoteMaskTo<uint16_t>, 1> to_u16div2;
  to_u16div2(int8_t());
  to_u16div2(uint8_t());

  const ForPromoteVectors<TestPromoteMaskTo<int32_t>, 1> to_i32div2;
  to_i32div2(int16_t());
  to_i32div2(uint16_t());
#if HWY_HAVE_FLOAT16
  to_i32div2(float16_t());
#endif

  const ForPromoteVectors<TestPromoteMaskTo<int32_t>, 1> to_u32div2;
  to_u32div2(int16_t());
  to_u32div2(uint16_t());
#if HWY_HAVE_FLOAT16
  to_u32div2(float16_t());
#endif

  const ForPromoteVectors<TestPromoteMaskTo<int32_t>, 2> to_i32div4;
  to_i32div4(int8_t());

#if HWY_HAVE_INTEGER64
  const ForPromoteVectors<TestPromoteMaskTo<int64_t>, 1> to_i64div2;
  to_i64div2(int32_t());
  to_i64div2(uint32_t());
  to_i64div2(float());

  const ForPromoteVectors<TestPromoteMaskTo<uint64_t>, 1> to_u64div2;
  to_u64div2(int32_t());
  to_u64div2(uint32_t());
  to_u64div2(float());

  const ForPromoteVectors<TestPromoteMaskTo<int64_t>, 2> to_i64div4;
  to_i64div4(int16_t());

  const ForPromoteVectors<TestPromoteMaskTo<int64_t>, 3> to_i64div8;
  to_i64div8(int8_t());
#endif

#if HWY_HAVE_FLOAT64
  const ForPromoteVectors<TestPromoteMaskTo<double>, 1> to_f64div2;
  to_f64div2(int32_t());
  to_f64div2(uint32_t());
  to_f64div2(float());

#if HWY_HAVE_FLOAT16
  const ForPromoteVectors<TestPromoteMaskTo<double>, 2> to_f64div4;
  to_f64div4(float16_t());
#endif  // HWY_HAVE_FLOAT16
#endif  // HWY_HAVE_FLOAT64
}

template <class TTo>
struct TestDemoteMaskTo {
  using TTo_I = MakeSigned<TTo>;

  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    using TI = MakeSigned<T>;

    const Rebind<TTo, decltype(d)> d_to;
    const RebindToSigned<decltype(d)> di;
    const RebindToSigned<decltype(d_to)> di_to;

    const size_t N = Lanes(di);
    auto bool_lanes = AllocateAligned<TI>(N);
    auto expected = AllocateAligned<TTo_I>(N);
    HWY_ASSERT(bool_lanes && expected);

    ZeroBytes(bool_lanes.get(), N * sizeof(TI));

    // For all combinations of zero/nonzero state of subset of lanes:
    const size_t max_lanes = AdjustedLog2Reps(HWY_MIN(N, size_t(6)));
    for (size_t code = 0; code < (1ull << max_lanes); ++code) {
      for (size_t i = 0; i < max_lanes; ++i) {
        bool_lanes[i] = (code & (1ull << i)) ? TI(1) : TI(0);
      }

      for (size_t i = 0; i < N; ++i) {
        expected[i] = static_cast<TTo_I>(-static_cast<TI>(bool_lanes[i]));
      }

      const auto m = RebindMask(d, Gt(Load(di, bool_lanes.get()), Zero(di)));
      const auto demoted_mask = DemoteMaskTo(d_to, d, m);

      const auto expected_mask =
          RebindMask(d_to, MaskFromVec(Load(di_to, expected.get())));

      HWY_ASSERT_VEC_EQ(di_to, expected.get(),
                        BitCast(di_to, VecFromMask(d_to, demoted_mask)));
      HWY_ASSERT_MASK_EQ(d_to, expected_mask, demoted_mask);
    }
  }
};

HWY_NOINLINE void TestAllDemoteMaskTo() {
  const ForDemoteVectors<TestDemoteMaskTo<int8_t>> from_uif16_to_i8;
  from_uif16_to_i8(int16_t());
  from_uif16_to_i8(uint16_t());
#if HWY_HAVE_FLOAT16
  from_uif16_to_i8(float16_t());
#endif

  const ForDemoteVectors<TestDemoteMaskTo<uint8_t>> from_uif16_to_u8;
  from_uif16_to_u8(int16_t());
  from_uif16_to_u8(uint16_t());
#if HWY_HAVE_FLOAT16
  from_uif16_to_u8(float16_t());
#endif

  const ForDemoteVectors<TestDemoteMaskTo<int16_t>> from_uif32_to_i16;
  from_uif32_to_i16(int32_t());
  from_uif32_to_i16(uint32_t());
#if HWY_HAVE_FLOAT16
  from_uif32_to_i16(float());
#endif

  const ForDemoteVectors<TestDemoteMaskTo<uint16_t>> from_uif32_to_u16;
  from_uif32_to_u16(int32_t());
  from_uif32_to_u16(uint32_t());
  from_uif32_to_u16(float());

#if HWY_HAVE_FLOAT16
  const ForDemoteVectors<TestDemoteMaskTo<float16_t>> from_uif32_to_f16;
  from_uif32_to_f16(int32_t());
  from_uif32_to_f16(uint32_t());
  from_uif32_to_f16(float());
#endif

  const ForDemoteVectors<TestDemoteMaskTo<int8_t>, 2> from_i32_to_i8;
  from_i32_to_i8(int32_t());

#if HWY_HAVE_INTEGER64
  const ForDemoteVectors<TestDemoteMaskTo<int32_t>> from_uif64_to_i32;
  from_uif64_to_i32(int64_t());
  from_uif64_to_i32(uint64_t());
#if HWY_HAVE_FLOAT64
  from_uif64_to_i32(double());
#endif

  const ForDemoteVectors<TestDemoteMaskTo<uint32_t>> from_uif64_to_u32;
  from_uif64_to_u32(int64_t());
  from_uif64_to_u32(uint64_t());
#if HWY_HAVE_FLOAT64
  from_uif64_to_u32(double());
#endif

  const ForDemoteVectors<TestDemoteMaskTo<float>> from_uif64_to_f32;
  from_uif64_to_f32(int64_t());
  from_uif64_to_f32(uint64_t());
#if HWY_HAVE_FLOAT64
  from_uif64_to_f32(double());
#endif

  const ForDemoteVectors<TestDemoteMaskTo<int16_t>, 2> from_i64_to_i16;
  from_i64_to_i16(int64_t());

#if HWY_HAVE_FLOAT64 && HWY_HAVE_FLOAT16
  const ForDemoteVectors<TestDemoteMaskTo<float16_t>, 2> from_f64_to_f16;
  from_f64_to_f16(double());
#endif

  const ForDemoteVectors<TestDemoteMaskTo<int8_t>, 3> from_i64_to_i8;
  from_i64_to_i8(int64_t());
#endif
}

struct TestOrderedDemote2MasksTo {
#if HWY_TARGET != HWY_SCALAR
  template <class DTo, class D>
  static HWY_NOINLINE void DoTestOrderedDemote2Masks(DTo d_to, D d) {
    using T = TFromD<D>;
    using TTo = TFromD<DTo>;
    using TI = MakeSigned<T>;
    using TTo_I = MakeSigned<TTo>;

    const RebindToSigned<decltype(d)> di;
    const RebindToSigned<decltype(d_to)> di_to;

    const size_t N = Lanes(di);
    auto bool_lanes = AllocateAligned<TI>(N * 2);
    auto expected = AllocateAligned<TTo_I>(N * 2);
    HWY_ASSERT(bool_lanes && expected);

    ZeroBytes(bool_lanes.get(), N * 2 * sizeof(TI));
    ZeroBytes(expected.get(), N * 2 * sizeof(TTo_I));

    // For all combinations of zero/nonzero state of subset of lanes:
    const size_t max_lanes = AdjustedLog2Reps(HWY_MIN(N * 2, size_t(6)));
    for (size_t code = 0; code < (1ull << max_lanes); ++code) {
      for (size_t i = 0; i < max_lanes; ++i) {
        bool_lanes[i] = (code & (1ull << i)) ? TI(1) : TI(0);
      }

      const size_t idx2 = N + (code & (N - 1));
      bool_lanes[idx2] = TI(1);

      for (size_t i = 0; i < N * 2; ++i) {
        expected[i] = static_cast<TTo_I>(-static_cast<TI>(bool_lanes[i]));
      }

      const auto m0 = RebindMask(d, Gt(Load(di, bool_lanes.get()), Zero(di)));
      const auto m1 =
          RebindMask(d, Gt(Load(di, bool_lanes.get() + N), Zero(di)));
      const auto expected_mask =
          RebindMask(d_to, MaskFromVec(Load(di_to, expected.get())));

      HWY_ASSERT_MASK_EQ(d_to, expected_mask,
                         OrderedDemote2MasksTo(d_to, d, m0, m1));

      bool_lanes[idx2] = TI(0);
    }

    HWY_ASSERT_MASK_EQ(
        d_to, FirstN(d_to, N - 1),
        OrderedDemote2MasksTo(d_to, d, FirstN(d, N - 1), FirstN(d, 0)));
    HWY_ASSERT_MASK_EQ(
        d_to, FirstN(d_to, N),
        OrderedDemote2MasksTo(d_to, d, FirstN(d, N), FirstN(d, 0)));
    HWY_ASSERT_MASK_EQ(
        d_to, FirstN(d_to, 2 * N - 1),
        OrderedDemote2MasksTo(d_to, d, FirstN(d, N), FirstN(d, N - 1)));
    HWY_ASSERT_MASK_EQ(
        d_to, FirstN(d_to, 2 * N),
        OrderedDemote2MasksTo(d_to, d, FirstN(d, N), FirstN(d, N)));
  }

  template <class D, HWY_IF_T_SIZE_ONE_OF_D(
                         D, (HWY_HAVE_FLOAT16 ? (1 << 4) : 0) | (1 << 8))>
  static HWY_INLINE void DoTestOrderedDemote2MasksToFloat(D d) {
    using TF = MakeFloat<MakeNarrow<MakeUnsigned<TFromD<D>>>>;
    DoTestOrderedDemote2Masks(Repartition<TF, decltype(d)>(), d);
  }

  template <class D,
            HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2) |
                                          (HWY_HAVE_FLOAT16 ? 0 : (1 << 4)))>
  static HWY_INLINE void DoTestOrderedDemote2MasksToFloat(D /*d*/) {}
#endif  // HWY_TARGET != HWY_SCALAR

  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
#if HWY_TARGET != HWY_SCALAR
    const RebindToSigned<decltype(d)> di;
    const RebindToUnsigned<decltype(d)> du;

    DoTestOrderedDemote2Masks(RepartitionToNarrow<decltype(di)>(), d);
    DoTestOrderedDemote2Masks(RepartitionToNarrow<decltype(du)>(), d);
    DoTestOrderedDemote2MasksToFloat(d);
#else
    (void)d;
#endif
  }
};

HWY_NOINLINE void TestAllOrderedDemote2MasksTo() {
  ForUIF163264(ForShrinkableVectors<TestOrderedDemote2MasksTo>());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyMaskConvertTest);
HWY_EXPORT_AND_TEST_P(HwyMaskConvertTest, TestAllPromoteMaskTo);
HWY_EXPORT_AND_TEST_P(HwyMaskConvertTest, TestAllDemoteMaskTo);
HWY_EXPORT_AND_TEST_P(HwyMaskConvertTest, TestAllOrderedDemote2MasksTo);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
