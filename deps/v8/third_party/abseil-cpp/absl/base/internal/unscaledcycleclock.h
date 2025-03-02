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
//
// UnscaledCycleClock
//    An UnscaledCycleClock yields the value and frequency of a cycle counter
//    that increments at a rate that is approximately constant.
//    This class is for internal use only, you should consider using CycleClock
//    instead.
//
// Notes:
// The cycle counter frequency is not necessarily the core clock frequency.
// That is, CycleCounter cycles are not necessarily "CPU cycles".
//
// An arbitrary offset may have been added to the counter at power on.
//
// On some platforms, the rate and offset of the counter may differ
// slightly when read from different CPUs of a multiprocessor.  Usually,
// we try to ensure that the operating system adjusts values periodically
// so that values agree approximately.   If you need stronger guarantees,
// consider using alternate interfaces.
//
// The CPU is not required to maintain the ordering of a cycle counter read
// with respect to surrounding instructions.

#ifndef ABSL_BASE_INTERNAL_UNSCALEDCYCLECLOCK_H_
#define ABSL_BASE_INTERNAL_UNSCALEDCYCLECLOCK_H_

#include <cstdint>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include "absl/base/config.h"
#include "absl/base/internal/unscaledcycleclock_config.h"

#if ABSL_USE_UNSCALED_CYCLECLOCK

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace time_internal {
class UnscaledCycleClockWrapperForGetCurrentTime;
}  // namespace time_internal

namespace base_internal {
class CycleClock;
class UnscaledCycleClockWrapperForInitializeFrequency;

class UnscaledCycleClock {
 private:
  UnscaledCycleClock() = delete;

  // Return the value of a cycle counter that counts at a rate that is
  // approximately constant.
  static int64_t Now();

  // Return the how much UnscaledCycleClock::Now() increases per second.
  // This is not necessarily the core CPU clock frequency.
  // It may be the nominal value report by the kernel, rather than a measured
  // value.
  static double Frequency();

  // Allowed users
  friend class base_internal::CycleClock;
  friend class time_internal::UnscaledCycleClockWrapperForGetCurrentTime;
  friend class base_internal::UnscaledCycleClockWrapperForInitializeFrequency;
};

#if defined(__x86_64__)

inline int64_t UnscaledCycleClock::Now() {
  uint64_t low, high;
  __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
  return static_cast<int64_t>((high << 32) | low);
}

#elif defined(__aarch64__)

// System timer of ARMv8 runs at a different frequency than the CPU's.
// The frequency is fixed, typically in the range 1-50MHz.  It can be
// read at CNTFRQ special register.  We assume the OS has set up
// the virtual timer properly.
inline int64_t UnscaledCycleClock::Now() {
  int64_t virtual_timer_value;
  asm volatile("mrs %0, cntvct_el0" : "=r"(virtual_timer_value));
  return virtual_timer_value;
}

#endif

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_USE_UNSCALED_CYCLECLOCK

#endif  // ABSL_BASE_INTERNAL_UNSCALEDCYCLECLOCK_H_
