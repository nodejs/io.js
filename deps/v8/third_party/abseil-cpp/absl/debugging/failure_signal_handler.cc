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
//

#include "absl/debugging/failure_signal_handler.h"

#include "absl/base/config.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sched.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#ifdef ABSL_HAVE_MMAP
#include <sys/mman.h>
#if defined(MAP_ANON) && !defined(MAP_ANONYMOUS)
#define MAP_ANONYMOUS MAP_ANON
#endif
#endif

#ifdef __linux__
#include <sys/prctl.h>
#endif

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "absl/base/attributes.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/internal/sysinfo.h"
#include "absl/debugging/internal/examine_stack.h"
#include "absl/debugging/stacktrace.h"

#if !defined(_WIN32) && !defined(__wasi__)
#define ABSL_HAVE_SIGACTION
// Apple WatchOS and TVOS don't allow sigaltstack
// Apple macOS has sigaltstack, but using it makes backtrace() unusable.
#if !(defined(TARGET_OS_OSX) && TARGET_OS_OSX) &&     \
    !(defined(TARGET_OS_WATCH) && TARGET_OS_WATCH) && \
    !(defined(TARGET_OS_TV) && TARGET_OS_TV) && !defined(__QNX__)
#define ABSL_HAVE_SIGALTSTACK
#endif
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN

ABSL_CONST_INIT static FailureSignalHandlerOptions fsh_options;

// Resets the signal handler for signo to the default action for that
// signal, then raises the signal.
static void RaiseToDefaultHandler(int signo) {
  signal(signo, SIG_DFL);
  raise(signo);
}

struct FailureSignalData {
  const int signo;
  const char* const as_string;
#ifdef ABSL_HAVE_SIGACTION
  struct sigaction previous_action;
  // StructSigaction is used to silence -Wmissing-field-initializers.
  using StructSigaction = struct sigaction;
#define FSD_PREVIOUS_INIT FailureSignalData::StructSigaction()
#else
  void (*previous_handler)(int);
#define FSD_PREVIOUS_INIT SIG_DFL
#endif
};

ABSL_CONST_INIT static FailureSignalData failure_signal_data[] = {
    {SIGSEGV, "SIGSEGV", FSD_PREVIOUS_INIT},
    {SIGILL, "SIGILL", FSD_PREVIOUS_INIT},
    {SIGFPE, "SIGFPE", FSD_PREVIOUS_INIT},
    {SIGABRT, "SIGABRT", FSD_PREVIOUS_INIT},
    {SIGTERM, "SIGTERM", FSD_PREVIOUS_INIT},
#ifndef _WIN32
    {SIGBUS, "SIGBUS", FSD_PREVIOUS_INIT},
    {SIGTRAP, "SIGTRAP", FSD_PREVIOUS_INIT},
#endif
};

#undef FSD_PREVIOUS_INIT

static void RaiseToPreviousHandler(int signo) {
  // Search for the previous handler.
  for (const auto& it : failure_signal_data) {
    if (it.signo == signo) {
#ifdef ABSL_HAVE_SIGACTION
      sigaction(signo, &it.previous_action, nullptr);
#else
      signal(signo, it.previous_handler);
#endif
      raise(signo);
      return;
    }
  }

  // Not found, use the default handler.
  RaiseToDefaultHandler(signo);
}

namespace debugging_internal {

const char* FailureSignalToString(int signo) {
  for (const auto& it : failure_signal_data) {
    if (it.signo == signo) {
      return it.as_string;
    }
  }
  return "";
}

}  // namespace debugging_internal

#ifdef ABSL_HAVE_SIGALTSTACK

static bool SetupAlternateStackOnce() {
#if defined(__wasm__) || defined(__asjms__)
  const size_t page_mask = getpagesize() - 1;
#else
  const size_t page_mask = static_cast<size_t>(sysconf(_SC_PAGESIZE)) - 1;
#endif
  size_t stack_size =
      (std::max(static_cast<size_t>(SIGSTKSZ), size_t{65536}) + page_mask) &
      ~page_mask;
#if defined(ABSL_HAVE_ADDRESS_SANITIZER) || \
    defined(ABSL_HAVE_MEMORY_SANITIZER) || defined(ABSL_HAVE_THREAD_SANITIZER)
  // Account for sanitizer instrumentation requiring additional stack space.
  stack_size *= 5;
#endif

  stack_t sigstk;
  memset(&sigstk, 0, sizeof(sigstk));
  sigstk.ss_size = stack_size;

#ifdef ABSL_HAVE_MMAP
#ifndef MAP_STACK
#define MAP_STACK 0
#endif
  sigstk.ss_sp = mmap(nullptr, sigstk.ss_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
  if (sigstk.ss_sp == MAP_FAILED) {
    ABSL_RAW_LOG(FATAL, "mmap() for alternate signal stack failed");
  }
#else
  sigstk.ss_sp = malloc(sigstk.ss_size);
  if (sigstk.ss_sp == nullptr) {
    ABSL_RAW_LOG(FATAL, "malloc() for alternate signal stack failed");
  }
#endif

  if (sigaltstack(&sigstk, nullptr) != 0) {
    ABSL_RAW_LOG(FATAL, "sigaltstack() failed with errno=%d", errno);
  }

#ifdef __linux__
#if defined(PR_SET_VMA) && defined(PR_SET_VMA_ANON_NAME)
  // Make a best-effort attempt to name the allocated region in
  // /proc/$PID/smaps.
  //
  // The call to prctl() may fail if the kernel was not configured with the
  // CONFIG_ANON_VMA_NAME kernel option.  This is OK since the call is
  // primarily a debugging aid.
  prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, sigstk.ss_sp, sigstk.ss_size,
        "absl-signalstack");
#endif
#endif  // __linux__

  return true;
}

#endif

#ifdef ABSL_HAVE_SIGACTION

// Sets up an alternate stack for signal handlers once.
// Returns the appropriate flag for sig_action.sa_flags
// if the system supports using an alternate stack.
static int MaybeSetupAlternateStack() {
#ifdef ABSL_HAVE_SIGALTSTACK
  ABSL_ATTRIBUTE_UNUSED static const bool kOnce = SetupAlternateStackOnce();
  return SA_ONSTACK;
#else
  return 0;
#endif
}

static void InstallOneFailureHandler(FailureSignalData* data,
                                     void (*handler)(int, siginfo_t*, void*)) {
  struct sigaction act;
  memset(&act, 0, sizeof(act));
  sigemptyset(&act.sa_mask);
  act.sa_flags |= SA_SIGINFO;
  // SA_NODEFER is required to handle SIGABRT from
  // ImmediateAbortSignalHandler().
  act.sa_flags |= SA_NODEFER;
  if (fsh_options.use_alternate_stack) {
    act.sa_flags |= MaybeSetupAlternateStack();
  }
  act.sa_sigaction = handler;
  ABSL_RAW_CHECK(sigaction(data->signo, &act, &data->previous_action) == 0,
                 "sigaction() failed");
}

#else

static void InstallOneFailureHandler(FailureSignalData* data,
                                     void (*handler)(int)) {
  data->previous_handler = signal(data->signo, handler);
  ABSL_RAW_CHECK(data->previous_handler != SIG_ERR, "signal() failed");
}

#endif

static void WriteSignalMessage(int signo, int cpu,
                               void (*writerfn)(const char*)) {
  char buf[96];
  char on_cpu[32] = {0};
  if (cpu != -1) {
    snprintf(on_cpu, sizeof(on_cpu), " on cpu %d", cpu);
  }
  const char* const signal_string =
      debugging_internal::FailureSignalToString(signo);
  if (signal_string != nullptr && signal_string[0] != '\0') {
    snprintf(buf, sizeof(buf), "*** %s received at time=%ld%s ***\n",
             signal_string,
             static_cast<long>(time(nullptr)),  // NOLINT(runtime/int)
             on_cpu);
  } else {
    snprintf(buf, sizeof(buf), "*** Signal %d received at time=%ld%s ***\n",
             signo, static_cast<long>(time(nullptr)),  // NOLINT(runtime/int)
             on_cpu);
  }
  writerfn(buf);
}

// `void*` might not be big enough to store `void(*)(const char*)`.
struct WriterFnStruct {
  void (*writerfn)(const char*);
};

// Many of the absl::debugging_internal::Dump* functions in
// examine_stack.h take a writer function pointer that has a void* arg
// for historical reasons. failure_signal_handler_writer only takes a
// data pointer. This function converts between these types.
static void WriterFnWrapper(const char* data, void* arg) {
  static_cast<WriterFnStruct*>(arg)->writerfn(data);
}

// Convenient wrapper around DumpPCAndFrameSizesAndStackTrace() for signal
// handlers. "noinline" so that GetStackFrames() skips the top-most stack
// frame for this function.
ABSL_ATTRIBUTE_NOINLINE static void WriteStackTrace(
    void* ucontext, bool symbolize_stacktrace,
    void (*writerfn)(const char*, void*), void* writerfn_arg) {
  constexpr int kNumStackFrames = 32;
  void* stack[kNumStackFrames];
  int frame_sizes[kNumStackFrames];
  int min_dropped_frames;
  int depth = absl::GetStackFramesWithContext(
      stack, frame_sizes, kNumStackFrames,
      1,  // Do not include this function in stack trace.
      ucontext, &min_dropped_frames);
  absl::debugging_internal::DumpPCAndFrameSizesAndStackTrace(
      absl::debugging_internal::GetProgramCounter(ucontext), stack, frame_sizes,
      depth, min_dropped_frames, symbolize_stacktrace, writerfn, writerfn_arg);
}

// Called by AbslFailureSignalHandler() to write the failure info. It is
// called once with writerfn set to WriteToStderr() and then possibly
// with writerfn set to the user provided function.
static void WriteFailureInfo(int signo, void* ucontext, int cpu,
                             void (*writerfn)(const char*)) {
  WriterFnStruct writerfn_struct{writerfn};
  WriteSignalMessage(signo, cpu, writerfn);
  WriteStackTrace(ucontext, fsh_options.symbolize_stacktrace, WriterFnWrapper,
                  &writerfn_struct);
}

// absl::SleepFor() can't be used here since AbslInternalSleepFor()
// may be overridden to do something that isn't async-signal-safe on
// some platforms.
static void PortableSleepForSeconds(int seconds) {
#ifdef _WIN32
  Sleep(static_cast<DWORD>(seconds * 1000));
#else
  struct timespec sleep_time;
  sleep_time.tv_sec = seconds;
  sleep_time.tv_nsec = 0;
  while (nanosleep(&sleep_time, &sleep_time) != 0 && errno == EINTR) {
  }
#endif
}

#ifdef ABSL_HAVE_ALARM
// AbslFailureSignalHandler() installs this as a signal handler for
// SIGALRM, then sets an alarm to be delivered to the program after a
// set amount of time. If AbslFailureSignalHandler() hangs for more than
// the alarm timeout, ImmediateAbortSignalHandler() will abort the
// program.
static void ImmediateAbortSignalHandler(int) { RaiseToDefaultHandler(SIGABRT); }
#endif

// absl::base_internal::GetTID() returns pid_t on most platforms, but
// returns absl::base_internal::pid_t on Windows.
using GetTidType = decltype(absl::base_internal::GetTID());
ABSL_CONST_INIT static std::atomic<GetTidType> failed_tid(0);

#ifndef ABSL_HAVE_SIGACTION
static void AbslFailureSignalHandler(int signo) {
  void* ucontext = nullptr;
#else
static void AbslFailureSignalHandler(int signo, siginfo_t*, void* ucontext) {
#endif

  const GetTidType this_tid = absl::base_internal::GetTID();
  GetTidType previous_failed_tid = 0;
  if (!failed_tid.compare_exchange_strong(previous_failed_tid, this_tid,
                                          std::memory_order_acq_rel,
                                          std::memory_order_relaxed)) {
    ABSL_RAW_LOG(
        ERROR,
        "Signal %d raised at PC=%p while already in AbslFailureSignalHandler()",
        signo, absl::debugging_internal::GetProgramCounter(ucontext));
    if (this_tid != previous_failed_tid) {
      // Another thread is already in AbslFailureSignalHandler(), so wait
      // a bit for it to finish. If the other thread doesn't kill us,
      // we do so after sleeping.
      PortableSleepForSeconds(3);
      RaiseToDefaultHandler(signo);
      // The recursively raised signal may be blocked until we return.
      return;
    }
  }

  // Increase the chance that the CPU we report was the same CPU on which the
  // signal was received by doing this as early as possible, i.e. after
  // verifying that this is not a recursive signal handler invocation.
  int my_cpu = -1;
#ifdef ABSL_HAVE_SCHED_GETCPU
  my_cpu = sched_getcpu();
#endif

#ifdef ABSL_HAVE_ALARM
  // Set an alarm to abort the program in case this code hangs or deadlocks.
  if (fsh_options.alarm_on_failure_secs > 0) {
    alarm(0);  // Cancel any existing alarms.
    signal(SIGALRM, ImmediateAbortSignalHandler);
    alarm(static_cast<unsigned int>(fsh_options.alarm_on_failure_secs));
  }
#endif

  // First write to stderr.
  WriteFailureInfo(
      signo, ucontext, my_cpu, +[](const char* data) {
        absl::raw_log_internal::AsyncSignalSafeWriteError(data, strlen(data));
      });

  // Riskier code (because it is less likely to be async-signal-safe)
  // goes after this point.
  if (fsh_options.writerfn != nullptr) {
    WriteFailureInfo(signo, ucontext, my_cpu, fsh_options.writerfn);
    fsh_options.writerfn(nullptr);
  }

  if (fsh_options.call_previous_handler) {
    RaiseToPreviousHandler(signo);
  } else {
    RaiseToDefaultHandler(signo);
  }
}

void InstallFailureSignalHandler(const FailureSignalHandlerOptions& options) {
  fsh_options = options;
  for (auto& it : failure_signal_data) {
    InstallOneFailureHandler(&it, AbslFailureSignalHandler);
  }
}

ABSL_NAMESPACE_END
}  // namespace absl
