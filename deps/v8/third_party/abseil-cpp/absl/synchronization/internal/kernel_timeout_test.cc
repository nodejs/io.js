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

#include <ctime>
#include <chrono>  // NOLINT(build/c++11)
#include <limits>

#include "absl/base/config.h"
#include "absl/random/random.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"

// Test go/btm support by randomizing the value of clock_gettime() for
// CLOCK_MONOTONIC. This works by overriding a weak symbol in glibc.
// We should be resistant to this randomization when !SupportsSteadyClock().
#if defined(__GOOGLE_GRTE_VERSION__) &&      \
    !defined(ABSL_HAVE_ADDRESS_SANITIZER) && \
    !defined(ABSL_HAVE_MEMORY_SANITIZER) &&  \
    !defined(ABSL_HAVE_THREAD_SANITIZER)
extern "C" int __clock_gettime(clockid_t c, struct timespec* ts);

extern "C" int clock_gettime(clockid_t c, struct timespec* ts) {
  if (c == CLOCK_MONOTONIC &&
      !absl::synchronization_internal::KernelTimeout::SupportsSteadyClock()) {
    thread_local absl::BitGen gen;  // NOLINT
    ts->tv_sec = absl::Uniform(gen, 0, 1'000'000'000);
    ts->tv_nsec = absl::Uniform(gen, 0, 1'000'000'000);
    return 0;
  }
  return __clock_gettime(c, ts);
}
#endif

namespace {

#if defined(ABSL_HAVE_ADDRESS_SANITIZER) ||                        \
    defined(ABSL_HAVE_MEMORY_SANITIZER) ||                         \
    defined(ABSL_HAVE_THREAD_SANITIZER) || defined(__ANDROID__) || \
    defined(__APPLE__) || defined(_WIN32) || defined(_WIN64)
constexpr absl::Duration kTimingBound = absl::Milliseconds(5);
#else
constexpr absl::Duration kTimingBound = absl::Microseconds(250);
#endif

using absl::synchronization_internal::KernelTimeout;

// TODO(b/348224897): re-enabled when the flakiness is fixed.
TEST(KernelTimeout, DISABLED_FiniteTimes) {
  constexpr absl::Duration kDurationsToTest[] = {
    absl::ZeroDuration(),
    absl::Nanoseconds(1),
    absl::Microseconds(1),
    absl::Milliseconds(1),
    absl::Seconds(1),
    absl::Minutes(1),
    absl::Hours(1),
    absl::Hours(1000),
    -absl::Nanoseconds(1),
    -absl::Microseconds(1),
    -absl::Milliseconds(1),
    -absl::Seconds(1),
    -absl::Minutes(1),
    -absl::Hours(1),
    -absl::Hours(1000),
  };

  for (auto duration : kDurationsToTest) {
    const absl::Time now = absl::Now();
    const absl::Time when = now + duration;
    SCOPED_TRACE(duration);
    KernelTimeout t(when);
    EXPECT_TRUE(t.has_timeout());
    EXPECT_TRUE(t.is_absolute_timeout());
    EXPECT_FALSE(t.is_relative_timeout());
    EXPECT_EQ(absl::TimeFromTimespec(t.MakeAbsTimespec()), when);
#ifndef _WIN32
    EXPECT_LE(
        absl::AbsDuration(absl::Now() + duration -
                          absl::TimeFromTimespec(
                              t.MakeClockAbsoluteTimespec(CLOCK_REALTIME))),
        absl::Milliseconds(10));
#endif
    EXPECT_LE(
        absl::AbsDuration(absl::DurationFromTimespec(t.MakeRelativeTimespec()) -
                          std::max(duration, absl::ZeroDuration())),
        kTimingBound);
    EXPECT_EQ(absl::FromUnixNanos(t.MakeAbsNanos()), when);
    EXPECT_LE(absl::AbsDuration(absl::Milliseconds(t.InMillisecondsFromNow()) -
                                std::max(duration, absl::ZeroDuration())),
              absl::Milliseconds(5));
    EXPECT_LE(absl::AbsDuration(absl::FromChrono(t.ToChronoTimePoint()) - when),
              absl::Microseconds(1));
    EXPECT_LE(absl::AbsDuration(absl::FromChrono(t.ToChronoDuration()) -
                                std::max(duration, absl::ZeroDuration())),
              kTimingBound);
  }
}

TEST(KernelTimeout, InfiniteFuture) {
  KernelTimeout t(absl::InfiniteFuture());
  EXPECT_FALSE(t.has_timeout());
  // Callers are expected to check has_timeout() instead of using the methods
  // below, but we do try to do something reasonable if they don't. We may not
  // be able to round-trip back to absl::InfiniteDuration() or
  // absl::InfiniteFuture(), but we should return a very large value.
  EXPECT_GT(absl::TimeFromTimespec(t.MakeAbsTimespec()),
            absl::Now() + absl::Hours(100000));
#ifndef _WIN32
  EXPECT_GT(absl::TimeFromTimespec(t.MakeClockAbsoluteTimespec(CLOCK_REALTIME)),
            absl::Now() + absl::Hours(100000));
#endif
  EXPECT_GT(absl::DurationFromTimespec(t.MakeRelativeTimespec()),
            absl::Hours(100000));
  EXPECT_GT(absl::FromUnixNanos(t.MakeAbsNanos()),
            absl::Now() + absl::Hours(100000));
  EXPECT_EQ(t.InMillisecondsFromNow(),
            std::numeric_limits<KernelTimeout::DWord>::max());
  EXPECT_EQ(t.ToChronoTimePoint(),
            std::chrono::time_point<std::chrono::system_clock>::max());
  EXPECT_GE(t.ToChronoDuration(), std::chrono::nanoseconds::max());
}

TEST(KernelTimeout, DefaultConstructor) {
  // The default constructor is equivalent to absl::InfiniteFuture().
  KernelTimeout t;
  EXPECT_FALSE(t.has_timeout());
  // Callers are expected to check has_timeout() instead of using the methods
  // below, but we do try to do something reasonable if they don't. We may not
  // be able to round-trip back to absl::InfiniteDuration() or
  // absl::InfiniteFuture(), but we should return a very large value.
  EXPECT_GT(absl::TimeFromTimespec(t.MakeAbsTimespec()),
            absl::Now() + absl::Hours(100000));
#ifndef _WIN32
  EXPECT_GT(absl::TimeFromTimespec(t.MakeClockAbsoluteTimespec(CLOCK_REALTIME)),
            absl::Now() + absl::Hours(100000));
#endif
  EXPECT_GT(absl::DurationFromTimespec(t.MakeRelativeTimespec()),
            absl::Hours(100000));
  EXPECT_GT(absl::FromUnixNanos(t.MakeAbsNanos()),
            absl::Now() + absl::Hours(100000));
  EXPECT_EQ(t.InMillisecondsFromNow(),
            std::numeric_limits<KernelTimeout::DWord>::max());
  EXPECT_EQ(t.ToChronoTimePoint(),
            std::chrono::time_point<std::chrono::system_clock>::max());
  EXPECT_GE(t.ToChronoDuration(), std::chrono::nanoseconds::max());
}

TEST(KernelTimeout, TimeMaxNanos) {
  // Time >= kMaxNanos should behave as no timeout.
  KernelTimeout t(absl::FromUnixNanos(std::numeric_limits<int64_t>::max()));
  EXPECT_FALSE(t.has_timeout());
  // Callers are expected to check has_timeout() instead of using the methods
  // below, but we do try to do something reasonable if they don't. We may not
  // be able to round-trip back to absl::InfiniteDuration() or
  // absl::InfiniteFuture(), but we should return a very large value.
  EXPECT_GT(absl::TimeFromTimespec(t.MakeAbsTimespec()),
            absl::Now() + absl::Hours(100000));
#ifndef _WIN32
  EXPECT_GT(absl::TimeFromTimespec(t.MakeClockAbsoluteTimespec(CLOCK_REALTIME)),
            absl::Now() + absl::Hours(100000));
#endif
  EXPECT_GT(absl::DurationFromTimespec(t.MakeRelativeTimespec()),
            absl::Hours(100000));
  EXPECT_GT(absl::FromUnixNanos(t.MakeAbsNanos()),
            absl::Now() + absl::Hours(100000));
  EXPECT_EQ(t.InMillisecondsFromNow(),
            std::numeric_limits<KernelTimeout::DWord>::max());
  EXPECT_EQ(t.ToChronoTimePoint(),
            std::chrono::time_point<std::chrono::system_clock>::max());
  EXPECT_GE(t.ToChronoDuration(), std::chrono::nanoseconds::max());
}

TEST(KernelTimeout, Never) {
  // KernelTimeout::Never() is equivalent to absl::InfiniteFuture().
  KernelTimeout t = KernelTimeout::Never();
  EXPECT_FALSE(t.has_timeout());
  // Callers are expected to check has_timeout() instead of using the methods
  // below, but we do try to do something reasonable if they don't. We may not
  // be able to round-trip back to absl::InfiniteDuration() or
  // absl::InfiniteFuture(), but we should return a very large value.
  EXPECT_GT(absl::TimeFromTimespec(t.MakeAbsTimespec()),
            absl::Now() + absl::Hours(100000));
#ifndef _WIN32
  EXPECT_GT(absl::TimeFromTimespec(t.MakeClockAbsoluteTimespec(CLOCK_REALTIME)),
            absl::Now() + absl::Hours(100000));
#endif
  EXPECT_GT(absl::DurationFromTimespec(t.MakeRelativeTimespec()),
            absl::Hours(100000));
  EXPECT_GT(absl::FromUnixNanos(t.MakeAbsNanos()),
            absl::Now() + absl::Hours(100000));
  EXPECT_EQ(t.InMillisecondsFromNow(),
            std::numeric_limits<KernelTimeout::DWord>::max());
  EXPECT_EQ(t.ToChronoTimePoint(),
            std::chrono::time_point<std::chrono::system_clock>::max());
  EXPECT_GE(t.ToChronoDuration(), std::chrono::nanoseconds::max());
}

TEST(KernelTimeout, InfinitePast) {
  KernelTimeout t(absl::InfinitePast());
  EXPECT_TRUE(t.has_timeout());
  EXPECT_TRUE(t.is_absolute_timeout());
  EXPECT_FALSE(t.is_relative_timeout());
  EXPECT_LE(absl::TimeFromTimespec(t.MakeAbsTimespec()),
            absl::FromUnixNanos(1));
#ifndef _WIN32
  EXPECT_LE(absl::TimeFromTimespec(t.MakeClockAbsoluteTimespec(CLOCK_REALTIME)),
            absl::FromUnixSeconds(1));
#endif
  EXPECT_EQ(absl::DurationFromTimespec(t.MakeRelativeTimespec()),
            absl::ZeroDuration());
  EXPECT_LE(absl::FromUnixNanos(t.MakeAbsNanos()), absl::FromUnixNanos(1));
  EXPECT_EQ(t.InMillisecondsFromNow(), KernelTimeout::DWord{0});
  EXPECT_LT(t.ToChronoTimePoint(), std::chrono::system_clock::from_time_t(0) +
                                       std::chrono::seconds(1));
  EXPECT_EQ(t.ToChronoDuration(), std::chrono::nanoseconds(0));
}

// TODO(b/348224897): re-enabled when the flakiness is fixed.
TEST(KernelTimeout, DISABLED_FiniteDurations) {
  constexpr absl::Duration kDurationsToTest[] = {
    absl::ZeroDuration(),
    absl::Nanoseconds(1),
    absl::Microseconds(1),
    absl::Milliseconds(1),
    absl::Seconds(1),
    absl::Minutes(1),
    absl::Hours(1),
    absl::Hours(1000),
  };

  for (auto duration : kDurationsToTest) {
    SCOPED_TRACE(duration);
    KernelTimeout t(duration);
    EXPECT_TRUE(t.has_timeout());
    EXPECT_FALSE(t.is_absolute_timeout());
    EXPECT_TRUE(t.is_relative_timeout());
    EXPECT_LE(absl::AbsDuration(absl::Now() + duration -
                                absl::TimeFromTimespec(t.MakeAbsTimespec())),
              absl::Milliseconds(5));
#ifndef _WIN32
    EXPECT_LE(
        absl::AbsDuration(absl::Now() + duration -
                          absl::TimeFromTimespec(
                              t.MakeClockAbsoluteTimespec(CLOCK_REALTIME))),
        absl::Milliseconds(5));
#endif
    EXPECT_LE(
        absl::AbsDuration(absl::DurationFromTimespec(t.MakeRelativeTimespec()) -
                          duration),
        kTimingBound);
    EXPECT_LE(absl::AbsDuration(absl::Now() + duration -
                                absl::FromUnixNanos(t.MakeAbsNanos())),
              absl::Milliseconds(5));
    EXPECT_LE(absl::Milliseconds(t.InMillisecondsFromNow()) - duration,
              absl::Milliseconds(5));
    EXPECT_LE(absl::AbsDuration(absl::Now() + duration -
                                absl::FromChrono(t.ToChronoTimePoint())),
              kTimingBound);
    EXPECT_LE(
        absl::AbsDuration(absl::FromChrono(t.ToChronoDuration()) - duration),
        kTimingBound);
  }
}

// TODO(b/348224897): re-enabled when the flakiness is fixed.
TEST(KernelTimeout, DISABLED_NegativeDurations) {
  constexpr absl::Duration kDurationsToTest[] = {
    -absl::ZeroDuration(),
    -absl::Nanoseconds(1),
    -absl::Microseconds(1),
    -absl::Milliseconds(1),
    -absl::Seconds(1),
    -absl::Minutes(1),
    -absl::Hours(1),
    -absl::Hours(1000),
    -absl::InfiniteDuration(),
  };

  for (auto duration : kDurationsToTest) {
    // Negative durations should all be converted to zero durations or "now".
    SCOPED_TRACE(duration);
    KernelTimeout t(duration);
    EXPECT_TRUE(t.has_timeout());
    EXPECT_FALSE(t.is_absolute_timeout());
    EXPECT_TRUE(t.is_relative_timeout());
    EXPECT_LE(absl::AbsDuration(absl::Now() -
                                absl::TimeFromTimespec(t.MakeAbsTimespec())),
              absl::Milliseconds(5));
#ifndef _WIN32
    EXPECT_LE(absl::AbsDuration(absl::Now() - absl::TimeFromTimespec(
                                                  t.MakeClockAbsoluteTimespec(
                                                      CLOCK_REALTIME))),
              absl::Milliseconds(5));
#endif
    EXPECT_EQ(absl::DurationFromTimespec(t.MakeRelativeTimespec()),
              absl::ZeroDuration());
    EXPECT_LE(
        absl::AbsDuration(absl::Now() - absl::FromUnixNanos(t.MakeAbsNanos())),
        absl::Milliseconds(5));
    EXPECT_EQ(t.InMillisecondsFromNow(), KernelTimeout::DWord{0});
    EXPECT_LE(absl::AbsDuration(absl::Now() -
                                absl::FromChrono(t.ToChronoTimePoint())),
              absl::Milliseconds(5));
    EXPECT_EQ(t.ToChronoDuration(), std::chrono::nanoseconds(0));
  }
}

TEST(KernelTimeout, InfiniteDuration) {
  KernelTimeout t(absl::InfiniteDuration());
  EXPECT_FALSE(t.has_timeout());
  // Callers are expected to check has_timeout() instead of using the methods
  // below, but we do try to do something reasonable if they don't. We may not
  // be able to round-trip back to absl::InfiniteDuration() or
  // absl::InfiniteFuture(), but we should return a very large value.
  EXPECT_GT(absl::TimeFromTimespec(t.MakeAbsTimespec()),
            absl::Now() + absl::Hours(100000));
#ifndef _WIN32
  EXPECT_GT(absl::TimeFromTimespec(t.MakeClockAbsoluteTimespec(CLOCK_REALTIME)),
            absl::Now() + absl::Hours(100000));
#endif
  EXPECT_GT(absl::DurationFromTimespec(t.MakeRelativeTimespec()),
            absl::Hours(100000));
  EXPECT_GT(absl::FromUnixNanos(t.MakeAbsNanos()),
            absl::Now() + absl::Hours(100000));
  EXPECT_EQ(t.InMillisecondsFromNow(),
            std::numeric_limits<KernelTimeout::DWord>::max());
  EXPECT_EQ(t.ToChronoTimePoint(),
            std::chrono::time_point<std::chrono::system_clock>::max());
  EXPECT_GE(t.ToChronoDuration(), std::chrono::nanoseconds::max());
}

TEST(KernelTimeout, DurationMaxNanos) {
  // Duration >= kMaxNanos should behave as no timeout.
  KernelTimeout t(absl::Nanoseconds(std::numeric_limits<int64_t>::max()));
  EXPECT_FALSE(t.has_timeout());
  // Callers are expected to check has_timeout() instead of using the methods
  // below, but we do try to do something reasonable if they don't. We may not
  // be able to round-trip back to absl::InfiniteDuration() or
  // absl::InfiniteFuture(), but we should return a very large value.
  EXPECT_GT(absl::TimeFromTimespec(t.MakeAbsTimespec()),
            absl::Now() + absl::Hours(100000));
#ifndef _WIN32
  EXPECT_GT(absl::TimeFromTimespec(t.MakeClockAbsoluteTimespec(CLOCK_REALTIME)),
            absl::Now() + absl::Hours(100000));
#endif
  EXPECT_GT(absl::DurationFromTimespec(t.MakeRelativeTimespec()),
            absl::Hours(100000));
  EXPECT_GT(absl::FromUnixNanos(t.MakeAbsNanos()),
            absl::Now() + absl::Hours(100000));
  EXPECT_EQ(t.InMillisecondsFromNow(),
            std::numeric_limits<KernelTimeout::DWord>::max());
  EXPECT_EQ(t.ToChronoTimePoint(),
            std::chrono::time_point<std::chrono::system_clock>::max());
  EXPECT_GE(t.ToChronoDuration(), std::chrono::nanoseconds::max());
}

TEST(KernelTimeout, OverflowNanos) {
  // Test what happens when KernelTimeout is constructed with an absl::Duration
  // that would overflow now_nanos + duration.
  int64_t now_nanos = absl::ToUnixNanos(absl::Now());
  int64_t limit = std::numeric_limits<int64_t>::max() - now_nanos;
  absl::Duration duration = absl::Nanoseconds(limit) + absl::Seconds(1);
  KernelTimeout t(duration);
  // Timeouts should still be far in the future.
  EXPECT_GT(absl::TimeFromTimespec(t.MakeAbsTimespec()),
            absl::Now() + absl::Hours(100000));
#ifndef _WIN32
  EXPECT_GT(absl::TimeFromTimespec(t.MakeClockAbsoluteTimespec(CLOCK_REALTIME)),
            absl::Now() + absl::Hours(100000));
#endif
  EXPECT_GT(absl::DurationFromTimespec(t.MakeRelativeTimespec()),
            absl::Hours(100000));
  EXPECT_GT(absl::FromUnixNanos(t.MakeAbsNanos()),
            absl::Now() + absl::Hours(100000));
  EXPECT_LE(absl::Milliseconds(t.InMillisecondsFromNow()) - duration,
            absl::Milliseconds(5));
  EXPECT_GT(t.ToChronoTimePoint(),
            std::chrono::system_clock::now() + std::chrono::hours(100000));
  EXPECT_GT(t.ToChronoDuration(), std::chrono::hours(100000));
}

}  // namespace
