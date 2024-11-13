// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_PLATFORM_MUTEX_H_
#define V8_BASE_PLATFORM_MUTEX_H_

#include <optional>

#include "include/v8config.h"

#if V8_OS_DARWIN
#include <shared_mutex>
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

// ----------------------------------------------------------------------------
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
  bool TryLock() V8_WARN_UNUSED_RESULT;

  // The implementation-defined native handle type.
#if V8_OS_POSIX
  using NativeHandle = pthread_mutex_t;
#elif V8_OS_WIN
  using NativeHandle = V8_SRWLOCK;
#elif V8_OS_STARBOARD
  using NativeHandle = SbMutex;
#endif

  NativeHandle& native_handle() {
    return native_handle_;
  }
  const NativeHandle& native_handle() const {
    return native_handle_;
  }

  V8_INLINE void AssertHeld() const { DCHECK_EQ(1, level_); }
  V8_INLINE void AssertUnheld() const { DCHECK_EQ(0, level_); }

 private:
  NativeHandle native_handle_;
#ifdef DEBUG
  int level_;
#endif

  V8_INLINE void AssertHeldAndUnmark() {
#ifdef DEBUG
    DCHECK_EQ(1, level_);
    level_--;
#endif
  }

  V8_INLINE void AssertUnheldAndMark() {
#ifdef DEBUG
    DCHECK_EQ(0, level_);
    level_++;
#endif
  }

  friend class ConditionVariable;
};

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

// -----------------------------------------------------------------------------
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
  bool TryLock() V8_WARN_UNUSED_RESULT;

  V8_INLINE void AssertHeld() const { DCHECK_LT(0, level_); }

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

// ----------------------------------------------------------------------------
// SharedMutex - a replacement for std::shared_mutex
//
// This class is a synchronization primitive that can be used to protect shared
// data from being simultaneously accessed by multiple threads. In contrast to
// other mutex types which facilitate exclusive access, a shared_mutex has two
// levels of access:
// - shared: several threads can share ownership of the same mutex.
// - exclusive: only one thread can own the mutex.
// Shared mutexes are usually used in situations when multiple readers can
// access the same resource at the same time without causing data races, but
// only one writer can do so.
// The SharedMutex class is non-copyable.

class V8_BASE_EXPORT SharedMutex final {
 public:
  SharedMutex();
  SharedMutex(const SharedMutex&) = delete;
  SharedMutex& operator=(const SharedMutex&) = delete;
  ~SharedMutex();

  // Acquires shared ownership of the {SharedMutex}. If another thread is
  // holding the mutex in exclusive ownership, a call to {LockShared()} will
  // block execution until shared ownership can be acquired.
  // If {LockShared()} is called by a thread that already owns the mutex in any
  // mode (exclusive or shared), the behavior is undefined and outright fails
  // with dchecks on.
  void LockShared();

  // Locks the SharedMutex. If another thread has already locked the mutex, a
  // call to {LockExclusive()} will block execution until the lock is acquired.
  // If {LockExclusive()} is called by a thread that already owns the mutex in
  // any mode (shared or exclusive), the behavior is undefined and outright
  // fails with dchecks on.
  void LockExclusive();

  // Releases the {SharedMutex} from shared ownership by the calling thread.
  // The mutex must be locked by the current thread of execution in shared mode,
  // otherwise, the behavior is undefined and outright fails with dchecks on.
  void UnlockShared();

  // Unlocks the {SharedMutex}. It must be locked by the current thread of
  // execution, otherwise, the behavior is undefined and outright fails with
  // dchecks on.
  void UnlockExclusive();

  // Tries to lock the {SharedMutex} in shared mode. Returns immediately. On
  // successful lock acquisition returns true, otherwise returns false.
  // This function is allowed to fail spuriously and return false even if the
  // mutex is not currenly exclusively locked by any other thread.
  // If it is called by a thread that already owns the mutex in any mode
  // (shared or exclusive), the behavior is undefined, and outright fails with
  // dchecks on.
  bool TryLockShared() V8_WARN_UNUSED_RESULT;

  // Tries to lock the {SharedMutex}. Returns immediately. On successful lock
  // acquisition returns true, otherwise returns false.
  // This function is allowed to fail spuriously and return false even if the
  // mutex is not currently locked by any other thread.
  // If it is called by a thread that already owns the mutex in any mode
  // (shared or exclusive), the behavior is undefined, and outright fails with
  // dchecks on.
  bool TryLockExclusive() V8_WARN_UNUSED_RESULT;

 private:
  // The implementation-defined native handle type.
#if V8_OS_DARWIN
  // pthread_rwlock_t is broken on MacOS when signals are being sent to the
  // process (see https://crbug.com/v8/11399).
  // We thus use std::shared_mutex on MacOS, which does not have this problem.
  using NativeHandle = std::shared_mutex;
#elif V8_OS_POSIX
  using NativeHandle = pthread_rwlock_t;
#elif V8_OS_WIN
  using NativeHandle = V8_SRWLOCK;
#elif V8_OS_STARBOARD
  using NativeHandle = starboard::RWLock;
#endif

  NativeHandle native_handle_;
};

// -----------------------------------------------------------------------------
// LockGuard
//
// This class is a mutex wrapper that provides a convenient RAII-style mechanism
// for owning a mutex for the duration of a scoped block.
// When a LockGuard object is created, it attempts to take ownership of the
// mutex it is given. When control leaves the scope in which the LockGuard
// object was created, the LockGuard is destructed and the mutex is released.
// The LockGuard class is non-copyable.

// Controls whether a LockGuard always requires a valid Mutex or will just
// ignore it if it's nullptr.
enum class NullBehavior { kRequireNotNull, kIgnoreIfNull };

template <typename Mutex, NullBehavior Behavior = NullBehavior::kRequireNotNull>
class V8_NODISCARD LockGuard final {
 public:
  explicit LockGuard(Mutex* mutex) : mutex_(mutex) {
    DCHECK_IMPLIES(Behavior == NullBehavior::kRequireNotNull,
                   mutex_ != nullptr);
    if (has_mutex()) mutex_->Lock();
  }
  LockGuard(const LockGuard&) = delete;
  LockGuard& operator=(const LockGuard&) = delete;
  LockGuard(LockGuard&& other) V8_NOEXCEPT : mutex_(other.mutex_) {
    DCHECK_IMPLIES(Behavior == NullBehavior::kRequireNotNull,
                   mutex_ != nullptr);
    other.mutex_ = nullptr;
  }
  ~LockGuard() {
    if (has_mutex()) mutex_->Unlock();
  }

 private:
  Mutex* mutex_;

  bool V8_INLINE has_mutex() const { return mutex_ != nullptr; }
};

using MutexGuard = LockGuard<Mutex>;
using RecursiveMutexGuard = LockGuard<RecursiveMutex>;

enum MutexSharedType : bool { kShared = true, kExclusive = false };

template <MutexSharedType kIsShared,
          NullBehavior Behavior = NullBehavior::kRequireNotNull>
class V8_NODISCARD SharedMutexGuard final {
 public:
  explicit SharedMutexGuard(SharedMutex* mutex) : mutex_(mutex) {
    if (!has_mutex()) return;
    if (kIsShared) {
      mutex_->LockShared();
    } else {
      mutex_->LockExclusive();
    }
  }
  SharedMutexGuard(const SharedMutexGuard&) = delete;
  SharedMutexGuard& operator=(const SharedMutexGuard&) = delete;
  ~SharedMutexGuard() {
    if (!has_mutex()) return;
    if (kIsShared) {
      mutex_->UnlockShared();
    } else {
      mutex_->UnlockExclusive();
    }
  }

 private:
  SharedMutex* const mutex_;

  bool V8_INLINE has_mutex() const {
    DCHECK_IMPLIES(Behavior == NullBehavior::kRequireNotNull,
                   mutex_ != nullptr);
    return Behavior == NullBehavior::kRequireNotNull || mutex_ != nullptr;
  }
};

template <MutexSharedType kIsShared,
          NullBehavior Behavior = NullBehavior::kRequireNotNull>
class V8_NODISCARD SharedMutexGuardIf final {
 public:
  SharedMutexGuardIf(SharedMutex* mutex, bool enable_mutex) {
    if (enable_mutex) mutex_.emplace(mutex);
  }
  SharedMutexGuardIf(const SharedMutexGuardIf&) = delete;
  SharedMutexGuardIf& operator=(const SharedMutexGuardIf&) = delete;

 private:
  std::optional<SharedMutexGuard<kIsShared, Behavior>> mutex_;
};

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_PLATFORM_MUTEX_H_
