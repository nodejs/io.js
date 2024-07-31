// Copyright 2023 The Abseil Authors.
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

#include "absl/synchronization/internal/sem_waiter.h"

#ifdef ABSL_INTERNAL_HAVE_SEM_WAITER

#include <semaphore.h>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cerrno>

#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/internal/thread_identity.h"
#include "absl/base/optimization.h"
#include "absl/synchronization/internal/kernel_timeout.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace synchronization_internal {

#ifdef ABSL_INTERNAL_NEED_REDUNDANT_CONSTEXPR_DECL
constexpr char SemWaiter::kName[];
#endif

SemWaiter::SemWaiter() : wakeups_(0) {
  if (sem_init(&sem_, 0, 0) != 0) {
    ABSL_RAW_LOG(FATAL, "sem_init failed with errno %d\n", errno);
  }
}

#if defined(__GLIBC__) && \
    (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 30))
#define ABSL_INTERNAL_HAVE_SEM_CLOCKWAIT 1
#elif defined(__ANDROID_API__) && __ANDROID_API__ >= 30
#define ABSL_INTERNAL_HAVE_SEM_CLOCKWAIT 1
#endif

// Calls sem_timedwait() or possibly something else like
// sem_clockwait() depending on the platform and
// KernelTimeout requested. The return value is the same as a call to the return
// value to a call to sem_timedwait().
int SemWaiter::TimedWait(KernelTimeout t) {
  if (KernelTimeout::SupportsSteadyClock() && t.is_relative_timeout()) {
#if defined(ABSL_INTERNAL_HAVE_SEM_CLOCKWAIT) && defined(CLOCK_MONOTONIC)
    const auto abs_clock_timeout = t.MakeClockAbsoluteTimespec(CLOCK_MONOTONIC);
    return sem_clockwait(&sem_, CLOCK_MONOTONIC, &abs_clock_timeout);
#endif
  }

  const auto abs_timeout = t.MakeAbsTimespec();
  return sem_timedwait(&sem_, &abs_timeout);
}

bool SemWaiter::Wait(KernelTimeout t) {
  // Loop until we timeout or consume a wakeup.
  // Note that, since the thread ticker is just reset, we don't need to check
  // whether the thread is idle on the very first pass of the loop.
  bool first_pass = true;
  while (true) {
    int x = wakeups_.load(std::memory_order_relaxed);
    while (x != 0) {
      if (!wakeups_.compare_exchange_weak(x, x - 1,
                                          std::memory_order_acquire,
                                          std::memory_order_relaxed)) {
        continue;  // Raced with someone, retry.
      }
      // Successfully consumed a wakeup, we're done.
      return true;
    }

    if (!first_pass) MaybeBecomeIdle();
    // Nothing to consume, wait (looping on EINTR).
    while (true) {
      if (!t.has_timeout()) {
        if (sem_wait(&sem_) == 0) break;
        if (errno == EINTR) continue;
        ABSL_RAW_LOG(FATAL, "sem_wait failed: %d", errno);
      } else {
        if (TimedWait(t) == 0) break;
        if (errno == EINTR) continue;
        if (errno == ETIMEDOUT) return false;
        ABSL_RAW_LOG(FATAL, "SemWaiter::TimedWait() failed: %d", errno);
      }
    }
    first_pass = false;
  }
}

void SemWaiter::Post() {
  // Post a wakeup.
  if (wakeups_.fetch_add(1, std::memory_order_release) == 0) {
    // We incremented from 0, need to wake a potential waiter.
    Poke();
  }
}

void SemWaiter::Poke() {
  if (sem_post(&sem_) != 0) {  // Wake any semaphore waiter.
    ABSL_RAW_LOG(FATAL, "sem_post failed with errno %d\n", errno);
  }
}

}  // namespace synchronization_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_INTERNAL_HAVE_SEM_WAITER
