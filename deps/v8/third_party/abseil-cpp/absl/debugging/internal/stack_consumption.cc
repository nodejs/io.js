//
// Copyright 2018 The Abseil Authors.
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

#include "absl/debugging/internal/stack_consumption.h"

#ifdef ABSL_INTERNAL_HAVE_DEBUGGING_STACK_CONSUMPTION

#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "absl/base/attributes.h"
#include "absl/base/internal/raw_logging.h"

#if defined(MAP_ANON) && !defined(MAP_ANONYMOUS)
#define MAP_ANONYMOUS MAP_ANON
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace debugging_internal {
namespace {

// This code requires that we know the direction in which the stack
// grows. It is commonly believed that this can be detected by putting
// a variable on the stack and then passing its address to a function
// that compares the address of this variable to the address of a
// variable on the function's own stack. However, this is unspecified
// behavior in C++: If two pointers p and q of the same type point to
// different objects that are not members of the same object or
// elements of the same array or to different functions, or if only
// one of them is null, the results of p<q, p>q, p<=q, and p>=q are
// unspecified. Therefore, instead we hardcode the direction of the
// stack on platforms we know about.
#if defined(__i386__) || defined(__x86_64__) || defined(__ppc__) || \
    defined(__aarch64__) || defined(__riscv)
constexpr bool kStackGrowsDown = true;
#else
#error Need to define kStackGrowsDown
#endif

// To measure the stack footprint of some code, we create a signal handler
// (for SIGUSR2 say) that exercises this code on an alternate stack. This
// alternate stack is initialized to some known pattern (0x55, 0x55, 0x55,
// ...). We then self-send this signal, and after the signal handler returns,
// look at the alternate stack buffer to see what portion has been touched.
//
// This trick gives us the the stack footprint of the signal handler.  But the
// signal handler, even before the code for it is exercised, consumes some
// stack already. We however only want the stack usage of the code inside the
// signal handler. To measure this accurately, we install two signal handlers:
// one that does nothing and just returns, and the user-provided signal
// handler. The difference between the stack consumption of these two signals
// handlers should give us the stack foorprint of interest.

void EmptySignalHandler(int) {}

// This is arbitrary value, and could be increase further, at the cost of
// memset()ting it all to known sentinel value.
constexpr int kAlternateStackSize = 64 << 10;  // 64KiB

constexpr int kSafetyMargin = 32;
constexpr char kAlternateStackFillValue = 0x55;

// These helper functions look at the alternate stack buffer, and figure
// out what portion of this buffer has been touched - this is the stack
// consumption of the signal handler running on this alternate stack.
// This function will return -1 if the alternate stack buffer has not been
// touched. It will abort the program if the buffer has overflowed or is about
// to overflow.
int GetStackConsumption(const void* const altstack) {
  const char* begin;
  int increment;
  if (kStackGrowsDown) {
    begin = reinterpret_cast<const char*>(altstack);
    increment = 1;
  } else {
    begin = reinterpret_cast<const char*>(altstack) + kAlternateStackSize - 1;
    increment = -1;
  }

  for (int usage_count = kAlternateStackSize; usage_count > 0; --usage_count) {
    if (*begin != kAlternateStackFillValue) {
      ABSL_RAW_CHECK(usage_count <= kAlternateStackSize - kSafetyMargin,
                     "Buffer has overflowed or is about to overflow");
      return usage_count;
    }
    begin += increment;
  }

  ABSL_RAW_LOG(FATAL, "Unreachable code");
  return -1;
}

}  // namespace

int GetSignalHandlerStackConsumption(void (*signal_handler)(int)) {
  // The alt-signal-stack cannot be heap allocated because there is a
  // bug in glibc-2.2 where some signal handler setup code looks at the
  // current stack pointer to figure out what thread is currently running.
  // Therefore, the alternate stack must be allocated from the main stack
  // itself.
  void* altstack = mmap(nullptr, kAlternateStackSize, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  ABSL_RAW_CHECK(altstack != MAP_FAILED, "mmap() failed");

  // Set up the alt-signal-stack (and save the older one).
  stack_t sigstk;
  memset(&sigstk, 0, sizeof(sigstk));
  sigstk.ss_sp = altstack;
  sigstk.ss_size = kAlternateStackSize;
  sigstk.ss_flags = 0;
  stack_t old_sigstk;
  memset(&old_sigstk, 0, sizeof(old_sigstk));
  ABSL_RAW_CHECK(sigaltstack(&sigstk, &old_sigstk) == 0,
                 "sigaltstack() failed");

  // Set up SIGUSR1 and SIGUSR2 signal handlers (and save the older ones).
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  struct sigaction old_sa1, old_sa2;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_ONSTACK;

  // SIGUSR1 maps to EmptySignalHandler.
  sa.sa_handler = EmptySignalHandler;
  ABSL_RAW_CHECK(sigaction(SIGUSR1, &sa, &old_sa1) == 0, "sigaction() failed");

  // SIGUSR2 maps to signal_handler.
  sa.sa_handler = signal_handler;
  ABSL_RAW_CHECK(sigaction(SIGUSR2, &sa, &old_sa2) == 0, "sigaction() failed");

  // Send SIGUSR1 signal and measure the stack consumption of the empty
  // signal handler.
  // The first signal might use more stack space. Run once and ignore the
  // results to get that out of the way.
  ABSL_RAW_CHECK(kill(getpid(), SIGUSR1) == 0, "kill() failed");

  memset(altstack, kAlternateStackFillValue, kAlternateStackSize);
  ABSL_RAW_CHECK(kill(getpid(), SIGUSR1) == 0, "kill() failed");
  int base_stack_consumption = GetStackConsumption(altstack);

  // Send SIGUSR2 signal and measure the stack consumption of signal_handler.
  ABSL_RAW_CHECK(kill(getpid(), SIGUSR2) == 0, "kill() failed");
  int signal_handler_stack_consumption = GetStackConsumption(altstack);

  // Now restore the old alt-signal-stack and signal handlers.
  if (old_sigstk.ss_sp == nullptr && old_sigstk.ss_size == 0 &&
      (old_sigstk.ss_flags & SS_DISABLE)) {
    // https://git.musl-libc.org/cgit/musl/commit/src/signal/sigaltstack.c?id=7829f42a2c8944555439380498ab8b924d0f2070
    // The original stack has ss_size==0 and ss_flags==SS_DISABLE, but some
    // versions of musl have a bug that rejects ss_size==0. Work around this by
    // setting ss_size to MINSIGSTKSZ, which should be ignored by the kernel
    // when SS_DISABLE is set.
    old_sigstk.ss_size = static_cast<size_t>(MINSIGSTKSZ);
  }
  ABSL_RAW_CHECK(sigaltstack(&old_sigstk, nullptr) == 0,
                 "sigaltstack() failed");
  ABSL_RAW_CHECK(sigaction(SIGUSR1, &old_sa1, nullptr) == 0,
                 "sigaction() failed");
  ABSL_RAW_CHECK(sigaction(SIGUSR2, &old_sa2, nullptr) == 0,
                 "sigaction() failed");

  ABSL_RAW_CHECK(munmap(altstack, kAlternateStackSize) == 0, "munmap() failed");
  if (signal_handler_stack_consumption != -1 && base_stack_consumption != -1) {
    return signal_handler_stack_consumption - base_stack_consumption;
  }
  return -1;
}

}  // namespace debugging_internal
ABSL_NAMESPACE_END
}  // namespace absl

#else

// https://github.com/abseil/abseil-cpp/issues/1465
// CMake builds on Apple platforms error when libraries are empty.
// Our CMake configuration can avoid this error on header-only libraries,
// but since this library is conditionally empty, including a single
// variable is an easy workaround.
#ifdef __APPLE__
namespace absl {
ABSL_NAMESPACE_BEGIN
namespace debugging_internal {
extern const char kAvoidEmptyStackConsumptionLibraryWarning;
const char kAvoidEmptyStackConsumptionLibraryWarning = 0;
}  // namespace debugging_internal
ABSL_NAMESPACE_END
}  // namespace absl
#endif  // __APPLE__

#endif  // ABSL_INTERNAL_HAVE_DEBUGGING_STACK_CONSUMPTION
