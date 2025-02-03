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
// File: log/globals.h
// -----------------------------------------------------------------------------
//
// This header declares global logging library configuration knobs.

#ifndef ABSL_LOG_GLOBALS_H_
#define ABSL_LOG_GLOBALS_H_

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/log_severity.h"
#include "absl/log/internal/vlog_config.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

//------------------------------------------------------------------------------
//  Minimum Log Level
//------------------------------------------------------------------------------
//
// Messages logged at or above this severity are directed to all registered log
// sinks or skipped otherwise. This parameter can also be modified using
// command line flag --minloglevel.
// See absl/base/log_severity.h for descriptions of severity levels.

// MinLogLevel()
//
// Returns the value of the Minimum Log Level parameter.
// This function is async-signal-safe.
ABSL_MUST_USE_RESULT absl::LogSeverityAtLeast MinLogLevel();

// SetMinLogLevel()
//
// Updates the value of Minimum Log Level parameter.
// This function is async-signal-safe.
void SetMinLogLevel(absl::LogSeverityAtLeast severity);

namespace log_internal {

// ScopedMinLogLevel
//
// RAII type used to temporarily update the Min Log Level parameter.
class ScopedMinLogLevel final {
 public:
  explicit ScopedMinLogLevel(absl::LogSeverityAtLeast severity);
  ScopedMinLogLevel(const ScopedMinLogLevel&) = delete;
  ScopedMinLogLevel& operator=(const ScopedMinLogLevel&) = delete;
  ~ScopedMinLogLevel();

 private:
  absl::LogSeverityAtLeast saved_severity_;
};

}  // namespace log_internal

//------------------------------------------------------------------------------
// Stderr Threshold
//------------------------------------------------------------------------------
//
// Messages logged at or above this level are directed to stderr in
// addition to other registered log sinks. This parameter can also be modified
// using command line flag --stderrthreshold.
// See absl/base/log_severity.h for descriptions of severity levels.

// StderrThreshold()
//
// Returns the value of the Stderr Threshold parameter.
// This function is async-signal-safe.
ABSL_MUST_USE_RESULT absl::LogSeverityAtLeast StderrThreshold();

// SetStderrThreshold()
//
// Updates the Stderr Threshold parameter.
// This function is async-signal-safe.
void SetStderrThreshold(absl::LogSeverityAtLeast severity);
inline void SetStderrThreshold(absl::LogSeverity severity) {
  absl::SetStderrThreshold(static_cast<absl::LogSeverityAtLeast>(severity));
}

// ScopedStderrThreshold
//
// RAII type used to temporarily update the Stderr Threshold parameter.
class ScopedStderrThreshold final {
 public:
  explicit ScopedStderrThreshold(absl::LogSeverityAtLeast severity);
  ScopedStderrThreshold(const ScopedStderrThreshold&) = delete;
  ScopedStderrThreshold& operator=(const ScopedStderrThreshold&) = delete;
  ~ScopedStderrThreshold();

 private:
  absl::LogSeverityAtLeast saved_severity_;
};

//------------------------------------------------------------------------------
// Log Backtrace At
//------------------------------------------------------------------------------
//
// Users can request an existing `LOG` statement, specified by file and line
// number, to also include a backtrace when logged.

// ShouldLogBacktraceAt()
//
// Returns true if we should log a backtrace at the specified location.
namespace log_internal {
ABSL_MUST_USE_RESULT bool ShouldLogBacktraceAt(absl::string_view file,
                                               int line);
}  // namespace log_internal

// SetLogBacktraceLocation()
//
// Sets the location the backtrace should be logged at.  If the specified
// location isn't a `LOG` statement, the effect will be the same as
// `ClearLogBacktraceLocation` (but less efficient).
void SetLogBacktraceLocation(absl::string_view file, int line);

// ClearLogBacktraceLocation()
//
// Clears the set location so that backtraces will no longer be logged at it.
void ClearLogBacktraceLocation();

//------------------------------------------------------------------------------
// Prepend Log Prefix
//------------------------------------------------------------------------------
//
// This option tells the logging library that every logged message
// should include the prefix (severity, date, time, PID, etc.)
//
// ShouldPrependLogPrefix()
//
// Returns the value of the Prepend Log Prefix option.
// This function is async-signal-safe.
ABSL_MUST_USE_RESULT bool ShouldPrependLogPrefix();

// EnableLogPrefix()
//
// Updates the value of the Prepend Log Prefix option.
// This function is async-signal-safe.
void EnableLogPrefix(bool on_off);

//------------------------------------------------------------------------------
// `VLOG` Configuration
//------------------------------------------------------------------------------
//
// These methods set the `(ABSL_)VLOG(_IS_ON)` threshold.  They allow
// programmatic control of the thresholds set by the --v and --vmodule flags.
//
// Only `VLOG`s with a severity level LESS THAN OR EQUAL TO the threshold will
// be evaluated.
//
// For example, if the threshold is 2, then:
//
//   VLOG(2) << "This message will be logged.";
//   VLOG(3) << "This message will NOT be logged.";
//
// The default threshold is 0. Since `VLOG` levels must not be negative, a
// negative threshold value will turn off all VLOGs.

// SetGlobalVLogLevel()
//
// Sets the global `VLOG` level to threshold. Returns the previous global
// threshold.
inline int SetGlobalVLogLevel(int threshold) {
  return absl::log_internal::UpdateGlobalVLogLevel(threshold);
}

// SetVLogLevel()
//
// Sets the `VLOG` threshold for all files that match `module_pattern`,
// overwriting any prior value. Files that don't match aren't affected.
// Returns the threshold that previously applied to `module_pattern`.
inline int SetVLogLevel(absl::string_view module_pattern, int threshold) {
  return absl::log_internal::PrependVModule(module_pattern, threshold);
}

//------------------------------------------------------------------------------
// Configure Android Native Log Tag
//------------------------------------------------------------------------------
//
// The logging library forwards to the Android system log API when built for
// Android.  That API takes a string "tag" value in addition to a message and
// severity level.  The tag is used to identify the source of messages and to
// filter them.  This library uses the tag "native" by default.

// SetAndroidNativeTag()
//
// Stores a copy of the string pointed to by `tag` and uses it as the Android
// logging tag thereafter. `tag` must not be null.
// This function must not be called more than once!
void SetAndroidNativeTag(const char* tag);

namespace log_internal {
// GetAndroidNativeTag()
//
// Returns the configured Android logging tag.
const char* GetAndroidNativeTag();
}  // namespace log_internal

namespace log_internal {

using LoggingGlobalsListener = void (*)();
void SetLoggingGlobalsListener(LoggingGlobalsListener l);

// Internal implementation for the setter routines. These are used
// to break circular dependencies between flags and globals. Each "Raw"
// routine corresponds to the non-"Raw" counterpart and used to set the
// configuration parameter directly without calling back to the listener.
void RawSetMinLogLevel(absl::LogSeverityAtLeast severity);
void RawSetStderrThreshold(absl::LogSeverityAtLeast severity);
void RawEnableLogPrefix(bool on_off);

}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_LOG_GLOBALS_H_
