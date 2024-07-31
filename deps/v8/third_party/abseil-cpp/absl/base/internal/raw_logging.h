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
//
// Thread-safe logging routines that do not allocate any memory or
// acquire any locks, and can therefore be used by low-level memory
// allocation, synchronization, and signal-handling code.

#ifndef ABSL_BASE_INTERNAL_RAW_LOGGING_H_
#define ABSL_BASE_INTERNAL_RAW_LOGGING_H_

#include <string>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/atomic_hook.h"
#include "absl/base/log_severity.h"
#include "absl/base/macros.h"
#include "absl/base/optimization.h"
#include "absl/base/port.h"

// This is similar to LOG(severity) << format..., but
// * it is to be used ONLY by low-level modules that can't use normal LOG()
// * it is designed to be a low-level logger that does not allocate any
//   memory and does not need any locks, hence:
// * it logs straight and ONLY to STDERR w/o buffering
// * it uses an explicit printf-format and arguments list
// * it will silently chop off really long message strings
// Usage example:
//   ABSL_RAW_LOG(ERROR, "Failed foo with %i: %s", status, error);
// This will print an almost standard log line like this to stderr only:
//   E0821 211317 file.cc:123] RAW: Failed foo with 22: bad_file

#define ABSL_RAW_LOG(severity, ...)                                            \
  do {                                                                         \
    constexpr const char* absl_raw_log_internal_basename =                     \
        ::absl::raw_log_internal::Basename(__FILE__, sizeof(__FILE__) - 1);    \
    ::absl::raw_log_internal::RawLog(ABSL_RAW_LOG_INTERNAL_##severity,         \
                                     absl_raw_log_internal_basename, __LINE__, \
                                     __VA_ARGS__);                             \
    ABSL_RAW_LOG_INTERNAL_MAYBE_UNREACHABLE_##severity;                        \
  } while (0)

// Similar to CHECK(condition) << message, but for low-level modules:
// we use only ABSL_RAW_LOG that does not allocate memory.
// We do not want to provide args list here to encourage this usage:
//   if (!cond)  ABSL_RAW_LOG(FATAL, "foo ...", hard_to_compute_args);
// so that the args are not computed when not needed.
#define ABSL_RAW_CHECK(condition, message)                             \
  do {                                                                 \
    if (ABSL_PREDICT_FALSE(!(condition))) {                            \
      ABSL_RAW_LOG(FATAL, "Check %s failed: %s", #condition, message); \
    }                                                                  \
  } while (0)

// ABSL_INTERNAL_LOG and ABSL_INTERNAL_CHECK work like the RAW variants above,
// except that if the richer log library is linked into the binary, we dispatch
// to that instead.  This is potentially useful for internal logging and
// assertions, where we are using RAW_LOG neither for its async-signal-safety
// nor for its non-allocating nature, but rather because raw logging has very
// few other dependencies.
//
// The API is a subset of the above: each macro only takes two arguments.  Use
// StrCat if you need to build a richer message.
#define ABSL_INTERNAL_LOG(severity, message)                              \
  do {                                                                    \
    constexpr const char* absl_raw_log_internal_filename = __FILE__;      \
    ::absl::raw_log_internal::internal_log_function(                      \
        ABSL_RAW_LOG_INTERNAL_##severity, absl_raw_log_internal_filename, \
        __LINE__, message);                                               \
    ABSL_RAW_LOG_INTERNAL_MAYBE_UNREACHABLE_##severity;                   \
  } while (0)

#define ABSL_INTERNAL_CHECK(condition, message)                    \
  do {                                                             \
    if (ABSL_PREDICT_FALSE(!(condition))) {                        \
      std::string death_message = "Check " #condition " failed: "; \
      death_message += std::string(message);                       \
      ABSL_INTERNAL_LOG(FATAL, death_message);                     \
    }                                                              \
  } while (0)

#ifndef NDEBUG

#define ABSL_RAW_DLOG(severity, ...) ABSL_RAW_LOG(severity, __VA_ARGS__)
#define ABSL_RAW_DCHECK(condition, message) ABSL_RAW_CHECK(condition, message)

#else  // NDEBUG

#define ABSL_RAW_DLOG(severity, ...)                   \
  while (false) ABSL_RAW_LOG(severity, __VA_ARGS__)
#define ABSL_RAW_DCHECK(condition, message) \
  while (false) ABSL_RAW_CHECK(condition, message)

#endif  // NDEBUG

#define ABSL_RAW_LOG_INTERNAL_INFO ::absl::LogSeverity::kInfo
#define ABSL_RAW_LOG_INTERNAL_WARNING ::absl::LogSeverity::kWarning
#define ABSL_RAW_LOG_INTERNAL_ERROR ::absl::LogSeverity::kError
#define ABSL_RAW_LOG_INTERNAL_FATAL ::absl::LogSeverity::kFatal
#define ABSL_RAW_LOG_INTERNAL_DFATAL ::absl::kLogDebugFatal
#define ABSL_RAW_LOG_INTERNAL_LEVEL(severity) \
  ::absl::NormalizeLogSeverity(severity)

#define ABSL_RAW_LOG_INTERNAL_MAYBE_UNREACHABLE_INFO
#define ABSL_RAW_LOG_INTERNAL_MAYBE_UNREACHABLE_WARNING
#define ABSL_RAW_LOG_INTERNAL_MAYBE_UNREACHABLE_ERROR
#define ABSL_RAW_LOG_INTERNAL_MAYBE_UNREACHABLE_FATAL ABSL_UNREACHABLE()
#define ABSL_RAW_LOG_INTERNAL_MAYBE_UNREACHABLE_DFATAL
#define ABSL_RAW_LOG_INTERNAL_MAYBE_UNREACHABLE_LEVEL(severity)

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace raw_log_internal {

// Helper function to implement ABSL_RAW_LOG
// Logs format... at "severity" level, reporting it
// as called from file:line.
// This does not allocate memory or acquire locks.
void RawLog(absl::LogSeverity severity, const char* file, int line,
            const char* format, ...) ABSL_PRINTF_ATTRIBUTE(4, 5);

// Writes the provided buffer directly to stderr, in a signal-safe, low-level
// manner.  Preserves errno.
void AsyncSignalSafeWriteError(const char* s, size_t len);

// compile-time function to get the "base" filename, that is, the part of
// a filename after the last "/" or "\" path separator.  The search starts at
// the end of the string; the second parameter is the length of the string.
constexpr const char* Basename(const char* fname, int offset) {
  return offset == 0 || fname[offset - 1] == '/' || fname[offset - 1] == '\\'
             ? fname + offset
             : Basename(fname, offset - 1);
}

// For testing only.
// Returns true if raw logging is fully supported. When it is not
// fully supported, no messages will be emitted, but a log at FATAL
// severity will cause an abort.
//
// TODO(gfalcon): Come up with a better name for this method.
bool RawLoggingFullySupported();

// Function type for a raw_log customization hook for suppressing messages
// by severity, and for writing custom prefixes on non-suppressed messages.
//
// The installed hook is called for every raw log invocation.  The message will
// be logged to stderr only if the hook returns true.  FATAL errors will cause
// the process to abort, even if writing to stderr is suppressed.  The hook is
// also provided with an output buffer, where it can write a custom log message
// prefix.
//
// The raw_log system does not allocate memory or grab locks.  User-provided
// hooks must avoid these operations, and must not throw exceptions.
//
// 'severity' is the severity level of the message being written.
// 'file' and 'line' are the file and line number where the ABSL_RAW_LOG macro
// was located.
// 'buf' and 'buf_size' are pointers to the buffer and buffer size.  If the
// hook writes a prefix, it must increment *buf and decrement *buf_size
// accordingly.
using LogFilterAndPrefixHook = bool (*)(absl::LogSeverity severity,
                                        const char* file, int line, char** buf,
                                        int* buf_size);

// Function type for a raw_log customization hook called to abort a process
// when a FATAL message is logged.  If the provided AbortHook() returns, the
// logging system will call abort().
//
// 'file' and 'line' are the file and line number where the ABSL_RAW_LOG macro
// was located.
// The NUL-terminated logged message lives in the buffer between 'buf_start'
// and 'buf_end'.  'prefix_end' points to the first non-prefix character of the
// buffer (as written by the LogFilterAndPrefixHook.)
//
// The lifetime of the filename and message buffers will not end while the
// process remains alive.
using AbortHook = void (*)(const char* file, int line, const char* buf_start,
                           const char* prefix_end, const char* buf_end);

// Internal logging function for ABSL_INTERNAL_LOG to dispatch to.
//
// TODO(gfalcon): When string_view no longer depends on base, change this
// interface to take its message as a string_view instead.
using InternalLogFunction = void (*)(absl::LogSeverity severity,
                                     const char* file, int line,
                                     const std::string& message);

ABSL_INTERNAL_ATOMIC_HOOK_ATTRIBUTES ABSL_DLL extern base_internal::AtomicHook<
    InternalLogFunction>
    internal_log_function;

// Registers hooks of the above types.  Only a single hook of each type may be
// registered.  It is an error to call these functions multiple times with
// different input arguments.
//
// These functions are safe to call at any point during initialization; they do
// not block or malloc, and are async-signal safe.
void RegisterLogFilterAndPrefixHook(LogFilterAndPrefixHook func);
void RegisterAbortHook(AbortHook func);
void RegisterInternalLogFunction(InternalLogFunction func);

}  // namespace raw_log_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_BASE_INTERNAL_RAW_LOGGING_H_
