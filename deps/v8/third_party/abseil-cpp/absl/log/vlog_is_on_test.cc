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

// This fixture is used to reset the VLOG levels to their default values before
// each test.
class VLogIsOnTest : public ::testing::Test {
 protected:
  void SetUp() override { ResetVLogLevels(); }

 private:
  // Resets the VLOG levels to their default values.
  // It is supposed to be called in the SetUp() method of the test fixture to
  // eliminate any side effects from other tests.
  static void ResetVLogLevels() {
    absl::log_internal::UpdateVModule("");
    absl::SetGlobalVLogLevel(0);
  }
};

TEST_F(VLogIsOnTest, GlobalWorksWithoutMaxVerbosityAndMinLogLevel) {
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

TEST_F(VLogIsOnTest, FileWorksWithoutMaxVerbosityAndMinLogLevel) {
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

TEST_F(VLogIsOnTest, PatternWorksWithoutMaxVerbosityAndMinLogLevel) {
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

TEST_F(VLogIsOnTest,
       PatternOverridesLessGenericOneWithoutMaxVerbosityAndMinLogLevel) {
  if (MaxLogVerbosity().has_value() || MinLogLevel().has_value()) {
    GTEST_SKIP();
  }

  // This should disable logging in this file
  absl::SetVLogLevel("vlog_is_on*", -1);
  // This overrides the previous setting, because "vlog*" is more generic than
  // "vlog_is_on*". This should enable VLOG level 3 in this file.
  absl::SetVLogLevel("vlog*", 3);
  absl::ScopedMockLog log(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, _, "important"));

  log.StartCapturingLogs();
  VLOG(3) << "important";
  VLOG(4) << "spam";
}

TEST_F(VLogIsOnTest,
       PatternDoesNotOverridesMoreGenericOneWithoutMaxVerbosityAndMinLogLevel) {
  if (MaxLogVerbosity().has_value() || MinLogLevel().has_value()) {
    GTEST_SKIP();
  }

  // This should enable VLOG level 3 in this file.
  absl::SetVLogLevel("vlog*", 3);
  // This should not change the VLOG level in this file. The pattern does not
  // match this file and it is less generic than the previous patter "vlog*".
  // Therefore, it does not disable VLOG level 3 in this file.
  absl::SetVLogLevel("vlog_is_on_some_other_test*", -1);
  absl::ScopedMockLog log(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, _, "important"));

  log.StartCapturingLogs();
  VLOG(3) << "important";
  VLOG(5) << "spam";
}

TEST_F(VLogIsOnTest, GlobalDoesNotFilterBelowMaxVerbosity) {
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

TEST_F(VLogIsOnTest, FileDoesNotFilterBelowMaxVerbosity) {
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

TEST_F(VLogIsOnTest, PatternDoesNotFilterBelowMaxVerbosity) {
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

TEST_F(VLogIsOnTest, GlobalFiltersAboveMaxVerbosity) {
  if (!MaxLogVerbosity().has_value() || *MaxLogVerbosity() >= 4) {
    GTEST_SKIP();
  }

  // Set an arbitrary high value to avoid filtering VLOGs in tests by default.
  absl::SetGlobalVLogLevel(1000);
  absl::ScopedMockLog log(absl::MockLogDefault::kDisallowUnexpected);

  log.StartCapturingLogs();
  VLOG(4) << "dfgh";
}

TEST_F(VLogIsOnTest, FileFiltersAboveMaxVerbosity) {
  if (!MaxLogVerbosity().has_value() || *MaxLogVerbosity() >= 4) {
    GTEST_SKIP();
  }

  // Set an arbitrary high value to avoid filtering VLOGs in tests by default.
  absl::SetVLogLevel("vlog_is_on_test", 1000);
  absl::ScopedMockLog log(absl::MockLogDefault::kDisallowUnexpected);

  log.StartCapturingLogs();
  VLOG(4) << "dfgh";
}

TEST_F(VLogIsOnTest, PatternFiltersAboveMaxVerbosity) {
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
