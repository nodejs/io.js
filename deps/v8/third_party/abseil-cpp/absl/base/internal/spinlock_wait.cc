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

// The OS-specific header included below must provide two calls:
// AbslInternalSpinLockDelay() and AbslInternalSpinLockWake().
// See spinlock_wait.h for the specs.

#include <atomic>
#include <cstdint>

#include "absl/base/internal/spinlock_wait.h"

#if defined(_WIN32)
#include "absl/base/internal/spinlock_win32.inc"
#elif defined(__linux__)
#include "absl/base/internal/spinlock_linux.inc"
#elif defined(__akaros__)
#include "absl/base/internal/spinlock_akaros.inc"
#else
#include "absl/base/internal/spinlock_posix.inc"
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {

// See spinlock_wait.h for spec.
uint32_t SpinLockWait(std::atomic<uint32_t> *w, int n,
                      const SpinLockWaitTransition trans[],
                      base_internal::SchedulingMode scheduling_mode) {
  int loop = 0;
  for (;;) {
    uint32_t v = w->load(std::memory_order_acquire);
    int i;
    for (i = 0; i != n && v != trans[i].from; i++) {
    }
    if (i == n) {
      SpinLockDelay(w, v, ++loop, scheduling_mode);  // no matching transition
    } else if (trans[i].to == v ||                   // null transition
               w->compare_exchange_strong(v, trans[i].to,
                                          std::memory_order_acquire,
                                          std::memory_order_relaxed)) {
      if (trans[i].done) return v;
    }
  }
}

static std::atomic<uint64_t> delay_rand;

// Return a suggested delay in nanoseconds for iteration number "loop"
int SpinLockSuggestedDelayNS(int loop) {
  // Weak pseudo-random number generator to get some spread between threads
  // when many are spinning.
  uint64_t r = delay_rand.load(std::memory_order_relaxed);
  r = 0x5deece66dLL * r + 0xb;   // numbers from nrand48()
  delay_rand.store(r, std::memory_order_relaxed);

  if (loop < 0 || loop > 32) {   // limit loop to 0..32
    loop = 32;
  }
  const int kMinDelay = 128 << 10;  // 128us
  // Double delay every 8 iterations, up to 16x (2ms).
  int delay = kMinDelay << (loop / 8);
  // Randomize in delay..2*delay range, for resulting 128us..4ms range.
  return delay | ((delay - 1) & static_cast<int>(r));
}

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl
