// Copyright 2021 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef ABSL_DEBUGGING_INTERNAL_STACKTRACE_RISCV_INL_H_
#define ABSL_DEBUGGING_INTERNAL_STACKTRACE_RISCV_INL_H_

// Generate stack trace for riscv

#include <sys/ucontext.h>

#include "absl/base/config.h"
#if defined(__linux__)
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>
#endif

#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/debugging/stacktrace.h"

static constexpr ptrdiff_t kUnknownFrameSize = 0;

// Compute the size of a stack frame in [low..high).  We assume that low < high.
// Return size of kUnknownFrameSize.
template <typename T>
static inline ptrdiff_t ComputeStackFrameSize(const T *low, const T *high) {
  const char *low_char_ptr = reinterpret_cast<const char *>(low);
  const char *high_char_ptr = reinterpret_cast<const char *>(high);
  return low < high ? static_cast<ptrdiff_t>(high_char_ptr - low_char_ptr)
                    : kUnknownFrameSize;
}

// Given a pointer to a stack frame, locate and return the calling stackframe,
// or return null if no stackframe can be found. Perform sanity checks (the
// strictness of which is controlled by the boolean parameter
// "STRICT_UNWINDING") to reduce the chance that a bad pointer is returned.
template <bool STRICT_UNWINDING, bool WITH_CONTEXT>
ABSL_ATTRIBUTE_NO_SANITIZE_ADDRESS  // May read random elements from stack.
ABSL_ATTRIBUTE_NO_SANITIZE_MEMORY   // May read random elements from stack.
static void ** NextStackFrame(void **old_frame_pointer, const void *uc,
                              const std::pair<size_t, size_t> range) {
  //               .
  //               .
  //               .
  //   +-> +----------------+
  //   |   | return address |
  //   |   |   previous fp  |
  //   |   |      ...       |
  //   |   +----------------+ <-+
  //   |   | return address |   |
  //   +---|-  previous fp  |   |
  //       |      ...       |   |
  // $fp ->|----------------+   |
  //       | return address |   |
  //       |   previous fp -|---+
  // $sp ->|      ...       |
  //       +----------------+
  void **new_frame_pointer = reinterpret_cast<void **>(old_frame_pointer[-2]);
  uintptr_t frame_pointer = reinterpret_cast<uintptr_t>(new_frame_pointer);

  // The RISCV ELF psABI mandates that the stack pointer is always 16-byte
  // aligned.
  // TODO(#1236) this doesn't hold for ILP32E which only mandates a 4-byte
  // alignment.
  if (frame_pointer & 15)
    return nullptr;

  // If the new frame pointer matches the signal context, avoid terminating
  // early to deal with alternate signal stacks.
  if (WITH_CONTEXT)
    if (const ucontext_t *ucv = static_cast<const ucontext_t *>(uc))
      // RISCV ELF psABI has the frame pointer at x8/fp/s0.
      // -- RISCV psABI Table 18.2
      if (ucv->uc_mcontext.__gregs[8] == frame_pointer)
        return new_frame_pointer;

  // Check frame size.  In strict mode, we assume frames to be under 100,000
  // bytes.  In non-strict mode, we relax the limit to 1MB.
  const ptrdiff_t max_size = STRICT_UNWINDING ? 100000 : 1000000;
  const ptrdiff_t frame_size =
      ComputeStackFrameSize(old_frame_pointer, new_frame_pointer);
  if (frame_size == kUnknownFrameSize) {
    if (STRICT_UNWINDING)
      return nullptr;

    // In non-strict mode permit non-contiguous stacks (e.g. alternate signal
    // frame handling).
    if (reinterpret_cast<uintptr_t>(new_frame_pointer) < range.first ||
        reinterpret_cast<uintptr_t>(new_frame_pointer) > range.second)
      return nullptr;
  }

  if (frame_size > max_size)
    return nullptr;

  return new_frame_pointer;
}

template <bool IS_STACK_FRAMES, bool IS_WITH_CONTEXT>
ABSL_ATTRIBUTE_NO_SANITIZE_ADDRESS  // May read random elements from stack.
ABSL_ATTRIBUTE_NO_SANITIZE_MEMORY   // May read random elements from stack.
static int UnwindImpl(void **result, int *sizes, int max_depth, int skip_count,
                      const void *ucp, int *min_dropped_frames) {
  // The `frame_pointer` that is computed here points to the top of the frame.
  // The two words preceding the address are the return address and the previous
  // frame pointer.
#if defined(__GNUC__)
  void **frame_pointer = reinterpret_cast<void **>(__builtin_frame_address(0));
#else
#error reading stack pointer not yet supported on this platform
#endif

  std::pair<size_t, size_t> stack = {
      // assume that the first page is not the stack.
      static_cast<size_t>(sysconf(_SC_PAGESIZE)),
      std::numeric_limits<size_t>::max() - sizeof(void *)
  };

  int n = 0;
  void *return_address = nullptr;
  while (frame_pointer && n < max_depth) {
    return_address = frame_pointer[-1];

    // The absl::GetStackFrames routine is called when we are in some
    // informational context (the failure signal handler for example).  Use the
    // non-strict unwinding rules to produce a stack trace that is as complete
    // as possible (even if it contains a few bogus entries in some rare cases).
    void **next_frame_pointer =
        NextStackFrame<!IS_STACK_FRAMES, IS_WITH_CONTEXT>(frame_pointer, ucp,
                                                          stack);

    if (skip_count > 0) {
      skip_count--;
    } else {
      result[n] = return_address;
      if (IS_STACK_FRAMES) {
        // NextStackFrame() has already checked that frame size fits to int
        sizes[n] = static_cast<int>(ComputeStackFrameSize(frame_pointer,
                                                          next_frame_pointer));
      }
      n++;
    }

    frame_pointer = next_frame_pointer;
  }

  if (min_dropped_frames != nullptr) {
    // Implementation detail: we clamp the max of frames we are willing to
    // count, so as not to spend too much time in the loop below.
    const int kMaxUnwind = 200;
    int num_dropped_frames = 0;
    for (int j = 0; frame_pointer != nullptr && j < kMaxUnwind; j++) {
      if (skip_count > 0) {
        skip_count--;
      } else {
        num_dropped_frames++;
      }
      frame_pointer =
          NextStackFrame<!IS_STACK_FRAMES, IS_WITH_CONTEXT>(frame_pointer, ucp,
                                                            stack);
    }
    *min_dropped_frames = num_dropped_frames;
  }

  return n;
}

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace debugging_internal {
bool StackTraceWorksForTest() { return true; }
}  // namespace debugging_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif
