// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Slightly adapted for inclusion in V8.
// Copyright 2016 the V8 project authors. All rights reserved.

#ifndef V8_BASE_DEBUG_STACK_TRACE_H_
#define V8_BASE_DEBUG_STACK_TRACE_H_

#include <stddef.h>

#include <iosfwd>
#include <string>

#include "src/base/base-export.h"
#include "src/base/build_config.h"

#if V8_OS_POSIX
#include <unistd.h>
#endif

#if V8_OS_WIN
struct _EXCEPTION_POINTERS;
struct _CONTEXT;
#endif

namespace v8 {
namespace base {
namespace debug {

// Enables stack dump to console output on exception and signals.
// When enabled, the process will quit immediately. This is meant to be used in
// tests only!
V8_BASE_EXPORT bool EnableInProcessStackDumping();
V8_BASE_EXPORT void DisableSignalStackDump();

// A stacktrace can be helpful in debugging. For example, you can include a
// stacktrace member in an object (probably around #ifndef NDEBUG) so that you
// can later see where the given object was created from.
class V8_BASE_EXPORT StackTrace {
 public:
  // Creates a stacktrace from the current location.
  StackTrace();

  // Creates a stacktrace from an existing array of instruction
  // pointers (such as returned by Addresses()).  |count| will be
  // trimmed to |kMaxTraces|.
  StackTrace(const void* const* trace, size_t count);

#if V8_OS_WIN
  // Creates a stacktrace for an exception.
  // Note: this function will throw an import not found (StackWalk64) exception
  // on system without dbghelp 5.1.
  explicit StackTrace(_EXCEPTION_POINTERS* exception_pointers);
  explicit StackTrace(const _CONTEXT* context);
#endif

  // Copying and assignment are allowed with the default functions.

  ~StackTrace();

  // Gets an array of instruction pointer values. |*count| will be set to the
  // number of elements in the returned array.
  const void* const* Addresses(size_t* count) const;

  // Prints the stack trace to stderr.
  void Print() const;

  // Resolves backtrace to symbols and write to stream.
  void OutputToStream(std::ostream* os) const;

  // Resolves backtrace to symbols and returns as string.
  std::string ToString() const;

 private:
#if V8_OS_WIN
  void InitTrace(const _CONTEXT* context_record);
#endif

  // From http://msdn.microsoft.com/en-us/library/bb204633.aspx,
  // the sum of FramesToSkip and FramesToCapture must be less than 63,
  // so set it to 62. Even if on POSIX it could be a larger value, it usually
  // doesn't give much more information.
  static const int kMaxTraces = 62;

  void* trace_[kMaxTraces];

  // The number of valid frames in |trace_|.
  size_t count_;
};

}  // namespace debug
}  // namespace base
}  // namespace v8

#endif  // V8_BASE_DEBUG_STACK_TRACE_H_
