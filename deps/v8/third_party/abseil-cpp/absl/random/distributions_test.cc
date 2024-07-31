// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/random/distributions.h"

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include "gtest/gtest.h"
#include "absl/random/internal/distribution_test_util.h"
#include "absl/random/random.h"

namespace {

constexpr int kSize = 400000;

class RandomDistributionsTest : public testing::Test {};


struct Invalid {};

template <typename A, typename B>
auto InferredUniformReturnT(int)
    -> decltype(absl::Uniform(std::declval<absl::InsecureBitGen&>(),
                              std::declval<A>(), std::declval<B>()));

template <typename, typename>
Invalid InferredUniformReturnT(...);

template <typename TagType, typename A, typename B>
auto InferredTaggedUniformReturnT(int)
    -> decltype(absl::Uniform(std::declval<TagType>(),
                              std::declval<absl::InsecureBitGen&>(),
                              std::declval<A>(), std::declval<B>()));

template <typename, typename, typename>
Invalid InferredTaggedUniformReturnT(...);

// Given types <A, B, Expect>, CheckArgsInferType() verifies that
//
//   absl::Uniform(gen, A{}, B{})
//
// returns the type "Expect".
//
// This interface can also be used to assert that a given absl::Uniform()
// overload does not exist / will not compile. Given types <A, B>, the
// expression
//
//   decltype(absl::Uniform(..., std::declval<A>(), std::declval<B>()))
//
// will not compile, leaving the definition of InferredUniformReturnT<A, B> to
// resolve (via SFINAE) to the overload which returns type "Invalid". This
// allows tests to assert that an invocation such as
//
//   absl::Uniform(gen, 1.23f, std::numeric_limits<int>::max() - 1)
//
// should not compile, since neither type, float nor int, can precisely
// represent both endpoint-values. Writing:
//
//   CheckArgsInferType<float, int, Invalid>()
//
// will assert that this overload does not exist.
template <typename A, typename B, typename Expect>
void CheckArgsInferType() {
  static_assert(
      absl::conjunction<
          std::is_same<Expect, decltype(InferredUniformReturnT<A, B>(0))>,
          std::is_same<Expect,
                       decltype(InferredUniformReturnT<B, A>(0))>>::value,
      "");
  static_assert(
      absl::conjunction<
          std::is_same<Expect, decltype(InferredTaggedUniformReturnT<
                                        absl::IntervalOpenOpenTag, A, B>(0))>,
          std::is_same<Expect,
                       decltype(InferredTaggedUniformReturnT<
                                absl::IntervalOpenOpenTag, B, A>(0))>>::value,
      "");
}

template <typename A, typename B, typename ExplicitRet>
auto ExplicitUniformReturnT(int) -> decltype(
    absl::Uniform<ExplicitRet>(*std::declval<absl::InsecureBitGen*>(),
                               std::declval<A>(), std::declval<B>()));

template <typename, typename, typename ExplicitRet>
Invalid ExplicitUniformReturnT(...);

template <typename TagType, typename A, typename B, typename ExplicitRet>
auto ExplicitTaggedUniformReturnT(int) -> decltype(absl::Uniform<ExplicitRet>(
    std::declval<TagType>(), *std::declval<absl::InsecureBitGen*>(),
    std::declval<A>(), std::declval<B>()));

template <typename, typename, typename, typename ExplicitRet>
Invalid ExplicitTaggedUniformReturnT(...);

// Given types <A, B, Expect>, CheckArgsReturnExpectedType() verifies that
//
//   absl::Uniform<Expect>(gen, A{}, B{})
//
// returns the type "Expect", and that the function-overload has the signature
//
//   Expect(URBG&, Expect, Expect)
template <typename A, typename B, typename Expect>
void CheckArgsReturnExpectedType() {
  static_assert(
      absl::conjunction<
          std::is_same<Expect,
                       decltype(ExplicitUniformReturnT<A, B, Expect>(0))>,
          std::is_same<Expect, decltype(ExplicitUniformReturnT<B, A, Expect>(
                                   0))>>::value,
      "");
  static_assert(
      absl::conjunction<
          std::is_same<Expect,
                       decltype(ExplicitTaggedUniformReturnT<
                                absl::IntervalOpenOpenTag, A, B, Expect>(0))>,
          std::is_same<Expect, decltype(ExplicitTaggedUniformReturnT<
                                        absl::IntervalOpenOpenTag, B, A,
                                        Expect>(0))>>::value,
      "");
}

TEST_F(RandomDistributionsTest, UniformTypeInference) {
  // Infers common types.
  CheckArgsInferType<uint16_t, uint16_t, uint16_t>();
  CheckArgsInferType<uint32_t, uint32_t, uint32_t>();
  CheckArgsInferType<uint64_t, uint64_t, uint64_t>();
  CheckArgsInferType<int16_t, int16_t, int16_t>();
  CheckArgsInferType<int32_t, int32_t, int32_t>();
  CheckArgsInferType<int64_t, int64_t, int64_t>();
  CheckArgsInferType<float, float, float>();
  CheckArgsInferType<double, double, double>();

  // Explicitly-specified return-values override inferences.
  CheckArgsReturnExpectedType<int16_t, int16_t, int32_t>();
  CheckArgsReturnExpectedType<uint16_t, uint16_t, int32_t>();
  CheckArgsReturnExpectedType<int16_t, int16_t, int64_t>();
  CheckArgsReturnExpectedType<int16_t, int32_t, int64_t>();
  CheckArgsReturnExpectedType<int16_t, int32_t, double>();
  CheckArgsReturnExpectedType<float, float, double>();
  CheckArgsReturnExpectedType<int, int, int16_t>();

  // Properly promotes uint16_t.
  CheckArgsInferType<uint16_t, uint32_t, uint32_t>();
  CheckArgsInferType<uint16_t, uint64_t, uint64_t>();
  CheckArgsInferType<uint16_t, int32_t, int32_t>();
  CheckArgsInferType<uint16_t, int64_t, int64_t>();
  CheckArgsInferType<uint16_t, float, float>();
  CheckArgsInferType<uint16_t, double, double>();

  // Properly promotes int16_t.
  CheckArgsInferType<int16_t, int32_t, int32_t>();
  CheckArgsInferType<int16_t, int64_t, int64_t>();
  CheckArgsInferType<int16_t, float, float>();
  CheckArgsInferType<int16_t, double, double>();

  // Invalid (u)int16_t-pairings do not compile.
  // See "CheckArgsInferType" comments above, for how this is achieved.
  CheckArgsInferType<uint16_t, int16_t, Invalid>();
  CheckArgsInferType<int16_t, uint32_t, Invalid>();
  CheckArgsInferType<int16_t, uint64_t, Invalid>();

  // Properly promotes uint32_t.
  CheckArgsInferType<uint32_t, uint64_t, uint64_t>();
  CheckArgsInferType<uint32_t, int64_t, int64_t>();
  CheckArgsInferType<uint32_t, double, double>();

  // Properly promotes int32_t.
  CheckArgsInferType<int32_t, int64_t, int64_t>();
  CheckArgsInferType<int32_t, double, double>();

  // Invalid (u)int32_t-pairings do not compile.
  CheckArgsInferType<uint32_t, int32_t, Invalid>();
  CheckArgsInferType<int32_t, uint64_t, Invalid>();
  CheckArgsInferType<int32_t, float, Invalid>();
  CheckArgsInferType<uint32_t, float, Invalid>();

  // Invalid (u)int64_t-pairings do not compile.
  CheckArgsInferType<uint64_t, int64_t, Invalid>();
  CheckArgsInferType<int64_t, float, Invalid>();
  CheckArgsInferType<int64_t, double, Invalid>();

  // Properly promotes float.
  CheckArgsInferType<float, double, double>();
}

TEST_F(RandomDistributionsTest, UniformExamples) {
  // Examples.
  absl::InsecureBitGen gen;
  EXPECT_NE(1, absl::Uniform(gen, static_cast<uint16_t>(0), 1.0f));
  EXPECT_NE(1, absl::Uniform(gen, 0, 1.0));
  EXPECT_NE(1, absl::Uniform(absl::IntervalOpenOpen, gen,
                             static_cast<uint16_t>(0), 1.0f));
  EXPECT_NE(1, absl::Uniform(absl::IntervalOpenOpen, gen, 0, 1.0));
  EXPECT_NE(1, absl::Uniform(absl::IntervalOpenOpen, gen, -1, 1.0));
  EXPECT_NE(1, absl::Uniform<double>(absl::IntervalOpenOpen, gen, -1, 1));
  EXPECT_NE(1, absl::Uniform<float>(absl::IntervalOpenOpen, gen, 0, 1));
  EXPECT_NE(1, absl::Uniform<float>(gen, 0, 1));
}

TEST_F(RandomDistributionsTest, UniformNoBounds) {
  absl::InsecureBitGen gen;

  absl::Uniform<uint8_t>(gen);
  absl::Uniform<uint16_t>(gen);
  absl::Uniform<uint32_t>(gen);
  absl::Uniform<uint64_t>(gen);
  absl::Uniform<absl::uint128>(gen);
}

TEST_F(RandomDistributionsTest, UniformNonsenseRanges) {
  // The ranges used in this test are undefined behavior.
  // The results are arbitrary and subject to future changes.

#if (defined(__i386__) || defined(_M_IX86)) && FLT_EVAL_METHOD != 0
  // We're using an x87-compatible FPU, and intermediate operations can be
  // performed with 80-bit floats. This produces slightly different results from
  // what we expect below.
  GTEST_SKIP()
      << "Skipping the test because we detected x87 floating-point semantics";
#endif

  absl::InsecureBitGen gen;

  // <uint>
  EXPECT_EQ(0, absl::Uniform<uint64_t>(gen, 0, 0));
  EXPECT_EQ(1, absl::Uniform<uint64_t>(gen, 1, 0));
  EXPECT_EQ(0, absl::Uniform<uint64_t>(absl::IntervalOpenOpen, gen, 0, 0));
  EXPECT_EQ(1, absl::Uniform<uint64_t>(absl::IntervalOpenOpen, gen, 1, 0));

  constexpr auto m = (std::numeric_limits<uint64_t>::max)();

  EXPECT_EQ(m, absl::Uniform(gen, m, m));
  EXPECT_EQ(m, absl::Uniform(gen, m, m - 1));
  EXPECT_EQ(m - 1, absl::Uniform(gen, m - 1, m));
  EXPECT_EQ(m, absl::Uniform(absl::IntervalOpenOpen, gen, m, m));
  EXPECT_EQ(m, absl::Uniform(absl::IntervalOpenOpen, gen, m, m - 1));
  EXPECT_EQ(m - 1, absl::Uniform(absl::IntervalOpenOpen, gen, m - 1, m));

  // <int>
  EXPECT_EQ(0, absl::Uniform<int64_t>(gen, 0, 0));
  EXPECT_EQ(1, absl::Uniform<int64_t>(gen, 1, 0));
  EXPECT_EQ(0, absl::Uniform<int64_t>(absl::IntervalOpenOpen, gen, 0, 0));
  EXPECT_EQ(1, absl::Uniform<int64_t>(absl::IntervalOpenOpen, gen, 1, 0));

  constexpr auto l = (std::numeric_limits<int64_t>::min)();
  constexpr auto r = (std::numeric_limits<int64_t>::max)();

  EXPECT_EQ(l, absl::Uniform(gen, l, l));
  EXPECT_EQ(r, absl::Uniform(gen, r, r));
  EXPECT_EQ(r, absl::Uniform(gen, r, r - 1));
  EXPECT_EQ(r - 1, absl::Uniform(gen, r - 1, r));
  EXPECT_EQ(l, absl::Uniform(absl::IntervalOpenOpen, gen, l, l));
  EXPECT_EQ(r, absl::Uniform(absl::IntervalOpenOpen, gen, r, r));
  EXPECT_EQ(r, absl::Uniform(absl::IntervalOpenOpen, gen, r, r - 1));
  EXPECT_EQ(r - 1, absl::Uniform(absl::IntervalOpenOpen, gen, r - 1, r));

  // <double>
  const double e = std::nextafter(1.0, 2.0);  // 1 + epsilon
  const double f = std::nextafter(1.0, 0.0);  // 1 - epsilon
  const double g = std::numeric_limits<double>::denorm_min();

  EXPECT_EQ(1.0, absl::Uniform(gen, 1.0, e));
  EXPECT_EQ(1.0, absl::Uniform(gen, 1.0, f));
  EXPECT_EQ(0.0, absl::Uniform(gen, 0.0, g));

  EXPECT_EQ(e, absl::Uniform(absl::IntervalOpenOpen, gen, 1.0, e));
  EXPECT_EQ(f, absl::Uniform(absl::IntervalOpenOpen, gen, 1.0, f));
  EXPECT_EQ(g, absl::Uniform(absl::IntervalOpenOpen, gen, 0.0, g));
}

// TODO(lar): Validate properties of non-default interval-semantics.
TEST_F(RandomDistributionsTest, UniformReal) {
  std::vector<double> values(kSize);

  absl::InsecureBitGen gen;
  for (int i = 0; i < kSize; i++) {
    values[i] = absl::Uniform(gen, 0, 1.0);
  }

  const auto moments =
      absl::random_internal::ComputeDistributionMoments(values);
  EXPECT_NEAR(0.5, moments.mean, 0.02);
  EXPECT_NEAR(1 / 12.0, moments.variance, 0.02);
  EXPECT_NEAR(0.0, moments.skewness, 0.02);
  EXPECT_NEAR(9 / 5.0, moments.kurtosis, 0.02);
}

TEST_F(RandomDistributionsTest, UniformInt) {
  std::vector<double> values(kSize);

  absl::InsecureBitGen gen;
  for (int i = 0; i < kSize; i++) {
    const int64_t kMax = 1000000000000ll;
    int64_t j = absl::Uniform(absl::IntervalClosedClosed, gen, 0, kMax);
    // convert to double.
    values[i] = static_cast<double>(j) / static_cast<double>(kMax);
  }

  const auto moments =
      absl::random_internal::ComputeDistributionMoments(values);
  EXPECT_NEAR(0.5, moments.mean, 0.02);
  EXPECT_NEAR(1 / 12.0, moments.variance, 0.02);
  EXPECT_NEAR(0.0, moments.skewness, 0.02);
  EXPECT_NEAR(9 / 5.0, moments.kurtosis, 0.02);

  /*
  // NOTE: These are not supported by absl::Uniform, which is specialized
  // on integer and real valued types.

  enum E { E0, E1 };    // enum
  enum S : int { S0, S1 };    // signed enum
  enum U : unsigned int { U0, U1 };  // unsigned enum

  absl::Uniform(gen, E0, E1);
  absl::Uniform(gen, S0, S1);
  absl::Uniform(gen, U0, U1);
  */
}

TEST_F(RandomDistributionsTest, Exponential) {
  std::vector<double> values(kSize);

  absl::InsecureBitGen gen;
  for (int i = 0; i < kSize; i++) {
    values[i] = absl::Exponential<double>(gen);
  }

  const auto moments =
      absl::random_internal::ComputeDistributionMoments(values);
  EXPECT_NEAR(1.0, moments.mean, 0.02);
  EXPECT_NEAR(1.0, moments.variance, 0.025);
  EXPECT_NEAR(2.0, moments.skewness, 0.1);
  EXPECT_LT(5.0, moments.kurtosis);
}

TEST_F(RandomDistributionsTest, PoissonDefault) {
  std::vector<double> values(kSize);

  absl::InsecureBitGen gen;
  for (int i = 0; i < kSize; i++) {
    values[i] = absl::Poisson<int64_t>(gen);
  }

  const auto moments =
      absl::random_internal::ComputeDistributionMoments(values);
  EXPECT_NEAR(1.0, moments.mean, 0.02);
  EXPECT_NEAR(1.0, moments.variance, 0.02);
  EXPECT_NEAR(1.0, moments.skewness, 0.025);
  EXPECT_LT(2.0, moments.kurtosis);
}

TEST_F(RandomDistributionsTest, PoissonLarge) {
  constexpr double kMean = 100000000.0;
  std::vector<double> values(kSize);

  absl::InsecureBitGen gen;
  for (int i = 0; i < kSize; i++) {
    values[i] = absl::Poisson<int64_t>(gen, kMean);
  }

  const auto moments =
      absl::random_internal::ComputeDistributionMoments(values);
  EXPECT_NEAR(kMean, moments.mean, kMean * 0.015);
  EXPECT_NEAR(kMean, moments.variance, kMean * 0.015);
  EXPECT_NEAR(std::sqrt(kMean), moments.skewness, kMean * 0.02);
  EXPECT_LT(2.0, moments.kurtosis);
}

TEST_F(RandomDistributionsTest, Bernoulli) {
  constexpr double kP = 0.5151515151;
  std::vector<double> values(kSize);

  absl::InsecureBitGen gen;
  for (int i = 0; i < kSize; i++) {
    values[i] = absl::Bernoulli(gen, kP);
  }

  const auto moments =
      absl::random_internal::ComputeDistributionMoments(values);
  EXPECT_NEAR(kP, moments.mean, 0.01);
}

TEST_F(RandomDistributionsTest, Beta) {
  constexpr double kAlpha = 2.0;
  constexpr double kBeta = 3.0;
  std::vector<double> values(kSize);

  absl::InsecureBitGen gen;
  for (int i = 0; i < kSize; i++) {
    values[i] = absl::Beta(gen, kAlpha, kBeta);
  }

  const auto moments =
      absl::random_internal::ComputeDistributionMoments(values);
  EXPECT_NEAR(0.4, moments.mean, 0.01);
}

TEST_F(RandomDistributionsTest, Zipf) {
  std::vector<double> values(kSize);

  absl::InsecureBitGen gen;
  for (int i = 0; i < kSize; i++) {
    values[i] = absl::Zipf<int64_t>(gen, 100);
  }

  // The mean of a zipf distribution is: H(N, s-1) / H(N,s).
  // Given the parameter v = 1, this gives the following function:
  // (Hn(100, 1) - Hn(1,1)) / (Hn(100,2) - Hn(1,2)) = 6.5944
  const auto moments =
      absl::random_internal::ComputeDistributionMoments(values);
  EXPECT_NEAR(6.5944, moments.mean, 2000) << moments;
}

TEST_F(RandomDistributionsTest, Gaussian) {
  std::vector<double> values(kSize);

  absl::InsecureBitGen gen;
  for (int i = 0; i < kSize; i++) {
    values[i] = absl::Gaussian<double>(gen);
  }

  const auto moments =
      absl::random_internal::ComputeDistributionMoments(values);
  EXPECT_NEAR(0.0, moments.mean, 0.02);
  EXPECT_NEAR(1.0, moments.variance, 0.04);
  EXPECT_NEAR(0, moments.skewness, 0.2);
  EXPECT_NEAR(3.0, moments.kurtosis, 0.5);
}

TEST_F(RandomDistributionsTest, LogUniform) {
  std::vector<double> values(kSize);

  absl::InsecureBitGen gen;
  for (int i = 0; i < kSize; i++) {
    values[i] = absl::LogUniform<int64_t>(gen, 0, (1 << 10) - 1);
  }

  // The mean is the sum of the fractional means of the uniform distributions:
  // [0..0][1..1][2..3][4..7][8..15][16..31][32..63]
  // [64..127][128..255][256..511][512..1023]
  const double mean = (0 + 1 + 1 + 2 + 3 + 4 + 7 + 8 + 15 + 16 + 31 + 32 + 63 +
                       64 + 127 + 128 + 255 + 256 + 511 + 512 + 1023) /
                      (2.0 * 11.0);

  const auto moments =
      absl::random_internal::ComputeDistributionMoments(values);
  EXPECT_NEAR(mean, moments.mean, 2) << moments;
}

}  // namespace
