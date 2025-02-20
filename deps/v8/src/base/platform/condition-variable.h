// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_PLATFORM_CONDITION_VARIABLE_H_
#define V8_BASE_PLATFORM_CONDITION_VARIABLE_H_

#include "absl/synchronization/mutex.h"
#include "src/base/base-export.h"
#include "src/base/lazy-instance.h"
#include "src/base/platform/mutex.h"

#if V8_OS_STARBOARD
#include "starboard/common/condition_variable.h"
#endif

namespace v8 {
namespace base {

// Forward declarations.
class ConditionVariableEvent;
class TimeDelta;

// -----------------------------------------------------------------------------
// ConditionVariable
//
// This class is a synchronization primitive that can be used to block a thread,
// or multiple threads at the same time, until:
// - a notification is received from another thread,
// - a timeout expires, or
// - a spurious wakeup occurs
// Any thread that intends to wait on a ConditionVariable has to acquire a lock
// on a Mutex first. The |Wait()| and |WaitFor()| operations atomically release
// the mutex and suspend the execution of the calling thread. When the condition
// variable is notified, the thread is awakened, and the mutex is reacquired.

class V8_BASE_EXPORT ConditionVariable {
 public:
  ConditionVariable();
  ConditionVariable(const ConditionVariable&) = delete;
  ConditionVariable& operator=(const ConditionVariable&) = delete;
  ~ConditionVariable();

  // If any threads are waiting on this condition variable, calling
  // |NotifyOne()| unblocks one of the waiting threads.
  void NotifyOne();

  // Unblocks all threads currently waiting for this condition variable.
  void NotifyAll();

  // |Wait()| causes the calling thread to block until the condition variable is
  // notified or a spurious wakeup occurs. Atomically releases the mutex, blocks
  // the current executing thread, and adds it to the list of threads waiting on
  // this condition variable. The thread will be unblocked when |NotifyAll()| or
  // |NotifyOne()| is executed. It may also be unblocked spuriously. When
  // unblocked, regardless of the reason, the lock on the mutex is reacquired
  // and |Wait()| exits.
  void Wait(Mutex* mutex);

  // Atomically releases the mutex, blocks the current executing thread, and
  // adds it to the list of threads waiting on this condition variable. The
  // thread will be unblocked when |NotifyAll()| or |NotifyOne()| is executed,
  // or when the relative timeout |rel_time| expires. It may also be unblocked
  // spuriously. When unblocked, regardless of the reason, the lock on the mutex
  // is reacquired and |WaitFor()| exits. Returns true if the condition variable
  // was notified prior to the timeout.
  bool WaitFor(Mutex* mutex, const TimeDelta& rel_time) V8_WARN_UNUSED_RESULT;

 private:
  absl::CondVar native_handle_;
};

// POD ConditionVariable initialized lazily (i.e. the first time Pointer() is
// called).
// Usage:
//   static LazyConditionVariable my_condvar =
//       LAZY_CONDITION_VARIABLE_INITIALIZER;
//
//   void my_function() {
//     MutexGuard lock_guard(&my_mutex);
//     my_condvar.Pointer()->Wait(&my_mutex);
//   }
using LazyConditionVariable =
    LazyStaticInstance<ConditionVariable,
                       DefaultConstructTrait<ConditionVariable>,
                       ThreadSafeInitOnceTrait>::type;

#define LAZY_CONDITION_VARIABLE_INITIALIZER LAZY_STATIC_INSTANCE_INITIALIZER

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_PLATFORM_CONDITION_VARIABLE_H_
