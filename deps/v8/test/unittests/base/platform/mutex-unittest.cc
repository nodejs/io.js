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
#include "test/unittests/fuzztest.h"
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


}  // namespace base
}  // namespace v8
