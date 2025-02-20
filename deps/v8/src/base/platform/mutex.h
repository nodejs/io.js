// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_PLATFORM_MUTEX_H_
#define V8_BASE_PLATFORM_MUTEX_H_

#include <optional>

#include "absl/synchronization/mutex.h"
#include "include/v8config.h"

#if V8_OS_DARWIN
#include <os/lock.h>
#endif

#if V8_OS_POSIX
#include <pthread.h>
#endif

#include "src/base/base-export.h"
#include "src/base/lazy-instance.h"
#include "src/base/logging.h"

#if V8_OS_WIN
#include "src/base/win32-headers.h"
#endif

#if V8_OS_STARBOARD
#include "starboard/common/mutex.h"
#include "starboard/common/recursive_mutex.h"
#include "starboard/common/rwlock.h"
#endif

namespace v8 {
namespace base {

class ConditionVariable;

// Mutex - a replacement for std::mutex
//
// This class is a synchronization primitive that can be used to protect shared
// data from being simultaneously accessed by multiple threads. A mutex offers
// exclusive, non-recursive ownership semantics:
// - A calling thread owns a mutex from the time that it successfully calls
//   either |Lock()| or |TryLock()| until it calls |Unlock()|.
// - When a thread owns a mutex, all other threads will block (for calls to
//   |Lock()|) or receive a |false| return value (for |TryLock()|) if they
//   attempt to claim ownership of the mutex.
// A calling thread must not own the mutex prior to calling |Lock()| or
// |TryLock()|. The behavior of a program is undefined if a mutex is destroyed
// while still owned by some thread. The Mutex class is non-copyable.

class V8_BASE_EXPORT Mutex final {
 public:
  Mutex();
  Mutex(const Mutex&) = delete;
  Mutex& operator=(const Mutex&) = delete;
  ~Mutex();

  // Locks the given mutex. If the mutex is currently unlocked, it becomes
  // locked and owned by the calling thread, and immediately. If the mutex
  // is already locked by another thread, suspends the calling thread until
  // the mutex is unlocked.
  void Lock();

  // Unlocks the given mutex. The mutex is assumed to be locked and owned by
  // the calling thread on entrance.
  void Unlock();

  // Tries to lock the given mutex. Returns whether the mutex was
  // successfully locked.
  // Note: Instead of `DCHECK(!mutex.TryLock())` use `mutex.AssertHeld()`.
  bool TryLock() V8_WARN_UNUSED_RESULT;

  V8_INLINE void AssertHeld() const {
    // If this access results in a race condition being detected by TSan, this
    // means that you in fact did *not* hold the mutex.
    DCHECK_EQ(1, level_);
  }

 private:
#ifdef DEBUG
  // This is being used for Assert* methods. Accesses are only allowed if you
  // actually hold the mutex, otherwise you would get race conditions.
  int level_;
#endif

  V8_INLINE void AssertHeldAndUnmark() {
#ifdef DEBUG
    // If this access results in a race condition being detected by TSan, this
    // means that you in fact did *not* hold the mutex.
    DCHECK_EQ(1, level_);
    level_--;
#endif
  }

  V8_INLINE void AssertUnheldAndMark() {
#ifdef DEBUG
    // This is only invoked *after* actually getting the mutex, so should not
    // result in race conditions.
    DCHECK_EQ(0, level_);
    level_++;
#endif
  }

  friend class ConditionVariable;

  absl::Mutex native_handle_;
};

class V8_BASE_EXPORT SpinningMutex final {
 public:
  SpinningMutex();
  void Lock();
  inline void Unlock();
  inline bool TryLock();
  void AssertHeld() const {}  // Not supported.

 private:
#if V8_OS_DARWIN
  os_unfair_lock lock_;
#else
  absl::Mutex lock_;
#endif
};

#if V8_OS_DARWIN

V8_INLINE bool SpinningMutex::TryLock() {
  return os_unfair_lock_trylock(&lock_);
}

V8_INLINE void SpinningMutex::Unlock() { return os_unfair_lock_unlock(&lock_); }

#else

V8_INLINE bool SpinningMutex::TryLock() ABSL_NO_THREAD_SAFETY_ANALYSIS {
  return lock_.TryLock();
}

V8_INLINE void SpinningMutex::Unlock() ABSL_NO_THREAD_SAFETY_ANALYSIS {
  lock_.Unlock();
}

#endif

// POD Mutex initialized lazily (i.e. the first time Pointer() is called).
// Usage:
//   static LazyMutex my_mutex = LAZY_MUTEX_INITIALIZER;
//
//   void my_function() {
//     MutexGuard guard(my_mutex.Pointer());
//     // Do something.
//   }
//
using LazyMutex = LazyStaticInstance<Mutex, DefaultConstructTrait<Mutex>,
                                     ThreadSafeInitOnceTrait>::type;
#define LAZY_MUTEX_INITIALIZER LAZY_STATIC_INSTANCE_INITIALIZER
using LazySpinningMutex =
    LazyStaticInstance<SpinningMutex, DefaultConstructTrait<SpinningMutex>,
                       ThreadSafeInitOnceTrait>::type;

#define LAZY_SELFISH_MUTEX_INITIALIZER LAZY_STATIC_INSTANCE_INITIALIZER

// RecursiveMutex - a replacement for std::recursive_mutex
//
// This class is a synchronization primitive that can be used to protect shared
// data from being simultaneously accessed by multiple threads. A recursive
// mutex offers exclusive, recursive ownership semantics:
// - A calling thread owns a recursive mutex for a period of time that starts
//   when it successfully calls either |Lock()| or |TryLock()|. During this
//   period, the thread may make additional calls to |Lock()| or |TryLock()|.
//   The period of ownership ends when the thread makes a matching number of
//   calls to |Unlock()|.
// - When a thread owns a recursive mutex, all other threads will block (for
//   calls to |Lock()|) or receive a |false| return value (for |TryLock()|) if
//   they attempt to claim ownership of the recursive mutex.
// - The maximum number of times that a recursive mutex may be locked is
//   unspecified, but after that number is reached, calls to |Lock()| will
//   probably abort the process and calls to |TryLock()| return false.
// The behavior of a program is undefined if a recursive mutex is destroyed
// while still owned by some thread. The RecursiveMutex class is non-copyable.

class V8_BASE_EXPORT RecursiveMutex final {
 public:
  RecursiveMutex();
  RecursiveMutex(const RecursiveMutex&) = delete;
  RecursiveMutex& operator=(const RecursiveMutex&) = delete;
  ~RecursiveMutex();

  // Locks the mutex. If another thread has already locked the mutex, a call to
  // |Lock()| will block execution until the lock is acquired. A thread may call
  // |Lock()| on a recursive mutex repeatedly. Ownership will only be released
  // after the thread makes a matching number of calls to |Unlock()|.
  // The behavior is undefined if the mutex is not unlocked before being
  // destroyed, i.e. some thread still owns it.
  void Lock();

  // Unlocks the mutex if its level of ownership is 1 (there was exactly one
  // more call to |Lock()| than there were calls to unlock() made by this
  // thread), reduces the level of ownership by 1 otherwise. The mutex must be
  // locked by the current thread of execution, otherwise, the behavior is
  // undefined.
  void Unlock();

  // Tries to lock the given mutex. Returns whether the mutex was
  // successfully locked.
  // Note: Instead of `DCHECK(!mutex.TryLock())` use `mutex.AssertHeld()`.
  bool TryLock() V8_WARN_UNUSED_RESULT;

  V8_INLINE void AssertHeld() const {
    // If this access results in a race condition being detected by TSan, this
    // mean that you in fact did *not* hold the mutex.
    DCHECK_LT(0, level_);
  }

 private:
  // The implementation-defined native handle type.
#if V8_OS_POSIX
  using NativeHandle = pthread_mutex_t;
#elif V8_OS_WIN
  using NativeHandle = V8_CRITICAL_SECTION;
#elif V8_OS_STARBOARD
  using NativeHandle = starboard::RecursiveMutex;
#endif

  NativeHandle native_handle_;
#ifdef DEBUG
  // This is being used for Assert* methods. Accesses are only allowed if you
  // actually hold the mutex, otherwise you would get race conditions.
  int level_;
#endif
};


// POD RecursiveMutex initialized lazily (i.e. the first time Pointer() is
// called).
// Usage:
//   static LazyRecursiveMutex my_mutex = LAZY_RECURSIVE_MUTEX_INITIALIZER;
//
//   void my_function() {
//     LockGuard<RecursiveMutex> guard(my_mutex.Pointer());
//     // Do something.
//   }
//
using LazyRecursiveMutex =
    LazyStaticInstance<RecursiveMutex, DefaultConstructTrait<RecursiveMutex>,
                       ThreadSafeInitOnceTrait>::type;

#define LAZY_RECURSIVE_MUTEX_INITIALIZER LAZY_STATIC_INSTANCE_INITIALIZER

// LockGuard
//
// This class is a mutex wrapper that provides a convenient RAII-style mechanism
// for owning a mutex for the duration of a scoped block.
// When a LockGuard object is created, it attempts to take ownership of the
// mutex it is given. When control leaves the scope in which the LockGuard
// object was created, the LockGuard is destructed and the mutex is released.
// The LockGuard class is non-copyable.

template <typename Mutex>
class V8_NODISCARD LockGuard final {
 public:
  explicit LockGuard(Mutex* mutex) : mutex_(mutex) {
    DCHECK_NOT_NULL(mutex_);
    mutex_->Lock();
  }
  explicit LockGuard(Mutex& mutex) : mutex_(&mutex) {
    // `mutex_` is guaranteed to be non-null here.
    mutex_->Lock();
  }
  LockGuard(const LockGuard&) = delete;
  LockGuard& operator=(const LockGuard&) = delete;
  LockGuard(LockGuard&& other) V8_NOEXCEPT : mutex_(other.mutex_) {
    DCHECK_NOT_NULL(mutex_);
    other.mutex_ = nullptr;
  }
  ~LockGuard() {
    if (mutex_) {
      // mutex_ may have been moved away.
      mutex_->Unlock();
    }
  }

 private:
  Mutex* mutex_;
};

using MutexGuard = LockGuard<Mutex>;
using SpinningMutexGuard = LockGuard<SpinningMutex>;
using RecursiveMutexGuard = LockGuard<RecursiveMutex>;

class V8_NODISCARD SpinningMutexGuardIf final {
 public:
  SpinningMutexGuardIf(SpinningMutex* mutex, bool enable_mutex) {
    if (enable_mutex) {
      mutex_.emplace(mutex);
    }
  }
  SpinningMutexGuardIf(const SpinningMutexGuardIf&) = delete;
  SpinningMutexGuardIf& operator=(const SpinningMutexGuardIf&) = delete;

 private:
  std::optional<SpinningMutexGuard> mutex_;
};

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_PLATFORM_MUTEX_H_
