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
// File: log/internal/globals.h
// -----------------------------------------------------------------------------
//
// This header file contains various global objects and static helper routines
// use in logging implementation.

#ifndef ABSL_LOG_INTERNAL_GLOBALS_H_
#define ABSL_LOG_INTERNAL_GLOBALS_H_

#include "absl/base/config.h"
#include "absl/base/log_severity.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {

// IsInitialized returns true if the logging library is initialized.
// This function is async-signal-safe
bool IsInitialized();

// SetLoggingInitialized is called once after logging initialization is done.
void SetInitialized();

// Unconditionally write a `message` to stderr. If `severity` exceeds kInfo
// we also flush the stderr stream.
void WriteToStderr(absl::string_view message, absl::LogSeverity severity);

// Set the TimeZone used for human-friendly times (for example, the log message
// prefix) printed by the logging library. This may only be called once.
void SetTimeZone(absl::TimeZone tz);

// Returns the TimeZone used for human-friendly times (for example, the log
// message prefix) printed by the logging library Returns nullptr prior to
// initialization.
const absl::TimeZone* TimeZone();

// Returns true if stack traces emitted by the logging library should be
// symbolized. This function is async-signal-safe.
bool ShouldSymbolizeLogStackTrace();

// Enables or disables symbolization of stack traces emitted by the
// logging library. This function is async-signal-safe.
void EnableSymbolizeLogStackTrace(bool on_off);

// Returns the maximum number of frames that appear in stack traces
// emitted by the logging library. This function is async-signal-safe.
int MaxFramesInLogStackTrace();

// Sets the maximum number of frames that appear in stack traces emitted by
// the logging library. This function is async-signal-safe.
void SetMaxFramesInLogStackTrace(int max_num_frames);

// Determines whether we exit the program for a LOG(DFATAL) message in
// debug mode.  It does this by skipping the call to Fail/FailQuietly.
// This is intended for testing only.
//
// This can have some effects on LOG(FATAL) as well. Failure messages
// are always allocated (rather than sharing a buffer), the crash
// reason is not recorded, the "gwq" status message is not updated,
// and the stack trace is not recorded.  The LOG(FATAL) *will* still
// exit the program. Since this function is used only in testing,
// these differences are acceptable.
//
// Additionally, LOG(LEVEL(FATAL)) is indistinguishable from LOG(DFATAL) and
// will not terminate the program if SetExitOnDFatal(false) has been called.
bool ExitOnDFatal();

// SetExitOnDFatal() sets the ExitOnDFatal() status
void SetExitOnDFatal(bool on_off);

// Determines if the logging library should suppress logging of stacktraces in
// the `SIGABRT` handler, typically because we just logged a stacktrace as part
// of `LOG(FATAL)` and are about to send ourselves a `SIGABRT` to end the
// program.
bool SuppressSigabortTrace();

// Sets the SuppressSigabortTrace() status and returns the previous state.
bool SetSuppressSigabortTrace(bool on_off);

}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_LOG_INTERNAL_GLOBALS_H_
