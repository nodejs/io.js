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

#include "absl/random/mock_distributions.h"

#include <cmath>
#include <limits>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/numeric/int128.h"
#include "absl/random/distributions.h"
#include "absl/random/mocking_bit_gen.h"
#include "absl/random/random.h"

namespace {
using ::testing::Return;

TEST(MockDistributions, Examples) {
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

  EXPECT_NE(absl::Beta<double>(gen, 3.0, 2.0), 0.567);
  EXPECT_CALL(absl::MockBeta<double>(), Call(gen, 3.0, 2.0))
      .WillOnce(Return(0.567));
  EXPECT_EQ(absl::Beta<double>(gen, 3.0, 2.0), 0.567);

  EXPECT_NE(absl::Zipf<int>(gen, 1000000, 2.0, 1.0), 1221);
  EXPECT_CALL(absl::MockZipf<int>(), Call(gen, 1000000, 2.0, 1.0))
      .WillOnce(Return(1221));
  EXPECT_EQ(absl::Zipf<int>(gen, 1000000, 2.0, 1.0), 1221);

  EXPECT_NE(absl::Gaussian<double>(gen, 0.0, 1.0), 0.001);
  EXPECT_CALL(absl::MockGaussian<double>(), Call(gen, 0.0, 1.0))
      .WillOnce(Return(0.001));
  EXPECT_EQ(absl::Gaussian<double>(gen, 0.0, 1.0), 0.001);

  EXPECT_NE(absl::LogUniform<int>(gen, 0, 1000000, 2), 2040);
  EXPECT_CALL(absl::MockLogUniform<int>(), Call(gen, 0, 1000000, 2))
      .WillOnce(Return(2040));
  EXPECT_EQ(absl::LogUniform<int>(gen, 0, 1000000, 2), 2040);
}

TEST(MockDistributions, UniformUInt128BoundariesAreAllowed) {
  absl::MockingBitGen gen;

  EXPECT_CALL(absl::MockUniform<absl::uint128>(), Call(gen))
      .WillOnce(Return(absl::Uint128Max()));
  EXPECT_EQ(absl::Uniform<absl::uint128>(gen), absl::Uint128Max());
}

TEST(MockDistributions, UniformDoubleBoundaryCasesAreAllowed) {
  absl::MockingBitGen gen;

  EXPECT_CALL(absl::MockUniform<double>(), Call(gen, 1.0, 10.0))
      .WillOnce(Return(
          std::nextafter(10.0, -std::numeric_limits<double>::infinity())));
  EXPECT_EQ(absl::Uniform<double>(gen, 1.0, 10.0),
            std::nextafter(10.0, -std::numeric_limits<double>::infinity()));

  EXPECT_CALL(absl::MockUniform<double>(),
              Call(absl::IntervalOpen, gen, 1.0, 10.0))
      .WillOnce(Return(
          std::nextafter(10.0, -std::numeric_limits<double>::infinity())));
  EXPECT_EQ(absl::Uniform<double>(absl::IntervalOpen, gen, 1.0, 10.0),
            std::nextafter(10.0, -std::numeric_limits<double>::infinity()));

  EXPECT_CALL(absl::MockUniform<double>(),
              Call(absl::IntervalOpen, gen, 1.0, 10.0))
      .WillOnce(
          Return(std::nextafter(1.0, std::numeric_limits<double>::infinity())));
  EXPECT_EQ(absl::Uniform<double>(absl::IntervalOpen, gen, 1.0, 10.0),
            std::nextafter(1.0, std::numeric_limits<double>::infinity()));
}

TEST(MockDistributions, UniformDoubleEmptyRangesAllowTheBoundary) {
  absl::MockingBitGen gen;

  ON_CALL(absl::MockUniform<double>(), Call(absl::IntervalOpen, gen, 1.0, 1.0))
      .WillByDefault(Return(1.0));
  EXPECT_EQ(absl::Uniform<double>(absl::IntervalOpen, gen, 1.0, 1.0), 1.0);

  ON_CALL(absl::MockUniform<double>(),
          Call(absl::IntervalOpenClosed, gen, 1.0, 1.0))
      .WillByDefault(Return(1.0));
  EXPECT_EQ(absl::Uniform<double>(absl::IntervalOpenClosed, gen, 1.0, 1.0),
            1.0);

  ON_CALL(absl::MockUniform<double>(),
          Call(absl::IntervalClosedOpen, gen, 1.0, 1.0))
      .WillByDefault(Return(1.0));
  EXPECT_EQ(absl::Uniform<double>(absl::IntervalClosedOpen, gen, 1.0, 1.0),
            1.0);
}

TEST(MockDistributions, UniformIntEmptyRangeCasesAllowTheBoundary) {
  absl::MockingBitGen gen;

  ON_CALL(absl::MockUniform<int>(), Call(absl::IntervalOpen, gen, 1, 1))
      .WillByDefault(Return(1));
  EXPECT_EQ(absl::Uniform<int>(absl::IntervalOpen, gen, 1, 1), 1);

  ON_CALL(absl::MockUniform<int>(), Call(absl::IntervalOpenClosed, gen, 1, 1))
      .WillByDefault(Return(1));
  EXPECT_EQ(absl::Uniform<int>(absl::IntervalOpenClosed, gen, 1, 1), 1);

  ON_CALL(absl::MockUniform<int>(), Call(absl::IntervalClosedOpen, gen, 1, 1))
      .WillByDefault(Return(1));
  EXPECT_EQ(absl::Uniform<int>(absl::IntervalClosedOpen, gen, 1, 1), 1);
}

TEST(MockUniformDeathTest, OutOfBoundsValuesAreRejected) {
  absl::MockingBitGen gen;

  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<int>(), Call(gen, 1, 100))
            .WillOnce(Return(0));
        absl::Uniform<int>(gen, 1, 100);
      },
      " 0 is not in \\[1, 100\\)");
  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<int>(), Call(gen, 1, 100))
            .WillOnce(Return(101));
        absl::Uniform<int>(gen, 1, 100);
      },
      " 101 is not in \\[1, 100\\)");
  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<int>(), Call(gen, 1, 100))
            .WillOnce(Return(100));
        absl::Uniform<int>(gen, 1, 100);
      },
      " 100 is not in \\[1, 100\\)");

  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<int>(),
                    Call(absl::IntervalOpen, gen, 1, 100))
            .WillOnce(Return(1));
        absl::Uniform<int>(absl::IntervalOpen, gen, 1, 100);
      },
      " 1 is not in \\(1, 100\\)");
  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<int>(),
                    Call(absl::IntervalOpen, gen, 1, 100))
            .WillOnce(Return(101));
        absl::Uniform<int>(absl::IntervalOpen, gen, 1, 100);
      },
      " 101 is not in \\(1, 100\\)");
  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<int>(),
                    Call(absl::IntervalOpen, gen, 1, 100))
            .WillOnce(Return(100));
        absl::Uniform<int>(absl::IntervalOpen, gen, 1, 100);
      },
      " 100 is not in \\(1, 100\\)");

  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<int>(),
                    Call(absl::IntervalOpenClosed, gen, 1, 100))
            .WillOnce(Return(1));
        absl::Uniform<int>(absl::IntervalOpenClosed, gen, 1, 100);
      },
      " 1 is not in \\(1, 100\\]");
  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<int>(),
                    Call(absl::IntervalOpenClosed, gen, 1, 100))
            .WillOnce(Return(101));
        absl::Uniform<int>(absl::IntervalOpenClosed, gen, 1, 100);
      },
      " 101 is not in \\(1, 100\\]");

  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<int>(),
                    Call(absl::IntervalOpenClosed, gen, 1, 100))
            .WillOnce(Return(0));
        absl::Uniform<int>(absl::IntervalOpenClosed, gen, 1, 100);
      },
      " 0 is not in \\(1, 100\\]");
  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<int>(),
                    Call(absl::IntervalOpenClosed, gen, 1, 100))
            .WillOnce(Return(101));
        absl::Uniform<int>(absl::IntervalOpenClosed, gen, 1, 100);
      },
      " 101 is not in \\(1, 100\\]");

  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<int>(),
                    Call(absl::IntervalClosed, gen, 1, 100))
            .WillOnce(Return(0));
        absl::Uniform<int>(absl::IntervalClosed, gen, 1, 100);
      },
      " 0 is not in \\[1, 100\\]");
  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<int>(),
                    Call(absl::IntervalClosed, gen, 1, 100))
            .WillOnce(Return(101));
        absl::Uniform<int>(absl::IntervalClosed, gen, 1, 100);
      },
      " 101 is not in \\[1, 100\\]");
}

TEST(MockUniformDeathTest, OutOfBoundsDoublesAreRejected) {
  absl::MockingBitGen gen;

  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<double>(), Call(gen, 1.0, 10.0))
            .WillOnce(Return(10.0));
        EXPECT_EQ(absl::Uniform<double>(gen, 1.0, 10.0), 10.0);
      },
      " 10 is not in \\[1, 10\\)");

  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<double>(),
                    Call(absl::IntervalOpen, gen, 1.0, 10.0))
            .WillOnce(Return(10.0));
        EXPECT_EQ(absl::Uniform<double>(absl::IntervalOpen, gen, 1.0, 10.0),
                  10.0);
      },
      " 10 is not in \\(1, 10\\)");

  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<double>(),
                    Call(absl::IntervalOpen, gen, 1.0, 10.0))
            .WillOnce(Return(1.0));
        EXPECT_EQ(absl::Uniform<double>(absl::IntervalOpen, gen, 1.0, 10.0),
                  1.0);
      },
      " 1 is not in \\(1, 10\\)");
}

}  // namespace
