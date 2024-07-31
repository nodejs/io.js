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

// Produce stack trace.
//
// There are three different ways we can try to get the stack trace:
//
// 1) Our hand-coded stack-unwinder.  This depends on a certain stack
//    layout, which is used by gcc (and those systems using a
//    gcc-compatible ABI) on x86 systems, at least since gcc 2.95.
//    It uses the frame pointer to do its work.
//
// 2) The libunwind library.  This is still in development, and as a
//    separate library adds a new dependency, but doesn't need a frame
//    pointer.  It also doesn't call malloc.
//
// 3) The gdb unwinder -- also the one used by the c++ exception code.
//    It's obviously well-tested, but has a fatal flaw: it can call
//    malloc() from the unwinder.  This is a problem because we're
//    trying to use the unwinder to instrument malloc().
//
// Note: if you add a new implementation here, make sure it works
// correctly when absl::GetStackTrace() is called with max_depth == 0.
// Some code may do that.

#include "absl/debugging/stacktrace.h"

#include <atomic>

#include "absl/base/attributes.h"
#include "absl/base/port.h"
#include "absl/debugging/internal/stacktrace_config.h"

#if defined(ABSL_STACKTRACE_INL_HEADER)
#include ABSL_STACKTRACE_INL_HEADER
#else
# error Cannot calculate stack trace: will need to write for your environment

# include "absl/debugging/internal/stacktrace_aarch64-inl.inc"
# include "absl/debugging/internal/stacktrace_arm-inl.inc"
# include "absl/debugging/internal/stacktrace_emscripten-inl.inc"
# include "absl/debugging/internal/stacktrace_generic-inl.inc"
# include "absl/debugging/internal/stacktrace_powerpc-inl.inc"
# include "absl/debugging/internal/stacktrace_riscv-inl.inc"
# include "absl/debugging/internal/stacktrace_unimplemented-inl.inc"
# include "absl/debugging/internal/stacktrace_win32-inl.inc"
# include "absl/debugging/internal/stacktrace_x86-inl.inc"
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace {

typedef int (*Unwinder)(void**, int*, int, int, const void*, int*);
std::atomic<Unwinder> custom;

template <bool IS_STACK_FRAMES, bool IS_WITH_CONTEXT>
ABSL_ATTRIBUTE_ALWAYS_INLINE inline int Unwind(void** result, int* sizes,
                                               int max_depth, int skip_count,
                                               const void* uc,
                                               int* min_dropped_frames) {
  Unwinder f = &UnwindImpl<IS_STACK_FRAMES, IS_WITH_CONTEXT>;
  Unwinder g = custom.load(std::memory_order_acquire);
  if (g != nullptr) f = g;

  // Add 1 to skip count for the unwinder function itself
  int size = (*f)(result, sizes, max_depth, skip_count + 1, uc,
                  min_dropped_frames);
  // To disable tail call to (*f)(...)
  ABSL_BLOCK_TAIL_CALL_OPTIMIZATION();
  return size;
}

}  // anonymous namespace

ABSL_ATTRIBUTE_NOINLINE ABSL_ATTRIBUTE_NO_TAIL_CALL int GetStackFrames(
    void** result, int* sizes, int max_depth, int skip_count) {
  return Unwind<true, false>(result, sizes, max_depth, skip_count, nullptr,
                             nullptr);
}

ABSL_ATTRIBUTE_NOINLINE ABSL_ATTRIBUTE_NO_TAIL_CALL int
GetStackFramesWithContext(void** result, int* sizes, int max_depth,
                          int skip_count, const void* uc,
                          int* min_dropped_frames) {
  return Unwind<true, true>(result, sizes, max_depth, skip_count, uc,
                            min_dropped_frames);
}

ABSL_ATTRIBUTE_NOINLINE ABSL_ATTRIBUTE_NO_TAIL_CALL int GetStackTrace(
    void** result, int max_depth, int skip_count) {
  return Unwind<false, false>(result, nullptr, max_depth, skip_count, nullptr,
                              nullptr);
}

ABSL_ATTRIBUTE_NOINLINE ABSL_ATTRIBUTE_NO_TAIL_CALL int
GetStackTraceWithContext(void** result, int max_depth, int skip_count,
                         const void* uc, int* min_dropped_frames) {
  return Unwind<false, true>(result, nullptr, max_depth, skip_count, uc,
                             min_dropped_frames);
}

void SetStackUnwinder(Unwinder w) {
  custom.store(w, std::memory_order_release);
}

int DefaultStackUnwinder(void** pcs, int* sizes, int depth, int skip,
                         const void* uc, int* min_dropped_frames) {
  skip++;  // For this function
  Unwinder f = nullptr;
  if (sizes == nullptr) {
    if (uc == nullptr) {
      f = &UnwindImpl<false, false>;
    } else {
      f = &UnwindImpl<false, true>;
    }
  } else {
    if (uc == nullptr) {
      f = &UnwindImpl<true, false>;
    } else {
      f = &UnwindImpl<true, true>;
    }
  }
  volatile int x = 0;
  int n = (*f)(pcs, sizes, depth, skip, uc, min_dropped_frames);
  x = 1; (void) x;  // To disable tail call to (*f)(...)
  return n;
}

ABSL_NAMESPACE_END
}  // namespace absl
