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

#include "absl/log/internal/log_format.h"

#include <string.h>

#ifdef _MSC_VER
#include <winsock2.h>  // For timeval
#else
#include <sys/time.h>
#endif

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>

#include "absl/base/config.h"
#include "absl/base/log_severity.h"
#include "absl/base/optimization.h"
#include "absl/log/internal/append_truncated.h"
#include "absl/log/internal/config.h"
#include "absl/log/internal/globals.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "absl/types/span.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {
namespace {

// This templated function avoids compiler warnings about tautological
// comparisons when log_internal::Tid is unsigned. It can be replaced with a
// constexpr if once the minimum C++ version Abseil supports is C++17.
template <typename T>
inline std::enable_if_t<!std::is_signed<T>::value>
PutLeadingWhitespace(T tid, char*& p) {
  if (tid < 10) *p++ = ' ';
  if (tid < 100) *p++ = ' ';
  if (tid < 1000) *p++ = ' ';
  if (tid < 10000) *p++ = ' ';
  if (tid < 100000) *p++ = ' ';
  if (tid < 1000000) *p++ = ' ';
}

template <typename T>
inline std::enable_if_t<std::is_signed<T>::value>
PutLeadingWhitespace(T tid, char*& p) {
  if (tid >= 0 && tid < 10) *p++ = ' ';
  if (tid > -10 && tid < 100) *p++ = ' ';
  if (tid > -100 && tid < 1000) *p++ = ' ';
  if (tid > -1000 && tid < 10000) *p++ = ' ';
  if (tid > -10000 && tid < 100000) *p++ = ' ';
  if (tid > -100000 && tid < 1000000) *p++ = ' ';
}

// The fields before the filename are all fixed-width except for the thread ID,
// which is of bounded width.
size_t FormatBoundedFields(absl::LogSeverity severity, absl::Time timestamp,
                           log_internal::Tid tid, absl::Span<char>& buf) {
  constexpr size_t kBoundedFieldsMaxLen =
      sizeof("SMMDD HH:MM:SS.NNNNNN  ") +
      (1 + std::numeric_limits<log_internal::Tid>::digits10 + 1) - sizeof("");
  if (ABSL_PREDICT_FALSE(buf.size() < kBoundedFieldsMaxLen)) {
    // We don't bother trying to truncate these fields if the buffer is too
    // short (or almost too short) because it would require doing a lot more
    // length checking (slow) and it should never happen.  A 15kB buffer should
    // be enough for anyone.  Instead we mark `buf` full without writing
    // anything.
    buf.remove_suffix(buf.size());
    return 0;
  }

  // We can't call absl::LocalTime(), localtime_r(), or anything else here that
  // isn't async-signal-safe. We can only use the time zone if it has already
  // been loaded.
  const absl::TimeZone* tz = absl::log_internal::TimeZone();
  if (ABSL_PREDICT_FALSE(tz == nullptr)) {
    // If a time zone hasn't been set yet because we are logging before the
    // logging library has been initialized, we fallback to a simpler, slower
    // method. Just report the raw Unix time in seconds. We cram this into the
    // normal time format for the benefit of parsers.
    auto tv = absl::ToTimeval(timestamp);
    int snprintf_result = absl::SNPrintF(
        buf.data(), buf.size(), "%c0000 00:00:%02d.%06d %7d ",
        absl::LogSeverityName(severity)[0], static_cast<int>(tv.tv_sec),
        static_cast<int>(tv.tv_usec), static_cast<int>(tid));
    if (snprintf_result >= 0) {
      buf.remove_prefix(static_cast<size_t>(snprintf_result));
      return static_cast<size_t>(snprintf_result);
    }
    return 0;
  }

  char* p = buf.data();
  *p++ = absl::LogSeverityName(severity)[0];
  const absl::TimeZone::CivilInfo ci = tz->At(timestamp);
  absl::numbers_internal::PutTwoDigits(static_cast<uint32_t>(ci.cs.month()), p);
  p += 2;
  absl::numbers_internal::PutTwoDigits(static_cast<uint32_t>(ci.cs.day()), p);
  p += 2;
  *p++ = ' ';
  absl::numbers_internal::PutTwoDigits(static_cast<uint32_t>(ci.cs.hour()), p);
  p += 2;
  *p++ = ':';
  absl::numbers_internal::PutTwoDigits(static_cast<uint32_t>(ci.cs.minute()),
                                       p);
  p += 2;
  *p++ = ':';
  absl::numbers_internal::PutTwoDigits(static_cast<uint32_t>(ci.cs.second()),
                                       p);
  p += 2;
  *p++ = '.';
  const int64_t usecs = absl::ToInt64Microseconds(ci.subsecond);
  absl::numbers_internal::PutTwoDigits(static_cast<uint32_t>(usecs / 10000), p);
  p += 2;
  absl::numbers_internal::PutTwoDigits(static_cast<uint32_t>(usecs / 100 % 100),
                                       p);
  p += 2;
  absl::numbers_internal::PutTwoDigits(static_cast<uint32_t>(usecs % 100), p);
  p += 2;
  *p++ = ' ';
  PutLeadingWhitespace(tid, p);
  p = absl::numbers_internal::FastIntToBuffer(tid, p);
  *p++ = ' ';
  const size_t bytes_formatted = static_cast<size_t>(p - buf.data());
  buf.remove_prefix(bytes_formatted);
  return bytes_formatted;
}

size_t FormatLineNumber(int line, absl::Span<char>& buf) {
  constexpr size_t kLineFieldMaxLen =
      sizeof(":] ") + (1 + std::numeric_limits<int>::digits10 + 1) - sizeof("");
  if (ABSL_PREDICT_FALSE(buf.size() < kLineFieldMaxLen)) {
    // As above, we don't bother trying to truncate this if the buffer is too
    // short and it should never happen.
    buf.remove_suffix(buf.size());
    return 0;
  }
  char* p = buf.data();
  *p++ = ':';
  p = absl::numbers_internal::FastIntToBuffer(line, p);
  *p++ = ']';
  *p++ = ' ';
  const size_t bytes_formatted = static_cast<size_t>(p - buf.data());
  buf.remove_prefix(bytes_formatted);
  return bytes_formatted;
}

}  // namespace

std::string FormatLogMessage(absl::LogSeverity severity,
                             absl::CivilSecond civil_second,
                             absl::Duration subsecond, log_internal::Tid tid,
                             absl::string_view basename, int line,
                             PrefixFormat format, absl::string_view message) {
  return absl::StrFormat(
      "%c%02d%02d %02d:%02d:%02d.%06d %7d %s:%d] %s%s",
      absl::LogSeverityName(severity)[0], civil_second.month(),
      civil_second.day(), civil_second.hour(), civil_second.minute(),
      civil_second.second(), absl::ToInt64Microseconds(subsecond), tid,
      basename, line, format == PrefixFormat::kRaw ? "RAW: " : "", message);
}

// This method is fairly hot, and the library always passes a huge `buf`, so we
// save some bounds-checking cycles by not trying to do precise truncation.
// Truncating at a field boundary is probably a better UX anyway.
//
// The prefix is written in three parts, each of which does a single
// bounds-check and truncation:
// 1. severity, timestamp, and thread ID
// 2. filename
// 3. line number and bracket
size_t FormatLogPrefix(absl::LogSeverity severity, absl::Time timestamp,
                       log_internal::Tid tid, absl::string_view basename,
                       int line, PrefixFormat format, absl::Span<char>& buf) {
  auto prefix_size = FormatBoundedFields(severity, timestamp, tid, buf);
  prefix_size += log_internal::AppendTruncated(basename, buf);
  prefix_size += FormatLineNumber(line, buf);
  if (format == PrefixFormat::kRaw)
    prefix_size += log_internal::AppendTruncated("RAW: ", buf);
  return prefix_size;
}

}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl
