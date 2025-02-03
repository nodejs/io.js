// Copyright (c) 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Slightly adapted for inclusion in V8.
// Copyright 2024 the V8 project authors. All rights reserved.

#include <signal.h>

#include "src/base/build_config.h"
#include "src/base/debug/stack_trace.h"
#include "src/base/free_deleter.h"
#include "src/base/logging.h"
#include "src/base/macros.h"

namespace v8 {
namespace base {
namespace debug {

namespace {

bool IsDumpStackInSignalHandler = true;

bool StartThread(void* (*threadEntry)(void*)) {
  // based on Thread::Start()
  int result;
  pthread_attr_t attr;
  memset(&attr, 0, sizeof(attr));
  result = pthread_attr_init(&attr);
  if (result != 0) return false;
  constexpr size_t kDefaultStackSize = 4 * 1024 * 1024;
  size_t stack_size;
  result = pthread_attr_getstacksize(&attr, &stack_size);
  DCHECK_EQ(0, result);
  if (stack_size < kDefaultStackSize) stack_size = kDefaultStackSize;

  result = pthread_attr_setstacksize(&attr, stack_size);
  if (result != 0) return pthread_attr_destroy(&attr), false;
  {
    std::mutex lock_guard;
    pthread_t thread_;
    result = pthread_create(&thread_, &attr, threadEntry, nullptr);
    if (result != 0) {
      perror("pthread_create");
      return pthread_attr_destroy(&attr), false;
    }
  }
  result = pthread_attr_destroy(&attr);
  return result == 0;
}

void StackDumpSignalHandler(int signal, siginfo_t* info, void* void_context) {
  fprintf(stderr, "Received signal %d\n", signal);
  if (signal == SIGABRT) {
    // From third_party/zoslib, will first call __display_traceback().
    abort();
  }
  if (IsDumpStackInSignalHandler) __display_backtrace(STDERR_FILENO);
  raise(signal);
}

void* StackDumpingSignalThread(void* data) {
  struct sigaction sigpipe_action;
  memset(&sigpipe_action, 0, sizeof(sigpipe_action));
  sigpipe_action.sa_handler = SIG_IGN;
  sigemptyset(&sigpipe_action.sa_mask);
  bool success = (sigaction(SIGPIPE, &sigpipe_action, nullptr) == 0);

  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_flags = SA_RESETHAND | SA_SIGINFO | SA_ONSTACK;
  action.sa_sigaction = &StackDumpSignalHandler;
  sigemptyset(&action.sa_mask);

  success &= (sigaction(SIGILL, &action, nullptr) == 0);
  success &= (sigaction(SIGABRT, &action, nullptr) == 0);
  success &= (sigaction(SIGFPE, &action, nullptr) == 0);
  success &= (sigaction(SIGBUS, &action, nullptr) == 0);
  success &= (sigaction(SIGSEGV, &action, nullptr) == 0);
  success &= (sigaction(SIGSYS, &action, nullptr) == 0);
  success &= (sigaction(SIGINT, &action, nullptr) == 0);
  success &= (sigaction(SIGTERM, &action, nullptr) == 0);

  CHECK_EQ(true, success);

  while (1) {
    CHECK_EQ(pause(), -1);
    CHECK_EQ(errno, EINTR);
  }
}

}  // namespace

bool EnableInProcessStackDumping() {
  IsDumpStackInSignalHandler = true;
  bool success = StartThread(StackDumpingSignalThread);
  CHECK_EQ(true, success);
  // Block all signals on the main thread:
  sigset_t set;
  sigfillset(&set);
  CHECK_EQ(0, pthread_sigmask(SIG_BLOCK, &set, NULL));
  return success;
}

void DisableSignalStackDump() {
  IsDumpStackInSignalHandler = false;
  // zoslib's abort() displays backtrace by default, so disable it:
  __set_backtrace_on_abort(false);
}

StackTrace::StackTrace() {}

void StackTrace::Print() const { __display_backtrace(STDERR_FILENO); }

void StackTrace::OutputToStream(std::ostream* os) const {
  // TODO(gabylb): zos - pending std::osstream version in zoslib:
  // __display_backtrace(os);
  UNREACHABLE();
}

}  // namespace debug
}  // namespace base
}  // namespace v8
