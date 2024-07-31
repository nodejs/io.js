// Copyright 2023 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/log/vlog_is_on.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/log_severity.h"
#include "absl/flags/flag.h"
#include "absl/log/flags.h"
#include "absl/log/globals.h"
#include "absl/log/log.h"
#include "absl/log/scoped_mock_log.h"
#include "absl/types/optional.h"

namespace {

using ::testing::_;

absl::optional<int> MaxLogVerbosity() {
#ifdef ABSL_MAX_VLOG_VERBOSITY
  return ABSL_MAX_VLOG_VERBOSITY;
#else
  return absl::nullopt;
#endif
}

absl::optional<int> MinLogLevel() {
#ifdef ABSL_MIN_LOG_LEVEL
  return static_cast<int>(ABSL_MIN_LOG_LEVEL);
#else
  return absl::nullopt;
#endif
}

TEST(VLogIsOn, GlobalWorksWithoutMaxVerbosityAndMinLogLevel) {
  if (MaxLogVerbosity().has_value() || MinLogLevel().has_value()) {
    GTEST_SKIP();
  }

  absl::SetGlobalVLogLevel(3);
  absl::ScopedMockLog log(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, _, "important"));

  log.StartCapturingLogs();
  VLOG(3) << "important";
  VLOG(4) << "spam";
}

TEST(VLogIsOn, FileWorksWithoutMaxVerbosityAndMinLogLevel) {
  if (MaxLogVerbosity().has_value() || MinLogLevel().has_value()) {
    GTEST_SKIP();
  }

  absl::SetVLogLevel("vlog_is_on_test", 3);
  absl::ScopedMockLog log(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, _, "important"));

  log.StartCapturingLogs();
  VLOG(3) << "important";
  VLOG(4) << "spam";
}

TEST(VLogIsOn, PatternWorksWithoutMaxVerbosityAndMinLogLevel) {
  if (MaxLogVerbosity().has_value() || MinLogLevel().has_value()) {
    GTEST_SKIP();
  }

  absl::SetVLogLevel("vlog_is_on*", 3);
  absl::ScopedMockLog log(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, _, "important"));

  log.StartCapturingLogs();
  VLOG(3) << "important";
  VLOG(4) << "spam";
}

TEST(VLogIsOn, GlobalDoesNotFilterBelowMaxVerbosity) {
  if (!MaxLogVerbosity().has_value() || *MaxLogVerbosity() < 2) {
    GTEST_SKIP();
  }

  // Set an arbitrary high value to avoid filtering VLOGs in tests by default.
  absl::SetGlobalVLogLevel(1000);
  absl::ScopedMockLog log(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, _, "asdf"));

  log.StartCapturingLogs();
  VLOG(2) << "asdf";
}

TEST(VLogIsOn, FileDoesNotFilterBelowMaxVerbosity) {
  if (!MaxLogVerbosity().has_value() || *MaxLogVerbosity() < 2) {
    GTEST_SKIP();
  }

  // Set an arbitrary high value to avoid filtering VLOGs in tests by default.
  absl::SetVLogLevel("vlog_is_on_test", 1000);
  absl::ScopedMockLog log(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, _, "asdf"));

  log.StartCapturingLogs();
  VLOG(2) << "asdf";
}

TEST(VLogIsOn, PatternDoesNotFilterBelowMaxVerbosity) {
  if (!MaxLogVerbosity().has_value() || *MaxLogVerbosity() < 2) {
    GTEST_SKIP();
  }

  // Set an arbitrary high value to avoid filtering VLOGs in tests by default.
  absl::SetVLogLevel("vlog_is_on*", 1000);
  absl::ScopedMockLog log(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, _, "asdf"));

  log.StartCapturingLogs();
  VLOG(2) << "asdf";
}

TEST(VLogIsOn, GlobalFiltersAboveMaxVerbosity) {
  if (!MaxLogVerbosity().has_value() || *MaxLogVerbosity() >= 4) {
    GTEST_SKIP();
  }

  // Set an arbitrary high value to avoid filtering VLOGs in tests by default.
  absl::SetGlobalVLogLevel(1000);
  absl::ScopedMockLog log(absl::MockLogDefault::kDisallowUnexpected);

  log.StartCapturingLogs();
  VLOG(4) << "dfgh";
}

TEST(VLogIsOn, FileFiltersAboveMaxVerbosity) {
  if (!MaxLogVerbosity().has_value() || *MaxLogVerbosity() >= 4) {
    GTEST_SKIP();
  }

  // Set an arbitrary high value to avoid filtering VLOGs in tests by default.
  absl::SetVLogLevel("vlog_is_on_test", 1000);
  absl::ScopedMockLog log(absl::MockLogDefault::kDisallowUnexpected);

  log.StartCapturingLogs();
  VLOG(4) << "dfgh";
}

TEST(VLogIsOn, PatternFiltersAboveMaxVerbosity) {
  if (!MaxLogVerbosity().has_value() || *MaxLogVerbosity() >= 4) {
    GTEST_SKIP();
  }

  // Set an arbitrary high value to avoid filtering VLOGs in tests by default.
  absl::SetVLogLevel("vlog_is_on*", 1000);
  absl::ScopedMockLog log(absl::MockLogDefault::kDisallowUnexpected);

  log.StartCapturingLogs();
  VLOG(4) << "dfgh";
}

}  // namespace
