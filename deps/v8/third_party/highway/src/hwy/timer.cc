// Copyright 2019 Google LLC
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "hwy/timer.h"

#include <stdlib.h>

#include <chrono>  // NOLINT
#include <ratio>   // NOLINT

#include "hwy/base.h"
#include "hwy/robust_statistics.h"
#include "hwy/timer-inl.h"

#if HWY_ARCH_X86 && !HWY_COMPILER_MSVC
#include <cpuid.h>  // NOLINT
#endif

namespace hwy {
namespace timer = hwy::HWY_NAMESPACE::timer;

namespace platform {
namespace {

// Measures the actual current frequency of Ticks. We cannot rely on the nominal
// frequency encoded in x86 GetCpuString because it is misleading on M1 Rosetta,
// and not reported by AMD. CPUID 0x15 is also not yet widely supported. Also
// used on RISC-V and aarch64.
HWY_MAYBE_UNUSED double MeasureNominalClockRate() {
  double max_ticks_per_sec = 0.0;
  // Arbitrary, enough to ignore 2 outliers without excessive init time.
  for (int rep = 0; rep < 3; ++rep) {
    auto time0 = std::chrono::steady_clock::now();
    using Time = decltype(time0);
    const timer::Ticks ticks0 = timer::Start();
    const Time time_min = time0 + std::chrono::milliseconds(10);

    Time time1;
    timer::Ticks ticks1;
    for (;;) {
      time1 = std::chrono::steady_clock::now();
      // Ideally this would be Stop, but that requires RDTSCP on x86. To avoid
      // another codepath, just use Start instead. now() presumably has its own
      // fence-like behavior.
      ticks1 = timer::Start();  // Do not use Stop, see comment above
      if (time1 >= time_min) break;
    }

    const double dticks = static_cast<double>(ticks1 - ticks0);
    std::chrono::duration<double, std::ratio<1>> dtime = time1 - time0;
    const double ticks_per_sec = dticks / dtime.count();
    max_ticks_per_sec = HWY_MAX(max_ticks_per_sec, ticks_per_sec);
  }
  return max_ticks_per_sec;
}

#if HWY_ARCH_X86

void Cpuid(const uint32_t level, const uint32_t count,
           uint32_t* HWY_RESTRICT abcd) {
#if HWY_COMPILER_MSVC
  int regs[4];
  __cpuidex(regs, level, count);
  for (int i = 0; i < 4; ++i) {
    abcd[i] = regs[i];
  }
#else
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;
  __cpuid_count(level, count, a, b, c, d);
  abcd[0] = a;
  abcd[1] = b;
  abcd[2] = c;
  abcd[3] = d;
#endif
}

bool HasRDTSCP() {
  uint32_t abcd[4];
  Cpuid(0x80000001U, 0, abcd);         // Extended feature flags
  return (abcd[3] & (1u << 27)) != 0;  // RDTSCP
}

#endif  // HWY_ARCH_X86
}  // namespace

HWY_DLLEXPORT bool GetCpuString(char* cpu100) {
#if HWY_ARCH_X86
  uint32_t abcd[4];

  // Check if brand string is supported (it is on all reasonable Intel/AMD)
  Cpuid(0x80000000U, 0, abcd);
  if (abcd[0] < 0x80000004U) {
    cpu100[0] = '\0';
    return false;
  }

  for (size_t i = 0; i < 3; ++i) {
    Cpuid(static_cast<uint32_t>(0x80000002U + i), 0, abcd);
    CopyBytes<sizeof(abcd)>(&abcd[0], cpu100 + i * 16);  // not same size
  }
  cpu100[48] = '\0';
  return true;
#else
  cpu100[0] = '?';
  cpu100[1] = '\0';
  return false;
#endif
}

HWY_DLLEXPORT double Now() {
  static const double mul = 1.0 / InvariantTicksPerSecond();
  return static_cast<double>(timer::Start()) * mul;
}

HWY_DLLEXPORT bool HaveTimerStop(char* cpu100) {
#if HWY_ARCH_X86
  if (!HasRDTSCP()) {
    (void)GetCpuString(cpu100);
    return false;
  }
#endif
  *cpu100 = '\0';
  return true;
}

HWY_DLLEXPORT double InvariantTicksPerSecond() {
#if HWY_ARCH_PPC && defined(__GLIBC__) && defined(__powerpc64__)
  return static_cast<double>(__ppc_get_timebase_freq());
#elif HWY_ARCH_X86 || HWY_ARCH_RISCV || (HWY_ARCH_ARM_A64 && !HWY_COMPILER_MSVC)
  // We assume the x86 TSC is invariant; it is on all recent Intel/AMD CPUs.
  static const double freq = MeasureNominalClockRate();
  return freq;
#elif defined(_WIN32) || defined(_WIN64)
  LARGE_INTEGER freq;
  (void)QueryPerformanceFrequency(&freq);
  return static_cast<double>(freq.QuadPart);
#elif defined(__APPLE__)
  // https://developer.apple.com/library/mac/qa/qa1398/_index.html
  mach_timebase_info_data_t timebase;
  (void)mach_timebase_info(&timebase);
  return static_cast<double>(timebase.denom) / timebase.numer * 1E9;
#else
  return 1E9;  // Haiku and clock_gettime return nanoseconds.
#endif
}

HWY_DLLEXPORT uint64_t TimerResolution() {
  char cpu100[100];
  bool can_use_stop = HaveTimerStop(cpu100);

  // For measuring timer overhead/resolution. Used in a nested loop =>
  // quadratic time, acceptable because we know timer overhead is "low".
  // constexpr because this is used to define array bounds.
  constexpr size_t kTimerSamples = 256;

  // Nested loop avoids exceeding stack/L1 capacity.
  timer::Ticks repetitions[kTimerSamples];
  for (size_t rep = 0; rep < kTimerSamples; ++rep) {
    timer::Ticks samples[kTimerSamples];
    if (can_use_stop) {
      for (size_t i = 0; i < kTimerSamples; ++i) {
        const timer::Ticks t0 = timer::Start();
        const timer::Ticks t1 = timer::Stop();  // we checked HasRDTSCP above
        samples[i] = t1 - t0;
      }
    } else {
      for (size_t i = 0; i < kTimerSamples; ++i) {
        const timer::Ticks t0 = timer::Start();
        const timer::Ticks t1 = timer::Start();  // do not use Stop, see above
        samples[i] = t1 - t0;
      }
    }
    repetitions[rep] = robust_statistics::Mode(samples);
  }
  return robust_statistics::Mode(repetitions);
}

}  // namespace platform
}  // namespace hwy
