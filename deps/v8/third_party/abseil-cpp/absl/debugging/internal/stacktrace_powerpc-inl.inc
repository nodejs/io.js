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
// Produce stack trace.  I'm guessing (hoping!) the code is much like
// for x86.  For apple machines, at least, it seems to be; see
//    https://developer.apple.com/documentation/mac/runtimehtml/RTArch-59.html
//    https://www.linux-foundation.org/spec/ELF/ppc64/PPC-elf64abi-1.9.html#STACK
// Linux has similar code: http://patchwork.ozlabs.org/linuxppc/patch?id=8882

#ifndef ABSL_DEBUGGING_INTERNAL_STACKTRACE_POWERPC_INL_H_
#define ABSL_DEBUGGING_INTERNAL_STACKTRACE_POWERPC_INL_H_

#if defined(__linux__)
#include <asm/ptrace.h>   // for PT_NIP.
#include <ucontext.h>     // for ucontext_t
#endif

#include <unistd.h>
#include <cassert>
#include <cstdint>
#include <cstdio>

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "absl/base/port.h"
#include "absl/debugging/stacktrace.h"
#include "absl/debugging/internal/address_is_readable.h"
#include "absl/debugging/internal/vdso_support.h"  // a no-op on non-elf or non-glibc systems

// Given a stack pointer, return the saved link register value.
// Note that this is the link register for a callee.
static inline void *StacktracePowerPCGetLR(void **sp) {
  // PowerPC has 3 main ABIs, which say where in the stack the
  // Link Register is.  For DARWIN and AIX (used by apple and
  // linux ppc64), it's in sp[2].  For SYSV (used by linux ppc),
  // it's in sp[1].
#if defined(_CALL_AIX) || defined(_CALL_DARWIN)
  return *(sp+2);
#elif defined(_CALL_SYSV)
  return *(sp+1);
#elif defined(__APPLE__) || defined(__FreeBSD__) || \
    (defined(__linux__) && defined(__PPC64__))
  // This check is in case the compiler doesn't define _CALL_AIX/etc.
  return *(sp+2);
#elif defined(__linux)
  // This check is in case the compiler doesn't define _CALL_SYSV.
  return *(sp+1);
#else
#error Need to specify the PPC ABI for your architecture.
#endif
}

// Given a pointer to a stack frame, locate and return the calling
// stackframe, or return null if no stackframe can be found. Perform sanity
// checks (the strictness of which is controlled by the boolean parameter
// "STRICT_UNWINDING") to reduce the chance that a bad pointer is returned.
template<bool STRICT_UNWINDING, bool IS_WITH_CONTEXT>
ABSL_ATTRIBUTE_NO_SANITIZE_ADDRESS  // May read random elements from stack.
ABSL_ATTRIBUTE_NO_SANITIZE_MEMORY   // May read random elements from stack.
static void **NextStackFrame(void **old_sp, const void *uc) {
  void **new_sp = (void **) *old_sp;
  enum { kStackAlignment = 16 };

  // Check that the transition from frame pointer old_sp to frame
  // pointer new_sp isn't clearly bogus
  if (STRICT_UNWINDING) {
    // With the stack growing downwards, older stack frame must be
    // at a greater address that the current one.
    if (new_sp <= old_sp) return nullptr;
    // Assume stack frames larger than 100,000 bytes are bogus.
    if ((uintptr_t)new_sp - (uintptr_t)old_sp > 100000) return nullptr;
  } else {
    // In the non-strict mode, allow discontiguous stack frames.
    // (alternate-signal-stacks for example).
    if (new_sp == old_sp) return nullptr;
    // And allow frames upto about 1MB.
    if ((new_sp > old_sp)
        && ((uintptr_t)new_sp - (uintptr_t)old_sp > 1000000)) return nullptr;
  }
  if ((uintptr_t)new_sp % kStackAlignment != 0) return nullptr;

#if defined(__linux__)
  enum StackTraceKernelSymbolStatus {
      kNotInitialized = 0, kAddressValid, kAddressInvalid };

  if (IS_WITH_CONTEXT && uc != nullptr) {
    static StackTraceKernelSymbolStatus kernel_symbol_status =
        kNotInitialized;  // Sentinel: not computed yet.
    // Initialize with sentinel value: __kernel_rt_sigtramp_rt64 can not
    // possibly be there.
    static const unsigned char *kernel_sigtramp_rt64_address = nullptr;
    if (kernel_symbol_status == kNotInitialized) {
      absl::debugging_internal::VDSOSupport vdso;
      if (vdso.IsPresent()) {
        absl::debugging_internal::VDSOSupport::SymbolInfo
            sigtramp_rt64_symbol_info;
        if (!vdso.LookupSymbol(
                "__kernel_sigtramp_rt64", "LINUX_2.6.15",
                absl::debugging_internal::VDSOSupport::kVDSOSymbolType,
                &sigtramp_rt64_symbol_info) ||
            sigtramp_rt64_symbol_info.address == nullptr) {
          // Unexpected: VDSO is present, yet the expected symbol is missing
          // or null.
          assert(false && "VDSO is present, but doesn't have expected symbol");
          kernel_symbol_status = kAddressInvalid;
        } else {
          kernel_sigtramp_rt64_address =
              reinterpret_cast<const unsigned char *>(
                  sigtramp_rt64_symbol_info.address);
          kernel_symbol_status = kAddressValid;
        }
      } else {
        kernel_symbol_status = kAddressInvalid;
      }
    }

    if (new_sp != nullptr &&
        kernel_symbol_status == kAddressValid &&
        StacktracePowerPCGetLR(new_sp) == kernel_sigtramp_rt64_address) {
      const ucontext_t* signal_context =
          reinterpret_cast<const ucontext_t*>(uc);
      void **const sp_before_signal =
#if defined(__PPC64__)
          reinterpret_cast<void **>(signal_context->uc_mcontext.gp_regs[PT_R1]);
#else
          reinterpret_cast<void **>(
              signal_context->uc_mcontext.uc_regs->gregs[PT_R1]);
#endif
      // Check that alleged sp before signal is nonnull and is reasonably
      // aligned.
      if (sp_before_signal != nullptr &&
          ((uintptr_t)sp_before_signal % kStackAlignment) == 0) {
        // Check that alleged stack pointer is actually readable. This is to
        // prevent a "double fault" in case we hit the first fault due to e.g.
        // a stack corruption.
        if (absl::debugging_internal::AddressIsReadable(sp_before_signal)) {
          // Alleged stack pointer is readable, use it for further unwinding.
          new_sp = sp_before_signal;
        }
      }
    }
  }
#endif

  return new_sp;
}

// This ensures that absl::GetStackTrace sets up the Link Register properly.
ABSL_ATTRIBUTE_NOINLINE static void AbslStacktracePowerPCDummyFunction() {
  ABSL_BLOCK_TAIL_CALL_OPTIMIZATION();
}

template <bool IS_STACK_FRAMES, bool IS_WITH_CONTEXT>
ABSL_ATTRIBUTE_NO_SANITIZE_ADDRESS  // May read random elements from stack.
ABSL_ATTRIBUTE_NO_SANITIZE_MEMORY   // May read random elements from stack.
static int UnwindImpl(void** result, int* sizes, int max_depth, int skip_count,
                      const void *ucp, int *min_dropped_frames) {
  void **sp;
  // Apple macOS uses an old version of gnu as -- both Darwin 7.9.0 (Panther)
  // and Darwin 8.8.1 (Tiger) use as 1.38.  This means we have to use a
  // different asm syntax.  I don't know quite the best way to discriminate
  // systems using the old as from the new one; I've gone with __APPLE__.
#ifdef __APPLE__
  __asm__ volatile ("mr %0,r1" : "=r" (sp));
#else
  __asm__ volatile ("mr %0,1" : "=r" (sp));
#endif

  // On PowerPC, the "Link Register" or "Link Record" (LR), is a stack
  // entry that holds the return address of the subroutine call (what
  // instruction we run after our function finishes).  This is the
  // same as the stack-pointer of our parent routine, which is what we
  // want here.  While the compiler will always(?) set up LR for
  // subroutine calls, it may not for leaf functions (such as this one).
  // This routine forces the compiler (at least gcc) to push it anyway.
  AbslStacktracePowerPCDummyFunction();

  // The LR save area is used by the callee, so the top entry is bogus.
  skip_count++;

  int n = 0;

  // Unlike ABIs of X86 and ARM, PowerPC ABIs say that return address (in
  // the link register) of a function call is stored in the caller's stack
  // frame instead of the callee's.  When we look for the return address
  // associated with a stack frame, we need to make sure that there is a
  // caller frame before it.  So we call NextStackFrame before entering the
  // loop below and check next_sp instead of sp for loop termination.
  // The outermost frame is set up by runtimes and it does not have a
  // caller frame, so it is skipped.

  // The absl::GetStackFrames routine is called when we are in some
  // informational context (the failure signal handler for example).
  // Use the non-strict unwinding rules to produce a stack trace
  // that is as complete as possible (even if it contains a few
  // bogus entries in some rare cases).
  void **next_sp = NextStackFrame<!IS_STACK_FRAMES, IS_WITH_CONTEXT>(sp, ucp);

  while (next_sp && n < max_depth) {
    if (skip_count > 0) {
      skip_count--;
    } else {
      result[n] = StacktracePowerPCGetLR(sp);
      if (IS_STACK_FRAMES) {
        if (next_sp > sp) {
          sizes[n] = (uintptr_t)next_sp - (uintptr_t)sp;
        } else {
          // A frame-size of 0 is used to indicate unknown frame size.
          sizes[n] = 0;
        }
      }
      n++;
    }

    sp = next_sp;
    next_sp = NextStackFrame<!IS_STACK_FRAMES, IS_WITH_CONTEXT>(sp, ucp);
  }

  if (min_dropped_frames != nullptr) {
    // Implementation detail: we clamp the max of frames we are willing to
    // count, so as not to spend too much time in the loop below.
    const int kMaxUnwind = 1000;
    int num_dropped_frames = 0;
    for (int j = 0; next_sp != nullptr && j < kMaxUnwind; j++) {
      if (skip_count > 0) {
        skip_count--;
      } else {
        num_dropped_frames++;
      }
      next_sp = NextStackFrame<!IS_STACK_FRAMES, IS_WITH_CONTEXT>(next_sp, ucp);
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

#endif  // ABSL_DEBUGGING_INTERNAL_STACKTRACE_POWERPC_INL_H_
