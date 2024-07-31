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

#include "absl/synchronization/internal/stdcpp_waiter.h"

#ifdef ABSL_INTERNAL_HAVE_STDCPP_WAITER

#include <chrono>  // NOLINT(build/c++11)
#include <condition_variable>  // NOLINT(build/c++11)
#include <mutex>  // NOLINT(build/c++11)

#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/internal/thread_identity.h"
#include "absl/base/optimization.h"
#include "absl/synchronization/internal/kernel_timeout.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace synchronization_internal {

#ifdef ABSL_INTERNAL_NEED_REDUNDANT_CONSTEXPR_DECL
constexpr char StdcppWaiter::kName[];
#endif

StdcppWaiter::StdcppWaiter() : waiter_count_(0), wakeup_count_(0) {}

bool StdcppWaiter::Wait(KernelTimeout t) {
  std::unique_lock<std::mutex> lock(mu_);
  ++waiter_count_;

  // Loop until we find a wakeup to consume or timeout.
  // Note that, since the thread ticker is just reset, we don't need to check
  // whether the thread is idle on the very first pass of the loop.
  bool first_pass = true;
  while (wakeup_count_ == 0) {
    if (!first_pass) MaybeBecomeIdle();
    // No wakeups available, time to wait.
    if (!t.has_timeout()) {
      cv_.wait(lock);
    } else {
      auto wait_result = t.SupportsSteadyClock() && t.is_relative_timeout()
                             ? cv_.wait_for(lock, t.ToChronoDuration())
                             : cv_.wait_until(lock, t.ToChronoTimePoint());
      if (wait_result == std::cv_status::timeout) {
        --waiter_count_;
        return false;
      }
    }
    first_pass = false;
  }

  // Consume a wakeup and we're done.
  --wakeup_count_;
  --waiter_count_;
  return true;
}

void StdcppWaiter::Post() {
  std::lock_guard<std::mutex> lock(mu_);
  ++wakeup_count_;
  InternalCondVarPoke();
}

void StdcppWaiter::Poke() {
  std::lock_guard<std::mutex> lock(mu_);
  InternalCondVarPoke();
}

void StdcppWaiter::InternalCondVarPoke() {
  if (waiter_count_ != 0) {
    cv_.notify_one();
  }
}

}  // namespace synchronization_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_INTERNAL_HAVE_STDCPP_WAITER
