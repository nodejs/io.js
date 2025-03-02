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

#include <cmath>  // std::isfinite

#include "hwy/base.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tests/convert_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/nanobenchmark.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

template <typename T, size_t N, int kPow2>
size_t DeduceN(Simd<T, N, kPow2>) {
  return N;
}

template <typename ToT>
struct TestRebind {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const Rebind<ToT, D> dto;
    const size_t N = Lanes(d);
    HWY_ASSERT(N <= MaxLanes(d));
    const size_t NTo = Lanes(dto);
    if (NTo != N) {
      HWY_ABORT("u%zu -> u%zu: lanes %zu %zu pow2 %d %d cap %zu %zu\n",
                8 * sizeof(T), 8 * sizeof(ToT), N, NTo, d.Pow2(), dto.Pow2(),
                DeduceN(d), DeduceN(dto));
    }
  }
};

// Lane count remains the same when we rebind to smaller/equal/larger types.
HWY_NOINLINE void TestAllRebind() {
#if HWY_HAVE_INTEGER64
  ForShrinkableVectors<TestRebind<uint8_t>, 3>()(uint64_t());
#endif  // HWY_HAVE_INTEGER64
  ForShrinkableVectors<TestRebind<uint8_t>, 2>()(uint32_t());
  ForShrinkableVectors<TestRebind<uint8_t>, 1>()(uint16_t());
  ForPartialVectors<TestRebind<uint8_t>>()(uint8_t());
  ForExtendableVectors<TestRebind<uint16_t>, 1>()(uint8_t());
  ForExtendableVectors<TestRebind<uint32_t>, 2>()(uint8_t());
#if HWY_HAVE_INTEGER64
  ForExtendableVectors<TestRebind<uint64_t>, 3>()(uint8_t());
#endif  // HWY_HAVE_INTEGER64
}

template <typename ToT>
struct TestPromoteTo {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D from_d) {
    static_assert(sizeof(T) < sizeof(ToT), "Input type must be narrower");
    const Rebind<ToT, D> to_d;

    const size_t N = Lanes(from_d);
    auto from = AllocateAligned<T>(N);
    auto expected = AllocateAligned<ToT>(N);
    HWY_ASSERT(from && expected);

    RandomState rng;
    for (size_t rep = 0; rep < AdjustedReps(200); ++rep) {
      for (size_t i = 0; i < N; ++i) {
        const uint64_t bits = rng();
        CopyBytes<sizeof(T)>(&bits, &from[i]);  // not same size
        expected[i] = from[i];
      }

      HWY_ASSERT_VEC_EQ(to_d, expected.get(),
                        PromoteTo(to_d, Load(from_d, from.get())));
    }
  }
};

HWY_NOINLINE void TestAllPromoteTo() {
  const ForPromoteVectors<TestPromoteTo<uint16_t>, 1> to_u16div2;
  to_u16div2(uint8_t());

  const ForPromoteVectors<TestPromoteTo<uint32_t>, 2> to_u32div4;
  to_u32div4(uint8_t());

  const ForPromoteVectors<TestPromoteTo<uint32_t>, 1> to_u32div2;
  to_u32div2(uint16_t());

  const ForPromoteVectors<TestPromoteTo<int16_t>, 1> to_i16div2;
  to_i16div2(uint8_t());
  to_i16div2(int8_t());

  const ForPromoteVectors<TestPromoteTo<int32_t>, 1> to_i32div2;
  to_i32div2(uint16_t());
  to_i32div2(int16_t());

  const ForPromoteVectors<TestPromoteTo<int32_t>, 2> to_i32div4;
  to_i32div4(uint8_t());
  to_i32div4(int8_t());

  // Must test f16/bf16 separately because we can only load/store/convert them.

#if HWY_HAVE_INTEGER64
  const ForPromoteVectors<TestPromoteTo<uint64_t>, 1> to_u64div2;
  to_u64div2(uint32_t());

  const ForPromoteVectors<TestPromoteTo<int64_t>, 1> to_i64div2;
  to_i64div2(int32_t());
  to_i64div2(uint32_t());

  const ForPromoteVectors<TestPromoteTo<uint64_t>, 2> to_u64div4;
  to_u64div4(uint16_t());

  const ForPromoteVectors<TestPromoteTo<int64_t>, 2> to_i64div4;
  to_i64div4(int16_t());
  to_i64div4(uint16_t());

  const ForPromoteVectors<TestPromoteTo<uint64_t>, 3> to_u64div8;
  to_u64div8(uint8_t());

  const ForPromoteVectors<TestPromoteTo<int64_t>, 3> to_i64div8;
  to_i64div8(int8_t());
  to_i64div8(uint8_t());
#endif

#if HWY_HAVE_FLOAT64
  const ForPromoteVectors<TestPromoteTo<double>, 1> to_f64div2;
  to_f64div2(int32_t());
  to_f64div2(uint32_t());
  to_f64div2(float());
#endif
}

template <typename ToT>
struct TestPromoteUpperLowerTo {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D from_d) {
    static_assert(sizeof(T) < sizeof(ToT), "Input type must be narrower");
    const Repartition<ToT, D> to_d;

    const size_t N = Lanes(from_d);
    auto from = AllocateAligned<T>(N);
    auto expected = AllocateAligned<ToT>(N / 2);
    HWY_ASSERT(from && expected);

    RandomState rng;
    for (size_t rep = 0; rep < AdjustedReps(200); ++rep) {
      for (size_t i = 0; i < N; ++i) {
        const uint64_t bits = rng();
        CopyBytes<sizeof(T)>(&bits, &from[i]);  // not same size
      }

      for (size_t i = 0; i < N / 2; ++i) {
        expected[i] = from[N / 2 + i];
      }
      HWY_ASSERT_VEC_EQ(to_d, expected.get(),
                        PromoteUpperTo(to_d, Load(from_d, from.get())));

      for (size_t i = 0; i < N / 2; ++i) {
        expected[i] = from[i];
      }
      HWY_ASSERT_VEC_EQ(to_d, expected.get(),
                        PromoteLowerTo(to_d, Load(from_d, from.get())));
    }
  }
};

HWY_NOINLINE void TestAllPromoteUpperLowerTo() {
  const ForShrinkableVectors<TestPromoteUpperLowerTo<uint16_t>, 1> to_u16div2;
  to_u16div2(uint8_t());

  const ForShrinkableVectors<TestPromoteUpperLowerTo<uint32_t>, 1> to_u32div2;
  to_u32div2(uint16_t());

  const ForShrinkableVectors<TestPromoteUpperLowerTo<int16_t>, 1> to_i16div2;
  to_i16div2(uint8_t());
  to_i16div2(int8_t());

  const ForShrinkableVectors<TestPromoteUpperLowerTo<int32_t>, 1> to_i32div2;
  to_i32div2(uint16_t());
  to_i32div2(int16_t());

  // Must test f16/bf16 separately because we can only load/store/convert them.

#if HWY_HAVE_INTEGER64
  const ForShrinkableVectors<TestPromoteUpperLowerTo<uint64_t>, 1> to_u64div2;
  to_u64div2(uint32_t());

  const ForShrinkableVectors<TestPromoteUpperLowerTo<int64_t>, 1> to_i64div2;
  to_i64div2(int32_t());
  to_i64div2(uint32_t());
#endif  // HWY_HAVE_INTEGER64

#if HWY_HAVE_FLOAT64
  const ForShrinkableVectors<TestPromoteUpperLowerTo<double>, 1> to_f64div2;
  to_f64div2(int32_t());
  to_f64div2(uint32_t());
  to_f64div2(float());
#endif  // HWY_HAVE_FLOAT64
}

template <typename ToT>
struct TestPromoteOddEvenTo {
  static HWY_INLINE ToT CastValueToWide(hwy::FloatTag /* to_type_tag */,
                                        hwy::FloatTag /* from_type_tag */,
                                        hwy::float16_t val) {
    return static_cast<ToT>(F32FromF16(val));
  }

  static HWY_INLINE ToT CastValueToWide(hwy::FloatTag /* to_type_tag */,
                                        hwy::SpecialTag /* from_type_tag */,
                                        hwy::bfloat16_t val) {
    return static_cast<ToT>(F32FromBF16(val));
  }

  template <class T>
  static HWY_INLINE ToT CastValueToWide(hwy::SignedTag /* to_type_tag */,
                                        hwy::FloatTag /* from_type_tag */,
                                        T val) {
    const T kMinInRangeVal = ConvertScalarTo<T>(LimitsMin<ToT>());
    const T kMinOutOfRangePosVal = ConvertScalarTo<T>(-kMinInRangeVal);
    if (val < kMinInRangeVal) {
      return LimitsMin<ToT>();
    } else if (val >= kMinOutOfRangePosVal) {
      return LimitsMax<ToT>();
    } else {
      return static_cast<ToT>(val);
    }
  }

  template <class T>
  static HWY_INLINE ToT CastValueToWide(hwy::UnsignedTag /* to_type_tag */,
                                        hwy::FloatTag /* from_type_tag */,
                                        T val) {
    const T kMinOutOfRangePosVal =
        ConvertScalarTo<T>(-ConvertScalarTo<T>(LimitsMin<MakeSigned<ToT>>()) *
                           ConvertScalarTo<T>(2));
    if (val < ConvertScalarTo<T>(0)) {
      return ToT{0};
    } else if (val >= kMinOutOfRangePosVal) {
      return LimitsMax<ToT>();
    } else {
      return static_cast<ToT>(val);
    }
  }

  template <class ToTypeTag, class FromTypeTag, class T>
  static HWY_INLINE ToT CastValueToWide(ToTypeTag /* to_type_tag */,
                                        FromTypeTag /* from_type_tag */,
                                        T val) {
    return static_cast<ToT>(val);
  }

  template <class T>
  static HWY_INLINE ToT CastValueToWide(T val) {
    using FromT = RemoveCvRef<T>;
    return CastValueToWide(hwy::TypeTag<ToT>(), hwy::TypeTag<FromT>(),
                           static_cast<FromT>(val));
  }

  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D from_d) {
    static_assert(sizeof(T) < sizeof(ToT), "Input type must be narrower");
    const Repartition<ToT, D> to_d;

    const size_t N = Lanes(from_d);
    HWY_ASSERT(N >= 2);
    auto from = AllocateAligned<T>(N);
    auto expected = AllocateAligned<ToT>(N / 2);
    HWY_ASSERT(from && expected);

    RandomState rng;
    for (size_t rep = 0; rep < AdjustedReps(200); ++rep) {
      for (size_t i = 0; i < N; ++i) {
        from[i] = RandomFiniteValue<T>(&rng);
      }

#if HWY_TARGET != HWY_SCALAR
      for (size_t i = 0; i < N / 2; ++i) {
        expected[i] = CastValueToWide(from[i * 2 + 1]);
      }
      HWY_ASSERT_VEC_EQ(to_d, expected.get(),
                        PromoteOddTo(to_d, Load(from_d, from.get())));
#endif

      for (size_t i = 0; i < N / 2; ++i) {
        expected[i] = CastValueToWide(from[i * 2]);
      }
      HWY_ASSERT_VEC_EQ(to_d, expected.get(),
                        PromoteEvenTo(to_d, Load(from_d, from.get())));
    }
  }
};

HWY_NOINLINE void TestAllPromoteOddEvenTo() {
  const ForShrinkableVectors<TestPromoteOddEvenTo<uint16_t>, 1> to_u16div2;
  to_u16div2(uint8_t());

  const ForShrinkableVectors<TestPromoteOddEvenTo<uint32_t>, 1> to_u32div2;
  to_u32div2(uint16_t());

  const ForShrinkableVectors<TestPromoteOddEvenTo<int16_t>, 1> to_i16div2;
  to_i16div2(uint8_t());
  to_i16div2(int8_t());

  const ForShrinkableVectors<TestPromoteOddEvenTo<int32_t>, 1> to_i32div2;
  to_i32div2(uint16_t());
  to_i32div2(int16_t());

  const ForShrinkableVectors<TestPromoteOddEvenTo<float>, 1> to_f32div2;
  to_f32div2(hwy::float16_t());
  to_f32div2(hwy::bfloat16_t());

#if HWY_HAVE_INTEGER64
  const ForShrinkableVectors<TestPromoteOddEvenTo<uint64_t>, 1> to_u64div2;
  to_u64div2(uint32_t());
  to_u64div2(float());

  const ForShrinkableVectors<TestPromoteOddEvenTo<int64_t>, 1> to_i64div2;
  to_i64div2(int32_t());
  to_i64div2(uint32_t());
  to_i64div2(float());
#endif  // HWY_HAVE_INTEGER64

#if HWY_HAVE_FLOAT64
  const ForShrinkableVectors<TestPromoteOddEvenTo<double>, 1> to_f64div2;
  to_f64div2(int32_t());
  to_f64div2(uint32_t());
  to_f64div2(float());
#endif  // HWY_HAVE_FLOAT64

  // The following are not supported by the underlying PromoteTo:
  // to_u16div2(int8_t());
  // to_u32div2(int16_t());
  // to_u64div2(int32_t());
}

template <typename T, HWY_IF_FLOAT(T)>
bool IsFinite(T t) {
  return std::isfinite(t);
}
// Wrapper avoids calling std::isfinite for integer types (ambiguous).
template <typename T, HWY_IF_NOT_FLOAT(T)>
bool IsFinite(T /*unused*/) {
  return true;
}

template <class D>
AlignedFreeUniquePtr<float[]> F16TestCases(D d, size_t& padded) {
  const float test_cases[] = {
      // +/- 1
      1.0f, -1.0f,
      // +/- 0
      0.0f, -0.0f,
      // near 0
      0.25f, -0.25f,
      // +/- integer
      4.0f, -32.0f,
      // positive near limit
      65472.0f, 65504.0f,
      // negative near limit
      -65472.0f, -65504.0f,
      // positive +/- delta
      2.00390625f, 3.99609375f,
      // negative +/- delta
      -2.00390625f, -3.99609375f,
      // No infinity/NaN - implementation-defined due to Arm.
  };
  constexpr size_t kNumTestCases = sizeof(test_cases) / sizeof(test_cases[0]);
  const size_t N = Lanes(d);
  HWY_ASSERT(N != 0);
  padded = RoundUpTo(kNumTestCases, N);  // allow loading whole vectors
  auto in = AllocateAligned<float>(padded);
  auto expected = AllocateAligned<float>(padded);
  HWY_ASSERT(in && expected);
  size_t i = 0;
  for (; i < kNumTestCases; ++i) {
    // Ensure the value can be exactly represented as binary16.
    in[i] = F32FromF16(F16FromF32(test_cases[i]));
  }
  for (; i < padded; ++i) {
    in[i] = 0.0f;
  }
  return in;
}

// This minimal interface is always supported, even if !HWY_HAVE_FLOAT16.
struct TestF16 {
  template <typename TF32, class DF32>
  HWY_NOINLINE void operator()(TF32 /*t*/, DF32 df32) {
    size_t padded;
    const size_t N = Lanes(df32);  // same count for f16
    HWY_ASSERT(N != 0);
    auto in = F16TestCases(df32, padded);

    using TF16 = hwy::float16_t;
    const Rebind<TF16, DF32> df16;
#if HWY_TARGET != HWY_SCALAR
    const Twice<decltype(df16)> df16t;
#endif
    const RebindToUnsigned<decltype(df16)> du16;
    // Extra Load/Store to ensure they are usable.
    auto temp16 = AllocateAligned<TF16>(N);
    HWY_ASSERT(temp16);

    // Extra Zero/BitCast to ensure they are usable. Neg is tested in
    // arithmetic_test.
    const Vec<decltype(du16)> v0_u16 = BitCast(du16, Zero(df16));
#if HWY_TARGET == HWY_SCALAR
    const Vec<DF32> v0 = BitCast(df32, ZipLower(v0_u16, v0_u16));
#else
    const Vec<DF32> v0 =
        BitCast(df32, ZeroExtendVector(Twice<decltype(du16)>(), v0_u16));
#endif

    for (size_t i = 0; i < padded; i += N) {
      const Vec<DF32> loaded = Or(Load(df32, &in[i]), v0);
      const Vec<decltype(df16)> v16 = DemoteTo(df16, loaded);
      Store(v16, df16, temp16.get());
      HWY_ASSERT_VEC_EQ(df32, loaded,
                        PromoteTo(df32, Load(df16, temp16.get())));

#if HWY_TARGET == HWY_SCALAR
      const Vec<decltype(df16)> v16L = v16;
#else
      const Vec<decltype(df16t)> v16L = Combine(df16t, Zero(df16), v16);
#endif
      HWY_ASSERT_VEC_EQ(df32, loaded, PromoteLowerTo(df32, v16L));

#if HWY_TARGET != HWY_SCALAR
      const Vec<decltype(df16t)> v16H = Combine(df16t, v16, Zero(df16));
      HWY_ASSERT_VEC_EQ(df32, loaded, PromoteUpperTo(df32, v16H));
#endif
    }
  }
};

HWY_NOINLINE void TestAllF16() { ForDemoteVectors<TestF16>()(float()); }

// This minimal interface is always supported, even if !HWY_HAVE_FLOAT16.
struct TestF16FromF64 {
  template <typename TF64, class DF64>
  HWY_NOINLINE void operator()(TF64 /*t*/, DF64 df64) {
#if HWY_HAVE_FLOAT64
    size_t padded;
    const size_t N = Lanes(df64);  // same count for f16 and f32
    HWY_ASSERT(N != 0);

    const Rebind<hwy::float16_t, DF64> df16;
    const Rebind<float, DF64> df32;
    const RebindToUnsigned<decltype(df64)> du64;
    using VF16 = Vec<decltype(df16)>;
    using VF32 = Vec<decltype(df32)>;
    using VF64 = Vec<decltype(df64)>;
    using VU64 = Vec<decltype(du64)>;

    auto f32_in = F16TestCases(df32, padded);
    const VU64 u64_zero =
        Set(du64, static_cast<uint64_t>(Unpredictable1() - 1));
    const VF64 f64_zero = BitCast(df64, u64_zero);
    const VF16 f16_zero = ResizeBitCast(df16, u64_zero);

    for (size_t i = 0; i < padded; i += N) {
      const VF32 vf32 = Load(df32, f32_in.get() + i);
      const VF16 vf16 = Or(DemoteTo(df16, vf32), f16_zero);
      const VF64 vf64 = Or(PromoteTo(df64, vf32), f64_zero);

      HWY_ASSERT_VEC_EQ(df16, vf16, DemoteTo(df16, vf64));
      HWY_ASSERT_VEC_EQ(df64, vf64, PromoteTo(df64, vf16));
    }
#else
    (void)df64;
#endif
  }
};

HWY_NOINLINE void TestAllF16FromF64() {
#if HWY_HAVE_FLOAT64
  ForDemoteVectors<TestF16FromF64, 2>()(double());
#endif
}

template <class D>
AlignedFreeUniquePtr<float[]> BF16TestCases(
    D d, size_t& padded, AlignedFreeUniquePtr<float[]>& expected) {
  const float test_cases[] = {
      // +/- 1
      1.0f, -1.0f,
      // +/- 0
      0.0f, -0.0f,
      // near 0
      0.25f, -0.25f,
      // +/- integer
      4.0f, -32.0f,
      // positive near limit
      3.389531389251535E38f, 1.99384199368e+38f,
      // negative near limit
      -3.389531389251535E38f, -1.99384199368e+38f,
      // positive +/- delta
      2.015625f, 3.984375f,
      // negative +/- delta
      -2.015625f, -3.984375f,

      // The above have all excess mantissa bits zero, such that
      // PromoteTo(DemoteTo) matches the input. Also test round to nearest even:
      1.0039063f,  // only below is set
      1.0117188f,  // LSB and below are set
      1.9921875f,  // all bits except below are set
      1.9960938f,  // all bits and below are set
  };
  constexpr size_t kNumTestCases = sizeof(test_cases) / sizeof(test_cases[0]);
  const size_t N = Lanes(d);
  HWY_ASSERT(N != 0);
  padded = RoundUpTo(kNumTestCases, N);  // allow loading whole vectors
  auto in = AllocateAligned<float>(padded);
  expected = AllocateAligned<float>(padded);
  HWY_ASSERT(in && expected);
  size_t i = 0;
  for (; i < kNumTestCases; ++i) {
    in[i] = test_cases[i] * static_cast<float>(hwy::Unpredictable1());
    expected[i] = hwy::ConvertScalarTo<float>(
        hwy::ConvertScalarTo<hwy::bfloat16_t>(in[i]));
  }
  for (; i < padded; ++i) {
    in[i] = expected[i] = 0.0f;
  }
  return in;
}

struct TestBF16 {
  template <typename TF32, class DF32>
  HWY_NOINLINE void operator()(TF32 /*t*/, DF32 df32) {
    size_t padded;
    AlignedFreeUniquePtr<float[]> expected;
    const auto in = BF16TestCases(df32, padded, expected);
    using TBF16 = bfloat16_t;
#if HWY_TARGET == HWY_SCALAR
    const Rebind<TBF16, DF32> dbf16;  // avoid 4/2 = 2 lanes
#else
    const Repartition<TBF16, DF32> dbf16;
#endif
    const Half<decltype(dbf16)> dbf16_half;
    using VF = Vec<decltype(df32)>;
    using VBF16 = Vec<decltype(dbf16)>;
    using VBF16H = Vec<decltype(dbf16_half)>;
    const size_t N = Lanes(df32);

    HWY_ASSERT(Lanes(dbf16_half) == N);
    auto temp16 = AllocateAligned<TBF16>(N);
    HWY_ASSERT(temp16);

    for (size_t i = 0; i < padded; i += N) {
      const VF vin = Load(df32, &in[i]);
      const VF vexp = Load(df32, &expected[i]);
      const VBF16H v16 = DemoteTo(dbf16_half, vin);
      Store(v16, dbf16_half, temp16.get());
      const VBF16H v16_loaded = Load(dbf16_half, temp16.get());
      HWY_ASSERT_VEC_EQ(df32, vexp, PromoteTo(df32, v16_loaded));

#if HWY_TARGET == HWY_SCALAR
      const VBF16 v16L = v16_loaded;
#else
      const VBF16 v16L = Combine(dbf16, Zero(dbf16_half), v16_loaded);
#endif
      HWY_ASSERT_VEC_EQ(df32, vexp, PromoteLowerTo(df32, v16L));

#if HWY_TARGET != HWY_SCALAR
      const VBF16 v16H = Combine(dbf16, v16_loaded, Zero(dbf16_half));
      HWY_ASSERT_VEC_EQ(df32, vexp, PromoteUpperTo(df32, v16H));
#endif
    }
  }
};

HWY_NOINLINE void TestAllBF16() { ForShrinkableVectors<TestBF16>()(float()); }

struct TestConvertU8 {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, const D du32) {
    const Rebind<uint8_t, D> du8;
    const auto wrap = Set(du32, 0xFF);
    HWY_ASSERT_VEC_EQ(du8, Iota(du8, 0), U8FromU32(And(Iota(du32, 0), wrap)));
    HWY_ASSERT_VEC_EQ(du8, Iota(du8, 0x7F),
                      U8FromU32(And(Iota(du32, 0x7F), wrap)));
  }
};

HWY_NOINLINE void TestAllConvertU8() {
  ForDemoteVectors<TestConvertU8, 2>()(uint32_t());
}

class TestIntFromFloat {
  template <typename TF, class DF>
  static HWY_NOINLINE void TestHuge(TF /*unused*/, const DF df) {
    using TI = MakeSigned<TF>;
    const Rebind<TI, DF> di;

    // Huge positive
    HWY_ASSERT_VEC_EQ(di, Set(di, LimitsMax<TI>()),
                      ConvertTo(di, Set(df, HighestValue<TF>())));

    // Huge negative
    HWY_ASSERT_VEC_EQ(di, Set(di, LimitsMin<TI>()),
                      ConvertTo(di, Set(df, LowestValue<TF>())));
  }

  template <typename TF, class DF>
  static HWY_NOINLINE void TestPowers(TF /*unused*/, const DF df) {
    using TI = MakeSigned<TF>;
    const Rebind<TI, DF> di;
    constexpr size_t kBits = sizeof(TF) * 8;

    // Powers of two, plus offsets to set some mantissa bits.
    const int64_t ofs_table[3] = {0LL, 3LL << (kBits / 2), 1LL << (kBits - 15)};
    for (int sign = 0; sign < 2; ++sign) {
      for (size_t shift = 0; shift < kBits - 1; ++shift) {
        for (int64_t ofs : ofs_table) {
          const int64_t mag = (int64_t{1} << shift) + ofs;
          const int64_t val = sign ? mag : -mag;
          const TF val_f = ConvertScalarTo<TF>(val);
          // Convert expected value to account for loss of precision.
          HWY_ASSERT_VEC_EQ(
              di, Set(di, static_cast<TI>(ConvertScalarTo<double>(val_f))),
              ConvertTo(di, Set(df, val_f)));
        }
      }
    }
  }

  template <typename TF, class DF>
  static HWY_NOINLINE void TestRandom(TF /*unused*/, const DF df) {
    using TI = MakeSigned<TF>;
    const Rebind<TI, DF> di;
    const size_t N = Lanes(df);

    // TF does not have enough precision to represent TI.
    const double min = static_cast<double>(LimitsMin<TI>());
    const double max = static_cast<double>(LimitsMax<TI>());

    // Also check random values.
    auto from = AllocateAligned<TF>(N);
    auto expected = AllocateAligned<TI>(N);
    HWY_ASSERT(from && expected);
    RandomState rng;
    for (size_t rep = 0; rep < AdjustedReps(1000); ++rep) {
      for (size_t i = 0; i < N; ++i) {
        do {
          const uint64_t bits = rng();
          CopyBytes<sizeof(TF)>(&bits, &from[i]);  // not same size
        } while (!ScalarIsFinite(from[i]));
        if (from[i] >= max) {
          expected[i] = LimitsMax<TI>();
        } else if (from[i] <= min) {
          expected[i] = LimitsMin<TI>();
        } else {
          expected[i] = static_cast<TI>(from[i]);
        }
      }

      HWY_ASSERT_VEC_EQ(di, expected.get(),
                        ConvertTo(di, Load(df, from.get())));
    }
  }

 public:
  template <typename TF, class DF>
  HWY_NOINLINE void operator()(TF tf, const DF df) {
    using TI = MakeSigned<TF>;
    const Rebind<TI, DF> di;
    const size_t N = Lanes(df);

    // Integer positive
    HWY_ASSERT_VEC_EQ(di, Iota(di, 4), ConvertTo(di, Iota(df, 4.0)));

    // Integer negative
    HWY_ASSERT_VEC_EQ(di, Iota(di, -static_cast<TI>(N)),
                      ConvertTo(di, Iota(df, -ConvertScalarTo<TF>(N))));

    // Above positive
    HWY_ASSERT_VEC_EQ(di, Iota(di, 2), ConvertTo(di, Iota(df, 2.1)));

    // Below positive
    HWY_ASSERT_VEC_EQ(di, Iota(di, 3), ConvertTo(di, Iota(df, 3.9)));

    const double neg = -static_cast<double>(N + 1);
    const double eps =
        ConvertScalarTo<double>(Epsilon<TF>()) * static_cast<double>(N);
    // Above negative
    HWY_ASSERT_VEC_EQ(di, Iota(di, -static_cast<TI>(N)),
                      ConvertTo(di, Iota(df, ConvertScalarTo<TF>(neg + eps))));

    // Below negative
    HWY_ASSERT_VEC_EQ(di, Iota(di, -static_cast<TI>(N + 1)),
                      ConvertTo(di, Iota(df, ConvertScalarTo<TF>(neg - eps))));

    TestHuge(tf, df);
    TestPowers(tf, df);
    TestRandom(tf, df);
  }
};

HWY_NOINLINE void TestAllIntFromFloat() {
  ForFloatTypes(ForPartialVectors<TestIntFromFloat>());
}

class TestUintFromFloat {
  template <typename TF, class DF>
  static HWY_NOINLINE void TestPowers(TF /*unused*/, const DF df) {
    using TU = MakeUnsigned<TF>;
    const Rebind<TU, DF> du;
    constexpr size_t kBits = sizeof(TU) * 8;

    // Powers of two, plus offsets to set some mantissa bits.
    const uint64_t ofs_table[3] = {0ULL, 3ULL << (kBits / 2),
                                   1ULL << (kBits - 15)};
    for (int sign = 0; sign < 2; ++sign) {
      for (size_t shift = 0; shift < kBits - 1; ++shift) {
        for (uint64_t ofs : ofs_table) {
          const uint64_t mag = (uint64_t{1} << shift) + ofs;
          const TF flt_mag = static_cast<TF>(mag);
          const TF flt_val = static_cast<TF>(sign ? -flt_mag : flt_mag);
          const TU expected_result = sign ? TU{0} : static_cast<TU>(mag);

          HWY_ASSERT_VEC_EQ(du, Set(du, expected_result),
                            ConvertTo(du, Set(df, flt_val)));
        }
      }
    }
  }

  template <typename TF, class DF>
  static HWY_NOINLINE void TestRandom(TF /*unused*/, const DF df) {
    using TU = MakeUnsigned<TF>;
    const Rebind<TU, DF> du;
    const size_t N = Lanes(df);

    // If LimitsMax<TU>() can be exactly represented in TF,
    // kSmallestOutOfTURangePosVal is equal to LimitsMax<TU>().

    // Otherwise, if LimitsMax<TU>() cannot be exactly represented in TF,
    // kSmallestOutOfTURangePosVal is equal to LimitsMax<TU>() + 1, which can
    // be exactly represented in TF.
    constexpr TF kSmallestOutOfTURangePosVal =
        (sizeof(TU) * 8 <= static_cast<size_t>(MantissaBits<TF>()) + 1)
            ? static_cast<TF>(LimitsMax<TU>())
            : static_cast<TF>(static_cast<TF>(TU{1} << (sizeof(TU) * 8 - 1)) *
                              ConvertScalarTo<TF>(2));

    constexpr uint64_t kRandBitsMask =
        static_cast<uint64_t>(LimitsMax<MakeSigned<TU>>());

    // Also check random values.
    auto from_pos = AllocateAligned<TF>(N);
    auto from_neg = AllocateAligned<TF>(N);
    auto expected = AllocateAligned<TU>(N);
    HWY_ASSERT(from_pos && from_neg && expected);

    RandomState rng;
    for (size_t rep = 0; rep < AdjustedReps(1000); ++rep) {
      for (size_t i = 0; i < N; ++i) {
        do {
          const TU bits = static_cast<TU>(rng() & kRandBitsMask);
          CopyBytes<sizeof(TF)>(&bits, &from_pos[i]);
        } while (!std::isfinite(from_pos[i]));
        from_neg[i] = static_cast<TF>(-from_pos[i]);

        expected[i] = (from_pos[i] < kSmallestOutOfTURangePosVal)
                          ? static_cast<TU>(from_pos[i])
                          : LimitsMax<TU>();
      }

      HWY_ASSERT_VEC_EQ(du, expected.get(),
                        ConvertTo(du, Load(df, from_pos.get())));
      HWY_ASSERT_VEC_EQ(du, Zero(du), ConvertTo(du, Load(df, from_neg.get())));
    }
  }

 public:
  template <typename TF, class DF>
  HWY_NOINLINE void operator()(TF tf, const DF df) {
    using TU = MakeUnsigned<TF>;
    const Rebind<TU, DF> du;
    const size_t N = Lanes(df);

    // Integer positive
    HWY_ASSERT_VEC_EQ(du, Iota(du, 4), ConvertTo(du, Iota(df, 4.0)));

    // Integer negative
    HWY_ASSERT_VEC_EQ(du, Zero(du),
                      ConvertTo(du, Iota(df, -ConvertScalarTo<TF>(N))));

    // Above positive
    HWY_ASSERT_VEC_EQ(du, Iota(du, 2), ConvertTo(du, Iota(df, 2.001)));

    // Below positive
    HWY_ASSERT_VEC_EQ(du, Iota(du, 3), ConvertTo(du, Iota(df, 3.9999)));

    const TF eps = static_cast<TF>(0.0001);
    // Above negative
    HWY_ASSERT_VEC_EQ(
        du, Zero(du),
        ConvertTo(du, Iota(df, -ConvertScalarTo<TF>(N + 1) + eps)));

    // Below negative
    HWY_ASSERT_VEC_EQ(
        du, Zero(du),
        ConvertTo(du, Iota(df, -ConvertScalarTo<TF>(N + 1) - eps)));

    TestPowers(tf, df);
    TestRandom(tf, df);
  }
};

HWY_NOINLINE void TestAllUintFromFloat() {
  // std::isfinite does not support float16_t.
  ForFloat3264Types(ForPartialVectors<TestUintFromFloat>());
}

struct TestFloatFromInt {
  template <typename TF, class DF>
  HWY_NOINLINE void operator()(TF /*unused*/, const DF df) {
    using TI = MakeSigned<TF>;
    const RebindToSigned<DF> di;
    const size_t N = Lanes(df);

    // Integer positive
    HWY_ASSERT_VEC_EQ(df, Iota(df, 4.0), ConvertTo(df, Iota(di, 4)));

    // Integer negative
    HWY_ASSERT_VEC_EQ(df, Iota(df, -ConvertScalarTo<TF>(N)),
                      ConvertTo(df, Iota(di, -static_cast<TI>(N))));

    // Max positive
    HWY_ASSERT_VEC_EQ(df, Set(df, ConvertScalarTo<TF>(LimitsMax<TI>())),
                      ConvertTo(df, Set(di, LimitsMax<TI>())));

    // Min negative
    HWY_ASSERT_VEC_EQ(df, Set(df, ConvertScalarTo<TF>(LimitsMin<TI>())),
                      ConvertTo(df, Set(di, LimitsMin<TI>())));
  }
};

HWY_NOINLINE void TestAllFloatFromInt() {
  ForFloatTypes(ForPartialVectors<TestFloatFromInt>());
}

struct TestFloatFromUint {
  template <typename TF, class DF>
  HWY_NOINLINE void operator()(TF /*unused*/, const DF df) {
    using TU = MakeUnsigned<TF>;
    const RebindToUnsigned<DF> du;

    // Integer positive
    HWY_ASSERT_VEC_EQ(df, Iota(df, 4.0), ConvertTo(df, Iota(du, 4)));
    HWY_ASSERT_VEC_EQ(df, Set(df, ConvertScalarTo<TF>(32767)),
                      ConvertTo(df, Set(du, 32767)));  // 2^16-1
    if (sizeof(TF) > 4) {
      HWY_ASSERT_VEC_EQ(df, Iota(df, 4294967295.0),
                        ConvertTo(df, Iota(du, 4294967295ULL)));  // 2^32-1
    }

    // Max positive
    HWY_ASSERT_VEC_EQ(df, Set(df, ConvertScalarTo<TF>(LimitsMax<TU>())),
                      ConvertTo(df, Set(du, LimitsMax<TU>())));

    // Zero
    HWY_ASSERT_VEC_EQ(df, Zero(df), ConvertTo(df, Zero(du)));
  }
};

HWY_NOINLINE void TestAllFloatFromUint() {
  ForFloatTypes(ForPartialVectors<TestFloatFromUint>());
}

#undef HWY_F2I_INLINE
#if HWY_TARGET == HWY_RVV
// Workaround for incorrect rounding mode.
#define HWY_F2I_INLINE HWY_NOINLINE
#else
#define HWY_F2I_INLINE HWY_INLINE
#endif

template <class TTo>
class TestNonFiniteF2IConvertTo {
 private:
  static_assert(IsIntegerLaneType<TTo>() && IsSame<TTo, RemoveCvRef<TTo>>(),
                "TTo must be an integer type");

  template <class DF, HWY_IF_T_SIZE_LE_D(DF, sizeof(TTo) - 1)>
  static HWY_F2I_INLINE VFromD<Rebind<TTo, DF>> DoF2IConvVec(DF df,
                                                             VFromD<DF> v) {
    return PromoteTo(Rebind<TTo, decltype(df)>(), v);
  }

  template <class DF, HWY_IF_T_SIZE_D(DF, sizeof(TTo))>
  static HWY_F2I_INLINE VFromD<Rebind<TTo, DF>> DoF2IConvVec(DF df,
                                                             VFromD<DF> v) {
    return ConvertTo(Rebind<TTo, decltype(df)>(), v);
  }

  template <class DF, HWY_IF_T_SIZE_GT_D(DF, sizeof(TTo))>
  static HWY_F2I_INLINE VFromD<Rebind<TTo, DF>> DoF2IConvVec(DF df,
                                                             VFromD<DF> v) {
    return DemoteTo(Rebind<TTo, decltype(df)>(), v);
  }

  template <class DF, HWY_IF_T_SIZE_LE_D(DF, sizeof(TTo) - 1)>
  static HWY_INLINE Mask<Rebind<TTo, DF>> DoF2IConvMask(DF df, Mask<DF> m) {
    return PromoteMaskTo(Rebind<TTo, DF>(), df, m);
  }

  template <class DF, HWY_IF_T_SIZE_D(DF, sizeof(TTo))>
  static HWY_INLINE Mask<Rebind<TTo, DF>> DoF2IConvMask(DF df, Mask<DF> m) {
    return RebindMask(Rebind<TTo, decltype(df)>(), m);
  }

  template <class DF, HWY_IF_T_SIZE_GT_D(DF, sizeof(TTo))>
  static HWY_INLINE Mask<Rebind<TTo, DF>> DoF2IConvMask(DF df, Mask<DF> m) {
    return DemoteMaskTo(Rebind<TTo, DF>(), df, m);
  }

  template <class DF, HWY_IF_T_SIZE_LE_D(DF, sizeof(TTo) - 1)>
  static HWY_INLINE Vec<Rebind<MakeSigned<TTo>, DF>> DoF2IConvMsbMaskVec(
      DF /*df*/, Vec<DF> v) {
    return PromoteTo(Rebind<MakeSigned<TTo>, DF>(),
                     BitCast(RebindToSigned<DF>(), v));
  }

  template <class DF, HWY_IF_T_SIZE_D(DF, sizeof(TTo))>
  static HWY_INLINE Vec<Rebind<MakeSigned<TTo>, DF>> DoF2IConvMsbMaskVec(
      DF /*df*/, Vec<DF> v) {
    return BitCast(Rebind<MakeSigned<TTo>, DF>(), v);
  }

  template <class DF, HWY_IF_T_SIZE_GT_D(DF, sizeof(TTo))>
  static HWY_INLINE Vec<Rebind<MakeSigned<TTo>, DF>> DoF2IConvMsbMaskVec(
      DF /*df*/, Vec<DF> v) {
    return DemoteTo(Rebind<MakeSigned<TTo>, DF>(),
                    BitCast(RebindToSigned<DF>(), v));
  }

  template <class DF>
  static HWY_NOINLINE void VerifyNonFiniteF2I(DF df, const VecArg<VFromD<DF>> v,
                                              const char* filename,
                                              const int line) {
    using TF = TFromD<DF>;
    using TU = MakeUnsigned<TF>;
    using TTo_I = MakeSigned<TTo>;

    const TF kMinOutOfRangePosVal =
        ConvertScalarTo<TF>((-ConvertScalarTo<TF>(LimitsMin<TTo_I>())) *
                            ConvertScalarTo<TF>(IsSigned<TTo>() ? 1 : 2));
    HWY_ASSERT(ConvertScalarTo<double>(kMinOutOfRangePosVal) > 0.0);

    const Rebind<TTo, DF> d_to;
    const RebindToSigned<decltype(d_to)> di_to;
    const RebindToUnsigned<DF> du;

    const auto non_elided_zero =
        BitCast(df, Set(du, static_cast<TU>(Unpredictable1() - 1)));

    const auto v2 = Or(non_elided_zero, v);
    const auto is_nan_mask = IsNaN(v2);
    const auto is_in_range_mask =
        AndNot(is_nan_mask, Lt(Abs(IfThenZeroElse(is_nan_mask, v2)),
                               Set(df, kMinOutOfRangePosVal)));

    const auto is_nan_vmask = VecFromMask(d_to, DoF2IConvMask(df, is_nan_mask));

    const auto expected_in_range =
        DoF2IConvVec(df, IfThenElseZero(is_in_range_mask, v2));
    const auto expected_out_of_range =
        Or(is_nan_vmask,
           BitCast(d_to, IfNegativeThenElse(
                             DoF2IConvMsbMaskVec(df, v2),
                             BitCast(di_to, Set(d_to, LimitsMin<TTo>())),
                             BitCast(di_to, Set(d_to, LimitsMax<TTo>())))));

    const auto expected = IfThenElse(DoF2IConvMask(df, is_in_range_mask),
                                     expected_in_range, expected_out_of_range);

    AssertVecEqual(d_to, expected, Or(DoF2IConvVec(df, v), is_nan_vmask),
                   filename, line);
    AssertVecEqual(d_to, expected, Or(DoF2IConvVec(df, v2), is_nan_vmask),
                   filename, line);
  }

 public:
  template <typename TF, class DF>
  HWY_NOINLINE void operator()(TF /*unused*/, const DF df) {
    using TI = MakeSigned<TF>;
    using TU = MakeUnsigned<TF>;
    const RebindToSigned<DF> di;

    // TODO(janwas): workaround for QEMU 7.2 crash on vfwcvt_rtz_x_f_v:
    // target/riscv/translate.c:213 in void decode_save_opc(DisasContext *):
    // ctx->insn_start != NULL.
#if HWY_TARGET == HWY_RVV || (HWY_ARCH_RISCV && HWY_TARGET == HWY_EMU128)
    if (sizeof(TTo) > sizeof(TF)) {
      return;
    }
#endif

    const auto pos_nan = BitCast(df, Set(di, LimitsMax<TI>()));
    const auto neg_nan = BitCast(df, Set(di, static_cast<TI>(-1)));
    const auto pos_inf =
        BitCast(df, Set(di, static_cast<TI>(ExponentMask<TF>())));
    const auto neg_inf = Neg(pos_inf);

    VerifyNonFiniteF2I(df, pos_nan, __FILE__, __LINE__);
    VerifyNonFiniteF2I(df, neg_nan, __FILE__, __LINE__);
    VerifyNonFiniteF2I(df, pos_inf, __FILE__, __LINE__);
    VerifyNonFiniteF2I(df, neg_inf, __FILE__, __LINE__);

    const TI non_elided_one = static_cast<TI>(Unpredictable1());

    const auto iota1 = Iota(df, ConvertScalarTo<TF>(non_elided_one));
    VerifyNonFiniteF2I(df, iota1, __FILE__, __LINE__);

    const size_t N = Lanes(df);

#if HWY_TARGET != HWY_SCALAR
    if (N > 1) {
      VerifyNonFiniteF2I(df, OddEven(pos_nan, iota1), __FILE__, __LINE__);
      VerifyNonFiniteF2I(df, OddEven(iota1, pos_nan), __FILE__, __LINE__);
      VerifyNonFiniteF2I(df, OddEven(neg_nan, iota1), __FILE__, __LINE__);
      VerifyNonFiniteF2I(df, OddEven(iota1, neg_nan), __FILE__, __LINE__);

      VerifyNonFiniteF2I(df, OddEven(pos_inf, iota1), __FILE__, __LINE__);
      VerifyNonFiniteF2I(df, OddEven(iota1, pos_inf), __FILE__, __LINE__);
      VerifyNonFiniteF2I(df, OddEven(neg_inf, iota1), __FILE__, __LINE__);
      VerifyNonFiniteF2I(df, OddEven(iota1, neg_inf), __FILE__, __LINE__);
    }
#endif

    auto in_lanes = AllocateAligned<TF>(N);
    HWY_ASSERT(in_lanes);

    RandomState rng;
    for (size_t rep = 0; rep < AdjustedReps(1000); ++rep) {
      for (size_t i = 0; i < N; ++i) {
        in_lanes[i] = BitCastScalar<TF>(static_cast<TU>(rng()));
      }

      const auto v = Load(df, in_lanes.get());
      VerifyNonFiniteF2I(df, v, __FILE__, __LINE__);
      VerifyNonFiniteF2I(df, Or(v, pos_inf), __FILE__, __LINE__);

#if HWY_TARGET != HWY_SCALAR
      if (N > 1) {
        VerifyNonFiniteF2I(df, OddEven(pos_nan, v), __FILE__, __LINE__);
        VerifyNonFiniteF2I(df, OddEven(v, pos_nan), __FILE__, __LINE__);
        VerifyNonFiniteF2I(df, OddEven(neg_nan, v), __FILE__, __LINE__);
        VerifyNonFiniteF2I(df, OddEven(v, neg_nan), __FILE__, __LINE__);

        VerifyNonFiniteF2I(df, OddEven(pos_inf, v), __FILE__, __LINE__);
        VerifyNonFiniteF2I(df, OddEven(v, pos_inf), __FILE__, __LINE__);
        VerifyNonFiniteF2I(df, OddEven(neg_inf, v), __FILE__, __LINE__);
        VerifyNonFiniteF2I(df, OddEven(v, neg_inf), __FILE__, __LINE__);
      }
#endif
    }
  }
};

HWY_NOINLINE void TestAllNonFiniteF2IConvertTo() {
#if HWY_HAVE_FLOAT16
  ForPartialVectors<TestNonFiniteF2IConvertTo<int16_t>>()(hwy::float16_t());
  ForPartialVectors<TestNonFiniteF2IConvertTo<uint16_t>>()(hwy::float16_t());
#endif

  ForPartialVectors<TestNonFiniteF2IConvertTo<int32_t>>()(float());
  ForPartialVectors<TestNonFiniteF2IConvertTo<uint32_t>>()(float());

#if HWY_HAVE_FLOAT64
  ForPartialVectors<TestNonFiniteF2IConvertTo<int64_t>>()(double());
  ForPartialVectors<TestNonFiniteF2IConvertTo<uint64_t>>()(double());
#endif

#if HWY_HAVE_INTEGER64
  ForPromoteVectors<TestNonFiniteF2IConvertTo<int64_t>>()(float());
  ForPromoteVectors<TestNonFiniteF2IConvertTo<uint64_t>>()(float());
#endif

#if HWY_HAVE_FLOAT64
  ForDemoteVectors<TestNonFiniteF2IConvertTo<int32_t>>()(double());
  ForDemoteVectors<TestNonFiniteF2IConvertTo<uint32_t>>()(double());
#endif
}

struct TestI32F64 {
  template <typename TF, class DF>
  HWY_NOINLINE void operator()(TF /*unused*/, const DF df) {
    using TI = int32_t;
    const Rebind<TI, DF> di;
    const size_t N = Lanes(df);

    // Integer positive
    HWY_ASSERT_VEC_EQ(df, Iota(df, 4.0), PromoteTo(df, Iota(di, 4)));

    // Integer negative
    HWY_ASSERT_VEC_EQ(df, Iota(df, -ConvertScalarTo<TF>(N)),
                      PromoteTo(df, Iota(di, -static_cast<TI>(N))));

    // Above positive
    HWY_ASSERT_VEC_EQ(df, Iota(df, 2.0), PromoteTo(df, Iota(di, 2)));

    // Below positive
    HWY_ASSERT_VEC_EQ(df, Iota(df, 4.0), PromoteTo(df, Iota(di, 4)));

    // Above negative
    HWY_ASSERT_VEC_EQ(df, Iota(df, ConvertScalarTo<TF>(-4.0)),
                      PromoteTo(df, Iota(di, -4)));

    // Below negative
    HWY_ASSERT_VEC_EQ(df, Iota(df, -2.0), PromoteTo(df, Iota(di, -2)));

    // Max positive int
    HWY_ASSERT_VEC_EQ(df, Set(df, TF{LimitsMax<TI>()}),
                      PromoteTo(df, Set(di, LimitsMax<TI>())));

    // Min negative int
    HWY_ASSERT_VEC_EQ(df, Set(df, TF{LimitsMin<TI>()}),
                      PromoteTo(df, Set(di, LimitsMin<TI>())));
  }
};

HWY_NOINLINE void TestAllI32F64() {
#if HWY_HAVE_FLOAT64
  ForDemoteVectors<TestI32F64>()(double());
#endif
}

template <class ToT>
struct TestF2IPromoteTo {
  template <typename TF, class DF>
  HWY_NOINLINE void operator()(TF /*unused*/, const DF df) {
    const Rebind<ToT, decltype(df)> d_to;

    // TODO(janwas): workaround for QEMU 7.2 crash on vfwcvt_rtz_x_f_v:
    // target/riscv/translate.c:213 in void decode_save_opc(DisasContext *):
    // ctx->insn_start != NULL.
#if HWY_TARGET == HWY_RVV || (HWY_ARCH_RISCV && HWY_TARGET == HWY_EMU128)
    return;
#endif

    HWY_ASSERT_VEC_EQ(d_to, Set(d_to, ToT(1)), PromoteTo(d_to, Set(df, TF{1})));
    HWY_ASSERT_VEC_EQ(d_to, Zero(d_to), PromoteTo(d_to, Zero(df)));
    HWY_ASSERT_VEC_EQ(d_to, Set(d_to, IsSigned<ToT>() ? ToT(-1) : ToT(0)),
                      PromoteTo(d_to, Set(df, TF{-1})));

    constexpr size_t kNumOfNonSignBitsInToT =
        sizeof(ToT) * 8 - static_cast<size_t>(IsSigned<ToT>());

    // kSmallestInToTRangeVal is the smallest value of TF that is within the
    // range of ToT.
    constexpr TF kSmallestInToTRangeVal = static_cast<TF>(LimitsMin<ToT>());

    // If LimitsMax<ToT>() can be exactly represented in TF,
    // kSmallestOutOfToTRangePosVal is equal to LimitsMax<ToT>().

    // Otherwise, if LimitsMax<ToT>() cannot be exactly represented in TF,
    // kSmallestOutOfToTRangePosVal is equal to LimitsMax<ToT>() + 1, which can
    // be exactly represented in TF.
    constexpr TF kSmallestOutOfToTRangePosVal =
        (kNumOfNonSignBitsInToT <= static_cast<size_t>(MantissaBits<TF>()) + 1)
            ? static_cast<TF>(LimitsMax<ToT>())
            : static_cast<TF>(
                  static_cast<TF>(ToT{1} << (kNumOfNonSignBitsInToT - 1)) *
                  TF{2});

    HWY_ASSERT_VEC_EQ(d_to, Set(d_to, LimitsMax<ToT>()),
                      PromoteTo(d_to, Set(df, kSmallestOutOfToTRangePosVal)));
    HWY_ASSERT_VEC_EQ(
        d_to, Set(d_to, LimitsMax<ToT>()),
        PromoteTo(d_to, Set(df, kSmallestOutOfToTRangePosVal * TF{2})));
    HWY_ASSERT_VEC_EQ(
        d_to, Set(d_to, LimitsMin<ToT>()),
        PromoteTo(d_to, Set(df, kSmallestOutOfToTRangePosVal * TF{-2})));

    const size_t N = Lanes(df);
    auto in_pos = AllocateAligned<TF>(N);
    auto in_neg = AllocateAligned<TF>(N);
    auto expected_pos_to_int = AllocateAligned<ToT>(N);
    auto expected_neg_to_int = AllocateAligned<ToT>(N);
    HWY_ASSERT(in_pos && in_neg && expected_pos_to_int && expected_neg_to_int);

    using FromTU = MakeUnsigned<TF>;

    constexpr uint64_t kRandBitsMask =
        static_cast<uint64_t>(LimitsMax<MakeSigned<TF>>());

    RandomState rng;
    for (size_t rep = 0; rep < AdjustedReps(1000); ++rep) {
      for (size_t i = 0; i < N; ++i) {
        do {
          const FromTU bits = static_cast<FromTU>(rng() & kRandBitsMask);
          CopyBytes<sizeof(TF)>(&bits, &in_pos[i]);  // not same size
        } while (!std::isfinite(in_pos[i]));
        const TF pos_val = in_pos[i];
        const TF neg_val = static_cast<TF>(-pos_val);
        in_neg[i] = neg_val;

        expected_pos_to_int[i] = (pos_val < kSmallestOutOfToTRangePosVal)
                                     ? static_cast<ToT>(pos_val)
                                     : LimitsMax<ToT>();
        expected_neg_to_int[i] = (neg_val > kSmallestInToTRangeVal)
                                     ? static_cast<ToT>(neg_val)
                                     : LimitsMin<ToT>();
      }

      HWY_ASSERT_VEC_EQ(d_to, expected_pos_to_int.get(),
                        PromoteTo(d_to, Load(df, in_pos.get())));
      HWY_ASSERT_VEC_EQ(d_to, expected_neg_to_int.get(),
                        PromoteTo(d_to, Load(df, in_neg.get())));
    }
  }
};

HWY_NOINLINE void TestAllF2IPromoteTo() {
#if HWY_HAVE_INTEGER64
  const ForPromoteVectors<TestF2IPromoteTo<int64_t>, 1> to_i64div2;
  to_i64div2(float());

  const ForPromoteVectors<TestF2IPromoteTo<uint64_t>, 1> to_u64div2;
  to_u64div2(float());
#endif
}

template <typename ToT>
struct TestF2IPromoteUpperLowerTo {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D from_d) {
    static_assert(sizeof(T) < sizeof(ToT), "Input type must be narrower");
    const Repartition<ToT, D> to_d;

    // TODO(janwas): workaround for QEMU 7.2 crash on vfwcvt_rtz_x_f_v:
    // target/riscv/translate.c:213 in void decode_save_opc(DisasContext *):
    // ctx->insn_start != NULL.
#if HWY_TARGET == HWY_RVV || (HWY_ARCH_RISCV && HWY_TARGET == HWY_EMU128)
    return;
#endif

    const size_t N = Lanes(from_d);
    auto from = AllocateAligned<T>(N);
    auto expected = AllocateAligned<ToT>(N / 2);
    HWY_ASSERT(from && expected);

    using TU = MakeUnsigned<T>;

    constexpr int kNumOfMantBits = MantissaBits<T>();
    constexpr TU kMaxBiasedExp = static_cast<TU>(MaxExponentField<T>());
    constexpr TU kExponentBias = kMaxBiasedExp >> 1;

    constexpr TU kMaxInToTRangeBiasedExpBits =
        static_cast<TU>(HWY_MIN(kExponentBias + sizeof(ToT) * 8 -
                                    static_cast<TU>(IsSigned<ToT>()) - 1u,
                                kMaxBiasedExp - 1)
                        << kNumOfMantBits);
    constexpr TU kMinOutOfToTRangeBiasedExpBits = static_cast<TU>(
        kMaxInToTRangeBiasedExpBits + (TU{1} << kNumOfMantBits));
    constexpr TU kMaxFiniteBiasedExpBits =
        static_cast<TU>((kMaxBiasedExp - 1) << kNumOfMantBits);

    constexpr TU kExponentMask = ExponentMask<T>();
    constexpr TU kSignMantMask = static_cast<TU>(~kExponentMask);

    RandomState rng;
    for (size_t rep = 0; rep < AdjustedReps(200); ++rep) {
      for (size_t i = 0; i < N; ++i) {
        const uint64_t bits = rng();
        const TU flt_bits = static_cast<TU>(
            HWY_MIN(bits & kExponentMask, kMaxInToTRangeBiasedExpBits) |
            (bits & kSignMantMask));
        CopySameSize(&flt_bits, &from[i]);
      }

      for (size_t i = 0; i < N / 2; ++i) {
        const T val = from[N / 2 + i];
        expected[i] =
            (!IsSigned<ToT>() && val <= 0) ? ToT{0} : static_cast<ToT>(val);
      }
      HWY_ASSERT_VEC_EQ(to_d, expected.get(),
                        PromoteUpperTo(to_d, Load(from_d, from.get())));

      for (size_t i = 0; i < N / 2; ++i) {
        const T val = from[i];
        expected[i] =
            (!IsSigned<ToT>() && val <= 0) ? ToT{0} : static_cast<ToT>(val);
      }
      HWY_ASSERT_VEC_EQ(to_d, expected.get(),
                        PromoteLowerTo(to_d, Load(from_d, from.get())));

      for (size_t i = 0; i < N; ++i) {
        const uint64_t bits = rng();
        const TU flt_bits =
            static_cast<TU>(HWY_MIN(HWY_MAX(bits & kExponentMask,
                                            kMinOutOfToTRangeBiasedExpBits),
                                    kMaxFiniteBiasedExpBits) |
                            (bits & kSignMantMask));
        CopySameSize(&flt_bits, &from[i]);
      }

      for (size_t i = 0; i < N / 2; ++i) {
        const T val = from[N / 2 + i];
        expected[i] = (val < 0) ? LimitsMin<ToT>() : LimitsMax<ToT>();
      }
      HWY_ASSERT_VEC_EQ(to_d, expected.get(),
                        PromoteUpperTo(to_d, Load(from_d, from.get())));

      for (size_t i = 0; i < N / 2; ++i) {
        const T val = from[i];
        expected[i] = (val < 0) ? LimitsMin<ToT>() : LimitsMax<ToT>();
      }
      HWY_ASSERT_VEC_EQ(to_d, expected.get(),
                        PromoteLowerTo(to_d, Load(from_d, from.get())));
    }
  }
};

HWY_NOINLINE void TestAllF2IPromoteUpperLowerTo() {
#if HWY_HAVE_INTEGER64
  const ForShrinkableVectors<TestF2IPromoteUpperLowerTo<int64_t>, 1> to_i64div2;
  to_i64div2(float());

  const ForShrinkableVectors<TestF2IPromoteUpperLowerTo<uint64_t>, 1>
      to_u64div2;
  to_u64div2(float());
#endif
}

template <bool kConvToUnsigned>
class TestNonFiniteF2IPromoteUpperLowerTo {
  template <class DF>
  static HWY_NOINLINE void VerifyNonFiniteF2I(DF df, const VecArg<VFromD<DF>> v,
                                              const char* filename,
                                              const int line) {
    using TF = TFromD<DF>;
    using TI = MakeSigned<TF>;
    using TU = MakeUnsigned<TF>;
    using TW_I = MakeWide<TI>;
    using TW_U = MakeWide<TU>;
    using TW = If<kConvToUnsigned, TW_U, TW_I>;

    constexpr TF kMinOutOfRangePosVal =
        static_cast<TF>((-static_cast<TF>(LimitsMin<TW_I>())) *
                        static_cast<TF>(kConvToUnsigned ? 2 : 1));
    static_assert(kMinOutOfRangePosVal > static_cast<TF>(0),
                  "kMinOutOfRangePosVal > 0 must be true");

    const TU scalar_non_elided_zero = static_cast<TU>(Unpredictable1() - 1);

    const Half<DF> dh;
    const RebindToUnsigned<DF> du;
    const Repartition<TW, decltype(df)> dw;

    const auto non_elided_zero = BitCast(df, Set(du, scalar_non_elided_zero));
    const auto v2 = Or(non_elided_zero, v);

    const auto promoted_lo = PromoteTo(dw, LowerHalf(dh, v2));
    const auto promoted_hi = PromoteTo(dw, UpperHalf(dh, v2));
    const auto promoted_even = PromoteTo(dw, LowerHalf(ConcatEven(df, v2, v2)));
    const auto promoted_odd = PromoteTo(dw, LowerHalf(ConcatOdd(df, v2, v2)));

    AssertVecEqual(dw, promoted_lo, PromoteLowerTo(dw, v), filename, line);
    AssertVecEqual(dw, promoted_hi, PromoteUpperTo(dw, v), filename, line);
    AssertVecEqual(dw, promoted_even, PromoteEvenTo(dw, v), filename, line);
    AssertVecEqual(dw, promoted_odd, PromoteOddTo(dw, v), filename, line);

    AssertVecEqual(dw, promoted_lo, PromoteLowerTo(dw, v2), filename, line);
    AssertVecEqual(dw, promoted_hi, PromoteUpperTo(dw, v2), filename, line);
    AssertVecEqual(dw, promoted_even, PromoteEvenTo(dw, v2), filename, line);
    AssertVecEqual(dw, promoted_odd, PromoteOddTo(dw, v2), filename, line);
  }

 public:
  template <typename TF, class DF>
  HWY_NOINLINE void operator()(TF /*unused*/, const DF df) {
    using TI = MakeSigned<TF>;
    const RebindToSigned<DF> di;

    // TODO(janwas): workaround for QEMU 7.2 crash on vfwcvt_rtz_x_f_v:
    // target/riscv/translate.c:213 in void decode_save_opc(DisasContext *):
    // ctx->insn_start != NULL.
#if HWY_TARGET == HWY_RVV || (HWY_ARCH_RISCV && HWY_TARGET == HWY_EMU128)
    return;
#endif

    const auto pos_nan = BitCast(df, Set(di, LimitsMax<TI>()));
    const auto neg_nan = BitCast(df, Set(di, static_cast<TI>(-1)));
    const auto pos_inf =
        BitCast(df, Set(di, static_cast<TI>(ExponentMask<TF>())));
    const auto neg_inf = Neg(pos_inf);

    VerifyNonFiniteF2I(df, pos_nan, __FILE__, __LINE__);
    VerifyNonFiniteF2I(df, neg_nan, __FILE__, __LINE__);
    VerifyNonFiniteF2I(df, pos_inf, __FILE__, __LINE__);
    VerifyNonFiniteF2I(df, neg_inf, __FILE__, __LINE__);

    const TI non_elided_one = static_cast<TI>(Unpredictable1());
    const auto iota1 = Iota(df, ConvertScalarTo<TF>(non_elided_one));
    VerifyNonFiniteF2I(df, iota1, __FILE__, __LINE__);

#if HWY_TARGET != HWY_SCALAR
    if (Lanes(df) > 1) {
      VerifyNonFiniteF2I(df, OddEven(pos_nan, iota1), __FILE__, __LINE__);
      VerifyNonFiniteF2I(df, OddEven(iota1, pos_nan), __FILE__, __LINE__);
      VerifyNonFiniteF2I(df, OddEven(neg_nan, iota1), __FILE__, __LINE__);
      VerifyNonFiniteF2I(df, OddEven(iota1, neg_nan), __FILE__, __LINE__);
      VerifyNonFiniteF2I(df, OddEven(pos_inf, iota1), __FILE__, __LINE__);
      VerifyNonFiniteF2I(df, OddEven(iota1, pos_inf), __FILE__, __LINE__);
      VerifyNonFiniteF2I(df, OddEven(neg_inf, iota1), __FILE__, __LINE__);
      VerifyNonFiniteF2I(df, OddEven(iota1, neg_inf), __FILE__, __LINE__);
    }
#endif
  }
};

HWY_NOINLINE void TestAllNonFiniteF2IPromoteUpperLowerTo() {
#if HWY_HAVE_INTEGER64
  ForShrinkableVectors<TestNonFiniteF2IPromoteUpperLowerTo<false>, 1>()(
      float());
  ForShrinkableVectors<TestNonFiniteF2IPromoteUpperLowerTo<true>, 1>()(float());
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
HWY_BEFORE_TEST(HwyConvertTest);
HWY_EXPORT_AND_TEST_P(HwyConvertTest, TestAllRebind);
HWY_EXPORT_AND_TEST_P(HwyConvertTest, TestAllPromoteTo);
HWY_EXPORT_AND_TEST_P(HwyConvertTest, TestAllPromoteUpperLowerTo);
HWY_EXPORT_AND_TEST_P(HwyConvertTest, TestAllPromoteOddEvenTo);
HWY_EXPORT_AND_TEST_P(HwyConvertTest, TestAllF16);
HWY_EXPORT_AND_TEST_P(HwyConvertTest, TestAllF16FromF64);
HWY_EXPORT_AND_TEST_P(HwyConvertTest, TestAllBF16);
HWY_EXPORT_AND_TEST_P(HwyConvertTest, TestAllConvertU8);
HWY_EXPORT_AND_TEST_P(HwyConvertTest, TestAllIntFromFloat);
HWY_EXPORT_AND_TEST_P(HwyConvertTest, TestAllUintFromFloat);
HWY_EXPORT_AND_TEST_P(HwyConvertTest, TestAllFloatFromInt);
HWY_EXPORT_AND_TEST_P(HwyConvertTest, TestAllFloatFromUint);
HWY_EXPORT_AND_TEST_P(HwyConvertTest, TestAllNonFiniteF2IConvertTo);
HWY_EXPORT_AND_TEST_P(HwyConvertTest, TestAllI32F64);
HWY_EXPORT_AND_TEST_P(HwyConvertTest, TestAllF2IPromoteTo);
HWY_EXPORT_AND_TEST_P(HwyConvertTest, TestAllF2IPromoteUpperLowerTo);
HWY_EXPORT_AND_TEST_P(HwyConvertTest, TestAllNonFiniteF2IPromoteUpperLowerTo);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
