// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/platform/mutex.h"

#include <chrono>  // NOLINT(build/c++11)
#include <queue>
#include <thread>  // NOLINT(build/c++11)

#include "src/base/platform/condition-variable.h"
#include "src/base/platform/platform.h"
#include "src/base/utils/random-number-generator.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace base {

TEST(Mutex, LockGuardMutex) {
  Mutex mutex;
  { MutexGuard lock_guard(&mutex); }
  { MutexGuard lock_guard(&mutex); }
}


TEST(Mutex, LockGuardRecursiveMutex) {
  RecursiveMutex recursive_mutex;
  { LockGuard<RecursiveMutex> lock_guard(&recursive_mutex); }
  {
    LockGuard<RecursiveMutex> lock_guard1(&recursive_mutex);
    LockGuard<RecursiveMutex> lock_guard2(&recursive_mutex);
  }
}


TEST(Mutex, LockGuardLazyMutex) {
  LazyMutex lazy_mutex = LAZY_MUTEX_INITIALIZER;
  { MutexGuard lock_guard(lazy_mutex.Pointer()); }
  { MutexGuard lock_guard(lazy_mutex.Pointer()); }
}


TEST(Mutex, LockGuardLazyRecursiveMutex) {
  LazyRecursiveMutex lazy_recursive_mutex = LAZY_RECURSIVE_MUTEX_INITIALIZER;
  { LockGuard<RecursiveMutex> lock_guard(lazy_recursive_mutex.Pointer()); }
  {
    LockGuard<RecursiveMutex> lock_guard1(lazy_recursive_mutex.Pointer());
    LockGuard<RecursiveMutex> lock_guard2(lazy_recursive_mutex.Pointer());
  }
}


TEST(Mutex, MultipleMutexes) {
  Mutex mutex1;
  Mutex mutex2;
  Mutex mutex3;
  // Order 1
  mutex1.Lock();
  mutex2.Lock();
  mutex3.Lock();
  mutex1.Unlock();
  mutex2.Unlock();
  mutex3.Unlock();
  // Order 2
  mutex1.Lock();
  mutex2.Lock();
  mutex3.Lock();
  mutex3.Unlock();
  mutex2.Unlock();
  mutex1.Unlock();
}


TEST(Mutex, MultipleRecursiveMutexes) {
  RecursiveMutex recursive_mutex1;
  RecursiveMutex recursive_mutex2;
  // Order 1
  recursive_mutex1.Lock();
  recursive_mutex2.Lock();
  EXPECT_TRUE(recursive_mutex1.TryLock());
  EXPECT_TRUE(recursive_mutex2.TryLock());
  recursive_mutex1.Unlock();
  recursive_mutex1.Unlock();
  recursive_mutex2.Unlock();
  recursive_mutex2.Unlock();
  // Order 2
  recursive_mutex1.Lock();
  EXPECT_TRUE(recursive_mutex1.TryLock());
  recursive_mutex2.Lock();
  EXPECT_TRUE(recursive_mutex2.TryLock());
  recursive_mutex2.Unlock();
  recursive_mutex1.Unlock();
  recursive_mutex2.Unlock();
  recursive_mutex1.Unlock();
}

TEST(Mutex, SharedMutexSimple) {
  SharedMutex mutex;
  mutex.LockShared();
  mutex.UnlockShared();
  mutex.LockExclusive();
  mutex.UnlockExclusive();
  mutex.LockShared();
  mutex.UnlockShared();
}

namespace {

void CheckCannotLockShared(SharedMutex& mutex) {
  std::thread([&]() { EXPECT_FALSE(mutex.TryLockShared()); }).join();
}

void CheckCannotLockExclusive(SharedMutex& mutex) {
  std::thread([&]() { EXPECT_FALSE(mutex.TryLockExclusive()); }).join();
}

class SharedMutexTestWorker : public Thread {
  // This class starts a thread that can lock/unlock shared/exclusive a
  // SharedMutex. The thread has a queue of actions that it needs to execute
  // (FIFO). Tasks can be added to the queue through the method `Do`.
  // After each lock/unlock, this class does a few checks about the state of the
  // SharedMutex (e.g., if we hold a shared lock on a mutex, no one can hold an
  // exclusive lock on this mutex).
 public:
  explicit SharedMutexTestWorker(SharedMutex& shared_mutex,
                                 std::atomic<int>& reader_count,
                                 std::atomic<int>& writer_count)
      : Thread(Options("SharedMutexTestWorker")),
        shared_mutex_(shared_mutex),
        reader_count_(reader_count),
        writer_count_(writer_count) {
    EXPECT_TRUE(Start());
  }

  enum class Action {
    kLockShared,
    kUnlockShared,
    kLockExclusive,
    kUnlockExclusive,
    kSleep,
    kEnd
  };

  void Do(Action what) {
    MutexGuard guard(&queue_mutex_);
    actions_.push(what);
    cv_.NotifyOne();
  }

  void End() {
    Do(Action::kEnd);
    Join();
  }

  static constexpr int kSleepTimeMs = 5;

  void Run() override {
    while (true) {
      queue_mutex_.Lock();
      while (actions_.empty()) {
        cv_.Wait(&queue_mutex_);
      }
      Action action = actions_.front();
      actions_.pop();
      // Unblock the queue before processing the action, in order to not block
      // the queue if the action is blocked.
      queue_mutex_.Unlock();
      switch (action) {
        case Action::kLockShared:
          shared_mutex_.LockShared();
          EXPECT_EQ(writer_count_, 0);
          CheckCannotLockExclusive(shared_mutex_);
          reader_count_++;
          break;
        case Action::kUnlockShared:
          reader_count_--;
          EXPECT_EQ(writer_count_, 0);
          CheckCannotLockExclusive(shared_mutex_);
          shared_mutex_.UnlockShared();
          break;
        case Action::kLockExclusive:
          shared_mutex_.LockExclusive();
          EXPECT_EQ(reader_count_, 0);
          EXPECT_EQ(writer_count_, 0);
          CheckCannotLockShared(shared_mutex_);
          CheckCannotLockExclusive(shared_mutex_);
          writer_count_++;
          break;
        case Action::kUnlockExclusive:
          writer_count_--;
          EXPECT_EQ(reader_count_, 0);
          EXPECT_EQ(writer_count_, 0);
          CheckCannotLockShared(shared_mutex_);
          CheckCannotLockExclusive(shared_mutex_);
          shared_mutex_.UnlockExclusive();
          break;
        case Action::kSleep:
          std::this_thread::sleep_for(std::chrono::milliseconds(kSleepTimeMs));
          break;
        case Action::kEnd:
          return;
      }
    }
  }

 private:
  // {actions_}, the queue of actions to execute, is shared between the thread
  // and the object. Holding {queue_mutex_} is required to access it. When the
  // queue is empty, the thread will Wait on {cv_}. Once `Do` adds an item to
  // the queue, it should NotifyOne on {cv_} to wake up the thread.
  Mutex queue_mutex_;
  ConditionVariable cv_;
  std::queue<Action> actions_;

  SharedMutex& shared_mutex_;

  // {reader_count} and {writer_count_} are used to verify the integrity of
  // {shared_mutex_}. For instance, if a thread acquires a shared lock, we
  // expect {writer_count_} to be 0.
  std::atomic<int>& reader_count_;
  std::atomic<int>& writer_count_;
};

}  // namespace

TEST(Mutex, SharedMutexThreads) {
  // A simple hand-written scenario involving 3 threads using the SharedMutex.
  SharedMutex mutex;
  std::atomic<int> reader_count = 0;
  std::atomic<int> writer_count = 0;

  SharedMutexTestWorker worker1(mutex, reader_count, writer_count);
  SharedMutexTestWorker worker2(mutex, reader_count, writer_count);
  SharedMutexTestWorker worker3(mutex, reader_count, writer_count);

  worker1.Do(SharedMutexTestWorker::Action::kLockShared);
  worker2.Do(SharedMutexTestWorker::Action::kLockShared);
  worker3.Do(SharedMutexTestWorker::Action::kLockExclusive);
  worker3.Do(SharedMutexTestWorker::Action::kSleep);
  worker1.Do(SharedMutexTestWorker::Action::kUnlockShared);
  worker1.Do(SharedMutexTestWorker::Action::kLockExclusive);
  worker2.Do(SharedMutexTestWorker::Action::kUnlockShared);
  worker2.Do(SharedMutexTestWorker::Action::kLockShared);
  worker2.Do(SharedMutexTestWorker::Action::kSleep);
  worker1.Do(SharedMutexTestWorker::Action::kUnlockExclusive);
  worker3.Do(SharedMutexTestWorker::Action::kUnlockExclusive);
  worker2.Do(SharedMutexTestWorker::Action::kUnlockShared);

  worker1.End();
  worker2.End();
  worker3.End();

  EXPECT_EQ(reader_count, 0);
  EXPECT_EQ(writer_count, 0);

  // Since the all of the worker threads are done, we should be able to take
  // both the shared and exclusive lock.
  EXPECT_TRUE(mutex.TryLockShared());
  mutex.UnlockShared();
  EXPECT_TRUE(mutex.TryLockExclusive());
  mutex.UnlockExclusive();
}

TEST(Mutex, SharedMutexThreadsFuzz) {
  // This test creates a lot of threads, each of which tries to take shared or
  // exclusive lock on a single SharedMutex.
  SharedMutex mutex;
  std::atomic<int> reader_count = 0;
  std::atomic<int> writer_count = 0;

  static constexpr int kThreadCount = 50;
  static constexpr int kActionPerWorker = 10;
  static constexpr int kReadToWriteRatio = 5;

  SharedMutexTestWorker* workers[kThreadCount];
  for (int i = 0; i < kThreadCount; i++) {
    workers[i] = new SharedMutexTestWorker(mutex, reader_count, writer_count);
  }

  base::RandomNumberGenerator rand_gen(GTEST_FLAG_GET(random_seed));
  for (int i = 0; i < kActionPerWorker; i++) {
    for (int j = 0; j < kThreadCount; j++) {
      if (rand_gen.NextInt() % kReadToWriteRatio == 0) {
        workers[j]->Do(SharedMutexTestWorker::Action::kLockExclusive);
        workers[j]->Do(SharedMutexTestWorker::Action::kSleep);
        workers[j]->Do(SharedMutexTestWorker::Action::kUnlockExclusive);
      } else {
        workers[j]->Do(SharedMutexTestWorker::Action::kLockShared);
        workers[j]->Do(SharedMutexTestWorker::Action::kSleep);
        workers[j]->Do(SharedMutexTestWorker::Action::kUnlockShared);
      }
    }
  }
  for (int i = 0; i < kThreadCount; i++) {
    workers[i]->End();
    delete workers[i];
  }

  EXPECT_EQ(reader_count, 0);
  EXPECT_EQ(writer_count, 0);

  // Since the all of the worker threads are done, we should be able to take
  // both the shared and exclusive lock.
  EXPECT_TRUE(mutex.TryLockShared());
  mutex.UnlockShared();
  EXPECT_TRUE(mutex.TryLockExclusive());
  mutex.UnlockExclusive();
}

}  // namespace base
}  // namespace v8
