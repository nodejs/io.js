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

#include "absl/log/log_entry.h"

#include <stddef.h>
#include <stdint.h>

#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/log_severity.h"
#include "absl/log/internal/append_truncated.h"
#include "absl/log/internal/log_format.h"
#include "absl/log/internal/test_helpers.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "absl/types/span.h"

namespace {
using ::absl::log_internal::LogEntryTestPeer;
using ::testing::Eq;
using ::testing::IsTrue;
using ::testing::StartsWith;
using ::testing::StrEq;

auto* test_env ABSL_ATTRIBUTE_UNUSED = ::testing::AddGlobalTestEnvironment(
    new absl::log_internal::LogTestEnvironment);
}  // namespace

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {

class LogEntryTestPeer {
 public:
  LogEntryTestPeer(absl::string_view base_filename, int line, bool prefix,
                   absl::LogSeverity severity, absl::string_view timestamp,
                   absl::LogEntry::tid_t tid, PrefixFormat format,
                   absl::string_view text_message)
      : format_{format}, buf_(15000, '\0') {
    entry_.base_filename_ = base_filename;
    entry_.line_ = line;
    entry_.prefix_ = prefix;
    entry_.severity_ = severity;
    std::string time_err;
    EXPECT_THAT(
        absl::ParseTime("%Y-%m-%d%ET%H:%M:%E*S", timestamp,
                        absl::LocalTimeZone(), &entry_.timestamp_, &time_err),
        IsTrue())
        << "Failed to parse time " << timestamp << ": " << time_err;
    entry_.tid_ = tid;
    std::pair<absl::string_view, std::string> timestamp_bits =
        absl::StrSplit(timestamp, absl::ByChar('.'));
    EXPECT_THAT(absl::ParseCivilTime(timestamp_bits.first, &ci_.cs), IsTrue())
        << "Failed to parse time " << timestamp_bits.first;
    timestamp_bits.second.resize(9, '0');
    int64_t nanos = 0;
    EXPECT_THAT(absl::SimpleAtoi(timestamp_bits.second, &nanos), IsTrue())
        << "Failed to parse time " << timestamp_bits.first;
    ci_.subsecond = absl::Nanoseconds(nanos);

    absl::Span<char> view = absl::MakeSpan(buf_);
    view.remove_suffix(2);
    entry_.prefix_len_ =
        entry_.prefix_
            ? log_internal::FormatLogPrefix(
                  entry_.log_severity(), entry_.timestamp(), entry_.tid(),
                  entry_.source_basename(), entry_.source_line(), format_, view)
            : 0;

    EXPECT_THAT(entry_.prefix_len_,
                Eq(static_cast<size_t>(view.data() - buf_.data())));
    log_internal::AppendTruncated(text_message, view);
    view = absl::Span<char>(view.data(), view.size() + 2);
    view[0] = '\n';
    view[1] = '\0';
    view.remove_prefix(2);
    buf_.resize(static_cast<size_t>(view.data() - buf_.data()));
    entry_.text_message_with_prefix_and_newline_and_nul_ = absl::MakeSpan(buf_);
  }
  LogEntryTestPeer(const LogEntryTestPeer&) = delete;
  LogEntryTestPeer& operator=(const LogEntryTestPeer&) = delete;

  std::string FormatLogMessage() const {
    return log_internal::FormatLogMessage(
        entry_.log_severity(), ci_.cs, ci_.subsecond, entry_.tid(),
        entry_.source_basename(), entry_.source_line(), format_,
        entry_.text_message());
  }
  std::string FormatPrefixIntoSizedBuffer(size_t sz) {
    std::string str(sz, '\0');
    absl::Span<char> buf(&str[0], str.size());
    const size_t prefix_size = log_internal::FormatLogPrefix(
        entry_.log_severity(), entry_.timestamp(), entry_.tid(),
        entry_.source_basename(), entry_.source_line(), format_, buf);
    EXPECT_THAT(prefix_size, Eq(static_cast<size_t>(buf.data() - str.data())));
    str.resize(prefix_size);
    return str;
  }
  const absl::LogEntry& entry() const { return entry_; }

 private:
  absl::LogEntry entry_;
  PrefixFormat format_;
  absl::TimeZone::CivilInfo ci_;
  std::vector<char> buf_;
};

}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl

namespace {
constexpr bool kUsePrefix = true, kNoPrefix = false;

TEST(LogEntryTest, Baseline) {
  LogEntryTestPeer entry("foo.cc", 1234, kUsePrefix, absl::LogSeverity::kInfo,
                         "2020-01-02T03:04:05.6789", 451,
                         absl::log_internal::PrefixFormat::kNotRaw,
                         "hello world");
  EXPECT_THAT(entry.FormatLogMessage(),
              Eq("I0102 03:04:05.678900     451 foo.cc:1234] hello world"));
  EXPECT_THAT(entry.FormatPrefixIntoSizedBuffer(1000),
              Eq("I0102 03:04:05.678900     451 foo.cc:1234] "));
  for (size_t sz = strlen("I0102 03:04:05.678900     451 foo.cc:1234] ") + 20;
       sz != std::numeric_limits<size_t>::max(); sz--)
    EXPECT_THAT("I0102 03:04:05.678900     451 foo.cc:1234] ",
                StartsWith(entry.FormatPrefixIntoSizedBuffer(sz)));

  EXPECT_THAT(entry.entry().text_message_with_prefix_and_newline(),
              Eq("I0102 03:04:05.678900     451 foo.cc:1234] hello world\n"));
  EXPECT_THAT(
      entry.entry().text_message_with_prefix_and_newline_c_str(),
      StrEq("I0102 03:04:05.678900     451 foo.cc:1234] hello world\n"));
  EXPECT_THAT(entry.entry().text_message_with_prefix(),
              Eq("I0102 03:04:05.678900     451 foo.cc:1234] hello world"));
  EXPECT_THAT(entry.entry().text_message(), Eq("hello world"));
}

TEST(LogEntryTest, NoPrefix) {
  LogEntryTestPeer entry("foo.cc", 1234, kNoPrefix, absl::LogSeverity::kInfo,
                         "2020-01-02T03:04:05.6789", 451,
                         absl::log_internal::PrefixFormat::kNotRaw,
                         "hello world");
  EXPECT_THAT(entry.FormatLogMessage(),
              Eq("I0102 03:04:05.678900     451 foo.cc:1234] hello world"));
  // These methods are not responsible for honoring `prefix()`.
  EXPECT_THAT(entry.FormatPrefixIntoSizedBuffer(1000),
              Eq("I0102 03:04:05.678900     451 foo.cc:1234] "));
  for (size_t sz = strlen("I0102 03:04:05.678900     451 foo.cc:1234] ") + 20;
       sz != std::numeric_limits<size_t>::max(); sz--)
    EXPECT_THAT("I0102 03:04:05.678900     451 foo.cc:1234] ",
                StartsWith(entry.FormatPrefixIntoSizedBuffer(sz)));

  EXPECT_THAT(entry.entry().text_message_with_prefix_and_newline(),
              Eq("hello world\n"));
  EXPECT_THAT(entry.entry().text_message_with_prefix_and_newline_c_str(),
              StrEq("hello world\n"));
  EXPECT_THAT(entry.entry().text_message_with_prefix(), Eq("hello world"));
  EXPECT_THAT(entry.entry().text_message(), Eq("hello world"));
}

TEST(LogEntryTest, EmptyFields) {
  LogEntryTestPeer entry("", 0, kUsePrefix, absl::LogSeverity::kInfo,
                         "2020-01-02T03:04:05", 0,
                         absl::log_internal::PrefixFormat::kNotRaw, "");
  const std::string format_message = entry.FormatLogMessage();
  EXPECT_THAT(format_message, Eq("I0102 03:04:05.000000       0 :0] "));
  EXPECT_THAT(entry.FormatPrefixIntoSizedBuffer(1000), Eq(format_message));
  for (size_t sz = format_message.size() + 20;
       sz != std::numeric_limits<size_t>::max(); sz--)
    EXPECT_THAT(format_message,
                StartsWith(entry.FormatPrefixIntoSizedBuffer(sz)));

  EXPECT_THAT(entry.entry().text_message_with_prefix_and_newline(),
              Eq("I0102 03:04:05.000000       0 :0] \n"));
  EXPECT_THAT(entry.entry().text_message_with_prefix_and_newline_c_str(),
              StrEq("I0102 03:04:05.000000       0 :0] \n"));
  EXPECT_THAT(entry.entry().text_message_with_prefix(),
              Eq("I0102 03:04:05.000000       0 :0] "));
  EXPECT_THAT(entry.entry().text_message(), Eq(""));
}

TEST(LogEntryTest, NegativeFields) {
  // When Abseil's minimum C++ version is C++17, this conditional can be
  // converted to a constexpr if and the static_cast below removed.
  if (std::is_signed<absl::LogEntry::tid_t>::value) {
    LogEntryTestPeer entry(
        "foo.cc", -1234, kUsePrefix, absl::LogSeverity::kInfo,
        "2020-01-02T03:04:05.6789", static_cast<absl::LogEntry::tid_t>(-451),
        absl::log_internal::PrefixFormat::kNotRaw, "hello world");
    EXPECT_THAT(entry.FormatLogMessage(),
                Eq("I0102 03:04:05.678900    -451 foo.cc:-1234] hello world"));
    EXPECT_THAT(entry.FormatPrefixIntoSizedBuffer(1000),
                Eq("I0102 03:04:05.678900    -451 foo.cc:-1234] "));
    for (size_t sz =
             strlen("I0102 03:04:05.678900    -451 foo.cc:-1234] ") + 20;
         sz != std::numeric_limits<size_t>::max(); sz--)
      EXPECT_THAT("I0102 03:04:05.678900    -451 foo.cc:-1234] ",
                  StartsWith(entry.FormatPrefixIntoSizedBuffer(sz)));

    EXPECT_THAT(
        entry.entry().text_message_with_prefix_and_newline(),
        Eq("I0102 03:04:05.678900    -451 foo.cc:-1234] hello world\n"));
    EXPECT_THAT(
        entry.entry().text_message_with_prefix_and_newline_c_str(),
        StrEq("I0102 03:04:05.678900    -451 foo.cc:-1234] hello world\n"));
    EXPECT_THAT(entry.entry().text_message_with_prefix(),
                Eq("I0102 03:04:05.678900    -451 foo.cc:-1234] hello world"));
    EXPECT_THAT(entry.entry().text_message(), Eq("hello world"));
  } else {
    LogEntryTestPeer entry("foo.cc", -1234, kUsePrefix,
                           absl::LogSeverity::kInfo, "2020-01-02T03:04:05.6789",
                           451, absl::log_internal::PrefixFormat::kNotRaw,
                           "hello world");
    EXPECT_THAT(entry.FormatLogMessage(),
                Eq("I0102 03:04:05.678900     451 foo.cc:-1234] hello world"));
    EXPECT_THAT(entry.FormatPrefixIntoSizedBuffer(1000),
                Eq("I0102 03:04:05.678900     451 foo.cc:-1234] "));
    for (size_t sz =
             strlen("I0102 03:04:05.678900     451 foo.cc:-1234] ") + 20;
         sz != std::numeric_limits<size_t>::max(); sz--)
      EXPECT_THAT("I0102 03:04:05.678900     451 foo.cc:-1234] ",
                  StartsWith(entry.FormatPrefixIntoSizedBuffer(sz)));

    EXPECT_THAT(
        entry.entry().text_message_with_prefix_and_newline(),
        Eq("I0102 03:04:05.678900     451 foo.cc:-1234] hello world\n"));
    EXPECT_THAT(
        entry.entry().text_message_with_prefix_and_newline_c_str(),
        StrEq("I0102 03:04:05.678900     451 foo.cc:-1234] hello world\n"));
    EXPECT_THAT(entry.entry().text_message_with_prefix(),
                Eq("I0102 03:04:05.678900     451 foo.cc:-1234] hello world"));
    EXPECT_THAT(entry.entry().text_message(), Eq("hello world"));
  }
}

TEST(LogEntryTest, LongFields) {
  LogEntryTestPeer entry(
      "I am the very model of a modern Major-General / "
      "I've information vegetable, animal, and mineral.",
      2147483647, kUsePrefix, absl::LogSeverity::kInfo,
      "2020-01-02T03:04:05.678967896789", 2147483647,
      absl::log_internal::PrefixFormat::kNotRaw,
      "I know the kings of England, and I quote the fights historical / "
      "From Marathon to Waterloo, in order categorical.");
  EXPECT_THAT(entry.FormatLogMessage(),
              Eq("I0102 03:04:05.678967 2147483647 I am the very model of a "
                 "modern Major-General / I've information vegetable, animal, "
                 "and mineral.:2147483647] I know the kings of England, and I "
                 "quote the fights historical / From Marathon to Waterloo, in "
                 "order categorical."));
  EXPECT_THAT(entry.FormatPrefixIntoSizedBuffer(1000),
              Eq("I0102 03:04:05.678967 2147483647 I am the very model of a "
                 "modern Major-General / I've information vegetable, animal, "
                 "and mineral.:2147483647] "));
  for (size_t sz =
           strlen("I0102 03:04:05.678967 2147483647 I am the very model of a "
                  "modern Major-General / I've information vegetable, animal, "
                  "and mineral.:2147483647] ") +
           20;
       sz != std::numeric_limits<size_t>::max(); sz--)
    EXPECT_THAT(
        "I0102 03:04:05.678967 2147483647 I am the very model of a "
        "modern Major-General / I've information vegetable, animal, "
        "and mineral.:2147483647] ",
        StartsWith(entry.FormatPrefixIntoSizedBuffer(sz)));

  EXPECT_THAT(entry.entry().text_message_with_prefix_and_newline(),
              Eq("I0102 03:04:05.678967 2147483647 I am the very model of a "
                 "modern Major-General / I've information vegetable, animal, "
                 "and mineral.:2147483647] I know the kings of England, and I "
                 "quote the fights historical / From Marathon to Waterloo, in "
                 "order categorical.\n"));
  EXPECT_THAT(
      entry.entry().text_message_with_prefix_and_newline_c_str(),
      StrEq("I0102 03:04:05.678967 2147483647 I am the very model of a "
            "modern Major-General / I've information vegetable, animal, "
            "and mineral.:2147483647] I know the kings of England, and I "
            "quote the fights historical / From Marathon to Waterloo, in "
            "order categorical.\n"));
  EXPECT_THAT(entry.entry().text_message_with_prefix(),
              Eq("I0102 03:04:05.678967 2147483647 I am the very model of a "
                 "modern Major-General / I've information vegetable, animal, "
                 "and mineral.:2147483647] I know the kings of England, and I "
                 "quote the fights historical / From Marathon to Waterloo, in "
                 "order categorical."));
  EXPECT_THAT(
      entry.entry().text_message(),
      Eq("I know the kings of England, and I quote the fights historical / "
         "From Marathon to Waterloo, in order categorical."));
}

TEST(LogEntryTest, LongNegativeFields) {
  // When Abseil's minimum C++ version is C++17, this conditional can be
  // converted to a constexpr if and the static_cast below removed.
  if (std::is_signed<absl::LogEntry::tid_t>::value) {
    LogEntryTestPeer entry(
        "I am the very model of a modern Major-General / "
        "I've information vegetable, animal, and mineral.",
        -2147483647, kUsePrefix, absl::LogSeverity::kInfo,
        "2020-01-02T03:04:05.678967896789",
        static_cast<absl::LogEntry::tid_t>(-2147483647),
        absl::log_internal::PrefixFormat::kNotRaw,
        "I know the kings of England, and I quote the fights historical / "
        "From Marathon to Waterloo, in order categorical.");
    EXPECT_THAT(
        entry.FormatLogMessage(),
        Eq("I0102 03:04:05.678967 -2147483647 I am the very model of a "
           "modern Major-General / I've information vegetable, animal, "
           "and mineral.:-2147483647] I know the kings of England, and I "
           "quote the fights historical / From Marathon to Waterloo, in "
           "order categorical."));
    EXPECT_THAT(entry.FormatPrefixIntoSizedBuffer(1000),
                Eq("I0102 03:04:05.678967 -2147483647 I am the very model of a "
                   "modern Major-General / I've information vegetable, animal, "
                   "and mineral.:-2147483647] "));
    for (size_t sz =
             strlen(
                 "I0102 03:04:05.678967 -2147483647 I am the very model of a "
                 "modern Major-General / I've information vegetable, animal, "
                 "and mineral.:-2147483647] ") +
             20;
         sz != std::numeric_limits<size_t>::max(); sz--)
      EXPECT_THAT(
          "I0102 03:04:05.678967 -2147483647 I am the very model of a "
          "modern Major-General / I've information vegetable, animal, "
          "and mineral.:-2147483647] ",
          StartsWith(entry.FormatPrefixIntoSizedBuffer(sz)));

    EXPECT_THAT(
        entry.entry().text_message_with_prefix_and_newline(),
        Eq("I0102 03:04:05.678967 -2147483647 I am the very model of a "
           "modern Major-General / I've information vegetable, animal, "
           "and mineral.:-2147483647] I know the kings of England, and I "
           "quote the fights historical / From Marathon to Waterloo, in "
           "order categorical.\n"));
    EXPECT_THAT(
        entry.entry().text_message_with_prefix_and_newline_c_str(),
        StrEq("I0102 03:04:05.678967 -2147483647 I am the very model of a "
              "modern Major-General / I've information vegetable, animal, "
              "and mineral.:-2147483647] I know the kings of England, and I "
              "quote the fights historical / From Marathon to Waterloo, in "
              "order categorical.\n"));
    EXPECT_THAT(
        entry.entry().text_message_with_prefix(),
        Eq("I0102 03:04:05.678967 -2147483647 I am the very model of a "
           "modern Major-General / I've information vegetable, animal, "
           "and mineral.:-2147483647] I know the kings of England, and I "
           "quote the fights historical / From Marathon to Waterloo, in "
           "order categorical."));
    EXPECT_THAT(
        entry.entry().text_message(),
        Eq("I know the kings of England, and I quote the fights historical / "
           "From Marathon to Waterloo, in order categorical."));
  } else {
    LogEntryTestPeer entry(
        "I am the very model of a modern Major-General / "
        "I've information vegetable, animal, and mineral.",
        -2147483647, kUsePrefix, absl::LogSeverity::kInfo,
        "2020-01-02T03:04:05.678967896789", 2147483647,
        absl::log_internal::PrefixFormat::kNotRaw,
        "I know the kings of England, and I quote the fights historical / "
        "From Marathon to Waterloo, in order categorical.");
    EXPECT_THAT(
        entry.FormatLogMessage(),
        Eq("I0102 03:04:05.678967 2147483647 I am the very model of a "
           "modern Major-General / I've information vegetable, animal, "
           "and mineral.:-2147483647] I know the kings of England, and I "
           "quote the fights historical / From Marathon to Waterloo, in "
           "order categorical."));
    EXPECT_THAT(entry.FormatPrefixIntoSizedBuffer(1000),
                Eq("I0102 03:04:05.678967 2147483647 I am the very model of a "
                   "modern Major-General / I've information vegetable, animal, "
                   "and mineral.:-2147483647] "));
    for (size_t sz =
             strlen(
                 "I0102 03:04:05.678967 2147483647 I am the very model of a "
                 "modern Major-General / I've information vegetable, animal, "
                 "and mineral.:-2147483647] ") +
             20;
         sz != std::numeric_limits<size_t>::max(); sz--)
      EXPECT_THAT(
          "I0102 03:04:05.678967 2147483647 I am the very model of a "
          "modern Major-General / I've information vegetable, animal, "
          "and mineral.:-2147483647] ",
          StartsWith(entry.FormatPrefixIntoSizedBuffer(sz)));

    EXPECT_THAT(
        entry.entry().text_message_with_prefix_and_newline(),
        Eq("I0102 03:04:05.678967 2147483647 I am the very model of a "
           "modern Major-General / I've information vegetable, animal, "
           "and mineral.:-2147483647] I know the kings of England, and I "
           "quote the fights historical / From Marathon to Waterloo, in "
           "order categorical.\n"));
    EXPECT_THAT(
        entry.entry().text_message_with_prefix_and_newline_c_str(),
        StrEq("I0102 03:04:05.678967 2147483647 I am the very model of a "
              "modern Major-General / I've information vegetable, animal, "
              "and mineral.:-2147483647] I know the kings of England, and I "
              "quote the fights historical / From Marathon to Waterloo, in "
              "order categorical.\n"));
    EXPECT_THAT(
        entry.entry().text_message_with_prefix(),
        Eq("I0102 03:04:05.678967 2147483647 I am the very model of a "
           "modern Major-General / I've information vegetable, animal, "
           "and mineral.:-2147483647] I know the kings of England, and I "
           "quote the fights historical / From Marathon to Waterloo, in "
           "order categorical."));
    EXPECT_THAT(
        entry.entry().text_message(),
        Eq("I know the kings of England, and I quote the fights historical / "
           "From Marathon to Waterloo, in order categorical."));
  }
}

TEST(LogEntryTest, Raw) {
  LogEntryTestPeer entry("foo.cc", 1234, kUsePrefix, absl::LogSeverity::kInfo,
                         "2020-01-02T03:04:05.6789", 451,
                         absl::log_internal::PrefixFormat::kRaw, "hello world");
  EXPECT_THAT(
      entry.FormatLogMessage(),
      Eq("I0102 03:04:05.678900     451 foo.cc:1234] RAW: hello world"));
  EXPECT_THAT(entry.FormatPrefixIntoSizedBuffer(1000),
              Eq("I0102 03:04:05.678900     451 foo.cc:1234] RAW: "));
  for (size_t sz =
           strlen("I0102 03:04:05.678900     451 foo.cc:1234] RAW: ") + 20;
       sz != std::numeric_limits<size_t>::max(); sz--)
    EXPECT_THAT("I0102 03:04:05.678900     451 foo.cc:1234] RAW: ",
                StartsWith(entry.FormatPrefixIntoSizedBuffer(sz)));

  EXPECT_THAT(
      entry.entry().text_message_with_prefix_and_newline(),
      Eq("I0102 03:04:05.678900     451 foo.cc:1234] RAW: hello world\n"));
  EXPECT_THAT(
      entry.entry().text_message_with_prefix_and_newline_c_str(),
      StrEq("I0102 03:04:05.678900     451 foo.cc:1234] RAW: hello world\n"));
  EXPECT_THAT(
      entry.entry().text_message_with_prefix(),
      Eq("I0102 03:04:05.678900     451 foo.cc:1234] RAW: hello world"));
  EXPECT_THAT(entry.entry().text_message(), Eq("hello world"));
}

}  // namespace
