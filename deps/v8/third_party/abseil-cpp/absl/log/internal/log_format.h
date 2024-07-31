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
// File: log/internal/log_format.h
// -----------------------------------------------------------------------------
//
// This file declares routines implementing formatting of log message and log
// prefix.

#ifndef ABSL_LOG_INTERNAL_LOG_FORMAT_H_
#define ABSL_LOG_INTERNAL_LOG_FORMAT_H_

#include <stddef.h>

#include <string>

#include "absl/base/config.h"
#include "absl/base/log_severity.h"
#include "absl/log/internal/config.h"
#include "absl/strings/string_view.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "absl/types/span.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {

enum class PrefixFormat {
  kNotRaw,
  kRaw,
};

// Formats log message based on provided data.
std::string FormatLogMessage(absl::LogSeverity severity,
                             absl::CivilSecond civil_second,
                             absl::Duration subsecond, log_internal::Tid tid,
                             absl::string_view basename, int line,
                             PrefixFormat format, absl::string_view message);

// Formats various entry metadata into a text string meant for use as a
// prefix on a log message string.  Writes into `buf`, advances `buf` to point
// at the remainder of the buffer (i.e. past any written bytes), and returns the
// number of bytes written.
//
// In addition to calling `buf->remove_prefix()` (or the equivalent), this
// function may also do `buf->remove_suffix(buf->size())` in cases where no more
// bytes (i.e. no message data) should be written into the buffer.  For example,
// if the prefix ought to be:
//   I0926 09:00:00.000000 1234567 foo.cc:123]
// `buf` is too small, the function might fill the whole buffer:
//   I0926 09:00:00.000000 1234
// (note the apparrently incorrect thread ID), or it might write less:
//   I0926 09:00:00.000000
// In this case, it might also empty `buf` prior to returning to prevent
// message data from being written into the space where a reader would expect to
// see a thread ID.
size_t FormatLogPrefix(absl::LogSeverity severity, absl::Time timestamp,
                       log_internal::Tid tid, absl::string_view basename,
                       int line, PrefixFormat format, absl::Span<char>& buf);

}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_LOG_INTERNAL_LOG_FORMAT_H_
