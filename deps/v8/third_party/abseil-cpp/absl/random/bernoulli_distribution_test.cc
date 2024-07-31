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

#include "absl/random/bernoulli_distribution.h"

#include <cmath>
#include <cstddef>
#include <random>
#include <sstream>
#include <utility>

#include "gtest/gtest.h"
#include "absl/random/internal/pcg_engine.h"
#include "absl/random/internal/sequence_urbg.h"
#include "absl/random/random.h"

namespace {

class BernoulliTest : public testing::TestWithParam<std::pair<double, size_t>> {
};

TEST_P(BernoulliTest, Serialize) {
  const double d = GetParam().first;
  absl::bernoulli_distribution before(d);

  {
    absl::bernoulli_distribution via_param{
        absl::bernoulli_distribution::param_type(d)};
    EXPECT_EQ(via_param, before);
  }

  std::stringstream ss;
  ss << before;
  absl::bernoulli_distribution after(0.6789);

  EXPECT_NE(before.p(), after.p());
  EXPECT_NE(before.param(), after.param());
  EXPECT_NE(before, after);

  ss >> after;

  EXPECT_EQ(before.p(), after.p());
  EXPECT_EQ(before.param(), after.param());
  EXPECT_EQ(before, after);
}

TEST_P(BernoulliTest, Accuracy) {
  // Sadly, the claim to fame for this implementation is precise accuracy, which
  // is very, very hard to measure, the improvements come as trials approach the
  // limit of double accuracy; thus the outcome differs from the
  // std::bernoulli_distribution with a probability of approximately 1 in 2^-53.
  const std::pair<double, size_t> para = GetParam();
  size_t trials = para.second;
  double p = para.first;

  // We use a fixed bit generator for distribution accuracy tests.  This allows
  // these tests to be deterministic, while still testing the qualify of the
  // implementation.
  absl::random_internal::pcg64_2018_engine rng(0x2B7E151628AED2A6);

  size_t yes = 0;
  absl::bernoulli_distribution dist(p);
  for (size_t i = 0; i < trials; ++i) {
    if (dist(rng)) yes++;
  }

  // Compute the distribution parameters for a binomial test, using a normal
  // approximation for the confidence interval, as there are a sufficiently
  // large number of trials that the central limit theorem applies.
  const double stddev_p = std::sqrt((p * (1.0 - p)) / trials);
  const double expected = trials * p;
  const double stddev = trials * stddev_p;

  // 5 sigma, approved by Richard Feynman
  EXPECT_NEAR(yes, expected, 5 * stddev)
      << "@" << p << ", "
      << std::abs(static_cast<double>(yes) - expected) / stddev << " stddev";
}

// There must be many more trials to make the mean approximately normal for `p`
// closes to 0 or 1.
INSTANTIATE_TEST_SUITE_P(
    All, BernoulliTest,
    ::testing::Values(
        // Typical values.
        std::make_pair(0, 30000), std::make_pair(1e-3, 30000000),
        std::make_pair(0.1, 3000000), std::make_pair(0.5, 3000000),
        std::make_pair(0.9, 30000000), std::make_pair(0.999, 30000000),
        std::make_pair(1, 30000),
        // Boundary cases.
        std::make_pair(std::nextafter(1.0, 0.0), 1),  // ~1 - epsilon
        std::make_pair(std::numeric_limits<double>::epsilon(), 1),
        std::make_pair(std::nextafter(std::numeric_limits<double>::min(),
                                      1.0),  // min + epsilon
                       1),
        std::make_pair(std::numeric_limits<double>::min(),  // smallest normal
                       1),
        std::make_pair(
            std::numeric_limits<double>::denorm_min(),  // smallest denorm
            1),
        std::make_pair(std::numeric_limits<double>::min() / 2, 1),  // denorm
        std::make_pair(std::nextafter(std::numeric_limits<double>::min(),
                                      0.0),  // denorm_max
                       1)));

// NOTE: absl::bernoulli_distribution is not guaranteed to be stable.
TEST(BernoulliTest, StabilityTest) {
  // absl::bernoulli_distribution stability relies on FastUniformBits and
  // integer arithmetic.
  absl::random_internal::sequence_urbg urbg({
      0x0003eb76f6f7f755ull, 0xFFCEA50FDB2F953Bull, 0xC332DDEFBE6C5AA5ull,
      0x6558218568AB9702ull, 0x2AEF7DAD5B6E2F84ull, 0x1521B62829076170ull,
      0xECDD4775619F1510ull, 0x13CCA830EB61BD96ull, 0x0334FE1EAA0363CFull,
      0xB5735C904C70A239ull, 0xD59E9E0BCBAADE14ull, 0xEECC86BC60622CA7ull,
      0x4864f22c059bf29eull, 0x247856d8b862665cull, 0xe46e86e9a1337e10ull,
      0xd8c8541f3519b133ull, 0xe75b5162c567b9e4ull, 0xf732e5ded7009c5bull,
      0xb170b98353121eacull, 0x1ec2e8986d2362caull, 0x814c8e35fe9a961aull,
      0x0c3cd59c9b638a02ull, 0xcb3bb6478a07715cull, 0x1224e62c978bbc7full,
      0x671ef2cb04e81f6eull, 0x3c1cbd811eaf1808ull, 0x1bbc23cfa8fac721ull,
      0xa4c2cda65e596a51ull, 0xb77216fad37adf91ull, 0x836d794457c08849ull,
      0xe083df03475f49d7ull, 0xbc9feb512e6b0d6cull, 0xb12d74fdd718c8c5ull,
      0x12ff09653bfbe4caull, 0x8dd03a105bc4ee7eull, 0x5738341045ba0d85ull,
      0xe3fd722dc65ad09eull, 0x5a14fd21ea2a5705ull, 0x14e6ea4d6edb0c73ull,
      0x275b0dc7e0a18acfull, 0x36cebe0d2653682eull, 0x0361e9b23861596bull,
  });

  // Generate a string of '0' and '1' for the distribution output.
  auto generate = [&urbg](absl::bernoulli_distribution& dist) {
    std::string output;
    output.reserve(36);
    urbg.reset();
    for (int i = 0; i < 35; i++) {
      output.append(dist(urbg) ? "1" : "0");
    }
    return output;
  };

  const double kP = 0.0331289862362;
  {
    absl::bernoulli_distribution dist(kP);
    auto v = generate(dist);
    EXPECT_EQ(35, urbg.invocations());
    EXPECT_EQ(v, "00000000000010000000000010000000000") << dist;
  }
  {
    absl::bernoulli_distribution dist(kP * 10.0);
    auto v = generate(dist);
    EXPECT_EQ(35, urbg.invocations());
    EXPECT_EQ(v, "00000100010010010010000011000011010") << dist;
  }
  {
    absl::bernoulli_distribution dist(kP * 20.0);
    auto v = generate(dist);
    EXPECT_EQ(35, urbg.invocations());
    EXPECT_EQ(v, "00011110010110110011011111110111011") << dist;
  }
  {
    absl::bernoulli_distribution dist(1.0 - kP);
    auto v = generate(dist);
    EXPECT_EQ(35, urbg.invocations());
    EXPECT_EQ(v, "11111111111111111111011111111111111") << dist;
  }
}

TEST(BernoulliTest, StabilityTest2) {
  absl::random_internal::sequence_urbg urbg(
      {0x0003eb76f6f7f755ull, 0xFFCEA50FDB2F953Bull, 0xC332DDEFBE6C5AA5ull,
       0x6558218568AB9702ull, 0x2AEF7DAD5B6E2F84ull, 0x1521B62829076170ull,
       0xECDD4775619F1510ull, 0x13CCA830EB61BD96ull, 0x0334FE1EAA0363CFull,
       0xB5735C904C70A239ull, 0xD59E9E0BCBAADE14ull, 0xEECC86BC60622CA7ull});

  // Generate a string of '0' and '1' for the distribution output.
  auto generate = [&urbg](absl::bernoulli_distribution& dist) {
    std::string output;
    output.reserve(13);
    urbg.reset();
    for (int i = 0; i < 12; i++) {
      output.append(dist(urbg) ? "1" : "0");
    }
    return output;
  };

  constexpr double b0 = 1.0 / 13.0 / 0.2;
  constexpr double b1 = 2.0 / 13.0 / 0.2;
  constexpr double b3 = (5.0 / 13.0 / 0.2) - ((1 - b0) + (1 - b1) + (1 - b1));
  {
    absl::bernoulli_distribution dist(b0);
    auto v = generate(dist);
    EXPECT_EQ(12, urbg.invocations());
    EXPECT_EQ(v, "000011100101") << dist;
  }
  {
    absl::bernoulli_distribution dist(b1);
    auto v = generate(dist);
    EXPECT_EQ(12, urbg.invocations());
    EXPECT_EQ(v, "001111101101") << dist;
  }
  {
    absl::bernoulli_distribution dist(b3);
    auto v = generate(dist);
    EXPECT_EQ(12, urbg.invocations());
    EXPECT_EQ(v, "001111101111") << dist;
  }
}

}  // namespace
