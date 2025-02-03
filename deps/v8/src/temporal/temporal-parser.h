// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TEMPORAL_TEMPORAL_PARSER_H_
#define V8_TEMPORAL_TEMPORAL_PARSER_H_

#include <optional>

#include "src/execution/isolate.h"

namespace v8::internal {

/**
 * ParsedISO8601Result contains the parsed result of ISO 8601 grammar
 * documented in #sec-temporal-iso8601grammar
 * for TemporalInstantString, TemporalZonedDateTimeString,
 * CalendarName, TemporalDateString, TemporalDateTimeString,
 * TemporalMonthDayString, TemporalRelativeToString, TemporalTimeString,
 * TimeZoneIdentifier, and TemporalYearMonthString. For all the fields
 * represented by int32_t, a special value kMinInt31 is used to represent the
 * field is "undefined" after parsing.
 */
struct ParsedISO8601Result {
  int32_t date_year;    // DateYear production
  int32_t date_month;   // DateMonth production
  int32_t date_day;     // DateDay production
  int32_t time_hour;    // TimeHour production
  int32_t time_minute;  // TimeMinute production
  int32_t time_second;  // TimeSecond production
  int32_t
      time_nanosecond;  // TimeFractionalPart production stored in nanosecond
  int32_t tzuo_sign;    // TimeZoneUTCOffsetSign production
  int32_t tzuo_hour;    // TimeZoneUTCOffsetHour production
  int32_t tzuo_minute;  // TimeZoneUTCOffsetMinute production
  int32_t tzuo_second;  // TimeZoneUTCOffsetSecond production
  int32_t
      tzuo_nanosecond;  // TimeZoneUTCOffsetFractionalPart stored in nanosecond
  bool utc_designator;  // UTCDesignator is presented
  int32_t tzi_name_start;   // Starting offset of TimeZoneIANAName in the input
                            // string.
  int32_t tzi_name_length;  // Length of TimeZoneIANAName production
  int32_t calendar_name_start;  // Starting offset of CalendarName production in
                                // the input string.
  int32_t calendar_name_length;  // Length of CalendarName production.
  int32_t offset_string_start;   // Starting offset of TimeZoneNumericUTCOffset
                                 // in the input string.
  int32_t
      offset_string_length;  // Length of TimeZoneNumericUTCOffset production

  ParsedISO8601Result()
      : date_year(kMinInt31),
        date_month(kMinInt31),
        date_day(kMinInt31),
        time_hour(kMinInt31),
        time_minute(kMinInt31),
        time_second(kMinInt31),
        time_nanosecond(kMinInt31),
        tzuo_sign(kMinInt31),
        tzuo_hour(kMinInt31),
        tzuo_minute(kMinInt31),
        tzuo_second(kMinInt31),
        tzuo_nanosecond(kMinInt31),
        utc_designator(false),
        tzi_name_start(0),
        tzi_name_length(0),
        calendar_name_start(0),
        calendar_name_length(0),
        offset_string_start(0),
        offset_string_length(0) {}

  bool date_year_is_undefined() const { return date_year == kMinInt31; }
  bool date_month_is_undefined() const { return date_month == kMinInt31; }
  bool date_day_is_undefined() const { return date_day == kMinInt31; }
  bool time_hour_is_undefined() const { return time_hour == kMinInt31; }
  bool time_minute_is_undefined() const { return time_minute == kMinInt31; }
  bool time_second_is_undefined() const { return time_second == kMinInt31; }
  bool time_nanosecond_is_undefined() const {
    return time_nanosecond == kMinInt31;
  }
  bool tzuo_hour_is_undefined() const { return tzuo_hour == kMinInt31; }
  bool tzuo_minute_is_undefined() const { return tzuo_minute == kMinInt31; }
  bool tzuo_second_is_undefined() const { return tzuo_second == kMinInt31; }
  bool tzuo_sign_is_undefined() const { return tzuo_sign == kMinInt31; }
  bool tzuo_nanosecond_is_undefined() const {
    return tzuo_nanosecond == kMinInt31;
  }
};

/**
 * ParsedISO8601Duration contains the parsed result of ISO 8601 grammar
 * documented in #prod-TemporalDurationString
 * for TemporalDurationString.
 * A special value kEmpty is used to represent the
 * field is "undefined" after parsing for all fields except sign.
 */
struct ParsedISO8601Duration {
  double sign;               // Sign production
  double years;              // DurationYears production
  double months;             // DurationMonths production
  double weeks;              // DurationWeeks production
  double days;               // DurationDays production
  double whole_hours;        // DurationWholeHours production
  double whole_minutes;      // DurationWholeMinutes production
  double whole_seconds;      // DurationWholeSeconds production
  int32_t hours_fraction;    // DurationHoursFraction, in unit of 1e-9 hours
  int32_t minutes_fraction;  // DurationMinuteFraction, in unit of 1e-9 minutes
  int32_t seconds_fraction;  // DurationSecondFraction, in unit of nanosecond (
                             // 1e-9 seconds).

  static constexpr int32_t kEmpty = -1;
  ParsedISO8601Duration()
      : sign(1),
        years(kEmpty),
        months(kEmpty),
        weeks(kEmpty),
        days(kEmpty),
        whole_hours(kEmpty),
        whole_minutes(kEmpty),
        whole_seconds(kEmpty),
        hours_fraction(kEmpty),
        minutes_fraction(kEmpty),
        seconds_fraction(kEmpty) {}
};

/**
 * TemporalParser is low level parsing functions to support the implementation
 * of various ParseTemporal*String Abstract Operations listed after
 * #sec-temporal-parsetemporalinstantstring.
 * All the methods take an Isolate, a Handle<String> as input, and also a
 * pointer to a bool to answer the "satisfy the syntax of a Temporal*String"
 * question and return the parsed result.
 */
class V8_EXPORT_PRIVATE TemporalParser {
 public:
#define DEFINE_PARSE_METHOD(R, NAME)                         \
  V8_WARN_UNUSED_RESULT static std::optional<R> Parse##NAME( \
      Isolate* isolate, Handle<String> iso_string)
  DEFINE_PARSE_METHOD(ParsedISO8601Result, TemporalDateString);
  DEFINE_PARSE_METHOD(ParsedISO8601Result, TemporalDateTimeString);
  DEFINE_PARSE_METHOD(ParsedISO8601Result, TemporalTimeString);
  DEFINE_PARSE_METHOD(ParsedISO8601Result, TemporalYearMonthString);
  DEFINE_PARSE_METHOD(ParsedISO8601Result, TemporalMonthDayString);
  DEFINE_PARSE_METHOD(ParsedISO8601Result, TemporalInstantString);
  DEFINE_PARSE_METHOD(ParsedISO8601Result, TemporalZonedDateTimeString);
  DEFINE_PARSE_METHOD(ParsedISO8601Result, TimeZoneIdentifier);
  DEFINE_PARSE_METHOD(ParsedISO8601Result, TemporalRelativeToString);
  DEFINE_PARSE_METHOD(ParsedISO8601Result, CalendarName);
  DEFINE_PARSE_METHOD(ParsedISO8601Duration, TemporalDurationString);
  DEFINE_PARSE_METHOD(ParsedISO8601Result, TimeZoneNumericUTCOffset);
};
#undef DEFINE_PARSE_METHOD

}  // namespace v8::internal

#endif  // V8_TEMPORAL_TEMPORAL_PARSER_H_
