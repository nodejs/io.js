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

#include "absl/synchronization/internal/futex_waiter.h"

#ifdef ABSL_INTERNAL_HAVE_FUTEX_WAITER

#include <atomic>
#include <cstdint>
#include <cerrno>

#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/internal/thread_identity.h"
#include "absl/base/optimization.h"
#include "absl/synchronization/internal/kernel_timeout.h"
#include "absl/synchronization/internal/futex.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace synchronization_internal {

#ifdef ABSL_INTERNAL_NEED_REDUNDANT_CONSTEXPR_DECL
constexpr char FutexWaiter::kName[];
#endif

int FutexWaiter::WaitUntil(std::atomic<int32_t>* v, int32_t val,
                           KernelTimeout t) {
#ifdef CLOCK_MONOTONIC
  constexpr bool kHasClockMonotonic = true;
#else
  constexpr bool kHasClockMonotonic = false;
#endif

  // We can't call Futex::WaitUntil() here because the prodkernel implementation
  // does not know about KernelTimeout::SupportsSteadyClock().
  if (!t.has_timeout()) {
    return Futex::Wait(v, val);
  } else if (kHasClockMonotonic && KernelTimeout::SupportsSteadyClock() &&
             t.is_relative_timeout()) {
    auto rel_timespec = t.MakeRelativeTimespec();
    return Futex::WaitRelativeTimeout(v, val, &rel_timespec);
  } else {
    auto abs_timespec = t.MakeAbsTimespec();
    return Futex::WaitAbsoluteTimeout(v, val, &abs_timespec);
  }
}

bool FutexWaiter::Wait(KernelTimeout t) {
  // Loop until we can atomically decrement futex from a positive
  // value, waiting on a futex while we believe it is zero.
  // Note that, since the thread ticker is just reset, we don't need to check
  // whether the thread is idle on the very first pass of the loop.
  bool first_pass = true;
  while (true) {
    int32_t x = futex_.load(std::memory_order_relaxed);
    while (x != 0) {
      if (!futex_.compare_exchange_weak(x, x - 1,
                                        std::memory_order_acquire,
                                        std::memory_order_relaxed)) {
        continue;  // Raced with someone, retry.
      }
      return true;  // Consumed a wakeup, we are done.
    }

    if (!first_pass) MaybeBecomeIdle();
    const int err = WaitUntil(&futex_, 0, t);
    if (err != 0) {
      if (err == -EINTR || err == -EWOULDBLOCK) {
        // Do nothing, the loop will retry.
      } else if (err == -ETIMEDOUT) {
        return false;
      } else {
        ABSL_RAW_LOG(FATAL, "Futex operation failed with error %d\n", err);
      }
    }
    first_pass = false;
  }
}

void FutexWaiter::Post() {
  if (futex_.fetch_add(1, std::memory_order_release) == 0) {
    // We incremented from 0, need to wake a potential waiter.
    Poke();
  }
}

void FutexWaiter::Poke() {
  // Wake one thread waiting on the futex.
  const int err = Futex::Wake(&futex_, 1);
  if (ABSL_PREDICT_FALSE(err < 0)) {
    ABSL_RAW_LOG(FATAL, "Futex operation failed with error %d\n", err);
  }
}

}  // namespace synchronization_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_INTERNAL_HAVE_FUTEX_WAITER
