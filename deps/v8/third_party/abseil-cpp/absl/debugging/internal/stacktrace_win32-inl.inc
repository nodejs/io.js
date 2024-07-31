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
// Produces a stack trace for Windows.  Normally, one could use
// stacktrace_x86-inl.h or stacktrace_x86_64-inl.h -- and indeed, that
// should work for binaries compiled using MSVC in "debug" mode.
// However, in "release" mode, Windows uses frame-pointer
// optimization, which makes getting a stack trace very difficult.
//
// There are several approaches one can take.  One is to use Windows
// intrinsics like StackWalk64.  These can work, but have restrictions
// on how successful they can be.  Another attempt is to write a
// version of stacktrace_x86-inl.h that has heuristic support for
// dealing with FPO, similar to what WinDbg does (see
// http://www.nynaeve.net/?p=97).  There are (non-working) examples of
// these approaches, complete with TODOs, in stacktrace_win32-inl.h#1
//
// The solution we've ended up doing is to call the undocumented
// windows function RtlCaptureStackBackTrace, which probably doesn't
// work with FPO but at least is fast, and doesn't require a symbol
// server.
//
// This code is inspired by a patch from David Vitek:
//   https://code.google.com/p/google-perftools/issues/detail?id=83

#ifndef ABSL_DEBUGGING_INTERNAL_STACKTRACE_WIN32_INL_H_
#define ABSL_DEBUGGING_INTERNAL_STACKTRACE_WIN32_INL_H_

#include <windows.h>    // for GetProcAddress and GetModuleHandle
#include <cassert>

typedef USHORT NTAPI RtlCaptureStackBackTrace_Function(
    IN ULONG frames_to_skip,
    IN ULONG frames_to_capture,
    OUT PVOID *backtrace,
    OUT PULONG backtrace_hash);

// It is not possible to load RtlCaptureStackBackTrace at static init time in
// UWP. CaptureStackBackTrace is the public version of RtlCaptureStackBackTrace
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_APP) && \
    !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
static RtlCaptureStackBackTrace_Function* const RtlCaptureStackBackTrace_fn =
    &::CaptureStackBackTrace;
#else
// Load the function we need at static init time, where we don't have
// to worry about someone else holding the loader's lock.
static RtlCaptureStackBackTrace_Function* const RtlCaptureStackBackTrace_fn =
    (RtlCaptureStackBackTrace_Function*)GetProcAddress(
        GetModuleHandleA("ntdll.dll"), "RtlCaptureStackBackTrace");
#endif  // WINAPI_PARTITION_APP && !WINAPI_PARTITION_DESKTOP

template <bool IS_STACK_FRAMES, bool IS_WITH_CONTEXT>
static int UnwindImpl(void** result, int* sizes, int max_depth, int skip_count,
                      const void*, int* min_dropped_frames) {
  USHORT n = 0;
  if (!RtlCaptureStackBackTrace_fn || skip_count < 0 || max_depth < 0) {
    // can't get a stacktrace with no function/invalid args
  } else {
    n = RtlCaptureStackBackTrace_fn(static_cast<ULONG>(skip_count) + 2,
                                    static_cast<ULONG>(max_depth), result, 0);
  }
  if (IS_STACK_FRAMES) {
    // No implementation for finding out the stack frame sizes yet.
    memset(sizes, 0, sizeof(*sizes) * n);
  }
  if (min_dropped_frames != nullptr) {
    // Not implemented.
    *min_dropped_frames = 0;
  }
  return n;
}

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace debugging_internal {
bool StackTraceWorksForTest() {
  return false;
}
}  // namespace debugging_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_DEBUGGING_INTERNAL_STACKTRACE_WIN32_INL_H_
