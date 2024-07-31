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
// -----------------------------------------------------------------------------
// mutex.h
// -----------------------------------------------------------------------------
//
// This header file defines a `Mutex` -- a mutually exclusive lock -- and the
// most common type of synchronization primitive for facilitating locks on
// shared resources. A mutex is used to prevent multiple threads from accessing
// and/or writing to a shared resource concurrently.
//
// Unlike a `std::mutex`, the Abseil `Mutex` provides the following additional
// features:
//   * Conditional predicates intrinsic to the `Mutex` object
//   * Shared/reader locks, in addition to standard exclusive/writer locks
//   * Deadlock detection and debug support.
//
// The following helper classes are also defined within this file:
//
//  MutexLock - An RAII wrapper to acquire and release a `Mutex` for exclusive/
//              write access within the current scope.
//
//  ReaderMutexLock
//            - An RAII wrapper to acquire and release a `Mutex` for shared/read
//              access within the current scope.
//
//  WriterMutexLock
//            - Effectively an alias for `MutexLock` above, designed for use in
//              distinguishing reader and writer locks within code.
//
// In addition to simple mutex locks, this file also defines ways to perform
// locking under certain conditions.
//
//  Condition - (Preferred) Used to wait for a particular predicate that
//              depends on state protected by the `Mutex` to become true.
//  CondVar   - A lower-level variant of `Condition` that relies on
//              application code to explicitly signal the `CondVar` when
//              a condition has been met.
//
// See below for more information on using `Condition` or `CondVar`.
//
// Mutexes and mutex behavior can be quite complicated. The information within
// this header file is limited, as a result. Please consult the Mutex guide for
// more complete information and examples.

#ifndef ABSL_SYNCHRONIZATION_MUTEX_H_
#define ABSL_SYNCHRONIZATION_MUTEX_H_

#include <atomic>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/base/internal/identity.h"
#include "absl/base/internal/low_level_alloc.h"
#include "absl/base/internal/thread_identity.h"
#include "absl/base/internal/tsan_mutex_interface.h"
#include "absl/base/port.h"
#include "absl/base/thread_annotations.h"
#include "absl/synchronization/internal/kernel_timeout.h"
#include "absl/synchronization/internal/per_thread_sem.h"
#include "absl/time/time.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

class Condition;
struct SynchWaitParams;

// -----------------------------------------------------------------------------
// Mutex
// -----------------------------------------------------------------------------
//
// A `Mutex` is a non-reentrant (aka non-recursive) Mutually Exclusive lock
// on some resource, typically a variable or data structure with associated
// invariants. Proper usage of mutexes prevents concurrent access by different
// threads to the same resource.
//
// A `Mutex` has two basic operations: `Mutex::Lock()` and `Mutex::Unlock()`.
// The `Lock()` operation *acquires* a `Mutex` (in a state known as an
// *exclusive* -- or *write* -- lock), and the `Unlock()` operation *releases* a
// Mutex. During the span of time between the Lock() and Unlock() operations,
// a mutex is said to be *held*. By design, all mutexes support exclusive/write
// locks, as this is the most common way to use a mutex.
//
// Mutex operations are only allowed under certain conditions; otherwise an
// operation is "invalid", and disallowed by the API. The conditions concern
// both the current state of the mutex and the identity of the threads that
// are performing the operations.
//
// The `Mutex` state machine for basic lock/unlock operations is quite simple:
//
// |                | Lock()                 | Unlock() |
// |----------------+------------------------+----------|
// | Free           | Exclusive              | invalid  |
// | Exclusive      | blocks, then exclusive | Free     |
//
// The full conditions are as follows.
//
// * Calls to `Unlock()` require that the mutex be held, and must be made in the
//   same thread that performed the corresponding `Lock()` operation which
//   acquired the mutex; otherwise the call is invalid.
//
// * The mutex being non-reentrant (or non-recursive) means that a call to
//   `Lock()` or `TryLock()` must not be made in a thread that already holds the
//   mutex; such a call is invalid.
//
// * In other words, the state of being "held" has both a temporal component
//   (from `Lock()` until `Unlock()`) as well as a thread identity component:
//   the mutex is held *by a particular thread*.
//
// An "invalid" operation has undefined behavior. The `Mutex` implementation
// is allowed to do anything on an invalid call, including, but not limited to,
// crashing with a useful error message, silently succeeding, or corrupting
// data structures. In debug mode, the implementation may crash with a useful
// error message.
//
// `Mutex` is not guaranteed to be "fair" in prioritizing waiting threads; it
// is, however, approximately fair over long periods, and starvation-free for
// threads at the same priority.
//
// The lock/unlock primitives are now annotated with lock annotations
// defined in (base/thread_annotations.h). When writing multi-threaded code,
// you should use lock annotations whenever possible to document your lock
// synchronization policy. Besides acting as documentation, these annotations
// also help compilers or static analysis tools to identify and warn about
// issues that could potentially result in race conditions and deadlocks.
//
// For more information about the lock annotations, please see
// [Thread Safety
// Analysis](http://clang.llvm.org/docs/ThreadSafetyAnalysis.html) in the Clang
// documentation.
//
// See also `MutexLock`, below, for scoped `Mutex` acquisition.

class ABSL_LOCKABLE Mutex {
 public:
  // Creates a `Mutex` that is not held by anyone. This constructor is
  // typically used for Mutexes allocated on the heap or the stack.
  //
  // To create `Mutex` instances with static storage duration
  // (e.g. a namespace-scoped or global variable), see
  // `Mutex::Mutex(absl::kConstInit)` below instead.
  Mutex();

  // Creates a mutex with static storage duration.  A global variable
  // constructed this way avoids the lifetime issues that can occur on program
  // startup and shutdown.  (See absl/base/const_init.h.)
  //
  // For Mutexes allocated on the heap and stack, instead use the default
  // constructor, which can interact more fully with the thread sanitizer.
  //
  // Example usage:
  //   namespace foo {
  //   ABSL_CONST_INIT absl::Mutex mu(absl::kConstInit);
  //   }
  explicit constexpr Mutex(absl::ConstInitType);

  ~Mutex();

  // Mutex::Lock()
  //
  // Blocks the calling thread, if necessary, until this `Mutex` is free, and
  // then acquires it exclusively. (This lock is also known as a "write lock.")
  void Lock() ABSL_EXCLUSIVE_LOCK_FUNCTION();

  // Mutex::Unlock()
  //
  // Releases this `Mutex` and returns it from the exclusive/write state to the
  // free state. Calling thread must hold the `Mutex` exclusively.
  void Unlock() ABSL_UNLOCK_FUNCTION();

  // Mutex::TryLock()
  //
  // If the mutex can be acquired without blocking, does so exclusively and
  // returns `true`. Otherwise, returns `false`. Returns `true` with high
  // probability if the `Mutex` was free.
  bool TryLock() ABSL_EXCLUSIVE_TRYLOCK_FUNCTION(true);

  // Mutex::AssertHeld()
  //
  // Require that the mutex be held exclusively (write mode) by this thread.
  //
  // If the mutex is not currently held by this thread, this function may report
  // an error (typically by crashing with a diagnostic) or it may do nothing.
  // This function is intended only as a tool to assist debugging; it doesn't
  // guarantee correctness.
  void AssertHeld() const ABSL_ASSERT_EXCLUSIVE_LOCK();

  // ---------------------------------------------------------------------------
  // Reader-Writer Locking
  // ---------------------------------------------------------------------------

  // A Mutex can also be used as a starvation-free reader-writer lock.
  // Neither read-locks nor write-locks are reentrant/recursive to avoid
  // potential client programming errors.
  //
  // The Mutex API provides `Writer*()` aliases for the existing `Lock()`,
  // `Unlock()` and `TryLock()` methods for use within applications mixing
  // reader/writer locks. Using `Reader*()` and `Writer*()` operations in this
  // manner can make locking behavior clearer when mixing read and write modes.
  //
  // Introducing reader locks necessarily complicates the `Mutex` state
  // machine somewhat. The table below illustrates the allowed state transitions
  // of a mutex in such cases. Note that ReaderLock() may block even if the lock
  // is held in shared mode; this occurs when another thread is blocked on a
  // call to WriterLock().
  //
  // ---------------------------------------------------------------------------
  //     Operation: WriterLock() Unlock()  ReaderLock()           ReaderUnlock()
  // ---------------------------------------------------------------------------
  // State
  // ---------------------------------------------------------------------------
  // Free           Exclusive    invalid   Shared(1)              invalid
  // Shared(1)      blocks       invalid   Shared(2) or blocks    Free
  // Shared(n) n>1  blocks       invalid   Shared(n+1) or blocks  Shared(n-1)
  // Exclusive      blocks       Free      blocks                 invalid
  // ---------------------------------------------------------------------------
  //
  // In comments below, "shared" refers to a state of Shared(n) for any n > 0.

  // Mutex::ReaderLock()
  //
  // Blocks the calling thread, if necessary, until this `Mutex` is either free,
  // or in shared mode, and then acquires a share of it. Note that
  // `ReaderLock()` will block if some other thread has an exclusive/writer lock
  // on the mutex.

  void ReaderLock() ABSL_SHARED_LOCK_FUNCTION();

  // Mutex::ReaderUnlock()
  //
  // Releases a read share of this `Mutex`. `ReaderUnlock` may return a mutex to
  // the free state if this thread holds the last reader lock on the mutex. Note
  // that you cannot call `ReaderUnlock()` on a mutex held in write mode.
  void ReaderUnlock() ABSL_UNLOCK_FUNCTION();

  // Mutex::ReaderTryLock()
  //
  // If the mutex can be acquired without blocking, acquires this mutex for
  // shared access and returns `true`. Otherwise, returns `false`. Returns
  // `true` with high probability if the `Mutex` was free or shared.
  bool ReaderTryLock() ABSL_SHARED_TRYLOCK_FUNCTION(true);

  // Mutex::AssertReaderHeld()
  //
  // Require that the mutex be held at least in shared mode (read mode) by this
  // thread.
  //
  // If the mutex is not currently held by this thread, this function may report
  // an error (typically by crashing with a diagnostic) or it may do nothing.
  // This function is intended only as a tool to assist debugging; it doesn't
  // guarantee correctness.
  void AssertReaderHeld() const ABSL_ASSERT_SHARED_LOCK();

  // Mutex::WriterLock()
  // Mutex::WriterUnlock()
  // Mutex::WriterTryLock()
  //
  // Aliases for `Mutex::Lock()`, `Mutex::Unlock()`, and `Mutex::TryLock()`.
  //
  // These methods may be used (along with the complementary `Reader*()`
  // methods) to distinguish simple exclusive `Mutex` usage (`Lock()`,
  // etc.) from reader/writer lock usage.
  void WriterLock() ABSL_EXCLUSIVE_LOCK_FUNCTION() { this->Lock(); }

  void WriterUnlock() ABSL_UNLOCK_FUNCTION() { this->Unlock(); }

  bool WriterTryLock() ABSL_EXCLUSIVE_TRYLOCK_FUNCTION(true) {
    return this->TryLock();
  }

  // ---------------------------------------------------------------------------
  // Conditional Critical Regions
  // ---------------------------------------------------------------------------

  // Conditional usage of a `Mutex` can occur using two distinct paradigms:
  //
  //   * Use of `Mutex` member functions with `Condition` objects.
  //   * Use of the separate `CondVar` abstraction.
  //
  // In general, prefer use of `Condition` and the `Mutex` member functions
  // listed below over `CondVar`. When there are multiple threads waiting on
  // distinctly different conditions, however, a battery of `CondVar`s may be
  // more efficient. This section discusses use of `Condition` objects.
  //
  // `Mutex` contains member functions for performing lock operations only under
  // certain conditions, of class `Condition`. For correctness, the `Condition`
  // must return a boolean that is a pure function, only of state protected by
  // the `Mutex`. The condition must be invariant w.r.t. environmental state
  // such as thread, cpu id, or time, and must be `noexcept`. The condition will
  // always be invoked with the mutex held in at least read mode, so you should
  // not block it for long periods or sleep it on a timer.
  //
  // Since a condition must not depend directly on the current time, use
  // `*WithTimeout()` member function variants to make your condition
  // effectively true after a given duration, or `*WithDeadline()` variants to
  // make your condition effectively true after a given time.
  //
  // The condition function should have no side-effects aside from debug
  // logging; as a special exception, the function may acquire other mutexes
  // provided it releases all those that it acquires.  (This exception was
  // required to allow logging.)

  // Mutex::Await()
  //
  // Unlocks this `Mutex` and blocks until simultaneously both `cond` is `true`
  // and this `Mutex` can be reacquired, then reacquires this `Mutex` in the
  // same mode in which it was previously held. If the condition is initially
  // `true`, `Await()` *may* skip the release/re-acquire step.
  //
  // `Await()` requires that this thread holds this `Mutex` in some mode.
  void Await(const Condition& cond) {
    AwaitCommon(cond, synchronization_internal::KernelTimeout::Never());
  }

  // Mutex::LockWhen()
  // Mutex::ReaderLockWhen()
  // Mutex::WriterLockWhen()
  //
  // Blocks until simultaneously both `cond` is `true` and this `Mutex` can
  // be acquired, then atomically acquires this `Mutex`. `LockWhen()` is
  // logically equivalent to `*Lock(); Await();` though they may have different
  // performance characteristics.
  void LockWhen(const Condition& cond) ABSL_EXCLUSIVE_LOCK_FUNCTION() {
    LockWhenCommon(cond, synchronization_internal::KernelTimeout::Never(),
                   true);
  }

  void ReaderLockWhen(const Condition& cond) ABSL_SHARED_LOCK_FUNCTION() {
    LockWhenCommon(cond, synchronization_internal::KernelTimeout::Never(),
                   false);
  }

  void WriterLockWhen(const Condition& cond) ABSL_EXCLUSIVE_LOCK_FUNCTION() {
    this->LockWhen(cond);
  }

  // ---------------------------------------------------------------------------
  // Mutex Variants with Timeouts/Deadlines
  // ---------------------------------------------------------------------------

  // Mutex::AwaitWithTimeout()
  // Mutex::AwaitWithDeadline()
  //
  // Unlocks this `Mutex` and blocks until simultaneously:
  //   - either `cond` is true or the {timeout has expired, deadline has passed}
  //     and
  //   - this `Mutex` can be reacquired,
  // then reacquire this `Mutex` in the same mode in which it was previously
  // held, returning `true` iff `cond` is `true` on return.
  //
  // If the condition is initially `true`, the implementation *may* skip the
  // release/re-acquire step and return immediately.
  //
  // Deadlines in the past are equivalent to an immediate deadline.
  // Negative timeouts are equivalent to a zero timeout.
  //
  // This method requires that this thread holds this `Mutex` in some mode.
  bool AwaitWithTimeout(const Condition& cond, absl::Duration timeout) {
    return AwaitCommon(cond, synchronization_internal::KernelTimeout{timeout});
  }

  bool AwaitWithDeadline(const Condition& cond, absl::Time deadline) {
    return AwaitCommon(cond, synchronization_internal::KernelTimeout{deadline});
  }

  // Mutex::LockWhenWithTimeout()
  // Mutex::ReaderLockWhenWithTimeout()
  // Mutex::WriterLockWhenWithTimeout()
  //
  // Blocks until simultaneously both:
  //   - either `cond` is `true` or the timeout has expired, and
  //   - this `Mutex` can be acquired,
  // then atomically acquires this `Mutex`, returning `true` iff `cond` is
  // `true` on return.
  //
  // Negative timeouts are equivalent to a zero timeout.
  bool LockWhenWithTimeout(const Condition& cond, absl::Duration timeout)
      ABSL_EXCLUSIVE_LOCK_FUNCTION() {
    return LockWhenCommon(
        cond, synchronization_internal::KernelTimeout{timeout}, true);
  }
  bool ReaderLockWhenWithTimeout(const Condition& cond, absl::Duration timeout)
      ABSL_SHARED_LOCK_FUNCTION() {
    return LockWhenCommon(
        cond, synchronization_internal::KernelTimeout{timeout}, false);
  }
  bool WriterLockWhenWithTimeout(const Condition& cond, absl::Duration timeout)
      ABSL_EXCLUSIVE_LOCK_FUNCTION() {
    return this->LockWhenWithTimeout(cond, timeout);
  }

  // Mutex::LockWhenWithDeadline()
  // Mutex::ReaderLockWhenWithDeadline()
  // Mutex::WriterLockWhenWithDeadline()
  //
  // Blocks until simultaneously both:
  //   - either `cond` is `true` or the deadline has been passed, and
  //   - this `Mutex` can be acquired,
  // then atomically acquires this Mutex, returning `true` iff `cond` is `true`
  // on return.
  //
  // Deadlines in the past are equivalent to an immediate deadline.
  bool LockWhenWithDeadline(const Condition& cond, absl::Time deadline)
      ABSL_EXCLUSIVE_LOCK_FUNCTION() {
    return LockWhenCommon(
        cond, synchronization_internal::KernelTimeout{deadline}, true);
  }
  bool ReaderLockWhenWithDeadline(const Condition& cond, absl::Time deadline)
      ABSL_SHARED_LOCK_FUNCTION() {
    return LockWhenCommon(
        cond, synchronization_internal::KernelTimeout{deadline}, false);
  }
  bool WriterLockWhenWithDeadline(const Condition& cond, absl::Time deadline)
      ABSL_EXCLUSIVE_LOCK_FUNCTION() {
    return this->LockWhenWithDeadline(cond, deadline);
  }

  // ---------------------------------------------------------------------------
  // Debug Support: Invariant Checking, Deadlock Detection, Logging.
  // ---------------------------------------------------------------------------

  // Mutex::EnableInvariantDebugging()
  //
  // If `invariant`!=null and if invariant debugging has been enabled globally,
  // cause `(*invariant)(arg)` to be called at moments when the invariant for
  // this `Mutex` should hold (for example: just after acquire, just before
  // release).
  //
  // The routine `invariant` should have no side-effects since it is not
  // guaranteed how many times it will be called; it should check the invariant
  // and crash if it does not hold. Enabling global invariant debugging may
  // substantially reduce `Mutex` performance; it should be set only for
  // non-production runs.  Optimization options may also disable invariant
  // checks.
  void EnableInvariantDebugging(void (*invariant)(void*), void* arg);

  // Mutex::EnableDebugLog()
  //
  // Cause all subsequent uses of this `Mutex` to be logged via
  // `ABSL_RAW_LOG(INFO)`. Log entries are tagged with `name` if no previous
  // call to `EnableInvariantDebugging()` or `EnableDebugLog()` has been made.
  //
  // Note: This method substantially reduces `Mutex` performance.
  void EnableDebugLog(const char* name);

  // Deadlock detection

  // Mutex::ForgetDeadlockInfo()
  //
  // Forget any deadlock-detection information previously gathered
  // about this `Mutex`. Call this method in debug mode when the lock ordering
  // of a `Mutex` changes.
  void ForgetDeadlockInfo();

  // Mutex::AssertNotHeld()
  //
  // Return immediately if this thread does not hold this `Mutex` in any
  // mode; otherwise, may report an error (typically by crashing with a
  // diagnostic), or may return immediately.
  //
  // Currently this check is performed only if all of:
  //    - in debug mode
  //    - SetMutexDeadlockDetectionMode() has been set to kReport or kAbort
  //    - number of locks concurrently held by this thread is not large.
  // are true.
  void AssertNotHeld() const;

  // Special cases.

  // A `MuHow` is a constant that indicates how a lock should be acquired.
  // Internal implementation detail.  Clients should ignore.
  typedef const struct MuHowS* MuHow;

  // Mutex::InternalAttemptToUseMutexInFatalSignalHandler()
  //
  // Causes the `Mutex` implementation to prepare itself for re-entry caused by
  // future use of `Mutex` within a fatal signal handler. This method is
  // intended for use only for last-ditch attempts to log crash information.
  // It does not guarantee that attempts to use Mutexes within the handler will
  // not deadlock; it merely makes other faults less likely.
  //
  // WARNING:  This routine must be invoked from a signal handler, and the
  // signal handler must either loop forever or terminate the process.
  // Attempts to return from (or `longjmp` out of) the signal handler once this
  // call has been made may cause arbitrary program behaviour including
  // crashes and deadlocks.
  static void InternalAttemptToUseMutexInFatalSignalHandler();

 private:
  std::atomic<intptr_t> mu_;  // The Mutex state.

  // Post()/Wait() versus associated PerThreadSem; in class for required
  // friendship with PerThreadSem.
  static void IncrementSynchSem(Mutex* mu, base_internal::PerThreadSynch* w);
  static bool DecrementSynchSem(Mutex* mu, base_internal::PerThreadSynch* w,
                                synchronization_internal::KernelTimeout t);

  // slow path acquire
  void LockSlowLoop(SynchWaitParams* waitp, int flags);
  // wrappers around LockSlowLoop()
  bool LockSlowWithDeadline(MuHow how, const Condition* cond,
                            synchronization_internal::KernelTimeout t,
                            int flags);
  void LockSlow(MuHow how, const Condition* cond,
                int flags) ABSL_ATTRIBUTE_COLD;
  // slow path release
  void UnlockSlow(SynchWaitParams* waitp) ABSL_ATTRIBUTE_COLD;
  // TryLock slow path.
  bool TryLockSlow();
  // ReaderTryLock slow path.
  bool ReaderTryLockSlow();
  // Common code between Await() and AwaitWithTimeout/Deadline()
  bool AwaitCommon(const Condition& cond,
                   synchronization_internal::KernelTimeout t);
  bool LockWhenCommon(const Condition& cond,
                      synchronization_internal::KernelTimeout t, bool write);
  // Attempt to remove thread s from queue.
  void TryRemove(base_internal::PerThreadSynch* s);
  // Block a thread on mutex.
  void Block(base_internal::PerThreadSynch* s);
  // Wake a thread; return successor.
  base_internal::PerThreadSynch* Wakeup(base_internal::PerThreadSynch* w);
  void Dtor();

  friend class CondVar;   // for access to Trans()/Fer().
  void Trans(MuHow how);  // used for CondVar->Mutex transfer
  void Fer(
      base_internal::PerThreadSynch* w);  // used for CondVar->Mutex transfer

  // Catch the error of writing Mutex when intending MutexLock.
  explicit Mutex(const volatile Mutex* /*ignored*/) {}

  Mutex(const Mutex&) = delete;
  Mutex& operator=(const Mutex&) = delete;
};

// -----------------------------------------------------------------------------
// Mutex RAII Wrappers
// -----------------------------------------------------------------------------

// MutexLock
//
// `MutexLock` is a helper class, which acquires and releases a `Mutex` via
// RAII.
//
// Example:
//
// Class Foo {
//  public:
//   Foo::Bar* Baz() {
//     MutexLock lock(&mu_);
//     ...
//     return bar;
//   }
//
// private:
//   Mutex mu_;
// };
class ABSL_SCOPED_LOCKABLE MutexLock {
 public:
  // Constructors

  // Calls `mu->Lock()` and returns when that call returns. That is, `*mu` is
  // guaranteed to be locked when this object is constructed. Requires that
  // `mu` be dereferenceable.
  explicit MutexLock(Mutex* mu) ABSL_EXCLUSIVE_LOCK_FUNCTION(mu) : mu_(mu) {
    this->mu_->Lock();
  }

  // Like above, but calls `mu->LockWhen(cond)` instead. That is, in addition to
  // the above, the condition given by `cond` is also guaranteed to hold when
  // this object is constructed.
  explicit MutexLock(Mutex* mu, const Condition& cond)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : mu_(mu) {
    this->mu_->LockWhen(cond);
  }

  MutexLock(const MutexLock&) = delete;  // NOLINT(runtime/mutex)
  MutexLock(MutexLock&&) = delete;       // NOLINT(runtime/mutex)
  MutexLock& operator=(const MutexLock&) = delete;
  MutexLock& operator=(MutexLock&&) = delete;

  ~MutexLock() ABSL_UNLOCK_FUNCTION() { this->mu_->Unlock(); }

 private:
  Mutex* const mu_;
};

// ReaderMutexLock
//
// The `ReaderMutexLock` is a helper class, like `MutexLock`, which acquires and
// releases a shared lock on a `Mutex` via RAII.
class ABSL_SCOPED_LOCKABLE ReaderMutexLock {
 public:
  explicit ReaderMutexLock(Mutex* mu) ABSL_SHARED_LOCK_FUNCTION(mu) : mu_(mu) {
    mu->ReaderLock();
  }

  explicit ReaderMutexLock(Mutex* mu, const Condition& cond)
      ABSL_SHARED_LOCK_FUNCTION(mu)
      : mu_(mu) {
    mu->ReaderLockWhen(cond);
  }

  ReaderMutexLock(const ReaderMutexLock&) = delete;
  ReaderMutexLock(ReaderMutexLock&&) = delete;
  ReaderMutexLock& operator=(const ReaderMutexLock&) = delete;
  ReaderMutexLock& operator=(ReaderMutexLock&&) = delete;

  ~ReaderMutexLock() ABSL_UNLOCK_FUNCTION() { this->mu_->ReaderUnlock(); }

 private:
  Mutex* const mu_;
};

// WriterMutexLock
//
// The `WriterMutexLock` is a helper class, like `MutexLock`, which acquires and
// releases a write (exclusive) lock on a `Mutex` via RAII.
class ABSL_SCOPED_LOCKABLE WriterMutexLock {
 public:
  explicit WriterMutexLock(Mutex* mu) ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : mu_(mu) {
    mu->WriterLock();
  }

  explicit WriterMutexLock(Mutex* mu, const Condition& cond)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : mu_(mu) {
    mu->WriterLockWhen(cond);
  }

  WriterMutexLock(const WriterMutexLock&) = delete;
  WriterMutexLock(WriterMutexLock&&) = delete;
  WriterMutexLock& operator=(const WriterMutexLock&) = delete;
  WriterMutexLock& operator=(WriterMutexLock&&) = delete;

  ~WriterMutexLock() ABSL_UNLOCK_FUNCTION() { this->mu_->WriterUnlock(); }

 private:
  Mutex* const mu_;
};

// -----------------------------------------------------------------------------
// Condition
// -----------------------------------------------------------------------------
//
// `Mutex` contains a number of member functions which take a `Condition` as an
// argument; clients can wait for conditions to become `true` before attempting
// to acquire the mutex. These sections are known as "condition critical"
// sections. To use a `Condition`, you simply need to construct it, and use
// within an appropriate `Mutex` member function; everything else in the
// `Condition` class is an implementation detail.
//
// A `Condition` is specified as a function pointer which returns a boolean.
// `Condition` functions should be pure functions -- their results should depend
// only on passed arguments, should not consult any external state (such as
// clocks), and should have no side-effects, aside from debug logging. Any
// objects that the function may access should be limited to those which are
// constant while the mutex is blocked on the condition (e.g. a stack variable),
// or objects of state protected explicitly by the mutex.
//
// No matter which construction is used for `Condition`, the underlying
// function pointer / functor / callable must not throw any
// exceptions. Correctness of `Mutex` / `Condition` is not guaranteed in
// the face of a throwing `Condition`. (When Abseil is allowed to depend
// on C++17, these function pointers will be explicitly marked
// `noexcept`; until then this requirement cannot be enforced in the
// type system.)
//
// Note: to use a `Condition`, you need only construct it and pass it to a
// suitable `Mutex' member function, such as `Mutex::Await()`, or to the
// constructor of one of the scope guard classes.
//
// Example using LockWhen/Unlock:
//
//   // assume count_ is not internal reference count
//   int count_ ABSL_GUARDED_BY(mu_);
//   Condition count_is_zero(+[](int *count) { return *count == 0; }, &count_);
//
//   mu_.LockWhen(count_is_zero);
//   // ...
//   mu_.Unlock();
//
// Example using a scope guard:
//
//   {
//     MutexLock lock(&mu_, count_is_zero);
//     // ...
//   }
//
// When multiple threads are waiting on exactly the same condition, make sure
// that they are constructed with the same parameters (same pointer to function
// + arg, or same pointer to object + method), so that the mutex implementation
// can avoid redundantly evaluating the same condition for each thread.
class Condition {
 public:
  // A Condition that returns the result of "(*func)(arg)"
  Condition(bool (*func)(void*), void* arg);

  // Templated version for people who are averse to casts.
  //
  // To use a lambda, prepend it with unary plus, which converts the lambda
  // into a function pointer:
  //     Condition(+[](T* t) { return ...; }, arg).
  //
  // Note: lambdas in this case must contain no bound variables.
  //
  // See class comment for performance advice.
  template <typename T>
  Condition(bool (*func)(T*), T* arg);

  // Same as above, but allows for cases where `arg` comes from a pointer that
  // is convertible to the function parameter type `T*` but not an exact match.
  //
  // For example, the argument might be `X*` but the function takes `const X*`,
  // or the argument might be `Derived*` while the function takes `Base*`, and
  // so on for cases where the argument pointer can be implicitly converted.
  //
  // Implementation notes: This constructor overload is required in addition to
  // the one above to allow deduction of `T` from `arg` for cases such as where
  // a function template is passed as `func`. Also, the dummy `typename = void`
  // template parameter exists just to work around a MSVC mangling bug.
  template <typename T, typename = void>
  Condition(bool (*func)(T*),
            typename absl::internal::type_identity<T>::type* arg);

  // Templated version for invoking a method that returns a `bool`.
  //
  // `Condition(object, &Class::Method)` constructs a `Condition` that evaluates
  // `object->Method()`.
  //
  // Implementation Note: `absl::internal::type_identity` is used to allow
  // methods to come from base classes. A simpler signature like
  // `Condition(T*, bool (T::*)())` does not suffice.
  template <typename T>
  Condition(T* object,
            bool (absl::internal::type_identity<T>::type::*method)());

  // Same as above, for const members
  template <typename T>
  Condition(const T* object,
            bool (absl::internal::type_identity<T>::type::*method)() const);

  // A Condition that returns the value of `*cond`
  explicit Condition(const bool* cond);

  // Templated version for invoking a functor that returns a `bool`.
  // This approach accepts pointers to non-mutable lambdas, `std::function`,
  // the result of` std::bind` and user-defined functors that define
  // `bool F::operator()() const`.
  //
  // Example:
  //
  //   auto reached = [this, current]() {
  //     mu_.AssertReaderHeld();                // For annotalysis.
  //     return processed_ >= current;
  //   };
  //   mu_.Await(Condition(&reached));
  //
  // NOTE: never use "mu_.AssertHeld()" instead of "mu_.AssertReaderHeld()" in
  // the lambda as it may be called when the mutex is being unlocked from a
  // scope holding only a reader lock, which will make the assertion not
  // fulfilled and crash the binary.

  // See class comment for performance advice. In particular, if there
  // might be more than one waiter for the same condition, make sure
  // that all waiters construct the condition with the same pointers.

  // Implementation note: The second template parameter ensures that this
  // constructor doesn't participate in overload resolution if T doesn't have
  // `bool operator() const`.
  template <typename T, typename E = decltype(static_cast<bool (T::*)() const>(
                            &T::operator()))>
  explicit Condition(const T* obj)
      : Condition(obj, static_cast<bool (T::*)() const>(&T::operator())) {}

  // A Condition that always returns `true`.
  // kTrue is only useful in a narrow set of circumstances, mostly when
  // it's passed conditionally. For example:
  //
  //   mu.LockWhen(some_flag ? kTrue : SomeOtherCondition);
  //
  // Note: {LockWhen,Await}With{Deadline,Timeout} methods with kTrue condition
  // don't return immediately when the timeout happens, they still block until
  // the Mutex becomes available. The return value of these methods does
  // not indicate if the timeout was reached; rather it indicates whether or
  // not the condition is true.
  ABSL_CONST_INIT static const Condition kTrue;

  // Evaluates the condition.
  bool Eval() const;

  // Returns `true` if the two conditions are guaranteed to return the same
  // value if evaluated at the same time, `false` if the evaluation *may* return
  // different results.
  //
  // Two `Condition` values are guaranteed equal if both their `func` and `arg`
  // components are the same. A null pointer is equivalent to a `true`
  // condition.
  static bool GuaranteedEqual(const Condition* a, const Condition* b);

 private:
  // Sizing an allocation for a method pointer can be subtle. In the Itanium
  // specifications, a method pointer has a predictable, uniform size. On the
  // other hand, MSVC ABI, method pointer sizes vary based on the
  // inheritance of the class. Specifically, method pointers from classes with
  // multiple inheritance are bigger than those of classes with single
  // inheritance. Other variations also exist.

#ifndef _MSC_VER
  // Allocation for a function pointer or method pointer.
  // The {0} initializer ensures that all unused bytes of this buffer are
  // always zeroed out.  This is necessary, because GuaranteedEqual() compares
  // all of the bytes, unaware of which bytes are relevant to a given `eval_`.
  using MethodPtr = bool (Condition::*)();
  char callback_[sizeof(MethodPtr)] = {0};
#else
  // It is well known that the larget MSVC pointer-to-member is 24 bytes. This
  // may be the largest known pointer-to-member of any platform. For this
  // reason we will allocate 24 bytes for MSVC platform toolchains.
  char callback_[24] = {0};
#endif

  // Function with which to evaluate callbacks and/or arguments.
  bool (*eval_)(const Condition*) = nullptr;

  // Either an argument for a function call or an object for a method call.
  void* arg_ = nullptr;

  // Various functions eval_ can point to:
  static bool CallVoidPtrFunction(const Condition*);
  template <typename T>
  static bool CastAndCallFunction(const Condition* c);
  template <typename T, typename ConditionMethodPtr>
  static bool CastAndCallMethod(const Condition* c);

  // Helper methods for storing, validating, and reading callback arguments.
  template <typename T>
  inline void StoreCallback(T callback) {
    static_assert(
        sizeof(callback) <= sizeof(callback_),
        "An overlarge pointer was passed as a callback to Condition.");
    std::memcpy(callback_, &callback, sizeof(callback));
  }

  template <typename T>
  inline void ReadCallback(T* callback) const {
    std::memcpy(callback, callback_, sizeof(*callback));
  }

  static bool AlwaysTrue(const Condition*) { return true; }

  // Used only to create kTrue.
  constexpr Condition() : eval_(AlwaysTrue), arg_(nullptr) {}
};

// -----------------------------------------------------------------------------
// CondVar
// -----------------------------------------------------------------------------
//
// A condition variable, reflecting state evaluated separately outside of the
// `Mutex` object, which can be signaled to wake callers.
// This class is not normally needed; use `Mutex` member functions such as
// `Mutex::Await()` and intrinsic `Condition` abstractions. In rare cases
// with many threads and many conditions, `CondVar` may be faster.
//
// The implementation may deliver signals to any condition variable at
// any time, even when no call to `Signal()` or `SignalAll()` is made; as a
// result, upon being awoken, you must check the logical condition you have
// been waiting upon.
//
// Examples:
//
// Usage for a thread waiting for some condition C protected by mutex mu:
//       mu.Lock();
//       while (!C) { cv->Wait(&mu); }        // releases and reacquires mu
//       //  C holds; process data
//       mu.Unlock();
//
// Usage to wake T is:
//       mu.Lock();
//       // process data, possibly establishing C
//       if (C) { cv->Signal(); }
//       mu.Unlock();
//
// If C may be useful to more than one waiter, use `SignalAll()` instead of
// `Signal()`.
//
// With this implementation it is efficient to use `Signal()/SignalAll()` inside
// the locked region; this usage can make reasoning about your program easier.
//
class CondVar {
 public:
  // A `CondVar` allocated on the heap or on the stack can use the this
  // constructor.
  CondVar();

  // CondVar::Wait()
  //
  // Atomically releases a `Mutex` and blocks on this condition variable.
  // Waits until awakened by a call to `Signal()` or `SignalAll()` (or a
  // spurious wakeup), then reacquires the `Mutex` and returns.
  //
  // Requires and ensures that the current thread holds the `Mutex`.
  void Wait(Mutex* mu) {
    WaitCommon(mu, synchronization_internal::KernelTimeout::Never());
  }

  // CondVar::WaitWithTimeout()
  //
  // Atomically releases a `Mutex` and blocks on this condition variable.
  // Waits until awakened by a call to `Signal()` or `SignalAll()` (or a
  // spurious wakeup), or until the timeout has expired, then reacquires
  // the `Mutex` and returns.
  //
  // Returns true if the timeout has expired without this `CondVar`
  // being signalled in any manner. If both the timeout has expired
  // and this `CondVar` has been signalled, the implementation is free
  // to return `true` or `false`.
  //
  // Requires and ensures that the current thread holds the `Mutex`.
  bool WaitWithTimeout(Mutex* mu, absl::Duration timeout) {
    return WaitCommon(mu, synchronization_internal::KernelTimeout(timeout));
  }

  // CondVar::WaitWithDeadline()
  //
  // Atomically releases a `Mutex` and blocks on this condition variable.
  // Waits until awakened by a call to `Signal()` or `SignalAll()` (or a
  // spurious wakeup), or until the deadline has passed, then reacquires
  // the `Mutex` and returns.
  //
  // Deadlines in the past are equivalent to an immediate deadline.
  //
  // Returns true if the deadline has passed without this `CondVar`
  // being signalled in any manner. If both the deadline has passed
  // and this `CondVar` has been signalled, the implementation is free
  // to return `true` or `false`.
  //
  // Requires and ensures that the current thread holds the `Mutex`.
  bool WaitWithDeadline(Mutex* mu, absl::Time deadline) {
    return WaitCommon(mu, synchronization_internal::KernelTimeout(deadline));
  }

  // CondVar::Signal()
  //
  // Signal this `CondVar`; wake at least one waiter if one exists.
  void Signal();

  // CondVar::SignalAll()
  //
  // Signal this `CondVar`; wake all waiters.
  void SignalAll();

  // CondVar::EnableDebugLog()
  //
  // Causes all subsequent uses of this `CondVar` to be logged via
  // `ABSL_RAW_LOG(INFO)`. Log entries are tagged with `name` if `name != 0`.
  // Note: this method substantially reduces `CondVar` performance.
  void EnableDebugLog(const char* name);

 private:
  bool WaitCommon(Mutex* mutex, synchronization_internal::KernelTimeout t);
  void Remove(base_internal::PerThreadSynch* s);
  std::atomic<intptr_t> cv_;  // Condition variable state.
  CondVar(const CondVar&) = delete;
  CondVar& operator=(const CondVar&) = delete;
};

// Variants of MutexLock.
//
// If you find yourself using one of these, consider instead using
// Mutex::Unlock() and/or if-statements for clarity.

// MutexLockMaybe
//
// MutexLockMaybe is like MutexLock, but is a no-op when mu is null.
class ABSL_SCOPED_LOCKABLE MutexLockMaybe {
 public:
  explicit MutexLockMaybe(Mutex* mu) ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : mu_(mu) {
    if (this->mu_ != nullptr) {
      this->mu_->Lock();
    }
  }

  explicit MutexLockMaybe(Mutex* mu, const Condition& cond)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : mu_(mu) {
    if (this->mu_ != nullptr) {
      this->mu_->LockWhen(cond);
    }
  }

  ~MutexLockMaybe() ABSL_UNLOCK_FUNCTION() {
    if (this->mu_ != nullptr) {
      this->mu_->Unlock();
    }
  }

 private:
  Mutex* const mu_;
  MutexLockMaybe(const MutexLockMaybe&) = delete;
  MutexLockMaybe(MutexLockMaybe&&) = delete;
  MutexLockMaybe& operator=(const MutexLockMaybe&) = delete;
  MutexLockMaybe& operator=(MutexLockMaybe&&) = delete;
};

// ReleasableMutexLock
//
// ReleasableMutexLock is like MutexLock, but permits `Release()` of its
// mutex before destruction. `Release()` may be called at most once.
class ABSL_SCOPED_LOCKABLE ReleasableMutexLock {
 public:
  explicit ReleasableMutexLock(Mutex* mu) ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : mu_(mu) {
    this->mu_->Lock();
  }

  explicit ReleasableMutexLock(Mutex* mu, const Condition& cond)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : mu_(mu) {
    this->mu_->LockWhen(cond);
  }

  ~ReleasableMutexLock() ABSL_UNLOCK_FUNCTION() {
    if (this->mu_ != nullptr) {
      this->mu_->Unlock();
    }
  }

  void Release() ABSL_UNLOCK_FUNCTION();

 private:
  Mutex* mu_;
  ReleasableMutexLock(const ReleasableMutexLock&) = delete;
  ReleasableMutexLock(ReleasableMutexLock&&) = delete;
  ReleasableMutexLock& operator=(const ReleasableMutexLock&) = delete;
  ReleasableMutexLock& operator=(ReleasableMutexLock&&) = delete;
};

inline Mutex::Mutex() : mu_(0) {
  ABSL_TSAN_MUTEX_CREATE(this, __tsan_mutex_not_static);
}

inline constexpr Mutex::Mutex(absl::ConstInitType) : mu_(0) {}

#if !defined(__APPLE__) && !defined(ABSL_BUILD_DLL)
ABSL_ATTRIBUTE_ALWAYS_INLINE
inline Mutex::~Mutex() { Dtor(); }
#endif

#if defined(NDEBUG) && !defined(ABSL_HAVE_THREAD_SANITIZER)
// Use default (empty) destructor in release build for performance reasons.
// We need to mark both Dtor and ~Mutex as always inline for inconsistent
// builds that use both NDEBUG and !NDEBUG with dynamic libraries. In these
// cases we want the empty functions to dissolve entirely rather than being
// exported from dynamic libraries and potentially override the non-empty ones.
ABSL_ATTRIBUTE_ALWAYS_INLINE
inline void Mutex::Dtor() {}
#endif

inline CondVar::CondVar() : cv_(0) {}

// static
template <typename T, typename ConditionMethodPtr>
bool Condition::CastAndCallMethod(const Condition* c) {
  T* object = static_cast<T*>(c->arg_);
  ConditionMethodPtr condition_method_pointer;
  c->ReadCallback(&condition_method_pointer);
  return (object->*condition_method_pointer)();
}

// static
template <typename T>
bool Condition::CastAndCallFunction(const Condition* c) {
  bool (*function)(T*);
  c->ReadCallback(&function);
  T* argument = static_cast<T*>(c->arg_);
  return (*function)(argument);
}

template <typename T>
inline Condition::Condition(bool (*func)(T*), T* arg)
    : eval_(&CastAndCallFunction<T>),
      arg_(const_cast<void*>(static_cast<const void*>(arg))) {
  static_assert(sizeof(&func) <= sizeof(callback_),
                "An overlarge function pointer was passed to Condition.");
  StoreCallback(func);
}

template <typename T, typename>
inline Condition::Condition(
    bool (*func)(T*), typename absl::internal::type_identity<T>::type* arg)
    // Just delegate to the overload above.
    : Condition(func, arg) {}

template <typename T>
inline Condition::Condition(
    T* object, bool (absl::internal::type_identity<T>::type::*method)())
    : eval_(&CastAndCallMethod<T, decltype(method)>), arg_(object) {
  static_assert(sizeof(&method) <= sizeof(callback_),
                "An overlarge method pointer was passed to Condition.");
  StoreCallback(method);
}

template <typename T>
inline Condition::Condition(
    const T* object,
    bool (absl::internal::type_identity<T>::type::*method)() const)
    : eval_(&CastAndCallMethod<const T, decltype(method)>),
      arg_(reinterpret_cast<void*>(const_cast<T*>(object))) {
  StoreCallback(method);
}

// Register hooks for profiling support.
//
// The function pointer registered here will be called whenever a mutex is
// contended.  The callback is given the cycles for which waiting happened (as
// measured by //absl/base/internal/cycleclock.h, and which may not
// be real "cycle" counts.)
//
// There is no ordering guarantee between when the hook is registered and when
// callbacks will begin.  Only a single profiler can be installed in a running
// binary; if this function is called a second time with a different function
// pointer, the value is ignored (and will cause an assertion failure in debug
// mode.)
void RegisterMutexProfiler(void (*fn)(int64_t wait_cycles));

// Register a hook for Mutex tracing.
//
// The function pointer registered here will be called whenever a mutex is
// contended.  The callback is given an opaque handle to the contended mutex,
// an event name, and the number of wait cycles (as measured by
// //absl/base/internal/cycleclock.h, and which may not be real
// "cycle" counts.)
//
// The only event name currently sent is "slow release".
//
// This has the same ordering and single-use limitations as
// RegisterMutexProfiler() above.
void RegisterMutexTracer(void (*fn)(const char* msg, const void* obj,
                                    int64_t wait_cycles));

// Register a hook for CondVar tracing.
//
// The function pointer registered here will be called here on various CondVar
// events.  The callback is given an opaque handle to the CondVar object and
// a string identifying the event.  This is thread-safe, but only a single
// tracer can be registered.
//
// Events that can be sent are "Wait", "Unwait", "Signal wakeup", and
// "SignalAll wakeup".
//
// This has the same ordering and single-use limitations as
// RegisterMutexProfiler() above.
void RegisterCondVarTracer(void (*fn)(const char* msg, const void* cv));

// EnableMutexInvariantDebugging()
//
// Enable or disable global support for Mutex invariant debugging.  If enabled,
// then invariant predicates can be registered per-Mutex for debug checking.
// See Mutex::EnableInvariantDebugging().
void EnableMutexInvariantDebugging(bool enabled);

// When in debug mode, and when the feature has been enabled globally, the
// implementation will keep track of lock ordering and complain (or optionally
// crash) if a cycle is detected in the acquired-before graph.

// Possible modes of operation for the deadlock detector in debug mode.
enum class OnDeadlockCycle {
  kIgnore,  // Neither report on nor attempt to track cycles in lock ordering
  kReport,  // Report lock cycles to stderr when detected
  kAbort,   // Report lock cycles to stderr when detected, then abort
};

// SetMutexDeadlockDetectionMode()
//
// Enable or disable global support for detection of potential deadlocks
// due to Mutex lock ordering inversions.  When set to 'kIgnore', tracking of
// lock ordering is disabled.  Otherwise, in debug builds, a lock ordering graph
// will be maintained internally, and detected cycles will be reported in
// the manner chosen here.
void SetMutexDeadlockDetectionMode(OnDeadlockCycle mode);

ABSL_NAMESPACE_END
}  // namespace absl

// In some build configurations we pass --detect-odr-violations to the
// gold linker.  This causes it to flag weak symbol overrides as ODR
// violations.  Because ODR only applies to C++ and not C,
// --detect-odr-violations ignores symbols not mangled with C++ names.
// By changing our extension points to be extern "C", we dodge this
// check.
extern "C" {
void ABSL_INTERNAL_C_SYMBOL(AbslInternalMutexYield)();
}  // extern "C"

#endif  // ABSL_SYNCHRONIZATION_MUTEX_H_
