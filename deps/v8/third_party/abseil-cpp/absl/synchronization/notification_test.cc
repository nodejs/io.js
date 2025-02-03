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

#include "absl/synchronization/notification.h"

#include <thread>  // NOLINT(build/c++11)
#include <tuple>
#include <vector>

#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/tracing.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// A thread-safe class that holds a counter.
class ThreadSafeCounter {
 public:
  ThreadSafeCounter() : count_(0) {}

  void Increment() {
    MutexLock lock(&mutex_);
    ++count_;
  }

  int Get() const {
    MutexLock lock(&mutex_);
    return count_;
  }

  void WaitUntilGreaterOrEqual(int n) {
    MutexLock lock(&mutex_);
    auto cond = [this, n]() { return count_ >= n; };
    mutex_.Await(Condition(&cond));
  }

 private:
  mutable Mutex mutex_;
  int count_;
};

// Runs the |i|'th worker thread for the tests in BasicTests().  Increments the
// |ready_counter|, waits on the |notification|, and then increments the
// |done_counter|.
static void RunWorker(int i, ThreadSafeCounter* ready_counter,
                      Notification* notification,
                      ThreadSafeCounter* done_counter) {
  ready_counter->Increment();
  notification->WaitForNotification();
  done_counter->Increment();
}

// Tests that the |notification| properly blocks and awakens threads.  Assumes
// that the |notification| is not yet triggered.  If |notify_before_waiting| is
// true, the |notification| is triggered before any threads are created, so the
// threads never block in WaitForNotification().  Otherwise, the |notification|
// is triggered at a later point when most threads are likely to be blocking in
// WaitForNotification().
static void BasicTests(bool notify_before_waiting, Notification* notification) {
  EXPECT_FALSE(notification->HasBeenNotified());
  EXPECT_FALSE(
      notification->WaitForNotificationWithTimeout(absl::Milliseconds(0)));
  EXPECT_FALSE(notification->WaitForNotificationWithDeadline(absl::Now()));

  const absl::Duration delay = absl::Milliseconds(50);
  const absl::Time start = absl::Now();
  EXPECT_FALSE(notification->WaitForNotificationWithTimeout(delay));
  const absl::Duration elapsed = absl::Now() - start;

  // Allow for a slight early return, to account for quality of implementation
  // issues on various platforms.
  const absl::Duration slop = absl::Milliseconds(5);
  EXPECT_LE(delay - slop, elapsed)
      << "WaitForNotificationWithTimeout returned " << delay - elapsed
      << " early (with " << slop << " slop), start time was " << start;

  ThreadSafeCounter ready_counter;
  ThreadSafeCounter done_counter;

  if (notify_before_waiting) {
    notification->Notify();
  }

  // Create a bunch of threads that increment the |done_counter| after being
  // notified.
  const int kNumThreads = 10;
  std::vector<std::thread> workers;
  for (int i = 0; i < kNumThreads; ++i) {
    workers.push_back(std::thread(&RunWorker, i, &ready_counter, notification,
                                  &done_counter));
  }

  if (!notify_before_waiting) {
    ready_counter.WaitUntilGreaterOrEqual(kNumThreads);

    // Workers have not been notified yet, so the |done_counter| should be
    // unmodified.
    EXPECT_EQ(0, done_counter.Get());

    notification->Notify();
  }

  // After notifying and then joining the workers, both counters should be
  // fully incremented.
  notification->WaitForNotification();  // should exit immediately
  EXPECT_TRUE(notification->HasBeenNotified());
  EXPECT_TRUE(notification->WaitForNotificationWithTimeout(absl::Seconds(0)));
  EXPECT_TRUE(notification->WaitForNotificationWithDeadline(absl::Now()));
  for (std::thread& worker : workers) {
    worker.join();
  }
  EXPECT_EQ(kNumThreads, ready_counter.Get());
  EXPECT_EQ(kNumThreads, done_counter.Get());
}

TEST(NotificationTest, SanityTest) {
  Notification local_notification1, local_notification2;
  BasicTests(false, &local_notification1);
  BasicTests(true, &local_notification2);
}

#if ABSL_HAVE_ATTRIBUTE_WEAK

namespace base_internal {

namespace {

using TraceRecord = std::tuple<const void*, ObjectKind>;

thread_local TraceRecord tls_signal;
thread_local TraceRecord tls_wait;
thread_local TraceRecord tls_continue;
thread_local TraceRecord tls_observed;

}  // namespace

// Strong extern "C" implementation.
extern "C" {

void ABSL_INTERNAL_C_SYMBOL(AbslInternalTraceWait)(const void* object,
                                                   ObjectKind kind) {
  tls_wait = {object, kind};
}

void ABSL_INTERNAL_C_SYMBOL(AbslInternalTraceContinue)(const void* object,
                                                       ObjectKind kind) {
  tls_continue = {object, kind};
}

void ABSL_INTERNAL_C_SYMBOL(AbslInternalTraceSignal)(const void* object,
                                                     ObjectKind kind) {
  tls_signal = {object, kind};
}

void ABSL_INTERNAL_C_SYMBOL(AbslInternalTraceObserved)(const void* object,
                                                       ObjectKind kind) {
  tls_observed = {object, kind};
}

}  // extern "C"

TEST(NotificationTest, TracesNotify) {
  Notification n;
  tls_signal = {};
  n.Notify();
  EXPECT_EQ(tls_signal, TraceRecord(&n, ObjectKind::kNotification));
}

TEST(NotificationTest, TracesWaitForNotification) {
  Notification n;
  n.Notify();
  tls_wait = tls_continue = {};
  n.WaitForNotification();
  EXPECT_EQ(tls_wait, TraceRecord(&n, ObjectKind::kNotification));
  EXPECT_EQ(tls_continue, TraceRecord(&n, ObjectKind::kNotification));
}

TEST(NotificationTest, TracesWaitForNotificationWithTimeout) {
  Notification n;

  tls_wait = tls_continue = {};
  n.WaitForNotificationWithTimeout(absl::Milliseconds(1));
  EXPECT_EQ(tls_wait, TraceRecord(&n, ObjectKind::kNotification));
  EXPECT_EQ(tls_continue, TraceRecord(nullptr, ObjectKind::kNotification));

  n.Notify();
  tls_wait = tls_continue = {};
  n.WaitForNotificationWithTimeout(absl::Milliseconds(1));
  EXPECT_EQ(tls_wait, TraceRecord(&n, ObjectKind::kNotification));
  EXPECT_EQ(tls_continue, TraceRecord(&n, ObjectKind::kNotification));
}

TEST(NotificationTest, TracesHasBeenNotified) {
  Notification n;

  tls_observed = {};
  ASSERT_FALSE(n.HasBeenNotified());
  EXPECT_EQ(tls_observed, TraceRecord(nullptr, ObjectKind::kUnknown));

  n.Notify();
  tls_observed = {};
  ASSERT_TRUE(n.HasBeenNotified());
  EXPECT_EQ(tls_observed, TraceRecord(&n, ObjectKind::kNotification));
}

}  // namespace base_internal

#endif  // ABSL_HAVE_ATTRIBUTE_WEAK

ABSL_NAMESPACE_END
}  // namespace absl
