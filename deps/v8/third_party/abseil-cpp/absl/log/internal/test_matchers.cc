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

#include "absl/log/internal/test_matchers.h"

#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/log/internal/test_helpers.h"
#include "absl/log/log_entry.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {
namespace {
using ::testing::_;
using ::testing::AllOf;
using ::testing::Ge;
using ::testing::HasSubstr;
using ::testing::MakeMatcher;
using ::testing::Matcher;
using ::testing::MatcherInterface;
using ::testing::MatchResultListener;
using ::testing::Not;
using ::testing::Property;
using ::testing::ResultOf;
using ::testing::Truly;

class AsStringImpl final
    : public MatcherInterface<absl::string_view> {
 public:
  explicit AsStringImpl(
      const Matcher<const std::string&>& str_matcher)
      : str_matcher_(str_matcher) {}
  bool MatchAndExplain(
      absl::string_view actual,
      MatchResultListener* listener) const override {
    return str_matcher_.MatchAndExplain(std::string(actual), listener);
  }
  void DescribeTo(std::ostream* os) const override {
    return str_matcher_.DescribeTo(os);
  }

  void DescribeNegationTo(std::ostream* os) const override {
    return str_matcher_.DescribeNegationTo(os);
  }

 private:
  const Matcher<const std::string&> str_matcher_;
};

class MatchesOstreamImpl final
    : public MatcherInterface<absl::string_view> {
 public:
  explicit MatchesOstreamImpl(std::string expected)
      : expected_(std::move(expected)) {}
  bool MatchAndExplain(absl::string_view actual,
                       MatchResultListener*) const override {
    return actual == expected_;
  }
  void DescribeTo(std::ostream* os) const override {
    *os << "matches the contents of the ostringstream, which are \""
        << expected_ << "\"";
  }

  void DescribeNegationTo(std::ostream* os) const override {
    *os << "does not match the contents of the ostringstream, which are \""
        << expected_ << "\"";
  }

 private:
  const std::string expected_;
};
}  // namespace

Matcher<absl::string_view> AsString(
    const Matcher<const std::string&>& str_matcher) {
  return MakeMatcher(new AsStringImpl(str_matcher));
}

Matcher<const absl::LogEntry&> SourceFilename(
    const Matcher<absl::string_view>& source_filename) {
  return Property("source_filename", &absl::LogEntry::source_filename,
                  source_filename);
}

Matcher<const absl::LogEntry&> SourceBasename(
    const Matcher<absl::string_view>& source_basename) {
  return Property("source_basename", &absl::LogEntry::source_basename,
                  source_basename);
}

Matcher<const absl::LogEntry&> SourceLine(
    const Matcher<int>& source_line) {
  return Property("source_line", &absl::LogEntry::source_line, source_line);
}

Matcher<const absl::LogEntry&> Prefix(
    const Matcher<bool>& prefix) {
  return Property("prefix", &absl::LogEntry::prefix, prefix);
}

Matcher<const absl::LogEntry&> LogSeverity(
    const Matcher<absl::LogSeverity>& log_severity) {
  return Property("log_severity", &absl::LogEntry::log_severity, log_severity);
}

Matcher<const absl::LogEntry&> Timestamp(
    const Matcher<absl::Time>& timestamp) {
  return Property("timestamp", &absl::LogEntry::timestamp, timestamp);
}

Matcher<absl::Time> InMatchWindow() {
  return AllOf(Ge(absl::Now()),
               Truly([](absl::Time arg) { return arg <= absl::Now(); }));
}

Matcher<const absl::LogEntry&> ThreadID(
    const Matcher<absl::LogEntry::tid_t>& tid) {
  return Property("tid", &absl::LogEntry::tid, tid);
}

Matcher<const absl::LogEntry&> TextMessageWithPrefixAndNewline(
    const Matcher<absl::string_view>&
        text_message_with_prefix_and_newline) {
  return Property("text_message_with_prefix_and_newline",
                  &absl::LogEntry::text_message_with_prefix_and_newline,
                  text_message_with_prefix_and_newline);
}

Matcher<const absl::LogEntry&> TextMessageWithPrefix(
    const Matcher<absl::string_view>& text_message_with_prefix) {
  return Property("text_message_with_prefix",
                  &absl::LogEntry::text_message_with_prefix,
                  text_message_with_prefix);
}

Matcher<const absl::LogEntry&> TextMessage(
    const Matcher<absl::string_view>& text_message) {
  return Property("text_message", &absl::LogEntry::text_message, text_message);
}

Matcher<const absl::LogEntry&> TextPrefix(
    const Matcher<absl::string_view>& text_prefix) {
  return ResultOf(
      [](const absl::LogEntry& entry) {
        absl::string_view msg = entry.text_message_with_prefix();
        msg.remove_suffix(entry.text_message().size());
        return msg;
      },
      text_prefix);
}
Matcher<const absl::LogEntry&> RawEncodedMessage(
    const Matcher<absl::string_view>& raw_encoded_message) {
  return Property("encoded_message", &absl::LogEntry::encoded_message,
                  raw_encoded_message);
}

Matcher<const absl::LogEntry&> Verbosity(
    const Matcher<int>& verbosity) {
  return Property("verbosity", &absl::LogEntry::verbosity, verbosity);
}

Matcher<const absl::LogEntry&> Stacktrace(
    const Matcher<absl::string_view>& stacktrace) {
  return Property("stacktrace", &absl::LogEntry::stacktrace, stacktrace);
}

Matcher<absl::string_view> MatchesOstream(
    const std::ostringstream& stream) {
  return MakeMatcher(new MatchesOstreamImpl(stream.str()));
}

// We need to validate what is and isn't logged as the process dies due to
// `FATAL`, `QFATAL`, `CHECK`, etc., but assertions inside a death test
// subprocess don't directly affect the pass/fail status of the parent process.
// Instead, we use the mock actions `DeathTestExpectedLogging` and
// `DeathTestUnexpectedLogging` to write specific phrases to `stderr` that we
// can validate in the parent process using this matcher.
Matcher<const std::string&> DeathTestValidateExpectations() {
  if (log_internal::LoggingEnabledAt(absl::LogSeverity::kFatal)) {
    return Matcher<const std::string&>(
        AllOf(HasSubstr("Mock received expected entry"),
              Not(HasSubstr("Mock received unexpected entry"))));
  }
  // If `FATAL` logging is disabled, neither message should have been written.
  return Matcher<const std::string&>(
      AllOf(Not(HasSubstr("Mock received expected entry")),
            Not(HasSubstr("Mock received unexpected entry"))));
}

}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl
