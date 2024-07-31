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
// Produce stack trace

#ifndef ABSL_DEBUGGING_INTERNAL_STACKTRACE_X86_INL_INC_
#define ABSL_DEBUGGING_INTERNAL_STACKTRACE_X86_INL_INC_

#if defined(__linux__) && (defined(__i386__) || defined(__x86_64__))
#include <ucontext.h>  // for ucontext_t
#endif

#if !defined(_WIN32)
#include <unistd.h>
#endif

#include <cassert>
#include <cstdint>
#include <limits>

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/base/port.h"
#include "absl/debugging/internal/address_is_readable.h"
#include "absl/debugging/internal/vdso_support.h"  // a no-op on non-elf or non-glibc systems
#include "absl/debugging/stacktrace.h"

using absl::debugging_internal::AddressIsReadable;

#if defined(__linux__) && defined(__i386__)
// Count "push %reg" instructions in VDSO __kernel_vsyscall(),
// preceding "syscall" or "sysenter".
// If __kernel_vsyscall uses frame pointer, answer 0.
//
// kMaxBytes tells how many instruction bytes of __kernel_vsyscall
// to analyze before giving up. Up to kMaxBytes+1 bytes of
// instructions could be accessed.
//
// Here are known __kernel_vsyscall instruction sequences:
//
// SYSENTER (linux-2.6.26/arch/x86/vdso/vdso32/sysenter.S).
// Used on Intel.
//  0xffffe400 <__kernel_vsyscall+0>:       push   %ecx
//  0xffffe401 <__kernel_vsyscall+1>:       push   %edx
//  0xffffe402 <__kernel_vsyscall+2>:       push   %ebp
//  0xffffe403 <__kernel_vsyscall+3>:       mov    %esp,%ebp
//  0xffffe405 <__kernel_vsyscall+5>:       sysenter
//
// SYSCALL (see linux-2.6.26/arch/x86/vdso/vdso32/syscall.S).
// Used on AMD.
//  0xffffe400 <__kernel_vsyscall+0>:       push   %ebp
//  0xffffe401 <__kernel_vsyscall+1>:       mov    %ecx,%ebp
//  0xffffe403 <__kernel_vsyscall+3>:       syscall
//

// The sequence below isn't actually expected in Google fleet,
// here only for completeness. Remove this comment from OSS release.

// i386 (see linux-2.6.26/arch/x86/vdso/vdso32/int80.S)
//  0xffffe400 <__kernel_vsyscall+0>:       int $0x80
//  0xffffe401 <__kernel_vsyscall+1>:       ret
//
static const int kMaxBytes = 10;

// We use assert()s instead of DCHECK()s -- this is too low level
// for DCHECK().

static int CountPushInstructions(const unsigned char *const addr) {
  int result = 0;
  for (int i = 0; i < kMaxBytes; ++i) {
    if (addr[i] == 0x89) {
      // "mov reg,reg"
      if (addr[i + 1] == 0xE5) {
        // Found "mov %esp,%ebp".
        return 0;
      }
      ++i;  // Skip register encoding byte.
    } else if (addr[i] == 0x0F &&
               (addr[i + 1] == 0x34 || addr[i + 1] == 0x05)) {
      // Found "sysenter" or "syscall".
      return result;
    } else if ((addr[i] & 0xF0) == 0x50) {
      // Found "push %reg".
      ++result;
    } else if (addr[i] == 0xCD && addr[i + 1] == 0x80) {
      // Found "int $0x80"
      assert(result == 0);
      return 0;
    } else {
      // Unexpected instruction.
      assert(false && "unexpected instruction in __kernel_vsyscall");
      return 0;
    }
  }
  // Unexpected: didn't find SYSENTER or SYSCALL in
  // [__kernel_vsyscall, __kernel_vsyscall + kMaxBytes) interval.
  assert(false && "did not find SYSENTER or SYSCALL in __kernel_vsyscall");
  return 0;
}
#endif

// Assume stack frames larger than 100,000 bytes are bogus.
static const int kMaxFrameBytes = 100000;
// Stack end to use when we don't know the actual stack end
// (effectively just the end of address space).
constexpr uintptr_t kUnknownStackEnd =
    std::numeric_limits<size_t>::max() - sizeof(void *);

// Returns the stack frame pointer from signal context, 0 if unknown.
// vuc is a ucontext_t *.  We use void* to avoid the use
// of ucontext_t on non-POSIX systems.
static uintptr_t GetFP(const void *vuc) {
#if !defined(__linux__)
  static_cast<void>(vuc);  // Avoid an unused argument compiler warning.
#else
  if (vuc != nullptr) {
    auto *uc = reinterpret_cast<const ucontext_t *>(vuc);
#if defined(__i386__)
    const auto bp = uc->uc_mcontext.gregs[REG_EBP];
    const auto sp = uc->uc_mcontext.gregs[REG_ESP];
#elif defined(__x86_64__)
    const auto bp = uc->uc_mcontext.gregs[REG_RBP];
    const auto sp = uc->uc_mcontext.gregs[REG_RSP];
#else
    const uintptr_t bp = 0;
    const uintptr_t sp = 0;
#endif
    // Sanity-check that the base pointer is valid. It's possible that some
    // code in the process is compiled with --copt=-fomit-frame-pointer or
    // --copt=-momit-leaf-frame-pointer.
    //
    // TODO(bcmills): -momit-leaf-frame-pointer is currently the default
    // behavior when building with clang.  Talk to the C++ toolchain team about
    // fixing that.
    if (bp >= sp && bp - sp <= kMaxFrameBytes)
      return static_cast<uintptr_t>(bp);

    // If bp isn't a plausible frame pointer, return the stack pointer instead.
    // If we're lucky, it points to the start of a stack frame; otherwise, we'll
    // get one frame of garbage in the stack trace and fail the sanity check on
    // the next iteration.
    return static_cast<uintptr_t>(sp);
  }
#endif
  return 0;
}

// Given a pointer to a stack frame, locate and return the calling
// stackframe, or return null if no stackframe can be found. Perform sanity
// checks (the strictness of which is controlled by the boolean parameter
// "STRICT_UNWINDING") to reduce the chance that a bad pointer is returned.
template <bool STRICT_UNWINDING, bool WITH_CONTEXT>
ABSL_ATTRIBUTE_NO_SANITIZE_ADDRESS  // May read random elements from stack.
ABSL_ATTRIBUTE_NO_SANITIZE_MEMORY   // May read random elements from stack.
static void **NextStackFrame(void **old_fp, const void *uc,
                             size_t stack_low, size_t stack_high) {
  void **new_fp = (void **)*old_fp;

#if defined(__linux__) && defined(__i386__)
  if (WITH_CONTEXT && uc != nullptr) {
    // How many "push %reg" instructions are there at __kernel_vsyscall?
    // This is constant for a given kernel and processor, so compute
    // it only once.
    static int num_push_instructions = -1;  // Sentinel: not computed yet.
    // Initialize with sentinel value: __kernel_rt_sigreturn can not possibly
    // be there.
    static const unsigned char *kernel_rt_sigreturn_address = nullptr;
    static const unsigned char *kernel_vsyscall_address = nullptr;
    if (num_push_instructions == -1) {
#ifdef ABSL_HAVE_VDSO_SUPPORT
      absl::debugging_internal::VDSOSupport vdso;
      if (vdso.IsPresent()) {
        absl::debugging_internal::VDSOSupport::SymbolInfo
            rt_sigreturn_symbol_info;
        absl::debugging_internal::VDSOSupport::SymbolInfo vsyscall_symbol_info;
        if (!vdso.LookupSymbol("__kernel_rt_sigreturn", "LINUX_2.5", STT_FUNC,
                               &rt_sigreturn_symbol_info) ||
            !vdso.LookupSymbol("__kernel_vsyscall", "LINUX_2.5", STT_FUNC,
                               &vsyscall_symbol_info) ||
            rt_sigreturn_symbol_info.address == nullptr ||
            vsyscall_symbol_info.address == nullptr) {
          // Unexpected: 32-bit VDSO is present, yet one of the expected
          // symbols is missing or null.
          assert(false && "VDSO is present, but doesn't have expected symbols");
          num_push_instructions = 0;
        } else {
          kernel_rt_sigreturn_address =
              reinterpret_cast<const unsigned char *>(
                  rt_sigreturn_symbol_info.address);
          kernel_vsyscall_address =
              reinterpret_cast<const unsigned char *>(
                  vsyscall_symbol_info.address);
          num_push_instructions =
              CountPushInstructions(kernel_vsyscall_address);
        }
      } else {
        num_push_instructions = 0;
      }
#else  // ABSL_HAVE_VDSO_SUPPORT
      num_push_instructions = 0;
#endif  // ABSL_HAVE_VDSO_SUPPORT
    }
    if (num_push_instructions != 0 && kernel_rt_sigreturn_address != nullptr &&
        old_fp[1] == kernel_rt_sigreturn_address) {
      const ucontext_t *ucv = static_cast<const ucontext_t *>(uc);
      // This kernel does not use frame pointer in its VDSO code,
      // and so %ebp is not suitable for unwinding.
      void **const reg_ebp =
          reinterpret_cast<void **>(ucv->uc_mcontext.gregs[REG_EBP]);
      const unsigned char *const reg_eip =
          reinterpret_cast<unsigned char *>(ucv->uc_mcontext.gregs[REG_EIP]);
      if (new_fp == reg_ebp && kernel_vsyscall_address <= reg_eip &&
          reg_eip - kernel_vsyscall_address < kMaxBytes) {
        // We "stepped up" to __kernel_vsyscall, but %ebp is not usable.
        // Restore from 'ucv' instead.
        void **const reg_esp =
            reinterpret_cast<void **>(ucv->uc_mcontext.gregs[REG_ESP]);
        // Check that alleged %esp is not null and is reasonably aligned.
        if (reg_esp &&
            ((uintptr_t)reg_esp & (sizeof(reg_esp) - 1)) == 0) {
          // Check that alleged %esp is actually readable. This is to prevent
          // "double fault" in case we hit the first fault due to e.g. stack
          // corruption.
          void *const reg_esp2 = reg_esp[num_push_instructions - 1];
          if (AddressIsReadable(reg_esp2)) {
            // Alleged %esp is readable, use it for further unwinding.
            new_fp = reinterpret_cast<void **>(reg_esp2);
          }
        }
      }
    }
  }
#endif

  const uintptr_t old_fp_u = reinterpret_cast<uintptr_t>(old_fp);
  const uintptr_t new_fp_u = reinterpret_cast<uintptr_t>(new_fp);

  // Check that the transition from frame pointer old_fp to frame
  // pointer new_fp isn't clearly bogus.  Skip the checks if new_fp
  // matches the signal context, so that we don't skip out early when
  // using an alternate signal stack.
  //
  // TODO(bcmills): The GetFP call should be completely unnecessary when
  // ENABLE_COMBINED_UNWINDER is set (because we should be back in the thread's
  // stack by this point), but it is empirically still needed (e.g. when the
  // stack includes a call to abort).  unw_get_reg returns UNW_EBADREG for some
  // frames.  Figure out why GetValidFrameAddr and/or libunwind isn't doing what
  // it's supposed to.
  if (STRICT_UNWINDING &&
      (!WITH_CONTEXT || uc == nullptr || new_fp_u != GetFP(uc))) {
    // With the stack growing downwards, older stack frame must be
    // at a greater address that the current one.
    if (new_fp_u <= old_fp_u) return nullptr;

    // If we get a very large frame size, it may be an indication that we
    // guessed frame pointers incorrectly and now risk a paging fault
    // dereferencing a wrong frame pointer. Or maybe not because large frames
    // are possible as well. The main stack is assumed to be readable,
    // so we assume the large frame is legit if we know the real stack bounds
    // and are within the stack.
    if (new_fp_u - old_fp_u > kMaxFrameBytes) {
      if (stack_high < kUnknownStackEnd &&
          static_cast<size_t>(getpagesize()) < stack_low) {
        // Stack bounds are known.
        if (!(stack_low < new_fp_u && new_fp_u <= stack_high)) {
          // new_fp_u is not within the known stack.
          return nullptr;
        }
      } else {
        // Stack bounds are unknown, prefer truncated stack to possible crash.
        return nullptr;
      }
    }
    if (stack_low < old_fp_u && old_fp_u <= stack_high) {
      // Old BP was in the expected stack region...
      if (!(stack_low < new_fp_u && new_fp_u <= stack_high)) {
        // ... but new BP is outside of expected stack region.
        // It is most likely bogus.
        return nullptr;
      }
    } else {
      // We may be here if we are executing in a co-routine with a
      // separate stack. We can't do safety checks in this case.
    }
  } else {
    if (new_fp == nullptr) return nullptr;  // skip AddressIsReadable() below
    // In the non-strict mode, allow discontiguous stack frames.
    // (alternate-signal-stacks for example).
    if (new_fp == old_fp) return nullptr;
  }

  if (new_fp_u & (sizeof(void *) - 1)) return nullptr;
#ifdef __i386__
  // On 32-bit machines, the stack pointer can be very close to
  // 0xffffffff, so we explicitly check for a pointer into the
  // last two pages in the address space
  if (new_fp_u >= 0xffffe000) return nullptr;
#endif
#if !defined(_WIN32)
  if (!STRICT_UNWINDING) {
    // Lax sanity checks cause a crash in 32-bit tcmalloc/crash_reason_test
    // on AMD-based machines with VDSO-enabled kernels.
    // Make an extra sanity check to insure new_fp is readable.
    // Note: NextStackFrame<false>() is only called while the program
    //       is already on its last leg, so it's ok to be slow here.

    if (!AddressIsReadable(new_fp)) {
      return nullptr;
    }
  }
#endif
  return new_fp;
}

template <bool IS_STACK_FRAMES, bool IS_WITH_CONTEXT>
ABSL_ATTRIBUTE_NO_SANITIZE_ADDRESS  // May read random elements from stack.
ABSL_ATTRIBUTE_NO_SANITIZE_MEMORY   // May read random elements from stack.
ABSL_ATTRIBUTE_NOINLINE
static int UnwindImpl(void **result, int *sizes, int max_depth, int skip_count,
                      const void *ucp, int *min_dropped_frames) {
  int n = 0;
  void **fp = reinterpret_cast<void **>(__builtin_frame_address(0));

  // Assume that the first page is not stack.
  size_t stack_low = static_cast<size_t>(getpagesize());
  size_t stack_high = kUnknownStackEnd;

  while (fp && n < max_depth) {
    if (*(fp + 1) == reinterpret_cast<void *>(0)) {
      // In 64-bit code, we often see a frame that
      // points to itself and has a return address of 0.
      break;
    }
    void **next_fp = NextStackFrame<!IS_STACK_FRAMES, IS_WITH_CONTEXT>(
        fp, ucp, stack_low, stack_high);
    if (skip_count > 0) {
      skip_count--;
    } else {
      result[n] = *(fp + 1);
      if (IS_STACK_FRAMES) {
        if (next_fp > fp) {
          sizes[n] = static_cast<int>(
              reinterpret_cast<uintptr_t>(next_fp) -
              reinterpret_cast<uintptr_t>(fp));
        } else {
          // A frame-size of 0 is used to indicate unknown frame size.
          sizes[n] = 0;
        }
      }
      n++;
    }
    fp = next_fp;
  }
  if (min_dropped_frames != nullptr) {
    // Implementation detail: we clamp the max of frames we are willing to
    // count, so as not to spend too much time in the loop below.
    const int kMaxUnwind = 1000;
    int num_dropped_frames = 0;
    for (int j = 0; fp != nullptr && j < kMaxUnwind; j++) {
      if (skip_count > 0) {
        skip_count--;
      } else {
        num_dropped_frames++;
      }
      fp = NextStackFrame<!IS_STACK_FRAMES, IS_WITH_CONTEXT>(fp, ucp, stack_low,
                                                             stack_high);
    }
    *min_dropped_frames = num_dropped_frames;
  }
  return n;
}

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace debugging_internal {
bool StackTraceWorksForTest() {
  return true;
}
}  // namespace debugging_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_DEBUGGING_INTERNAL_STACKTRACE_X86_INL_INC_
