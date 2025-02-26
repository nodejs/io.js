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

#include "absl/log/globals.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/base/log_severity.h"
#include "absl/log/internal/globals.h"
#include "absl/log/internal/test_helpers.h"
#include "absl/log/log.h"
#include "absl/log/scoped_mock_log.h"

namespace {
using ::testing::_;
using ::testing::StrEq;

auto* test_env ABSL_ATTRIBUTE_UNUSED = ::testing::AddGlobalTestEnvironment(
    new absl::log_internal::LogTestEnvironment);

constexpr static absl::LogSeverityAtLeast DefaultMinLogLevel() {
  return absl::LogSeverityAtLeast::kInfo;
}
constexpr static absl::LogSeverityAtLeast DefaultStderrThreshold() {
  return absl::LogSeverityAtLeast::kError;
}

TEST(TestGlobals, MinLogLevel) {
  EXPECT_EQ(absl::MinLogLevel(), DefaultMinLogLevel());
  absl::SetMinLogLevel(absl::LogSeverityAtLeast::kError);
  EXPECT_EQ(absl::MinLogLevel(), absl::LogSeverityAtLeast::kError);
  absl::SetMinLogLevel(DefaultMinLogLevel());
}

TEST(TestGlobals, ScopedMinLogLevel) {
  EXPECT_EQ(absl::MinLogLevel(), DefaultMinLogLevel());
  {
    absl::log_internal::ScopedMinLogLevel scoped_stderr_threshold(
        absl::LogSeverityAtLeast::kError);
    EXPECT_EQ(absl::MinLogLevel(), absl::LogSeverityAtLeast::kError);
  }
  EXPECT_EQ(absl::MinLogLevel(), DefaultMinLogLevel());
}

TEST(TestGlobals, StderrThreshold) {
  EXPECT_EQ(absl::StderrThreshold(), DefaultStderrThreshold());
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kError);
  EXPECT_EQ(absl::StderrThreshold(), absl::LogSeverityAtLeast::kError);
  absl::SetStderrThreshold(DefaultStderrThreshold());
}

TEST(TestGlobals, ScopedStderrThreshold) {
  EXPECT_EQ(absl::StderrThreshold(), DefaultStderrThreshold());
  {
    absl::ScopedStderrThreshold scoped_stderr_threshold(
        absl::LogSeverityAtLeast::kError);
    EXPECT_EQ(absl::StderrThreshold(), absl::LogSeverityAtLeast::kError);
  }
  EXPECT_EQ(absl::StderrThreshold(), DefaultStderrThreshold());
}

TEST(TestGlobals, LogBacktraceAt) {
  EXPECT_FALSE(absl::log_internal::ShouldLogBacktraceAt("some_file.cc", 111));
  absl::SetLogBacktraceLocation("some_file.cc", 111);
  EXPECT_TRUE(absl::log_internal::ShouldLogBacktraceAt("some_file.cc", 111));
  EXPECT_FALSE(
      absl::log_internal::ShouldLogBacktraceAt("another_file.cc", 222));
}

TEST(TestGlobals, LogPrefix) {
  EXPECT_TRUE(absl::ShouldPrependLogPrefix());
  absl::EnableLogPrefix(false);
  EXPECT_FALSE(absl::ShouldPrependLogPrefix());
  absl::EnableLogPrefix(true);
  EXPECT_TRUE(absl::ShouldPrependLogPrefix());
}

TEST(TestGlobals, SetGlobalVLogLevel) {
  EXPECT_EQ(absl::SetGlobalVLogLevel(42), 0);
  EXPECT_EQ(absl::SetGlobalVLogLevel(1337), 42);
  // Restore the value since it affects the default unset module value for
  // `SetVLogLevel()`.
  EXPECT_EQ(absl::SetGlobalVLogLevel(0), 1337);
}

TEST(TestGlobals, SetVLogLevel) {
  EXPECT_EQ(absl::SetVLogLevel("setvloglevel", 42), 0);
  EXPECT_EQ(absl::SetVLogLevel("setvloglevel", 1337), 42);
  EXPECT_EQ(absl::SetVLogLevel("othersetvloglevel", 50), 0);

  EXPECT_EQ(absl::SetVLogLevel("*pattern*", 1), 0);
  EXPECT_EQ(absl::SetVLogLevel("*less_generic_pattern*", 2), 1);
  // "pattern_match" matches the pattern "*pattern*". Therefore, the previous
  // level must be 1.
  EXPECT_EQ(absl::SetVLogLevel("pattern_match", 3), 1);
  // "less_generic_pattern_match" matches the pattern "*pattern*". Therefore,
  // the previous level must be 2.
  EXPECT_EQ(absl::SetVLogLevel("less_generic_pattern_match", 4), 2);
}

TEST(TestGlobals, AndroidLogTag) {
  // Verify invalid tags result in a check failure.
  EXPECT_DEATH_IF_SUPPORTED(absl::SetAndroidNativeTag(nullptr), ".*");

  // Verify valid tags applied.
  EXPECT_THAT(absl::log_internal::GetAndroidNativeTag(), StrEq("native"));
  absl::SetAndroidNativeTag("test_tag");
  EXPECT_THAT(absl::log_internal::GetAndroidNativeTag(), StrEq("test_tag"));

  // Verify that additional calls (more than 1) result in a check failure.
  EXPECT_DEATH_IF_SUPPORTED(absl::SetAndroidNativeTag("test_tag_fail"), ".*");
}

TEST(TestExitOnDFatal, OffTest) {
  // Turn off...
  absl::log_internal::SetExitOnDFatal(false);
  EXPECT_FALSE(absl::log_internal::ExitOnDFatal());

  // We don't die.
  {
    absl::ScopedMockLog log(absl::MockLogDefault::kDisallowUnexpected);

    // LOG(DFATAL) has severity FATAL if debugging, but is
    // downgraded to ERROR if not debugging.
    EXPECT_CALL(log, Log(absl::kLogDebugFatal, _, "This should not be fatal"));

    log.StartCapturingLogs();
    LOG(DFATAL) << "This should not be fatal";
  }
}

#if GTEST_HAS_DEATH_TEST
TEST(TestDeathWhileExitOnDFatal, OnTest) {
  absl::log_internal::SetExitOnDFatal(true);
  EXPECT_TRUE(absl::log_internal::ExitOnDFatal());

  // Death comes on little cats' feet.
  EXPECT_DEBUG_DEATH({ LOG(DFATAL) << "This should be fatal in debug mode"; },
                     "This should be fatal in debug mode");
}
#endif

}  // namespace
