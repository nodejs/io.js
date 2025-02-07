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

#include "absl/time/time.h"
#include "absl/time/civil_time.h"

#if defined(_MSC_VER)
#include <winsock2.h>  // for timeval
#endif

#include "absl/base/config.h"

// For feature testing and determining which headers can be included.
#if ABSL_INTERNAL_CPLUSPLUS_LANG >= 202002L
#include <version>
#endif

#include <chrono>  // NOLINT(build/c++11)
#ifdef __cpp_lib_three_way_comparison
#include <compare>
#endif  // __cpp_lib_three_way_comparison
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <ios>
#include <limits>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/numeric/int128.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "absl/time/internal/test_util.h"

namespace {

#if defined(GTEST_USES_SIMPLE_RE) && GTEST_USES_SIMPLE_RE
const char kZoneAbbrRE[] = ".*";  // just punt
#else
const char kZoneAbbrRE[] = "[A-Za-z]{3,4}|[-+][0-9]{2}([0-9]{2})?";
#endif

// This helper is a macro so that failed expectations show up with the
// correct line numbers.
#define EXPECT_CIVIL_INFO(ci, y, m, d, h, min, s, off, isdst)      \
  do {                                                             \
    EXPECT_EQ(y, ci.cs.year());                                    \
    EXPECT_EQ(m, ci.cs.month());                                   \
    EXPECT_EQ(d, ci.cs.day());                                     \
    EXPECT_EQ(h, ci.cs.hour());                                    \
    EXPECT_EQ(min, ci.cs.minute());                                \
    EXPECT_EQ(s, ci.cs.second());                                  \
    EXPECT_EQ(off, ci.offset);                                     \
    EXPECT_EQ(isdst, ci.is_dst);                                   \
    EXPECT_THAT(ci.zone_abbr, testing::MatchesRegex(kZoneAbbrRE)); \
  } while (0)

// A gMock matcher to match timespec values. Use this matcher like:
// timespec ts1, ts2;
// EXPECT_THAT(ts1, TimespecMatcher(ts2));
MATCHER_P(TimespecMatcher, ts, "") {
  if (ts.tv_sec == arg.tv_sec && ts.tv_nsec == arg.tv_nsec) return true;
  *result_listener << "expected: {" << ts.tv_sec << ", " << ts.tv_nsec << "} ";
  *result_listener << "actual: {" << arg.tv_sec << ", " << arg.tv_nsec << "}";
  return false;
}

// A gMock matcher to match timeval values. Use this matcher like:
// timeval tv1, tv2;
// EXPECT_THAT(tv1, TimevalMatcher(tv2));
MATCHER_P(TimevalMatcher, tv, "") {
  if (tv.tv_sec == arg.tv_sec && tv.tv_usec == arg.tv_usec) return true;
  *result_listener << "expected: {" << tv.tv_sec << ", " << tv.tv_usec << "} ";
  *result_listener << "actual: {" << arg.tv_sec << ", " << arg.tv_usec << "}";
  return false;
}

TEST(Time, ConstExpr) {
  constexpr absl::Time t0 = absl::UnixEpoch();
  static_assert(t0 == absl::UnixEpoch(), "UnixEpoch");
  constexpr absl::Time t1 = absl::InfiniteFuture();
  static_assert(t1 != absl::UnixEpoch(), "InfiniteFuture");
  constexpr absl::Time t2 = absl::InfinitePast();
  static_assert(t2 != absl::UnixEpoch(), "InfinitePast");
  constexpr absl::Time t3 = absl::FromUnixNanos(0);
  static_assert(t3 == absl::UnixEpoch(), "FromUnixNanos");
  constexpr absl::Time t4 = absl::FromUnixMicros(0);
  static_assert(t4 == absl::UnixEpoch(), "FromUnixMicros");
  constexpr absl::Time t5 = absl::FromUnixMillis(0);
  static_assert(t5 == absl::UnixEpoch(), "FromUnixMillis");
  constexpr absl::Time t6 = absl::FromUnixSeconds(0);
  static_assert(t6 == absl::UnixEpoch(), "FromUnixSeconds");
  constexpr absl::Time t7 = absl::FromTimeT(0);
  static_assert(t7 == absl::UnixEpoch(), "FromTimeT");
}

TEST(Time, ValueSemantics) {
  absl::Time a;      // Default construction
  absl::Time b = a;  // Copy construction
  EXPECT_EQ(a, b);
  absl::Time c(a);  // Copy construction (again)
  EXPECT_EQ(a, b);
  EXPECT_EQ(a, c);
  EXPECT_EQ(b, c);
  b = c;  // Assignment
  EXPECT_EQ(a, b);
  EXPECT_EQ(a, c);
  EXPECT_EQ(b, c);
}

TEST(Time, UnixEpoch) {
  const auto ci = absl::UTCTimeZone().At(absl::UnixEpoch());
  EXPECT_EQ(absl::CivilSecond(1970, 1, 1, 0, 0, 0), ci.cs);
  EXPECT_EQ(absl::ZeroDuration(), ci.subsecond);
  EXPECT_EQ(absl::Weekday::thursday, absl::GetWeekday(ci.cs));
}

TEST(Time, Breakdown) {
  absl::TimeZone tz = absl::time_internal::LoadTimeZone("America/New_York");
  absl::Time t = absl::UnixEpoch();

  // The Unix epoch as seen in NYC.
  auto ci = tz.At(t);
  EXPECT_CIVIL_INFO(ci, 1969, 12, 31, 19, 0, 0, -18000, false);
  EXPECT_EQ(absl::ZeroDuration(), ci.subsecond);
  EXPECT_EQ(absl::Weekday::wednesday, absl::GetWeekday(ci.cs));

  // Just before the epoch.
  t -= absl::Nanoseconds(1);
  ci = tz.At(t);
  EXPECT_CIVIL_INFO(ci, 1969, 12, 31, 18, 59, 59, -18000, false);
  EXPECT_EQ(absl::Nanoseconds(999999999), ci.subsecond);
  EXPECT_EQ(absl::Weekday::wednesday, absl::GetWeekday(ci.cs));

  // Some time later.
  t += absl::Hours(24) * 2735;
  t += absl::Hours(18) + absl::Minutes(30) + absl::Seconds(15) +
       absl::Nanoseconds(9);
  ci = tz.At(t);
  EXPECT_CIVIL_INFO(ci, 1977, 6, 28, 14, 30, 15, -14400, true);
  EXPECT_EQ(8, ci.subsecond / absl::Nanoseconds(1));
  EXPECT_EQ(absl::Weekday::tuesday, absl::GetWeekday(ci.cs));
}

TEST(Time, AdditiveOperators) {
  const absl::Duration d = absl::Nanoseconds(1);
  const absl::Time t0;
  const absl::Time t1 = t0 + d;

  EXPECT_EQ(d, t1 - t0);
  EXPECT_EQ(-d, t0 - t1);
  EXPECT_EQ(t0, t1 - d);

  absl::Time t(t0);
  EXPECT_EQ(t0, t);
  t += d;
  EXPECT_EQ(t0 + d, t);
  EXPECT_EQ(d, t - t0);
  t -= d;
  EXPECT_EQ(t0, t);

  // Tests overflow between subseconds and seconds.
  t = absl::UnixEpoch();
  t += absl::Milliseconds(500);
  EXPECT_EQ(absl::UnixEpoch() + absl::Milliseconds(500), t);
  t += absl::Milliseconds(600);
  EXPECT_EQ(absl::UnixEpoch() + absl::Milliseconds(1100), t);
  t -= absl::Milliseconds(600);
  EXPECT_EQ(absl::UnixEpoch() + absl::Milliseconds(500), t);
  t -= absl::Milliseconds(500);
  EXPECT_EQ(absl::UnixEpoch(), t);
}

TEST(Time, RelationalOperators) {
  constexpr absl::Time t1 = absl::FromUnixNanos(0);
  constexpr absl::Time t2 = absl::FromUnixNanos(1);
  constexpr absl::Time t3 = absl::FromUnixNanos(2);

  static_assert(absl::UnixEpoch() == t1, "");
  static_assert(t1 == t1, "");
  static_assert(t2 == t2, "");
  static_assert(t3 == t3, "");

  static_assert(t1 < t2, "");
  static_assert(t2 < t3, "");
  static_assert(t1 < t3, "");

  static_assert(t1 <= t1, "");
  static_assert(t1 <= t2, "");
  static_assert(t2 <= t2, "");
  static_assert(t2 <= t3, "");
  static_assert(t3 <= t3, "");
  static_assert(t1 <= t3, "");

  static_assert(t2 > t1, "");
  static_assert(t3 > t2, "");
  static_assert(t3 > t1, "");

  static_assert(t2 >= t2, "");
  static_assert(t2 >= t1, "");
  static_assert(t3 >= t3, "");
  static_assert(t3 >= t2, "");
  static_assert(t1 >= t1, "");
  static_assert(t3 >= t1, "");

#ifdef ABSL_INTERNAL_TIME_HAS_THREE_WAY_COMPARISON

  static_assert((t1 <=> t1) == std::strong_ordering::equal, "");
  static_assert((t2 <=> t2) == std::strong_ordering::equal, "");
  static_assert((t3 <=> t3) == std::strong_ordering::equal, "");

  static_assert((t1 <=> t2) == std::strong_ordering::less, "");
  static_assert((t2 <=> t3) == std::strong_ordering::less, "");
  static_assert((t1 <=> t3) == std::strong_ordering::less, "");

  static_assert((t2 <=> t1) == std::strong_ordering::greater, "");
  static_assert((t3 <=> t2) == std::strong_ordering::greater, "");
  static_assert((t3 <=> t1) == std::strong_ordering::greater, "");

#endif  // ABSL_INTERNAL_TIME_HAS_THREE_WAY_COMPARISON
}

TEST(Time, Infinity) {
  constexpr absl::Time ifuture = absl::InfiniteFuture();
  constexpr absl::Time ipast = absl::InfinitePast();

  static_assert(ifuture == ifuture, "");
  static_assert(ipast == ipast, "");
  static_assert(ipast < ifuture, "");
  static_assert(ifuture > ipast, "");

#ifdef ABSL_INTERNAL_TIME_HAS_THREE_WAY_COMPARISON

  static_assert((ifuture <=> ifuture) == std::strong_ordering::equal, "");
  static_assert((ipast <=> ipast) == std::strong_ordering::equal, "");
  static_assert((ipast <=> ifuture) == std::strong_ordering::less, "");
  static_assert((ifuture <=> ipast) == std::strong_ordering::greater, "");

#endif  // ABSL_INTERNAL_TIME_HAS_THREE_WAY_COMPARISON

  // Arithmetic saturates
  EXPECT_EQ(ifuture, ifuture + absl::Seconds(1));
  EXPECT_EQ(ifuture, ifuture - absl::Seconds(1));
  EXPECT_EQ(ipast, ipast + absl::Seconds(1));
  EXPECT_EQ(ipast, ipast - absl::Seconds(1));

  EXPECT_EQ(absl::InfiniteDuration(), ifuture - ifuture);
  EXPECT_EQ(absl::InfiniteDuration(), ifuture - ipast);
  EXPECT_EQ(-absl::InfiniteDuration(), ipast - ifuture);
  EXPECT_EQ(-absl::InfiniteDuration(), ipast - ipast);

  constexpr absl::Time t = absl::UnixEpoch();  // Any finite time.
  static_assert(t < ifuture, "");
  static_assert(t > ipast, "");

#ifdef ABSL_INTERNAL_TIME_HAS_THREE_WAY_COMPARISON

  static_assert((t <=> ifuture) == std::strong_ordering::less, "");
  static_assert((t <=> ipast) == std::strong_ordering::greater, "");
  static_assert((ipast <=> t) == std::strong_ordering::less, "");
  static_assert((ifuture <=> t) == std::strong_ordering::greater, "");

#endif  // ABSL_INTERNAL_TIME_HAS_THREE_WAY_COMPARISON

  EXPECT_EQ(ifuture, t + absl::InfiniteDuration());
  EXPECT_EQ(ipast, t - absl::InfiniteDuration());
}

TEST(Time, FloorConversion) {
#define TEST_FLOOR_CONVERSION(TO, FROM) \
  EXPECT_EQ(1, TO(FROM(1001)));         \
  EXPECT_EQ(1, TO(FROM(1000)));         \
  EXPECT_EQ(0, TO(FROM(999)));          \
  EXPECT_EQ(0, TO(FROM(1)));            \
  EXPECT_EQ(0, TO(FROM(0)));            \
  EXPECT_EQ(-1, TO(FROM(-1)));          \
  EXPECT_EQ(-1, TO(FROM(-999)));        \
  EXPECT_EQ(-1, TO(FROM(-1000)));       \
  EXPECT_EQ(-2, TO(FROM(-1001)));

  TEST_FLOOR_CONVERSION(absl::ToUnixMicros, absl::FromUnixNanos);
  TEST_FLOOR_CONVERSION(absl::ToUnixMillis, absl::FromUnixMicros);
  TEST_FLOOR_CONVERSION(absl::ToUnixSeconds, absl::FromUnixMillis);
  TEST_FLOOR_CONVERSION(absl::ToTimeT, absl::FromUnixMillis);

#undef TEST_FLOOR_CONVERSION

  // Tests ToUnixNanos.
  EXPECT_EQ(1, absl::ToUnixNanos(absl::UnixEpoch() + absl::Nanoseconds(3) / 2));
  EXPECT_EQ(1, absl::ToUnixNanos(absl::UnixEpoch() + absl::Nanoseconds(1)));
  EXPECT_EQ(0, absl::ToUnixNanos(absl::UnixEpoch() + absl::Nanoseconds(1) / 2));
  EXPECT_EQ(0, absl::ToUnixNanos(absl::UnixEpoch() + absl::ZeroDuration()));
  EXPECT_EQ(-1,
            absl::ToUnixNanos(absl::UnixEpoch() - absl::Nanoseconds(1) / 2));
  EXPECT_EQ(-1, absl::ToUnixNanos(absl::UnixEpoch() - absl::Nanoseconds(1)));
  EXPECT_EQ(-2,
            absl::ToUnixNanos(absl::UnixEpoch() - absl::Nanoseconds(3) / 2));

  // Tests ToUniversal, which uses a different epoch than the tests above.
  EXPECT_EQ(1,
            absl::ToUniversal(absl::UniversalEpoch() + absl::Nanoseconds(101)));
  EXPECT_EQ(1,
            absl::ToUniversal(absl::UniversalEpoch() + absl::Nanoseconds(100)));
  EXPECT_EQ(0,
            absl::ToUniversal(absl::UniversalEpoch() + absl::Nanoseconds(99)));
  EXPECT_EQ(0,
            absl::ToUniversal(absl::UniversalEpoch() + absl::Nanoseconds(1)));
  EXPECT_EQ(0,
            absl::ToUniversal(absl::UniversalEpoch() + absl::ZeroDuration()));
  EXPECT_EQ(-1,
            absl::ToUniversal(absl::UniversalEpoch() + absl::Nanoseconds(-1)));
  EXPECT_EQ(-1,
            absl::ToUniversal(absl::UniversalEpoch() + absl::Nanoseconds(-99)));
  EXPECT_EQ(
      -1, absl::ToUniversal(absl::UniversalEpoch() + absl::Nanoseconds(-100)));
  EXPECT_EQ(
      -2, absl::ToUniversal(absl::UniversalEpoch() + absl::Nanoseconds(-101)));

  // Tests ToTimespec()/TimeFromTimespec()
  const struct {
    absl::Time t;
    timespec ts;
  } to_ts[] = {
      {absl::FromUnixSeconds(1) + absl::Nanoseconds(1), {1, 1}},
      {absl::FromUnixSeconds(1) + absl::Nanoseconds(1) / 2, {1, 0}},
      {absl::FromUnixSeconds(1) + absl::ZeroDuration(), {1, 0}},
      {absl::FromUnixSeconds(0) + absl::ZeroDuration(), {0, 0}},
      {absl::FromUnixSeconds(0) - absl::Nanoseconds(1) / 2, {-1, 999999999}},
      {absl::FromUnixSeconds(0) - absl::Nanoseconds(1), {-1, 999999999}},
      {absl::FromUnixSeconds(-1) + absl::Nanoseconds(1), {-1, 1}},
      {absl::FromUnixSeconds(-1) + absl::Nanoseconds(1) / 2, {-1, 0}},
      {absl::FromUnixSeconds(-1) + absl::ZeroDuration(), {-1, 0}},
      {absl::FromUnixSeconds(-1) - absl::Nanoseconds(1) / 2, {-2, 999999999}},
  };
  for (const auto& test : to_ts) {
    EXPECT_THAT(absl::ToTimespec(test.t), TimespecMatcher(test.ts));
  }
  const struct {
    timespec ts;
    absl::Time t;
  } from_ts[] = {
      {{1, 1}, absl::FromUnixSeconds(1) + absl::Nanoseconds(1)},
      {{1, 0}, absl::FromUnixSeconds(1) + absl::ZeroDuration()},
      {{0, 0}, absl::FromUnixSeconds(0) + absl::ZeroDuration()},
      {{0, -1}, absl::FromUnixSeconds(0) - absl::Nanoseconds(1)},
      {{-1, 999999999}, absl::FromUnixSeconds(0) - absl::Nanoseconds(1)},
      {{-1, 1}, absl::FromUnixSeconds(-1) + absl::Nanoseconds(1)},
      {{-1, 0}, absl::FromUnixSeconds(-1) + absl::ZeroDuration()},
      {{-1, -1}, absl::FromUnixSeconds(-1) - absl::Nanoseconds(1)},
      {{-2, 999999999}, absl::FromUnixSeconds(-1) - absl::Nanoseconds(1)},
  };
  for (const auto& test : from_ts) {
    EXPECT_EQ(test.t, absl::TimeFromTimespec(test.ts));
  }

  // Tests  absl::ToTimeval()/TimeFromTimeval() (same as timespec above)
  const struct {
    absl::Time t;
    timeval tv;
  } to_tv[] = {
      {absl::FromUnixSeconds(1) + absl::Microseconds(1), {1, 1}},
      {absl::FromUnixSeconds(1) + absl::Microseconds(1) / 2, {1, 0}},
      {absl::FromUnixSeconds(1) + absl::ZeroDuration(), {1, 0}},
      {absl::FromUnixSeconds(0) + absl::ZeroDuration(), {0, 0}},
      {absl::FromUnixSeconds(0) - absl::Microseconds(1) / 2, {-1, 999999}},
      {absl::FromUnixSeconds(0) - absl::Microseconds(1), {-1, 999999}},
      {absl::FromUnixSeconds(-1) + absl::Microseconds(1), {-1, 1}},
      {absl::FromUnixSeconds(-1) + absl::Microseconds(1) / 2, {-1, 0}},
      {absl::FromUnixSeconds(-1) + absl::ZeroDuration(), {-1, 0}},
      {absl::FromUnixSeconds(-1) - absl::Microseconds(1) / 2, {-2, 999999}},
  };
  for (const auto& test : to_tv) {
    EXPECT_THAT(absl::ToTimeval(test.t), TimevalMatcher(test.tv));
  }
  const struct {
    timeval tv;
    absl::Time t;
  } from_tv[] = {
      {{1, 1}, absl::FromUnixSeconds(1) + absl::Microseconds(1)},
      {{1, 0}, absl::FromUnixSeconds(1) + absl::ZeroDuration()},
      {{0, 0}, absl::FromUnixSeconds(0) + absl::ZeroDuration()},
      {{0, -1}, absl::FromUnixSeconds(0) - absl::Microseconds(1)},
      {{-1, 999999}, absl::FromUnixSeconds(0) - absl::Microseconds(1)},
      {{-1, 1}, absl::FromUnixSeconds(-1) + absl::Microseconds(1)},
      {{-1, 0}, absl::FromUnixSeconds(-1) + absl::ZeroDuration()},
      {{-1, -1}, absl::FromUnixSeconds(-1) - absl::Microseconds(1)},
      {{-2, 999999}, absl::FromUnixSeconds(-1) - absl::Microseconds(1)},
  };
  for (const auto& test : from_tv) {
    EXPECT_EQ(test.t, absl::TimeFromTimeval(test.tv));
  }

  // Tests flooring near negative infinity.
  const int64_t min_plus_1 = std::numeric_limits<int64_t>::min() + 1;
  EXPECT_EQ(min_plus_1, absl::ToUnixSeconds(absl::FromUnixSeconds(min_plus_1)));
  EXPECT_EQ(std::numeric_limits<int64_t>::min(),
            absl::ToUnixSeconds(absl::FromUnixSeconds(min_plus_1) -
                                absl::Nanoseconds(1) / 2));

  // Tests flooring near positive infinity.
  EXPECT_EQ(std::numeric_limits<int64_t>::max(),
            absl::ToUnixSeconds(
                absl::FromUnixSeconds(std::numeric_limits<int64_t>::max()) +
                absl::Nanoseconds(1) / 2));
  EXPECT_EQ(std::numeric_limits<int64_t>::max(),
            absl::ToUnixSeconds(
                absl::FromUnixSeconds(std::numeric_limits<int64_t>::max())));
  EXPECT_EQ(std::numeric_limits<int64_t>::max() - 1,
            absl::ToUnixSeconds(
                absl::FromUnixSeconds(std::numeric_limits<int64_t>::max()) -
                absl::Nanoseconds(1) / 2));
}

TEST(Time, RoundtripConversion) {
#define TEST_CONVERSION_ROUND_TRIP(SOURCE, FROM, TO, MATCHER) \
  EXPECT_THAT(TO(FROM(SOURCE)), MATCHER(SOURCE))

  // FromUnixNanos() and ToUnixNanos()
  int64_t now_ns = absl::GetCurrentTimeNanos();
  TEST_CONVERSION_ROUND_TRIP(-1, absl::FromUnixNanos, absl::ToUnixNanos,
                             testing::Eq);
  TEST_CONVERSION_ROUND_TRIP(0, absl::FromUnixNanos, absl::ToUnixNanos,
                             testing::Eq);
  TEST_CONVERSION_ROUND_TRIP(1, absl::FromUnixNanos, absl::ToUnixNanos,
                             testing::Eq);
  TEST_CONVERSION_ROUND_TRIP(now_ns, absl::FromUnixNanos, absl::ToUnixNanos,
                             testing::Eq)
      << now_ns;

  // FromUnixMicros() and ToUnixMicros()
  int64_t now_us = absl::GetCurrentTimeNanos() / 1000;
  TEST_CONVERSION_ROUND_TRIP(-1, absl::FromUnixMicros, absl::ToUnixMicros,
                             testing::Eq);
  TEST_CONVERSION_ROUND_TRIP(0, absl::FromUnixMicros, absl::ToUnixMicros,
                             testing::Eq);
  TEST_CONVERSION_ROUND_TRIP(1, absl::FromUnixMicros, absl::ToUnixMicros,
                             testing::Eq);
  TEST_CONVERSION_ROUND_TRIP(now_us, absl::FromUnixMicros, absl::ToUnixMicros,
                             testing::Eq)
      << now_us;

  // FromUnixMillis() and ToUnixMillis()
  int64_t now_ms = absl::GetCurrentTimeNanos() / 1000000;
  TEST_CONVERSION_ROUND_TRIP(-1, absl::FromUnixMillis, absl::ToUnixMillis,
                             testing::Eq);
  TEST_CONVERSION_ROUND_TRIP(0, absl::FromUnixMillis, absl::ToUnixMillis,
                             testing::Eq);
  TEST_CONVERSION_ROUND_TRIP(1, absl::FromUnixMillis, absl::ToUnixMillis,
                             testing::Eq);
  TEST_CONVERSION_ROUND_TRIP(now_ms, absl::FromUnixMillis, absl::ToUnixMillis,
                             testing::Eq)
      << now_ms;

  // FromUnixSeconds() and ToUnixSeconds()
  int64_t now_s = std::time(nullptr);
  TEST_CONVERSION_ROUND_TRIP(-1, absl::FromUnixSeconds, absl::ToUnixSeconds,
                             testing::Eq);
  TEST_CONVERSION_ROUND_TRIP(0, absl::FromUnixSeconds, absl::ToUnixSeconds,
                             testing::Eq);
  TEST_CONVERSION_ROUND_TRIP(1, absl::FromUnixSeconds, absl::ToUnixSeconds,
                             testing::Eq);
  TEST_CONVERSION_ROUND_TRIP(now_s, absl::FromUnixSeconds, absl::ToUnixSeconds,
                             testing::Eq)
      << now_s;

  // FromTimeT() and ToTimeT()
  time_t now_time_t = std::time(nullptr);
  TEST_CONVERSION_ROUND_TRIP(-1, absl::FromTimeT, absl::ToTimeT, testing::Eq);
  TEST_CONVERSION_ROUND_TRIP(0, absl::FromTimeT, absl::ToTimeT, testing::Eq);
  TEST_CONVERSION_ROUND_TRIP(1, absl::FromTimeT, absl::ToTimeT, testing::Eq);
  TEST_CONVERSION_ROUND_TRIP(now_time_t, absl::FromTimeT, absl::ToTimeT,
                             testing::Eq)
      << now_time_t;

  // TimeFromTimeval() and  absl::ToTimeval()
  timeval tv;
  tv.tv_sec = -1;
  tv.tv_usec = 0;
  TEST_CONVERSION_ROUND_TRIP(tv, absl::TimeFromTimeval, absl::ToTimeval,
                             TimevalMatcher);
  tv.tv_sec = -1;
  tv.tv_usec = 999999;
  TEST_CONVERSION_ROUND_TRIP(tv, absl::TimeFromTimeval, absl::ToTimeval,
                             TimevalMatcher);
  tv.tv_sec = 0;
  tv.tv_usec = 0;
  TEST_CONVERSION_ROUND_TRIP(tv, absl::TimeFromTimeval, absl::ToTimeval,
                             TimevalMatcher);
  tv.tv_sec = 0;
  tv.tv_usec = 1;
  TEST_CONVERSION_ROUND_TRIP(tv, absl::TimeFromTimeval, absl::ToTimeval,
                             TimevalMatcher);
  tv.tv_sec = 1;
  tv.tv_usec = 0;
  TEST_CONVERSION_ROUND_TRIP(tv, absl::TimeFromTimeval, absl::ToTimeval,
                             TimevalMatcher);

  // TimeFromTimespec() and ToTimespec()
  timespec ts;
  ts.tv_sec = -1;
  ts.tv_nsec = 0;
  TEST_CONVERSION_ROUND_TRIP(ts, absl::TimeFromTimespec, absl::ToTimespec,
                             TimespecMatcher);
  ts.tv_sec = -1;
  ts.tv_nsec = 999999999;
  TEST_CONVERSION_ROUND_TRIP(ts, absl::TimeFromTimespec, absl::ToTimespec,
                             TimespecMatcher);
  ts.tv_sec = 0;
  ts.tv_nsec = 0;
  TEST_CONVERSION_ROUND_TRIP(ts, absl::TimeFromTimespec, absl::ToTimespec,
                             TimespecMatcher);
  ts.tv_sec = 0;
  ts.tv_nsec = 1;
  TEST_CONVERSION_ROUND_TRIP(ts, absl::TimeFromTimespec, absl::ToTimespec,
                             TimespecMatcher);
  ts.tv_sec = 1;
  ts.tv_nsec = 0;
  TEST_CONVERSION_ROUND_TRIP(ts, absl::TimeFromTimespec, absl::ToTimespec,
                             TimespecMatcher);

  // FromUDate() and ToUDate()
  double now_ud = absl::GetCurrentTimeNanos() / 1000000;
  TEST_CONVERSION_ROUND_TRIP(-1.5, absl::FromUDate, absl::ToUDate,
                             testing::DoubleEq);
  TEST_CONVERSION_ROUND_TRIP(-1, absl::FromUDate, absl::ToUDate,
                             testing::DoubleEq);
  TEST_CONVERSION_ROUND_TRIP(-0.5, absl::FromUDate, absl::ToUDate,
                             testing::DoubleEq);
  TEST_CONVERSION_ROUND_TRIP(0, absl::FromUDate, absl::ToUDate,
                             testing::DoubleEq);
  TEST_CONVERSION_ROUND_TRIP(0.5, absl::FromUDate, absl::ToUDate,
                             testing::DoubleEq);
  TEST_CONVERSION_ROUND_TRIP(1, absl::FromUDate, absl::ToUDate,
                             testing::DoubleEq);
  TEST_CONVERSION_ROUND_TRIP(1.5, absl::FromUDate, absl::ToUDate,
                             testing::DoubleEq);
  TEST_CONVERSION_ROUND_TRIP(now_ud, absl::FromUDate, absl::ToUDate,
                             testing::DoubleEq)
      << std::fixed << std::setprecision(17) << now_ud;

  // FromUniversal() and ToUniversal()
  int64_t now_uni = ((719162LL * (24 * 60 * 60)) * (1000 * 1000 * 10)) +
                    (absl::GetCurrentTimeNanos() / 100);
  TEST_CONVERSION_ROUND_TRIP(-1, absl::FromUniversal, absl::ToUniversal,
                             testing::Eq);
  TEST_CONVERSION_ROUND_TRIP(0, absl::FromUniversal, absl::ToUniversal,
                             testing::Eq);
  TEST_CONVERSION_ROUND_TRIP(1, absl::FromUniversal, absl::ToUniversal,
                             testing::Eq);
  TEST_CONVERSION_ROUND_TRIP(now_uni, absl::FromUniversal, absl::ToUniversal,
                             testing::Eq)
      << now_uni;

#undef TEST_CONVERSION_ROUND_TRIP
}

template <typename Duration>
std::chrono::system_clock::time_point MakeChronoUnixTime(const Duration& d) {
  return std::chrono::system_clock::from_time_t(0) + d;
}

TEST(Time, FromChrono) {
  EXPECT_EQ(absl::FromTimeT(-1),
            absl::FromChrono(std::chrono::system_clock::from_time_t(-1)));
  EXPECT_EQ(absl::FromTimeT(0),
            absl::FromChrono(std::chrono::system_clock::from_time_t(0)));
  EXPECT_EQ(absl::FromTimeT(1),
            absl::FromChrono(std::chrono::system_clock::from_time_t(1)));

  EXPECT_EQ(
      absl::FromUnixMillis(-1),
      absl::FromChrono(MakeChronoUnixTime(std::chrono::milliseconds(-1))));
  EXPECT_EQ(absl::FromUnixMillis(0),
            absl::FromChrono(MakeChronoUnixTime(std::chrono::milliseconds(0))));
  EXPECT_EQ(absl::FromUnixMillis(1),
            absl::FromChrono(MakeChronoUnixTime(std::chrono::milliseconds(1))));

  // Chrono doesn't define exactly its range and precision (neither does
  // absl::Time), so let's simply test +/- ~100 years to make sure things work.
  const auto century_sec = 60 * 60 * 24 * 365 * int64_t{100};
  const auto century = std::chrono::seconds(century_sec);
  const auto chrono_future = MakeChronoUnixTime(century);
  const auto chrono_past = MakeChronoUnixTime(-century);
  EXPECT_EQ(absl::FromUnixSeconds(century_sec),
            absl::FromChrono(chrono_future));
  EXPECT_EQ(absl::FromUnixSeconds(-century_sec), absl::FromChrono(chrono_past));

  // Roundtrip them both back to chrono.
  EXPECT_EQ(chrono_future,
            absl::ToChronoTime(absl::FromUnixSeconds(century_sec)));
  EXPECT_EQ(chrono_past,
            absl::ToChronoTime(absl::FromUnixSeconds(-century_sec)));
}

TEST(Time, ToChronoTime) {
  EXPECT_EQ(std::chrono::system_clock::from_time_t(-1),
            absl::ToChronoTime(absl::FromTimeT(-1)));
  EXPECT_EQ(std::chrono::system_clock::from_time_t(0),
            absl::ToChronoTime(absl::FromTimeT(0)));
  EXPECT_EQ(std::chrono::system_clock::from_time_t(1),
            absl::ToChronoTime(absl::FromTimeT(1)));

  EXPECT_EQ(MakeChronoUnixTime(std::chrono::milliseconds(-1)),
            absl::ToChronoTime(absl::FromUnixMillis(-1)));
  EXPECT_EQ(MakeChronoUnixTime(std::chrono::milliseconds(0)),
            absl::ToChronoTime(absl::FromUnixMillis(0)));
  EXPECT_EQ(MakeChronoUnixTime(std::chrono::milliseconds(1)),
            absl::ToChronoTime(absl::FromUnixMillis(1)));

  // Time before the Unix epoch should floor, not trunc.
  const auto tick = absl::Nanoseconds(1) / 4;
  EXPECT_EQ(std::chrono::system_clock::from_time_t(0) -
                std::chrono::system_clock::duration(1),
            absl::ToChronoTime(absl::UnixEpoch() - tick));
}

// Check that absl::int128 works as a std::chrono::duration representation.
TEST(Time, Chrono128) {
  // Define a std::chrono::time_point type whose time[sic]_since_epoch() is
  // a signed 128-bit count of attoseconds. This has a range and resolution
  // (currently) beyond those of absl::Time, and undoubtedly also beyond those
  // of std::chrono::system_clock::time_point.
  //
  // Note: The to/from-chrono support should probably be updated to handle
  // such wide representations.
  using Timestamp =
      std::chrono::time_point<std::chrono::system_clock,
                              std::chrono::duration<absl::int128, std::atto>>;

  // Expect that we can round-trip the std::chrono::system_clock::time_point
  // extremes through both absl::Time and Timestamp, and that Timestamp can
  // handle the (current) absl::Time extremes.
  //
  // Note: We should use std::chrono::floor() instead of time_point_cast(),
  // but floor() is only available since c++17.
  for (const auto tp : {std::chrono::system_clock::time_point::min(),
                        std::chrono::system_clock::time_point::max()}) {
    EXPECT_EQ(tp, absl::ToChronoTime(absl::FromChrono(tp)));
    EXPECT_EQ(tp, std::chrono::time_point_cast<
                      std::chrono::system_clock::time_point::duration>(
                      std::chrono::time_point_cast<Timestamp::duration>(tp)));
  }
  Timestamp::duration::rep v = std::numeric_limits<int64_t>::min();
  v *= Timestamp::duration::period::den;
  auto ts = Timestamp(Timestamp::duration(v));
  ts += std::chrono::duration<int64_t, std::atto>(0);
  EXPECT_EQ(std::numeric_limits<int64_t>::min(),
            ts.time_since_epoch().count() / Timestamp::duration::period::den);
  EXPECT_EQ(0,
            ts.time_since_epoch().count() % Timestamp::duration::period::den);
  v = std::numeric_limits<int64_t>::max();
  v *= Timestamp::duration::period::den;
  ts = Timestamp(Timestamp::duration(v));
  ts += std::chrono::duration<int64_t, std::atto>(999999999750000000);
  EXPECT_EQ(std::numeric_limits<int64_t>::max(),
            ts.time_since_epoch().count() / Timestamp::duration::period::den);
  EXPECT_EQ(999999999750000000,
            ts.time_since_epoch().count() % Timestamp::duration::period::den);
}

TEST(Time, TimeZoneAt) {
  const absl::TimeZone nyc =
      absl::time_internal::LoadTimeZone("America/New_York");
  const std::string fmt = "%a, %e %b %Y %H:%M:%S %z (%Z)";

  // A non-transition where the civil time is unique.
  absl::CivilSecond nov01(2013, 11, 1, 8, 30, 0);
  const auto nov01_ci = nyc.At(nov01);
  EXPECT_EQ(absl::TimeZone::TimeInfo::UNIQUE, nov01_ci.kind);
  EXPECT_EQ("Fri,  1 Nov 2013 08:30:00 -0400 (EDT)",
            absl::FormatTime(fmt, nov01_ci.pre, nyc));
  EXPECT_EQ(nov01_ci.pre, nov01_ci.trans);
  EXPECT_EQ(nov01_ci.pre, nov01_ci.post);
  EXPECT_EQ(nov01_ci.pre, absl::FromCivil(nov01, nyc));

  // A Spring DST transition, when there is a gap in civil time
  // and we prefer the later of the possible interpretations of a
  // non-existent time.
  absl::CivilSecond mar13(2011, 3, 13, 2, 15, 0);
  const auto mar_ci = nyc.At(mar13);
  EXPECT_EQ(absl::TimeZone::TimeInfo::SKIPPED, mar_ci.kind);
  EXPECT_EQ("Sun, 13 Mar 2011 03:15:00 -0400 (EDT)",
            absl::FormatTime(fmt, mar_ci.pre, nyc));
  EXPECT_EQ("Sun, 13 Mar 2011 03:00:00 -0400 (EDT)",
            absl::FormatTime(fmt, mar_ci.trans, nyc));
  EXPECT_EQ("Sun, 13 Mar 2011 01:15:00 -0500 (EST)",
            absl::FormatTime(fmt, mar_ci.post, nyc));
  EXPECT_EQ(mar_ci.trans, absl::FromCivil(mar13, nyc));

  // A Fall DST transition, when civil times are repeated and
  // we prefer the earlier of the possible interpretations of an
  // ambiguous time.
  absl::CivilSecond nov06(2011, 11, 6, 1, 15, 0);
  const auto nov06_ci = nyc.At(nov06);
  EXPECT_EQ(absl::TimeZone::TimeInfo::REPEATED, nov06_ci.kind);
  EXPECT_EQ("Sun,  6 Nov 2011 01:15:00 -0400 (EDT)",
            absl::FormatTime(fmt, nov06_ci.pre, nyc));
  EXPECT_EQ("Sun,  6 Nov 2011 01:00:00 -0500 (EST)",
            absl::FormatTime(fmt, nov06_ci.trans, nyc));
  EXPECT_EQ("Sun,  6 Nov 2011 01:15:00 -0500 (EST)",
            absl::FormatTime(fmt, nov06_ci.post, nyc));
  EXPECT_EQ(nov06_ci.pre, absl::FromCivil(nov06, nyc));

  // Check that (time_t) -1 is handled correctly.
  absl::CivilSecond minus1(1969, 12, 31, 18, 59, 59);
  const auto minus1_cl = nyc.At(minus1);
  EXPECT_EQ(absl::TimeZone::TimeInfo::UNIQUE, minus1_cl.kind);
  EXPECT_EQ(-1, absl::ToTimeT(minus1_cl.pre));
  EXPECT_EQ("Wed, 31 Dec 1969 18:59:59 -0500 (EST)",
            absl::FormatTime(fmt, minus1_cl.pre, nyc));
  EXPECT_EQ("Wed, 31 Dec 1969 23:59:59 +0000 (UTC)",
            absl::FormatTime(fmt, minus1_cl.pre, absl::UTCTimeZone()));
}

// FromCivil(CivilSecond(year, mon, day, hour, min, sec), UTCTimeZone())
// has a specialized fastpath implementation, which we exercise here.
TEST(Time, FromCivilUTC) {
  const absl::TimeZone utc = absl::UTCTimeZone();
  const std::string fmt = "%a, %e %b %Y %H:%M:%S %z (%Z)";
  const int kMax = std::numeric_limits<int>::max();
  const int kMin = std::numeric_limits<int>::min();
  absl::Time t;

  // 292091940881 is the last positive year to use the fastpath.
  t = absl::FromCivil(
      absl::CivilSecond(292091940881, kMax, kMax, kMax, kMax, kMax), utc);
  EXPECT_EQ("Fri, 25 Nov 292277026596 12:21:07 +0000 (UTC)",
            absl::FormatTime(fmt, t, utc));
  t = absl::FromCivil(
      absl::CivilSecond(292091940882, kMax, kMax, kMax, kMax, kMax), utc);
  EXPECT_EQ("infinite-future", absl::FormatTime(fmt, t, utc));  // no overflow

  // -292091936940 is the last negative year to use the fastpath.
  t = absl::FromCivil(
      absl::CivilSecond(-292091936940, kMin, kMin, kMin, kMin, kMin), utc);
  EXPECT_EQ("Fri,  1 Nov -292277022657 10:37:52 +0000 (UTC)",
            absl::FormatTime(fmt, t, utc));
  t = absl::FromCivil(
      absl::CivilSecond(-292091936941, kMin, kMin, kMin, kMin, kMin), utc);
  EXPECT_EQ("infinite-past", absl::FormatTime(fmt, t, utc));  // no underflow

  // Check that we're counting leap years correctly.
  t = absl::FromCivil(absl::CivilSecond(1900, 2, 28, 23, 59, 59), utc);
  EXPECT_EQ("Wed, 28 Feb 1900 23:59:59 +0000 (UTC)",
            absl::FormatTime(fmt, t, utc));
  t = absl::FromCivil(absl::CivilSecond(1900, 3, 1, 0, 0, 0), utc);
  EXPECT_EQ("Thu,  1 Mar 1900 00:00:00 +0000 (UTC)",
            absl::FormatTime(fmt, t, utc));
  t = absl::FromCivil(absl::CivilSecond(2000, 2, 29, 23, 59, 59), utc);
  EXPECT_EQ("Tue, 29 Feb 2000 23:59:59 +0000 (UTC)",
            absl::FormatTime(fmt, t, utc));
  t = absl::FromCivil(absl::CivilSecond(2000, 3, 1, 0, 0, 0), utc);
  EXPECT_EQ("Wed,  1 Mar 2000 00:00:00 +0000 (UTC)",
            absl::FormatTime(fmt, t, utc));
}

TEST(Time, ToTM) {
  const absl::TimeZone utc = absl::UTCTimeZone();

  // Compares the results of absl::ToTM() to gmtime_r() for lots of times over
  // the course of a few days.
  const absl::Time start =
      absl::FromCivil(absl::CivilSecond(2014, 1, 2, 3, 4, 5), utc);
  const absl::Time end =
      absl::FromCivil(absl::CivilSecond(2014, 1, 5, 3, 4, 5), utc);
  for (absl::Time t = start; t < end; t += absl::Seconds(30)) {
    const struct tm tm_bt = absl::ToTM(t, utc);
    const time_t tt = absl::ToTimeT(t);
    struct tm tm_lc;
#ifdef _WIN32
    gmtime_s(&tm_lc, &tt);
#else
    gmtime_r(&tt, &tm_lc);
#endif
    EXPECT_EQ(tm_lc.tm_year, tm_bt.tm_year);
    EXPECT_EQ(tm_lc.tm_mon, tm_bt.tm_mon);
    EXPECT_EQ(tm_lc.tm_mday, tm_bt.tm_mday);
    EXPECT_EQ(tm_lc.tm_hour, tm_bt.tm_hour);
    EXPECT_EQ(tm_lc.tm_min, tm_bt.tm_min);
    EXPECT_EQ(tm_lc.tm_sec, tm_bt.tm_sec);
    EXPECT_EQ(tm_lc.tm_wday, tm_bt.tm_wday);
    EXPECT_EQ(tm_lc.tm_yday, tm_bt.tm_yday);
    EXPECT_EQ(tm_lc.tm_isdst, tm_bt.tm_isdst);

    ASSERT_FALSE(HasFailure());
  }

  // Checks that the tm_isdst field is correct when in standard time.
  const absl::TimeZone nyc =
      absl::time_internal::LoadTimeZone("America/New_York");
  absl::Time t = absl::FromCivil(absl::CivilSecond(2014, 3, 1, 0, 0, 0), nyc);
  struct tm tm = absl::ToTM(t, nyc);
  EXPECT_FALSE(tm.tm_isdst);

  // Checks that the tm_isdst field is correct when in daylight time.
  t = absl::FromCivil(absl::CivilSecond(2014, 4, 1, 0, 0, 0), nyc);
  tm = absl::ToTM(t, nyc);
  EXPECT_TRUE(tm.tm_isdst);

  // Checks overflow.
  tm = absl::ToTM(absl::InfiniteFuture(), nyc);
  EXPECT_EQ(std::numeric_limits<int>::max() - 1900, tm.tm_year);
  EXPECT_EQ(11, tm.tm_mon);
  EXPECT_EQ(31, tm.tm_mday);
  EXPECT_EQ(23, tm.tm_hour);
  EXPECT_EQ(59, tm.tm_min);
  EXPECT_EQ(59, tm.tm_sec);
  EXPECT_EQ(4, tm.tm_wday);
  EXPECT_EQ(364, tm.tm_yday);
  EXPECT_FALSE(tm.tm_isdst);

  // Checks underflow.
  tm = absl::ToTM(absl::InfinitePast(), nyc);
  EXPECT_EQ(std::numeric_limits<int>::min(), tm.tm_year);
  EXPECT_EQ(0, tm.tm_mon);
  EXPECT_EQ(1, tm.tm_mday);
  EXPECT_EQ(0, tm.tm_hour);
  EXPECT_EQ(0, tm.tm_min);
  EXPECT_EQ(0, tm.tm_sec);
  EXPECT_EQ(0, tm.tm_wday);
  EXPECT_EQ(0, tm.tm_yday);
  EXPECT_FALSE(tm.tm_isdst);
}

TEST(Time, FromTM) {
  const absl::TimeZone nyc =
      absl::time_internal::LoadTimeZone("America/New_York");

  // Verifies that tm_isdst doesn't affect anything when the time is unique.
  struct tm tm;
  std::memset(&tm, 0, sizeof(tm));
  tm.tm_year = 2014 - 1900;
  tm.tm_mon = 6 - 1;
  tm.tm_mday = 28;
  tm.tm_hour = 1;
  tm.tm_min = 2;
  tm.tm_sec = 3;
  tm.tm_isdst = -1;
  absl::Time t = absl::FromTM(tm, nyc);
  EXPECT_EQ("2014-06-28T01:02:03-04:00", absl::FormatTime(t, nyc));  // DST
  tm.tm_isdst = 0;
  t = absl::FromTM(tm, nyc);
  EXPECT_EQ("2014-06-28T01:02:03-04:00", absl::FormatTime(t, nyc));  // DST
  tm.tm_isdst = 1;
  t = absl::FromTM(tm, nyc);
  EXPECT_EQ("2014-06-28T01:02:03-04:00", absl::FormatTime(t, nyc));  // DST

  // Adjusts tm to refer to an ambiguous time.
  tm.tm_year = 2014 - 1900;
  tm.tm_mon = 11 - 1;
  tm.tm_mday = 2;
  tm.tm_hour = 1;
  tm.tm_min = 30;
  tm.tm_sec = 42;
  tm.tm_isdst = -1;
  t = absl::FromTM(tm, nyc);
  EXPECT_EQ("2014-11-02T01:30:42-04:00", absl::FormatTime(t, nyc));  // DST
  tm.tm_isdst = 0;
  t = absl::FromTM(tm, nyc);
  EXPECT_EQ("2014-11-02T01:30:42-05:00", absl::FormatTime(t, nyc));  // STD
  tm.tm_isdst = 1;
  t = absl::FromTM(tm, nyc);
  EXPECT_EQ("2014-11-02T01:30:42-04:00", absl::FormatTime(t, nyc));  // DST

  // Adjusts tm to refer to a skipped time.
  tm.tm_year = 2014 - 1900;
  tm.tm_mon = 3 - 1;
  tm.tm_mday = 9;
  tm.tm_hour = 2;
  tm.tm_min = 30;
  tm.tm_sec = 42;
  tm.tm_isdst = -1;
  t = absl::FromTM(tm, nyc);
  EXPECT_EQ("2014-03-09T03:30:42-04:00", absl::FormatTime(t, nyc));  // DST
  tm.tm_isdst = 0;
  t = absl::FromTM(tm, nyc);
  EXPECT_EQ("2014-03-09T01:30:42-05:00", absl::FormatTime(t, nyc));  // STD
  tm.tm_isdst = 1;
  t = absl::FromTM(tm, nyc);
  EXPECT_EQ("2014-03-09T03:30:42-04:00", absl::FormatTime(t, nyc));  // DST

  // Adjusts tm to refer to a time with a year larger than 2147483647.
  tm.tm_year = 2147483647 - 1900 + 1;
  tm.tm_mon = 6 - 1;
  tm.tm_mday = 28;
  tm.tm_hour = 1;
  tm.tm_min = 2;
  tm.tm_sec = 3;
  tm.tm_isdst = -1;
  t = absl::FromTM(tm, absl::UTCTimeZone());
  EXPECT_EQ("2147483648-06-28T01:02:03+00:00",
            absl::FormatTime(t, absl::UTCTimeZone()));

  // Adjusts tm to refer to a time with a very large month.
  tm.tm_year = 2019 - 1900;
  tm.tm_mon = 2147483647;
  tm.tm_mday = 28;
  tm.tm_hour = 1;
  tm.tm_min = 2;
  tm.tm_sec = 3;
  tm.tm_isdst = -1;
  t = absl::FromTM(tm, absl::UTCTimeZone());
  EXPECT_EQ("178958989-08-28T01:02:03+00:00",
            absl::FormatTime(t, absl::UTCTimeZone()));
}

TEST(Time, TMRoundTrip) {
  const absl::TimeZone nyc =
      absl::time_internal::LoadTimeZone("America/New_York");

  // Test round-tripping across a skipped transition
  absl::Time start = absl::FromCivil(absl::CivilHour(2014, 3, 9, 0), nyc);
  absl::Time end = absl::FromCivil(absl::CivilHour(2014, 3, 9, 4), nyc);
  for (absl::Time t = start; t < end; t += absl::Minutes(1)) {
    struct tm tm = absl::ToTM(t, nyc);
    absl::Time rt = absl::FromTM(tm, nyc);
    EXPECT_EQ(rt, t);
  }

  // Test round-tripping across an ambiguous transition
  start = absl::FromCivil(absl::CivilHour(2014, 11, 2, 0), nyc);
  end = absl::FromCivil(absl::CivilHour(2014, 11, 2, 4), nyc);
  for (absl::Time t = start; t < end; t += absl::Minutes(1)) {
    struct tm tm = absl::ToTM(t, nyc);
    absl::Time rt = absl::FromTM(tm, nyc);
    EXPECT_EQ(rt, t);
  }

  // Test round-tripping of unique instants crossing a day boundary
  start = absl::FromCivil(absl::CivilHour(2014, 6, 27, 22), nyc);
  end = absl::FromCivil(absl::CivilHour(2014, 6, 28, 4), nyc);
  for (absl::Time t = start; t < end; t += absl::Minutes(1)) {
    struct tm tm = absl::ToTM(t, nyc);
    absl::Time rt = absl::FromTM(tm, nyc);
    EXPECT_EQ(rt, t);
  }
}

TEST(Time, Range) {
  // The API's documented range is +/- 100 billion years.
  const absl::Duration range = absl::Hours(24) * 365.2425 * 100000000000;

  // Arithmetic and comparison still works at +/-range around base values.
  absl::Time bases[2] = {absl::UnixEpoch(), absl::Now()};
  for (const auto base : bases) {
    absl::Time bottom = base - range;
    EXPECT_GT(bottom, bottom - absl::Nanoseconds(1));
    EXPECT_LT(bottom, bottom + absl::Nanoseconds(1));
    absl::Time top = base + range;
    EXPECT_GT(top, top - absl::Nanoseconds(1));
    EXPECT_LT(top, top + absl::Nanoseconds(1));
    absl::Duration full_range = 2 * range;
    EXPECT_EQ(full_range, top - bottom);
    EXPECT_EQ(-full_range, bottom - top);
  }
}

TEST(Time, Limits) {
  // It is an implementation detail that Time().rep_ == ZeroDuration(),
  // and that the resolution of a Duration is 1/4 of a nanosecond.
  const absl::Time zero;
  const absl::Time max =
      zero + absl::Seconds(std::numeric_limits<int64_t>::max()) +
      absl::Nanoseconds(999999999) + absl::Nanoseconds(3) / 4;
  const absl::Time min =
      zero + absl::Seconds(std::numeric_limits<int64_t>::min());

  // Some simple max/min bounds checks.
  EXPECT_LT(max, absl::InfiniteFuture());
  EXPECT_GT(min, absl::InfinitePast());
  EXPECT_LT(zero, max);
  EXPECT_GT(zero, min);
  EXPECT_GE(absl::UnixEpoch(), min);
  EXPECT_LT(absl::UnixEpoch(), max);

  // Check sign of Time differences.
  EXPECT_LT(absl::ZeroDuration(), max - zero);
  EXPECT_LT(absl::ZeroDuration(),
            zero - absl::Nanoseconds(1) / 4 - min);  // avoid zero - min

  // Arithmetic works at max - 0.25ns and min + 0.25ns.
  EXPECT_GT(max, max - absl::Nanoseconds(1) / 4);
  EXPECT_LT(min, min + absl::Nanoseconds(1) / 4);
}

TEST(Time, ConversionSaturation) {
  const absl::TimeZone utc = absl::UTCTimeZone();
  absl::Time t;

  const auto max_time_t = std::numeric_limits<time_t>::max();
  const auto min_time_t = std::numeric_limits<time_t>::min();
  time_t tt = max_time_t - 1;
  t = absl::FromTimeT(tt);
  tt = absl::ToTimeT(t);
  EXPECT_EQ(max_time_t - 1, tt);
  t += absl::Seconds(1);
  tt = absl::ToTimeT(t);
  EXPECT_EQ(max_time_t, tt);
  t += absl::Seconds(1);  // no effect
  tt = absl::ToTimeT(t);
  EXPECT_EQ(max_time_t, tt);

  tt = min_time_t + 1;
  t = absl::FromTimeT(tt);
  tt = absl::ToTimeT(t);
  EXPECT_EQ(min_time_t + 1, tt);
  t -= absl::Seconds(1);
  tt = absl::ToTimeT(t);
  EXPECT_EQ(min_time_t, tt);
  t -= absl::Seconds(1);  // no effect
  tt = absl::ToTimeT(t);
  EXPECT_EQ(min_time_t, tt);

  const auto max_timeval_sec =
      std::numeric_limits<decltype(timeval::tv_sec)>::max();
  const auto min_timeval_sec =
      std::numeric_limits<decltype(timeval::tv_sec)>::min();
  timeval tv;
  tv.tv_sec = max_timeval_sec;
  tv.tv_usec = 999998;
  t = absl::TimeFromTimeval(tv);
  tv = absl::ToTimeval(t);
  EXPECT_EQ(max_timeval_sec, tv.tv_sec);
  EXPECT_EQ(999998, tv.tv_usec);
  t += absl::Microseconds(1);
  tv = absl::ToTimeval(t);
  EXPECT_EQ(max_timeval_sec, tv.tv_sec);
  EXPECT_EQ(999999, tv.tv_usec);
  t += absl::Microseconds(1);  // no effect
  tv = absl::ToTimeval(t);
  EXPECT_EQ(max_timeval_sec, tv.tv_sec);
  EXPECT_EQ(999999, tv.tv_usec);

  tv.tv_sec = min_timeval_sec;
  tv.tv_usec = 1;
  t = absl::TimeFromTimeval(tv);
  tv = absl::ToTimeval(t);
  EXPECT_EQ(min_timeval_sec, tv.tv_sec);
  EXPECT_EQ(1, tv.tv_usec);
  t -= absl::Microseconds(1);
  tv = absl::ToTimeval(t);
  EXPECT_EQ(min_timeval_sec, tv.tv_sec);
  EXPECT_EQ(0, tv.tv_usec);
  t -= absl::Microseconds(1);  // no effect
  tv = absl::ToTimeval(t);
  EXPECT_EQ(min_timeval_sec, tv.tv_sec);
  EXPECT_EQ(0, tv.tv_usec);

  const auto max_timespec_sec =
      std::numeric_limits<decltype(timespec::tv_sec)>::max();
  const auto min_timespec_sec =
      std::numeric_limits<decltype(timespec::tv_sec)>::min();
  timespec ts;
  ts.tv_sec = max_timespec_sec;
  ts.tv_nsec = 999999998;
  t = absl::TimeFromTimespec(ts);
  ts = absl::ToTimespec(t);
  EXPECT_EQ(max_timespec_sec, ts.tv_sec);
  EXPECT_EQ(999999998, ts.tv_nsec);
  t += absl::Nanoseconds(1);
  ts = absl::ToTimespec(t);
  EXPECT_EQ(max_timespec_sec, ts.tv_sec);
  EXPECT_EQ(999999999, ts.tv_nsec);
  t += absl::Nanoseconds(1);  // no effect
  ts = absl::ToTimespec(t);
  EXPECT_EQ(max_timespec_sec, ts.tv_sec);
  EXPECT_EQ(999999999, ts.tv_nsec);

  ts.tv_sec = min_timespec_sec;
  ts.tv_nsec = 1;
  t = absl::TimeFromTimespec(ts);
  ts = absl::ToTimespec(t);
  EXPECT_EQ(min_timespec_sec, ts.tv_sec);
  EXPECT_EQ(1, ts.tv_nsec);
  t -= absl::Nanoseconds(1);
  ts = absl::ToTimespec(t);
  EXPECT_EQ(min_timespec_sec, ts.tv_sec);
  EXPECT_EQ(0, ts.tv_nsec);
  t -= absl::Nanoseconds(1);  // no effect
  ts = absl::ToTimespec(t);
  EXPECT_EQ(min_timespec_sec, ts.tv_sec);
  EXPECT_EQ(0, ts.tv_nsec);

  // Checks how TimeZone::At() saturates on infinities.
  auto ci = utc.At(absl::InfiniteFuture());
  EXPECT_CIVIL_INFO(ci, std::numeric_limits<int64_t>::max(), 12, 31, 23, 59, 59,
                    0, false);
  EXPECT_EQ(absl::InfiniteDuration(), ci.subsecond);
  EXPECT_EQ(absl::Weekday::thursday, absl::GetWeekday(ci.cs));
  EXPECT_EQ(365, absl::GetYearDay(ci.cs));
  EXPECT_STREQ("-00", ci.zone_abbr);  // artifact of TimeZone::At()
  ci = utc.At(absl::InfinitePast());
  EXPECT_CIVIL_INFO(ci, std::numeric_limits<int64_t>::min(), 1, 1, 0, 0, 0, 0,
                    false);
  EXPECT_EQ(-absl::InfiniteDuration(), ci.subsecond);
  EXPECT_EQ(absl::Weekday::sunday, absl::GetWeekday(ci.cs));
  EXPECT_EQ(1, absl::GetYearDay(ci.cs));
  EXPECT_STREQ("-00", ci.zone_abbr);  // artifact of TimeZone::At()

  // Approach the maximal Time value from below.
  t = absl::FromCivil(absl::CivilSecond(292277026596, 12, 4, 15, 30, 6), utc);
  EXPECT_EQ("292277026596-12-04T15:30:06+00:00",
            absl::FormatTime(absl::RFC3339_full, t, utc));
  t = absl::FromCivil(absl::CivilSecond(292277026596, 12, 4, 15, 30, 7), utc);
  EXPECT_EQ("292277026596-12-04T15:30:07+00:00",
            absl::FormatTime(absl::RFC3339_full, t, utc));
  EXPECT_EQ(
      absl::UnixEpoch() + absl::Seconds(std::numeric_limits<int64_t>::max()),
      t);

  // Checks that we can also get the maximal Time value for a far-east zone.
  const absl::TimeZone plus14 = absl::FixedTimeZone(14 * 60 * 60);
  t = absl::FromCivil(absl::CivilSecond(292277026596, 12, 5, 5, 30, 7), plus14);
  EXPECT_EQ("292277026596-12-05T05:30:07+14:00",
            absl::FormatTime(absl::RFC3339_full, t, plus14));
  EXPECT_EQ(
      absl::UnixEpoch() + absl::Seconds(std::numeric_limits<int64_t>::max()),
      t);

  // One second later should push us to infinity.
  t = absl::FromCivil(absl::CivilSecond(292277026596, 12, 4, 15, 30, 8), utc);
  EXPECT_EQ("infinite-future", absl::FormatTime(absl::RFC3339_full, t, utc));

  // Approach the minimal Time value from above.
  t = absl::FromCivil(absl::CivilSecond(-292277022657, 1, 27, 8, 29, 53), utc);
  EXPECT_EQ("-292277022657-01-27T08:29:53+00:00",
            absl::FormatTime(absl::RFC3339_full, t, utc));
  t = absl::FromCivil(absl::CivilSecond(-292277022657, 1, 27, 8, 29, 52), utc);
  EXPECT_EQ("-292277022657-01-27T08:29:52+00:00",
            absl::FormatTime(absl::RFC3339_full, t, utc));
  EXPECT_EQ(
      absl::UnixEpoch() + absl::Seconds(std::numeric_limits<int64_t>::min()),
      t);

  // Checks that we can also get the minimal Time value for a far-west zone.
  const absl::TimeZone minus12 = absl::FixedTimeZone(-12 * 60 * 60);
  t = absl::FromCivil(absl::CivilSecond(-292277022657, 1, 26, 20, 29, 52),
                      minus12);
  EXPECT_EQ("-292277022657-01-26T20:29:52-12:00",
            absl::FormatTime(absl::RFC3339_full, t, minus12));
  EXPECT_EQ(
      absl::UnixEpoch() + absl::Seconds(std::numeric_limits<int64_t>::min()),
      t);

  // One second before should push us to -infinity.
  t = absl::FromCivil(absl::CivilSecond(-292277022657, 1, 27, 8, 29, 51), utc);
  EXPECT_EQ("infinite-past", absl::FormatTime(absl::RFC3339_full, t, utc));
}

// In zones with POSIX-style recurring rules we use special logic to
// handle conversions in the distant future.  Here we check the limits
// of those conversions, particularly with respect to integer overflow.
TEST(Time, ExtendedConversionSaturation) {
  const absl::TimeZone syd =
      absl::time_internal::LoadTimeZone("Australia/Sydney");
  const absl::TimeZone nyc =
      absl::time_internal::LoadTimeZone("America/New_York");
  const absl::Time max =
      absl::FromUnixSeconds(std::numeric_limits<int64_t>::max());
  absl::TimeZone::CivilInfo ci;
  absl::Time t;

  // The maximal time converted in each zone.
  ci = syd.At(max);
  EXPECT_CIVIL_INFO(ci, 292277026596, 12, 5, 2, 30, 7, 39600, true);
  t = absl::FromCivil(absl::CivilSecond(292277026596, 12, 5, 2, 30, 7), syd);
  EXPECT_EQ(max, t);
  ci = nyc.At(max);
  EXPECT_CIVIL_INFO(ci, 292277026596, 12, 4, 10, 30, 7, -18000, false);
  t = absl::FromCivil(absl::CivilSecond(292277026596, 12, 4, 10, 30, 7), nyc);
  EXPECT_EQ(max, t);

  // One second later should push us to infinity.
  t = absl::FromCivil(absl::CivilSecond(292277026596, 12, 5, 2, 30, 8), syd);
  EXPECT_EQ(absl::InfiniteFuture(), t);
  t = absl::FromCivil(absl::CivilSecond(292277026596, 12, 4, 10, 30, 8), nyc);
  EXPECT_EQ(absl::InfiniteFuture(), t);

  // And we should stick there.
  t = absl::FromCivil(absl::CivilSecond(292277026596, 12, 5, 2, 30, 9), syd);
  EXPECT_EQ(absl::InfiniteFuture(), t);
  t = absl::FromCivil(absl::CivilSecond(292277026596, 12, 4, 10, 30, 9), nyc);
  EXPECT_EQ(absl::InfiniteFuture(), t);

  // All the way up to a saturated date/time, without overflow.
  t = absl::FromCivil(absl::CivilSecond::max(), syd);
  EXPECT_EQ(absl::InfiniteFuture(), t);
  t = absl::FromCivil(absl::CivilSecond::max(), nyc);
  EXPECT_EQ(absl::InfiniteFuture(), t);
}

TEST(Time, FromCivilAlignment) {
  const absl::TimeZone utc = absl::UTCTimeZone();
  const absl::CivilSecond cs(2015, 2, 3, 4, 5, 6);
  absl::Time t = absl::FromCivil(cs, utc);
  EXPECT_EQ("2015-02-03T04:05:06+00:00", absl::FormatTime(t, utc));
  t = absl::FromCivil(absl::CivilMinute(cs), utc);
  EXPECT_EQ("2015-02-03T04:05:00+00:00", absl::FormatTime(t, utc));
  t = absl::FromCivil(absl::CivilHour(cs), utc);
  EXPECT_EQ("2015-02-03T04:00:00+00:00", absl::FormatTime(t, utc));
  t = absl::FromCivil(absl::CivilDay(cs), utc);
  EXPECT_EQ("2015-02-03T00:00:00+00:00", absl::FormatTime(t, utc));
  t = absl::FromCivil(absl::CivilMonth(cs), utc);
  EXPECT_EQ("2015-02-01T00:00:00+00:00", absl::FormatTime(t, utc));
  t = absl::FromCivil(absl::CivilYear(cs), utc);
  EXPECT_EQ("2015-01-01T00:00:00+00:00", absl::FormatTime(t, utc));
}

TEST(Time, LegacyDateTime) {
  const absl::TimeZone utc = absl::UTCTimeZone();
  const std::string ymdhms = "%Y-%m-%d %H:%M:%S";
  const int kMax = std::numeric_limits<int>::max();
  const int kMin = std::numeric_limits<int>::min();
  absl::Time t;

  t = absl::FromDateTime(std::numeric_limits<absl::civil_year_t>::max(), kMax,
                         kMax, kMax, kMax, kMax, utc);
  EXPECT_EQ("infinite-future",
            absl::FormatTime(ymdhms, t, utc));  // no overflow
  t = absl::FromDateTime(std::numeric_limits<absl::civil_year_t>::min(), kMin,
                         kMin, kMin, kMin, kMin, utc);
  EXPECT_EQ("infinite-past", absl::FormatTime(ymdhms, t, utc));  // no overflow

  // Check normalization.
  EXPECT_TRUE(absl::ConvertDateTime(2013, 10, 32, 8, 30, 0, utc).normalized);
  t = absl::FromDateTime(2015, 1, 1, 0, 0, 60, utc);
  EXPECT_EQ("2015-01-01 00:01:00", absl::FormatTime(ymdhms, t, utc));
  t = absl::FromDateTime(2015, 1, 1, 0, 60, 0, utc);
  EXPECT_EQ("2015-01-01 01:00:00", absl::FormatTime(ymdhms, t, utc));
  t = absl::FromDateTime(2015, 1, 1, 24, 0, 0, utc);
  EXPECT_EQ("2015-01-02 00:00:00", absl::FormatTime(ymdhms, t, utc));
  t = absl::FromDateTime(2015, 1, 32, 0, 0, 0, utc);
  EXPECT_EQ("2015-02-01 00:00:00", absl::FormatTime(ymdhms, t, utc));
  t = absl::FromDateTime(2015, 13, 1, 0, 0, 0, utc);
  EXPECT_EQ("2016-01-01 00:00:00", absl::FormatTime(ymdhms, t, utc));
  t = absl::FromDateTime(2015, 13, 32, 60, 60, 60, utc);
  EXPECT_EQ("2016-02-03 13:01:00", absl::FormatTime(ymdhms, t, utc));
  t = absl::FromDateTime(2015, 1, 1, 0, 0, -1, utc);
  EXPECT_EQ("2014-12-31 23:59:59", absl::FormatTime(ymdhms, t, utc));
  t = absl::FromDateTime(2015, 1, 1, 0, -1, 0, utc);
  EXPECT_EQ("2014-12-31 23:59:00", absl::FormatTime(ymdhms, t, utc));
  t = absl::FromDateTime(2015, 1, 1, -1, 0, 0, utc);
  EXPECT_EQ("2014-12-31 23:00:00", absl::FormatTime(ymdhms, t, utc));
  t = absl::FromDateTime(2015, 1, -1, 0, 0, 0, utc);
  EXPECT_EQ("2014-12-30 00:00:00", absl::FormatTime(ymdhms, t, utc));
  t = absl::FromDateTime(2015, -1, 1, 0, 0, 0, utc);
  EXPECT_EQ("2014-11-01 00:00:00", absl::FormatTime(ymdhms, t, utc));
  t = absl::FromDateTime(2015, -1, -1, -1, -1, -1, utc);
  EXPECT_EQ("2014-10-29 22:58:59", absl::FormatTime(ymdhms, t, utc));
}

TEST(Time, NextTransitionUTC) {
  const auto tz = absl::UTCTimeZone();
  absl::TimeZone::CivilTransition trans;

  auto t = absl::InfinitePast();
  EXPECT_FALSE(tz.NextTransition(t, &trans));

  t = absl::InfiniteFuture();
  EXPECT_FALSE(tz.NextTransition(t, &trans));
}

TEST(Time, PrevTransitionUTC) {
  const auto tz = absl::UTCTimeZone();
  absl::TimeZone::CivilTransition trans;

  auto t = absl::InfiniteFuture();
  EXPECT_FALSE(tz.PrevTransition(t, &trans));

  t = absl::InfinitePast();
  EXPECT_FALSE(tz.PrevTransition(t, &trans));
}

TEST(Time, NextTransitionNYC) {
  const auto tz = absl::time_internal::LoadTimeZone("America/New_York");
  absl::TimeZone::CivilTransition trans;

  auto t = absl::FromCivil(absl::CivilSecond(2018, 6, 30, 0, 0, 0), tz);
  EXPECT_TRUE(tz.NextTransition(t, &trans));
  EXPECT_EQ(absl::CivilSecond(2018, 11, 4, 2, 0, 0), trans.from);
  EXPECT_EQ(absl::CivilSecond(2018, 11, 4, 1, 0, 0), trans.to);

  t = absl::InfiniteFuture();
  EXPECT_FALSE(tz.NextTransition(t, &trans));

  t = absl::InfinitePast();
  EXPECT_TRUE(tz.NextTransition(t, &trans));
  if (trans.from == absl::CivilSecond(1918, 03, 31, 2, 0, 0)) {
    // It looks like the tzdata is only 32 bit (probably macOS),
    // which bottoms out at 1901-12-13T20:45:52+00:00.
    EXPECT_EQ(absl::CivilSecond(1918, 3, 31, 3, 0, 0), trans.to);
  } else {
    EXPECT_EQ(absl::CivilSecond(1883, 11, 18, 12, 3, 58), trans.from);
    EXPECT_EQ(absl::CivilSecond(1883, 11, 18, 12, 0, 0), trans.to);
  }
}

TEST(Time, PrevTransitionNYC) {
  const auto tz = absl::time_internal::LoadTimeZone("America/New_York");
  absl::TimeZone::CivilTransition trans;

  auto t = absl::FromCivil(absl::CivilSecond(2018, 6, 30, 0, 0, 0), tz);
  EXPECT_TRUE(tz.PrevTransition(t, &trans));
  EXPECT_EQ(absl::CivilSecond(2018, 3, 11, 2, 0, 0), trans.from);
  EXPECT_EQ(absl::CivilSecond(2018, 3, 11, 3, 0, 0), trans.to);

  t = absl::InfinitePast();
  EXPECT_FALSE(tz.PrevTransition(t, &trans));

  t = absl::InfiniteFuture();
  EXPECT_TRUE(tz.PrevTransition(t, &trans));
  // We have a transition but we don't know which one.
}

TEST(Time, AbslStringify) {
  // FormatTime is already well tested, so just use one test case here to
  // verify that StrFormat("%v", t) works as expected.
  absl::Time t = absl::Now();
  EXPECT_EQ(absl::StrFormat("%v", t), absl::FormatTime(t));
}

}  // namespace
