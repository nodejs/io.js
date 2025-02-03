// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_PARKED_SCOPE_H_
#define V8_HEAP_PARKED_SCOPE_H_

#include <optional>

#include "src/base/platform/condition-variable.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/semaphore.h"
#include "src/execution/local-isolate.h"
#include "src/heap/local-heap.h"

namespace v8 {
namespace internal {

// Scope that explicitly parks a thread, prohibiting access to the heap and the
// creation of handles. Do not use this directly! Use the family of
// ExecuteWhileParked methods, instead.
class V8_NODISCARD ParkedScope {
 private:
  explicit ParkedScope(LocalIsolate* local_isolate)
      : ParkedScope(local_isolate->heap()) {}
  explicit ParkedScope(LocalHeap* local_heap) : local_heap_(local_heap) {
    ++local_heap_->nested_parked_scopes_;
    local_heap_->Park();
  }

  ~ParkedScope() {
    DCHECK_LT(0, local_heap_->nested_parked_scopes_);
    --local_heap_->nested_parked_scopes_;
    local_heap_->Unpark();
  }

  LocalHeap* const local_heap_;

  friend class LocalHeap;
};

// Scope that explicitly unparks a thread, allowing access to the heap and the
// creation of handles.
class V8_NODISCARD UnparkedScope {
 public:
  explicit UnparkedScope(LocalIsolate* local_isolate)
      : UnparkedScope(local_isolate->heap()) {}
  explicit UnparkedScope(LocalHeap* local_heap) : local_heap_(local_heap) {
    local_heap_->Unpark();
  }

  ~UnparkedScope() { local_heap_->Park(); }

 private:
  LocalHeap* const local_heap_;
};

// Scope that explicitly unparks a background thread, allowing access to the
// heap and the creation of handles. It has no effect on the main thread.
class V8_NODISCARD UnparkedScopeIfOnBackground {
 public:
  explicit UnparkedScopeIfOnBackground(LocalIsolate* local_isolate)
      : UnparkedScopeIfOnBackground(local_isolate->heap()) {}
  explicit UnparkedScopeIfOnBackground(LocalHeap* local_heap) {
    if (!local_heap->is_main_thread()) scope_.emplace(local_heap);
  }

 private:
  std::optional<UnparkedScope> scope_;
};

// Scope that automatically parks the thread while blocking on the given
// base::Mutex.
class V8_NODISCARD ParkedMutexGuard {
 public:
  explicit V8_INLINE ParkedMutexGuard(LocalIsolate* local_isolate,
                                      base::Mutex* mutex);
  explicit V8_INLINE ParkedMutexGuard(LocalHeap* local_heap,
                                      base::Mutex* mutex);

  ParkedMutexGuard(const ParkedMutexGuard&) = delete;
  ParkedMutexGuard& operator=(const ParkedMutexGuard&) = delete;

  ~ParkedMutexGuard() { mutex_->Unlock(); }

 private:
  base::Mutex* mutex_;
};

// Scope that automatically parks the thread while blocking on the given
// base::RecursiveMutex.
class V8_NODISCARD ParkedRecursiveMutexGuard {
 public:
  V8_INLINE ParkedRecursiveMutexGuard(LocalIsolate* local_isolate,
                                      base::RecursiveMutex* mutex);
  V8_INLINE ParkedRecursiveMutexGuard(LocalHeap* local_heap,
                                      base::RecursiveMutex* mutex);
  ParkedRecursiveMutexGuard(const ParkedRecursiveMutexGuard&) = delete;
  ParkedRecursiveMutexGuard& operator=(const ParkedRecursiveMutexGuard&) =
      delete;

  ~ParkedRecursiveMutexGuard() { mutex_->Unlock(); }

 private:
  base::RecursiveMutex* mutex_;
};

template <base::MutexSharedType kIsShared,
          base::NullBehavior Behavior = base::NullBehavior::kRequireNotNull>
class V8_NODISCARD ParkedSharedMutexGuardIf final {
 public:
  ParkedSharedMutexGuardIf(LocalIsolate* local_isolate,
                           base::SharedMutex* mutex, bool enable_mutex)
      : ParkedSharedMutexGuardIf(local_isolate->heap(), mutex, enable_mutex) {}
  V8_INLINE ParkedSharedMutexGuardIf(LocalHeap* local_heap,
                                     base::SharedMutex* mutex,
                                     bool enable_mutex);
  ParkedSharedMutexGuardIf(const ParkedSharedMutexGuardIf&) = delete;
  ParkedSharedMutexGuardIf& operator=(const ParkedSharedMutexGuardIf&) = delete;

  ~ParkedSharedMutexGuardIf() {
    if (!mutex_) return;

    if (kIsShared) {
      mutex_->UnlockShared();
    } else {
      mutex_->UnlockExclusive();
    }
  }

 private:
  base::SharedMutex* mutex_ = nullptr;
};

// A subclass of base::ConditionVariable that automatically parks the thread
// while waiting.
class V8_NODISCARD ParkingConditionVariable final
    : public base::ConditionVariable {
 public:
  ParkingConditionVariable() = default;
  ParkingConditionVariable(const ParkingConditionVariable&) = delete;
  ParkingConditionVariable& operator=(const ParkingConditionVariable&) = delete;

  V8_INLINE void ParkedWait(LocalIsolate* local_isolate, base::Mutex* mutex);
  V8_INLINE void ParkedWait(LocalHeap* local_heap, base::Mutex* mutex);

  void ParkedWait(const ParkedScope& scope, base::Mutex* mutex) {
    USE(scope);
    Wait(mutex);
  }

  V8_INLINE bool ParkedWaitFor(LocalIsolate* local_isolate, base::Mutex* mutex,
                               const base::TimeDelta& rel_time)
      V8_WARN_UNUSED_RESULT;
  V8_INLINE bool ParkedWaitFor(LocalHeap* local_heap, base::Mutex* mutex,
                               const base::TimeDelta& rel_time)
      V8_WARN_UNUSED_RESULT;

  bool ParkedWaitFor(const ParkedScope& scope, base::Mutex* mutex,
                     const base::TimeDelta& rel_time) V8_WARN_UNUSED_RESULT {
    USE(scope);
    return WaitFor(mutex, rel_time);
  }

 private:
  using base::ConditionVariable::Wait;
  using base::ConditionVariable::WaitFor;
};

// A subclass of base::Semaphore that automatically parks the thread while
// waiting.
class V8_NODISCARD ParkingSemaphore final : public base::Semaphore {
 public:
  explicit ParkingSemaphore(int count) : base::Semaphore(count) {}
  ParkingSemaphore(const ParkingSemaphore&) = delete;
  ParkingSemaphore& operator=(const ParkingSemaphore&) = delete;

  V8_INLINE void ParkedWait(LocalIsolate* local_isolate);
  V8_INLINE void ParkedWait(LocalHeap* local_heap);

  void ParkedWait(const ParkedScope& scope) {
    USE(scope);
    Wait();
  }

  V8_INLINE bool ParkedWaitFor(LocalIsolate* local_isolate,
                               const base::TimeDelta& rel_time)
      V8_WARN_UNUSED_RESULT;
  V8_INLINE bool ParkedWaitFor(LocalHeap* local_heap,
                               const base::TimeDelta& rel_time)
      V8_WARN_UNUSED_RESULT;

  bool ParkedWaitFor(const ParkedScope& scope,
                     const base::TimeDelta& rel_time) {
    USE(scope);
    return WaitFor(rel_time);
  }

 private:
  using base::Semaphore::Wait;
  using base::Semaphore::WaitFor;
};

class ParkingThread : public v8::base::Thread {
 public:
  explicit ParkingThread(const Options& options) : v8::base::Thread(options) {}

  V8_INLINE void ParkedJoin(LocalIsolate* local_isolate);
  V8_INLINE void ParkedJoin(LocalHeap* local_heap);

  void ParkedJoin(const ParkedScope& scope) {
    USE(scope);
    Join();
  }

  template <typename ThreadCollection>
  static V8_INLINE void ParkedJoinAll(LocalIsolate* local_isolate,
                                      const ThreadCollection& threads);
  template <typename ThreadCollection>
  static V8_INLINE void ParkedJoinAll(LocalHeap* local_heap,
                                      const ThreadCollection& threads);

  template <typename ThreadCollection>
  static void ParkedJoinAll(const ParkedScope& scope,
                            const ThreadCollection& threads) {
    USE(scope);
    for (auto& thread : threads) thread->Join();
  }

 private:
  using v8::base::Thread::Join;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_PARKED_SCOPE_H_
