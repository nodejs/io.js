//
// Copyright 2018 The Abseil Authors.
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
//
#include "absl/random/mocking_bit_gen.h"

#include <cmath>
#include <numeric>
#include <random>

#include "gmock/gmock.h"
#include "gtest/gtest-spi.h"
#include "gtest/gtest.h"
#include "absl/random/bit_gen_ref.h"
#include "absl/random/mock_distributions.h"
#include "absl/random/random.h"

namespace {

using ::testing::_;
using ::testing::Ne;
using ::testing::Return;

TEST(BasicMocking, AllDistributionsAreOverridable) {
  absl::MockingBitGen gen;

  EXPECT_NE(absl::Uniform<int>(gen, 1, 1000000), 20);
  EXPECT_CALL(absl::MockUniform<int>(), Call(gen, 1, 1000000))
      .WillOnce(Return(20));
  EXPECT_EQ(absl::Uniform<int>(gen, 1, 1000000), 20);

  EXPECT_NE(absl::Uniform<double>(gen, 0.0, 100.0), 5.0);
  EXPECT_CALL(absl::MockUniform<double>(), Call(gen, 0.0, 100.0))
      .WillOnce(Return(5.0));
  EXPECT_EQ(absl::Uniform<double>(gen, 0.0, 100.0), 5.0);

  EXPECT_NE(absl::Exponential<double>(gen, 1.0), 42);
  EXPECT_CALL(absl::MockExponential<double>(), Call(gen, 1.0))
      .WillOnce(Return(42));
  EXPECT_EQ(absl::Exponential<double>(gen, 1.0), 42);

  EXPECT_NE(absl::Poisson<int>(gen, 1.0), 500);
  EXPECT_CALL(absl::MockPoisson<int>(), Call(gen, 1.0)).WillOnce(Return(500));
  EXPECT_EQ(absl::Poisson<int>(gen, 1.0), 500);

  EXPECT_NE(absl::Bernoulli(gen, 0.000001), true);
  EXPECT_CALL(absl::MockBernoulli(), Call(gen, 0.000001))
      .WillOnce(Return(true));
  EXPECT_EQ(absl::Bernoulli(gen, 0.000001), true);

  EXPECT_NE(absl::Zipf<int>(gen, 1000000, 2.0, 1.0), 1221);
  EXPECT_CALL(absl::MockZipf<int>(), Call(gen, 1000000, 2.0, 1.0))
      .WillOnce(Return(1221));
  EXPECT_EQ(absl::Zipf<int>(gen, 1000000, 2.0, 1.0), 1221);

  EXPECT_NE(absl::Gaussian<double>(gen, 0.0, 1.0), 0.001);
  EXPECT_CALL(absl::MockGaussian<double>(), Call(gen, 0.0, 1.0))
      .WillOnce(Return(0.001));
  EXPECT_EQ(absl::Gaussian<double>(gen, 0.0, 1.0), 0.001);

  EXPECT_NE(absl::LogUniform<int>(gen, 0, 1000000, 2), 500000);
  EXPECT_CALL(absl::MockLogUniform<int>(), Call(gen, 0, 1000000, 2))
      .WillOnce(Return(500000));
  EXPECT_EQ(absl::LogUniform<int>(gen, 0, 1000000, 2), 500000);
}

TEST(BasicMocking, OnDistribution) {
  absl::MockingBitGen gen;

  EXPECT_NE(absl::Uniform<int>(gen, 1, 1000000), 20);
  ON_CALL(absl::MockUniform<int>(), Call(gen, 1, 1000000))
      .WillByDefault(Return(20));
  EXPECT_EQ(absl::Uniform<int>(gen, 1, 1000000), 20);

  EXPECT_NE(absl::Uniform<double>(gen, 0.0, 100.0), 5.0);
  ON_CALL(absl::MockUniform<double>(), Call(gen, 0.0, 100.0))
      .WillByDefault(Return(5.0));
  EXPECT_EQ(absl::Uniform<double>(gen, 0.0, 100.0), 5.0);

  EXPECT_NE(absl::Exponential<double>(gen, 1.0), 42);
  ON_CALL(absl::MockExponential<double>(), Call(gen, 1.0))
      .WillByDefault(Return(42));
  EXPECT_EQ(absl::Exponential<double>(gen, 1.0), 42);

  EXPECT_NE(absl::Poisson<int>(gen, 1.0), 500);
  ON_CALL(absl::MockPoisson<int>(), Call(gen, 1.0)).WillByDefault(Return(500));
  EXPECT_EQ(absl::Poisson<int>(gen, 1.0), 500);

  EXPECT_NE(absl::Bernoulli(gen, 0.000001), true);
  ON_CALL(absl::MockBernoulli(), Call(gen, 0.000001))
      .WillByDefault(Return(true));
  EXPECT_EQ(absl::Bernoulli(gen, 0.000001), true);

  EXPECT_NE(absl::Zipf<int>(gen, 1000000, 2.0, 1.0), 1221);
  ON_CALL(absl::MockZipf<int>(), Call(gen, 1000000, 2.0, 1.0))
      .WillByDefault(Return(1221));
  EXPECT_EQ(absl::Zipf<int>(gen, 1000000, 2.0, 1.0), 1221);

  EXPECT_NE(absl::Gaussian<double>(gen, 0.0, 1.0), 0.001);
  ON_CALL(absl::MockGaussian<double>(), Call(gen, 0.0, 1.0))
      .WillByDefault(Return(0.001));
  EXPECT_EQ(absl::Gaussian<double>(gen, 0.0, 1.0), 0.001);

  EXPECT_NE(absl::LogUniform<int>(gen, 0, 1000000, 2), 2040);
  ON_CALL(absl::MockLogUniform<int>(), Call(gen, 0, 1000000, 2))
      .WillByDefault(Return(2040));
  EXPECT_EQ(absl::LogUniform<int>(gen, 0, 1000000, 2), 2040);
}

TEST(BasicMocking, GMockMatchers) {
  absl::MockingBitGen gen;

  EXPECT_NE(absl::Zipf<int>(gen, 1000000, 2.0, 1.0), 1221);
  ON_CALL(absl::MockZipf<int>(), Call(gen, 1000000, 2.0, 1.0))
      .WillByDefault(Return(1221));
  EXPECT_EQ(absl::Zipf<int>(gen, 1000000, 2.0, 1.0), 1221);
}

TEST(BasicMocking, OverridesWithMultipleGMockExpectations) {
  absl::MockingBitGen gen;

  EXPECT_CALL(absl::MockUniform<int>(), Call(gen, 1, 10000))
      .WillOnce(Return(20))
      .WillOnce(Return(40))
      .WillOnce(Return(60));
  EXPECT_EQ(absl::Uniform(gen, 1, 10000), 20);
  EXPECT_EQ(absl::Uniform(gen, 1, 10000), 40);
  EXPECT_EQ(absl::Uniform(gen, 1, 10000), 60);
}

TEST(BasicMocking, DefaultArgument) {
  absl::MockingBitGen gen;

  ON_CALL(absl::MockExponential<double>(), Call(gen, 1.0))
      .WillByDefault(Return(200));

  EXPECT_EQ(absl::Exponential<double>(gen), 200);
  EXPECT_EQ(absl::Exponential<double>(gen, 1.0), 200);
}

TEST(BasicMocking, MultipleGenerators) {
  auto get_value = [](absl::BitGenRef gen_ref) {
    return absl::Uniform(gen_ref, 1, 1000000);
  };
  absl::MockingBitGen unmocked_generator;
  absl::MockingBitGen mocked_with_3;
  absl::MockingBitGen mocked_with_11;

  EXPECT_CALL(absl::MockUniform<int>(), Call(mocked_with_3, 1, 1000000))
      .WillOnce(Return(3))
      .WillRepeatedly(Return(17));
  EXPECT_CALL(absl::MockUniform<int>(), Call(mocked_with_11, 1, 1000000))
      .WillOnce(Return(11))
      .WillRepeatedly(Return(17));

  // Ensure that unmocked generator generates neither value.
  int unmocked_value = get_value(unmocked_generator);
  EXPECT_NE(unmocked_value, 3);
  EXPECT_NE(unmocked_value, 11);
  // Mocked generators should generate their mocked values.
  EXPECT_EQ(get_value(mocked_with_3), 3);
  EXPECT_EQ(get_value(mocked_with_11), 11);
  // Ensure that the mocks have expired.
  EXPECT_NE(get_value(mocked_with_3), 3);
  EXPECT_NE(get_value(mocked_with_11), 11);
}

TEST(BasicMocking, MocksNotTrigeredForIncorrectTypes) {
  absl::MockingBitGen gen;
  EXPECT_CALL(absl::MockUniform<uint32_t>(), Call(gen)).WillOnce(Return(42));

  EXPECT_NE(absl::Uniform<uint16_t>(gen), 42);  // Not mocked
  EXPECT_EQ(absl::Uniform<uint32_t>(gen), 42);  // Mock triggered
}

TEST(BasicMocking, FailsOnUnsatisfiedMocks) {
  EXPECT_NONFATAL_FAILURE(
      []() {
        absl::MockingBitGen gen;
        EXPECT_CALL(absl::MockExponential<double>(), Call(gen, 1.0))
            .WillOnce(Return(3.0));
        // Does not call absl::Exponential().
      }(),
      "unsatisfied and active");
}

TEST(OnUniform, RespectsUniformIntervalSemantics) {
  absl::MockingBitGen gen;

  EXPECT_CALL(absl::MockUniform<int>(),
              Call(absl::IntervalClosed, gen, 1, 1000000))
      .WillOnce(Return(301));
  EXPECT_NE(absl::Uniform(gen, 1, 1000000), 301);  // Not mocked
  EXPECT_EQ(absl::Uniform(absl::IntervalClosed, gen, 1, 1000000), 301);
}

TEST(OnUniform, RespectsNoArgUnsignedShorthand) {
  absl::MockingBitGen gen;
  EXPECT_CALL(absl::MockUniform<uint32_t>(), Call(gen)).WillOnce(Return(42));
  EXPECT_EQ(absl::Uniform<uint32_t>(gen), 42);
}

TEST(RepeatedlyModifier, ForceSnakeEyesForManyDice) {
  auto roll_some_dice = [](absl::BitGenRef gen_ref) {
    std::vector<int> results(16);
    for (auto& r : results) {
      r = absl::Uniform(absl::IntervalClosed, gen_ref, 1, 6);
    }
    return results;
  };
  std::vector<int> results;
  absl::MockingBitGen gen;

  // Without any mocked calls, not all dice roll a "6".
  results = roll_some_dice(gen);
  EXPECT_LT(std::accumulate(std::begin(results), std::end(results), 0),
            results.size() * 6);

  // Verify that we can force all "6"-rolls, with mocking.
  ON_CALL(absl::MockUniform<int>(), Call(absl::IntervalClosed, gen, 1, 6))
      .WillByDefault(Return(6));
  results = roll_some_dice(gen);
  EXPECT_EQ(std::accumulate(std::begin(results), std::end(results), 0),
            results.size() * 6);
}

TEST(WillOnce, DistinctCounters) {
  absl::MockingBitGen gen;
  EXPECT_CALL(absl::MockUniform<int>(), Call(gen, 1, 1000000))
      .Times(3)
      .WillRepeatedly(Return(0));
  EXPECT_CALL(absl::MockUniform<int>(), Call(gen, 1000001, 2000000))
      .Times(3)
      .WillRepeatedly(Return(1));
  EXPECT_EQ(absl::Uniform(gen, 1000001, 2000000), 1);
  EXPECT_EQ(absl::Uniform(gen, 1, 1000000), 0);
  EXPECT_EQ(absl::Uniform(gen, 1000001, 2000000), 1);
  EXPECT_EQ(absl::Uniform(gen, 1, 1000000), 0);
  EXPECT_EQ(absl::Uniform(gen, 1000001, 2000000), 1);
  EXPECT_EQ(absl::Uniform(gen, 1, 1000000), 0);
}

TEST(TimesModifier, ModifierSaturatesAndExpires) {
  EXPECT_NONFATAL_FAILURE(
      []() {
        absl::MockingBitGen gen;
        EXPECT_CALL(absl::MockUniform<int>(), Call(gen, 1, 1000000))
            .Times(3)
            .WillRepeatedly(Return(15))
            .RetiresOnSaturation();

        EXPECT_EQ(absl::Uniform(gen, 1, 1000000), 15);
        EXPECT_EQ(absl::Uniform(gen, 1, 1000000), 15);
        EXPECT_EQ(absl::Uniform(gen, 1, 1000000), 15);
        // Times(3) has expired - Should get a different value now.

        EXPECT_NE(absl::Uniform(gen, 1, 1000000), 15);
      }(),
      "");
}

TEST(TimesModifier, Times0) {
  absl::MockingBitGen gen;
  EXPECT_CALL(absl::MockBernoulli(), Call(gen, 0.0)).Times(0);
  EXPECT_CALL(absl::MockPoisson<int>(), Call(gen, 1.0)).Times(0);
}

TEST(AnythingMatcher, MatchesAnyArgument) {
  using testing::_;

  {
    absl::MockingBitGen gen;
    ON_CALL(absl::MockUniform<int>(), Call(absl::IntervalClosed, gen, _, 1000))
        .WillByDefault(Return(11));
    ON_CALL(absl::MockUniform<int>(),
            Call(absl::IntervalClosed, gen, _, Ne(1000)))
        .WillByDefault(Return(99));

    EXPECT_EQ(absl::Uniform(absl::IntervalClosed, gen, 10, 1000000), 99);
    EXPECT_EQ(absl::Uniform(absl::IntervalClosed, gen, 10, 1000), 11);
  }

  {
    absl::MockingBitGen gen;
    ON_CALL(absl::MockUniform<int>(), Call(gen, 1, _))
        .WillByDefault(Return(25));
    ON_CALL(absl::MockUniform<int>(), Call(gen, Ne(1), _))
        .WillByDefault(Return(99));
    EXPECT_EQ(absl::Uniform(gen, 3, 1000000), 99);
    EXPECT_EQ(absl::Uniform(gen, 1, 1000000), 25);
  }

  {
    absl::MockingBitGen gen;
    ON_CALL(absl::MockUniform<int>(), Call(gen, _, _))
        .WillByDefault(Return(145));
    EXPECT_EQ(absl::Uniform(gen, 1, 1000), 145);
    EXPECT_EQ(absl::Uniform(gen, 10, 1000), 145);
    EXPECT_EQ(absl::Uniform(gen, 100, 1000), 145);
  }
}

TEST(AnythingMatcher, WithWillByDefault) {
  using testing::_;
  absl::MockingBitGen gen;
  std::vector<int> values = {11, 22, 33, 44, 55, 66, 77, 88, 99, 1010};

  ON_CALL(absl::MockUniform<size_t>(), Call(gen, 0, _))
      .WillByDefault(Return(0));
  for (int i = 0; i < 100; i++) {
    auto& elem = values[absl::Uniform(gen, 0u, values.size())];
    EXPECT_EQ(elem, 11);
  }
}

TEST(BasicMocking, WillByDefaultWithArgs) {
  using testing::_;

  absl::MockingBitGen gen;
  ON_CALL(absl::MockPoisson<int>(), Call(gen, _))
      .WillByDefault([](double lambda) {
        return static_cast<int>(std::rint(lambda * 10));
      });
  EXPECT_EQ(absl::Poisson<int>(gen, 1.7), 17);
  EXPECT_EQ(absl::Poisson<int>(gen, 0.03), 0);
}

TEST(MockingBitGen, InSequenceSucceedsInOrder) {
  absl::MockingBitGen gen;

  testing::InSequence seq;

  EXPECT_CALL(absl::MockPoisson<int>(), Call(gen, 1.0)).WillOnce(Return(3));
  EXPECT_CALL(absl::MockPoisson<int>(), Call(gen, 2.0)).WillOnce(Return(4));

  EXPECT_EQ(absl::Poisson<int>(gen, 1.0), 3);
  EXPECT_EQ(absl::Poisson<int>(gen, 2.0), 4);
}

TEST(MockingBitGen, NiceMock) {
  ::testing::NiceMock<absl::MockingBitGen> gen;
  ON_CALL(absl::MockUniform<int>(), Call(gen, _, _)).WillByDefault(Return(145));

  ON_CALL(absl::MockPoisson<int>(), Call(gen, _)).WillByDefault(Return(3));

  EXPECT_EQ(absl::Uniform(gen, 1, 1000), 145);
  EXPECT_EQ(absl::Uniform(gen, 10, 1000), 145);
  EXPECT_EQ(absl::Uniform(gen, 100, 1000), 145);
}

TEST(MockingBitGen, NaggyMock) {
  // This is difficult to test, as only the output matters, so just verify
  // that ON_CALL can be installed. Anything else requires log inspection.
  ::testing::NaggyMock<absl::MockingBitGen> gen;

  ON_CALL(absl::MockUniform<int>(), Call(gen, _, _)).WillByDefault(Return(145));
  ON_CALL(absl::MockPoisson<int>(), Call(gen, _)).WillByDefault(Return(3));

  EXPECT_EQ(absl::Uniform(gen, 1, 1000), 145);
}

TEST(MockingBitGen, StrictMock_NotEnough) {
  EXPECT_NONFATAL_FAILURE(
      []() {
        ::testing::StrictMock<absl::MockingBitGen> gen;
        EXPECT_CALL(absl::MockUniform<int>(), Call(gen, _, _))
            .WillOnce(Return(145));
      }(),
      "unsatisfied and active");
}

TEST(MockingBitGen, StrictMock_TooMany) {
  ::testing::StrictMock<absl::MockingBitGen> gen;

  EXPECT_CALL(absl::MockUniform<int>(), Call(gen, _, _)).WillOnce(Return(145));
  EXPECT_EQ(absl::Uniform(gen, 1, 1000), 145);

  EXPECT_NONFATAL_FAILURE(
      [&]() { EXPECT_EQ(absl::Uniform(gen, 10, 1000), 0); }(),
      "over-saturated and active");
}

}  // namespace
