// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/platform/platform.h"
#include "src/base/platform/time.h"
#include "src/heap/parked-scope-inl.h"
#include "src/objects/js-atomics-synchronization-inl.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

#if V8_CAN_CREATE_SHARED_HEAP_BOOL

namespace v8 {
namespace internal {

using JSAtomicsMutexTest = TestJSSharedMemoryWithNativeContext;
using JSAtomicsConditionTest = TestJSSharedMemoryWithNativeContext;

namespace {

class LockingThread : public ParkingThread {
 public:
  LockingThread(Handle<JSAtomicsMutex> mutex,
                base::Optional<base::TimeDelta> timeout,
                ParkingSemaphore* sema_ready,
                ParkingSemaphore* sema_execute_start,
                ParkingSemaphore* sema_execute_complete)
      : ParkingThread(Options("LockingThread")),
        mutex_(mutex),
        timeout_(timeout),
        sema_ready_(sema_ready),
        sema_execute_start_(sema_execute_start),
        sema_execute_complete_(sema_execute_complete) {}

  void Run() override {
    IsolateWithContextWrapper isolate_wrapper;
    Isolate* isolate = isolate_wrapper.isolate();
    bool locked = LockJSMutexAndSignal(isolate);
    base::OS::Sleep(base::TimeDelta::FromMilliseconds(1));
    if (locked) {
      mutex_->Unlock(isolate);
    } else {
      EXPECT_TRUE(timeout_.has_value());
    }
    sema_execute_complete_->Signal();
  }

 protected:
  bool LockJSMutexAndSignal(Isolate* isolate) {
    sema_ready_->Signal();
    sema_execute_start_->ParkedWait(isolate->main_thread_local_isolate());

    HandleScope scope(isolate);
    bool locked = JSAtomicsMutex::Lock(isolate, mutex_, timeout_);
    if (locked) {
      EXPECT_TRUE(mutex_->IsHeld());
      EXPECT_TRUE(mutex_->IsCurrentThreadOwner());
    } else {
      EXPECT_FALSE(mutex_->IsCurrentThreadOwner());
    }
    return locked;
  }

  Handle<JSAtomicsMutex> mutex_;
  base::Optional<base::TimeDelta> timeout_;
  ParkingSemaphore* sema_ready_;
  ParkingSemaphore* sema_execute_start_;
  ParkingSemaphore* sema_execute_complete_;
};

class BlockingLockingThread final : public LockingThread {
 public:
  BlockingLockingThread(Handle<JSAtomicsMutex> mutex,
                        base::Optional<base::TimeDelta> timeout,
                        ParkingSemaphore* sema_ready,
                        ParkingSemaphore* sema_execute_start,
                        ParkingSemaphore* sema_execute_complete)
      : LockingThread(mutex, timeout, sema_ready, sema_execute_start,
                      sema_execute_complete) {}

  void Run() override {
    IsolateWithContextWrapper isolate_wrapper;
    Isolate* isolate = isolate_wrapper.isolate();
    EXPECT_TRUE(LockJSMutexAndSignal(isolate));
    {
      // Hold the js lock until the main thread notifies us.
      base::MutexGuard guard(&mutex_for_cv_);
      sema_execute_complete_->Signal();
      should_wait_ = true;
      while (should_wait_) {
        cv_.ParkedWait(isolate->main_thread_local_isolate(), &mutex_for_cv_);
      }
    }
    mutex_->Unlock(isolate);
    sema_execute_complete_->Signal();
  }

  void NotifyCV() {
    base::MutexGuard guard(&mutex_for_cv_);
    should_wait_ = false;
    cv_.NotifyOne();
  }

 private:
  base::Mutex mutex_for_cv_;
  ParkingConditionVariable cv_;
  bool should_wait_;
};

}  // namespace

TEST_F(JSAtomicsMutexTest, Contention) {
  constexpr int kThreads = 32;

  Isolate* i_main_isolate = i_isolate();
  Handle<JSAtomicsMutex> contended_mutex =
      i_main_isolate->factory()->NewJSAtomicsMutex();
  ParkingSemaphore sema_ready(0);
  ParkingSemaphore sema_execute_start(0);
  ParkingSemaphore sema_execute_complete(0);
  std::vector<std::unique_ptr<LockingThread>> threads;
  for (int i = 0; i < kThreads; i++) {
    auto thread = std::make_unique<LockingThread>(
        contended_mutex, base::nullopt, &sema_ready, &sema_execute_start,
        &sema_execute_complete);
    CHECK(thread->Start());
    threads.push_back(std::move(thread));
  }

  LocalIsolate* local_isolate = i_main_isolate->main_thread_local_isolate();
  for (int i = 0; i < kThreads; i++) {
    sema_ready.ParkedWait(local_isolate);
  }
  for (int i = 0; i < kThreads; i++) sema_execute_start.Signal();
  for (int i = 0; i < kThreads; i++) {
    sema_execute_complete.ParkedWait(local_isolate);
  }

  ParkingThread::ParkedJoinAll(local_isolate, threads);

  EXPECT_FALSE(contended_mutex->IsHeld());
}

TEST_F(JSAtomicsMutexTest, Timeout) {
  constexpr int kThreads = 32;

  Isolate* i_main_isolate = i_isolate();
  Handle<JSAtomicsMutex> contended_mutex =
      i_main_isolate->factory()->NewJSAtomicsMutex();
  ParkingSemaphore sema_ready(0);
  ParkingSemaphore sema_execute_start(0);
  ParkingSemaphore sema_execute_complete(0);
  std::unique_ptr<BlockingLockingThread> blocking_thread =
      std::make_unique<BlockingLockingThread>(contended_mutex, base::nullopt,
                                              &sema_ready, &sema_execute_start,
                                              &sema_execute_complete);

  LocalIsolate* local_isolate = i_main_isolate->main_thread_local_isolate();
  CHECK(blocking_thread->Start());
  sema_ready.ParkedWait(local_isolate);
  sema_execute_start.Signal();
  sema_execute_complete.ParkedWait(local_isolate);

  std::vector<std::unique_ptr<LockingThread>> threads;
  for (int i = 1; i < kThreads; i++) {
    auto thread = std::make_unique<LockingThread>(
        contended_mutex, base::TimeDelta::FromMilliseconds(1), &sema_ready,
        &sema_execute_start, &sema_execute_complete);
    CHECK(thread->Start());
    threads.push_back(std::move(thread));
  }

  for (int i = 1; i < kThreads; i++) {
    sema_ready.ParkedWait(local_isolate);
  }
  for (int i = 1; i < kThreads; i++) sema_execute_start.Signal();
  for (int i = 1; i < kThreads; i++) {
    sema_execute_complete.ParkedWait(local_isolate);
  }

  ParkingThread::ParkedJoinAll(local_isolate, threads);
  EXPECT_TRUE(contended_mutex->IsHeld());
  blocking_thread->NotifyCV();
  sema_execute_complete.ParkedWait(local_isolate);
  EXPECT_FALSE(contended_mutex->IsHeld());
  blocking_thread->ParkedJoin(local_isolate);
}

namespace {
class WaitOnConditionThread final : public ParkingThread {
 public:
  WaitOnConditionThread(Handle<JSAtomicsMutex> mutex,
                        Handle<JSAtomicsCondition> condition,
                        uint32_t* waiting_threads_count,
                        ParkingSemaphore* sema_ready,
                        ParkingSemaphore* sema_execute_complete)
      : ParkingThread(Options("WaitOnConditionThread")),
        mutex_(mutex),
        condition_(condition),
        waiting_threads_count_(waiting_threads_count),
        sema_ready_(sema_ready),
        sema_execute_complete_(sema_execute_complete) {}

  void Run() override {
    IsolateWithContextWrapper isolate_wrapper;
    Isolate* isolate = isolate_wrapper.isolate();

    sema_ready_->Signal();

    HandleScope scope(isolate);
    JSAtomicsMutex::Lock(isolate, mutex_);
    while (keep_waiting) {
      (*waiting_threads_count_)++;
      EXPECT_TRUE(JSAtomicsCondition::WaitFor(isolate, condition_, mutex_,
                                              base::nullopt));
      (*waiting_threads_count_)--;
    }
    mutex_->Unlock(isolate);

    sema_execute_complete_->Signal();
  }

  bool keep_waiting = true;

 private:
  Handle<JSAtomicsMutex> mutex_;
  Handle<JSAtomicsCondition> condition_;
  uint32_t* waiting_threads_count_;
  ParkingSemaphore* sema_ready_;
  ParkingSemaphore* sema_execute_complete_;
};
}  // namespace

TEST_F(JSAtomicsConditionTest, NotifyAll) {
  constexpr uint32_t kThreads = 32;

  Isolate* i_main_isolate = i_isolate();
  Handle<JSAtomicsMutex> mutex = i_main_isolate->factory()->NewJSAtomicsMutex();
  Handle<JSAtomicsCondition> condition =
      i_main_isolate->factory()->NewJSAtomicsCondition();

  uint32_t waiting_threads_count = 0;
  ParkingSemaphore sema_ready(0);
  ParkingSemaphore sema_execute_complete(0);
  std::vector<std::unique_ptr<WaitOnConditionThread>> threads;
  for (uint32_t i = 0; i < kThreads; i++) {
    auto thread = std::make_unique<WaitOnConditionThread>(
        mutex, condition, &waiting_threads_count, &sema_ready,
        &sema_execute_complete);
    CHECK(thread->Start());
    threads.push_back(std::move(thread));
  }

  LocalIsolate* local_isolate = i_main_isolate->main_thread_local_isolate();
  for (uint32_t i = 0; i < kThreads; i++) {
    sema_ready.ParkedWait(local_isolate);
  }

  // Wait until all threads are waiting on the condition.
  for (;;) {
    JSAtomicsMutex::LockGuard lock_guard(i_main_isolate, mutex);
    uint32_t count = waiting_threads_count;
    if (count == kThreads) break;
  }

  // Wake all the threads up.
  for (uint32_t i = 0; i < kThreads; i++) {
    threads[i]->keep_waiting = false;
  }
  EXPECT_EQ(kThreads,
            JSAtomicsCondition::Notify(i_main_isolate, condition,
                                       JSAtomicsCondition::kAllWaiters));

  for (uint32_t i = 0; i < kThreads; i++) {
    sema_execute_complete.ParkedWait(local_isolate);
  }

  ParkingThread::ParkedJoinAll(local_isolate, threads);

  EXPECT_EQ(0U, waiting_threads_count);
  EXPECT_FALSE(mutex->IsHeld());
}

}  // namespace internal
}  // namespace v8

#endif  // V8_CAN_CREATE_SHARED_HEAP
