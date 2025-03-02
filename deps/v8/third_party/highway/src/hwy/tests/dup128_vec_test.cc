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

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tests/dup128_vec_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

struct TestDup128VecFromValues {
  template <class D, HWY_IF_T_SIZE_D(D, 1)>
  static HWY_INLINE Vec<D> VecFromValues(
      D d, TFromD<D> t0, TFromD<D> t1, TFromD<D> t2, TFromD<D> t3, TFromD<D> t4,
      TFromD<D> t5, TFromD<D> t6, TFromD<D> t7, TFromD<D> t8, TFromD<D> t9,
      TFromD<D> t10, TFromD<D> t11, TFromD<D> t12, TFromD<D> t13, TFromD<D> t14,
      TFromD<D> t15) {
    return Dup128VecFromValues(d, t0, t1, t2, t3, t4, t5, t6, t7, t8, t9, t10,
                               t11, t12, t13, t14, t15);
  }
  template <class D, HWY_IF_T_SIZE_D(D, 2)>
  static HWY_INLINE Vec<D> VecFromValues(
      D d, TFromD<D> t0, TFromD<D> t1, TFromD<D> t2, TFromD<D> t3, TFromD<D> t4,
      TFromD<D> t5, TFromD<D> t6, TFromD<D> t7, TFromD<D> /*t8*/,
      TFromD<D> /*t9*/, TFromD<D> /*t10*/, TFromD<D> /*t11*/, TFromD<D> /*t12*/,
      TFromD<D> /*t13*/, TFromD<D> /*t14*/, TFromD<D> /*t15*/) {
    return Dup128VecFromValues(d, t0, t1, t2, t3, t4, t5, t6, t7);
  }
  template <class D, HWY_IF_T_SIZE_D(D, 4)>
  static HWY_INLINE Vec<D> VecFromValues(D d, TFromD<D> t0, TFromD<D> t1,
                                         TFromD<D> t2, TFromD<D> t3,
                                         TFromD<D> /*t4*/, TFromD<D> /*t5*/,
                                         TFromD<D> /*t6*/, TFromD<D> /*t7*/,
                                         TFromD<D> /*t8*/, TFromD<D> /*t9*/,
                                         TFromD<D> /*t10*/, TFromD<D> /*t11*/,
                                         TFromD<D> /*t12*/, TFromD<D> /*t13*/,
                                         TFromD<D> /*t14*/, TFromD<D> /*t15*/) {
    return Dup128VecFromValues(d, t0, t1, t2, t3);
  }
  template <class D, HWY_IF_T_SIZE_D(D, 8)>
  static HWY_INLINE Vec<D> VecFromValues(D d, TFromD<D> t0, TFromD<D> t1,
                                         TFromD<D> /*t2*/, TFromD<D> /*t3*/,
                                         TFromD<D> /*t4*/, TFromD<D> /*t5*/,
                                         TFromD<D> /*t6*/, TFromD<D> /*t7*/,
                                         TFromD<D> /*t8*/, TFromD<D> /*t9*/,
                                         TFromD<D> /*t10*/, TFromD<D> /*t11*/,
                                         TFromD<D> /*t12*/, TFromD<D> /*t13*/,
                                         TFromD<D> /*t14*/, TFromD<D> /*t15*/) {
    return Dup128VecFromValues(d, t0, t1);
  }

  template <class D, class T, HWY_IF_NOT_SPECIAL_FLOAT_D(D)>
  static HWY_INLINE TFromD<D> CastValueToLaneType(D /*d*/, T val) {
    return static_cast<TFromD<D>>(val);
  }

  template <class D, class T, HWY_IF_BF16_D(D)>
  static HWY_INLINE hwy::bfloat16_t CastValueToLaneType(D /*d*/, T val) {
    return BF16FromF32(static_cast<float>(val));
  }

  template <class D, class T, HWY_IF_F16_D(D)>
  static HWY_INLINE hwy::float16_t CastValueToLaneType(D /*d*/, T val) {
    return F16FromF32(static_cast<float>(val));
  }

  template <class D, typename T2, HWY_IF_NOT_SPECIAL_FLOAT_D(D)>
  static HWY_INLINE Vec<D> BlockwiseIota(D d, T2 start) {
    return BroadcastBlock<0>(Iota(d, static_cast<TFromD<D>>(start)));
  }

  template <class D, typename T2, HWY_IF_BF16_D(D)>
  static HWY_INLINE Vec<D> BlockwiseIota(D d, T2 start) {
#if HWY_TARGET == HWY_SCALAR
    return Set(d, BF16FromF32(static_cast<float>(start)));
#else  // HWY_TARGET != HWY_SCALAR
#if HWY_MAX_BYTES >= 32 &&                              \
    (HWY_TARGET == HWY_RVV || HWY_TARGET <= HWY_AVX2 || \
     HWY_TARGET == HWY_WASM_EMU256 || HWY_TARGET == HWY_SVE_256)

#if HWY_TARGET == HWY_RVV
    const ScalableTag<float, 1> df32;
#else
    const FixedTag<float, 8> df32;
#endif
    const Rebind<hwy::bfloat16_t, decltype(df32)> dbf16;

    const auto vbf16_iota = DemoteTo(dbf16, Iota(df32, start));
#else
    const FixedTag<float, 4> df32;
    const Repartition<hwy::bfloat16_t, decltype(df32)> dbf16;

    const auto vbf16_iota = OrderedDemote2To(
        dbf16, Iota(df32, start), Iota(df32, static_cast<float>(start) + 4.0f));
#endif

    return BroadcastBlock<0>(ResizeBitCast(d, vbf16_iota));
#endif  // HWY_TARGET == HWY_SCALAR
  }

  template <class D, typename T2, HWY_IF_F16_D(D)>
  static HWY_INLINE Vec<D> BlockwiseIota(D d, T2 start) {
#if HWY_HAVE_FLOAT16
    return BroadcastBlock<0>(Iota(d, start));
#elif HWY_TARGET == HWY_SCALAR
    return Set(d, F16FromF32(static_cast<float>(start)));
#else  // !HWY_HAVE_FLOAT16 && HWY_TARGET != HWY_SCALAR
#if HWY_MAX_BYTES >= 32 &&                              \
    (HWY_TARGET == HWY_RVV || HWY_TARGET <= HWY_AVX2 || \
     HWY_TARGET == HWY_WASM_EMU256 || HWY_TARGET == HWY_SVE_256)

#if HWY_TARGET == HWY_RVV
    const ScalableTag<float, 1> df32;
#else
    const FixedTag<float, 8> df32;
#endif
    const Rebind<hwy::float16_t, decltype(df32)> df16;

    const auto vf16_iota = DemoteTo(df16, Iota(df32, start));
#else
    const FixedTag<float, 4> df32;
    const Repartition<hwy::float16_t, decltype(df32)> df16;
    const Half<decltype(df16)> dh_f16;

    const auto vf16_iota = Combine(
        df16, DemoteTo(dh_f16, Iota(df32, static_cast<float>(start) + 4.0f)),
        DemoteTo(dh_f16, Iota(df32, start)));
#endif

    return BroadcastBlock<0>(ResizeBitCast(d, vf16_iota));
#endif  // HWY_HAVE_FLOAT16
  }

  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    HWY_ASSERT_VEC_EQ(
        d, Zero(d),
        VecFromValues(d, CastValueToLaneType(d, 0), CastValueToLaneType(d, 0),
                      CastValueToLaneType(d, 0), CastValueToLaneType(d, 0),
                      CastValueToLaneType(d, 0), CastValueToLaneType(d, 0),
                      CastValueToLaneType(d, 0), CastValueToLaneType(d, 0),
                      CastValueToLaneType(d, 0), CastValueToLaneType(d, 0),
                      CastValueToLaneType(d, 0), CastValueToLaneType(d, 0),
                      CastValueToLaneType(d, 0), CastValueToLaneType(d, 0),
                      CastValueToLaneType(d, 0), CastValueToLaneType(d, 0)));
    HWY_ASSERT_VEC_EQ(
        d, Set(d, CastValueToLaneType(d, 1)),
        VecFromValues(d, CastValueToLaneType(d, 1), CastValueToLaneType(d, 1),
                      CastValueToLaneType(d, 1), CastValueToLaneType(d, 1),
                      CastValueToLaneType(d, 1), CastValueToLaneType(d, 1),
                      CastValueToLaneType(d, 1), CastValueToLaneType(d, 1),
                      CastValueToLaneType(d, 1), CastValueToLaneType(d, 1),
                      CastValueToLaneType(d, 1), CastValueToLaneType(d, 1),
                      CastValueToLaneType(d, 1), CastValueToLaneType(d, 1),
                      CastValueToLaneType(d, 1), CastValueToLaneType(d, 1)));
    HWY_ASSERT_VEC_EQ(
        d, BlockwiseIota(d, 1),
        VecFromValues(d, CastValueToLaneType(d, 1), CastValueToLaneType(d, 2),
                      CastValueToLaneType(d, 3), CastValueToLaneType(d, 4),
                      CastValueToLaneType(d, 5), CastValueToLaneType(d, 6),
                      CastValueToLaneType(d, 7), CastValueToLaneType(d, 8),
                      CastValueToLaneType(d, 9), CastValueToLaneType(d, 10),
                      CastValueToLaneType(d, 11), CastValueToLaneType(d, 12),
                      CastValueToLaneType(d, 13), CastValueToLaneType(d, 14),
                      CastValueToLaneType(d, 15), CastValueToLaneType(d, 16)));
    HWY_ASSERT_VEC_EQ(
        d, BlockwiseIota(d, -16),
        VecFromValues(d, CastValueToLaneType(d, -16),
                      CastValueToLaneType(d, -15), CastValueToLaneType(d, -14),
                      CastValueToLaneType(d, -13), CastValueToLaneType(d, -12),
                      CastValueToLaneType(d, -11), CastValueToLaneType(d, -10),
                      CastValueToLaneType(d, -9), CastValueToLaneType(d, -8),
                      CastValueToLaneType(d, -7), CastValueToLaneType(d, -6),
                      CastValueToLaneType(d, -5), CastValueToLaneType(d, -4),
                      CastValueToLaneType(d, -3), CastValueToLaneType(d, -2),
                      CastValueToLaneType(d, -1)));

    RandomState rng;
    auto rand_vals = AllocateAligned<T>(16);
    HWY_ASSERT(rand_vals);

    for (size_t rep = 0; rep < AdjustedReps(200); ++rep) {
      for (size_t i = 0; i < 16; ++i) {
        rand_vals[i] = RandomFiniteValue<T>(&rng);
      }

      const auto expected = LoadDup128(d, rand_vals.get());
      const auto actual = VecFromValues(
          d, rand_vals[0], rand_vals[1], rand_vals[2], rand_vals[3],
          rand_vals[4], rand_vals[5], rand_vals[6], rand_vals[7], rand_vals[8],
          rand_vals[9], rand_vals[10], rand_vals[11], rand_vals[12],
          rand_vals[13], rand_vals[14], rand_vals[15]);
      HWY_ASSERT_VEC_EQ(d, expected, actual);
    }
  }
};

HWY_NOINLINE void TestAllDup128VecFromValues() {
  const ForPartialVectors<TestDup128VecFromValues> func;
  ForIntegerTypes(func);
  func(hwy::float16_t());
  func(hwy::bfloat16_t());
  ForFloat3264Types(func);
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyDup128VecTest);
HWY_EXPORT_AND_TEST_P(HwyDup128VecTest, TestAllDup128VecFromValues);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
