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

#include "absl/log/log_sink.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/log/internal/test_actions.h"
#include "absl/log/internal/test_helpers.h"
#include "absl/log/internal/test_matchers.h"
#include "absl/log/log.h"
#include "absl/log/log_sink_registry.h"
#include "absl/log/scoped_mock_log.h"
#include "absl/strings/string_view.h"

namespace {

using ::absl::log_internal::DeathTestExpectedLogging;
using ::absl::log_internal::DeathTestUnexpectedLogging;
using ::absl::log_internal::DeathTestValidateExpectations;
using ::absl::log_internal::DiedOfFatal;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::HasSubstr;
using ::testing::InSequence;

auto* test_env ABSL_ATTRIBUTE_UNUSED = ::testing::AddGlobalTestEnvironment(
    new absl::log_internal::LogTestEnvironment);

// Tests for global log sink registration.
// ---------------------------------------

TEST(LogSinkRegistryTest, AddLogSink) {
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  InSequence s;
  EXPECT_CALL(test_sink, Log(_, _, "hello world")).Times(0);
  EXPECT_CALL(test_sink, Log(absl::LogSeverity::kInfo, __FILE__, "Test : 42"));
  EXPECT_CALL(test_sink,
              Log(absl::LogSeverity::kWarning, __FILE__, "Danger ahead"));
  EXPECT_CALL(test_sink,
              Log(absl::LogSeverity::kError, __FILE__, "This is an error"));

  LOG(INFO) << "hello world";
  test_sink.StartCapturingLogs();

  LOG(INFO) << "Test : " << 42;
  LOG(WARNING) << "Danger" << ' ' << "ahead";
  LOG(ERROR) << "This is an error";

  test_sink.StopCapturingLogs();
  LOG(INFO) << "Goodby world";
}

TEST(LogSinkRegistryTest, MultipleLogSinks) {
  absl::ScopedMockLog test_sink1(absl::MockLogDefault::kDisallowUnexpected);
  absl::ScopedMockLog test_sink2(absl::MockLogDefault::kDisallowUnexpected);

  ::testing::InSequence seq;
  EXPECT_CALL(test_sink1, Log(absl::LogSeverity::kInfo, _, "First")).Times(1);
  EXPECT_CALL(test_sink2, Log(absl::LogSeverity::kInfo, _, "First")).Times(0);

  EXPECT_CALL(test_sink1, Log(absl::LogSeverity::kInfo, _, "Second")).Times(1);
  EXPECT_CALL(test_sink2, Log(absl::LogSeverity::kInfo, _, "Second")).Times(1);

  EXPECT_CALL(test_sink1, Log(absl::LogSeverity::kInfo, _, "Third")).Times(0);
  EXPECT_CALL(test_sink2, Log(absl::LogSeverity::kInfo, _, "Third")).Times(1);

  LOG(INFO) << "Before first";

  test_sink1.StartCapturingLogs();
  LOG(INFO) << "First";

  test_sink2.StartCapturingLogs();
  LOG(INFO) << "Second";

  test_sink1.StopCapturingLogs();
  LOG(INFO) << "Third";

  test_sink2.StopCapturingLogs();
  LOG(INFO) << "Fourth";
}

TEST(LogSinkRegistrationDeathTest, DuplicateSinkRegistration) {
  ASSERT_DEATH_IF_SUPPORTED(
      {
        absl::ScopedMockLog sink;
        sink.StartCapturingLogs();
        absl::AddLogSink(&sink.UseAsLocalSink());
      },
      HasSubstr("Duplicate log sinks"));
}

TEST(LogSinkRegistrationDeathTest, MismatchSinkRemoval) {
  ASSERT_DEATH_IF_SUPPORTED(
      {
        absl::ScopedMockLog sink;
        absl::RemoveLogSink(&sink.UseAsLocalSink());
      },
      HasSubstr("Mismatched log sink"));
}

// Tests for log sink semantic.
// ---------------------------------------

TEST(LogSinkTest, FlushSinks) {
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(test_sink, Flush()).Times(2);

  test_sink.StartCapturingLogs();

  absl::FlushLogSinks();
  absl::FlushLogSinks();
}

TEST(LogSinkDeathTest, DeathInSend) {
  class FatalSendSink : public absl::LogSink {
   public:
    void Send(const absl::LogEntry&) override { LOG(FATAL) << "goodbye world"; }
  };

  FatalSendSink sink;
  EXPECT_EXIT({ LOG(INFO).ToSinkAlso(&sink) << "hello world"; }, DiedOfFatal,
              _);
}

// Tests for explicit log sink redirection.
// ---------------------------------------

TEST(LogSinkTest, ToSinkAlso) {
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);
  absl::ScopedMockLog another_sink(absl::MockLogDefault::kDisallowUnexpected);
  EXPECT_CALL(test_sink, Log(_, _, "hello world"));
  EXPECT_CALL(another_sink, Log(_, _, "hello world"));

  test_sink.StartCapturingLogs();
  LOG(INFO).ToSinkAlso(&another_sink.UseAsLocalSink()) << "hello world";
}

TEST(LogSinkTest, ToSinkOnly) {
  absl::ScopedMockLog another_sink(absl::MockLogDefault::kDisallowUnexpected);
  EXPECT_CALL(another_sink, Log(_, _, "hello world"));
  LOG(INFO).ToSinkOnly(&another_sink.UseAsLocalSink()) << "hello world";
}

TEST(LogSinkTest, ToManySinks) {
  absl::ScopedMockLog sink1(absl::MockLogDefault::kDisallowUnexpected);
  absl::ScopedMockLog sink2(absl::MockLogDefault::kDisallowUnexpected);
  absl::ScopedMockLog sink3(absl::MockLogDefault::kDisallowUnexpected);
  absl::ScopedMockLog sink4(absl::MockLogDefault::kDisallowUnexpected);
  absl::ScopedMockLog sink5(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(sink3, Log(_, _, "hello world"));
  EXPECT_CALL(sink4, Log(_, _, "hello world"));
  EXPECT_CALL(sink5, Log(_, _, "hello world"));

  LOG(INFO)
          .ToSinkAlso(&sink1.UseAsLocalSink())
          .ToSinkAlso(&sink2.UseAsLocalSink())
          .ToSinkOnly(&sink3.UseAsLocalSink())
          .ToSinkAlso(&sink4.UseAsLocalSink())
          .ToSinkAlso(&sink5.UseAsLocalSink())
      << "hello world";
}

class ReentrancyTest : public ::testing::Test {
 protected:
  ReentrancyTest() = default;
  enum class LogMode : int { kNormal, kToSinkAlso, kToSinkOnly };

  class ReentrantSendLogSink : public absl::LogSink {
   public:
    explicit ReentrantSendLogSink(absl::LogSeverity severity,
                                  absl::LogSink* sink, LogMode mode)
        : severity_(severity), sink_(sink), mode_(mode) {}
    explicit ReentrantSendLogSink(absl::LogSeverity severity)
        : ReentrantSendLogSink(severity, nullptr, LogMode::kNormal) {}

    void Send(const absl::LogEntry&) override {
      switch (mode_) {
        case LogMode::kNormal:
          LOG(LEVEL(severity_)) << "The log is coming from *inside the sink*.";
          break;
        case LogMode::kToSinkAlso:
          LOG(LEVEL(severity_)).ToSinkAlso(sink_)
              << "The log is coming from *inside the sink*.";
          break;
        case LogMode::kToSinkOnly:
          LOG(LEVEL(severity_)).ToSinkOnly(sink_)
              << "The log is coming from *inside the sink*.";
          break;
        default:
          LOG(FATAL) << "Invalid mode " << static_cast<int>(mode_);
      }
    }

   private:
    absl::LogSeverity severity_;
    absl::LogSink* sink_;
    LogMode mode_;
  };

  static absl::string_view LogAndReturn(absl::LogSeverity severity,
                                        absl::string_view to_log,
                                        absl::string_view to_return) {
    LOG(LEVEL(severity)) << to_log;
    return to_return;
  }
};

TEST_F(ReentrancyTest, LogFunctionThatLogs) {
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  InSequence seq;
  EXPECT_CALL(test_sink, Log(absl::LogSeverity::kInfo, _, "hello"));
  EXPECT_CALL(test_sink, Log(absl::LogSeverity::kInfo, _, "world"));
  EXPECT_CALL(test_sink, Log(absl::LogSeverity::kWarning, _, "danger"));
  EXPECT_CALL(test_sink, Log(absl::LogSeverity::kInfo, _, "here"));

  test_sink.StartCapturingLogs();
  LOG(INFO) << LogAndReturn(absl::LogSeverity::kInfo, "hello", "world");
  LOG(INFO) << LogAndReturn(absl::LogSeverity::kWarning, "danger", "here");
}

TEST_F(ReentrancyTest, RegisteredLogSinkThatLogsInSend) {
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);
  ReentrantSendLogSink renentrant_sink(absl::LogSeverity::kInfo);
  EXPECT_CALL(test_sink, Log(_, _, "hello world"));

  test_sink.StartCapturingLogs();
  absl::AddLogSink(&renentrant_sink);
  LOG(INFO) << "hello world";
  absl::RemoveLogSink(&renentrant_sink);
}

TEST_F(ReentrancyTest, AlsoLogSinkThatLogsInSend) {
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);
  ReentrantSendLogSink reentrant_sink(absl::LogSeverity::kInfo);
  EXPECT_CALL(test_sink, Log(_, _, "hello world"));
  EXPECT_CALL(test_sink,
              Log(_, _, "The log is coming from *inside the sink*."));

  test_sink.StartCapturingLogs();
  LOG(INFO).ToSinkAlso(&reentrant_sink) << "hello world";
}

TEST_F(ReentrancyTest, RegisteredAlsoLogSinkThatLogsInSend) {
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);
  ReentrantSendLogSink reentrant_sink(absl::LogSeverity::kInfo);
  EXPECT_CALL(test_sink, Log(_, _, "hello world"));
  // We only call into the test_log sink once with this message, since the
  // second time log statement is run we are in "ThreadIsLogging" mode and all
  // the log statements are redirected into stderr.
  EXPECT_CALL(test_sink,
              Log(_, _, "The log is coming from *inside the sink*."));

  test_sink.StartCapturingLogs();
  absl::AddLogSink(&reentrant_sink);
  LOG(INFO).ToSinkAlso(&reentrant_sink) << "hello world";
  absl::RemoveLogSink(&reentrant_sink);
}

TEST_F(ReentrancyTest, OnlyLogSinkThatLogsInSend) {
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);
  ReentrantSendLogSink reentrant_sink(absl::LogSeverity::kInfo);
  EXPECT_CALL(test_sink,
              Log(_, _, "The log is coming from *inside the sink*."));

  test_sink.StartCapturingLogs();
  LOG(INFO).ToSinkOnly(&reentrant_sink) << "hello world";
}

TEST_F(ReentrancyTest, RegisteredOnlyLogSinkThatLogsInSend) {
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);
  ReentrantSendLogSink reentrant_sink(absl::LogSeverity::kInfo);
  EXPECT_CALL(test_sink,
              Log(_, _, "The log is coming from *inside the sink*."));

  test_sink.StartCapturingLogs();
  absl::AddLogSink(&reentrant_sink);
  LOG(INFO).ToSinkOnly(&reentrant_sink) << "hello world";
  absl::RemoveLogSink(&reentrant_sink);
}

using ReentrancyDeathTest = ReentrancyTest;

TEST_F(ReentrancyDeathTest, LogFunctionThatLogsFatal) {
  EXPECT_EXIT(
      {
        absl::ScopedMockLog test_sink;

        EXPECT_CALL(test_sink, Log)
            .Times(AnyNumber())
            .WillRepeatedly(DeathTestUnexpectedLogging());
        EXPECT_CALL(test_sink, Log(_, _, "hello"))
            .WillOnce(DeathTestExpectedLogging());

        test_sink.StartCapturingLogs();
        LOG(INFO) << LogAndReturn(absl::LogSeverity::kFatal, "hello", "world");
      },
      DiedOfFatal, DeathTestValidateExpectations());
}

TEST_F(ReentrancyDeathTest, RegisteredLogSinkThatLogsFatalInSend) {
  EXPECT_EXIT(
      {
        absl::ScopedMockLog test_sink;
        ReentrantSendLogSink reentrant_sink(absl::LogSeverity::kFatal);
        EXPECT_CALL(test_sink, Log)
            .Times(AnyNumber())
            .WillRepeatedly(DeathTestUnexpectedLogging());
        EXPECT_CALL(test_sink, Log(_, _, "hello world"))
            .WillOnce(DeathTestExpectedLogging());

        test_sink.StartCapturingLogs();
        absl::AddLogSink(&reentrant_sink);
        LOG(INFO) << "hello world";
        // No need to call RemoveLogSink - process is dead at this point.
      },
      DiedOfFatal, DeathTestValidateExpectations());
}

TEST_F(ReentrancyDeathTest, AlsoLogSinkThatLogsFatalInSend) {
  EXPECT_EXIT(
      {
        absl::ScopedMockLog test_sink;
        ReentrantSendLogSink reentrant_sink(absl::LogSeverity::kFatal);

        EXPECT_CALL(test_sink, Log)
            .Times(AnyNumber())
            .WillRepeatedly(DeathTestUnexpectedLogging());
        EXPECT_CALL(test_sink, Log(_, _, "hello world"))
            .WillOnce(DeathTestExpectedLogging());
        EXPECT_CALL(test_sink,
                    Log(_, _, "The log is coming from *inside the sink*."))
            .WillOnce(DeathTestExpectedLogging());

        test_sink.StartCapturingLogs();
        LOG(INFO).ToSinkAlso(&reentrant_sink) << "hello world";
      },
      DiedOfFatal, DeathTestValidateExpectations());
}

TEST_F(ReentrancyDeathTest, RegisteredAlsoLogSinkThatLogsFatalInSend) {
  EXPECT_EXIT(
      {
        absl::ScopedMockLog test_sink;
        ReentrantSendLogSink reentrant_sink(absl::LogSeverity::kFatal);
        EXPECT_CALL(test_sink, Log)
            .Times(AnyNumber())
            .WillRepeatedly(DeathTestUnexpectedLogging());
        EXPECT_CALL(test_sink, Log(_, _, "hello world"))
            .WillOnce(DeathTestExpectedLogging());
        EXPECT_CALL(test_sink,
                    Log(_, _, "The log is coming from *inside the sink*."))
            .WillOnce(DeathTestExpectedLogging());

        test_sink.StartCapturingLogs();
        absl::AddLogSink(&reentrant_sink);
        LOG(INFO).ToSinkAlso(&reentrant_sink) << "hello world";
        // No need to call RemoveLogSink - process is dead at this point.
      },
      DiedOfFatal, DeathTestValidateExpectations());
}

TEST_F(ReentrancyDeathTest, OnlyLogSinkThatLogsFatalInSend) {
  EXPECT_EXIT(
      {
        absl::ScopedMockLog test_sink;
        ReentrantSendLogSink reentrant_sink(absl::LogSeverity::kFatal);
        EXPECT_CALL(test_sink, Log)
            .Times(AnyNumber())
            .WillRepeatedly(DeathTestUnexpectedLogging());
        EXPECT_CALL(test_sink,
                    Log(_, _, "The log is coming from *inside the sink*."))
            .WillOnce(DeathTestExpectedLogging());

        test_sink.StartCapturingLogs();
        LOG(INFO).ToSinkOnly(&reentrant_sink) << "hello world";
      },
      DiedOfFatal, DeathTestValidateExpectations());
}

TEST_F(ReentrancyDeathTest, RegisteredOnlyLogSinkThatLogsFatalInSend) {
  EXPECT_EXIT(
      {
        absl::ScopedMockLog test_sink;
        ReentrantSendLogSink reentrant_sink(absl::LogSeverity::kFatal);
        EXPECT_CALL(test_sink, Log)
            .Times(AnyNumber())
            .WillRepeatedly(DeathTestUnexpectedLogging());
        EXPECT_CALL(test_sink,
                    Log(_, _, "The log is coming from *inside the sink*."))
            .WillOnce(DeathTestExpectedLogging());

        test_sink.StartCapturingLogs();
        absl::AddLogSink(&reentrant_sink);
        LOG(INFO).ToSinkOnly(&reentrant_sink) << "hello world";
        // No need to call RemoveLogSink - process is dead at this point.
      },
      DiedOfFatal, DeathTestValidateExpectations());
}

}  // namespace
