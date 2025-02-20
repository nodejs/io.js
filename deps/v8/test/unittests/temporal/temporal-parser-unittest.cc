// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/temporal/temporal-parser.h"

#include <optional>

#include "src/execution/isolate.h"
#include "src/heap/factory-inl.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {

// This file test the TemporalParser to parse ISO 8601 according to
// #sec-temporal-iso8601grammar

// For int32_t fields in ParsedISO8601Result, kMinInt31 denote 'undefined'
// value.
const int32_t kUndefined = kMinInt31;

void CheckCalendar(Isolate* isolate, Handle<String> iso_string,
                   int32_t calendar_start, int32_t calendar_length,
                   const std::string& expected_calendar) {
  DirectHandle<String> actual_calendar = isolate->factory()->NewSubString(
      iso_string, calendar_start, calendar_start + calendar_length);
  CHECK(expected_calendar == actual_calendar->ToCString().get());
}

void CheckDate(const ParsedISO8601Result& actual, int32_t date_year,
               int32_t date_month, int32_t date_day) {
  CHECK_EQ(date_year, actual.date_year);
  CHECK_EQ(date_month, actual.date_month);
  CHECK_EQ(date_day, actual.date_day);
}

void CheckTime(const ParsedISO8601Result& actual, int32_t time_hour,
               int32_t time_minute, int32_t time_second,
               int32_t time_nanosecond) {
  CHECK_EQ(time_hour, actual.time_hour);
  CHECK_EQ(time_minute, actual.time_minute);
  CHECK_EQ(time_second, actual.time_second);
  CHECK_EQ(time_nanosecond, actual.time_nanosecond);
}

void CheckTimeZoneNumericUTCOffset(const ParsedISO8601Result& actual,
                                   int32_t tzuo_sign, int32_t tzuo_hour,
                                   int32_t tzuo_minute, int32_t tzuo_second,
                                   int32_t tzuo_nanosecond) {
  CHECK_EQ(tzuo_sign, actual.tzuo_sign);
  CHECK_EQ(tzuo_hour, actual.tzuo_hour);
  CHECK_EQ(tzuo_minute, actual.tzuo_minute);
  CHECK_EQ(tzuo_second, actual.tzuo_second);
  CHECK_EQ(tzuo_nanosecond, actual.tzuo_nanosecond);
}

void CheckDuration(const ParsedISO8601Duration& actual, int64_t sign,
                   int64_t years, int64_t months, int64_t weeks, int64_t days,
                   int64_t whole_hours, int64_t hours_fraction,
                   int64_t whole_minutes, int64_t minutes_fraction,
                   int64_t whole_seconds, int64_t seconds_fraction) {
  CHECK_EQ(sign, actual.sign);
  CHECK_EQ(years, actual.years);
  CHECK_EQ(months, actual.months);
  CHECK_EQ(weeks, actual.weeks);
  CHECK_EQ(days, actual.days);
  CHECK_EQ(whole_hours, actual.whole_hours);
  CHECK_EQ(hours_fraction, actual.hours_fraction);
  CHECK_EQ(whole_minutes, actual.whole_minutes);
  CHECK_EQ(minutes_fraction, actual.minutes_fraction);
  CHECK_EQ(whole_seconds, actual.whole_seconds);
  CHECK_EQ(seconds_fraction, actual.seconds_fraction);
}

char commatoperiod(char in) { return (in == ',') ? '.' : in; }

char asciitolower(char in) {
  return (in <= 'Z' && in >= 'A') ? (in - ('Z' - 'z')) : in;
}

#define IMPL_VERIFY_PARSE_TEMPORAL_DATE_STRING_SUCCESS(R)             \
  void VerifyParseTemporal##R##StringSuccess(                         \
      const char* str, int32_t date_year, int32_t date_month,         \
      int32_t date_day, const char* calendar_name) {                  \
    Handle<String> input = MakeString(str);                           \
    std::optional<ParsedISO8601Result> result =                       \
        TemporalParser::ParseTemporal##R##String(i_isolate(), input); \
    CHECK(result.has_value());                                        \
    ParsedISO8601Result actual = *result;                             \
    CheckDate(actual, date_year, date_month, date_day);               \
    CheckCalendar(i_isolate(), input, actual.calendar_name_start,     \
                  actual.calendar_name_length, calendar_name);        \
  }

#define IMPL_VERIFY_PARSE_TEMPORAL_DATE_TIME_STRING_SUCCESS(R)               \
  void VerifyParseTemporal##R##StringSuccess(                                \
      const char* str, int32_t date_year, int32_t date_month,                \
      int32_t date_day, int32_t time_hour, int32_t time_minute,              \
      int32_t time_second, int32_t time_nanosecond,                          \
      const char* calendar_name) {                                           \
    Handle<String> input = MakeString(str);                                  \
    std::optional<ParsedISO8601Result> result =                              \
        TemporalParser::ParseTemporal##R##String(i_isolate(), input);        \
    CHECK(result.has_value());                                               \
    ParsedISO8601Result actual = *result;                                    \
    CheckDate(actual, date_year, date_month, date_day);                      \
    CheckCalendar(i_isolate(), input, actual.calendar_name_start,            \
                  actual.calendar_name_length, calendar_name);               \
    CheckTime(actual, time_hour, time_minute, time_second, time_nanosecond); \
  }

#define IMPL_VERIFY_PARSE_TEMPORAL_ZONED_DATE_TIME_STRING_SUCCESS(R)           \
  void VerifyParseTemporal##R##StringSuccess(                                  \
      const char* str, int32_t date_year, int32_t date_month,                  \
      int32_t date_day, int32_t time_hour, int32_t time_minute,                \
      int32_t time_second, int32_t time_nanosecond, const char* calendar_name, \
      int32_t tzuo_sign, int32_t tzuo_hour, int32_t tzuo_minute,               \
      int32_t tzuo_second, int32_t tzuo_nanosecond, bool utc_designator,       \
      const char* tzi_name) {                                                  \
    Handle<String> input = MakeString(str);                                    \
    std::optional<ParsedISO8601Result> result =                                \
        TemporalParser::ParseTemporal##R##String(i_isolate(), input);          \
    CHECK(result.has_value());                                                 \
    ParsedISO8601Result actual = *result;                                      \
    CheckDate(actual, date_year, date_month, date_day);                        \
    CheckCalendar(i_isolate(), input, actual.calendar_name_start,              \
                  actual.calendar_name_length, calendar_name);                 \
    CheckTime(actual, time_hour, time_minute, time_second, time_nanosecond);   \
    CHECK_EQ(utc_designator, actual.utc_designator);                           \
    std::string actual_tzi_name(str + actual.tzi_name_start,                   \
                                actual.tzi_name_length);                       \
    CHECK(actual_tzi_name == tzi_name);                                        \
    if (!utc_designator) {                                                     \
      CheckTimeZoneNumericUTCOffset(actual, tzuo_sign, tzuo_hour, tzuo_minute, \
                                    tzuo_second, tzuo_nanosecond);             \
    }                                                                          \
  }

class TemporalParserTest : public TestWithIsolate {
 protected:
  IMPL_VERIFY_PARSE_TEMPORAL_DATE_STRING_SUCCESS(YearMonth)
  IMPL_VERIFY_PARSE_TEMPORAL_DATE_STRING_SUCCESS(MonthDay)
  IMPL_VERIFY_PARSE_TEMPORAL_DATE_TIME_STRING_SUCCESS(DateTime)
  IMPL_VERIFY_PARSE_TEMPORAL_ZONED_DATE_TIME_STRING_SUCCESS(ZonedDateTime)

  void VerifyParseTemporalInstantStringSuccess(
      const char* str, bool utc_designator, int32_t tzuo_sign,
      int32_t tzuo_hour, int32_t tzuo_minute, int32_t tzuo_second,
      int32_t tzuo_nanosecond) {
    DirectHandle<String> input = MakeString(str);
    std::optional<ParsedISO8601Result> result =
        TemporalParser::ParseTemporalInstantString(i_isolate(), input);
    CHECK(result.has_value());
    ParsedISO8601Result actual = *result;
    CHECK_EQ(utc_designator, actual.utc_designator);
    if (!utc_designator) {
      CheckTimeZoneNumericUTCOffset(actual, tzuo_sign, tzuo_hour, tzuo_minute,
                                    tzuo_second, tzuo_nanosecond);
    }
  }

  void VerifyParseCalendarNameSuccess(const char* str) {
    DirectHandle<String> input = MakeString(str);
    std::optional<ParsedISO8601Result> result =
        TemporalParser::ParseCalendarName(i_isolate(), input);
    CHECK(result.has_value());
    ParsedISO8601Result actual = *result;
    // For ParseCalendarName, we just validate the input fully match
    // CalendarName, therefore, the test pass if the start is 0 and
    // the calendar_name_length is the same as the length of the input.
    CHECK_EQ(actual.calendar_name_start, 0);
    CHECK_EQ(actual.calendar_name_length, input->length());
  }

  void VerifyParseTimeZoneIdentifierSuccess(const char* str) {
    DirectHandle<String> input = MakeString(str);
    std::optional<ParsedISO8601Result> result =
        TemporalParser::ParseTimeZoneIdentifier(i_isolate(), input);
    CHECK(result.has_value());
    ParsedISO8601Result actual = *result;
    // For ParseTimeZoneIdentifier, we just validate the input fully match
    // TimeZoneIdentifier, therefore, the test pass if the start is 0 and
    // the tzi_name_length is the same as the length of the input.
    CHECK_EQ(actual.tzi_name_start, 0);
    CHECK_EQ(actual.tzi_name_length, input->length());
  }

  void VerifyParseTemporalTimeStringSuccess(const char* str, int32_t time_hour,
                                            int32_t time_minute,
                                            int32_t time_second,
                                            int32_t time_nanosecond,
                                            const char* calendar_name) {
    Handle<String> input = MakeString(str);
    ParsedISO8601Result actual =
        *TemporalParser::ParseTemporalTimeString(i_isolate(), input);
    CheckTime(actual, time_hour, time_minute, time_second, time_nanosecond);
    CheckCalendar(i_isolate(), input, actual.calendar_name_start,
                  actual.calendar_name_length, calendar_name);
  }

  void VerifyTemporalTimeStringTimeUndefined(const char* str) {
    VerifyParseTemporalTimeStringSuccess(str, kUndefined, kUndefined,
                                         kUndefined, kUndefined, "");
  }

  void VerifyParseDurationSuccess(const char* str, int64_t sign, int64_t years,
                                  int64_t months, int64_t weeks, int64_t days,
                                  int64_t whole_hours, int64_t hours_fraction,
                                  int64_t whole_minutes,
                                  int64_t minutes_fraction,
                                  int64_t whole_seconds,
                                  int64_t seconds_fraction) {
    DirectHandle<String> input = MakeString(str);
    std::optional<ParsedISO8601Duration> result =
        TemporalParser::ParseTemporalDurationString(i_isolate(), input);
    CHECK(result.has_value());
    CheckDuration(*result, sign, years, months, weeks, days, whole_hours,
                  hours_fraction, whole_minutes, minutes_fraction,
                  whole_seconds, seconds_fraction);
  }

  void VerifyParseDurationSuccess(const char* str,
                                  const ParsedISO8601Duration& expected) {
    VerifyParseDurationSuccess(
        str, expected.sign, expected.years, expected.months, expected.weeks,
        expected.days, expected.whole_hours, expected.hours_fraction,
        expected.whole_minutes, expected.minutes_fraction,
        expected.whole_seconds, expected.seconds_fraction);
  }

  void VerifyParseDurationWithPositiveSign(const char* str) {
    DirectHandle<String> input = MakeString(str);
    std::optional<ParsedISO8601Duration> result =
        TemporalParser::ParseTemporalDurationString(i_isolate(), input);
    CHECK(result.has_value());
    ParsedISO8601Duration expected = *result;
    std::string with_sign("+");
    with_sign += str;
    VerifyParseDurationSuccess(with_sign.c_str(), expected);
  }

  void VerifyParseDurationWithMinusSign(const char* str) {
    std::string with_sign("-");
    with_sign += str;
    DirectHandle<String> input = MakeString(with_sign.c_str());
    std::optional<ParsedISO8601Duration> result =
        TemporalParser::ParseTemporalDurationString(i_isolate(), input);
    CHECK(result.has_value());
    ParsedISO8601Duration expected = *result;
    with_sign = "\u2212";
    with_sign += str;
    VerifyParseDurationSuccess(with_sign.c_str(), expected);
  }

  void VerifyParseDurationWithLowerCase(const char* str) {
    DirectHandle<String> input = MakeString(str);
    std::optional<ParsedISO8601Duration> result =
        TemporalParser::ParseTemporalDurationString(i_isolate(), input);
    CHECK(result.has_value());
    ParsedISO8601Duration expected = *result;
    std::string lower(str);
    std::transform(lower.begin(), lower.end(), lower.begin(), asciitolower);
    VerifyParseDurationSuccess(lower.c_str(), expected);
  }

  void VerifyParseDurationWithComma(const char* str) {
    std::string period(str);
    std::transform(period.begin(), period.end(), period.begin(), commatoperiod);
    DirectHandle<String> input = MakeString(str);
    std::optional<ParsedISO8601Duration> result =
        TemporalParser::ParseTemporalDurationString(i_isolate(), input);
    CHECK(result.has_value());
    ParsedISO8601Duration expected = *result;
    VerifyParseDurationSuccess(str, expected);
  }

  void VerifyParseTimeZoneNumericUTCOffsetSuccess(
      const char* str, int32_t tzuo_sign, int32_t tzuo_hour,
      int32_t tzuo_minute, int32_t tzuo_second, int32_t tzuo_nanosecond) {
    DirectHandle<String> input = MakeString(str);
    std::optional<ParsedISO8601Result> result =
        TemporalParser::ParseTimeZoneNumericUTCOffset(i_isolate(), input);
    CHECK(result.has_value());
    CheckTimeZoneNumericUTCOffset(*result, tzuo_sign, tzuo_hour, tzuo_minute,
                                  tzuo_second, tzuo_nanosecond);
  }
};

#define VERIFY_PARSE_FAIL(R, str)                                     \
  do {                                                                \
    DirectHandle<String> input = MakeString(str);                     \
    CHECK(!TemporalParser::Parse##R(i_isolate(), input).has_value()); \
  } while (false)

#define VERIFY_PARSE_FAIL_ON_DATE(R)                            \
  do {                                                          \
    VERIFY_PARSE_FAIL(R, "");                                   \
    /* sign only go with DateExtendedYear */                    \
    VERIFY_PARSE_FAIL(R, "+2021-03-04");                        \
    VERIFY_PARSE_FAIL(R, "-2021-03-04");                        \
    /* 1, 2, 3, 5 digits are not year */                        \
    VERIFY_PARSE_FAIL(R, "921-03-04");                          \
    VERIFY_PARSE_FAIL(R, "-821-03-04");                         \
    VERIFY_PARSE_FAIL(R, "9210304");                            \
    VERIFY_PARSE_FAIL(R, "-8210304");                           \
    VERIFY_PARSE_FAIL(R, "21-03-04");                           \
    VERIFY_PARSE_FAIL(R, "-31-03-04");                          \
    VERIFY_PARSE_FAIL(R, "\u221231-03-04");                     \
    VERIFY_PARSE_FAIL(R, "-310304");                            \
    VERIFY_PARSE_FAIL(R, "1-03-04");                            \
    VERIFY_PARSE_FAIL(R, "-3-03-04");                           \
    VERIFY_PARSE_FAIL(R, "10304");                              \
    VERIFY_PARSE_FAIL(R, "-30304");                             \
    VERIFY_PARSE_FAIL(R, "12921-03-04");                        \
    VERIFY_PARSE_FAIL(R, "-32821-03-04");                       \
    VERIFY_PARSE_FAIL(R, "129210304");                          \
    VERIFY_PARSE_FAIL(R, "-328210304");                         \
    VERIFY_PARSE_FAIL(R, "123456-03-04");                       \
    VERIFY_PARSE_FAIL(R, "1234560304");                         \
                                                                \
    /* 7 digits year */                                         \
    VERIFY_PARSE_FAIL(R, "0002021-09-03");                      \
    VERIFY_PARSE_FAIL(R, "-0002021-09-03");                     \
                                                                \
    /* It is a Syntax Error if DateExtendedYear is "-000000" */ \
    VERIFY_PARSE_FAIL(R, "-000000-09-03");                      \
    VERIFY_PARSE_FAIL(R, "\u2212000000-09-03");                 \
                                                                \
    /* single digit month */                                    \
    VERIFY_PARSE_FAIL(R, "1900-9-03");                          \
    VERIFY_PARSE_FAIL(R, "1900903");                            \
    /* out of range month */                                    \
    VERIFY_PARSE_FAIL(R, "1900-13-03");                         \
    VERIFY_PARSE_FAIL(R, "19001401");                           \
    /* single digit day */                                      \
    VERIFY_PARSE_FAIL(R, "1900-12-3");                          \
    VERIFY_PARSE_FAIL(R, "1900121");                            \
    /* Out of range day */                                      \
    VERIFY_PARSE_FAIL(R, "1900-12-32");                         \
    VERIFY_PARSE_FAIL(R, "19001232");                           \
    VERIFY_PARSE_FAIL(R, "1900-12-00");                         \
    VERIFY_PARSE_FAIL(R, "19001200");                           \
                                                                \
    /* Legal Date with other illegal stuff */                   \
    /* only with DateTimeSeparator */                           \
    VERIFY_PARSE_FAIL(R, "1900-12-31 ");                        \
    VERIFY_PARSE_FAIL(R, "19001231T");                          \
    VERIFY_PARSE_FAIL(R, "1900-12-31t");                        \
    VERIFY_PARSE_FAIL(R, "19001231 ");                          \
                                                                \
    /* Single digit hour */                                     \
    VERIFY_PARSE_FAIL(R, "1900-12-31 1");                       \
    VERIFY_PARSE_FAIL(R, "19001231T2");                         \
                                                                \
    /* Out of range hour */                                     \
    VERIFY_PARSE_FAIL(R, "1900-12-31t24");                      \
    VERIFY_PARSE_FAIL(R, "19001231 -1");                        \
                                                                \
    /* Single digit minute */                                   \
    VERIFY_PARSE_FAIL(R, "1900-12-31 03:1");                    \
    VERIFY_PARSE_FAIL(R, "19001231T024");                       \
                                                                \
    /* Out of range minute */                                   \
    VERIFY_PARSE_FAIL(R, "1900-12-31t04:61");                   \
    VERIFY_PARSE_FAIL(R, "19001231 23:70");                     \
                                                                \
    /* Single digit second */                                   \
    VERIFY_PARSE_FAIL(R, "1900-12-31 03:22:9");                 \
    VERIFY_PARSE_FAIL(R, "19001231T02494");                     \
                                                                \
    /* Out of range second */                                   \
    VERIFY_PARSE_FAIL(R, "1900-12-31t04:23:61");                \
    VERIFY_PARSE_FAIL(R, "19001231 23:12:80");                  \
                                                                \
    /* DecimalSeparator without TimeFractionalPart */           \
    VERIFY_PARSE_FAIL(R, "1900-12-31 03:22:09,");               \
    VERIFY_PARSE_FAIL(R, "19001231T024904.");                   \
                                                                \
    /* TimeFractionalPart too long */                           \
    VERIFY_PARSE_FAIL(R, "1900-12-31 03:22:09,9876543219");     \
    VERIFY_PARSE_FAIL(R, "19001231T024904.1234567890");         \
                                                                \
    /* Legal Date with illegal TimeZoneUTCOffset */             \
    VERIFY_PARSE_FAIL(R, "1900-12-31+1");                       \
    VERIFY_PARSE_FAIL(R, "1900-12-31+12:2");                    \
    VERIFY_PARSE_FAIL(R, "1900-12-31+122");                     \
    VERIFY_PARSE_FAIL(R, "1900-12-31+12:23:3");                 \
    VERIFY_PARSE_FAIL(R, "1900-12-31+12233");                   \
    VERIFY_PARSE_FAIL(R, "1900-12-31+12:23:45.");               \
    VERIFY_PARSE_FAIL(R, "1900-12-31+122345,");                 \
    VERIFY_PARSE_FAIL(R, "1900-12-31+12:23:45.1234567890");     \
    VERIFY_PARSE_FAIL(R, "1900-12-31+122345,0987654321");       \
    /* Legal Date with illegal [TimeZoneIANAName] */            \
    VERIFY_PARSE_FAIL(R, "1900-12-31[.]");                      \
    VERIFY_PARSE_FAIL(R, "1900-12-31[..]");                     \
    VERIFY_PARSE_FAIL(R, "1900-12-31[abc/.]");                  \
    VERIFY_PARSE_FAIL(R, "1900-12-31[abc/..]");                 \
    VERIFY_PARSE_FAIL(R, "1900-12-31[abcdefghijklmno]");        \
    VERIFY_PARSE_FAIL(R, "1900-12-31[abcdefghijklmn/-abcde]");  \
    VERIFY_PARSE_FAIL(R, "1900-12-31[-bcdefghijklmn/abcde]");   \
    VERIFY_PARSE_FAIL(R, "1900-12-31[abcdefghi//abde]");        \
    /* Legal Date with illegal [Etc/GMT ASCIISign Hour] */      \
    VERIFY_PARSE_FAIL(R, "1900-12-31[ETC/GMT+10]");             \
    /* Wrong case for Etc */                                    \
    VERIFY_PARSE_FAIL(R, "1900-12-31[etc/GMT-10]");             \
    /* Wrong case for GMT */                                    \
    VERIFY_PARSE_FAIL(R, "1900-12-31[Etc/gmt+00]");             \
    VERIFY_PARSE_FAIL(R, "1900-12-31[Etc/gmt-00]");             \
    VERIFY_PARSE_FAIL(R, "1900-12-31[Etc/gMt+00]");             \
    VERIFY_PARSE_FAIL(R, "1900-12-31[Etc/gmT-00]");             \
    /* not ASCII sign */                                        \
    VERIFY_PARSE_FAIL(R, "1900-12-31[Etc/GMT\u221200]");        \
    /* Out of range */                                          \
    VERIFY_PARSE_FAIL(R, "1900-12-31[Etc/GMT+24]");             \
    VERIFY_PARSE_FAIL(R, "1900-12-31[Etc/GMT-24]");             \
    /* leading 0 Hour */                                        \
    VERIFY_PARSE_FAIL(R, "1900-12-31[Etc/GMT+02]");             \
    VERIFY_PARSE_FAIL(R, "1900-12-31[Etc/GMT-00]");             \
    VERIFY_PARSE_FAIL(R, "2021-11-09Z[Etc/GMT+01]");            \
    /* Three digit hour */                                      \
    VERIFY_PARSE_FAIL(R, "1900-12-31[Etc/GMT+201]");            \
    VERIFY_PARSE_FAIL(R, "1900-12-31[Etc/GMT-000]");            \
    /* With minute */                                           \
    VERIFY_PARSE_FAIL(R, "1900-12-31[Etc/GMT+05:30]");          \
    VERIFY_PARSE_FAIL(R, "1900-12-31[Etc/GMT+0530]");           \
    /* Legal Date with illegal [TimeZoneUTCOffsetName] */       \
    VERIFY_PARSE_FAIL(R, "1900-12-31[+]");                      \
    VERIFY_PARSE_FAIL(R, "1900-12-31[-]");                      \
    VERIFY_PARSE_FAIL(R, "1900-12-31[+0]");                     \
    VERIFY_PARSE_FAIL(R, "1900-12-31[-1]");                     \
    VERIFY_PARSE_FAIL(R, "1900-12-31[+01:]");                   \
    VERIFY_PARSE_FAIL(R, "1900-12-31[+24]");                    \
    VERIFY_PARSE_FAIL(R, "1900-12-31[-25]");                    \
    VERIFY_PARSE_FAIL(R, "1900-12-31[+01:2]");                  \
    VERIFY_PARSE_FAIL(R, "1900-12-31[-012]");                   \
    VERIFY_PARSE_FAIL(R, "1900-12-31[+01:60]");                 \
    VERIFY_PARSE_FAIL(R, "1900-12-31[-23:60]");                 \
    VERIFY_PARSE_FAIL(R, "1900-12-31[+2360]");                  \
    VERIFY_PARSE_FAIL(R, "1900-12-31[\u22121260]");             \
    VERIFY_PARSE_FAIL(R, "1900-12-31[+01:23:]");                \
    VERIFY_PARSE_FAIL(R, "1900-12-31[-01234]");                 \
    VERIFY_PARSE_FAIL(R, "1900-12-31[+01:23:4]");               \
    VERIFY_PARSE_FAIL(R, "1900-12-31[+01:23:61]");              \
    VERIFY_PARSE_FAIL(R, "1900-12-31[-012372]");                \
    VERIFY_PARSE_FAIL(R, "1900-12-31[+01:23:45.]");             \
    VERIFY_PARSE_FAIL(R, "1900-12-31[+01:23:45,]");             \
    VERIFY_PARSE_FAIL(R, "1900-12-31[+01:23:45.1234567890]");   \
    VERIFY_PARSE_FAIL(R, "1900-12-31[-01:23:45,0000000000]");   \
    VERIFY_PARSE_FAIL(R, "1900-12-31[+01:23:4a]");              \
    VERIFY_PARSE_FAIL(R, "1900-12-31[+01:b3:40]");              \
    VERIFY_PARSE_FAIL(R, "1900-12-31[+abcdefg]");               \
    /* Legal Date with illegal [CalendarName] */                \
    VERIFY_PARSE_FAIL(R, "1900-12-31[u-ca=]");                  \
    VERIFY_PARSE_FAIL(R, "1900-12-31[u-ca=123456789]");         \
    VERIFY_PARSE_FAIL(R, "1900-12-31[u-ca=a]");                 \
    VERIFY_PARSE_FAIL(R, "1900-12-31[u-ca=ab]");                \
    VERIFY_PARSE_FAIL(R, "1900-12-31[u-ca=abcdefghi]");         \
    VERIFY_PARSE_FAIL(R, "1900-12-31[u-ca=a-abcdefgh]");        \
    VERIFY_PARSE_FAIL(R, "1900-12-31[u-ca=ab-abcdefgh]");       \
    VERIFY_PARSE_FAIL(R, "1900-12-31[u-ca=abc-abcdefghi]");     \
    VERIFY_PARSE_FAIL(R, "1900-12-31[u-ca=abc-def-ghijklmno]"); \
  } while (false)

TEST_F(TemporalParserTest, TemporalTimeStringSuccess) {
  // DateTime: Date TimeSpecSeparator_opt TimeZone_opt
  // Date TimeSpecSeparator
  // Differeent DateTimeSeparator: <S> T or t
  VerifyParseTemporalTimeStringSuccess("2021-11-09T01", 1, kUndefined,
                                       kUndefined, kUndefined, "");
  VerifyParseTemporalTimeStringSuccess("2021-12-07t23", 23, kUndefined,
                                       kUndefined, kUndefined, "");
  VerifyParseTemporalTimeStringSuccess("2021-09-31 02", 2, kUndefined,
                                       kUndefined, kUndefined, "");
  VerifyParseTemporalTimeStringSuccess("2021-11-09T0304", 3, 4, kUndefined,
                                       kUndefined, "");
  VerifyParseTemporalTimeStringSuccess("2021-12-07t05:16", 5, 16, kUndefined,
                                       kUndefined, "");
  VerifyParseTemporalTimeStringSuccess("2021-09-31 01:03:04", 1, 3, 4,
                                       kUndefined, "");
  VerifyParseTemporalTimeStringSuccess("2021-09-31 22:59:60", 22, 59, 60,
                                       kUndefined, "");
  VerifyParseTemporalTimeStringSuccess("2021-09-31 215907", 21, 59, 7,
                                       kUndefined, "");
  VerifyParseTemporalTimeStringSuccess("2021-09-31 20:49:37.987654321", 20, 49,
                                       37, 987654321, "");
  VerifyParseTemporalTimeStringSuccess("1964-07-10 19:51:42,123", 19, 51, 42,
                                       123000000, "");
  VerifyParseTemporalTimeStringSuccess("1964-07-10 13:03:60,12345", 13, 3, 60,
                                       123450000, "");
  VerifyParseTemporalTimeStringSuccess("1964-07-10 01:03:04,123456789", 1, 3, 4,
                                       123456789, "");
  VerifyParseTemporalTimeStringSuccess("19640710 09:18:27,12345678", 9, 18, 27,
                                       123456780, "");

  VerifyParseTemporalTimeStringSuccess("2021-11-09T03+11", 3, kUndefined,
                                       kUndefined, kUndefined, "");
  VerifyParseTemporalTimeStringSuccess("2021-11-09t04:55-12:03", 4, 55,
                                       kUndefined, kUndefined, "");
  VerifyParseTemporalTimeStringSuccess("2021-11-09t06:22:01.987654321-12:03", 6,
                                       22, 1, 987654321, "");
  VerifyParseTemporalTimeStringSuccess("2021-11-09t073344,98765432-12:03", 7,
                                       33, 44, 987654320, "");
  VerifyParseTemporalTimeStringSuccess("2021-11-09t07:33:44,98765432-1203", 7,
                                       33, 44, 987654320, "");
  VerifyParseTemporalTimeStringSuccess("2021-11-09 075317,9876543-1203", 7, 53,
                                       17, 987654300, "");
  VerifyParseTemporalTimeStringSuccess("2021-11-09T12-13:03:04", 12, kUndefined,
                                       kUndefined, kUndefined, "");
  VerifyParseTemporalTimeStringSuccess("2021-11-09t1122-120304", 11, 22,
                                       kUndefined, kUndefined, "");
  VerifyParseTemporalTimeStringSuccess("2021-11-09 223344-12:03:04.987654321",
                                       22, 33, 44, kUndefined, "");
  VerifyParseTemporalTimeStringSuccess(
      "2021-11-09 234512.9876-12:03:04.987654321", 23, 45, 12, 987600000, "");

  VerifyParseTemporalTimeStringSuccess(
      "2021-11-09T223344.987654321-120304.123456789", 22, 33, 44, 987654321,
      "");
  VerifyParseTemporalTimeStringSuccess(
      "19670316T223344.987654321-120304.123456789", 22, 33, 44, 987654321, "");

  VerifyParseTemporalTimeStringSuccess("2021-11-09T11z", 11, kUndefined,
                                       kUndefined, kUndefined, "");
  VerifyParseTemporalTimeStringSuccess("2021-11-09t12Z", 12, kUndefined,
                                       kUndefined, kUndefined, "");
  VerifyParseTemporalTimeStringSuccess("2021-11-09 01:23Z", 1, 23, kUndefined,
                                       kUndefined, "");
  VerifyParseTemporalTimeStringSuccess("2021-11-09 01:23:45Z", 1, 23, 45,
                                       kUndefined, "");
  VerifyParseTemporalTimeStringSuccess("2021-11-09 01:23:45.678912345Z", 1, 23,
                                       45, 678912345, "");
  VerifyParseTemporalTimeStringSuccess("2021-11-09 01:23:45,567891234Z", 1, 23,
                                       45, 567891234, "");
  VerifyParseTemporalTimeStringSuccess("2021-11-09 0123Z", 1, 23, kUndefined,
                                       kUndefined, "");
  VerifyParseTemporalTimeStringSuccess("2021-11-09 012345Z", 1, 23, 45,
                                       kUndefined, "");
  VerifyParseTemporalTimeStringSuccess("2021-11-09t012345.678912345Z", 1, 23,
                                       45, 678912345, "");
  VerifyParseTemporalTimeStringSuccess("2021-11-09 012345,891234Z", 1, 23, 45,
                                       891234000, "");
  VerifyParseTemporalTimeStringSuccess("20211109T012345,891234567Z", 1, 23, 45,
                                       891234567, "");

  VerifyParseTemporalTimeStringSuccess(
      "2021-11-09 23:45:56.891234567Z[Etc/GMT+23]", 23, 45, 56, 891234567, "");
  // TimeZoneIANAName
  VerifyParseTemporalTimeStringSuccess("2021-11-09T12z[.BCDEFGHIJKLMN]", 12,
                                       kUndefined, kUndefined, kUndefined, "");
  VerifyParseTemporalTimeStringSuccess(
      "2021-11-09T23:45Z[ABCDEFGHIJKLMN/_bcde-ghij_lmn/.peqrstuv]", 23, 45,
      kUndefined, kUndefined, "");
  VerifyParseTemporalTimeStringSuccess(
      "2021-11-09t234534.234+1234[aBcDEfGHiJ.L_N/ABC...G_..KLMN]", 23, 45, 34,
      234000000, "");
  VerifyParseTemporalTimeStringSuccess(
      "2021-11-09 "
      "123456.789123456-012345.789123456[aBcDEfGHiJ.L_N/ABCbcdGfIJKLMN]",
      12, 34, 56, 789123456, "");

  VerifyParseTemporalTimeStringSuccess("2021-11-09 01:23:45.678912345Z", 1, 23,
                                       45, 678912345, "");

  VerifyParseTemporalTimeStringSuccess("2021-03-11T01[u-ca=iso8601]", 1,
                                       kUndefined, kUndefined, kUndefined,
                                       "iso8601");
  VerifyParseTemporalTimeStringSuccess(
      "2021-03-11 02:34[u-ca=abcdefgh-wxyzefg]", 2, 34, kUndefined, kUndefined,
      "abcdefgh-wxyzefg");

  VerifyParseTemporalTimeStringSuccess(
      "2021-11-03 "
      "123456.789-012345.789123456[aBcDEfGHiJ.L_N/"
      "ABCbcdGfIJKLMN][u-ca=abc]",
      12, 34, 56, 789000000, "abc");

  VerifyParseTemporalTimeStringSuccess(
      "2021-03-11T23[+12:34:56,789123456][u-ca=abcdefgh-wxyzefg]", 23,
      kUndefined, kUndefined, kUndefined, "abcdefgh-wxyzefg");
  VerifyParseTemporalTimeStringSuccess(
      "20210311T22:11[\u221200:34:56.789123456][u-ca=abcdefgh-"
      "wxyzefg-ijklmnop]",
      22, 11, kUndefined, kUndefined, "abcdefgh-wxyzefg-ijklmnop");
  VerifyParseTemporalTimeStringSuccess("2021-11-03T23:45:12.345[u-ca=abc]", 23,
                                       45, 12, 345000000, "abc");
  VerifyParseTemporalTimeStringSuccess("2021-11-03 234527[u-ca=iso-8601]", 23,
                                       45, 27, kUndefined, "iso-8601");

  VerifyParseTemporalTimeStringSuccess("2021-11-03t12[u-ca=123456-789]", 12,
                                       kUndefined, kUndefined, kUndefined,
                                       "123456-789");
}

TEST_F(TemporalParserTest, TemporalTimeStringIllegal) {
  VERIFY_PARSE_FAIL_ON_DATE(TemporalTimeString);
  VERIFY_PARSE_FAIL(TemporalTimeString, "");

  VERIFY_PARSE_FAIL(TemporalTimeString,
                    "2021-03-11t03:45.67[u-ca=abcdefgh-wxyzefg-ijklmnop]");
  // Single digit Hour
  VERIFY_PARSE_FAIL(TemporalTimeString, "0");
  VERIFY_PARSE_FAIL(TemporalTimeString, "9");
  // out of range Hour
  VERIFY_PARSE_FAIL(TemporalTimeString, "99");
  VERIFY_PARSE_FAIL(TemporalTimeString, "24");
  // Single digit Hour or TimeMinute
  VERIFY_PARSE_FAIL(TemporalTimeString, "000");
  VERIFY_PARSE_FAIL(TemporalTimeString, "111");
  VERIFY_PARSE_FAIL(TemporalTimeString, "00:0");
  VERIFY_PARSE_FAIL(TemporalTimeString, "11:1");
  VERIFY_PARSE_FAIL(TemporalTimeString, "0:00");
  VERIFY_PARSE_FAIL(TemporalTimeString, "1:11");
  // out of range Hour TimeMinute
  VERIFY_PARSE_FAIL(TemporalTimeString, "2400");
  VERIFY_PARSE_FAIL(TemporalTimeString, "24:00");
  VERIFY_PARSE_FAIL(TemporalTimeString, "23:60");
  // out of range Hour TimeMinute or TimeSecond
  VERIFY_PARSE_FAIL(TemporalTimeString, "24:00:01");
  VERIFY_PARSE_FAIL(TemporalTimeString, "23:60:01");
  VERIFY_PARSE_FAIL(TemporalTimeString, "23:59:61");

  // Single digit Hour, TimeMinute or TimeSecond
  VERIFY_PARSE_FAIL(TemporalTimeString, "00000");
  VERIFY_PARSE_FAIL(TemporalTimeString, "22222");
  VERIFY_PARSE_FAIL(TemporalTimeString, "00:00:0");
  VERIFY_PARSE_FAIL(TemporalTimeString, "22:2:22");
  VERIFY_PARSE_FAIL(TemporalTimeString, "3:33:33");
  VERIFY_PARSE_FAIL(TemporalTimeString, "444:444");
  VERIFY_PARSE_FAIL(TemporalTimeString, "44444.567");
  VERIFY_PARSE_FAIL(TemporalTimeString, "44444,567");

  // wrong separator
  VERIFY_PARSE_FAIL(TemporalTimeString, "12:34:56 5678");

  // out of range Hour TimeMinute, TimeSecond or TimeFraction
  VERIFY_PARSE_FAIL(TemporalTimeString, "12:34:56.1234567890");
  VERIFY_PARSE_FAIL(TemporalTimeString, "24:01:02.123456789");
  VERIFY_PARSE_FAIL(TemporalTimeString, "23:60:02.123456789");
  VERIFY_PARSE_FAIL(TemporalTimeString, "23:59:61.123456789");
  VERIFY_PARSE_FAIL(TemporalTimeString, "23:33:44.0000000000");

  VERIFY_PARSE_FAIL(TemporalTimeString, "1900-12-31[Etc/GMT+2]");
  VERIFY_PARSE_FAIL(TemporalTimeString, "1900-12-31[Etc/GMT-0]");
  VERIFY_PARSE_FAIL(TemporalTimeString, "1900-12-31[Etc/GMT-0]");

  // Date TimeZone
  // DateExtendedYear
  VERIFY_PARSE_FAIL(TemporalTimeString, "+002021-11-03");
  VERIFY_PARSE_FAIL(TemporalTimeString, "+000001-11-03");
  VERIFY_PARSE_FAIL(TemporalTimeString, "+0020211103");
  VERIFY_PARSE_FAIL(TemporalTimeString, "+0000011231");
  VERIFY_PARSE_FAIL(TemporalTimeString, "+0000000101");
  VERIFY_PARSE_FAIL(TemporalTimeString, "+0000000101");
  VERIFY_PARSE_FAIL(TemporalTimeString, "+654321-11-03");
  VERIFY_PARSE_FAIL(TemporalTimeString, "+999999-12-31");
  VERIFY_PARSE_FAIL(TemporalTimeString, "-654321-11-03");
  VERIFY_PARSE_FAIL(TemporalTimeString, "-999999-12-31");
  VERIFY_PARSE_FAIL(TemporalTimeString, "\u2212999999-12-31");
  VERIFY_PARSE_FAIL(TemporalTimeString, "+6543211103");
  VERIFY_PARSE_FAIL(TemporalTimeString, "+9999991231");
  VERIFY_PARSE_FAIL(TemporalTimeString, "-6543211103");
  VERIFY_PARSE_FAIL(TemporalTimeString, "-9999991231");
  VERIFY_PARSE_FAIL(TemporalTimeString, "\u22129999991231");

  // Date TimeZone
  // Date TimeZoneOffsetRequired
  // Date TimeZoneUTCOffset TimeZoneBracketedAnnotation_opt
  // Date TimeZoneNumericUTCOffset
  VERIFY_PARSE_FAIL(TemporalTimeString, "2021-11-09+11");
  VERIFY_PARSE_FAIL(TemporalTimeString, "2021-11-09-12:03");
  VERIFY_PARSE_FAIL(TemporalTimeString, "2021-11-09-1203");
  VERIFY_PARSE_FAIL(TemporalTimeString, "2021-11-09-12:03:04");
  VERIFY_PARSE_FAIL(TemporalTimeString, "2021-11-09-120304");
  VERIFY_PARSE_FAIL(TemporalTimeString, "2021-11-09-12:03:04,987654321");
  VERIFY_PARSE_FAIL(TemporalTimeString, "2021-11-09-120304.987654321");

  // Date UTCDesignator
  // Date UTCDesignator
  VERIFY_PARSE_FAIL(TemporalTimeString, "2021-11-09z");
  VERIFY_PARSE_FAIL(TemporalTimeString, "2021-11-09Z");

  // Date TimeZoneNameRequired
  // Date TimeZoneBracketedAnnotation
  VERIFY_PARSE_FAIL(TemporalTimeString, "2021-11-09[Etc/GMT+01]");
  VERIFY_PARSE_FAIL(TemporalTimeString, "2021-11-09[Etc/GMT-23]");
  VERIFY_PARSE_FAIL(TemporalTimeString, "2021-11-09[Etc/GMT+23]");
  VERIFY_PARSE_FAIL(TemporalTimeString, "2021-11-09[Etc/GMT-00]");
  VERIFY_PARSE_FAIL(TemporalTimeString, "2021-11-09[Etc/GMT+01]");
  VERIFY_PARSE_FAIL(TemporalTimeString, "2021-11-09[Etc/GMT-23]");

  VERIFY_PARSE_FAIL(TemporalTimeString, "2021-11-09[ABCDEFGHIJKLMN]");
  VERIFY_PARSE_FAIL(TemporalTimeString,
                    "2021-11-09[ABCDEFGHIJKLMN/abcdefghijklmn/opeqrstuv]");
  VERIFY_PARSE_FAIL(TemporalTimeString,
                    "2021-11-09[aBcDEfGHiJ.L_N/ABC...G_..KLMN]");
  VERIFY_PARSE_FAIL(TemporalTimeString,
                    "2021-11-09[aBcDE-GHiJ.L_N/ABCbcdG-IJKLMN]");
  // TimeZoneUTCOffsetName
  VERIFY_PARSE_FAIL(TemporalTimeString, "2021-11-09[+12]");
  VERIFY_PARSE_FAIL(TemporalTimeString, "2021-11-09[+12:34]");
  VERIFY_PARSE_FAIL(TemporalTimeString, "2021-11-09[+12:34:56]");
  VERIFY_PARSE_FAIL(TemporalTimeString, "2021-11-09[+12:34:56,789123456]");
  VERIFY_PARSE_FAIL(TemporalTimeString, "2021-11-09[+12:34:56.789123456]");
  VERIFY_PARSE_FAIL(TemporalTimeString, "2021-11-09[\u221200:34:56.789123456]");
  VERIFY_PARSE_FAIL(TemporalTimeString, "2021-03-11[u-ca=iso8601]");
  VERIFY_PARSE_FAIL(TemporalTimeString, "2021-03-11[u-ca=abcdefgh-wxyzefg]");
  VERIFY_PARSE_FAIL(TemporalTimeString,
                    "2021-03-11[u-ca=abcdefgh-wxyzefg-ijklmnop]");

  VERIFY_PARSE_FAIL(TemporalTimeString,
                    "2021-03-11[+12:34:56,789123456][u-ca=abcdefgh-wxyzefg]");
  VERIFY_PARSE_FAIL(TemporalTimeString, "2021-11-03[u-ca=abc]");
  VERIFY_PARSE_FAIL(TemporalTimeString, "2021-11-03[u-ca=iso-8601]");
  VERIFY_PARSE_FAIL(TemporalTimeString, "2021-11-03[u-ca=123456-789]");
}

#define IMPL_DATE_TIME_STRING_SUCCESS(R)                                       \
  do {                                                                         \
    /* CalendarDateTime : DateTime Calendaropt */                              \
    /* DateTime */                                                             \
    /* DateYear - DateMonth - DateDay */                                       \
    VerifyParse##R##Success("2021-11-03", 2021, 11, 03, kUndefined,            \
                            kUndefined, kUndefined, kUndefined, "");           \
    /* DateYear DateMonth DateDay */                                           \
    VerifyParse##R##Success("20211103", 2021, 11, 03, kUndefined, kUndefined,  \
                            kUndefined, kUndefined, "");                       \
    /* DateExtendedYear */                                                     \
    VerifyParse##R##Success("+002021-11-03", 2021, 11, 03, kUndefined,         \
                            kUndefined, kUndefined, kUndefined, "");           \
    VerifyParse##R##Success("+000001-11-03", 1, 11, 03, kUndefined,            \
                            kUndefined, kUndefined, kUndefined, "");           \
    VerifyParse##R##Success("+0020211103", 2021, 11, 03, kUndefined,           \
                            kUndefined, kUndefined, kUndefined, "");           \
    VerifyParse##R##Success("+0000011231", 1, 12, 31, kUndefined, kUndefined,  \
                            kUndefined, kUndefined, "");                       \
    VerifyParse##R##Success("+0000000101", 0, 1, 1, kUndefined, kUndefined,    \
                            kUndefined, kUndefined, "");                       \
    VerifyParse##R##Success("+0000000101", 0, 1, 1, kUndefined, kUndefined,    \
                            kUndefined, kUndefined, "");                       \
    VerifyParse##R##Success("+654321-11-03", 654321, 11, 3, kUndefined,        \
                            kUndefined, kUndefined, kUndefined, "");           \
    VerifyParse##R##Success("+999999-12-31", 999999, 12, 31, kUndefined,       \
                            kUndefined, kUndefined, kUndefined, "");           \
    VerifyParse##R##Success("-654321-11-03", -654321, 11, 3, kUndefined,       \
                            kUndefined, kUndefined, kUndefined, "");           \
    VerifyParse##R##Success("-999999-12-31", -999999, 12, 31, kUndefined,      \
                            kUndefined, kUndefined, kUndefined, "");           \
    VerifyParse##R##Success("\u2212999999-12-31", -999999, 12, 31, kUndefined, \
                            kUndefined, kUndefined, kUndefined, "");           \
    VerifyParse##R##Success("+6543211103", 654321, 11, 3, kUndefined,          \
                            kUndefined, kUndefined, kUndefined, "");           \
    VerifyParse##R##Success("+9999991231", 999999, 12, 31, kUndefined,         \
                            kUndefined, kUndefined, kUndefined, "");           \
    VerifyParse##R##Success("-6543211103", -654321, 11, 3, kUndefined,         \
                            kUndefined, kUndefined, kUndefined, "");           \
    VerifyParse##R##Success("-9999991231", -999999, 12, 31, kUndefined,        \
                            kUndefined, kUndefined, kUndefined, "");           \
    VerifyParse##R##Success("\u22129999991231", -999999, 12, 31, kUndefined,   \
                            kUndefined, kUndefined, kUndefined, "");           \
                                                                               \
    /* DateTime: Date TimeSpecSeparator_opt TimeZone_opt */                    \
    /* Date TimeSpecSeparator */                                               \
    /* Differeent DateTimeSeparator: <S> T or t */                             \
    VerifyParse##R##Success("2021-11-09T01", 2021, 11, 9, 1, kUndefined,       \
                            kUndefined, kUndefined, "");                       \
    VerifyParse##R##Success("2021-12-07t01", 2021, 12, 7, 1, kUndefined,       \
                            kUndefined, kUndefined, "");                       \
    VerifyParse##R##Success("2021-09-31 01", 2021, 9, 31, 1, kUndefined,       \
                            kUndefined, kUndefined, "");                       \
    VerifyParse##R##Success("2021-11-09T0102", 2021, 11, 9, 1, 2, kUndefined,  \
                            kUndefined, "");                                   \
    VerifyParse##R##Success("2021-12-07t01:02", 2021, 12, 7, 1, 2, kUndefined, \
                            kUndefined, "");                                   \
    VerifyParse##R##Success("2021-09-31 01:03:04", 2021, 9, 31, 1, 3, 4,       \
                            kUndefined, "");                                   \
    VerifyParse##R##Success("2021-09-31 01:03:60", 2021, 9, 31, 1, 3, 60,      \
                            kUndefined, "");                                   \
    VerifyParse##R##Success("2021-09-31 010304", 2021, 9, 31, 1, 3, 4,         \
                            kUndefined, "");                                   \
    VerifyParse##R##Success("2021-09-31 01:03:04.987654321", 2021, 9, 31, 1,   \
                            3, 4, 987654321, "");                              \
    VerifyParse##R##Success("1964-07-10 01:03:04,1", 1964, 7, 10, 1, 3, 4,     \
                            100000000, "");                                    \
    VerifyParse##R##Success("1964-07-10 01:03:60,1", 1964, 7, 10, 1, 3, 60,    \
                            100000000, "");                                    \
    VerifyParse##R##Success("1964-07-10 01:03:04,123456789", 1964, 7, 10, 1,   \
                            3, 4, 123456789, "");                              \
    VerifyParse##R##Success("19640710 01:03:04,123456789", 1964, 7, 10, 1, 3,  \
                            4, 123456789, "");                                 \
    /* Date TimeZone */                                                        \
    /* Date TimeZoneOffsetRequired */                                          \
    /* Date TimeZoneUTCOffset TimeZoneBracketedAnnotation_opt */               \
    /* Date TimeZoneNumericUTCOffset */                                        \
    VerifyParse##R##Success("2021-11-09+11", 2021, 11, 9, kUndefined,          \
                            kUndefined, kUndefined, kUndefined, "");           \
    VerifyParse##R##Success("2021-11-09-12:03", 2021, 11, 9, kUndefined,       \
                            kUndefined, kUndefined, kUndefined, "");           \
    VerifyParse##R##Success("2021-11-09-1203", 2021, 11, 9, kUndefined,        \
                            kUndefined, kUndefined, kUndefined, "");           \
    VerifyParse##R##Success("2021-11-09-12:03:04", 2021, 11, 9, kUndefined,    \
                            kUndefined, kUndefined, kUndefined, "");           \
    VerifyParse##R##Success("2021-11-09-120304", 2021, 11, 9, kUndefined,      \
                            kUndefined, kUndefined, kUndefined, "");           \
    VerifyParse##R##Success("2021-11-09-12:03:04,987654321", 2021, 11, 9,      \
                            kUndefined, kUndefined, kUndefined, kUndefined,    \
                            "");                                               \
    VerifyParse##R##Success("2021-11-09-120304.123456789", 2021, 11, 9,        \
                            kUndefined, kUndefined, kUndefined, kUndefined,    \
                            "");                                               \
    VerifyParse##R##Success("2021-11-09T03+11", 2021, 11, 9, 3, kUndefined,    \
                            kUndefined, kUndefined, "");                       \
    VerifyParse##R##Success("2021-11-09t04:55-12:03", 2021, 11, 9, 4, 55,      \
                            kUndefined, kUndefined, "");                       \
    VerifyParse##R##Success("2021-11-09t06:22:01.987654321", 2021, 11, 9, 6,   \
                            22, 1, 987654321, "");                             \
    VerifyParse##R##Success("2021-11-09t062202,987654321", 2021, 11, 9, 6, 22, \
                            2, 987654321, "");                                 \
    VerifyParse##R##Success("2021-11-09t06:22:03.987654321-1203", 2021, 11, 9, \
                            6, 22, 3, 987654321, "");                          \
    VerifyParse##R##Success("2021-11-09 062204.987654321-1203", 2021, 11, 9,   \
                            6, 22, 4, 987654321, "");                          \
                                                                               \
    VerifyParse##R##Success("2021-11-09T12-12:03:04", 2021, 11, 9, 12,         \
                            kUndefined, kUndefined, kUndefined, "");           \
    VerifyParse##R##Success("2021-11-09t1122-120304", 2021, 11, 9, 11, 22,     \
                            kUndefined, kUndefined, "");                       \
    VerifyParse##R##Success("2021-11-09 223344-12:03:04,987654321", 2021, 11,  \
                            9, 22, 33, 44, kUndefined, "");                    \
    VerifyParse##R##Success("2021-11-09T223344.987654321-120304.123456789",    \
                            2021, 11, 9, 22, 33, 44, 987654321, "");           \
    VerifyParse##R##Success("19670316T223344.987654321-120304.123456789",      \
                            1967, 3, 16, 22, 33, 44, 987654321, "");           \
    /* Date UTCDesignator */                                                   \
    VerifyParse##R##Success("2021-11-09z", 2021, 11, 9, kUndefined,            \
                            kUndefined, kUndefined, kUndefined, "");           \
    VerifyParse##R##Success("2021-11-09Z", 2021, 11, 9, kUndefined,            \
                            kUndefined, kUndefined, kUndefined, "");           \
    VerifyParse##R##Success("2021-11-09T11z", 2021, 11, 9, 11, kUndefined,     \
                            kUndefined, kUndefined, "");                       \
    VerifyParse##R##Success("2021-11-09t12Z", 2021, 11, 9, 12, kUndefined,     \
                            kUndefined, kUndefined, "");                       \
    VerifyParse##R##Success("2021-11-09 01:23Z", 2021, 11, 9, 1, 23,           \
                            kUndefined, kUndefined, "");                       \
    VerifyParse##R##Success("2021-11-09 01:23:45Z", 2021, 11, 9, 1, 23, 45,    \
                            kUndefined, "");                                   \
    VerifyParse##R##Success("2021-11-09 01:23:45.678912345Z", 2021, 11, 9, 1,  \
                            23, 45, 678912345, "");                            \
    VerifyParse##R##Success("2021-11-09 01:23:45,567891234Z", 2021, 11, 9, 1,  \
                            23, 45, 567891234, "");                            \
    VerifyParse##R##Success("2021-11-09 0123Z", 2021, 11, 9, 1, 23,            \
                            kUndefined, kUndefined, "");                       \
    VerifyParse##R##Success("2021-11-09 012345Z", 2021, 11, 9, 1, 23, 45,      \
                            kUndefined, "");                                   \
    VerifyParse##R##Success("2021-11-09t012345.678912345Z", 2021, 11, 9, 1,    \
                            23, 45, 678912345, "");                            \
    VerifyParse##R##Success("2021-11-09 012345,891234567Z", 2021, 11, 9, 1,    \
                            23, 45, 891234567, "");                            \
    VerifyParse##R##Success("20211109T012345,891234567Z", 2021, 11, 9, 1, 23,  \
                            45, 891234567, "");                                \
    /* Date TimeZoneNameRequired */                                            \
    /* Date TimeZoneBracketedAnnotation */                                     \
    VerifyParse##R##Success("2021-11-09[Etc/GMT+1]", 2021, 11, 9, kUndefined,  \
                            kUndefined, kUndefined, kUndefined, "");           \
    VerifyParse##R##Success("2021-11-09[Etc/GMT-23]", 2021, 11, 9, kUndefined, \
                            kUndefined, kUndefined, kUndefined, "");           \
    VerifyParse##R##Success("2021-11-09[Etc/GMT+23]", 2021, 11, 9, kUndefined, \
                            kUndefined, kUndefined, kUndefined, "");           \
    VerifyParse##R##Success("2021-11-09[Etc/GMT-0]", 2021, 11, 9, kUndefined,  \
                            kUndefined, kUndefined, kUndefined, "");           \
    VerifyParse##R##Success("2021-11-09Z[Etc/GMT+1]", 2021, 11, 9, kUndefined, \
                            kUndefined, kUndefined, kUndefined, "");           \
    VerifyParse##R##Success("2021-11-09z[Etc/GMT-23]", 2021, 11, 9,            \
                            kUndefined, kUndefined, kUndefined, kUndefined,    \
                            "");                                               \
    VerifyParse##R##Success("2021-11-09 23:45:56.891234567Z[Etc/GMT+23]",      \
                            2021, 11, 9, 23, 45, 56, 891234567, "");           \
    /* TimeZoneIANAName */                                                     \
    VerifyParse##R##Success("2021-11-09[ABCDEFGHIJKLMN]", 2021, 11, 9,         \
                            kUndefined, kUndefined, kUndefined, kUndefined,    \
                            "");                                               \
    VerifyParse##R##Success(                                                   \
        "2021-11-09[ABCDEFGHIJKLMN/abcdefghijklmn/opeqrstuv]", 2021, 11, 9,    \
        kUndefined, kUndefined, kUndefined, kUndefined, "");                   \
    VerifyParse##R##Success("2021-11-09[aBcDEfGHiJ.L_N/ABC...G_..KLMN]", 2021, \
                            11, 9, kUndefined, kUndefined, kUndefined,         \
                            kUndefined, "");                                   \
    VerifyParse##R##Success("2021-11-09[aBcDE-GHiJ.L_N/ABCbcdG-IJKLMN]", 2021, \
                            11, 9, kUndefined, kUndefined, kUndefined,         \
                            kUndefined, "");                                   \
    VerifyParse##R##Success("2021-11-09T12z[.BCDEFGHIJKLMN]", 2021, 11, 9, 12, \
                            kUndefined, kUndefined, kUndefined, "");           \
    VerifyParse##R##Success(                                                   \
        "2021-11-09T23:45Z[ABCDEFGHIJKLMN/_bcde-ghij_lmn/.peqrstuv]", 2021,    \
        11, 9, 23, 45, kUndefined, kUndefined, "");                            \
    VerifyParse##R##Success(                                                   \
        "2021-11-09t234534.234+1234[aBcDEfGHiJ.L_N/ABC...G_..KLMN]", 2021, 11, \
        9, 23, 45, 34, 234000000, "");                                         \
    VerifyParse##R##Success(                                                   \
                                                                               \
        "2021-11-09 "                                                          \
        "123456.789123456-012345.789123456[aBcDEfGHiJ.L_N/ABCbcdGfIJKLMN]",    \
        2021, 11, 9, 12, 34, 56, 789123456, "");                               \
    /* TimeZoneUTCOffsetName */                                                \
    VerifyParse##R##Success("2021-11-09[+12]", 2021, 11, 9, kUndefined,        \
                            kUndefined, kUndefined, kUndefined, "");           \
    VerifyParse##R##Success("2021-11-09[+12:34]", 2021, 11, 9, kUndefined,     \
                            kUndefined, kUndefined, kUndefined, "");           \
    VerifyParse##R##Success("2021-11-09[-12:34:56]", 2021, 11, 9, kUndefined,  \
                            kUndefined, kUndefined, kUndefined, "");           \
    VerifyParse##R##Success("2021-11-09[+12:34:56,789123456]", 2021, 11, 9,    \
                            kUndefined, kUndefined, kUndefined, kUndefined,    \
                            "");                                               \
    VerifyParse##R##Success("2021-11-09[+12:34:56.789123456]", 2021, 11, 9,    \
                            kUndefined, kUndefined, kUndefined, kUndefined,    \
                            "");                                               \
    VerifyParse##R##Success("2021-11-09[\u221200:34:56.789123456]", 2021, 11,  \
                            9, kUndefined, kUndefined, kUndefined, kUndefined, \
                            "");                                               \
                                                                               \
    /* Date TimeSpecSeparator TimeZone */                                      \
    /* DateTime Calendaropt */                                                 \
    VerifyParse##R##Success("2021-11-03[u-ca=abc]", 2021, 11, 03, kUndefined,  \
                            kUndefined, kUndefined, kUndefined, "abc");        \
    VerifyParse##R##Success("2021-11-03[u-ca=iso-8601]", 2021, 11, 03,         \
                            kUndefined, kUndefined, kUndefined, kUndefined,    \
                            "iso-8601");                                       \
    VerifyParse##R##Success("2021-11-03[u-ca=123456-789]", 2021, 11, 03,       \
                            kUndefined, kUndefined, kUndefined, kUndefined,    \
                            "123456-789");                                     \
    VerifyParse##R##Success("2021-03-11[u-ca=abcdefgh-wxyzefg]", 2021, 3, 11,  \
                            kUndefined, kUndefined, kUndefined, kUndefined,    \
                            "abcdefgh-wxyzefg");                               \
    VerifyParse##R##Success("2021-03-11[u-ca=abcdefgh-wxyzefg-ijklmnop]",      \
                            2021, 3, 11, kUndefined, kUndefined, kUndefined,   \
                            kUndefined, "abcdefgh-wxyzefg-ijklmnop");          \
                                                                               \
    VerifyParse##R##Success(                                                   \
                                                                               \
        "2021-11-03 "                                                          \
        "123456.789123456-012345.789123456[aBcDEfGHiJ.L_N/"                    \
        "ABCbcdGfIJKLMN][u-ca=abc]",                                           \
        2021, 11, 03, 12, 34, 56, 789123456, "abc");                           \
    VerifyParse##R##Success(                                                   \
        "2021-03-11[+12:34:56,789123456][u-ca=abcdefgh-wxyzefg]", 2021, 3, 11, \
        kUndefined, kUndefined, kUndefined, kUndefined, "abcdefgh-wxyzefg");   \
    VerifyParse##R##Success(                                                   \
        "20210311[\u221200:34:56.789123456][u-ca="                             \
        "abcdefgh-wxyzefg-ijklmnop]",                                          \
        2021, 3, 11, kUndefined, kUndefined, kUndefined, kUndefined,           \
        "abcdefgh-wxyzefg-ijklmnop");                                          \
    VerifyParse##R##Success("2021-11-09 01:23:45.678912345Z", 2021, 11, 9, 1,  \
                            23, 45, 678912345, "");                            \
  } while (false)

TEST_F(TemporalParserTest, TemporalDateTimeStringSuccess) {
  IMPL_DATE_TIME_STRING_SUCCESS(TemporalDateTimeString);
}

TEST_F(TemporalParserTest, TemporalDateTimeStringIllegal) {
  VERIFY_PARSE_FAIL_ON_DATE(TemporalDateTimeString);
  VERIFY_PARSE_FAIL(TemporalDateTimeString, "+20210304");
  VERIFY_PARSE_FAIL(TemporalDateTimeString, "-20210304");
  VERIFY_PARSE_FAIL(TemporalDateTimeString, "\u221220210304");
  VERIFY_PARSE_FAIL(TemporalDateTimeString, "210304");
  // It is a Syntax Error if DateExtendedYear is "-000000"
  VERIFY_PARSE_FAIL(TemporalDateTimeString, "-0000000304");
  VERIFY_PARSE_FAIL(TemporalDateTimeString, "\u22120000000304");
}

TEST_F(TemporalParserTest, TemporalYearMonthStringSuccess) {
  // TemporalYearMonthString :
  //   DateSpecYearMonth
  //   DateTime

  // DateSpecYearMonth:
  //   DateYear -opt DateMonth
  VerifyParseTemporalYearMonthStringSuccess("2021-11", 2021, 11, kUndefined,
                                            "");
  VerifyParseTemporalYearMonthStringSuccess("202111", 2021, 11, kUndefined, "");
  VerifyParseTemporalYearMonthStringSuccess("+002021-11", 2021, 11, kUndefined,
                                            "");
  VerifyParseTemporalYearMonthStringSuccess("-002021-02", -2021, 02, kUndefined,
                                            "");
  VerifyParseTemporalYearMonthStringSuccess("-98765412", -987654, 12,
                                            kUndefined, "");

  // DateTime:
  // DateYear - DateMonth - DateDay
  VerifyParseTemporalYearMonthStringSuccess("2021-11-03", 2021, 11, 03, "");
  // DateYear DateMonth DateDay
  VerifyParseTemporalYearMonthStringSuccess("20211103", 2021, 11, 03, "");
  // DateExtendedYear
  VerifyParseTemporalYearMonthStringSuccess("+002021-11-03", 2021, 11, 03, "");
  VerifyParseTemporalYearMonthStringSuccess("+000001-11-03", 1, 11, 03, "");
  VerifyParseTemporalYearMonthStringSuccess("+0020211103", 2021, 11, 03, "");
  VerifyParseTemporalYearMonthStringSuccess("+0000011231", 1, 12, 31, "");
  VerifyParseTemporalYearMonthStringSuccess("+0000000101", 0, 1, 1, "");
  VerifyParseTemporalYearMonthStringSuccess("+0000000101", 0, 1, 1, "");
  VerifyParseTemporalYearMonthStringSuccess("+654321-11-03", 654321, 11, 3, "");
  VerifyParseTemporalYearMonthStringSuccess("+999999-12-31", 999999, 12, 31,
                                            "");
  VerifyParseTemporalYearMonthStringSuccess("-654321-11-03", -654321, 11, 3,
                                            "");
  VerifyParseTemporalYearMonthStringSuccess("-999999-12-31", -999999, 12, 31,
                                            "");
  VerifyParseTemporalYearMonthStringSuccess("\u2212999999-12-31", -999999, 12,
                                            31, "");
  VerifyParseTemporalYearMonthStringSuccess("+6543211103", 654321, 11, 3, "");
  VerifyParseTemporalYearMonthStringSuccess("+9999991231", 999999, 12, 31, "");
  VerifyParseTemporalYearMonthStringSuccess("-6543211103", -654321, 11, 3, "");
  VerifyParseTemporalYearMonthStringSuccess("-9999991231", -999999, 12, 31, "");
  VerifyParseTemporalYearMonthStringSuccess("\u22129999991231", -999999, 12, 31,
                                            "");

  // DateTime: Date TimeSpecSeparator_opt TimeZone_opt
  // Date TimeSpecSeparator
  // Differeent DateTimeSeparator: <S> T or t
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09T01", 2021, 11, 9, "");
  VerifyParseTemporalYearMonthStringSuccess("2021-12-07t01", 2021, 12, 7, "");
  VerifyParseTemporalYearMonthStringSuccess("2021-09-31 01", 2021, 9, 31, "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09T0102", 2021, 11, 9, "");
  VerifyParseTemporalYearMonthStringSuccess("2021-12-07t01:02", 2021, 12, 7,
                                            "");
  VerifyParseTemporalYearMonthStringSuccess("2021-09-31 01:03:04", 2021, 9, 31,
                                            "");
  VerifyParseTemporalYearMonthStringSuccess("2021-09-31 01:03:60", 2021, 9, 31,
                                            "");
  VerifyParseTemporalYearMonthStringSuccess("2021-09-31 010304", 2021, 9, 31,
                                            "");
  VerifyParseTemporalYearMonthStringSuccess("2021-09-31 01:03:04.987654321",
                                            2021, 9, 31, "");
  VerifyParseTemporalYearMonthStringSuccess("1964-07-10 01:03:04,1", 1964, 7,
                                            10, "");
  VerifyParseTemporalYearMonthStringSuccess("1964-07-10 01:03:60,1", 1964, 7,
                                            10, "");
  VerifyParseTemporalYearMonthStringSuccess("1964-07-10 01:03:04,123456789",
                                            1964, 7, 10, "");
  VerifyParseTemporalYearMonthStringSuccess("19640710 01:03:04,123456789", 1964,
                                            7, 10, "");
  // Date TimeZone
  // Date TimeZoneOffsetRequired
  // Date TimeZoneUTCOffset TimeZoneBracketedAnnotation_opt
  // Date TimeZoneNumericUTCOffset
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09+11", 2021, 11, 9, "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09-12:03", 2021, 11, 9,
                                            "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09-1203", 2021, 11, 9, "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09-12:03:04", 2021, 11, 9,
                                            "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09-120304", 2021, 11, 9,
                                            "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09-12:03:04,987654321",
                                            2021, 11, 9, "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09-120304.123456789", 2021,
                                            11, 9, "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09T03+11", 2021, 11, 9,
                                            "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09t04:55-12:03", 2021, 11,
                                            9, "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09t06:22:01.987654321",
                                            2021, 11, 9, "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09t062202,987654321", 2021,
                                            11, 9, "");
  VerifyParseTemporalYearMonthStringSuccess(
      "2021-11-09t06:22:03.987654321-1203", 2021, 11, 9, "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09 062204.987654321-1203",
                                            2021, 11, 9, "");

  VerifyParseTemporalYearMonthStringSuccess("2021-11-09T12-12:03:04", 2021, 11,
                                            9, "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09t1122-120304", 2021, 11,
                                            9, "");
  VerifyParseTemporalYearMonthStringSuccess(
      "2021-11-09 223344-12:03:04,987654321", 2021, 11, 9, "");
  VerifyParseTemporalYearMonthStringSuccess(
      "2021-11-09T223344.987654321-120304.123456789", 2021, 11, 9, "");
  VerifyParseTemporalYearMonthStringSuccess(
      "19670316T223344.987654321-120304.123456789", 1967, 3, 16, "");
  // Date UTCDesignator
  // Date UTCDesignator
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09z", 2021, 11, 9, "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09Z", 2021, 11, 9, "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09T11z", 2021, 11, 9, "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09t12Z", 2021, 11, 9, "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09 01:23Z", 2021, 11, 9,
                                            "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09 01:23:45Z", 2021, 11, 9,
                                            "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09 01:23:45.678912345Z",
                                            2021, 11, 9, "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09 01:23:45,567891234Z",
                                            2021, 11, 9, "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09 0123Z", 2021, 11, 9,
                                            "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09 012345Z", 2021, 11, 9,
                                            "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09t012345.678912345Z",
                                            2021, 11, 9, "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09 012345,891234567Z",
                                            2021, 11, 9, "");
  VerifyParseTemporalYearMonthStringSuccess("20211109T012345,891234567Z", 2021,
                                            11, 9, "");
  // Date TimeZoneNameRequired
  // Date TimeZoneBracketedAnnotation
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09[Etc/GMT+1]", 2021, 11,
                                            9, "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09[Etc/GMT-23]", 2021, 11,
                                            9, "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09[Etc/GMT+23]", 2021, 11,
                                            9, "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09[Etc/GMT-0]", 2021, 11,
                                            9, "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09Z[Etc/GMT+1]", 2021, 11,
                                            9, "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09z[Etc/GMT-23]", 2021, 11,
                                            9, "");
  VerifyParseTemporalYearMonthStringSuccess(
      "2021-11-09 23:45:56.891234567Z[Etc/GMT+23]", 2021, 11, 9, "");
  // TimeZoneIANAName
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09[ABCDEFGHIJKLMN]", 2021,
                                            11, 9, "");
  VerifyParseTemporalYearMonthStringSuccess(
      "2021-11-09[ABCDEFGHIJKLMN/abcdefghijklmn/opeqrstuv]", 2021, 11, 9, "");
  VerifyParseTemporalYearMonthStringSuccess(
      "2021-11-09[aBcDEfGHiJ.L_N/ABC...G_..KLMN]", 2021, 11, 9, "");
  VerifyParseTemporalYearMonthStringSuccess(
      "2021-11-09[aBcDE-GHiJ.L_N/ABCbcdG-IJKLMN]", 2021, 11, 9, "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09T12z[.BCDEFGHIJKLMN]",
                                            2021, 11, 9, "");
  VerifyParseTemporalYearMonthStringSuccess(
      "2021-11-09T23:45Z[ABCDEFGHIJKLMN/_bcde-ghij_lmn/.peqrstuv]", 2021, 11, 9,
      "");
  VerifyParseTemporalYearMonthStringSuccess(
      "2021-11-09t234534.234+1234[aBcDEfGHiJ.L_N/ABC...G_..KLMN]", 2021, 11, 9,
      "");
  VerifyParseTemporalYearMonthStringSuccess(
      "2021-11-09 "
      "123456.789123456-012345.789123456[aBcDEfGHiJ.L_N/ABCbcdGfIJKLMN]",
      2021, 11, 9, "");
  // TimeZoneUTCOffsetName
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09[+12]", 2021, 11, 9, "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09[+12:34]", 2021, 11, 9,
                                            "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09[-12:34:56]", 2021, 11,
                                            9, "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09[+12:34:56,789123456]",
                                            2021, 11, 9, "");
  VerifyParseTemporalYearMonthStringSuccess("2021-11-09[+12:34:56.789123456]",
                                            2021, 11, 9, "");
  VerifyParseTemporalYearMonthStringSuccess(
      "2021-11-09[\u221200:34:56.789123456]", 2021, 11, 9, "");
}

TEST_F(TemporalParserTest, TemporalYearMonthStringIllegal) {
  VERIFY_PARSE_FAIL_ON_DATE(TemporalYearMonthString);
  // DateYear -opt DateMonth
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "+2021-12");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "-2021-12");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "2021\u221212");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "\u22122021-12");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "2021-00");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "2021-13");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "202100");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "202113");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "+98765-12");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "-12345-12");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "\u221212345-12");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "+9876-12");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "-1234-12");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "\u22121234-12");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "+987-12");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "-123-12");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "\u2212123-12");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "+98-12");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "-12-12");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "\u221212-12");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "+9-12");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "-1-12");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "\u22121-12");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "+9876512");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "-1234512");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "\u22121234512");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "+987612");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "-123412");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "\u2212123412");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "+98712");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "\u221212312");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "+9812");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "-1212");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "\u22121212");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "+912");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "-112");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "\u2212112");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "+12");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "-12");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "\u221212");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "+1");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "-1");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "\u22121");
  // It is a Syntax Error if DateExtendedYear is "-000000"
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "-000000");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "\u2212000000");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "-00000001");
  VERIFY_PARSE_FAIL(TemporalYearMonthString, "\u221200000001");
}

TEST_F(TemporalParserTest, TemporalMonthDayStringSuccess) {
  // TemporalMonthDayString :
  //   DateSpecMonthDay
  //   DateTime

  // DateSpecMonthDay:
  //   TwoDashesopt DateMonth -opt DateDay
  VerifyParseTemporalMonthDayStringSuccess("--11-03", kUndefined, 11, 3, "");
  VerifyParseTemporalMonthDayStringSuccess("--1231", kUndefined, 12, 31, "");
  VerifyParseTemporalMonthDayStringSuccess("11-03", kUndefined, 11, 3, "");
  VerifyParseTemporalMonthDayStringSuccess("0131", kUndefined, 1, 31, "");

  // DateTime:
  // DateYear - DateMonth - DateDay
  VerifyParseTemporalMonthDayStringSuccess("2021-11-03", 2021, 11, 03, "");
  // DateYear DateMonth DateDay
  VerifyParseTemporalMonthDayStringSuccess("20211103", 2021, 11, 03, "");
  // DateExtendedYear
  VerifyParseTemporalMonthDayStringSuccess("+002021-11-03", 2021, 11, 03, "");
  VerifyParseTemporalMonthDayStringSuccess("+000001-11-03", 1, 11, 03, "");
  VerifyParseTemporalMonthDayStringSuccess("+0020211103", 2021, 11, 03, "");
  VerifyParseTemporalMonthDayStringSuccess("+0000011231", 1, 12, 31, "");
  VerifyParseTemporalMonthDayStringSuccess("+0000000101", 0, 1, 1, "");
  VerifyParseTemporalMonthDayStringSuccess("+0000000101", 0, 1, 1, "");
  VerifyParseTemporalMonthDayStringSuccess("+654321-11-03", 654321, 11, 3, "");
  VerifyParseTemporalMonthDayStringSuccess("+999999-12-31", 999999, 12, 31, "");
  VerifyParseTemporalMonthDayStringSuccess("-654321-11-03", -654321, 11, 3, "");
  VerifyParseTemporalMonthDayStringSuccess("-999999-12-31", -999999, 12, 31,
                                           "");
  VerifyParseTemporalMonthDayStringSuccess("\u2212999999-12-31", -999999, 12,
                                           31, "");
  VerifyParseTemporalMonthDayStringSuccess("+6543211103", 654321, 11, 3, "");
  VerifyParseTemporalMonthDayStringSuccess("+9999991231", 999999, 12, 31, "");
  VerifyParseTemporalMonthDayStringSuccess("-6543211103", -654321, 11, 3, "");
  VerifyParseTemporalMonthDayStringSuccess("-9999991231", -999999, 12, 31, "");
  VerifyParseTemporalMonthDayStringSuccess("\u22129999991231", -999999, 12, 31,
                                           "");

  // DateTime: Date TimeSpecSeparator_opt TimeZone_opt
  // Date TimeSpecSeparator
  // Differeent DateTimeSeparator: <S> T or t
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09T01", 2021, 11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-12-07t01", 2021, 12, 7, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-09-31 01", 2021, 9, 31, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09T0102", 2021, 11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-12-07t01:02", 2021, 12, 7, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-09-31 01:03:04", 2021, 9, 31,
                                           "");
  VerifyParseTemporalMonthDayStringSuccess("2021-09-31 01:03:60", 2021, 9, 31,
                                           "");
  VerifyParseTemporalMonthDayStringSuccess("2021-09-31 010304", 2021, 9, 31,
                                           "");
  VerifyParseTemporalMonthDayStringSuccess("2021-09-31 01:03:04.987654321",
                                           2021, 9, 31, "");
  VerifyParseTemporalMonthDayStringSuccess("1964-07-10 01:03:04,1", 1964, 7, 10,
                                           "");
  VerifyParseTemporalMonthDayStringSuccess("1964-07-10 01:03:60,1", 1964, 7, 10,
                                           "");
  VerifyParseTemporalMonthDayStringSuccess("1964-07-10 01:03:04,123456789",
                                           1964, 7, 10, "");
  VerifyParseTemporalMonthDayStringSuccess("19640710 01:03:04,123456789", 1964,
                                           7, 10, "");
  // Date TimeZone
  // Date TimeZoneOffsetRequired
  // Date TimeZoneUTCOffset TimeZoneBracketedAnnotation_opt
  // Date TimeZoneNumericUTCOffset
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09+11", 2021, 11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09-12:03", 2021, 11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09-1203", 2021, 11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09-12:03:04", 2021, 11, 9,
                                           "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09-120304", 2021, 11, 9,
                                           "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09-12:03:04,987654321",
                                           2021, 11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09-120304.123456789", 2021,
                                           11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09T03+11", 2021, 11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09t04:55-12:03", 2021, 11,
                                           9, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09t06:22:01.987654321",
                                           2021, 11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09t062202,987654321", 2021,
                                           11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09t06:22:03.987654321-1203",
                                           2021, 11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09 062204.987654321-1203",
                                           2021, 11, 9, "");

  VerifyParseTemporalMonthDayStringSuccess("2021-11-09T12-12:03:04", 2021, 11,
                                           9, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09t1122-120304", 2021, 11,
                                           9, "");
  VerifyParseTemporalMonthDayStringSuccess(
      "2021-11-09 223344-12:03:04,987654321", 2021, 11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess(
      "2021-11-09T223344.987654321-120304.123456789", 2021, 11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess(
      "19670316T223344.987654321-120304.123456789", 1967, 3, 16, "");
  // Date UTCDesignator
  // Date UTCDesignator
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09z", 2021, 11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09Z", 2021, 11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09T11z", 2021, 11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09t12Z", 2021, 11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09 01:23Z", 2021, 11, 9,
                                           "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09 01:23:45Z", 2021, 11, 9,
                                           "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09 01:23:45.678912345Z",
                                           2021, 11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09 01:23:45,567891234Z",
                                           2021, 11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09 0123Z", 2021, 11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09 012345Z", 2021, 11, 9,
                                           "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09t012345.678912345Z", 2021,
                                           11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09 012345,891234567Z", 2021,
                                           11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess("20211109T012345,891234567Z", 2021,
                                           11, 9, "");
  // Date TimeZoneNameRequired
  // Date TimeZoneBracketedAnnotation
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09[Etc/GMT+1]", 2021, 11, 9,
                                           "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09[Etc/GMT-23]", 2021, 11,
                                           9, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09[Etc/GMT+23]", 2021, 11,
                                           9, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09[Etc/GMT-0]", 2021, 11, 9,
                                           "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09Z[Etc/GMT+1]", 2021, 11,
                                           9, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09z[Etc/GMT-23]", 2021, 11,
                                           9, "");
  VerifyParseTemporalMonthDayStringSuccess(
      "2021-11-09 23:45:56.891234567Z[Etc/GMT+23]", 2021, 11, 9, "");
  // TimeZoneIANAName
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09[ABCDEFGHIJKLMN]", 2021,
                                           11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess(
      "2021-11-09[ABCDEFGHIJKLMN/abcdefghijklmn/opeqrstuv]", 2021, 11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess(
      "2021-11-09[aBcDEfGHiJ.L_N/ABC...G_..KLMN]", 2021, 11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess(
      "2021-11-09[aBcDE-GHiJ.L_N/ABCbcdG-IJKLMN]", 2021, 11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09T12z[.BCDEFGHIJKLMN]",
                                           2021, 11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess(
      "2021-11-09T23:45Z[ABCDEFGHIJKLMN/_bcde-ghij_lmn/.peqrstuv]", 2021, 11, 9,
      "");
  VerifyParseTemporalMonthDayStringSuccess(
      "2021-11-09t234534.234+1234[aBcDEfGHiJ.L_N/ABC...G_..KLMN]", 2021, 11, 9,
      "");
  VerifyParseTemporalMonthDayStringSuccess(
      "2021-11-09 "
      "123456.789123456-012345.789123456[aBcDEfGHiJ.L_N/ABCbcdGfIJKLMN]",
      2021, 11, 9, "");
  // TimeZoneUTCOffsetName
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09[+12]", 2021, 11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09[+12:34]", 2021, 11, 9,
                                           "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09[-12:34:56]", 2021, 11, 9,
                                           "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09[+12:34:56,789123456]",
                                           2021, 11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess("2021-11-09[+12:34:56.789123456]",
                                           2021, 11, 9, "");
  VerifyParseTemporalMonthDayStringSuccess(
      "2021-11-09[\u221200:34:56.789123456]", 2021, 11, 9, "");
}

TEST_F(TemporalParserTest, TemporalMonthDayStringIllegal) {
  VERIFY_PARSE_FAIL_ON_DATE(TemporalMonthDayString);
  // TwoDashesopt DateMonth -opt DateDay
  VERIFY_PARSE_FAIL(TemporalMonthDayString, "--13-23");
  VERIFY_PARSE_FAIL(TemporalMonthDayString, "--12-32");
  VERIFY_PARSE_FAIL(TemporalMonthDayString, "--12-00");
  VERIFY_PARSE_FAIL(TemporalMonthDayString, "--00-02");
  VERIFY_PARSE_FAIL(TemporalMonthDayString, "00-02");
  VERIFY_PARSE_FAIL(TemporalMonthDayString, "10-00");
  VERIFY_PARSE_FAIL(TemporalMonthDayString, "0002");
  VERIFY_PARSE_FAIL(TemporalMonthDayString, "1000");
  VERIFY_PARSE_FAIL(TemporalMonthDayString, "-12-23");
  VERIFY_PARSE_FAIL(TemporalMonthDayString, "-1223");
  VERIFY_PARSE_FAIL(TemporalMonthDayString, "--1-23");
  VERIFY_PARSE_FAIL(TemporalMonthDayString, "--12-2");
  VERIFY_PARSE_FAIL(TemporalMonthDayString, "--122");
  VERIFY_PARSE_FAIL(TemporalMonthDayString, "--12-");
  VERIFY_PARSE_FAIL(TemporalMonthDayString, "-12-");
  VERIFY_PARSE_FAIL(TemporalMonthDayString, "--12");
  VERIFY_PARSE_FAIL(TemporalMonthDayString, "-12");
  VERIFY_PARSE_FAIL(TemporalMonthDayString, "-1");
  VERIFY_PARSE_FAIL(TemporalMonthDayString, "-1-2");
}

TEST_F(TemporalParserTest, TemporalInstantStringSuccess) {
  // Date TimeZoneOffsetRequired
  VerifyParseTemporalInstantStringSuccess("2021-11-09z", true, kUndefined,
                                          kUndefined, kUndefined, kUndefined,
                                          kUndefined);
  VerifyParseTemporalInstantStringSuccess("+002021-11-09z", true, kUndefined,
                                          kUndefined, kUndefined, kUndefined,
                                          kUndefined);
  VerifyParseTemporalInstantStringSuccess("-002021-11-09z", true, kUndefined,
                                          kUndefined, kUndefined, kUndefined,
                                          kUndefined);
  VerifyParseTemporalInstantStringSuccess("2021-11-09+00", false, 1, 0,
                                          kUndefined, kUndefined, kUndefined);
  VerifyParseTemporalInstantStringSuccess("20211109+00", false, 1, 0,
                                          kUndefined, kUndefined, kUndefined);
  VerifyParseTemporalInstantStringSuccess("20211109-23", false, -1, 23,
                                          kUndefined, kUndefined, kUndefined);
  VerifyParseTemporalInstantStringSuccess("20211109+0059", false, 1, 0, 59,
                                          kUndefined, kUndefined);
  VerifyParseTemporalInstantStringSuccess("20211109-23:59", false, -1, 23, 59,
                                          kUndefined, kUndefined);
  VerifyParseTemporalInstantStringSuccess("20211109+005921", false, 1, 0, 59,
                                          21, kUndefined);
  VerifyParseTemporalInstantStringSuccess("20211109-23:00:34", false, -1, 23, 0,
                                          34, kUndefined);
  VerifyParseTemporalInstantStringSuccess("20211109+00:59:21.000000001", false,
                                          1, 0, 59, 21, 1);
  VerifyParseTemporalInstantStringSuccess("20211109-230034.9", false, -1, 23, 0,
                                          34, 900000000);
  VerifyParseTemporalInstantStringSuccess("20211109-230035,89", false, -1, 23,
                                          0, 35, 890000000);

  // Date DateTimeSeparator TimeSpec TimeZoneOffsetRequired
  VerifyParseTemporalInstantStringSuccess("2021-11-09T12:34:56.987654321z",
                                          true, kUndefined, kUndefined,
                                          kUndefined, kUndefined, kUndefined);
  VerifyParseTemporalInstantStringSuccess("2021-11-09 12:34:56.987654321z",
                                          true, kUndefined, kUndefined,
                                          kUndefined, kUndefined, kUndefined);
  VerifyParseTemporalInstantStringSuccess("0001-11-09t12:34:56.987654321z",
                                          true, kUndefined, kUndefined,
                                          kUndefined, kUndefined, kUndefined);
  VerifyParseTemporalInstantStringSuccess("2021-11-09T12+00", false, 1, 0,
                                          kUndefined, kUndefined, kUndefined);
  VerifyParseTemporalInstantStringSuccess("20211109t23+00", false, 1, 0,
                                          kUndefined, kUndefined, kUndefined);
  VerifyParseTemporalInstantStringSuccess("20211109 10-23", false, -1, 23,
                                          kUndefined, kUndefined, kUndefined);
  VerifyParseTemporalInstantStringSuccess("20211109T00:34+0059", false, 1, 0,
                                          59, kUndefined, kUndefined);
  VerifyParseTemporalInstantStringSuccess("20211109t0233-23:59", false, -1, 23,
                                          59, kUndefined, kUndefined);
  VerifyParseTemporalInstantStringSuccess("20211109 091234+005921", false, 1, 0,
                                          59, 21, kUndefined);
  VerifyParseTemporalInstantStringSuccess("20211109T123456.789123456-23:00:34",
                                          false, -1, 23, 0, 34, kUndefined);
  VerifyParseTemporalInstantStringSuccess(
      "20211109t12:34:56.987654321+00:59:21.000000001", false, 1, 0, 59, 21, 1);
  VerifyParseTemporalInstantStringSuccess("20211109 235960,999999999-230034.9",
                                          false, -1, 23, 0, 34, 900000000);
  VerifyParseTemporalInstantStringSuccess("20211109T000000.000000000-230035,89",
                                          false, -1, 23, 0, 35, 890000000);
}

TEST_F(TemporalParserTest, TemporalInstantStringIllegal) {
  VERIFY_PARSE_FAIL_ON_DATE(TemporalInstantString);

  // Without TimeZoneUTCOffsetSign
  VERIFY_PARSE_FAIL(TemporalInstantString, "202111090");
  VERIFY_PARSE_FAIL(TemporalInstantString, "202111099");
  VERIFY_PARSE_FAIL(TemporalInstantString, "2021110900");
  VERIFY_PARSE_FAIL(TemporalInstantString, "2021110901");
  VERIFY_PARSE_FAIL(TemporalInstantString, "2021110923");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109ff");

  // Wrong TimeZoneUTCOffsetHour
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109+24");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109-24");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109\u221224");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109+ab");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109-2a");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109\u22122Z");
  // Single digit is not TimeZoneUTCOffsetHour
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109+0");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109+2");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109-1");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109\u22123");

  // Extra
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109+23 ");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109 -22");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109+23:");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109-22:");

  // Wrong TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour : TimeZoneUTCOffsetMinute
  // single digit TimeZoneUTCOffsetMinute
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109+01:0");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109+21:5");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109-20:4");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109\u221219:3");
  // TimeZoneUTCOffsetMinute out of range
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109+01:60");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109+21:5a");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109-20:4f");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109\u221219:a0");

  // Wrong TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour TimeZoneUTCOffsetMinute
  // single digit TimeZoneUTCOffsetMinute
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109+010");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109+215");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109-204");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109\u2212193");
  // TimeZoneUTCOffsetMinute out of range
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109+0160");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109+215a");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109-204f");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109\u221219a0");

  // TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour : TimeZoneUTCOffsetMinute :
  // TimeZoneUTCOffsetSecond TimeZoneUTCOffsetFractionopt with : here but not
  // there
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109+01:0059");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109+1534:33");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109-07:34:339");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109-07:34:.9");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109-07:34:,9");
  // fraction too long
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109-07:34:01.9876543219");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109+07:34:01,9876543219");
  // fraction in hour or minute
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109+01.0:00:59");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109+01:00.1:59");

  // Wrong TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour TimeZoneUTCOffsetMinute
  // TimeZoneUTCOffsetSecond TimeZoneUTCOffsetFractionopt
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109+0100.159");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109+01.15009");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109-0100,159");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109-01,15009");
  // fraction too long
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109-073401.0000000000");
  VERIFY_PARSE_FAIL(TemporalInstantString, "20211109+073401,9876543219");
  // It is a Syntax Error if DateExtendedYear is "-000000"
  VERIFY_PARSE_FAIL(TemporalInstantString, "-0000001109+073401,9876543219");
  VERIFY_PARSE_FAIL(TemporalInstantString,
                    "\u22120000001109+073401,9876543219");
}

#define IMPL_ZONED_DATE_TIME_STRING_SUCCESS(R)                               \
  do {                                                                       \
    VerifyParse##R##Success("2021-11-03T02:03:04.56789z[Asia/Taipei]", 2021, \
                            11, 03, 2, 3, 4, 567890000, "", kUndefined,      \
                            kUndefined, kUndefined, kUndefined, kUndefined,  \
                            true, "Asia/Taipei");                            \
    VerifyParse##R##Success(                                                 \
        "1911-10-10[Asia/Shanghai][u-ca=roc]", 1911, 10, 10, kUndefined,     \
        kUndefined, kUndefined, kUndefined, "roc", kUndefined, kUndefined,   \
        kUndefined, kUndefined, kUndefined, false, "Asia/Shanghai");         \
    VerifyParse##R##Success(                                                 \
                                                                             \
        "+123456-12-31 "                                                     \
        "05:06:07+12:34:56.78901234[Europe/San_Marino][u-ca=hebrew]",        \
        123456, 12, 31, 5, 6, 7, kUndefined, "hebrew", 1, 12, 34, 56,        \
        789012340, false, "Europe/San_Marino");                              \
  } while (false)

TEST_F(TemporalParserTest, TemporalZonedDateTimeStringSuccess) {
  IMPL_ZONED_DATE_TIME_STRING_SUCCESS(TemporalZonedDateTimeString);
}

#define VERIFY_PARSE_FAIL_ON_ZONED_DATE_TIME(R)                            \
  do {                                                                     \
    VERIFY_PARSE_FAIL_ON_DATE(R);                                          \
    VERIFY_PARSE_FAIL(R, "+20210304");                                     \
    VERIFY_PARSE_FAIL(R, "-20210304");                                     \
    VERIFY_PARSE_FAIL(R, "\u221220210304");                                \
    VERIFY_PARSE_FAIL(R, "210304");                                        \
    VERIFY_PARSE_FAIL(R, "2021-03-04");                                    \
    VERIFY_PARSE_FAIL(R, "2021-03-04T23:59:20.1234");                      \
    VERIFY_PARSE_FAIL(R, "2021-03-04T23:59:20.1234z");                     \
    VERIFY_PARSE_FAIL(R, "2021-03-04[u-ca=roc]");                          \
    VERIFY_PARSE_FAIL(R, "2021-03-04T23:59:20.1234[u-ca=roc]");            \
    VERIFY_PARSE_FAIL(R, "2021-03-04T23:59:20.1234z[u-ca=roc]");           \
    VERIFY_PARSE_FAIL(R, "2021-03-04[]");                                  \
    VERIFY_PARSE_FAIL(R, "2021-03-04T23:59:20.1234[]");                    \
    VERIFY_PARSE_FAIL(R, "2021-03-04T23:59:20.1234z[]");                   \
    VERIFY_PARSE_FAIL(R, "2021-03-04[etc/gmt+00]");                        \
    VERIFY_PARSE_FAIL(R, "2021-03-04T23:59:20.1234[ETC/GMT+00]");          \
    VERIFY_PARSE_FAIL(R, "2021-03-04T23:59:20.1234z[Etc/GMT+24]");         \
    VERIFY_PARSE_FAIL(R, "2021-03-04[Etc/GMT+00:00]");                     \
    VERIFY_PARSE_FAIL(R, "2021-03-04[Etc/GMT\u221200]");                   \
    VERIFY_PARSE_FAIL(R, "2021-03-04T23:59:20.1234[.]");                   \
    VERIFY_PARSE_FAIL(R, "2021-03-04T23:59:20.1234z[..]");                 \
    VERIFY_PARSE_FAIL(R, "2021-03-04T23:59:20.1234[ABCD/.]");              \
    VERIFY_PARSE_FAIL(R, "2021-03-04T23:59:20.1234z[EFGH/..]");            \
    VERIFY_PARSE_FAIL(R, "2021-03-04T23:59:20.1234[abcdefghijklmno]");     \
    VERIFY_PARSE_FAIL(R, "2021-03-04 23:59:20.1234[abc/abcdefghijklmno]"); \
    VERIFY_PARSE_FAIL(R, "2021-03-04t23:59:20.1234[+1]");                  \
    VERIFY_PARSE_FAIL(R, "2021-03-04t23:59:20.1234[+123]");                \
    VERIFY_PARSE_FAIL(R, "2021-03-04t23:59:20.1234[+12345]");              \
    VERIFY_PARSE_FAIL(R, "2021-03-04t23:59:20.1234[-1]");                  \
    VERIFY_PARSE_FAIL(R, "2021-03-04t23:59:20.1234[-123]");                \
    VERIFY_PARSE_FAIL(R, "2021-03-04t23:59:20.1234[-12345]");              \
    VERIFY_PARSE_FAIL(R, "2021-03-04t23:59:20.1234[+12:3456]");            \
    VERIFY_PARSE_FAIL(R, "2021-03-04t23:59:20.1234[+1234:56]");            \
    VERIFY_PARSE_FAIL(R, "2021-03-04t23:59:20.1234[+123456.9876543210]");  \
    VERIFY_PARSE_FAIL(R, "2021-03-04t23:59:20.1234[+123456.]");            \
    VERIFY_PARSE_FAIL(R, "2021-03-04t23:59:20.1234[+123456,]");            \
    VERIFY_PARSE_FAIL(R, "-000000-03-04t23:59:20.1234[+123456,]");         \
    VERIFY_PARSE_FAIL(R, "\u2212000000-03-04t23:59:20.1234[+123456,]");    \
  } while (false)

TEST_F(TemporalParserTest, TemporalZonedDateTimeStringIllegal) {
  VERIFY_PARSE_FAIL_ON_ZONED_DATE_TIME(TemporalZonedDateTimeString);
}

constexpr int64_t empty = ParsedISO8601Duration::kEmpty;

// Test basic cases.
TEST_F(TemporalParserTest, TemporalDurationStringBasic) {
  VerifyParseDurationSuccess("PT0S", 1, empty, empty, empty, empty, empty,
                             empty, empty, empty, 0, empty);
  VerifyParseDurationSuccess("-PT0S", -1, empty, empty, empty, empty, empty,
                             empty, empty, empty, 0, empty);
  VerifyParseDurationSuccess("P1Y", 1, 1, empty, empty, empty, empty, empty,
                             empty, empty, empty, empty);
  VerifyParseDurationSuccess("P2M", 1, empty, 2, empty, empty, empty, empty,
                             empty, empty, empty, empty);
  VerifyParseDurationSuccess("P3W", 1, empty, empty, 3, empty, empty, empty,
                             empty, empty, empty, empty);
  VerifyParseDurationSuccess("P4D", 1, empty, empty, empty, 4, empty, empty,
                             empty, empty, empty, empty);
  VerifyParseDurationSuccess("PT5H", 1, empty, empty, empty, empty, 5, empty,
                             empty, empty, empty, empty);
  VerifyParseDurationSuccess("PT1.987654321H", 1, empty, empty, empty, empty, 1,
                             987654321, empty, empty, empty, empty);
  VerifyParseDurationSuccess("PT2.9H", 1, empty, empty, empty, empty, 2,
                             900000000, empty, empty, empty, empty);
  VerifyParseDurationSuccess("PT6M", 1, empty, empty, empty, empty, empty,
                             empty, 6, empty, empty, empty);
  VerifyParseDurationSuccess("PT2.234567891M", 1, empty, empty, empty, empty,
                             empty, empty, 2, 234567891, empty, empty);
  VerifyParseDurationSuccess("PT3.23M", 1, empty, empty, empty, empty, empty,
                             empty, 3, 230000000, empty, empty);
  VerifyParseDurationSuccess("PT7S", 1, empty, empty, empty, empty, empty,
                             empty, empty, empty, 7, empty);
  VerifyParseDurationSuccess("PT3.345678912S", 1, empty, empty, empty, empty,
                             empty, empty, empty, empty, 3, 345678912);
  VerifyParseDurationSuccess("PT4.345S", 1, empty, empty, empty, empty, empty,
                             empty, empty, empty, 4, 345000000);

  VerifyParseDurationSuccess("P1Y2M3W4DT5.6H7.8M9.1S", 1, 1, 2, 3, 4, 5,
                             600000000, 7, 800000000, 9, 100000000);
  VerifyParseDurationSuccess("-P9Y8M7W6DT5.4H3.2M1.9S", -1, 9, 8, 7, 6, 5,
                             400000000, 3, 200000000, 1, 900000000);

  VerifyParseDurationSuccess("P0Y0M0W0DT0.0H0.0M0.0S", 1, 0, 0, 0, 0, 0, 0, 0,
                             0, 0, 0);
  VerifyParseDurationSuccess("-P0Y0M0W0DT0.0H0.0M0.0S", -1, 0, 0, 0, 0, 0, 0, 0,
                             0, 0, 0);
}

// Test duration with ascii minus sign parsed correctly.
TEST_F(TemporalParserTest, TemporalDurationStringNegative) {
  VerifyParseDurationSuccess("-P1Y", -1, 1, empty, empty, empty, empty, empty,
                             empty, empty, empty, empty);
  VerifyParseDurationSuccess("-P2M", -1, empty, 2, empty, empty, empty, empty,
                             empty, empty, empty, empty);
  VerifyParseDurationSuccess("-P3W", -1, empty, empty, 3, empty, empty, empty,
                             empty, empty, empty, empty);
  VerifyParseDurationSuccess("-P4D", -1, empty, empty, empty, 4, empty, empty,
                             empty, empty, empty, empty);
  VerifyParseDurationSuccess("-PT5H", -1, empty, empty, empty, empty, 5, empty,
                             empty, empty, empty, empty);
  VerifyParseDurationSuccess("-PT4.123H", -1, empty, empty, empty, empty, 4,
                             123000000, empty, empty, empty, empty);
  VerifyParseDurationSuccess("-PT3.123456H", -1, empty, empty, empty, empty, 3,
                             123456000, empty, empty, empty, empty);
  VerifyParseDurationSuccess("-PT6M", -1, empty, empty, empty, empty, empty,
                             empty, 6, empty, empty, empty);
  VerifyParseDurationSuccess("-PT5.2M", -1, empty, empty, empty, empty, empty,
                             empty, 5, 200000000, empty, empty);
  VerifyParseDurationSuccess("-PT4.3456M", -1, empty, empty, empty, empty,
                             empty, empty, 4, 345600000, empty, empty);
  VerifyParseDurationSuccess("-PT7S", -1, empty, empty, empty, empty, empty,
                             empty, empty, empty, 7, empty);
  VerifyParseDurationSuccess("-PT6.987S", -1, empty, empty, empty, empty, empty,
                             empty, empty, empty, 6, 987000000);
}

// Test duration with + sign parsed the same as without + sign.
TEST_F(TemporalParserTest, TemporalDurationStringPlus) {
  // Check + Sign
  VerifyParseDurationWithPositiveSign("P1Y");
  VerifyParseDurationWithPositiveSign("P2M");
  VerifyParseDurationWithPositiveSign("P3W");
  VerifyParseDurationWithPositiveSign("P4D");
  VerifyParseDurationWithPositiveSign("PT5H");
  VerifyParseDurationWithPositiveSign("PT1.987654321H");
  VerifyParseDurationWithPositiveSign("PT2.9H");
  VerifyParseDurationWithPositiveSign("PT6M");
  VerifyParseDurationWithPositiveSign("PT2.234567891M");
  VerifyParseDurationWithPositiveSign("PT3.23M");
  VerifyParseDurationWithPositiveSign("PT7S");
  VerifyParseDurationWithPositiveSign("PT3.345678912S");
}

// Test duration with Unicode U+2212 minus sign parsed the same as ascii - sign.
TEST_F(TemporalParserTest, TemporalDurationStringMinus) {
  // Check + Sign
  VerifyParseDurationWithMinusSign("P1Y");
  VerifyParseDurationWithMinusSign("P2M");
  VerifyParseDurationWithMinusSign("P3W");
  VerifyParseDurationWithMinusSign("P4D");
  VerifyParseDurationWithMinusSign("PT5H");
  VerifyParseDurationWithMinusSign("PT1.987654321H");
  VerifyParseDurationWithMinusSign("PT2.9H");
  VerifyParseDurationWithMinusSign("PT6M");
  VerifyParseDurationWithMinusSign("PT2.234567891M");
  VerifyParseDurationWithMinusSign("PT3.23M");
  VerifyParseDurationWithMinusSign("PT7S");
  VerifyParseDurationWithMinusSign("PT3.345678912S");
}

// Test duration in lower case mark parsed the same as with upper case mark.
TEST_F(TemporalParserTest, TemporalDurationStringLowerCase) {
  // Check + Sign
  VerifyParseDurationWithLowerCase("P1Y");
  VerifyParseDurationWithLowerCase("P2M");
  VerifyParseDurationWithLowerCase("P3W");
  VerifyParseDurationWithLowerCase("P4D");
  VerifyParseDurationWithLowerCase("PT5H");
  VerifyParseDurationWithLowerCase("PT1.987654321H");
  VerifyParseDurationWithLowerCase("PT2.9H");
  VerifyParseDurationWithLowerCase("PT6M");
  VerifyParseDurationWithLowerCase("PT2.234567891M");
  VerifyParseDurationWithLowerCase("PT3.23M");
  VerifyParseDurationWithLowerCase("PT7S");
  VerifyParseDurationWithLowerCase("PT3.345678912S");
}

TEST_F(TemporalParserTest, TemporalDurationStringComma) {
  VerifyParseDurationWithComma("PT1,987654321H");
  VerifyParseDurationWithComma("PT2,9H");
  VerifyParseDurationWithComma("PT2,234567891M");
  VerifyParseDurationWithComma("PT3,23M");
  VerifyParseDurationWithComma("PT3,345678912S");
}

TEST_F(TemporalParserTest, TemporalDurationStringLongDigits) {
  VerifyParseDurationSuccess("P8999999999999999999Y", 1, 8999999999999999999,
                             empty, empty, empty, empty, empty, empty, empty,
                             empty, empty);
  VerifyParseDurationSuccess("P8999999999999999998M", 1, empty,
                             8999999999999999998, empty, empty, empty, empty,
                             empty, empty, empty, empty);
  VerifyParseDurationSuccess("P8999999999999999997W", 1, empty, empty,
                             8999999999999999997, empty, empty, empty, empty,
                             empty, empty, empty);
  VerifyParseDurationSuccess("P8999999999999999996D", 1, empty, empty, empty,
                             8999999999999999996, empty, empty, empty, empty,
                             empty, empty);
  VerifyParseDurationSuccess("PT8999999999999999995H", 1, empty, empty, empty,
                             empty, 8999999999999999995, empty, empty, empty,
                             empty, empty);
  VerifyParseDurationSuccess("PT8999999999999999994M", 1, empty, empty, empty,
                             empty, empty, empty, 8999999999999999994, empty,
                             empty, empty);
  VerifyParseDurationSuccess("PT8999999999999999993S", 1, empty, empty, empty,
                             empty, empty, empty, empty, empty,
                             8999999999999999993, empty);

  VerifyParseDurationSuccess("PT0.999999999H", 1, empty, empty, empty, empty, 0,
                             999999999, empty, empty, empty, empty);
  VerifyParseDurationSuccess("PT0.999999999M", 1, empty, empty, empty, empty,
                             empty, empty, 0, 999999999, empty, empty);
  VerifyParseDurationSuccess("PT0.999999999S", 1, empty, empty, empty, empty,
                             empty, empty, empty, empty, 0, 999999999);

  VerifyParseDurationSuccess("-P8999999999999999999Y", -1, 8999999999999999999,
                             empty, empty, empty, empty, empty, empty, empty,
                             empty, empty);
  VerifyParseDurationSuccess("-P8999999999999999998M", -1, empty,
                             8999999999999999998, empty, empty, empty, empty,
                             empty, empty, empty, empty);
  VerifyParseDurationSuccess("-P8999999999999999997W", -1, empty, empty,
                             8999999999999999997, empty, empty, empty, empty,
                             empty, empty, empty);
  VerifyParseDurationSuccess("-P8999999999999999996D", -1, empty, empty, empty,
                             8999999999999999996, empty, empty, empty, empty,
                             empty, empty);
  VerifyParseDurationSuccess("-PT8999999999999999995H", -1, empty, empty, empty,
                             empty, 8999999999999999995, empty, empty, empty,
                             empty, empty);
  VerifyParseDurationSuccess("-PT8999999999999999995H", -1, empty, empty, empty,
                             empty, 8999999999999999995, empty, empty, empty,
                             empty, empty);
  VerifyParseDurationSuccess("-PT8999999999999999994M", -1, empty, empty, empty,
                             empty, empty, empty, 8999999999999999994, empty,
                             empty, empty);
  VerifyParseDurationSuccess("-PT8999999999999999993S", -1, empty, empty, empty,
                             empty, empty, empty, empty, empty,
                             8999999999999999993, empty);

  VerifyParseDurationSuccess("-PT0.999999999H", -1, empty, empty, empty, empty,
                             0, 999999999, empty, empty, empty, empty);
  VerifyParseDurationSuccess("-PT0.999999999M", -1, empty, empty, empty, empty,
                             empty, empty, 0, 999999999, empty, empty);
  VerifyParseDurationSuccess("-PT0.999999999S", -1, empty, empty, empty, empty,
                             empty, empty, empty, empty, 0, 999999999);
}

TEST_F(TemporalParserTest, TemporalDurationStringNotSatisfy) {
  VERIFY_PARSE_FAIL(TemporalDurationString, "");

  // Missing P
  VERIFY_PARSE_FAIL(TemporalDurationString, "1Y");
  VERIFY_PARSE_FAIL(TemporalDurationString, "1M");
  VERIFY_PARSE_FAIL(TemporalDurationString, "+1W");
  VERIFY_PARSE_FAIL(TemporalDurationString, "-1D");

  // fraction with years, months, weeks or days
  VERIFY_PARSE_FAIL(TemporalDurationString, "P1.1Y");
  VERIFY_PARSE_FAIL(TemporalDurationString, "P2.2M");
  VERIFY_PARSE_FAIL(TemporalDurationString, "P3.3W");
  VERIFY_PARSE_FAIL(TemporalDurationString, "P4.4D");

  // Time without T
  VERIFY_PARSE_FAIL(TemporalDurationString, "P1H");
  VERIFY_PARSE_FAIL(TemporalDurationString, "P1S");

  // Sign after P
  VERIFY_PARSE_FAIL(TemporalDurationString, "P+1Y");
  VERIFY_PARSE_FAIL(TemporalDurationString, "P-2M");
  VERIFY_PARSE_FAIL(TemporalDurationString, "P\u22123W");
  VERIFY_PARSE_FAIL(TemporalDurationString, "P+4D");
  VERIFY_PARSE_FAIL(TemporalDurationString, "PT-4H");
  VERIFY_PARSE_FAIL(TemporalDurationString, "PT+5M");

  // with :
  VERIFY_PARSE_FAIL(TemporalDurationString, "PT01:22");

  // more than 9 digits in fraction
  VERIFY_PARSE_FAIL(TemporalDurationString, "PT1.9876543219H");
  VERIFY_PARSE_FAIL(TemporalDurationString, "PT0.9876543219M");
  VERIFY_PARSE_FAIL(TemporalDurationString, "PT2.9876543219S");

  // out of order
  VERIFY_PARSE_FAIL(TemporalDurationString, "P2M1Y");
  VERIFY_PARSE_FAIL(TemporalDurationString, "P3W4M");
  VERIFY_PARSE_FAIL(TemporalDurationString, "P5D6W");
  VERIFY_PARSE_FAIL(TemporalDurationString, "PT1H6Y");
  VERIFY_PARSE_FAIL(TemporalDurationString, "PT1M6W");
  VERIFY_PARSE_FAIL(TemporalDurationString, "PT1S6D");

  // Extra in the end
  VERIFY_PARSE_FAIL(TemporalDurationString, "P1Y ");
  VERIFY_PARSE_FAIL(TemporalDurationString, "P1Yp");
  VERIFY_PARSE_FAIL(TemporalDurationString, "P2M:");

  // Extra in the beginning
  VERIFY_PARSE_FAIL(TemporalDurationString, "pP1Y");
  VERIFY_PARSE_FAIL(TemporalDurationString, " P1Y");
  VERIFY_PARSE_FAIL(TemporalDurationString, ".P2M");

  // Fraction without digit
  VERIFY_PARSE_FAIL(TemporalDurationString, "PT.1H");
  VERIFY_PARSE_FAIL(TemporalDurationString, "PT.2M");
  VERIFY_PARSE_FAIL(TemporalDurationString, "PT.3S");

  // without date nor time
  VERIFY_PARSE_FAIL(TemporalDurationString, "P");
  VERIFY_PARSE_FAIL(TemporalDurationString, "PT");
}

TEST_F(TemporalParserTest, TimeZoneNumericUTCOffsetBasic) {
  // TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour
  VerifyParseTimeZoneNumericUTCOffsetSuccess("+00", 1, 0, kUndefined,
                                             kUndefined, kUndefined);
  VerifyParseTimeZoneNumericUTCOffsetSuccess("+23", 1, 23, kUndefined,
                                             kUndefined, kUndefined);
  VerifyParseTimeZoneNumericUTCOffsetSuccess("-23", -1, 23, kUndefined,
                                             kUndefined, kUndefined);
  VerifyParseTimeZoneNumericUTCOffsetSuccess("\u221223", -1, 23, kUndefined,
                                             kUndefined, kUndefined);

  // TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour : TimeZoneUTCOffsetMinute
  VerifyParseTimeZoneNumericUTCOffsetSuccess("+01:00", 1, 1, 0, kUndefined,
                                             kUndefined);
  VerifyParseTimeZoneNumericUTCOffsetSuccess("+21:59", 1, 21, 59, kUndefined,
                                             kUndefined);
  VerifyParseTimeZoneNumericUTCOffsetSuccess("-20:48", -1, 20, 48, kUndefined,
                                             kUndefined);
  VerifyParseTimeZoneNumericUTCOffsetSuccess("\u221219:33", -1, 19, 33,
                                             kUndefined, kUndefined);

  // TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour TimeZoneUTCOffsetMinute
  VerifyParseTimeZoneNumericUTCOffsetSuccess("+0100", 1, 1, 0, kUndefined,
                                             kUndefined);
  VerifyParseTimeZoneNumericUTCOffsetSuccess("+2159", 1, 21, 59, kUndefined,
                                             kUndefined);
  VerifyParseTimeZoneNumericUTCOffsetSuccess("-2048", -1, 20, 48, kUndefined,
                                             kUndefined);
  VerifyParseTimeZoneNumericUTCOffsetSuccess("\u22121933", -1, 19, 33,
                                             kUndefined, kUndefined);

  // TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour : TimeZoneUTCOffsetMinute :
  // TimeZoneUTCOffsetSecond TimeZoneUTCOffsetFractionopt
  VerifyParseTimeZoneNumericUTCOffsetSuccess("+01:00:59", 1, 1, 0, 59,
                                             kUndefined);
  VerifyParseTimeZoneNumericUTCOffsetSuccess("+15:34:33", 1, 15, 34, 33,
                                             kUndefined);
  VerifyParseTimeZoneNumericUTCOffsetSuccess("-09:59:00", -1, 9, 59, 00,
                                             kUndefined);
  VerifyParseTimeZoneNumericUTCOffsetSuccess("\u221218:53:22", -1, 18, 53, 22,
                                             kUndefined);

  VerifyParseTimeZoneNumericUTCOffsetSuccess("+01:00:59.987654321", 1, 1, 0, 59,
                                             987654321);
  // ',' as DecimalSeparator
  VerifyParseTimeZoneNumericUTCOffsetSuccess("+01:00:59,123456789", 1, 1, 0, 59,
                                             123456789);
  VerifyParseTimeZoneNumericUTCOffsetSuccess("-09:59:00.9", -1, 9, 59, 00,
                                             900000000);
  // ',' as DecimalSeparator
  VerifyParseTimeZoneNumericUTCOffsetSuccess("-09:59:00,000000001", -1, 9, 59,
                                             00, 1);
  VerifyParseTimeZoneNumericUTCOffsetSuccess("-09:59:00.000000001", -1, 9, 59,
                                             00, 1);

  // TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour TimeZoneUTCOffsetMinute
  // TimeZoneUTCOffsetSecond TimeZoneUTCOffsetFractionopt
  VerifyParseTimeZoneNumericUTCOffsetSuccess("+010059", 1, 1, 0, 59,
                                             kUndefined);
  VerifyParseTimeZoneNumericUTCOffsetSuccess("+153433", 1, 15, 34, 33,
                                             kUndefined);
  VerifyParseTimeZoneNumericUTCOffsetSuccess("-095900", -1, 9, 59, 00,
                                             kUndefined);
  VerifyParseTimeZoneNumericUTCOffsetSuccess("\u2212185322", -1, 18, 53, 22,
                                             kUndefined);

  VerifyParseTimeZoneNumericUTCOffsetSuccess("+010059.987654321", 1, 1, 0, 59,
                                             987654321);
  // ',' as DecimalSeparator
  VerifyParseTimeZoneNumericUTCOffsetSuccess("+010059,123456789", 1, 1, 0, 59,
                                             123456789);
  VerifyParseTimeZoneNumericUTCOffsetSuccess("-095900.9", -1, 9, 59, 00,
                                             900000000);
  // ',' as DecimalSeparator
  VerifyParseTimeZoneNumericUTCOffsetSuccess("-095900,000000001", -1, 9, 59, 00,
                                             1);
  VerifyParseTimeZoneNumericUTCOffsetSuccess("-095900.000000001", -1, 9, 59, 00,
                                             1);
}

TEST_F(TemporalParserTest, TimeZoneNumericUTCOffsetIllegal) {
  // Without TimeZoneUTCOffsetSign
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "0");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "9");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "00");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "01");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "23");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "ff");

  // Wrong TimeZoneUTCOffsetHour
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "+24");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "-24");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "\u221224");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "+ab");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "-2a");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "\u22122Z");
  // Single digit is not TimeZoneUTCOffsetHour
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "+0");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "+2");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "-1");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "\u22123");

  // Extra
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "+23 ");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, " -22");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "+23:");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "-22:");

  // Wrong TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour : TimeZoneUTCOffsetMinute
  // single digit TimeZoneUTCOffsetMinute
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "+01:0");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "+21:5");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "-20:4");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "\u221219:3");
  // TimeZoneUTCOffsetMinute out of range
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "+01:60");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "+21:5a");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "-20:4f");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "\u221219:a0");

  // Wrong TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour TimeZoneUTCOffsetMinute
  // single digit TimeZoneUTCOffsetMinute
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "+010");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "+215");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "-204");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "\u2212193");
  // TimeZoneUTCOffsetMinute out of range
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "+0160");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "+215a");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "-204f");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "\u221219a0");

  // TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour : TimeZoneUTCOffsetMinute :
  // TimeZoneUTCOffsetSecond TimeZoneUTCOffsetFractionopt with : here but not
  // there
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "+01:0059");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "+1534:33");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "-07:34:339");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "-07:34:.9");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "-07:34:,9");
  // fraction too long
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "-07:34:01.9876543219");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "+07:34:01,9876543219");
  // fraction in hour or minute
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "+01.0:00:59");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "+01:00.1:59");

  // Wrong TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour TimeZoneUTCOffsetMinute
  // TimeZoneUTCOffsetSecond TimeZoneUTCOffsetFractionopt
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "+0100.159");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "+01.15009");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "-0100,159");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "-01,15009");
  // fraction too long
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "-073401.0000000000");
  VERIFY_PARSE_FAIL(TimeZoneNumericUTCOffset, "+073401,9876543219");
}

TEST_F(TemporalParserTest, TimeZoneIdentifierSucccess) {
  // TimeZoneIANAName:
  //  Etc/GMT ASCIISign UnpaddedHour:
  VerifyParseTimeZoneIdentifierSuccess("Etc/GMT+0");
  VerifyParseTimeZoneIdentifierSuccess("Etc/GMT+1");
  VerifyParseTimeZoneIdentifierSuccess("Etc/GMT+11");
  VerifyParseTimeZoneIdentifierSuccess("Etc/GMT+23");
  //  TimeZoneIANANameTail
  VerifyParseTimeZoneIdentifierSuccess("_");
  VerifyParseTimeZoneIdentifierSuccess("_/_");
  VerifyParseTimeZoneIdentifierSuccess("a.");
  VerifyParseTimeZoneIdentifierSuccess("a..");
  VerifyParseTimeZoneIdentifierSuccess("a_");
  VerifyParseTimeZoneIdentifierSuccess("a-");
  VerifyParseTimeZoneIdentifierSuccess("a-b");
  VerifyParseTimeZoneIdentifierSuccess("a-b/c");
  VerifyParseTimeZoneIdentifierSuccess("abcdefghijklmn");
  VerifyParseTimeZoneIdentifierSuccess("abcdefghijklmn/ABCDEFGHIJKLMN");

  //  TimeZoneIANALegacyName
  VerifyParseTimeZoneIdentifierSuccess("Etc/GMT0");
  VerifyParseTimeZoneIdentifierSuccess("GMT0");
  VerifyParseTimeZoneIdentifierSuccess("GMT-0");
  VerifyParseTimeZoneIdentifierSuccess("GMT+0");
  VerifyParseTimeZoneIdentifierSuccess("EST5EDT");
  VerifyParseTimeZoneIdentifierSuccess("CST6CDT");
  VerifyParseTimeZoneIdentifierSuccess("MST7MDT");
  VerifyParseTimeZoneIdentifierSuccess("PST8PDT");

  // TimeZoneUTCOffsetName
  //  Sign Hour
  VerifyParseTimeZoneIdentifierSuccess("+00");
  VerifyParseTimeZoneIdentifierSuccess("+23");
  VerifyParseTimeZoneIdentifierSuccess("-00");
  VerifyParseTimeZoneIdentifierSuccess("-23");
  VerifyParseTimeZoneIdentifierSuccess("\u221200");
  VerifyParseTimeZoneIdentifierSuccess("\u221223");
  //  Sign Hour : MinuteSecond
  VerifyParseTimeZoneIdentifierSuccess("+00:00");
  VerifyParseTimeZoneIdentifierSuccess("+23:59");
  VerifyParseTimeZoneIdentifierSuccess("-00:00");
  VerifyParseTimeZoneIdentifierSuccess("-23:59");
  VerifyParseTimeZoneIdentifierSuccess("\u221200:00");
  VerifyParseTimeZoneIdentifierSuccess("\u221223:59");
  //  Sign Hour MinuteSecond
  VerifyParseTimeZoneIdentifierSuccess("+0000");
  VerifyParseTimeZoneIdentifierSuccess("+2359");
  VerifyParseTimeZoneIdentifierSuccess("-0000");
  VerifyParseTimeZoneIdentifierSuccess("-2359");
  VerifyParseTimeZoneIdentifierSuccess("\u22120000");
  VerifyParseTimeZoneIdentifierSuccess("\u22122359");

  //  Sign Hour : MinuteSecond : MinuteSecond Fractionopt
  VerifyParseTimeZoneIdentifierSuccess("+00:00:00");
  VerifyParseTimeZoneIdentifierSuccess("+23:59:59");
  VerifyParseTimeZoneIdentifierSuccess("-00:00:00");
  VerifyParseTimeZoneIdentifierSuccess("-23:59:59");
  VerifyParseTimeZoneIdentifierSuccess("\u221200:00:00");
  VerifyParseTimeZoneIdentifierSuccess("\u221223:59:59");

  VerifyParseTimeZoneIdentifierSuccess("+00:00:00.0");
  VerifyParseTimeZoneIdentifierSuccess("+00:00:00,0");
  VerifyParseTimeZoneIdentifierSuccess("+00:00:00.10");
  VerifyParseTimeZoneIdentifierSuccess("+00:00:00,01");
  VerifyParseTimeZoneIdentifierSuccess("+00:00:00.012");
  VerifyParseTimeZoneIdentifierSuccess("+00:00:00,010");
  VerifyParseTimeZoneIdentifierSuccess("+00:00:00.0123");
  VerifyParseTimeZoneIdentifierSuccess("+00:00:00,0120");
  VerifyParseTimeZoneIdentifierSuccess("+00:00:00.01234");
  VerifyParseTimeZoneIdentifierSuccess("+00:00:00,01230");
  VerifyParseTimeZoneIdentifierSuccess("+00:00:00.012345");
  VerifyParseTimeZoneIdentifierSuccess("+00:00:00,012340");
  VerifyParseTimeZoneIdentifierSuccess("+00:00:00.0123450");
  VerifyParseTimeZoneIdentifierSuccess("+00:00:00,0123456");
  VerifyParseTimeZoneIdentifierSuccess("+00:00:00,01234567");
  VerifyParseTimeZoneIdentifierSuccess("+00:00:00.01234560");
  VerifyParseTimeZoneIdentifierSuccess("+00:00:00,012345678");
  VerifyParseTimeZoneIdentifierSuccess("+00:00:00.012345680");

  //  Sign Hour MinuteSecond MinuteSecond Fractionopt
  VerifyParseTimeZoneIdentifierSuccess("+000000");
  VerifyParseTimeZoneIdentifierSuccess("+235959");
  VerifyParseTimeZoneIdentifierSuccess("-000000");
  VerifyParseTimeZoneIdentifierSuccess("-235959");
  VerifyParseTimeZoneIdentifierSuccess("\u2212000000");
  VerifyParseTimeZoneIdentifierSuccess("\u2212235959");

  VerifyParseTimeZoneIdentifierSuccess("-000000.0");
  VerifyParseTimeZoneIdentifierSuccess("-000000,0");
  VerifyParseTimeZoneIdentifierSuccess("-000000.10");
  VerifyParseTimeZoneIdentifierSuccess("-000000,01");
  VerifyParseTimeZoneIdentifierSuccess("-000000.012");
  VerifyParseTimeZoneIdentifierSuccess("-000000,010");
  VerifyParseTimeZoneIdentifierSuccess("-000000.0123");
  VerifyParseTimeZoneIdentifierSuccess("-000000,0120");
  VerifyParseTimeZoneIdentifierSuccess("-000000.01234");
  VerifyParseTimeZoneIdentifierSuccess("-000000,01230");
  VerifyParseTimeZoneIdentifierSuccess("-000000.012345");
  VerifyParseTimeZoneIdentifierSuccess("-000000,012340");
  VerifyParseTimeZoneIdentifierSuccess("-000000.0123450");
  VerifyParseTimeZoneIdentifierSuccess("-000000,0123456");
  VerifyParseTimeZoneIdentifierSuccess("-000000,01234567");
  VerifyParseTimeZoneIdentifierSuccess("-000000.01234560");
  VerifyParseTimeZoneIdentifierSuccess("-000000,012345678");
  VerifyParseTimeZoneIdentifierSuccess("-000000.012345680");
}
TEST_F(TemporalParserTest, TimeZoneIdentifierIllegal) {
  //  Etc/GMT ASCIISign Hour:
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "[Etc/GMT+1]");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "Etc/GMT+01");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "Etc/GMT+24");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, ".");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "..");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "A/..");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "A/.");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "-ab");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "abcdefghijklmno");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "abcdefghijklmno/ABCDEFGHIJKLMN");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "abcdefghijklmn/ABCDEFGHIJKLMNO");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "1");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "a1");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "Etc/GMT1");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "GMT1");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "GMT+1");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "GMT-1");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "EDT5EST");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "CDT6CST");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "MDT7MST");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "PDT8PST");

  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "+2");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "+24");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "-24");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "\u221224");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "+0:60");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "+00:5");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "+00:60");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "-00:60");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "\u221200:60");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "+24:59");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "-24:59");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "\u221224:59");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "+0060");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "+00590");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "-0060");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "\u22120060");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "+2459");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "-2459");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "\u22122459");

  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "+00:0000");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "+0000:00");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "+23:0000");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "+2300:00");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "+00:5900");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "+0059:00");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "+00:0059");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "+0000:59");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "-00:0000");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "-0000:00");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "-23:0000");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "-2300:00");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "-00:5900");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "-0059:00");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "-00:0059");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "-0000:59");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "\u221200:0000");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "\u22120000:00");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "\u221223:0000");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "\u22122300:00");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "\u221200:5900");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "\u22120059:00");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "\u221200:0059");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "\u22120000:59");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "-00059");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "-0:0059");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "-00:059");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "-000:59");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "-0005:9");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "-0000000");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "-00000000");

  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "+240000");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "+006000");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "+000060");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "-240000");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "-006000");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "-000060");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "\u2212240000");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "\u2212006000");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "\u2212000060");

  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "+00:00:00.0000000000");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "-00:00:00.0000000000");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "\u221200:00:00.0000000000");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "+000000.0000000000");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "-000000.0000000000");
  VERIFY_PARSE_FAIL(TimeZoneIdentifier, "\u2212000000.0000000000");
}

TEST_F(TemporalParserTest, CalendarNameSuccess) {
  // CalendarName
  VerifyParseCalendarNameSuccess("chinese");
  VerifyParseCalendarNameSuccess("roc");
  VerifyParseCalendarNameSuccess("indian");
  VerifyParseCalendarNameSuccess("persian");
  VerifyParseCalendarNameSuccess("abcd-efghi");
  VerifyParseCalendarNameSuccess("abcd-efghi");
  VerifyParseCalendarNameSuccess("a2345678-b2345678-c2345678-d7654321");
}

TEST_F(TemporalParserTest, CalendarNameIllegal) {
  VERIFY_PARSE_FAIL(CalendarName, "20210304[u-ca=]");
  VERIFY_PARSE_FAIL(CalendarName, "20210304[u-ca=a]");
  VERIFY_PARSE_FAIL(CalendarName, "20210304[u-ca=ab]");
  VERIFY_PARSE_FAIL(CalendarName, "20210304[u-ca=abcdef-ab]");
  VERIFY_PARSE_FAIL(CalendarName, "20210304[u-ca=abcdefghijkl]");
  // It is a Syntax Error if DateExtendedYear is "-000000"
  VERIFY_PARSE_FAIL(CalendarName, "-0000000304[u-ca=abcdef-ab]");
  VERIFY_PARSE_FAIL(CalendarName, "\u22120000000304[u-ca=abcdef-ab]");
  // TemporalInstantString
  VERIFY_PARSE_FAIL(CalendarName, "2021-11-08z[ABCD]");
  // CalendarDateTime
  VERIFY_PARSE_FAIL(CalendarName, "2021-11-08[u-ca=chinese]");
  VERIFY_PARSE_FAIL(CalendarName, "2021-11-08[ABCDEFG][u-ca=chinese]");
  VERIFY_PARSE_FAIL(CalendarName, "2021-11-08[ABCDEFG/hijklmn][u-ca=roc]");
  // Time
  VERIFY_PARSE_FAIL(CalendarName, "23:45:59");
  // DateSpecYearMonth
  VERIFY_PARSE_FAIL(CalendarName, "2021-12");
  // DateSpecMonthDay
  VERIFY_PARSE_FAIL(CalendarName, "--12-31");
  VERIFY_PARSE_FAIL(CalendarName, "12-31");
  VERIFY_PARSE_FAIL(CalendarName, "--1231");
}

}  // namespace internal
}  // namespace v8
