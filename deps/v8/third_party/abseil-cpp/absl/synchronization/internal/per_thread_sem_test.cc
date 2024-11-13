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

#include "absl/synchronization/internal/per_thread_sem.h"

#include <atomic>
#include <condition_variable>  // NOLINT(build/c++11)
#include <functional>
#include <limits>
#include <mutex>               // NOLINT(build/c++11)
#include <string>
#include <thread>              // NOLINT(build/c++11)

#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/base/internal/cycleclock.h"
#include "absl/base/internal/thread_identity.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

// In this test we explicitly avoid the use of synchronization
// primitives which might use PerThreadSem, most notably absl::Mutex.

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace synchronization_internal {

class SimpleSemaphore {
 public:
  SimpleSemaphore() : count_(0) {}

  // Decrements (locks) the semaphore. If the semaphore's value is
  // greater than zero, then the decrement proceeds, and the function
  // returns, immediately. If the semaphore currently has the value
  // zero, then the call blocks until it becomes possible to perform
  // the decrement.
  void Wait() {
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait(lock, [this]() { return count_ > 0; });
    --count_;
    cv_.notify_one();
  }

  // Increments (unlocks) the semaphore. If the semaphore's value
  // consequently becomes greater than zero, then another thread
  // blocked Wait() call will be woken up and proceed to lock the
  // semaphore.
  void Post() {
    std::lock_guard<std::mutex> lock(mu_);
    ++count_;
    cv_.notify_one();
  }

 private:
  std::mutex mu_;
  std::condition_variable cv_;
  int count_;
};

struct ThreadData {
  int num_iterations;                 // Number of replies to send.
  SimpleSemaphore identity2_written;  // Posted by thread writing identity2.
  base_internal::ThreadIdentity *identity1;  // First Post()-er.
  base_internal::ThreadIdentity *identity2;  // First Wait()-er.
  KernelTimeout timeout;
};

// Need friendship with PerThreadSem.
class PerThreadSemTest : public testing::Test {
 public:
  static void TimingThread(ThreadData* t) {
    t->identity2 = GetOrCreateCurrentThreadIdentity();
    t->identity2_written.Post();
    while (t->num_iterations--) {
      Wait(t->timeout);
      Post(t->identity1);
    }
  }

  void TestTiming(const char *msg, bool timeout) {
    static const int kNumIterations = 100;
    ThreadData t;
    t.num_iterations = kNumIterations;
    t.timeout = timeout ?
        KernelTimeout(absl::Now() + absl::Seconds(10000))  // far in the future
        : KernelTimeout::Never();
    t.identity1 = GetOrCreateCurrentThreadIdentity();

    // We can't use the Thread class here because it uses the Mutex
    // class which will invoke PerThreadSem, so we use std::thread instead.
    std::thread partner_thread(std::bind(TimingThread, &t));

    // Wait for our partner thread to register their identity.
    t.identity2_written.Wait();

    int64_t min_cycles = std::numeric_limits<int64_t>::max();
    int64_t total_cycles = 0;
    for (int i = 0; i < kNumIterations; ++i) {
      absl::SleepFor(absl::Milliseconds(20));
      int64_t cycles = base_internal::CycleClock::Now();
      Post(t.identity2);
      Wait(t.timeout);
      cycles = base_internal::CycleClock::Now() - cycles;
      min_cycles = std::min(min_cycles, cycles);
      total_cycles += cycles;
    }
    std::string out = StrCat(
        msg, "min cycle count=", min_cycles, " avg cycle count=",
        absl::SixDigits(static_cast<double>(total_cycles) / kNumIterations));
    printf("%s\n", out.c_str());

    partner_thread.join();
  }

 protected:
  static void Post(base_internal::ThreadIdentity *id) {
    PerThreadSem::Post(id);
  }
  static bool Wait(KernelTimeout t) {
    return PerThreadSem::Wait(t);
  }

  // convenience overload
  static bool Wait(absl::Time t) {
    return Wait(KernelTimeout(t));
  }

  static void Tick(base_internal::ThreadIdentity *identity) {
    PerThreadSem::Tick(identity);
  }
};

namespace {

TEST_F(PerThreadSemTest, WithoutTimeout) {
  PerThreadSemTest::TestTiming("Without timeout: ", false);
}

TEST_F(PerThreadSemTest, WithTimeout) {
  PerThreadSemTest::TestTiming("With timeout:    ", true);
}

TEST_F(PerThreadSemTest, Timeouts) {
  const absl::Duration delay = absl::Milliseconds(50);
  const absl::Time start = absl::Now();
  EXPECT_FALSE(Wait(start + delay));
  const absl::Duration elapsed = absl::Now() - start;
  // Allow for a slight early return, to account for quality of implementation
  // issues on various platforms.
  absl::Duration slop = absl::Milliseconds(1);
#ifdef _MSC_VER
  // Use higher slop on MSVC due to flaky test failures.
  slop = absl::Milliseconds(16);
#endif
  EXPECT_LE(delay - slop, elapsed)
      << "Wait returned " << delay - elapsed
      << " early (with " << slop << " slop), start time was " << start;

  absl::Time negative_timeout = absl::UnixEpoch() - absl::Milliseconds(100);
  EXPECT_FALSE(Wait(negative_timeout));
  EXPECT_LE(negative_timeout, absl::Now() + slop);  // trivially true :)

  Post(GetOrCreateCurrentThreadIdentity());
  // The wait here has an expired timeout, but we have a wake to consume,
  // so this should succeed
  EXPECT_TRUE(Wait(negative_timeout));
}

TEST_F(PerThreadSemTest, ThreadIdentityReuse) {
  // Create a base_internal::ThreadIdentity object and keep reusing it. There
  // should be no memory or resource leaks.
  for (int i = 0; i < 10000; i++) {
    std::thread t([]() { GetOrCreateCurrentThreadIdentity(); });
    t.join();
  }
}

}  // namespace

}  // namespace synchronization_internal
ABSL_NAMESPACE_END
}  // namespace absl
