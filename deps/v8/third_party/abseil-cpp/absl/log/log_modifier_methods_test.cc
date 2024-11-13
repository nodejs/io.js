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

#include <errno.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/internal/test_actions.h"
#include "absl/log/internal/test_helpers.h"
#include "absl/log/internal/test_matchers.h"
#include "absl/log/log.h"
#include "absl/log/log_sink.h"
#include "absl/log/scoped_mock_log.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

namespace {
#if GTEST_HAS_DEATH_TEST
using ::absl::log_internal::DeathTestExpectedLogging;
using ::absl::log_internal::DeathTestUnexpectedLogging;
using ::absl::log_internal::DeathTestValidateExpectations;
using ::absl::log_internal::DiedOfQFatal;
#endif
using ::absl::log_internal::LogSeverity;
using ::absl::log_internal::Prefix;
using ::absl::log_internal::SourceBasename;
using ::absl::log_internal::SourceFilename;
using ::absl::log_internal::SourceLine;
using ::absl::log_internal::Stacktrace;
using ::absl::log_internal::TextMessage;
using ::absl::log_internal::TextMessageWithPrefix;
using ::absl::log_internal::TextMessageWithPrefixAndNewline;
using ::absl::log_internal::TextPrefix;
using ::absl::log_internal::ThreadID;
using ::absl::log_internal::Timestamp;
using ::absl::log_internal::Verbosity;

using ::testing::AllOf;
using ::testing::AnyNumber;
using ::testing::AnyOf;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::Truly;

TEST(TailCallsModifiesTest, AtLocationFileLine) {
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(
      test_sink,
      Send(AllOf(
          // The metadata should change:
          SourceFilename(Eq("/my/very/very/very_long_source_file.cc")),
          SourceBasename(Eq("very_long_source_file.cc")), SourceLine(Eq(777)),
          // The logged line should change too, even though the prefix must
          // grow to fit the new metadata.
          TextMessageWithPrefix(Truly([](absl::string_view msg) {
            return absl::EndsWith(msg,
                                  " very_long_source_file.cc:777] hello world");
          })))));

  test_sink.StartCapturingLogs();
  LOG(INFO).AtLocation("/my/very/very/very_long_source_file.cc", 777)
      << "hello world";
}

TEST(TailCallsModifiesTest, NoPrefix) {
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(test_sink, Send(AllOf(Prefix(IsFalse()), TextPrefix(IsEmpty()),
                                    TextMessageWithPrefix(Eq("hello world")))));

  test_sink.StartCapturingLogs();
  LOG(INFO).NoPrefix() << "hello world";
}

TEST(TailCallsModifiesTest, NoPrefixNoMessageNoShirtNoShoesNoService) {
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(test_sink,
              Send(AllOf(Prefix(IsFalse()), TextPrefix(IsEmpty()),
                         TextMessageWithPrefix(IsEmpty()),
                         TextMessageWithPrefixAndNewline(Eq("\n")))));
  test_sink.StartCapturingLogs();
  LOG(INFO).NoPrefix();
}

TEST(TailCallsModifiesTest, WithVerbosity) {
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(test_sink, Send(Verbosity(Eq(2))));

  test_sink.StartCapturingLogs();
  LOG(INFO).WithVerbosity(2) << "hello world";
}

TEST(TailCallsModifiesTest, WithVerbosityNoVerbosity) {
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(test_sink,
              Send(Verbosity(Eq(absl::LogEntry::kNoVerbosityLevel))));

  test_sink.StartCapturingLogs();
  LOG(INFO).WithVerbosity(2).WithVerbosity(absl::LogEntry::kNoVerbosityLevel)
      << "hello world";
}

TEST(TailCallsModifiesTest, WithTimestamp) {
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(test_sink, Send(Timestamp(Eq(absl::UnixEpoch()))));

  test_sink.StartCapturingLogs();
  LOG(INFO).WithTimestamp(absl::UnixEpoch()) << "hello world";
}

TEST(TailCallsModifiesTest, WithThreadID) {
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(test_sink,
              Send(AllOf(ThreadID(Eq(absl::LogEntry::tid_t{1234})))));

  test_sink.StartCapturingLogs();
  LOG(INFO).WithThreadID(1234) << "hello world";
}

TEST(TailCallsModifiesTest, WithMetadataFrom) {
  class ForwardingLogSink : public absl::LogSink {
   public:
    void Send(const absl::LogEntry &entry) override {
      LOG(LEVEL(entry.log_severity())).WithMetadataFrom(entry)
          << "forwarded: " << entry.text_message();
    }
  } forwarding_sink;

  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(
      test_sink,
      Send(AllOf(SourceFilename(Eq("fake/file")), SourceBasename(Eq("file")),
                 SourceLine(Eq(123)), Prefix(IsFalse()),
                 LogSeverity(Eq(absl::LogSeverity::kWarning)),
                 Timestamp(Eq(absl::UnixEpoch())),
                 ThreadID(Eq(absl::LogEntry::tid_t{456})),
                 TextMessage(Eq("forwarded: hello world")), Verbosity(Eq(7)),
                 ENCODED_MESSAGE(MatchesEvent(
                     Eq("fake/file"), Eq(123), Eq(absl::UnixEpoch()),
                     Eq(logging::proto::WARNING), Eq(456),
                     ElementsAre(EqualsProto(R"pb(literal: "forwarded: ")pb"),
                                 EqualsProto(R"pb(str: "hello world")pb")))))));

  test_sink.StartCapturingLogs();
  LOG(WARNING)
          .AtLocation("fake/file", 123)
          .NoPrefix()
          .WithTimestamp(absl::UnixEpoch())
          .WithThreadID(456)
          .WithVerbosity(7)
          .ToSinkOnly(&forwarding_sink)
      << "hello world";
}

TEST(TailCallsModifiesTest, WithPerror) {
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(
      test_sink,
      Send(AllOf(TextMessage(AnyOf(Eq("hello world: Bad file number [9]"),
                                   Eq("hello world: Bad file descriptor [9]"),
                                   Eq("hello world: Bad file descriptor [8]"))),
                 ENCODED_MESSAGE(HasValues(ElementsAre(
                     EqualsProto(R"pb(literal: "hello world")pb"),
                     EqualsProto(R"pb(literal: ": ")pb"),
                     AnyOf(EqualsProto(R"pb(str: "Bad file number")pb"),
                           EqualsProto(R"pb(str: "Bad file descriptor")pb")),
                     EqualsProto(R"pb(literal: " [")pb"),
                     AnyOf(EqualsProto(R"pb(str: "8")pb"),
                           EqualsProto(R"pb(str: "9")pb")),
                     EqualsProto(R"pb(literal: "]")pb")))))));

  test_sink.StartCapturingLogs();
  errno = EBADF;
  LOG(INFO).WithPerror() << "hello world";
}

#if GTEST_HAS_DEATH_TEST
TEST(ModifierMethodDeathTest, ToSinkOnlyQFatal) {
  EXPECT_EXIT(
      {
        absl::ScopedMockLog test_sink(
            absl::MockLogDefault::kDisallowUnexpected);

        auto do_log = [&test_sink] {
          LOG(QFATAL).ToSinkOnly(&test_sink.UseAsLocalSink()) << "hello world";
        };

        EXPECT_CALL(test_sink, Send)
            .Times(AnyNumber())
            .WillRepeatedly(DeathTestUnexpectedLogging());

        EXPECT_CALL(test_sink, Send(AllOf(TextMessage(Eq("hello world")),
                                          Stacktrace(IsEmpty()))))
            .WillOnce(DeathTestExpectedLogging());

        test_sink.StartCapturingLogs();
        do_log();
      },
      DiedOfQFatal, DeathTestValidateExpectations());
}
#endif

}  // namespace
