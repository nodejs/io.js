// Copyright 2019 The Abseil Authors.
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

#include "absl/profiling/internal/periodic_sampler.h"

#include <thread>  // NOLINT(build/c++11)

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/base/macros.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace profiling_internal {
namespace {

using testing::Eq;
using testing::Return;
using testing::StrictMock;

class MockPeriodicSampler : public PeriodicSamplerBase {
 public:
  virtual ~MockPeriodicSampler() = default;

  MOCK_METHOD(int, period, (), (const, noexcept));
  MOCK_METHOD(int64_t, GetExponentialBiased, (int), (noexcept));
};

TEST(PeriodicSamplerBaseTest, Sample) {
  StrictMock<MockPeriodicSampler> sampler;

  EXPECT_CALL(sampler, period()).Times(3).WillRepeatedly(Return(16));
  EXPECT_CALL(sampler, GetExponentialBiased(16))
      .WillOnce(Return(2))
      .WillOnce(Return(3))
      .WillOnce(Return(4));

  EXPECT_FALSE(sampler.Sample());
  EXPECT_TRUE(sampler.Sample());

  EXPECT_FALSE(sampler.Sample());
  EXPECT_FALSE(sampler.Sample());
  EXPECT_TRUE(sampler.Sample());

  EXPECT_FALSE(sampler.Sample());
  EXPECT_FALSE(sampler.Sample());
  EXPECT_FALSE(sampler.Sample());
}

TEST(PeriodicSamplerBaseTest, ImmediatelySample) {
  StrictMock<MockPeriodicSampler> sampler;

  EXPECT_CALL(sampler, period()).Times(2).WillRepeatedly(Return(16));
  EXPECT_CALL(sampler, GetExponentialBiased(16))
      .WillOnce(Return(1))
      .WillOnce(Return(2))
      .WillOnce(Return(3));

  EXPECT_TRUE(sampler.Sample());

  EXPECT_FALSE(sampler.Sample());
  EXPECT_TRUE(sampler.Sample());

  EXPECT_FALSE(sampler.Sample());
  EXPECT_FALSE(sampler.Sample());
}

TEST(PeriodicSamplerBaseTest, Disabled) {
  StrictMock<MockPeriodicSampler> sampler;

  EXPECT_CALL(sampler, period()).Times(3).WillRepeatedly(Return(0));

  EXPECT_FALSE(sampler.Sample());
  EXPECT_FALSE(sampler.Sample());
  EXPECT_FALSE(sampler.Sample());
}

TEST(PeriodicSamplerBaseTest, AlwaysOn) {
  StrictMock<MockPeriodicSampler> sampler;

  EXPECT_CALL(sampler, period()).Times(3).WillRepeatedly(Return(1));

  EXPECT_TRUE(sampler.Sample());
  EXPECT_TRUE(sampler.Sample());
  EXPECT_TRUE(sampler.Sample());
}

TEST(PeriodicSamplerBaseTest, Disable) {
  StrictMock<MockPeriodicSampler> sampler;

  EXPECT_CALL(sampler, period()).WillOnce(Return(16));
  EXPECT_CALL(sampler, GetExponentialBiased(16)).WillOnce(Return(3));
  EXPECT_FALSE(sampler.Sample());
  EXPECT_FALSE(sampler.Sample());

  EXPECT_CALL(sampler, period()).Times(2).WillRepeatedly(Return(0));

  EXPECT_FALSE(sampler.Sample());
  EXPECT_FALSE(sampler.Sample());
}

TEST(PeriodicSamplerBaseTest, Enable) {
  StrictMock<MockPeriodicSampler> sampler;

  EXPECT_CALL(sampler, period()).WillOnce(Return(0));
  EXPECT_FALSE(sampler.Sample());

  EXPECT_CALL(sampler, period()).Times(2).WillRepeatedly(Return(16));
  EXPECT_CALL(sampler, GetExponentialBiased(16))
      .Times(2)
      .WillRepeatedly(Return(3));

  EXPECT_FALSE(sampler.Sample());
  EXPECT_FALSE(sampler.Sample());
  EXPECT_TRUE(sampler.Sample());

  EXPECT_FALSE(sampler.Sample());
  EXPECT_FALSE(sampler.Sample());
}

TEST(PeriodicSamplerTest, ConstructConstInit) {
  struct Tag {};
  ABSL_CONST_INIT static PeriodicSampler<Tag> sampler;
  (void)sampler;
}

TEST(PeriodicSamplerTest, DefaultPeriod0) {
  struct Tag {};
  PeriodicSampler<Tag> sampler;
  EXPECT_THAT(sampler.period(), Eq(0));
}

TEST(PeriodicSamplerTest, DefaultPeriod) {
  struct Tag {};
  PeriodicSampler<Tag, 100> sampler;
  EXPECT_THAT(sampler.period(), Eq(100));
}

TEST(PeriodicSamplerTest, SetGlobalPeriod) {
  struct Tag1 {};
  struct Tag2 {};
  PeriodicSampler<Tag1, 25> sampler1;
  PeriodicSampler<Tag2, 50> sampler2;

  EXPECT_THAT(sampler1.period(), Eq(25));
  EXPECT_THAT(sampler2.period(), Eq(50));

  std::thread thread([] {
    PeriodicSampler<Tag1, 25> sampler1;
    PeriodicSampler<Tag2, 50> sampler2;
    EXPECT_THAT(sampler1.period(), Eq(25));
    EXPECT_THAT(sampler2.period(), Eq(50));
    sampler1.SetGlobalPeriod(10);
    sampler2.SetGlobalPeriod(20);
  });
  thread.join();

  EXPECT_THAT(sampler1.period(), Eq(10));
  EXPECT_THAT(sampler2.period(), Eq(20));
}

}  // namespace
}  // namespace profiling_internal
ABSL_NAMESPACE_END
}  // namespace absl
