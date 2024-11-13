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

#include "absl/synchronization/blocking_counter.h"

#include <thread>  // NOLINT(build/c++11)
#include <tuple>
#include <vector>

#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/tracing.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace {

void PauseAndDecreaseCounter(BlockingCounter* counter, int* done) {
  absl::SleepFor(absl::Seconds(1));
  *done = 1;
  counter->DecrementCount();
}

TEST(BlockingCounterTest, BasicFunctionality) {
  // This test verifies that BlockingCounter functions correctly. Starts a
  // number of threads that just sleep for a second and decrement a counter.

  // Initialize the counter.
  const int num_workers = 10;
  BlockingCounter counter(num_workers);

  std::vector<std::thread> workers;
  std::vector<int> done(num_workers, 0);

  // Start a number of parallel tasks that will just wait for a seconds and
  // then decrement the count.
  workers.reserve(num_workers);
  for (int k = 0; k < num_workers; k++) {
    workers.emplace_back(
        [&counter, &done, k] { PauseAndDecreaseCounter(&counter, &done[k]); });
  }

  // Wait for the threads to have all finished.
  counter.Wait();

  // Check that all the workers have completed.
  for (int k = 0; k < num_workers; k++) {
    EXPECT_EQ(1, done[k]);
  }

  for (std::thread& w : workers) {
    w.join();
  }
}

TEST(BlockingCounterTest, WaitZeroInitialCount) {
  BlockingCounter counter(0);
  counter.Wait();
}

#if GTEST_HAS_DEATH_TEST
TEST(BlockingCounterTest, WaitNegativeInitialCount) {
  EXPECT_DEATH(BlockingCounter counter(-1),
               "BlockingCounter initial_count negative");
}
#endif

}  // namespace

#if ABSL_HAVE_ATTRIBUTE_WEAK

namespace base_internal {

namespace {

using TraceRecord = std::tuple<const void*, ObjectKind>;

thread_local TraceRecord tls_signal;
thread_local TraceRecord tls_wait;
thread_local TraceRecord tls_continue;

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

}  // extern "C"

TEST(BlockingCounterTest, TracesSignal) {
  BlockingCounter counter(2);

  tls_signal = {};
  counter.DecrementCount();
  EXPECT_EQ(tls_signal, TraceRecord(nullptr, ObjectKind::kUnknown));

  tls_signal = {};
  counter.DecrementCount();
  EXPECT_EQ(tls_signal, TraceRecord(&counter, ObjectKind::kBlockingCounter));
}

TEST(BlockingCounterTest, TracesWaitContinue) {
  BlockingCounter counter(1);
  counter.DecrementCount();

  tls_wait = {};
  tls_continue = {};
  counter.Wait();
  EXPECT_EQ(tls_wait, TraceRecord(&counter, ObjectKind::kBlockingCounter));
  EXPECT_EQ(tls_continue, TraceRecord(&counter, ObjectKind::kBlockingCounter));
}

}  // namespace base_internal

#endif  // ABSL_HAVE_ATTRIBUTE_WEAK

ABSL_NAMESPACE_END
}  // namespace absl
