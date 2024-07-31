// Copyright 2017 The Abseil Authors.
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

#ifndef ABSL_BASE_LOG_SEVERITY_H_
#define ABSL_BASE_LOG_SEVERITY_H_

#include <array>
#include <ostream>

#include "absl/base/attributes.h"
#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// absl::LogSeverity
//
// Four severity levels are defined. Logging APIs should terminate the program
// when a message is logged at severity `kFatal`; the other levels have no
// special semantics.
//
// Values other than the four defined levels (e.g. produced by `static_cast`)
// are valid, but their semantics when passed to a function, macro, or flag
// depend on the function, macro, or flag. The usual behavior is to normalize
// such values to a defined severity level, however in some cases values other
// than the defined levels are useful for comparison.
//
// Example:
//
//   // Effectively disables all logging:
//   SetMinLogLevel(static_cast<absl::LogSeverity>(100));
//
// Abseil flags may be defined with type `LogSeverity`. Dependency layering
// constraints require that the `AbslParseFlag()` overload be declared and
// defined in the flags library itself rather than here. The `AbslUnparseFlag()`
// overload is defined there as well for consistency.
//
// absl::LogSeverity Flag String Representation
//
// An `absl::LogSeverity` has a string representation used for parsing
// command-line flags based on the enumerator name (e.g. `kFatal`) or
// its unprefixed name (without the `k`) in any case-insensitive form. (E.g.
// "FATAL", "fatal" or "Fatal" are all valid.) Unparsing such flags produces an
// unprefixed string representation in all caps (e.g. "FATAL") or an integer.
//
// Additionally, the parser accepts arbitrary integers (as if the type were
// `int`).
//
// Examples:
//
//   --my_log_level=kInfo
//   --my_log_level=INFO
//   --my_log_level=info
//   --my_log_level=0
//
// `DFATAL` and `kLogDebugFatal` are similarly accepted.
//
// Unparsing a flag produces the same result as `absl::LogSeverityName()` for
// the standard levels and a base-ten integer otherwise.
enum class LogSeverity : int {
  kInfo = 0,
  kWarning = 1,
  kError = 2,
  kFatal = 3,
};

// LogSeverities()
//
// Returns an iterable of all standard `absl::LogSeverity` values, ordered from
// least to most severe.
constexpr std::array<absl::LogSeverity, 4> LogSeverities() {
  return {{absl::LogSeverity::kInfo, absl::LogSeverity::kWarning,
           absl::LogSeverity::kError, absl::LogSeverity::kFatal}};
}

// `absl::kLogDebugFatal` equals `absl::LogSeverity::kFatal` in debug builds
// (i.e. when `NDEBUG` is not defined) and `absl::LogSeverity::kError`
// otherwise.  Avoid ODR-using this variable as it has internal linkage and thus
// distinct storage in different TUs.
#ifdef NDEBUG
static constexpr absl::LogSeverity kLogDebugFatal = absl::LogSeverity::kError;
#else
static constexpr absl::LogSeverity kLogDebugFatal = absl::LogSeverity::kFatal;
#endif

// LogSeverityName()
//
// Returns the all-caps string representation (e.g. "INFO") of the specified
// severity level if it is one of the standard levels and "UNKNOWN" otherwise.
constexpr const char* LogSeverityName(absl::LogSeverity s) {
  switch (s) {
    case absl::LogSeverity::kInfo: return "INFO";
    case absl::LogSeverity::kWarning: return "WARNING";
    case absl::LogSeverity::kError: return "ERROR";
    case absl::LogSeverity::kFatal: return "FATAL";
  }
  return "UNKNOWN";
}

// NormalizeLogSeverity()
//
// Values less than `kInfo` normalize to `kInfo`; values greater than `kFatal`
// normalize to `kError` (**NOT** `kFatal`).
constexpr absl::LogSeverity NormalizeLogSeverity(absl::LogSeverity s) {
  absl::LogSeverity n = s;
  if (n < absl::LogSeverity::kInfo) n = absl::LogSeverity::kInfo;
  if (n > absl::LogSeverity::kFatal) n = absl::LogSeverity::kError;
  return n;
}
constexpr absl::LogSeverity NormalizeLogSeverity(int s) {
  return absl::NormalizeLogSeverity(static_cast<absl::LogSeverity>(s));
}

// operator<<
//
// The exact representation of a streamed `absl::LogSeverity` is deliberately
// unspecified; do not rely on it.
std::ostream& operator<<(std::ostream& os, absl::LogSeverity s);

// Enums representing a lower bound for LogSeverity. APIs that only operate on
// messages of at least a certain level (for example, `SetMinLogLevel()`) use
// this type to specify that level. absl::LogSeverityAtLeast::kInfinity is
// a level above all threshold levels and therefore no log message will
// ever meet this threshold.
enum class LogSeverityAtLeast : int {
  kInfo = static_cast<int>(absl::LogSeverity::kInfo),
  kWarning = static_cast<int>(absl::LogSeverity::kWarning),
  kError = static_cast<int>(absl::LogSeverity::kError),
  kFatal = static_cast<int>(absl::LogSeverity::kFatal),
  kInfinity = 1000,
};

std::ostream& operator<<(std::ostream& os, absl::LogSeverityAtLeast s);

// Enums representing an upper bound for LogSeverity. APIs that only operate on
// messages of at most a certain level (for example, buffer all messages at or
// below a certain level) use this type to specify that level.
// absl::LogSeverityAtMost::kNegativeInfinity is a level below all threshold
// levels and therefore will exclude all log messages.
enum class LogSeverityAtMost : int {
  kNegativeInfinity = -1000,
  kInfo = static_cast<int>(absl::LogSeverity::kInfo),
  kWarning = static_cast<int>(absl::LogSeverity::kWarning),
  kError = static_cast<int>(absl::LogSeverity::kError),
  kFatal = static_cast<int>(absl::LogSeverity::kFatal),
};

std::ostream& operator<<(std::ostream& os, absl::LogSeverityAtMost s);

#define COMPOP(op1, op2, T)                                         \
  constexpr bool operator op1(absl::T lhs, absl::LogSeverity rhs) { \
    return static_cast<absl::LogSeverity>(lhs) op1 rhs;             \
  }                                                                 \
  constexpr bool operator op2(absl::LogSeverity lhs, absl::T rhs) { \
    return lhs op2 static_cast<absl::LogSeverity>(rhs);             \
  }

// Comparisons between `LogSeverity` and `LogSeverityAtLeast`/
// `LogSeverityAtMost` are only supported in one direction.
// Valid checks are:
//   LogSeverity >= LogSeverityAtLeast
//   LogSeverity < LogSeverityAtLeast
//   LogSeverity <= LogSeverityAtMost
//   LogSeverity > LogSeverityAtMost
COMPOP(>, <, LogSeverityAtLeast)
COMPOP(<=, >=, LogSeverityAtLeast)
COMPOP(<, >, LogSeverityAtMost)
COMPOP(>=, <=, LogSeverityAtMost)
#undef COMPOP

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_BASE_LOG_SEVERITY_H_
