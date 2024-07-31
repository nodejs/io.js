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

#include "gtest/gtest.h"
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

}  // namespace
