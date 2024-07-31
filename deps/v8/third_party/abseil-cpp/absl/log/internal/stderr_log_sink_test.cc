//
// Copyright 2022 The Abseil Authors.
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

#include <stdlib.h>

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/base/log_severity.h"
#include "absl/log/globals.h"
#include "absl/log/internal/test_helpers.h"
#include "absl/log/log.h"

namespace {
using ::testing::AllOf;
using ::testing::HasSubstr;

auto* test_env ABSL_ATTRIBUTE_UNUSED = ::testing::AddGlobalTestEnvironment(
    new absl::log_internal::LogTestEnvironment);

MATCHER_P2(HasSubstrTimes, substr, expected_count, "") {
  int count = 0;
  std::string::size_type pos = 0;
  std::string needle(substr);
  while ((pos = arg.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }

  return count == expected_count;
}

TEST(StderrLogSinkDeathTest, InfoMessagesInStderr) {
  EXPECT_DEATH_IF_SUPPORTED(
      {
        absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
        LOG(INFO) << "INFO message";
        exit(1);
      },
      "INFO message");
}

TEST(StderrLogSinkDeathTest, WarningMessagesInStderr) {
  EXPECT_DEATH_IF_SUPPORTED(
      {
        absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
        LOG(WARNING) << "WARNING message";
        exit(1);
      },
      "WARNING message");
}

TEST(StderrLogSinkDeathTest, ErrorMessagesInStderr) {
  EXPECT_DEATH_IF_SUPPORTED(
      {
        absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
        LOG(ERROR) << "ERROR message";
        exit(1);
      },
      "ERROR message");
}

TEST(StderrLogSinkDeathTest, FatalMessagesInStderr) {
  char message[] = "FATAL message";
  char stacktrace[] = "*** Check failure stack trace: ***";

  int expected_count = 1;

  EXPECT_DEATH_IF_SUPPORTED(
      {
        absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
        LOG(FATAL) << message;
      },
      AllOf(HasSubstrTimes(message, expected_count), HasSubstr(stacktrace)));
}

TEST(StderrLogSinkDeathTest, SecondaryFatalMessagesInStderr) {
  auto MessageGen = []() -> std::string {
    LOG(FATAL) << "Internal failure";
    return "External failure";
  };

  EXPECT_DEATH_IF_SUPPORTED(
      {
        absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
        LOG(FATAL) << MessageGen();
      },
      "Internal failure");
}

}  // namespace
