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

#include "absl/base/internal/unscaledcycleclock.h"

#if ABSL_USE_UNSCALED_CYCLECLOCK

#if defined(_WIN32)
#include <intrin.h>
#endif

#if defined(__powerpc__) || defined(__ppc__)
#ifdef __GLIBC__
#include <sys/platform/ppc.h>
#elif defined(__FreeBSD__)
// clang-format off
// This order does actually matter =(.
#include <sys/types.h>
#include <sys/sysctl.h>
// clang-format on

#include "absl/base/call_once.h"
#endif
#endif

#include "absl/base/internal/sysinfo.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {

#if defined(__i386__)

int64_t UnscaledCycleClock::Now() {
  int64_t ret;
  __asm__ volatile("rdtsc" : "=A"(ret));
  return ret;
}

double UnscaledCycleClock::Frequency() {
  return base_internal::NominalCPUFrequency();
}

#elif defined(__x86_64__)

double UnscaledCycleClock::Frequency() {
  return base_internal::NominalCPUFrequency();
}

#elif defined(__powerpc__) || defined(__ppc__)

int64_t UnscaledCycleClock::Now() {
#ifdef __GLIBC__
  return __ppc_get_timebase();
#else
#ifdef __powerpc64__
  int64_t tbr;
  asm volatile("mfspr %0, 268" : "=r"(tbr));
  return tbr;
#else
  int32_t tbu, tbl, tmp;
  asm volatile(
      "mftbu %[hi32]\n"
      "mftb %[lo32]\n"
      "mftbu %[tmp]\n"
      "cmpw %[tmp],%[hi32]\n"
      "bne $-16\n"  // Retry on failure.
      : [hi32] "=r"(tbu), [lo32] "=r"(tbl), [tmp] "=r"(tmp));
  return (static_cast<int64_t>(tbu) << 32) | tbl;
#endif
#endif
}

double UnscaledCycleClock::Frequency() {
#ifdef __GLIBC__
  return __ppc_get_timebase_freq();
#elif defined(_AIX)
  // This is the same constant value as returned by
  // __ppc_get_timebase_freq().
  return static_cast<double>(512000000);
#elif defined(__FreeBSD__)
  static once_flag init_timebase_frequency_once;
  static double timebase_frequency = 0.0;
  base_internal::LowLevelCallOnce(&init_timebase_frequency_once, [&]() {
    size_t length = sizeof(timebase_frequency);
    sysctlbyname("kern.timecounter.tc.timebase.frequency", &timebase_frequency,
                 &length, nullptr, 0);
  });
  return timebase_frequency;
#else
#error Must implement UnscaledCycleClock::Frequency()
#endif
}

#elif defined(__aarch64__)

// System timer of ARMv8 runs at a different frequency than the CPU's.
// The frequency is fixed, typically in the range 1-50MHz.  It can be
// read at CNTFRQ special register.  We assume the OS has set up
// the virtual timer properly.
int64_t UnscaledCycleClock::Now() {
  int64_t virtual_timer_value;
  asm volatile("mrs %0, cntvct_el0" : "=r"(virtual_timer_value));
  return virtual_timer_value;
}

double UnscaledCycleClock::Frequency() {
  uint64_t aarch64_timer_frequency;
  asm volatile("mrs %0, cntfrq_el0" : "=r"(aarch64_timer_frequency));
  return aarch64_timer_frequency;
}

#elif defined(_M_IX86) || defined(_M_X64)

#pragma intrinsic(__rdtsc)

int64_t UnscaledCycleClock::Now() { return __rdtsc(); }

double UnscaledCycleClock::Frequency() {
  return base_internal::NominalCPUFrequency();
}

#endif

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_USE_UNSCALED_CYCLECLOCK
