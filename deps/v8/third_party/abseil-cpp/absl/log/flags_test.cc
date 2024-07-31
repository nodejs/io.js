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

#include "absl/log/internal/flags.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/base/log_severity.h"
#include "absl/flags/flag.h"
#include "absl/flags/reflection.h"
#include "absl/log/globals.h"
#include "absl/log/internal/test_helpers.h"
#include "absl/log/internal/test_matchers.h"
#include "absl/log/log.h"
#include "absl/log/scoped_mock_log.h"
#include "absl/strings/str_cat.h"

namespace {
using ::absl::log_internal::TextMessage;

using ::testing::HasSubstr;
using ::testing::Not;

auto* test_env ABSL_ATTRIBUTE_UNUSED = ::testing::AddGlobalTestEnvironment(
    new absl::log_internal::LogTestEnvironment);

constexpr static absl::LogSeverityAtLeast DefaultStderrThreshold() {
  return absl::LogSeverityAtLeast::kError;
}

class LogFlagsTest : public ::testing::Test {
 protected:
  absl::FlagSaver flag_saver_;
};

// This test is disabled because it adds order dependency to the test suite.
// This order dependency is currently not fixable due to the way the
// stderrthreshold global value is out of sync with the stderrthreshold flag.
TEST_F(LogFlagsTest, DISABLED_StderrKnobsDefault) {
  EXPECT_EQ(absl::StderrThreshold(), DefaultStderrThreshold());
}

TEST_F(LogFlagsTest, SetStderrThreshold) {
  absl::SetFlag(&FLAGS_stderrthreshold,
                static_cast<int>(absl::LogSeverityAtLeast::kInfo));

  EXPECT_EQ(absl::StderrThreshold(), absl::LogSeverityAtLeast::kInfo);

  absl::SetFlag(&FLAGS_stderrthreshold,
                static_cast<int>(absl::LogSeverityAtLeast::kError));

  EXPECT_EQ(absl::StderrThreshold(), absl::LogSeverityAtLeast::kError);
}

TEST_F(LogFlagsTest, SetMinLogLevel) {
  absl::SetFlag(&FLAGS_minloglevel,
                static_cast<int>(absl::LogSeverityAtLeast::kError));

  EXPECT_EQ(absl::MinLogLevel(), absl::LogSeverityAtLeast::kError);

  absl::log_internal::ScopedMinLogLevel scoped_min_log_level(
      absl::LogSeverityAtLeast::kWarning);

  EXPECT_EQ(absl::GetFlag(FLAGS_minloglevel),
            static_cast<int>(absl::LogSeverityAtLeast::kWarning));
}

TEST_F(LogFlagsTest, PrependLogPrefix) {
  absl::SetFlag(&FLAGS_log_prefix, false);

  EXPECT_EQ(absl::ShouldPrependLogPrefix(), false);

  absl::EnableLogPrefix(true);

  EXPECT_EQ(absl::GetFlag(FLAGS_log_prefix), true);
}

TEST_F(LogFlagsTest, EmptyBacktraceAtFlag) {
  absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(test_sink, Send(TextMessage(Not(HasSubstr("(stacktrace:")))));

  test_sink.StartCapturingLogs();
  absl::SetFlag(&FLAGS_log_backtrace_at, "");
  LOG(INFO) << "hello world";
}

TEST_F(LogFlagsTest, BacktraceAtNonsense) {
  absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(test_sink, Send(TextMessage(Not(HasSubstr("(stacktrace:")))));

  test_sink.StartCapturingLogs();
  absl::SetFlag(&FLAGS_log_backtrace_at, "gibberish");
  LOG(INFO) << "hello world";
}

TEST_F(LogFlagsTest, BacktraceAtWrongFile) {
  absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);
  const int log_line = __LINE__ + 1;
  auto do_log = [] { LOG(INFO) << "hello world"; };
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(test_sink, Send(TextMessage(Not(HasSubstr("(stacktrace:")))));

  test_sink.StartCapturingLogs();
  absl::SetFlag(&FLAGS_log_backtrace_at,
                absl::StrCat("some_other_file.cc:", log_line));
  do_log();
}

TEST_F(LogFlagsTest, BacktraceAtWrongLine) {
  absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);
  const int log_line = __LINE__ + 1;
  auto do_log = [] { LOG(INFO) << "hello world"; };
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(test_sink, Send(TextMessage(Not(HasSubstr("(stacktrace:")))));

  test_sink.StartCapturingLogs();
  absl::SetFlag(&FLAGS_log_backtrace_at,
                absl::StrCat("flags_test.cc:", log_line + 1));
  do_log();
}

TEST_F(LogFlagsTest, BacktraceAtWholeFilename) {
  absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);
  const int log_line = __LINE__ + 1;
  auto do_log = [] { LOG(INFO) << "hello world"; };
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(test_sink, Send(TextMessage(Not(HasSubstr("(stacktrace:")))));

  test_sink.StartCapturingLogs();
  absl::SetFlag(&FLAGS_log_backtrace_at, absl::StrCat(__FILE__, ":", log_line));
  do_log();
}

TEST_F(LogFlagsTest, BacktraceAtNonmatchingSuffix) {
  absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);
  const int log_line = __LINE__ + 1;
  auto do_log = [] { LOG(INFO) << "hello world"; };
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(test_sink, Send(TextMessage(Not(HasSubstr("(stacktrace:")))));

  test_sink.StartCapturingLogs();
  absl::SetFlag(&FLAGS_log_backtrace_at,
                absl::StrCat("flags_test.cc:", log_line, "gibberish"));
  do_log();
}

TEST_F(LogFlagsTest, LogsBacktrace) {
  absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);
  const int log_line = __LINE__ + 1;
  auto do_log = [] { LOG(INFO) << "hello world"; };
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  testing::InSequence seq;
  EXPECT_CALL(test_sink, Send(TextMessage(HasSubstr("(stacktrace:"))));
  EXPECT_CALL(test_sink, Send(TextMessage(Not(HasSubstr("(stacktrace:")))));

  test_sink.StartCapturingLogs();
  absl::SetFlag(&FLAGS_log_backtrace_at,
                absl::StrCat("flags_test.cc:", log_line));
  do_log();
  absl::SetFlag(&FLAGS_log_backtrace_at, "");
  do_log();
}

}  // namespace
