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

#include "absl/log/scoped_mock_log.h"

#include <memory>
#include <thread>  // NOLINT(build/c++11)

#include "gmock/gmock.h"
#include "gtest/gtest-spi.h"
#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/base/log_severity.h"
#include "absl/log/globals.h"
#include "absl/log/internal/test_helpers.h"
#include "absl/log/internal/test_matchers.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/barrier.h"
#include "absl/synchronization/notification.h"

namespace {

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::InSequence;
using ::testing::Lt;
using ::testing::Truly;
using absl::log_internal::SourceBasename;
using absl::log_internal::SourceFilename;
using absl::log_internal::SourceLine;
using absl::log_internal::TextMessageWithPrefix;
using absl::log_internal::ThreadID;

auto* test_env ABSL_ATTRIBUTE_UNUSED = ::testing::AddGlobalTestEnvironment(
    new absl::log_internal::LogTestEnvironment);

#if GTEST_HAS_DEATH_TEST
TEST(ScopedMockLogDeathTest,
     StartCapturingLogsCannotBeCalledWhenAlreadyCapturing) {
  EXPECT_DEATH(
      {
        absl::ScopedMockLog log;
        log.StartCapturingLogs();
        log.StartCapturingLogs();
      },
      "StartCapturingLogs");
}

TEST(ScopedMockLogDeathTest, StopCapturingLogsCannotBeCalledWhenNotCapturing) {
  EXPECT_DEATH(
      {
        absl::ScopedMockLog log;
        log.StopCapturingLogs();
      },
      "StopCapturingLogs");
}

TEST(ScopedMockLogDeathTest, FailsCheckIfStartCapturingLogsIsNeverCalled) {
  EXPECT_DEATH({ absl::ScopedMockLog log; },
               "Did you forget to call StartCapturingLogs");
}
#endif

// Tests that ScopedMockLog intercepts LOG()s when it's alive.
TEST(ScopedMockLogTest, LogMockCatchAndMatchStrictExpectations) {
  absl::ScopedMockLog log;

  // The following expectations must match in the order they appear.
  InSequence s;
  EXPECT_CALL(log,
              Log(absl::LogSeverity::kWarning, HasSubstr(__FILE__), "Danger."));
  EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, _, "Working...")).Times(2);
  EXPECT_CALL(log, Log(absl::LogSeverity::kError, _, "Bad!!"));

  log.StartCapturingLogs();
  LOG(WARNING) << "Danger.";
  LOG(INFO) << "Working...";
  LOG(INFO) << "Working...";
  LOG(ERROR) << "Bad!!";
}

TEST(ScopedMockLogTest, LogMockCatchAndMatchSendExpectations) {
  absl::ScopedMockLog log;

  EXPECT_CALL(
      log,
      Send(AllOf(SourceFilename(Eq("/my/very/very/very_long_source_file.cc")),
                 SourceBasename(Eq("very_long_source_file.cc")),
                 SourceLine(Eq(777)), ThreadID(Eq(absl::LogEntry::tid_t{1234})),
                 TextMessageWithPrefix(Truly([](absl::string_view msg) {
                   return absl::EndsWith(
                       msg, " very_long_source_file.cc:777] Info message");
                 })))));

  log.StartCapturingLogs();
  LOG(INFO)
          .AtLocation("/my/very/very/very_long_source_file.cc", 777)
          .WithThreadID(1234)
      << "Info message";
}

TEST(ScopedMockLogTest, ScopedMockLogCanBeNice) {
  absl::ScopedMockLog log;

  InSequence s;
  EXPECT_CALL(log,
              Log(absl::LogSeverity::kWarning, HasSubstr(__FILE__), "Danger."));
  EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, _, "Working...")).Times(2);
  EXPECT_CALL(log, Log(absl::LogSeverity::kError, _, "Bad!!"));

  log.StartCapturingLogs();

  // Any number of these are OK.
  LOG(INFO) << "Info message.";
  // Any number of these are OK.
  LOG(WARNING).AtLocation("SomeOtherFile.cc", 100) << "Danger ";

  LOG(WARNING) << "Danger.";

  // Any number of these are OK.
  LOG(INFO) << "Info message.";
  // Any number of these are OK.
  LOG(WARNING).AtLocation("SomeOtherFile.cc", 100) << "Danger ";

  LOG(INFO) << "Working...";

  // Any number of these are OK.
  LOG(INFO) << "Info message.";
  // Any number of these are OK.
  LOG(WARNING).AtLocation("SomeOtherFile.cc", 100) << "Danger ";

  LOG(INFO) << "Working...";

  // Any number of these are OK.
  LOG(INFO) << "Info message.";
  // Any number of these are OK.
  LOG(WARNING).AtLocation("SomeOtherFile.cc", 100) << "Danger ";

  LOG(ERROR) << "Bad!!";

  // Any number of these are OK.
  LOG(INFO) << "Info message.";
  // Any number of these are OK.
  LOG(WARNING).AtLocation("SomeOtherFile.cc", 100) << "Danger ";
}

// Tests that ScopedMockLog generates a test failure if a message is logged
// that is not expected (here, that means ERROR or FATAL).
TEST(ScopedMockLogTest, RejectsUnexpectedLogs) {
  EXPECT_NONFATAL_FAILURE(
      {
        absl::ScopedMockLog log(absl::MockLogDefault::kDisallowUnexpected);
        // Any INFO and WARNING messages are permitted.
        EXPECT_CALL(log, Log(Lt(absl::LogSeverity::kError), _, _))
            .Times(AnyNumber());
        log.StartCapturingLogs();
        LOG(INFO) << "Ignored";
        LOG(WARNING) << "Ignored";
        LOG(ERROR) << "Should not be ignored";
      },
      "Should not be ignored");
}

TEST(ScopedMockLogTest, CapturesLogsAfterStartCapturingLogs) {
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfinity);
  absl::ScopedMockLog log;

  // The ScopedMockLog object shouldn't see these LOGs, as it hasn't
  // started capturing LOGs yet.
  LOG(INFO) << "Ignored info";
  LOG(WARNING) << "Ignored warning";
  LOG(ERROR) << "Ignored error";

  EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, _, "Expected info"));
  log.StartCapturingLogs();

  // Only this LOG will be seen by the ScopedMockLog.
  LOG(INFO) << "Expected info";
}

TEST(ScopedMockLogTest, DoesNotCaptureLogsAfterStopCapturingLogs) {
  absl::ScopedMockLog log;
  EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, _, "Expected info"));

  log.StartCapturingLogs();

  // This LOG should be seen by the ScopedMockLog.
  LOG(INFO) << "Expected info";

  log.StopCapturingLogs();

  // The ScopedMockLog object shouldn't see these LOGs, as it has
  // stopped capturing LOGs.
  LOG(INFO) << "Ignored info";
  LOG(WARNING) << "Ignored warning";
  LOG(ERROR) << "Ignored error";
}

// Tests that all messages are intercepted regardless of issuing thread. The
// purpose of this test is NOT to exercise thread-safety.
TEST(ScopedMockLogTest, LogFromMultipleThreads) {
  absl::ScopedMockLog log;

  // We don't establish an order to expectations here, since the threads may
  // execute their log statements in different order.
  EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, __FILE__, "Thread 1"));
  EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, __FILE__, "Thread 2"));

  log.StartCapturingLogs();

  absl::Barrier barrier(2);
  std::thread thread1([&barrier]() {
    barrier.Block();
    LOG(INFO) << "Thread 1";
  });
  std::thread thread2([&barrier]() {
    barrier.Block();
    LOG(INFO) << "Thread 2";
  });

  thread1.join();
  thread2.join();
}

// Tests that no sequence will be imposed on two LOG message expectations from
// different threads. This test would actually deadlock if replaced to two LOG
// statements from the same thread.
TEST(ScopedMockLogTest, NoSequenceWithMultipleThreads) {
  absl::ScopedMockLog log;

  absl::Barrier barrier(2);
  EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, _, _))
      .Times(2)
      .WillRepeatedly([&barrier]() { barrier.Block(); });

  log.StartCapturingLogs();

  std::thread thread1([]() { LOG(INFO) << "Thread 1"; });
  std::thread thread2([]() { LOG(INFO) << "Thread 2"; });

  thread1.join();
  thread2.join();
}

TEST(ScopedMockLogTsanTest,
     ScopedMockLogCanBeDeletedWhenAnotherThreadIsLogging) {
  auto log = absl::make_unique<absl::ScopedMockLog>();
  EXPECT_CALL(*log, Log(absl::LogSeverity::kInfo, __FILE__, "Thread log"))
      .Times(AnyNumber());

  log->StartCapturingLogs();

  absl::Notification logging_started;

  std::thread thread([&logging_started]() {
    for (int i = 0; i < 100; ++i) {
      if (i == 50) logging_started.Notify();
      LOG(INFO) << "Thread log";
    }
  });

  logging_started.WaitForNotification();
  log.reset();
  thread.join();
}

TEST(ScopedMockLogTest, AsLocalSink) {
  absl::ScopedMockLog log(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(log, Log(_, _, "two"));
  EXPECT_CALL(log, Log(_, _, "three"));

  LOG(INFO) << "one";
  LOG(INFO).ToSinkOnly(&log.UseAsLocalSink()) << "two";
  LOG(INFO).ToSinkAlso(&log.UseAsLocalSink()) << "three";
}

}  // namespace
