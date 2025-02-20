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

#if defined(_MSC_VER)
#include <winsock2.h>  // for timeval
#endif

#include "absl/base/config.h"

// For feature testing and determining which headers can be included.
#if ABSL_INTERNAL_CPLUSPLUS_LANG >= 202002L
#include <version>
#endif

#include <array>
#include <cfloat>
#include <chrono>  // NOLINT(build/c++11)
#ifdef __cpp_lib_three_way_comparison
#include <compare>
#endif  // __cpp_lib_three_way_comparison
#include <cmath>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <limits>
#include <random>
#include <string>
#include <type_traits>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_format.h"
#include "absl/time/time.h"

namespace {

constexpr int64_t kint64max = std::numeric_limits<int64_t>::max();
constexpr int64_t kint64min = std::numeric_limits<int64_t>::min();

// Approximates the given number of years. This is only used to make some test
// code more readable.
absl::Duration ApproxYears(int64_t n) { return absl::Hours(n) * 365 * 24; }

// A gMock matcher to match timespec values. Use this matcher like:
// timespec ts1, ts2;
// EXPECT_THAT(ts1, TimespecMatcher(ts2));
MATCHER_P(TimespecMatcher, ts, "") {
  if (ts.tv_sec == arg.tv_sec && ts.tv_nsec == arg.tv_nsec)
    return true;
  *result_listener << "expected: {" << ts.tv_sec << ", " << ts.tv_nsec << "} ";
  *result_listener << "actual: {" << arg.tv_sec << ", " << arg.tv_nsec << "}";
  return false;
}

// A gMock matcher to match timeval values. Use this matcher like:
// timeval tv1, tv2;
// EXPECT_THAT(tv1, TimevalMatcher(tv2));
MATCHER_P(TimevalMatcher, tv, "") {
  if (tv.tv_sec == arg.tv_sec && tv.tv_usec == arg.tv_usec)
    return true;
  *result_listener << "expected: {" << tv.tv_sec << ", " << tv.tv_usec << "} ";
  *result_listener << "actual: {" << arg.tv_sec << ", " << arg.tv_usec << "}";
  return false;
}

TEST(Duration, ConstExpr) {
  static_assert(std::is_trivially_destructible<absl::Duration>::value,
                "Duration is documented as being trivially destructible");
  constexpr absl::Duration d0 = absl::ZeroDuration();
  static_assert(d0 == absl::ZeroDuration(), "ZeroDuration()");
  constexpr absl::Duration d1 = absl::Seconds(1);
  static_assert(d1 == absl::Seconds(1), "Seconds(1)");
  static_assert(d1 != absl::ZeroDuration(), "Seconds(1)");
  constexpr absl::Duration d2 = absl::InfiniteDuration();
  static_assert(d2 == absl::InfiniteDuration(), "InfiniteDuration()");
  static_assert(d2 != absl::ZeroDuration(), "InfiniteDuration()");
}

TEST(Duration, ValueSemantics) {
  // If this compiles, the test passes.
  constexpr absl::Duration a;      // Default construction
  constexpr absl::Duration b = a;  // Copy construction
  constexpr absl::Duration c(b);   // Copy construction (again)

  absl::Duration d;
  d = c;  // Assignment
}

TEST(Duration, Factories) {
  constexpr absl::Duration zero = absl::ZeroDuration();
  constexpr absl::Duration nano = absl::Nanoseconds(1);
  constexpr absl::Duration micro = absl::Microseconds(1);
  constexpr absl::Duration milli = absl::Milliseconds(1);
  constexpr absl::Duration sec = absl::Seconds(1);
  constexpr absl::Duration min = absl::Minutes(1);
  constexpr absl::Duration hour = absl::Hours(1);

  EXPECT_EQ(zero, absl::Duration());
  EXPECT_EQ(zero, absl::Seconds(0));
  EXPECT_EQ(nano, absl::Nanoseconds(1));
  EXPECT_EQ(micro, absl::Nanoseconds(1000));
  EXPECT_EQ(milli, absl::Microseconds(1000));
  EXPECT_EQ(sec, absl::Milliseconds(1000));
  EXPECT_EQ(min, absl::Seconds(60));
  EXPECT_EQ(hour, absl::Minutes(60));

  // Tests factory limits
  const absl::Duration inf = absl::InfiniteDuration();

  EXPECT_GT(inf, absl::Seconds(kint64max));
  EXPECT_LT(-inf, absl::Seconds(kint64min));
  EXPECT_LT(-inf, absl::Seconds(-kint64max));

  EXPECT_EQ(inf, absl::Minutes(kint64max));
  EXPECT_EQ(-inf, absl::Minutes(kint64min));
  EXPECT_EQ(-inf, absl::Minutes(-kint64max));
  EXPECT_GT(inf, absl::Minutes(kint64max / 60));
  EXPECT_LT(-inf, absl::Minutes(kint64min / 60));
  EXPECT_LT(-inf, absl::Minutes(-kint64max / 60));

  EXPECT_EQ(inf, absl::Hours(kint64max));
  EXPECT_EQ(-inf, absl::Hours(kint64min));
  EXPECT_EQ(-inf, absl::Hours(-kint64max));
  EXPECT_GT(inf, absl::Hours(kint64max / 3600));
  EXPECT_LT(-inf, absl::Hours(kint64min / 3600));
  EXPECT_LT(-inf, absl::Hours(-kint64max / 3600));
}

TEST(Duration, ToConversion) {
#define TEST_DURATION_CONVERSION(UNIT)                                  \
  do {                                                                  \
    const absl::Duration d = absl::UNIT(1.5);                           \
    constexpr absl::Duration z = absl::ZeroDuration();                  \
    constexpr absl::Duration inf = absl::InfiniteDuration();            \
    constexpr double dbl_inf = std::numeric_limits<double>::infinity(); \
    EXPECT_EQ(kint64min, absl::ToInt64##UNIT(-inf));                    \
    EXPECT_EQ(-1, absl::ToInt64##UNIT(-d));                             \
    EXPECT_EQ(0, absl::ToInt64##UNIT(z));                               \
    EXPECT_EQ(1, absl::ToInt64##UNIT(d));                               \
    EXPECT_EQ(kint64max, absl::ToInt64##UNIT(inf));                     \
    EXPECT_EQ(-dbl_inf, absl::ToDouble##UNIT(-inf));                    \
    EXPECT_EQ(-1.5, absl::ToDouble##UNIT(-d));                          \
    EXPECT_EQ(0, absl::ToDouble##UNIT(z));                              \
    EXPECT_EQ(1.5, absl::ToDouble##UNIT(d));                            \
    EXPECT_EQ(dbl_inf, absl::ToDouble##UNIT(inf));                      \
  } while (0)

  TEST_DURATION_CONVERSION(Nanoseconds);
  TEST_DURATION_CONVERSION(Microseconds);
  TEST_DURATION_CONVERSION(Milliseconds);
  TEST_DURATION_CONVERSION(Seconds);
  TEST_DURATION_CONVERSION(Minutes);
  TEST_DURATION_CONVERSION(Hours);

#undef TEST_DURATION_CONVERSION
}

template <int64_t N>
void TestToConversion() {
  constexpr absl::Duration nano = absl::Nanoseconds(N);
  EXPECT_EQ(N, absl::ToInt64Nanoseconds(nano));
  EXPECT_EQ(0, absl::ToInt64Microseconds(nano));
  EXPECT_EQ(0, absl::ToInt64Milliseconds(nano));
  EXPECT_EQ(0, absl::ToInt64Seconds(nano));
  EXPECT_EQ(0, absl::ToInt64Minutes(nano));
  EXPECT_EQ(0, absl::ToInt64Hours(nano));
  const absl::Duration micro = absl::Microseconds(N);
  EXPECT_EQ(N * 1000, absl::ToInt64Nanoseconds(micro));
  EXPECT_EQ(N, absl::ToInt64Microseconds(micro));
  EXPECT_EQ(0, absl::ToInt64Milliseconds(micro));
  EXPECT_EQ(0, absl::ToInt64Seconds(micro));
  EXPECT_EQ(0, absl::ToInt64Minutes(micro));
  EXPECT_EQ(0, absl::ToInt64Hours(micro));
  const absl::Duration milli = absl::Milliseconds(N);
  EXPECT_EQ(N * 1000 * 1000, absl::ToInt64Nanoseconds(milli));
  EXPECT_EQ(N * 1000, absl::ToInt64Microseconds(milli));
  EXPECT_EQ(N, absl::ToInt64Milliseconds(milli));
  EXPECT_EQ(0, absl::ToInt64Seconds(milli));
  EXPECT_EQ(0, absl::ToInt64Minutes(milli));
  EXPECT_EQ(0, absl::ToInt64Hours(milli));
  const absl::Duration sec = absl::Seconds(N);
  EXPECT_EQ(N * 1000 * 1000 * 1000, absl::ToInt64Nanoseconds(sec));
  EXPECT_EQ(N * 1000 * 1000, absl::ToInt64Microseconds(sec));
  EXPECT_EQ(N * 1000, absl::ToInt64Milliseconds(sec));
  EXPECT_EQ(N, absl::ToInt64Seconds(sec));
  EXPECT_EQ(0, absl::ToInt64Minutes(sec));
  EXPECT_EQ(0, absl::ToInt64Hours(sec));
  const absl::Duration min = absl::Minutes(N);
  EXPECT_EQ(N * 60 * 1000 * 1000 * 1000, absl::ToInt64Nanoseconds(min));
  EXPECT_EQ(N * 60 * 1000 * 1000, absl::ToInt64Microseconds(min));
  EXPECT_EQ(N * 60 * 1000, absl::ToInt64Milliseconds(min));
  EXPECT_EQ(N * 60, absl::ToInt64Seconds(min));
  EXPECT_EQ(N, absl::ToInt64Minutes(min));
  EXPECT_EQ(0, absl::ToInt64Hours(min));
  const absl::Duration hour = absl::Hours(N);
  EXPECT_EQ(N * 60 * 60 * 1000 * 1000 * 1000, absl::ToInt64Nanoseconds(hour));
  EXPECT_EQ(N * 60 * 60 * 1000 * 1000, absl::ToInt64Microseconds(hour));
  EXPECT_EQ(N * 60 * 60 * 1000, absl::ToInt64Milliseconds(hour));
  EXPECT_EQ(N * 60 * 60, absl::ToInt64Seconds(hour));
  EXPECT_EQ(N * 60, absl::ToInt64Minutes(hour));
  EXPECT_EQ(N, absl::ToInt64Hours(hour));
}

TEST(Duration, ToConversionDeprecated) {
  TestToConversion<43>();
  TestToConversion<1>();
  TestToConversion<0>();
  TestToConversion<-1>();
  TestToConversion<-43>();
}

template <int64_t N>
void TestFromChronoBasicEquality() {
  using std::chrono::nanoseconds;
  using std::chrono::microseconds;
  using std::chrono::milliseconds;
  using std::chrono::seconds;
  using std::chrono::minutes;
  using std::chrono::hours;

  static_assert(absl::Nanoseconds(N) == absl::FromChrono(nanoseconds(N)), "");
  static_assert(absl::Microseconds(N) == absl::FromChrono(microseconds(N)), "");
  static_assert(absl::Milliseconds(N) == absl::FromChrono(milliseconds(N)), "");
  static_assert(absl::Seconds(N) == absl::FromChrono(seconds(N)), "");
  static_assert(absl::Minutes(N) == absl::FromChrono(minutes(N)), "");
  static_assert(absl::Hours(N) == absl::FromChrono(hours(N)), "");
}

TEST(Duration, FromChrono) {
  TestFromChronoBasicEquality<-123>();
  TestFromChronoBasicEquality<-1>();
  TestFromChronoBasicEquality<0>();
  TestFromChronoBasicEquality<1>();
  TestFromChronoBasicEquality<123>();

  // Minutes (might, depending on the platform) saturate at +inf.
  const auto chrono_minutes_max = std::chrono::minutes::max();
  const auto minutes_max = absl::FromChrono(chrono_minutes_max);
  const int64_t minutes_max_count = chrono_minutes_max.count();
  if (minutes_max_count > kint64max / 60) {
    EXPECT_EQ(absl::InfiniteDuration(), minutes_max);
  } else {
    EXPECT_EQ(absl::Minutes(minutes_max_count), minutes_max);
  }

  // Minutes (might, depending on the platform) saturate at -inf.
  const auto chrono_minutes_min = std::chrono::minutes::min();
  const auto minutes_min = absl::FromChrono(chrono_minutes_min);
  const int64_t minutes_min_count = chrono_minutes_min.count();
  if (minutes_min_count < kint64min / 60) {
    EXPECT_EQ(-absl::InfiniteDuration(), minutes_min);
  } else {
    EXPECT_EQ(absl::Minutes(minutes_min_count), minutes_min);
  }

  // Hours (might, depending on the platform) saturate at +inf.
  const auto chrono_hours_max = std::chrono::hours::max();
  const auto hours_max = absl::FromChrono(chrono_hours_max);
  const int64_t hours_max_count = chrono_hours_max.count();
  if (hours_max_count > kint64max / 3600) {
    EXPECT_EQ(absl::InfiniteDuration(), hours_max);
  } else {
    EXPECT_EQ(absl::Hours(hours_max_count), hours_max);
  }

  // Hours (might, depending on the platform) saturate at -inf.
  const auto chrono_hours_min = std::chrono::hours::min();
  const auto hours_min = absl::FromChrono(chrono_hours_min);
  const int64_t hours_min_count = chrono_hours_min.count();
  if (hours_min_count < kint64min / 3600) {
    EXPECT_EQ(-absl::InfiniteDuration(), hours_min);
  } else {
    EXPECT_EQ(absl::Hours(hours_min_count), hours_min);
  }
}

template <int64_t N>
void TestToChrono() {
  using std::chrono::nanoseconds;
  using std::chrono::microseconds;
  using std::chrono::milliseconds;
  using std::chrono::seconds;
  using std::chrono::minutes;
  using std::chrono::hours;

  EXPECT_EQ(nanoseconds(N), absl::ToChronoNanoseconds(absl::Nanoseconds(N)));
  EXPECT_EQ(microseconds(N), absl::ToChronoMicroseconds(absl::Microseconds(N)));
  EXPECT_EQ(milliseconds(N), absl::ToChronoMilliseconds(absl::Milliseconds(N)));
  EXPECT_EQ(seconds(N), absl::ToChronoSeconds(absl::Seconds(N)));

  constexpr auto absl_minutes = absl::Minutes(N);
  auto chrono_minutes = minutes(N);
  if (absl_minutes == -absl::InfiniteDuration()) {
    chrono_minutes = minutes::min();
  } else if (absl_minutes == absl::InfiniteDuration()) {
    chrono_minutes = minutes::max();
  }
  EXPECT_EQ(chrono_minutes, absl::ToChronoMinutes(absl_minutes));

  constexpr auto absl_hours = absl::Hours(N);
  auto chrono_hours = hours(N);
  if (absl_hours == -absl::InfiniteDuration()) {
    chrono_hours = hours::min();
  } else if (absl_hours == absl::InfiniteDuration()) {
    chrono_hours = hours::max();
  }
  EXPECT_EQ(chrono_hours, absl::ToChronoHours(absl_hours));
}

TEST(Duration, ToChrono) {
  using std::chrono::nanoseconds;
  using std::chrono::microseconds;
  using std::chrono::milliseconds;
  using std::chrono::seconds;
  using std::chrono::minutes;
  using std::chrono::hours;

  TestToChrono<kint64min>();
  TestToChrono<-1>();
  TestToChrono<0>();
  TestToChrono<1>();
  TestToChrono<kint64max>();

  // Verify truncation toward zero.
  const auto tick = absl::Nanoseconds(1) / 4;
  EXPECT_EQ(nanoseconds(0), absl::ToChronoNanoseconds(tick));
  EXPECT_EQ(nanoseconds(0), absl::ToChronoNanoseconds(-tick));
  EXPECT_EQ(microseconds(0), absl::ToChronoMicroseconds(tick));
  EXPECT_EQ(microseconds(0), absl::ToChronoMicroseconds(-tick));
  EXPECT_EQ(milliseconds(0), absl::ToChronoMilliseconds(tick));
  EXPECT_EQ(milliseconds(0), absl::ToChronoMilliseconds(-tick));
  EXPECT_EQ(seconds(0), absl::ToChronoSeconds(tick));
  EXPECT_EQ(seconds(0), absl::ToChronoSeconds(-tick));
  EXPECT_EQ(minutes(0), absl::ToChronoMinutes(tick));
  EXPECT_EQ(minutes(0), absl::ToChronoMinutes(-tick));
  EXPECT_EQ(hours(0), absl::ToChronoHours(tick));
  EXPECT_EQ(hours(0), absl::ToChronoHours(-tick));

  // Verifies +/- infinity saturation at max/min.
  constexpr auto inf = absl::InfiniteDuration();
  EXPECT_EQ(nanoseconds::min(), absl::ToChronoNanoseconds(-inf));
  EXPECT_EQ(nanoseconds::max(), absl::ToChronoNanoseconds(inf));
  EXPECT_EQ(microseconds::min(), absl::ToChronoMicroseconds(-inf));
  EXPECT_EQ(microseconds::max(), absl::ToChronoMicroseconds(inf));
  EXPECT_EQ(milliseconds::min(), absl::ToChronoMilliseconds(-inf));
  EXPECT_EQ(milliseconds::max(), absl::ToChronoMilliseconds(inf));
  EXPECT_EQ(seconds::min(), absl::ToChronoSeconds(-inf));
  EXPECT_EQ(seconds::max(), absl::ToChronoSeconds(inf));
  EXPECT_EQ(minutes::min(), absl::ToChronoMinutes(-inf));
  EXPECT_EQ(minutes::max(), absl::ToChronoMinutes(inf));
  EXPECT_EQ(hours::min(), absl::ToChronoHours(-inf));
  EXPECT_EQ(hours::max(), absl::ToChronoHours(inf));
}

TEST(Duration, FactoryOverloads) {
  enum E { kOne = 1 };
#define TEST_FACTORY_OVERLOADS(NAME)                                          \
  EXPECT_EQ(1, NAME(kOne) / NAME(kOne));                                      \
  EXPECT_EQ(1, NAME(static_cast<int8_t>(1)) / NAME(1));                       \
  EXPECT_EQ(1, NAME(static_cast<int16_t>(1)) / NAME(1));                      \
  EXPECT_EQ(1, NAME(static_cast<int32_t>(1)) / NAME(1));                      \
  EXPECT_EQ(1, NAME(static_cast<int64_t>(1)) / NAME(1));                      \
  EXPECT_EQ(1, NAME(static_cast<uint8_t>(1)) / NAME(1));                      \
  EXPECT_EQ(1, NAME(static_cast<uint16_t>(1)) / NAME(1));                     \
  EXPECT_EQ(1, NAME(static_cast<uint32_t>(1)) / NAME(1));                     \
  EXPECT_EQ(1, NAME(static_cast<uint64_t>(1)) / NAME(1));                     \
  EXPECT_EQ(NAME(1) / 2, NAME(static_cast<float>(0.5)));                      \
  EXPECT_EQ(NAME(1) / 2, NAME(static_cast<double>(0.5)));                     \
  EXPECT_EQ(1.5, absl::FDivDuration(NAME(static_cast<float>(1.5)), NAME(1))); \
  EXPECT_EQ(1.5, absl::FDivDuration(NAME(static_cast<double>(1.5)), NAME(1)));

  TEST_FACTORY_OVERLOADS(absl::Nanoseconds);
  TEST_FACTORY_OVERLOADS(absl::Microseconds);
  TEST_FACTORY_OVERLOADS(absl::Milliseconds);
  TEST_FACTORY_OVERLOADS(absl::Seconds);
  TEST_FACTORY_OVERLOADS(absl::Minutes);
  TEST_FACTORY_OVERLOADS(absl::Hours);

#undef TEST_FACTORY_OVERLOADS

  EXPECT_EQ(absl::Milliseconds(1500), absl::Seconds(1.5));
  EXPECT_LT(absl::Nanoseconds(1), absl::Nanoseconds(1.5));
  EXPECT_GT(absl::Nanoseconds(2), absl::Nanoseconds(1.5));

  const double dbl_inf = std::numeric_limits<double>::infinity();
  EXPECT_EQ(absl::InfiniteDuration(), absl::Nanoseconds(dbl_inf));
  EXPECT_EQ(absl::InfiniteDuration(), absl::Microseconds(dbl_inf));
  EXPECT_EQ(absl::InfiniteDuration(), absl::Milliseconds(dbl_inf));
  EXPECT_EQ(absl::InfiniteDuration(), absl::Seconds(dbl_inf));
  EXPECT_EQ(absl::InfiniteDuration(), absl::Minutes(dbl_inf));
  EXPECT_EQ(absl::InfiniteDuration(), absl::Hours(dbl_inf));
  EXPECT_EQ(-absl::InfiniteDuration(), absl::Nanoseconds(-dbl_inf));
  EXPECT_EQ(-absl::InfiniteDuration(), absl::Microseconds(-dbl_inf));
  EXPECT_EQ(-absl::InfiniteDuration(), absl::Milliseconds(-dbl_inf));
  EXPECT_EQ(-absl::InfiniteDuration(), absl::Seconds(-dbl_inf));
  EXPECT_EQ(-absl::InfiniteDuration(), absl::Minutes(-dbl_inf));
  EXPECT_EQ(-absl::InfiniteDuration(), absl::Hours(-dbl_inf));
}

TEST(Duration, InfinityExamples) {
  // These examples are used in the documentation in time.h. They are
  // written so that they can be copy-n-pasted easily.

  constexpr absl::Duration inf = absl::InfiniteDuration();
  constexpr absl::Duration d = absl::Seconds(1);  // Any finite duration

  EXPECT_TRUE(inf == inf + inf);
  EXPECT_TRUE(inf == inf + d);
  EXPECT_TRUE(inf == inf - inf);
  EXPECT_TRUE(-inf == d - inf);

  EXPECT_TRUE(inf == d * 1e100);
  EXPECT_TRUE(0 == d / inf);  // NOLINT(readability/check)

  // Division by zero returns infinity, or kint64min/MAX where necessary.
  EXPECT_TRUE(inf == d / 0);
  EXPECT_TRUE(kint64max == d / absl::ZeroDuration());
}

TEST(Duration, InfinityComparison) {
  const absl::Duration inf = absl::InfiniteDuration();
  const absl::Duration any_dur = absl::Seconds(1);

  // Equality
  EXPECT_EQ(inf, inf);
  EXPECT_EQ(-inf, -inf);
  EXPECT_NE(inf, -inf);
  EXPECT_NE(any_dur, inf);
  EXPECT_NE(any_dur, -inf);

  // Relational
  EXPECT_GT(inf, any_dur);
  EXPECT_LT(-inf, any_dur);
  EXPECT_LT(-inf, inf);
  EXPECT_GT(inf, -inf);

#ifdef ABSL_INTERNAL_TIME_HAS_THREE_WAY_COMPARISON
  EXPECT_EQ(inf <=> inf, std::strong_ordering::equal);
  EXPECT_EQ(-inf <=> -inf, std::strong_ordering::equal);
  EXPECT_EQ(-inf <=> inf, std::strong_ordering::less);
  EXPECT_EQ(inf <=> -inf, std::strong_ordering::greater);
  EXPECT_EQ(any_dur <=> inf, std::strong_ordering::less);
  EXPECT_EQ(any_dur <=> -inf, std::strong_ordering::greater);
#endif  // ABSL_INTERNAL_TIME_HAS_THREE_WAY_COMPARISON
}

TEST(Duration, InfinityAddition) {
  const absl::Duration sec_max = absl::Seconds(kint64max);
  const absl::Duration sec_min = absl::Seconds(kint64min);
  const absl::Duration any_dur = absl::Seconds(1);
  const absl::Duration inf = absl::InfiniteDuration();

  // Addition
  EXPECT_EQ(inf, inf + inf);
  EXPECT_EQ(inf, inf + -inf);
  EXPECT_EQ(-inf, -inf + inf);
  EXPECT_EQ(-inf, -inf + -inf);

  EXPECT_EQ(inf, inf + any_dur);
  EXPECT_EQ(inf, any_dur + inf);
  EXPECT_EQ(-inf, -inf + any_dur);
  EXPECT_EQ(-inf, any_dur + -inf);

  // Interesting case
  absl::Duration almost_inf = sec_max + absl::Nanoseconds(999999999);
  EXPECT_GT(inf, almost_inf);
  almost_inf += -absl::Nanoseconds(999999999);
  EXPECT_GT(inf, almost_inf);

  // Addition overflow/underflow
  EXPECT_EQ(inf, sec_max + absl::Seconds(1));
  EXPECT_EQ(inf, sec_max + sec_max);
  EXPECT_EQ(-inf, sec_min + -absl::Seconds(1));
  EXPECT_EQ(-inf, sec_min + -sec_max);

  // For reference: IEEE 754 behavior
  const double dbl_inf = std::numeric_limits<double>::infinity();
  EXPECT_TRUE(std::isinf(dbl_inf + dbl_inf));
  EXPECT_TRUE(std::isnan(dbl_inf + -dbl_inf));  // We return inf
  EXPECT_TRUE(std::isnan(-dbl_inf + dbl_inf));  // We return inf
  EXPECT_TRUE(std::isinf(-dbl_inf + -dbl_inf));
}

TEST(Duration, InfinitySubtraction) {
  const absl::Duration sec_max = absl::Seconds(kint64max);
  const absl::Duration sec_min = absl::Seconds(kint64min);
  const absl::Duration any_dur = absl::Seconds(1);
  const absl::Duration inf = absl::InfiniteDuration();

  // Subtraction
  EXPECT_EQ(inf, inf - inf);
  EXPECT_EQ(inf, inf - -inf);
  EXPECT_EQ(-inf, -inf - inf);
  EXPECT_EQ(-inf, -inf - -inf);

  EXPECT_EQ(inf, inf - any_dur);
  EXPECT_EQ(-inf, any_dur - inf);
  EXPECT_EQ(-inf, -inf - any_dur);
  EXPECT_EQ(inf, any_dur - -inf);

  // Subtraction overflow/underflow
  EXPECT_EQ(inf, sec_max - -absl::Seconds(1));
  EXPECT_EQ(inf, sec_max - -sec_max);
  EXPECT_EQ(-inf, sec_min - absl::Seconds(1));
  EXPECT_EQ(-inf, sec_min - sec_max);

  // Interesting case
  absl::Duration almost_neg_inf = sec_min;
  EXPECT_LT(-inf, almost_neg_inf);

#ifdef ABSL_INTERNAL_TIME_HAS_THREE_WAY_COMPARISON
  EXPECT_EQ(-inf <=> almost_neg_inf, std::strong_ordering::less);
  EXPECT_EQ(almost_neg_inf <=> -inf, std::strong_ordering::greater);
#endif  // ABSL_INTERNAL_TIME_HAS_THREE_WAY_COMPARISON

  almost_neg_inf -= -absl::Nanoseconds(1);
  EXPECT_LT(-inf, almost_neg_inf);

#ifdef ABSL_INTERNAL_TIME_HAS_THREE_WAY_COMPARISON
  EXPECT_EQ(-inf <=> almost_neg_inf, std::strong_ordering::less);
  EXPECT_EQ(almost_neg_inf <=> -inf, std::strong_ordering::greater);
#endif  // ABSL_INTERNAL_TIME_HAS_THREE_WAY_COMPARISON

  // For reference: IEEE 754 behavior
  const double dbl_inf = std::numeric_limits<double>::infinity();
  EXPECT_TRUE(std::isnan(dbl_inf - dbl_inf));  // We return inf
  EXPECT_TRUE(std::isinf(dbl_inf - -dbl_inf));
  EXPECT_TRUE(std::isinf(-dbl_inf - dbl_inf));
  EXPECT_TRUE(std::isnan(-dbl_inf - -dbl_inf));  // We return inf
}

TEST(Duration, InfinityMultiplication) {
  const absl::Duration sec_max = absl::Seconds(kint64max);
  const absl::Duration sec_min = absl::Seconds(kint64min);
  const absl::Duration inf = absl::InfiniteDuration();

#define TEST_INF_MUL_WITH_TYPE(T)                                     \
  EXPECT_EQ(inf, inf * static_cast<T>(2));                            \
  EXPECT_EQ(-inf, inf * static_cast<T>(-2));                          \
  EXPECT_EQ(-inf, -inf * static_cast<T>(2));                          \
  EXPECT_EQ(inf, -inf * static_cast<T>(-2));                          \
  EXPECT_EQ(inf, inf * static_cast<T>(0));                            \
  EXPECT_EQ(-inf, -inf * static_cast<T>(0));                          \
  EXPECT_EQ(inf, sec_max * static_cast<T>(2));                        \
  EXPECT_EQ(inf, sec_min * static_cast<T>(-2));                       \
  EXPECT_EQ(inf, (sec_max / static_cast<T>(2)) * static_cast<T>(3));  \
  EXPECT_EQ(-inf, sec_max * static_cast<T>(-2));                      \
  EXPECT_EQ(-inf, sec_min * static_cast<T>(2));                       \
  EXPECT_EQ(-inf, (sec_min / static_cast<T>(2)) * static_cast<T>(3));

  TEST_INF_MUL_WITH_TYPE(int64_t);  // NOLINT(readability/function)
  TEST_INF_MUL_WITH_TYPE(double);   // NOLINT(readability/function)

#undef TEST_INF_MUL_WITH_TYPE

  const double dbl_inf = std::numeric_limits<double>::infinity();
  EXPECT_EQ(inf, inf * dbl_inf);
  EXPECT_EQ(-inf, -inf * dbl_inf);
  EXPECT_EQ(-inf, inf * -dbl_inf);
  EXPECT_EQ(inf, -inf * -dbl_inf);

  const absl::Duration any_dur = absl::Seconds(1);
  EXPECT_EQ(inf, any_dur * dbl_inf);
  EXPECT_EQ(-inf, -any_dur * dbl_inf);
  EXPECT_EQ(-inf, any_dur * -dbl_inf);
  EXPECT_EQ(inf, -any_dur * -dbl_inf);

  // Fixed-point multiplication will produce a finite value, whereas floating
  // point fuzziness will overflow to inf.
  EXPECT_NE(absl::InfiniteDuration(), absl::Seconds(1) * kint64max);
  EXPECT_EQ(inf, absl::Seconds(1) * static_cast<double>(kint64max));
  EXPECT_NE(-absl::InfiniteDuration(), absl::Seconds(1) * kint64min);
  EXPECT_EQ(-inf, absl::Seconds(1) * static_cast<double>(kint64min));

  // Note that sec_max * or / by 1.0 overflows to inf due to the 53-bit
  // limitations of double.
  EXPECT_NE(inf, sec_max);
  EXPECT_NE(inf, sec_max / 1);
  EXPECT_EQ(inf, sec_max / 1.0);
  EXPECT_NE(inf, sec_max * 1);
  EXPECT_EQ(inf, sec_max * 1.0);
}

TEST(Duration, InfinityDivision) {
  const absl::Duration sec_max = absl::Seconds(kint64max);
  const absl::Duration sec_min = absl::Seconds(kint64min);
  const absl::Duration inf = absl::InfiniteDuration();

  // Division of Duration by a double
#define TEST_INF_DIV_WITH_TYPE(T)            \
  EXPECT_EQ(inf, inf / static_cast<T>(2));   \
  EXPECT_EQ(-inf, inf / static_cast<T>(-2)); \
  EXPECT_EQ(-inf, -inf / static_cast<T>(2)); \
  EXPECT_EQ(inf, -inf / static_cast<T>(-2));

  TEST_INF_DIV_WITH_TYPE(int64_t);  // NOLINT(readability/function)
  TEST_INF_DIV_WITH_TYPE(double);   // NOLINT(readability/function)

#undef TEST_INF_DIV_WITH_TYPE

  // Division of Duration by a double overflow/underflow
  EXPECT_EQ(inf, sec_max / 0.5);
  EXPECT_EQ(inf, sec_min / -0.5);
  EXPECT_EQ(inf, ((sec_max / 0.5) + absl::Seconds(1)) / 0.5);
  EXPECT_EQ(-inf, sec_max / -0.5);
  EXPECT_EQ(-inf, sec_min / 0.5);
  EXPECT_EQ(-inf, ((sec_min / 0.5) - absl::Seconds(1)) / 0.5);

  const double dbl_inf = std::numeric_limits<double>::infinity();
  EXPECT_EQ(inf, inf / dbl_inf);
  EXPECT_EQ(-inf, inf / -dbl_inf);
  EXPECT_EQ(-inf, -inf / dbl_inf);
  EXPECT_EQ(inf, -inf / -dbl_inf);

  const absl::Duration any_dur = absl::Seconds(1);
  EXPECT_EQ(absl::ZeroDuration(), any_dur / dbl_inf);
  EXPECT_EQ(absl::ZeroDuration(), any_dur / -dbl_inf);
  EXPECT_EQ(absl::ZeroDuration(), -any_dur / dbl_inf);
  EXPECT_EQ(absl::ZeroDuration(), -any_dur / -dbl_inf);
}

TEST(Duration, InfinityModulus) {
  const absl::Duration sec_max = absl::Seconds(kint64max);
  const absl::Duration any_dur = absl::Seconds(1);
  const absl::Duration inf = absl::InfiniteDuration();

  EXPECT_EQ(inf, inf % inf);
  EXPECT_EQ(inf, inf % -inf);
  EXPECT_EQ(-inf, -inf % -inf);
  EXPECT_EQ(-inf, -inf % inf);

  EXPECT_EQ(any_dur, any_dur % inf);
  EXPECT_EQ(any_dur, any_dur % -inf);
  EXPECT_EQ(-any_dur, -any_dur % inf);
  EXPECT_EQ(-any_dur, -any_dur % -inf);

  EXPECT_EQ(inf, inf % -any_dur);
  EXPECT_EQ(inf, inf % any_dur);
  EXPECT_EQ(-inf, -inf % -any_dur);
  EXPECT_EQ(-inf, -inf % any_dur);

  // Remainder isn't affected by overflow.
  EXPECT_EQ(absl::ZeroDuration(), sec_max % absl::Seconds(1));
  EXPECT_EQ(absl::ZeroDuration(), sec_max % absl::Milliseconds(1));
  EXPECT_EQ(absl::ZeroDuration(), sec_max % absl::Microseconds(1));
  EXPECT_EQ(absl::ZeroDuration(), sec_max % absl::Nanoseconds(1));
  EXPECT_EQ(absl::ZeroDuration(), sec_max % absl::Nanoseconds(1) / 4);
}

TEST(Duration, InfinityIDiv) {
  const absl::Duration sec_max = absl::Seconds(kint64max);
  const absl::Duration any_dur = absl::Seconds(1);
  const absl::Duration inf = absl::InfiniteDuration();
  const double dbl_inf = std::numeric_limits<double>::infinity();

  // IDivDuration (int64_t return value + a remainer)
  absl::Duration rem = absl::ZeroDuration();
  EXPECT_EQ(kint64max, absl::IDivDuration(inf, inf, &rem));
  EXPECT_EQ(inf, rem);

  rem = absl::ZeroDuration();
  EXPECT_EQ(kint64max, absl::IDivDuration(-inf, -inf, &rem));
  EXPECT_EQ(-inf, rem);

  rem = absl::ZeroDuration();
  EXPECT_EQ(kint64max, absl::IDivDuration(inf, any_dur, &rem));
  EXPECT_EQ(inf, rem);

  rem = absl::ZeroDuration();
  EXPECT_EQ(0, absl::IDivDuration(any_dur, inf, &rem));
  EXPECT_EQ(any_dur, rem);

  rem = absl::ZeroDuration();
  EXPECT_EQ(kint64max, absl::IDivDuration(-inf, -any_dur, &rem));
  EXPECT_EQ(-inf, rem);

  rem = absl::ZeroDuration();
  EXPECT_EQ(0, absl::IDivDuration(-any_dur, -inf, &rem));
  EXPECT_EQ(-any_dur, rem);

  rem = absl::ZeroDuration();
  EXPECT_EQ(kint64min, absl::IDivDuration(-inf, inf, &rem));
  EXPECT_EQ(-inf, rem);

  rem = absl::ZeroDuration();
  EXPECT_EQ(kint64min, absl::IDivDuration(inf, -inf, &rem));
  EXPECT_EQ(inf, rem);

  rem = absl::ZeroDuration();
  EXPECT_EQ(kint64min, absl::IDivDuration(-inf, any_dur, &rem));
  EXPECT_EQ(-inf, rem);

  rem = absl::ZeroDuration();
  EXPECT_EQ(0, absl::IDivDuration(-any_dur, inf, &rem));
  EXPECT_EQ(-any_dur, rem);

  rem = absl::ZeroDuration();
  EXPECT_EQ(kint64min, absl::IDivDuration(inf, -any_dur, &rem));
  EXPECT_EQ(inf, rem);

  rem = absl::ZeroDuration();
  EXPECT_EQ(0, absl::IDivDuration(any_dur, -inf, &rem));
  EXPECT_EQ(any_dur, rem);

  // IDivDuration overflow/underflow
  rem = any_dur;
  EXPECT_EQ(kint64max,
            absl::IDivDuration(sec_max, absl::Nanoseconds(1) / 4, &rem));
  EXPECT_EQ(sec_max - absl::Nanoseconds(kint64max) / 4, rem);

  rem = any_dur;
  EXPECT_EQ(kint64max,
            absl::IDivDuration(sec_max, absl::Milliseconds(1), &rem));
  EXPECT_EQ(sec_max - absl::Milliseconds(kint64max), rem);

  rem = any_dur;
  EXPECT_EQ(kint64max,
            absl::IDivDuration(-sec_max, -absl::Milliseconds(1), &rem));
  EXPECT_EQ(-sec_max + absl::Milliseconds(kint64max), rem);

  rem = any_dur;
  EXPECT_EQ(kint64min,
            absl::IDivDuration(-sec_max, absl::Milliseconds(1), &rem));
  EXPECT_EQ(-sec_max - absl::Milliseconds(kint64min), rem);

  rem = any_dur;
  EXPECT_EQ(kint64min,
            absl::IDivDuration(sec_max, -absl::Milliseconds(1), &rem));
  EXPECT_EQ(sec_max + absl::Milliseconds(kint64min), rem);

  //
  // operator/(Duration, Duration) is a wrapper for IDivDuration().
  //

  // IEEE 754 says inf / inf should be nan, but int64_t doesn't have
  // nan so we'll return kint64max/kint64min instead.
  EXPECT_TRUE(std::isnan(dbl_inf / dbl_inf));
  EXPECT_EQ(kint64max, inf / inf);
  EXPECT_EQ(kint64max, -inf / -inf);
  EXPECT_EQ(kint64min, -inf / inf);
  EXPECT_EQ(kint64min, inf / -inf);

  EXPECT_TRUE(std::isinf(dbl_inf / 2.0));
  EXPECT_EQ(kint64max, inf / any_dur);
  EXPECT_EQ(kint64max, -inf / -any_dur);
  EXPECT_EQ(kint64min, -inf / any_dur);
  EXPECT_EQ(kint64min, inf / -any_dur);

  EXPECT_EQ(0.0, 2.0 / dbl_inf);
  EXPECT_EQ(0, any_dur / inf);
  EXPECT_EQ(0, any_dur / -inf);
  EXPECT_EQ(0, -any_dur / inf);
  EXPECT_EQ(0, -any_dur / -inf);
  EXPECT_EQ(0, absl::ZeroDuration() / inf);

  // Division of Duration by a Duration overflow/underflow
  EXPECT_EQ(kint64max, sec_max / absl::Milliseconds(1));
  EXPECT_EQ(kint64max, -sec_max / -absl::Milliseconds(1));
  EXPECT_EQ(kint64min, -sec_max / absl::Milliseconds(1));
  EXPECT_EQ(kint64min, sec_max / -absl::Milliseconds(1));
}

TEST(Duration, InfinityFDiv) {
  const absl::Duration any_dur = absl::Seconds(1);
  const absl::Duration inf = absl::InfiniteDuration();
  const double dbl_inf = std::numeric_limits<double>::infinity();

  EXPECT_EQ(dbl_inf, absl::FDivDuration(inf, inf));
  EXPECT_EQ(dbl_inf, absl::FDivDuration(-inf, -inf));
  EXPECT_EQ(dbl_inf, absl::FDivDuration(inf, any_dur));
  EXPECT_EQ(0.0, absl::FDivDuration(any_dur, inf));
  EXPECT_EQ(dbl_inf, absl::FDivDuration(-inf, -any_dur));
  EXPECT_EQ(0.0, absl::FDivDuration(-any_dur, -inf));

  EXPECT_EQ(-dbl_inf, absl::FDivDuration(-inf, inf));
  EXPECT_EQ(-dbl_inf, absl::FDivDuration(inf, -inf));
  EXPECT_EQ(-dbl_inf, absl::FDivDuration(-inf, any_dur));
  EXPECT_EQ(0.0, absl::FDivDuration(-any_dur, inf));
  EXPECT_EQ(-dbl_inf, absl::FDivDuration(inf, -any_dur));
  EXPECT_EQ(0.0, absl::FDivDuration(any_dur, -inf));
}

TEST(Duration, DivisionByZero) {
  const absl::Duration zero = absl::ZeroDuration();
  const absl::Duration inf = absl::InfiniteDuration();
  const absl::Duration any_dur = absl::Seconds(1);
  const double dbl_inf = std::numeric_limits<double>::infinity();
  const double dbl_denorm = std::numeric_limits<double>::denorm_min();

  // Operator/(Duration, double)
  EXPECT_EQ(inf, zero / 0.0);
  EXPECT_EQ(-inf, zero / -0.0);
  EXPECT_EQ(inf, any_dur / 0.0);
  EXPECT_EQ(-inf, any_dur / -0.0);
  EXPECT_EQ(-inf, -any_dur / 0.0);
  EXPECT_EQ(inf, -any_dur / -0.0);

  // Tests dividing by a number very close to, but not quite zero.
  EXPECT_EQ(zero, zero / dbl_denorm);
  EXPECT_EQ(zero, zero / -dbl_denorm);
  EXPECT_EQ(inf, any_dur / dbl_denorm);
  EXPECT_EQ(-inf, any_dur / -dbl_denorm);
  EXPECT_EQ(-inf, -any_dur / dbl_denorm);
  EXPECT_EQ(inf, -any_dur / -dbl_denorm);

  // IDiv
  absl::Duration rem = zero;
  EXPECT_EQ(kint64max, absl::IDivDuration(zero, zero, &rem));
  EXPECT_EQ(inf, rem);

  rem = zero;
  EXPECT_EQ(kint64max, absl::IDivDuration(any_dur, zero, &rem));
  EXPECT_EQ(inf, rem);

  rem = zero;
  EXPECT_EQ(kint64min, absl::IDivDuration(-any_dur, zero, &rem));
  EXPECT_EQ(-inf, rem);

  // Operator/(Duration, Duration)
  EXPECT_EQ(kint64max, zero / zero);
  EXPECT_EQ(kint64max, any_dur / zero);
  EXPECT_EQ(kint64min, -any_dur / zero);

  // FDiv
  EXPECT_EQ(dbl_inf, absl::FDivDuration(zero, zero));
  EXPECT_EQ(dbl_inf, absl::FDivDuration(any_dur, zero));
  EXPECT_EQ(-dbl_inf, absl::FDivDuration(-any_dur, zero));
}

TEST(Duration, NaN) {
  // Note that IEEE 754 does not define the behavior of a nan's sign when it is
  // copied, so the code below allows for either + or - InfiniteDuration.
#define TEST_NAN_HANDLING(NAME, NAN)           \
  do {                                         \
    const auto inf = absl::InfiniteDuration(); \
    auto x = NAME(NAN);                        \
    EXPECT_TRUE(x == inf || x == -inf);        \
    auto y = NAME(42);                         \
    y *= NAN;                                  \
    EXPECT_TRUE(y == inf || y == -inf);        \
    auto z = NAME(42);                         \
    z /= NAN;                                  \
    EXPECT_TRUE(z == inf || z == -inf);        \
  } while (0)

  const double nan = std::numeric_limits<double>::quiet_NaN();
  TEST_NAN_HANDLING(absl::Nanoseconds, nan);
  TEST_NAN_HANDLING(absl::Microseconds, nan);
  TEST_NAN_HANDLING(absl::Milliseconds, nan);
  TEST_NAN_HANDLING(absl::Seconds, nan);
  TEST_NAN_HANDLING(absl::Minutes, nan);
  TEST_NAN_HANDLING(absl::Hours, nan);

  TEST_NAN_HANDLING(absl::Nanoseconds, -nan);
  TEST_NAN_HANDLING(absl::Microseconds, -nan);
  TEST_NAN_HANDLING(absl::Milliseconds, -nan);
  TEST_NAN_HANDLING(absl::Seconds, -nan);
  TEST_NAN_HANDLING(absl::Minutes, -nan);
  TEST_NAN_HANDLING(absl::Hours, -nan);

#undef TEST_NAN_HANDLING
}

TEST(Duration, Range) {
  const absl::Duration range = ApproxYears(100 * 1e9);
  const absl::Duration range_future = range;
  const absl::Duration range_past = -range;

  EXPECT_LT(range_future, absl::InfiniteDuration());
  EXPECT_GT(range_past, -absl::InfiniteDuration());

  const absl::Duration full_range = range_future - range_past;
  EXPECT_GT(full_range, absl::ZeroDuration());
  EXPECT_LT(full_range, absl::InfiniteDuration());

  const absl::Duration neg_full_range = range_past - range_future;
  EXPECT_LT(neg_full_range, absl::ZeroDuration());
  EXPECT_GT(neg_full_range, -absl::InfiniteDuration());

  EXPECT_LT(neg_full_range, full_range);
  EXPECT_EQ(neg_full_range, -full_range);

#ifdef ABSL_INTERNAL_TIME_HAS_THREE_WAY_COMPARISON
  EXPECT_EQ(range_future <=> absl::InfiniteDuration(),
            std::strong_ordering::less);
  EXPECT_EQ(range_past <=> -absl::InfiniteDuration(),
            std::strong_ordering::greater);
  EXPECT_EQ(full_range <=> absl::ZeroDuration(),  //
            std::strong_ordering::greater);
  EXPECT_EQ(full_range <=> -absl::InfiniteDuration(),
            std::strong_ordering::greater);
  EXPECT_EQ(neg_full_range <=> -absl::InfiniteDuration(),
            std::strong_ordering::greater);
  EXPECT_EQ(neg_full_range <=> full_range, std::strong_ordering::less);
  EXPECT_EQ(neg_full_range <=> -full_range, std::strong_ordering::equal);
#endif  // ABSL_INTERNAL_TIME_HAS_THREE_WAY_COMPARISON
}

TEST(Duration, RelationalOperators) {
#define TEST_REL_OPS(UNIT)               \
  static_assert(UNIT(2) == UNIT(2), ""); \
  static_assert(UNIT(1) != UNIT(2), ""); \
  static_assert(UNIT(1) < UNIT(2), "");  \
  static_assert(UNIT(3) > UNIT(2), "");  \
  static_assert(UNIT(1) <= UNIT(2), ""); \
  static_assert(UNIT(2) <= UNIT(2), ""); \
  static_assert(UNIT(3) >= UNIT(2), ""); \
  static_assert(UNIT(2) >= UNIT(2), "");

  TEST_REL_OPS(absl::Nanoseconds);
  TEST_REL_OPS(absl::Microseconds);
  TEST_REL_OPS(absl::Milliseconds);
  TEST_REL_OPS(absl::Seconds);
  TEST_REL_OPS(absl::Minutes);
  TEST_REL_OPS(absl::Hours);

#undef TEST_REL_OPS
}

#ifdef ABSL_INTERNAL_TIME_HAS_THREE_WAY_COMPARISON

TEST(Duration, SpaceshipOperators) {
#define TEST_REL_OPS(UNIT)               \
  static_assert(UNIT(2) <=> UNIT(2) == std::strong_ordering::equal, ""); \
  static_assert(UNIT(1) <=> UNIT(2) == std::strong_ordering::less, ""); \
  static_assert(UNIT(3) <=> UNIT(2) == std::strong_ordering::greater, "");

  TEST_REL_OPS(absl::Nanoseconds);
  TEST_REL_OPS(absl::Microseconds);
  TEST_REL_OPS(absl::Milliseconds);
  TEST_REL_OPS(absl::Seconds);
  TEST_REL_OPS(absl::Minutes);
  TEST_REL_OPS(absl::Hours);

#undef TEST_REL_OPS
}

#endif  // ABSL_INTERNAL_TIME_HAS_THREE_WAY_COMPARISON

TEST(Duration, Addition) {
#define TEST_ADD_OPS(UNIT)                  \
  do {                                      \
    EXPECT_EQ(UNIT(2), UNIT(1) + UNIT(1));  \
    EXPECT_EQ(UNIT(1), UNIT(2) - UNIT(1));  \
    EXPECT_EQ(UNIT(0), UNIT(2) - UNIT(2));  \
    EXPECT_EQ(UNIT(-1), UNIT(1) - UNIT(2)); \
    EXPECT_EQ(UNIT(-2), UNIT(0) - UNIT(2)); \
    EXPECT_EQ(UNIT(-2), UNIT(1) - UNIT(3)); \
    absl::Duration a = UNIT(1);             \
    a += UNIT(1);                           \
    EXPECT_EQ(UNIT(2), a);                  \
    a -= UNIT(1);                           \
    EXPECT_EQ(UNIT(1), a);                  \
  } while (0)

  TEST_ADD_OPS(absl::Nanoseconds);
  TEST_ADD_OPS(absl::Microseconds);
  TEST_ADD_OPS(absl::Milliseconds);
  TEST_ADD_OPS(absl::Seconds);
  TEST_ADD_OPS(absl::Minutes);
  TEST_ADD_OPS(absl::Hours);

#undef TEST_ADD_OPS

  EXPECT_EQ(absl::Seconds(2), absl::Seconds(3) - 2 * absl::Milliseconds(500));
  EXPECT_EQ(absl::Seconds(2) + absl::Milliseconds(500),
            absl::Seconds(3) - absl::Milliseconds(500));

  EXPECT_EQ(absl::Seconds(1) + absl::Milliseconds(998),
            absl::Milliseconds(999) + absl::Milliseconds(999));

  EXPECT_EQ(absl::Milliseconds(-1),
            absl::Milliseconds(998) - absl::Milliseconds(999));

  // Tests fractions of a nanoseconds. These are implementation details only.
  EXPECT_GT(absl::Nanoseconds(1), absl::Nanoseconds(1) / 2);
  EXPECT_EQ(absl::Nanoseconds(1),
            absl::Nanoseconds(1) / 2 + absl::Nanoseconds(1) / 2);
  EXPECT_GT(absl::Nanoseconds(1) / 4, absl::Nanoseconds(0));
  EXPECT_EQ(absl::Nanoseconds(1) / 8, absl::Nanoseconds(0));

  // Tests subtraction that will cause wrap around of the rep_lo_ bits.
  absl::Duration d_7_5 = absl::Seconds(7) + absl::Milliseconds(500);
  absl::Duration d_3_7 = absl::Seconds(3) + absl::Milliseconds(700);
  absl::Duration ans_3_8 = absl::Seconds(3) + absl::Milliseconds(800);
  EXPECT_EQ(ans_3_8, d_7_5 - d_3_7);

  // Subtracting min_duration
  absl::Duration min_dur = absl::Seconds(kint64min);
  EXPECT_EQ(absl::Seconds(0), min_dur - min_dur);
  EXPECT_EQ(absl::Seconds(kint64max), absl::Seconds(-1) - min_dur);
}

TEST(Duration, Negation) {
  // By storing negations of various values in constexpr variables we
  // verify that the initializers are constant expressions.
  constexpr absl::Duration negated_zero_duration = -absl::ZeroDuration();
  EXPECT_EQ(negated_zero_duration, absl::ZeroDuration());

  constexpr absl::Duration negated_infinite_duration =
      -absl::InfiniteDuration();
  EXPECT_NE(negated_infinite_duration, absl::InfiniteDuration());
  EXPECT_EQ(-negated_infinite_duration, absl::InfiniteDuration());

  // The public APIs to check if a duration is infinite depend on using
  // -InfiniteDuration(), but we're trying to test operator- here, so we
  // need to use the lower-level internal query IsInfiniteDuration.
  EXPECT_TRUE(
      absl::time_internal::IsInfiniteDuration(negated_infinite_duration));

  // The largest Duration is kint64max seconds and kTicksPerSecond - 1 ticks.
  // Using the absl::time_internal::MakeDuration API is the cleanest way to
  // construct that Duration.
  constexpr absl::Duration max_duration = absl::time_internal::MakeDuration(
      kint64max, absl::time_internal::kTicksPerSecond - 1);
  constexpr absl::Duration negated_max_duration = -max_duration;
  // The largest negatable value is one tick above the minimum representable;
  // it's the negation of max_duration.
  constexpr absl::Duration nearly_min_duration =
      absl::time_internal::MakeDuration(kint64min, int64_t{1});
  constexpr absl::Duration negated_nearly_min_duration = -nearly_min_duration;

  EXPECT_EQ(negated_max_duration, nearly_min_duration);
  EXPECT_EQ(negated_nearly_min_duration, max_duration);
  EXPECT_EQ(-(-max_duration), max_duration);

  constexpr absl::Duration min_duration =
      absl::time_internal::MakeDuration(kint64min);
  constexpr absl::Duration negated_min_duration = -min_duration;
  EXPECT_EQ(negated_min_duration, absl::InfiniteDuration());
}

TEST(Duration, AbsoluteValue) {
  EXPECT_EQ(absl::ZeroDuration(), AbsDuration(absl::ZeroDuration()));
  EXPECT_EQ(absl::Seconds(1), AbsDuration(absl::Seconds(1)));
  EXPECT_EQ(absl::Seconds(1), AbsDuration(absl::Seconds(-1)));

  EXPECT_EQ(absl::InfiniteDuration(), AbsDuration(absl::InfiniteDuration()));
  EXPECT_EQ(absl::InfiniteDuration(), AbsDuration(-absl::InfiniteDuration()));

  absl::Duration max_dur =
      absl::Seconds(kint64max) + (absl::Seconds(1) - absl::Nanoseconds(1) / 4);
  EXPECT_EQ(max_dur, AbsDuration(max_dur));

  absl::Duration min_dur = absl::Seconds(kint64min);
  EXPECT_EQ(absl::InfiniteDuration(), AbsDuration(min_dur));
  EXPECT_EQ(max_dur, AbsDuration(min_dur + absl::Nanoseconds(1) / 4));
}

TEST(Duration, Multiplication) {
#define TEST_MUL_OPS(UNIT)                                    \
  do {                                                        \
    EXPECT_EQ(UNIT(5), UNIT(2) * 2.5);                        \
    EXPECT_EQ(UNIT(2), UNIT(5) / 2.5);                        \
    EXPECT_EQ(UNIT(-5), UNIT(-2) * 2.5);                      \
    EXPECT_EQ(UNIT(-5), -UNIT(2) * 2.5);                      \
    EXPECT_EQ(UNIT(-5), UNIT(2) * -2.5);                      \
    EXPECT_EQ(UNIT(-2), UNIT(-5) / 2.5);                      \
    EXPECT_EQ(UNIT(-2), -UNIT(5) / 2.5);                      \
    EXPECT_EQ(UNIT(-2), UNIT(5) / -2.5);                      \
    EXPECT_EQ(UNIT(2), UNIT(11) % UNIT(3));                   \
    absl::Duration a = UNIT(2);                               \
    a *= 2.5;                                                 \
    EXPECT_EQ(UNIT(5), a);                                    \
    a /= 2.5;                                                 \
    EXPECT_EQ(UNIT(2), a);                                    \
    a %= UNIT(1);                                             \
    EXPECT_EQ(UNIT(0), a);                                    \
    absl::Duration big = UNIT(1000000000);                    \
    big *= 3;                                                 \
    big /= 3;                                                 \
    EXPECT_EQ(UNIT(1000000000), big);                         \
    EXPECT_EQ(-UNIT(2), -UNIT(2));                            \
    EXPECT_EQ(-UNIT(2), UNIT(2) * -1);                        \
    EXPECT_EQ(-UNIT(2), -1 * UNIT(2));                        \
    EXPECT_EQ(-UNIT(-2), UNIT(2));                            \
    EXPECT_EQ(2, UNIT(2) / UNIT(1));                          \
    absl::Duration rem;                                       \
    EXPECT_EQ(2, absl::IDivDuration(UNIT(2), UNIT(1), &rem)); \
    EXPECT_EQ(2.0, absl::FDivDuration(UNIT(2), UNIT(1)));     \
  } while (0)

  TEST_MUL_OPS(absl::Nanoseconds);
  TEST_MUL_OPS(absl::Microseconds);
  TEST_MUL_OPS(absl::Milliseconds);
  TEST_MUL_OPS(absl::Seconds);
  TEST_MUL_OPS(absl::Minutes);
  TEST_MUL_OPS(absl::Hours);

#undef TEST_MUL_OPS

  // Ensures that multiplication and division by 1 with a maxed-out durations
  // doesn't lose precision.
  absl::Duration max_dur =
      absl::Seconds(kint64max) + (absl::Seconds(1) - absl::Nanoseconds(1) / 4);
  absl::Duration min_dur = absl::Seconds(kint64min);
  EXPECT_EQ(max_dur, max_dur * 1);
  EXPECT_EQ(max_dur, max_dur / 1);
  EXPECT_EQ(min_dur, min_dur * 1);
  EXPECT_EQ(min_dur, min_dur / 1);

  // Tests division on a Duration with a large number of significant digits.
  // Tests when the digits span hi and lo as well as only in hi.
  absl::Duration sigfigs = absl::Seconds(2000000000) + absl::Nanoseconds(3);
  EXPECT_EQ(absl::Seconds(666666666) + absl::Nanoseconds(666666667) +
                absl::Nanoseconds(1) / 2,
            sigfigs / 3);
  sigfigs = absl::Seconds(int64_t{7000000000});
  EXPECT_EQ(absl::Seconds(2333333333) + absl::Nanoseconds(333333333) +
                absl::Nanoseconds(1) / 4,
            sigfigs / 3);

  EXPECT_EQ(absl::Seconds(7) + absl::Milliseconds(500), absl::Seconds(3) * 2.5);
  EXPECT_EQ(absl::Seconds(8) * -1 + absl::Milliseconds(300),
            (absl::Seconds(2) + absl::Milliseconds(200)) * -3.5);
  EXPECT_EQ(-absl::Seconds(8) + absl::Milliseconds(300),
            (absl::Seconds(2) + absl::Milliseconds(200)) * -3.5);
  EXPECT_EQ(absl::Seconds(1) + absl::Milliseconds(875),
            (absl::Seconds(7) + absl::Milliseconds(500)) / 4);
  EXPECT_EQ(absl::Seconds(30),
            (absl::Seconds(7) + absl::Milliseconds(500)) / 0.25);
  EXPECT_EQ(absl::Seconds(3),
            (absl::Seconds(7) + absl::Milliseconds(500)) / 2.5);

  // Tests division remainder.
  EXPECT_EQ(absl::Nanoseconds(0), absl::Nanoseconds(7) % absl::Nanoseconds(1));
  EXPECT_EQ(absl::Nanoseconds(0), absl::Nanoseconds(0) % absl::Nanoseconds(10));
  EXPECT_EQ(absl::Nanoseconds(2), absl::Nanoseconds(7) % absl::Nanoseconds(5));
  EXPECT_EQ(absl::Nanoseconds(2), absl::Nanoseconds(2) % absl::Nanoseconds(5));

  EXPECT_EQ(absl::Nanoseconds(1), absl::Nanoseconds(10) % absl::Nanoseconds(3));
  EXPECT_EQ(absl::Nanoseconds(1),
            absl::Nanoseconds(10) % absl::Nanoseconds(-3));
  EXPECT_EQ(absl::Nanoseconds(-1),
            absl::Nanoseconds(-10) % absl::Nanoseconds(3));
  EXPECT_EQ(absl::Nanoseconds(-1),
            absl::Nanoseconds(-10) % absl::Nanoseconds(-3));

  EXPECT_EQ(absl::Milliseconds(100),
            absl::Seconds(1) % absl::Milliseconds(300));
  EXPECT_EQ(
      absl::Milliseconds(300),
      (absl::Seconds(3) + absl::Milliseconds(800)) % absl::Milliseconds(500));

  EXPECT_EQ(absl::Nanoseconds(1), absl::Nanoseconds(1) % absl::Seconds(1));
  EXPECT_EQ(absl::Nanoseconds(-1), absl::Nanoseconds(-1) % absl::Seconds(1));
  EXPECT_EQ(0, absl::Nanoseconds(-1) / absl::Seconds(1));  // Actual -1e-9

  // Tests identity a = (a/b)*b + a%b
#define TEST_MOD_IDENTITY(a, b) \
  EXPECT_EQ((a), ((a) / (b))*(b) + ((a)%(b)))

  TEST_MOD_IDENTITY(absl::Seconds(0), absl::Seconds(2));
  TEST_MOD_IDENTITY(absl::Seconds(1), absl::Seconds(1));
  TEST_MOD_IDENTITY(absl::Seconds(1), absl::Seconds(2));
  TEST_MOD_IDENTITY(absl::Seconds(2), absl::Seconds(1));

  TEST_MOD_IDENTITY(absl::Seconds(-2), absl::Seconds(1));
  TEST_MOD_IDENTITY(absl::Seconds(2), absl::Seconds(-1));
  TEST_MOD_IDENTITY(absl::Seconds(-2), absl::Seconds(-1));

  TEST_MOD_IDENTITY(absl::Nanoseconds(0), absl::Nanoseconds(2));
  TEST_MOD_IDENTITY(absl::Nanoseconds(1), absl::Nanoseconds(1));
  TEST_MOD_IDENTITY(absl::Nanoseconds(1), absl::Nanoseconds(2));
  TEST_MOD_IDENTITY(absl::Nanoseconds(2), absl::Nanoseconds(1));

  TEST_MOD_IDENTITY(absl::Nanoseconds(-2), absl::Nanoseconds(1));
  TEST_MOD_IDENTITY(absl::Nanoseconds(2), absl::Nanoseconds(-1));
  TEST_MOD_IDENTITY(absl::Nanoseconds(-2), absl::Nanoseconds(-1));

  // Mixed seconds + subseconds
  absl::Duration mixed_a = absl::Seconds(1) + absl::Nanoseconds(2);
  absl::Duration mixed_b = absl::Seconds(1) + absl::Nanoseconds(3);

  TEST_MOD_IDENTITY(absl::Seconds(0), mixed_a);
  TEST_MOD_IDENTITY(mixed_a, mixed_a);
  TEST_MOD_IDENTITY(mixed_a, mixed_b);
  TEST_MOD_IDENTITY(mixed_b, mixed_a);

  TEST_MOD_IDENTITY(-mixed_a, mixed_b);
  TEST_MOD_IDENTITY(mixed_a, -mixed_b);
  TEST_MOD_IDENTITY(-mixed_a, -mixed_b);

#undef TEST_MOD_IDENTITY
}

TEST(Duration, Truncation) {
  const absl::Duration d = absl::Nanoseconds(1234567890);
  const absl::Duration inf = absl::InfiniteDuration();
  for (int unit_sign : {1, -1}) {  // sign shouldn't matter
    EXPECT_EQ(absl::Nanoseconds(1234567890),
              Trunc(d, unit_sign * absl::Nanoseconds(1)));
    EXPECT_EQ(absl::Microseconds(1234567),
              Trunc(d, unit_sign * absl::Microseconds(1)));
    EXPECT_EQ(absl::Milliseconds(1234),
              Trunc(d, unit_sign * absl::Milliseconds(1)));
    EXPECT_EQ(absl::Seconds(1), Trunc(d, unit_sign * absl::Seconds(1)));
    EXPECT_EQ(inf, Trunc(inf, unit_sign * absl::Seconds(1)));

    EXPECT_EQ(absl::Nanoseconds(-1234567890),
              Trunc(-d, unit_sign * absl::Nanoseconds(1)));
    EXPECT_EQ(absl::Microseconds(-1234567),
              Trunc(-d, unit_sign * absl::Microseconds(1)));
    EXPECT_EQ(absl::Milliseconds(-1234),
              Trunc(-d, unit_sign * absl::Milliseconds(1)));
    EXPECT_EQ(absl::Seconds(-1), Trunc(-d, unit_sign * absl::Seconds(1)));
    EXPECT_EQ(-inf, Trunc(-inf, unit_sign * absl::Seconds(1)));
  }
}

TEST(Duration, Flooring) {
  const absl::Duration d = absl::Nanoseconds(1234567890);
  const absl::Duration inf = absl::InfiniteDuration();
  for (int unit_sign : {1, -1}) {  // sign shouldn't matter
    EXPECT_EQ(absl::Nanoseconds(1234567890),
              absl::Floor(d, unit_sign * absl::Nanoseconds(1)));
    EXPECT_EQ(absl::Microseconds(1234567),
              absl::Floor(d, unit_sign * absl::Microseconds(1)));
    EXPECT_EQ(absl::Milliseconds(1234),
              absl::Floor(d, unit_sign * absl::Milliseconds(1)));
    EXPECT_EQ(absl::Seconds(1), absl::Floor(d, unit_sign * absl::Seconds(1)));
    EXPECT_EQ(inf, absl::Floor(inf, unit_sign * absl::Seconds(1)));

    EXPECT_EQ(absl::Nanoseconds(-1234567890),
              absl::Floor(-d, unit_sign * absl::Nanoseconds(1)));
    EXPECT_EQ(absl::Microseconds(-1234568),
              absl::Floor(-d, unit_sign * absl::Microseconds(1)));
    EXPECT_EQ(absl::Milliseconds(-1235),
              absl::Floor(-d, unit_sign * absl::Milliseconds(1)));
    EXPECT_EQ(absl::Seconds(-2), absl::Floor(-d, unit_sign * absl::Seconds(1)));
    EXPECT_EQ(-inf, absl::Floor(-inf, unit_sign * absl::Seconds(1)));
  }
}

TEST(Duration, Ceiling) {
  const absl::Duration d = absl::Nanoseconds(1234567890);
  const absl::Duration inf = absl::InfiniteDuration();
  for (int unit_sign : {1, -1}) {  // // sign shouldn't matter
    EXPECT_EQ(absl::Nanoseconds(1234567890),
              absl::Ceil(d, unit_sign * absl::Nanoseconds(1)));
    EXPECT_EQ(absl::Microseconds(1234568),
              absl::Ceil(d, unit_sign * absl::Microseconds(1)));
    EXPECT_EQ(absl::Milliseconds(1235),
              absl::Ceil(d, unit_sign * absl::Milliseconds(1)));
    EXPECT_EQ(absl::Seconds(2), absl::Ceil(d, unit_sign * absl::Seconds(1)));
    EXPECT_EQ(inf, absl::Ceil(inf, unit_sign * absl::Seconds(1)));

    EXPECT_EQ(absl::Nanoseconds(-1234567890),
              absl::Ceil(-d, unit_sign * absl::Nanoseconds(1)));
    EXPECT_EQ(absl::Microseconds(-1234567),
              absl::Ceil(-d, unit_sign * absl::Microseconds(1)));
    EXPECT_EQ(absl::Milliseconds(-1234),
              absl::Ceil(-d, unit_sign * absl::Milliseconds(1)));
    EXPECT_EQ(absl::Seconds(-1), absl::Ceil(-d, unit_sign * absl::Seconds(1)));
    EXPECT_EQ(-inf, absl::Ceil(-inf, unit_sign * absl::Seconds(1)));
  }
}

TEST(Duration, RoundTripUnits) {
  const int kRange = 100000;

#define ROUND_TRIP_UNIT(U, LOW, HIGH)          \
  do {                                         \
    for (int64_t i = LOW; i < HIGH; ++i) {     \
      absl::Duration d = absl::U(i);           \
      if (d == absl::InfiniteDuration())       \
        EXPECT_EQ(kint64max, d / absl::U(1));  \
      else if (d == -absl::InfiniteDuration()) \
        EXPECT_EQ(kint64min, d / absl::U(1));  \
      else                                     \
        EXPECT_EQ(i, absl::U(i) / absl::U(1)); \
    }                                          \
  } while (0)

  ROUND_TRIP_UNIT(Nanoseconds, kint64min, kint64min + kRange);
  ROUND_TRIP_UNIT(Nanoseconds, -kRange, kRange);
  ROUND_TRIP_UNIT(Nanoseconds, kint64max - kRange, kint64max);

  ROUND_TRIP_UNIT(Microseconds, kint64min, kint64min + kRange);
  ROUND_TRIP_UNIT(Microseconds, -kRange, kRange);
  ROUND_TRIP_UNIT(Microseconds, kint64max - kRange, kint64max);

  ROUND_TRIP_UNIT(Milliseconds, kint64min, kint64min + kRange);
  ROUND_TRIP_UNIT(Milliseconds, -kRange, kRange);
  ROUND_TRIP_UNIT(Milliseconds, kint64max - kRange, kint64max);

  ROUND_TRIP_UNIT(Seconds, kint64min, kint64min + kRange);
  ROUND_TRIP_UNIT(Seconds, -kRange, kRange);
  ROUND_TRIP_UNIT(Seconds, kint64max - kRange, kint64max);

  ROUND_TRIP_UNIT(Minutes, kint64min / 60, kint64min / 60 + kRange);
  ROUND_TRIP_UNIT(Minutes, -kRange, kRange);
  ROUND_TRIP_UNIT(Minutes, kint64max / 60 - kRange, kint64max / 60);

  ROUND_TRIP_UNIT(Hours, kint64min / 3600, kint64min / 3600 + kRange);
  ROUND_TRIP_UNIT(Hours, -kRange, kRange);
  ROUND_TRIP_UNIT(Hours, kint64max / 3600 - kRange, kint64max / 3600);

#undef ROUND_TRIP_UNIT
}

TEST(Duration, TruncConversions) {
  // Tests ToTimespec()/DurationFromTimespec()
  const struct {
    absl::Duration d;
    timespec ts;
  } to_ts[] = {
      {absl::Seconds(1) + absl::Nanoseconds(1), {1, 1}},
      {absl::Seconds(1) + absl::Nanoseconds(1) / 2, {1, 0}},
      {absl::Seconds(1) + absl::Nanoseconds(0), {1, 0}},
      {absl::Seconds(0) + absl::Nanoseconds(0), {0, 0}},
      {absl::Seconds(0) - absl::Nanoseconds(1) / 2, {0, 0}},
      {absl::Seconds(0) - absl::Nanoseconds(1), {-1, 999999999}},
      {absl::Seconds(-1) + absl::Nanoseconds(1), {-1, 1}},
      {absl::Seconds(-1) + absl::Nanoseconds(1) / 2, {-1, 1}},
      {absl::Seconds(-1) + absl::Nanoseconds(0), {-1, 0}},
      {absl::Seconds(-1) - absl::Nanoseconds(1) / 2, {-1, 0}},
  };
  for (const auto& test : to_ts) {
    EXPECT_THAT(absl::ToTimespec(test.d), TimespecMatcher(test.ts));
  }
  const struct {
    timespec ts;
    absl::Duration d;
  } from_ts[] = {
      {{1, 1}, absl::Seconds(1) + absl::Nanoseconds(1)},
      {{1, 0}, absl::Seconds(1) + absl::Nanoseconds(0)},
      {{0, 0}, absl::Seconds(0) + absl::Nanoseconds(0)},
      {{0, -1}, absl::Seconds(0) - absl::Nanoseconds(1)},
      {{-1, 999999999}, absl::Seconds(0) - absl::Nanoseconds(1)},
      {{-1, 1}, absl::Seconds(-1) + absl::Nanoseconds(1)},
      {{-1, 0}, absl::Seconds(-1) + absl::Nanoseconds(0)},
      {{-1, -1}, absl::Seconds(-1) - absl::Nanoseconds(1)},
      {{-2, 999999999}, absl::Seconds(-1) - absl::Nanoseconds(1)},
  };
  for (const auto& test : from_ts) {
    EXPECT_EQ(test.d, absl::DurationFromTimespec(test.ts));
  }

  // Tests ToTimeval()/DurationFromTimeval() (same as timespec above)
  const struct {
    absl::Duration d;
    timeval tv;
  } to_tv[] = {
      {absl::Seconds(1) + absl::Microseconds(1), {1, 1}},
      {absl::Seconds(1) + absl::Microseconds(1) / 2, {1, 0}},
      {absl::Seconds(1) + absl::Microseconds(0), {1, 0}},
      {absl::Seconds(0) + absl::Microseconds(0), {0, 0}},
      {absl::Seconds(0) - absl::Microseconds(1) / 2, {0, 0}},
      {absl::Seconds(0) - absl::Microseconds(1), {-1, 999999}},
      {absl::Seconds(-1) + absl::Microseconds(1), {-1, 1}},
      {absl::Seconds(-1) + absl::Microseconds(1) / 2, {-1, 1}},
      {absl::Seconds(-1) + absl::Microseconds(0), {-1, 0}},
      {absl::Seconds(-1) - absl::Microseconds(1) / 2, {-1, 0}},
  };
  for (const auto& test : to_tv) {
    EXPECT_THAT(absl::ToTimeval(test.d), TimevalMatcher(test.tv));
  }
  const struct {
    timeval tv;
    absl::Duration d;
  } from_tv[] = {
      {{1, 1}, absl::Seconds(1) + absl::Microseconds(1)},
      {{1, 0}, absl::Seconds(1) + absl::Microseconds(0)},
      {{0, 0}, absl::Seconds(0) + absl::Microseconds(0)},
      {{0, -1}, absl::Seconds(0) - absl::Microseconds(1)},
      {{-1, 999999}, absl::Seconds(0) - absl::Microseconds(1)},
      {{-1, 1}, absl::Seconds(-1) + absl::Microseconds(1)},
      {{-1, 0}, absl::Seconds(-1) + absl::Microseconds(0)},
      {{-1, -1}, absl::Seconds(-1) - absl::Microseconds(1)},
      {{-2, 999999}, absl::Seconds(-1) - absl::Microseconds(1)},
  };
  for (const auto& test : from_tv) {
    EXPECT_EQ(test.d, absl::DurationFromTimeval(test.tv));
  }
}

TEST(Duration, SmallConversions) {
  // Special tests for conversions of small durations.

  EXPECT_EQ(absl::ZeroDuration(), absl::Seconds(0));
  // TODO(bww): Is the next one OK?
  EXPECT_EQ(absl::ZeroDuration(), absl::Seconds(std::nextafter(0.125e-9, 0)));
  EXPECT_EQ(absl::Nanoseconds(1) / 4, absl::Seconds(0.125e-9));
  EXPECT_EQ(absl::Nanoseconds(1) / 4, absl::Seconds(0.250e-9));
  EXPECT_EQ(absl::Nanoseconds(1) / 2, absl::Seconds(0.375e-9));
  EXPECT_EQ(absl::Nanoseconds(1) / 2, absl::Seconds(0.500e-9));
  EXPECT_EQ(absl::Nanoseconds(3) / 4, absl::Seconds(0.625e-9));
  EXPECT_EQ(absl::Nanoseconds(3) / 4, absl::Seconds(0.750e-9));
  EXPECT_EQ(absl::Nanoseconds(1), absl::Seconds(0.875e-9));
  EXPECT_EQ(absl::Nanoseconds(1), absl::Seconds(1.000e-9));

  EXPECT_EQ(absl::ZeroDuration(), absl::Seconds(std::nextafter(-0.125e-9, 0)));
  EXPECT_EQ(-absl::Nanoseconds(1) / 4, absl::Seconds(-0.125e-9));
  EXPECT_EQ(-absl::Nanoseconds(1) / 4, absl::Seconds(-0.250e-9));
  EXPECT_EQ(-absl::Nanoseconds(1) / 2, absl::Seconds(-0.375e-9));
  EXPECT_EQ(-absl::Nanoseconds(1) / 2, absl::Seconds(-0.500e-9));
  EXPECT_EQ(-absl::Nanoseconds(3) / 4, absl::Seconds(-0.625e-9));
  EXPECT_EQ(-absl::Nanoseconds(3) / 4, absl::Seconds(-0.750e-9));
  EXPECT_EQ(-absl::Nanoseconds(1), absl::Seconds(-0.875e-9));
  EXPECT_EQ(-absl::Nanoseconds(1), absl::Seconds(-1.000e-9));

  timespec ts;
  ts.tv_sec = 0;
  ts.tv_nsec = 0;
  EXPECT_THAT(ToTimespec(absl::Nanoseconds(0)), TimespecMatcher(ts));
  // TODO(bww): Are the next three OK?
  EXPECT_THAT(ToTimespec(absl::Nanoseconds(1) / 4), TimespecMatcher(ts));
  EXPECT_THAT(ToTimespec(absl::Nanoseconds(2) / 4), TimespecMatcher(ts));
  EXPECT_THAT(ToTimespec(absl::Nanoseconds(3) / 4), TimespecMatcher(ts));
  ts.tv_nsec = 1;
  EXPECT_THAT(ToTimespec(absl::Nanoseconds(4) / 4), TimespecMatcher(ts));
  EXPECT_THAT(ToTimespec(absl::Nanoseconds(5) / 4), TimespecMatcher(ts));
  EXPECT_THAT(ToTimespec(absl::Nanoseconds(6) / 4), TimespecMatcher(ts));
  EXPECT_THAT(ToTimespec(absl::Nanoseconds(7) / 4), TimespecMatcher(ts));
  ts.tv_nsec = 2;
  EXPECT_THAT(ToTimespec(absl::Nanoseconds(8) / 4), TimespecMatcher(ts));

  timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 0;
  EXPECT_THAT(ToTimeval(absl::Nanoseconds(0)), TimevalMatcher(tv));
  // TODO(bww): Is the next one OK?
  EXPECT_THAT(ToTimeval(absl::Nanoseconds(999)), TimevalMatcher(tv));
  tv.tv_usec = 1;
  EXPECT_THAT(ToTimeval(absl::Nanoseconds(1000)), TimevalMatcher(tv));
  EXPECT_THAT(ToTimeval(absl::Nanoseconds(1999)), TimevalMatcher(tv));
  tv.tv_usec = 2;
  EXPECT_THAT(ToTimeval(absl::Nanoseconds(2000)), TimevalMatcher(tv));
}

void VerifyApproxSameAsMul(double time_as_seconds, int* const misses) {
  auto direct_seconds = absl::Seconds(time_as_seconds);
  auto mul_by_one_second = time_as_seconds * absl::Seconds(1);
  // These are expected to differ by up to one tick due to fused multiply/add
  // contraction.
  if (absl::AbsDuration(direct_seconds - mul_by_one_second) >
      absl::time_internal::MakeDuration(0, 1u)) {
    if (*misses > 10) return;
    ASSERT_LE(++(*misses), 10) << "Too many errors, not reporting more.";
    EXPECT_EQ(direct_seconds, mul_by_one_second)
        << "given double time_as_seconds = " << std::setprecision(17)
        << time_as_seconds;
  }
}

// For a variety of interesting durations, we find the exact point
// where one double converts to that duration, and the very next double
// converts to the next duration.  For both of those points, verify that
// Seconds(point) returns a duration near point * Seconds(1.0). (They may
// not be exactly equal due to fused multiply/add contraction.)
TEST(Duration, ToDoubleSecondsCheckEdgeCases) {
#if (defined(__i386__) || defined(_M_IX86)) && FLT_EVAL_METHOD != 0
  // We're using an x87-compatible FPU, and intermediate operations can be
  // performed with 80-bit floats. This means the edge cases are different than
  // what we expect here, so just skip this test.
  GTEST_SKIP()
      << "Skipping the test because we detected x87 floating-point semantics";
#endif

  constexpr uint32_t kTicksPerSecond = absl::time_internal::kTicksPerSecond;
  constexpr auto duration_tick = absl::time_internal::MakeDuration(0, 1u);
  int misses = 0;
  for (int64_t seconds = 0; seconds < 99; ++seconds) {
    uint32_t tick_vals[] = {0, +999, +999999, +999999999, kTicksPerSecond - 1,
                            0, 1000, 1000000, 1000000000, kTicksPerSecond,
                            1, 1001, 1000001, 1000000001, kTicksPerSecond + 1,
                            2, 1002, 1000002, 1000000002, kTicksPerSecond + 2,
                            3, 1003, 1000003, 1000000003, kTicksPerSecond + 3,
                            4, 1004, 1000004, 1000000004, kTicksPerSecond + 4,
                            5, 6,    7,       8,          9};
    for (uint32_t ticks : tick_vals) {
      absl::Duration s_plus_t = absl::Seconds(seconds) + ticks * duration_tick;
      for (absl::Duration d : {s_plus_t, -s_plus_t}) {
        absl::Duration after_d = d + duration_tick;
        EXPECT_NE(d, after_d);
        EXPECT_EQ(after_d - d, duration_tick);

        double low_edge = ToDoubleSeconds(d);
        EXPECT_EQ(d, absl::Seconds(low_edge));

        double high_edge = ToDoubleSeconds(after_d);
        EXPECT_EQ(after_d, absl::Seconds(high_edge));

        for (;;) {
          double midpoint = low_edge + (high_edge - low_edge) / 2;
          if (midpoint == low_edge || midpoint == high_edge) break;
          absl::Duration mid_duration = absl::Seconds(midpoint);
          if (mid_duration == d) {
            low_edge = midpoint;
          } else {
            EXPECT_EQ(mid_duration, after_d);
            high_edge = midpoint;
          }
        }
        // Now low_edge is the highest double that converts to Duration d,
        // and high_edge is the lowest double that converts to Duration after_d.
        VerifyApproxSameAsMul(low_edge, &misses);
        VerifyApproxSameAsMul(high_edge, &misses);
      }
    }
  }
}

TEST(Duration, ToDoubleSecondsCheckRandom) {
  std::random_device rd;
  std::seed_seq seed({rd(), rd(), rd(), rd(), rd(), rd(), rd(), rd()});
  std::mt19937_64 gen(seed);
  // We want doubles distributed from 1/8ns up to 2^63, where
  // as many values are tested from 1ns to 2ns as from 1sec to 2sec,
  // so even distribute along a log-scale of those values, and
  // exponentiate before using them.  (9.223377e+18 is just slightly
  // out of bounds for absl::Duration.)
  std::uniform_real_distribution<double> uniform(std::log(0.125e-9),
                                                 std::log(9.223377e+18));
  int misses = 0;
  for (int i = 0; i < 1000000; ++i) {
    double d = std::exp(uniform(gen));
    VerifyApproxSameAsMul(d, &misses);
    VerifyApproxSameAsMul(-d, &misses);
  }
}

TEST(Duration, ConversionSaturation) {
  absl::Duration d;

  const auto max_timeval_sec =
      std::numeric_limits<decltype(timeval::tv_sec)>::max();
  const auto min_timeval_sec =
      std::numeric_limits<decltype(timeval::tv_sec)>::min();
  timeval tv;
  tv.tv_sec = max_timeval_sec;
  tv.tv_usec = 999998;
  d = absl::DurationFromTimeval(tv);
  tv = ToTimeval(d);
  EXPECT_EQ(max_timeval_sec, tv.tv_sec);
  EXPECT_EQ(999998, tv.tv_usec);
  d += absl::Microseconds(1);
  tv = ToTimeval(d);
  EXPECT_EQ(max_timeval_sec, tv.tv_sec);
  EXPECT_EQ(999999, tv.tv_usec);
  d += absl::Microseconds(1);  // no effect
  tv = ToTimeval(d);
  EXPECT_EQ(max_timeval_sec, tv.tv_sec);
  EXPECT_EQ(999999, tv.tv_usec);

  tv.tv_sec = min_timeval_sec;
  tv.tv_usec = 1;
  d = absl::DurationFromTimeval(tv);
  tv = ToTimeval(d);
  EXPECT_EQ(min_timeval_sec, tv.tv_sec);
  EXPECT_EQ(1, tv.tv_usec);
  d -= absl::Microseconds(1);
  tv = ToTimeval(d);
  EXPECT_EQ(min_timeval_sec, tv.tv_sec);
  EXPECT_EQ(0, tv.tv_usec);
  d -= absl::Microseconds(1);  // no effect
  tv = ToTimeval(d);
  EXPECT_EQ(min_timeval_sec, tv.tv_sec);
  EXPECT_EQ(0, tv.tv_usec);

  const auto max_timespec_sec =
      std::numeric_limits<decltype(timespec::tv_sec)>::max();
  const auto min_timespec_sec =
      std::numeric_limits<decltype(timespec::tv_sec)>::min();
  timespec ts;
  ts.tv_sec = max_timespec_sec;
  ts.tv_nsec = 999999998;
  d = absl::DurationFromTimespec(ts);
  ts = absl::ToTimespec(d);
  EXPECT_EQ(max_timespec_sec, ts.tv_sec);
  EXPECT_EQ(999999998, ts.tv_nsec);
  d += absl::Nanoseconds(1);
  ts = absl::ToTimespec(d);
  EXPECT_EQ(max_timespec_sec, ts.tv_sec);
  EXPECT_EQ(999999999, ts.tv_nsec);
  d += absl::Nanoseconds(1);  // no effect
  ts = absl::ToTimespec(d);
  EXPECT_EQ(max_timespec_sec, ts.tv_sec);
  EXPECT_EQ(999999999, ts.tv_nsec);

  ts.tv_sec = min_timespec_sec;
  ts.tv_nsec = 1;
  d = absl::DurationFromTimespec(ts);
  ts = absl::ToTimespec(d);
  EXPECT_EQ(min_timespec_sec, ts.tv_sec);
  EXPECT_EQ(1, ts.tv_nsec);
  d -= absl::Nanoseconds(1);
  ts = absl::ToTimespec(d);
  EXPECT_EQ(min_timespec_sec, ts.tv_sec);
  EXPECT_EQ(0, ts.tv_nsec);
  d -= absl::Nanoseconds(1);  // no effect
  ts = absl::ToTimespec(d);
  EXPECT_EQ(min_timespec_sec, ts.tv_sec);
  EXPECT_EQ(0, ts.tv_nsec);
}

TEST(Duration, FormatDuration) {
  // Example from Go's docs.
  EXPECT_EQ("72h3m0.5s",
            absl::FormatDuration(absl::Hours(72) + absl::Minutes(3) +
                                 absl::Milliseconds(500)));
  // Go's largest time: 2540400h10m10.000000000s
  EXPECT_EQ("2540400h10m10s",
            absl::FormatDuration(absl::Hours(2540400) + absl::Minutes(10) +
                                 absl::Seconds(10)));

  EXPECT_EQ("0", absl::FormatDuration(absl::ZeroDuration()));
  EXPECT_EQ("0", absl::FormatDuration(absl::Seconds(0)));
  EXPECT_EQ("0", absl::FormatDuration(absl::Nanoseconds(0)));

  EXPECT_EQ("1ns", absl::FormatDuration(absl::Nanoseconds(1)));
  EXPECT_EQ("1us", absl::FormatDuration(absl::Microseconds(1)));
  EXPECT_EQ("1ms", absl::FormatDuration(absl::Milliseconds(1)));
  EXPECT_EQ("1s", absl::FormatDuration(absl::Seconds(1)));
  EXPECT_EQ("1m", absl::FormatDuration(absl::Minutes(1)));
  EXPECT_EQ("1h", absl::FormatDuration(absl::Hours(1)));

  EXPECT_EQ("1h1m", absl::FormatDuration(absl::Hours(1) + absl::Minutes(1)));
  EXPECT_EQ("1h1s", absl::FormatDuration(absl::Hours(1) + absl::Seconds(1)));
  EXPECT_EQ("1m1s", absl::FormatDuration(absl::Minutes(1) + absl::Seconds(1)));

  EXPECT_EQ("1h0.25s",
            absl::FormatDuration(absl::Hours(1) + absl::Milliseconds(250)));
  EXPECT_EQ("1m0.25s",
            absl::FormatDuration(absl::Minutes(1) + absl::Milliseconds(250)));
  EXPECT_EQ("1h1m0.25s",
            absl::FormatDuration(absl::Hours(1) + absl::Minutes(1) +
                                 absl::Milliseconds(250)));
  EXPECT_EQ("1h0.0005s",
            absl::FormatDuration(absl::Hours(1) + absl::Microseconds(500)));
  EXPECT_EQ("1h0.0000005s",
            absl::FormatDuration(absl::Hours(1) + absl::Nanoseconds(500)));

  // Subsecond special case.
  EXPECT_EQ("1.5ns", absl::FormatDuration(absl::Nanoseconds(1) +
                                          absl::Nanoseconds(1) / 2));
  EXPECT_EQ("1.25ns", absl::FormatDuration(absl::Nanoseconds(1) +
                                           absl::Nanoseconds(1) / 4));
  EXPECT_EQ("1ns", absl::FormatDuration(absl::Nanoseconds(1) +
                                        absl::Nanoseconds(1) / 9));
  EXPECT_EQ("1.2us", absl::FormatDuration(absl::Microseconds(1) +
                                          absl::Nanoseconds(200)));
  EXPECT_EQ("1.2ms", absl::FormatDuration(absl::Milliseconds(1) +
                                          absl::Microseconds(200)));
  EXPECT_EQ("1.0002ms", absl::FormatDuration(absl::Milliseconds(1) +
                                             absl::Nanoseconds(200)));
  EXPECT_EQ("1.00001ms", absl::FormatDuration(absl::Milliseconds(1) +
                                              absl::Nanoseconds(10)));
  EXPECT_EQ("1.000001ms",
            absl::FormatDuration(absl::Milliseconds(1) + absl::Nanoseconds(1)));

  // Negative durations.
  EXPECT_EQ("-1ns", absl::FormatDuration(absl::Nanoseconds(-1)));
  EXPECT_EQ("-1us", absl::FormatDuration(absl::Microseconds(-1)));
  EXPECT_EQ("-1ms", absl::FormatDuration(absl::Milliseconds(-1)));
  EXPECT_EQ("-1s", absl::FormatDuration(absl::Seconds(-1)));
  EXPECT_EQ("-1m", absl::FormatDuration(absl::Minutes(-1)));
  EXPECT_EQ("-1h", absl::FormatDuration(absl::Hours(-1)));

  EXPECT_EQ("-1h1m",
            absl::FormatDuration(-(absl::Hours(1) + absl::Minutes(1))));
  EXPECT_EQ("-1h1s",
            absl::FormatDuration(-(absl::Hours(1) + absl::Seconds(1))));
  EXPECT_EQ("-1m1s",
            absl::FormatDuration(-(absl::Minutes(1) + absl::Seconds(1))));

  EXPECT_EQ("-1ns", absl::FormatDuration(absl::Nanoseconds(-1)));
  EXPECT_EQ("-1.2us", absl::FormatDuration(
                          -(absl::Microseconds(1) + absl::Nanoseconds(200))));
  EXPECT_EQ("-1.2ms", absl::FormatDuration(
                          -(absl::Milliseconds(1) + absl::Microseconds(200))));
  EXPECT_EQ("-1.0002ms", absl::FormatDuration(-(absl::Milliseconds(1) +
                                                absl::Nanoseconds(200))));
  EXPECT_EQ("-1.00001ms", absl::FormatDuration(-(absl::Milliseconds(1) +
                                                 absl::Nanoseconds(10))));
  EXPECT_EQ("-1.000001ms", absl::FormatDuration(-(absl::Milliseconds(1) +
                                                  absl::Nanoseconds(1))));

  //
  // Interesting corner cases.
  //

  const absl::Duration qns = absl::Nanoseconds(1) / 4;
  const absl::Duration max_dur =
      absl::Seconds(kint64max) + (absl::Seconds(1) - qns);
  const absl::Duration min_dur = absl::Seconds(kint64min);

  EXPECT_EQ("0.25ns", absl::FormatDuration(qns));
  EXPECT_EQ("-0.25ns", absl::FormatDuration(-qns));
  EXPECT_EQ("2562047788015215h30m7.99999999975s",
            absl::FormatDuration(max_dur));
  EXPECT_EQ("-2562047788015215h30m8s", absl::FormatDuration(min_dur));

  // Tests printing full precision from units that print using FDivDuration
  EXPECT_EQ("55.00000000025s", absl::FormatDuration(absl::Seconds(55) + qns));
  EXPECT_EQ("55.00000025ms",
            absl::FormatDuration(absl::Milliseconds(55) + qns));
  EXPECT_EQ("55.00025us", absl::FormatDuration(absl::Microseconds(55) + qns));
  EXPECT_EQ("55.25ns", absl::FormatDuration(absl::Nanoseconds(55) + qns));

  // Formatting infinity
  EXPECT_EQ("inf", absl::FormatDuration(absl::InfiniteDuration()));
  EXPECT_EQ("-inf", absl::FormatDuration(-absl::InfiniteDuration()));

  // Formatting approximately +/- 100 billion years
  const absl::Duration huge_range = ApproxYears(100000000000);
  EXPECT_EQ("876000000000000h", absl::FormatDuration(huge_range));
  EXPECT_EQ("-876000000000000h", absl::FormatDuration(-huge_range));

  EXPECT_EQ("876000000000000h0.999999999s",
            absl::FormatDuration(huge_range +
                                 (absl::Seconds(1) - absl::Nanoseconds(1))));
  EXPECT_EQ("876000000000000h0.9999999995s",
            absl::FormatDuration(
                huge_range + (absl::Seconds(1) - absl::Nanoseconds(1) / 2)));
  EXPECT_EQ("876000000000000h0.99999999975s",
            absl::FormatDuration(
                huge_range + (absl::Seconds(1) - absl::Nanoseconds(1) / 4)));

  EXPECT_EQ("-876000000000000h0.999999999s",
            absl::FormatDuration(-huge_range -
                                 (absl::Seconds(1) - absl::Nanoseconds(1))));
  EXPECT_EQ("-876000000000000h0.9999999995s",
            absl::FormatDuration(
                -huge_range - (absl::Seconds(1) - absl::Nanoseconds(1) / 2)));
  EXPECT_EQ("-876000000000000h0.99999999975s",
            absl::FormatDuration(
                -huge_range - (absl::Seconds(1) - absl::Nanoseconds(1) / 4)));
}

TEST(Duration, ParseDuration) {
  absl::Duration d;

  // No specified unit. Should only work for zero and infinity.
  EXPECT_TRUE(absl::ParseDuration("0", &d));
  EXPECT_EQ(absl::ZeroDuration(), d);
  EXPECT_TRUE(absl::ParseDuration("+0", &d));
  EXPECT_EQ(absl::ZeroDuration(), d);
  EXPECT_TRUE(absl::ParseDuration("-0", &d));
  EXPECT_EQ(absl::ZeroDuration(), d);

  EXPECT_TRUE(absl::ParseDuration("inf", &d));
  EXPECT_EQ(absl::InfiniteDuration(), d);
  EXPECT_TRUE(absl::ParseDuration("+inf", &d));
  EXPECT_EQ(absl::InfiniteDuration(), d);
  EXPECT_TRUE(absl::ParseDuration("-inf", &d));
  EXPECT_EQ(-absl::InfiniteDuration(), d);
  EXPECT_FALSE(absl::ParseDuration("infBlah", &d));

  // Illegal input forms.
  EXPECT_FALSE(absl::ParseDuration("", &d));
  EXPECT_FALSE(absl::ParseDuration("0.0", &d));
  EXPECT_FALSE(absl::ParseDuration(".0", &d));
  EXPECT_FALSE(absl::ParseDuration(".", &d));
  EXPECT_FALSE(absl::ParseDuration("01", &d));
  EXPECT_FALSE(absl::ParseDuration("1", &d));
  EXPECT_FALSE(absl::ParseDuration("-1", &d));
  EXPECT_FALSE(absl::ParseDuration("2", &d));
  EXPECT_FALSE(absl::ParseDuration("2 s", &d));
  EXPECT_FALSE(absl::ParseDuration(".s", &d));
  EXPECT_FALSE(absl::ParseDuration("-.s", &d));
  EXPECT_FALSE(absl::ParseDuration("s", &d));
  EXPECT_FALSE(absl::ParseDuration(" 2s", &d));
  EXPECT_FALSE(absl::ParseDuration("2s ", &d));
  EXPECT_FALSE(absl::ParseDuration(" 2s ", &d));
  EXPECT_FALSE(absl::ParseDuration("2mt", &d));
  EXPECT_FALSE(absl::ParseDuration("1e3s", &d));

  // One unit type.
  EXPECT_TRUE(absl::ParseDuration("1ns", &d));
  EXPECT_EQ(absl::Nanoseconds(1), d);
  EXPECT_TRUE(absl::ParseDuration("1us", &d));
  EXPECT_EQ(absl::Microseconds(1), d);
  EXPECT_TRUE(absl::ParseDuration("1ms", &d));
  EXPECT_EQ(absl::Milliseconds(1), d);
  EXPECT_TRUE(absl::ParseDuration("1s", &d));
  EXPECT_EQ(absl::Seconds(1), d);
  EXPECT_TRUE(absl::ParseDuration("2m", &d));
  EXPECT_EQ(absl::Minutes(2), d);
  EXPECT_TRUE(absl::ParseDuration("2h", &d));
  EXPECT_EQ(absl::Hours(2), d);

  // Huge counts of a unit.
  EXPECT_TRUE(absl::ParseDuration("9223372036854775807us", &d));
  EXPECT_EQ(absl::Microseconds(9223372036854775807), d);
  EXPECT_TRUE(absl::ParseDuration("-9223372036854775807us", &d));
  EXPECT_EQ(absl::Microseconds(-9223372036854775807), d);

  // Multiple units.
  EXPECT_TRUE(absl::ParseDuration("2h3m4s", &d));
  EXPECT_EQ(absl::Hours(2) + absl::Minutes(3) + absl::Seconds(4), d);
  EXPECT_TRUE(absl::ParseDuration("3m4s5us", &d));
  EXPECT_EQ(absl::Minutes(3) + absl::Seconds(4) + absl::Microseconds(5), d);
  EXPECT_TRUE(absl::ParseDuration("2h3m4s5ms6us7ns", &d));
  EXPECT_EQ(absl::Hours(2) + absl::Minutes(3) + absl::Seconds(4) +
                absl::Milliseconds(5) + absl::Microseconds(6) +
                absl::Nanoseconds(7),
            d);

  // Multiple units out of order.
  EXPECT_TRUE(absl::ParseDuration("2us3m4s5h", &d));
  EXPECT_EQ(absl::Hours(5) + absl::Minutes(3) + absl::Seconds(4) +
                absl::Microseconds(2),
            d);

  // Fractional values of units.
  EXPECT_TRUE(absl::ParseDuration("1.5ns", &d));
  EXPECT_EQ(1.5 * absl::Nanoseconds(1), d);
  EXPECT_TRUE(absl::ParseDuration("1.5us", &d));
  EXPECT_EQ(1.5 * absl::Microseconds(1), d);
  EXPECT_TRUE(absl::ParseDuration("1.5ms", &d));
  EXPECT_EQ(1.5 * absl::Milliseconds(1), d);
  EXPECT_TRUE(absl::ParseDuration("1.5s", &d));
  EXPECT_EQ(1.5 * absl::Seconds(1), d);
  EXPECT_TRUE(absl::ParseDuration("1.5m", &d));
  EXPECT_EQ(1.5 * absl::Minutes(1), d);
  EXPECT_TRUE(absl::ParseDuration("1.5h", &d));
  EXPECT_EQ(1.5 * absl::Hours(1), d);

  // Huge fractional counts of a unit.
  EXPECT_TRUE(absl::ParseDuration("0.4294967295s", &d));
  EXPECT_EQ(absl::Nanoseconds(429496729) + absl::Nanoseconds(1) / 2, d);
  EXPECT_TRUE(absl::ParseDuration("0.429496729501234567890123456789s", &d));
  EXPECT_EQ(absl::Nanoseconds(429496729) + absl::Nanoseconds(1) / 2, d);

  // Negative durations.
  EXPECT_TRUE(absl::ParseDuration("-1s", &d));
  EXPECT_EQ(absl::Seconds(-1), d);
  EXPECT_TRUE(absl::ParseDuration("-1m", &d));
  EXPECT_EQ(absl::Minutes(-1), d);
  EXPECT_TRUE(absl::ParseDuration("-1h", &d));
  EXPECT_EQ(absl::Hours(-1), d);

  EXPECT_TRUE(absl::ParseDuration("-1h2s", &d));
  EXPECT_EQ(-(absl::Hours(1) + absl::Seconds(2)), d);
  EXPECT_FALSE(absl::ParseDuration("1h-2s", &d));
  EXPECT_FALSE(absl::ParseDuration("-1h-2s", &d));
  EXPECT_FALSE(absl::ParseDuration("-1h -2s", &d));
}

TEST(Duration, FormatParseRoundTrip) {
#define TEST_PARSE_ROUNDTRIP(d)                \
  do {                                         \
    std::string s = absl::FormatDuration(d);   \
    absl::Duration dur;                        \
    EXPECT_TRUE(absl::ParseDuration(s, &dur)); \
    EXPECT_EQ(d, dur);                         \
  } while (0)

  TEST_PARSE_ROUNDTRIP(absl::Nanoseconds(1));
  TEST_PARSE_ROUNDTRIP(absl::Microseconds(1));
  TEST_PARSE_ROUNDTRIP(absl::Milliseconds(1));
  TEST_PARSE_ROUNDTRIP(absl::Seconds(1));
  TEST_PARSE_ROUNDTRIP(absl::Minutes(1));
  TEST_PARSE_ROUNDTRIP(absl::Hours(1));
  TEST_PARSE_ROUNDTRIP(absl::Hours(1) + absl::Nanoseconds(2));

  TEST_PARSE_ROUNDTRIP(absl::Nanoseconds(-1));
  TEST_PARSE_ROUNDTRIP(absl::Microseconds(-1));
  TEST_PARSE_ROUNDTRIP(absl::Milliseconds(-1));
  TEST_PARSE_ROUNDTRIP(absl::Seconds(-1));
  TEST_PARSE_ROUNDTRIP(absl::Minutes(-1));
  TEST_PARSE_ROUNDTRIP(absl::Hours(-1));

  TEST_PARSE_ROUNDTRIP(absl::Hours(-1) + absl::Nanoseconds(2));
  TEST_PARSE_ROUNDTRIP(absl::Hours(1) + absl::Nanoseconds(-2));
  TEST_PARSE_ROUNDTRIP(absl::Hours(-1) + absl::Nanoseconds(-2));

  TEST_PARSE_ROUNDTRIP(absl::Nanoseconds(1) +
                       absl::Nanoseconds(1) / 4);  // 1.25ns

  const absl::Duration huge_range = ApproxYears(100000000000);
  TEST_PARSE_ROUNDTRIP(huge_range);
  TEST_PARSE_ROUNDTRIP(huge_range + (absl::Seconds(1) - absl::Nanoseconds(1)));

#undef TEST_PARSE_ROUNDTRIP
}

TEST(Duration, AbslStringify) {
  // FormatDuration is already well tested, so just use one test case here to
  // verify that StrFormat("%v", d) works as expected.
  absl::Duration d = absl::Seconds(1);
  EXPECT_EQ(absl::StrFormat("%v", d), absl::FormatDuration(d));
}

TEST(Duration, NoPadding) {
  // Should match the size of a struct with uint32_t alignment and no padding.
  using NoPadding = std::array<uint32_t, 3>;
  EXPECT_EQ(sizeof(NoPadding), sizeof(absl::Duration));
  EXPECT_EQ(alignof(NoPadding), alignof(absl::Duration));
}

}  // namespace
