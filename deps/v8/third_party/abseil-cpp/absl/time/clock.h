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
// -----------------------------------------------------------------------------
// File: clock.h
// -----------------------------------------------------------------------------
//
// This header file contains utility functions for working with the system-wide
// realtime clock. For descriptions of the main time abstractions used within
// this header file, consult the time.h header file.
#ifndef ABSL_TIME_CLOCK_H_
#define ABSL_TIME_CLOCK_H_

#include <cstdint>

#include "absl/base/config.h"
#include "absl/base/macros.h"
#include "absl/time/time.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// Now()
//
// Returns the current time, expressed as an `absl::Time` absolute time value.
absl::Time Now();

// GetCurrentTimeNanos()
//
// Returns the current time, expressed as a count of nanoseconds since the Unix
// Epoch (https://en.wikipedia.org/wiki/Unix_time). Prefer `absl::Now()` instead
// for all but the most performance-sensitive cases (i.e. when you are calling
// this function hundreds of thousands of times per second).
int64_t GetCurrentTimeNanos();

// SleepFor()
//
// Sleeps for the specified duration, expressed as an `absl::Duration`.
//
// Notes:
// * Signal interruptions will not reduce the sleep duration.
// * Returns immediately when passed a nonpositive duration.
void SleepFor(absl::Duration duration);

ABSL_NAMESPACE_END
}  // namespace absl

// -----------------------------------------------------------------------------
// Implementation Details
// -----------------------------------------------------------------------------

// In some build configurations we pass --detect-odr-violations to the
// gold linker.  This causes it to flag weak symbol overrides as ODR
// violations.  Because ODR only applies to C++ and not C,
// --detect-odr-violations ignores symbols not mangled with C++ names.
// By changing our extension points to be extern "C", we dodge this
// check.
extern "C" {
ABSL_DLL void ABSL_INTERNAL_C_SYMBOL(AbslInternalSleepFor)(
    absl::Duration duration);
}  // extern "C"

inline void absl::SleepFor(absl::Duration duration) {
  ABSL_INTERNAL_C_SYMBOL(AbslInternalSleepFor)(duration);
}

#endif  // ABSL_TIME_CLOCK_H_
