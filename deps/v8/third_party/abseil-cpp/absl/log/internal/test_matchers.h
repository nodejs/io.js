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
//
// -----------------------------------------------------------------------------
// File: log/internal/test_matchers.h
// -----------------------------------------------------------------------------
//
// This file declares Googletest's matchers used in the Abseil Logging library
// unit tests.

#ifndef ABSL_LOG_INTERNAL_TEST_MATCHERS_H_
#define ABSL_LOG_INTERNAL_TEST_MATCHERS_H_

#include <iosfwd>
#include <sstream>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/base/log_severity.h"
#include "absl/log/internal/test_helpers.h"
#include "absl/log/log_entry.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {
// In some configurations, Googletest's string matchers (e.g.
// `::testing::EndsWith`) need help to match `absl::string_view`.
::testing::Matcher<absl::string_view> AsString(
    const ::testing::Matcher<const std::string&>& str_matcher);

// These matchers correspond to the components of `absl::LogEntry`.
::testing::Matcher<const absl::LogEntry&> SourceFilename(
    const ::testing::Matcher<absl::string_view>& source_filename);
::testing::Matcher<const absl::LogEntry&> SourceBasename(
    const ::testing::Matcher<absl::string_view>& source_basename);
// Be careful with this one; multi-line statements using `__LINE__` evaluate
// differently on different platforms.  In particular, the MSVC implementation
// of `EXPECT_DEATH` returns the line number of the macro expansion to all lines
// within the code block that's expected to die.
::testing::Matcher<const absl::LogEntry&> SourceLine(
    const ::testing::Matcher<int>& source_line);
::testing::Matcher<const absl::LogEntry&> Prefix(
    const ::testing::Matcher<bool>& prefix);
::testing::Matcher<const absl::LogEntry&> LogSeverity(
    const ::testing::Matcher<absl::LogSeverity>& log_severity);
::testing::Matcher<const absl::LogEntry&> Timestamp(
    const ::testing::Matcher<absl::Time>& timestamp);
// Matches if the `LogEntry`'s timestamp falls after the instantiation of this
// matcher and before its execution, as is normal when used with EXPECT_CALL.
::testing::Matcher<absl::Time> InMatchWindow();
::testing::Matcher<const absl::LogEntry&> ThreadID(
    const ::testing::Matcher<absl::LogEntry::tid_t>&);
::testing::Matcher<const absl::LogEntry&> TextMessageWithPrefixAndNewline(
    const ::testing::Matcher<absl::string_view>&
        text_message_with_prefix_and_newline);
::testing::Matcher<const absl::LogEntry&> TextMessageWithPrefix(
    const ::testing::Matcher<absl::string_view>& text_message_with_prefix);
::testing::Matcher<const absl::LogEntry&> TextMessage(
    const ::testing::Matcher<absl::string_view>& text_message);
::testing::Matcher<const absl::LogEntry&> TextPrefix(
    const ::testing::Matcher<absl::string_view>& text_prefix);
::testing::Matcher<const absl::LogEntry&> Verbosity(
    const ::testing::Matcher<int>& verbosity);
::testing::Matcher<const absl::LogEntry&> Stacktrace(
    const ::testing::Matcher<absl::string_view>& stacktrace);
// Behaves as `Eq(stream.str())`, but produces better failure messages.
::testing::Matcher<absl::string_view> MatchesOstream(
    const std::ostringstream& stream);
::testing::Matcher<const std::string&> DeathTestValidateExpectations();

::testing::Matcher<const absl::LogEntry&> RawEncodedMessage(
    const ::testing::Matcher<absl::string_view>& raw_encoded_message);
#define ENCODED_MESSAGE(message_matcher) ::testing::_

}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_LOG_INTERNAL_TEST_MATCHERS_H_
