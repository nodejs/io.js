// Copyright 2023 Google LLC
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

// High-resolution and high-precision timer

// Per-target include guard
// NOTE: this file could/should be a normal header, but user code may reference
// hn::timer, and defining that here requires highway.h.
#if defined(HIGHWAY_HWY_TIMER_INL_H_) == defined(HWY_TARGET_TOGGLE)
#ifdef HIGHWAY_HWY_TIMER_INL_H_
#undef HIGHWAY_HWY_TIMER_INL_H_
#else
#define HIGHWAY_HWY_TIMER_INL_H_
#endif

#include "hwy/highway.h"

#if defined(_WIN32) || defined(_WIN64)
#ifndef NOMINMAX
#define NOMINMAX
#endif  // NOMINMAX
#include <windows.h>
#endif

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_time.h>
#endif

#if defined(__HAIKU__)
#include <OS.h>
#endif

#if HWY_ARCH_PPC && defined(__GLIBC__) && defined(__powerpc64__)
#include <sys/platform/ppc.h>  // NOLINT __ppc_get_timebase_freq
#endif

#if HWY_ARCH_X86 && HWY_COMPILER_MSVC
#include <intrin.h>
#endif

#include <stdint.h>
#include <time.h>  // clock_gettime

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace timer {

// Ticks := platform-specific timer values (CPU cycles on x86). Must be
// unsigned to guarantee wraparound on overflow.
using Ticks = uint64_t;

// Start/Stop return absolute timestamps and must be placed immediately before
// and after the region to measure. We provide separate Start/Stop functions
// because they use different fences.
//
// Background: RDTSC is not 'serializing'; earlier instructions may complete
// after it, and/or later instructions may complete before it. 'Fences' ensure
// regions' elapsed times are independent of such reordering. The only
// documented unprivileged serializing instruction is CPUID, which acts as a
// full fence (no reordering across it in either direction). Unfortunately
// the latency of CPUID varies wildly (perhaps made worse by not initializing
// its EAX input). Because it cannot reliably be deducted from the region's
// elapsed time, it must not be included in the region to measure (i.e.
// between the two RDTSC).
//
// The newer RDTSCP is sometimes described as serializing, but it actually
// only serves as a half-fence with release semantics. Although all
// instructions in the region will complete before the final timestamp is
// captured, subsequent instructions may leak into the region and increase the
// elapsed time. Inserting another fence after the final RDTSCP would prevent
// such reordering without affecting the measured region.
//
// Fortunately, such a fence exists. The LFENCE instruction is only documented
// to delay later loads until earlier loads are visible. However, Intel's
// reference manual says it acts as a full fence (waiting until all earlier
// instructions have completed, and delaying later instructions until it
// completes). AMD assigns the same behavior to MFENCE.
//
// We need a fence before the initial RDTSC to prevent earlier instructions
// from leaking into the region, and arguably another after RDTSC to avoid
// region instructions from completing before the timestamp is recorded.
// When surrounded by fences, the additional RDTSCP half-fence provides no
// benefit, so the initial timestamp can be recorded via RDTSC, which has
// lower overhead than RDTSCP because it does not read TSC_AUX. In summary,
// we define Start = LFENCE/RDTSC/LFENCE; Stop = RDTSCP/LFENCE.
//
// Using Start+Start leads to higher variance and overhead than Stop+Stop.
// However, Stop+Stop includes an LFENCE in the region measurements, which
// adds a delay dependent on earlier loads. The combination of Start+Stop
// is faster than Start+Start and more consistent than Stop+Stop because
// the first LFENCE already delayed subsequent loads before the measured
// region. This combination seems not to have been considered in prior work:
// http://akaros.cs.berkeley.edu/lxr/akaros/kern/arch/x86/rdtsc_test.c
//
// Note: performance counters can measure 'exact' instructions-retired or
// (unhalted) cycle counts. The RDPMC instruction is not serializing and also
// requires fences. Unfortunately, it is not accessible on all OSes and we
// prefer to avoid kernel-mode drivers. Performance counters are also affected
// by several under/over-count errata, so we use the TSC instead.

// Returns a 64-bit timestamp in unit of 'ticks'; to convert to seconds,
// divide by InvariantTicksPerSecond.
inline Ticks Start() {
  Ticks t;
#if HWY_ARCH_PPC && defined(__GLIBC__) && defined(__powerpc64__)
  asm volatile("mfspr %0, %1" : "=r"(t) : "i"(268));
#elif HWY_ARCH_ARM_A64 && !HWY_COMPILER_MSVC
  // pmccntr_el0 is privileged but cntvct_el0 is accessible in Linux and QEMU.
  asm volatile("mrs %0, cntvct_el0" : "=r"(t));
#elif HWY_ARCH_X86 && HWY_COMPILER_MSVC
  _ReadWriteBarrier();
  _mm_lfence();
  _ReadWriteBarrier();
  t = __rdtsc();
  _ReadWriteBarrier();
  _mm_lfence();
  _ReadWriteBarrier();
#elif HWY_ARCH_X86_64
  asm volatile(
      "lfence\n\t"
      "rdtsc\n\t"
      "shl $32, %%rdx\n\t"
      "or %%rdx, %0\n\t"
      "lfence"
      : "=a"(t)
      :
      // "memory" avoids reordering. rdx = TSC >> 32.
      // "cc" = flags modified by SHL.
      : "rdx", "memory", "cc");
#elif HWY_ARCH_RISCV
  asm volatile("fence; rdtime %0" : "=r"(t));
#elif defined(_WIN32) || defined(_WIN64)
  LARGE_INTEGER counter;
  (void)QueryPerformanceCounter(&counter);
  t = counter.QuadPart;
#elif defined(__APPLE__)
  t = mach_absolute_time();
#elif defined(__HAIKU__)
  t = system_time_nsecs();  // since boot
#else  // POSIX
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  t = static_cast<Ticks>(ts.tv_sec * 1000000000LL + ts.tv_nsec);
#endif
  return t;
}

// WARNING: on x86, caller must check HasRDTSCP before using this!
inline Ticks Stop() {
  uint64_t t;
#if HWY_ARCH_PPC && defined(__GLIBC__) && defined(__powerpc64__)
  asm volatile("mfspr %0, %1" : "=r"(t) : "i"(268));
#elif HWY_ARCH_ARM_A64 && !HWY_COMPILER_MSVC
  // pmccntr_el0 is privileged but cntvct_el0 is accessible in Linux and QEMU.
  asm volatile("mrs %0, cntvct_el0" : "=r"(t));
#elif HWY_ARCH_X86 && HWY_COMPILER_MSVC
  _ReadWriteBarrier();
  unsigned aux;
  t = __rdtscp(&aux);
  _ReadWriteBarrier();
  _mm_lfence();
  _ReadWriteBarrier();
#elif HWY_ARCH_X86_64
  // Use inline asm because __rdtscp generates code to store TSC_AUX (ecx).
  asm volatile(
      "rdtscp\n\t"
      "shl $32, %%rdx\n\t"
      "or %%rdx, %0\n\t"
      "lfence"
      : "=a"(t)
      :
      // "memory" avoids reordering. rcx = TSC_AUX. rdx = TSC >> 32.
      // "cc" = flags modified by SHL.
      : "rcx", "rdx", "memory", "cc");
#else
  t = Start();
#endif
  return t;
}

}  // namespace timer

// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#endif  // per-target include guard
