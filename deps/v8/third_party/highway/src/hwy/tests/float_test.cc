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

// Tests some ops specific to floating-point types (Div, Round etc.)

#include <stdio.h>

#include <cmath>  // std::ceil, std::floor

#include "hwy/base.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tests/float_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

HWY_NOINLINE void TestAllF16FromF32() {
  const FixedTag<float, 1> d1;

  // +/- 0
  HWY_ASSERT_EQ(0, BitCastScalar<uint16_t>(hwy::F16FromF32(0.0f)));
  HWY_ASSERT_EQ(0x8000, BitCastScalar<uint16_t>(hwy::F16FromF32(-0.0f)));
  // smallest f32 subnormal
  HWY_ASSERT_EQ(0,
                BitCastScalar<uint16_t>(hwy::F16FromF32(5.87747175411E-39f)));
  HWY_ASSERT_EQ(0x8000,
                BitCastScalar<uint16_t>(hwy::F16FromF32(-5.87747175411E-39f)));
  // largest f16 subnormal
  HWY_ASSERT_EQ(0x3FF, BitCastScalar<uint16_t>(hwy::F16FromF32(6.0975552E-5f)));
  HWY_ASSERT_EQ(0x83FF,
                BitCastScalar<uint16_t>(hwy::F16FromF32(-6.0975552E-5f)));
  // smallest normalized f16
  HWY_ASSERT_EQ(0x400,
                BitCastScalar<uint16_t>(hwy::F16FromF32(6.103515625E-5f)));
  HWY_ASSERT_EQ(0x8400,
                BitCastScalar<uint16_t>(hwy::F16FromF32(-6.103515625E-5f)));

  // rounding to nearest even
  HWY_ASSERT_EQ((15 << 10) + 0,  // round down to even: 0[10..0] => 0
                BitCastScalar<uint16_t>(hwy::F16FromF32(1.00048828125f)));
  HWY_ASSERT_EQ((15 << 10) + 1,  // round up: 0[1..1] => 1
                BitCastScalar<uint16_t>(hwy::F16FromF32(1.00097644329f)));
  HWY_ASSERT_EQ((15 << 10) + 2,  // round up to even: 1[10..0] => 10
                BitCastScalar<uint16_t>(hwy::F16FromF32(1.00146484375f)));

  // greater than f16 max => inf
  HWY_ASSERT_EQ(0x7C00, BitCastScalar<uint16_t>(hwy::F16FromF32(7E4f)));
  HWY_ASSERT_EQ(0xFC00, BitCastScalar<uint16_t>(hwy::F16FromF32(-7E4f)));
  // infinity
  HWY_ASSERT_EQ(0x7C00,
                BitCastScalar<uint16_t>(hwy::F16FromF32(GetLane(Inf(d1)))));
  HWY_ASSERT_EQ(0xFC00,
                BitCastScalar<uint16_t>(hwy::F16FromF32(-GetLane(Inf(d1)))));
  // NaN
  HWY_ASSERT_EQ(0x7FFF,
                BitCastScalar<uint16_t>(hwy::F16FromF32(GetLane(NaN(d1)))));
  HWY_ASSERT_EQ(0xFFFF,
                BitCastScalar<uint16_t>(hwy::F16FromF32(-GetLane(NaN(d1)))));
}

HWY_NOINLINE void TestAllF32FromF16() {
  const FixedTag<float, 1> d1;

  // +/- 0
  HWY_ASSERT_EQ(0.0f, hwy::F32FromF16(BitCastScalar<float16_t>(uint16_t{0})));
  HWY_ASSERT_EQ(-0.0f,
                hwy::F32FromF16(BitCastScalar<float16_t>(uint16_t{0x8000})));
  // largest f16 subnormal
  HWY_ASSERT_EQ(6.0975552E-5f,
                hwy::F32FromF16(BitCastScalar<float16_t>(uint16_t{0x3FF})));
  HWY_ASSERT_EQ(-6.0975552E-5f,
                hwy::F32FromF16(BitCastScalar<float16_t>(uint16_t{0x83FF})));
  // smallest normalized f16
  HWY_ASSERT_EQ(6.103515625E-5f,
                hwy::F32FromF16(BitCastScalar<float16_t>(uint16_t{0x400})));
  HWY_ASSERT_EQ(-6.103515625E-5f,
                hwy::F32FromF16(BitCastScalar<float16_t>(uint16_t{0x8400})));
  // infinity
  HWY_ASSERT_EQ(GetLane(Inf(d1)),
                hwy::F32FromF16(BitCastScalar<float16_t>(uint16_t{0x7C00})));
  HWY_ASSERT_EQ(-GetLane(Inf(d1)),
                hwy::F32FromF16(BitCastScalar<float16_t>(uint16_t{0xFC00})));
  // NaN
  HWY_ASSERT_EQ(GetLane(NaN(d1)),
                hwy::F32FromF16(BitCastScalar<float16_t>(uint16_t{0x7FFF})));
  HWY_ASSERT_EQ(-GetLane(NaN(d1)),
                hwy::F32FromF16(BitCastScalar<float16_t>(uint16_t{0xFFFF})));
}

struct TestDiv {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const auto v = Iota(d, -2);
    const auto v1 = Set(d, ConvertScalarTo<T>(1));

    // Unchanged after division by 1.
    HWY_ASSERT_VEC_EQ(d, v, Div(v, v1));

    const size_t N = Lanes(d);
    auto expected = AllocateAligned<T>(N);
    HWY_ASSERT(expected);
    for (size_t i = 0; i < N; ++i) {
      expected[i] = ConvertScalarTo<T>((static_cast<double>(i) - 2.0) / 2.0);
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), Div(v, Set(d, ConvertScalarTo<T>(2))));
  }
};

HWY_NOINLINE void TestAllDiv() { ForFloatTypes(ForPartialVectors<TestDiv>()); }

struct TestApproximateReciprocal {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const auto v = Iota(d, -2);
    const auto nonzero =
        IfThenElse(Eq(v, Zero(d)), Set(d, ConvertScalarTo<T>(1)), v);
    const size_t N = Lanes(d);
    auto input = AllocateAligned<T>(N);
    auto actual = AllocateAligned<T>(N);
    HWY_ASSERT(input && actual);

    Store(nonzero, d, input.get());
    Store(ApproximateReciprocal(nonzero), d, actual.get());

    double max_l1 = 0.0;
    double worst_expected = 0.0;
    double worst_actual = 0.0;
    for (size_t i = 0; i < N; ++i) {
      const double expected = 1.0 / input[i];
      const double l1 = ScalarAbs(expected - actual[i]);
      if (l1 > max_l1) {
        max_l1 = l1;
        worst_expected = expected;
        worst_actual = actual[i];
      }
    }
    const double abs_worst_expected = ScalarAbs(worst_expected);
    if (abs_worst_expected > 1E-5) {
      const double max_rel = max_l1 / abs_worst_expected;
      fprintf(stderr, "max l1 %f rel %f (%f vs %f)\n", max_l1, max_rel,
              worst_expected, worst_actual);
      HWY_ASSERT(max_rel < 0.004);
    }
  }
};

HWY_NOINLINE void TestAllApproximateReciprocal() {
  ForFloatTypes(ForPartialVectors<TestApproximateReciprocal>());
}

struct TestSquareRoot {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const auto vi = Iota(d, 0);
    HWY_ASSERT_VEC_EQ(d, vi, Sqrt(Mul(vi, vi)));
  }
};

HWY_NOINLINE void TestAllSquareRoot() {
  ForFloatTypes(ForPartialVectors<TestSquareRoot>());
}

struct TestReciprocalSquareRoot {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const Vec<D> v = Set(d, ConvertScalarTo<T>(123.0f));
    const size_t N = Lanes(d);
    auto lanes = AllocateAligned<T>(N);
    HWY_ASSERT(lanes);
    Store(ApproximateReciprocalSqrt(v), d, lanes.get());
    for (size_t i = 0; i < N; ++i) {
      T err = ConvertScalarTo<T>(ConvertScalarTo<float>(lanes[i]) - 0.090166f);
      if (err < ConvertScalarTo<T>(0)) err = -err;
      if (static_cast<double>(err) >= 4E-4) {
        HWY_ABORT("Lane %d (%d): actual %f err %f\n", static_cast<int>(i),
                  static_cast<int>(N), static_cast<double>(lanes[i]),
                  static_cast<double>(err));
      }
    }
  }
};

HWY_NOINLINE void TestAllReciprocalSquareRoot() {
  ForFloatTypes(ForPartialVectors<TestReciprocalSquareRoot>());
}

template <typename T, class D>
AlignedFreeUniquePtr<T[]> RoundTestCases(T /*unused*/, D d, size_t& padded) {
  const T eps = Epsilon<T>();
  const T huge = ConvertScalarTo<T>(sizeof(T) >= 4 ? 1E34 : 3E4);
  const T test_cases[] = {
      // +/- 1
      ConvertScalarTo<T>(1), ConvertScalarTo<T>(-1),
      // +/- 0
      ConvertScalarTo<T>(0), ConvertScalarTo<T>(-0),
      // near 0
      ConvertScalarTo<T>(0.4), ConvertScalarTo<T>(-0.4),
      // +/- integer
      ConvertScalarTo<T>(4), ConvertScalarTo<T>(-32),
      // positive near limit
      ConvertScalarTo<T>(MantissaEnd<T>() - ConvertScalarTo<T>(1.5)),
      ConvertScalarTo<T>(MantissaEnd<T>() + ConvertScalarTo<T>(1.5)),
      // negative near limit
      ConvertScalarTo<T>(-MantissaEnd<T>() - ConvertScalarTo<T>(1.5)),
      ConvertScalarTo<T>(-MantissaEnd<T>() + ConvertScalarTo<T>(1.5)),
      // positive tiebreak
      ConvertScalarTo<T>(1.5), ConvertScalarTo<T>(2.5),
      // negative tiebreak
      ConvertScalarTo<T>(-1.5), ConvertScalarTo<T>(-2.5),
      // positive +/- delta
      ConvertScalarTo<T>(2.0001), ConvertScalarTo<T>(3.9999),
      // negative +/- delta
      ConvertScalarTo<T>(-999.9999), ConvertScalarTo<T>(-998.0001),
      // positive +/- epsilon
      ConvertScalarTo<T>(ConvertScalarTo<T>(1) + eps),
      ConvertScalarTo<T>(ConvertScalarTo<T>(1) - eps),
      // negative +/- epsilon
      ConvertScalarTo<T>(ConvertScalarTo<T>(-1) + eps),
      ConvertScalarTo<T>(ConvertScalarTo<T>(-1) - eps),
      // +/- huge (but still fits in float)
      huge, -huge,
      // +/- infinity
      GetLane(Inf(d)), GetLane(Neg(Inf(d))),
      // qNaN
      GetLane(NaN(d))};
  const size_t kNumTestCases = sizeof(test_cases) / sizeof(test_cases[0]);
  const size_t N = Lanes(d);
  padded = RoundUpTo(kNumTestCases, N);  // allow loading whole vectors
  auto in = AllocateAligned<T>(padded);
  auto expected = AllocateAligned<T>(padded);
  HWY_ASSERT(in && expected);
  CopyBytes(test_cases, in.get(), kNumTestCases * sizeof(T));
  ZeroBytes(in.get() + kNumTestCases, (padded - kNumTestCases) * sizeof(T));
  return in;
}

struct TestRound {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T t, D d) {
    size_t padded;
    auto in = RoundTestCases(t, d, padded);
    auto expected = AllocateAligned<T>(padded);
    HWY_ASSERT(expected);

    for (size_t i = 0; i < padded; ++i) {
// Avoid [std::]round, which does not round to nearest *even*.
// NOTE: std:: version from C++11 cmath is not defined in RVV GCC, see
// https://lists.freebsd.org/pipermail/freebsd-current/2014-January/048130.html
// Cast to f32/64 because nearbyint does not support _Float16.
#if HWY_HAVE_FLOAT64
      const double f = ConvertScalarTo<double>(in[i]);
#else
      const float f = ConvertScalarTo<float>(in[i]);
#endif
      expected[i] = ConvertScalarTo<T>(nearbyint(f));
    }
    for (size_t i = 0; i < padded; i += Lanes(d)) {
      HWY_ASSERT_VEC_EQ(d, &expected[i], Round(Load(d, &in[i])));
    }
  }
};

HWY_NOINLINE void TestAllRound() {
  ForFloatTypes(ForPartialVectors<TestRound>());
}

struct TestNearestInt {
  static HWY_INLINE int16_t RoundScalarFloatToInt(float16_t f) {
    return static_cast<int16_t>(std::lrintf(ConvertScalarTo<float>(f)));
  }

  static HWY_INLINE int32_t RoundScalarFloatToInt(float f) {
    return static_cast<int32_t>(std::lrintf(f));
  }

  static HWY_INLINE int64_t RoundScalarFloatToInt(double f) {
    return static_cast<int64_t>(std::llrint(f));
  }

  template <typename TF, class DF>
  HWY_NOINLINE void operator()(TF tf, const DF df) {
    using TI = MakeSigned<TF>;
    const RebindToSigned<DF> di;

    size_t padded;
    auto in = RoundTestCases(tf, df, padded);
    auto expected = AllocateAligned<TI>(padded);
    HWY_ASSERT(expected);

    constexpr double kMax = static_cast<double>(LimitsMax<TI>());
    for (size_t i = 0; i < padded; ++i) {
      if (ScalarIsNaN(in[i])) {
        // We replace NaN with 0 below (no_nan)
        expected[i] = 0;
      } else if (ScalarIsInf(in[i]) ||
                 ConvertScalarTo<double>(ScalarAbs(in[i])) >= kMax) {
        // Avoid undefined result for std::lrintf or std::llrint
        expected[i] = ScalarSignBit(in[i]) ? LimitsMin<TI>() : LimitsMax<TI>();
      } else {
        expected[i] = RoundScalarFloatToInt(in[i]);
      }
    }
    for (size_t i = 0; i < padded; i += Lanes(df)) {
      const auto v = Load(df, &in[i]);
      const auto no_nan = IfThenElse(Eq(v, v), v, Zero(df));
      HWY_ASSERT_VEC_EQ(di, &expected[i], NearestInt(no_nan));
    }
  }
};

HWY_NOINLINE void TestAllNearestInt() {
  ForFloatTypes(ForPartialVectors<TestNearestInt>());
}

struct TestDemoteToNearestInt {
  template <typename TF, class DF>
  HWY_NOINLINE void operator()(TF tf, const DF df) {
    using TI = MakeNarrow<MakeSigned<TF>>;
    const Rebind<TI, DF> di;

    size_t padded;
    auto in = RoundTestCases(tf, df, padded);
    auto expected = AllocateAligned<TI>(padded);
    HWY_ASSERT(expected);

    constexpr double kMax = static_cast<double>(LimitsMax<TI>());
    for (size_t i = 0; i < padded; ++i) {
      if (ScalarIsNaN(in[i])) {
        // We replace NaN with 0 below (no_nan)
        expected[i] = 0;
      } else if (ScalarIsInf(in[i]) ||
                 static_cast<double>(ScalarAbs(in[i])) >= kMax) {
        // Avoid undefined result for std::lrint
        expected[i] = ScalarSignBit(in[i]) ? LimitsMin<TI>() : LimitsMax<TI>();
      } else {
        expected[i] =
            static_cast<TI>(std::lrint(ConvertScalarTo<double>(in[i])));
      }
    }
    for (size_t i = 0; i < padded; i += Lanes(df)) {
      const auto v = Load(df, &in[i]);
      const auto no_nan = IfThenElse(Eq(v, v), v, Zero(df));
      HWY_ASSERT_VEC_EQ(di, &expected[i], DemoteToNearestInt(di, no_nan));
    }
  }
};

HWY_NOINLINE void TestAllDemoteToNearestInt() {
#if HWY_HAVE_FLOAT64
  ForDemoteVectors<TestDemoteToNearestInt>()(double());
#endif
}

struct TestTrunc {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T t, D d) {
    size_t padded;
    auto in = RoundTestCases(t, d, padded);
    auto expected = AllocateAligned<T>(padded);
    HWY_ASSERT(expected);

    for (size_t i = 0; i < padded; ++i) {
      // NOTE: std:: version from C++11 cmath is not defined in RVV GCC, see
      // https://lists.freebsd.org/pipermail/freebsd-current/2014-January/048130.html
      // Cast to double because trunc does not support _Float16.
      expected[i] = ConvertScalarTo<T>(trunc(ConvertScalarTo<double>(in[i])));
    }
    for (size_t i = 0; i < padded; i += Lanes(d)) {
      HWY_ASSERT_VEC_EQ(d, &expected[i], Trunc(Load(d, &in[i])));
    }
  }
};

HWY_NOINLINE void TestAllTrunc() {
  ForFloatTypes(ForPartialVectors<TestTrunc>());
}

struct TestCeil {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T t, D d) {
    const RebindToSigned<decltype(d)> di;
    using TI = MakeSigned<T>;

    size_t padded;
    auto in = RoundTestCases(t, d, padded);
    auto expected = AllocateAligned<T>(padded);
    auto expected_int = AllocateAligned<TI>(padded);
    HWY_ASSERT(expected && expected_int);

    constexpr double kMinOutOfRangeVal = -static_cast<double>(LimitsMin<TI>());
    static_assert(kMinOutOfRangeVal > 0.0,
                  "kMinOutOfRangeVal > 0.0 must be true");

    for (size_t i = 0; i < padded; ++i) {
      // Cast to double because ceil does not support _Float16.
      const double ceil_val = std::ceil(ConvertScalarTo<double>(in[i]));
      expected[i] = ConvertScalarTo<T>(ceil_val);
      if (ScalarIsNaN(ceil_val)) {
        expected_int[i] = 0;
      } else if (ScalarIsInf(ceil_val) || static_cast<double>(ScalarAbs(
                                              ceil_val)) >= kMinOutOfRangeVal) {
        expected_int[i] =
            ScalarSignBit(ceil_val) ? LimitsMin<TI>() : LimitsMax<TI>();
      } else {
        expected_int[i] = ConvertScalarTo<TI>(ceil_val);
      }
    }
    for (size_t i = 0; i < padded; i += Lanes(d)) {
      const auto v = Load(d, &in[i]);
      HWY_ASSERT_VEC_EQ(d, &expected[i], Ceil(v));
      HWY_ASSERT_VEC_EQ(di, &expected_int[i],
                        IfThenZeroElse(RebindMask(di, IsNaN(v)), CeilInt(v)));
    }
  }
};

HWY_NOINLINE void TestAllCeil() {
  ForFloatTypes(ForPartialVectors<TestCeil>());
}

struct TestFloor {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T t, D d) {
    const RebindToSigned<decltype(d)> di;
    using TI = MakeSigned<T>;

    size_t padded;
    auto in = RoundTestCases(t, d, padded);
    auto expected = AllocateAligned<T>(padded);
    auto expected_int = AllocateAligned<TI>(padded);
    HWY_ASSERT(expected && expected_int);

    constexpr double kMinOutOfRangeVal = -static_cast<double>(LimitsMin<TI>());
    static_assert(kMinOutOfRangeVal > 0.0,
                  "kMinOutOfRangeVal > 0.0 must be true");

    for (size_t i = 0; i < padded; ++i) {
      // Cast to double because floor does not support _Float16.
      const double floor_val = std::floor(ConvertScalarTo<double>(in[i]));
      expected[i] = ConvertScalarTo<T>(floor_val);
      if (ScalarIsNaN(floor_val)) {
        expected_int[i] = 0;
      } else if (ScalarIsInf(floor_val) ||
                 static_cast<double>(ScalarAbs(floor_val)) >=
                     kMinOutOfRangeVal) {
        expected_int[i] =
            ScalarSignBit(floor_val) ? LimitsMin<TI>() : LimitsMax<TI>();
      } else {
        expected_int[i] = ConvertScalarTo<TI>(floor_val);
      }
    }
    for (size_t i = 0; i < padded; i += Lanes(d)) {
      const auto v = Load(d, &in[i]);
      HWY_ASSERT_VEC_EQ(d, &expected[i], Floor(v));
      HWY_ASSERT_VEC_EQ(di, &expected_int[i],
                        IfThenZeroElse(RebindMask(di, IsNaN(v)), FloorInt(v)));
    }
  }
};

HWY_NOINLINE void TestAllFloor() {
  ForFloatTypes(ForPartialVectors<TestFloor>());
}

struct TestAbsDiff {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const size_t N = Lanes(d);
    auto in_lanes_a = AllocateAligned<T>(N);
    auto in_lanes_b = AllocateAligned<T>(N);
    auto out_lanes = AllocateAligned<T>(N);
    HWY_ASSERT(in_lanes_a && in_lanes_b && out_lanes);
    for (size_t i = 0; i < N; ++i) {
      in_lanes_a[i] = ConvertScalarTo<T>((i ^ 1u) << i);
      in_lanes_b[i] = ConvertScalarTo<T>(i << i);
      out_lanes[i] = ConvertScalarTo<T>(
          ScalarAbs(ConvertScalarTo<T>(in_lanes_a[i] - in_lanes_b[i])));
    }
    const auto a = Load(d, in_lanes_a.get());
    const auto b = Load(d, in_lanes_b.get());
    const auto expected = Load(d, out_lanes.get());
    HWY_ASSERT_VEC_EQ(d, expected, AbsDiff(a, b));
    HWY_ASSERT_VEC_EQ(d, expected, AbsDiff(b, a));
  }
};

HWY_NOINLINE void TestAllAbsDiff() {
  ForFloatTypes(ForPartialVectors<TestAbsDiff>());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyFloatTest);
HWY_EXPORT_AND_TEST_P(HwyFloatTest, TestAllF16FromF32);
HWY_EXPORT_AND_TEST_P(HwyFloatTest, TestAllF32FromF16);
HWY_EXPORT_AND_TEST_P(HwyFloatTest, TestAllDiv);
HWY_EXPORT_AND_TEST_P(HwyFloatTest, TestAllApproximateReciprocal);
HWY_EXPORT_AND_TEST_P(HwyFloatTest, TestAllSquareRoot);
HWY_EXPORT_AND_TEST_P(HwyFloatTest, TestAllReciprocalSquareRoot);
HWY_EXPORT_AND_TEST_P(HwyFloatTest, TestAllRound);
HWY_EXPORT_AND_TEST_P(HwyFloatTest, TestAllNearestInt);
HWY_EXPORT_AND_TEST_P(HwyFloatTest, TestAllDemoteToNearestInt);
HWY_EXPORT_AND_TEST_P(HwyFloatTest, TestAllTrunc);
HWY_EXPORT_AND_TEST_P(HwyFloatTest, TestAllCeil);
HWY_EXPORT_AND_TEST_P(HwyFloatTest, TestAllFloor);
HWY_EXPORT_AND_TEST_P(HwyFloatTest, TestAllAbsDiff);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
