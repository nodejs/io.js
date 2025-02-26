// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/platform/condition-variable.h"

#include <errno.h>
#include <time.h>

#include "absl/time/time.h"
#include "src/base/platform/time.h"

#if V8_OS_WIN
#include <windows.h>
#endif

namespace v8 {
namespace base {

ConditionVariable::ConditionVariable() = default;
ConditionVariable::~ConditionVariable() = default;

void ConditionVariable::NotifyOne() { native_handle_.Signal(); }

void ConditionVariable::NotifyAll() { native_handle_.SignalAll(); }

void ConditionVariable::Wait(Mutex* mutex) {
  mutex->AssertHeldAndUnmark();
  native_handle_.Wait(&mutex->native_handle_);
  mutex->AssertUnheldAndMark();
}

bool ConditionVariable::WaitFor(Mutex* mutex, const TimeDelta& rel_time) {
  uint64_t time = rel_time.InNanoseconds();
  absl::Duration duration = absl::Nanoseconds(time);

  mutex->AssertHeldAndUnmark();
  bool timed_out =
      native_handle_.WaitWithTimeout(&mutex->native_handle_, duration);

  mutex->AssertUnheldAndMark();

  return !timed_out;
}

}  // namespace base
}  // namespace v8
