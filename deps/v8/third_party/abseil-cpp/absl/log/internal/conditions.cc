// Copyright 2022 The Abseil Authors.
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

#include "absl/log/internal/conditions.h"

#include <atomic>
#include <cstdint>

#include "absl/base/config.h"
#include "absl/base/internal/cycleclock.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {
namespace {

// The following code behaves like AtomicStatsCounter::LossyAdd() for
// speed since it is fine to lose occasional updates.
// Returns old value of *counter.
uint32_t LossyIncrement(std::atomic<uint32_t>* counter) {
  const uint32_t value = counter->load(std::memory_order_relaxed);
  counter->store(value + 1, std::memory_order_relaxed);
  return value;
}

}  // namespace

bool LogEveryNState::ShouldLog(int n) {
  return n > 0 && (LossyIncrement(&counter_) % static_cast<uint32_t>(n)) == 0;
}

bool LogFirstNState::ShouldLog(int n) {
  const uint32_t counter_value = counter_.load(std::memory_order_relaxed);
  if (static_cast<int64_t>(counter_value) < n) {
    counter_.store(counter_value + 1, std::memory_order_relaxed);
    return true;
  }
  return false;
}

bool LogEveryPow2State::ShouldLog() {
  const uint32_t new_value = LossyIncrement(&counter_) + 1;
  return (new_value & (new_value - 1)) == 0;
}

bool LogEveryNSecState::ShouldLog(double seconds) {
  using absl::base_internal::CycleClock;
  LossyIncrement(&counter_);
  const int64_t now_cycles = CycleClock::Now();
  int64_t next_cycles = next_log_time_cycles_.load(std::memory_order_relaxed);
#if defined(__myriad2__)
  // myriad2 does not have 8-byte compare and exchange.  Use a racy version that
  // is "good enough" but will over-log in the face of concurrent logging.
  if (now_cycles > next_cycles) {
    next_log_time_cycles_.store(now_cycles + seconds * CycleClock::Frequency(),
                                std::memory_order_relaxed);
    return true;
  }
  return false;
#else
  do {
    if (now_cycles <= next_cycles) return false;
  } while (!next_log_time_cycles_.compare_exchange_weak(
      next_cycles, now_cycles + seconds * CycleClock::Frequency(),
      std::memory_order_relaxed, std::memory_order_relaxed));
  return true;
#endif
}

}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl
