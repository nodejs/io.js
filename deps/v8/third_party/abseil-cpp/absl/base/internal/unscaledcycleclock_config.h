// Copyright 2022 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef ABSL_BASE_INTERNAL_UNSCALEDCYCLECLOCK_CONFIG_H_
#define ABSL_BASE_INTERNAL_UNSCALEDCYCLECLOCK_CONFIG_H_

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

// The following platforms have an implementation of a hardware counter.
#if defined(__i386__) || defined(__x86_64__) || defined(__aarch64__) || \
    defined(__powerpc__) || defined(__ppc__) || defined(__riscv) ||     \
    defined(_M_IX86) || (defined(_M_X64) && !defined(_M_ARM64EC))
#define ABSL_HAVE_UNSCALED_CYCLECLOCK_IMPLEMENTATION 1
#else
#define ABSL_HAVE_UNSCALED_CYCLECLOCK_IMPLEMENTATION 0
#endif

// The following platforms often disable access to the hardware
// counter (through a sandbox) even if the underlying hardware has a
// usable counter. The CycleTimer interface also requires a *scaled*
// CycleClock that runs at atleast 1 MHz. We've found some Android
// ARM64 devices where this is not the case, so we disable it by
// default on Android ARM64.
#if defined(__native_client__) || (defined(__APPLE__)) || \
    (defined(__ANDROID__) && defined(__aarch64__))
#define ABSL_USE_UNSCALED_CYCLECLOCK_DEFAULT 0
#else
#define ABSL_USE_UNSCALED_CYCLECLOCK_DEFAULT 1
#endif

// UnscaledCycleClock is an optional internal feature.
// Use "#if ABSL_USE_UNSCALED_CYCLECLOCK" to test for its presence.
// Can be overridden at compile-time via -DABSL_USE_UNSCALED_CYCLECLOCK=0|1
#if !defined(ABSL_USE_UNSCALED_CYCLECLOCK)
#define ABSL_USE_UNSCALED_CYCLECLOCK               \
  (ABSL_HAVE_UNSCALED_CYCLECLOCK_IMPLEMENTATION && \
   ABSL_USE_UNSCALED_CYCLECLOCK_DEFAULT)
#endif

#if ABSL_USE_UNSCALED_CYCLECLOCK
// This macro can be used to test if UnscaledCycleClock::Frequency()
// is NominalCPUFrequency() on a particular platform.
#if (defined(__i386__) || defined(__x86_64__) || defined(__riscv) || \
     defined(_M_IX86) || defined(_M_X64))
#define ABSL_INTERNAL_UNSCALED_CYCLECLOCK_FREQUENCY_IS_CPU_FREQUENCY
#endif
#endif

#endif  // ABSL_BASE_INTERNAL_UNSCALEDCYCLECLOCK_CONFIG_H_
