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

// The implementation of CycleClock::Frequency.
//
// NOTE: only i386 and x86_64 have been well tested.
// PPC, sparc, alpha, and ia64 are based on
//    http://peter.kuscsik.com/wordpress/?p=14
// with modifications by m3b.  See also
//    https://setisvn.ssl.berkeley.edu/svn/lib/fftw-3.0.1/kernel/cycle.h

#include "absl/base/internal/cycleclock.h"

#include <atomic>
#include <chrono>  // NOLINT(build/c++11)

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/unscaledcycleclock.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {

#if ABSL_USE_UNSCALED_CYCLECLOCK

#ifdef ABSL_INTERNAL_NEED_REDUNDANT_CONSTEXPR_DECL
constexpr int32_t CycleClock::kShift;
constexpr double CycleClock::kFrequencyScale;
#endif

ABSL_CONST_INIT std::atomic<CycleClockSourceFunc>
    CycleClock::cycle_clock_source_{nullptr};

void CycleClockSource::Register(CycleClockSourceFunc source) {
  // Corresponds to the load(std::memory_order_acquire) in LoadCycleClockSource.
  CycleClock::cycle_clock_source_.store(source, std::memory_order_release);
}

#ifdef _WIN32
int64_t CycleClock::Now() {
  auto fn = LoadCycleClockSource();
  if (fn == nullptr) {
    return base_internal::UnscaledCycleClock::Now() >> kShift;
  }
  return fn() >> kShift;
}
#endif

#else

int64_t CycleClock::Now() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

double CycleClock::Frequency() {
  return 1e9;
}

#endif

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl
