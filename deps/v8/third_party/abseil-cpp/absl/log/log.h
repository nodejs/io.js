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
// File: log/log.h
// -----------------------------------------------------------------------------
//
// This header declares a family of LOG macros.
//
// Basic invocation looks like this:
//
//   LOG(INFO) << "Found " << num_cookies << " cookies";
//
// Most `LOG` macros take a severity level argument.  The severity levels are
// `INFO`, `WARNING`, `ERROR`, and `FATAL`.  They are defined
// in absl/base/log_severity.h.
// * The `FATAL` severity level terminates the program with a stack trace after
//   logging its message.  Error handlers registered with `RunOnFailure`
//   (process_state.h) are run, but exit handlers registered with `atexit(3)`
//   are not.
// * The `QFATAL` pseudo-severity level is equivalent to `FATAL` but triggers
//   quieter termination messages, e.g. without a full stack trace, and skips
//   running registered error handlers.
// * The `DFATAL` pseudo-severity level is defined as `FATAL` in debug mode and
//   as `ERROR` otherwise.
// Some preprocessor shenanigans are used to ensure that e.g. `LOG(INFO)` has
// the same meaning even if a local symbol or preprocessor macro named `INFO` is
// defined.  To specify a severity level using an expression instead of a
// literal, use `LEVEL(expr)`.
// Example:
//
//   LOG(LEVEL(stale ? absl::LogSeverity::kWarning : absl::LogSeverity::kInfo))
//       << "Cookies are " << days << " days old";

// `LOG` macros evaluate to an unterminated statement.  The value at the end of
// the statement supports some chainable methods:
//
//   * .AtLocation(absl::string_view file, int line)
//     .AtLocation(absl::SourceLocation loc)
//     Overrides the location inferred from the callsite.  The string pointed to
//     by `file` must be valid until the end of the statement.
//   * .NoPrefix()
//     Omits the prefix from this line.  The prefix includes metadata about the
//     logged data such as source code location and timestamp.
//   * .WithVerbosity(int verbose_level)
//     Sets the verbosity field of the logged message as if it was logged by
//     `VLOG(verbose_level)`.  Unlike `VLOG`, this method does not affect
//     evaluation of the statement when the specified `verbose_level` has been
//     disabled.  The only effect is on `LogSink` implementations which make use
//     of the `absl::LogSink::verbosity()` value.  The value
//     `absl::LogEntry::kNoVerbosityLevel` can be specified to mark the message
//     not verbose.
//   * .WithTimestamp(absl::Time timestamp)
//     Uses the specified timestamp instead of one collected at the time of
//     execution.
//   * .WithThreadID(absl::LogEntry::tid_t tid)
//     Uses the specified thread ID instead of one collected at the time of
//     execution.
//   * .WithMetadataFrom(const absl::LogEntry &entry)
//     Copies all metadata (but no data) from the specified `absl::LogEntry`.
//     This can be used to change the severity of a message, but it has some
//     limitations:
//     * `ABSL_MIN_LOG_LEVEL` is evaluated against the severity passed into
//       `LOG` (or the implicit `FATAL` level of `CHECK`).
//     * `LOG(FATAL)` and `CHECK` terminate the process unconditionally, even if
//       the severity is changed later.
//     `.WithMetadataFrom(entry)` should almost always be used in combination
//     with `LOG(LEVEL(entry.log_severity()))`.
//   * .WithPerror()
//     Appends to the logged message a colon, a space, a textual description of
//     the current value of `errno` (as by `strerror(3)`), and the numerical
//     value of `errno`.
//   * .ToSinkAlso(absl::LogSink* sink)
//     Sends this message to `*sink` in addition to whatever other sinks it
//     would otherwise have been sent to.  `sink` must not be null.
//   * .ToSinkOnly(absl::LogSink* sink)
//     Sends this message to `*sink` and no others.  `sink` must not be null.
//
// No interfaces in this header are async-signal-safe; their use in signal
// handlers is unsupported and may deadlock your program or eat your lunch.
//
// Many logging statements are inherently conditional.  For example,
// `LOG_IF(INFO, !foo)` does nothing if `foo` is true.  Even seemingly
// unconditional statements like `LOG(INFO)` might be disabled at
// compile-time to minimize binary size or for security reasons.
//
// * Except for the condition in a `CHECK` or `QCHECK` statement, programs must
//   not rely on evaluation of expressions anywhere in logging statements for
//   correctness.  For example, this is ok:
//
//     CHECK((fp = fopen("config.ini", "r")) != nullptr);
//
//   But this is probably not ok:
//
//     LOG(INFO) << "Server status: " << StartServerAndReturnStatusString();
//
//   The example below is bad too; the `i++` in the `LOG_IF` condition might
//   not be evaluated, resulting in an infinite loop:
//
//     for (int i = 0; i < 1000000;)
//       LOG_IF(INFO, i++ % 1000 == 0) << "Still working...";
//
// * Except where otherwise noted, conditions which cause a statement not to log
//   also cause expressions not to be evaluated.  Programs may rely on this for
//   performance reasons, e.g. by streaming the result of an expensive function
//   call into a `DLOG` or `LOG_EVERY_N` statement.
// * Care has been taken to ensure that expressions are parsed by the compiler
//   even if they are never evaluated.  This means that syntax errors will be
//   caught and variables will be considered used for the purposes of
//   unused-variable diagnostics.  For example, this statement won't compile
//   even if `INFO`-level logging has been compiled out:
//
//     int number_of_cakes = 40;
//     LOG(INFO) << "Number of cakes: " << number_of_cake;  // Note the typo!
//
//   Similarly, this won't produce unused-variable compiler diagnostics even
//   if `INFO`-level logging is compiled out:
//
//     {
//       char fox_line1[] = "Hatee-hatee-hatee-ho!";
//       LOG_IF(ERROR, false) << "The fox says " << fox_line1;
//       char fox_line2[] = "A-oo-oo-oo-ooo!";
//       LOG(INFO) << "The fox also says " << fox_line2;
//     }
//
//   This error-checking is not perfect; for example, symbols that have been
//   declared but not defined may not produce link errors if used in logging
//   statements that compile away.
//
// Expressions streamed into these macros are formatted using `operator<<` just
// as they would be if streamed into a `std::ostream`, however it should be
// noted that their actual type is unspecified.
//
// To implement a custom formatting operator for a type you own, there are two
// options: `AbslStringify()` or `std::ostream& operator<<(std::ostream&, ...)`.
// It is recommended that users make their types loggable through
// `AbslStringify()` as it is a universal stringification extension that also
// enables `absl::StrFormat` and `absl::StrCat` support. If both
// `AbslStringify()` and `std::ostream& operator<<(std::ostream&, ...)` are
// defined, `AbslStringify()` will be used.
//
// To use the `AbslStringify()` API, define a friend function template in your
// type's namespace with the following signature:
//
//   template <typename Sink>
//   void AbslStringify(Sink& sink, const UserDefinedType& value);
//
// `Sink` has the same interface as `absl::FormatSink`, but without
// `PutPaddedString()`.
//
// Example:
//
//   struct Point {
//     template <typename Sink>
//     friend void AbslStringify(Sink& sink, const Point& p) {
//       absl::Format(&sink, "(%v, %v)", p.x, p.y);
//     }
//
//     int x;
//     int y;
//   };
//
// To use `std::ostream& operator<<(std::ostream&, ...)`, define
// `std::ostream& operator<<(std::ostream&, ...)` in your type's namespace (for
// ADL) just as you would to stream it to `std::cout`.
//
// Currently `AbslStringify()` ignores output manipulators but this is not
// guaranteed behavior and may be subject to change in the future. If you would
// like guaranteed behavior regarding output manipulators, please use
// `std::ostream& operator<<(std::ostream&, ...)` to make custom types loggable
// instead.
//
// Those macros that support streaming honor output manipulators and `fmtflag`
// changes that output data (e.g. `std::ends`) or control formatting of data
// (e.g. `std::hex` and `std::fixed`), however flushing such a stream is
// ignored.  The message produced by a log statement is sent to registered
// `absl::LogSink` instances at the end of the statement; those sinks are
// responsible for their own flushing (e.g. to disk) semantics.
//
// Flag settings are not carried over from one `LOG` statement to the next; this
// is a bit different than e.g. `std::cout`:
//
//   LOG(INFO) << std::hex << 0xdeadbeef;  // logs "0xdeadbeef"
//   LOG(INFO) << 0xdeadbeef;              // logs "3735928559"

#ifndef ABSL_LOG_LOG_H_
#define ABSL_LOG_LOG_H_

#include "absl/log/internal/log_impl.h"

// LOG()
//
// `LOG` takes a single argument which is a severity level.  Data streamed in
// comprise the logged message.
// Example:
//
//   LOG(INFO) << "Found " << num_cookies << " cookies";
#define LOG(severity) ABSL_LOG_INTERNAL_LOG_IMPL(_##severity)

// PLOG()
//
// `PLOG` behaves like `LOG` except that a description of the current state of
// `errno` is appended to the streamed message.
#define PLOG(severity) ABSL_LOG_INTERNAL_PLOG_IMPL(_##severity)

// DLOG()
//
// `DLOG` behaves like `LOG` in debug mode (i.e. `#ifndef NDEBUG`).  Otherwise
// it compiles away and does nothing.  Note that `DLOG(FATAL)` does not
// terminate the program if `NDEBUG` is defined.
#define DLOG(severity) ABSL_LOG_INTERNAL_DLOG_IMPL(_##severity)

// `VLOG` uses numeric levels to provide verbose logging that can configured at
// runtime, including at a per-module level.  `VLOG` statements are logged at
// `INFO` severity if they are logged at all; the numeric levels are on a
// different scale than the proper severity levels.  Positive levels are
// disabled by default.  Negative levels should not be used.
// Example:
//
//   VLOG(1) << "I print when you run the program with --v=1 or higher";
//   VLOG(2) << "I print when you run the program with --v=2 or higher";
//
// See vlog_is_on.h for further documentation, including the usage of the
// --vmodule flag to log at different levels in different source files.
//
// `VLOG` does not produce any output when verbose logging is not enabled.
// However, simply testing whether verbose logging is enabled can be expensive.
// If you don't intend to enable verbose logging in non-debug builds, consider
// using `DVLOG` instead.
#define VLOG(severity) ABSL_LOG_INTERNAL_VLOG_IMPL(severity)

// `DVLOG` behaves like `VLOG` in debug mode (i.e. `#ifndef NDEBUG`).
// Otherwise, it compiles away and does nothing.
#define DVLOG(severity) ABSL_LOG_INTERNAL_DVLOG_IMPL(severity)

// `LOG_IF` and friends add a second argument which specifies a condition.  If
// the condition is false, nothing is logged.
// Example:
//
//   LOG_IF(INFO, num_cookies > 10) << "Got lots of cookies";
//
// There is no `VLOG_IF` because the order of evaluation of the arguments is
// ambiguous and the alternate spelling with an `if`-statement is trivial.
#define LOG_IF(severity, condition) \
  ABSL_LOG_INTERNAL_LOG_IF_IMPL(_##severity, condition)
#define PLOG_IF(severity, condition) \
  ABSL_LOG_INTERNAL_PLOG_IF_IMPL(_##severity, condition)
#define DLOG_IF(severity, condition) \
  ABSL_LOG_INTERNAL_DLOG_IF_IMPL(_##severity, condition)

// LOG_EVERY_N
//
// An instance of `LOG_EVERY_N` increments a hidden zero-initialized counter
// every time execution passes through it and logs the specified message when
// the counter's value is a multiple of `n`, doing nothing otherwise.  Each
// instance has its own counter.  The counter's value can be logged by streaming
// the symbol `COUNTER`.  `LOG_EVERY_N` is thread-safe.
// Example:
//
//   LOG_EVERY_N(WARNING, 1000) << "Got a packet with a bad CRC (" << COUNTER
//                              << " total)";
#define LOG_EVERY_N(severity, n) \
  ABSL_LOG_INTERNAL_LOG_EVERY_N_IMPL(_##severity, n)

// LOG_FIRST_N
//
// `LOG_FIRST_N` behaves like `LOG_EVERY_N` except that the specified message is
// logged when the counter's value is less than `n`.  `LOG_FIRST_N` is
// thread-safe.
#define LOG_FIRST_N(severity, n) \
  ABSL_LOG_INTERNAL_LOG_FIRST_N_IMPL(_##severity, n)

// LOG_EVERY_POW_2
//
// `LOG_EVERY_POW_2` behaves like `LOG_EVERY_N` except that the specified
// message is logged when the counter's value is a power of 2.
// `LOG_EVERY_POW_2` is thread-safe.
#define LOG_EVERY_POW_2(severity) \
  ABSL_LOG_INTERNAL_LOG_EVERY_POW_2_IMPL(_##severity)

// LOG_EVERY_N_SEC
//
// An instance of `LOG_EVERY_N_SEC` uses a hidden state variable to log the
// specified message at most once every `n_seconds`.  A hidden counter of
// executions (whether a message is logged or not) is also maintained and can be
// logged by streaming the symbol `COUNTER`.  `LOG_EVERY_N_SEC` is thread-safe.
// Example:
//
//   LOG_EVERY_N_SEC(INFO, 2.5) << "Got " << COUNTER << " cookies so far";
#define LOG_EVERY_N_SEC(severity, n_seconds) \
  ABSL_LOG_INTERNAL_LOG_EVERY_N_SEC_IMPL(_##severity, n_seconds)

#define PLOG_EVERY_N(severity, n) \
  ABSL_LOG_INTERNAL_PLOG_EVERY_N_IMPL(_##severity, n)
#define PLOG_FIRST_N(severity, n) \
  ABSL_LOG_INTERNAL_PLOG_FIRST_N_IMPL(_##severity, n)
#define PLOG_EVERY_POW_2(severity) \
  ABSL_LOG_INTERNAL_PLOG_EVERY_POW_2_IMPL(_##severity)
#define PLOG_EVERY_N_SEC(severity, n_seconds) \
  ABSL_LOG_INTERNAL_PLOG_EVERY_N_SEC_IMPL(_##severity, n_seconds)

#define DLOG_EVERY_N(severity, n) \
  ABSL_LOG_INTERNAL_DLOG_EVERY_N_IMPL(_##severity, n)
#define DLOG_FIRST_N(severity, n) \
  ABSL_LOG_INTERNAL_DLOG_FIRST_N_IMPL(_##severity, n)
#define DLOG_EVERY_POW_2(severity) \
  ABSL_LOG_INTERNAL_DLOG_EVERY_POW_2_IMPL(_##severity)
#define DLOG_EVERY_N_SEC(severity, n_seconds) \
  ABSL_LOG_INTERNAL_DLOG_EVERY_N_SEC_IMPL(_##severity, n_seconds)

#define VLOG_EVERY_N(severity, n) \
  ABSL_LOG_INTERNAL_VLOG_EVERY_N_IMPL(severity, n)
#define VLOG_FIRST_N(severity, n) \
  ABSL_LOG_INTERNAL_VLOG_FIRST_N_IMPL(severity, n)
#define VLOG_EVERY_POW_2(severity) \
  ABSL_LOG_INTERNAL_VLOG_EVERY_POW_2_IMPL(severity)
#define VLOG_EVERY_N_SEC(severity, n_seconds) \
  ABSL_LOG_INTERNAL_VLOG_EVERY_N_SEC_IMPL(severity, n_seconds)

// `LOG_IF_EVERY_N` and friends behave as the corresponding `LOG_EVERY_N`
// but neither increment a counter nor log a message if condition is false (as
// `LOG_IF`).
// Example:
//
//   LOG_IF_EVERY_N(INFO, (size > 1024), 10) << "Got the " << COUNTER
//                                           << "th big cookie";
#define LOG_IF_EVERY_N(severity, condition, n) \
  ABSL_LOG_INTERNAL_LOG_IF_EVERY_N_IMPL(_##severity, condition, n)
#define LOG_IF_FIRST_N(severity, condition, n) \
  ABSL_LOG_INTERNAL_LOG_IF_FIRST_N_IMPL(_##severity, condition, n)
#define LOG_IF_EVERY_POW_2(severity, condition) \
  ABSL_LOG_INTERNAL_LOG_IF_EVERY_POW_2_IMPL(_##severity, condition)
#define LOG_IF_EVERY_N_SEC(severity, condition, n_seconds) \
  ABSL_LOG_INTERNAL_LOG_IF_EVERY_N_SEC_IMPL(_##severity, condition, n_seconds)

#define PLOG_IF_EVERY_N(severity, condition, n) \
  ABSL_LOG_INTERNAL_PLOG_IF_EVERY_N_IMPL(_##severity, condition, n)
#define PLOG_IF_FIRST_N(severity, condition, n) \
  ABSL_LOG_INTERNAL_PLOG_IF_FIRST_N_IMPL(_##severity, condition, n)
#define PLOG_IF_EVERY_POW_2(severity, condition) \
  ABSL_LOG_INTERNAL_PLOG_IF_EVERY_POW_2_IMPL(_##severity, condition)
#define PLOG_IF_EVERY_N_SEC(severity, condition, n_seconds) \
  ABSL_LOG_INTERNAL_PLOG_IF_EVERY_N_SEC_IMPL(_##severity, condition, n_seconds)

#define DLOG_IF_EVERY_N(severity, condition, n) \
  ABSL_LOG_INTERNAL_DLOG_IF_EVERY_N_IMPL(_##severity, condition, n)
#define DLOG_IF_FIRST_N(severity, condition, n) \
  ABSL_LOG_INTERNAL_DLOG_IF_FIRST_N_IMPL(_##severity, condition, n)
#define DLOG_IF_EVERY_POW_2(severity, condition) \
  ABSL_LOG_INTERNAL_DLOG_IF_EVERY_POW_2_IMPL(_##severity, condition)
#define DLOG_IF_EVERY_N_SEC(severity, condition, n_seconds) \
  ABSL_LOG_INTERNAL_DLOG_IF_EVERY_N_SEC_IMPL(_##severity, condition, n_seconds)

#endif  // ABSL_LOG_LOG_H_
