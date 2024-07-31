// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/platform/semaphore.h"

#if V8_OS_DARWIN
#include <dispatch/dispatch.h>
#elif V8_OS_WIN
#include <windows.h>
#endif

#include <errno.h>

#include "src/base/logging.h"
#include "src/base/platform/elapsed-timer.h"
#include "src/base/platform/time.h"

namespace v8 {
namespace base {

#if V8_OS_DARWIN

Semaphore::Semaphore(int count) {
  native_handle_ = dispatch_semaphore_create(count);
  DCHECK(native_handle_);
}

Semaphore::~Semaphore() { dispatch_release(native_handle_); }

void Semaphore::Signal() { dispatch_semaphore_signal(native_handle_); }

void Semaphore::Wait() {
  dispatch_semaphore_wait(native_handle_, DISPATCH_TIME_FOREVER);
}


bool Semaphore::WaitFor(const TimeDelta& rel_time) {
  dispatch_time_t timeout =
      dispatch_time(DISPATCH_TIME_NOW, rel_time.InNanoseconds());
  return dispatch_semaphore_wait(native_handle_, timeout) == 0;
}

#elif V8_OS_POSIX

Semaphore::Semaphore(int count) {
  DCHECK_GE(count, 0);
  int result = sem_init(&native_handle_, 0, count);
  DCHECK_EQ(0, result);
  USE(result);
}


Semaphore::~Semaphore() {
  int result = sem_destroy(&native_handle_);
  DCHECK_EQ(0, result);
  USE(result);
}

void Semaphore::Signal() {
  int result = sem_post(&native_handle_);
  // This check may fail with <libc-2.21, which we use on the try bots, if the
  // semaphore is destroyed while sem_post is still executed. A work around is
  // to extend the lifetime of the semaphore.
  if (result != 0) {
    FATAL("Error when signaling semaphore, errno: %d", errno);
  }
}


void Semaphore::Wait() {
  while (true) {
    int result = sem_wait(&native_handle_);
    if (result == 0) return;  // Semaphore was signalled.
    // Signal caused spurious wakeup.
    DCHECK_EQ(-1, result);
    DCHECK_EQ(EINTR, errno);
  }
}


bool Semaphore::WaitFor(const TimeDelta& rel_time) {
  // Compute the time for end of timeout.
  const Time time = Time::NowFromSystemTime() + rel_time;
  const struct timespec ts = time.ToTimespec();

  // Wait for semaphore signalled or timeout.
  while (true) {
    int result = sem_timedwait(&native_handle_, &ts);
    if (result == 0) return true;  // Semaphore was signalled.
#if V8_LIBC_GLIBC && !V8_GLIBC_PREREQ(2, 4)
    if (result > 0) {
      // sem_timedwait in glibc prior to 2.3.4 returns the errno instead of -1.
      errno = result;
      result = -1;
    }
#endif
    if (result == -1 && errno == ETIMEDOUT) {
      // Timed out while waiting for semaphore.
      return false;
    }
    // Signal caused spurious wakeup.
    DCHECK_EQ(-1, result);
    DCHECK_EQ(EINTR, errno);
  }
}

#elif V8_OS_WIN

Semaphore::Semaphore(int count) {
  DCHECK_GE(count, 0);
  native_handle_ = ::CreateSemaphoreA(nullptr, count, 0x7FFFFFFF, nullptr);
  DCHECK_NOT_NULL(native_handle_);
}


Semaphore::~Semaphore() {
  BOOL result = CloseHandle(native_handle_);
  DCHECK(result);
  USE(result);
}

void Semaphore::Signal() {
  LONG dummy;
  BOOL result = ReleaseSemaphore(native_handle_, 1, &dummy);
  DCHECK(result);
  USE(result);
}


void Semaphore::Wait() {
  DWORD result = WaitForSingleObject(native_handle_, INFINITE);
  DCHECK(result == WAIT_OBJECT_0);
  USE(result);
}


bool Semaphore::WaitFor(const TimeDelta& rel_time) {
  TimeTicks now = TimeTicks::Now();
  TimeTicks end = now + rel_time;
  while (true) {
    int64_t msec = (end - now).InMilliseconds();
    if (msec >= static_cast<int64_t>(INFINITE)) {
      DWORD result = WaitForSingleObject(native_handle_, INFINITE - 1);
      if (result == WAIT_OBJECT_0) {
        return true;
      }
      DCHECK(result == WAIT_TIMEOUT);
      now = TimeTicks::Now();
    } else {
      DWORD result = WaitForSingleObject(
          native_handle_, (msec < 0) ? 0 : static_cast<DWORD>(msec));
      if (result == WAIT_TIMEOUT) {
        return false;
      }
      DCHECK(result == WAIT_OBJECT_0);
      return true;
    }
  }
}

#elif V8_OS_STARBOARD

Semaphore::Semaphore(int count) : native_handle_(count) { DCHECK_GE(count, 0); }

Semaphore::~Semaphore() {}

void Semaphore::Signal() { native_handle_.Put(); }

void Semaphore::Wait() { native_handle_.Take(); }

bool Semaphore::WaitFor(const TimeDelta& rel_time) {
  int64_t microseconds = rel_time.InMicroseconds();
  return native_handle_.TakeWait(microseconds);
}

#endif  // V8_OS_DARWIN

}  // namespace base
}  // namespace v8
