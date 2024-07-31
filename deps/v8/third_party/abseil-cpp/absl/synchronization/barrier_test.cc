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

#include "absl/synchronization/barrier.h"

#include <thread>  // NOLINT(build/c++11)
#include <vector>

#include "gtest/gtest.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"


TEST(Barrier, SanityTest) {
  constexpr int kNumThreads = 10;
  absl::Barrier* barrier = new absl::Barrier(kNumThreads);

  absl::Mutex mutex;
  int counter = 0;  // Guarded by mutex.

  auto thread_func = [&] {
    if (barrier->Block()) {
      // This thread is the last thread to reach the barrier so it is
      // responsible for deleting it.
      delete barrier;
    }

    // Increment the counter.
    absl::MutexLock lock(&mutex);
    ++counter;
  };

  // Start (kNumThreads - 1) threads running thread_func.
  std::vector<std::thread> threads;
  for (int i = 0; i < kNumThreads - 1; ++i) {
    threads.push_back(std::thread(thread_func));
  }

  // Give (kNumThreads - 1) threads a chance to reach the barrier.
  // This test assumes at least one thread will have run after the
  // sleep has elapsed. Sleeping in a test is usually bad form, but we
  // need to make sure that we are testing the barrier instead of some
  // other synchronization method.
  absl::SleepFor(absl::Seconds(1));

  // The counter should still be zero since no thread should have
  // been able to pass the barrier yet.
  {
    absl::MutexLock lock(&mutex);
    EXPECT_EQ(counter, 0);
  }

  // Start 1 more thread. This should make all threads pass the barrier.
  threads.push_back(std::thread(thread_func));

  // All threads should now be able to proceed and finish.
  for (auto& thread : threads) {
    thread.join();
  }

  // All threads should now have incremented the counter.
  absl::MutexLock lock(&mutex);
  EXPECT_EQ(counter, kNumThreads);
}
