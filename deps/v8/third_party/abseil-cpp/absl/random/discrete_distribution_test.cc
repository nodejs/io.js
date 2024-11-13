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

#include "absl/random/discrete_distribution.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/random/internal/chi_square.h"
#include "absl/random/internal/distribution_test_util.h"
#include "absl/random/internal/pcg_engine.h"
#include "absl/random/internal/sequence_urbg.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/strip.h"

namespace {

template <typename IntType>
class DiscreteDistributionTypeTest : public ::testing::Test {};

using IntTypes = ::testing::Types<int8_t, uint8_t, int16_t, uint16_t, int32_t,
                                  uint32_t, int64_t, uint64_t>;
TYPED_TEST_SUITE(DiscreteDistributionTypeTest, IntTypes);

TYPED_TEST(DiscreteDistributionTypeTest, ParamSerializeTest) {
  using param_type =
      typename absl::discrete_distribution<TypeParam>::param_type;

  absl::discrete_distribution<TypeParam> empty;
  EXPECT_THAT(empty.probabilities(), testing::ElementsAre(1.0));

  absl::discrete_distribution<TypeParam> before({1.0, 2.0, 1.0});

  // Validate that the probabilities sum to 1.0. We picked values which
  // can be represented exactly to avoid floating-point roundoff error.
  double s = 0;
  for (const auto& x : before.probabilities()) {
    s += x;
  }
  EXPECT_EQ(s, 1.0);
  EXPECT_THAT(before.probabilities(), testing::ElementsAre(0.25, 0.5, 0.25));

  // Validate the same data via an initializer list.
  {
    std::vector<double> data({1.0, 2.0, 1.0});

    absl::discrete_distribution<TypeParam> via_param{
        param_type(std::begin(data), std::end(data))};

    EXPECT_EQ(via_param, before);
  }

  std::stringstream ss;
  ss << before;
  absl::discrete_distribution<TypeParam> after;

  EXPECT_NE(before, after);

  ss >> after;

  EXPECT_EQ(before, after);
}

TYPED_TEST(DiscreteDistributionTypeTest, Constructor) {
  auto fn = [](double x) { return x; };
  {
    absl::discrete_distribution<int> unary(0, 1.0, 9.0, fn);
    EXPECT_THAT(unary.probabilities(), testing::ElementsAre(1.0));
  }

  {
    absl::discrete_distribution<int> unary(2, 1.0, 9.0, fn);
    // => fn(1.0 + 0 * 4 + 2) => 3
    // => fn(1.0 + 1 * 4 + 2) => 7
    EXPECT_THAT(unary.probabilities(), testing::ElementsAre(0.3, 0.7));
  }
}

TEST(DiscreteDistributionTest, InitDiscreteDistribution) {
  using testing::_;
  using testing::Pair;

  {
    std::vector<double> p({1.0, 2.0, 3.0});
    std::vector<std::pair<double, size_t>> q =
        absl::random_internal::InitDiscreteDistribution(&p);

    EXPECT_THAT(p, testing::ElementsAre(1 / 6.0, 2 / 6.0, 3 / 6.0));

    // Each bucket is p=1/3, so bucket 0 will send half it's traffic
    // to bucket 2, while the rest will retain all of their traffic.
    EXPECT_THAT(q, testing::ElementsAre(Pair(0.5, 2),  //
                                        Pair(1.0, _),  //
                                        Pair(1.0, _)));
  }

  {
    std::vector<double> p({1.0, 2.0, 3.0, 5.0, 2.0});

    std::vector<std::pair<double, size_t>> q =
        absl::random_internal::InitDiscreteDistribution(&p);

    EXPECT_THAT(p, testing::ElementsAre(1 / 13.0, 2 / 13.0, 3 / 13.0, 5 / 13.0,
                                        2 / 13.0));

    // A more complex bucketing solution: Each bucket has p=0.2
    // So buckets 0, 1, 4 will send their alternate traffic elsewhere, which
    // happens to be bucket 3.
    // However, summing up that alternate traffic gives bucket 3 too much
    // traffic, so it will send some traffic to bucket 2.
    constexpr double b0 = 1.0 / 13.0 / 0.2;
    constexpr double b1 = 2.0 / 13.0 / 0.2;
    constexpr double b3 = (5.0 / 13.0 / 0.2) - ((1 - b0) + (1 - b1) + (1 - b1));

    EXPECT_THAT(q, testing::ElementsAre(Pair(b0, 3),   //
                                        Pair(b1, 3),   //
                                        Pair(1.0, _),  //
                                        Pair(b3, 2),   //
                                        Pair(b1, 3)));
  }
}

TEST(DiscreteDistributionTest, ChiSquaredTest50) {
  using absl::random_internal::kChiSquared;

  constexpr size_t kTrials = 10000;
  constexpr int kBuckets = 50;  // inclusive, so actually +1

  // 1-in-100000 threshold, but remember, there are about 8 tests
  // in this file. And the test could fail for other reasons.
  // Empirically validated with --runs_per_test=10000.
  const int kThreshold =
      absl::random_internal::ChiSquareValue(kBuckets, 0.99999);

  std::vector<double> weights(kBuckets, 0);
  std::iota(std::begin(weights), std::end(weights), 1);
  absl::discrete_distribution<int> dist(std::begin(weights), std::end(weights));

  // We use a fixed bit generator for distribution accuracy tests.  This allows
  // these tests to be deterministic, while still testing the qualify of the
  // implementation.
  absl::random_internal::pcg64_2018_engine rng(0x2B7E151628AED2A6);

  std::vector<int32_t> counts(kBuckets, 0);
  for (size_t i = 0; i < kTrials; i++) {
    auto x = dist(rng);
    counts[x]++;
  }

  // Scale weights.
  double sum = 0;
  for (double x : weights) {
    sum += x;
  }
  for (double& x : weights) {
    x = kTrials * (x / sum);
  }

  double chi_square =
      absl::random_internal::ChiSquare(std::begin(counts), std::end(counts),
                                       std::begin(weights), std::end(weights));

  if (chi_square > kThreshold) {
    double p_value =
        absl::random_internal::ChiSquarePValue(chi_square, kBuckets);

    // Chi-squared test failed. Output does not appear to be uniform.
    std::string msg;
    for (size_t i = 0; i < counts.size(); i++) {
      absl::StrAppend(&msg, i, ": ", counts[i], " vs ", weights[i], "\n");
    }
    absl::StrAppend(&msg, kChiSquared, " p-value ", p_value, "\n");
    absl::StrAppend(&msg, "High ", kChiSquared, " value: ", chi_square, " > ",
                    kThreshold);
    LOG(INFO) << msg;
    FAIL() << msg;
  }
}

TEST(DiscreteDistributionTest, StabilityTest) {
  // absl::discrete_distribution stability relies on
  // absl::uniform_int_distribution and absl::bernoulli_distribution.
  absl::random_internal::sequence_urbg urbg(
      {0x0003eb76f6f7f755ull, 0xFFCEA50FDB2F953Bull, 0xC332DDEFBE6C5AA5ull,
       0x6558218568AB9702ull, 0x2AEF7DAD5B6E2F84ull, 0x1521B62829076170ull,
       0xECDD4775619F1510ull, 0x13CCA830EB61BD96ull, 0x0334FE1EAA0363CFull,
       0xB5735C904C70A239ull, 0xD59E9E0BCBAADE14ull, 0xEECC86BC60622CA7ull});

  std::vector<int> output(6);

  {
    absl::discrete_distribution<int32_t> dist({1.0, 2.0, 3.0, 5.0, 2.0});
    EXPECT_EQ(0, dist.min());
    EXPECT_EQ(4, dist.max());
    for (auto& v : output) {
      v = dist(urbg);
    }
    EXPECT_EQ(12, urbg.invocations());
  }

  // With 12 calls to urbg, each call into discrete_distribution consumes
  // precisely 2 values: one for the uniform call, and a second for the
  // bernoulli.
  //
  // Given the alt mapping: 0=>3, 1=>3, 2=>2, 3=>2, 4=>3, we can
  //
  // uniform:      443210143131
  // bernoulli: b0 000011100101
  // bernoulli: b1 001111101101
  // bernoulli: b2 111111111111
  // bernoulli: b3 001111101111
  // bernoulli: b4 001111101101
  // ...
  EXPECT_THAT(output, testing::ElementsAre(3, 3, 1, 3, 3, 3));

  {
    urbg.reset();
    absl::discrete_distribution<int64_t> dist({1.0, 2.0, 3.0, 5.0, 2.0});
    EXPECT_EQ(0, dist.min());
    EXPECT_EQ(4, dist.max());
    for (auto& v : output) {
      v = dist(urbg);
    }
    EXPECT_EQ(12, urbg.invocations());
  }
  EXPECT_THAT(output, testing::ElementsAre(3, 3, 0, 3, 0, 4));
}

}  // namespace
