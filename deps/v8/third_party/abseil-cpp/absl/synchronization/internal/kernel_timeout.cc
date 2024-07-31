// Copyright 2023 The Abseil Authors
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

#include "absl/synchronization/internal/kernel_timeout.h"

#ifndef _WIN32
#include <sys/types.h>
#endif

#include <algorithm>
#include <chrono>  // NOLINT(build/c++11)
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>

#include "absl/base/attributes.h"
#include "absl/base/call_once.h"
#include "absl/base/config.h"
#include "absl/time/time.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace synchronization_internal {

#ifdef ABSL_INTERNAL_NEED_REDUNDANT_CONSTEXPR_DECL
constexpr uint64_t KernelTimeout::kNoTimeout;
constexpr int64_t KernelTimeout::kMaxNanos;
#endif

int64_t KernelTimeout::SteadyClockNow() {
  if (!SupportsSteadyClock()) {
    return absl::GetCurrentTimeNanos();
  }
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

KernelTimeout::KernelTimeout(absl::Time t) {
  // `absl::InfiniteFuture()` is a common "no timeout" value and cheaper to
  // compare than convert.
  if (t == absl::InfiniteFuture()) {
    rep_ = kNoTimeout;
    return;
  }

  int64_t unix_nanos = absl::ToUnixNanos(t);

  // A timeout that lands before the unix epoch is converted to 0.
  // In theory implementations should expire these timeouts immediately.
  if (unix_nanos < 0) {
    unix_nanos = 0;
  }

  // Values greater than or equal to kMaxNanos are converted to infinite.
  if (unix_nanos >= kMaxNanos) {
    rep_ = kNoTimeout;
    return;
  }

  rep_ = static_cast<uint64_t>(unix_nanos) << 1;
}

KernelTimeout::KernelTimeout(absl::Duration d) {
  // `absl::InfiniteDuration()` is a common "no timeout" value and cheaper to
  // compare than convert.
  if (d == absl::InfiniteDuration()) {
    rep_ = kNoTimeout;
    return;
  }

  int64_t nanos = absl::ToInt64Nanoseconds(d);

  // Negative durations are normalized to 0.
  // In theory implementations should expire these timeouts immediately.
  if (nanos < 0) {
    nanos = 0;
  }

  int64_t now = SteadyClockNow();
  if (nanos > kMaxNanos - now) {
    // Durations that would be greater than kMaxNanos are converted to infinite.
    rep_ = kNoTimeout;
    return;
  }

  nanos += now;
  rep_ = (static_cast<uint64_t>(nanos) << 1) | uint64_t{1};
}

int64_t KernelTimeout::MakeAbsNanos() const {
  if (!has_timeout()) {
    return kMaxNanos;
  }

  int64_t nanos = RawAbsNanos();

  if (is_relative_timeout()) {
    // We need to change epochs, because the relative timeout might be
    // represented by an absolute timestamp from another clock.
    nanos = std::max<int64_t>(nanos - SteadyClockNow(), 0);
    int64_t now = absl::GetCurrentTimeNanos();
    if (nanos > kMaxNanos - now) {
      // Overflow.
      nanos = kMaxNanos;
    } else {
      nanos += now;
    }
  } else if (nanos == 0) {
    // Some callers have assumed that 0 means no timeout, so instead we return a
    // time of 1 nanosecond after the epoch.
    nanos = 1;
  }

  return nanos;
}

int64_t KernelTimeout::InNanosecondsFromNow() const {
  if (!has_timeout()) {
    return kMaxNanos;
  }

  int64_t nanos = RawAbsNanos();
  if (is_absolute_timeout()) {
    return std::max<int64_t>(nanos - absl::GetCurrentTimeNanos(), 0);
  }
  return std::max<int64_t>(nanos - SteadyClockNow(), 0);
}

struct timespec KernelTimeout::MakeAbsTimespec() const {
  return absl::ToTimespec(absl::Nanoseconds(MakeAbsNanos()));
}

struct timespec KernelTimeout::MakeRelativeTimespec() const {
  return absl::ToTimespec(absl::Nanoseconds(InNanosecondsFromNow()));
}

#ifndef _WIN32
struct timespec KernelTimeout::MakeClockAbsoluteTimespec(clockid_t c) const {
  if (!has_timeout()) {
    return absl::ToTimespec(absl::Nanoseconds(kMaxNanos));
  }

  int64_t nanos = RawAbsNanos();
  if (is_absolute_timeout()) {
    nanos -= absl::GetCurrentTimeNanos();
  } else {
    nanos -= SteadyClockNow();
  }

  struct timespec now;
  ABSL_RAW_CHECK(clock_gettime(c, &now) == 0, "clock_gettime() failed");
  absl::Duration from_clock_epoch =
      absl::DurationFromTimespec(now) + absl::Nanoseconds(nanos);
  if (from_clock_epoch <= absl::ZeroDuration()) {
    // Some callers have assumed that 0 means no timeout, so instead we return a
    // time of 1 nanosecond after the epoch. For safety we also do not return
    // negative values.
    return absl::ToTimespec(absl::Nanoseconds(1));
  }
  return absl::ToTimespec(from_clock_epoch);
}
#endif

KernelTimeout::DWord KernelTimeout::InMillisecondsFromNow() const {
  constexpr DWord kInfinite = std::numeric_limits<DWord>::max();

  if (!has_timeout()) {
    return kInfinite;
  }

  constexpr uint64_t kNanosInMillis = uint64_t{1'000'000};
  constexpr uint64_t kMaxValueNanos =
      std::numeric_limits<int64_t>::max() - kNanosInMillis + 1;

  uint64_t ns_from_now = static_cast<uint64_t>(InNanosecondsFromNow());
  if (ns_from_now >= kMaxValueNanos) {
    // Rounding up would overflow.
    return kInfinite;
  }
  // Convert to milliseconds, always rounding up.
  uint64_t ms_from_now = (ns_from_now + kNanosInMillis - 1) / kNanosInMillis;
  if (ms_from_now > kInfinite) {
    return kInfinite;
  }
  return static_cast<DWord>(ms_from_now);
}

std::chrono::time_point<std::chrono::system_clock>
KernelTimeout::ToChronoTimePoint() const {
  if (!has_timeout()) {
    return std::chrono::time_point<std::chrono::system_clock>::max();
  }

  // The cast to std::microseconds is because (on some platforms) the
  // std::ratio used by std::chrono::steady_clock doesn't convert to
  // std::nanoseconds, so it doesn't compile.
  auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::nanoseconds(MakeAbsNanos()));
  return std::chrono::system_clock::from_time_t(0) + micros;
}

std::chrono::nanoseconds KernelTimeout::ToChronoDuration() const {
  if (!has_timeout()) {
    return std::chrono::nanoseconds::max();
  }
  return std::chrono::nanoseconds(InNanosecondsFromNow());
}

}  // namespace synchronization_internal
ABSL_NAMESPACE_END
}  // namespace absl
