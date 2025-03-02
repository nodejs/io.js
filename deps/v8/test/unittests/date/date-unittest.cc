// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/date/date.h"

#include "src/execution/isolate.h"
#include "src/handles/global-handles.h"
#include "src/init/v8.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {

namespace internal {

class DateTest : public TestWithContext {
 public:
  void CheckDST(int64_t time) {
    DateCache* date_cache = i_isolate()->date_cache();
    int64_t actual = date_cache->ToLocal(time);
    int64_t expected = time + date_cache->GetLocalOffsetFromOS(time, true);
    CHECK_EQ(actual, expected);
  }
};

class DateCacheMock : public DateCache {
 public:
  struct Rule {
    int year, start_month, start_day, end_month, end_day, offset_sec;
  };

  DateCacheMock(int local_offset, Rule* rules, int rules_count)
      : local_offset_(local_offset), rules_(rules), rules_count_(rules_count) {}

 protected:
  int GetDaylightSavingsOffsetFromOS(int64_t time_sec) override {
    int days = DaysFromTime(time_sec * 1000);
    int time_in_day_sec = TimeInDay(time_sec * 1000, days) / 1000;
    int year, month, day;
    YearMonthDayFromDays(days, &year, &month, &day);
    Rule* rule = FindRuleFor(year, month, day, time_in_day_sec);
    return rule == nullptr ? 0 : rule->offset_sec * 1000;
  }

  int GetLocalOffsetFromOS(int64_t time_ms, bool is_utc) override {
    return local_offset_ + GetDaylightSavingsOffsetFromOS(time_ms / 1000);
  }

 private:
  Rule* FindRuleFor(int year, int month, int day, int time_in_day_sec) {
    Rule* result = nullptr;
    for (int i = 0; i < rules_count_; i++)
      if (Match(&rules_[i], year, month, day, time_in_day_sec)) {
        result = &rules_[i];
      }
    return result;
  }

  bool Match(Rule* rule, int year, int month, int day, int time_in_day_sec) {
    if (rule->year != 0 && rule->year != year) return false;
    if (rule->start_month > month) return false;
    if (rule->end_month < month) return false;
    int start_day = ComputeRuleDay(year, rule->start_month, rule->start_day);
    if (rule->start_month == month && start_day > day) return false;
    if (rule->start_month == month && start_day == day &&
        2 * 3600 > time_in_day_sec)
      return false;
    int end_day = ComputeRuleDay(year, rule->end_month, rule->end_day);
    if (rule->end_month == month && end_day < day) return false;
    if (rule->end_month == month && end_day == day &&
        2 * 3600 <= time_in_day_sec)
      return false;
    return true;
  }

  int ComputeRuleDay(int year, int month, int day) {
    if (day != 0) return day;
    int days = DaysFromYearMonth(year, month);
    // Find the first Sunday of the month.
    while (Weekday(days + day) != 6) day++;
    return day + 1;
  }

  int local_offset_;
  Rule* rules_;
  int rules_count_;
};

static int64_t TimeFromYearMonthDay(DateCache* date_cache, int year, int month,
                                    int day) {
  int64_t result = date_cache->DaysFromYearMonth(year, month);
  return (result + day - 1) * DateCache::kMsPerDay;
}

TEST_F(DateTest, DaylightSavingsTime) {
  v8::HandleScope scope(isolate());
  DateCacheMock::Rule rules[] = {
      {0, 2, 0, 10, 0, 3600},     // DST from March to November in any year.
      {2010, 2, 0, 7, 20, 3600},  // DST from March to August 20 in 2010.
      {2010, 7, 20, 8, 10,
       0},  // No DST from August 20 to September 10 in 2010.
      {2010, 8, 10, 10, 0, 3600},  // DST from September 10 to November in 2010.
  };

  int local_offset_ms = -36000000;  // -10 hours.

  DateCacheMock* date_cache =
      new DateCacheMock(local_offset_ms, rules, arraysize(rules));

  reinterpret_cast<Isolate*>(isolate())->set_date_cache(date_cache);

  int64_t start_of_2010 = TimeFromYearMonthDay(date_cache, 2010, 0, 1);
  int64_t start_of_2011 = TimeFromYearMonthDay(date_cache, 2011, 0, 1);
  int64_t august_20 = TimeFromYearMonthDay(date_cache, 2010, 7, 20);
  int64_t september_10 = TimeFromYearMonthDay(date_cache, 2010, 8, 10);
  CheckDST((august_20 + september_10) / 2);
  CheckDST(september_10);
  CheckDST(september_10 + 2 * 3600);
  CheckDST(september_10 + 2 * 3600 - 1000);
  CheckDST(august_20 + 2 * 3600);
  CheckDST(august_20 + 2 * 3600 - 1000);
  CheckDST(august_20);
  // Check each day of 2010.
  for (int64_t time = start_of_2011 + 2 * 3600; time >= start_of_2010;
       time -= DateCache::kMsPerDay) {
    CheckDST(time);
    CheckDST(time - 1000);
    CheckDST(time + 1000);
  }
  // Check one day from 2010 to 2100.
  for (int year = 2100; year >= 2010; year--) {
    CheckDST(TimeFromYearMonthDay(date_cache, year, 5, 5));
  }
  CheckDST((august_20 + september_10) / 2);
  CheckDST(september_10);
  CheckDST(september_10 + 2 * 3600);
  CheckDST(september_10 + 2 * 3600 - 1000);
  CheckDST(august_20 + 2 * 3600);
  CheckDST(august_20 + 2 * 3600 - 1000);
  CheckDST(august_20);
}

namespace {
int legacy_parse_count = 0;
void DateParseLegacyCounterCallback(v8::Isolate* isolate,
                                    v8::Isolate::UseCounterFeature feature) {
  if (feature == v8::Isolate::kLegacyDateParser) legacy_parse_count++;
}
}  // anonymous namespace

TEST_F(DateTest, DateParseLegacyUseCounter) {
  v8::HandleScope scope(isolate());
  isolate()->SetUseCounterCallback(DateParseLegacyCounterCallback);
  CHECK_EQ(0, legacy_parse_count);
  RunJS("Date.parse('2015-02-31')");
  CHECK_EQ(0, legacy_parse_count);
  RunJS("Date.parse('2015-02-31T11:22:33.444Z01:23')");
  CHECK_EQ(0, legacy_parse_count);
  RunJS("Date.parse('2015-02-31T11:22:33.444')");
  CHECK_EQ(0, legacy_parse_count);
  RunJS("Date.parse('2000 01 01')");
  CHECK_EQ(1, legacy_parse_count);
  RunJS("Date.parse('2015-02-31T11:22:33.444     ')");
  CHECK_EQ(1, legacy_parse_count);
}

}  // namespace internal
}  // namespace v8
