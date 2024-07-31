// Copyright 2018 The Abseil Authors.
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

#include "absl/time/civil_time.h"

#include <iomanip>
#include <limits>
#include <sstream>
#include <type_traits>

#include "gtest/gtest.h"
#include "absl/base/macros.h"
#include "absl/strings/str_format.h"

namespace {

TEST(CivilTime, DefaultConstruction) {
  absl::CivilSecond ss;
  EXPECT_EQ("1970-01-01T00:00:00", absl::FormatCivilTime(ss));

  absl::CivilMinute mm;
  EXPECT_EQ("1970-01-01T00:00", absl::FormatCivilTime(mm));

  absl::CivilHour hh;
  EXPECT_EQ("1970-01-01T00", absl::FormatCivilTime(hh));

  absl::CivilDay d;
  EXPECT_EQ("1970-01-01", absl::FormatCivilTime(d));

  absl::CivilMonth m;
  EXPECT_EQ("1970-01", absl::FormatCivilTime(m));

  absl::CivilYear y;
  EXPECT_EQ("1970", absl::FormatCivilTime(y));
}

TEST(CivilTime, StructMember) {
  struct S {
    absl::CivilDay day;
  };
  S s = {};
  EXPECT_EQ(absl::CivilDay{}, s.day);
}

TEST(CivilTime, FieldsConstruction) {
  EXPECT_EQ("2015-01-02T03:04:05",
            absl::FormatCivilTime(absl::CivilSecond(2015, 1, 2, 3, 4, 5)));
  EXPECT_EQ("2015-01-02T03:04:00",
            absl::FormatCivilTime(absl::CivilSecond(2015, 1, 2, 3, 4)));
  EXPECT_EQ("2015-01-02T03:00:00",
            absl::FormatCivilTime(absl::CivilSecond(2015, 1, 2, 3)));
  EXPECT_EQ("2015-01-02T00:00:00",
            absl::FormatCivilTime(absl::CivilSecond(2015, 1, 2)));
  EXPECT_EQ("2015-01-01T00:00:00",
            absl::FormatCivilTime(absl::CivilSecond(2015, 1)));
  EXPECT_EQ("2015-01-01T00:00:00",
            absl::FormatCivilTime(absl::CivilSecond(2015)));

  EXPECT_EQ("2015-01-02T03:04",
            absl::FormatCivilTime(absl::CivilMinute(2015, 1, 2, 3, 4, 5)));
  EXPECT_EQ("2015-01-02T03:04",
            absl::FormatCivilTime(absl::CivilMinute(2015, 1, 2, 3, 4)));
  EXPECT_EQ("2015-01-02T03:00",
            absl::FormatCivilTime(absl::CivilMinute(2015, 1, 2, 3)));
  EXPECT_EQ("2015-01-02T00:00",
            absl::FormatCivilTime(absl::CivilMinute(2015, 1, 2)));
  EXPECT_EQ("2015-01-01T00:00",
            absl::FormatCivilTime(absl::CivilMinute(2015, 1)));
  EXPECT_EQ("2015-01-01T00:00",
            absl::FormatCivilTime(absl::CivilMinute(2015)));

  EXPECT_EQ("2015-01-02T03",
            absl::FormatCivilTime(absl::CivilHour(2015, 1, 2, 3, 4, 5)));
  EXPECT_EQ("2015-01-02T03",
            absl::FormatCivilTime(absl::CivilHour(2015, 1, 2, 3, 4)));
  EXPECT_EQ("2015-01-02T03",
            absl::FormatCivilTime(absl::CivilHour(2015, 1, 2, 3)));
  EXPECT_EQ("2015-01-02T00",
            absl::FormatCivilTime(absl::CivilHour(2015, 1, 2)));
  EXPECT_EQ("2015-01-01T00",
            absl::FormatCivilTime(absl::CivilHour(2015, 1)));
  EXPECT_EQ("2015-01-01T00",
            absl::FormatCivilTime(absl::CivilHour(2015)));

  EXPECT_EQ("2015-01-02",
            absl::FormatCivilTime(absl::CivilDay(2015, 1, 2, 3, 4, 5)));
  EXPECT_EQ("2015-01-02",
            absl::FormatCivilTime(absl::CivilDay(2015, 1, 2, 3, 4)));
  EXPECT_EQ("2015-01-02",
            absl::FormatCivilTime(absl::CivilDay(2015, 1, 2, 3)));
  EXPECT_EQ("2015-01-02",
            absl::FormatCivilTime(absl::CivilDay(2015, 1, 2)));
  EXPECT_EQ("2015-01-01",
            absl::FormatCivilTime(absl::CivilDay(2015, 1)));
  EXPECT_EQ("2015-01-01",
            absl::FormatCivilTime(absl::CivilDay(2015)));

  EXPECT_EQ("2015-01",
            absl::FormatCivilTime(absl::CivilMonth(2015, 1, 2, 3, 4, 5)));
  EXPECT_EQ("2015-01",
            absl::FormatCivilTime(absl::CivilMonth(2015, 1, 2, 3, 4)));
  EXPECT_EQ("2015-01",
            absl::FormatCivilTime(absl::CivilMonth(2015, 1, 2, 3)));
  EXPECT_EQ("2015-01",
            absl::FormatCivilTime(absl::CivilMonth(2015, 1, 2)));
  EXPECT_EQ("2015-01",
            absl::FormatCivilTime(absl::CivilMonth(2015, 1)));
  EXPECT_EQ("2015-01",
            absl::FormatCivilTime(absl::CivilMonth(2015)));

  EXPECT_EQ("2015",
            absl::FormatCivilTime(absl::CivilYear(2015, 1, 2, 3, 4, 5)));
  EXPECT_EQ("2015",
            absl::FormatCivilTime(absl::CivilYear(2015, 1, 2, 3, 4)));
  EXPECT_EQ("2015",
            absl::FormatCivilTime(absl::CivilYear(2015, 1, 2, 3)));
  EXPECT_EQ("2015",
            absl::FormatCivilTime(absl::CivilYear(2015, 1, 2)));
  EXPECT_EQ("2015",
            absl::FormatCivilTime(absl::CivilYear(2015, 1)));
  EXPECT_EQ("2015",
            absl::FormatCivilTime(absl::CivilYear(2015)));
}

TEST(CivilTime, FieldsConstructionLimits) {
  const int kIntMax = std::numeric_limits<int>::max();
  EXPECT_EQ("2038-01-19T03:14:07",
            absl::FormatCivilTime(absl::CivilSecond(
                1970, 1, 1, 0, 0, kIntMax)));
  EXPECT_EQ("6121-02-11T05:21:07",
            absl::FormatCivilTime(absl::CivilSecond(
                1970, 1, 1, 0, kIntMax, kIntMax)));
  EXPECT_EQ("251104-11-20T12:21:07",
            absl::FormatCivilTime(absl::CivilSecond(
                1970, 1, 1, kIntMax, kIntMax, kIntMax)));
  EXPECT_EQ("6130715-05-30T12:21:07",
            absl::FormatCivilTime(absl::CivilSecond(
                1970, 1, kIntMax, kIntMax, kIntMax, kIntMax)));
  EXPECT_EQ("185087685-11-26T12:21:07",
            absl::FormatCivilTime(absl::CivilSecond(
                1970, kIntMax, kIntMax, kIntMax, kIntMax, kIntMax)));

  const int kIntMin = std::numeric_limits<int>::min();
  EXPECT_EQ("1901-12-13T20:45:52",
            absl::FormatCivilTime(absl::CivilSecond(
                1970, 1, 1, 0, 0, kIntMin)));
  EXPECT_EQ("-2182-11-20T18:37:52",
            absl::FormatCivilTime(absl::CivilSecond(
                1970, 1, 1, 0, kIntMin, kIntMin)));
  EXPECT_EQ("-247165-02-11T10:37:52",
            absl::FormatCivilTime(absl::CivilSecond(
                1970, 1, 1, kIntMin, kIntMin, kIntMin)));
  EXPECT_EQ("-6126776-08-01T10:37:52",
            absl::FormatCivilTime(absl::CivilSecond(
                1970, 1, kIntMin, kIntMin, kIntMin, kIntMin)));
  EXPECT_EQ("-185083747-10-31T10:37:52",
            absl::FormatCivilTime(absl::CivilSecond(
                1970, kIntMin, kIntMin, kIntMin, kIntMin, kIntMin)));
}

TEST(CivilTime, RangeLimits) {
  const absl::civil_year_t kYearMax =
      std::numeric_limits<absl::civil_year_t>::max();
  EXPECT_EQ(absl::CivilYear(kYearMax),
            absl::CivilYear::max());
  EXPECT_EQ(absl::CivilMonth(kYearMax, 12),
            absl::CivilMonth::max());
  EXPECT_EQ(absl::CivilDay(kYearMax, 12, 31),
            absl::CivilDay::max());
  EXPECT_EQ(absl::CivilHour(kYearMax, 12, 31, 23),
            absl::CivilHour::max());
  EXPECT_EQ(absl::CivilMinute(kYearMax, 12, 31, 23, 59),
            absl::CivilMinute::max());
  EXPECT_EQ(absl::CivilSecond(kYearMax, 12, 31, 23, 59, 59),
            absl::CivilSecond::max());

  const absl::civil_year_t kYearMin =
      std::numeric_limits<absl::civil_year_t>::min();
  EXPECT_EQ(absl::CivilYear(kYearMin),
            absl::CivilYear::min());
  EXPECT_EQ(absl::CivilMonth(kYearMin, 1),
            absl::CivilMonth::min());
  EXPECT_EQ(absl::CivilDay(kYearMin, 1, 1),
            absl::CivilDay::min());
  EXPECT_EQ(absl::CivilHour(kYearMin, 1, 1, 0),
            absl::CivilHour::min());
  EXPECT_EQ(absl::CivilMinute(kYearMin, 1, 1, 0, 0),
            absl::CivilMinute::min());
  EXPECT_EQ(absl::CivilSecond(kYearMin, 1, 1, 0, 0, 0),
            absl::CivilSecond::min());
}

TEST(CivilTime, ImplicitCrossAlignment) {
  absl::CivilYear year(2015);
  absl::CivilMonth month = year;
  absl::CivilDay day = month;
  absl::CivilHour hour = day;
  absl::CivilMinute minute = hour;
  absl::CivilSecond second = minute;

  second = year;
  EXPECT_EQ(second, year);
  second = month;
  EXPECT_EQ(second, month);
  second = day;
  EXPECT_EQ(second, day);
  second = hour;
  EXPECT_EQ(second, hour);
  second = minute;
  EXPECT_EQ(second, minute);

  minute = year;
  EXPECT_EQ(minute, year);
  minute = month;
  EXPECT_EQ(minute, month);
  minute = day;
  EXPECT_EQ(minute, day);
  minute = hour;
  EXPECT_EQ(minute, hour);

  hour = year;
  EXPECT_EQ(hour, year);
  hour = month;
  EXPECT_EQ(hour, month);
  hour = day;
  EXPECT_EQ(hour, day);

  day = year;
  EXPECT_EQ(day, year);
  day = month;
  EXPECT_EQ(day, month);

  month = year;
  EXPECT_EQ(month, year);

  // Ensures unsafe conversions are not allowed.
  EXPECT_FALSE(
      (std::is_convertible<absl::CivilSecond, absl::CivilMinute>::value));
  EXPECT_FALSE(
      (std::is_convertible<absl::CivilSecond, absl::CivilHour>::value));
  EXPECT_FALSE(
      (std::is_convertible<absl::CivilSecond, absl::CivilDay>::value));
  EXPECT_FALSE(
      (std::is_convertible<absl::CivilSecond, absl::CivilMonth>::value));
  EXPECT_FALSE(
      (std::is_convertible<absl::CivilSecond, absl::CivilYear>::value));

  EXPECT_FALSE(
      (std::is_convertible<absl::CivilMinute, absl::CivilHour>::value));
  EXPECT_FALSE(
      (std::is_convertible<absl::CivilMinute, absl::CivilDay>::value));
  EXPECT_FALSE(
      (std::is_convertible<absl::CivilMinute, absl::CivilMonth>::value));
  EXPECT_FALSE(
      (std::is_convertible<absl::CivilMinute, absl::CivilYear>::value));

  EXPECT_FALSE(
      (std::is_convertible<absl::CivilHour, absl::CivilDay>::value));
  EXPECT_FALSE(
      (std::is_convertible<absl::CivilHour, absl::CivilMonth>::value));
  EXPECT_FALSE(
      (std::is_convertible<absl::CivilHour, absl::CivilYear>::value));

  EXPECT_FALSE(
      (std::is_convertible<absl::CivilDay, absl::CivilMonth>::value));
  EXPECT_FALSE(
      (std::is_convertible<absl::CivilDay, absl::CivilYear>::value));

  EXPECT_FALSE(
      (std::is_convertible<absl::CivilMonth, absl::CivilYear>::value));
}

TEST(CivilTime, ExplicitCrossAlignment) {
  //
  // Assign from smaller units -> larger units
  //

  absl::CivilSecond second(2015, 1, 2, 3, 4, 5);
  EXPECT_EQ("2015-01-02T03:04:05", absl::FormatCivilTime(second));

  absl::CivilMinute minute(second);
  EXPECT_EQ("2015-01-02T03:04", absl::FormatCivilTime(minute));

  absl::CivilHour hour(minute);
  EXPECT_EQ("2015-01-02T03", absl::FormatCivilTime(hour));

  absl::CivilDay day(hour);
  EXPECT_EQ("2015-01-02", absl::FormatCivilTime(day));

  absl::CivilMonth month(day);
  EXPECT_EQ("2015-01", absl::FormatCivilTime(month));

  absl::CivilYear year(month);
  EXPECT_EQ("2015", absl::FormatCivilTime(year));

  //
  // Now assign from larger units -> smaller units
  //

  month = absl::CivilMonth(year);
  EXPECT_EQ("2015-01", absl::FormatCivilTime(month));

  day = absl::CivilDay(month);
  EXPECT_EQ("2015-01-01", absl::FormatCivilTime(day));

  hour = absl::CivilHour(day);
  EXPECT_EQ("2015-01-01T00", absl::FormatCivilTime(hour));

  minute = absl::CivilMinute(hour);
  EXPECT_EQ("2015-01-01T00:00", absl::FormatCivilTime(minute));

  second = absl::CivilSecond(minute);
  EXPECT_EQ("2015-01-01T00:00:00", absl::FormatCivilTime(second));
}

// Metafunction to test whether difference is allowed between two types.
template <typename T1, typename T2>
struct HasDiff {
  template <typename U1, typename U2>
  static std::false_type test(...);
  template <typename U1, typename U2>
  static std::true_type test(decltype(std::declval<U1>() - std::declval<U2>()));
  static constexpr bool value = decltype(test<T1, T2>(0))::value;
};

TEST(CivilTime, DisallowCrossAlignedDifference) {
  // Difference is allowed between types with the same alignment.
  static_assert(HasDiff<absl::CivilSecond, absl::CivilSecond>::value, "");
  static_assert(HasDiff<absl::CivilMinute, absl::CivilMinute>::value, "");
  static_assert(HasDiff<absl::CivilHour, absl::CivilHour>::value, "");
  static_assert(HasDiff<absl::CivilDay, absl::CivilDay>::value, "");
  static_assert(HasDiff<absl::CivilMonth, absl::CivilMonth>::value, "");
  static_assert(HasDiff<absl::CivilYear, absl::CivilYear>::value, "");

  // Difference is disallowed between types with different alignments.
  static_assert(!HasDiff<absl::CivilSecond, absl::CivilMinute>::value, "");
  static_assert(!HasDiff<absl::CivilSecond, absl::CivilHour>::value, "");
  static_assert(!HasDiff<absl::CivilSecond, absl::CivilDay>::value, "");
  static_assert(!HasDiff<absl::CivilSecond, absl::CivilMonth>::value, "");
  static_assert(!HasDiff<absl::CivilSecond, absl::CivilYear>::value, "");

  static_assert(!HasDiff<absl::CivilMinute, absl::CivilHour>::value, "");
  static_assert(!HasDiff<absl::CivilMinute, absl::CivilDay>::value, "");
  static_assert(!HasDiff<absl::CivilMinute, absl::CivilMonth>::value, "");
  static_assert(!HasDiff<absl::CivilMinute, absl::CivilYear>::value, "");

  static_assert(!HasDiff<absl::CivilHour, absl::CivilDay>::value, "");
  static_assert(!HasDiff<absl::CivilHour, absl::CivilMonth>::value, "");
  static_assert(!HasDiff<absl::CivilHour, absl::CivilYear>::value, "");

  static_assert(!HasDiff<absl::CivilDay, absl::CivilMonth>::value, "");
  static_assert(!HasDiff<absl::CivilDay, absl::CivilYear>::value, "");

  static_assert(!HasDiff<absl::CivilMonth, absl::CivilYear>::value, "");
}

TEST(CivilTime, ValueSemantics) {
  const absl::CivilHour a(2015, 1, 2, 3);
  const absl::CivilHour b = a;
  const absl::CivilHour c(b);
  absl::CivilHour d;
  d = c;
  EXPECT_EQ("2015-01-02T03", absl::FormatCivilTime(d));
}

TEST(CivilTime, Relational) {
  // Tests that the alignment unit is ignored in comparison.
  const absl::CivilYear year(2014);
  const absl::CivilMonth month(year);
  EXPECT_EQ(year, month);

#define TEST_RELATIONAL(OLDER, YOUNGER) \
  do {                                  \
    EXPECT_FALSE(OLDER < OLDER);        \
    EXPECT_FALSE(OLDER > OLDER);        \
    EXPECT_TRUE(OLDER >= OLDER);        \
    EXPECT_TRUE(OLDER <= OLDER);        \
    EXPECT_FALSE(YOUNGER < YOUNGER);    \
    EXPECT_FALSE(YOUNGER > YOUNGER);    \
    EXPECT_TRUE(YOUNGER >= YOUNGER);    \
    EXPECT_TRUE(YOUNGER <= YOUNGER);    \
    EXPECT_EQ(OLDER, OLDER);            \
    EXPECT_NE(OLDER, YOUNGER);          \
    EXPECT_LT(OLDER, YOUNGER);          \
    EXPECT_LE(OLDER, YOUNGER);          \
    EXPECT_GT(YOUNGER, OLDER);          \
    EXPECT_GE(YOUNGER, OLDER);          \
  } while (0)

  // Alignment is ignored in comparison (verified above), so CivilSecond is
  // used to test comparison in all field positions.
  TEST_RELATIONAL(absl::CivilSecond(2014, 1, 1, 0, 0, 0),
                  absl::CivilSecond(2015, 1, 1, 0, 0, 0));
  TEST_RELATIONAL(absl::CivilSecond(2014, 1, 1, 0, 0, 0),
                  absl::CivilSecond(2014, 2, 1, 0, 0, 0));
  TEST_RELATIONAL(absl::CivilSecond(2014, 1, 1, 0, 0, 0),
                  absl::CivilSecond(2014, 1, 2, 0, 0, 0));
  TEST_RELATIONAL(absl::CivilSecond(2014, 1, 1, 0, 0, 0),
                  absl::CivilSecond(2014, 1, 1, 1, 0, 0));
  TEST_RELATIONAL(absl::CivilSecond(2014, 1, 1, 1, 0, 0),
                  absl::CivilSecond(2014, 1, 1, 1, 1, 0));
  TEST_RELATIONAL(absl::CivilSecond(2014, 1, 1, 1, 1, 0),
                  absl::CivilSecond(2014, 1, 1, 1, 1, 1));

  // Tests the relational operators of two different civil-time types.
  TEST_RELATIONAL(absl::CivilDay(2014, 1, 1),
                  absl::CivilMinute(2014, 1, 1, 1, 1));
  TEST_RELATIONAL(absl::CivilDay(2014, 1, 1),
                  absl::CivilMonth(2014, 2));

#undef TEST_RELATIONAL
}

TEST(CivilTime, Arithmetic) {
  absl::CivilSecond second(2015, 1, 2, 3, 4, 5);
  EXPECT_EQ("2015-01-02T03:04:06", absl::FormatCivilTime(second += 1));
  EXPECT_EQ("2015-01-02T03:04:07", absl::FormatCivilTime(second + 1));
  EXPECT_EQ("2015-01-02T03:04:08", absl::FormatCivilTime(2 + second));
  EXPECT_EQ("2015-01-02T03:04:05", absl::FormatCivilTime(second - 1));
  EXPECT_EQ("2015-01-02T03:04:05", absl::FormatCivilTime(second -= 1));
  EXPECT_EQ("2015-01-02T03:04:05", absl::FormatCivilTime(second++));
  EXPECT_EQ("2015-01-02T03:04:07", absl::FormatCivilTime(++second));
  EXPECT_EQ("2015-01-02T03:04:07", absl::FormatCivilTime(second--));
  EXPECT_EQ("2015-01-02T03:04:05", absl::FormatCivilTime(--second));

  absl::CivilMinute minute(2015, 1, 2, 3, 4);
  EXPECT_EQ("2015-01-02T03:05", absl::FormatCivilTime(minute += 1));
  EXPECT_EQ("2015-01-02T03:06", absl::FormatCivilTime(minute + 1));
  EXPECT_EQ("2015-01-02T03:07", absl::FormatCivilTime(2 + minute));
  EXPECT_EQ("2015-01-02T03:04", absl::FormatCivilTime(minute - 1));
  EXPECT_EQ("2015-01-02T03:04", absl::FormatCivilTime(minute -= 1));
  EXPECT_EQ("2015-01-02T03:04", absl::FormatCivilTime(minute++));
  EXPECT_EQ("2015-01-02T03:06", absl::FormatCivilTime(++minute));
  EXPECT_EQ("2015-01-02T03:06", absl::FormatCivilTime(minute--));
  EXPECT_EQ("2015-01-02T03:04", absl::FormatCivilTime(--minute));

  absl::CivilHour hour(2015, 1, 2, 3);
  EXPECT_EQ("2015-01-02T04", absl::FormatCivilTime(hour += 1));
  EXPECT_EQ("2015-01-02T05", absl::FormatCivilTime(hour + 1));
  EXPECT_EQ("2015-01-02T06", absl::FormatCivilTime(2 + hour));
  EXPECT_EQ("2015-01-02T03", absl::FormatCivilTime(hour - 1));
  EXPECT_EQ("2015-01-02T03", absl::FormatCivilTime(hour -= 1));
  EXPECT_EQ("2015-01-02T03", absl::FormatCivilTime(hour++));
  EXPECT_EQ("2015-01-02T05", absl::FormatCivilTime(++hour));
  EXPECT_EQ("2015-01-02T05", absl::FormatCivilTime(hour--));
  EXPECT_EQ("2015-01-02T03", absl::FormatCivilTime(--hour));

  absl::CivilDay day(2015, 1, 2);
  EXPECT_EQ("2015-01-03", absl::FormatCivilTime(day += 1));
  EXPECT_EQ("2015-01-04", absl::FormatCivilTime(day + 1));
  EXPECT_EQ("2015-01-05", absl::FormatCivilTime(2 + day));
  EXPECT_EQ("2015-01-02", absl::FormatCivilTime(day - 1));
  EXPECT_EQ("2015-01-02", absl::FormatCivilTime(day -= 1));
  EXPECT_EQ("2015-01-02", absl::FormatCivilTime(day++));
  EXPECT_EQ("2015-01-04", absl::FormatCivilTime(++day));
  EXPECT_EQ("2015-01-04", absl::FormatCivilTime(day--));
  EXPECT_EQ("2015-01-02", absl::FormatCivilTime(--day));

  absl::CivilMonth month(2015, 1);
  EXPECT_EQ("2015-02", absl::FormatCivilTime(month += 1));
  EXPECT_EQ("2015-03", absl::FormatCivilTime(month + 1));
  EXPECT_EQ("2015-04", absl::FormatCivilTime(2 + month));
  EXPECT_EQ("2015-01", absl::FormatCivilTime(month - 1));
  EXPECT_EQ("2015-01", absl::FormatCivilTime(month -= 1));
  EXPECT_EQ("2015-01", absl::FormatCivilTime(month++));
  EXPECT_EQ("2015-03", absl::FormatCivilTime(++month));
  EXPECT_EQ("2015-03", absl::FormatCivilTime(month--));
  EXPECT_EQ("2015-01", absl::FormatCivilTime(--month));

  absl::CivilYear year(2015);
  EXPECT_EQ("2016", absl::FormatCivilTime(year += 1));
  EXPECT_EQ("2017", absl::FormatCivilTime(year + 1));
  EXPECT_EQ("2018", absl::FormatCivilTime(2 + year));
  EXPECT_EQ("2015", absl::FormatCivilTime(year - 1));
  EXPECT_EQ("2015", absl::FormatCivilTime(year -= 1));
  EXPECT_EQ("2015", absl::FormatCivilTime(year++));
  EXPECT_EQ("2017", absl::FormatCivilTime(++year));
  EXPECT_EQ("2017", absl::FormatCivilTime(year--));
  EXPECT_EQ("2015", absl::FormatCivilTime(--year));
}

TEST(CivilTime, ArithmeticLimits) {
  const int kIntMax = std::numeric_limits<int>::max();
  const int kIntMin = std::numeric_limits<int>::min();

  absl::CivilSecond second(1970, 1, 1, 0, 0, 0);
  second += kIntMax;
  EXPECT_EQ("2038-01-19T03:14:07", absl::FormatCivilTime(second));
  second -= kIntMax;
  EXPECT_EQ("1970-01-01T00:00:00", absl::FormatCivilTime(second));
  second += kIntMin;
  EXPECT_EQ("1901-12-13T20:45:52", absl::FormatCivilTime(second));
  second -= kIntMin;
  EXPECT_EQ("1970-01-01T00:00:00", absl::FormatCivilTime(second));

  absl::CivilMinute minute(1970, 1, 1, 0, 0);
  minute += kIntMax;
  EXPECT_EQ("6053-01-23T02:07", absl::FormatCivilTime(minute));
  minute -= kIntMax;
  EXPECT_EQ("1970-01-01T00:00", absl::FormatCivilTime(minute));
  minute += kIntMin;
  EXPECT_EQ("-2114-12-08T21:52", absl::FormatCivilTime(minute));
  minute -= kIntMin;
  EXPECT_EQ("1970-01-01T00:00", absl::FormatCivilTime(minute));

  absl::CivilHour hour(1970, 1, 1, 0);
  hour += kIntMax;
  EXPECT_EQ("246953-10-09T07", absl::FormatCivilTime(hour));
  hour -= kIntMax;
  EXPECT_EQ("1970-01-01T00", absl::FormatCivilTime(hour));
  hour += kIntMin;
  EXPECT_EQ("-243014-03-24T16", absl::FormatCivilTime(hour));
  hour -= kIntMin;
  EXPECT_EQ("1970-01-01T00", absl::FormatCivilTime(hour));

  absl::CivilDay day(1970, 1, 1);
  day += kIntMax;
  EXPECT_EQ("5881580-07-11", absl::FormatCivilTime(day));
  day -= kIntMax;
  EXPECT_EQ("1970-01-01", absl::FormatCivilTime(day));
  day += kIntMin;
  EXPECT_EQ("-5877641-06-23", absl::FormatCivilTime(day));
  day -= kIntMin;
  EXPECT_EQ("1970-01-01", absl::FormatCivilTime(day));

  absl::CivilMonth month(1970, 1);
  month += kIntMax;
  EXPECT_EQ("178958940-08", absl::FormatCivilTime(month));
  month -= kIntMax;
  EXPECT_EQ("1970-01", absl::FormatCivilTime(month));
  month += kIntMin;
  EXPECT_EQ("-178955001-05", absl::FormatCivilTime(month));
  month -= kIntMin;
  EXPECT_EQ("1970-01", absl::FormatCivilTime(month));

  absl::CivilYear year(0);
  year += kIntMax;
  EXPECT_EQ("2147483647", absl::FormatCivilTime(year));
  year -= kIntMax;
  EXPECT_EQ("0", absl::FormatCivilTime(year));
  year += kIntMin;
  EXPECT_EQ("-2147483648", absl::FormatCivilTime(year));
  year -= kIntMin;
  EXPECT_EQ("0", absl::FormatCivilTime(year));
}

TEST(CivilTime, Difference) {
  absl::CivilSecond second(2015, 1, 2, 3, 4, 5);
  EXPECT_EQ(0, second - second);
  EXPECT_EQ(10, (second + 10) - second);
  EXPECT_EQ(-10, (second - 10) - second);

  absl::CivilMinute minute(2015, 1, 2, 3, 4);
  EXPECT_EQ(0, minute - minute);
  EXPECT_EQ(10, (minute + 10) - minute);
  EXPECT_EQ(-10, (minute - 10) - minute);

  absl::CivilHour hour(2015, 1, 2, 3);
  EXPECT_EQ(0, hour - hour);
  EXPECT_EQ(10, (hour + 10) - hour);
  EXPECT_EQ(-10, (hour - 10) - hour);

  absl::CivilDay day(2015, 1, 2);
  EXPECT_EQ(0, day - day);
  EXPECT_EQ(10, (day + 10) - day);
  EXPECT_EQ(-10, (day - 10) - day);

  absl::CivilMonth month(2015, 1);
  EXPECT_EQ(0, month - month);
  EXPECT_EQ(10, (month + 10) - month);
  EXPECT_EQ(-10, (month - 10) - month);

  absl::CivilYear year(2015);
  EXPECT_EQ(0, year - year);
  EXPECT_EQ(10, (year + 10) - year);
  EXPECT_EQ(-10, (year - 10) - year);
}

TEST(CivilTime, DifferenceLimits) {
  const absl::civil_diff_t kDiffMax =
      std::numeric_limits<absl::civil_diff_t>::max();
  const absl::civil_diff_t kDiffMin =
      std::numeric_limits<absl::civil_diff_t>::min();

  // Check day arithmetic at the end of the year range.
  const absl::CivilDay max_day(kDiffMax, 12, 31);
  EXPECT_EQ(1, max_day - (max_day - 1));
  EXPECT_EQ(-1, (max_day - 1) - max_day);

  // Check day arithmetic at the start of the year range.
  const absl::CivilDay min_day(kDiffMin, 1, 1);
  EXPECT_EQ(1, (min_day + 1) - min_day);
  EXPECT_EQ(-1, min_day - (min_day + 1));

  // Check the limits of the return value.
  const absl::CivilDay d1(1970, 1, 1);
  const absl::CivilDay d2(25252734927768524, 7, 27);
  EXPECT_EQ(kDiffMax, d2 - d1);
  EXPECT_EQ(kDiffMin, d1 - (d2 + 1));
}

TEST(CivilTime, Properties) {
  absl::CivilSecond ss(2015, 2, 3, 4, 5, 6);
  EXPECT_EQ(2015, ss.year());
  EXPECT_EQ(2, ss.month());
  EXPECT_EQ(3, ss.day());
  EXPECT_EQ(4, ss.hour());
  EXPECT_EQ(5, ss.minute());
  EXPECT_EQ(6, ss.second());
  EXPECT_EQ(absl::Weekday::tuesday, absl::GetWeekday(ss));
  EXPECT_EQ(34, absl::GetYearDay(ss));

  absl::CivilMinute mm(2015, 2, 3, 4, 5, 6);
  EXPECT_EQ(2015, mm.year());
  EXPECT_EQ(2, mm.month());
  EXPECT_EQ(3, mm.day());
  EXPECT_EQ(4, mm.hour());
  EXPECT_EQ(5, mm.minute());
  EXPECT_EQ(0, mm.second());
  EXPECT_EQ(absl::Weekday::tuesday, absl::GetWeekday(mm));
  EXPECT_EQ(34, absl::GetYearDay(mm));

  absl::CivilHour hh(2015, 2, 3, 4, 5, 6);
  EXPECT_EQ(2015, hh.year());
  EXPECT_EQ(2, hh.month());
  EXPECT_EQ(3, hh.day());
  EXPECT_EQ(4, hh.hour());
  EXPECT_EQ(0, hh.minute());
  EXPECT_EQ(0, hh.second());
  EXPECT_EQ(absl::Weekday::tuesday, absl::GetWeekday(hh));
  EXPECT_EQ(34, absl::GetYearDay(hh));

  absl::CivilDay d(2015, 2, 3, 4, 5, 6);
  EXPECT_EQ(2015, d.year());
  EXPECT_EQ(2, d.month());
  EXPECT_EQ(3, d.day());
  EXPECT_EQ(0, d.hour());
  EXPECT_EQ(0, d.minute());
  EXPECT_EQ(0, d.second());
  EXPECT_EQ(absl::Weekday::tuesday, absl::GetWeekday(d));
  EXPECT_EQ(34, absl::GetYearDay(d));

  absl::CivilMonth m(2015, 2, 3, 4, 5, 6);
  EXPECT_EQ(2015, m.year());
  EXPECT_EQ(2, m.month());
  EXPECT_EQ(1, m.day());
  EXPECT_EQ(0, m.hour());
  EXPECT_EQ(0, m.minute());
  EXPECT_EQ(0, m.second());
  EXPECT_EQ(absl::Weekday::sunday, absl::GetWeekday(m));
  EXPECT_EQ(32, absl::GetYearDay(m));

  absl::CivilYear y(2015, 2, 3, 4, 5, 6);
  EXPECT_EQ(2015, y.year());
  EXPECT_EQ(1, y.month());
  EXPECT_EQ(1, y.day());
  EXPECT_EQ(0, y.hour());
  EXPECT_EQ(0, y.minute());
  EXPECT_EQ(0, y.second());
  EXPECT_EQ(absl::Weekday::thursday, absl::GetWeekday(y));
  EXPECT_EQ(1, absl::GetYearDay(y));
}

TEST(CivilTime, Format) {
  absl::CivilSecond ss;
  EXPECT_EQ("1970-01-01T00:00:00", absl::FormatCivilTime(ss));

  absl::CivilMinute mm;
  EXPECT_EQ("1970-01-01T00:00", absl::FormatCivilTime(mm));

  absl::CivilHour hh;
  EXPECT_EQ("1970-01-01T00", absl::FormatCivilTime(hh));

  absl::CivilDay d;
  EXPECT_EQ("1970-01-01", absl::FormatCivilTime(d));

  absl::CivilMonth m;
  EXPECT_EQ("1970-01", absl::FormatCivilTime(m));

  absl::CivilYear y;
  EXPECT_EQ("1970", absl::FormatCivilTime(y));
}

TEST(CivilTime, Parse) {
  absl::CivilSecond ss;
  absl::CivilMinute mm;
  absl::CivilHour hh;
  absl::CivilDay d;
  absl::CivilMonth m;
  absl::CivilYear y;

  // CivilSecond OK; others fail
  EXPECT_TRUE(absl::ParseCivilTime("2015-01-02T03:04:05", &ss));
  EXPECT_EQ("2015-01-02T03:04:05", absl::FormatCivilTime(ss));
  EXPECT_FALSE(absl::ParseCivilTime("2015-01-02T03:04:05", &mm));
  EXPECT_FALSE(absl::ParseCivilTime("2015-01-02T03:04:05", &hh));
  EXPECT_FALSE(absl::ParseCivilTime("2015-01-02T03:04:05", &d));
  EXPECT_FALSE(absl::ParseCivilTime("2015-01-02T03:04:05", &m));
  EXPECT_FALSE(absl::ParseCivilTime("2015-01-02T03:04:05", &y));

  // CivilMinute OK; others fail
  EXPECT_FALSE(absl::ParseCivilTime("2015-01-02T03:04", &ss));
  EXPECT_TRUE(absl::ParseCivilTime("2015-01-02T03:04", &mm));
  EXPECT_EQ("2015-01-02T03:04", absl::FormatCivilTime(mm));
  EXPECT_FALSE(absl::ParseCivilTime("2015-01-02T03:04", &hh));
  EXPECT_FALSE(absl::ParseCivilTime("2015-01-02T03:04", &d));
  EXPECT_FALSE(absl::ParseCivilTime("2015-01-02T03:04", &m));
  EXPECT_FALSE(absl::ParseCivilTime("2015-01-02T03:04", &y));

  // CivilHour OK; others fail
  EXPECT_FALSE(absl::ParseCivilTime("2015-01-02T03", &ss));
  EXPECT_FALSE(absl::ParseCivilTime("2015-01-02T03", &mm));
  EXPECT_TRUE(absl::ParseCivilTime("2015-01-02T03", &hh));
  EXPECT_EQ("2015-01-02T03", absl::FormatCivilTime(hh));
  EXPECT_FALSE(absl::ParseCivilTime("2015-01-02T03", &d));
  EXPECT_FALSE(absl::ParseCivilTime("2015-01-02T03", &m));
  EXPECT_FALSE(absl::ParseCivilTime("2015-01-02T03", &y));

  // CivilDay OK; others fail
  EXPECT_FALSE(absl::ParseCivilTime("2015-01-02", &ss));
  EXPECT_FALSE(absl::ParseCivilTime("2015-01-02", &mm));
  EXPECT_FALSE(absl::ParseCivilTime("2015-01-02", &hh));
  EXPECT_TRUE(absl::ParseCivilTime("2015-01-02", &d));
  EXPECT_EQ("2015-01-02", absl::FormatCivilTime(d));
  EXPECT_FALSE(absl::ParseCivilTime("2015-01-02", &m));
  EXPECT_FALSE(absl::ParseCivilTime("2015-01-02", &y));

  // CivilMonth OK; others fail
  EXPECT_FALSE(absl::ParseCivilTime("2015-01", &ss));
  EXPECT_FALSE(absl::ParseCivilTime("2015-01", &mm));
  EXPECT_FALSE(absl::ParseCivilTime("2015-01", &hh));
  EXPECT_FALSE(absl::ParseCivilTime("2015-01", &d));
  EXPECT_TRUE(absl::ParseCivilTime("2015-01", &m));
  EXPECT_EQ("2015-01", absl::FormatCivilTime(m));
  EXPECT_FALSE(absl::ParseCivilTime("2015-01", &y));

  // CivilYear OK; others fail
  EXPECT_FALSE(absl::ParseCivilTime("2015", &ss));
  EXPECT_FALSE(absl::ParseCivilTime("2015", &mm));
  EXPECT_FALSE(absl::ParseCivilTime("2015", &hh));
  EXPECT_FALSE(absl::ParseCivilTime("2015", &d));
  EXPECT_FALSE(absl::ParseCivilTime("2015", &m));
  EXPECT_TRUE(absl::ParseCivilTime("2015", &y));
  EXPECT_EQ("2015", absl::FormatCivilTime(y));
}

TEST(CivilTime, FormatAndParseLenient) {
  absl::CivilSecond ss;
  EXPECT_EQ("1970-01-01T00:00:00", absl::FormatCivilTime(ss));

  absl::CivilMinute mm;
  EXPECT_EQ("1970-01-01T00:00", absl::FormatCivilTime(mm));

  absl::CivilHour hh;
  EXPECT_EQ("1970-01-01T00", absl::FormatCivilTime(hh));

  absl::CivilDay d;
  EXPECT_EQ("1970-01-01", absl::FormatCivilTime(d));

  absl::CivilMonth m;
  EXPECT_EQ("1970-01", absl::FormatCivilTime(m));

  absl::CivilYear y;
  EXPECT_EQ("1970", absl::FormatCivilTime(y));

  EXPECT_TRUE(absl::ParseLenientCivilTime("2015-01-02T03:04:05", &ss));
  EXPECT_EQ("2015-01-02T03:04:05", absl::FormatCivilTime(ss));

  EXPECT_TRUE(absl::ParseLenientCivilTime("2015-01-02T03:04:05", &mm));
  EXPECT_EQ("2015-01-02T03:04", absl::FormatCivilTime(mm));

  EXPECT_TRUE(absl::ParseLenientCivilTime("2015-01-02T03:04:05", &hh));
  EXPECT_EQ("2015-01-02T03", absl::FormatCivilTime(hh));

  EXPECT_TRUE(absl::ParseLenientCivilTime("2015-01-02T03:04:05", &d));
  EXPECT_EQ("2015-01-02", absl::FormatCivilTime(d));

  EXPECT_TRUE(absl::ParseLenientCivilTime("2015-01-02T03:04:05", &m));
  EXPECT_EQ("2015-01", absl::FormatCivilTime(m));

  EXPECT_TRUE(absl::ParseLenientCivilTime("2015-01-02T03:04:05", &y));
  EXPECT_EQ("2015", absl::FormatCivilTime(y));
}

TEST(CivilTime, ParseEdgeCases) {
  absl::CivilSecond ss;
  EXPECT_TRUE(
      absl::ParseLenientCivilTime("9223372036854775807-12-31T23:59:59", &ss));
  EXPECT_EQ("9223372036854775807-12-31T23:59:59", absl::FormatCivilTime(ss));
  EXPECT_TRUE(
      absl::ParseLenientCivilTime("-9223372036854775808-01-01T00:00:00", &ss));
  EXPECT_EQ("-9223372036854775808-01-01T00:00:00", absl::FormatCivilTime(ss));

  absl::CivilMinute mm;
  EXPECT_TRUE(
      absl::ParseLenientCivilTime("9223372036854775807-12-31T23:59", &mm));
  EXPECT_EQ("9223372036854775807-12-31T23:59", absl::FormatCivilTime(mm));
  EXPECT_TRUE(
      absl::ParseLenientCivilTime("-9223372036854775808-01-01T00:00", &mm));
  EXPECT_EQ("-9223372036854775808-01-01T00:00", absl::FormatCivilTime(mm));

  absl::CivilHour hh;
  EXPECT_TRUE(
      absl::ParseLenientCivilTime("9223372036854775807-12-31T23", &hh));
  EXPECT_EQ("9223372036854775807-12-31T23", absl::FormatCivilTime(hh));
  EXPECT_TRUE(
      absl::ParseLenientCivilTime("-9223372036854775808-01-01T00", &hh));
  EXPECT_EQ("-9223372036854775808-01-01T00", absl::FormatCivilTime(hh));

  absl::CivilDay d;
  EXPECT_TRUE(absl::ParseLenientCivilTime("9223372036854775807-12-31", &d));
  EXPECT_EQ("9223372036854775807-12-31", absl::FormatCivilTime(d));
  EXPECT_TRUE(absl::ParseLenientCivilTime("-9223372036854775808-01-01", &d));
  EXPECT_EQ("-9223372036854775808-01-01", absl::FormatCivilTime(d));

  absl::CivilMonth m;
  EXPECT_TRUE(absl::ParseLenientCivilTime("9223372036854775807-12", &m));
  EXPECT_EQ("9223372036854775807-12", absl::FormatCivilTime(m));
  EXPECT_TRUE(absl::ParseLenientCivilTime("-9223372036854775808-01", &m));
  EXPECT_EQ("-9223372036854775808-01", absl::FormatCivilTime(m));

  absl::CivilYear y;
  EXPECT_TRUE(absl::ParseLenientCivilTime("9223372036854775807", &y));
  EXPECT_EQ("9223372036854775807", absl::FormatCivilTime(y));
  EXPECT_TRUE(absl::ParseLenientCivilTime("-9223372036854775808", &y));
  EXPECT_EQ("-9223372036854775808", absl::FormatCivilTime(y));

  // Tests some valid, but interesting, cases
  EXPECT_TRUE(absl::ParseLenientCivilTime("0", &ss)) << ss;
  EXPECT_EQ(absl::CivilYear(0), ss);
  EXPECT_TRUE(absl::ParseLenientCivilTime("0-1", &ss)) << ss;
  EXPECT_EQ(absl::CivilMonth(0, 1), ss);
  EXPECT_TRUE(absl::ParseLenientCivilTime(" 2015 ", &ss)) << ss;
  EXPECT_EQ(absl::CivilYear(2015), ss);
  EXPECT_TRUE(absl::ParseLenientCivilTime(" 2015-6 ", &ss)) << ss;
  EXPECT_EQ(absl::CivilMonth(2015, 6), ss);
  EXPECT_TRUE(absl::ParseLenientCivilTime("2015-6-7", &ss)) << ss;
  EXPECT_EQ(absl::CivilDay(2015, 6, 7), ss);
  EXPECT_TRUE(absl::ParseLenientCivilTime(" 2015-6-7 ", &ss)) << ss;
  EXPECT_EQ(absl::CivilDay(2015, 6, 7), ss);
  EXPECT_TRUE(absl::ParseLenientCivilTime("2015-06-07T10:11:12 ", &ss)) << ss;
  EXPECT_EQ(absl::CivilSecond(2015, 6, 7, 10, 11, 12), ss);
  EXPECT_TRUE(absl::ParseLenientCivilTime(" 2015-06-07T10:11:12 ", &ss)) << ss;
  EXPECT_EQ(absl::CivilSecond(2015, 6, 7, 10, 11, 12), ss);
  EXPECT_TRUE(absl::ParseLenientCivilTime("-01-01", &ss)) << ss;
  EXPECT_EQ(absl::CivilMonth(-1, 1), ss);

  // Tests some invalid cases
  EXPECT_FALSE(absl::ParseLenientCivilTime("01-01-2015", &ss)) << ss;
  EXPECT_FALSE(absl::ParseLenientCivilTime("2015-", &ss)) << ss;
  EXPECT_FALSE(absl::ParseLenientCivilTime("0xff-01", &ss)) << ss;
  EXPECT_FALSE(absl::ParseLenientCivilTime("2015-02-30T04:05:06", &ss)) << ss;
  EXPECT_FALSE(absl::ParseLenientCivilTime("2015-02-03T04:05:96", &ss)) << ss;
  EXPECT_FALSE(absl::ParseLenientCivilTime("X2015-02-03T04:05:06", &ss)) << ss;
  EXPECT_FALSE(absl::ParseLenientCivilTime("2015-02-03T04:05:003", &ss)) << ss;
  EXPECT_FALSE(absl::ParseLenientCivilTime("2015 -02-03T04:05:06", &ss)) << ss;
  EXPECT_FALSE(absl::ParseLenientCivilTime("2015-02-03-04:05:06", &ss)) << ss;
  EXPECT_FALSE(absl::ParseLenientCivilTime("2015:02:03T04-05-06", &ss)) << ss;
  EXPECT_FALSE(absl::ParseLenientCivilTime("9223372036854775808", &y)) << y;
}

TEST(CivilTime, AbslStringify) {
  EXPECT_EQ("2015-01-02T03:04:05",
            absl::StrFormat("%v", absl::CivilSecond(2015, 1, 2, 3, 4, 5)));

  EXPECT_EQ("2015-01-02T03:04",
            absl::StrFormat("%v", absl::CivilMinute(2015, 1, 2, 3, 4)));

  EXPECT_EQ("2015-01-02T03",
            absl::StrFormat("%v", absl::CivilHour(2015, 1, 2, 3)));

  EXPECT_EQ("2015-01-02", absl::StrFormat("%v", absl::CivilDay(2015, 1, 2)));

  EXPECT_EQ("2015-01", absl::StrFormat("%v", absl::CivilMonth(2015, 1)));

  EXPECT_EQ("2015", absl::StrFormat("%v", absl::CivilYear(2015)));
}

TEST(CivilTime, OutputStream) {
  absl::CivilSecond cs(2016, 2, 3, 4, 5, 6);
  {
    std::stringstream ss;
    ss << std::left << std::setfill('.');
    ss << std::setw(3) << 'X';
    ss << std::setw(21) << absl::CivilYear(cs);
    ss << std::setw(3) << 'X';
    EXPECT_EQ("X..2016.................X..", ss.str());
  }
  {
    std::stringstream ss;
    ss << std::left << std::setfill('.');
    ss << std::setw(3) << 'X';
    ss << std::setw(21) << absl::CivilMonth(cs);
    ss << std::setw(3) << 'X';
    EXPECT_EQ("X..2016-02..............X..", ss.str());
  }
  {
    std::stringstream ss;
    ss << std::left << std::setfill('.');
    ss << std::setw(3) << 'X';
    ss << std::setw(21) << absl::CivilDay(cs);
    ss << std::setw(3) << 'X';
    EXPECT_EQ("X..2016-02-03...........X..", ss.str());
  }
  {
    std::stringstream ss;
    ss << std::left << std::setfill('.');
    ss << std::setw(3) << 'X';
    ss << std::setw(21) << absl::CivilHour(cs);
    ss << std::setw(3) << 'X';
    EXPECT_EQ("X..2016-02-03T04........X..", ss.str());
  }
  {
    std::stringstream ss;
    ss << std::left << std::setfill('.');
    ss << std::setw(3) << 'X';
    ss << std::setw(21) << absl::CivilMinute(cs);
    ss << std::setw(3) << 'X';
    EXPECT_EQ("X..2016-02-03T04:05.....X..", ss.str());
  }
  {
    std::stringstream ss;
    ss << std::left << std::setfill('.');
    ss << std::setw(3) << 'X';
    ss << std::setw(21) << absl::CivilSecond(cs);
    ss << std::setw(3) << 'X';
    EXPECT_EQ("X..2016-02-03T04:05:06..X..", ss.str());
  }
  {
    std::stringstream ss;
    ss << std::left << std::setfill('.');
    ss << std::setw(3) << 'X';
    ss << std::setw(21) << absl::Weekday::wednesday;
    ss << std::setw(3) << 'X';
    EXPECT_EQ("X..Wednesday............X..", ss.str());
  }
}

TEST(CivilTime, Weekday) {
  absl::CivilDay d(1970, 1, 1);
  EXPECT_EQ(absl::Weekday::thursday, absl::GetWeekday(d)) << d;

  // We used to get this wrong for years < -30.
  d = absl::CivilDay(-31, 12, 24);
  EXPECT_EQ(absl::Weekday::wednesday, absl::GetWeekday(d)) << d;
}

TEST(CivilTime, NextPrevWeekday) {
  // Jan 1, 1970 was a Thursday.
  const absl::CivilDay thursday(1970, 1, 1);

  // Thursday -> Thursday
  absl::CivilDay d = absl::NextWeekday(thursday, absl::Weekday::thursday);
  EXPECT_EQ(7, d - thursday) << d;
  EXPECT_EQ(d - 14, absl::PrevWeekday(thursday, absl::Weekday::thursday));

  // Thursday -> Friday
  d = absl::NextWeekday(thursday, absl::Weekday::friday);
  EXPECT_EQ(1, d - thursday) << d;
  EXPECT_EQ(d - 7, absl::PrevWeekday(thursday, absl::Weekday::friday));

  // Thursday -> Saturday
  d = absl::NextWeekday(thursday, absl::Weekday::saturday);
  EXPECT_EQ(2, d - thursday) << d;
  EXPECT_EQ(d - 7, absl::PrevWeekday(thursday, absl::Weekday::saturday));

  // Thursday -> Sunday
  d = absl::NextWeekday(thursday, absl::Weekday::sunday);
  EXPECT_EQ(3, d - thursday) << d;
  EXPECT_EQ(d - 7, absl::PrevWeekday(thursday, absl::Weekday::sunday));

  // Thursday -> Monday
  d = absl::NextWeekday(thursday, absl::Weekday::monday);
  EXPECT_EQ(4, d - thursday) << d;
  EXPECT_EQ(d - 7, absl::PrevWeekday(thursday, absl::Weekday::monday));

  // Thursday -> Tuesday
  d = absl::NextWeekday(thursday, absl::Weekday::tuesday);
  EXPECT_EQ(5, d - thursday) << d;
  EXPECT_EQ(d - 7, absl::PrevWeekday(thursday, absl::Weekday::tuesday));

  // Thursday -> Wednesday
  d = absl::NextWeekday(thursday, absl::Weekday::wednesday);
  EXPECT_EQ(6, d - thursday) << d;
  EXPECT_EQ(d - 7, absl::PrevWeekday(thursday, absl::Weekday::wednesday));
}

// NOTE: Run this with --copt=-ftrapv to detect overflow problems.
TEST(CivilTime, DifferenceWithHugeYear) {
  absl::CivilDay d1(9223372036854775807, 1, 1);
  absl::CivilDay d2(9223372036854775807, 12, 31);
  EXPECT_EQ(364, d2 - d1);

  d1 = absl::CivilDay(-9223372036854775807 - 1, 1, 1);
  d2 = absl::CivilDay(-9223372036854775807 - 1, 12, 31);
  EXPECT_EQ(365, d2 - d1);

  // Check the limits of the return value at the end of the year range.
  d1 = absl::CivilDay(9223372036854775807, 1, 1);
  d2 = absl::CivilDay(9198119301927009252, 6, 6);
  EXPECT_EQ(9223372036854775807, d1 - d2);
  d2 = d2 - 1;
  EXPECT_EQ(-9223372036854775807 - 1, d2 - d1);

  // Check the limits of the return value at the start of the year range.
  d1 = absl::CivilDay(-9223372036854775807 - 1, 1, 1);
  d2 = absl::CivilDay(-9198119301927009254, 7, 28);
  EXPECT_EQ(9223372036854775807, d2 - d1);
  d2 = d2 + 1;
  EXPECT_EQ(-9223372036854775807 - 1, d1 - d2);

  // Check the limits of the return value from either side of year 0.
  d1 = absl::CivilDay(-12626367463883278, 9, 3);
  d2 = absl::CivilDay(12626367463883277, 3, 28);
  EXPECT_EQ(9223372036854775807, d2 - d1);
  d2 = d2 + 1;
  EXPECT_EQ(-9223372036854775807 - 1, d1 - d2);
}

// NOTE: Run this with --copt=-ftrapv to detect overflow problems.
TEST(CivilTime, DifferenceNoIntermediateOverflow) {
  // The difference up to the minute field would be below the minimum
  // int64_t, but the 52 extra seconds brings us back to the minimum.
  absl::CivilSecond s1(-292277022657, 1, 27, 8, 29 - 1, 52);
  absl::CivilSecond s2(1970, 1, 1, 0, 0 - 1, 0);
  EXPECT_EQ(-9223372036854775807 - 1, s1 - s2);

  // The difference up to the minute field would be above the maximum
  // int64_t, but the -53 extra seconds brings us back to the maximum.
  s1 = absl::CivilSecond(292277026596, 12, 4, 15, 30, 7 - 7);
  s2 = absl::CivilSecond(1970, 1, 1, 0, 0, 0 - 7);
  EXPECT_EQ(9223372036854775807, s1 - s2);
}

TEST(CivilTime, NormalizeSimpleOverflow) {
  absl::CivilSecond cs;
  cs = absl::CivilSecond(2013, 11, 15, 16, 32, 59 + 1);
  EXPECT_EQ("2013-11-15T16:33:00", absl::FormatCivilTime(cs));
  cs = absl::CivilSecond(2013, 11, 15, 16, 59 + 1, 14);
  EXPECT_EQ("2013-11-15T17:00:14", absl::FormatCivilTime(cs));
  cs = absl::CivilSecond(2013, 11, 15, 23 + 1, 32, 14);
  EXPECT_EQ("2013-11-16T00:32:14", absl::FormatCivilTime(cs));
  cs = absl::CivilSecond(2013, 11, 30 + 1, 16, 32, 14);
  EXPECT_EQ("2013-12-01T16:32:14", absl::FormatCivilTime(cs));
  cs = absl::CivilSecond(2013, 12 + 1, 15, 16, 32, 14);
  EXPECT_EQ("2014-01-15T16:32:14", absl::FormatCivilTime(cs));
}

TEST(CivilTime, NormalizeSimpleUnderflow) {
  absl::CivilSecond cs;
  cs = absl::CivilSecond(2013, 11, 15, 16, 32, 0 - 1);
  EXPECT_EQ("2013-11-15T16:31:59", absl::FormatCivilTime(cs));
  cs = absl::CivilSecond(2013, 11, 15, 16, 0 - 1, 14);
  EXPECT_EQ("2013-11-15T15:59:14", absl::FormatCivilTime(cs));
  cs = absl::CivilSecond(2013, 11, 15, 0 - 1, 32, 14);
  EXPECT_EQ("2013-11-14T23:32:14", absl::FormatCivilTime(cs));
  cs = absl::CivilSecond(2013, 11, 1 - 1, 16, 32, 14);
  EXPECT_EQ("2013-10-31T16:32:14", absl::FormatCivilTime(cs));
  cs = absl::CivilSecond(2013, 1 - 1, 15, 16, 32, 14);
  EXPECT_EQ("2012-12-15T16:32:14", absl::FormatCivilTime(cs));
}

TEST(CivilTime, NormalizeMultipleOverflow) {
  absl::CivilSecond cs(2013, 12, 31, 23, 59, 59 + 1);
  EXPECT_EQ("2014-01-01T00:00:00", absl::FormatCivilTime(cs));
}

TEST(CivilTime, NormalizeMultipleUnderflow) {
  absl::CivilSecond cs(2014, 1, 1, 0, 0, 0 - 1);
  EXPECT_EQ("2013-12-31T23:59:59", absl::FormatCivilTime(cs));
}

TEST(CivilTime, NormalizeOverflowLimits) {
  absl::CivilSecond cs;

  const int kintmax = std::numeric_limits<int>::max();
  cs = absl::CivilSecond(0, kintmax, kintmax, kintmax, kintmax, kintmax);
  EXPECT_EQ("185085715-11-27T12:21:07", absl::FormatCivilTime(cs));

  const int kintmin = std::numeric_limits<int>::min();
  cs = absl::CivilSecond(0, kintmin, kintmin, kintmin, kintmin, kintmin);
  EXPECT_EQ("-185085717-10-31T10:37:52", absl::FormatCivilTime(cs));
}

TEST(CivilTime, NormalizeComplexOverflow) {
  absl::CivilSecond cs;
  cs = absl::CivilSecond(2013, 11, 15, 16, 32, 14 + 123456789);
  EXPECT_EQ("2017-10-14T14:05:23", absl::FormatCivilTime(cs));
  cs = absl::CivilSecond(2013, 11, 15, 16, 32 + 1234567, 14);
  EXPECT_EQ("2016-03-22T00:39:14", absl::FormatCivilTime(cs));
  cs = absl::CivilSecond(2013, 11, 15, 16 + 123456, 32, 14);
  EXPECT_EQ("2027-12-16T16:32:14", absl::FormatCivilTime(cs));
  cs = absl::CivilSecond(2013, 11, 15 + 1234, 16, 32, 14);
  EXPECT_EQ("2017-04-02T16:32:14", absl::FormatCivilTime(cs));
  cs = absl::CivilSecond(2013, 11 + 123, 15, 16, 32, 14);
  EXPECT_EQ("2024-02-15T16:32:14", absl::FormatCivilTime(cs));
}

TEST(CivilTime, NormalizeComplexUnderflow) {
  absl::CivilSecond cs;
  cs = absl::CivilSecond(1999, 3, 0, 0, 0, 0);  // year 400
  EXPECT_EQ("1999-02-28T00:00:00", absl::FormatCivilTime(cs));
  cs = absl::CivilSecond(2013, 11, 15, 16, 32, 14 - 123456789);
  EXPECT_EQ("2009-12-17T18:59:05", absl::FormatCivilTime(cs));
  cs = absl::CivilSecond(2013, 11, 15, 16, 32 - 1234567, 14);
  EXPECT_EQ("2011-07-12T08:25:14", absl::FormatCivilTime(cs));
  cs = absl::CivilSecond(2013, 11, 15, 16 - 123456, 32, 14);
  EXPECT_EQ("1999-10-16T16:32:14", absl::FormatCivilTime(cs));
  cs = absl::CivilSecond(2013, 11, 15 - 1234, 16, 32, 14);
  EXPECT_EQ("2010-06-30T16:32:14", absl::FormatCivilTime(cs));
  cs = absl::CivilSecond(2013, 11 - 123, 15, 16, 32, 14);
  EXPECT_EQ("2003-08-15T16:32:14", absl::FormatCivilTime(cs));
}

TEST(CivilTime, NormalizeMishmash) {
  absl::CivilSecond cs;
  cs = absl::CivilSecond(2013, 11 - 123, 15 + 1234, 16 - 123456, 32 + 1234567,
                         14 - 123456789);
  EXPECT_EQ("1991-05-09T03:06:05", absl::FormatCivilTime(cs));
  cs = absl::CivilSecond(2013, 11 + 123, 15 - 1234, 16 + 123456, 32 - 1234567,
                         14 + 123456789);
  EXPECT_EQ("2036-05-24T05:58:23", absl::FormatCivilTime(cs));

  cs = absl::CivilSecond(2013, 11, -146097 + 1, 16, 32, 14);
  EXPECT_EQ("1613-11-01T16:32:14", absl::FormatCivilTime(cs));
  cs = absl::CivilSecond(2013, 11 + 400 * 12, -146097 + 1, 16, 32, 14);
  EXPECT_EQ("2013-11-01T16:32:14", absl::FormatCivilTime(cs));
}

// Convert all the days from 1970-1-1 to 1970-1-146097 (aka 2369-12-31)
// and check that they normalize to the expected time.  146097 days span
// the 400-year Gregorian cycle used during normalization.
TEST(CivilTime, NormalizeAllTheDays) {
  absl::CivilDay expected(1970, 1, 1);
  for (int day = 1; day <= 146097; ++day) {
    absl::CivilSecond cs(1970, 1, day, 0, 0, 0);
    EXPECT_EQ(expected, cs);
    ++expected;
  }
}

TEST(CivilTime, NormalizeWithHugeYear) {
  absl::CivilMonth c(9223372036854775807, 1);
  EXPECT_EQ("9223372036854775807-01", absl::FormatCivilTime(c));
  c = c - 1;  // Causes normalization
  EXPECT_EQ("9223372036854775806-12", absl::FormatCivilTime(c));

  c = absl::CivilMonth(-9223372036854775807 - 1, 1);
  EXPECT_EQ("-9223372036854775808-01", absl::FormatCivilTime(c));
  c = c + 12;  // Causes normalization
  EXPECT_EQ("-9223372036854775807-01", absl::FormatCivilTime(c));
}

TEST(CivilTime, LeapYears) {
  const absl::CivilSecond s1(2013, 2, 28 + 1, 0, 0, 0);
  EXPECT_EQ("2013-03-01T00:00:00", absl::FormatCivilTime(s1));

  const absl::CivilSecond s2(2012, 2, 28 + 1, 0, 0, 0);
  EXPECT_EQ("2012-02-29T00:00:00", absl::FormatCivilTime(s2));

  const absl::CivilSecond s3(1900, 2, 28 + 1, 0, 0, 0);
  EXPECT_EQ("1900-03-01T00:00:00", absl::FormatCivilTime(s3));

  const struct {
    int year;
    int days;
    struct {
      int month;
      int day;
    } leap_day;  // The date of the day after Feb 28.
  } kLeapYearTable[]{
      {1900, 365, {3, 1}},
      {1999, 365, {3, 1}},
      {2000, 366, {2, 29}},  // leap year
      {2001, 365, {3, 1}},
      {2002, 365, {3, 1}},
      {2003, 365, {3, 1}},
      {2004, 366, {2, 29}},  // leap year
      {2005, 365, {3, 1}},
      {2006, 365, {3, 1}},
      {2007, 365, {3, 1}},
      {2008, 366, {2, 29}},  // leap year
      {2009, 365, {3, 1}},
      {2100, 365, {3, 1}},
  };

  for (int i = 0; i < ABSL_ARRAYSIZE(kLeapYearTable); ++i) {
    const int y = kLeapYearTable[i].year;
    const int m = kLeapYearTable[i].leap_day.month;
    const int d = kLeapYearTable[i].leap_day.day;
    const int n = kLeapYearTable[i].days;

    // Tests incrementing through the leap day.
    const absl::CivilDay feb28(y, 2, 28);
    const absl::CivilDay next_day = feb28 + 1;
    EXPECT_EQ(m, next_day.month());
    EXPECT_EQ(d, next_day.day());

    // Tests difference in days of leap years.
    const absl::CivilYear year(feb28);
    const absl::CivilYear next_year = year + 1;
    EXPECT_EQ(n, absl::CivilDay(next_year) - absl::CivilDay(year));
  }
}

TEST(CivilTime, FirstThursdayInMonth) {
  const absl::CivilDay nov1(2014, 11, 1);
  const absl::CivilDay thursday =
      absl::NextWeekday(nov1 - 1, absl::Weekday::thursday);
  EXPECT_EQ("2014-11-06", absl::FormatCivilTime(thursday));

  // Bonus: Date of Thanksgiving in the United States
  // Rule: Fourth Thursday of November
  const absl::CivilDay thanksgiving = thursday +  7 * 3;
  EXPECT_EQ("2014-11-27", absl::FormatCivilTime(thanksgiving));
}

TEST(CivilTime, DocumentationExample) {
  absl::CivilSecond second(2015, 6, 28, 1, 2, 3);  // 2015-06-28 01:02:03
  absl::CivilMinute minute(second);                // 2015-06-28 01:02:00
  absl::CivilDay day(minute);                      // 2015-06-28 00:00:00

  second -= 1;                    // 2015-06-28 01:02:02
  --second;                       // 2015-06-28 01:02:01
  EXPECT_EQ(minute, second - 1);  // Comparison between types
  EXPECT_LT(minute, second);

  // int diff = second - minute;  // ERROR: Mixed types, won't compile

  absl::CivilDay june_1(2015, 6, 1);  // Pass fields to c'tor.
  int diff = day - june_1;            // Num days between 'day' and June 1
  EXPECT_EQ(27, diff);

  // Fields smaller than alignment are floored to their minimum value.
  absl::CivilDay day_floor(2015, 1, 2, 9, 9, 9);
  EXPECT_EQ(0, day_floor.hour());  // 09:09:09 is floored
  EXPECT_EQ(absl::CivilDay(2015, 1, 2), day_floor);

  // Unspecified fields default to their minimum value
  absl::CivilDay day_default(2015);  // Defaults to Jan 1
  EXPECT_EQ(absl::CivilDay(2015, 1, 1), day_default);

  // Iterates all the days of June.
  absl::CivilMonth june(day);  // CivilDay -> CivilMonth
  absl::CivilMonth july = june + 1;
  for (absl::CivilDay day = june_1; day < july; ++day) {
    // ...
  }
}

}  // namespace
