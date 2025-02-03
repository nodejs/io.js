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

// The testcases in this file are expected to pass or be skipped with any value
// of ABSL_MIN_LOG_LEVEL

#ifndef ABSL_LOG_LOG_BASIC_TEST_IMPL_H_
#define ABSL_LOG_LOG_BASIC_TEST_IMPL_H_

// Verify that both sets of macros behave identically by parameterizing the
// entire test file.
#ifndef ABSL_TEST_LOG
#error ABSL_TEST_LOG must be defined for these tests to work.
#endif

#ifndef ABSL_TEST_DLOG
#error ABSL_TEST_DLOG must be defined for these tests to work.
#endif

#include <cerrno>
#include <sstream>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/internal/sysinfo.h"
#include "absl/base/log_severity.h"
#include "absl/log/globals.h"
#include "absl/log/internal/globals.h"
#include "absl/log/internal/test_actions.h"
#include "absl/log/internal/test_helpers.h"
#include "absl/log/internal/test_matchers.h"
#include "absl/log/log_entry.h"
#include "absl/log/scoped_mock_log.h"

namespace absl_log_internal {
#if GTEST_HAS_DEATH_TEST
using ::absl::log_internal::DeathTestExpectedLogging;
using ::absl::log_internal::DeathTestUnexpectedLogging;
using ::absl::log_internal::DeathTestValidateExpectations;
using ::absl::log_internal::DiedOfFatal;
using ::absl::log_internal::DiedOfQFatal;
#endif
using ::absl::log_internal::InMatchWindow;
using ::absl::log_internal::LoggingEnabledAt;
using ::absl::log_internal::LogSeverity;
using ::absl::log_internal::Prefix;
using ::absl::log_internal::SourceBasename;
using ::absl::log_internal::SourceFilename;
using ::absl::log_internal::SourceLine;
using ::absl::log_internal::Stacktrace;
using ::absl::log_internal::TextMessage;
using ::absl::log_internal::ThreadID;
using ::absl::log_internal::Timestamp;
using ::absl::log_internal::Verbosity;
using ::testing::AnyNumber;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::IsTrue;

class BasicLogTest : public testing::TestWithParam<absl::LogSeverityAtLeast> {};

std::string ThresholdName(
    testing::TestParamInfo<absl::LogSeverityAtLeast> severity) {
  std::stringstream ostr;
  ostr << severity.param;
  return ostr.str().substr(
      severity.param == absl::LogSeverityAtLeast::kInfinity ? 0 : 2);
}

INSTANTIATE_TEST_SUITE_P(WithParam, BasicLogTest,
                         testing::Values(absl::LogSeverityAtLeast::kInfo,
                                         absl::LogSeverityAtLeast::kWarning,
                                         absl::LogSeverityAtLeast::kError,
                                         absl::LogSeverityAtLeast::kFatal,
                                         absl::LogSeverityAtLeast::kInfinity),
                         ThresholdName);

TEST_P(BasicLogTest, Info) {
  absl::log_internal::ScopedMinLogLevel scoped_min_log_level(GetParam());

  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  const int log_line = __LINE__ + 1;
  auto do_log = [] { ABSL_TEST_LOG(INFO) << "hello world"; };

  if (LoggingEnabledAt(absl::LogSeverity::kInfo)) {
    EXPECT_CALL(
        test_sink,
        Send(AllOf(
            SourceFilename(Eq(__FILE__)),
            SourceBasename(Eq("log_basic_test_impl.inc")),
            SourceLine(Eq(log_line)), Prefix(IsTrue()),
            LogSeverity(Eq(absl::LogSeverity::kInfo)),
            Timestamp(InMatchWindow()),
            ThreadID(Eq(absl::base_internal::GetTID())),
            TextMessage(Eq("hello world")),
            Verbosity(Eq(absl::LogEntry::kNoVerbosityLevel)),
            ENCODED_MESSAGE(MatchesEvent(
                Eq(__FILE__), Eq(log_line), InMatchWindow(),
                Eq(logging::proto::INFO), Eq(absl::base_internal::GetTID()),
                ElementsAre(EqualsProto(R"pb(literal: "hello world")pb")))),
            Stacktrace(IsEmpty()))));
  }

  test_sink.StartCapturingLogs();
  do_log();
}

TEST_P(BasicLogTest, Warning) {
  absl::log_internal::ScopedMinLogLevel scoped_min_log_level(GetParam());

  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  const int log_line = __LINE__ + 1;
  auto do_log = [] { ABSL_TEST_LOG(WARNING) << "hello world"; };

  if (LoggingEnabledAt(absl::LogSeverity::kWarning)) {
    EXPECT_CALL(
        test_sink,
        Send(AllOf(
            SourceFilename(Eq(__FILE__)),
            SourceBasename(Eq("log_basic_test_impl.inc")),
            SourceLine(Eq(log_line)), Prefix(IsTrue()),
            LogSeverity(Eq(absl::LogSeverity::kWarning)),
            Timestamp(InMatchWindow()),
            ThreadID(Eq(absl::base_internal::GetTID())),
            TextMessage(Eq("hello world")),
            Verbosity(Eq(absl::LogEntry::kNoVerbosityLevel)),
            ENCODED_MESSAGE(MatchesEvent(
                Eq(__FILE__), Eq(log_line), InMatchWindow(),
                Eq(logging::proto::WARNING), Eq(absl::base_internal::GetTID()),
                ElementsAre(EqualsProto(R"pb(literal: "hello world")pb")))),
            Stacktrace(IsEmpty()))));
  }

  test_sink.StartCapturingLogs();
  do_log();
}

TEST_P(BasicLogTest, Error) {
  absl::log_internal::ScopedMinLogLevel scoped_min_log_level(GetParam());

  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  const int log_line = __LINE__ + 1;
  auto do_log = [] { ABSL_TEST_LOG(ERROR) << "hello world"; };

  if (LoggingEnabledAt(absl::LogSeverity::kError)) {
    EXPECT_CALL(
        test_sink,
        Send(AllOf(
            SourceFilename(Eq(__FILE__)),
            SourceBasename(Eq("log_basic_test_impl.inc")),
            SourceLine(Eq(log_line)), Prefix(IsTrue()),
            LogSeverity(Eq(absl::LogSeverity::kError)),
            Timestamp(InMatchWindow()),
            ThreadID(Eq(absl::base_internal::GetTID())),
            TextMessage(Eq("hello world")),
            Verbosity(Eq(absl::LogEntry::kNoVerbosityLevel)),
            ENCODED_MESSAGE(MatchesEvent(
                Eq(__FILE__), Eq(log_line), InMatchWindow(),
                Eq(logging::proto::ERROR), Eq(absl::base_internal::GetTID()),
                ElementsAre(EqualsProto(R"pb(literal: "hello world")pb")))),
            Stacktrace(IsEmpty()))));
  }

  test_sink.StartCapturingLogs();
  do_log();
}

#if GTEST_HAS_DEATH_TEST
using BasicLogDeathTest = BasicLogTest;

INSTANTIATE_TEST_SUITE_P(WithParam, BasicLogDeathTest,
                         testing::Values(absl::LogSeverityAtLeast::kInfo,
                                         absl::LogSeverityAtLeast::kFatal,
                                         absl::LogSeverityAtLeast::kInfinity),
                         ThresholdName);

TEST_P(BasicLogDeathTest, Fatal) {
  absl::log_internal::ScopedMinLogLevel scoped_min_log_level(GetParam());

  const int log_line = __LINE__ + 1;
  auto do_log = [] { ABSL_TEST_LOG(FATAL) << "hello world"; };

  EXPECT_EXIT(
      {
        absl::ScopedMockLog test_sink(
            absl::MockLogDefault::kDisallowUnexpected);

        EXPECT_CALL(test_sink, Send)
            .Times(AnyNumber())
            .WillRepeatedly(DeathTestUnexpectedLogging());

        ::testing::InSequence s;

        // Note the logic in DeathTestValidateExpectations() caters for the case
        // of logging being disabled at FATAL level.

        if (LoggingEnabledAt(absl::LogSeverity::kFatal)) {
          // The first call without the stack trace.
          EXPECT_CALL(
              test_sink,
              Send(AllOf(SourceFilename(Eq(__FILE__)),
                         SourceBasename(Eq("log_basic_test_impl.inc")),
                         SourceLine(Eq(log_line)), Prefix(IsTrue()),
                         LogSeverity(Eq(absl::LogSeverity::kFatal)),
                         Timestamp(InMatchWindow()),
                         ThreadID(Eq(absl::base_internal::GetTID())),
                         TextMessage(Eq("hello world")),
                         Verbosity(Eq(absl::LogEntry::kNoVerbosityLevel)),
                         ENCODED_MESSAGE(MatchesEvent(
                             Eq(__FILE__), Eq(log_line), InMatchWindow(),
                             Eq(logging::proto::FATAL),
                             Eq(absl::base_internal::GetTID()),
                             ElementsAre(EqualsProto(
                                 R"pb(literal: "hello world")pb")))),
                         Stacktrace(IsEmpty()))))
              .WillOnce(DeathTestExpectedLogging());

          // The second call with the stack trace.
          EXPECT_CALL(
              test_sink,
              Send(AllOf(SourceFilename(Eq(__FILE__)),
                         SourceBasename(Eq("log_basic_test_impl.inc")),
                         SourceLine(Eq(log_line)), Prefix(IsTrue()),
                         LogSeverity(Eq(absl::LogSeverity::kFatal)),
                         Timestamp(InMatchWindow()),
                         ThreadID(Eq(absl::base_internal::GetTID())),
                         TextMessage(Eq("hello world")),
                         Verbosity(Eq(absl::LogEntry::kNoVerbosityLevel)),
                         ENCODED_MESSAGE(MatchesEvent(
                             Eq(__FILE__), Eq(log_line), InMatchWindow(),
                             Eq(logging::proto::FATAL),
                             Eq(absl::base_internal::GetTID()),
                             ElementsAre(EqualsProto(
                                 R"pb(literal: "hello world")pb")))),
                         Stacktrace(Not(IsEmpty())))))
              .WillOnce(DeathTestExpectedLogging());
        }

        test_sink.StartCapturingLogs();
        do_log();
      },
      DiedOfFatal, DeathTestValidateExpectations());
}

TEST_P(BasicLogDeathTest, QFatal) {
  absl::log_internal::ScopedMinLogLevel scoped_min_log_level(GetParam());

  const int log_line = __LINE__ + 1;
  auto do_log = [] { ABSL_TEST_LOG(QFATAL) << "hello world"; };

  EXPECT_EXIT(
      {
        absl::ScopedMockLog test_sink(
            absl::MockLogDefault::kDisallowUnexpected);

        EXPECT_CALL(test_sink, Send)
            .Times(AnyNumber())
            .WillRepeatedly(DeathTestUnexpectedLogging());

        if (LoggingEnabledAt(absl::LogSeverity::kFatal)) {
          EXPECT_CALL(
              test_sink,
              Send(AllOf(SourceFilename(Eq(__FILE__)),
                         SourceBasename(Eq("log_basic_test_impl.inc")),
                         SourceLine(Eq(log_line)), Prefix(IsTrue()),
                         LogSeverity(Eq(absl::LogSeverity::kFatal)),
                         Timestamp(InMatchWindow()),
                         ThreadID(Eq(absl::base_internal::GetTID())),
                         TextMessage(Eq("hello world")),
                         Verbosity(Eq(absl::LogEntry::kNoVerbosityLevel)),
                         ENCODED_MESSAGE(MatchesEvent(
                             Eq(__FILE__), Eq(log_line), InMatchWindow(),
                             Eq(logging::proto::FATAL),
                             Eq(absl::base_internal::GetTID()),
                             ElementsAre(EqualsProto(
                                 R"pb(literal: "hello world")pb")))),
                         Stacktrace(IsEmpty()))))
              .WillOnce(DeathTestExpectedLogging());
        }

        test_sink.StartCapturingLogs();
        do_log();
      },
      DiedOfQFatal, DeathTestValidateExpectations());
}
#endif

#ifdef NDEBUG
TEST_P(BasicLogTest, DFatal) {
  absl::log_internal::ScopedMinLogLevel scoped_min_log_level(GetParam());

  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  const int log_line = __LINE__ + 1;
  auto do_log = [] { ABSL_TEST_LOG(DFATAL) << "hello world"; };

  if (LoggingEnabledAt(absl::LogSeverity::kError)) {
    EXPECT_CALL(
        test_sink,
        Send(AllOf(
            SourceFilename(Eq(__FILE__)),
            SourceBasename(Eq("log_basic_test_impl.inc")),
            SourceLine(Eq(log_line)), Prefix(IsTrue()),
            LogSeverity(Eq(absl::LogSeverity::kError)),
            Timestamp(InMatchWindow()),
            ThreadID(Eq(absl::base_internal::GetTID())),
            TextMessage(Eq("hello world")),
            Verbosity(Eq(absl::LogEntry::kNoVerbosityLevel)),
            ENCODED_MESSAGE(MatchesEvent(
                Eq(__FILE__), Eq(log_line), InMatchWindow(),
                Eq(logging::proto::ERROR), Eq(absl::base_internal::GetTID()),
                ElementsAre(EqualsProto(R"pb(literal: "hello world")pb")))),
            Stacktrace(IsEmpty()))));
  }

  test_sink.StartCapturingLogs();
  do_log();
}

#elif GTEST_HAS_DEATH_TEST
TEST_P(BasicLogDeathTest, DFatal) {
  // TODO(b/242568884): re-enable once bug is fixed.
  // absl::log_internal::ScopedMinLogLevel scoped_min_log_level(GetParam());

  const int log_line = __LINE__ + 1;
  auto do_log = [] { ABSL_TEST_LOG(DFATAL) << "hello world"; };

  EXPECT_EXIT(
      {
        absl::ScopedMockLog test_sink(
            absl::MockLogDefault::kDisallowUnexpected);

        EXPECT_CALL(test_sink, Send)
            .Times(AnyNumber())
            .WillRepeatedly(DeathTestUnexpectedLogging());

        ::testing::InSequence s;

        if (LoggingEnabledAt(absl::LogSeverity::kFatal)) {
          // The first call without the stack trace.
          EXPECT_CALL(
              test_sink,
              Send(AllOf(SourceFilename(Eq(__FILE__)),
                         SourceBasename(Eq("log_basic_test_impl.inc")),
                         SourceLine(Eq(log_line)), Prefix(IsTrue()),
                         LogSeverity(Eq(absl::LogSeverity::kFatal)),
                         Timestamp(InMatchWindow()),
                         ThreadID(Eq(absl::base_internal::GetTID())),
                         TextMessage(Eq("hello world")),
                         Verbosity(Eq(absl::LogEntry::kNoVerbosityLevel)),
                         ENCODED_MESSAGE(MatchesEvent(
                             Eq(__FILE__), Eq(log_line), InMatchWindow(),
                             Eq(logging::proto::FATAL),
                             Eq(absl::base_internal::GetTID()),
                             ElementsAre(EqualsProto(
                                 R"pb(literal: "hello world")pb")))),
                         Stacktrace(IsEmpty()))))
              .WillOnce(DeathTestExpectedLogging());

          // The second call with the stack trace.
          EXPECT_CALL(
              test_sink,
              Send(AllOf(SourceFilename(Eq(__FILE__)),
                         SourceBasename(Eq("log_basic_test_impl.inc")),
                         SourceLine(Eq(log_line)), Prefix(IsTrue()),
                         LogSeverity(Eq(absl::LogSeverity::kFatal)),
                         Timestamp(InMatchWindow()),
                         ThreadID(Eq(absl::base_internal::GetTID())),
                         TextMessage(Eq("hello world")),
                         Verbosity(Eq(absl::LogEntry::kNoVerbosityLevel)),
                         ENCODED_MESSAGE(MatchesEvent(
                             Eq(__FILE__), Eq(log_line), InMatchWindow(),
                             Eq(logging::proto::FATAL),
                             Eq(absl::base_internal::GetTID()),
                             ElementsAre(EqualsProto(
                                 R"pb(literal: "hello world")pb")))),
                         Stacktrace(Not(IsEmpty())))))
              .WillOnce(DeathTestExpectedLogging());
        }

        test_sink.StartCapturingLogs();
        do_log();
      },
      DiedOfFatal, DeathTestValidateExpectations());
}
#endif

#ifndef NDEBUG
TEST_P(BasicLogTest, DFatalIsCancellable) {
  // LOG(DFATAL) does not die when DFATAL death is disabled.
  absl::log_internal::SetExitOnDFatal(false);
  ABSL_TEST_LOG(DFATAL) << "hello world";
  absl::log_internal::SetExitOnDFatal(true);
}

#if GTEST_HAS_DEATH_TEST
TEST_P(BasicLogDeathTest, DLogFatalIsNotCancellable) {
  EXPECT_EXIT(
      {
        absl::log_internal::SetExitOnDFatal(false);
        ABSL_TEST_DLOG(FATAL) << "hello world";
        absl::log_internal::SetExitOnDFatal(true);
      },
      DiedOfFatal, "");
}
#endif
#endif

TEST_P(BasicLogTest, Level) {
  absl::log_internal::ScopedMinLogLevel scoped_min_log_level(GetParam());

  for (auto severity : {absl::LogSeverity::kInfo, absl::LogSeverity::kWarning,
                        absl::LogSeverity::kError}) {
    absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

    const int log_line = __LINE__ + 2;
    auto do_log = [severity] {
      ABSL_TEST_LOG(LEVEL(severity)) << "hello world";
    };

    if (LoggingEnabledAt(severity)) {
      EXPECT_CALL(
          test_sink,
          Send(AllOf(
              SourceFilename(Eq(__FILE__)),
              SourceBasename(Eq("log_basic_test_impl.inc")),
              SourceLine(Eq(log_line)), Prefix(IsTrue()),
              LogSeverity(Eq(severity)), Timestamp(InMatchWindow()),
              ThreadID(Eq(absl::base_internal::GetTID())),
              TextMessage(Eq("hello world")),
              Verbosity(Eq(absl::LogEntry::kNoVerbosityLevel)),
              ENCODED_MESSAGE(MatchesEvent(
                  Eq(__FILE__), Eq(log_line), InMatchWindow(),
                  Eq(severity == absl::LogSeverity::kInfo ? logging::proto::INFO
                     : severity == absl::LogSeverity::kWarning
                         ? logging::proto::WARNING
                     : severity == absl::LogSeverity::kError
                         ? logging::proto::ERROR
                         : 0),
                  Eq(absl::base_internal::GetTID()),
                  ElementsAre(EqualsProto(R"pb(literal: "hello world")pb")))),
              Stacktrace(IsEmpty()))));
    }
    test_sink.StartCapturingLogs();
    do_log();
  }
}

#if GTEST_HAS_DEATH_TEST
TEST_P(BasicLogDeathTest, Level) {
  // TODO(b/242568884): re-enable once bug is fixed.
  // absl::log_internal::ScopedMinLogLevel scoped_min_log_level(GetParam());

  // Ensure that `severity` is not a compile-time constant to prove that
  // `LOG(LEVEL(severity))` works regardless:
  auto volatile severity = absl::LogSeverity::kFatal;

  const int log_line = __LINE__ + 1;
  auto do_log = [severity] { ABSL_TEST_LOG(LEVEL(severity)) << "hello world"; };

  EXPECT_EXIT(
      {
        absl::ScopedMockLog test_sink(
            absl::MockLogDefault::kDisallowUnexpected);

        EXPECT_CALL(test_sink, Send)
            .Times(AnyNumber())
            .WillRepeatedly(DeathTestUnexpectedLogging());

        ::testing::InSequence s;

        if (LoggingEnabledAt(absl::LogSeverity::kFatal)) {
          EXPECT_CALL(
              test_sink,
              Send(AllOf(SourceFilename(Eq(__FILE__)),
                         SourceBasename(Eq("log_basic_test_impl.inc")),
                         SourceLine(Eq(log_line)), Prefix(IsTrue()),
                         LogSeverity(Eq(absl::LogSeverity::kFatal)),
                         Timestamp(InMatchWindow()),
                         ThreadID(Eq(absl::base_internal::GetTID())),
                         TextMessage(Eq("hello world")),
                         Verbosity(Eq(absl::LogEntry::kNoVerbosityLevel)),
                         ENCODED_MESSAGE(MatchesEvent(
                             Eq(__FILE__), Eq(log_line), InMatchWindow(),
                             Eq(logging::proto::FATAL),
                             Eq(absl::base_internal::GetTID()),
                             ElementsAre(EqualsProto(
                                 R"pb(literal: "hello world")pb")))),
                         Stacktrace(IsEmpty()))))
              .WillOnce(DeathTestExpectedLogging());

          EXPECT_CALL(
              test_sink,
              Send(AllOf(SourceFilename(Eq(__FILE__)),
                         SourceBasename(Eq("log_basic_test_impl.inc")),
                         SourceLine(Eq(log_line)), Prefix(IsTrue()),
                         LogSeverity(Eq(absl::LogSeverity::kFatal)),
                         Timestamp(InMatchWindow()),
                         ThreadID(Eq(absl::base_internal::GetTID())),
                         TextMessage(Eq("hello world")),
                         Verbosity(Eq(absl::LogEntry::kNoVerbosityLevel)),
                         ENCODED_MESSAGE(MatchesEvent(
                             Eq(__FILE__), Eq(log_line), InMatchWindow(),
                             Eq(logging::proto::FATAL),
                             Eq(absl::base_internal::GetTID()),
                             ElementsAre(EqualsProto(
                                 R"pb(literal: "hello world")pb")))),
                         Stacktrace(Not(IsEmpty())))))
              .WillOnce(DeathTestExpectedLogging());
        }

        test_sink.StartCapturingLogs();
        do_log();
      },
      DiedOfFatal, DeathTestValidateExpectations());
}
#endif

TEST_P(BasicLogTest, LevelClampsNegativeValues) {
  absl::log_internal::ScopedMinLogLevel scoped_min_log_level(GetParam());

  if (!LoggingEnabledAt(absl::LogSeverity::kInfo)) {
    GTEST_SKIP() << "This test cases required INFO log to be enabled";
    return;
  }

  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(test_sink, Send(LogSeverity(Eq(absl::LogSeverity::kInfo))));

  test_sink.StartCapturingLogs();
  ABSL_TEST_LOG(LEVEL(-1)) << "hello world";
}

TEST_P(BasicLogTest, LevelClampsLargeValues) {
  absl::log_internal::ScopedMinLogLevel scoped_min_log_level(GetParam());

  if (!LoggingEnabledAt(absl::LogSeverity::kError)) {
    GTEST_SKIP() << "This test cases required ERROR log to be enabled";
    return;
  }

  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(test_sink, Send(LogSeverity(Eq(absl::LogSeverity::kError))));

  test_sink.StartCapturingLogs();
  ABSL_TEST_LOG(LEVEL(static_cast<int>(absl::LogSeverity::kFatal) + 1))
      << "hello world";
}

TEST(ErrnoPreservationTest, InSeverityExpression) {
  errno = 77;
  int saved_errno;
  ABSL_TEST_LOG(LEVEL((saved_errno = errno, absl::LogSeverity::kInfo)));
  EXPECT_THAT(saved_errno, Eq(77));
}

TEST(ErrnoPreservationTest, InStreamedExpression) {
  if (!LoggingEnabledAt(absl::LogSeverity::kInfo)) {
    GTEST_SKIP() << "This test cases required INFO log to be enabled";
    return;
  }

  errno = 77;
  int saved_errno = 0;
  ABSL_TEST_LOG(INFO) << (saved_errno = errno, "hello world");
  EXPECT_THAT(saved_errno, Eq(77));
}

TEST(ErrnoPreservationTest, AfterStatement) {
  errno = 77;
  ABSL_TEST_LOG(INFO);
  const int saved_errno = errno;
  EXPECT_THAT(saved_errno, Eq(77));
}

// Tests that using a variable/parameter in a logging statement suppresses
// unused-variable/parameter warnings.
// -----------------------------------------------------------------------
class UnusedVariableWarningCompileTest {
  // These four don't prove anything unless `ABSL_MIN_LOG_LEVEL` is greater than
  // `kInfo`.
  static void LoggedVariable() {
    const int x = 0;
    ABSL_TEST_LOG(INFO) << x;
  }
  static void LoggedParameter(const int x) { ABSL_TEST_LOG(INFO) << x; }
  static void SeverityVariable() {
    const int x = 0;
    ABSL_TEST_LOG(LEVEL(x)) << "hello world";
  }
  static void SeverityParameter(const int x) {
    ABSL_TEST_LOG(LEVEL(x)) << "hello world";
  }
};

}  // namespace absl_log_internal

#endif  // ABSL_LOG_LOG_BASIC_TEST_IMPL_H_
