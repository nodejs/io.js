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

#include "absl/base/internal/raw_logging.h"

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten/console.h>
#endif

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/atomic_hook.h"
#include "absl/base/internal/errno_saver.h"
#include "absl/base/log_severity.h"

// We know how to perform low-level writes to stderr in POSIX and Windows.  For
// these platforms, we define the token ABSL_LOW_LEVEL_WRITE_SUPPORTED.
// Much of raw_logging.cc becomes a no-op when we can't output messages,
// although a FATAL ABSL_RAW_LOG message will still abort the process.

// ABSL_HAVE_POSIX_WRITE is defined when the platform provides posix write()
// (as from unistd.h)
//
// This preprocessor token is also defined in raw_io.cc.  If you need to copy
// this, consider moving both to config.h instead.
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) || \
    defined(__hexagon__) || defined(__Fuchsia__) ||                     \
    defined(__native_client__) || defined(__OpenBSD__) ||               \
    defined(__EMSCRIPTEN__) || defined(__ASYLO__)

#include <unistd.h>

#define ABSL_HAVE_POSIX_WRITE 1
#define ABSL_LOW_LEVEL_WRITE_SUPPORTED 1
#else
#undef ABSL_HAVE_POSIX_WRITE
#endif

// ABSL_HAVE_SYSCALL_WRITE is defined when the platform provides the syscall
//   syscall(SYS_write, /*int*/ fd, /*char* */ buf, /*size_t*/ len);
// for low level operations that want to avoid libc.
#if (defined(__linux__) || defined(__FreeBSD__)) && !defined(__ANDROID__)
#include <sys/syscall.h>
#define ABSL_HAVE_SYSCALL_WRITE 1
#define ABSL_LOW_LEVEL_WRITE_SUPPORTED 1
#else
#undef ABSL_HAVE_SYSCALL_WRITE
#endif

#ifdef _WIN32
#include <io.h>

#define ABSL_HAVE_RAW_IO 1
#define ABSL_LOW_LEVEL_WRITE_SUPPORTED 1
#else
#undef ABSL_HAVE_RAW_IO
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace raw_log_internal {
namespace {

// TODO(gfalcon): We want raw-logging to work on as many platforms as possible.
// Explicitly `#error` out when not `ABSL_LOW_LEVEL_WRITE_SUPPORTED`, except for
// a selected set of platforms for which we expect not to be able to raw log.

#ifdef ABSL_LOW_LEVEL_WRITE_SUPPORTED
constexpr char kTruncated[] = " ... (message truncated)\n";

// sprintf the format to the buffer, adjusting *buf and *size to reflect the
// consumed bytes, and return whether the message fit without truncation.  If
// truncation occurred, if possible leave room in the buffer for the message
// kTruncated[].
bool VADoRawLog(char** buf, int* size, const char* format, va_list ap)
    ABSL_PRINTF_ATTRIBUTE(3, 0);
bool VADoRawLog(char** buf, int* size, const char* format, va_list ap) {
  if (*size < 0) return false;
  int n = vsnprintf(*buf, static_cast<size_t>(*size), format, ap);
  bool result = true;
  if (n < 0 || n > *size) {
    result = false;
    if (static_cast<size_t>(*size) > sizeof(kTruncated)) {
      n = *size - static_cast<int>(sizeof(kTruncated));
    } else {
      n = 0;  // no room for truncation message
    }
  }
  *size -= n;
  *buf += n;
  return result;
}
#endif  // ABSL_LOW_LEVEL_WRITE_SUPPORTED

constexpr int kLogBufSize = 3000;

// CAVEAT: vsnprintf called from *DoRawLog below has some (exotic) code paths
// that invoke malloc() and getenv() that might acquire some locks.

// Helper for RawLog below.
// *DoRawLog writes to *buf of *size and move them past the written portion.
// It returns true iff there was no overflow or error.
bool DoRawLog(char** buf, int* size, const char* format, ...)
    ABSL_PRINTF_ATTRIBUTE(3, 4);
bool DoRawLog(char** buf, int* size, const char* format, ...) {
  if (*size < 0) return false;
  va_list ap;
  va_start(ap, format);
  int n = vsnprintf(*buf, static_cast<size_t>(*size), format, ap);
  va_end(ap);
  if (n < 0 || n > *size) return false;
  *size -= n;
  *buf += n;
  return true;
}

bool DefaultLogFilterAndPrefix(absl::LogSeverity, const char* file, int line,
                               char** buf, int* buf_size) {
  DoRawLog(buf, buf_size, "[%s : %d] RAW: ", file, line);
  return true;
}

ABSL_INTERNAL_ATOMIC_HOOK_ATTRIBUTES
absl::base_internal::AtomicHook<LogFilterAndPrefixHook>
    log_filter_and_prefix_hook(DefaultLogFilterAndPrefix);
ABSL_INTERNAL_ATOMIC_HOOK_ATTRIBUTES
absl::base_internal::AtomicHook<AbortHook> abort_hook;

void RawLogVA(absl::LogSeverity severity, const char* file, int line,
              const char* format, va_list ap) ABSL_PRINTF_ATTRIBUTE(4, 0);
void RawLogVA(absl::LogSeverity severity, const char* file, int line,
              const char* format, va_list ap) {
  char buffer[kLogBufSize];
  char* buf = buffer;
  int size = sizeof(buffer);
#ifdef ABSL_LOW_LEVEL_WRITE_SUPPORTED
  bool enabled = true;
#else
  bool enabled = false;
#endif

#ifdef ABSL_MIN_LOG_LEVEL
  if (severity < static_cast<absl::LogSeverity>(ABSL_MIN_LOG_LEVEL) &&
      severity < absl::LogSeverity::kFatal) {
    enabled = false;
  }
#endif

  enabled = log_filter_and_prefix_hook(severity, file, line, &buf, &size);
  const char* const prefix_end = buf;

#ifdef ABSL_LOW_LEVEL_WRITE_SUPPORTED
  if (enabled) {
    bool no_chop = VADoRawLog(&buf, &size, format, ap);
    if (no_chop) {
      DoRawLog(&buf, &size, "\n");
    } else {
      DoRawLog(&buf, &size, "%s", kTruncated);
    }
    AsyncSignalSafeWriteError(buffer, strlen(buffer));
  }
#else
  static_cast<void>(format);
  static_cast<void>(ap);
  static_cast<void>(enabled);
#endif

  // Abort the process after logging a FATAL message, even if the output itself
  // was suppressed.
  if (severity == absl::LogSeverity::kFatal) {
    abort_hook(file, line, buffer, prefix_end, buffer + kLogBufSize);
    abort();
  }
}

// Non-formatting version of RawLog().
//
// TODO(gfalcon): When string_view no longer depends on base, change this
// interface to take its message as a string_view instead.
void DefaultInternalLog(absl::LogSeverity severity, const char* file, int line,
                        const std::string& message) {
  RawLog(severity, file, line, "%.*s", static_cast<int>(message.size()),
         message.data());
}

}  // namespace

void AsyncSignalSafeWriteError(const char* s, size_t len) {
  if (!len) return;
  absl::base_internal::ErrnoSaver errno_saver;
#if defined(__EMSCRIPTEN__)
  // In WebAssembly, bypass filesystem emulation via fwrite.
  if (s[len - 1] == '\n') {
    // Skip a trailing newline character as emscripten_errn adds one itself.
    len--;
  }
  // emscripten_errn was introduced in 3.1.41 but broken in standalone mode
  // until 3.1.43.
#if ABSL_INTERNAL_EMSCRIPTEN_VERSION >= 3001043
  emscripten_errn(s, len);
#else
  char buf[kLogBufSize];
  if (len >= kLogBufSize) {
    len = kLogBufSize - 1;
    constexpr size_t trunc_len = sizeof(kTruncated) - 2;
    memcpy(buf + len - trunc_len, kTruncated, trunc_len);
    buf[len] = '\0';
    len -= trunc_len;
  } else {
    buf[len] = '\0';
  }
  memcpy(buf, s, len);
  _emscripten_err(buf);
#endif
#elif defined(ABSL_HAVE_SYSCALL_WRITE)
  // We prefer calling write via `syscall` to minimize the risk of libc doing
  // something "helpful".
  syscall(SYS_write, STDERR_FILENO, s, len);
#elif defined(ABSL_HAVE_POSIX_WRITE)
  write(STDERR_FILENO, s, len);
#elif defined(ABSL_HAVE_RAW_IO)
  _write(/* stderr */ 2, s, static_cast<unsigned>(len));
#else
  // stderr logging unsupported on this platform
  (void)s;
  (void)len;
#endif
}

void RawLog(absl::LogSeverity severity, const char* file, int line,
            const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  RawLogVA(severity, file, line, format, ap);
  va_end(ap);
}

bool RawLoggingFullySupported() {
#ifdef ABSL_LOW_LEVEL_WRITE_SUPPORTED
  return true;
#else   // !ABSL_LOW_LEVEL_WRITE_SUPPORTED
  return false;
#endif  // !ABSL_LOW_LEVEL_WRITE_SUPPORTED
}

ABSL_INTERNAL_ATOMIC_HOOK_ATTRIBUTES ABSL_DLL
    absl::base_internal::AtomicHook<InternalLogFunction>
        internal_log_function(DefaultInternalLog);

void RegisterLogFilterAndPrefixHook(LogFilterAndPrefixHook func) {
  log_filter_and_prefix_hook.Store(func);
}

void RegisterAbortHook(AbortHook func) { abort_hook.Store(func); }

void RegisterInternalLogFunction(InternalLogFunction func) {
  internal_log_function.Store(func);
}

}  // namespace raw_log_internal
ABSL_NAMESPACE_END
}  // namespace absl
