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

#ifndef ABSL_BASE_INTERNAL_SPINLOCK_WAIT_H_
#define ABSL_BASE_INTERNAL_SPINLOCK_WAIT_H_

// Operations to make atomic transitions on a word, and to allow
// waiting for those transitions to become possible.

#include <stdint.h>
#include <atomic>

#include "absl/base/internal/scheduling_mode.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {

// SpinLockWait() waits until it can perform one of several transitions from
// "from" to "to".  It returns when it performs a transition where done==true.
struct SpinLockWaitTransition {
  uint32_t from;
  uint32_t to;
  bool done;
};

// Wait until *w can transition from trans[i].from to trans[i].to for some i
// satisfying 0<=i<n && trans[i].done, atomically make the transition,
// then return the old value of *w.   Make any other atomic transitions
// where !trans[i].done, but continue waiting.
//
// Wakeups for threads blocked on SpinLockWait do not respect priorities.
uint32_t SpinLockWait(std::atomic<uint32_t> *w, int n,
                      const SpinLockWaitTransition trans[],
                      SchedulingMode scheduling_mode);

// If possible, wake some thread that has called SpinLockDelay(w, ...). If `all`
// is true, wake all such threads. On some systems, this may be a no-op; on
// those systems, threads calling SpinLockDelay() will always wake eventually
// even if SpinLockWake() is never called.
void SpinLockWake(std::atomic<uint32_t> *w, bool all);

// Wait for an appropriate spin delay on iteration "loop" of a
// spin loop on location *w, whose previously observed value was "value".
// SpinLockDelay() may do nothing, may yield the CPU, may sleep a clock tick,
// or may wait for a call to SpinLockWake(w).
void SpinLockDelay(std::atomic<uint32_t> *w, uint32_t value, int loop,
                   base_internal::SchedulingMode scheduling_mode);

// Helper used by AbslInternalSpinLockDelay.
// Returns a suggested delay in nanoseconds for iteration number "loop".
int SpinLockSuggestedDelayNS(int loop);

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl

// In some build configurations we pass --detect-odr-violations to the
// gold linker.  This causes it to flag weak symbol overrides as ODR
// violations.  Because ODR only applies to C++ and not C,
// --detect-odr-violations ignores symbols not mangled with C++ names.
// By changing our extension points to be extern "C", we dodge this
// check.
extern "C" {
void ABSL_INTERNAL_C_SYMBOL(AbslInternalSpinLockWake)(std::atomic<uint32_t> *w,
                                                      bool all);
void ABSL_INTERNAL_C_SYMBOL(AbslInternalSpinLockDelay)(
    std::atomic<uint32_t> *w, uint32_t value, int loop,
    absl::base_internal::SchedulingMode scheduling_mode);
}

inline void absl::base_internal::SpinLockWake(std::atomic<uint32_t> *w,
                                              bool all) {
  ABSL_INTERNAL_C_SYMBOL(AbslInternalSpinLockWake)(w, all);
}

inline void absl::base_internal::SpinLockDelay(
    std::atomic<uint32_t> *w, uint32_t value, int loop,
    absl::base_internal::SchedulingMode scheduling_mode) {
  ABSL_INTERNAL_C_SYMBOL(AbslInternalSpinLockDelay)
  (w, value, loop, scheduling_mode);
}

#endif  // ABSL_BASE_INTERNAL_SPINLOCK_WAIT_H_
