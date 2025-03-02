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

#ifndef HIGHWAY_HWY_TIMER_H_
#define HIGHWAY_HWY_TIMER_H_

// Platform-specific timer functions. Provides Now() and functions for
// interpreting and converting the timer-inl.h Ticks.

#include <stdint.h>

#include "hwy/highway_export.h"

namespace hwy {
namespace platform {

// Returns current timestamp [in seconds] relative to an unspecified origin.
// Features: monotonic (no negative elapsed time), steady (unaffected by system
// time changes), high-resolution (on the order of microseconds).
// Uses InvariantTicksPerSecond and the baseline version of timer::Start().
HWY_DLLEXPORT double Now();

// Functions for use with timer-inl.h:

// Returns whether it is safe to call timer::Stop without executing an illegal
// instruction; if false, fills cpu100 (a pointer to a 100 character buffer)
// via GetCpuString().
HWY_DLLEXPORT bool HaveTimerStop(char* cpu100);

// Returns tick rate, useful for converting timer::Ticks to seconds. Invariant
// means the tick counter frequency is independent of CPU throttling or sleep.
// This call may be expensive, callers should cache the result.
HWY_DLLEXPORT double InvariantTicksPerSecond();

// Returns ticks elapsed in back to back timer calls, i.e. a function of the
// timer resolution (minimum measurable difference) and overhead.
// This call is expensive, callers should cache the result.
HWY_DLLEXPORT uint64_t TimerResolution();

// Returns false if no detailed description is available, otherwise fills
// `cpu100` with up to 100 characters (including \0) identifying the CPU model.
HWY_DLLEXPORT bool GetCpuString(char* cpu100);

}  // namespace platform

struct Timestamp {
  Timestamp() { t = platform::Now(); }
  double t;
};

static inline double SecondsSince(const Timestamp& t0) {
  const Timestamp t1;
  return t1.t - t0.t;
}

}  // namespace hwy

#endif  // HIGHWAY_HWY_TIMER_H_
