// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/js-temporal-objects.h"

#include <optional>
#include <set>

#include "src/common/globals.h"
#include "src/date/date.h"
#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/numbers/conversions-inl.h"
#include "src/objects/js-objects-inl.h"
#include "src/objects/js-objects.h"
#include "src/objects/js-temporal-objects-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/option-utils.h"
#include "src/objects/property-descriptor.h"
#include "src/objects/string-set.h"
#include "src/strings/string-builder-inl.h"
#include "src/temporal/temporal-parser.h"

#ifdef V8_INTL_SUPPORT
#include "src/objects/intl-objects.h"
#include "src/objects/js-date-time-format.h"
#include "src/objects/managed-inl.h"
#include "unicode/calendar.h"
#include "unicode/unistr.h"
#endif  // V8_INTL_SUPPORT

namespace v8::internal {

namespace {

enum class Unit {
  kNotPresent,
  kAuto,
  kYear,
  kMonth,
  kWeek,
  kDay,
  kHour,
  kMinute,
  kSecond,
  kMillisecond,
  kMicrosecond,
  kNanosecond
};

/**
 * This header declare the Abstract Operations defined in the
 * Temporal spec with the enum and struct for them.
 */

// Struct

// only for BalanceTime
struct UnbalancedTimeRecord {
  double hour;
  double minute;
  double second;
  double millisecond;
  double microsecond;
  double nanosecond;
};

using temporal::DateRecord;
using temporal::DateTimeRecord;
using temporal::TimeRecord;

struct DateRecordWithCalendar {
  DateRecord date;
  DirectHandle<Object> calendar;  // String or Undefined
};

struct TimeRecordWithCalendar {
  TimeRecord time;
  DirectHandle<Object> calendar;  // String or Undefined
};

struct TimeZoneRecord {
  bool z;
  DirectHandle<Object> offset_string;  // String or Undefined
  DirectHandle<Object> name;           // String or Undefined
};

struct DateTimeRecordWithCalendar {
  DateRecord date;
  TimeRecord time;
  TimeZoneRecord time_zone;
  DirectHandle<Object> calendar;  // String or Undefined
};

struct InstantRecord {
  DateRecord date;
  TimeRecord time;
  DirectHandle<Object> offset_string;  // String or Undefined
};

using temporal::DurationRecord;
using temporal::IsValidDuration;
using temporal::TimeDurationRecord;

struct DurationRecordWithRemainder {
  DurationRecord record;
  double remainder;
};

// #sec-temporal-date-duration-records
struct DateDurationRecord {
  double years;
  double months;
  double weeks;
  double days;
  // #sec-temporal-createdatedurationrecord
  static Maybe<DateDurationRecord> Create(Isolate* isolate, double years,
                                          double months, double weeks,
                                          double days);
};

// Options

V8_WARN_UNUSED_RESULT DirectHandle<String> UnitToString(Isolate* isolate,
                                                        Unit unit);

// #sec-temporal-totemporaldisambiguation
enum class Disambiguation { kCompatible, kEarlier, kLater, kReject };

// #sec-temporal-totemporaloverflow
enum class ShowOverflow { kConstrain, kReject };
// #sec-temporal-toshowcalendaroption
enum class ShowCalendar { kAuto, kAlways, kNever };

// #sec-temporal-toshowtimezonenameoption
enum class ShowTimeZone { kAuto, kNever };
Maybe<ShowTimeZone> ToShowTimeZoneNameOption(Isolate* isolate,
                                             DirectHandle<JSReceiver> options,
                                             const char* method_name) {
  // 1. Return ? GetOption(normalizedOptions, "timeZoneName", "string", «
  // "auto", "never" », "auto").
  return GetStringOption<ShowTimeZone>(
      isolate, options, "timeZoneName", method_name, {"auto", "never"},
      {ShowTimeZone::kAuto, ShowTimeZone::kNever}, ShowTimeZone::kAuto);
}

// #sec-temporal-toshowoffsetoption
enum class ShowOffset { kAuto, kNever };
Maybe<ShowOffset> ToShowOffsetOption(Isolate* isolate,
                                     DirectHandle<JSReceiver> options,
                                     const char* method_name) {
  // 1. Return ? GetOption(normalizedOptions, "offset", "string", « "auto",
  // "never" », "auto").
  return GetStringOption<ShowOffset>(
      isolate, options, "offset", method_name, {"auto", "never"},
      {ShowOffset::kAuto, ShowOffset::kNever}, ShowOffset::kAuto);
}

enum class Precision { k0, k1, k2, k3, k4, k5, k6, k7, k8, k9, kAuto, kMinute };

// Enum for add/subtract
enum class Arithmetic { kAdd, kSubtract };

// Enum for since/until
enum class TimePreposition { kSince, kUntil };

enum class Offset { kPrefer, kUse, kIgnore, kReject };
V8_WARN_UNUSED_RESULT Maybe<Offset> ToTemporalOffset(
    Isolate* isolate, DirectHandle<Object> options, Offset fallback,
    const char* method_name);

// sec-temporal-totemporalroundingmode
enum class RoundingMode {
  kCeil,
  kFloor,
  kExpand,
  kTrunc,
  kHalfCeil,
  kHalfFloor,
  kHalfExpand,
  kHalfTrunc,
  kHalfEven
};
// #table-temporal-unsigned-rounding-modes
enum class UnsignedRoundingMode {
  kInfinity,
  kZero,
  kHalfInfinity,
  kHalfZero,
  kHalfEven
};

enum class MatchBehaviour { kMatchExactly, kMatchMinutes };

// #sec-temporal-gettemporalunit
enum class UnitGroup {
  kDate,
  kTime,
  kDateTime,
};

struct DifferenceSettings {
  Unit smallest_unit;
  Unit largest_unit;
  RoundingMode rounding_mode;
  double rounding_increment;
  DirectHandle<JSReceiver> options;
};
enum class DisallowedUnitsInDifferenceSettings {
  kNone,
  kWeekAndDay,
};
Maybe<DifferenceSettings> GetDifferenceSettings(
    Isolate* isolate, TimePreposition operation, DirectHandle<Object> options,
    UnitGroup unit_group, DisallowedUnitsInDifferenceSettings disallowed_units,
    Unit fallback_smallest_unit, Unit smallest_largest_default_unit,
    const char* method_name);

// #sec-temporal-totemporaloffset
// ISO8601 String Parsing

// #sec-temporal-parsetemporalcalendarstring
V8_WARN_UNUSED_RESULT MaybeDirectHandle<String> ParseTemporalCalendarString(
    Isolate* isolate, DirectHandle<String> iso_string);

// #sec-temporal-parsetemporaldatetimestring
V8_WARN_UNUSED_RESULT Maybe<DateTimeRecordWithCalendar>
ParseTemporalDateTimeString(Isolate* isolate, DirectHandle<String> iso_string);

// #sec-temporal-parsetemporaldatestring
V8_WARN_UNUSED_RESULT Maybe<DateRecordWithCalendar> ParseTemporalDateString(
    Isolate* isolate, DirectHandle<String> iso_string);

// #sec-temporal-parsetemporaltimestring
Maybe<TimeRecordWithCalendar> ParseTemporalTimeString(
    Isolate* isolate, DirectHandle<String> iso_string);

// #sec-temporal-parsetemporaldurationstring
V8_WARN_UNUSED_RESULT Maybe<DurationRecord> ParseTemporalDurationString(
    Isolate* isolate, DirectHandle<String> iso_string);

// #sec-temporal-parsetemporaltimezonestring
V8_WARN_UNUSED_RESULT Maybe<TimeZoneRecord> ParseTemporalTimeZoneString(
    Isolate* isolate, DirectHandle<String> iso_string);

// #sec-temporal-parsetimezoneoffsetstring
V8_WARN_UNUSED_RESULT Maybe<int64_t> ParseTimeZoneOffsetString(
    Isolate* isolate, DirectHandle<String> offset_string);

// #sec-temporal-parsetemporalinstant
V8_WARN_UNUSED_RESULT MaybeDirectHandle<BigInt> ParseTemporalInstant(
    Isolate* isolate, DirectHandle<String> iso_string);
V8_WARN_UNUSED_RESULT MaybeDirectHandle<BigInt> ParseTemporalInstant(
    Isolate* isolate, DirectHandle<String> iso_string);

DateRecord BalanceISODate(Isolate* isolate, const DateRecord& date);

// Math and Misc

V8_WARN_UNUSED_RESULT MaybeDirectHandle<BigInt> AddInstant(
    Isolate* isolate, DirectHandle<BigInt> epoch_nanoseconds,
    const TimeDurationRecord& addend);

// #sec-temporal-balanceduration
V8_WARN_UNUSED_RESULT Maybe<TimeDurationRecord> BalanceDuration(
    Isolate* isolate, Unit largest_unit, DirectHandle<Object> relative_to,
    const TimeDurationRecord& duration, const char* method_name);
// The special case of BalanceDuration while the nanosecond is a large value
// and the rest are 0.
V8_WARN_UNUSED_RESULT Maybe<TimeDurationRecord> BalanceDuration(
    Isolate* isolate, Unit largest_unit, DirectHandle<BigInt> nanoseconds,
    const char* method_name);
// A special version of BalanceDuration which add two TimeDurationRecord
// internally as BigInt to avoid overflow double.
V8_WARN_UNUSED_RESULT Maybe<TimeDurationRecord> BalanceDuration(
    Isolate* isolate, Unit largest_unit, const TimeDurationRecord& dur1,
    const TimeDurationRecord& dur2, const char* method_name);

// sec-temporal-balancepossiblyinfiniteduration
enum BalanceOverflow {
  kNone,
  kPositive,
  kNegative,
};
struct BalancePossiblyInfiniteDurationResult {
  TimeDurationRecord value;
  BalanceOverflow overflow;
};
V8_WARN_UNUSED_RESULT Maybe<BalancePossiblyInfiniteDurationResult>
BalancePossiblyInfiniteDuration(Isolate* isolate, Unit largest_unit,
                                DirectHandle<Object> relative_to,
                                const TimeDurationRecord& duration,
                                const char* method_name);

// The special case of BalancePossiblyInfiniteDuration while the nanosecond is a
// large value and days contains non-zero values but the rest are 0.
// This version has no relative_to.
V8_WARN_UNUSED_RESULT Maybe<BalancePossiblyInfiniteDurationResult>
BalancePossiblyInfiniteDuration(Isolate* isolate, Unit largest_unit,
                                DirectHandle<Object> relative_to, double days,
                                DirectHandle<BigInt> nanoseconds,
                                const char* method_name);
V8_WARN_UNUSED_RESULT Maybe<BalancePossiblyInfiniteDurationResult>
BalancePossiblyInfiniteDuration(Isolate* isolate, Unit largest_unit,
                                double days, DirectHandle<BigInt> nanoseconds,
                                const char* method_name) {
  return BalancePossiblyInfiniteDuration(isolate, largest_unit,
                                         isolate->factory()->undefined_value(),
                                         days, nanoseconds, method_name);
}

V8_WARN_UNUSED_RESULT Maybe<DurationRecord> DifferenceISODateTime(
    Isolate* isolate, const DateTimeRecord& date_time1,
    const DateTimeRecord& date_time2, DirectHandle<JSReceiver> calendar,
    Unit largest_unit, DirectHandle<JSReceiver> relative_to,
    const char* method_name);

// #sec-temporal-adddatetime
V8_WARN_UNUSED_RESULT Maybe<DateTimeRecord> AddDateTime(
    Isolate* isolate, const DateTimeRecord& date_time,
    DirectHandle<JSReceiver> calendar, const DurationRecord& addend,
    DirectHandle<Object> options);

// #sec-temporal-addzoneddatetime
V8_WARN_UNUSED_RESULT MaybeDirectHandle<BigInt> AddZonedDateTime(
    Isolate* isolate, DirectHandle<BigInt> eopch_nanoseconds,
    DirectHandle<JSReceiver> time_zone, DirectHandle<JSReceiver> calendar,
    const DurationRecord& addend, const char* method_name);

V8_WARN_UNUSED_RESULT MaybeDirectHandle<BigInt> AddZonedDateTime(
    Isolate* isolate, DirectHandle<BigInt> eopch_nanoseconds,
    DirectHandle<JSReceiver> time_zone, DirectHandle<JSReceiver> calendar,
    const DurationRecord& addend, DirectHandle<Object> options,
    const char* method_name);

// #sec-temporal-isvalidepochnanoseconds
bool IsValidEpochNanoseconds(Isolate* isolate,
                             DirectHandle<BigInt> epoch_nanoseconds);

struct NanosecondsToDaysResult {
  double days;
  double nanoseconds;
  int64_t day_length;
};

// #sec-temporal-nanosecondstodays
V8_WARN_UNUSED_RESULT Maybe<NanosecondsToDaysResult> NanosecondsToDays(
    Isolate* isolate, DirectHandle<BigInt> nanoseconds,
    DirectHandle<Object> relative_to_obj, const char* method_name);

// #sec-temporal-interpretisodatetimeoffset
enum class OffsetBehaviour { kOption, kExact, kWall };

// sec-temporal-totemporalroundingmode
Maybe<RoundingMode> ToTemporalRoundingMode(Isolate* isolate,
                                           DirectHandle<JSReceiver> options,
                                           RoundingMode fallback,
                                           const char* method_name) {
  // 1. Return ? GetOption(normalizedOptions, "roundingMode", "string", «
  // "ceil", "floor", "expand", "trunc", "halfCeil", "halfFloor", "halfExpand",
  // "halfTrunc", "halfEven" », fallback).

  return GetStringOption<RoundingMode>(
      isolate, options, "roundingMode", method_name,
      {"ceil", "floor", "expand", "trunc", "halfCeil", "halfFloor",
       "halfExpand", "halfTrunc", "halfEven"},
      {RoundingMode::kCeil, RoundingMode::kFloor, RoundingMode::kExpand,
       RoundingMode::kTrunc, RoundingMode::kHalfCeil, RoundingMode::kHalfFloor,
       RoundingMode::kHalfExpand, RoundingMode::kHalfTrunc,
       RoundingMode::kHalfEven},
      fallback);
}

V8_WARN_UNUSED_RESULT
DirectHandle<BigInt> GetEpochFromISOParts(Isolate* isolate,
                                          const DateTimeRecord& date_time);

// #sec-temporal-isodaysinmonth
int32_t ISODaysInMonth(Isolate* isolate, int32_t year, int32_t month);

// #sec-temporal-isodaysinyear
int32_t ISODaysInYear(Isolate* isolate, int32_t year);

bool IsValidTime(Isolate* isolate, const TimeRecord& time);

// #sec-temporal-isvalidisodate
bool IsValidISODate(Isolate* isolate, const DateRecord& date);

// #sec-temporal-compareisodate
int32_t CompareISODate(const DateRecord& date1, const DateRecord& date2);

// #sec-temporal-balanceisoyearmonth
void BalanceISOYearMonth(Isolate* isolate, int32_t* year, int32_t* month);

// #sec-temporal-balancetime
V8_WARN_UNUSED_RESULT DateTimeRecord
BalanceTime(const UnbalancedTimeRecord& time);

// #sec-temporal-differencetime
V8_WARN_UNUSED_RESULT Maybe<TimeDurationRecord> DifferenceTime(
    Isolate* isolate, const TimeRecord& time1, const TimeRecord& time2);

// #sec-temporal-addtime
V8_WARN_UNUSED_RESULT DateTimeRecord AddTime(Isolate* isolate,
                                             const TimeRecord& time,
                                             const TimeDurationRecord& addend);

// #sec-temporal-totaldurationnanoseconds
DirectHandle<BigInt> TotalDurationNanoseconds(
    Isolate* isolate, const TimeDurationRecord& duration, double offset_shift);

// #sec-temporal-totemporaltimerecord
Maybe<TimeRecord> ToTemporalTimeRecord(
    Isolate* isolate, DirectHandle<JSReceiver> temporal_time_like,
    const char* method_name);
// Calendar Operations

// #sec-temporal-calendardateadd
V8_WARN_UNUSED_RESULT MaybeDirectHandle<JSTemporalPlainDate> CalendarDateAdd(
    Isolate* isolate, DirectHandle<JSReceiver> calendar,
    DirectHandle<Object> date, DirectHandle<Object> durations,
    DirectHandle<Object> options, DirectHandle<Object> date_add);
V8_WARN_UNUSED_RESULT MaybeDirectHandle<JSTemporalPlainDate> CalendarDateAdd(
    Isolate* isolate, DirectHandle<JSReceiver> calendar,
    DirectHandle<Object> date, DirectHandle<Object> durations,
    DirectHandle<Object> options);
V8_WARN_UNUSED_RESULT MaybeDirectHandle<JSTemporalPlainDate> CalendarDateAdd(
    Isolate* isolate, DirectHandle<JSReceiver> calendar,
    DirectHandle<Object> date, DirectHandle<Object> durations);

// #sec-temporal-calendardateuntil
V8_WARN_UNUSED_RESULT MaybeDirectHandle<JSTemporalDuration> CalendarDateUntil(
    Isolate* isolate, DirectHandle<JSReceiver> calendar,
    DirectHandle<Object> one, DirectHandle<Object> two,
    DirectHandle<Object> options, DirectHandle<Object> date_until);

// #sec-temporal-calendarfields
MaybeDirectHandle<FixedArray> CalendarFields(
    Isolate* isolate, DirectHandle<JSReceiver> calendar,
    DirectHandle<FixedArray> field_names);

// #sec-temporal-getoffsetnanosecondsfor
V8_WARN_UNUSED_RESULT Maybe<int64_t> GetOffsetNanosecondsFor(
    Isolate* isolate, DirectHandle<JSReceiver> time_zone,
    DirectHandle<Object> instant, const char* method_name);

// #sec-temporal-totemporalcalendarwithisodefault
MaybeDirectHandle<JSReceiver> ToTemporalCalendarWithISODefault(
    Isolate* isolate, DirectHandle<Object> temporal_calendar_like,
    const char* method_name);

// #sec-temporal-isbuiltincalendar
bool IsBuiltinCalendar(Isolate* isolate, DirectHandle<String> id);

// Internal Helper Function
int32_t CalendarIndex(Isolate* isolate, DirectHandle<String> id);

// #sec-isvalidtimezonename
bool IsValidTimeZoneName(Isolate* isolate, DirectHandle<String> time_zone);

// #sec-canonicalizetimezonename
DirectHandle<String> CanonicalizeTimeZoneName(Isolate* isolate,
                                              DirectHandle<String> identifier);

// #sec-temporal-tointegerthrowoninfinity
MaybeDirectHandle<Number> ToIntegerThrowOnInfinity(
    Isolate* isolate, DirectHandle<Object> argument);

// #sec-temporal-topositiveinteger
MaybeDirectHandle<Number> ToPositiveInteger(Isolate* isolate,
                                            DirectHandle<Object> argument);

inline double modulo(double a, int32_t b) { return a - std::floor(a / b) * b; }

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define AT __FILE__ ":" TOSTRING(__LINE__)

#ifdef DEBUG
#define TEMPORAL_DEBUG_INFO AT
#define TEMPORAL_ENTER_FUNC()
// #define TEMPORAL_ENTER_FUNC()  do { PrintF("Start: %s\n", __func__); } while
// (false)
#else
// #define TEMPORAL_DEBUG_INFO ""
#define TEMPORAL_DEBUG_INFO AT
#define TEMPORAL_ENTER_FUNC()
// #define TEMPORAL_ENTER_FUNC()  do { PrintF("Start: %s\n", __func__); } while
// (false)
#endif  // DEBUG

#define NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR()       \
  NewTypeError(                                     \
      MessageTemplate::kInvalidArgumentForTemporal, \
      isolate->factory()->NewStringFromStaticChars(TEMPORAL_DEBUG_INFO))

#define NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR()       \
  NewRangeError(                                     \
      MessageTemplate::kInvalidTimeValueForTemporal, \
      isolate->factory()->NewStringFromStaticChars(TEMPORAL_DEBUG_INFO))

// #sec-defaulttimezone
#ifdef V8_INTL_SUPPORT
DirectHandle<String> DefaultTimeZone(Isolate* isolate) {
  TEMPORAL_ENTER_FUNC();
  return Intl::DefaultTimeZone(isolate);
}
#else   //  V8_INTL_SUPPORT
DirectHandle<String> DefaultTimeZone(Isolate* isolate) {
  TEMPORAL_ENTER_FUNC();
  return isolate->factory()->UTC_string();
}
#endif  //  V8_INTL_SUPPORT

// #sec-temporal-isodatetimewithinlimits
bool ISODateTimeWithinLimits(Isolate* isolate,
                             const DateTimeRecord& date_time) {
  TEMPORAL_ENTER_FUNC();
  /**
   * Note: It is really overkill to decide within the limit by following the
   * specified algorithm literally, which require the conversion to BigInt.
   * Take a short cut and use pre-calculated year/month/day boundary instead.
   *
   * Math:
   * (-8.64 x 10^21- 8.64 x 10^13,  8.64 x 10^21 + 8.64 x 10^13) ns
   * = (-8.64 x 100000001 x 10^13,  8.64 x 100000001 x 10^13) ns
   * = (-8.64 x 100000001 x 10^10,  8.64 x 100000001 x 10^10) microsecond
   * = (-8.64 x 100000001 x 10^7,  8.64 x 100000001 x 10^7) millisecond
   * = (-8.64 x 100000001 x 10^4,  8.64 x 100000001 x 10^4) second
   * = (-86400 x 100000001 ,  86400 x 100000001 ) second
   * = (-100000001,  100000001) days => Because 60*60*24 = 86400
   * 100000001 days is about 273790 years, 11 months and 4 days.
   * Therefore 100000001 days before Jan 1 1970 is around Apr 19, -271821 and
   * 100000001 days after Jan 1 1970 is around Sept 13, 275760.
   */
  if (date_time.date.year > -271821 && date_time.date.year < 275760)
    return true;
  if (date_time.date.year < -271821 || date_time.date.year > 275760)
    return false;
  if (date_time.date.year == -271821) {
    if (date_time.date.month > 4) return true;
    if (date_time.date.month < 4) return false;
    if (date_time.date.day > 19) return true;
    if (date_time.date.day < 19) return false;
    if (date_time.time.hour > 0) return true;
    if (date_time.time.minute > 0) return true;
    if (date_time.time.second > 0) return true;
    if (date_time.time.millisecond > 0) return true;
    if (date_time.time.microsecond > 0) return true;
    return date_time.time.nanosecond > 0;
  } else {
    DCHECK_EQ(date_time.date.year, 275760);
    if (date_time.date.month > 9) return false;
    if (date_time.date.month < 9) return true;
    return date_time.date.day < 14;
  }
  // 1. Assert: year, month, day, hour, minute, second, millisecond,
  // microsecond, and nanosecond are integers.
  // 2. Let ns be ! GetEpochFromISOParts(year, month, day, hour, minute,
  // second, millisecond, microsecond, nanosecond).
  // 3. If ns ≤ -8.64 × 10^21 - 8.64 × 10^13, then
  // 4. If ns ≥ 8.64 × 10^21 + 8.64 × 10^13, then
  // 5. Return true.
}

// #sec-temporal-isoyearmonthwithinlimits
bool ISOYearMonthWithinLimits(int32_t year, int32_t month) {
  TEMPORAL_ENTER_FUNC();
  // 1. Assert: year and month are integers.
  // 2. If year < −271821 or year > 275760, then
  // a. Return false.
  if (year < -271821 || year > 275760) return false;
  // 3. If year is −271821 and month < 4, then
  // a. Return false.
  if (year == -271821 && month < 4) return false;
  // 4. If year is 275760 and month > 9, then
  // a. Return false.
  if (year == 275760 && month > 9) return false;
  // 5. Return true.
  return true;
}

#define ORDINARY_CREATE_FROM_CONSTRUCTOR(obj, target, new_target, T)           \
  DirectHandle<JSReceiver> new_target_receiver = Cast<JSReceiver>(new_target); \
  DirectHandle<Map> map;                                                       \
  ASSIGN_RETURN_ON_EXCEPTION(                                                  \
      isolate, map,                                                            \
      JSFunction::GetDerivedMap(isolate, target, new_target_receiver));        \
  DirectHandle<T> object =                                                     \
      Cast<T>(isolate->factory()->NewFastOrSlowJSObjectFromMap(map));

#define THROW_INVALID_RANGE(T) \
  THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());

#define CONSTRUCTOR(name)                                                      \
  DirectHandle<JSFunction>(                                                    \
      Cast<JSFunction>(                                                        \
          isolate->context()->native_context()->temporal_##name##_function()), \
      isolate)

// #sec-temporal-systemutcepochnanoseconds
DirectHandle<BigInt> SystemUTCEpochNanoseconds(Isolate* isolate) {
  TEMPORAL_ENTER_FUNC();
  // 1. Let ns be the approximate current UTC date and time, in nanoseconds
  // since the epoch.
  double ms =
      V8::GetCurrentPlatform()->CurrentClockTimeMillisecondsHighResolution();
  // 2. Set ns to the result of clamping ns between −8.64 × 10^21 and 8.64 ×
  // 10^21.

  // 3. Return ℤ(ns).
  double ns = ms * 1000000.0;
  ns = std::floor(std::max(-8.64e21, std::min(ns, 8.64e21)));
  return BigInt::FromNumber(isolate, isolate->factory()->NewNumber(ns))
      .ToHandleChecked();
}

// #sec-temporal-createtemporalcalendar
MaybeDirectHandle<JSTemporalCalendar> CreateTemporalCalendar(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, DirectHandle<String> identifier) {
  TEMPORAL_ENTER_FUNC();
  // 1. Assert: ! IsBuiltinCalendar(identifier) is true.
  // 2. If newTarget is not provided, set newTarget to %Temporal.Calendar%.
  // 3. Let object be ? OrdinaryCreateFromConstructor(newTarget,
  // "%Temporal.Calendar.prototype%", « [[InitializedTemporalCalendar]],
  // [[Identifier]] »).
  int32_t index = CalendarIndex(isolate, identifier);

  ORDINARY_CREATE_FROM_CONSTRUCTOR(object, target, new_target,
                                   JSTemporalCalendar)

  object->set_flags(0);
  // 4. Set object.[[Identifier]] to identifier.
  object->set_calendar_index(index);
  // 5. Return object.
  return object;
}

MaybeDirectHandle<JSTemporalCalendar> CreateTemporalCalendar(
    Isolate* isolate, DirectHandle<String> identifier) {
  TEMPORAL_ENTER_FUNC();
  return CreateTemporalCalendar(isolate, CONSTRUCTOR(calendar),
                                CONSTRUCTOR(calendar), identifier);
}

// #sec-temporal-createtemporaldate
MaybeDirectHandle<JSTemporalPlainDate> CreateTemporalDate(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, const DateRecord& date,
    DirectHandle<JSReceiver> calendar) {
  TEMPORAL_ENTER_FUNC();
  // 1. Assert: isoYear is an integer.
  // 2. Assert: isoMonth is an integer.
  // 3. Assert: isoDay is an integer.
  // 4. Assert: Type(calendar) is Object.
  // 5. If ! IsValidISODate(isoYear, isoMonth, isoDay) is false, throw a
  // RangeError exception.
  if (!IsValidISODate(isolate, date)) {
    THROW_INVALID_RANGE(JSTemporalPlainDate);
  }
  // 6. If ! ISODateTimeWithinLimits(isoYear, isoMonth, isoDay, 12, 0, 0, 0, 0,
  // 0) is false, throw a RangeError exception.
  if (!ISODateTimeWithinLimits(isolate, {date, {12, 0, 0, 0, 0, 0}})) {
    THROW_INVALID_RANGE(JSTemporalPlainDate);
  }
  // 7. If newTarget is not present, set it to %Temporal.PlainDate%.

  // 8. Let object be ? OrdinaryCreateFromConstructor(newTarget,
  // "%Temporal.PlainDate.prototype%", « [[InitializedTemporalDate]],
  // [[ISOYear]], [[ISOMonth]], [[ISODay]], [[Calendar]] »).
  ORDINARY_CREATE_FROM_CONSTRUCTOR(object, target, new_target,
                                   JSTemporalPlainDate)
  object->set_year_month_day(0);
  // 9. Set object.[[ISOYear]] to isoYear.
  object->set_iso_year(date.year);
  // 10. Set object.[[ISOMonth]] to isoMonth.
  object->set_iso_month(date.month);
  // 11. Set object.[[ISODay]] to isoDay.
  object->set_iso_day(date.day);
  // 12. Set object.[[Calendar]] to calendar.
  object->set_calendar(*calendar);
  // 13. Return object.
  return object;
}

MaybeDirectHandle<JSTemporalPlainDate> CreateTemporalDate(
    Isolate* isolate, const DateRecord& date,
    DirectHandle<JSReceiver> calendar) {
  TEMPORAL_ENTER_FUNC();
  return CreateTemporalDate(isolate, CONSTRUCTOR(plain_date),
                            CONSTRUCTOR(plain_date), date, calendar);
}

// #sec-temporal-createtemporaldatetime
MaybeDirectHandle<JSTemporalPlainDateTime> CreateTemporalDateTime(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, const DateTimeRecord& date_time,
    DirectHandle<JSReceiver> calendar) {
  TEMPORAL_ENTER_FUNC();
  // 1. Assert: isoYear, isoMonth, isoDay, hour, minute, second, millisecond,
  // microsecond, and nanosecond are integers.
  // 2. Assert: Type(calendar) is Object.
  // 3. If ! IsValidISODate(isoYear, isoMonth, isoDay) is false, throw a
  // RangeError exception.
  if (!IsValidISODate(isolate, date_time.date)) {
    THROW_INVALID_RANGE(JSTemporalPlainDateTime);
  }
  // 4. If ! IsValidTime(hour, minute, second, millisecond, microsecond,
  // nanosecond) is false, throw a RangeError exception.
  if (!IsValidTime(isolate, date_time.time)) {
    THROW_INVALID_RANGE(JSTemporalPlainDateTime);
  }
  // 5. If ! ISODateTimeWithinLimits(isoYear, isoMonth, isoDay, hour, minute,
  // second, millisecond, microsecond, nanosecond) is false, then
  if (!ISODateTimeWithinLimits(isolate, date_time)) {
    // a. Throw a RangeError exception.
    THROW_INVALID_RANGE(JSTemporalPlainDateTime);
  }
  // 6. If newTarget is not present, set it to %Temporal.PlainDateTime%.
  // 7. Let object be ? OrdinaryCreateFromConstructor(newTarget,
  // "%Temporal.PlainDateTime.prototype%", « [[InitializedTemporalDateTime]],
  // [[ISOYear]], [[ISOMonth]], [[ISODay]], [[ISOHour]], [[ISOMinute]],
  // [[ISOSecond]], [[ISOMillisecond]], [[ISOMicrosecond]], [[ISONanosecond]],
  // [[Calendar]] »).
  ORDINARY_CREATE_FROM_CONSTRUCTOR(object, target, new_target,
                                   JSTemporalPlainDateTime)

  object->set_year_month_day(0);
  object->set_hour_minute_second(0);
  object->set_second_parts(0);
  // 8. Set object.[[ISOYear]] to isoYear.
  object->set_iso_year(date_time.date.year);
  // 9. Set object.[[ISOMonth]] to isoMonth.
  object->set_iso_month(date_time.date.month);
  // 10. Set object.[[ISODay]] to isoDay.
  object->set_iso_day(date_time.date.day);
  // 11. Set object.[[ISOHour]] to hour.
  object->set_iso_hour(date_time.time.hour);
  // 12. Set object.[[ISOMinute]] to minute.
  object->set_iso_minute(date_time.time.minute);
  // 13. Set object.[[ISOSecond]] to second.
  object->set_iso_second(date_time.time.second);
  // 14. Set object.[[ISOMillisecond]] to millisecond.
  object->set_iso_millisecond(date_time.time.millisecond);
  // 15. Set object.[[ISOMicrosecond]] to microsecond.
  object->set_iso_microsecond(date_time.time.microsecond);
  // 16. Set object.[[ISONanosecond]] to nanosecond.
  object->set_iso_nanosecond(date_time.time.nanosecond);
  // 17. Set object.[[Calendar]] to calendar.
  object->set_calendar(*calendar);
  // 18. Return object.
  return object;
}

MaybeDirectHandle<JSTemporalPlainDateTime> CreateTemporalDateTimeDefaultTarget(
    Isolate* isolate, const DateTimeRecord& date_time,
    DirectHandle<JSReceiver> calendar) {
  TEMPORAL_ENTER_FUNC();
  return CreateTemporalDateTime(isolate, CONSTRUCTOR(plain_date_time),
                                CONSTRUCTOR(plain_date_time), date_time,
                                calendar);
}

}  // namespace

namespace temporal {

MaybeDirectHandle<JSTemporalPlainDateTime> CreateTemporalDateTime(
    Isolate* isolate, const DateTimeRecord& date_time,
    DirectHandle<JSReceiver> calendar) {
  return CreateTemporalDateTimeDefaultTarget(isolate, date_time, calendar);
}

}  // namespace temporal

namespace {
// #sec-temporal-createtemporaltime
MaybeDirectHandle<JSTemporalPlainTime> CreateTemporalTime(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, const TimeRecord& time) {
  TEMPORAL_ENTER_FUNC();
  // 2. If ! IsValidTime(hour, minute, second, millisecond, microsecond,
  // nanosecond) is false, throw a RangeError exception.
  if (!IsValidTime(isolate, time)) {
    THROW_INVALID_RANGE(JSTemporalPlainTime);
  }

  DirectHandle<JSTemporalCalendar> calendar =
      temporal::GetISO8601Calendar(isolate);

  // 4. Let object be ? OrdinaryCreateFromConstructor(newTarget,
  // "%Temporal.PlainTime.prototype%", « [[InitializedTemporalTime]],
  // [[ISOHour]], [[ISOMinute]], [[ISOSecond]], [[ISOMillisecond]],
  // [[ISOMicrosecond]], [[ISONanosecond]], [[Calendar]] »).
  ORDINARY_CREATE_FROM_CONSTRUCTOR(object, target, new_target,
                                   JSTemporalPlainTime)
  object->set_hour_minute_second(0);
  object->set_second_parts(0);
  // 5. Set object.[[ISOHour]] to hour.
  object->set_iso_hour(time.hour);
  // 6. Set object.[[ISOMinute]] to minute.
  object->set_iso_minute(time.minute);
  // 7. Set object.[[ISOSecond]] to second.
  object->set_iso_second(time.second);
  // 8. Set object.[[ISOMillisecond]] to millisecond.
  object->set_iso_millisecond(time.millisecond);
  // 9. Set object.[[ISOMicrosecond]] to microsecond.
  object->set_iso_microsecond(time.microsecond);
  // 10. Set object.[[ISONanosecond]] to nanosecond.
  object->set_iso_nanosecond(time.nanosecond);
  // 11. Set object.[[Calendar]] to ? GetISO8601Calendar().
  object->set_calendar(*calendar);

  // 12. Return object.
  return object;
}

MaybeDirectHandle<JSTemporalPlainTime> CreateTemporalTime(
    Isolate* isolate, const TimeRecord& time) {
  TEMPORAL_ENTER_FUNC();
  return CreateTemporalTime(isolate, CONSTRUCTOR(plain_time),
                            CONSTRUCTOR(plain_time), time);
}

// #sec-temporal-createtemporalmonthday
MaybeDirectHandle<JSTemporalPlainMonthDay> CreateTemporalMonthDay(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, int32_t iso_month, int32_t iso_day,
    DirectHandle<JSReceiver> calendar, int32_t reference_iso_year) {
  TEMPORAL_ENTER_FUNC();
  // 1. Assert: isoMonth, isoDay, and referenceISOYear are integers.
  // 2. Assert: Type(calendar) is Object.
  // 3. If ! IsValidISODate(referenceISOYear, isoMonth, isoDay) is false, throw
  if (!IsValidISODate(isolate, {reference_iso_year, iso_month, iso_day})) {
    // a RangeError exception.
    THROW_INVALID_RANGE(JSTemporalPlainMonthDay);
  }
  // 4. If ISODateTimeWithinLimits(referenceISOYear, isoMonth, isoDay, 12, 0, 0,
  // 0, 0, 0) is false, throw a RangeError exception.
  if (!ISODateTimeWithinLimits(
          isolate,
          {{reference_iso_year, iso_month, iso_day}, {12, 0, 0, 0, 0, 0}})) {
    THROW_INVALID_RANGE(JSTemporalPlainMonthDay);
  }

  // 5. If newTarget is not present, set it to %Temporal.PlainMonthDay%.
  // 6. Let object be ? OrdinaryCreateFromConstructor(newTarget,
  // "%Temporal.PlainMonthDay.prototype%", « [[InitializedTemporalMonthDay]],
  // [[ISOMonth]], [[ISODay]], [[ISOYear]], [[Calendar]] »).
  ORDINARY_CREATE_FROM_CONSTRUCTOR(object, target, new_target,
                                   JSTemporalPlainMonthDay)
  object->set_year_month_day(0);
  // 7. Set object.[[ISOMonth]] to isoMonth.
  object->set_iso_month(iso_month);
  // 8. Set object.[[ISODay]] to isoDay.
  object->set_iso_day(iso_day);
  // 9. Set object.[[Calendar]] to calendar.
  object->set_calendar(*calendar);
  // 10. Set object.[[ISOYear]] to referenceISOYear.
  object->set_iso_year(reference_iso_year);
  // 11. Return object.
  return object;
}

MaybeDirectHandle<JSTemporalPlainMonthDay> CreateTemporalMonthDay(
    Isolate* isolate, int32_t iso_month, int32_t iso_day,
    DirectHandle<JSReceiver> calendar, int32_t reference_iso_year) {
  return CreateTemporalMonthDay(isolate, CONSTRUCTOR(plain_month_day),
                                CONSTRUCTOR(plain_month_day), iso_month,
                                iso_day, calendar, reference_iso_year);
}

// #sec-temporal-createtemporalyearmonth
MaybeDirectHandle<JSTemporalPlainYearMonth> CreateTemporalYearMonth(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, int32_t iso_year, int32_t iso_month,
    DirectHandle<JSReceiver> calendar, int32_t reference_iso_day) {
  TEMPORAL_ENTER_FUNC();
  // 1. Assert: isoYear, isoMonth, and referenceISODay are integers.
  // 2. Assert: Type(calendar) is Object.
  // 3. If ! IsValidISODate(isoYear, isoMonth, referenceISODay) is false, throw
  // a RangeError exception.
  if (!IsValidISODate(isolate, {iso_year, iso_month, reference_iso_day})) {
    THROW_INVALID_RANGE(JSTemporalPlainYearMonth);
  }
  // 4. If ! ISOYearMonthWithinLimits(isoYear, isoMonth) is false, throw a
  // RangeError exception.
  if (!ISOYearMonthWithinLimits(iso_year, iso_month)) {
    THROW_INVALID_RANGE(JSTemporalPlainYearMonth);
  }
  // 5. If newTarget is not present, set it to %Temporal.PlainYearMonth%.
  // 6. Let object be ? OrdinaryCreateFromConstructor(newTarget,
  // "%Temporal.PlainYearMonth.prototype%", « [[InitializedTemporalYearMonth]],
  // [[ISOYear]], [[ISOMonth]], [[ISODay]], [[Calendar]] »).
  ORDINARY_CREATE_FROM_CONSTRUCTOR(object, target, new_target,
                                   JSTemporalPlainYearMonth)
  object->set_year_month_day(0);
  // 7. Set object.[[ISOYear]] to isoYear.
  object->set_iso_year(iso_year);
  // 8. Set object.[[ISOMonth]] to isoMonth.
  object->set_iso_month(iso_month);
  // 9. Set object.[[Calendar]] to calendar.
  object->set_calendar(*calendar);
  // 10. Set object.[[ISODay]] to referenceISODay.
  object->set_iso_day(reference_iso_day);
  // 11. Return object.
  return object;
}

MaybeDirectHandle<JSTemporalPlainYearMonth> CreateTemporalYearMonth(
    Isolate* isolate, int32_t iso_year, int32_t iso_month,
    DirectHandle<JSReceiver> calendar, int32_t reference_iso_day) {
  TEMPORAL_ENTER_FUNC();
  return CreateTemporalYearMonth(isolate, CONSTRUCTOR(plain_year_month),
                                 CONSTRUCTOR(plain_year_month), iso_year,
                                 iso_month, calendar, reference_iso_day);
}

// #sec-temporal-createtemporalzoneddatetime
MaybeDirectHandle<JSTemporalZonedDateTime> CreateTemporalZonedDateTime(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, DirectHandle<BigInt> epoch_nanoseconds,
    DirectHandle<JSReceiver> time_zone, DirectHandle<JSReceiver> calendar) {
  TEMPORAL_ENTER_FUNC();
  // 1. Assert: Type(epochNanoseconds) is BigInt.
  // 2. Assert: ! IsValidEpochNanoseconds(epochNanoseconds) is true.
  DCHECK(IsValidEpochNanoseconds(isolate, epoch_nanoseconds));
  // 3. Assert: Type(timeZone) is Object.
  // 4. Assert: Type(calendar) is Object.
  // 5. If newTarget is not present, set it to %Temporal.ZonedDateTime%.
  // 6. Let object be ? OrdinaryCreateFromConstructor(newTarget,
  // "%Temporal.ZonedDateTime.prototype%", «
  // [[InitializedTemporalZonedDateTime]], [[Nanoseconds]], [[TimeZone]],
  // [[Calendar]] »).
  ORDINARY_CREATE_FROM_CONSTRUCTOR(object, target, new_target,
                                   JSTemporalZonedDateTime)
  // 7. Set object.[[Nanoseconds]] to epochNanoseconds.
  object->set_nanoseconds(*epoch_nanoseconds);
  // 8. Set object.[[TimeZone]] to timeZone.
  object->set_time_zone(*time_zone);
  // 9. Set object.[[Calendar]] to calendar.
  object->set_calendar(*calendar);
  // 10. Return object.
  return object;
}

MaybeDirectHandle<JSTemporalZonedDateTime> CreateTemporalZonedDateTime(
    Isolate* isolate, DirectHandle<BigInt> epoch_nanoseconds,
    DirectHandle<JSReceiver> time_zone, DirectHandle<JSReceiver> calendar) {
  TEMPORAL_ENTER_FUNC();
  return CreateTemporalZonedDateTime(isolate, CONSTRUCTOR(zoned_date_time),
                                     CONSTRUCTOR(zoned_date_time),
                                     epoch_nanoseconds, time_zone, calendar);
}

inline double NormalizeMinusZero(double v) { return IsMinusZero(v) ? 0 : v; }

// #sec-temporal-createdatedurationrecord
Maybe<DateDurationRecord> DateDurationRecord::Create(
    Isolate* isolate, double years, double months, double weeks, double days) {
  // 1. If ! IsValidDuration(years, months, weeks, days, 0, 0, 0, 0, 0, 0) is
  // false, throw a RangeError exception.
  if (!IsValidDuration(isolate,
                       {years, months, weeks, {days, 0, 0, 0, 0, 0, 0}})) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<DateDurationRecord>());
  }
  // 2. Return the Record { [[Years]]: ℝ(𝔽(years)), [[Months]]: ℝ(𝔽(months)),
  // [[Weeks]]: ℝ(𝔽(weeks)), [[Days]]: ℝ(𝔽(days)) }.
  DateDurationRecord record = {years, months, weeks, days};
  return Just(record);
}

}  // namespace

namespace temporal {
// #sec-temporal-createtimedurationrecord
Maybe<TimeDurationRecord> TimeDurationRecord::Create(
    Isolate* isolate, double days, double hours, double minutes, double seconds,
    double milliseconds, double microseconds, double nanoseconds) {
  // 1. If ! IsValidDuration(0, 0, 0, days, hours, minutes, seconds,
  // milliseconds, microseconds, nanoseconds) is false, throw a RangeError
  // exception.
  TimeDurationRecord record = {days,         hours,        minutes,    seconds,
                               milliseconds, microseconds, nanoseconds};
  if (!IsValidDuration(isolate, {0, 0, 0, record})) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<TimeDurationRecord>());
  }
  // 2. Return the Record { [[Days]]: ℝ(𝔽(days)), [[Hours]]: ℝ(𝔽(hours)),
  // [[Minutes]]: ℝ(𝔽(minutes)), [[Seconds]]: ℝ(𝔽(seconds)), [[Milliseconds]]:
  // ℝ(𝔽(milliseconds)), [[Microseconds]]: ℝ(𝔽(microseconds)), [[Nanoseconds]]:
  // ℝ(𝔽(nanoseconds)) }.
  return Just(record);
}

// #sec-temporal-createdurationrecord
Maybe<DurationRecord> DurationRecord::Create(
    Isolate* isolate, double years, double months, double weeks, double days,
    double hours, double minutes, double seconds, double milliseconds,
    double microseconds, double nanoseconds) {
  // 1. If ! IsValidDuration(years, months, weeks, days, hours, minutes,
  // seconds, milliseconds, microseconds, nanoseconds) is false, throw a
  // RangeError exception.
  DurationRecord record = {
      years,
      months,
      weeks,
      {days, hours, minutes, seconds, milliseconds, microseconds, nanoseconds}};
  if (!IsValidDuration(isolate, record)) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<DurationRecord>());
  }
  // 2. Return the Record { [[Years]]: ℝ(𝔽(years)), [[Months]]: ℝ(𝔽(months)),
  // [[Weeks]]: ℝ(𝔽(weeks)), [[Days]]: ℝ(𝔽(days)), [[Hours]]: ℝ(𝔽(hours)),
  // [[Minutes]]: ℝ(𝔽(minutes)), [[Seconds]]: ℝ(𝔽(seconds)), [[Milliseconds]]:
  // ℝ(𝔽(milliseconds)), [[Microseconds]]: ℝ(𝔽(microseconds)), [[Nanoseconds]]:
  // ℝ(𝔽(nanoseconds)) }.
  return Just(record);
}
}  // namespace temporal

namespace {
// #sec-temporal-createtemporalduration
MaybeDirectHandle<JSTemporalDuration> CreateTemporalDuration(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, const DurationRecord& duration) {
  TEMPORAL_ENTER_FUNC();
  Factory* factory = isolate->factory();
  // 1. If ! IsValidDuration(years, months, weeks, days, hours, minutes,
  // seconds, milliseconds, microseconds, nanoseconds) is false, throw a
  // RangeError exception.
  if (!IsValidDuration(isolate, duration)) {
    THROW_INVALID_RANGE(JSTemporalDuration);
  }

  // 2. If newTarget is not present, set it to %Temporal.Duration%.
  // 3. Let object be ? OrdinaryCreateFromConstructor(newTarget,
  // "%Temporal.Duration.prototype%", « [[InitializedTemporalDuration]],
  // [[Years]], [[Months]], [[Weeks]], [[Days]], [[Hours]], [[Minutes]],
  // [[Seconds]], [[Milliseconds]], [[Microseconds]], [[Nanoseconds]] »).
  const TimeDurationRecord& time_duration = duration.time_duration;
  DirectHandle<Number> years =
      factory->NewNumber(NormalizeMinusZero(duration.years));
  DirectHandle<Number> months =
      factory->NewNumber(NormalizeMinusZero(duration.months));
  DirectHandle<Number> weeks =
      factory->NewNumber(NormalizeMinusZero(duration.weeks));
  DirectHandle<Number> days =
      factory->NewNumber(NormalizeMinusZero(time_duration.days));
  DirectHandle<Number> hours =
      factory->NewNumber(NormalizeMinusZero(time_duration.hours));
  DirectHandle<Number> minutes =
      factory->NewNumber(NormalizeMinusZero(time_duration.minutes));
  DirectHandle<Number> seconds =
      factory->NewNumber(NormalizeMinusZero(time_duration.seconds));
  DirectHandle<Number> milliseconds =
      factory->NewNumber(NormalizeMinusZero(time_duration.milliseconds));
  DirectHandle<Number> microseconds =
      factory->NewNumber(NormalizeMinusZero(time_duration.microseconds));
  DirectHandle<Number> nanoseconds =
      factory->NewNumber(NormalizeMinusZero(time_duration.nanoseconds));
  ORDINARY_CREATE_FROM_CONSTRUCTOR(object, target, new_target,
                                   JSTemporalDuration)
  // 4. Set object.[[Years]] to ℝ(𝔽(years)).
  object->set_years(*years);
  // 5. Set object.[[Months]] to ℝ(𝔽(months)).
  object->set_months(*months);
  // 6. Set object.[[Weeks]] to ℝ(𝔽(weeks)).
  object->set_weeks(*weeks);
  // 7. Set object.[[Days]] to ℝ(𝔽(days)).
  object->set_days(*days);
  // 8. Set object.[[Hours]] to ℝ(𝔽(hours)).
  object->set_hours(*hours);
  // 9. Set object.[[Minutes]] to ℝ(𝔽(minutes)).
  object->set_minutes(*minutes);
  // 10. Set object.[[Seconds]] to ℝ(𝔽(seconds)).
  object->set_seconds(*seconds);
  // 11. Set object.[[Milliseconds]] to ℝ(𝔽(milliseconds)).
  object->set_milliseconds(*milliseconds);
  // 12. Set object.[[Microseconds]] to ℝ(𝔽(microseconds)).
  object->set_microseconds(*microseconds);
  // 13. Set object.[[Nanoseconds]] to ℝ(𝔽(nanoseconds)).
  object->set_nanoseconds(*nanoseconds);
  // 14. Return object.
  return object;
}

MaybeDirectHandle<JSTemporalDuration> CreateTemporalDuration(
    Isolate* isolate, const DurationRecord& duration) {
  TEMPORAL_ENTER_FUNC();
  return CreateTemporalDuration(isolate, CONSTRUCTOR(duration),
                                CONSTRUCTOR(duration), duration);
}

}  // namespace

namespace temporal {

// #sec-temporal-createtemporalinstant
MaybeDirectHandle<JSTemporalInstant> CreateTemporalInstant(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target,
    DirectHandle<BigInt> epoch_nanoseconds) {
  TEMPORAL_ENTER_FUNC();
  // 1. Assert: Type(epochNanoseconds) is BigInt.
  // 2. Assert: ! IsValidEpochNanoseconds(epochNanoseconds) is true.
  DCHECK(IsValidEpochNanoseconds(isolate, epoch_nanoseconds));

  // 4. Let object be ? OrdinaryCreateFromConstructor(newTarget,
  // "%Temporal.Instant.prototype%", « [[InitializedTemporalInstant]],
  // [[Nanoseconds]] »).
  ORDINARY_CREATE_FROM_CONSTRUCTOR(object, target, new_target,
                                   JSTemporalInstant)
  // 5. Set object.[[Nanoseconds]] to ns.
  object->set_nanoseconds(*epoch_nanoseconds);
  return object;
}

MaybeDirectHandle<JSTemporalInstant> CreateTemporalInstant(
    Isolate* isolate, DirectHandle<BigInt> epoch_nanoseconds) {
  TEMPORAL_ENTER_FUNC();
  return CreateTemporalInstant(isolate, CONSTRUCTOR(instant),
                               CONSTRUCTOR(instant), epoch_nanoseconds);
}

}  // namespace temporal

namespace {

MaybeDirectHandle<JSTemporalTimeZone> CreateTemporalTimeZoneFromIndex(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, int32_t index) {
  TEMPORAL_ENTER_FUNC();
  ORDINARY_CREATE_FROM_CONSTRUCTOR(object, target, new_target,
                                   JSTemporalTimeZone)
  object->set_flags(0);
  object->set_details(0);

  object->set_is_offset(false);
  object->set_offset_milliseconds_or_time_zone_index(index);
  return object;
}

DirectHandle<JSTemporalTimeZone> CreateTemporalTimeZoneUTC(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target) {
  TEMPORAL_ENTER_FUNC();
  return CreateTemporalTimeZoneFromIndex(isolate, target, new_target, 0)
      .ToHandleChecked();
}

DirectHandle<JSTemporalTimeZone> CreateTemporalTimeZoneUTC(Isolate* isolate) {
  TEMPORAL_ENTER_FUNC();
  return CreateTemporalTimeZoneUTC(isolate, CONSTRUCTOR(time_zone),
                                   CONSTRUCTOR(time_zone));
}

bool IsUTC(Isolate* isolate, DirectHandle<String> time_zone);

// #sec-temporal-createtemporaltimezone
MaybeDirectHandle<JSTemporalTimeZone> CreateTemporalTimeZone(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, DirectHandle<String> identifier) {
  TEMPORAL_ENTER_FUNC();

  // 1. If newTarget is not present, set it to %Temporal.TimeZone%.
  // 2. Let object be ? OrdinaryCreateFromConstructor(newTarget,
  // "%Temporal.TimeZone.prototype%", « [[InitializedTemporalTimeZone]],
  // [[Identifier]], [[OffsetNanoseconds]] »).

  // 3. Let offsetNanosecondsResult be ParseTimeZoneOffsetString(identifier).
  Maybe<int64_t> maybe_offset_nanoseconds =
      ParseTimeZoneOffsetString(isolate, identifier);
  // 4. If offsetNanosecondsResult is an abrupt completion, then
  if (maybe_offset_nanoseconds.IsNothing()) {
    DCHECK(isolate->has_exception());
    isolate->clear_exception();
    // a. Assert: ! CanonicalizeTimeZoneName(identifier) is identifier.
    DCHECK(String::Equals(isolate, identifier,
                          CanonicalizeTimeZoneName(isolate, identifier)));

    // b. Set object.[[Identifier]] to identifier.
    // c. Set object.[[OffsetNanoseconds]] to undefined.
    if (IsUTC(isolate, identifier)) {
      return CreateTemporalTimeZoneUTC(isolate, target, new_target);
    }
#ifdef V8_INTL_SUPPORT
    int32_t time_zone_index = Intl::GetTimeZoneIndex(isolate, identifier);
    DCHECK_GE(time_zone_index, 0);
    return CreateTemporalTimeZoneFromIndex(isolate, target, new_target,
                                           time_zone_index);
#else
    UNREACHABLE();
#endif  // V8_INTL_SUPPORT
    // 5. Else,
  } else {
    // a. Set object.[[Identifier]] to !
    // FormatTimeZoneOffsetString(offsetNanosecondsResult.[[Value]]). b. Set
    // object.[[OffsetNanoseconds]] to offsetNanosecondsResult.[[Value]].
    ORDINARY_CREATE_FROM_CONSTRUCTOR(object, target, new_target,
                                     JSTemporalTimeZone)
    object->set_flags(0);
    object->set_details(0);

    object->set_is_offset(true);
    object->set_offset_nanoseconds(maybe_offset_nanoseconds.FromJust());
    return object;
  }
  // 6. Return object.
}

MaybeDirectHandle<JSTemporalTimeZone> CreateTemporalTimeZoneDefaultTarget(
    Isolate* isolate, DirectHandle<String> identifier) {
  TEMPORAL_ENTER_FUNC();
  return CreateTemporalTimeZone(isolate, CONSTRUCTOR(time_zone),
                                CONSTRUCTOR(time_zone), identifier);
}

}  // namespace

namespace temporal {
MaybeDirectHandle<JSTemporalTimeZone> CreateTemporalTimeZone(
    Isolate* isolate, DirectHandle<String> identifier) {
  return CreateTemporalTimeZoneDefaultTarget(isolate, identifier);
}
}  // namespace temporal

namespace {

// #sec-temporal-systeminstant
DirectHandle<JSTemporalInstant> SystemInstant(Isolate* isolate) {
  TEMPORAL_ENTER_FUNC();
  // 1. Let ns be ! SystemUTCEpochNanoseconds().
  DirectHandle<BigInt> ns = SystemUTCEpochNanoseconds(isolate);
  // 2. Return ? CreateTemporalInstant(ns).
  return temporal::CreateTemporalInstant(isolate, ns).ToHandleChecked();
}

// #sec-temporal-systemtimezone
DirectHandle<JSTemporalTimeZone> SystemTimeZone(Isolate* isolate) {
  TEMPORAL_ENTER_FUNC();
  DirectHandle<String> default_time_zone = DefaultTimeZone(isolate);
  return temporal::CreateTemporalTimeZone(isolate, default_time_zone)
      .ToHandleChecked();
}

DateTimeRecord GetISOPartsFromEpoch(Isolate* isolate,
                                    DirectHandle<BigInt> epoch_nanoseconds) {
  TEMPORAL_ENTER_FUNC();
  DateTimeRecord result;
  // 1. Assert: ! IsValidEpochNanoseconds(ℤ(epochNanoseconds)) is true.
  DCHECK(IsValidEpochNanoseconds(isolate, epoch_nanoseconds));
  // 2. Let remainderNs be epochNanoseconds modulo 10^6.
  DirectHandle<BigInt> million = BigInt::FromUint64(isolate, 1000000);
  DirectHandle<BigInt> remainder_ns =
      BigInt::Remainder(isolate, epoch_nanoseconds, million).ToHandleChecked();
  // Need to do some remainder magic to negative remainder.
  if (remainder_ns->IsNegative()) {
    remainder_ns =
        BigInt::Add(isolate, remainder_ns, million).ToHandleChecked();
  }

  // 3. Let epochMilliseconds be (epochNanoseconds − remainderNs) / 10^6.
  int64_t epoch_milliseconds =
      BigInt::Divide(isolate,
                     BigInt::Subtract(isolate, epoch_nanoseconds, remainder_ns)
                         .ToHandleChecked(),
                     million)
          .ToHandleChecked()
          ->AsInt64();
  int year = 0;
  int month = 0;
  int day = 0;
  int wday = 0;
  int hour = 0;
  int min = 0;
  int sec = 0;
  int ms = 0;
  isolate->date_cache()->BreakDownTime(epoch_milliseconds, &year, &month, &day,
                                       &wday, &hour, &min, &sec, &ms);

  // 4. Let year be ! YearFromTime(epochMilliseconds).
  result.date.year = year;
  // 5. Let month be ! MonthFromTime(epochMilliseconds) + 1.
  result.date.month = month + 1;
  DCHECK_GE(result.date.month, 1);
  DCHECK_LE(result.date.month, 12);
  // 6. Let day be ! DateFromTime(epochMilliseconds).
  result.date.day = day;
  DCHECK_GE(result.date.day, 1);
  DCHECK_LE(result.date.day, 31);
  // 7. Let hour be ! HourFromTime(epochMilliseconds).
  result.time.hour = hour;
  DCHECK_GE(result.time.hour, 0);
  DCHECK_LE(result.time.hour, 23);
  // 8. Let minute be ! MinFromTime(epochMilliseconds).
  result.time.minute = min;
  DCHECK_GE(result.time.minute, 0);
  DCHECK_LE(result.time.minute, 59);
  // 9. Let second be ! SecFromTime(epochMilliseconds).
  result.time.second = sec;
  DCHECK_GE(result.time.second, 0);
  DCHECK_LE(result.time.second, 59);
  // 10. Let millisecond be ! msFromTime(epochMilliseconds).
  result.time.millisecond = ms;
  DCHECK_GE(result.time.millisecond, 0);
  DCHECK_LE(result.time.millisecond, 999);
  // 11. Let microsecond be floor(remainderNs / 1000) modulo 1000.
  int64_t remainder = remainder_ns->AsInt64();
  result.time.microsecond = (remainder / 1000) % 1000;
  DCHECK_GE(result.time.microsecond, 0);
  // 12. 12. Assert: microsecond < 1000.
  DCHECK_LE(result.time.microsecond, 999);
  // 13. Let nanosecond be remainderNs modulo 1000.
  result.time.nanosecond = remainder % 1000;
  DCHECK_GE(result.time.nanosecond, 0);
  DCHECK_LE(result.time.nanosecond, 999);
  // 14. Return the Record { [[Year]]: year, [[Month]]: month, [[Day]]: day,
  // [[Hour]]: hour, [[Minute]]: minute, [[Second]]: second, [[Millisecond]]:
  // millisecond, [[Microsecond]]: microsecond, [[Nanosecond]]: nanosecond }.
  return result;
}

// #sec-temporal-balanceisodatetime
DateTimeRecord BalanceISODateTime(Isolate* isolate,
                                  const DateTimeRecord& date_time) {
  TEMPORAL_ENTER_FUNC();
  // 1. Assert: year, month, day, hour, minute, second, millisecond,
  // microsecond, and nanosecond are integers.
  // 2. Let balancedTime be ! BalanceTime(hour, minute, second, millisecond,
  // microsecond, nanosecond).
  DateTimeRecord balanced_time =
      BalanceTime({static_cast<double>(date_time.time.hour),
                   static_cast<double>(date_time.time.minute),
                   static_cast<double>(date_time.time.second),
                   static_cast<double>(date_time.time.millisecond),
                   static_cast<double>(date_time.time.microsecond),
                   static_cast<double>(date_time.time.nanosecond)});
  // 3. Let balancedDate be ! BalanceISODate(year, month, day +
  // balancedTime.[[Days]]).
  DateRecord added_date = date_time.date;
  added_date.day += balanced_time.date.day;
  DateRecord balanced_date = BalanceISODate(isolate, added_date);
  // 4. Return the Record { [[Year]]: balancedDate.[[Year]], [[Month]]:
  // balancedDate.[[Month]], [[Day]]: balancedDate.[[Day]], [[Hour]]:
  // balancedTime.[[Hour]], [[Minute]]: balancedTime.[[Minute]], [[Second]]:
  // balancedTime.[[Second]], [[Millisecond]]: balancedTime.[[Millisecond]],
  // [[Microsecond]]: balancedTime.[[Microsecond]], [[Nanosecond]]:
  // balancedTime.[[Nanosecond]] }.
  return {balanced_date, balanced_time.time};
}

// #sec-temporal-roundtowardszero
double RoundTowardsZero(double x) {
  // 1. Return the mathematical value that is the same sign as x and whose
  // magnitude is floor(abs(x)).
  if (x < 0) {
    return -std::floor(std::abs(x));
  } else {
    return std::floor(std::abs(x));
  }
}

// #sec-temporal-temporaldurationtostring
DirectHandle<String> TemporalDurationToString(Isolate* isolate,
                                              const DurationRecord& duration,
                                              Precision precision) {
  IncrementalStringBuilder builder(isolate);
  DCHECK(precision != Precision::kMinute);
  // 1. Let sign be ! DurationSign(years, months, weeks, days, hours, minutes,
  // seconds, milliseconds, microseconds, nanoseconds).
  DurationRecord dur = duration;
  int32_t sign = DurationRecord::Sign(dur);
  // Note: for the operation below, to avoid microseconds .. seconds lost
  // precision while the resulting value may exceed the precision limit, we use
  // extra double xx_add to hold the additional temp value.
  // 2. Set microseconds to microseconds + RoundTowardsZero(nanoseconds / 1000).
  double microseconds_add =
      RoundTowardsZero(dur.time_duration.nanoseconds / 1000);
  // 3. Set nanoseconds to remainder(nanoseconds, 1000).
  dur.time_duration.nanoseconds =
      std::fmod(dur.time_duration.nanoseconds, 1000);
  // 4. Set milliseconds to milliseconds + RoundTowardsZero(microseconds /
  // 1000).
  double milliseconds_add = RoundTowardsZero(
      dur.time_duration.microseconds / 1000 + microseconds_add / 1000);
  // 5. Set microseconds to remainder(microseconds, 1000).
  dur.time_duration.microseconds =
      std::fmod(std::fmod(dur.time_duration.microseconds, 1000) +
                    std::fmod(microseconds_add, 1000),
                1000);
  // 6. Set seconds to seconds + RoundTowardsZero(milliseconds / 1000).
  double seconds_add = RoundTowardsZero(dur.time_duration.milliseconds / 1000 +
                                        milliseconds_add / 1000);
  // 7. Set milliseconds to remainder(milliseconds, 1000).
  dur.time_duration.milliseconds =
      std::fmod(std::fmod(dur.time_duration.milliseconds, 1000) +
                    std::fmod(milliseconds_add, 1000),
                1000);

  // 8. Let datePart be "".
  IncrementalStringBuilder date_part(isolate);
  // Number.MAX_VALUE.toString() is "1.7976931348623157e+308"
  // We add several more spaces to 320.
  base::ScopedVector<char> buf(320);

  // 9. If years is not 0, then
  if (dur.years != 0) {
    // a. Set datePart to the string concatenation of abs(years) formatted as a
    // decimal number and the code unit 0x0059 (LATIN CAPITAL LETTER Y).
    SNPrintF(buf, "%.0f", std::abs(dur.years));
    date_part.AppendCString(buf.data());
    date_part.AppendCharacter('Y');
  }
  // 10. If months is not 0, then
  if (dur.months != 0) {
    // a. Set datePart to the string concatenation of datePart,
    // abs(months) formatted as a decimal number, and the code unit
    // 0x004D (LATIN CAPITAL LETTER M).
    SNPrintF(buf, "%.0f", std::abs(dur.months));
    date_part.AppendCString(buf.data());
    date_part.AppendCharacter('M');
  }
  // 11. If weeks is not 0, then
  if (dur.weeks != 0) {
    // a. Set datePart to the string concatenation of datePart,
    // abs(weeks) formatted as a decimal number, and the code unit
    // 0x0057 (LATIN CAPITAL LETTER W).
    SNPrintF(buf, "%.0f", std::abs(dur.weeks));
    date_part.AppendCString(buf.data());
    date_part.AppendCharacter('W');
  }
  // 12. If days is not 0, then
  if (dur.time_duration.days != 0) {
    // a. Set datePart to the string concatenation of datePart,
    // abs(days) formatted as a decimal number, and the code unit 0x0044
    // (LATIN CAPITAL LETTER D).
    SNPrintF(buf, "%.0f", std::abs(dur.time_duration.days));
    date_part.AppendCString(buf.data());
    date_part.AppendCharacter('D');
  }
  // 13. Let timePart be "".
  IncrementalStringBuilder time_part(isolate);
  // 14. If hours is not 0, then
  if (dur.time_duration.hours != 0) {
    // a. Set timePart to the string concatenation of abs(hours) formatted as a
    // decimal number and the code unit 0x0048 (LATIN CAPITAL LETTER H).
    SNPrintF(buf, "%.0f", std::abs(dur.time_duration.hours));
    time_part.AppendCString(buf.data());
    time_part.AppendCharacter('H');
  }
  // 15. If minutes is not 0, then
  if (dur.time_duration.minutes != 0) {
    // a. Set timePart to the string concatenation of timePart,
    // abs(minutes) formatted as a decimal number, and the code unit
    // 0x004D (LATIN CAPITAL LETTER M).
    SNPrintF(buf, "%.0f", std::abs(dur.time_duration.minutes));
    time_part.AppendCString(buf.data());
    time_part.AppendCharacter('M');
  }
  IncrementalStringBuilder seconds_part(isolate);
  IncrementalStringBuilder decimal_part(isolate);
  // 16. If any of seconds, milliseconds, microseconds, and nanoseconds are not
  // 0; or years, months, weeks, days, hours, and minutes are all 0, or
  // precision is not "auto" then
  if ((dur.time_duration.seconds != 0 || seconds_add != 0 ||
       dur.time_duration.milliseconds != 0 ||
       dur.time_duration.microseconds != 0 ||
       dur.time_duration.nanoseconds != 0) ||
      (dur.years == 0 && dur.months == 0 && dur.weeks == 0 &&
       dur.time_duration.days == 0 && dur.time_duration.hours == 0 &&
       dur.time_duration.minutes == 0) ||
      precision != Precision::kAuto) {
    // a. Let fraction be abs(milliseconds) × 10^6 + abs(microseconds) × 10^3 +
    // abs(nanoseconds).
    int64_t fraction = std::abs(dur.time_duration.milliseconds) * 1e6 +
                       std::abs(dur.time_duration.microseconds) * 1e3 +
                       std::abs(dur.time_duration.nanoseconds);
    // b. Let decimalPart be fraction formatted as a nine-digit decimal number,
    // padded to the left with zeroes if necessary.
    int64_t divisor = 100000000;

    // c. If precision is "auto", then
    if (precision == Precision::kAuto) {
      // i. Set decimalPart to the longest possible substring of decimalPart
      // starting at position 0 and not ending with the code unit 0x0030 (DIGIT
      // ZERO).
      while (fraction > 0) {
        decimal_part.AppendInt(static_cast<int32_t>(fraction / divisor));
        fraction %= divisor;
        divisor /= 10;
      }
      // d. Else if precision = 0, then
    } else if (precision == Precision::k0) {
      // i. Set decimalPart to "".
      // e. Else,
    } else {
      // i. Set decimalPart to the substring of decimalPart from 0 to precision.
      int32_t precision_len = static_cast<int32_t>(precision);
      DCHECK_LE(0, precision_len);
      DCHECK_GE(9, precision_len);
      for (int32_t len = 0; len < precision_len; len++) {
        decimal_part.AppendInt(static_cast<int32_t>(fraction / divisor));
        fraction %= divisor;
        divisor /= 10;
      }
    }
    // f. Let secondsPart be abs(seconds) formatted as a decimal number.
    if (std::abs(seconds_add + dur.time_duration.seconds) < kMaxSafeInteger) {
      // Fast path: The seconds_add + dur.time_duration.seconds is in the range
      // the double could keep the precision.
      dur.time_duration.seconds += seconds_add;
      SNPrintF(buf, "%.0f", std::abs(dur.time_duration.seconds));
      seconds_part.AppendCString(buf.data());
    } else {
      // Slow path: The seconds_add + dur.time_duration.seconds is out of the
      // range which the double could keep the precision. Format by math via
      // BigInt.
      seconds_part.AppendString(
          BigInt::ToString(
              isolate,
              BigInt::Add(
                  isolate,
                  BigInt::FromNumber(isolate, isolate->factory()->NewNumber(
                                                  std::abs(seconds_add)))
                      .ToHandleChecked(),
                  BigInt::FromNumber(isolate,
                                     isolate->factory()->NewNumber(
                                         std::abs(dur.time_duration.seconds)))
                      .ToHandleChecked())
                  .ToHandleChecked())
              .ToHandleChecked());
    }

    // g. If decimalPart is not "", then
    if (decimal_part.Length() != 0) {
      // i. Set secondsPart to the string-concatenation of secondsPart, the code
      // unit 0x002E (FULL STOP), and decimalPart.
      seconds_part.AppendCharacter('.');
      seconds_part.AppendString(decimal_part.Finish().ToHandleChecked());
    }

    // h. Set timePart to the string concatenation of timePart, secondsPart, and
    // the code unit 0x0053 (LATIN CAPITAL LETTER S).
    time_part.AppendString(seconds_part.Finish().ToHandleChecked());
    time_part.AppendCharacter('S');
  }
  // 17. Let signPart be the code unit 0x002D (HYPHEN-MINUS) if sign < 0, and
  // otherwise the empty String.
  if (sign < 0) {
    builder.AppendCharacter('-');
  }

  // 18. Let result be the string concatenation of signPart, the code unit
  // 0x0050 (LATIN CAPITAL LETTER P) and datePart.
  builder.AppendCharacter('P');
  builder.AppendString(date_part.Finish().ToHandleChecked());

  // 19. If timePart is not "", then
  if (time_part.Length() > 0) {
    // a. Set result to the string concatenation of result, the code unit 0x0054
    // (LATIN CAPITAL LETTER T), and timePart.
    builder.AppendCharacter('T');
    builder.AppendString(time_part.Finish().ToHandleChecked());
  }
  return builder.Finish().ToHandleChecked();
}

void ToZeroPaddedDecimalString(IncrementalStringBuilder* builder, int32_t n,
                               int32_t min_length);
// #sec-temporal-formatsecondsstringpart
void FormatSecondsStringPart(IncrementalStringBuilder* builder, int32_t second,
                             int32_t millisecond, int32_t microsecond,
                             int32_t nanosecond, Precision precision) {
  // 1. Assert: second, millisecond, microsecond and nanosecond are integers.
  // 2. If precision is "minute", return "".
  if (precision == Precision::kMinute) {
    return;
  }
  // 3. Let secondsString be the string-concatenation of the code unit 0x003A
  // (COLON) and second formatted as a two-digit decimal number, padded to the
  // left with zeroes if necessary.
  builder->AppendCharacter(':');
  ToZeroPaddedDecimalString(builder, second, 2);
  // 4. Let fraction be millisecond × 10^6 + microsecond × 10^3 + nanosecond.
  int64_t fraction = millisecond * 1000000 + microsecond * 1000 + nanosecond;
  int64_t divisor = 100000000;
  // 5. If precision is "auto", then
  if (precision == Precision::kAuto) {
    // a. If fraction is 0, return secondsString.
    if (fraction == 0) {
      return;
    }
    builder->AppendCharacter('.');
    // b. Set fraction to ToZeroPaddedDecimalString(fraction, 9).
    // c. Set fraction to the longest possible substring of fraction starting at
    // position 0 and not ending with the code unit 0x0030 (DIGIT ZERO).
    while (fraction > 0) {
      builder->AppendInt(static_cast<int32_t>(fraction / divisor));
      fraction %= divisor;
      divisor /= 10;
    }
    // 6. Else,
  } else {
    // a. If precision is 0, return secondsString.
    if (precision == Precision::k0) {
      return;
    }
    builder->AppendCharacter('.');
    // b. Set fraction to ToZeroPaddedDecimalString(fraction, 9).
    // c. Set fraction to the substring of fraction from 0 to precision.
    int32_t precision_len = static_cast<int32_t>(precision);
    DCHECK_LE(0, precision_len);
    DCHECK_GE(9, precision_len);
    for (int32_t len = 0; len < precision_len; len++) {
      builder->AppendInt(static_cast<int32_t>(fraction / divisor));
      fraction %= divisor;
      divisor /= 10;
    }
  }
  // 7. Return the string-concatenation of secondsString, the code unit 0x002E
  // (FULL STOP), and fraction.
}

// #sec-temporal-temporaltimetostring
DirectHandle<String> TemporalTimeToString(Isolate* isolate,
                                          const TimeRecord& time,
                                          Precision precision) {
  // 1. Assert: hour, minute, second, millisecond, microsecond and nanosecond
  // are integers.
  IncrementalStringBuilder builder(isolate);
  // 2. Let hour be ToZeroPaddedDecimalString(hour, 2).
  ToZeroPaddedDecimalString(&builder, time.hour, 2);
  builder.AppendCharacter(':');
  // 3. Let minute be ToZeroPaddedDecimalString(minute, 2).
  ToZeroPaddedDecimalString(&builder, time.minute, 2);
  // 4. Let seconds be ! FormatSecondsStringPart(second, millisecond,
  // microsecond, nanosecond, precision).
  FormatSecondsStringPart(&builder, time.second, time.millisecond,
                          time.microsecond, time.nanosecond, precision);
  // 5. Return the string-concatenation of hour, the code unit 0x003A (COLON),
  // minute, and seconds.
  return builder.Finish().ToHandleChecked();
}

DirectHandle<String> TemporalTimeToString(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time,
    Precision precision) {
  return TemporalTimeToString(
      isolate,
      {temporal_time->iso_hour(), temporal_time->iso_minute(),
       temporal_time->iso_second(), temporal_time->iso_millisecond(),
       temporal_time->iso_microsecond(), temporal_time->iso_nanosecond()},
      precision);
}

}  // namespace

namespace temporal {
MaybeDirectHandle<JSTemporalPlainDateTime> BuiltinTimeZoneGetPlainDateTimeFor(
    Isolate* isolate, DirectHandle<JSReceiver> time_zone,
    DirectHandle<JSTemporalInstant> instant, DirectHandle<JSReceiver> calendar,
    const char* method_name) {
  TEMPORAL_ENTER_FUNC();
  // 1. Let offsetNanoseconds be ? GetOffsetNanosecondsFor(timeZone, instant).
  int64_t offset_nanoseconds;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, offset_nanoseconds,
      GetOffsetNanosecondsFor(isolate, time_zone, instant, method_name),
      DirectHandle<JSTemporalPlainDateTime>());
  // 2. Let result be ! GetISOPartsFromEpoch(instant.[[Nanoseconds]]).
  DateTimeRecord result = GetISOPartsFromEpoch(
      isolate, direct_handle(instant->nanoseconds(), isolate));

  // 3. Set result to ! BalanceISODateTime(result.[[Year]], result.[[Month]],
  // result.[[Day]], result.[[Hour]], result.[[Minute]], result.[[Second]],
  // result.[[Millisecond]], result.[[Microsecond]], result.[[Nanosecond]] +
  // offsetNanoseconds).

  // Note: Since offsetNanoseconds is bounded 86400x 10^9, the
  // result of result.[[Nanosecond]] + offsetNanoseconds may overflow int32_t
  // Therefore we distribute the sum to other fields below to make sure it won't
  // overflow each of the int32_t fields. But it will leave each field to be
  // balanced by BalanceISODateTime
  result.time.nanosecond += offset_nanoseconds % 1000;
  result.time.microsecond += (offset_nanoseconds / 1000) % 1000;
  result.time.millisecond += (offset_nanoseconds / 1000000L) % 1000;
  result.time.second += (offset_nanoseconds / 1000000000L) % 60;
  result.time.minute += (offset_nanoseconds / 60000000000L) % 60;
  result.time.hour += (offset_nanoseconds / 3600000000000L) % 24;
  result.date.day += (offset_nanoseconds / 86400000000000L);

  result = BalanceISODateTime(isolate, result);
  // 4. Return ? CreateTemporalDateTime(result.[[Year]], result.[[Month]],
  // result.[[Day]], result.[[Hour]], result.[[Minute]], result.[[Second]],
  // result.[[Millisecond]], result.[[Microsecond]], result.[[Nanosecond]],
  // calendar).
  return temporal::CreateTemporalDateTime(isolate, result, calendar);
}

}  // namespace temporal

namespace {
// #sec-temporal-getpossibleinstantsfor
MaybeDirectHandle<FixedArray> GetPossibleInstantsFor(
    Isolate* isolate, DirectHandle<JSReceiver> time_zone,
    DirectHandle<Object> date_time) {
  TEMPORAL_ENTER_FUNC();
  // 1. Let possibleInstants be ? Invoke(timeZone, "getPossibleInstantsFor", «
  // dateTime »).
  DirectHandle<Object> function;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, function,
      Object::GetProperty(isolate, time_zone,
                          isolate->factory()->getPossibleInstantsFor_string()));
  if (!IsCallable(*function)) {
    THROW_NEW_ERROR(
        isolate,
        NewTypeError(MessageTemplate::kCalledNonCallable,
                     isolate->factory()->getPossibleInstantsFor_string()));
  }
  DirectHandle<Object> possible_instants;
  {
    DirectHandle<Object> args[] = {date_time};
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, possible_instants,
        Execution::Call(isolate, function, time_zone, base::VectorOf(args)));
  }

  // Step 4-6 of GetPossibleInstantsFor is implemented inside
  // temporal_instant_fixed_array_from_iterable.
  {
    DirectHandle<Object> args[] = {possible_instants};
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, possible_instants,
        Execution::CallBuiltin(
            isolate, isolate->temporal_instant_fixed_array_from_iterable(),
            possible_instants, base::VectorOf(args)));
  }
  DCHECK(IsFixedArray(*possible_instants));
  // 7. Return list.
  return Cast<FixedArray>(possible_instants);
}

// #sec-temporal-disambiguatepossibleinstants
MaybeDirectHandle<JSTemporalInstant> DisambiguatePossibleInstants(
    Isolate* isolate, DirectHandle<FixedArray> possible_instants,
    DirectHandle<JSReceiver> time_zone, DirectHandle<Object> date_time_obj,
    Disambiguation disambiguation, const char* method_name) {
  TEMPORAL_ENTER_FUNC();
  // 1. Assert: dateTime has an [[InitializedTemporalDateTime]] internal slot.
  DCHECK(IsJSTemporalPlainDateTime(*date_time_obj));
  auto date_time = Cast<JSTemporalPlainDateTime>(date_time_obj);

  // 2. Let n be possibleInstants's length.
  int32_t n = possible_instants->length();

  // 3. If n = 1, then
  if (n == 1) {
    // a. Return possibleInstants[0].
    DirectHandle<Object> ret_obj(possible_instants->get(0), isolate);
    DCHECK(IsJSTemporalInstant(*ret_obj));
    return Cast<JSTemporalInstant>(ret_obj);
  }
  // 4. If n ≠ 0, then
  if (n != 0) {
    // a. If disambiguation is "earlier" or "compatible", then
    if (disambiguation == Disambiguation::kEarlier ||
        disambiguation == Disambiguation::kCompatible) {
      // i. Return possibleInstants[0].
      DirectHandle<Object> ret_obj(possible_instants->get(0), isolate);
      DCHECK(IsJSTemporalInstant(*ret_obj));
      return Cast<JSTemporalInstant>(ret_obj);
    }
    // b. If disambiguation is "later", then
    if (disambiguation == Disambiguation::kLater) {
      // i. Return possibleInstants[n − 1].
      DirectHandle<Object> ret_obj(possible_instants->get(n - 1), isolate);
      DCHECK(IsJSTemporalInstant(*ret_obj));
      return Cast<JSTemporalInstant>(ret_obj);
    }
    // c. Assert: disambiguation is "reject".
    DCHECK_EQ(disambiguation, Disambiguation::kReject);
    // d. Throw a RangeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }
  // 5. Assert: n = 0.
  DCHECK_EQ(n, 0);
  // 6. If disambiguation is "reject", then
  if (disambiguation == Disambiguation::kReject) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }
  // 7. Let epochNanoseconds be ! GetEpochFromISOParts(dateTime.[[ISOYear]],
  // dateTime.[[ISOMonth]], dateTime.[[ISODay]], dateTime.[[ISOHour]],
  // dateTime.[[ISOMinute]], dateTime.[[ISOSecond]],
  // dateTime.[[ISOMillisecond]], dateTime.[[ISOMicrosecond]],
  // dateTime.[[ISONanosecond]]).
  DirectHandle<BigInt> epoch_nanoseconds = GetEpochFromISOParts(
      isolate,
      {{date_time->iso_year(), date_time->iso_month(), date_time->iso_day()},
       {date_time->iso_hour(), date_time->iso_minute(), date_time->iso_second(),
        date_time->iso_millisecond(), date_time->iso_microsecond(),
        date_time->iso_nanosecond()}});

  // 8. Let dayBeforeNs be epochNanoseconds - ℤ(nsPerDay).
  DirectHandle<BigInt> one_day_in_ns =
      BigInt::FromUint64(isolate, 86400000000000ULL);
  DirectHandle<BigInt> day_before_ns =
      BigInt::Subtract(isolate, epoch_nanoseconds, one_day_in_ns)
          .ToHandleChecked();
  // 9. If ! IsValidEpochNanoseconds(dayBeforeNs) is false, throw a RangeError
  // exception.
  if (!IsValidEpochNanoseconds(isolate, day_before_ns)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }
  // 10. Let dayBefore be ! CreateTemporalInstant(dayBeforeNs).
  DirectHandle<JSTemporalInstant> day_before =
      temporal::CreateTemporalInstant(isolate, day_before_ns).ToHandleChecked();
  // 11. Let dayAfterNs be epochNanoseconds + ℤ(nsPerDay).
  DirectHandle<BigInt> day_after_ns =
      BigInt::Add(isolate, epoch_nanoseconds, one_day_in_ns).ToHandleChecked();
  // 12. If ! IsValidEpochNanoseconds(dayAfterNs) is false, throw a RangeError
  // exception.
  if (!IsValidEpochNanoseconds(isolate, day_after_ns)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }
  // 13. Let dayAfter be ! CreateTemporalInstant(dayAfterNs).
  DirectHandle<JSTemporalInstant> day_after =
      temporal::CreateTemporalInstant(isolate, day_after_ns).ToHandleChecked();
  // 10. Let offsetBefore be ? GetOffsetNanosecondsFor(timeZone, dayBefore).
  int64_t offset_before;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, offset_before,
      GetOffsetNanosecondsFor(isolate, time_zone, day_before, method_name),
      DirectHandle<JSTemporalInstant>());
  // 11. Let offsetAfter be ? GetOffsetNanosecondsFor(timeZone, dayAfter).
  int64_t offset_after;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, offset_after,
      GetOffsetNanosecondsFor(isolate, time_zone, day_after, method_name),
      DirectHandle<JSTemporalInstant>());

  // 12. Let nanoseconds be offsetAfter − offsetBefore.
  double nanoseconds = offset_after - offset_before;

  // 13. If disambiguation is "earlier", then
  if (disambiguation == Disambiguation::kEarlier) {
    // a. Let earlier be ? AddDateTime(dateTime.[[ISOYear]],
    // dateTime.[[ISOMonth]], dateTime.[[ISODay]], dateTime.[[ISOHour]],
    // dateTime.[[ISOMinute]], dateTime.[[ISOSecond]],
    // dateTime.[[ISOMillisecond]],
    // dateTime.[[ISOMicrosecond]], dateTime.[[ISONanosecond]],
    // dateTime.[[Calendar]], 0, 0, 0, 0, 0, 0, 0, 0, 0, −nanoseconds,
    // undefined).
    DateTimeRecord earlier;
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, earlier,
        AddDateTime(
            isolate,
            {{date_time->iso_year(), date_time->iso_month(),
              date_time->iso_day()},
             {date_time->iso_hour(), date_time->iso_minute(),
              date_time->iso_second(), date_time->iso_millisecond(),
              date_time->iso_microsecond(), date_time->iso_nanosecond()}},
            direct_handle(date_time->calendar(), isolate),
            {0, 0, 0, {0, 0, 0, 0, 0, 0, -nanoseconds}},
            isolate->factory()->undefined_value()),
        DirectHandle<JSTemporalInstant>());
    // See https://github.com/tc39/proposal-temporal/issues/1816
    // b. Let earlierDateTime be ? CreateTemporalDateTime(earlier.[[Year]],
    // earlier.[[Month]], earlier.[[Day]], earlier.[[Hour]], earlier.[[Minute]],
    // earlier.[[Second]], earlier.[[Millisecond]], earlier.[[Microsecond]],
    // earlier.[[Nanosecond]], dateTime.[[Calendar]]).
    DirectHandle<JSTemporalPlainDateTime> earlier_date_time;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, earlier_date_time,
        temporal::CreateTemporalDateTime(
            isolate, earlier, direct_handle(date_time->calendar(), isolate)));

    // c. Set possibleInstants to ? GetPossibleInstantsFor(timeZone,
    // earlierDateTime).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, possible_instants,
        GetPossibleInstantsFor(isolate, time_zone, earlier_date_time));

    // d. If possibleInstants is empty, throw a RangeError exception.
    if (possible_instants->length() == 0) {
      THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
    }
    // e. Return possibleInstants[0].
    DirectHandle<Object> ret_obj(possible_instants->get(0), isolate);
    DCHECK(IsJSTemporalInstant(*ret_obj));
    return Cast<JSTemporalInstant>(ret_obj);
  }
  // 14. Assert: disambiguation is "compatible" or "later".
  DCHECK(disambiguation == Disambiguation::kCompatible ||
         disambiguation == Disambiguation::kLater);
  // 15. Let later be ? AddDateTime(dateTime.[[ISOYear]], dateTime.[[ISOMonth]],
  // dateTime.[[ISODay]], dateTime.[[ISOHour]], dateTime.[[ISOMinute]],
  // dateTime.[[ISOSecond]], dateTime.[[ISOMillisecond]],
  // dateTime.[[ISOMicrosecond]], dateTime.[[ISONanosecond]],
  // dateTime.[[Calendar]], 0, 0, 0, 0, 0, 0, 0, 0, 0, nanoseconds, undefined).
  DateTimeRecord later;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, later,
      AddDateTime(isolate,
                  {{date_time->iso_year(), date_time->iso_month(),
                    date_time->iso_day()},
                   {date_time->iso_hour(), date_time->iso_minute(),
                    date_time->iso_second(), date_time->iso_millisecond(),
                    date_time->iso_microsecond(), date_time->iso_nanosecond()}},
                  direct_handle(date_time->calendar(), isolate),
                  {0, 0, 0, {0, 0, 0, 0, 0, 0, nanoseconds}},
                  isolate->factory()->undefined_value()),
      DirectHandle<JSTemporalInstant>());

  // See https://github.com/tc39/proposal-temporal/issues/1816
  // 16. Let laterDateTime be ? CreateTemporalDateTime(later.[[Year]],
  // later.[[Month]], later.[[Day]], later.[[Hour]], later.[[Minute]],
  // later.[[Second]], later.[[Millisecond]], later.[[Microsecond]],
  // later.[[Nanosecond]], dateTime.[[Calendar]]).

  DirectHandle<JSTemporalPlainDateTime> later_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, later_date_time,
      temporal::CreateTemporalDateTime(
          isolate, later, direct_handle(date_time->calendar(), isolate)));
  // 17. Set possibleInstants to ? GetPossibleInstantsFor(timeZone,
  // laterDateTime).
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, possible_instants,
      GetPossibleInstantsFor(isolate, time_zone, later_date_time));
  // 18. Set n to possibleInstants's length.
  n = possible_instants->length();
  // 19. If n = 0, throw a RangeError exception.
  if (n == 0) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }
  // 20. Return possibleInstants[n − 1].
  DirectHandle<Object> ret_obj(possible_instants->get(n - 1), isolate);
  DCHECK(IsJSTemporalInstant(*ret_obj));
  return Cast<JSTemporalInstant>(ret_obj);
}

// #sec-temporal-gettemporalcalendarwithisodefault
MaybeDirectHandle<JSReceiver> GetTemporalCalendarWithISODefault(
    Isolate* isolate, DirectHandle<JSReceiver> item, const char* method_name) {
  TEMPORAL_ENTER_FUNC();

  Factory* factory = isolate->factory();
  // 1. If item has an [[InitializedTemporalDate]],
  // [[InitializedTemporalDateTime]], [[InitializedTemporalMonthDay]],
  // [[InitializedTemporalTime]], [[InitializedTemporalYearMonth]], or
  // [[InitializedTemporalZonedDateTime]] internal slot, then a. Return
  // item.[[Calendar]].
  if (IsJSTemporalPlainDate(*item)) {
    return direct_handle(Cast<JSTemporalPlainDate>(item)->calendar(), isolate);
  }
  if (IsJSTemporalPlainDateTime(*item)) {
    return direct_handle(Cast<JSTemporalPlainDateTime>(item)->calendar(),
                         isolate);
  }
  if (IsJSTemporalPlainMonthDay(*item)) {
    return direct_handle(Cast<JSTemporalPlainMonthDay>(item)->calendar(),
                         isolate);
  }
  if (IsJSTemporalPlainTime(*item)) {
    return direct_handle(Cast<JSTemporalPlainTime>(item)->calendar(), isolate);
  }
  if (IsJSTemporalPlainYearMonth(*item)) {
    return direct_handle(Cast<JSTemporalPlainYearMonth>(item)->calendar(),
                         isolate);
  }
  if (IsJSTemporalZonedDateTime(*item)) {
    return direct_handle(Cast<JSTemporalZonedDateTime>(item)->calendar(),
                         isolate);
  }

  // 2. Let calendar be ? Get(item, "calendar").
  DirectHandle<Object> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar,
      JSReceiver::GetProperty(isolate, item, factory->calendar_string()));
  // 3. Return ? ToTemporalCalendarWithISODefault(calendar).
  return ToTemporalCalendarWithISODefault(isolate, calendar, method_name);
}

enum class RequiredFields {
  kNone,
  kTimeZone,
  kTimeZoneAndOffset,
  kDay,
  kYearAndDay
};

// The common part of PrepareTemporalFields and PreparePartialTemporalFields
// #sec-temporal-preparetemporalfields
// #sec-temporal-preparepartialtemporalfields
V8_WARN_UNUSED_RESULT MaybeDirectHandle<JSReceiver>
PrepareTemporalFieldsOrPartial(Isolate* isolate,
                               DirectHandle<JSReceiver> fields,
                               DirectHandle<FixedArray> field_names,
                               RequiredFields required, bool partial) {
  TEMPORAL_ENTER_FUNC();

  Factory* factory = isolate->factory();
  // 1. Let result be OrdinaryObjectCreate(null).
  DirectHandle<JSReceiver> result =
      isolate->factory()->NewJSObjectWithNullProto();
  // 2. Let any be false.
  bool any = false;
  // 3. For each value property of fieldNames, do
  int length = field_names->length();
  for (int i = 0; i < length; i++) {
    DirectHandle<Object> property_obj(field_names->get(i), isolate);
    DirectHandle<String> property = Cast<String>(property_obj);
    // a. Let value be ? Get(fields, property).
    DirectHandle<Object> value;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, value, JSReceiver::GetProperty(isolate, fields, property));

    // b. If value is undefined, then
    if (IsUndefined(*value)) {
      // This part is only for PrepareTemporalFields
      // Skip for the case of PreparePartialTemporalFields.
      if (partial) continue;

      // i. If requiredFields contains property, then
      if (((required == RequiredFields::kDay ||
            required == RequiredFields::kYearAndDay) &&
           String::Equals(isolate, property, factory->day_string())) ||
          ((required == RequiredFields::kTimeZone ||
            required == RequiredFields::kTimeZoneAndOffset) &&
           String::Equals(isolate, property, factory->timeZone_string())) ||
          (required == RequiredFields::kTimeZoneAndOffset &&
           String::Equals(isolate, property, factory->offset_string())) ||
          (required == RequiredFields::kYearAndDay &&
           String::Equals(isolate, property, factory->year_string()))) {
        // 1. Throw a TypeError exception.
        THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
      }
      // ii. Else,
      // 1. If property is in the Property column of Table 13, then
      // a. Set value to the corresponding Default value of the same row.
      if (String::Equals(isolate, property, factory->hour_string()) ||
          String::Equals(isolate, property, factory->minute_string()) ||
          String::Equals(isolate, property, factory->second_string()) ||
          String::Equals(isolate, property, factory->millisecond_string()) ||
          String::Equals(isolate, property, factory->microsecond_string()) ||
          String::Equals(isolate, property, factory->nanosecond_string())) {
        value = DirectHandle<Object>(Smi::zero(), isolate);
      }
    } else {
      // For both PrepareTemporalFields and PreparePartialTemporalFields
      any = partial;
      // c. Else,
      // i. If property is in the Property column of Table 13 and there is a
      // Conversion value in the same row, then
      // 1. Let Conversion represent the abstract operation named by the
      // Conversion value of the same row.
      // 2. Set value to ? Conversion(value).
      if (String::Equals(isolate, property, factory->month_string()) ||
          String::Equals(isolate, property, factory->day_string())) {
        ASSIGN_RETURN_ON_EXCEPTION(isolate, value,
                                   ToPositiveInteger(isolate, value));
      } else if (String::Equals(isolate, property, factory->year_string()) ||
                 String::Equals(isolate, property, factory->hour_string()) ||
                 String::Equals(isolate, property, factory->minute_string()) ||
                 String::Equals(isolate, property, factory->second_string()) ||
                 String::Equals(isolate, property,
                                factory->millisecond_string()) ||
                 String::Equals(isolate, property,
                                factory->microsecond_string()) ||
                 String::Equals(isolate, property,
                                factory->nanosecond_string()) ||
                 String::Equals(isolate, property, factory->eraYear_string())) {
        ASSIGN_RETURN_ON_EXCEPTION(isolate, value,
                                   ToIntegerThrowOnInfinity(isolate, value));
      } else if (String::Equals(isolate, property,
                                factory->monthCode_string()) ||
                 String::Equals(isolate, property, factory->offset_string()) ||
                 String::Equals(isolate, property, factory->era_string())) {
        ASSIGN_RETURN_ON_EXCEPTION(isolate, value,
                                   Object::ToString(isolate, value));
      }
    }

    // d. Perform ! CreateDataPropertyOrThrow(result, property, value).
    CHECK(JSReceiver::CreateDataProperty(isolate, result, property, value,
                                         Just(kThrowOnError))
              .FromJust());
  }

  // Only for PreparePartialTemporalFields
  if (partial) {
    // 5. If any is false, then
    if (!any) {
      // a. Throw a TypeError exception.
      THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
    }
  }
  // 4. Return result.
  return result;
}

// #sec-temporal-preparetemporalfields
V8_WARN_UNUSED_RESULT MaybeDirectHandle<JSReceiver> PrepareTemporalFields(
    Isolate* isolate, DirectHandle<JSReceiver> fields,
    DirectHandle<FixedArray> field_names, RequiredFields required) {
  TEMPORAL_ENTER_FUNC();

  return PrepareTemporalFieldsOrPartial(isolate, fields, field_names, required,
                                        false);
}

// #sec-temporal-preparepartialtemporalfields
V8_WARN_UNUSED_RESULT MaybeDirectHandle<JSReceiver>
PreparePartialTemporalFields(Isolate* isolate, DirectHandle<JSReceiver> fields,
                             DirectHandle<FixedArray> field_names) {
  TEMPORAL_ENTER_FUNC();

  return PrepareTemporalFieldsOrPartial(isolate, fields, field_names,
                                        RequiredFields::kNone, true);
}

// Template for DateFromFields, YearMonthFromFields, and MonthDayFromFields
template <typename T>
MaybeDirectHandle<T> FromFields(Isolate* isolate,
                                DirectHandle<JSReceiver> calendar,
                                DirectHandle<JSReceiver> fields,
                                DirectHandle<Object> options,
                                DirectHandle<String> property,
                                InstanceType type) {
  DirectHandle<Object> function;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, function,
                             Object::GetProperty(isolate, calendar, property));
  if (!IsCallable(*function)) {
    THROW_NEW_ERROR(
        isolate, NewTypeError(MessageTemplate::kCalledNonCallable, property));
  }
  DirectHandle<Object> args[] = {fields, options};
  DirectHandle<Object> result;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, result,
      Execution::Call(isolate, function, calendar, base::VectorOf(args)));
  if ((!IsHeapObject(*result)) ||
      Cast<HeapObject>(*result)->map()->instance_type() != type) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }
  return Cast<T>(result);
}

// #sec-temporal-datefromfields
MaybeDirectHandle<JSTemporalPlainDate> DateFromFields(
    Isolate* isolate, DirectHandle<JSReceiver> calendar,
    DirectHandle<JSReceiver> fields, DirectHandle<Object> options) {
  return FromFields<JSTemporalPlainDate>(
      isolate, calendar, fields, options,
      isolate->factory()->dateFromFields_string(), JS_TEMPORAL_PLAIN_DATE_TYPE);
}

// #sec-temporal-yearmonthfromfields
MaybeDirectHandle<JSTemporalPlainYearMonth> YearMonthFromFields(
    Isolate* isolate, DirectHandle<JSReceiver> calendar,
    DirectHandle<JSReceiver> fields, DirectHandle<Object> options) {
  return FromFields<JSTemporalPlainYearMonth>(
      isolate, calendar, fields, options,
      isolate->factory()->yearMonthFromFields_string(),
      JS_TEMPORAL_PLAIN_YEAR_MONTH_TYPE);
}
MaybeDirectHandle<JSTemporalPlainYearMonth> YearMonthFromFields(
    Isolate* isolate, DirectHandle<JSReceiver> calendar,
    DirectHandle<JSReceiver> fields) {
  // 1. If options is not present, set options to undefined.
  return YearMonthFromFields(isolate, calendar, fields,
                             isolate->factory()->undefined_value());
}

// #sec-temporal-monthdayfromfields
MaybeDirectHandle<JSTemporalPlainMonthDay> MonthDayFromFields(
    Isolate* isolate, DirectHandle<JSReceiver> calendar,
    DirectHandle<JSReceiver> fields, DirectHandle<Object> options) {
  return FromFields<JSTemporalPlainMonthDay>(
      isolate, calendar, fields, options,
      isolate->factory()->monthDayFromFields_string(),
      JS_TEMPORAL_PLAIN_MONTH_DAY_TYPE);
}
MaybeDirectHandle<JSTemporalPlainMonthDay> MonthDayFromFields(
    Isolate* isolate, DirectHandle<JSReceiver> calendar,
    DirectHandle<JSReceiver> fields) {
  // 1. If options is not present, set options to undefined.
  return MonthDayFromFields(isolate, calendar, fields,
                            isolate->factory()->undefined_value());
}

// #sec-temporal-totemporaloverflow
Maybe<ShowOverflow> ToTemporalOverflow(Isolate* isolate,
                                       DirectHandle<Object> options,
                                       const char* method_name) {
  // 1. If options is undefined, return "constrain".
  if (IsUndefined(*options)) return Just(ShowOverflow::kConstrain);
  DCHECK(IsJSReceiver(*options));
  // 2. Return ? GetOption(options, "overflow", « String », « "constrain",
  // "reject" », "constrain").
  return GetStringOption<ShowOverflow>(
      isolate, Cast<JSReceiver>(options), "overflow", method_name,
      {"constrain", "reject"},
      {ShowOverflow::kConstrain, ShowOverflow::kReject},
      ShowOverflow::kConstrain);
}

// #sec-temporal-totemporaloffset
Maybe<Offset> ToTemporalOffset(Isolate* isolate, DirectHandle<Object> options,
                               Offset fallback, const char* method_name) {
  // 1. If options is undefined, return fallback.
  if (IsUndefined(*options)) return Just(fallback);
  DCHECK(IsJSReceiver(*options));

  // 2. Return ? GetOption(options, "offset", « String », « "prefer", "use",
  // "ignore", "reject" », fallback).
  return GetStringOption<Offset>(
      isolate, Cast<JSReceiver>(options), "offset", method_name,
      {"prefer", "use", "ignore", "reject"},
      {Offset::kPrefer, Offset::kUse, Offset::kIgnore, Offset::kReject},
      fallback);
}

// #sec-temporal-totemporaldisambiguation
Maybe<Disambiguation> ToTemporalDisambiguation(Isolate* isolate,
                                               DirectHandle<Object> options,
                                               const char* method_name) {
  // 1. If options is undefined, return "compatible".
  if (IsUndefined(*options)) return Just(Disambiguation::kCompatible);
  DCHECK(IsJSReceiver(*options));
  // 2. Return ? GetOption(options, "disambiguation", « String », «
  // "compatible", "earlier", "later", "reject" », "compatible").
  return GetStringOption<Disambiguation>(
      isolate, Cast<JSReceiver>(options), "disambiguation", method_name,
      {"compatible", "earlier", "later", "reject"},
      {Disambiguation::kCompatible, Disambiguation::kEarlier,
       Disambiguation::kLater, Disambiguation::kReject},
      Disambiguation::kCompatible);
}

// #sec-temporal-builtintimezonegetinstantfor
MaybeDirectHandle<JSTemporalInstant> BuiltinTimeZoneGetInstantFor(
    Isolate* isolate, DirectHandle<JSReceiver> time_zone,
    DirectHandle<JSTemporalPlainDateTime> date_time,
    Disambiguation disambiguation, const char* method_name) {
  TEMPORAL_ENTER_FUNC();
  // 1. Assert: dateTime has an [[InitializedTemporalDateTime]] internal slot.
  // 2. Let possibleInstants be ? GetPossibleInstantsFor(timeZone, dateTime).
  DirectHandle<FixedArray> possible_instants;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, possible_instants,
      GetPossibleInstantsFor(isolate, time_zone, date_time));
  // 3. Return ? DisambiguatePossibleInstants(possibleInstants, timeZone,
  // dateTime, disambiguation).
  return DisambiguatePossibleInstants(isolate, possible_instants, time_zone,
                                      date_time, disambiguation, method_name);
}

// #sec-temporal-totemporalinstant
MaybeDirectHandle<JSTemporalInstant> ToTemporalInstant(
    Isolate* isolate, DirectHandle<Object> item, const char* method_name) {
  TEMPORAL_ENTER_FUNC();

  // 1. If Type(item) is Object, then
  // a. If item has an [[InitializedTemporalInstant]] internal slot, then
  if (IsJSTemporalInstant(*item)) {
    // i. Return item.
    return Cast<JSTemporalInstant>(item);
  }
  // b. If item has an [[InitializedTemporalZonedDateTime]] internal slot, then
  if (IsJSTemporalZonedDateTime(*item)) {
    // i. Return ! CreateTemporalInstant(item.[[Nanoseconds]]).
    DirectHandle<BigInt> nanoseconds(
        Cast<JSTemporalZonedDateTime>(*item)->nanoseconds(), isolate);
    return temporal::CreateTemporalInstant(isolate, nanoseconds)
        .ToHandleChecked();
  }
  // 2. Let string be ? ToString(item).
  DirectHandle<String> string;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, string, Object::ToString(isolate, item));

  // 3. Let epochNanoseconds be ? ParseTemporalInstant(string).
  DirectHandle<BigInt> epoch_nanoseconds;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, epoch_nanoseconds,
                             ParseTemporalInstant(isolate, string));

  // 4. Return ? CreateTemporalInstant(ℤ(epochNanoseconds)).
  return temporal::CreateTemporalInstant(isolate, epoch_nanoseconds);
}

}  // namespace

namespace temporal {
// #sec-temporal-totemporalcalendar
MaybeDirectHandle<JSReceiver> ToTemporalCalendar(
    Isolate* isolate, DirectHandle<Object> temporal_calendar_like,
    const char* method_name) {
  Factory* factory = isolate->factory();
  // 1.If Type(temporalCalendarLike) is Object, then
  if (IsJSReceiver(*temporal_calendar_like)) {
    // a. If temporalCalendarLike has an [[InitializedTemporalDate]],
    // [[InitializedTemporalDateTime]], [[InitializedTemporalMonthDay]],
    // [[InitializedTemporalTime]], [[InitializedTemporalYearMonth]], or
    // [[InitializedTemporalZonedDateTime]] internal slot, then i. Return
    // temporalCalendarLike.[[Calendar]].

#define EXTRACT_CALENDAR(T, obj)                                         \
  if (IsJSTemporal##T(*obj)) {                                           \
    return direct_handle(Cast<JSTemporal##T>(obj)->calendar(), isolate); \
  }

    EXTRACT_CALENDAR(PlainDate, temporal_calendar_like)
    EXTRACT_CALENDAR(PlainDateTime, temporal_calendar_like)
    EXTRACT_CALENDAR(PlainMonthDay, temporal_calendar_like)
    EXTRACT_CALENDAR(PlainTime, temporal_calendar_like)
    EXTRACT_CALENDAR(PlainYearMonth, temporal_calendar_like)
    EXTRACT_CALENDAR(ZonedDateTime, temporal_calendar_like)

#undef EXTRACT_CALENDAR
    DirectHandle<JSReceiver> obj = Cast<JSReceiver>(temporal_calendar_like);

    // b. If ? HasProperty(temporalCalendarLike, "calendar") is false, return
    // temporalCalendarLike.
    bool has;
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, has,
        JSReceiver::HasProperty(isolate, obj, factory->calendar_string()),
        DirectHandle<JSReceiver>());
    if (!has) {
      return obj;
    }
    // c.  Set temporalCalendarLike to ? Get(temporalCalendarLike, "calendar").
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, temporal_calendar_like,
        JSReceiver::GetProperty(isolate, obj, factory->calendar_string()));
    // d. If Type(temporalCalendarLike) is Object
    if (IsJSReceiver(*temporal_calendar_like)) {
      obj = Cast<JSReceiver>(temporal_calendar_like);
      // and ? HasProperty(temporalCalendarLike, "calendar") is false,
      MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, has,
          JSReceiver::HasProperty(isolate, obj, factory->calendar_string()),
          DirectHandle<JSReceiver>());
      if (!has) {
        // return temporalCalendarLike.
        return obj;
      }
    }
  }

  // 2. Let identifier be ? ToString(temporalCalendarLike).
  DirectHandle<String> identifier;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, identifier,
                             Object::ToString(isolate, temporal_calendar_like));
  // 3. Let identifier be ? ParseTemporalCalendarString(identifier).
  ASSIGN_RETURN_ON_EXCEPTION(isolate, identifier,
                             ParseTemporalCalendarString(isolate, identifier));
  // 4. If IsBuiltinCalendar(identifier) is false, throw a RangeError
  // exception.
  if (!IsBuiltinCalendar(isolate, identifier)) {
    THROW_NEW_ERROR(
        isolate, NewRangeError(MessageTemplate::kInvalidCalendar, identifier));
  }
  // 5. Return ? CreateTemporalCalendar(identifier).
  return CreateTemporalCalendar(isolate, identifier);
}

}  // namespace temporal

namespace {
// #sec-temporal-totemporalcalendarwithisodefault
MaybeDirectHandle<JSReceiver> ToTemporalCalendarWithISODefault(
    Isolate* isolate, DirectHandle<Object> temporal_calendar_like,
    const char* method_name) {
  TEMPORAL_ENTER_FUNC();

  // 1. If temporalCalendarLike is undefined, then
  if (IsUndefined(*temporal_calendar_like)) {
    // a. Return ? GetISO8601Calendar().
    return temporal::GetISO8601Calendar(isolate);
  }
  // 2. Return ? ToTemporalCalendar(temporalCalendarLike).
  return temporal::ToTemporalCalendar(isolate, temporal_calendar_like,
                                      method_name);
}

// Create «  "day", "hour", "microsecond", "millisecond", "minute", "month",
// "monthCode", "nanosecond", "second", "year" » in several AOs.
DirectHandle<FixedArray> All10UnitsInFixedArray(Isolate* isolate) {
  DirectHandle<FixedArray> field_names = isolate->factory()->NewFixedArray(10);
  field_names->set(0, ReadOnlyRoots(isolate).day_string());
  field_names->set(1, ReadOnlyRoots(isolate).hour_string());
  field_names->set(2, ReadOnlyRoots(isolate).microsecond_string());
  field_names->set(3, ReadOnlyRoots(isolate).millisecond_string());
  field_names->set(4, ReadOnlyRoots(isolate).minute_string());
  field_names->set(5, ReadOnlyRoots(isolate).month_string());
  field_names->set(6, ReadOnlyRoots(isolate).monthCode_string());
  field_names->set(7, ReadOnlyRoots(isolate).nanosecond_string());
  field_names->set(8, ReadOnlyRoots(isolate).second_string());
  field_names->set(9, ReadOnlyRoots(isolate).year_string());
  return field_names;
}

// Create « "day", "month", "monthCode", "year" » in several AOs.
DirectHandle<FixedArray> DayMonthMonthCodeYearInFixedArray(Isolate* isolate) {
  DirectHandle<FixedArray> field_names = isolate->factory()->NewFixedArray(4);
  field_names->set(0, ReadOnlyRoots(isolate).day_string());
  field_names->set(1, ReadOnlyRoots(isolate).month_string());
  field_names->set(2, ReadOnlyRoots(isolate).monthCode_string());
  field_names->set(3, ReadOnlyRoots(isolate).year_string());
  return field_names;
}

// Create « "month", "monthCode", "year" » in several AOs.
DirectHandle<FixedArray> MonthMonthCodeYearInFixedArray(Isolate* isolate) {
  DirectHandle<FixedArray> field_names = isolate->factory()->NewFixedArray(3);
  field_names->set(0, ReadOnlyRoots(isolate).month_string());
  field_names->set(1, ReadOnlyRoots(isolate).monthCode_string());
  field_names->set(2, ReadOnlyRoots(isolate).year_string());
  return field_names;
}

// Create « "monthCode", "year" » in several AOs.
DirectHandle<FixedArray> MonthCodeYearInFixedArray(Isolate* isolate) {
  DirectHandle<FixedArray> field_names = isolate->factory()->NewFixedArray(2);
  field_names->set(0, ReadOnlyRoots(isolate).monthCode_string());
  field_names->set(1, ReadOnlyRoots(isolate).year_string());
  return field_names;
}

// #sec-temporal-totemporaldate
MaybeDirectHandle<JSTemporalPlainDate> ToTemporalDate(
    Isolate* isolate, DirectHandle<Object> item_obj,
    DirectHandle<Object> options, const char* method_name) {
  TEMPORAL_ENTER_FUNC();

  // 2. Assert: Type(options) is Object or Undefined.
  DCHECK(IsJSReceiver(*options) || IsUndefined(*options));
  // 3. If Type(item) is Object, then
  if (IsJSReceiver(*item_obj)) {
    DirectHandle<JSReceiver> item = Cast<JSReceiver>(item_obj);
    // a. If item has an [[InitializedTemporalDate]] internal slot, then
    // i. Return item.
    if (IsJSTemporalPlainDate(*item)) {
      return Cast<JSTemporalPlainDate>(item);
    }
    // b. If item has an [[InitializedTemporalZonedDateTime]] internal slot,
    // then
    if (IsJSTemporalZonedDateTime(*item)) {
      // i. Perform ? ToTemporalOverflow(options).
      MAYBE_RETURN_ON_EXCEPTION_VALUE(
          isolate, ToTemporalOverflow(isolate, options, method_name),
          DirectHandle<JSTemporalPlainDate>());

      // ii. Let instant be ! CreateTemporalInstant(item.[[Nanoseconds]]).
      auto zoned_date_time = Cast<JSTemporalZonedDateTime>(item);
      DirectHandle<JSTemporalInstant> instant =
          temporal::CreateTemporalInstant(
              isolate, direct_handle(zoned_date_time->nanoseconds(), isolate))
              .ToHandleChecked();
      // iii. Let plainDateTime be ?
      // BuiltinTimeZoneGetPlainDateTimeFor(item.[[TimeZone]],
      // instant, item.[[Calendar]]).
      DirectHandle<JSTemporalPlainDateTime> plain_date_time;
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, plain_date_time,
          temporal::BuiltinTimeZoneGetPlainDateTimeFor(
              isolate,
              DirectHandle<JSReceiver>(zoned_date_time->time_zone(), isolate),
              instant,
              DirectHandle<JSReceiver>(zoned_date_time->calendar(), isolate),
              method_name));
      // iv. Return ! CreateTemporalDate(plainDateTime.[[ISOYear]],
      // plainDateTime.[[ISOMonth]], plainDateTime.[[ISODay]],
      // plainDateTime.[[Calendar]]).
      return CreateTemporalDate(
                 isolate,
                 {plain_date_time->iso_year(), plain_date_time->iso_month(),
                  plain_date_time->iso_day()},
                 direct_handle(plain_date_time->calendar(), isolate))
          .ToHandleChecked();
    }

    // c. If item has an [[InitializedTemporalDateTime]] internal slot, then
    // item.[[ISODay]], item.[[Calendar]]).
    if (IsJSTemporalPlainDateTime(*item)) {
      // i. Perform ? ToTemporalOverflow(options).
      MAYBE_RETURN_ON_EXCEPTION_VALUE(
          isolate, ToTemporalOverflow(isolate, options, method_name),
          DirectHandle<JSTemporalPlainDate>());
      // ii. Return ! CreateTemporalDate(item.[[ISOYear]], item.[[ISOMonth]],
      auto date_time = Cast<JSTemporalPlainDateTime>(item);
      return CreateTemporalDate(isolate,
                                {date_time->iso_year(), date_time->iso_month(),
                                 date_time->iso_day()},
                                direct_handle(date_time->calendar(), isolate))
          .ToHandleChecked();
    }

    // d. Let calendar be ? GetTemporalCalendarWithISODefault(item).
    DirectHandle<JSReceiver> calendar;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, calendar,
        GetTemporalCalendarWithISODefault(isolate, item, method_name));
    // e. Let fieldNames be ? CalendarFields(calendar, « "day", "month",
    // "monthCode", "year" »).
    DirectHandle<FixedArray> field_names =
        DayMonthMonthCodeYearInFixedArray(isolate);
    ASSIGN_RETURN_ON_EXCEPTION(isolate, field_names,
                               CalendarFields(isolate, calendar, field_names));
    // f. Let fields be ? PrepareTemporalFields(item,
    // fieldNames, «»).
    DirectHandle<JSReceiver> fields;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, fields,
                               PrepareTemporalFields(isolate, item, field_names,
                                                     RequiredFields::kNone));
    // g. Return ? DateFromFields(calendar, fields, options).
    return DateFromFields(isolate, calendar, fields, options);
  }
  // 4. Perform ? ToTemporalOverflow(options).
  MAYBE_RETURN_ON_EXCEPTION_VALUE(
      isolate, ToTemporalOverflow(isolate, options, method_name),
      DirectHandle<JSTemporalPlainDate>());

  // 5. Let string be ? ToString(item).
  DirectHandle<String> string;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, string,
                             Object::ToString(isolate, item_obj));
  // 6. Let result be ? ParseTemporalDateString(string).
  DateRecordWithCalendar result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result, ParseTemporalDateString(isolate, string),
      DirectHandle<JSTemporalPlainDate>());

  // 7. Assert: ! IsValidISODate(result.[[Year]], result.[[Month]],
  // result.[[Day]]) is true.
  DCHECK(IsValidISODate(isolate, result.date));
  // 8. Let calendar be ? ToTemporalCalendarWithISODefault(result.[[Calendar]]).
  DirectHandle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar,
      ToTemporalCalendarWithISODefault(isolate, result.calendar, method_name));
  // 9. Return ? CreateTemporalDate(result.[[Year]], result.[[Month]],
  // result.[[Day]], calendar).
  return CreateTemporalDate(isolate, result.date, calendar);
}

MaybeDirectHandle<JSTemporalPlainDate> ToTemporalDate(
    Isolate* isolate, DirectHandle<Object> item_obj, const char* method_name) {
  // 1. If options is not present, set options to undefined.
  return ToTemporalDate(isolate, item_obj,
                        isolate->factory()->undefined_value(), method_name);
}

// #sec-isintegralnumber
bool IsIntegralNumber(Isolate* isolate, DirectHandle<Object> argument) {
  // 1. If Type(argument) is not Number, return false.
  if (!IsNumber(*argument)) return false;
  // 2. If argument is NaN, +∞𝔽, or -∞𝔽, return false.
  double number = Object::NumberValue(Cast<Number>(*argument));
  if (!std::isfinite(number)) return false;
  // 3. If floor(abs(ℝ(argument))) ≠ abs(ℝ(argument)), return false.
  if (std::floor(std::abs(number)) != std::abs(number)) return false;
  // 4. Return true.
  return true;
}

// #sec-temporal-tointegerwithoutrounding
Maybe<double> ToIntegerWithoutRounding(Isolate* isolate,
                                       DirectHandle<Object> argument) {
  // 1. Let number be ? ToNumber(argument).
  DirectHandle<Number> number;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, number, Object::ToNumber(isolate, argument), Nothing<double>());
  // 2. If number is NaN, +0𝔽, or −0𝔽 return 0.
  if (IsNaN(*number) || Object::NumberValue(*number) == 0) {
    return Just(static_cast<double>(0));
  }
  // 3. If IsIntegralNumber(number) is false, throw a RangeError exception.
  if (!IsIntegralNumber(isolate, number)) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(), Nothing<double>());
  }
  // 4. Return ℝ(number).
  return Just(Object::NumberValue(*number));
}

}  // namespace

namespace temporal {

// #sec-temporal-regulatetime
Maybe<TimeRecord> RegulateTime(Isolate* isolate, const TimeRecord& time,
                               ShowOverflow overflow) {
  TEMPORAL_ENTER_FUNC();

  // 1. Assert: hour, minute, second, millisecond, microsecond and nanosecond
  // are integers.
  // 2. Assert: overflow is either "constrain" or "reject".
  switch (overflow) {
    case ShowOverflow::kConstrain: {
      TimeRecord result(time);
      // 3. If overflow is "constrain", then
      // a. Return ! ConstrainTime(hour, minute, second, millisecond,
      // microsecond, nanosecond).
      result.hour = std::max(std::min(result.hour, 23), 0);
      result.minute = std::max(std::min(result.minute, 59), 0);
      result.second = std::max(std::min(result.second, 59), 0);
      result.millisecond = std::max(std::min(result.millisecond, 999), 0);
      result.microsecond = std::max(std::min(result.microsecond, 999), 0);
      result.nanosecond = std::max(std::min(result.nanosecond, 999), 0);
      return Just(result);
    }
    case ShowOverflow::kReject:
      // 4. If overflow is "reject", then
      // a. If ! IsValidTime(hour, minute, second, millisecond, microsecond,
      // nanosecond) is false, throw a RangeError exception.
      if (!IsValidTime(isolate, time)) {
        THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                     NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                     Nothing<TimeRecord>());
      }
      // b. Return the new Record { [[Hour]]: hour, [[Minute]]: minute,
      // [[Second]]: second, [[Millisecond]]: millisecond, [[Microsecond]]:
      // microsecond, [[Nanosecond]]: nanosecond }.
      return Just(time);
  }
}

// #sec-temporal-totemporaltime
MaybeDirectHandle<JSTemporalPlainTime> ToTemporalTime(
    Isolate* isolate, DirectHandle<Object> item_obj, const char* method_name,
    ShowOverflow overflow = ShowOverflow::kConstrain) {
  Factory* factory = isolate->factory();
  TimeRecordWithCalendar result;
  // 2. Assert: overflow is either "constrain" or "reject".
  // 3. If Type(item) is Object, then
  if (IsJSReceiver(*item_obj)) {
    DirectHandle<JSReceiver> item = Cast<JSReceiver>(item_obj);
    // a. If item has an [[InitializedTemporalTime]] internal slot, then
    // i. Return item.
    if (IsJSTemporalPlainTime(*item)) {
      return Cast<JSTemporalPlainTime>(item);
    }
    // b. If item has an [[InitializedTemporalZonedDateTime]] internal slot,
    // then
    if (IsJSTemporalZonedDateTime(*item)) {
      // i. Let instant be ! CreateTemporalInstant(item.[[Nanoseconds]]).
      auto zoned_date_time = Cast<JSTemporalZonedDateTime>(item);
      DirectHandle<JSTemporalInstant> instant =
          CreateTemporalInstant(
              isolate, direct_handle(zoned_date_time->nanoseconds(), isolate))
              .ToHandleChecked();
      // ii. Set plainDateTime to ?
      // BuiltinTimeZoneGetPlainDateTimeFor(item.[[TimeZone]],
      // instant, item.[[Calendar]]).
      DirectHandle<JSTemporalPlainDateTime> plain_date_time;
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, plain_date_time,
          BuiltinTimeZoneGetPlainDateTimeFor(
              isolate,
              DirectHandle<JSReceiver>(zoned_date_time->time_zone(), isolate),
              instant,
              DirectHandle<JSReceiver>(zoned_date_time->calendar(), isolate),
              method_name));
      // iii. Return !
      // CreateTemporalTime(plainDateTime.[[ISOHour]],
      // plainDateTime.[[ISOMinute]], plainDateTime.[[ISOSecond]],
      // plainDateTime.[[ISOMillisecond]], plainDateTime.[[ISOMicrosecond]],
      // plainDateTime.[[ISONanosecond]]).
      return CreateTemporalTime(isolate, {plain_date_time->iso_hour(),
                                          plain_date_time->iso_minute(),
                                          plain_date_time->iso_second(),
                                          plain_date_time->iso_millisecond(),
                                          plain_date_time->iso_microsecond(),
                                          plain_date_time->iso_nanosecond()})
          .ToHandleChecked();
    }
    // c. If item has an [[InitializedTemporalDateTime]] internal slot, then
    if (IsJSTemporalPlainDateTime(*item)) {
      // i. Return ! CreateTemporalTime(item.[[ISOHour]], item.[[ISOMinute]],
      // item.[[ISOSecond]], item.[[ISOMillisecond]], item.[[ISOMicrosecond]],
      // item.[[ISONanosecond]]).
      auto date_time = Cast<JSTemporalPlainDateTime>(item);
      return CreateTemporalTime(
                 isolate,
                 {date_time->iso_hour(), date_time->iso_minute(),
                  date_time->iso_second(), date_time->iso_millisecond(),
                  date_time->iso_microsecond(), date_time->iso_nanosecond()})
          .ToHandleChecked();
    }
    // d. Let calendar be ? GetTemporalCalendarWithISODefault(item).
    DirectHandle<JSReceiver> calendar;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, calendar,
        GetTemporalCalendarWithISODefault(isolate, item, method_name));
    // e. If ? ToString(calendar) is not "iso8601", then
    DirectHandle<String> identifier;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, identifier,
                               Object::ToString(isolate, calendar));
    if (!String::Equals(isolate, factory->iso8601_string(), identifier)) {
      // i. Throw a RangeError exception.
      THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
    }
    // f. Let result be ? ToTemporalTimeRecord(item).
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, result.time, ToTemporalTimeRecord(isolate, item, method_name),
        DirectHandle<JSTemporalPlainTime>());
    // g. Set result to ? RegulateTime(result.[[Hour]], result.[[Minute]],
    // result.[[Second]], result.[[Millisecond]], result.[[Microsecond]],
    // result.[[Nanosecond]], overflow).
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, result.time, RegulateTime(isolate, result.time, overflow),
        DirectHandle<JSTemporalPlainTime>());
  } else {
    // 4. Else,
    // a. Let string be ? ToString(item).
    DirectHandle<String> string;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, string,
                               Object::ToString(isolate, item_obj));
    // b. Let result be ? ParseTemporalTimeString(string).
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, result, ParseTemporalTimeString(isolate, string),
        DirectHandle<JSTemporalPlainTime>());
    // c. Assert: ! IsValidTime(result.[[Hour]], result.[[Minute]],
    // result.[[Second]], result.[[Millisecond]], result.[[Microsecond]],
    // result.[[Nanosecond]]) is true.
    DCHECK(IsValidTime(isolate, result.time));
    // d. If result.[[Calendar]] is not one of undefined or "iso8601", then
    DCHECK(IsUndefined(*result.calendar) || IsString(*result.calendar));
    if (!IsUndefined(*result.calendar) &&
        !String::Equals(isolate, Cast<String>(result.calendar),
                        isolate->factory()->iso8601_string())) {
      // i. Throw a RangeError exception.
      THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
    }
  }
  // 5. Return ? CreateTemporalTime(result.[[Hour]], result.[[Minute]],
  // result.[[Second]], result.[[Millisecond]], result.[[Microsecond]],
  // result.[[Nanosecond]]).
  return CreateTemporalTime(isolate, result.time);
}

// Helper function to loop through Table 8 Duration Record Fields
// This function implement
// "For each row of Table 8, except the header row, in table order, do"
// loop. It is designed to be used to implement the common part of
// ToPartialDuration, ToTemporalDurationRecord
Maybe<bool> IterateDurationRecordFieldsTable(
    Isolate* isolate, DirectHandle<JSReceiver> temporal_duration_like,
    Maybe<bool> (*RowFunction)(Isolate*,
                               DirectHandle<JSReceiver> temporal_duration_like,
                               DirectHandle<String>, double*),
    DurationRecord* record) {
  Factory* factory = isolate->factory();
  std::array<std::pair<DirectHandle<String>, double*>, 10> table8 = {
      {{factory->days_string(), &record->time_duration.days},
       {factory->hours_string(), &record->time_duration.hours},
       {factory->microseconds_string(), &record->time_duration.microseconds},
       {factory->milliseconds_string(), &record->time_duration.milliseconds},
       {factory->minutes_string(), &record->time_duration.minutes},
       {factory->months_string(), &record->months},
       {factory->nanoseconds_string(), &record->time_duration.nanoseconds},
       {factory->seconds_string(), &record->time_duration.seconds},
       {factory->weeks_string(), &record->weeks},
       {factory->years_string(), &record->years}}};

  // x. Let any be false.
  bool any = false;
  // x+1. For each row of Table 8, except the header row, in table order, do
  for (const auto& row : table8) {
    bool result;
    // row.first is prop: the Property Name value of the current row
    // row.second is the address of result's field whose name is the Field Name
    // value of the current row
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, result,
        RowFunction(isolate, temporal_duration_like, row.first, row.second),
        Nothing<bool>());
    any |= result;
  }
  return Just(any);
}

// #sec-temporal-totemporaldurationrecord
Maybe<DurationRecord> ToTemporalDurationRecord(
    Isolate* isolate, DirectHandle<Object> temporal_duration_like_obj,
    const char* method_name) {
  TEMPORAL_ENTER_FUNC();

  // 1. If Type(temporalDurationLike) is not Object, then
  if (!IsJSReceiver(*temporal_duration_like_obj)) {
    // a. Let string be ? ToString(temporalDurationLike).
    DirectHandle<String> string;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, string, Object::ToString(isolate, temporal_duration_like_obj),
        Nothing<DurationRecord>());
    // b. Let result be ? ParseTemporalDurationString(string).
    return ParseTemporalDurationString(isolate, string);
  }
  DirectHandle<JSReceiver> temporal_duration_like =
      Cast<JSReceiver>(temporal_duration_like_obj);
  // 2. If temporalDurationLike has an [[InitializedTemporalDuration]] internal
  // slot, then
  if (IsJSTemporalDuration(*temporal_duration_like)) {
    // a. Return ! CreateDurationRecord(temporalDurationLike.[[Years]],
    // temporalDurationLike.[[Months]], temporalDurationLike.[[Weeks]],
    // temporalDurationLike.[[Days]], temporalDurationLike.[[Hours]],
    // temporalDurationLike.[[Minutes]], temporalDurationLike.[[Seconds]],
    // temporalDurationLike.[[Milliseconds]],
    // temporalDurationLike.[[Microseconds]],
    // temporalDurationLike.[[Nanoseconds]]).
    auto duration = Cast<JSTemporalDuration>(temporal_duration_like);
    return DurationRecord::Create(isolate,
                                  Object::NumberValue(duration->years()),
                                  Object::NumberValue(duration->months()),
                                  Object::NumberValue(duration->weeks()),
                                  Object::NumberValue(duration->days()),
                                  Object::NumberValue(duration->hours()),
                                  Object::NumberValue(duration->minutes()),
                                  Object::NumberValue(duration->seconds()),
                                  Object::NumberValue(duration->milliseconds()),
                                  Object::NumberValue(duration->microseconds()),
                                  Object::NumberValue(duration->nanoseconds()));
  }
  // 3. Let result be a new Record with all the internal slots given in the
  // Internal Slot column in Table 8.
  DurationRecord result;
  // 4. Let any be false.
  bool any = false;

  // 5. For each row of Table 8, except the header row, in table order, do
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, any,
      IterateDurationRecordFieldsTable(
          isolate, temporal_duration_like,
          [](Isolate* isolate, DirectHandle<JSReceiver> temporal_duration_like,
             DirectHandle<String> prop, double* field) -> Maybe<bool> {
            bool not_undefined = false;
            // a. Let prop be the Property value of the current row.
            DirectHandle<Object> val;
            // b. Let val be ? Get(temporalDurationLike, prop).
            ASSIGN_RETURN_ON_EXCEPTION_VALUE(
                isolate, val,
                JSReceiver::GetProperty(isolate, temporal_duration_like, prop),
                Nothing<bool>());
            // c. If val is undefined, then
            if (IsUndefined(*val)) {
              // i. Set result's internal slot whose name is the Internal Slot
              // value of the current row to 0.
              *field = 0;
              // d. Else,
            } else {
              // i. Set any to true.
              not_undefined = true;
              // ii. Let val be 𝔽(? ToIntegerWithoutRounding(val)).
              // iii. Set result's field whose name is the Field Name value of
              // the current row to val.
              MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
                  isolate, *field, ToIntegerWithoutRounding(isolate, val),
                  Nothing<bool>());
            }
            return Just(not_undefined);
          },
          &result),
      Nothing<DurationRecord>());

  // 6. If any is false, then
  if (!any) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR(),
                                 Nothing<DurationRecord>());
  }
  // 7. If ! IsValidDuration(result.[[Years]], result.[[Months]],
  // result.[[Weeks]] result.[[Days]], result.[[Hours]], result.[[Minutes]],
  // result.[[Seconds]], result.[[Milliseconds]], result.[[Microseconds]],
  // result.[[Nanoseconds]]) is false, then
  if (!IsValidDuration(isolate, result)) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<DurationRecord>());
  }
  // 8. Return result.
  return Just(result);
}

// #sec-temporal-totemporalduration
MaybeDirectHandle<JSTemporalDuration> ToTemporalDuration(
    Isolate* isolate, DirectHandle<Object> item, const char* method_name) {
  TEMPORAL_ENTER_FUNC();

  DurationRecord result;
  // 1. If Type(item) is Object and item has an [[InitializedTemporalDuration]]
  // internal slot, then
  if (IsJSTemporalDuration(*item)) {
    // a. Return item.
    return Cast<JSTemporalDuration>(item);
  }
  // 2. Let result be ? ToTemporalDurationRecord(item).
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result, ToTemporalDurationRecord(isolate, item, method_name),
      DirectHandle<JSTemporalDuration>());

  // 3. Return ? CreateTemporalDuration(result.[[Years]], result.[[Months]],
  // result.[[Weeks]], result.[[Days]], result.[[Hours]], result.[[Minutes]],
  // result.[[Seconds]], result.[[Milliseconds]], result.[[Microseconds]],
  // result.[[Nanoseconds]]).
  return CreateTemporalDuration(isolate, result);
}

// #sec-temporal-totemporaltimezone
MaybeDirectHandle<JSReceiver> ToTemporalTimeZone(
    Isolate* isolate, DirectHandle<Object> temporal_time_zone_like,
    const char* method_name) {
  TEMPORAL_ENTER_FUNC();

  Factory* factory = isolate->factory();
  // 1. If Type(temporalTimeZoneLike) is Object, then
  if (IsJSReceiver(*temporal_time_zone_like)) {
    // a. If temporalTimeZoneLike has an [[InitializedTemporalZonedDateTime]]
    // internal slot, then
    if (IsJSTemporalZonedDateTime(*temporal_time_zone_like)) {
      // i. Return temporalTimeZoneLike.[[TimeZone]].
      auto zoned_date_time =
          Cast<JSTemporalZonedDateTime>(temporal_time_zone_like);
      return direct_handle(zoned_date_time->time_zone(), isolate);
    }
    DirectHandle<JSReceiver> obj = Cast<JSReceiver>(temporal_time_zone_like);
    // b. If ? HasProperty(temporalTimeZoneLike, "timeZone") is false,
    bool has;
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, has,
        JSReceiver::HasProperty(isolate, obj, factory->timeZone_string()),
        DirectHandle<JSReceiver>());
    if (!has) {
      // return temporalTimeZoneLike.
      return obj;
    }
    // c. Set temporalTimeZoneLike to ?
    // Get(temporalTimeZoneLike, "timeZone").
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, temporal_time_zone_like,
        JSReceiver::GetProperty(isolate, obj, factory->timeZone_string()));
    // d. If Type(temporalTimeZoneLike)
    if (IsJSReceiver(*temporal_time_zone_like)) {
      // is Object and ? HasProperty(temporalTimeZoneLike, "timeZone") is false,
      obj = Cast<JSReceiver>(temporal_time_zone_like);
      MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, has,
          JSReceiver::HasProperty(isolate, obj, factory->timeZone_string()),
          DirectHandle<JSReceiver>());
      if (!has) {
        // return temporalTimeZoneLike.
        return obj;
      }
    }
  }
  DirectHandle<String> identifier;
  // 2. Let identifier be ? ToString(temporalTimeZoneLike).
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, identifier, Object::ToString(isolate, temporal_time_zone_like));

  // 3. Let parseResult be ? ParseTemporalTimeZoneString(identifier).
  TimeZoneRecord parse_result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, parse_result, ParseTemporalTimeZoneString(isolate, identifier),
      DirectHandle<JSReceiver>());

  // 4. If parseResult.[[Name]] is not undefined, then
  if (!IsUndefined(*parse_result.name)) {
    DCHECK(IsString(*parse_result.name));
    // a. Let name be parseResult.[[Name]].
    DirectHandle<String> name = Cast<String>(parse_result.name);
    // b. If ParseText(StringToCodePoints(name, TimeZoneNumericUTCOffset)) is
    // a List of errors, then
    std::optional<ParsedISO8601Result> parsed_offset =
        TemporalParser::ParseTimeZoneNumericUTCOffset(isolate, name);
    if (!parsed_offset.has_value()) {
      // i. If ! IsValidTimeZoneName(name) is false, throw a RangeError
      // exception.
      if (!IsValidTimeZoneName(isolate, name)) {
        THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
      }
      // ii. Set name to ! CanonicalizeTimeZoneName(name).
      name = CanonicalizeTimeZoneName(isolate, name);
    }
    // c. Return ! CreateTemporalTimeZone(name).
    return temporal::CreateTemporalTimeZone(isolate, name);
  }
  // 5. If parseResult.[[Z]] is true, return ! CreateTemporalTimeZone("UTC").
  if (parse_result.z) {
    return CreateTemporalTimeZoneUTC(isolate);
  }
  // 6. Return ! CreateTemporalTimeZone(parseResult.[[OffsetString]]).
  DCHECK(IsString(*parse_result.offset_string));
  return temporal::CreateTemporalTimeZone(
      isolate, Cast<String>(parse_result.offset_string));
}

}  // namespace temporal

namespace {
// #sec-temporal-systemdatetime
MaybeDirectHandle<JSTemporalPlainDateTime> SystemDateTime(
    Isolate* isolate, DirectHandle<Object> temporal_time_zone_like,
    DirectHandle<Object> calendar_like, const char* method_name) {
  TEMPORAL_ENTER_FUNC();

  DirectHandle<JSReceiver> time_zone;
  // 1. 1. If temporalTimeZoneLike is undefined, then
  if (IsUndefined(*temporal_time_zone_like)) {
    // a. Let timeZone be ! SystemTimeZone().
    time_zone = SystemTimeZone(isolate);
  } else {
    // 2. Else,
    // a. Let timeZone be ? ToTemporalTimeZone(temporalTimeZoneLike).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, time_zone,
        temporal::ToTemporalTimeZone(isolate, temporal_time_zone_like,
                                     method_name));
  }
  DirectHandle<JSReceiver> calendar;
  // 3. Let calendar be ? ToTemporalCalendar(calendarLike).
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar,
      temporal::ToTemporalCalendar(isolate, calendar_like, method_name));
  // 4. Let instant be ! SystemInstant().
  DirectHandle<JSTemporalInstant> instant = SystemInstant(isolate);
  // 5. Return ? BuiltinTimeZoneGetPlainDateTimeFor(timeZone, instant,
  // calendar).
  return temporal::BuiltinTimeZoneGetPlainDateTimeFor(
      isolate, time_zone, instant, calendar, method_name);
}

MaybeDirectHandle<JSTemporalZonedDateTime> SystemZonedDateTime(
    Isolate* isolate, DirectHandle<Object> temporal_time_zone_like,
    DirectHandle<Object> calendar_like, const char* method_name) {
  TEMPORAL_ENTER_FUNC();

  DirectHandle<JSReceiver> time_zone;
  // 1. 1. If temporalTimeZoneLike is undefined, then
  if (IsUndefined(*temporal_time_zone_like)) {
    // a. Let timeZone be ! SystemTimeZone().
    time_zone = SystemTimeZone(isolate);
  } else {
    // 2. Else,
    // a. Let timeZone be ? ToTemporalTimeZone(temporalTimeZoneLike).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, time_zone,
        temporal::ToTemporalTimeZone(isolate, temporal_time_zone_like,
                                     method_name));
  }
  DirectHandle<JSReceiver> calendar;
  // 3. Let calendar be ? ToTemporalCalendar(calendarLike).
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar,
      temporal::ToTemporalCalendar(isolate, calendar_like, method_name));
  // 4. Let ns be ! SystemUTCEpochNanoseconds().
  DirectHandle<BigInt> ns = SystemUTCEpochNanoseconds(isolate);
  // Return ? CreateTemporalZonedDateTime(ns, timeZone, calendar).
  return CreateTemporalZonedDateTime(isolate, ns, time_zone, calendar);
}

int CompareResultToSign(ComparisonResult r) {
  DCHECK_NE(r, ComparisonResult::kUndefined);
  return static_cast<int>(r);
}

// #sec-temporal-formattimezoneoffsetstring
DirectHandle<String> FormatTimeZoneOffsetString(Isolate* isolate,
                                                int64_t offset_nanoseconds) {
  IncrementalStringBuilder builder(isolate);
  // 1. Assert: offsetNanoseconds is an integer.
  // 2. If offsetNanoseconds ≥ 0, let sign be "+"; otherwise, let sign be "-".
  builder.AppendCharacter((offset_nanoseconds >= 0) ? '+' : '-');
  // 3. Let offsetNanoseconds be abs(offsetNanoseconds).
  offset_nanoseconds = std::abs(offset_nanoseconds);
  // 4. Let nanoseconds be offsetNanoseconds modulo 10^9.
  int64_t nanoseconds = offset_nanoseconds % 1000000000;
  // 5. Let seconds be floor(offsetNanoseconds / 10^9) modulo 60.
  int32_t seconds = (offset_nanoseconds / 1000000000) % 60;
  // 6. Let minutes be floor(offsetNanoseconds / (6 × 10^10)) modulo 60.
  int32_t minutes = (offset_nanoseconds / 60000000000) % 60;
  // 7. Let hours be floor(offsetNanoseconds / (3.6 × 10^12)).
  int32_t hours = offset_nanoseconds / 3600000000000;
  // 8. Let h be ToZeroPaddedDecimalString(hours, 2).
  ToZeroPaddedDecimalString(&builder, hours, 2);

  // 9. Let m be ToZeroPaddedDecimalString(minutes, 2).
  builder.AppendCharacter(':');
  ToZeroPaddedDecimalString(&builder, minutes, 2);

  // 10. Let s be ToZeroPaddedDecimalString(seconds, 2).
  // 11. If nanoseconds ≠ 0, then
  if (nanoseconds != 0) {
    // a. Let fraction be ToZeroPaddedDecimalString(nanoseconds, 9).
    // b. Set fraction to the longest possible substring of fraction starting at
    // position 0 and not ending with the code unit 0x0030 (DIGIT ZERO). c. Let
    // post be the string-concatenation of the code unit 0x003A (COLON), s, the
    // code unit 0x002E (FULL STOP), and fraction.
    builder.AppendCharacter(':');
    ToZeroPaddedDecimalString(&builder, seconds, 2);
    builder.AppendCharacter('.');
    int64_t divisor = 100000000;
    do {
      builder.AppendInt(static_cast<int>(nanoseconds / divisor));
      nanoseconds %= divisor;
      divisor /= 10;
    } while (nanoseconds > 0);
    // 11. Else if seconds ≠ 0, then
  } else if (seconds != 0) {
    // a. Let post be the string-concatenation of the code unit 0x003A (COLON)
    // and s.
    builder.AppendCharacter(':');
    ToZeroPaddedDecimalString(&builder, seconds, 2);
  }
  // 12. Return the string-concatenation of sign, h, the code unit 0x003A
  // (COLON), m, and post.
  return builder.Finish().ToHandleChecked();
}

double RoundNumberToIncrement(Isolate* isolate, double x, double increment,
                              RoundingMode rounding_mode);

// #sec-temporal-formatisotimezoneoffsetstring
DirectHandle<String> FormatISOTimeZoneOffsetString(Isolate* isolate,
                                                   int64_t offset_nanoseconds) {
  IncrementalStringBuilder builder(isolate);
  // 1. Assert: offsetNanoseconds is an integer.
  // 2. Set offsetNanoseconds to ! RoundNumberToIncrement(offsetNanoseconds, 60
  // × 10^9, "halfExpand").
  offset_nanoseconds = RoundNumberToIncrement(
      isolate, offset_nanoseconds, 60000000000, RoundingMode::kHalfExpand);
  // 3. If offsetNanoseconds ≥ 0, let sign be "+"; otherwise, let sign be "-".
  builder.AppendCharacter((offset_nanoseconds >= 0) ? '+' : '-');
  // 4. Set offsetNanoseconds to abs(offsetNanoseconds).
  offset_nanoseconds = std::abs(offset_nanoseconds);
  // 5. Let minutes be offsetNanoseconds / (60 × 10^9) modulo 60.
  int32_t minutes = (offset_nanoseconds / 60000000000) % 60;
  // 6. Let hours be floor(offsetNanoseconds / (3600 × 10^9)).
  int32_t hours = offset_nanoseconds / 3600000000000;
  // 7. Let h be ToZeroPaddedDecimalString(hours, 2).
  ToZeroPaddedDecimalString(&builder, hours, 2);

  // 8. Let m be ToZeroPaddedDecimalString(minutes, 2).
  builder.AppendCharacter(':');
  ToZeroPaddedDecimalString(&builder, minutes, 2);
  // 9. Return the string-concatenation of sign, h, the code unit 0x003A
  // (COLON), and m.
  return builder.Finish().ToHandleChecked();
}

int32_t DecimalLength(int32_t n) {
  int32_t i = 1;
  while (n >= 10) {
    n /= 10;
    i++;
  }
  return i;
}

// #sec-tozeropaddeddecimalstring
void ToZeroPaddedDecimalString(IncrementalStringBuilder* builder, int32_t n,
                               int32_t min_length) {
  for (int32_t pad = min_length - DecimalLength(n); pad > 0; pad--) {
    builder->AppendCharacter('0');
  }
  builder->AppendInt(n);
}

// #sec-temporal-padisoyear
void PadISOYear(IncrementalStringBuilder* builder, int32_t y) {
  // 1. Assert: y is an integer.
  // 2. If y ≥ 0 and y ≤ 9999, then
  if (y >= 0 && y <= 9999) {
    // a. Return ToZeroPaddedDecimalString(y, 4).
    ToZeroPaddedDecimalString(builder, y, 4);
    return;
  }
  // 3. If y > 0, let yearSign be "+"; otherwise, let yearSign be "-".
  if (y > 0) {
    builder->AppendCharacter('+');
  } else {
    builder->AppendCharacter('-');
  }
  // 4. Let year be ToZeroPaddedDecimalString(abs(y), 6).
  ToZeroPaddedDecimalString(builder, std::abs(y), 6);
  // 5. Return the string-concatenation of yearSign and year.
}

// #sec-temporal-formatcalendarannotation
DirectHandle<String> FormatCalendarAnnotation(Isolate* isolate,
                                              DirectHandle<String> id,
                                              ShowCalendar show_calendar) {
  // 1.Assert: showCalendar is "auto", "always", or "never".
  // 2. If showCalendar is "never", return the empty String.
  if (show_calendar == ShowCalendar::kNever) {
    return isolate->factory()->empty_string();
  }
  // 3. If showCalendar is "auto" and id is "iso8601", return the empty String.
  if (show_calendar == ShowCalendar::kAuto &&
      String::Equals(isolate, id, isolate->factory()->iso8601_string())) {
    return isolate->factory()->empty_string();
  }
  // 4. Return the string-concatenation of "[u-ca=", id, and "]".
  IncrementalStringBuilder builder(isolate);
  builder.AppendCStringLiteral("[u-ca=");
  builder.AppendString(id);
  builder.AppendCharacter(']');
  return builder.Finish().ToHandleChecked();
}

// #sec-temporal-maybeformatcalendarannotation
MaybeDirectHandle<String> MaybeFormatCalendarAnnotation(
    Isolate* isolate, DirectHandle<JSReceiver> calendar_object,
    ShowCalendar show_calendar) {
  // 1. If showCalendar is "never", return the empty String.
  if (show_calendar == ShowCalendar::kNever) {
    return isolate->factory()->empty_string();
  }
  // 2. Let calendarID be ? ToString(calendarObject).
  DirectHandle<String> calendar_id;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, calendar_id,
                             Object::ToString(isolate, calendar_object));
  // 3. Return FormatCalendarAnnotation(calendarID, showCalendar).
  return FormatCalendarAnnotation(isolate, calendar_id, show_calendar);
}

// #sec-temporal-temporaldatetostring
MaybeDirectHandle<String> TemporalDateToString(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date,
    ShowCalendar show_calendar) {
  IncrementalStringBuilder builder(isolate);
  // 1. Assert: Type(temporalDate) is Object.
  // 2. Assert: temporalDate has an [[InitializedTemporalDate]] internal slot.
  // 3. Let year be ! PadISOYear(temporalDate.[[ISOYear]]).
  PadISOYear(&builder, temporal_date->iso_year());
  // 4. Let month be ToZeroPaddedDecimalString(temporalDate.[[ISOMonth]], 2).
  builder.AppendCharacter('-');
  ToZeroPaddedDecimalString(&builder, temporal_date->iso_month(), 2);
  // 5. Let day be ToZeroPaddedDecimalString(temporalDate.[[ISODay]], 2).
  builder.AppendCharacter('-');
  ToZeroPaddedDecimalString(&builder, temporal_date->iso_day(), 2);
  // 6. Let calendar be ?
  // MaybeFormatCalendarAnnotation(temporalDate.[[Calendar]], showCalendar).
  DirectHandle<String> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar,
      MaybeFormatCalendarAnnotation(
          isolate, direct_handle(temporal_date->calendar(), isolate),
          show_calendar));

  // 7. Return the string-concatenation of year, the code unit 0x002D
  // (HYPHEN-MINUS), month, the code unit 0x002D (HYPHEN-MINUS), day, and
  // calendar.
  builder.AppendString(calendar);
  return builder.Finish().ToHandleChecked();
}

// #sec-temporal-temporalmonthdaytostring
MaybeDirectHandle<String> TemporalMonthDayToString(
    Isolate* isolate, DirectHandle<JSTemporalPlainMonthDay> month_day,
    ShowCalendar show_calendar) {
  // 1. Assert: Type(monthDay) is Object.
  // 2. Assert: monthDay has an [[InitializedTemporalMonthDay]] internal slot.
  IncrementalStringBuilder builder(isolate);
  // 6. Let calendarID be ? ToString(monthDay.[[Calendar]]).
  DirectHandle<String> calendar_id;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar_id,
      Object::ToString(isolate, handle(month_day->calendar(), isolate)));
  // 7. If showCalendar is "always" or if calendarID is not "iso8601", then
  if (show_calendar == ShowCalendar::kAlways ||
      !String::Equals(isolate, calendar_id,
                      isolate->factory()->iso8601_string())) {
    // a. Let year be ! PadISOYear(monthDay.[[ISOYear]]).
    PadISOYear(&builder, month_day->iso_year());
    // b. Set result to the string-concatenation of year, the code unit
    // 0x002D (HYPHEN-MINUS), and result.
    builder.AppendCharacter('-');
  }
  // 3. Let month be ToZeroPaddedDecimalString(monthDay.[[ISOMonth]], 2).
  ToZeroPaddedDecimalString(&builder, month_day->iso_month(), 2);
  // 5. Let result be the string-concatenation of month, the code unit 0x002D
  // (HYPHEN-MINUS), and day.
  builder.AppendCharacter('-');
  // 4. Let day be ToZeroPaddedDecimalString(monthDay.[[ISODay]], 2).
  ToZeroPaddedDecimalString(&builder, month_day->iso_day(), 2);
  // 8. Let calendarString be ! FormatCalendarAnnotation(calendarID,
  // showCalendar).
  DirectHandle<String> calendar_string =
      FormatCalendarAnnotation(isolate, calendar_id, show_calendar);
  // 9. Set result to the string-concatenation of result and calendarString.
  builder.AppendString(calendar_string);
  // 10. Return result.
  return builder.Finish().ToHandleChecked();
}

// #sec-temporal-temporalyearmonthtostring
MaybeDirectHandle<String> TemporalYearMonthToString(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month,
    ShowCalendar show_calendar) {
  // 1. Assert: Type(yearMonth) is Object.
  // 2. Assert: yearMonth has an [[InitializedTemporalYearMonth]] internal slot.
  IncrementalStringBuilder builder(isolate);
  // 3. Let year be ! PadISOYear(yearMonth.[[ISOYear]]).
  PadISOYear(&builder, year_month->iso_year());
  // 4. Let month be ToZeroPaddedDecimalString(yearMonth.[[ISOMonth]], 2).
  // 5. Let result be the string-concatenation of year, the code unit 0x002D
  // (HYPHEN-MINUS), and month.
  builder.AppendCharacter('-');
  ToZeroPaddedDecimalString(&builder, year_month->iso_month(), 2);
  // 6. Let calendarID be ? ToString(yearMonth.[[Calendar]]).
  DirectHandle<String> calendar_id;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar_id,
      Object::ToString(isolate, handle(year_month->calendar(), isolate)));
  // 7. If showCalendar is "always" or if *_calendarID_ is not *"iso8601", then
  if (show_calendar == ShowCalendar::kAlways ||
      !String::Equals(isolate, calendar_id,
                      isolate->factory()->iso8601_string())) {
    // a. Let day be ToZeroPaddedDecimalString(yearMonth.[[ISODay]], 2).
    // b. Set result to the string-concatenation of result, the code unit 0x002D
    // (HYPHEN-MINUS), and day.
    builder.AppendCharacter('-');
    ToZeroPaddedDecimalString(&builder, year_month->iso_day(), 2);
  }
  // 8. Let calendarString be ! FormatCalendarAnnotation(calendarID,
  // showCalendar).
  DirectHandle<String> calendar_string =
      FormatCalendarAnnotation(isolate, calendar_id, show_calendar);
  // 9. Set result to the string-concatenation of result and calendarString.
  builder.AppendString(calendar_string);
  // 10. Return result.
  return builder.Finish().ToHandleChecked();
}

// #sec-temporal-builtintimezonegetoffsetstringfor
MaybeDirectHandle<String> BuiltinTimeZoneGetOffsetStringFor(
    Isolate* isolate, DirectHandle<JSReceiver> time_zone,
    DirectHandle<JSTemporalInstant> instant, const char* method_name) {
  TEMPORAL_ENTER_FUNC();
  // 1. Let offsetNanoseconds be ? GetOffsetNanosecondsFor(timeZone, instant).
  int64_t offset_nanoseconds;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, offset_nanoseconds,
      GetOffsetNanosecondsFor(isolate, time_zone, instant, method_name),
      DirectHandle<String>());

  // 2. Return ! FormatTimeZoneOffsetString(offsetNanoseconds).
  return FormatTimeZoneOffsetString(isolate, offset_nanoseconds);
}

// #sec-temporal-parseisodatetime
Maybe<DateTimeRecordWithCalendar> ParseISODateTime(
    Isolate* isolate, DirectHandle<String> iso_string,
    const ParsedISO8601Result& parsed);
// Note: We split ParseISODateTime to two function because the spec text
// repeats some parsing unnecessarily. If a function is calling ParseISODateTime
// from a AO which already call ParseText() for TemporalDateTimeString,
// TemporalInstantString, TemporalMonthDayString, TemporalTimeString,
// TemporalYearMonthString, TemporalZonedDateTimeString. But for the usage in
// ParseTemporalTimeZoneString, we use the following version.
Maybe<DateTimeRecordWithCalendar> ParseISODateTime(
    Isolate* isolate, DirectHandle<String> iso_string) {
  // 2. For each nonterminal goal of « TemporalDateTimeString,
  // TemporalInstantString, TemporalMonthDayString, TemporalTimeString,
  // TemporalYearMonthString, TemporalZonedDateTimeString », do

  // a. If parseResult is not a Parse Node, set parseResult to
  // ParseText(StringToCodePoints(isoString), goal).
  std::optional<ParsedISO8601Result> parsed;
  if ((parsed =
           TemporalParser::ParseTemporalDateTimeString(isolate, iso_string))
          .has_value() ||
      (parsed = TemporalParser::ParseTemporalInstantString(isolate, iso_string))
          .has_value() ||
      (parsed =
           TemporalParser::ParseTemporalMonthDayString(isolate, iso_string))
          .has_value() ||
      (parsed = TemporalParser::ParseTemporalTimeString(isolate, iso_string))
          .has_value() ||
      (parsed =
           TemporalParser::ParseTemporalYearMonthString(isolate, iso_string))
          .has_value() ||
      (parsed = TemporalParser::ParseTemporalZonedDateTimeString(isolate,
                                                                 iso_string))
          .has_value()) {
    return ParseISODateTime(isolate, iso_string, *parsed);
  }

  // 3. If parseResult is not a Parse Node, throw a RangeError exception.
  THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                               Nothing<DateTimeRecordWithCalendar>());
}

Maybe<DateTimeRecordWithCalendar> ParseISODateTime(
    Isolate* isolate, DirectHandle<String> iso_string,
    const ParsedISO8601Result& parsed) {
  TEMPORAL_ENTER_FUNC();

  DateTimeRecordWithCalendar result;
  // 6. Set yearMV to ! ToIntegerOrInfinity(year).
  result.date.year = parsed.date_year;
  // 7. If month is undefined, then
  if (parsed.date_month_is_undefined()) {
    // a. Set monthMV to 1.
    result.date.month = 1;
    // 8. Else,
  } else {
    // a. Set monthMV to ! ToIntegerOrInfinity(month).
    result.date.month = parsed.date_month;
  }

  // 9. If day is undefined, then
  if (parsed.date_day_is_undefined()) {
    // a. Set dayMV to 1.
    result.date.day = 1;
    // 10. Else,
  } else {
    // a. Set dayMV to ! ToIntegerOrInfinity(day).
    result.date.day = parsed.date_day;
  }
  // 11. Set hourMV to ! ToIntegerOrInfinity(hour).
  result.time.hour = parsed.time_hour_is_undefined() ? 0 : parsed.time_hour;
  // 12. Set minuteMV to ! ToIntegerOrInfinity(minute).
  result.time.minute =
      parsed.time_minute_is_undefined() ? 0 : parsed.time_minute;
  // 13. Set secondMV to ! ToIntegerOrInfinity(second).
  result.time.second =
      parsed.time_second_is_undefined() ? 0 : parsed.time_second;
  // 14. If secondMV is 60, then
  if (result.time.second == 60) {
    // a. Set secondMV to 59.
    result.time.second = 59;
  }
  // 15. If fSeconds is not empty, then
  if (!parsed.time_nanosecond_is_undefined()) {
    // a. Let fSecondsDigits be the substring of CodePointsToString(fSeconds)
    // from 1.
    //
    // b. Let fSecondsDigitsExtended be the string-concatenation of
    // fSecondsDigits and "000000000".
    //
    // c. Let millisecond be the substring of fSecondsDigitsExtended from 0 to
    // 3.
    //
    // d. Let microsecond be the substring of fSecondsDigitsExtended from 3 to
    // 6.
    //
    // e. Let nanosecond be the substring of fSecondsDigitsExtended from 6 to 9.
    //
    // f. Let millisecondMV be ! ToIntegerOrInfinity(millisecond).
    result.time.millisecond = parsed.time_nanosecond / 1000000;
    // g. Let microsecondMV be ! ToIntegerOrInfinity(microsecond).
    result.time.microsecond = (parsed.time_nanosecond / 1000) % 1000;
    // h. Let nanosecondMV be ! ToIntegerOrInfinity(nanosecond).
    result.time.nanosecond = (parsed.time_nanosecond % 1000);
    // 16. Else,
  } else {
    // a. Let millisecondMV be 0.
    result.time.millisecond = 0;
    // b. Let microsecondMV be 0.
    result.time.microsecond = 0;
    // c. Let nanosecondMV be 0.
    result.time.nanosecond = 0;
  }
  // 17. If ! IsValidISODate(yearMV, monthMV, dayMV) is false, throw a
  // RangeError exception.
  if (!IsValidISODate(isolate, result.date)) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<DateTimeRecordWithCalendar>());
  }
  // 18. If ! IsValidTime(hourMV, minuteMV, secondMV, millisecondMV,
  // microsecondMV, nanosecond) is false, throw a RangeError exception.
  if (!IsValidTime(isolate, result.time)) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<DateTimeRecordWithCalendar>());
  }

  // 19. Let timeZoneResult be the Record { [[Z]]: false, [[OffsetString]]:
  // undefined, [[Name]]: undefined }.
  result.time_zone = {false, isolate->factory()->undefined_value(),
                      isolate->factory()->undefined_value()};
  // 20. If parseResult contains a TimeZoneIdentifier Parse Node, then
  if (parsed.tzi_name_length != 0) {
    // a. Let name be the source text matched by the TimeZoneIdentifier Parse
    // Node contained within parseResult.
    //
    // b. Set timeZoneResult.[[Name]] to CodePointsToString(name).
    result.time_zone.name = isolate->factory()->NewSubString(
        iso_string, parsed.tzi_name_start,
        parsed.tzi_name_start + parsed.tzi_name_length);
  }
  // 21. If parseResult contains a UTCDesignator Parse Node, then
  if (parsed.utc_designator) {
    // a. Set timeZoneResult.[[Z]] to true.
    result.time_zone.z = true;
    // 22. Else,
  } else {
    // a. If parseResult contains a TimeZoneNumericUTCOffset Parse Node, then
    if (parsed.offset_string_length != 0) {
      // i. Let offset be the source text matched by the
      // TimeZoneNumericUTCOffset Parse Node contained within parseResult.
      // ii. Set timeZoneResult.[[OffsetString]] to CodePointsToString(offset).
      result.time_zone.offset_string = isolate->factory()->NewSubString(
          iso_string, parsed.offset_string_start,
          parsed.offset_string_start + parsed.offset_string_length);
    }
  }

  // 23. If calendar is empty, then
  if (parsed.calendar_name_length == 0) {
    // a. Let calendarVal be undefined.
    result.calendar = isolate->factory()->undefined_value();
    // 24. Else,
  } else {
    // a. Let calendarVal be CodePointsToString(calendar).
    result.calendar = isolate->factory()->NewSubString(
        iso_string, parsed.calendar_name_start,
        parsed.calendar_name_start + parsed.calendar_name_length);
  }
  // 24. Return the Record { [[Year]]: yearMV, [[Month]]: monthMV, [[Day]]:
  // dayMV, [[Hour]]: hourMV, [[Minute]]: minuteMV, [[Second]]: secondMV,
  // [[Millisecond]]: millisecondMV, [[Microsecond]]: microsecondMV,
  // [[Nanosecond]]: nanosecondMV, [[TimeZone]]: timeZoneResult,
  // [[Calendar]]: calendarVal, }.
  return Just(result);
}

// #sec-temporal-parsetemporaldatestring
Maybe<DateRecordWithCalendar> ParseTemporalDateString(
    Isolate* isolate, DirectHandle<String> iso_string) {
  TEMPORAL_ENTER_FUNC();
  // 1. Let parts be ? ParseTemporalDateTimeString(isoString).
  // 2. Return the Record { [[Year]]: parts.[[Year]], [[Month]]:
  // parts.[[Month]], [[Day]]: parts.[[Day]], [[Calendar]]: parts.[[Calendar]]
  // }.
  DateTimeRecordWithCalendar record;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, record, ParseTemporalDateTimeString(isolate, iso_string),
      Nothing<DateRecordWithCalendar>());
  DateRecordWithCalendar result = {record.date, record.calendar};
  return Just(result);
}

// #sec-temporal-parsetemporaltimestring
Maybe<TimeRecordWithCalendar> ParseTemporalTimeString(
    Isolate* isolate, DirectHandle<String> iso_string) {
  TEMPORAL_ENTER_FUNC();

  // 1. Assert: Type(isoString) is String.
  // 2. If isoString does not satisfy the syntax of a TemporalTimeString
  // (see 13.33), then
  std::optional<ParsedISO8601Result> parsed =
      TemporalParser::ParseTemporalTimeString(isolate, iso_string);
  if (!parsed.has_value()) {
    // a. Throw a *RangeError* exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<TimeRecordWithCalendar>());
  }

  // 3. If _isoString_ contains a |UTCDesignator|, then
  if (parsed->utc_designator) {
    // a. Throw a *RangeError* exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<TimeRecordWithCalendar>());
  }

  // 3. Let result be ? ParseISODateTime(isoString).
  DateTimeRecordWithCalendar result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result, ParseISODateTime(isolate, iso_string, *parsed),
      Nothing<TimeRecordWithCalendar>());
  // 4. Return the Record { [[Hour]]: result.[[Hour]], [[Minute]]:
  // result.[[Minute]], [[Second]]: result.[[Second]], [[Millisecond]]:
  // result.[[Millisecond]], [[Microsecond]]: result.[[Microsecond]],
  // [[Nanosecond]]: result.[[Nanosecond]], [[Calendar]]: result.[[Calendar]] }.
  TimeRecordWithCalendar ret = {result.time, result.calendar};
  return Just(ret);
}

// #sec-temporal-parsetemporalinstantstring
Maybe<InstantRecord> ParseTemporalInstantString(
    Isolate* isolate, DirectHandle<String> iso_string) {
  TEMPORAL_ENTER_FUNC();

  // 1. If ParseText(StringToCodePoints(isoString), TemporalInstantString) is a
  // List of errors, throw a RangeError exception.
  std::optional<ParsedISO8601Result> parsed =
      TemporalParser::ParseTemporalInstantString(isolate, iso_string);
  if (!parsed.has_value()) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<InstantRecord>());
  }

  // 2. Let result be ? ParseISODateTime(isoString).
  DateTimeRecordWithCalendar result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result, ParseISODateTime(isolate, iso_string, *parsed),
      Nothing<InstantRecord>());

  // 3. Let offsetString be result.[[TimeZone]].[[OffsetString]].
  DirectHandle<Object> offset_string = result.time_zone.offset_string;

  // 4. If result.[[TimeZone]].[[Z]] is true, then
  if (result.time_zone.z) {
    // a. Set offsetString to "+00:00".
    offset_string = isolate->factory()->NewStringFromStaticChars("+00:00");
  }
  // 5. Assert: offsetString is not undefined.
  DCHECK(!IsUndefined(*offset_string));

  // 6. Return the new Record { [[Year]]: result.[[Year]],
  // [[Month]]: result.[[Month]], [[Day]]: result.[[Day]],
  // [[Hour]]: result.[[Hour]], [[Minute]]: result.[[Minute]],
  // [[Second]]: result.[[Second]],
  // [[Millisecond]]: result.[[Millisecond]],
  // [[Microsecond]]: result.[[Microsecond]],
  // [[Nanosecond]]: result.[[Nanosecond]],
  // [[TimeZoneOffsetString]]: offsetString }.
  InstantRecord record({result.date, result.time, offset_string});
  return Just(record);
}

// #sec-temporal-parsetemporalrelativetostring
Maybe<DateTimeRecordWithCalendar> ParseTemporalRelativeToString(
    Isolate* isolate, DirectHandle<String> iso_string) {
  TEMPORAL_ENTER_FUNC();

  // 1. If ParseText(StringToCodePoints(isoString), TemporalDateTimeString) is a
  // List of errors, throw a RangeError exception.
  std::optional<ParsedISO8601Result> parsed =
      TemporalParser::ParseTemporalDateTimeString(isolate, iso_string);
  if (!parsed.has_value()) {
    // a. Throw a *RangeError* exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<DateTimeRecordWithCalendar>());
  }
  // 2. Returns ? ParseISODateTime(isoString).
  return ParseISODateTime(isolate, iso_string, *parsed);
}

// #sec-temporal-parsetemporalinstant
MaybeDirectHandle<BigInt> ParseTemporalInstant(
    Isolate* isolate, DirectHandle<String> iso_string) {
  TEMPORAL_ENTER_FUNC();

  // 1. Assert: Type(isoString) is String.
  // 2. Let result be ? ParseTemporalInstantString(isoString).
  InstantRecord result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result, ParseTemporalInstantString(isolate, iso_string),
      DirectHandle<BigInt>());

  // 3. Let offsetString be result.[[TimeZoneOffsetString]].
  // 4. Assert: offsetString is not undefined.
  DCHECK(!IsUndefined(*result.offset_string));

  // 5. Let utc be ? GetEpochFromISOParts(result.[[Year]], result.[[Month]],
  // result.[[Day]], result.[[Hour]], result.[[Minute]], result.[[Second]],
  // result.[[Millisecond]], result.[[Microsecond]], result.[[Nanosecond]]).
  DirectHandle<BigInt> utc =
      GetEpochFromISOParts(isolate, {result.date, result.time});

  // 6. Let offsetNanoseconds be ? ParseTimeZoneOffsetString(offsetString).
  int64_t offset_nanoseconds;
  DCHECK(IsString(*result.offset_string));
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, offset_nanoseconds,
      ParseTimeZoneOffsetString(isolate, Cast<String>(result.offset_string)),
      DirectHandle<BigInt>());

  // 7. Let result be utc - ℤ(offsetNanoseconds).
  DirectHandle<BigInt> result_value =
      BigInt::Subtract(isolate, utc,
                       BigInt::FromInt64(isolate, offset_nanoseconds))
          .ToHandleChecked();
  // 8. If ! IsValidEpochNanoseconds(result) is false, then
  if (!IsValidEpochNanoseconds(isolate, result_value)) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }
  // 9. Return result.
  return result_value;
}

// #sec-temporal-parsetemporalzoneddatetimestring
Maybe<DateTimeRecordWithCalendar> ParseTemporalZonedDateTimeString(
    Isolate* isolate, DirectHandle<String> iso_string) {
  TEMPORAL_ENTER_FUNC();
  // 1. If ParseText(StringToCodePoints(isoString), TemporalZonedDateTimeString)
  // is a List of errors, throw a RangeError exception.
  std::optional<ParsedISO8601Result> parsed =
      TemporalParser::ParseTemporalZonedDateTimeString(isolate, iso_string);
  if (!parsed.has_value()) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<DateTimeRecordWithCalendar>());
  }

  // 2. Return ? ParseISODateTime(isoString).
  return ParseISODateTime(isolate, iso_string, *parsed);
}

// #sec-temporal-createdurationrecord
Maybe<DurationRecord> CreateDurationRecord(Isolate* isolate,
                                           const DurationRecord& duration) {
  //   1. If ! IsValidDuration(years, months, weeks, days, hours, minutes,
  //   seconds, milliseconds, microseconds, nanoseconds) is false, throw a
  //   RangeError exception.
  if (!IsValidDuration(isolate, duration)) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<DurationRecord>());
  }
  // 2. Return the Record { [[Years]]: ℝ(𝔽(years)), [[Months]]: ℝ(𝔽(months)),
  // [[Weeks]]: ℝ(𝔽(weeks)), [[Days]]: ℝ(𝔽(days)), [[Hours]]: ℝ(𝔽(hours)),
  // [[Minutes]]: ℝ(𝔽(minutes)), [[Seconds]]: ℝ(𝔽(seconds)), [[Milliseconds]]:
  // ℝ(𝔽(milliseconds)), [[Microseconds]]: ℝ(𝔽(microseconds)), [[Nanoseconds]]:
  // ℝ(𝔽(nanoseconds)) }.
  return Just(duration);
}

inline double IfEmptyReturnZero(double value) {
  return value == ParsedISO8601Duration::kEmpty ? 0 : value;
}

// #sec-temporal-parsetemporaldurationstring
Maybe<DurationRecord> ParseTemporalDurationString(
    Isolate* isolate, DirectHandle<String> iso_string) {
  TEMPORAL_ENTER_FUNC();
  // In this function, we use 'double' as type for all mathematical values
  // because in
  // https://tc39.es/proposal-temporal/#sec-properties-of-temporal-duration-instances
  // they are "A float64-representable integer representing the number" in the
  // internal slots.
  // 1. Let duration be ParseText(StringToCodePoints(isoString),
  // TemporalDurationString).
  // 2. If duration is a List of errors, throw a RangeError exception.
  // 3. Let each of sign, years, months, weeks, days, hours, fHours, minutes,
  // fMinutes, seconds, and fSeconds be the source text matched by the
  // respective Sign, DurationYears, DurationMonths, DurationWeeks,
  // DurationDays, DurationWholeHours, DurationHoursFraction,
  // DurationWholeMinutes, DurationMinutesFraction, DurationWholeSeconds, and
  // DurationSecondsFraction Parse Node enclosed by duration, or an empty
  // sequence of code points if not present.
  std::optional<ParsedISO8601Duration> parsed =
      TemporalParser::ParseTemporalDurationString(isolate, iso_string);
  if (!parsed.has_value()) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<DurationRecord>());
  }
  // 4. Let yearsMV be ! ToIntegerOrInfinity(CodePointsToString(years)).
  double years_mv = IfEmptyReturnZero(parsed->years);
  // 5. Let monthsMV be ! ToIntegerOrInfinity(CodePointsToString(months)).
  double months_mv = IfEmptyReturnZero(parsed->months);
  // 6. Let weeksMV be ! ToIntegerOrInfinity(CodePointsToString(weeks)).
  double weeks_mv = IfEmptyReturnZero(parsed->weeks);
  // 7. Let daysMV be ! ToIntegerOrInfinity(CodePointsToString(days)).
  double days_mv = IfEmptyReturnZero(parsed->days);
  // 8. Let hoursMV be ! ToIntegerOrInfinity(CodePointsToString(hours)).
  double hours_mv = IfEmptyReturnZero(parsed->whole_hours);
  // 9. If fHours is not empty, then
  double minutes_mv;
  if (parsed->hours_fraction != ParsedISO8601Duration::kEmpty) {
    // a. If any of minutes, fMinutes, seconds, fSeconds is not empty, throw a
    // RangeError exception.
    if (parsed->whole_minutes != ParsedISO8601Duration::kEmpty ||
        parsed->minutes_fraction != ParsedISO8601Duration::kEmpty ||
        parsed->whole_seconds != ParsedISO8601Duration::kEmpty ||
        parsed->seconds_fraction != ParsedISO8601Duration::kEmpty) {
      THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                   NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                   Nothing<DurationRecord>());
    }
    // b. Let fHoursDigits be the substring of CodePointsToString(fHours)
    // from 1.
    //
    // c. Let fHoursScale be the length of fHoursDigits.
    //
    // d. Let
    // minutesMV be ! ToIntegerOrInfinity(fHoursDigits) / 10^fHoursScale × 60.
    minutes_mv = IfEmptyReturnZero(parsed->hours_fraction) * 60.0 / 1e9;
    // 10. Else,
  } else {
    // a. Let minutesMV be ! ToIntegerOrInfinity(CodePointsToString(minutes)).
    minutes_mv = IfEmptyReturnZero(parsed->whole_minutes);
  }
  double seconds_mv;
  // 11. If fMinutes is not empty, then
  if (parsed->minutes_fraction != ParsedISO8601Duration::kEmpty) {
    // a. If any of seconds, fSeconds is not empty, throw a RangeError
    // exception.
    if (parsed->whole_seconds != ParsedISO8601Duration::kEmpty ||
        parsed->seconds_fraction != ParsedISO8601Duration::kEmpty) {
      THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                   NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                   Nothing<DurationRecord>());
    }
    // b. Let fMinutesDigits be the substring of CodePointsToString(fMinutes)
    // from 1.
    //
    // c. Let fMinutesScale be the length of fMinutesDigits.
    //
    // d. Let secondsMV be ! ToIntegerOrInfinity(fMinutesDigits) /
    // 10^fMinutesScale × 60.
    seconds_mv = IfEmptyReturnZero(parsed->minutes_fraction) * 60.0 / 1e9;
    // 12. Else if seconds is not empty, then
  } else if (parsed->whole_seconds != ParsedISO8601Duration::kEmpty) {
    // a. Let secondsMV be ! ToIntegerOrInfinity(CodePointsToString(seconds)).
    seconds_mv = parsed->whole_seconds;
    // 13. Else,
  } else {
    // a. Let secondsMV be remainder(minutesMV, 1) × 60.
    seconds_mv = (minutes_mv - std::floor(minutes_mv)) * 60.0;
  }
  double milliseconds_mv, microseconds_mv, nanoseconds_mv;
  // Note: In step 14-17, we calculate from nanoseconds_mv to miilliseconds_mv
  // in the reversee order of the spec text to avoid numerical errors would be
  // introduced by multiple division inside the remainder operations. If we
  // strickly follow the order by using double, the end result of nanoseconds_mv
  // will be wrong due to numerical errors.
  //
  // 14. If fSeconds is not empty, then
  if (parsed->seconds_fraction != ParsedISO8601Duration::kEmpty) {
    // a. Let fSecondsDigits be the substring of CodePointsToString(fSeconds)
    // from 1.
    //
    // b. Let fSecondsScale be the length of fSecondsDigits.
    //
    // c. Let millisecondsMV be ! ToIntegerOrInfinity(fSecondsDigits) /
    // 10^fSecondsScale × 1000.
    DCHECK_LE(IfEmptyReturnZero(parsed->seconds_fraction), 1e9);
    nanoseconds_mv = std::round(IfEmptyReturnZero(parsed->seconds_fraction));
    // 15. Else,
  } else {
    // a. Let millisecondsMV be remainder(secondsMV, 1) × 1000.
    nanoseconds_mv = std::round((seconds_mv - std::floor(seconds_mv)) * 1e9);
  }
  milliseconds_mv = std::floor(nanoseconds_mv / 1000000);
  // 16. Let microsecondsMV be remainder(millisecondsMV, 1) × 1000.
  microseconds_mv = std::floor(nanoseconds_mv / 1000) -
                    std::floor(nanoseconds_mv / 1000000) * 1000;
  // 17. Let nanosecondsMV be remainder(microsecondsMV, 1) × 1000.
  nanoseconds_mv -= std::floor(nanoseconds_mv / 1000) * 1000;

  // 18. If sign contains the code point 0x002D (HYPHEN-MINUS) or 0x2212 (MINUS
  // SIGN), then a. Let factor be −1.
  // 19. Else,
  // a. Let factor be 1.
  double factor = parsed->sign;

  // 20. Return ? CreateDurationRecord(yearsMV × factor, monthsMV × factor,
  // weeksMV × factor, daysMV × factor, hoursMV × factor, floor(minutesMV) ×
  // factor, floor(secondsMV) × factor, floor(millisecondsMV) × factor,
  // floor(microsecondsMV) × factor, floor(nanosecondsMV) × factor).

  return CreateDurationRecord(
      isolate,
      {years_mv * factor,
       months_mv * factor,
       weeks_mv * factor,
       {days_mv * factor, hours_mv * factor, std::floor(minutes_mv) * factor,
        std::floor(seconds_mv) * factor, milliseconds_mv * factor,
        microseconds_mv * factor, nanoseconds_mv * factor}});
}

// #sec-temporal-parsetemporaltimezonestring
Maybe<TimeZoneRecord> ParseTemporalTimeZoneString(
    Isolate* isolate, DirectHandle<String> time_zone_string) {
  TEMPORAL_ENTER_FUNC();

  // 1. Let parseResult be ParseText(StringToCodePoints(timeZoneString),
  // TimeZoneIdentifier).
  std::optional<ParsedISO8601Result> parsed =
      TemporalParser::ParseTimeZoneIdentifier(isolate, time_zone_string);
  // 2. If parseResult is a Parse Node, then
  if (parsed.has_value()) {
    // a. Return the Record { [[Z]]: false, [[OffsetString]]: undefined,
    // [[Name]]: timeZoneString }.
    return Just(TimeZoneRecord(
        {false, isolate->factory()->undefined_value(), time_zone_string}));
  }

  // 3. Let result be ? ParseISODateTime(timeZoneString).
  DateTimeRecordWithCalendar result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result, ParseISODateTime(isolate, time_zone_string),
      Nothing<TimeZoneRecord>());

  // 4. Let timeZoneResult be result.[[TimeZone]].
  // 5. If timeZoneResult.[[Z]] is false, timeZoneResult.[[OffsetString]] is
  // undefined, and timeZoneResult.[[Name]] is undefined, throw a RangeError
  // exception.
  if (!result.time_zone.z && IsUndefined(*result.time_zone.offset_string) &&
      IsUndefined(*result.time_zone.name)) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<TimeZoneRecord>());
  }
  // 6. Return timeZoneResult.
  return Just(result.time_zone);
}

Maybe<int64_t> ParseTimeZoneOffsetString(Isolate* isolate,
                                         DirectHandle<String> iso_string) {
  TEMPORAL_ENTER_FUNC();

  // 1. Assert: Type(offsetString) is String.
  // 2. If offsetString does not satisfy the syntax of a
  // TimeZoneNumericUTCOffset (see 13.33), then
  std::optional<ParsedISO8601Result> parsed =
      TemporalParser::ParseTimeZoneNumericUTCOffset(isolate, iso_string);
  if (!parsed.has_value()) {
    /* a. Throw a RangeError exception. */
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(), Nothing<int64_t>());
  }
  // 3. Let sign, hours, minutes, seconds, and fraction be the parts of
  // offsetString produced respectively by the TimeZoneUTCOffsetSign,
  // TimeZoneUTCOffsetHour, TimeZoneUTCOffsetMinute, TimeZoneUTCOffsetSecond,
  // and TimeZoneUTCOffsetFraction productions, or undefined if not present.
  // 4. If either hours or sign are undefined, throw a RangeError exception.
  if (parsed->tzuo_hour_is_undefined() || parsed->tzuo_sign_is_undefined()) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(), Nothing<int64_t>());
  }
  // 5. If sign is the code unit 0x002D (HYPHEN-MINUS) or 0x2212 (MINUS SIGN),
  // then a. Set sign to −1.
  // 6. Else,
  // a. Set sign to 1.
  int64_t sign = parsed->tzuo_sign;

  // 7. Set hours to ! ToIntegerOrInfinity(hours).
  int64_t hours = parsed->tzuo_hour;
  // 8. Set minutes to ! ToIntegerOrInfinity(minutes).
  int64_t minutes =
      parsed->tzuo_minute_is_undefined() ? 0 : parsed->tzuo_minute;
  // 9. Set seconds to ! ToIntegerOrInfinity(seconds).
  int64_t seconds =
      parsed->tzuo_second_is_undefined() ? 0 : parsed->tzuo_second;
  // 10. If fraction is not undefined, then
  int64_t nanoseconds;
  if (!parsed->tzuo_nanosecond_is_undefined()) {
    // a. Set fraction to the string-concatenation of the previous value of
    // fraction and the string "000000000".
    // b. Let nanoseconds be the String value equal to the substring of fraction
    // consisting of the code units with indices 0 (inclusive) through 9
    // (exclusive). c. Set nanoseconds to ! ToIntegerOrInfinity(nanoseconds).
    nanoseconds = parsed->tzuo_nanosecond;
    // 11. Else,
  } else {
    // a. Let nanoseconds be 0.
    nanoseconds = 0;
  }
  // 12. Return sign × (((hours × 60 + minutes) × 60 + seconds) × 10^9 +
  // nanoseconds).
  return Just(sign * (((hours * 60 + minutes) * 60 + seconds) * 1000000000 +
                      nanoseconds));
}

bool IsValidTimeZoneNumericUTCOffsetString(Isolate* isolate,
                                           DirectHandle<String> iso_string) {
  TEMPORAL_ENTER_FUNC();

  std::optional<ParsedISO8601Result> parsed =
      TemporalParser::ParseTimeZoneNumericUTCOffset(isolate, iso_string);
  return parsed.has_value();
}

// #sec-temporal-parsetemporalcalendarstring
MaybeDirectHandle<String> ParseTemporalCalendarString(
    Isolate* isolate, DirectHandle<String> iso_string) {
  TEMPORAL_ENTER_FUNC();

  // 1. Let parseResult be Completion(ParseISODateTime(isoString)).
  Maybe<DateTimeRecordWithCalendar> parse_result =
      ParseISODateTime(isolate, iso_string);
  // 2. If parseResult is a normal completion, then
  if (parse_result.IsJust()) {
    // a. Let calendar be parseResult.[[Value]].[[Calendar]].
    DirectHandle<Object> calendar = parse_result.FromJust().calendar;
    // b. If calendar is undefined, return "iso8601".
    if (IsUndefined(*calendar)) {
      return isolate->factory()->iso8601_string();
      // c. Else, return calendar.
    } else {
      CHECK(IsString(*calendar));
      return Cast<String>(calendar);
    }
    // 3. Else,
  } else {
    DCHECK(isolate->has_exception());
    isolate->clear_exception();
    // a. Set parseResult to ParseText(StringToCodePoints(isoString),
    // CalendarName).
    std::optional<ParsedISO8601Result> parsed =
        TemporalParser::ParseCalendarName(isolate, iso_string);
    // b. If parseResult is a List of errors, throw a RangeError exception.
    if (!parsed.has_value()) {
      THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kInvalidCalendar,
                                             iso_string));
    }
    // c. Else, return isoString.
    return iso_string;
  }
}

// #sec-temporal-calendarequals
Maybe<bool> CalendarEqualsBool(Isolate* isolate, DirectHandle<JSReceiver> one,
                               DirectHandle<JSReceiver> two) {
  // 1. If one and two are the same Object value, return true.
  if (one.is_identical_to(two)) {
    return Just(true);
  }
  // 2. Let calendarOne be ? ToString(one).
  DirectHandle<String> calendar_one;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, calendar_one, Object::ToString(isolate, one), Nothing<bool>());
  // 3. Let calendarTwo be ? ToString(two).
  DirectHandle<String> calendar_two;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, calendar_two, Object::ToString(isolate, two), Nothing<bool>());
  // 4. If calendarOne is calendarTwo, return true.
  if (String::Equals(isolate, calendar_one, calendar_two)) {
    return Just(true);
  }
  // 5. Return false.
  return Just(false);
}
MaybeDirectHandle<Oddball> CalendarEquals(Isolate* isolate,
                                          DirectHandle<JSReceiver> one,
                                          DirectHandle<JSReceiver> two) {
  bool result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, result,
                                         CalendarEqualsBool(isolate, one, two),
                                         DirectHandle<Oddball>());
  return isolate->factory()->ToBoolean(result);
}

// #sec-temporal-calendarfields
MaybeDirectHandle<FixedArray> CalendarFields(
    Isolate* isolate, DirectHandle<JSReceiver> calendar,
    DirectHandle<FixedArray> field_names) {
  // 1. Let fields be ? GetMethod(calendar, "fields").
  DirectHandle<Object> fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, fields,
      Object::GetMethod(isolate, calendar,
                        isolate->factory()->fields_string()));
  // 2. Let fieldsArray be ! CreateArrayFromList(fieldNames).
  DirectHandle<Object> fields_array =
      isolate->factory()->NewJSArrayWithElements(field_names);
  // 3. If fields is not undefined, then
  if (!IsUndefined(*fields)) {
    // a. Set fieldsArray to ? Call(fields, calendar, « fieldsArray »).
    DirectHandle<Object> args[] = {fields_array};
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, fields_array,
        Execution::Call(isolate, fields, calendar, base::VectorOf(args)));
  }
  // 4. Return ? IterableToListOfType(fieldsArray, « String »).
  DirectHandle<Object> args[] = {fields_array};
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, fields_array,
      Execution::CallBuiltin(isolate,
                             isolate->string_fixed_array_from_iterable(),
                             fields_array, base::VectorOf(args)));
  DCHECK(IsFixedArray(*fields_array));
  return Cast<FixedArray>(fields_array);
}

MaybeDirectHandle<JSTemporalPlainDate> CalendarDateAdd(
    Isolate* isolate, DirectHandle<JSReceiver> calendar,
    DirectHandle<Object> date, DirectHandle<Object> duration) {
  // 2. If options is not present, set options to undefined.
  return CalendarDateAdd(isolate, calendar, date, duration,
                         isolate->factory()->undefined_value());
}

MaybeDirectHandle<JSTemporalPlainDate> CalendarDateAdd(
    Isolate* isolate, DirectHandle<JSReceiver> calendar,
    DirectHandle<Object> date, DirectHandle<Object> duration,
    DirectHandle<Object> options) {
  DirectHandle<Object> date_add;
  // 4. If dateAdd is not present, set dateAdd to ? GetMethod(calendar,
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, date_add,
      Object::GetMethod(isolate, calendar,
                        isolate->factory()->dateAdd_string()));
  return CalendarDateAdd(isolate, calendar, date, duration, options, date_add);
}

MaybeDirectHandle<JSTemporalPlainDate> CalendarDateAdd(
    Isolate* isolate, DirectHandle<JSReceiver> calendar,
    DirectHandle<Object> date, DirectHandle<Object> duration,
    DirectHandle<Object> options, DirectHandle<Object> date_add) {
  // 1. Assert: Type(options) is Object or Undefined.
  DCHECK(IsJSReceiver(*options) || IsUndefined(*options));

  // 3. Let addedDate be ? Call(dateAdd, calendar, « date, duration, options »).
  DirectHandle<Object> args[] = {date, duration, options};
  DirectHandle<Object> added_date;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, added_date,
      Execution::Call(isolate, date_add, calendar, base::VectorOf(args)));
  // 4. Perform ? RequireInternalSlot(addedDate, [[InitializedTemporalDate]]).
  if (!IsJSTemporalPlainDate(*added_date)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }
  // 5. Return addedDate.
  return Cast<JSTemporalPlainDate>(added_date);
}

MaybeDirectHandle<JSTemporalDuration> CalendarDateUntil(
    Isolate* isolate, DirectHandle<JSReceiver> calendar,
    DirectHandle<Object> one, DirectHandle<Object> two,
    DirectHandle<Object> options) {
  return CalendarDateUntil(isolate, calendar, one, two, options,
                           isolate->factory()->undefined_value());
}

MaybeDirectHandle<JSTemporalDuration> CalendarDateUntil(
    Isolate* isolate, DirectHandle<JSReceiver> calendar,
    DirectHandle<Object> one, DirectHandle<Object> two,
    DirectHandle<Object> options, DirectHandle<Object> date_until) {
  // 1. Assert: Type(calendar) is Object.
  // 2. If dateUntil is not present, set dateUntil to ? GetMethod(calendar,
  // "dateUntil").
  if (IsUndefined(*date_until)) {
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, date_until,
        Object::GetMethod(isolate, calendar,
                          isolate->factory()->dateUntil_string()));
  }
  // 3. Let duration be ? Call(dateUntil, calendar, « one, two, options »).
  DirectHandle<Object> args[] = {one, two, options};
  DirectHandle<Object> duration;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, duration,
      Execution::Call(isolate, date_until, calendar, base::VectorOf(args)));
  // 4. Perform ? RequireInternalSlot(duration,
  // [[InitializedTemporalDuration]]).
  if (!IsJSTemporalDuration(*duration)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }
  // 5. Return duration.
  return Cast<JSTemporalDuration>(duration);
}

// #sec-temporal-defaultmergefields
MaybeDirectHandle<JSReceiver> DefaultMergeFields(
    Isolate* isolate, DirectHandle<JSReceiver> fields,
    DirectHandle<JSReceiver> additional_fields) {
  Factory* factory = isolate->factory();
  // 1. Let merged be ! OrdinaryObjectCreate(%Object.prototype%).
  DirectHandle<JSObject> merged =
      isolate->factory()->NewJSObject(isolate->object_function());

  // 2. Let originalKeys be ? EnumerableOwnPropertyNames(fields, key).
  DirectHandle<FixedArray> original_keys;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, original_keys,
      KeyAccumulator::GetKeys(isolate, fields, KeyCollectionMode::kOwnOnly,
                              ENUMERABLE_STRINGS,
                              GetKeysConversion::kConvertToString));
  // 3. For each element nextKey of originalKeys, do
  for (int i = 0; i < original_keys->length(); i++) {
    // a. If nextKey is not "month" or "monthCode", then
    DirectHandle<Object> next_key(original_keys->get(i), isolate);
    DCHECK(IsString(*next_key));
    DirectHandle<String> next_key_string = Cast<String>(next_key);
    if (!(String::Equals(isolate, factory->month_string(), next_key_string) ||
          String::Equals(isolate, factory->monthCode_string(),
                         next_key_string))) {
      // i. Let propValue be ? Get(fields, nextKey).
      DirectHandle<Object> prop_value;
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, prop_value,
          Object::GetPropertyOrElement(isolate, fields, next_key_string));
      // ii. If propValue is not undefined, then
      if (!IsUndefined(*prop_value)) {
        // 1. Perform ! CreateDataPropertyOrThrow(merged, nextKey,
        // propValue).
        CHECK(JSReceiver::CreateDataProperty(isolate, merged, next_key_string,
                                             prop_value, Just(kDontThrow))
                  .FromJust());
      }
    }
  }
  // 4. Let newKeys be ? EnumerableOwnPropertyNames(additionalFields, key).
  DirectHandle<FixedArray> new_keys;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, new_keys,
      KeyAccumulator::GetKeys(isolate, additional_fields,
                              KeyCollectionMode::kOwnOnly, ENUMERABLE_STRINGS,
                              GetKeysConversion::kConvertToString));
  bool new_keys_has_month_or_month_code = false;
  // 5. For each element nextKey of newKeys, do
  for (int i = 0; i < new_keys->length(); i++) {
    DirectHandle<Object> next_key(new_keys->get(i), isolate);
    DCHECK(IsString(*next_key));
    DirectHandle<String> next_key_string = Cast<String>(next_key);
    // a. Let propValue be ? Get(additionalFields, nextKey).
    DirectHandle<Object> prop_value;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, prop_value,
        Object::GetPropertyOrElement(isolate, additional_fields,
                                     next_key_string));
    // b. If propValue is not undefined, then
    if (!IsUndefined(*prop_value)) {
      // 1. Perform ! CreateDataPropertyOrThrow(merged, nextKey, propValue).
      CHECK(JSReceiver::CreateDataProperty(isolate, merged, next_key_string,
                                           prop_value, Just(kDontThrow))
                .FromJust());
    }
    new_keys_has_month_or_month_code |=
        String::Equals(isolate, factory->month_string(), next_key_string) ||
        String::Equals(isolate, factory->monthCode_string(), next_key_string);
  }
  // 6. If newKeys does not contain either "month" or "monthCode", then
  if (!new_keys_has_month_or_month_code) {
    // a. Let month be ? Get(fields, "month").
    DirectHandle<Object> month;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, month,
        JSReceiver::GetProperty(isolate, fields, factory->month_string()));
    // b. If month is not undefined, then
    if (!IsUndefined(*month)) {
      // i. Perform ! CreateDataPropertyOrThrow(merged, "month", month).
      CHECK(JSReceiver::CreateDataProperty(isolate, merged,
                                           factory->month_string(), month,
                                           Just(kDontThrow))
                .FromJust());
    }
    // c. Let monthCode be ? Get(fields, "monthCode").
    DirectHandle<Object> month_code;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, month_code,
        JSReceiver::GetProperty(isolate, fields, factory->monthCode_string()));
    // d. If monthCode is not undefined, then
    if (!IsUndefined(*month_code)) {
      // i. Perform ! CreateDataPropertyOrThrow(merged, "monthCode", monthCode).
      CHECK(JSReceiver::CreateDataProperty(isolate, merged,
                                           factory->monthCode_string(),
                                           month_code, Just(kDontThrow))
                .FromJust());
    }
  }
  // 7. Return merged.
  return merged;
}

// #sec-temporal-getoffsetnanosecondsfor
Maybe<int64_t> GetOffsetNanosecondsFor(Isolate* isolate,
                                       DirectHandle<JSReceiver> time_zone_obj,
                                       DirectHandle<Object> instant,
                                       const char* method_name) {
  TEMPORAL_ENTER_FUNC();
  // 1. Let getOffsetNanosecondsFor be ? GetMethod(timeZone,
  // "getOffsetNanosecondsFor").
  DirectHandle<Object> get_offset_nanoseconds_for;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, get_offset_nanoseconds_for,
      Object::GetMethod(isolate, time_zone_obj,
                        isolate->factory()->getOffsetNanosecondsFor_string()),
      Nothing<int64_t>());
  if (!IsCallable(*get_offset_nanoseconds_for)) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewTypeError(MessageTemplate::kCalledNonCallable,
                     isolate->factory()->getOffsetNanosecondsFor_string()),
        Nothing<int64_t>());
  }
  DirectHandle<Object> offset_nanoseconds_obj;
  // 3. Let offsetNanoseconds be ? Call(getOffsetNanosecondsFor, timeZone, «
  // instant »).
  DirectHandle<Object> args[] = {instant};
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, offset_nanoseconds_obj,
      Execution::Call(isolate, get_offset_nanoseconds_for, time_zone_obj,
                      base::VectorOf(args)),
      Nothing<int64_t>());

  // 4. If Type(offsetNanoseconds) is not Number, throw a TypeError exception.
  if (!IsNumber(*offset_nanoseconds_obj)) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR(),
                                 Nothing<int64_t>());
  }

  // 5. If ! IsIntegralNumber(offsetNanoseconds) is false, throw a RangeError
  // exception.
  if (!IsIntegralNumber(isolate, offset_nanoseconds_obj)) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(), Nothing<int64_t>());
  }
  double offset_nanoseconds =
      Object::NumberValue(Cast<Number>(*offset_nanoseconds_obj));

  // 6. Set offsetNanoseconds to ℝ(offsetNanoseconds).
  int64_t offset_nanoseconds_int = static_cast<int64_t>(offset_nanoseconds);
  // 7. If abs(offsetNanoseconds) >= 86400 × 10^9, throw a RangeError exception.
  if (std::abs(offset_nanoseconds_int) >= 86400e9) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(), Nothing<int64_t>());
  }
  // 8. Return offsetNanoseconds.
  return Just(offset_nanoseconds_int);
}

// #sec-temporal-topositiveinteger
MaybeDirectHandle<Number> ToPositiveInteger(Isolate* isolate,
                                            DirectHandle<Object> argument) {
  TEMPORAL_ENTER_FUNC();

  // 1. Let integer be ? ToInteger(argument).
  DirectHandle<Number> integer;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, integer,
                             ToIntegerThrowOnInfinity(isolate, argument));
  // 2. If integer ≤ 0, then
  if (NumberToInt32(*integer) <= 0) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }
  return integer;
}

}  // namespace

namespace temporal {
MaybeDirectHandle<Object> InvokeCalendarMethod(
    Isolate* isolate, DirectHandle<JSReceiver> calendar,
    DirectHandle<String> name, DirectHandle<JSReceiver> date_like) {
  DirectHandle<Object> result;
  /* 1. Assert: Type(calendar) is Object. */
  DCHECK(calendar->TaggedImpl::IsObject());
  /* 2. Let result be ? Invoke(calendar, #name, « dateLike »). */
  DirectHandle<Object> function;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, function,
                             Object::GetProperty(isolate, calendar, name));
  if (!IsCallable(*function)) {
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kCalledNonCallable, name));
  }
  DirectHandle<Object> args[] = {date_like};
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, result,
      Execution::Call(isolate, function, calendar, base::VectorOf(args)));
  return result;
}

#define CALENDAR_ABSTRACT_OPERATION_INT_ACTION(Name, name, Action)             \
  MaybeDirectHandle<Smi> Calendar##Name(Isolate* isolate,                      \
                                        DirectHandle<JSReceiver> calendar,     \
                                        DirectHandle<JSReceiver> date_like) {  \
    /* 1. Assert: Type(calendar) is Object.   */                               \
    /* 2. Let result be ? Invoke(calendar, property, « dateLike »). */       \
    DirectHandle<Object> result;                                               \
    ASSIGN_RETURN_ON_EXCEPTION(                                                \
        isolate, result,                                                       \
        InvokeCalendarMethod(isolate, calendar,                                \
                             isolate->factory()->name##_string(), date_like)); \
    /* 3. If result is undefined, throw a RangeError exception. */             \
    if (IsUndefined(*result)) {                                                \
      THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());        \
    }                                                                          \
    /* 4. Return ? Action(result). */                                          \
    ASSIGN_RETURN_ON_EXCEPTION(isolate, result, Action(isolate, result));      \
    return direct_handle(                                                      \
        Smi::FromInt(Object::NumberValue(Cast<Number>(*result))), isolate);    \
  }

#define CALENDAR_ABSTRACT_OPERATION(Name, property)                      \
  MaybeDirectHandle<Object> Calendar##Name(                              \
      Isolate* isolate, DirectHandle<JSReceiver> calendar,               \
      DirectHandle<JSReceiver> date_like) {                              \
    return InvokeCalendarMethod(isolate, calendar,                       \
                                isolate->factory()->property##_string(), \
                                date_like);                              \
  }

// #sec-temporal-calendaryear
CALENDAR_ABSTRACT_OPERATION_INT_ACTION(Year, year, ToIntegerThrowOnInfinity)
// #sec-temporal-calendarmonth
CALENDAR_ABSTRACT_OPERATION_INT_ACTION(Month, month, ToPositiveInteger)
// #sec-temporal-calendarday
CALENDAR_ABSTRACT_OPERATION_INT_ACTION(Day, day, ToPositiveInteger)
// #sec-temporal-calendarmonthcode
MaybeDirectHandle<Object> CalendarMonthCode(
    Isolate* isolate, DirectHandle<JSReceiver> calendar,
    DirectHandle<JSReceiver> date_like) {
  // 1. Assert: Type(calendar) is Object.
  // 2. Let result be ? Invoke(calendar, monthCode , « dateLike »).
  DirectHandle<Object> result;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, result,
      InvokeCalendarMethod(isolate, calendar,
                           isolate->factory()->monthCode_string(), date_like));
  /* 3. If result is undefined, throw a RangeError exception. */
  if (IsUndefined(*result)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }
  // 4. Return ? ToString(result).
  return Object::ToString(isolate, result);
}

#ifdef V8_INTL_SUPPORT
// #sec-temporal-calendarerayear
MaybeDirectHandle<Object> CalendarEraYear(Isolate* isolate,
                                          DirectHandle<JSReceiver> calendar,
                                          DirectHandle<JSReceiver> date_like) {
  // 1. Assert: Type(calendar) is Object.
  // 2. Let result be ? Invoke(calendar, eraYear , « dateLike »).
  DirectHandle<Object> result;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, result,
      InvokeCalendarMethod(isolate, calendar,
                           isolate->factory()->eraYear_string(), date_like));
  // 3. If result is not undefined, set result to ? ToIntegerOrInfinity(result).
  if (!IsUndefined(*result)) {
    ASSIGN_RETURN_ON_EXCEPTION(isolate, result,
                               ToIntegerThrowOnInfinity(isolate, result));
  }
  // 4. Return result.
  return result;
}

// #sec-temporal-calendarera
MaybeDirectHandle<Object> CalendarEra(Isolate* isolate,
                                      DirectHandle<JSReceiver> calendar,
                                      DirectHandle<JSReceiver> date_like) {
  // 1. Assert: Type(calendar) is Object.
  // 2. Let result be ? Invoke(calendar, era , « dateLike »).
  DirectHandle<Object> result;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, result,
      InvokeCalendarMethod(isolate, calendar, isolate->factory()->era_string(),
                           date_like));
  // 3. If result is not undefined, set result to ? ToString(result).
  if (!IsUndefined(*result)) {
    ASSIGN_RETURN_ON_EXCEPTION(isolate, result,
                               Object::ToString(isolate, result));
  }
  // 4. Return result.
  return result;
}

#endif  //  V8_INTL_SUPPORT

// #sec-temporal-calendardayofweek
CALENDAR_ABSTRACT_OPERATION(DayOfWeek, dayOfWeek)
// #sec-temporal-calendardayofyear
CALENDAR_ABSTRACT_OPERATION(DayOfYear, dayOfYear)
// #sec-temporal-calendarweekofyear
CALENDAR_ABSTRACT_OPERATION(WeekOfYear, weekOfYear)
// #sec-temporal-calendardaysinweek
CALENDAR_ABSTRACT_OPERATION(DaysInWeek, daysInWeek)
// #sec-temporal-calendardaysinmonth
CALENDAR_ABSTRACT_OPERATION(DaysInMonth, daysInMonth)
// #sec-temporal-calendardaysinyear
CALENDAR_ABSTRACT_OPERATION(DaysInYear, daysInYear)
// #sec-temporal-calendarmonthsinyear
CALENDAR_ABSTRACT_OPERATION(MonthsInYear, monthsInYear)
// #sec-temporal-calendarinleapyear
CALENDAR_ABSTRACT_OPERATION(InLeapYear, inLeapYear)

// #sec-temporal-getiso8601calendar
DirectHandle<JSTemporalCalendar> GetISO8601Calendar(Isolate* isolate) {
  return CreateTemporalCalendar(isolate, isolate->factory()->iso8601_string())
      .ToHandleChecked();
}

}  // namespace temporal

namespace {

bool IsUTC(Isolate* isolate, DirectHandle<String> time_zone) {
  // 1. Assert: Type(timeZone) is String.
  // 2. Let tzText be ! StringToCodePoints(timeZone).
  // 3. Let tzUpperText be the result of toUppercase(tzText), according to the
  // Unicode Default Case Conversion algorithm.
  // 4. Let tzUpper be ! CodePointsToString(tzUpperText).
  // 5. If tzUpper and "UTC" are the same sequence of code points, return true.
  // 6. Return false.
  if (time_zone->length() != 3) return false;
  time_zone = String::Flatten(isolate, time_zone);
  DisallowGarbageCollection no_gc;
  const String::FlatContent& flat = time_zone->GetFlatContent(no_gc);
  return (flat.Get(0) == u'U' || flat.Get(0) == u'u') &&
         (flat.Get(1) == u'T' || flat.Get(1) == u't') &&
         (flat.Get(2) == u'C' || flat.Get(2) == u'c');
}

#ifdef V8_INTL_SUPPORT
class CalendarMap final {
 public:
  CalendarMap() {
    icu::Locale locale("und");
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::StringEnumeration> enumeration(
        icu::Calendar::getKeywordValuesForLocale("ca", locale, false, status));
    calendar_ids.push_back("iso8601");
    calendar_id_indices.insert({"iso8601", 0});
    int32_t i = 1;
    for (const char* item = enumeration->next(nullptr, status);
         U_SUCCESS(status) && item != nullptr;
         item = enumeration->next(nullptr, status)) {
      if (strcmp(item, "iso8601") != 0) {
        const char* type = uloc_toUnicodeLocaleType("ca", item);
        calendar_ids.push_back(type);
        calendar_id_indices.insert({type, i++});
      }
    }
  }
  bool Contains(const std::string& id) const {
    return calendar_id_indices.find(id) != calendar_id_indices.end();
  }

  std::string Id(int32_t index) const {
    DCHECK_LT(index, calendar_ids.size());
    return calendar_ids[index];
  }

  int32_t Index(const char* id) const {
    return calendar_id_indices.find(id)->second;
  }

 private:
  std::map<std::string, int32_t> calendar_id_indices;
  std::vector<std::string> calendar_ids;
};

DEFINE_LAZY_LEAKY_OBJECT_GETTER(CalendarMap, GetCalendarMap)

bool IsBuiltinCalendar(Isolate* isolate, DirectHandle<String> id) {
  // 1. Let calendars be AvailableCalendars().
  // 2. If calendars contains the ASCII-lowercase of id, return true.
  // 3. Return false.
  id = Intl::ConvertToLower(isolate, String::Flatten(isolate, id))
           .ToHandleChecked();
  return GetCalendarMap()->Contains(id->ToCString().get());
}

DirectHandle<String> CalendarIdentifier(Isolate* isolate, int32_t index) {
  return isolate->factory()->NewStringFromAsciiChecked(
      GetCalendarMap()->Id(index).c_str());
}

int32_t CalendarIndex(Isolate* isolate, DirectHandle<String> id) {
  id = Intl::ConvertToLower(isolate, String::Flatten(isolate, id))
           .ToHandleChecked();
  return GetCalendarMap()->Index(id->ToCString().get());
}

bool IsValidTimeZoneName(Isolate* isolate, DirectHandle<String> time_zone) {
  return Intl::IsValidTimeZoneName(isolate, time_zone);
}

DirectHandle<String> CanonicalizeTimeZoneName(Isolate* isolate,
                                              DirectHandle<String> identifier) {
  return Intl::CanonicalizeTimeZoneName(isolate, identifier).ToHandleChecked();
}

#else   // V8_INTL_SUPPORT
DirectHandle<String> CalendarIdentifier(Isolate* isolate, int32_t index) {
  DCHECK_EQ(index, 0);
  return isolate->factory()->iso8601_string();
}

// #sec-temporal-isbuiltincalendar
bool IsBuiltinCalendar(Isolate* isolate, DirectHandle<String> id) {
  // Note: For build without intl support, the only item in AvailableCalendars()
  // is "iso8601".
  // 1. Let calendars be AvailableCalendars().
  // 2. If calendars contains the ASCII-lowercase of id, return true.
  // 3. Return false.

  // Fast path
  if (isolate->factory()->iso8601_string()->Equals(*id)) return true;
  if (id->length() != 7) return false;
  id = String::Flatten(isolate, id);

  DisallowGarbageCollection no_gc;
  const String::FlatContent& flat = id->GetFlatContent(no_gc);
  // Return true if id is case insensitive equals to "iso8601".
  return AsciiAlphaToLower(flat.Get(0)) == 'i' &&
         AsciiAlphaToLower(flat.Get(1)) == 's' &&
         AsciiAlphaToLower(flat.Get(2)) == 'o' && flat.Get(3) == '8' &&
         flat.Get(4) == '6' && flat.Get(5) == '0' && flat.Get(6) == '1';
}

int32_t CalendarIndex(Isolate* isolate, DirectHandle<String> id) { return 0; }

// #sec-isvalidtimezonename
bool IsValidTimeZoneName(Isolate* isolate, DirectHandle<String> time_zone) {
  return IsUTC(isolate, time_zone);
}

// #sec-canonicalizetimezonename
DirectHandle<String> CanonicalizeTimeZoneName(Isolate* isolate,
                                              DirectHandle<String> identifier) {
  return isolate->factory()->UTC_string();
}
#endif  // V8_INTL_SUPPORT

// Common routine shared by ToTemporalTimeRecord and ToPartialTime
// #sec-temporal-topartialtime
// #sec-temporal-totemporaltimerecord
Maybe<TimeRecord> ToTemporalTimeRecordOrPartialTime(
    Isolate* isolate, DirectHandle<JSReceiver> temporal_time_like,
    const TimeRecord& time, bool skip_undefined, const char* method_name) {
  TEMPORAL_ENTER_FUNC();

  TimeRecord result(time);
  Factory* factory = isolate->factory();
  // 1. Assert: Type(temporalTimeLike) is Object.
  // 2. Let result be the new Record { [[Hour]]: undefined, [[Minute]]:
  // undefined, [[Second]]: undefined, [[Millisecond]]: undefined,
  // [[Microsecond]]: undefined, [[Nanosecond]]: undefined }.
  // See https://github.com/tc39/proposal-temporal/pull/1862
  // 3. Let _any_ be *false*.
  bool any = false;
  // 4. For each row of Table 4, except the header row, in table order, do
  std::array<std::pair<DirectHandle<String>, int32_t*>, 6> table4 = {
      {{factory->hour_string(), &result.hour},
       {factory->microsecond_string(), &result.microsecond},
       {factory->millisecond_string(), &result.millisecond},
       {factory->minute_string(), &result.minute},
       {factory->nanosecond_string(), &result.nanosecond},
       {factory->second_string(), &result.second}}};
  for (const auto& row : table4) {
    DirectHandle<Object> value;
    // a. Let property be the Property value of the current row.
    // b. Let value be ? Get(temporalTimeLike, property).
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, value,
        JSReceiver::GetProperty(isolate, temporal_time_like, row.first),
        Nothing<TimeRecord>());
    // c. If value is not undefined, then
    if (!IsUndefined(*value)) {
      // i. Set _any_ to *true*.
      any = true;
      // If it is inside ToPartialTime, we only continue if it is not undefined.
    } else if (skip_undefined) {
      continue;
    }
    // d. / ii. Set value to ? ToIntegerThrowOnOInfinity(value).
    DirectHandle<Number> value_number;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, value_number,
                                     ToIntegerThrowOnInfinity(isolate, value),
                                     Nothing<TimeRecord>());
    // e. / iii. Set result's internal slot whose name is the Internal Slot
    // value of the current row to value.
    *(row.second) = Object::NumberValue(*value_number);
  }

  // 5. If _any_ is *false*, then
  if (!any) {
    // a. Throw a *TypeError* exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR(),
                                 Nothing<TimeRecord>());
  }
  // 4. Return result.
  return Just(result);
}

// #sec-temporal-topartialtime
Maybe<TimeRecord> ToPartialTime(Isolate* isolate,
                                DirectHandle<JSReceiver> temporal_time_like,
                                const TimeRecord& time,
                                const char* method_name) {
  return ToTemporalTimeRecordOrPartialTime(isolate, temporal_time_like, time,
                                           true, method_name);
}

// #sec-temporal-totemporaltimerecord
Maybe<TimeRecord> ToTemporalTimeRecord(
    Isolate* isolate, DirectHandle<JSReceiver> temporal_time_like,
    const char* method_name) {
  return ToTemporalTimeRecordOrPartialTime(
      isolate, temporal_time_like,
      {kMinInt31, kMinInt31, kMinInt31, kMinInt31, kMinInt31, kMinInt31}, false,
      method_name);
}

// #sec-temporal-gettemporalunit
// In the spec text, the extraValues is defined as an optional argument of
// "a List of ECMAScript language values". Most of the caller does not pass in
// value for extraValues, which is represented by the default Unit::kNotPresent.
// For the three places in the spec text calling GetTemporalUnit with
// an extraValues argument:
// << "day" >> is passed in as in the algorithm of
//   Temporal.PlainDateTime.prototype.round() and
//   Temporal.ZonedDateTime.prototype.round();
// << "auto" >> is passed in as in the algorithm of
// Temporal.Duration.prototype.round().
// Therefore we can simply use a Unit of three possible value, the default
// Unit::kNotPresent, Unit::kDay, and Unit::kAuto to cover all the possible
// value for extraValues.
Maybe<Unit> GetTemporalUnit(Isolate* isolate,
                            DirectHandle<JSReceiver> normalized_options,
                            const char* key, UnitGroup unit_group,
                            Unit default_value, bool default_is_required,
                            const char* method_name,
                            Unit extra_values = Unit::kNotPresent) {
  std::vector<const char*> str_values;
  std::vector<Unit> enum_values;
  switch (unit_group) {
    case UnitGroup::kDate:
      if (default_value == Unit::kAuto || extra_values == Unit::kAuto) {
        str_values = {"year",  "month",  "week",  "day", "auto",
                      "years", "months", "weeks", "days"};
        enum_values = {Unit::kYear,  Unit::kMonth, Unit::kWeek,
                       Unit::kDay,   Unit::kAuto,  Unit::kYear,
                       Unit::kMonth, Unit::kWeek,  Unit::kDay};
      } else {
        DCHECK(default_value == Unit::kNotPresent ||
               default_value == Unit::kYear || default_value == Unit::kMonth ||
               default_value == Unit::kWeek || default_value == Unit::kDay);
        str_values = {"year",  "month",  "week",  "day",
                      "years", "months", "weeks", "days"};
        enum_values = {Unit::kYear, Unit::kMonth, Unit::kWeek, Unit::kDay,
                       Unit::kYear, Unit::kMonth, Unit::kWeek, Unit::kDay};
      }
      break;
    case UnitGroup::kTime:
      if (default_value == Unit::kAuto || extra_values == Unit::kAuto) {
        str_values = {"hour",        "minute",       "second",
                      "millisecond", "microsecond",  "nanosecond",
                      "auto",        "hours",        "minutes",
                      "seconds",     "milliseconds", "microseconds",
                      "nanoseconds"};
        enum_values = {
            Unit::kHour,        Unit::kMinute,      Unit::kSecond,
            Unit::kMillisecond, Unit::kMicrosecond, Unit::kNanosecond,
            Unit::kAuto,        Unit::kHour,        Unit::kMinute,
            Unit::kSecond,      Unit::kMillisecond, Unit::kMicrosecond,
            Unit::kNanosecond};
      } else if (default_value == Unit::kDay || extra_values == Unit::kDay) {
        str_values = {"hour",        "minute",       "second",
                      "millisecond", "microsecond",  "nanosecond",
                      "day",         "hours",        "minutes",
                      "seconds",     "milliseconds", "microseconds",
                      "nanoseconds", "days"};
        enum_values = {
            Unit::kHour,        Unit::kMinute,      Unit::kSecond,
            Unit::kMillisecond, Unit::kMicrosecond, Unit::kNanosecond,
            Unit::kDay,         Unit::kHour,        Unit::kMinute,
            Unit::kSecond,      Unit::kMillisecond, Unit::kMicrosecond,
            Unit::kNanosecond,  Unit::kDay};
      } else {
        DCHECK(default_value == Unit::kNotPresent ||
               default_value == Unit::kHour || default_value == Unit::kMinute ||
               default_value == Unit::kSecond ||
               default_value == Unit::kMillisecond ||
               default_value == Unit::kMicrosecond ||
               default_value == Unit::kNanosecond);
        str_values = {"hour",         "minute",       "second",
                      "millisecond",  "microsecond",  "nanosecond",
                      "hours",        "minutes",      "seconds",
                      "milliseconds", "microseconds", "nanoseconds"};
        enum_values = {
            Unit::kHour,        Unit::kMinute,      Unit::kSecond,
            Unit::kMillisecond, Unit::kMicrosecond, Unit::kNanosecond,
            Unit::kHour,        Unit::kMinute,      Unit::kSecond,
            Unit::kMillisecond, Unit::kMicrosecond, Unit::kNanosecond};
      }
      break;
    case UnitGroup::kDateTime:
      if (default_value == Unit::kAuto || extra_values == Unit::kAuto) {
        str_values = {"year",         "month",        "week",
                      "day",          "hour",         "minute",
                      "second",       "millisecond",  "microsecond",
                      "nanosecond",   "auto",         "years",
                      "months",       "weeks",        "days",
                      "hours",        "minutes",      "seconds",
                      "milliseconds", "microseconds", "nanoseconds"};
        enum_values = {
            Unit::kYear,        Unit::kMonth,       Unit::kWeek,
            Unit::kDay,         Unit::kHour,        Unit::kMinute,
            Unit::kSecond,      Unit::kMillisecond, Unit::kMicrosecond,
            Unit::kNanosecond,  Unit::kAuto,        Unit::kYear,
            Unit::kMonth,       Unit::kWeek,        Unit::kDay,
            Unit::kHour,        Unit::kMinute,      Unit::kSecond,
            Unit::kMillisecond, Unit::kMicrosecond, Unit::kNanosecond};
      } else {
        str_values = {
            "year",        "month",        "week",         "day",
            "hour",        "minute",       "second",       "millisecond",
            "microsecond", "nanosecond",   "years",        "months",
            "weeks",       "days",         "hours",        "minutes",
            "seconds",     "milliseconds", "microseconds", "nanoseconds"};
        enum_values = {
            Unit::kYear,        Unit::kMonth,       Unit::kWeek,
            Unit::kDay,         Unit::kHour,        Unit::kMinute,
            Unit::kSecond,      Unit::kMillisecond, Unit::kMicrosecond,
            Unit::kNanosecond,  Unit::kYear,        Unit::kMonth,
            Unit::kWeek,        Unit::kDay,         Unit::kHour,
            Unit::kMinute,      Unit::kSecond,      Unit::kMillisecond,
            Unit::kMicrosecond, Unit::kNanosecond};
      }
      break;
  }

  // 4. If default is required, then
  if (default_is_required) default_value = Unit::kNotPresent;
  // a. Let defaultValue be undefined.
  // 5. Else,
  // a. Let defaultValue be default.
  // b. If defaultValue is not undefined and singularNames does not contain
  // defaultValue, then i. Append defaultValue to singularNames.

  // 9. Let value be ? GetOption(normalizedOptions, key, "string",
  // allowedValues, defaultValue).
  Unit value;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, value,
      GetStringOption<Unit>(isolate, normalized_options, key, method_name,
                            str_values, enum_values, default_value),
      Nothing<Unit>());

  // 10. If value is undefined and default is required, throw a RangeError
  // exception.
  if (default_is_required && value == Unit::kNotPresent) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewRangeError(
            MessageTemplate::kValueOutOfRange,
            isolate->factory()->undefined_value(),
            isolate->factory()->NewStringFromAsciiChecked(method_name),
            isolate->factory()->NewStringFromAsciiChecked(key)),
        Nothing<Unit>());
  }
  // 12. Return value.
  return Just(value);
}

// #sec-temporal-mergelargestunitoption
MaybeDirectHandle<JSReceiver> MergeLargestUnitOption(
    Isolate* isolate, DirectHandle<JSReceiver> options, Unit largest_unit) {
  TEMPORAL_ENTER_FUNC();
  // 1. Let merged be OrdinaryObjectCreate(null).
  DirectHandle<JSReceiver> merged =
      isolate->factory()->NewJSObjectWithNullProto();
  // 2. Let keys be ? EnumerableOwnPropertyNames(options, key).
  // 3. For each element nextKey of keys, do
  // a. Let propValue be ? Get(options, nextKey).
  // b. Perform ! CreateDataPropertyOrThrow(merged, nextKey, propValue).
  JSReceiver::SetOrCopyDataProperties(
      isolate, merged, options, PropertiesEnumerationMode::kEnumerationOrder,
      {}, false)
      .Check();

  // 4. Perform ! CreateDataPropertyOrThrow(merged, "largestUnit", largestUnit).
  CHECK(JSReceiver::CreateDataProperty(
            isolate, merged, isolate->factory()->largestUnit_string(),
            UnitToString(isolate, largest_unit), Just(kThrowOnError))
            .FromJust());
  // 5. Return merged.
  return merged;
}

// #sec-temporal-tointegerthrowoninfinity
MaybeDirectHandle<Number> ToIntegerThrowOnInfinity(
    Isolate* isolate, DirectHandle<Object> argument) {
  TEMPORAL_ENTER_FUNC();

  // 1. Let integer be ? ToIntegerOrInfinity(argument).
  DirectHandle<Number> integer;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, integer,
                             Object::ToInteger(isolate, argument));
  // 2. If integer is +∞ or -∞, throw a RangeError exception.
  if (!std::isfinite(Object::NumberValue(*integer))) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }
  return integer;
}

// #sec-temporal-largeroftwotemporalunits
Unit LargerOfTwoTemporalUnits(Unit u1, Unit u2) {
  // 1. If either u1 or u2 is "year", return "year".
  if (u1 == Unit::kYear || u2 == Unit::kYear) return Unit::kYear;
  // 2. If either u1 or u2 is "month", return "month".
  if (u1 == Unit::kMonth || u2 == Unit::kMonth) return Unit::kMonth;
  // 3. If either u1 or u2 is "week", return "week".
  if (u1 == Unit::kWeek || u2 == Unit::kWeek) return Unit::kWeek;
  // 4. If either u1 or u2 is "day", return "day".
  if (u1 == Unit::kDay || u2 == Unit::kDay) return Unit::kDay;
  // 5. If either u1 or u2 is "hour", return "hour".
  if (u1 == Unit::kHour || u2 == Unit::kHour) return Unit::kHour;
  // 6. If either u1 or u2 is "minute", return "minute".
  if (u1 == Unit::kMinute || u2 == Unit::kMinute) return Unit::kMinute;
  // 7. If either u1 or u2 is "second", return "second".
  if (u1 == Unit::kSecond || u2 == Unit::kSecond) return Unit::kSecond;
  // 8. If either u1 or u2 is "millisecond", return "millisecond".
  if (u1 == Unit::kMillisecond || u2 == Unit::kMillisecond)
    return Unit::kMillisecond;
  // 9. If either u1 or u2 is "microsecond", return "microsecond".
  if (u1 == Unit::kMicrosecond || u2 == Unit::kMicrosecond)
    return Unit::kMicrosecond;
  // 10. Return "nanosecond".
  return Unit::kNanosecond;
}

DirectHandle<String> UnitToString(Isolate* isolate, Unit unit) {
  switch (unit) {
    case Unit::kYear:
      return isolate->factory()->year_string();
    case Unit::kMonth:
      return isolate->factory()->month_string();
    case Unit::kWeek:
      return isolate->factory()->week_string();
    case Unit::kDay:
      return isolate->factory()->day_string();
    case Unit::kHour:
      return isolate->factory()->hour_string();
    case Unit::kMinute:
      return isolate->factory()->minute_string();
    case Unit::kSecond:
      return isolate->factory()->second_string();
    case Unit::kMillisecond:
      return isolate->factory()->millisecond_string();
    case Unit::kMicrosecond:
      return isolate->factory()->microsecond_string();
    case Unit::kNanosecond:
      return isolate->factory()->nanosecond_string();
    case Unit::kNotPresent:
    case Unit::kAuto:
      UNREACHABLE();
  }
}

// #sec-temporal-create-iso-date-record
DateRecord CreateISODateRecord(Isolate* isolate, const DateRecord& date) {
  // 1. Assert: IsValidISODate(year, month, day) is true.
  DCHECK(IsValidISODate(isolate, date));
  // 2. Return the Record { [[Year]]: year, [[Month]]: month, [[Day]]: day }.
  return date;
}

// #sec-temporal-balanceisodate
DateRecord BalanceISODate(Isolate* isolate, const DateRecord& date) {
  TEMPORAL_ENTER_FUNC();
  // 1. Let epochDays be MakeDay(𝔽(year), 𝔽(month - 1), 𝔽(day)).
  double epoch_days = MakeDay(date.year, date.month - 1, date.day);
  // 2. Assert: epochDays is finite.
  DCHECK(std::isfinite(epoch_days));
  // 3. Let ms be MakeDate(epochDays, +0𝔽).
  double ms = MakeDate(epoch_days, 0);
  // 4. Return CreateISODateRecordWithCalendar(ℝ(YearFromTime(ms)),
  // ℝ(MonthFromTime(ms)) + 1, ℝ(DateFromTime(ms))).
  int year = 0;
  int month = 0;
  int day = 0;
  int wday = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  int millisecond = 0;

  DCHECK(std::isfinite(ms));
  DCHECK_LT(ms, static_cast<double>(std::numeric_limits<int64_t>::max()));
  DCHECK_GT(ms, static_cast<double>(std::numeric_limits<int64_t>::min()));
  isolate->date_cache()->BreakDownTime(ms, &year, &month, &day, &wday, &hour,
                                       &minute, &second, &millisecond);

  return CreateISODateRecord(isolate, {year, month + 1, day});
}

// #sec-temporal-adddatetime
Maybe<DateTimeRecord> AddDateTime(Isolate* isolate,
                                  const DateTimeRecord& date_time,
                                  DirectHandle<JSReceiver> calendar,
                                  const DurationRecord& dur,
                                  DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();

  // 1. Assert: ISODateTimeWithinLimits(year, month, day, hour, minute, second,
  // millisecond, microsecond, nanosecond) is true.
  DCHECK(ISODateTimeWithinLimits(isolate, date_time));
  // 2. Let timeResult be ! AddTime(hour, minute, second, millisecond,
  // microsecond, nanosecond, hours, minutes, seconds, milliseconds,
  // microseconds, nanoseconds).
  const TimeDurationRecord& time = dur.time_duration;
  DateTimeRecord time_result =
      AddTime(isolate, date_time.time,
              {0, time.hours, time.minutes, time.seconds, time.milliseconds,
               time.microseconds, time.nanoseconds});

  // 3. Let datePart be ? CreateTemporalDate(year, month, day, calendar).
  DirectHandle<JSTemporalPlainDate> date_part;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, date_part, CreateTemporalDate(isolate, date_time.date, calendar),
      Nothing<DateTimeRecord>());
  // 4. Let dateDuration be ? CreateTemporalDuration(years, months, weeks, days
  // + timeResult.[[Days]], 0, 0, 0, 0, 0, 0).
  DirectHandle<JSTemporalDuration> date_duration;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, date_duration,
      CreateTemporalDuration(
          isolate,
          {dur.years,
           dur.months,
           dur.weeks,
           {dur.time_duration.days + time_result.date.day, 0, 0, 0, 0, 0, 0}}),
      Nothing<DateTimeRecord>());
  // 5. Let addedDate be ? CalendarDateAdd(calendar, datePart, dateDuration,
  // options).
  DirectHandle<JSTemporalPlainDate> added_date;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, added_date,
      CalendarDateAdd(isolate, calendar, date_part, date_duration, options),
      Nothing<DateTimeRecord>());
  // 6. Return the new Record { [[Year]]: addedDate.[[ISOYear]], [[Month]]:
  // addedDate.[[ISOMonth]], [[Day]]: addedDate.[[ISODay]], [[Hour]]:
  // timeResult.[[Hour]], [[Minute]]: timeResult.[[Minute]], [[Second]]:
  // timeResult.[[Second]], [[Millisecond]]: timeResult.[[Millisecond]],
  // [[Microsecond]]: timeResult.[[Microsecond]], [[Nanosecond]]:
  // timeResult.[[Nanosecond]], }.
  time_result.date = {added_date->iso_year(), added_date->iso_month(),
                      added_date->iso_day()};
  return Just(time_result);
}

// #sec-temporal-balanceduration
Maybe<TimeDurationRecord> BalanceDuration(Isolate* isolate, Unit largest_unit,
                                          const TimeDurationRecord& duration,
                                          const char* method_name) {
  TEMPORAL_ENTER_FUNC();

  // 1. If relativeTo is not present, set relativeTo to undefined.
  return BalanceDuration(isolate, largest_unit,
                         isolate->factory()->undefined_value(), duration,
                         method_name);
}

Maybe<TimeDurationRecord> BalanceDuration(Isolate* isolate, Unit largest_unit,
                                          DirectHandle<BigInt> nanoseconds,
                                          const char* method_name) {
  // 1. Let balanceResult be ? BalancePossiblyInfiniteDuration(days, hours,
  // minutes, seconds, milliseconds, microseconds, nanoseconds, largestUnit,
  // relativeTo).
  BalancePossiblyInfiniteDurationResult balance_result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, balance_result,
      BalancePossiblyInfiniteDuration(isolate, largest_unit, 0, nanoseconds,
                                      method_name),
      Nothing<TimeDurationRecord>());

  // 2. If balanceResult is positive overflow or negative overflow, then
  if (balance_result.overflow != BalanceOverflow::kNone) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<TimeDurationRecord>());
    // 3. Else,
  } else {
    // a. Return balanceResult.
    return Just(balance_result.value);
  }
}

Maybe<TimeDurationRecord> BalanceDuration(Isolate* isolate, Unit largest_unit,
                                          const TimeDurationRecord& dur1,
                                          const TimeDurationRecord& dur2,
                                          const char* method_name) {
  // Add the two TimeDurationRecord as BigInt in nanoseconds.
  DirectHandle<BigInt> nanoseconds =
      BigInt::Add(isolate, TotalDurationNanoseconds(isolate, dur1, 0),
                  TotalDurationNanoseconds(isolate, dur2, 0))
          .ToHandleChecked();
  return BalanceDuration(isolate, largest_unit, nanoseconds, method_name);
}

// #sec-temporal-balanceduration
Maybe<TimeDurationRecord> BalanceDuration(Isolate* isolate, Unit largest_unit,
                                          DirectHandle<Object> relative_to_obj,
                                          const TimeDurationRecord& value,
                                          const char* method_name) {
  // 1. Let balanceResult be ? BalancePossiblyInfiniteDuration(days, hours,
  // minutes, seconds, milliseconds, microseconds, nanoseconds, largestUnit,
  // relativeTo).
  BalancePossiblyInfiniteDurationResult balance_result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, balance_result,
      BalancePossiblyInfiniteDuration(isolate, largest_unit, relative_to_obj,
                                      value, method_name),
      Nothing<TimeDurationRecord>());

  // 2. If balanceResult is positive overflow or negative overflow, then
  if (balance_result.overflow != BalanceOverflow::kNone) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<TimeDurationRecord>());
    // 3. Else,
  } else {
    // a. Return balanceResult.
    return Just(balance_result.value);
  }
}

// sec-temporal-balancepossiblyinfiniteduration
Maybe<BalancePossiblyInfiniteDurationResult> BalancePossiblyInfiniteDuration(
    Isolate* isolate, Unit largest_unit, DirectHandle<Object> relative_to_obj,
    const TimeDurationRecord& value, const char* method_name) {
  TEMPORAL_ENTER_FUNC();
  TimeDurationRecord duration = value;
  DirectHandle<BigInt> nanoseconds;

  // 2. If Type(relativeTo) is Object and relativeTo has an
  // [[InitializedTemporalZonedDateTime]] internal slot, then
  if (IsJSTemporalZonedDateTime(*relative_to_obj)) {
    auto relative_to = Cast<JSTemporalZonedDateTime>(relative_to_obj);
    // a. Let endNs be ? AddZonedDateTime(relativeTo.[[Nanoseconds]],
    // relativeTo.[[TimeZone]], relativeTo.[[Calendar]], 0, 0, 0, days, hours,
    // minutes, seconds, milliseconds, microseconds, nanoseconds).
    DirectHandle<BigInt> end_ns;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, end_ns,
        AddZonedDateTime(isolate,
                         direct_handle(relative_to->nanoseconds(), isolate),
                         direct_handle(relative_to->time_zone(), isolate),
                         direct_handle(relative_to->calendar(), isolate),
                         {0, 0, 0, duration}, method_name),
        Nothing<BalancePossiblyInfiniteDurationResult>());
    // b. Set nanoseconds to endNs − relativeTo.[[Nanoseconds]].
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, nanoseconds,
        BigInt::Subtract(isolate, end_ns,
                         direct_handle(relative_to->nanoseconds(), isolate)),
        Nothing<BalancePossiblyInfiniteDurationResult>());
    // 3. Else,
  } else {
    // a. Set nanoseconds to ℤ(! TotalDurationNanoseconds(days, hours, minutes,
    // seconds, milliseconds, microseconds, nanoseconds, 0)).
    nanoseconds = TotalDurationNanoseconds(isolate, duration, 0);
  }

  // Call the BigInt version for the same process after step 4
  // The only value need to pass in is nanoseconds and days because
  // 1) step 4 and 5 use nanoseconds and days only, and
  // 2) step 6 is "Set hours, minutes, seconds, milliseconds, and microseconds
  // to 0."
  return BalancePossiblyInfiniteDuration(isolate, largest_unit, relative_to_obj,
                                         duration.days, nanoseconds,
                                         method_name);
}

// The special case of BalancePossiblyInfiniteDuration while the nanosecond is a
// large value and days contains non-zero values but the rest are 0.
// This version has no relative_to.
Maybe<BalancePossiblyInfiniteDurationResult> BalancePossiblyInfiniteDuration(
    Isolate* isolate, Unit largest_unit, DirectHandle<Object> relative_to_obj,
    double days, DirectHandle<BigInt> nanoseconds, const char* method_name) {
  TEMPORAL_ENTER_FUNC();

  // 4. If largestUnit is one of "year", "month", "week", or "day", then
  if (largest_unit == Unit::kYear || largest_unit == Unit::kMonth ||
      largest_unit == Unit::kWeek || largest_unit == Unit::kDay) {
    // a. Let result be ? NanosecondsToDays(nanoseconds, relativeTo).
    NanosecondsToDaysResult result;
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, result,
        NanosecondsToDays(isolate, nanoseconds, relative_to_obj, method_name),
        Nothing<BalancePossiblyInfiniteDurationResult>());
    // b. Set days to result.[[Days]].
    days = result.days;
    // c. Set nanoseconds to result.[[Nanoseconds]].
    nanoseconds = BigInt::FromInt64(isolate, result.nanoseconds);
    // 5. Else,
  } else {
    // a. Set days to 0.
    days = 0;
  }
  // 6. Set hours, minutes, seconds, milliseconds, and microseconds to 0.
  DirectHandle<BigInt> thousand = BigInt::FromInt64(isolate, 1000);
  DirectHandle<BigInt> sixty = BigInt::FromInt64(isolate, 60);
  DirectHandle<BigInt> zero = BigInt::FromInt64(isolate, 0);
  DirectHandle<BigInt> hours = zero;
  DirectHandle<BigInt> minutes = zero;
  DirectHandle<BigInt> seconds = zero;
  DirectHandle<BigInt> milliseconds = zero;
  DirectHandle<BigInt> microseconds = zero;

  // 7. If nanoseconds < 0, let sign be −1; else, let sign be 1.
  // 8. Set nanoseconds to abs(nanoseconds).
  int32_t sign = 1;
  if (nanoseconds->IsNegative()) {
    sign = -1;
    nanoseconds = BigInt::UnaryMinus(isolate, nanoseconds);
  }

  // 9 If largestUnit is "year", "month", "week", "day", or "hour", then
  switch (largest_unit) {
    case Unit::kYear:
    case Unit::kMonth:
    case Unit::kWeek:
    case Unit::kDay:
    case Unit::kHour:
      // a. Set microseconds to floor(nanoseconds / 1000).
      microseconds =
          BigInt::Divide(isolate, nanoseconds, thousand).ToHandleChecked();
      // b. Set nanoseconds to nanoseconds modulo 1000.
      nanoseconds =
          BigInt::Remainder(isolate, nanoseconds, thousand).ToHandleChecked();
      // c. Set milliseconds to floor(microseconds / 1000).
      milliseconds =
          BigInt::Divide(isolate, microseconds, thousand).ToHandleChecked();
      // d. Set microseconds to microseconds modulo 1000.
      microseconds =
          BigInt::Remainder(isolate, microseconds, thousand).ToHandleChecked();
      // e. Set seconds to floor(milliseconds / 1000).
      seconds =
          BigInt::Divide(isolate, milliseconds, thousand).ToHandleChecked();
      // f. Set milliseconds to milliseconds modulo 1000.
      milliseconds =
          BigInt::Remainder(isolate, milliseconds, thousand).ToHandleChecked();
      // g. Set minutes to floor(seconds, 60).
      minutes = BigInt::Divide(isolate, seconds, sixty).ToHandleChecked();
      // h. Set seconds to seconds modulo 60.
      seconds = BigInt::Remainder(isolate, seconds, sixty).ToHandleChecked();
      // i. Set hours to floor(minutes / 60).
      hours = BigInt::Divide(isolate, minutes, sixty).ToHandleChecked();
      // j. Set minutes to minutes modulo 60.
      minutes = BigInt::Remainder(isolate, minutes, sixty).ToHandleChecked();
      break;
    // 10. Else if largestUnit is "minute", then
    case Unit::kMinute:
      // a. Set microseconds to floor(nanoseconds / 1000).
      microseconds =
          BigInt::Divide(isolate, nanoseconds, thousand).ToHandleChecked();
      // b. Set nanoseconds to nanoseconds modulo 1000.
      nanoseconds =
          BigInt::Remainder(isolate, nanoseconds, thousand).ToHandleChecked();
      // c. Set milliseconds to floor(microseconds / 1000).
      milliseconds =
          BigInt::Divide(isolate, microseconds, thousand).ToHandleChecked();
      // d. Set microseconds to microseconds modulo 1000.
      microseconds =
          BigInt::Remainder(isolate, microseconds, thousand).ToHandleChecked();
      // e. Set seconds to floor(milliseconds / 1000).
      seconds =
          BigInt::Divide(isolate, milliseconds, thousand).ToHandleChecked();
      // f. Set milliseconds to milliseconds modulo 1000.
      milliseconds =
          BigInt::Remainder(isolate, milliseconds, thousand).ToHandleChecked();
      // g. Set minutes to floor(seconds / 60).
      minutes = BigInt::Divide(isolate, seconds, sixty).ToHandleChecked();
      // h. Set seconds to seconds modulo 60.
      seconds = BigInt::Remainder(isolate, seconds, sixty).ToHandleChecked();
      break;
    // 11. Else if largestUnit is "second", then
    case Unit::kSecond:
      // a. Set microseconds to floor(nanoseconds / 1000).
      microseconds =
          BigInt::Divide(isolate, nanoseconds, thousand).ToHandleChecked();
      // b. Set nanoseconds to nanoseconds modulo 1000.
      nanoseconds =
          BigInt::Remainder(isolate, nanoseconds, thousand).ToHandleChecked();
      // c. Set milliseconds to floor(microseconds / 1000).
      milliseconds =
          BigInt::Divide(isolate, microseconds, thousand).ToHandleChecked();
      // d. Set microseconds to microseconds modulo 1000.
      microseconds =
          BigInt::Remainder(isolate, microseconds, thousand).ToHandleChecked();
      // e. Set seconds to floor(milliseconds / 1000).
      seconds =
          BigInt::Divide(isolate, milliseconds, thousand).ToHandleChecked();
      // f. Set milliseconds to milliseconds modulo 1000.
      milliseconds =
          BigInt::Remainder(isolate, milliseconds, thousand).ToHandleChecked();
      break;
    // 12. Else if largestUnit is "millisecond", then
    case Unit::kMillisecond:
      // a. Set microseconds to floor(nanoseconds / 1000).
      microseconds =
          BigInt::Divide(isolate, nanoseconds, thousand).ToHandleChecked();
      // b. Set nanoseconds to nanoseconds modulo 1000.
      nanoseconds =
          BigInt::Remainder(isolate, nanoseconds, thousand).ToHandleChecked();
      // c. Set milliseconds to floor(microseconds / 1000).
      milliseconds =
          BigInt::Divide(isolate, microseconds, thousand).ToHandleChecked();
      // d. Set microseconds to microseconds modulo 1000.
      microseconds =
          BigInt::Remainder(isolate, microseconds, thousand).ToHandleChecked();
      break;
    // 13. Else if largestUnit is "microsecond", then
    case Unit::kMicrosecond:
      // a. Set microseconds to floor(nanoseconds / 1000).
      microseconds =
          BigInt::Divide(isolate, nanoseconds, thousand).ToHandleChecked();
      // b. Set nanoseconds to nanoseconds modulo 1000.
      nanoseconds =
          BigInt::Remainder(isolate, nanoseconds, thousand).ToHandleChecked();
      break;
    // 14. Else,
    case Unit::kNanosecond:
      // a. Assert: largestUnit is "nanosecond".
      break;
    case Unit::kAuto:
    case Unit::kNotPresent:
      UNREACHABLE();
  }
  // 15. For each value v of « days, hours, minutes, seconds, milliseconds,
  // microseconds, nanoseconds », do a. If 𝔽(v) is not finite, then i. If sign
  // = 1, then
  // 1. Return positive overflow.
  // ii. Else if sign = -1, then
  // 1. Return negative overflow.
  double hours_value = Object::NumberValue(*BigInt::ToNumber(isolate, hours));
  double minutes_value =
      Object::NumberValue(*BigInt::ToNumber(isolate, minutes));
  double seconds_value =
      Object::NumberValue(*BigInt::ToNumber(isolate, seconds));
  double milliseconds_value =
      Object::NumberValue(*BigInt::ToNumber(isolate, milliseconds));
  double microseconds_value =
      Object::NumberValue(*BigInt::ToNumber(isolate, microseconds));
  double nanoseconds_value =
      Object::NumberValue(*BigInt::ToNumber(isolate, nanoseconds));
  if (std::isinf(days) || std::isinf(hours_value) ||
      std::isinf(minutes_value) || std::isinf(seconds_value) ||
      std::isinf(milliseconds_value) || std::isinf(microseconds_value) ||
      std::isinf(nanoseconds_value)) {
    return Just(BalancePossiblyInfiniteDurationResult(
        {{0, 0, 0, 0, 0, 0, 0},
         sign == 1 ? BalanceOverflow::kPositive : BalanceOverflow::kNegative}));
  }

  // 16. Return ? CreateTimeDurationRecord(days, hours × sign, minutes × sign,
  // seconds × sign, milliseconds × sign, microseconds × sign, nanoseconds ×
  // sign).
  TimeDurationRecord result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result,
      TimeDurationRecord::Create(
          isolate, days, hours_value * sign, minutes_value * sign,
          seconds_value * sign, milliseconds_value * sign,
          microseconds_value * sign, nanoseconds_value * sign),
      Nothing<BalancePossiblyInfiniteDurationResult>());
  return Just(
      BalancePossiblyInfiniteDurationResult({result, BalanceOverflow::kNone}));
}

// #sec-temporal-addzoneddatetime
MaybeDirectHandle<BigInt> AddZonedDateTime(
    Isolate* isolate, DirectHandle<BigInt> epoch_nanoseconds,
    DirectHandle<JSReceiver> time_zone, DirectHandle<JSReceiver> calendar,
    const DurationRecord& duration, const char* method_name) {
  TEMPORAL_ENTER_FUNC();

  // 1. If options is not present, set options to undefined.
  return AddZonedDateTime(isolate, epoch_nanoseconds, time_zone, calendar,
                          duration, isolate->factory()->undefined_value(),
                          method_name);
}

// #sec-temporal-addzoneddatetime
MaybeDirectHandle<BigInt> AddZonedDateTime(
    Isolate* isolate, DirectHandle<BigInt> epoch_nanoseconds,
    DirectHandle<JSReceiver> time_zone, DirectHandle<JSReceiver> calendar,
    const DurationRecord& duration, DirectHandle<Object> options,
    const char* method_name) {
  TEMPORAL_ENTER_FUNC();

  TimeDurationRecord time_duration = duration.time_duration;
  // 2. If all of years, months, weeks, and days are 0, then
  if (duration.years == 0 && duration.months == 0 && duration.weeks == 0 &&
      time_duration.days == 0) {
    // a. Return ? AddInstant(epochNanoseconds, hours, minutes, seconds,
    // milliseconds, microseconds, nanoseconds).
    return AddInstant(isolate, epoch_nanoseconds, time_duration);
  }
  // 3. Let instant be ! CreateTemporalInstant(epochNanoseconds).
  DirectHandle<JSTemporalInstant> instant =
      temporal::CreateTemporalInstant(isolate, epoch_nanoseconds)
          .ToHandleChecked();

  // 4. Let temporalDateTime be ?
  // BuiltinTimeZoneGetPlainDateTimeFor(timeZone, instant, calendar).
  DirectHandle<JSTemporalPlainDateTime> temporal_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temporal_date_time,
      temporal::BuiltinTimeZoneGetPlainDateTimeFor(isolate, time_zone, instant,
                                                   calendar, method_name));
  // 5. Let datePart be ? CreateTemporalDate(temporalDateTime.[[ISOYear]],
  // temporalDateTime.[[ISOMonth]], temporalDateTime.[[ISODay]], calendar).
  DirectHandle<JSTemporalPlainDate> date_part;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, date_part,
      CreateTemporalDate(
          isolate,
          {temporal_date_time->iso_year(), temporal_date_time->iso_month(),
           temporal_date_time->iso_day()},
          calendar));
  // 6. Let dateDuration be ? CreateTemporalDuration(years, months, weeks, days,
  // 0, 0, 0, 0, 0, 0).
  DirectHandle<JSTemporalDuration> date_duration;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, date_duration,
      CreateTemporalDuration(isolate,
                             {duration.years,
                              duration.months,
                              duration.weeks,
                              {time_duration.days, 0, 0, 0, 0, 0, 0}}));
  // 7. Let addedDate be ? CalendarDateAdd(calendar, datePart, dateDuration,
  // options).
  DirectHandle<JSTemporalPlainDate> added_date;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, added_date,
      CalendarDateAdd(isolate, calendar, date_part, date_duration, options));
  // 8. Let intermediateDateTime be ?
  // CreateTemporalDateTime(addedDate.[[ISOYear]], addedDate.[[ISOMonth]],
  // addedDate.[[ISODay]], temporalDateTime.[[ISOHour]],
  // temporalDateTime.[[ISOMinute]], temporalDateTime.[[ISOSecond]],
  // temporalDateTime.[[ISOMillisecond]], temporalDateTime.[[ISOMicrosecond]],
  // temporalDateTime.[[ISONanosecond]], calendar).
  DirectHandle<JSTemporalPlainDateTime> intermediate_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, intermediate_date_time,
      temporal::CreateTemporalDateTime(
          isolate,
          {{added_date->iso_year(), added_date->iso_month(),
            added_date->iso_day()},
           {temporal_date_time->iso_hour(), temporal_date_time->iso_minute(),
            temporal_date_time->iso_second(),
            temporal_date_time->iso_millisecond(),
            temporal_date_time->iso_microsecond(),
            temporal_date_time->iso_nanosecond()}},
          calendar));
  // 9. Let intermediateInstant be ? BuiltinTimeZoneGetInstantFor(timeZone,
  // intermediateDateTime, "compatible").
  DirectHandle<JSTemporalInstant> intermediate_instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, intermediate_instant,
      BuiltinTimeZoneGetInstantFor(isolate, time_zone, intermediate_date_time,
                                   Disambiguation::kCompatible, method_name));
  // 10. Return ? AddInstant(intermediateInstant.[[Nanoseconds]], hours,
  // minutes, seconds, milliseconds, microseconds, nanoseconds).
  time_duration.days = 0;
  return AddInstant(isolate,
                    direct_handle(intermediate_instant->nanoseconds(), isolate),
                    time_duration);
}

Maybe<NanosecondsToDaysResult> NanosecondsToDays(
    Isolate* isolate, DirectHandle<BigInt> nanoseconds,
    DirectHandle<Object> relative_to_obj, const char* method_name) {
  TEMPORAL_ENTER_FUNC();
  // 1. Let dayLengthNs be nsPerDay.
  constexpr int64_t kDayLengthNs = 86400000000000LLU;
  DirectHandle<BigInt> day_length_ns = BigInt::FromInt64(isolate, kDayLengthNs);
  double sign;
  switch (BigInt::CompareToNumber(nanoseconds,
                                  direct_handle(Smi::zero(), isolate))) {
    // 2. If nanoseconds = 0, then
    case ComparisonResult::kEqual:
      // a. Return the Record { [[Days]]: 0, [[Nanoseconds]]: 0, [[DayLength]]:
      // dayLengthNs }.
      return Just(NanosecondsToDaysResult({0, 0, kDayLengthNs}));
    // 3. If nanoseconds < 0, let sign be -1; else, let sign be 1.
    case ComparisonResult::kLessThan:
      sign = -1;
      break;
    case ComparisonResult::kGreaterThan:
      sign = 1;
      break;
    default:
      UNREACHABLE();
  }

  // 4. If Type(relativeTo) is not Object or relativeTo does not have an
  // [[InitializedTemporalZonedDateTime]] internal slot, then
  if (!IsJSTemporalZonedDateTime(*relative_to_obj)) {
    // a. Return the Record { [[Days]]: RoundTowardsZero(nanoseconds /
    // dayLengthNs), [[Nanoseconds]]: (abs(nanoseconds) modulo dayLengthNs) ×
    // sign, [[DayLength]]: dayLengthNs }.
    if (sign == -1) {
      nanoseconds = BigInt::UnaryMinus(isolate, nanoseconds);
    }
    DirectHandle<BigInt> days_bigint;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, days_bigint,
        BigInt::Divide(isolate, nanoseconds, day_length_ns),
        Nothing<NanosecondsToDaysResult>());
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, nanoseconds,
        BigInt::Remainder(isolate, nanoseconds, day_length_ns),
        Nothing<NanosecondsToDaysResult>());
    if (sign == -1) {
      days_bigint = BigInt::UnaryMinus(isolate, days_bigint);
      nanoseconds = BigInt::UnaryMinus(isolate, nanoseconds);
    }
    return Just(NanosecondsToDaysResult(
        {Object::NumberValue(*BigInt::ToNumber(isolate, days_bigint)),
         Object::NumberValue(*BigInt::ToNumber(isolate, nanoseconds)),
         kDayLengthNs}));
  }
  auto relative_to = Cast<JSTemporalZonedDateTime>(relative_to_obj);
  // 5. Let startNs be ℝ(relativeTo.[[Nanoseconds]]).
  DirectHandle<BigInt> start_ns(relative_to->nanoseconds(), isolate);
  // 6. Let startInstant be ! CreateTemporalInstant(ℤ(startNs)).
  DirectHandle<JSTemporalInstant> start_instant =
      temporal::CreateTemporalInstant(
          isolate, direct_handle(relative_to->nanoseconds(), isolate))
          .ToHandleChecked();

  // 7. Let startDateTime be ?
  // BuiltinTimeZoneGetPlainDateTimeFor(relativeTo.[[TimeZone]],
  // startInstant, relativeTo.[[Calendar]]).
  DirectHandle<JSReceiver> time_zone(relative_to->time_zone(), isolate);
  DirectHandle<JSReceiver> calendar(relative_to->calendar(), isolate);
  DirectHandle<JSTemporalPlainDateTime> start_date_time;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, start_date_time,
      temporal::BuiltinTimeZoneGetPlainDateTimeFor(
          isolate, time_zone, start_instant, calendar, method_name),
      Nothing<NanosecondsToDaysResult>());

  // 8. Let endNs be startNs + nanoseconds.
  DirectHandle<BigInt> end_ns;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, end_ns,
                                   BigInt::Add(isolate, start_ns, nanoseconds),
                                   Nothing<NanosecondsToDaysResult>());

  // 9. If ! IsValidEpochNanoseconds(ℤ(endNs)) is false, throw a RangeError
  // exception.
  if (!IsValidEpochNanoseconds(isolate, end_ns)) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<NanosecondsToDaysResult>());
  }

  // 10. Let endInstant be ! CreateTemporalInstant(ℤ(endNs)).
  DirectHandle<JSTemporalInstant> end_instant =
      temporal::CreateTemporalInstant(isolate, end_ns).ToHandleChecked();
  // 11. Let endDateTime be ?
  // BuiltinTimeZoneGetPlainDateTimeFor(relativeTo.[[TimeZone]],
  // endInstant, relativeTo.[[Calendar]]).
  DirectHandle<JSTemporalPlainDateTime> end_date_time;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, end_date_time,
      temporal::BuiltinTimeZoneGetPlainDateTimeFor(
          isolate, time_zone, end_instant, calendar, method_name),
      Nothing<NanosecondsToDaysResult>());

  // 12. Let dateDifference be ?
  // DifferenceISODateTime(startDateTime.[[ISOYear]],
  // startDateTime.[[ISOMonth]], startDateTime.[[ISODay]],
  // startDateTime.[[ISOHour]], startDateTime.[[ISOMinute]],
  // startDateTime.[[ISOSecond]], startDateTime.[[ISOMillisecond]],
  // startDateTime.[[ISOMicrosecond]], startDateTime.[[ISONanosecond]],
  // endDateTime.[[ISOYear]], endDateTime.[[ISOMonth]], endDateTime.[[ISODay]],
  // endDateTime.[[ISOHour]], endDateTime.[[ISOMinute]],
  // endDateTime.[[ISOSecond]], endDateTime.[[ISOMillisecond]],
  // endDateTime.[[ISOMicrosecond]], endDateTime.[[ISONanosecond]],
  // relativeTo.[[Calendar]], "day", OrdinaryObjectCreate(null)).
  DurationRecord date_difference;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, date_difference,
      DifferenceISODateTime(
          isolate,
          {{start_date_time->iso_year(), start_date_time->iso_month(),
            start_date_time->iso_day()},
           {start_date_time->iso_hour(), start_date_time->iso_minute(),
            start_date_time->iso_second(), start_date_time->iso_millisecond(),
            start_date_time->iso_microsecond(),
            start_date_time->iso_nanosecond()}},
          {{end_date_time->iso_year(), end_date_time->iso_month(),
            end_date_time->iso_day()},
           {end_date_time->iso_hour(), end_date_time->iso_minute(),
            end_date_time->iso_second(), end_date_time->iso_millisecond(),
            end_date_time->iso_microsecond(), end_date_time->iso_nanosecond()}},
          calendar, Unit::kDay, isolate->factory()->NewJSObjectWithNullProto(),
          method_name),
      Nothing<NanosecondsToDaysResult>());

  // 13. Let days be dateDifference.[[Days]].
  double days = date_difference.time_duration.days;

  // 14. Let intermediateNs be ℝ(? AddZonedDateTime(ℤ(startNs),
  // relativeTo.[[TimeZone]], relativeTo.[[Calendar]], 0, 0, 0, days, 0, 0, 0,
  // 0, 0, 0)).
  DirectHandle<BigInt> intermediate_ns;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, intermediate_ns,
      AddZonedDateTime(isolate, start_ns, time_zone, calendar,
                       {0, 0, 0, {days, 0, 0, 0, 0, 0, 0}}, method_name),
      Nothing<NanosecondsToDaysResult>());

  // 15. If sign is 1, then
  if (sign == 1) {
    // a. Repeat, while days > 0 and intermediateNs > endNs,
    while (days > 0 && BigInt::CompareToBigInt(intermediate_ns, end_ns) ==
                           ComparisonResult::kGreaterThan) {
      // i. Set days to days − 1.
      days -= 1;
      // ii. Set intermediateNs to ℝ(? AddZonedDateTime(ℤ(startNs),
      // relativeTo.[[TimeZone]], relativeTo.[[Calendar]], 0, 0, 0, days, 0, 0,
      // 0, 0, 0, 0)).
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, intermediate_ns,
          AddZonedDateTime(isolate, start_ns, time_zone, calendar,
                           {0, 0, 0, {days, 0, 0, 0, 0, 0, 0}}, method_name),
          Nothing<NanosecondsToDaysResult>());
    }
  }

  // 16. Set nanoseconds to endNs − intermediateNs.
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, nanoseconds, BigInt::Subtract(isolate, end_ns, intermediate_ns),
      Nothing<NanosecondsToDaysResult>());

  // 17. Let done be false.
  bool done = false;

  // 18. Repeat, while done is false,
  while (!done) {
    // a. Let oneDayFartherNs be ℝ(? AddZonedDateTime(ℤ(intermediateNs),
    // relativeTo.[[TimeZone]], relativeTo.[[Calendar]], 0, 0, 0, sign, 0, 0, 0,
    // 0, 0, 0)).
    DirectHandle<BigInt> one_day_farther_ns;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, one_day_farther_ns,
        AddZonedDateTime(isolate, intermediate_ns, time_zone, calendar,
                         {0, 0, 0, {sign, 0, 0, 0, 0, 0, 0}}, method_name),
        Nothing<NanosecondsToDaysResult>());

    // b. Set dayLengthNs to oneDayFartherNs − intermediateNs.
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, day_length_ns,
        BigInt::Subtract(isolate, one_day_farther_ns, intermediate_ns),
        Nothing<NanosecondsToDaysResult>());

    // c. If (nanoseconds − dayLengthNs) × sign ≥ 0, then
    if (sign * CompareResultToSign(
                   BigInt::CompareToBigInt(nanoseconds, day_length_ns)) >=
        0) {
      // i. Set nanoseconds to nanoseconds − dayLengthNs.
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, nanoseconds,
          BigInt::Subtract(isolate, nanoseconds, day_length_ns),
          Nothing<NanosecondsToDaysResult>());

      // ii. Set intermediateNs to oneDayFartherNs.
      intermediate_ns = one_day_farther_ns;

      // iii. Set days to days + sign.
      days += sign;
      // d. Else,
    } else {
      // i. Set done to true.
      done = true;
    }
  }

  // 20. Return the new Record { [[Days]]: days, [[Nanoseconds]]: nanoseconds,
  // [[DayLength]]: abs(dayLengthNs) }.
  NanosecondsToDaysResult result(
      {days, Object::NumberValue(*BigInt::ToNumber(isolate, nanoseconds)),
       std::abs(day_length_ns->AsInt64())});
  return Just(result);
}

// #sec-temporal-differenceisodatetime
Maybe<DurationRecord> DifferenceISODateTime(Isolate* isolate,
                                            const DateTimeRecord& date_time1,
                                            const DateTimeRecord& date_time2,
                                            DirectHandle<JSReceiver> calendar,
                                            Unit largest_unit,
                                            DirectHandle<JSReceiver> options,
                                            const char* method_name) {
  TEMPORAL_ENTER_FUNC();
  // 1. Assert: ISODateTimeWithinLimits(y1, mon1, d1, h1, min1, s1, ms1, mus1,
  // ns1) is true.
  DCHECK(ISODateTimeWithinLimits(isolate, date_time1));
  // 2. Assert: ISODateTimeWithinLimits(y2, mon2, d2, h2, min2, s2, ms2, mus2,
  // ns2) is true.
  DCHECK(ISODateTimeWithinLimits(isolate, date_time2));
  // 3. Let timeDifference be ! DifferenceTime(h1, min1, s1, ms1, mus1, ns1, h2,
  // min2, s2, ms2, mus2, ns2).
  TimeDurationRecord time_difference =
      DifferenceTime(isolate, date_time1.time, date_time2.time).ToChecked();

  // 4. Let timeSign be ! DurationSign(0, 0, 0, 0, timeDifference.[[Hours]],
  // timeDifference.[[Minutes]], timeDifference.[[Seconds]],
  // timeDifference.[[Milliseconds]], timeDifference.[[Microseconds]],
  // timeDifference.[[Nanoseconds]]).
  time_difference.days = 0;
  double time_sign = DurationRecord::Sign({0, 0, 0, time_difference});

  // 5. Let dateSign be ! CompareISODate(y2, mon2, d2, y1, mon1, d1).
  double date_sign = CompareISODate(date_time2.date, date_time1.date);

  // 6. Let adjustedDate be CreateISODateRecordWithCalendar(y1, mon1, d1).
  DateRecord adjusted_date = date_time1.date;
  CHECK(IsValidISODate(isolate, adjusted_date));

  // 7. If timeSign is -dateSign, then
  if (time_sign == -date_sign) {
    adjusted_date.day -= time_sign;
    // a. Set adjustedDate to BalanceISODate(adjustedDate.[[Year]],
    // adjustedDate.[[Month]], adjustedDate.[[Day]] - timeSign).
    adjusted_date = BalanceISODate(isolate, adjusted_date);
    // b. Set timeDifference to ! BalanceDuration(-timeSign,
    // timeDifference.[[Hours]], timeDifference.[[Minutes]],
    // timeDifference.[[Seconds]], timeDifference.[[Milliseconds]],
    // timeDifference.[[Microseconds]], timeDifference.[[Nanoseconds]],
    // largestUnit).
    time_difference.days = -time_sign;
    time_difference =
        BalanceDuration(isolate, largest_unit, time_difference, method_name)
            .ToChecked();
  }

  // 8. Let date1 be ! CreateTemporalDate(adjustedDate.[[Year]],
  // adjustedDate.[[Month]], adjustedDate.[[Day]], calendar).
  DirectHandle<JSTemporalPlainDate> date1 =
      CreateTemporalDate(isolate, adjusted_date, calendar).ToHandleChecked();

  // 9. Let date2 be ! CreateTemporalDate(y2, mon2, d2, calendar).
  DirectHandle<JSTemporalPlainDate> date2 =
      CreateTemporalDate(isolate, date_time2.date, calendar).ToHandleChecked();
  // 10. Let dateLargestUnit be ! LargerOfTwoTemporalUnits("day", largestUnit).
  Unit date_largest_unit = LargerOfTwoTemporalUnits(Unit::kDay, largest_unit);

  // 11. Let untilOptions be ? MergeLargestUnitOption(options, dateLargestUnit).
  DirectHandle<JSReceiver> until_options;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, until_options,
      MergeLargestUnitOption(isolate, options, date_largest_unit),
      Nothing<DurationRecord>());
  // 12. Let dateDifference be ? CalendarDateUntil(calendar, date1, date2,
  // untilOptions).
  DirectHandle<JSTemporalDuration> date_difference;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, date_difference,
      CalendarDateUntil(isolate, calendar, date1, date2, until_options),
      Nothing<DurationRecord>());
  // 13. Let balanceResult be ? BalanceDuration(dateDifference.[[Days]],
  // timeDifference.[[Hours]], timeDifference.[[Minutes]],
  // timeDifference.[[Seconds]], timeDifference.[[Milliseconds]],
  // timeDifference.[[Microseconds]], timeDifference.[[Nanoseconds]],
  // largestUnit).

  time_difference.days = Object::NumberValue(date_difference->days());
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, time_difference,
      BalanceDuration(isolate, largest_unit, time_difference, method_name),
      Nothing<DurationRecord>());

  // 14. Return ! CreateDurationRecord(dateDifference.[[Years]],
  // dateDifference.[[Months]], dateDifference.[[Weeks]],
  // balanceResult.[[Days]], balanceResult.[[Hours]], balanceResult.[[Minutes]],
  // balanceResult.[[Seconds]], balanceResult.[[Milliseconds]],
  // balanceResult.[[Microseconds]], balanceResult.[[Nanoseconds]]).

  return Just(CreateDurationRecord(
                  isolate, {Object::NumberValue(date_difference->years()),
                            Object::NumberValue(date_difference->months()),
                            Object::NumberValue(date_difference->weeks()),
                            time_difference})
                  .ToChecked());
}

// #sec-temporal-addinstant
MaybeDirectHandle<BigInt> AddInstant(Isolate* isolate,
                                     DirectHandle<BigInt> epoch_nanoseconds,
                                     const TimeDurationRecord& addend) {
  TEMPORAL_ENTER_FUNC();
  Factory* factory = isolate->factory();

  // 1. Assert: hours, minutes, seconds, milliseconds, microseconds, and
  // nanoseconds are integer Number values.
  // 2. Let result be epochNanoseconds + ℤ(nanoseconds) +
  // ℤ(microseconds) × 1000ℤ + ℤ(milliseconds) × 10^6ℤ + ℤ(seconds) × 10^9ℤ +
  // ℤ(minutes) × 60ℤ × 10^9ℤ + ℤ(hours) × 3600ℤ × 10^9ℤ.

  // epochNanoseconds + ℤ(nanoseconds)
  DirectHandle<BigInt> result =
      BigInt::Add(
          isolate, epoch_nanoseconds,
          BigInt::FromNumber(isolate, factory->NewNumber(addend.nanoseconds))
              .ToHandleChecked())
          .ToHandleChecked();

  // + ℤ(microseconds) × 1000ℤ
  DirectHandle<BigInt> temp =
      BigInt::Multiply(
          isolate,
          BigInt::FromNumber(isolate, factory->NewNumber(addend.microseconds))
              .ToHandleChecked(),
          BigInt::FromInt64(isolate, 1000))
          .ToHandleChecked();
  result = BigInt::Add(isolate, result, temp).ToHandleChecked();

  // + ℤ(milliseconds) × 10^6ℤ
  temp = BigInt::Multiply(isolate,
                          BigInt::FromNumber(
                              isolate, factory->NewNumber(addend.milliseconds))
                              .ToHandleChecked(),
                          BigInt::FromInt64(isolate, 1000000))
             .ToHandleChecked();
  result = BigInt::Add(isolate, result, temp).ToHandleChecked();

  // + ℤ(seconds) × 10^9ℤ
  temp = BigInt::Multiply(
             isolate,
             BigInt::FromNumber(isolate, factory->NewNumber(addend.seconds))
                 .ToHandleChecked(),
             BigInt::FromInt64(isolate, 1000000000))
             .ToHandleChecked();
  result = BigInt::Add(isolate, result, temp).ToHandleChecked();

  // + ℤ(minutes) × 60ℤ × 10^9ℤ.
  temp = BigInt::Multiply(
             isolate,
             BigInt::FromNumber(isolate, factory->NewNumber(addend.minutes))
                 .ToHandleChecked(),
             BigInt::FromInt64(isolate, 60000000000))
             .ToHandleChecked();
  result = BigInt::Add(isolate, result, temp).ToHandleChecked();

  // + ℤ(hours) × 3600ℤ × 10^9ℤ.
  temp = BigInt::Multiply(
             isolate,
             BigInt::FromNumber(isolate, factory->NewNumber(addend.hours))
                 .ToHandleChecked(),
             BigInt::FromInt64(isolate, 3600000000000))
             .ToHandleChecked();
  result = BigInt::Add(isolate, result, temp).ToHandleChecked();

  // 3. If ! IsValidEpochNanoseconds(result) is false, throw a RangeError
  // exception.
  if (!IsValidEpochNanoseconds(isolate, result)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }
  // 4. Return result.
  return result;
}

// #sec-temporal-isvalidepochnanoseconds
bool IsValidEpochNanoseconds(Isolate* isolate,
                             DirectHandle<BigInt> epoch_nanoseconds) {
  TEMPORAL_ENTER_FUNC();
  // nsMinInstant = -nsMaxInstant = -8.64 × 10^21
  constexpr double kNsMinInstant = -8.64e21;
  // nsMaxInstant = 10^8 × nsPerDay = 8.64 × 1021
  constexpr double kNsMaxInstant = 8.64e21;

  // 1. Assert: Type(epochNanoseconds) is BigInt.
  // 2. If ℝ(epochNanoseconds) < nsMinInstant or ℝ(epochNanoseconds) >
  // nsMaxInstant, then
  if (BigInt::CompareToNumber(epoch_nanoseconds,
                              isolate->factory()->NewNumber(kNsMinInstant)) ==
          ComparisonResult::kLessThan ||
      BigInt::CompareToNumber(epoch_nanoseconds,
                              isolate->factory()->NewNumber(kNsMaxInstant)) ==
          ComparisonResult::kGreaterThan) {
    // a. Return false.
    return false;
  }
  return true;
}

DirectHandle<BigInt> GetEpochFromISOParts(Isolate* isolate,
                                          const DateTimeRecord& date_time) {
  TEMPORAL_ENTER_FUNC();
  // 1. Assert: year, month, day, hour, minute, second, millisecond,
  // microsecond, and nanosecond are integers.
  // 2. Assert: ! IsValidISODate(year, month, day) is true.
  DCHECK(IsValidISODate(isolate, date_time.date));
  // 3. Assert: ! IsValidTime(hour, minute, second, millisecond, microsecond,
  // nanosecond) is true.
  DCHECK(IsValidTime(isolate, date_time.time));
  // 4. Let date be ! MakeDay(𝔽(year), 𝔽(month − 1), 𝔽(day)).
  double date = MakeDay(date_time.date.year, date_time.date.month - 1,
                        date_time.date.day);
  // 5. Let time be ! MakeTime(𝔽(hour), 𝔽(minute), 𝔽(second), 𝔽(millisecond)).
  double time = MakeTime(date_time.time.hour, date_time.time.minute,
                         date_time.time.second, date_time.time.millisecond);
  // 6. Let ms be ! MakeDate(date, time).
  double ms = MakeDate(date, time);
  // 7. Assert: ms is finite.
  // 8. Return ℝ(ms) × 10^6 + microsecond × 10^3 + nanosecond.
  return BigInt::Add(
             isolate,
             BigInt::Add(
                 isolate,
                 BigInt::Multiply(
                     isolate,
                     BigInt::FromNumber(isolate,
                                        isolate->factory()->NewNumber(ms))
                         .ToHandleChecked(),
                     BigInt::FromInt64(isolate, 1000000))
                     .ToHandleChecked(),
                 BigInt::Multiply(
                     isolate,
                     BigInt::FromInt64(isolate, date_time.time.microsecond),
                     BigInt::FromInt64(isolate, 1000))
                     .ToHandleChecked())
                 .ToHandleChecked(),
             BigInt::FromInt64(isolate, date_time.time.nanosecond))
      .ToHandleChecked();
}

}  // namespace

namespace temporal {

// #sec-temporal-durationsign
int32_t DurationRecord::Sign(const DurationRecord& dur) {
  TEMPORAL_ENTER_FUNC();

  // 1. For each value v of « years, months, weeks, days, hours, minutes,
  // seconds, milliseconds, microseconds, nanoseconds », do a. If v < 0, return
  // −1. b. If v > 0, return 1.
  // 2. Return 0.
  if (dur.years < 0) return -1;
  if (dur.years > 0) return 1;
  if (dur.months < 0) return -1;
  if (dur.months > 0) return 1;
  if (dur.weeks < 0) return -1;
  if (dur.weeks > 0) return 1;
  const TimeDurationRecord& time = dur.time_duration;
  if (time.days < 0) return -1;
  if (time.days > 0) return 1;
  if (time.hours < 0) return -1;
  if (time.hours > 0) return 1;
  if (time.minutes < 0) return -1;
  if (time.minutes > 0) return 1;
  if (time.seconds < 0) return -1;
  if (time.seconds > 0) return 1;
  if (time.milliseconds < 0) return -1;
  if (time.milliseconds > 0) return 1;
  if (time.microseconds < 0) return -1;
  if (time.microseconds > 0) return 1;
  if (time.nanoseconds < 0) return -1;
  if (time.nanoseconds > 0) return 1;
  return 0;
}

// #sec-temporal-isvalidduration
bool IsValidDuration(Isolate* isolate, const DurationRecord& dur) {
  TEMPORAL_ENTER_FUNC();

  // 1. Let sign be ! DurationSign(years, months, weeks, days, hours, minutes,
  // seconds, milliseconds, microseconds, nanoseconds).
  int32_t sign = DurationRecord::Sign(dur);
  // 2. For each value v of « years, months, weeks, days, hours, minutes,
  // seconds, milliseconds, microseconds, nanoseconds », do a. If v is not
  // finite, return false. b. If v < 0 and sign > 0, return false. c. If v > 0
  // and sign < 0, return false.
  // 3. Return true.
  const TimeDurationRecord& time = dur.time_duration;

  if (!(std::isfinite(dur.years) && std::isfinite(dur.months) &&
        std::isfinite(dur.weeks) && std::isfinite(time.days) &&
        std::isfinite(time.hours) && std::isfinite(time.minutes) &&
        std::isfinite(time.seconds) && std::isfinite(time.milliseconds) &&
        std::isfinite(time.microseconds) && std::isfinite(time.nanoseconds))) {
    return false;
  }
  if ((sign > 0 && (dur.years < 0 || dur.months < 0 || dur.weeks < 0 ||
                    time.days < 0 || time.hours < 0 || time.minutes < 0 ||
                    time.seconds < 0 || time.milliseconds < 0 ||
                    time.microseconds < 0 || time.nanoseconds < 0)) ||
      (sign < 0 && (dur.years > 0 || dur.months > 0 || dur.weeks > 0 ||
                    time.days > 0 || time.hours > 0 || time.minutes > 0 ||
                    time.seconds > 0 || time.milliseconds > 0 ||
                    time.microseconds > 0 || time.nanoseconds > 0))) {
    return false;
  }
  static const double kPower32Of2 = static_cast<double>(int64_t(1) << 32);
  static const int64_t kPower53Of2 = int64_t(1) << 53;
  // 3. If abs(years) ≥ 2**32, return false.
  if (std::abs(dur.years) >= kPower32Of2) {
    return false;
  }
  // 4. If abs(months) ≥ 2**32, return false.
  if (std::abs(dur.months) >= kPower32Of2) {
    return false;
  }
  // 5. If abs(weeks) ≥ 2**32, return false.
  if (std::abs(dur.weeks) >= kPower32Of2) {
    return false;
  }
  // 6. Let normalizedSeconds be days × 86,400 + hours × 3600 + minutes × 60 +
  // seconds + ℝ(𝔽(milliseconds)) × 10**-3 + ℝ(𝔽(microseconds)) × 10**-6 +
  // ℝ(𝔽(nanoseconds)) × 10**-9.
  // 7. NOTE: The above step cannot be implemented directly using floating-point
  // arithmetic. Multiplying by 10**-3, 10**-6, and 10**-9 respectively may be
  // imprecise when milliseconds, microseconds, or nanoseconds is an unsafe
  // integer. This multiplication can be implemented in C++ with an
  // implementation of std::remquo() with sufficient bits in the quotient.
  // String manipulation will also give an exact result, since the
  // multiplication is by a power of 10.
  // 8. If abs(normalizedSeconds) ≥ 2**53, return false.

  int64_t allowed = kPower53Of2;
  double in_seconds = std::abs(time.days * 86400.0 + time.hours * 3600.0 +
                               time.minutes * 60.0 + time.seconds);

  if (in_seconds >= allowed) {
    return false;
  }
  allowed -= in_seconds;

  // Check the part > 1 seconds.
  in_seconds = std::floor(std::abs(time.milliseconds / 1e3)) +
               std::floor(std::abs(time.microseconds / 1e6)) +
               std::floor(std::abs(time.nanoseconds / 1e9));
  if (in_seconds >= allowed) {
    return false;
  }
  allowed -= in_seconds;

  // Sum of the three remainings will surely < 3
  if (allowed > 3) {
    return true;
  }

  allowed *= 1000000000;  // convert to ns
  int64_t remainders = std::abs(fmod(time.milliseconds, 1e3)) * 1000000 +
                       std::abs(fmod(time.microseconds, 1e6)) * 1000 +
                       std::abs(fmod(time.nanoseconds, 1e9));
  if (remainders >= allowed) {
    return false;
  }
  return true;
}

}  // namespace temporal

namespace {

// #sec-temporal-isisoleapyear
bool IsISOLeapYear(Isolate* isolate, int32_t year) {
  TEMPORAL_ENTER_FUNC();

  // 1. Assert: year is an integer.
  // 2. If year modulo 4 ≠ 0, return false.
  // 3. If year modulo 400 = 0, return true.
  // 4. If year modulo 100 = 0, return false.
  // 5. Return true.
  return isolate->date_cache()->IsLeap(year);
}

// #sec-temporal-isodaysinmonth
int32_t ISODaysInMonth(Isolate* isolate, int32_t year, int32_t month) {
  TEMPORAL_ENTER_FUNC();

  // 1. Assert: year is an integer.
  // 2. Assert: month is an integer, month ≥ 1, and month ≤ 12.
  DCHECK_GE(month, 1);
  DCHECK_LE(month, 12);
  // 3. If month is 1, 3, 5, 7, 8, 10, or 12, return 31.
  if (month % 2 == ((month < 8) ? 1 : 0)) return 31;
  // 4. If month is 4, 6, 9, or 11, return 30.
  DCHECK(month == 2 || month == 4 || month == 6 || month == 9 || month == 11);
  if (month != 2) return 30;
  // 5. If ! IsISOLeapYear(year) is true, return 29.
  return IsISOLeapYear(isolate, year) ? 29 : 28;
  // 6. Return 28.
}

// #sec-temporal-isodaysinyear
int32_t ISODaysInYear(Isolate* isolate, int32_t year) {
  TEMPORAL_ENTER_FUNC();

  // 1. Assert: year is an integer.
  // 2. If ! IsISOLeapYear(year) is true, then
  // a. Return 366.
  // 3. Return 365.
  return IsISOLeapYear(isolate, year) ? 366 : 365;
}

bool IsValidTime(Isolate* isolate, const TimeRecord& time) {
  TEMPORAL_ENTER_FUNC();

  // 2. If hour < 0 or hour > 23, then
  // a. Return false.
  if (time.hour < 0 || time.hour > 23) return false;
  // 3. If minute < 0 or minute > 59, then
  // a. Return false.
  if (time.minute < 0 || time.minute > 59) return false;
  // 4. If second < 0 or second > 59, then
  // a. Return false.
  if (time.second < 0 || time.second > 59) return false;
  // 5. If millisecond < 0 or millisecond > 999, then
  // a. Return false.
  if (time.millisecond < 0 || time.millisecond > 999) return false;
  // 6. If microsecond < 0 or microsecond > 999, then
  // a. Return false.
  if (time.microsecond < 0 || time.microsecond > 999) return false;
  // 7. If nanosecond < 0 or nanosecond > 999, then
  // a. Return false.
  if (time.nanosecond < 0 || time.nanosecond > 999) return false;
  // 8. Return true.
  return true;
}

// #sec-temporal-isvalidisodate
bool IsValidISODate(Isolate* isolate, const DateRecord& date) {
  TEMPORAL_ENTER_FUNC();

  // 1. Assert: year, month, and day are integers.
  // 2. If month < 1 or month > 12, then
  // a. Return false.
  if (date.month < 1 || date.month > 12) return false;
  // 3. Let daysInMonth be ! ISODaysInMonth(year, month).
  // 4. If day < 1 or day > daysInMonth, then
  // a. Return false.
  if (date.day < 1 ||
      date.day > ISODaysInMonth(isolate, date.year, date.month)) {
    return false;
  }
  // 5. Return true.
  return true;
}

// #sec-temporal-compareisodate
int32_t CompareISODate(const DateRecord& one, const DateRecord& two) {
  TEMPORAL_ENTER_FUNC();

  // 1. Assert: y1, m1, d1, y2, m2, and d2 are integers.
  // 2. If y1 > y2, return 1.
  if (one.year > two.year) return 1;
  // 3. If y1 < y2, return -1.
  if (one.year < two.year) return -1;
  // 4. If m1 > m2, return 1.
  if (one.month > two.month) return 1;
  // 5. If m1 < m2, return -1.
  if (one.month < two.month) return -1;
  // 6. If d1 > d2, return 1.
  if (one.day > two.day) return 1;
  // 7. If d1 < d2, return -1.
  if (one.day < two.day) return -1;
  // 8. Return 0.
  return 0;
}

int32_t CompareTemporalTime(const TimeRecord& time1, const TimeRecord& time2);

// #sec-temporal-compareisodatetime
int32_t CompareISODateTime(const DateTimeRecord& one,
                           const DateTimeRecord& two) {
  // 2. Let dateResult be ! CompareISODate(y1, mon1, d1, y2, mon2, d2).
  int32_t date_result = CompareISODate(one.date, two.date);
  // 3. If dateResult is not 0, then
  if (date_result != 0) {
    // a. Return dateResult.
    return date_result;
  }
  // 4. Return ! CompareTemporalTime(h1, min1, s1, ms1, mus1, ns1, h2, min2, s2,
  // ms2, mus2, ns2).
  return CompareTemporalTime(one.time, two.time);
}

inline int32_t floor_divid(int32_t a, int32_t b) {
  return (((a) / (b)) + ((((a) < 0) && (((a) % (b)) != 0)) ? -1 : 0));
}
// #sec-temporal-balanceisoyearmonth
void BalanceISOYearMonth(Isolate* isolate, int32_t* year, int32_t* month) {
  TEMPORAL_ENTER_FUNC();

  // 1. Assert: year and month are integers.
  // 2. Set year to year + floor((month - 1) / 12).
  *year += floor_divid((*month - 1), 12);
  // 3. Set month to (month − 1) modulo 12 + 1.
  *month = static_cast<int32_t>(modulo(*month - 1, 12)) + 1;

  // 4. Return the new Record { [[Year]]: year, [[Month]]: month }.
}
// #sec-temporal-balancetime
DateTimeRecord BalanceTime(const UnbalancedTimeRecord& input) {
  TEMPORAL_ENTER_FUNC();
  UnbalancedTimeRecord time(input);
  TimeRecord result;

  // 1. Assert: hour, minute, second, millisecond, microsecond, and nanosecond
  // are integers.
  // 2. Set microsecond to microsecond + floor(nanosecond / 1000).
  time.microsecond += std::floor(time.nanosecond / 1000.0);
  // 3. Set nanosecond to nanosecond modulo 1000.
  result.nanosecond = modulo(time.nanosecond, 1000);
  // 4. Set millisecond to millisecond + floor(microsecond / 1000).
  time.millisecond += std::floor(time.microsecond / 1000.0);
  // 5. Set microsecond to microsecond modulo 1000.
  result.microsecond = modulo(time.microsecond, 1000);
  // 6. Set second to second + floor(millisecond / 1000).
  time.second += std::floor(time.millisecond / 1000.0);
  // 7. Set millisecond to millisecond modulo 1000.
  result.millisecond = modulo(time.millisecond, 1000);
  // 8. Set minute to minute + floor(second / 60).
  time.minute += std::floor(time.second / 60.0);
  // 9. Set second to second modulo 60.
  result.second = modulo(time.second, 60);
  // 10. Set hour to hour + floor(minute / 60).
  time.hour += std::floor(time.minute / 60.0);
  // 11. Set minute to minute modulo 60.
  result.minute = modulo(time.minute, 60);
  // 12. Let days be floor(hour / 24).
  int32_t days = std::floor(time.hour / 24.0);
  // 13. Set hour to hour modulo 24.
  result.hour = modulo(time.hour, 24);
  // 14. Return the new Record { [[Days]]: days, [[Hour]]: hour, [[Minute]]:
  // minute, [[Second]]: second, [[Millisecond]]: millisecond, [[Microsecond]]:
  // microsecond, [[Nanosecond]]: nanosecond }.
  return {{0, 0, days}, result};
}

// #sec-temporal-differencetime
Maybe<TimeDurationRecord> DifferenceTime(Isolate* isolate,
                                         const TimeRecord& time1,
                                         const TimeRecord& time2) {
  TEMPORAL_ENTER_FUNC();

  // 1. Assert: h1, min1, s1, ms1, mus1, ns1, h2, min2, s2, ms2, mus2, and ns2
  // are integers.
  TimeDurationRecord dur;
  // 2. Let hours be h2 − h1.
  dur.hours = time2.hour - time1.hour;
  // 3. Let minutes be min2 − min1.
  dur.minutes = time2.minute - time1.minute;
  // 4. Let seconds be s2 − s1.
  dur.seconds = time2.second - time1.second;
  // 5. Let milliseconds be ms2 − ms1.
  dur.milliseconds = time2.millisecond - time1.millisecond;
  // 6. Let microseconds be mus2 − mus1.
  dur.microseconds = time2.microsecond - time1.microsecond;
  // 7. Let nanoseconds be ns2 − ns1.
  dur.nanoseconds = time2.nanosecond - time1.nanosecond;
  // 8. Let sign be ! DurationSign(0, 0, 0, 0, hours, minutes, seconds,
  // milliseconds, microseconds, nanoseconds).
  double sign = DurationRecord::Sign(
      {0,
       0,
       0,
       {0, dur.hours, dur.minutes, dur.seconds, dur.milliseconds,
        dur.microseconds, dur.nanoseconds}});

  // 9. Let bt be ! BalanceTime(hours × sign, minutes × sign, seconds × sign,
  // milliseconds × sign, microseconds × sign, nanoseconds × sign).
  DateTimeRecord bt =
      BalanceTime({dur.hours * sign, dur.minutes * sign, dur.seconds * sign,
                   dur.milliseconds * sign, dur.microseconds * sign,
                   dur.nanoseconds * sign});

  // 9. Return ! CreateTimeDurationRecord(bt.[[Days]] × sign, bt.[[Hour]] ×
  // sign, bt.[[Minute]] × sign, bt.[[Second]] × sign, bt.[[Millisecond]] ×
  // sign, bt.[[Microsecond]] × sign, bt.[[Nanosecond]] × sign).
  return TimeDurationRecord::Create(
      isolate, bt.date.day * sign, bt.time.hour * sign, bt.time.minute * sign,
      bt.time.second * sign, bt.time.millisecond * sign,
      bt.time.microsecond * sign, bt.time.nanosecond * sign);
}

// #sec-temporal-addtime
DateTimeRecord AddTime(Isolate* isolate, const TimeRecord& time,
                       const TimeDurationRecord& addend) {
  TEMPORAL_ENTER_FUNC();

  DCHECK_EQ(addend.days, 0);
  // 1. Assert: hour, minute, second, millisecond, microsecond, nanosecond,
  // hours, minutes, seconds, milliseconds, microseconds, and nanoseconds are
  // integers.
  // 2. Let hour be hour + hours.
  return BalanceTime({time.hour + addend.hours,
                      // 3. Let minute be minute + minutes.
                      time.minute + addend.minutes,
                      // 4. Let second be second + seconds.
                      time.second + addend.seconds,
                      // 5. Let millisecond be millisecond + milliseconds.
                      time.millisecond + addend.milliseconds,
                      // 6. Let microsecond be microsecond + microseconds.
                      time.microsecond + addend.microseconds,
                      // 7. Let nanosecond be nanosecond + nanoseconds.
                      time.nanosecond + addend.nanoseconds});
  // 8. Return ! BalanceTime(hour, minute, second, millisecond, microsecond,
  // nanosecond).
}

// #sec-temporal-totaldurationnanoseconds
DirectHandle<BigInt> TotalDurationNanoseconds(Isolate* isolate,
                                              const TimeDurationRecord& value,
                                              double offset_shift) {
  TEMPORAL_ENTER_FUNC();

  TimeDurationRecord duration(value);

  DirectHandle<BigInt> nanoseconds =
      BigInt::FromNumber(isolate,
                         isolate->factory()->NewNumber(value.nanoseconds))
          .ToHandleChecked();

  // 1. Assert: offsetShift is an integer.
  // 2. Set nanoseconds to ℝ(nanoseconds).
  // 3. If days ≠ 0, then
  if (duration.days != 0) {
    // a. Set nanoseconds to nanoseconds − offsetShift.
    nanoseconds = BigInt::Subtract(
                      isolate, nanoseconds,
                      BigInt::FromNumber(
                          isolate, isolate->factory()->NewNumber(offset_shift))
                          .ToHandleChecked())
                      .ToHandleChecked();
  }

  DirectHandle<BigInt> thousand = BigInt::FromInt64(isolate, 1000);
  DirectHandle<BigInt> sixty = BigInt::FromInt64(isolate, 60);
  DirectHandle<BigInt> twentyfour = BigInt::FromInt64(isolate, 24);
  // 4. Set hours to ℝ(hours) + ℝ(days) × 24.

  DirectHandle<BigInt> x =
      BigInt::FromNumber(isolate, isolate->factory()->NewNumber(value.days))
          .ToHandleChecked();
  x = BigInt::Multiply(isolate, twentyfour, x).ToHandleChecked();
  x = BigInt::Add(isolate, x,
                  BigInt::FromNumber(isolate,
                                     isolate->factory()->NewNumber(value.hours))
                      .ToHandleChecked())
          .ToHandleChecked();

  // 5. Set minutes to ℝ(minutes) + hours × 60.
  x = BigInt::Multiply(isolate, sixty, x).ToHandleChecked();
  x = BigInt::Add(isolate, x,
                  BigInt::FromNumber(
                      isolate, isolate->factory()->NewNumber(value.minutes))
                      .ToHandleChecked())
          .ToHandleChecked();
  // 6. Set seconds to ℝ(seconds) + minutes × 60.
  x = BigInt::Multiply(isolate, sixty, x).ToHandleChecked();
  x = BigInt::Add(isolate, x,
                  BigInt::FromNumber(
                      isolate, isolate->factory()->NewNumber(value.seconds))
                      .ToHandleChecked())
          .ToHandleChecked();
  // 7. Set milliseconds to ℝ(milliseconds) + seconds × 1000.
  x = BigInt::Multiply(isolate, thousand, x).ToHandleChecked();
  x = BigInt::Add(isolate, x,
                  BigInt::FromNumber(isolate, isolate->factory()->NewNumber(
                                                  value.milliseconds))
                      .ToHandleChecked())
          .ToHandleChecked();
  // 8. Set microseconds to ℝ(microseconds) + milliseconds × 1000.
  x = BigInt::Multiply(isolate, thousand, x).ToHandleChecked();
  x = BigInt::Add(isolate, x,
                  BigInt::FromNumber(isolate, isolate->factory()->NewNumber(
                                                  value.microseconds))
                      .ToHandleChecked())
          .ToHandleChecked();
  // 9. Return nanoseconds + microseconds × 1000.
  x = BigInt::Multiply(isolate, thousand, x).ToHandleChecked();
  x = BigInt::Add(isolate, x, nanoseconds).ToHandleChecked();
  return x;
}

Maybe<DateRecord> RegulateISODate(Isolate* isolate, ShowOverflow overflow,
                                  const DateRecord& date);
Maybe<int32_t> ResolveISOMonth(Isolate* isolate,
                               DirectHandle<JSReceiver> fields);

// #sec-temporal-isomonthdayfromfields
Maybe<DateRecord> ISOMonthDayFromFields(Isolate* isolate,
                                        DirectHandle<JSReceiver> fields,
                                        DirectHandle<JSReceiver> options,
                                        const char* method_name) {
  Factory* factory = isolate->factory();
  // 1. Assert: Type(fields) is Object.
  // 2. Set fields to ? PrepareTemporalFields(fields, « "day", "month",
  // "monthCode", "year" », «"day"»).
  DirectHandle<FixedArray> field_names =
      DayMonthMonthCodeYearInFixedArray(isolate);
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, fields,
      PrepareTemporalFields(isolate, fields, field_names, RequiredFields::kDay),
      Nothing<DateRecord>());
  // 3. Let overflow be ? ToTemporalOverflow(options).
  ShowOverflow overflow;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, overflow, ToTemporalOverflow(isolate, options, method_name),
      Nothing<DateRecord>());
  // 4. Let month be ! Get(fields, "month").
  DirectHandle<Object> month_obj =
      JSReceiver::GetProperty(isolate, fields, factory->month_string())
          .ToHandleChecked();
  // 5. Let monthCode be ! Get(fields, "monthCode").
  DirectHandle<Object> month_code_obj =
      JSReceiver::GetProperty(isolate, fields, factory->monthCode_string())
          .ToHandleChecked();
  // 6. Let year be ! Get(fields, "year").
  DirectHandle<Object> year_obj =
      JSReceiver::GetProperty(isolate, fields, factory->year_string())
          .ToHandleChecked();
  // 7. If month is not undefined, and monthCode and year are both undefined,
  // then
  if (!IsUndefined(*month_obj, isolate) &&
      IsUndefined(*month_code_obj, isolate) &&
      IsUndefined(*year_obj, isolate)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR(),
                                 Nothing<DateRecord>());
  }
  // 8. Set month to ? ResolveISOMonth(fields).
  DateRecord result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, result.month,
                                         ResolveISOMonth(isolate, fields),
                                         Nothing<DateRecord>());

  // 9. Let day be ! Get(fields, "day").
  DirectHandle<Object> day_obj =
      JSReceiver::GetProperty(isolate, fields, factory->day_string())
          .ToHandleChecked();
  // 10. Assert: Type(day) is Number.
  // Note: "day" in fields is always converted by
  // ToIntegerThrowOnInfinity inside the PrepareTemporalFields above.
  // Therefore the day_obj is always an integer.
  result.day = FastD2I(floor(Object::NumberValue(Cast<Number>(*day_obj))));
  // 11. Let referenceISOYear be 1972 (the first leap year after the Unix
  // epoch).
  int32_t reference_iso_year = 1972;
  // 12. If monthCode is undefined, then
  if (IsUndefined(*month_code_obj, isolate)) {
    result.year = FastD2I(floor(Object::NumberValue(Cast<Number>(*year_obj))));
    // a. Let result be ? RegulateISODate(year, month, day, overflow).
  } else {
    // 13. Else,
    // a. Let result be ? RegulateISODate(referenceISOYear, month, day,
    // overflow).
    result.year = reference_iso_year;
  }
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result, RegulateISODate(isolate, overflow, result),
      Nothing<DateRecord>());
  // 14. Return the new Record { [[Month]]: result.[[Month]], [[Day]]:
  // result.[[Day]], [[ReferenceISOYear]]: referenceISOYear }.
  result.year = reference_iso_year;
  return Just(result);
}

}  // namespace

// #sec-temporal.duration
MaybeDirectHandle<JSTemporalDuration> JSTemporalDuration::Constructor(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, DirectHandle<Object> years,
    DirectHandle<Object> months, DirectHandle<Object> weeks,
    DirectHandle<Object> days, DirectHandle<Object> hours,
    DirectHandle<Object> minutes, DirectHandle<Object> seconds,
    DirectHandle<Object> milliseconds, DirectHandle<Object> microseconds,
    DirectHandle<Object> nanoseconds) {
  const char* method_name = "Temporal.Duration";
  // 1. If NewTarget is undefined, then
  if (IsUndefined(*new_target)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                                 isolate->factory()->NewStringFromAsciiChecked(
                                     method_name)));
  }
  // 2. Let y be ? ToIntegerWithoutRounding(years).
  double y;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, y, ToIntegerWithoutRounding(isolate, years),
      DirectHandle<JSTemporalDuration>());

  // 3. Let mo be ? ToIntegerWithoutRounding(months).
  double mo;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, mo, ToIntegerWithoutRounding(isolate, months),
      DirectHandle<JSTemporalDuration>());

  // 4. Let w be ? ToIntegerWithoutRounding(weeks).
  double w;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, w, ToIntegerWithoutRounding(isolate, weeks),
      DirectHandle<JSTemporalDuration>());

  // 5. Let d be ? ToIntegerWithoutRounding(days).
  double d;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, d, ToIntegerWithoutRounding(isolate, days),
      DirectHandle<JSTemporalDuration>());

  // 6. Let h be ? ToIntegerWithoutRounding(hours).
  double h;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, h, ToIntegerWithoutRounding(isolate, hours),
      DirectHandle<JSTemporalDuration>());

  // 7. Let m be ? ToIntegerWithoutRounding(minutes).
  double m;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, m, ToIntegerWithoutRounding(isolate, minutes),
      DirectHandle<JSTemporalDuration>());

  // 8. Let s be ? ToIntegerWithoutRounding(seconds).
  double s;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, s, ToIntegerWithoutRounding(isolate, seconds),
      DirectHandle<JSTemporalDuration>());

  // 9. Let ms be ? ToIntegerWithoutRounding(milliseconds).
  double ms;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, ms, ToIntegerWithoutRounding(isolate, milliseconds),
      DirectHandle<JSTemporalDuration>());

  // 10. Let mis be ? ToIntegerWithoutRounding(microseconds).
  double mis;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, mis, ToIntegerWithoutRounding(isolate, microseconds),
      DirectHandle<JSTemporalDuration>());

  // 11. Let ns be ? ToIntegerWithoutRounding(nanoseconds).
  double ns;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, ns, ToIntegerWithoutRounding(isolate, nanoseconds),
      DirectHandle<JSTemporalDuration>());

  // 12. Return ? CreateTemporalDuration(y, mo, w, d, h, m, s, ms, mis, ns,
  // NewTarget).
  return CreateTemporalDuration(isolate, target, new_target,
                                {y, mo, w, {d, h, m, s, ms, mis, ns}});
}

namespace {

// #sec-temporal-torelativetemporalobject
MaybeDirectHandle<Object> ToRelativeTemporalObject(
    Isolate* isolate, DirectHandle<JSReceiver> options,
    const char* method_name);

// #sec-temporal-defaulttemporallargestunit
Unit DefaultTemporalLargestUnit(const DurationRecord& dur);

// #sec-temporal-roundtemporalinstant
DirectHandle<BigInt> RoundTemporalInstant(Isolate* isolate,
                                          DirectHandle<BigInt> ns,
                                          double increment, Unit unit,
                                          RoundingMode rounding_mode);

// #sec-temporal-differenceinstant
TimeDurationRecord DifferenceInstant(Isolate* isolate, DirectHandle<BigInt> ns1,
                                     DirectHandle<BigInt> ns2,
                                     double rounding_increment,
                                     Unit smallest_unit, Unit largest_unit,
                                     RoundingMode rounding_mode,
                                     const char* method_name);

// #sec-temporal-differencezoneddatetime
Maybe<DurationRecord> DifferenceZonedDateTime(
    Isolate* isolate, DirectHandle<BigInt> ns1, DirectHandle<BigInt> ns2,
    DirectHandle<JSReceiver> time_zone, DirectHandle<JSReceiver> calendar,
    Unit largest_unit, DirectHandle<JSReceiver> options,
    const char* method_name);

// #sec-temporal-addduration
Maybe<DurationRecord> AddDuration(Isolate* isolate, const DurationRecord& dur1,
                                  const DurationRecord& dur2,
                                  DirectHandle<Object> relative_to_obj,
                                  const char* method_name);

// #sec-temporal-adjustroundeddurationdays
Maybe<DurationRecord> AdjustRoundedDurationDays(
    Isolate* isolate, const DurationRecord& duration, double increment,
    Unit unit, RoundingMode rounding_mode, DirectHandle<Object> relative_to_obj,
    const char* method_name) {
  // 1. If Type(relativeTo) is not Object; or relativeTo does not have an
  // [[InitializedTemporalZonedDateTime]] internal slot; or unit is one of
  // "year", "month", "week", or "day"; or unit is "nanosecond" and increment is
  // 1, then
  if (!IsJSTemporalZonedDateTime(*relative_to_obj) ||
      (unit == Unit::kYear || unit == Unit::kMonth || unit == Unit::kWeek ||
       unit == Unit::kDay) ||
      (unit == Unit::kNanosecond && increment == 1)) {
    // a. Return ! CreateDurationRecord(years, months, weeks, days, hours,
    // minutes, seconds, milliseconds, microseconds, nanoseconds).
    return Just(CreateDurationRecord(isolate, duration).ToChecked());
  }
  DirectHandle<JSTemporalZonedDateTime> relative_to =
      Cast<JSTemporalZonedDateTime>(relative_to_obj);
  // 2. Let timeRemainderNs be ! TotalDurationNanoseconds(0, hours, minutes,
  // seconds, milliseconds, microseconds, nanoseconds, 0).
  DirectHandle<BigInt> time_remainder_ns = TotalDurationNanoseconds(
      isolate,
      {0, duration.time_duration.hours, duration.time_duration.minutes,
       duration.time_duration.seconds, duration.time_duration.milliseconds,
       duration.time_duration.microseconds, duration.time_duration.nanoseconds},
      0);

  ComparisonResult compare = BigInt::CompareToNumber(
      time_remainder_ns, direct_handle(Smi::zero(), isolate));
  double direction;
  // 3. If timeRemainderNs = 0, let direction be 0.
  if (compare == ComparisonResult::kEqual) {
    direction = 0;
    // 4. Else if timeRemainderNs < 0, let direction be -1.
  } else if (compare == ComparisonResult::kLessThan) {
    direction = -1;
    // 5. Else, let direction be 1.
  } else {
    direction = 1;
  }

  // 6. Let dayStart be ? AddZonedDateTime(relativeTo.[[Nanoseconds]],
  // relativeTo.[[TimeZone]], relativeTo.[[Calendar]], years, months, weeks,
  // days, 0, 0, 0, 0, 0, 0).
  DirectHandle<BigInt> day_start;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, day_start,
      AddZonedDateTime(isolate,
                       direct_handle(relative_to->nanoseconds(), isolate),
                       direct_handle(relative_to->time_zone(), isolate),
                       direct_handle(relative_to->calendar(), isolate),
                       {duration.years,
                        duration.months,
                        duration.weeks,
                        {duration.time_duration.days, 0, 0, 0, 0, 0, 0}},
                       method_name),
      Nothing<DurationRecord>());
  // 7. Let dayEnd be ? AddZonedDateTime(dayStart, relativeTo.[[TimeZone]],
  // relativeTo.[[Calendar]], 0, 0, 0, direction, 0, 0, 0, 0, 0, 0).
  DirectHandle<BigInt> day_end;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, day_end,
      AddZonedDateTime(isolate, day_start,
                       direct_handle(relative_to->time_zone(), isolate),
                       direct_handle(relative_to->calendar(), isolate),
                       {0, 0, 0, {direction, 0, 0, 0, 0, 0, 0}}, method_name),
      Nothing<DurationRecord>());
  // 8. Let dayLengthNs be ℝ(dayEnd - dayStart).
  DirectHandle<BigInt> day_length_ns =
      BigInt::Subtract(isolate, day_end, day_start).ToHandleChecked();
  // 9. If (timeRemainderNs - dayLengthNs) × direction < 0, then
  DirectHandle<BigInt> time_remainder_ns_minus_day_length_ns =
      BigInt::Subtract(isolate, time_remainder_ns, day_length_ns)
          .ToHandleChecked();

  if (time_remainder_ns_minus_day_length_ns->AsInt64() * direction < 0) {
    // a. Return ! CreateDurationRecord(years, months, weeks, days, hours,
    // minutes, seconds, milliseconds, microseconds, nanoseconds).
    return Just(CreateDurationRecord(isolate, duration).ToChecked());
  }
  // 10. Set timeRemainderNs to ! RoundTemporalInstant(ℤ(timeRemainderNs -
  // dayLengthNs), increment, unit, roundingMode).
  time_remainder_ns =
      RoundTemporalInstant(isolate, time_remainder_ns_minus_day_length_ns,
                           increment, unit, rounding_mode);
  // 11. Let adjustedDateDuration be ? AddDuration(years, months, weeks, days,
  // 0, 0, 0, 0, 0, 0, 0, 0, 0, direction, 0, 0, 0, 0, 0, 0, relativeTo).
  DurationRecord adjusted_date_duration;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, adjusted_date_duration,
      AddDuration(isolate,
                  {duration.years,
                   duration.months,
                   duration.weeks,
                   {duration.time_duration.days, 0, 0, 0, 0, 0, 0}},
                  {0, 0, 0, {direction, 0, 0, 0, 0, 0, 0}}, relative_to,
                  method_name),
      Nothing<DurationRecord>());
  // 12. Let adjustedTimeDuration be ? BalanceDuration(0, 0, 0, 0, 0, 0,
  // timeRemainderNs, "hour").
  TimeDurationRecord adjusted_time_duration;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, adjusted_time_duration,
      BalanceDuration(isolate, Unit::kHour, time_remainder_ns, method_name),
      Nothing<DurationRecord>());
  // 13. Return ! CreateDurationRecord(adjustedDateDuration.[[Years]],
  // adjustedDateDuration.[[Months]], adjustedDateDuration.[[Weeks]],
  // adjustedDateDuration.[[Days]], adjustedTimeDuration.[[Hours]],
  // adjustedTimeDuration.[[Minutes]], adjustedTimeDuration.[[Seconds]],
  // adjustedTimeDuration.[[Milliseconds]],
  // adjustedTimeDuration.[[Microseconds]],
  // adjustedTimeDuration.[[Nanoseconds]]).
  adjusted_time_duration.days = adjusted_date_duration.time_duration.days;
  return Just(
      CreateDurationRecord(
          isolate, {adjusted_date_duration.years, adjusted_date_duration.months,
                    adjusted_date_duration.weeks, adjusted_time_duration})
          .ToChecked());
}

// #sec-temporal-calculateoffsetshift
Maybe<int64_t> CalculateOffsetShift(Isolate* isolate,
                                    DirectHandle<Object> relative_to_obj,
                                    const DateDurationRecord& dur,
                                    const char* method_name) {
  TEMPORAL_ENTER_FUNC();

  // 1. If Type(relativeTo) is not Object or relativeTo does not have an
  // [[InitializedTemporalZonedDateTime]] internal slot, return 0.
  if (!IsJSTemporalZonedDateTime(*relative_to_obj)) {
    return Just(static_cast<int64_t>(0));
  }
  auto relative_to = Cast<JSTemporalZonedDateTime>(relative_to_obj);
  // 2. Let instant be ! CreateTemporalInstant(relativeTo.[[Nanoseconds]]).
  DirectHandle<JSTemporalInstant> instant =
      temporal::CreateTemporalInstant(
          isolate, direct_handle(relative_to->nanoseconds(), isolate))
          .ToHandleChecked();
  // 3. Let offsetBefore be ? GetOffsetNanosecondsFor(relativeTo.[[TimeZone]],
  // instant).
  int64_t offset_before;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, offset_before,
      GetOffsetNanosecondsFor(isolate,
                              direct_handle(relative_to->time_zone(), isolate),
                              instant, method_name),
      Nothing<int64_t>());
  // 4. Let after be ? AddZonedDateTime(relativeTo.[[Nanoseconds]],
  // relativeTo.[[TimeZone]], relativeTo.[[Calendar]], y, mon, w, d, 0, 0, 0, 0,
  // 0, 0).
  DirectHandle<BigInt> after;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, after,
      AddZonedDateTime(
          isolate, direct_handle(relative_to->nanoseconds(), isolate),
          direct_handle(relative_to->time_zone(), isolate),
          direct_handle(relative_to->calendar(), isolate),
          {dur.years, dur.months, dur.weeks, {dur.days, 0, 0, 0, 0, 0, 0}},
          method_name),
      Nothing<int64_t>());
  // 5. Let instantAfter be ! CreateTemporalInstant(after).
  DirectHandle<JSTemporalInstant> instant_after =
      temporal::CreateTemporalInstant(isolate, after).ToHandleChecked();
  // 6. Let offsetAfter be ? GetOffsetNanosecondsFor(relativeTo.[[TimeZone]],
  // instantAfter).
  int64_t offset_after;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, offset_after,
      GetOffsetNanosecondsFor(isolate,
                              direct_handle(relative_to->time_zone(), isolate),
                              instant_after, method_name),
      Nothing<int64_t>());
  // 7. Return offsetAfter − offsetBefore
  return Just(offset_after - offset_before);
}

// #sec-temporal-moverelativedate
struct MoveRelativeDateResult {
  DirectHandle<JSTemporalPlainDate> relative_to;
  double days;
};
Maybe<MoveRelativeDateResult> MoveRelativeDate(
    Isolate* isolate, DirectHandle<JSReceiver> calendar,
    DirectHandle<JSTemporalPlainDate> relative_to,
    DirectHandle<JSTemporalDuration> duration, const char* method_name);

// #sec-temporal-unbalancedurationrelative
Maybe<DateDurationRecord> UnbalanceDurationRelative(
    Isolate* isolate, const DateDurationRecord& dur, Unit largest_unit,
    DirectHandle<Object> relative_to_obj, const char* method_name) {
  TEMPORAL_ENTER_FUNC();

  Factory* factory = isolate->factory();
  // 1. If largestUnit is "year", or years, months, weeks, and days are all 0,
  // then
  if (largest_unit == Unit::kYear ||
      (dur.years == 0 && dur.months == 0 && dur.weeks == 0 && dur.days == 0)) {
    // a. Return ! CreateDateDurationRecord(years, months, weeks, days).
    return Just(DateDurationRecord::Create(isolate, dur.years, dur.months,
                                           dur.weeks, dur.days)
                    .ToChecked());
  }
  // 2. Let sign be ! DurationSign(years, months, weeks, days, 0, 0, 0, 0, 0,
  // 0).
  double sign = DurationRecord::Sign(
      {dur.years, dur.months, dur.weeks, {dur.days, 0, 0, 0, 0, 0, 0}});
  // 3. Assert: sign ≠ 0.
  DCHECK_NE(sign, 0);
  // 4. Let oneYear be ! CreateTemporalDuration(sign, 0, 0, 0, 0, 0, 0, 0, 0,
  // 0).
  DirectHandle<JSTemporalDuration> one_year =
      CreateTemporalDuration(isolate, {sign, 0, 0, {0, 0, 0, 0, 0, 0, 0}})
          .ToHandleChecked();
  // 5. Let oneMonth be ! CreateTemporalDuration(0, sign, 0, 0, 0, 0, 0, 0, 0,
  // 0).
  DirectHandle<JSTemporalDuration> one_month =
      CreateTemporalDuration(isolate, {0, sign, 0, {0, 0, 0, 0, 0, 0, 0}})
          .ToHandleChecked();
  // 6. Let oneWeek be ! CreateTemporalDuration(0, 0, sign, 0, 0, 0, 0, 0, 0,
  // 0).
  DirectHandle<JSTemporalDuration> one_week =
      CreateTemporalDuration(isolate, {0, 0, sign, {0, 0, 0, 0, 0, 0, 0}})
          .ToHandleChecked();
  // 7. If relativeTo is not undefined, then
  DirectHandle<JSTemporalPlainDate> relative_to;
  DirectHandle<JSReceiver> calendar;
  if (!IsUndefined(*relative_to_obj)) {
    // a. Set relativeTo to ? ToTemporalDate(relativeTo).
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, relative_to,
        ToTemporalDate(isolate, relative_to_obj, method_name),
        Nothing<DateDurationRecord>());
    // b. Let calendar be relativeTo.[[Calendar]].
    calendar = direct_handle(relative_to->calendar(), isolate);
    // 8. Else,
  } else {
    // a. Let calendar be undefined.
  }
  DateDurationRecord result = dur;
  // 9. If largestUnit is "month", then
  if (largest_unit == Unit::kMonth) {
    // a. If calendar is undefined, then
    if (calendar.is_null()) {
      // i. Throw a RangeError exception.
      THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                   NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                   Nothing<DateDurationRecord>());
    }
    // b. Let dateAdd be ? GetMethod(calendar, "dateAdd").
    DirectHandle<Object> date_add;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, date_add,
        Object::GetMethod(isolate, calendar, factory->dateAdd_string()),
        Nothing<DateDurationRecord>());
    // c. Let dateUntil be ? GetMethod(calendar, "dateUntil").
    DirectHandle<Object> date_until;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, date_until,
        Object::GetMethod(isolate, calendar, factory->dateUntil_string()),
        Nothing<DateDurationRecord>());
    // d. Repeat, while years ≠ 0,
    while (result.years != 0) {
      // i. Let newRelativeTo be ? CalendarDateAdd(calendar, relativeTo,
      // oneYear, undefined, dateAdd).
      DirectHandle<JSTemporalPlainDate> new_relative_to;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, new_relative_to,
          CalendarDateAdd(isolate, calendar, relative_to, one_year,
                          factory->undefined_value(), date_add),
          Nothing<DateDurationRecord>());
      // ii. Let untilOptions be ! OrdinaryObjectCreate(null).
      DirectHandle<JSObject> until_options =
          factory->NewJSObjectWithNullProto();
      // iii. Perform ! CreateDataPropertyOrThrow(untilOptions, "largestUnit",
      // "month").
      CHECK(JSReceiver::CreateDataProperty(
                isolate, until_options, factory->largestUnit_string(),
                factory->month_string(), Just(kThrowOnError))
                .FromJust());
      // iv. Let untilResult be ? CalendarDateUntil(calendar, relativeTo,
      // newRelativeTo, untilOptions, dateUntil).
      DirectHandle<JSTemporalDuration> until_result;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, until_result,
          CalendarDateUntil(isolate, calendar, relative_to, new_relative_to,
                            until_options, date_until),
          Nothing<DateDurationRecord>());
      // v. Let oneYearMonths be untilResult.[[Months]].
      double one_year_months = Object::NumberValue(until_result->months());
      // vi. Set relativeTo to newRelativeTo.
      relative_to = new_relative_to;
      // vii. Set years to years − sign.
      result.years -= sign;
      // viii. Set months to months + oneYearMonths.
      result.months += one_year_months;
    }
    // 10. Else if largestUnit is "week", then
  } else if (largest_unit == Unit::kWeek) {
    // a. If calendar is undefined, then
    if (calendar.is_null()) {
      // i. Throw a RangeError exception.
      THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                   NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                   Nothing<DateDurationRecord>());
    }
    // b. Repeat, while years ≠ 0,
    while (result.years != 0) {
      // i. Let moveResult be ? MoveRelativeDate(calendar, relativeTo, oneYear).
      MoveRelativeDateResult move_result;
      MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, move_result,
          MoveRelativeDate(isolate, calendar, relative_to, one_year,
                           method_name),
          Nothing<DateDurationRecord>());
      // ii. Set relativeTo to moveResult.[[RelativeTo]].
      relative_to = move_result.relative_to;
      // iii. Set days to days + moveResult.[[Days]].
      result.days += move_result.days;
      // iv. Set years to years - sign.
      result.years -= sign;
    }
    // c. Repeat, while months ≠ 0,
    while (result.months != 0) {
      // i. Let moveResult be ? MoveRelativeDate(calendar, relativeTo,
      // oneMonth).
      MoveRelativeDateResult move_result;
      MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, move_result,
          MoveRelativeDate(isolate, calendar, relative_to, one_month,
                           method_name),
          Nothing<DateDurationRecord>());
      // ii. Set relativeTo to moveResult.[[RelativeTo]].
      relative_to = move_result.relative_to;
      // iii. Set days to days + moveResult.[[Days]].
      result.days += move_result.days;
      // iv. Set months to months - sign.
      result.months -= sign;
    }
    // 11. Else,
  } else {
    // a. If any of years, months, and weeks are not zero, then
    if ((result.years != 0) || (result.months != 0) || (result.weeks != 0)) {
      // i. If calendar is undefined, then
      if (calendar.is_null()) {
        // i. Throw a RangeError exception.
        THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                     NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                     Nothing<DateDurationRecord>());
      }
      // b. Repeat, while years ≠ 0,
      while (result.years != 0) {
        // i. Let moveResult be ? MoveRelativeDate(calendar, relativeTo,
        // oneYear).
        MoveRelativeDateResult move_result;
        MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
            isolate, move_result,
            MoveRelativeDate(isolate, calendar, relative_to, one_year,
                             method_name),
            Nothing<DateDurationRecord>());
        // ii. Set relativeTo to moveResult.[[RelativeTo]].
        relative_to = move_result.relative_to;
        // iii. Set days to days + moveResult.[[Days]].
        result.days += move_result.days;
        // iv. Set years to years - sign.
        result.years -= sign;
      }
      // c. Repeat, while months ≠ 0,
      while (result.months != 0) {
        // i. Let moveResult be ? MoveRelativeDate(calendar, relativeTo,
        // oneMonth).
        MoveRelativeDateResult move_result;
        MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
            isolate, move_result,
            MoveRelativeDate(isolate, calendar, relative_to, one_month,
                             method_name),
            Nothing<DateDurationRecord>());
        // ii. Set relativeTo to moveResult.[[RelativeTo]].
        relative_to = move_result.relative_to;
        // iii. Set days to days + moveResult.[[Days]].
        result.days += move_result.days;
        // iv. Set months to years - sign.
        result.months -= sign;
      }
      // d. Repeat, while weeks ≠ 0,
      while (result.weeks != 0) {
        // i. Let moveResult be ? MoveRelativeDate(calendar, relativeTo,
        // oneWeek).
        MoveRelativeDateResult move_result;
        MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
            isolate, move_result,
            MoveRelativeDate(isolate, calendar, relative_to, one_week,
                             method_name),
            Nothing<DateDurationRecord>());
        // ii. Set relativeTo to moveResult.[[RelativeTo]].
        relative_to = move_result.relative_to;
        // iii. Set days to days + moveResult.[[Days]].
        result.days += move_result.days;
        // iv. Set weeks to years - sign.
        result.weeks -= sign;
      }
    }
  }
  // 12. Return ? CreateDateDurationRecord(years, months, weeks, days).
  return DateDurationRecord::Create(isolate, result.years, result.months,
                                    result.weeks, result.days);
}

// #sec-temporal-balancedurationrelative
Maybe<DateDurationRecord> BalanceDurationRelative(
    Isolate* isolate, const DateDurationRecord& dur, Unit largest_unit,
    DirectHandle<Object> relative_to_obj, const char* method_name) {
  TEMPORAL_ENTER_FUNC();

  Factory* factory = isolate->factory();
  // 1. If largestUnit is not one of "year", "month", or "week", or years,
  // months, weeks, and days are all 0, then

  if ((largest_unit != Unit::kYear && largest_unit != Unit::kMonth &&
       largest_unit != Unit::kWeek) ||
      (dur.years == 0 && dur.months == 0 && dur.weeks == 0 && dur.days == 0)) {
    // a. Return ! CreateDateDurationRecord(years, months, weeks, days).
    return Just(DateDurationRecord::Create(isolate, dur.years, dur.months,
                                           dur.weeks, dur.days)
                    .ToChecked());
  }
  // 2. If relativeTo is undefined, then
  if (IsUndefined(*relative_to_obj)) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<DateDurationRecord>());
  }

  // 3. Let sign be ! DurationSign(years, months, weeks, days, 0, 0, 0, 0, 0,
  // 0).
  double sign = DurationRecord::Sign(
      {dur.years, dur.months, dur.weeks, {dur.days, 0, 0, 0, 0, 0, 0}});
  // 4. Assert: sign ≠ 0.
  DCHECK_NE(sign, 0);
  // 5. Let oneYear be ! CreateTemporalDuration(sign, 0, 0, 0, 0, 0, 0, 0, 0,
  // 0).
  DirectHandle<JSTemporalDuration> one_year =
      CreateTemporalDuration(isolate, {sign, 0, 0, {0, 0, 0, 0, 0, 0, 0}})
          .ToHandleChecked();
  // 6. Let oneMonth be ! CreateTemporalDuration(0, sign, 0, 0, 0, 0, 0, 0, 0,
  // 0).
  DirectHandle<JSTemporalDuration> one_month =
      CreateTemporalDuration(isolate, {0, sign, 0, {0, 0, 0, 0, 0, 0, 0}})
          .ToHandleChecked();
  // 7. Let oneWeek be ! CreateTemporalDuration(0, 0, sign, 0, 0, 0, 0, 0, 0,
  // 0).
  DirectHandle<JSTemporalDuration> one_week =
      CreateTemporalDuration(isolate, {0, 0, sign, {0, 0, 0, 0, 0, 0, 0}})
          .ToHandleChecked();
  // 8. Set relativeTo to ? ToTemporalDate(relativeTo).
  DirectHandle<JSTemporalPlainDate> relative_to;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, relative_to,
      ToTemporalDate(isolate, relative_to_obj, method_name),
      Nothing<DateDurationRecord>());
  // 9. Let calendar be relativeTo.[[Calendar]].
  DirectHandle<JSReceiver> calendar(relative_to->calendar(), isolate);

  DateDurationRecord result = dur;
  // 10.  If largestUnit is "year", then
  if (largest_unit == Unit::kYear) {
    // a. Let moveResult be ? MoveRelativeDate(calendar, relativeTo, oneYear).
    MoveRelativeDateResult move_result;
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, move_result,
        MoveRelativeDate(isolate, calendar, relative_to, one_year, method_name),
        Nothing<DateDurationRecord>());
    // b. Let newRelativeTo be moveResult.[[RelativeTo]].
    DirectHandle<JSTemporalPlainDate> new_relative_to = move_result.relative_to;
    // c. Let oneYearDays be moveResult.[[Days]].
    double one_year_days = move_result.days;
    // d. Repeat, while abs(days) ≥ abs(oneYearDays),
    while (std::abs(result.days) >= std::abs(one_year_days)) {
      // i. Set days to days - oneYearDays.
      result.days -= one_year_days;
      // ii. Set years to years + sign.
      result.years += sign;
      // iii. Set relativeTo to newRelativeTo.
      relative_to = new_relative_to;
      // iv. Set moveResult to ? MoveRelativeDate(calendar, relativeTo,
      // oneYear).
      MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, move_result,
          MoveRelativeDate(isolate, calendar, relative_to, one_year,
                           method_name),
          Nothing<DateDurationRecord>());

      // iv. Set newRelativeTo to moveResult.[[RelativeTo]].
      new_relative_to = move_result.relative_to;
      // v. Set oneYearDays to moveResult.[[Days]].
      one_year_days = move_result.days;
    }
    // e. Set moveResult to ? MoveRelativeDate(calendar, relativeTo, oneMonth).
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, move_result,
        MoveRelativeDate(isolate, calendar, relative_to, one_month,
                         method_name),
        Nothing<DateDurationRecord>());
    // f. Set newRelativeTo to moveResult.[[RelativeTo]].
    new_relative_to = move_result.relative_to;
    // g. Let oneMonthDays be moveResult.[[Days]].
    double one_month_days = move_result.days;
    // h. Repeat, while abs(days) ≥ abs(oneMonthDays),
    while (std::abs(result.days) >= std::abs(one_month_days)) {
      // i. Set days to days - oneMonthDays.
      result.days -= one_month_days;
      // ii. Set months to months + sign.
      result.months += sign;
      // iii. Set relativeTo to newRelativeTo.
      relative_to = new_relative_to;
      // iv. Set moveResult to ? MoveRelativeDate(calendar, relativeTo,
      // oneMonth).
      MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, move_result,
          MoveRelativeDate(isolate, calendar, relative_to, one_month,
                           method_name),
          Nothing<DateDurationRecord>());
      // iv. Set newRrelativeTo to moveResult.[[RelativeTo]].
      new_relative_to = move_result.relative_to;
      // v. Set oneMonthDays to moveResult.[[Days]].
      one_month_days = move_result.days;
    }
    // i. Let dateAdd be ? GetMethod(calendar, "dateAdd").
    DirectHandle<Object> date_add;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, date_add,
        Object::GetMethod(isolate, calendar, factory->dateAdd_string()),
        Nothing<DateDurationRecord>());
    // j. Set newRelativeTo be ? CalendarDateAdd(calendar, relativeTo, oneYear,
    // undefined, dateAdd).
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, new_relative_to,
        CalendarDateAdd(isolate, calendar, relative_to, one_year,
                        factory->undefined_value(), date_add),
        Nothing<DateDurationRecord>());
    // k. Let dateUntil be ? GetMethod(calendar, "dateUntil").
    DirectHandle<Object> date_until;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, date_until,
        Object::GetMethod(isolate, calendar, factory->dateUntil_string()),
        Nothing<DateDurationRecord>());
    // l. Let untilOptions be OrdinaryObjectCreate(null).
    DirectHandle<JSObject> until_options = factory->NewJSObjectWithNullProto();
    // m. Perform ! CreateDataPropertyOrThrow(untilOptions, "largestUnit",
    // "month").
    CHECK(JSReceiver::CreateDataProperty(
              isolate, until_options, factory->largestUnit_string(),
              factory->month_string(), Just(kThrowOnError))
              .FromJust());
    // n. Let untilResult be ? CalendarDateUntil(calendar, relativeTo,
    // newRelativeTo, untilOptions, dateUntil).
    DirectHandle<JSTemporalDuration> until_result;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, until_result,
        CalendarDateUntil(isolate, calendar, relative_to, new_relative_to,
                          until_options, date_until),
        Nothing<DateDurationRecord>());
    // o. Let oneYearMonths be untilResult.[[Months]].
    double one_year_months = Object::NumberValue(until_result->months());
    // p. Repeat, while abs(months) ≥ abs(oneYearMonths),
    while (std::abs(result.months) >= std::abs(one_year_months)) {
      // i. Set months to months - oneYearMonths.
      result.months -= one_year_months;
      // ii. Set years to years + sign.
      result.years += sign;
      // iii. Set relativeTo to newRelativeTo.
      relative_to = new_relative_to;
      // iv. Set newRelativeTo to ? CalendarDateAdd(calendar, relativeTo,
      // oneYear, undefined, dateAdd).
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, new_relative_to,
          CalendarDateAdd(isolate, calendar, relative_to, one_year,
                          factory->undefined_value(), date_add),
          Nothing<DateDurationRecord>());
      // v. Set untilOptions to OrdinaryObjectCreate(null).
      until_options = factory->NewJSObjectWithNullProto();
      // vi. Perform ! CreateDataPropertyOrThrow(untilOptions, "largestUnit",
      // "month").
      CHECK(JSReceiver::CreateDataProperty(
                isolate, until_options, factory->largestUnit_string(),
                factory->month_string(), Just(kThrowOnError))
                .FromJust());
      // vii. Set untilResult to ? CalendarDateUntil(calendar, relativeTo,
      // newRelativeTo, untilOptions, dateUntil).
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, until_result,
          CalendarDateUntil(isolate, calendar, relative_to, new_relative_to,
                            until_options, date_until),
          Nothing<DateDurationRecord>());
      // viii. Set oneYearMonths to untilResult.[[Months]].
      one_year_months = Object::NumberValue(until_result->months());
    }
    // 11. Else if largestUnit is "month", then
  } else if (largest_unit == Unit::kMonth) {
    // a. Let moveResult be ? MoveRelativeDate(calendar, relativeTo, oneMonth).
    MoveRelativeDateResult move_result;
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, move_result,
        MoveRelativeDate(isolate, calendar, relative_to, one_month,
                         method_name),
        Nothing<DateDurationRecord>());
    // b. Let newRelativeTo be moveResult.[[RelativeTo]].
    DirectHandle<JSTemporalPlainDate> new_relative_to = move_result.relative_to;
    // c. Let oneMonthDays be moveResult.[[Days]].
    double one_month_days = move_result.days;
    // d. Repeat, while abs(days) ≥ abs(oneMonthDays),
    while (std::abs(result.days) >= std::abs(one_month_days)) {
      // i. Set days to days - oneMonthDays.
      result.days -= one_month_days;
      // ii. Set months to months + sign.
      result.months += sign;
      // iii. Set relativeTo to newRelativeTo.
      relative_to = new_relative_to;
      // iv. Set moveResult to ? MoveRelativeDate(calendar, relativeTo,
      // oneMonth).
      MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, move_result,
          MoveRelativeDate(isolate, calendar, relative_to, one_month,
                           method_name),
          Nothing<DateDurationRecord>());
      // v. Set newRelativeTo to moveResult.[[RelativeTo]].
      new_relative_to = move_result.relative_to;
      // vi. Set oneMonthDays to moveResult.[[Days]].
      one_month_days = move_result.days;
    }
    // 12. Else
  } else {
    // a. Assert: largestUnit is "week".
    DCHECK_EQ(largest_unit, Unit::kWeek);
    // b. Let moveResult be ? MoveRelativeDate(calendar, relativeTo, oneWeek).
    MoveRelativeDateResult move_result;
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, move_result,
        MoveRelativeDate(isolate, calendar, relative_to, one_week, method_name),
        Nothing<DateDurationRecord>());
    // c. Let newRelativeTo be moveResult.[[RelativeTo]].
    DirectHandle<JSTemporalPlainDate> new_relative_to = move_result.relative_to;
    // d. Let oneWeekDays be moveResult.[[Days]].
    double one_week_days = move_result.days;
    // e. Repeat, while abs(days) ≥ abs(oneWeekDays),
    while (std::abs(result.days) >= std::abs(one_week_days)) {
      // i. Set days to days - oneWeekDays.
      result.days -= one_week_days;
      // ii. Set weeks to weeks + sign.
      result.weeks += sign;
      // iii. Set relativeTo to newRelativeTo.
      relative_to = new_relative_to;
      // v. Set moveResult to ? MoveRelativeDate(calendar, relativeTo,
      // oneWeek).
      MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, move_result,
          MoveRelativeDate(isolate, calendar, relative_to, one_week,
                           method_name),
          Nothing<DateDurationRecord>());
      // v. Set newRelativeTo to moveResult.[[RelativeTo]].
      new_relative_to = move_result.relative_to;
      // vi. Set oneWeekDays to moveResult.[[Days]].
      one_week_days = move_result.days;
    }
  }
  // 12. Return ? CreateDateDurationRecord(years, months, weeks, days).
  return DateDurationRecord::Create(isolate, result.years, result.months,
                                    result.weeks, result.days);
}

}  // namespace

// #sec-temporal.duration.compare
MaybeDirectHandle<Smi> JSTemporalDuration::Compare(
    Isolate* isolate, DirectHandle<Object> one_obj,
    DirectHandle<Object> two_obj, DirectHandle<Object> options_obj) {
  const char* method_name = "Temporal.Duration.compare";
  // 1. Set one to ? ToTemporalDuration(one).
  DirectHandle<JSTemporalDuration> one;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, one,
      temporal::ToTemporalDuration(isolate, one_obj, method_name));
  // 2. Set two to ? ToTemporalDuration(two).
  DirectHandle<JSTemporalDuration> two;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, two,
      temporal::ToTemporalDuration(isolate, two_obj, method_name));
  // 3. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));
  // 4. Let relativeTo be ? ToRelativeTemporalObject(options).
  DirectHandle<Object> relative_to;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, relative_to,
      ToRelativeTemporalObject(isolate, options, method_name));
  // 5. LetCalculateOffsetShift shift1 be ? CalculateOffsetShift(relativeTo,
  // one.[[Years]], one.[[Months]], one.[[Weeks]], one.[[Days]]).
  int64_t shift1;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, shift1,
      CalculateOffsetShift(
          isolate, relative_to,
          {Object::NumberValue(one->years()),
           Object::NumberValue(one->months()),
           Object::NumberValue(one->weeks()), Object::NumberValue(one->days())},
          method_name),
      DirectHandle<Smi>());
  // 6. Let shift2 be ? CalculateOffsetShift(relativeTo, two.[[Years]],
  // two.[[Months]], two.[[Weeks]], two.[[Days]]).
  int64_t shift2;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, shift2,
      CalculateOffsetShift(
          isolate, relative_to,
          {Object::NumberValue(two->years()),
           Object::NumberValue(two->months()),
           Object::NumberValue(two->weeks()), Object::NumberValue(two->days())},
          method_name),
      DirectHandle<Smi>());
  // 7. If any of one.[[Years]], two.[[Years]], one.[[Months]], two.[[Months]],
  // one.[[Weeks]], or two.[[Weeks]] are not 0, then
  double days1, days2;
  if (Object::NumberValue(one->years()) != 0 ||
      Object::NumberValue(two->years()) != 0 ||
      Object::NumberValue(one->months()) != 0 ||
      Object::NumberValue(two->months()) != 0 ||
      Object::NumberValue(one->weeks()) != 0 ||
      Object::NumberValue(two->weeks()) != 0) {
    // a. Let unbalanceResult1 be ? UnbalanceDurationRelative(one.[[Years]],
    // one.[[Months]], one.[[Weeks]], one.[[Days]], "day", relativeTo).
    DateDurationRecord unbalance_result1;
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, unbalance_result1,
        UnbalanceDurationRelative(isolate,
                                  {Object::NumberValue(one->years()),
                                   Object::NumberValue(one->months()),
                                   Object::NumberValue(one->weeks()),
                                   Object::NumberValue(one->days())},
                                  Unit::kDay, relative_to, method_name),
        DirectHandle<Smi>());
    // b. Let unbalanceResult2 be ? UnbalanceDurationRelative(two.[[Years]],
    // two.[[Months]], two.[[Weeks]], two.[[Days]], "day", relativeTo).
    DateDurationRecord unbalance_result2;
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, unbalance_result2,
        UnbalanceDurationRelative(isolate,
                                  {Object::NumberValue(two->years()),
                                   Object::NumberValue(two->months()),
                                   Object::NumberValue(two->weeks()),
                                   Object::NumberValue(two->days())},
                                  Unit::kDay, relative_to, method_name),
        DirectHandle<Smi>());
    // c. Let days1 be unbalanceResult1.[[Days]].
    days1 = unbalance_result1.days;
    // d. Let days2 be unbalanceResult2.[[Days]].
    days2 = unbalance_result2.days;
    // 8. Else,
  } else {
    // a. Let days1 be one.[[Days]].
    days1 = Object::NumberValue(one->days());
    // b. Let days2 be two.[[Days]].
    days2 = Object::NumberValue(two->days());
  }
  // 9. Let ns1 be ! TotalDurationNanoseconds(days1, one.[[Hours]],
  // one.[[Minutes]], one.[[Seconds]], one.[[Milliseconds]],
  // one.[[Microseconds]], one.[[Nanoseconds]], shift1).
  DirectHandle<BigInt> ns1 = TotalDurationNanoseconds(
      isolate,
      {days1, Object::NumberValue(one->hours()),
       Object::NumberValue(one->minutes()), Object::NumberValue(one->seconds()),
       Object::NumberValue(one->milliseconds()),
       Object::NumberValue(one->microseconds()),
       Object::NumberValue(one->nanoseconds())},
      shift1);
  // 10. Let ns2 be ! TotalDurationNanoseconds(days2, two.[[Hours]],
  // two.[[Minutes]], two.[[Seconds]], two.[[Milliseconds]],
  // two.[[Microseconds]], two.[[Nanoseconds]], shift2).
  DirectHandle<BigInt> ns2 = TotalDurationNanoseconds(
      isolate,
      {days2, Object::NumberValue(two->hours()),
       Object::NumberValue(two->minutes()), Object::NumberValue(two->seconds()),
       Object::NumberValue(two->milliseconds()),
       Object::NumberValue(two->microseconds()),
       Object::NumberValue(two->nanoseconds())},
      shift2);
  switch (BigInt::CompareToBigInt(ns1, ns2)) {
    // 11. If ns1 > ns2, return 1𝔽.
    case ComparisonResult::kGreaterThan:
      return direct_handle(Smi::FromInt(1), isolate);
    // 12. If ns1 < ns2, return -1𝔽.
    case ComparisonResult::kLessThan:
      return direct_handle(Smi::FromInt(-1), isolate);
    // 13. Return +0𝔽.
    default:
      return direct_handle(Smi::FromInt(0), isolate);
  }
}

// #sec-temporal.duration.from
MaybeDirectHandle<JSTemporalDuration> JSTemporalDuration::From(
    Isolate* isolate, DirectHandle<Object> item) {
  //  1. If Type(item) is Object and item has an [[InitializedTemporalDuration]]
  //  internal slot, then
  if (IsJSTemporalDuration(*item)) {
    // a. Return ? CreateTemporalDuration(item.[[Years]], item.[[Months]],
    // item.[[Weeks]], item.[[Days]], item.[[Hours]], item.[[Minutes]],
    // item.[[Seconds]], item.[[Milliseconds]], item.[[Microseconds]],
    // item.[[Nanoseconds]]).
    auto duration = Cast<JSTemporalDuration>(item);
    return CreateTemporalDuration(
        isolate, {Object::NumberValue(duration->years()),
                  Object::NumberValue(duration->months()),
                  Object::NumberValue(duration->weeks()),
                  {Object::NumberValue(duration->days()),
                   Object::NumberValue(duration->hours()),
                   Object::NumberValue(duration->minutes()),
                   Object::NumberValue(duration->seconds()),
                   Object::NumberValue(duration->milliseconds()),
                   Object::NumberValue(duration->microseconds()),
                   Object::NumberValue(duration->nanoseconds())}});
  }
  // 2. Return ? ToTemporalDuration(item).
  return temporal::ToTemporalDuration(isolate, item, "Temporal.Duration.from");
}

namespace {
// #sec-temporal-maximumtemporaldurationroundingincrement
struct Maximum {
  bool defined;
  double value;
};
Maximum MaximumTemporalDurationRoundingIncrement(Unit unit);
// #sec-temporal-totemporalroundingincrement
Maybe<double> ToTemporalRoundingIncrement(
    Isolate* isolate, DirectHandle<JSReceiver> normalized_options,
    double dividend, bool dividend_is_defined, bool inclusive);

// #sec-temporal-moverelativezoneddatetime
MaybeDirectHandle<JSTemporalZonedDateTime> MoveRelativeZonedDateTime(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    const DateDurationRecord& duration, const char* method_name);

// #sec-temporal-roundduration
Maybe<DurationRecordWithRemainder> RoundDuration(
    Isolate* isolate, const DurationRecord& duration, double increment,
    Unit unit, RoundingMode rounding_mode, DirectHandle<Object> relative_to,
    const char* method_name);
}  // namespace

// #sec-temporal.duration.prototype.round
MaybeDirectHandle<JSTemporalDuration> JSTemporalDuration::Round(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration,
    DirectHandle<Object> round_to_obj) {
  const char* method_name = "Temporal.Duration.prototype.round";
  Factory* factory = isolate->factory();
  // 1. Let duration be the this value.
  // 2. Perform ? RequireInternalSlot(duration,
  // [[InitializedTemporalDuration]]).
  // 3. If roundTo is undefined, then
  if (IsUndefined(*round_to_obj)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }
  DirectHandle<JSReceiver> round_to;
  // 4. If Type(roundTo) is String, then
  if (IsString(*round_to_obj)) {
    // a. Let paramString be roundTo.
    DirectHandle<String> param_string = Cast<String>(round_to_obj);
    // b. Set roundTo to ! OrdinaryObjectCreate(null).
    round_to = factory->NewJSObjectWithNullProto();
    // c. Perform ! CreateDataPropertyOrThrow(roundTo, "_smallestUnit_",
    // paramString).
    CHECK(JSReceiver::CreateDataProperty(isolate, round_to,
                                         factory->smallestUnit_string(),
                                         param_string, Just(kThrowOnError))
              .FromJust());
  } else {
    // a. Set roundTo to ? GetOptionsObject(roundTo).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, round_to,
        GetOptionsObject(isolate, round_to_obj, method_name));
  }
  // 6. Let smallestUnitPresent be true.
  bool smallest_unit_present = true;
  // 7. Let largestUnitPresent be true.
  bool largest_unit_present = true;
  // 8. Let smallestUnit be ? GetTemporalUnit(roundTo, "smallestUnit", datetime,
  // undefined).
  Unit smallest_unit;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, smallest_unit,
      GetTemporalUnit(isolate, round_to, "smallestUnit", UnitGroup::kDateTime,
                      Unit::kNotPresent, false, method_name),
      DirectHandle<JSTemporalDuration>());
  // 9. If smallestUnit is undefined, then
  if (smallest_unit == Unit::kNotPresent) {
    // a. Set smallestUnitPresent to false.
    smallest_unit_present = false;
    // b. Set smallestUnit to "nanosecond".
    smallest_unit = Unit::kNanosecond;
  }
  // 10. Let defaultLargestUnit be !
  // DefaultTemporalLargestUnit(duration.[[Years]], duration.[[Months]],
  // duration.[[Weeks]], duration.[[Days]], duration.[[Hours]],
  // duration.[[Minutes]], duration.[[Seconds]], duration.[[Milliseconds]],
  // duration.[[Microseconds]]).
  Unit default_largest_unit = DefaultTemporalLargestUnit(
      {Object::NumberValue(duration->years()),
       Object::NumberValue(duration->months()),
       Object::NumberValue(duration->weeks()),
       {Object::NumberValue(duration->days()),
        Object::NumberValue(duration->hours()),
        Object::NumberValue(duration->minutes()),
        Object::NumberValue(duration->seconds()),
        Object::NumberValue(duration->milliseconds()),
        Object::NumberValue(duration->microseconds()),
        Object::NumberValue(duration->nanoseconds())}});

  // 11. Set defaultLargestUnit to !
  // LargerOfTwoTemporalUnits(defaultLargestUnit, smallestUnit).
  default_largest_unit =
      LargerOfTwoTemporalUnits(default_largest_unit, smallest_unit);
  // 12. Let largestUnit be ? GetTemporalUnit(roundTo, "largestUnit", datetime,
  // undefined, « "auto" »).
  Unit largest_unit;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, largest_unit,
      GetTemporalUnit(isolate, round_to, "largestUnit", UnitGroup::kDateTime,
                      Unit::kNotPresent, false, method_name, Unit::kAuto),
      DirectHandle<JSTemporalDuration>());
  // 13. If largestUnit is undefined, then
  if (largest_unit == Unit::kNotPresent) {
    // a. Set largestUnitPresent to false.
    largest_unit_present = false;
    // b. Set largestUnit to defaultLargestUnit.
    largest_unit = default_largest_unit;
    // 14. Else if largestUnit is "auto", then
  } else if (largest_unit == Unit::kAuto) {
    // a. Set largestUnit to defaultLargestUnit.
    largest_unit = default_largest_unit;
  }
  // 15. If smallestUnitPresent is false and largestUnitPresent is false, then
  if (!smallest_unit_present && !largest_unit_present) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }
  // 16. If LargerOfTwoTemporalUnits(largestUnit, smallestUnit) is not
  // largestUnit, throw a RangeError exception.
  if (LargerOfTwoTemporalUnits(largest_unit, smallest_unit) != largest_unit) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }
  // 17. Let roundingMode be ? ToTemporalRoundingMode(roundTo, "halfExpand").
  RoundingMode rounding_mode;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, rounding_mode,
      ToTemporalRoundingMode(isolate, round_to, RoundingMode::kHalfExpand,
                             method_name),
      DirectHandle<JSTemporalDuration>());
  // 18. Let maximum be !
  // MaximumTemporalDurationRoundingIncrement(smallestUnit).
  Maximum maximum = MaximumTemporalDurationRoundingIncrement(smallest_unit);

  // 19. Let roundingIncrement be ? ToTemporalRoundingIncrement(roundTo,
  // maximum, false).
  double rounding_increment;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, rounding_increment,
      ToTemporalRoundingIncrement(isolate, round_to, maximum.value,
                                  maximum.defined, false),
      DirectHandle<JSTemporalDuration>());
  // 20. Let relativeTo be ? ToRelativeTemporalObject(roundTo).
  DirectHandle<Object> relative_to;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, relative_to,
      ToRelativeTemporalObject(isolate, round_to, method_name));
  // 21. Let unbalanceResult be ? UnbalanceDurationRelative(duration.[[Years]],
  // duration.[[Months]], duration.[[Weeks]], duration.[[Days]], largestUnit,
  // relativeTo).
  DateDurationRecord unbalance_result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, unbalance_result,
      UnbalanceDurationRelative(isolate,
                                {Object::NumberValue(duration->years()),
                                 Object::NumberValue(duration->months()),
                                 Object::NumberValue(duration->weeks()),
                                 Object::NumberValue(duration->days())},
                                largest_unit, relative_to, method_name),
      DirectHandle<JSTemporalDuration>());
  // 22. Let roundResult be (? RoundDuration(unbalanceResult.[[Years]],
  // unbalanceResult.[[Months]], unbalanceResult.[[Weeks]],
  // unbalanceResult.[[Days]], duration.[[Hours]], duration.[[Minutes]],
  // duration.[[Seconds]], duration.[[Milliseconds]], duration.[[Microseconds]],
  // duration.[[Nanoseconds]], roundingIncrement, smallestUnit, roundingMode,
  // relativeTo)).[[DurationRecord]].
  DurationRecordWithRemainder round_result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, round_result,
      RoundDuration(
          isolate,
          {unbalance_result.years,
           unbalance_result.months,
           unbalance_result.weeks,
           {unbalance_result.days, Object::NumberValue(duration->hours()),
            Object::NumberValue(duration->minutes()),
            Object::NumberValue(duration->seconds()),
            Object::NumberValue(duration->milliseconds()),
            Object::NumberValue(duration->microseconds()),
            Object::NumberValue(duration->nanoseconds())}},
          rounding_increment, smallest_unit, rounding_mode, relative_to,
          method_name),
      DirectHandle<JSTemporalDuration>());

  // 23. Let adjustResult be ? AdjustRoundedDurationDays(roundResult.[[Years]],
  // roundResult.[[Months]], roundResult.[[Weeks]], roundResult.[[Days]],
  // roundResult.[[Hours]], roundResult.[[Minutes]], roundResult.[[Seconds]],
  // roundResult.[[Milliseconds]], roundResult.[[Microseconds]],
  // roundResult.[[Nanoseconds]], roundingIncrement, smallestUnit, roundingMode,
  // relativeTo).
  DurationRecord adjust_result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, adjust_result,
      AdjustRoundedDurationDays(isolate, round_result.record,
                                rounding_increment, smallest_unit,
                                rounding_mode, relative_to, method_name),
      DirectHandle<JSTemporalDuration>());
  // 24. Let balanceResult be ? BalanceDurationRelative(adjustResult.[[Years]],
  // adjustResult.[[Months]], adjustResult.[[Weeks]], adjustResult.[[Days]],
  // largestUnit, relativeTo).
  DateDurationRecord balance_result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, balance_result,
      BalanceDurationRelative(
          isolate,
          {adjust_result.years, adjust_result.months, adjust_result.weeks,
           adjust_result.time_duration.days},
          largest_unit, relative_to, method_name),
      DirectHandle<JSTemporalDuration>());
  // 25. If Type(relativeTo) is Object and relativeTo has an
  // [[InitializedTemporalZonedDateTime]] internal slot, then
  if (IsJSTemporalZonedDateTime(*relative_to)) {
    // a. Set relativeTo to ? MoveRelativeZonedDateTime(relativeTo,
    // balanceResult.[[Years]], balanceResult.[[Months]],
    // balanceResult.[[Weeks]], 0).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, relative_to,
        MoveRelativeZonedDateTime(isolate,
                                  Cast<JSTemporalZonedDateTime>(relative_to),
                                  {balance_result.years, balance_result.months,
                                   balance_result.weeks, 0},
                                  method_name));
  }
  // 26. Let result be ? BalanceDuration(balanceResult.[[Days]],
  // adjustResult.[[Hours]], adjustResult.[[Minutes]], adjustResult.[[Seconds]],
  // adjustResult.[[Milliseconds]], adjustResult.[[Microseconds]],
  // adjustResult.[[Nanoseconds]], largestUnit, relativeTo).
  TimeDurationRecord result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result,
      BalanceDuration(isolate, largest_unit, relative_to,
                      {balance_result.days, adjust_result.time_duration.hours,
                       adjust_result.time_duration.minutes,
                       adjust_result.time_duration.seconds,
                       adjust_result.time_duration.milliseconds,
                       adjust_result.time_duration.microseconds,
                       adjust_result.time_duration.nanoseconds},
                      method_name),
      DirectHandle<JSTemporalDuration>());
  // 27. Return ! CreateTemporalDuration(balanceResult.[[Years]],
  // balanceResult.[[Months]], balanceResult.[[Weeks]], result.[[Days]],
  // result.[[Hours]], result.[[Minutes]], result.[[Seconds]],
  // result.[[Milliseconds]], result.[[Microseconds]], result.[[Nanoseconds]]).
  return CreateTemporalDuration(isolate,
                                {balance_result.years, balance_result.months,
                                 balance_result.weeks, result})
      .ToHandleChecked();
}

// #sec-temporal.duration.prototype.total
MaybeDirectHandle<Object> JSTemporalDuration::Total(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration,
    DirectHandle<Object> total_of_obj) {
  const char* method_name = "Temporal.Duration.prototype.total";
  Factory* factory = isolate->factory();
  // 1. Let duration be the this value.
  // 2. Perform ? RequireInternalSlot(duration,
  // [[InitializedTemporalDuration]]).
  // 3. If totalOf is undefined, throw a TypeError exception.
  if (IsUndefined(*total_of_obj, isolate)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }

  DirectHandle<JSReceiver> total_of;
  // 4. If Type(totalOf) is String, then
  if (IsString(*total_of_obj)) {
    // a. Let paramString be totalOf.
    DirectHandle<String> param_string = Cast<String>(total_of_obj);
    // b. Set totalOf to ! OrdinaryObjectCreate(null).
    total_of = factory->NewJSObjectWithNullProto();
    // c. Perform ! CreateDataPropertyOrThrow(total_of, "unit", paramString).
    CHECK(JSReceiver::CreateDataProperty(isolate, total_of,
                                         factory->unit_string(), param_string,
                                         Just(kThrowOnError))
              .FromJust());
  } else {
    // 5. Set totalOf to ? GetOptionsObject(totalOf).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, total_of,
        GetOptionsObject(isolate, total_of_obj, method_name));
  }

  // 6. Let relativeTo be ? ToRelativeTemporalObject(totalOf).
  DirectHandle<Object> relative_to;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, relative_to,
      ToRelativeTemporalObject(isolate, total_of, method_name));
  // 7. Let unit be ? GetTemporalUnit(totalOf, "unit", datetime, required).
  Unit unit;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, unit,
      GetTemporalUnit(isolate, total_of, "unit", UnitGroup::kDateTime,
                      Unit::kNotPresent, true, method_name),
      DirectHandle<Object>());
  // 8. Let unbalanceResult be ? UnbalanceDurationRelative(duration.[[Years]],
  // duration.[[Months]], duration.[[Weeks]], duration.[[Days]], unit,
  // relativeTo).
  DateDurationRecord unbalance_result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, unbalance_result,
      UnbalanceDurationRelative(isolate,
                                {Object::NumberValue(duration->years()),
                                 Object::NumberValue(duration->months()),
                                 Object::NumberValue(duration->weeks()),
                                 Object::NumberValue(duration->days())},
                                unit, relative_to, method_name),
      DirectHandle<Object>());

  // 9. Let intermediate be undefined.
  DirectHandle<Object> intermediate = factory->undefined_value();

  // 8. If relativeTo has an [[InitializedTemporalZonedDateTime]] internal slot,
  // then
  if (IsJSTemporalZonedDateTime(*relative_to)) {
    // a. Set intermediate to ? MoveRelativeZonedDateTime(relativeTo,
    // unbalanceResult.[[Years]], unbalanceResult.[[Months]],
    // unbalanceResult.[[Weeks]], 0).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, intermediate,
        MoveRelativeZonedDateTime(
            isolate, Cast<JSTemporalZonedDateTime>(relative_to),
            {unbalance_result.years, unbalance_result.months,
             unbalance_result.weeks, 0},
            method_name));
  }

  // 11. Let balanceResult be ?
  // BalancePossiblyInfiniteDuration(unbalanceResult.[[Days]],
  // duration.[[Hours]], duration.[[Minutes]], duration.[[Seconds]],
  // duration.[[Milliseconds]], duration.[[Microseconds]],
  // duration.[[Nanoseconds]], unit, intermediate).
  BalancePossiblyInfiniteDurationResult balance_result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, balance_result,
      BalancePossiblyInfiniteDuration(
          isolate, unit, intermediate,
          {unbalance_result.days, Object::NumberValue(duration->hours()),
           Object::NumberValue(duration->minutes()),
           Object::NumberValue(duration->seconds()),
           Object::NumberValue(duration->milliseconds()),
           Object::NumberValue(duration->microseconds()),
           Object::NumberValue(duration->nanoseconds())},
          method_name),
      DirectHandle<Object>());
  // 12. If balanceResult is positive overflow, return +∞𝔽.
  if (balance_result.overflow == BalanceOverflow::kPositive) {
    return factory->infinity_value();
  }
  // 13. If balanceResult is negative overflow, return -∞𝔽.
  if (balance_result.overflow == BalanceOverflow::kNegative) {
    return factory->minus_infinity_value();
  }
  // 14. Assert: balanceResult is a Time Duration Record.
  DCHECK_EQ(balance_result.overflow, BalanceOverflow::kNone);
  // 15. Let roundRecord be ? RoundDuration(unbalanceResult.[[Years]],
  // unbalanceResult.[[Months]], unbalanceResult.[[Weeks]],
  // balanceResult.[[Days]], balanceResult.[[Hours]], balanceResult.[[Minutes]],
  // balanceResult.[[Seconds]], balanceResult.[[Milliseconds]],
  // balanceResult.[[Microseconds]], balanceResult.[[Nanoseconds]], 1, unit,
  // "trunc", relativeTo).
  DurationRecordWithRemainder round_record;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, round_record,
      RoundDuration(isolate,
                    {unbalance_result.years, unbalance_result.months,
                     unbalance_result.weeks, balance_result.value},
                    1, unit, RoundingMode::kTrunc, relative_to, method_name),
      DirectHandle<Object>());
  // 16. Let roundResult be roundRecord.[[DurationRecord]].
  DurationRecord& round_result = round_record.record;

  double whole;
  switch (unit) {
    // 17. If unit is "year", then
    case Unit::kYear:
      // a. Let whole be roundResult.[[Years]].
      whole = round_result.years;
      break;
    // 18. If unit is "month", then
    case Unit::kMonth:
      // a. Let whole be roundResult.[[Months]].
      whole = round_result.months;
      break;
    // 19. If unit is "week", then
    case Unit::kWeek:
      // a. Let whole be roundResult.[[Weeks]].
      whole = round_result.weeks;
      break;
    // 20. If unit is "day", then
    case Unit::kDay:
      // a. Let whole be roundResult.[[Days]].
      whole = round_result.time_duration.days;
      break;
    // 21. If unit is "hour", then
    case Unit::kHour:
      // a. Let whole be roundResult.[[Hours]].
      whole = round_result.time_duration.hours;
      break;
    // 22. If unit is "minute", then
    case Unit::kMinute:
      // a. Let whole be roundResult.[[Minutes]].
      whole = round_result.time_duration.minutes;
      break;
    // 23. If unit is "second", then
    case Unit::kSecond:
      // a. Let whole be roundResult.[[Seconds]].
      whole = round_result.time_duration.seconds;
      break;
    // 24. If unit is "millisecond", then
    case Unit::kMillisecond:
      // a. Let whole be roundResult.[[Milliseconds]].
      whole = round_result.time_duration.milliseconds;
      break;
    // 25. If unit is "microsecond", then
    case Unit::kMicrosecond:
      // a. Let whole be roundResult.[[Microseconds]].
      whole = round_result.time_duration.microseconds;
      break;
    // 26. If unit is "naoosecond", then
    case Unit::kNanosecond:
      // a. Let whole be roundResult.[[Nanoseconds]].
      whole = round_result.time_duration.nanoseconds;
      break;
    default:
      UNREACHABLE();
  }
  // 27. Return 𝔽(whole + roundRecord.[[Remainder]]).
  return factory->NewNumber(whole + round_record.remainder);
}

namespace temporal {
// #sec-temporal-topartialduration
Maybe<DurationRecord> ToPartialDuration(
    Isolate* isolate, DirectHandle<Object> temporal_duration_like_obj,
    const DurationRecord& input) {
  // 1. If Type(temporalDurationLike) is not Object, then
  if (!IsJSReceiver(*temporal_duration_like_obj)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR(),
                                 Nothing<DurationRecord>());
  }
  DirectHandle<JSReceiver> temporal_duration_like =
      Cast<JSReceiver>(temporal_duration_like_obj);

  // 2. Let result be a new partial Duration Record with each field set to
  // undefined.
  DurationRecord result = input;

  // 3. Let any be false.
  bool any = false;

  // Table 8: Duration Record Fields
  // #table-temporal-duration-record-fields
  // 4. For each row of Table 8, except the header row, in table order, do
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, any,
      IterateDurationRecordFieldsTable(
          isolate, temporal_duration_like,
          [](Isolate* isolate, DirectHandle<JSReceiver> temporal_duration_like,
             DirectHandle<String> prop, double* field) -> Maybe<bool> {
            bool not_undefined = false;
            // a. Let prop be the Property value of the current row.
            DirectHandle<Object> val;
            // b. Let val be ? Get(temporalDurationLike, prop).
            ASSIGN_RETURN_ON_EXCEPTION_VALUE(
                isolate, val,
                JSReceiver::GetProperty(isolate, temporal_duration_like, prop),
                Nothing<bool>());
            // c. If val is not undefined, then
            if (!IsUndefined(*val)) {
              // i. Set any to true.
              not_undefined = true;
              // ii. Let val be 𝔽(? ToIntegerWithoutRounding(val)).
              // iii. Set result's field whose name is the Field Name value of
              // the current row to val.
              MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
                  isolate, *field, ToIntegerWithoutRounding(isolate, val),
                  Nothing<bool>());
            }
            return Just(not_undefined);
          },
          &result),
      Nothing<DurationRecord>());

  // 5. If any is false, then
  if (!any) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR(),
                                 Nothing<DurationRecord>());
  }
  // 6. Return result.
  return Just(result);
}

}  // namespace temporal

// #sec-temporal.duration.prototype.with
MaybeDirectHandle<JSTemporalDuration> JSTemporalDuration::With(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration,
    DirectHandle<Object> temporal_duration_like) {
  DurationRecord partial;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, partial,
      temporal::ToPartialDuration(
          isolate, temporal_duration_like,
          {Object::NumberValue(duration->years()),
           Object::NumberValue(duration->months()),
           Object::NumberValue(duration->weeks()),
           {Object::NumberValue(duration->days()),
            Object::NumberValue(duration->hours()),
            Object::NumberValue(duration->minutes()),
            Object::NumberValue(duration->seconds()),
            Object::NumberValue(duration->milliseconds()),
            Object::NumberValue(duration->microseconds()),
            Object::NumberValue(duration->nanoseconds())}}),
      DirectHandle<JSTemporalDuration>());

  // 24. Return ? CreateTemporalDuration(years, months, weeks, days, hours,
  // minutes, seconds, milliseconds, microseconds, nanoseconds).
  return CreateTemporalDuration(isolate, partial);
}

// #sec-get-temporal.duration.prototype.sign
MaybeDirectHandle<Smi> JSTemporalDuration::Sign(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration) {
  // 1. Let duration be the this value.
  // 2. Perform ? RequireInternalSlot(duration,
  // [[InitializedTemporalDuration]]).
  // 3. Return ! DurationSign(duration.[[Years]], duration.[[Months]],
  // duration.[[Weeks]], duration.[[Days]], duration.[[Hours]],
  // duration.[[Minutes]], duration.[[Seconds]], duration.[[Milliseconds]],
  // duration.[[Microseconds]], duration.[[Nanoseconds]]).
  return direct_handle(Smi::FromInt(DurationRecord::Sign(
                           {Object::NumberValue(duration->years()),
                            Object::NumberValue(duration->months()),
                            Object::NumberValue(duration->weeks()),
                            {Object::NumberValue(duration->days()),
                             Object::NumberValue(duration->hours()),
                             Object::NumberValue(duration->minutes()),
                             Object::NumberValue(duration->seconds()),
                             Object::NumberValue(duration->milliseconds()),
                             Object::NumberValue(duration->microseconds()),
                             Object::NumberValue(duration->nanoseconds())}})),
                       isolate);
}

// #sec-get-temporal.duration.prototype.blank
MaybeDirectHandle<Oddball> JSTemporalDuration::Blank(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration) {
  // 1. Let duration be the this value.
  // 2. Perform ? RequireInternalSlot(duration,
  // [[InitializedTemporalDuration]]).
  // 3. Let sign be ! DurationSign(duration.[[Years]], duration.[[Months]],
  // duration.[[Weeks]], duration.[[Days]], duration.[[Hours]],
  // duration.[[Minutes]], duration.[[Seconds]], duration.[[Milliseconds]],
  // duration.[[Microseconds]], duration.[[Nanoseconds]]).
  // 4. If sign = 0, return true.
  // 5. Return false.
  int32_t sign =
      DurationRecord::Sign({Object::NumberValue(duration->years()),
                            Object::NumberValue(duration->months()),
                            Object::NumberValue(duration->weeks()),
                            {Object::NumberValue(duration->days()),
                             Object::NumberValue(duration->hours()),
                             Object::NumberValue(duration->minutes()),
                             Object::NumberValue(duration->seconds()),
                             Object::NumberValue(duration->milliseconds()),
                             Object::NumberValue(duration->microseconds()),
                             Object::NumberValue(duration->nanoseconds())}});
  return isolate->factory()->ToBoolean(sign == 0);
}

namespace {
// #sec-temporal-createnegateddurationrecord
// see https://github.com/tc39/proposal-temporal/pull/2281
Maybe<DurationRecord> CreateNegatedDurationRecord(
    Isolate* isolate, const DurationRecord& duration) {
  return CreateDurationRecord(
      isolate,
      {-duration.years,
       -duration.months,
       -duration.weeks,
       {-duration.time_duration.days, -duration.time_duration.hours,
        -duration.time_duration.minutes, -duration.time_duration.seconds,
        -duration.time_duration.milliseconds,
        -duration.time_duration.microseconds,
        -duration.time_duration.nanoseconds}});
}

// #sec-temporal-createnegatedtemporalduration
MaybeDirectHandle<JSTemporalDuration> CreateNegatedTemporalDuration(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration) {
  TEMPORAL_ENTER_FUNC();
  // 1. Assert: Type(duration) is Object.
  // 2. Assert: duration has an [[InitializedTemporalDuration]] internal slot.
  // 3. Return ! CreateTemporalDuration(−duration.[[Years]],
  // −duration.[[Months]], −duration.[[Weeks]], −duration.[[Days]],
  // −duration.[[Hours]], −duration.[[Minutes]], −duration.[[Seconds]],
  // −duration.[[Milliseconds]], −duration.[[Microseconds]],
  // −duration.[[Nanoseconds]]).
  return CreateTemporalDuration(
             isolate, {-Object::NumberValue(duration->years()),
                       -Object::NumberValue(duration->months()),
                       -Object::NumberValue(duration->weeks()),
                       {-Object::NumberValue(duration->days()),
                        -Object::NumberValue(duration->hours()),
                        -Object::NumberValue(duration->minutes()),
                        -Object::NumberValue(duration->seconds()),
                        -Object::NumberValue(duration->milliseconds()),
                        -Object::NumberValue(duration->microseconds()),
                        -Object::NumberValue(duration->nanoseconds())}})
      .ToHandleChecked();
}

}  // namespace

// #sec-temporal.duration.prototype.negated
MaybeDirectHandle<JSTemporalDuration> JSTemporalDuration::Negated(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration) {
  // Let duration be the this value.
  // 2. Perform ? RequireInternalSlot(duration,
  // [[InitializedTemporalDuration]]).

  // 3. Return ! CreateNegatedTemporalDuration(duration).
  return CreateNegatedTemporalDuration(isolate, duration).ToHandleChecked();
}

// #sec-temporal.duration.prototype.abs
MaybeDirectHandle<JSTemporalDuration> JSTemporalDuration::Abs(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration) {
  // 1. Let duration be the this value.
  // 2. Perform ? RequireInternalSlot(duration,
  // [[InitializedTemporalDuration]]).
  // 3. Return ? CreateTemporalDuration(abs(duration.[[Years]]),
  // abs(duration.[[Months]]), abs(duration.[[Weeks]]), abs(duration.[[Days]]),
  // abs(duration.[[Hours]]), abs(duration.[[Minutes]]),
  // abs(duration.[[Seconds]]), abs(duration.[[Milliseconds]]),
  // abs(duration.[[Microseconds]]), abs(duration.[[Nanoseconds]])).
  return CreateTemporalDuration(
      isolate, {std::abs(Object::NumberValue(duration->years())),
                std::abs(Object::NumberValue(duration->months())),
                std::abs(Object::NumberValue(duration->weeks())),
                {std::abs(Object::NumberValue(duration->days())),
                 std::abs(Object::NumberValue(duration->hours())),
                 std::abs(Object::NumberValue(duration->minutes())),
                 std::abs(Object::NumberValue(duration->seconds())),
                 std::abs(Object::NumberValue(duration->milliseconds())),
                 std::abs(Object::NumberValue(duration->microseconds())),
                 std::abs(Object::NumberValue(duration->nanoseconds()))}});
}

namespace {

// #sec-temporal-interpretisodatetimeoffset
MaybeDirectHandle<BigInt> InterpretISODateTimeOffset(
    Isolate* isolate, const DateTimeRecord& data,
    OffsetBehaviour offset_behaviour, int64_t offset_nanoseconds,
    DirectHandle<JSReceiver> time_zone, Disambiguation disambiguation,
    Offset offset_option, MatchBehaviour match_behaviour,
    const char* method_name);

// #sec-temporal-interprettemporaldatetimefields
Maybe<temporal::DateTimeRecord> InterpretTemporalDateTimeFields(
    Isolate* isolate, DirectHandle<JSReceiver> calendar,
    DirectHandle<JSReceiver> fields, DirectHandle<Object> options,
    const char* method_name);

// #sec-temporal-torelativetemporalobject
MaybeDirectHandle<Object> ToRelativeTemporalObject(
    Isolate* isolate, DirectHandle<JSReceiver> options,
    const char* method_name) {
  TEMPORAL_ENTER_FUNC();

  Factory* factory = isolate->factory();
  // 1. Assert: Type(options) is Object.
  // 2. Let value be ? Get(options, "relativeTo").
  DirectHandle<Object> value_obj;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, value_obj,
      JSReceiver::GetProperty(isolate, options, factory->relativeTo_string()));
  // 3. If value is undefined, then
  if (IsUndefined(*value_obj)) {
    // a. Return value.
    return value_obj;
  }
  // 4. Let offsetBehaviour be option.
  OffsetBehaviour offset_behaviour = OffsetBehaviour::kOption;

  // 5. Let matchBehaviour be match exactly.
  MatchBehaviour match_behaviour = MatchBehaviour::kMatchExactly;

  DirectHandle<Object> time_zone_obj = factory->undefined_value();
  DirectHandle<Object> offset_string_obj;
  temporal::DateTimeRecord result;
  DirectHandle<JSReceiver> calendar;
  // 6. If Type(value) is Object, then
  if (IsJSReceiver(*value_obj)) {
    DirectHandle<JSReceiver> value = Cast<JSReceiver>(value_obj);
    // a. If value has either an [[InitializedTemporalDate]] or
    // [[InitializedTemporalZonedDateTime]] internal slot, then
    if (IsJSTemporalPlainDate(*value) || IsJSTemporalZonedDateTime(*value)) {
      // i. Return value.
      return value;
    }
    // b. If value has an [[InitializedTemporalDateTime]] internal slot, then
    if (IsJSTemporalPlainDateTime(*value)) {
      auto date_time_value = Cast<JSTemporalPlainDateTime>(value);
      // i. Return ? CreateTemporalDateTime(value.[[ISOYear]],
      // value.[[ISOMonth]], value.[[ISODay]],
      // value.[[Calendar]]).
      return CreateTemporalDate(
          isolate,
          {date_time_value->iso_year(), date_time_value->iso_month(),
           date_time_value->iso_day()},
          direct_handle(date_time_value->calendar(), isolate));
    }
    // c. Let calendar be ? GetTemporalCalendarWithISODefault(value).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, calendar,
        GetTemporalCalendarWithISODefault(isolate, value, method_name));
    // d. Let fieldNames be ? CalendarFields(calendar, «  "day", "hour",
    // "microsecond", "millisecond", "minute", "month", "monthCode",
    // "nanosecond", "second", "year" »).
    DirectHandle<FixedArray> field_names = All10UnitsInFixedArray(isolate);
    ASSIGN_RETURN_ON_EXCEPTION(isolate, field_names,
                               CalendarFields(isolate, calendar, field_names));
    // e. Let fields be ? PrepareTemporalFields(value, fieldNames, «»).
    DirectHandle<JSReceiver> fields;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, fields,
        PrepareTemporalFields(isolate, value, field_names,
                              RequiredFields::kNone));
    // f. Let dateOptions be ! OrdinaryObjectCreate(null).
    DirectHandle<JSObject> date_options = factory->NewJSObjectWithNullProto();
    // g. Perform ! CreateDataPropertyOrThrow(dateOptions, "overflow",
    // "constrain").
    CHECK(JSReceiver::CreateDataProperty(
              isolate, date_options, factory->overflow_string(),
              factory->constrain_string(), Just(kThrowOnError))
              .FromJust());
    // h. Let result be ? InterpretTemporalDateTimeFields(calendar, fields,
    // dateOptions).
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, result,
        InterpretTemporalDateTimeFields(isolate, calendar, fields, date_options,
                                        method_name),
        DirectHandle<Object>());
    // i. Let offsetString be ? Get(value, "offset").
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, offset_string_obj,
        JSReceiver::GetProperty(isolate, value, factory->offset_string()));
    // j. Let timeZone be ? Get(value, "timeZone").
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, time_zone_obj,
        JSReceiver::GetProperty(isolate, value, factory->timeZone_string()));
    // k. If timeZone is not undefined, then
    if (!IsUndefined(*time_zone_obj)) {
      // i. Set timeZone to ? ToTemporalTimeZone(timeZone).
      DirectHandle<JSReceiver> time_zone;
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, time_zone,
          temporal::ToTemporalTimeZone(isolate, time_zone_obj, method_name));
      time_zone_obj = time_zone;
    }

    // l. If offsetString is undefined, then
    if (IsUndefined(*offset_string_obj)) {
      // i. Set offsetBehaviour to wall.
      offset_behaviour = OffsetBehaviour::kWall;
    }
    // 6. Else,
  } else {
    // a. Let string be ? ToString(value).
    DirectHandle<String> string;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, string,
                               Object::ToString(isolate, value_obj));
    DateTimeRecordWithCalendar parsed_result;
    // b. Let result be ? ParseTemporalRelativeToString(string).
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, parsed_result, ParseTemporalRelativeToString(isolate, string),
        DirectHandle<Object>());
    result = {parsed_result.date, parsed_result.time};
    // c. Let calendar be ?
    // ToTemporalCalendarWithISODefault(result.[[Calendar]]).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, calendar,
        ToTemporalCalendarWithISODefault(isolate, parsed_result.calendar,
                                         method_name));

    // d. Let offsetString be result.[[TimeZone]].[[OffsetString]].
    offset_string_obj = parsed_result.time_zone.offset_string;

    // e. Let timeZoneName be result.[[TimeZone]].[[Name]].
    DirectHandle<Object> time_zone_name_obj = parsed_result.time_zone.name;

    // f. If timeZoneName is undefined, then
    if (IsUndefined(*time_zone_name_obj)) {
      // i. Let timeZone be undefined.
      time_zone_obj = factory->undefined_value();
      // g. Else,
    } else {
      // i. If ParseText(StringToCodePoints(timeZoneName),
      // TimeZoneNumericUTCOffset) is a List of errors, then
      DCHECK(IsString(*time_zone_name_obj));
      DirectHandle<String> time_zone_name = Cast<String>(time_zone_name_obj);
      std::optional<ParsedISO8601Result> parsed =
          TemporalParser::ParseTimeZoneNumericUTCOffset(isolate,
                                                        time_zone_name);
      if (!parsed.has_value()) {
        // 1. If ! IsValidTimeZoneName(timeZoneName) is false, throw a
        // RangeError exception.
        if (!IsValidTimeZoneName(isolate, time_zone_name)) {
          THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                       NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                       DirectHandle<Object>());
        }
        // 2. Set timeZoneName to ! CanonicalizeTimeZoneName(timeZoneName).
        time_zone_name = CanonicalizeTimeZoneName(isolate, time_zone_name);
      }
      // ii. Let timeZone be ! CreateTemporalTimeZone(timeZoneName).
      DirectHandle<JSTemporalTimeZone> time_zone =
          temporal::CreateTemporalTimeZone(isolate, time_zone_name)
              .ToHandleChecked();
      time_zone_obj = time_zone;

      // iii. If result.[[TimeZone]].[[Z]] is true, then
      if (parsed_result.time_zone.z) {
        // 1. Set offsetBehaviour to exact.
        offset_behaviour = OffsetBehaviour::kExact;
        // iv. Else if offsetString is undefined, then
      } else if (IsUndefined(*offset_string_obj)) {
        // 1. Set offsetBehaviour to wall.
        offset_behaviour = OffsetBehaviour::kWall;
      }
      // v. Set matchBehaviour to match minutes.
      match_behaviour = MatchBehaviour::kMatchMinutes;
    }
  }
  // 8. If timeZone is undefined, then
  if (IsUndefined(*time_zone_obj)) {
    // a. Return ? CreateTemporalDate(result.[[Year]], result.[[Month]],
    // result.[[Day]], calendar).
    return CreateTemporalDate(isolate, result.date, calendar);
  }
  DCHECK(IsJSReceiver(*time_zone_obj));
  DirectHandle<JSReceiver> time_zone = Cast<JSReceiver>(time_zone_obj);
  // 9. If offsetBehaviour is option, then
  int64_t offset_ns = 0;
  if (offset_behaviour == OffsetBehaviour::kOption) {
    // a. Set offsetString to ? ToString(offsetString).
    DirectHandle<String> offset_string;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, offset_string,
                               Object::ToString(isolate, offset_string_obj));
    // b. Let offsetNs be ? ParseTimeZoneOffsetString(offset_string).
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, offset_ns, ParseTimeZoneOffsetString(isolate, offset_string),
        DirectHandle<Object>());
    // 10. Else,
  } else {
    // a. Let offsetNs be 0.
    offset_ns = 0;
  }
  // 11. Let epochNanoseconds be ? InterpretISODateTimeOffset(result.[[Year]],
  // result.[[Month]], result.[[Day]], result.[[Hour]], result.[[Minute]],
  // result.[[Second]], result.[[Millisecond]], result.[[Microsecond]],
  // result.[[Nanosecond]], offsetBehaviour, offsetNs, timeZone, "compatible",
  // "reject", matchBehaviour).
  DirectHandle<BigInt> epoch_nanoseconds;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, epoch_nanoseconds,
      InterpretISODateTimeOffset(isolate, result, offset_behaviour, offset_ns,
                                 time_zone, Disambiguation::kCompatible,
                                 Offset::kReject, match_behaviour,
                                 method_name));

  // 12. Return ? CreateTemporalZonedDateTime(epochNanoseconds, timeZone,
  // calendar).
  return CreateTemporalZonedDateTime(isolate, epoch_nanoseconds, time_zone,
                                     calendar);
}

// #sec-temporal-defaulttemporallargestunit
Unit DefaultTemporalLargestUnit(const DurationRecord& dur) {
  // 1. If years is not zero, return "year".
  if (dur.years != 0) return Unit::kYear;
  // 2. If months is not zero, return "month".
  if (dur.months != 0) return Unit::kMonth;
  // 3. If weeks is not zero, return "week".
  if (dur.weeks != 0) return Unit::kWeek;
  // 4. If days is not zero, return "day".
  if (dur.time_duration.days != 0) return Unit::kDay;
  // 5dur.. If hours is not zero, return "hour".
  if (dur.time_duration.hours != 0) return Unit::kHour;
  // 6. If minutes is not zero, return "minute".
  if (dur.time_duration.minutes != 0) return Unit::kMinute;
  // 7. If seconds is not zero, return "second".
  if (dur.time_duration.seconds != 0) return Unit::kSecond;
  // 8. If milliseconds is not zero, return "millisecond".
  if (dur.time_duration.milliseconds != 0) return Unit::kMillisecond;
  // 9. If microseconds is not zero, return "microsecond".
  if (dur.time_duration.microseconds != 0) return Unit::kMicrosecond;
  // 10. Return "nanosecond".
  return Unit::kNanosecond;
}

// #sec-temporal-differencezoneddatetime
Maybe<DurationRecord> DifferenceZonedDateTime(
    Isolate* isolate, DirectHandle<BigInt> ns1, DirectHandle<BigInt> ns2,
    DirectHandle<JSReceiver> time_zone, DirectHandle<JSReceiver> calendar,
    Unit largest_unit, DirectHandle<JSReceiver> options,
    const char* method_name) {
  TEMPORAL_ENTER_FUNC();

  // 1. If ns1 is ns2, then
  if (BigInt::CompareToBigInt(ns1, ns2) == ComparisonResult::kEqual) {
    // a. Return ! CreateDurationRecord(0, 0, 0, 0, 0, 0, 0, 0, 0, 0).
    return Just(CreateDurationRecord(isolate, {0, 0, 0, {0, 0, 0, 0, 0, 0, 0}})
                    .ToChecked());
  }
  // 2. Let startInstant be ! CreateTemporalInstant(ns1).
  DirectHandle<JSTemporalInstant> start_instant =
      temporal::CreateTemporalInstant(isolate, ns1).ToHandleChecked();
  // 3. Let startDateTime be ?
  // temporal::BuiltinTimeZoneGetPlainDateTimeFor(timeZone, startInstant,
  // calendar).
  DirectHandle<JSTemporalPlainDateTime> start_date_time;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, start_date_time,
      temporal::BuiltinTimeZoneGetPlainDateTimeFor(
          isolate, time_zone, start_instant, calendar, method_name),
      Nothing<DurationRecord>());
  // 4. Let endInstant be ! CreateTemporalInstant(ns2).
  DirectHandle<JSTemporalInstant> end_instant =
      temporal::CreateTemporalInstant(isolate, ns2).ToHandleChecked();
  // 5. Let endDateTime be ?
  // temporal::BuiltinTimeZoneGetPlainDateTimeFor(timeZone, endInstant,
  // calendar).
  DirectHandle<JSTemporalPlainDateTime> end_date_time;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, end_date_time,
      temporal::BuiltinTimeZoneGetPlainDateTimeFor(
          isolate, time_zone, end_instant, calendar, method_name),
      Nothing<DurationRecord>());
  // 6. Let dateDifference be ? DifferenceISODateTime(startDateTime.[[ISOYear]],
  // startDateTime.[[ISOMonth]], startDateTime.[[ISODay]],
  // startDateTime.[[ISOHour]], startDateTime.[[ISOMinute]],
  // startDateTime.[[ISOSecond]], startDateTime.[[ISOMillisecond]],
  // startDateTime.[[ISOMicrosecond]], startDateTime.[[ISONanosecond]],
  // endDateTime.[[ISOYear]], endDateTime.[[ISOMonth]], endDateTime.[[ISODay]],
  // endDateTime.[[ISOHour]], endDateTime.[[ISOMinute]],
  // endDateTime.[[ISOSecond]], endDateTime.[[ISOMillisecond]],
  // endDateTime.[[ISOMicrosecond]], endDateTime.[[ISONanosecond]], calendar,
  // largestUnit, options).
  DurationRecord date_difference;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, date_difference,
      DifferenceISODateTime(
          isolate,
          {{start_date_time->iso_year(), start_date_time->iso_month(),
            start_date_time->iso_day()},
           {start_date_time->iso_hour(), start_date_time->iso_minute(),
            start_date_time->iso_second(), start_date_time->iso_millisecond(),
            start_date_time->iso_microsecond(),
            start_date_time->iso_nanosecond()}},
          {{end_date_time->iso_year(), end_date_time->iso_month(),
            end_date_time->iso_day()},
           {end_date_time->iso_hour(), end_date_time->iso_minute(),
            end_date_time->iso_second(), end_date_time->iso_millisecond(),
            end_date_time->iso_microsecond(), end_date_time->iso_nanosecond()}},
          calendar, largest_unit, options, method_name),
      Nothing<DurationRecord>());

  // 7. Let intermediateNs be ? AddZonedDateTime(ns1, timeZone, calendar,
  // dateDifference.[[Years]], dateDifference.[[Months]],
  // dateDifference.[[Weeks]], 0, 0, 0, 0, 0, 0, 0).
  DirectHandle<BigInt> intermediate_ns;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, intermediate_ns,
      AddZonedDateTime(isolate, ns1, time_zone, calendar,
                       {date_difference.years,
                        date_difference.months,
                        date_difference.weeks,
                        {0, 0, 0, 0, 0, 0, 0}},
                       method_name),
      Nothing<DurationRecord>());
  // 8. Let timeRemainderNs be ns2 − intermediateNs.
  DirectHandle<BigInt> time_remainder_ns =
      BigInt::Subtract(isolate, ns2, intermediate_ns).ToHandleChecked();

  // 9. Let intermediate be ? CreateTemporalZonedDateTime(intermediateNs,
  // timeZone, calendar).
  DirectHandle<JSTemporalZonedDateTime> intermediate =
      CreateTemporalZonedDateTime(isolate, intermediate_ns, time_zone, calendar)
          .ToHandleChecked();

  // 10. Let result be ? NanosecondsToDays(ℝ(timeRemainderNs), intermediate).
  NanosecondsToDaysResult result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result,
      NanosecondsToDays(isolate, time_remainder_ns, intermediate, method_name),
      Nothing<DurationRecord>());

  // 11. Let timeDifference be ! BalanceDuration(0, 0, 0, 0, 0, 0,
  // result.[[Nanoseconds]], "hour").
  TimeDurationRecord time_difference =
      BalanceDuration(isolate, Unit::kHour,
                      {0, 0, 0, 0, 0, 0, result.nanoseconds}, method_name)
          .ToChecked();

  // 12. Return ! CreateDurationRecord(dateDifference.[[Years]],
  // dateDifference.[[Months]], dateDifference.[[Weeks]], result.[[Days]],
  // timeDifference.[[Hours]], timeDifference.[[Minutes]],
  // timeDifference.[[Seconds]], timeDifference.[[Milliseconds]],
  // timeDifference.[[Microseconds]], timeDifference.[[Nanoseconds]]).
  time_difference.days = result.days;
  return Just(CreateDurationRecord(
                  isolate, {date_difference.years, date_difference.months,
                            date_difference.weeks, time_difference})
                  .ToChecked());
}

Maybe<DurationRecord> AddDuration(Isolate* isolate, const DurationRecord& dur1,
                                  const DurationRecord& dur2,
                                  DirectHandle<Object> relative_to_obj,
                                  const char* method_name) {
  TEMPORAL_ENTER_FUNC();

  Factory* factory = isolate->factory();
  DurationRecord result;
  // 1. Let largestUnit1 be ! DefaultTemporalLargestUnit(y1, mon1, w1, d1, h1,
  // min1, s1, ms1, mus1).
  Unit largest_unit1 = DefaultTemporalLargestUnit(dur1);
  // 2. Let largestUnit2 be ! DefaultTemporalLargestUnit(y2, mon2, w2, d2, h2,
  // min2, s2, ms2, mus2).
  Unit largest_unit2 = DefaultTemporalLargestUnit(dur2);
  // 3. Let largestUnit be ! LargerOfTwoTemporalUnits(largestUnit1,
  // largestUnit2).
  Unit largest_unit = LargerOfTwoTemporalUnits(largest_unit1, largest_unit2);

  // 5. If relativeTo is undefined, then
  if (IsUndefined(*relative_to_obj)) {
    // a. If largestUnit is one of "year", "month", or "week", then
    if (largest_unit == Unit::kYear || largest_unit == Unit::kMonth ||
        largest_unit == Unit::kWeek) {
      // i. Throw a RangeError exception.
      THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                   NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                   Nothing<DurationRecord>());
    }
    // b. Let result be ? BalanceDuration(d1 + d2, h1 + h2, min1 + min2, s1 +
    // s2, ms1 + ms2, mus1 + mus2, ns1 + ns2, largestUnit).
    // Note: We call a special version of BalanceDuration which add two duration
    // internally to avoid overflow the double.
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, result.time_duration,
        BalanceDuration(isolate, largest_unit, dur1.time_duration,
                        dur2.time_duration, method_name),
        Nothing<DurationRecord>());

    // c. Return ! CreateDurationRecord(0, 0, 0, result.[[Days]],
    // result.[[Hours]], result.[[Minutes]], result.[[Seconds]],
    // result.[[Milliseconds]], result.[[Microseconds]],
    // result.[[Nanoseconds]]).
    return Just(CreateDurationRecord(isolate, {0, 0, 0, result.time_duration})
                    .ToChecked());
    // 5. If relativeTo has an [[InitializedTemporalDate]] internal slot, then
  } else if (IsJSTemporalPlainDate(*relative_to_obj)) {
    // a. Let calendar be relativeTo.[[Calendar]].
    DirectHandle<JSTemporalPlainDate> relative_to =
        Cast<JSTemporalPlainDate>(relative_to_obj);
    DirectHandle<JSReceiver> calendar(relative_to->calendar(), isolate);
    // b. Let dateDuration1 be ? CreateTemporalDuration(y1, mon1, w1, d1, 0, 0,
    // 0, 0, 0, 0).
    DirectHandle<JSTemporalDuration> date_duration1;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, date_duration1,
        CreateTemporalDuration(isolate,
                               {dur1.years,
                                dur1.months,
                                dur1.weeks,
                                {dur1.time_duration.days, 0, 0, 0, 0, 0, 0}}),
        Nothing<DurationRecord>());
    // c. Let dateDuration2 be ? CreateTemporalDuration(y2, mon2, w2, d2, 0, 0,
    // 0, 0, 0, 0).
    DirectHandle<JSTemporalDuration> date_duration2;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, date_duration2,
        CreateTemporalDuration(isolate,
                               {dur2.years,
                                dur2.months,
                                dur2.weeks,
                                {dur2.time_duration.days, 0, 0, 0, 0, 0, 0}}),
        Nothing<DurationRecord>());
    // d. Let dateAdd be ? GetMethod(calendar, "dateAdd").
    DirectHandle<Object> date_add;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, date_add,
        Object::GetMethod(isolate, calendar, factory->dateAdd_string()),
        Nothing<DurationRecord>());
    // e. Let intermediate be ? CalendarDateAdd(calendar, relativeTo,
    // dateDuration1, undefined, dateAdd).
    DirectHandle<JSTemporalPlainDate> intermediate;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, intermediate,
        CalendarDateAdd(isolate, calendar, relative_to, date_duration1,
                        factory->undefined_value(), date_add),
        Nothing<DurationRecord>());
    // f. Let end be ? CalendarDateAdd(calendar, intermediate, dateDuration2,
    // undefined, dateAdd).
    DirectHandle<JSTemporalPlainDate> end;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, end,
        CalendarDateAdd(isolate, calendar, intermediate, date_duration2,
                        factory->undefined_value(), date_add),
        Nothing<DurationRecord>());
    // g. Let dateLargestUnit be ! LargerOfTwoTemporalUnits("day", largestUnit).
    Unit date_largest_unit = LargerOfTwoTemporalUnits(Unit::kDay, largest_unit);
    // h. Let differenceOptions be ! OrdinaryObjectCreate(null).
    DirectHandle<JSObject> difference_options =
        factory->NewJSObjectWithNullProto();
    // i. Perform ! CreateDataPropertyOrThrow(differenceOptions, "largestUnit",
    // dateLargestUnit).
    CHECK(JSReceiver::CreateDataProperty(
              isolate, difference_options, factory->largestUnit_string(),
              UnitToString(isolate, date_largest_unit), Just(kThrowOnError))
              .FromJust());

    // j. Let dateDifference be ? CalendarDateUntil(calendar, relativeTo, end,
    // differenceOptions).
    DirectHandle<JSTemporalDuration> date_difference;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, date_difference,
        CalendarDateUntil(isolate, calendar, relative_to, end,
                          difference_options),
        Nothing<DurationRecord>());
    // n. Let result be ? BalanceDuration(dateDifference.[[Days]], h1 + h2, min1
    // + min2, s1 + s2, ms1 + ms2, mus1 + mus2, ns1 + ns2, largestUnit).
    // Note: We call a special version of BalanceDuration which add two duration
    // internally to avoid overflow the double.
    TimeDurationRecord time_dur1 = dur1.time_duration;
    time_dur1.days = Object::NumberValue(date_difference->days());
    TimeDurationRecord time_dur2 = dur2.time_duration;
    time_dur2.days = 0;
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, result.time_duration,
        BalanceDuration(isolate, largest_unit, time_dur1, time_dur2,
                        method_name),
        Nothing<DurationRecord>());
    // l. Return ! CreateDurationRecord(dateDifference.[[Years]],
    // dateDifference.[[Months]], dateDifference.[[Weeks]], result.[[Days]],
    // result.[[Hours]], result.[[Minutes]], result.[[Seconds]],
    // result.[[Milliseconds]], result.[[Microseconds]],
    // result.[[Nanoseconds]]).
    return Just(CreateDurationRecord(
                    isolate, {Object::NumberValue(date_difference->years()),
                              Object::NumberValue(date_difference->months()),
                              Object::NumberValue(date_difference->weeks()),
                              result.time_duration})
                    .ToChecked());
  }
  // 6. Assert: relativeTo has an [[InitializedTemporalZonedDateTime]]
  // internal slot.
  DCHECK(IsJSTemporalZonedDateTime(*relative_to_obj));
  auto relative_to = Cast<JSTemporalZonedDateTime>(relative_to_obj);
  // 7. Let timeZone be relativeTo.[[TimeZone]].
  DirectHandle<JSReceiver> time_zone(relative_to->time_zone(), isolate);
  // 8. Let calendar be relativeTo.[[Calendar]].
  DirectHandle<JSReceiver> calendar(relative_to->calendar(), isolate);
  // 9. Let intermediateNs be ? AddZonedDateTime(relativeTo.[[Nanoseconds]],
  // timeZone, calendar, y1, mon1, w1, d1, h1, min1, s1, ms1, mus1, ns1).
  DirectHandle<BigInt> intermediate_ns;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, intermediate_ns,
      AddZonedDateTime(isolate,
                       direct_handle(relative_to->nanoseconds(), isolate),
                       time_zone, calendar, dur1, method_name),
      Nothing<DurationRecord>());
  // 10. Let endNs be ? AddZonedDateTime(intermediateNs, timeZone, calendar,
  // y2, mon2, w2, d2, h2, min2, s2, ms2, mus2, ns2).
  DirectHandle<BigInt> end_ns;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, end_ns,
      AddZonedDateTime(isolate, intermediate_ns, time_zone, calendar, dur2,
                       method_name),
      Nothing<DurationRecord>());
  // 11. If largestUnit is not one of "year", "month", "week", or "day", then
  if (!(largest_unit == Unit::kYear || largest_unit == Unit::kMonth ||
        largest_unit == Unit::kWeek || largest_unit == Unit::kDay)) {
    // a. Let result be ! DifferenceInstant(relativeTo.[[Nanoseconds]], endNs,
    // 1, *"nanosecond"*, largestUnit, *"halfExpand"*).
    result.time_duration = DifferenceInstant(
        isolate, direct_handle(relative_to->nanoseconds(), isolate), end_ns, 1,
        Unit::kNanosecond, largest_unit, RoundingMode::kHalfExpand,
        method_name);
    // b. Return ! CreateDurationRecord(0, 0, 0, 0, result.[[Hours]],
    // result.[[Minutes]], result.[[Seconds]], result.[[Milliseconds]],
    // result.[[Microseconds]], result.[[Nanoseconds]]).
    result.time_duration.days = 0;
    return Just(CreateDurationRecord(isolate, {0, 0, 0, result.time_duration})
                    .ToChecked());
  }
  // 12. Return ? DifferenceZonedDateTime(relativeTo.[[Nanoseconds]], endNs,
  // timeZone, calendar, largestUnit, OrdinaryObjectCreate(null)).
  return DifferenceZonedDateTime(
      isolate, direct_handle(relative_to->nanoseconds(), isolate), end_ns,
      time_zone, calendar, largest_unit, factory->NewJSObjectWithNullProto(),
      method_name);
}

MaybeDirectHandle<JSTemporalDuration>
AddDurationToOrSubtractDurationFromDuration(
    Isolate* isolate, Arithmetic operation,
    DirectHandle<JSTemporalDuration> duration, DirectHandle<Object> other_obj,
    DirectHandle<Object> options_obj, const char* method_name) {
  // 1. If operation is subtract, let sign be -1. Otherwise, let sign be 1.
  double sign = operation == Arithmetic::kSubtract ? -1.0 : 1.0;

  // 2. Set other to ? ToTemporalDurationRecord(other).
  DurationRecord other;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, other,
      temporal::ToTemporalDurationRecord(isolate, other_obj, method_name),
      DirectHandle<JSTemporalDuration>());

  // 3. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));

  // 4. Let relativeTo be ? ToRelativeTemporalObject(options).
  DirectHandle<Object> relative_to;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, relative_to,
      ToRelativeTemporalObject(isolate, options, method_name));

  // 5. Let result be ? AddDuration(duration.[[Years]], duration.[[Months]],
  // duration.[[Weeks]], duration.[[Days]], duration.[[Hours]],
  // duration.[[Minutes]], duration.[[Seconds]], duration.[[Milliseconds]],
  // duration.[[Microseconds]], duration.[[Nanoseconds]], sign ×
  // other.[[Years]], sign × other.[[Months]], sign × other.[[Weeks]], sign ×
  // other.[[Days]], sign × other.[[Hours]], sign × other.[[Minutes]], sign ×
  // other.[[Seconds]], sign × other.[[Milliseconds]], sign ×
  // other.[[Microseconds]], sign × other.[[Nanoseconds]], relativeTo).
  DurationRecord result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result,
      AddDuration(
          isolate,
          {Object::NumberValue(duration->years()),
           Object::NumberValue(duration->months()),
           Object::NumberValue(duration->weeks()),
           {Object::NumberValue(duration->days()),
            Object::NumberValue(duration->hours()),
            Object::NumberValue(duration->minutes()),
            Object::NumberValue(duration->seconds()),
            Object::NumberValue(duration->milliseconds()),
            Object::NumberValue(duration->microseconds()),
            Object::NumberValue(duration->nanoseconds())}},
          {sign * other.years,
           sign * other.months,
           sign * other.weeks,
           {sign * other.time_duration.days, sign * other.time_duration.hours,
            sign * other.time_duration.minutes,
            sign * other.time_duration.seconds,
            sign * other.time_duration.milliseconds,
            sign * other.time_duration.microseconds,
            sign * other.time_duration.nanoseconds}},
          relative_to, method_name),
      DirectHandle<JSTemporalDuration>());

  // 6. Return ! CreateTemporalDuration(result.[[Years]], result.[[Months]],
  // result.[[Weeks]], result.[[Days]], result.[[Hours]], result.[[Minutes]],
  // result.[[Seconds]], result.[[Milliseconds]], result.[[Microseconds]],
  // result.[[Nanoseconds]]).
  return CreateTemporalDuration(isolate, result).ToHandleChecked();
}

}  // namespace

// #sec-temporal.duration.prototype.add
MaybeDirectHandle<JSTemporalDuration> JSTemporalDuration::Add(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  return AddDurationToOrSubtractDurationFromDuration(
      isolate, Arithmetic::kAdd, duration, other, options,
      "Temporal.Duration.prototype.add");
}

// #sec-temporal.duration.prototype.subtract
MaybeDirectHandle<JSTemporalDuration> JSTemporalDuration::Subtract(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  return AddDurationToOrSubtractDurationFromDuration(
      isolate, Arithmetic::kSubtract, duration, other, options,
      "Temporal.Duration.prototype.subtract");
}

// #sec-temporal.duration.prototype.tojson
MaybeDirectHandle<String> JSTemporalDuration::ToJSON(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration) {
  // 1. Let duration be the this value.
  // 2. Perform ? RequireInternalSlot(duration,
  // [[InitializedTemporalDuration]]).
  // 3. Return ! TemporalDurationToString(duration.[[Years]],
  // duration.[[Months]], duration.[[Weeks]], duration.[[Days]],
  // duration.[[Hours]], duration.[[Minutes]], duration.[[Seconds]],
  // duration.[[Milliseconds]], duration.[[Microseconds]],
  // duration.[[Nanoseconds]], "auto").
  DurationRecord dur = {Object::NumberValue(duration->years()),
                        Object::NumberValue(duration->months()),
                        Object::NumberValue(duration->weeks()),
                        {Object::NumberValue(duration->days()),
                         Object::NumberValue(duration->hours()),
                         Object::NumberValue(duration->minutes()),
                         Object::NumberValue(duration->seconds()),
                         Object::NumberValue(duration->milliseconds()),
                         Object::NumberValue(duration->microseconds()),
                         Object::NumberValue(duration->nanoseconds())}};
  return TemporalDurationToString(isolate, dur, Precision::kAuto);
}

// #sec-temporal.duration.prototype.tolocalestring
MaybeDirectHandle<String> JSTemporalDuration::ToLocaleString(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration,
    DirectHandle<Object> locales, DirectHandle<Object> options) {
  // 1. Let duration be the this value.
  // 2. Perform ? RequireInternalSlot(duration,
  // [[InitializedTemporalDuration]]).
  // 3. Return ! TemporalDurationToString(duration.[[Years]],
  // duration.[[Months]], duration.[[Weeks]], duration.[[Days]],
  // duration.[[Hours]], duration.[[Minutes]], duration.[[Seconds]],
  // duration.[[Milliseconds]], duration.[[Microseconds]],
  // duration.[[Nanoseconds]], "auto").
  DurationRecord dur = {Object::NumberValue(duration->years()),
                        Object::NumberValue(duration->months()),
                        Object::NumberValue(duration->weeks()),
                        {Object::NumberValue(duration->days()),
                         Object::NumberValue(duration->hours()),
                         Object::NumberValue(duration->minutes()),
                         Object::NumberValue(duration->seconds()),
                         Object::NumberValue(duration->milliseconds()),
                         Object::NumberValue(duration->microseconds()),
                         Object::NumberValue(duration->nanoseconds())}};

  // TODO(ftang) Implement #sup-temporal.duration.prototype.tolocalestring
  return TemporalDurationToString(isolate, dur, Precision::kAuto);
}

namespace {
// #sec-temporal-moverelativezoneddatetime
MaybeDirectHandle<JSTemporalZonedDateTime> MoveRelativeZonedDateTime(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    const DateDurationRecord& duration, const char* method_name) {
  // 1. Let intermediateNs be ? AddZonedDateTime(zonedDateTime.[[Nanoseconds]],
  // zonedDateTime.[[TimeZone]], zonedDateTime.[[Calendar]], years, months,
  // weeks, days, 0, 0, 0, 0, 0, 0).
  DirectHandle<BigInt> intermediate_ns;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, intermediate_ns,
      AddZonedDateTime(isolate,
                       direct_handle(zoned_date_time->nanoseconds(), isolate),
                       direct_handle(zoned_date_time->time_zone(), isolate),
                       direct_handle(zoned_date_time->calendar(), isolate),
                       {duration.years,
                        duration.months,
                        duration.weeks,
                        {duration.days, 0, 0, 0, 0, 0, 0}},
                       method_name));
  // 2. Return ! CreateTemporalZonedDateTime(intermediateNs,
  // zonedDateTime.[[TimeZone]], zonedDateTime.[[Calendar]]).
  return CreateTemporalZonedDateTime(
             isolate, intermediate_ns,
             direct_handle(zoned_date_time->time_zone(), isolate),
             direct_handle(zoned_date_time->calendar(), isolate))
      .ToHandleChecked();
}

// #sec-temporal-daysuntil
double DaysUntil(Isolate* isolate, DirectHandle<JSTemporalPlainDate> earlier,
                 DirectHandle<JSTemporalPlainDate> later,
                 const char* method_name) {
  // 1. Let epochDays1 be MakeDay(𝔽(earlier.[[ISOYear]]), 𝔽(earlier.[[ISOMonth]]
  // - 1), 𝔽(earlier.[[ISODay]])).
  double epoch_days1 = MakeDay(earlier->iso_year(), earlier->iso_month() - 1,
                               earlier->iso_day());
  // 2. Assert: epochDays1 is finite.
  // 3. Let epochDays2 be MakeDay(𝔽(later.[[ISOYear]]), 𝔽(later.[[ISOMonth]] -
  // 1), 𝔽(later.[[ISODay]])).
  double epoch_days2 =
      MakeDay(later->iso_year(), later->iso_month() - 1, later->iso_day());
  // 4. Assert: epochDays2 is finite.
  // 5. Return ℝ(epochDays2) - ℝ(epochDays1).
  return epoch_days2 - epoch_days1;
}

// #sec-temporal-moverelativedate
Maybe<MoveRelativeDateResult> MoveRelativeDate(
    Isolate* isolate, DirectHandle<JSReceiver> calendar,
    DirectHandle<JSTemporalPlainDate> relative_to,
    DirectHandle<JSTemporalDuration> duration, const char* method_name) {
  // 1. Let newDate be ? CalendarDateAdd(calendar, relativeTo, duration).
  DirectHandle<JSTemporalPlainDate> new_date;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, new_date,
      CalendarDateAdd(isolate, calendar, relative_to, duration),
      Nothing<MoveRelativeDateResult>());
  // 2. Let days be DaysUntil(relativeTo, newDate).
  double days = DaysUntil(isolate, relative_to, new_date, method_name);
  // 3. Return the Record { [[RelativeTo]]: newDate, [[Days]]: days }.
  return Just(MoveRelativeDateResult({new_date, days}));
}

// #sec-temporal-roundduration
Maybe<DurationRecordWithRemainder> RoundDuration(
    Isolate* isolate, const DurationRecord& duration, double increment,
    Unit unit, RoundingMode rounding_mode, DirectHandle<Object> relative_to,
    const char* method_name) {
  TEMPORAL_ENTER_FUNC();
  // optional argument relativeTo (undefined, a Temporal.PlainDate, or a
  // Temporal.ZonedDateTime)
  DCHECK(IsUndefined(*relative_to) || IsJSTemporalPlainDate(*relative_to) ||
         IsJSTemporalZonedDateTime(*relative_to));

  Factory* factory = isolate->factory();
  DurationRecordWithRemainder result;
  result.record = duration;
  // 2. If unit is "year", "month", or "week", and relativeTo is undefined, then
  if ((unit == Unit::kYear || unit == Unit::kMonth || unit == Unit::kWeek) &&
      IsUndefined(*relative_to)) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<DurationRecordWithRemainder>());
  }

  // 3. Let zonedRelativeTo be undefined.
  DirectHandle<Object> zoned_relative_to =
      isolate->factory()->undefined_value();

  DirectHandle<JSReceiver> calendar;
  // 5. If relativeTo is not undefined, then
  if (!IsUndefined(*relative_to)) {
    // a. If relativeTo has an [[InitializedTemporalZonedDateTime]] internal
    // slot, then
    if (IsJSTemporalZonedDateTime(*relative_to)) {
      // i. Set zonedRelativeTo to relativeTo.
      zoned_relative_to = relative_to;
      // ii. Set relativeTo to ? ToTemporalDate(relativeTo).
      DirectHandle<JSTemporalPlainDate> date;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, date, ToTemporalDate(isolate, relative_to, method_name),
          Nothing<DurationRecordWithRemainder>());
      relative_to = date;
      // b. Else,
    } else {
      // i. Assert: relativeTo has an [[InitializedTemporalDate]] internal
      // slot.
      DCHECK(IsJSTemporalPlainDate(*relative_to));
    }
    // c. Let calendar be relativeTo.[[Calendar]].
    calendar = DirectHandle<JSReceiver>(
        Cast<JSTemporalPlainDate>(relative_to)->calendar(), isolate);
    // 5. Else,
  } else {
    // a. NOTE: calendar will not be used below.
  }
  double fractional_seconds = 0;
  // 6. If unit is one of "year", "month", "week", or "day", then
  if (unit == Unit::kYear || unit == Unit::kMonth || unit == Unit::kWeek ||
      unit == Unit::kDay) {
    // a. Let nanoseconds be ! TotalDurationNanoseconds(0, hours, minutes,
    // seconds, milliseconds, microseconds, nanoseconds, 0).
    TimeDurationRecord time_duration = duration.time_duration;
    time_duration.days = 0;
    DirectHandle<BigInt> nanoseconds =
        TotalDurationNanoseconds(isolate, time_duration, 0);

    // b. Let intermediate be undefined.
    DirectHandle<Object> intermediate = isolate->factory()->undefined_value();

    // c. If zonedRelativeTo is not undefined, then
    if (!IsUndefined(*zoned_relative_to)) {
      DCHECK(IsJSTemporalZonedDateTime(*zoned_relative_to));
      // i. Let intermediate be ? MoveRelativeZonedDateTime(zonedRelativeTo,
      // years, months, weeks, days).
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, intermediate,
          MoveRelativeZonedDateTime(
              isolate, Cast<JSTemporalZonedDateTime>(zoned_relative_to),
              {duration.years, duration.months, duration.weeks,
               duration.time_duration.days},
              method_name),
          Nothing<DurationRecordWithRemainder>());
    }

    // d. Let result be ? NanosecondsToDays(nanoseconds, intermediate).
    NanosecondsToDaysResult to_days_result;
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, to_days_result,
        NanosecondsToDays(isolate, nanoseconds, intermediate, method_name),
        Nothing<DurationRecordWithRemainder>());

    // e. Set days to days + result.[[Days]] + result.[[Nanoseconds]] /
    // result.[[DayLength]].
    result.record.time_duration.days +=
        to_days_result.days +
        // https://github.com/tc39/proposal-temporal/issues/2366
        std::round(to_days_result.nanoseconds / to_days_result.day_length);

    // f. Set hours, minutes, seconds, milliseconds, microseconds, and
    // nanoseconds to 0.
    result.record.time_duration.hours = result.record.time_duration.minutes =
        result.record.time_duration.seconds =
            result.record.time_duration.milliseconds =
                result.record.time_duration.microseconds =
                    result.record.time_duration.nanoseconds = 0;

    // 7. Else,
  } else {
    // a. Let fractionalSeconds be nanoseconds × 10^−9 + microseconds × 10^−6 +
    // milliseconds × 10^−3 + seconds.
    fractional_seconds = result.record.time_duration.nanoseconds * 1e-9 +
                         result.record.time_duration.microseconds * 1e-6 +
                         result.record.time_duration.milliseconds * 1e-3 +
                         result.record.time_duration.seconds;
  }
  // 8. Let remainder be undefined.
  result.remainder = -1;  // use -1 for undefined now.

  switch (unit) {
    // 9. If unit is "year", then
    case Unit::kYear: {
      // a. Let yearsDuration be ! CreateTemporalDuration(years, 0, 0, 0, 0, 0,
      // 0, 0, 0, 0).
      DirectHandle<JSTemporalDuration> years_duration =
          CreateTemporalDuration(isolate,
                                 {duration.years, 0, 0, {0, 0, 0, 0, 0, 0, 0}})
              .ToHandleChecked();

      // b. Let dateAdd be ? GetMethod(calendar, "dateAdd").
      DirectHandle<Object> date_add;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, date_add,
          Object::GetMethod(isolate, calendar, factory->dateAdd_string()),
          Nothing<DurationRecordWithRemainder>());

      // c. Let yearsLater be ? CalendarDateAdd(calendar, relativeTo,
      // yearsDuration, undefined, dateAdd).
      DirectHandle<JSTemporalPlainDate> years_later;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, years_later,
          CalendarDateAdd(isolate, calendar, relative_to, years_duration,
                          isolate->factory()->undefined_value(), date_add),
          Nothing<DurationRecordWithRemainder>());

      // d. Let yearsMonthsWeeks be ! CreateTemporalDuration(years, months,
      // weeks, 0, 0, 0, 0, 0, 0, 0).
      DirectHandle<JSTemporalDuration> years_months_weeks =
          CreateTemporalDuration(isolate, {duration.years,
                                           duration.months,
                                           duration.weeks,
                                           {0, 0, 0, 0, 0, 0, 0}})
              .ToHandleChecked();

      // e. Let yearsMonthsWeeksLater be ? CalendarDateAdd(calendar, relativeTo,
      // yearsMonthsWeeks, undefined, dateAdd).
      DirectHandle<JSTemporalPlainDate> years_months_weeks_later;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, years_months_weeks_later,
          CalendarDateAdd(isolate, calendar, relative_to, years_months_weeks,
                          isolate->factory()->undefined_value(), date_add),
          Nothing<DurationRecordWithRemainder>());

      // f. Let monthsWeeksInDays be DaysUntil(yearsLater,
      // yearsMonthsWeeksLater).
      double months_weeks_in_days = DaysUntil(
          isolate, years_later, years_months_weeks_later, method_name);

      // g. Set relativeTo to yearsLater.
      relative_to = years_later;

      // h. Let days be days + monthsWeeksInDays.
      result.record.time_duration.days += months_weeks_in_days;

      // i. Let daysDuration be ? CreateTemporalDuration(0, 0, 0, days, 0, 0, 0,
      // 0, 0, 0).
      DirectHandle<JSTemporalDuration> days_duration;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, days_duration,
          CreateTemporalDuration(
              isolate,
              {0, 0, 0, {result.record.time_duration.days, 0, 0, 0, 0, 0, 0}}),
          Nothing<DurationRecordWithRemainder>());

      // j. Let daysLater be ? CalendarDateAdd(calendar, relativeTo,
      // daysDuration, undefined, dateAdd).
      DirectHandle<JSTemporalPlainDate> days_later;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, days_later,
          CalendarDateAdd(isolate, calendar, relative_to, days_duration,
                          isolate->factory()->undefined_value(), date_add),
          Nothing<DurationRecordWithRemainder>());

      // k. Let untilOptions be OrdinaryObjectCreate(null).
      DirectHandle<JSObject> until_options =
          factory->NewJSObjectWithNullProto();

      // l. Perform ! CreateDataPropertyOrThrow(untilOptions, "largestUnit",
      // "year").
      CHECK(JSReceiver::CreateDataProperty(
                isolate, until_options, factory->largestUnit_string(),
                factory->year_string(), Just(kThrowOnError))
                .FromJust());

      // m. Let timePassed be ? CalendarDateUntil(calendar, relativeTo,
      // daysLater, untilOptions).
      DirectHandle<JSTemporalDuration> time_passed;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, time_passed,
          CalendarDateUntil(isolate, calendar, relative_to, days_later,
                            until_options),
          Nothing<DurationRecordWithRemainder>());

      // n. Let yearsPassed be timePassed.[[Years]].
      double years_passed = Object::NumberValue(time_passed->years());

      // o. Set years to years + yearsPassed.
      result.record.years += years_passed;

      // p. Let oldRelativeTo be relativeTo.
      DirectHandle<Object> old_relative_to = relative_to;

      // q. Let yearsDuration be ? CreateTemporalDuration(yearsPassed, 0, 0, 0,
      // 0, 0, 0, 0, 0, 0).
      years_duration = CreateTemporalDuration(
                           isolate, {years_passed, 0, 0, {0, 0, 0, 0, 0, 0, 0}})
                           .ToHandleChecked();

      // r. Set relativeTo to ? CalendarDateAdd(calendar, relativeTo,
      // yearsDuration, undefined, dateAdd).
      DirectHandle<JSTemporalPlainDate> years_added;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, years_added,
          CalendarDateAdd(isolate, calendar, relative_to, years_duration,
                          isolate->factory()->undefined_value(), date_add),
          Nothing<DurationRecordWithRemainder>());
      relative_to = years_added;

      // s. Let daysPassed be DaysUntil(oldRelativeTo, relativeTo).
      DCHECK(IsJSTemporalPlainDate(*old_relative_to));
      DCHECK(IsJSTemporalPlainDate(*relative_to));
      double days_passed =
          DaysUntil(isolate, Cast<JSTemporalPlainDate>(old_relative_to),
                    Cast<JSTemporalPlainDate>(relative_to), method_name);

      // t. Set days to days - daysPassed.
      result.record.time_duration.days -= days_passed;

      // u. If days < 0, let sign be -1; else, let sign be 1.
      double sign = result.record.time_duration.days < 0 ? -1 : 1;

      // v. Let oneYear be ! CreateTemporalDuration(sign, 0, 0, 0, 0, 0, 0, 0,
      // 0, 0).
      DirectHandle<JSTemporalDuration> one_year =
          CreateTemporalDuration(isolate, {sign, 0, 0, {0, 0, 0, 0, 0, 0, 0}})
              .ToHandleChecked();

      // w. Let moveResult be ? MoveRelativeDate(calendar, relativeTo, oneYear).
      MoveRelativeDateResult move_result;
      DCHECK(IsJSTemporalPlainDate(*relative_to));
      MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, move_result,
          MoveRelativeDate(isolate, calendar,
                           Cast<JSTemporalPlainDate>(relative_to), one_year,
                           method_name),
          Nothing<DurationRecordWithRemainder>());

      // x. Let oneYearDays be moveResult.[[Days]].
      double one_year_days = move_result.days;
      // y. Let fractionalYears be years + days / abs(oneYearDays).
      double fractional_years =
          result.record.years +
          result.record.time_duration.days / std::abs(one_year_days);
      // z. Set years to RoundNumberToIncrement(fractionalYears, increment,
      // roundingMode).
      result.record.years = RoundNumberToIncrement(isolate, fractional_years,
                                                   increment, rounding_mode);
      // aa. Set remainder to fractionalYears - years.
      result.remainder = fractional_years - result.record.years;
      // ab. Set months, weeks, and days to 0.
      result.record.months = result.record.weeks =
          result.record.time_duration.days = 0;
    } break;
    // 10. Else if unit is "month", then
    case Unit::kMonth: {
      // a. Let yearsMonths be ! CreateTemporalDuration(years, months, 0, 0, 0,
      // 0, 0, 0, 0, 0).
      DirectHandle<JSTemporalDuration> years_months =
          CreateTemporalDuration(
              isolate,
              {duration.years, duration.months, 0, {0, 0, 0, 0, 0, 0, 0}})
              .ToHandleChecked();

      // b. Let dateAdd be ? GetMethod(calendar, "dateAdd").
      DirectHandle<Object> date_add;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, date_add,
          Object::GetMethod(isolate, calendar, factory->dateAdd_string()),
          Nothing<DurationRecordWithRemainder>());

      // c. Let yearsMonthsLater be ? CalendarDateAdd(calendar, relativeTo,
      // yearsMonths, undefined, dateAdd).
      DirectHandle<JSTemporalPlainDate> years_months_later;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, years_months_later,
          CalendarDateAdd(isolate, calendar, relative_to, years_months,
                          isolate->factory()->undefined_value(), date_add),
          Nothing<DurationRecordWithRemainder>());

      // d. Let yearsMonthsWeeks be ! CreateTemporalDuration(years, months,
      // weeks, 0, 0, 0, 0, 0, 0, 0).
      DirectHandle<JSTemporalDuration> years_months_weeks =
          CreateTemporalDuration(isolate, {duration.years,
                                           duration.months,
                                           duration.weeks,
                                           {0, 0, 0, 0, 0, 0, 0}})
              .ToHandleChecked();

      // e. Let yearsMonthsWeeksLater be ? CalendarDateAdd(calendar, relativeTo,
      // yearsMonthsWeeks, undefined, dateAdd).
      DirectHandle<JSTemporalPlainDate> years_months_weeks_later;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, years_months_weeks_later,
          CalendarDateAdd(isolate, calendar, relative_to, years_months_weeks,
                          isolate->factory()->undefined_value(), date_add),
          Nothing<DurationRecordWithRemainder>());

      // f. Let weeksInDays be DaysUntil(yearsMonthsLater,
      // yearsMonthsWeeksLater).
      double weeks_in_days = DaysUntil(isolate, years_months_later,
                                       years_months_weeks_later, method_name);

      // g. Set relativeTo to yearsMonthsLater.
      relative_to = years_months_later;

      // h. Let days be days + weeksInDays.
      result.record.time_duration.days += weeks_in_days;

      // i. If days < 0, let sign be -1; else, let sign be 1.
      double sign = result.record.time_duration.days < 0 ? -1 : 1;

      // j. Let oneMonth be ! CreateTemporalDuration(0, sign, 0, 0, 0, 0, 0, 0,
      // 0, 0).
      DirectHandle<JSTemporalDuration> one_month =
          CreateTemporalDuration(isolate, {0, sign, 0, {0, 0, 0, 0, 0, 0, 0}})
              .ToHandleChecked();

      // k. Let moveResult be ? MoveRelativeDate(calendar, relativeTo,
      // oneMonth).
      MoveRelativeDateResult move_result;
      DCHECK(IsJSTemporalPlainDate(*relative_to));
      MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, move_result,
          MoveRelativeDate(isolate, calendar,
                           Cast<JSTemporalPlainDate>(relative_to), one_month,
                           method_name),
          Nothing<DurationRecordWithRemainder>());

      // l. Set relativeTo to moveResult.[[RelativeTo]].
      relative_to = move_result.relative_to;

      // m. Let oneMonthDays be moveResult.[[Days]].
      double one_month_days = move_result.days;

      // n. Repeat, while abs(days) ≥ abs(oneMonthDays),
      while (std::abs(result.record.time_duration.days) >=
             std::abs(one_month_days)) {
        // i. Set months to months + sign.
        result.record.months += sign;
        // ii. Set days to days - oneMonthDays.
        result.record.time_duration.days -= one_month_days;
        // iii. Set moveResult to ? MoveRelativeDate(calendar, relativeTo,
        // oneMonth).
        DCHECK(IsJSTemporalPlainDate(*relative_to));
        MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
            isolate, move_result,
            MoveRelativeDate(isolate, calendar,
                             Cast<JSTemporalPlainDate>(relative_to), one_month,
                             method_name),
            Nothing<DurationRecordWithRemainder>());
        // iv. Set relativeTo to moveResult.[[RelativeTo]].
        relative_to = move_result.relative_to;
        // v. Set oneMonthDays to moveResult.[[Days]].
        one_month_days = move_result.days;
      }
      // o. Let fractionalMonths be months + days / abs(oneMonthDays).
      double fractional_months =
          result.record.months +
          result.record.time_duration.days / std::abs(one_month_days);
      // p. Set months to RoundNumberToIncrement(fractionalMonths, increment,
      // roundingMode).
      result.record.months = RoundNumberToIncrement(isolate, fractional_months,
                                                    increment, rounding_mode);
      // q. Set remainder to fractionalMonths - months.
      result.remainder = fractional_months - result.record.months;
      // r. Set weeks and days to 0.
      result.record.weeks = result.record.time_duration.days = 0;
    } break;
    // 11. Else if unit is "week", then
    case Unit::kWeek: {
      // a. If days < 0, let sign be -1; else, let sign be 1.
      double sign = result.record.time_duration.days < 0 ? -1 : 1;
      // b. Let oneWeek be ! CreateTemporalDuration(0, 0, sign, 0, 0, 0, 0, 0,
      // 0, 0).
      DirectHandle<JSTemporalDuration> one_week =
          CreateTemporalDuration(isolate, {0, 0, sign, {0, 0, 0, 0, 0, 0, 0}})
              .ToHandleChecked();

      // c. Let moveResult be ? MoveRelativeDate(calendar, relativeTo, oneWeek).
      MoveRelativeDateResult move_result;
      DCHECK(IsJSTemporalPlainDate(*relative_to));
      MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, move_result,
          MoveRelativeDate(isolate, calendar,
                           Cast<JSTemporalPlainDate>(relative_to), one_week,
                           method_name),
          Nothing<DurationRecordWithRemainder>());

      // d. Set relativeTo to moveResult.[[RelativeTo]].
      relative_to = move_result.relative_to;

      // e. Let oneWeekDays be moveResult.[[Days]].
      double one_week_days = move_result.days;

      // f. Repeat, while abs(days) ≥ abs(oneWeekDays),
      while (std::abs(result.record.time_duration.days) >=
             std::abs(one_week_days)) {
        // i. Set weeks to weeks + sign.
        result.record.weeks += sign;
        // ii. Set days to days - oneWeekDays.
        result.record.time_duration.days -= one_week_days;
        // iii. Set moveResult to ? MoveRelativeDate(calendar, relativeTo,
        // oneWeek).
        DCHECK(IsJSTemporalPlainDate(*relative_to));
        MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
            isolate, move_result,
            MoveRelativeDate(isolate, calendar,
                             Cast<JSTemporalPlainDate>(relative_to), one_week,
                             method_name),
            Nothing<DurationRecordWithRemainder>());
        // iv. Set relativeTo to moveResult.[[RelativeTo]].
        relative_to = move_result.relative_to;
        // v. Set oneWeekDays to moveResult.[[Days]].
        one_week_days = move_result.days;
      }

      // g. Let fractionalWeeks be weeks + days / abs(oneWeekDays).
      double fractional_weeks =
          result.record.weeks +
          result.record.time_duration.days / std::abs(one_week_days);
      // h. Set weeks to RoundNumberToIncrement(fractionalWeeks, increment,
      // roundingMode).
      result.record.weeks = RoundNumberToIncrement(isolate, fractional_weeks,
                                                   increment, rounding_mode);
      // i. Set remainder to fractionalWeeks - weeks.
      result.remainder = fractional_weeks - result.record.weeks;
      // j. Set days to 0.
      result.record.time_duration.days = 0;
    } break;
    // 12. Else if unit is "day", then
    case Unit::kDay: {
      // a. Let fractionalDays be days.
      double fractional_days = result.record.time_duration.days;

      // b. Set days to ! RoundNumberToIncrement(days, increment, roundingMode).
      result.record.time_duration.days = RoundNumberToIncrement(
          isolate, result.record.time_duration.days, increment, rounding_mode);

      // c. Set remainder to fractionalDays - days.
      result.remainder = fractional_days - result.record.time_duration.days;
    } break;
    // 13. Else if unit is "hour", then
    case Unit::kHour: {
      // a. Let fractionalHours be (fractionalSeconds / 60 + minutes) / 60 +
      // hours.
      double fractional_hours =
          (fractional_seconds / 60.0 + duration.time_duration.minutes) / 60.0 +
          duration.time_duration.hours;

      // b. Set hours to ! RoundNumberToIncrement(fractionalHours, increment,
      // roundingMode).
      result.record.time_duration.hours = RoundNumberToIncrement(
          isolate, fractional_hours, increment, rounding_mode);

      // c. Set remainder to fractionalHours - hours.
      result.remainder = fractional_hours - result.record.time_duration.hours;

      // d. Set minutes, seconds, milliseconds, microseconds, and nanoseconds to
      // 0.
      result.record.time_duration.minutes =
          result.record.time_duration.seconds =
              result.record.time_duration.milliseconds =
                  result.record.time_duration.microseconds =
                      result.record.time_duration.nanoseconds = 0;
    } break;
    // 14. Else if unit is "minute", then
    case Unit::kMinute: {
      // a. Let fractionalMinutes be fractionalSeconds / 60 + minutes.
      double fractional_minutes =
          fractional_seconds / 60.0 + duration.time_duration.minutes;

      // b. Set minutes to ! RoundNumberToIncrement(fractionalMinutes,
      // increment, roundingMode).
      result.record.time_duration.minutes = RoundNumberToIncrement(
          isolate, fractional_minutes, increment, rounding_mode);

      // c. Set remainder to fractionalMinutes - minutes.
      result.remainder =
          fractional_minutes - result.record.time_duration.minutes;

      // d. Set seconds, milliseconds, microseconds, and nanoseconds to 0.
      result.record.time_duration.seconds =
          result.record.time_duration.milliseconds =
              result.record.time_duration.microseconds =
                  result.record.time_duration.nanoseconds = 0;
    } break;
    // 15. Else if unit is "second", then
    case Unit::kSecond: {
      // a. Set seconds to ! RoundNumberToIncrement(fractionalSeconds,
      // increment, roundingMode).
      result.record.time_duration.seconds = RoundNumberToIncrement(
          isolate, fractional_seconds, increment, rounding_mode);

      // b. Set remainder to fractionalSeconds - seconds.
      result.remainder =
          fractional_seconds - result.record.time_duration.seconds;

      // c. Set milliseconds, microseconds, and nanoseconds to 0.
      result.record.time_duration.milliseconds =
          result.record.time_duration.microseconds =
              result.record.time_duration.nanoseconds = 0;
    } break;
    // 16. Else if unit is "millisecond", then
    case Unit::kMillisecond: {
      // a. Let fractionalMilliseconds be nanoseconds × 10^−6 + microseconds ×
      // 10^−3 + milliseconds.
      double fractional_milliseconds =
          duration.time_duration.nanoseconds * 1e-6 +
          duration.time_duration.microseconds * 1e-3 +
          duration.time_duration.milliseconds;

      // b. Set milliseconds to ! RoundNumberToIncrement(fractionalMilliseconds,
      // increment, roundingMode).
      result.record.time_duration.milliseconds = RoundNumberToIncrement(
          isolate, fractional_milliseconds, increment, rounding_mode);

      // c. Set remainder to fractionalMilliseconds - milliseconds.
      result.remainder =
          fractional_milliseconds - result.record.time_duration.milliseconds;

      // d. Set microseconds and nanoseconds to 0.
      result.record.time_duration.microseconds =
          result.record.time_duration.nanoseconds = 0;
    } break;
    // 17. Else if unit is "microsecond", then
    case Unit::kMicrosecond: {
      // a. Let fractionalMicroseconds be nanoseconds × 10−3 + microseconds.
      double fractional_microseconds =
          duration.time_duration.nanoseconds * 1e-3 +
          duration.time_duration.microseconds;

      // b. Set microseconds to ! RoundNumberToIncrement(fractionalMicroseconds,
      // increment, roundingMode).
      result.record.time_duration.microseconds = RoundNumberToIncrement(
          isolate, fractional_microseconds, increment, rounding_mode);

      // c. Set remainder to fractionalMicroseconds - microseconds.
      result.remainder =
          fractional_microseconds - result.record.time_duration.microseconds;

      // d. Set nanoseconds to 0.
      result.record.time_duration.nanoseconds = 0;
    } break;
    // 18. Else,
    default: {
      // a. Assert: unit is "nanosecond".
      DCHECK_EQ(unit, Unit::kNanosecond);
      // b. Set remainder to nanoseconds.
      result.remainder = result.record.time_duration.nanoseconds;

      // c. Set nanoseconds to ! RoundNumberToIncrement(nanoseconds, increment,
      // roundingMode).
      result.record.time_duration.nanoseconds = RoundNumberToIncrement(
          isolate, result.record.time_duration.nanoseconds, increment,
          rounding_mode);

      // d. Set remainder to remainder − nanoseconds.
      result.remainder -= result.record.time_duration.nanoseconds;
    } break;
  }
  // 19. Let duration be ? CreateDurationRecord(years, months, weeks, days,
  // hours, minutes, seconds, milliseconds, microseconds, nanoseconds).
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result.record, CreateDurationRecord(isolate, result.record),
      Nothing<DurationRecordWithRemainder>());

  return Just(result);
}

Maybe<DurationRecordWithRemainder> RoundDuration(Isolate* isolate,
                                                 const DurationRecord& duration,
                                                 double increment, Unit unit,
                                                 RoundingMode rounding_mode,
                                                 const char* method_name) {
  // 1. If relativeTo is not present, set relativeTo to undefined.
  return RoundDuration(isolate, duration, increment, unit, rounding_mode,
                       isolate->factory()->undefined_value(), method_name);
}

// #sec-temporal-tosecondsstringprecision
struct StringPrecision {
  Precision precision;
  Unit unit;
  double increment;
};

// #sec-temporal-tosecondsstringprecision
Maybe<StringPrecision> ToSecondsStringPrecision(
    Isolate* isolate, DirectHandle<JSReceiver> normalized_options,
    const char* method_name);

}  // namespace

// #sec-temporal.duration.prototype.tostring
MaybeDirectHandle<String> JSTemporalDuration::ToString(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration,
    DirectHandle<Object> options_obj) {
  const char* method_name = "Temporal.Duration.prototype.toString";
  // 3. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));
  // 4. Let precision be ? ToSecondsStringPrecision(options).
  StringPrecision precision;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, precision,
      ToSecondsStringPrecision(isolate, options, method_name),
      DirectHandle<String>());

  // 5. If precision.[[Unit]] is "minute", throw a RangeError exception.
  if (precision.unit == Unit::kMinute) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }
  // 6. Let roundingMode be ? ToTemporalRoundingMode(options, "trunc").
  RoundingMode rounding_mode;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, rounding_mode,
      ToTemporalRoundingMode(isolate, options, RoundingMode::kTrunc,
                             method_name),
      DirectHandle<String>());

  // 7. Let result be ? RoundDuration(duration.[[Years]], duration.[[Months]],
  // duration.[[Weeks]], duration.[[Days]], duration.[[Hours]],
  // duration.[[Minutes]], duration.[[Seconds]], duration.[[Milliseconds]],
  // duration.[[Microseconds]], duration.[[Nanoseconds]],
  // precision.[[Increment]], precision.[[Unit]], roundingMode).
  DurationRecord dur = {Object::NumberValue(duration->years()),
                        Object::NumberValue(duration->months()),
                        Object::NumberValue(duration->weeks()),
                        {Object::NumberValue(duration->days()),
                         Object::NumberValue(duration->hours()),
                         Object::NumberValue(duration->minutes()),
                         Object::NumberValue(duration->seconds()),
                         Object::NumberValue(duration->milliseconds()),
                         Object::NumberValue(duration->microseconds()),
                         Object::NumberValue(duration->nanoseconds())}};
  DurationRecordWithRemainder result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result,
      RoundDuration(isolate, dur, precision.increment, precision.unit,
                    rounding_mode, method_name),
      DirectHandle<String>());

  // 8. Return ! TemporalDurationToString(result.[[Years]], result.[[Months]],
  // result.[[Weeks]], result.[[Days]], result.[[Hours]], result.[[Minutes]],
  // result.[[Seconds]], result.[[Milliseconds]], result.[[Microseconds]],
  // result.[[Nanoseconds]], precision.[[Precision]]).

  return TemporalDurationToString(isolate, result.record, precision.precision);
}

// #sec-temporal.calendar
MaybeDirectHandle<JSTemporalCalendar> JSTemporalCalendar::Constructor(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, DirectHandle<Object> identifier_obj) {
  // 1. If NewTarget is undefined, then
  if (IsUndefined(*new_target, isolate)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kConstructorNotFunction,
                                 isolate->factory()->NewStringFromStaticChars(
                                     "Temporal.Calendar")));
  }
  // 2. Set identifier to ? ToString(identifier).
  DirectHandle<String> identifier;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, identifier,
                             Object::ToString(isolate, identifier_obj));
  // 3. If ! IsBuiltinCalendar(id) is false, then
  if (!IsBuiltinCalendar(isolate, identifier)) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR(
        isolate, NewRangeError(MessageTemplate::kInvalidCalendar, identifier));
  }
  return CreateTemporalCalendar(isolate, target, new_target, identifier);
}

namespace {

// #sec-temporal-toisodayofyear
int32_t ToISODayOfYear(Isolate* isolate, const DateRecord& date) {
  TEMPORAL_ENTER_FUNC();
  // 1. Assert: IsValidISODate(year, month, day) is *true*.
  DCHECK(IsValidISODate(isolate, date));
  // 2. Let _epochDays_ be MakeDay(𝔽(year), 𝔽(month - 1), 𝔽(day)).
  // 3. Assert: _epochDays_ is finite.
  // 4. Return ℝ(DayWithinYear(MakeDate(_epochDays_, *+0*<sub>𝔽</sub>))) + 1.
  // Note: In ISO 8601, Jan: month=1, Dec: month=12,
  // In DateCache API, Jan: month=0, Dec: month=11 so we need to - 1 for month.
  return date.day +
         isolate->date_cache()->DaysFromYearMonth(date.year, date.month - 1) -
         isolate->date_cache()->DaysFromYearMonth(date.year, 0);
}

bool IsPlainDatePlainDateTimeOrPlainYearMonth(
    DirectHandle<Object> temporal_date_like) {
  return IsJSTemporalPlainDate(*temporal_date_like) ||
         IsJSTemporalPlainDateTime(*temporal_date_like) ||
         IsJSTemporalPlainYearMonth(*temporal_date_like);
}

// #sec-temporal-toisodayofweek
int32_t ToISODayOfWeek(Isolate* isolate, const DateRecord& date) {
  TEMPORAL_ENTER_FUNC();

  // 1. Assert: IsValidISODate(year, month, day) is *true*.
  DCHECK(IsValidISODate(isolate, date));
  // 2. Let _epochDays_ be MakeDay(𝔽(year), 𝔽(month - 1), 𝔽(day)).
  // Note: "- 1" after "date.day" came from the MakeyDay AO in
  // "9. Return Day(t) + dt - 1𝔽."
  int32_t epoch_days =
      isolate->date_cache()->DaysFromYearMonth(date.year, date.month - 1) +
      date.day - 1;
  // 3. Assert: _epochDays_ is finite.
  // 4. Let _dayOfWeek_ be WeekDay(MakeDate(_epochDays_, *+0*<sub>𝔽</sub>)).
  int32_t weekday = isolate->date_cache()->Weekday(epoch_days);
  // 5. If _dayOfWeek_ = *+0*<sub>𝔽</sub>, return 7.

  // Note: In ISO 8601, Jan: month=1, Dec: month=12.
  // In DateCache API, Jan: month=0, Dec: month=11 so we need to - 1 for month.
  // Weekday() expect "the number of days since the epoch" as input and the
  // value of day is 1-based so we need to minus 1 to calculate "the number of
  // days" because the number of days on the epoch (1970/1/1) should be 0,
  // not 1
  // Note: In ISO 8601, Sun: weekday=7 Mon: weekday=1
  // In DateCache API, Sun: weekday=0 Mon: weekday=1
  // 6. Return ℝ(_dayOfWeek_).
  return weekday == 0 ? 7 : weekday;
}

// #sec-temporal-regulateisodate
Maybe<DateRecord> RegulateISODate(Isolate* isolate, ShowOverflow overflow,
                                  const DateRecord& date) {
  TEMPORAL_ENTER_FUNC();

  // 1. Assert: year, month, and day are integers.
  // 2. Assert: overflow is either "constrain" or "reject".
  switch (overflow) {
    // 3. If overflow is "reject", then
    case ShowOverflow::kReject:
      // a. If ! IsValidISODate(year, month, day) is false, throw a RangeError
      // exception.
      if (!IsValidISODate(isolate, date)) {
        THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                     NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                     Nothing<DateRecord>());
      }
      // b. Return the Record { [[Year]]: year, [[Month]]: month, [[Day]]: day
      // }.
      return Just(date);
    // 4. If overflow is "constrain", then
    case ShowOverflow::kConstrain:
      DateRecord result(date);
      // a. Set month to ! ConstrainToRange(month, 1, 12).
      result.month = std::max(std::min(result.month, 12), 1);
      // b. Set day to ! ConstrainToRange(day, 1, ! ISODaysInMonth(year,
      // month)).
      result.day =
          std::max(std::min(result.day,
                            ISODaysInMonth(isolate, result.year, result.month)),
                   1);
      // c. Return the Record { [[Year]]: year, [[Month]]: month, [[Day]]: day
      // }.
      return Just(result);
  }
}

// #sec-temporal-regulateisoyearmonth
Maybe<int32_t> RegulateISOYearMonth(Isolate* isolate, ShowOverflow overflow,
                                    int32_t month) {
  // 1. Assert: year and month are integers.
  // 2. Assert: overflow is either "constrain" or "reject".
  switch (overflow) {
    // 3. If overflow is "constrain", then
    case ShowOverflow::kConstrain:
      // a. Return ! ConstrainISOYearMonth(year, month).
      return Just(std::max(std::min(month, 12), 1));
    // 4. If overflow is "reject", then
    case ShowOverflow::kReject:
      // a. If ! IsValidISOMonth(month) is false, throw a RangeError exception.
      if (month < 1 || 12 < month) {
        THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                     NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                     Nothing<int32_t>());
      }
      // b. Return the new Record { [[Year]]: year, [[Month]]: month }.
      return Just(month);
    default:
      UNREACHABLE();
  }
}

// #sec-temporal-resolveisomonth
Maybe<int32_t> ResolveISOMonth(Isolate* isolate,
                               DirectHandle<JSReceiver> fields) {
  Factory* factory = isolate->factory();
  // 1. Let month be ! Get(fields, "month").
  DirectHandle<Object> month_obj =
      JSReceiver::GetProperty(isolate, fields, factory->month_string())
          .ToHandleChecked();
  // 2. Let monthCode be ! Get(fields, "monthCode").
  DirectHandle<Object> month_code_obj =
      JSReceiver::GetProperty(isolate, fields, factory->monthCode_string())
          .ToHandleChecked();
  // 3. If monthCode is undefined, then
  if (IsUndefined(*month_code_obj, isolate)) {
    // a. If month is undefined, throw a TypeError exception.
    if (IsUndefined(*month_obj, isolate)) {
      THROW_NEW_ERROR_RETURN_VALUE(
          isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR(), Nothing<int32_t>());
    }
    // b. Return month.
    // Note: In Temporal spec, "month" in fields is always converted by
    // ToPositiveInteger inside PrepareTemporalFields before calling
    // ResolveISOMonth. Therefore the month_obj is always a positive integer.
    DCHECK(IsSmi(*month_obj) || IsHeapNumber(*month_obj));
    return Just(FastD2I(Object::NumberValue(Cast<Number>(*month_obj))));
  }
  // 4. Assert: Type(monthCode) is String.
  DCHECK(IsString(*month_code_obj));
  DirectHandle<String> month_code;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, month_code,
                                   Object::ToString(isolate, month_code_obj),
                                   Nothing<int32_t>());
  // 5. Let monthLength be the length of monthCode.
  // 6. If monthLength is not 3, throw a RangeError exception.
  if (month_code->length() != 3) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewRangeError(MessageTemplate::kPropertyValueOutOfRange,
                      factory->monthCode_string()),
        Nothing<int32_t>());
  }
  // 7. Let numberPart be the substring of monthCode from 1.
  // 8. Set numberPart to ! ToIntegerOrInfinity(numberPart).
  // 9. If numberPart < 1 or numberPart > 12, throw a RangeError exception.
  uint16_t m0 = month_code->Get(0);
  uint16_t m1 = month_code->Get(1);
  uint16_t m2 = month_code->Get(2);
  if (!((m0 == 'M') && ((m1 == '0' && '1' <= m2 && m2 <= '9') ||
                        (m1 == '1' && '0' <= m2 && m2 <= '2')))) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewRangeError(MessageTemplate::kPropertyValueOutOfRange,
                      factory->monthCode_string()),
        Nothing<int32_t>());
  }
  int32_t number_part =
      10 * static_cast<int32_t>(m1 - '0') + static_cast<int32_t>(m2 - '0');
  // 10. If month is not undefined, and month ≠ numberPart, then
  // 11. If ! SameValueNonNumeric(monthCode, ! BuildISOMonthCode(numberPart)) is
  // false, then a. Throw a RangeError exception.
  // Note: In Temporal spec, "month" in fields is always converted by
  // ToPositiveInteger inside PrepareTemporalFields before calling
  // ResolveISOMonth. Therefore the month_obj is always a positive integer.
  if (!IsUndefined(*month_obj) &&
      FastD2I(Object::NumberValue(Cast<Number>(*month_obj))) != number_part) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewRangeError(MessageTemplate::kPropertyValueOutOfRange,
                      factory->month_string()),
        Nothing<int32_t>());
  }

  // 12. Return numberPart.
  return Just(number_part);
}

// #sec-temporal-isodatefromfields
Maybe<DateRecord> ISODateFromFields(Isolate* isolate,
                                    DirectHandle<JSReceiver> fields,
                                    DirectHandle<JSReceiver> options,
                                    const char* method_name) {
  Factory* factory = isolate->factory();

  // 1. Assert: Type(fields) is Object.
  // 2. Set fields to ? PrepareTemporalFields(fields, « "day", "month",
  // "monthCode", "year" », «"year", "day"»).
  DirectHandle<FixedArray> field_names =
      DayMonthMonthCodeYearInFixedArray(isolate);
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, fields,
      PrepareTemporalFields(isolate, fields, field_names,
                            RequiredFields::kYearAndDay),
      Nothing<DateRecord>());
  // 3. Let overflow be ? ToTemporalOverflow(options).
  ShowOverflow overflow;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, overflow, ToTemporalOverflow(isolate, options, method_name),
      Nothing<DateRecord>());

  // 4. Let year be ! Get(fields, "year").
  DirectHandle<Object> year_obj =
      JSReceiver::GetProperty(isolate, fields, factory->year_string())
          .ToHandleChecked();
  // 5. Assert: Type(year) is Number.
  // Note: "year" in fields is always converted by
  // ToIntegerThrowOnInfinity inside the PrepareTemporalFields above.
  // Therefore the year_obj is always an integer.
  DCHECK(IsSmi(*year_obj) || IsHeapNumber(*year_obj));

  // 6. Let month be ? ResolveISOMonth(fields).
  int32_t month;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, month, ResolveISOMonth(isolate, fields), Nothing<DateRecord>());

  // 7. Let day be ! Get(fields, "day").
  DirectHandle<Object> day_obj =
      JSReceiver::GetProperty(isolate, fields, factory->day_string())
          .ToHandleChecked();
  // 8. Assert: Type(day) is Number.
  // Note: "day" in fields is always converted by
  // ToIntegerThrowOnInfinity inside the PrepareTemporalFields above.
  // Therefore the day_obj is always an integer.
  DCHECK(IsSmi(*day_obj) || IsHeapNumber(*day_obj));
  // 9. Return ? RegulateISODate(year, month, day, overflow).
  return RegulateISODate(
      isolate, overflow,
      {FastD2I(Object::NumberValue(Cast<Number>(*year_obj))), month,
       FastD2I(Object::NumberValue(Cast<Number>(*day_obj)))});
}

// #sec-temporal-addisodate
Maybe<DateRecord> AddISODate(Isolate* isolate, const DateRecord& date,
                             const DateDurationRecord& duration,
                             ShowOverflow overflow) {
  TEMPORAL_ENTER_FUNC();

  // 1. Assert: year, month, day, years, months, weeks, and days are integers.
  // 2. Assert: overflow is either "constrain" or "reject".
  DCHECK(overflow == ShowOverflow::kConstrain ||
         overflow == ShowOverflow::kReject);
  // 3. Let intermediate be ! BalanceISOYearMonth(year + years, month + months).
  DateRecord intermediate = date;
  intermediate.year += static_cast<int32_t>(duration.years);
  intermediate.month += static_cast<int32_t>(duration.months);
  BalanceISOYearMonth(isolate, &intermediate.year, &intermediate.month);
  // 4. Let intermediate be ? RegulateISODate(intermediate.[[Year]],
  // intermediate.[[Month]], day, overflow).
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, intermediate, RegulateISODate(isolate, overflow, intermediate),
      Nothing<DateRecord>());

  // 5. Set days to days + 7 × weeks.
  // 6. Let d be intermediate.[[Day]] + days.
  intermediate.day += duration.days + 7 * duration.weeks;
  // 7. Return BalanceISODate(intermediate.[[Year]], intermediate.[[Month]], d).
  return Just(BalanceISODate(isolate, intermediate));
}

// #sec-temporal-differenceisodate
Maybe<DateDurationRecord> DifferenceISODate(Isolate* isolate,
                                            const DateRecord& date1,
                                            const DateRecord& date2,
                                            Unit largest_unit,
                                            const char* method_name) {
  TEMPORAL_ENTER_FUNC();

  // 1. Assert: largestUnit is one of "year", "month", "week", or "day".
  DCHECK(largest_unit == Unit::kYear || largest_unit == Unit::kMonth ||
         largest_unit == Unit::kWeek || largest_unit == Unit::kDay);
  // 2. If largestUnit is "year" or "month", then
  switch (largest_unit) {
    case Unit::kYear:
    case Unit::kMonth: {
      // a. Let sign be -(! CompareISODate(y1, m1, d1, y2, m2, d2)).
      int32_t sign = -CompareISODate(date1, date2);
      // b. If sign is 0, return ! CreateDateDurationRecord(0, 0, 0, 0).
      if (sign == 0) {
        return DateDurationRecord::Create(isolate, 0, 0, 0, 0);
      }

      // c. Let start be the new Record { [[Year]]: y1, [[Month]]: m1, [[Day]]:
      // d1
      // }.
      DateRecord start = date1;
      // d. Let end be the new Record { [[Year]]: y2, [[Month]]: m2, [[Day]]:
      // d2 }.
      DateRecord end = date2;
      // e. Let years be end.[[Year]] − start.[[Year]].
      double years = end.year - start.year;
      // f. Let mid be ! AddISODate(y1, m1, d1, years, 0, 0, 0, "constrain").
      DateRecord mid;
      MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, mid,
          AddISODate(isolate, date1, {years, 0, 0, 0},
                     ShowOverflow::kConstrain),
          Nothing<DateDurationRecord>());

      // g. Let midSign be -(! CompareISODate(mid.[[Year]], mid.[[Month]],
      // mid.[[Day]], y2, m2, d2)).
      int32_t mid_sign = -CompareISODate(mid, date2);

      // h. If midSign is 0, then
      if (mid_sign == 0) {
        // i. If largestUnit is "year", return ! CreateDateDurationRecord(years,
        // 0, 0, 0).
        if (largest_unit == Unit::kYear) {
          return DateDurationRecord::Create(isolate, years, 0, 0, 0);
        }
        // ii. Return ! CreateDateDurationRecord(0, years × 12, 0, 0).
        return DateDurationRecord::Create(isolate, 0, years * 12, 0, 0);
      }
      // i. Let months be end.[[Month]] − start.[[Month]].
      double months = end.month - start.month;
      // j. If midSign is not equal to sign, then
      if (mid_sign != sign) {
        // i. Set years to years - sign.
        years -= sign;
        // ii. Set months to months + sign × 12.
        months += sign * 12;
      }
      // k. Set mid be ! AddISODate(y1, m1, d1, years, months, 0, 0,
      // "constrain").
      MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, mid,
          AddISODate(isolate, date1, {years, months, 0, 0},
                     ShowOverflow::kConstrain),
          Nothing<DateDurationRecord>());
      // l. Let midSign be -(! CompareISODate(mid.[[Year]], mid.[[Month]],
      // mid.[[Day]], y2, m2, d2)).
      mid_sign = -CompareISODate(mid, date2);
      // m. If midSign is 0, then
      if (mid_sign == 0) {
        // 1. i. If largestUnit is "year", return !
        // CreateDateDurationRecord(years, months, 0, 0).
        if (largest_unit == Unit::kYear) {
          return DateDurationRecord::Create(isolate, years, months, 0, 0);
        }
        // ii. Return ! CreateDateDurationRecord(0, months + years × 12, 0, 0).
        return DateDurationRecord::Create(isolate, 0, months + years * 12, 0,
                                          0);
      }
      // n. If midSign is not equal to sign, then
      if (mid_sign != sign) {
        // i. Set months to months - sign.
        months -= sign;
        // ii. If months is equal to -sign, then
        if (months == -sign) {
          // 1. Set years to years - sign.
          years -= sign;
          // 2. Set months to 11 × sign.
          months = 11 * sign;
        }
        // iii. Set mid be ! AddISODate(y1, m1, d1, years, months, 0, 0,
        // "constrain").
        MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
            isolate, mid,
            AddISODate(isolate, date1, {years, months, 0, 0},
                       ShowOverflow::kConstrain),
            Nothing<DateDurationRecord>());
        // iv. Let midSign be -(! CompareISODate(mid.[[Year]], mid.[[Month]],
        // mid.[[Day]], y2, m2, d2)).
        mid_sign = -CompareISODate(mid, date2);
      }
      // o. Let days be 0.
      double days = 0;
      // p. If mid.[[Month]] = end.[[Month]], then
      if (mid.month == end.month) {
        // i. Assert: mid.[[Year]] = end.[[Year]].
        DCHECK_EQ(mid.year, end.year);
        // ii. Set days to end.[[Day]] - mid.[[Day]].
        days = end.day - mid.day;
      } else if (sign < 0) {
        // q. Else if sign < 0, set days to -mid.[[Day]] - (!
        // ISODaysInMonth(end.[[Year]], end.[[Month]]) - end.[[Day]]).
        days =
            -mid.day - (ISODaysInMonth(isolate, end.year, end.month) - end.day);
      } else {
        // r. Else, set days to end.[[Day]] + (! ISODaysInMonth(mid.[[Year]],
        // mid.[[Month]]) - mid.[[Day]]).
        days =
            end.day + (ISODaysInMonth(isolate, mid.year, mid.month) - mid.day);
      }
      // s. If largestUnit is "month", then
      if (largest_unit == Unit::kMonth) {
        // i. Set months to months + years × 12.
        months += years * 12;
        // ii. Set years to 0.
        years = 0;
      }
      // t. Return ! CreateDateDurationRecord(years, months, 0, days).
      return DateDurationRecord::Create(isolate, years, months, 0, days);
    }
      // 3. If largestUnit is "day" or "week", then
    case Unit::kDay:
    case Unit::kWeek: {
      DateRecord smaller, greater;
      // a. If ! CompareISODate(y1, m1, d1, y2, m2, d2) < 0, then
      int32_t sign;
      if (CompareISODate(date1, date2) < 0) {
        // i. Let smaller be the Record { [[Year]]: y1, [[Month]]: m1, [[Day]]:
        // d1
        // }.
        smaller = date1;
        // ii. Let greater be the Record { [[Year]]: y2, [[Month]]: m2, [[Day]]:
        // d2
        // }.
        greater = date2;
        // iii. Let sign be 1.
        sign = 1;
      } else {
        // b. Else,
        // i. Let smaller be the new Record { [[Year]]: y2, [[Month]]: m2,
        // [[Day]]: d2 }.
        smaller = date2;
        // ii. Let greater be the new Record { [[Year]]: y1, [[Month]]: m1,
        // [[Day]]: d1 }.
        greater = date1;
        // iii. Let sign be −1.
        sign = -1;
      }
      // c. Let days be ! ToISODayOfYear(greater.[[Year]], greater.[[Month]],
      // greater.[[Day]]) − ! ToISODayOfYear(smaller.[[Year]],
      // smaller.[[Month]], smaller.[[Day]]).
      int32_t days =
          ToISODayOfYear(isolate, greater) - ToISODayOfYear(isolate, smaller);
      // d. Let year be smaller.[[Year]].
      // e. Repeat, while year < greater.[[Year]],
      for (int32_t year = smaller.year; year < greater.year; year++) {
        // i. Set days to days + ! ISODaysInYear(year).
        // ii. Set year to year + 1.
        days += ISODaysInYear(isolate, year);
      }
      // f. Let weeks be 0.
      int32_t weeks = 0;
      // g. If largestUnit is "week", then
      if (largest_unit == Unit::kWeek) {
        // i. Set weeks to floor(days / 7).
        weeks = days / 7;
        // ii. Set days to days mod 7.
        days = days % 7;
      }
      // h. Return ! CreateDateDurationRecord(0, 0, weeks × sign, days × sign).
      return DateDurationRecord::Create(isolate, 0, 0, weeks * sign,
                                        days * sign);
    }
    default:
      UNREACHABLE();
  }
}

// #sec-temporal-isoyearmonthfromfields
Maybe<DateRecord> ISOYearMonthFromFields(Isolate* isolate,
                                         DirectHandle<JSReceiver> fields,
                                         DirectHandle<JSReceiver> options,
                                         const char* method_name) {
  Factory* factory = isolate->factory();
  // 1. Assert: Type(fields) is Object.
  // 2. Set fields to ? PrepareTemporalFields(fields, « "month", "monthCode",
  // "year" », «»).
  DirectHandle<FixedArray> field_names = factory->NewFixedArray(3);
  field_names->set(0, ReadOnlyRoots(isolate).month_string());
  field_names->set(1, ReadOnlyRoots(isolate).monthCode_string());
  field_names->set(2, ReadOnlyRoots(isolate).year_string());
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, fields,
      PrepareTemporalFields(isolate, fields, field_names,
                            RequiredFields::kNone),
      Nothing<DateRecord>());
  // 3. Let overflow be ? ToTemporalOverflow(options).
  ShowOverflow overflow;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, overflow, ToTemporalOverflow(isolate, options, method_name),
      Nothing<DateRecord>());

  // 4. Let year be ! Get(fields, "year").
  DirectHandle<Object> year_obj =
      JSReceiver::GetProperty(isolate, fields, factory->year_string())
          .ToHandleChecked();
  // 5. If year is undefined, throw a TypeError exception.
  if (IsUndefined(*year_obj, isolate)) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR(),
                                 Nothing<DateRecord>());
  }
  DateRecord result;
  result.year = FastD2I(floor(Object::NumberValue(Cast<Number>(*year_obj))));
  // 6. Let month be ? ResolveISOMonth(fields).
  int32_t month;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, month, ResolveISOMonth(isolate, fields), Nothing<DateRecord>());
  // 7. Let result be ? RegulateISOYearMonth(year, month, overflow).
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result.month, RegulateISOYearMonth(isolate, overflow, month),
      Nothing<DateRecord>());
  // 8. Return the new Record { [[Year]]: result.[[Year]], [[Month]]:
  // result.[[Month]], [[ReferenceISODay]]: 1 }.
  result.day = 1;
  return Just(result);
}

// #sec-temporal-toisoweekofyear
int32_t ToISOWeekOfYear(Isolate* isolate, const DateRecord& date) {
  TEMPORAL_ENTER_FUNC();
  // 1. Assert: IsValidISODate(year, month, day) is *true*.
  DCHECK(IsValidISODate(isolate, date));

  // 2. Let wednesday be 3.
  constexpr int32_t kWednesday = 3;
  // 3. Let thursday_ be 4.
  constexpr int32_t kThursday = 4;
  // 4. Let friday be 5.
  constexpr int32_t kFriday = 5;
  // 5. Let saturday be 6.
  constexpr int32_t kSaturday = 6;
  // 6. Let daysInWeek be 7.
  constexpr int32_t kDaysInWeek = 7;
  // 7. Let maxWeekNumber be 53.
  constexpr int32_t kMaxWeekNumber = 53;
  // 8. Let dayOfYear be ToISODayOfYear(year, month, day).
  int32_t day_of_year = ToISODayOfYear(isolate, date);
  // 9. Let dayOfWeek be ToISODayOfWeek(year, month, day).
  int32_t day_of_week = ToISODayOfWeek(isolate, date);
  // 10. Let week be floor((dayOfYear + daysInWeek - dayOfWeek + wednesday ) /
  // daysInWeek).
  int32_t week =
      (day_of_year + kDaysInWeek - day_of_week + kWednesday) / kDaysInWeek;
  // 11. If week < 1, then
  if (week < 1) {
    // a. NOTE: This is the last week of the previous year.
    // b. Let dayOfJan1st be ToISODayOfWeek(year, 1, 1).
    int32_t day_of_jan_1st = ToISODayOfWeek(isolate, {date.year, 1, 1});
    // c. If dayOfJan1st is friday, then
    if (day_of_jan_1st == kFriday) {
      // a. Return maxWeekNumber.
      return kMaxWeekNumber;
    }
    // d. If dayOfJan1st is saturday, and InLeapYear(TimeFromYear(𝔽(year - 1)))
    // is *1*<sub>𝔽</sub>, then
    if (day_of_jan_1st == kSaturday && IsISOLeapYear(isolate, date.year - 1)) {
      // i. Return maxWeekNumber.
      return kMaxWeekNumber;
    }
    // e. Return maxWeekNumber - 1.
    return kMaxWeekNumber - 1;
  }
  // 12. If week is maxWeekNumber, then
  if (week == kMaxWeekNumber) {
    // a. Let daysInYear be DaysInYear(𝔽(year)).
    int32_t days_in_year = ISODaysInYear(isolate, date.year);
    // b. Let daysLaterInYear be daysInYear - dayOfYear.
    int32_t days_later_in_year = days_in_year - day_of_year;
    // c. Let daysAfterThursday be thursday - dayOfWeek.
    int32_t days_after_thursday = kThursday - day_of_week;
    // d. If daysLaterInYear &lt; daysAfterThursday, then
    if (days_later_in_year < days_after_thursday) {
      // 1. Return 1.
      return 1;
    }
  }
  // 13. Return week.
  return week;
}

}  // namespace

// #sec-temporal.calendar.prototype.dateadd
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalCalendar::DateAdd(
    Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
    DirectHandle<Object> date_obj, DirectHandle<Object> duration_obj,
    DirectHandle<Object> options_obj) {
  const char* method_name = "Temporal.Calendar.prototype.dateAdd";
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  // 4. Set date to ? ToTemporalDate(date).
  DirectHandle<JSTemporalPlainDate> date;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, date,
                             ToTemporalDate(isolate, date_obj, method_name));

  // 5. Set duration to ? ToTemporalDuration(duration).
  DirectHandle<JSTemporalDuration> duration;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, duration,
      temporal::ToTemporalDuration(isolate, duration_obj, method_name));

  // 6. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));

  // 7. Let overflow be ? ToTemporalOverflow(options).
  ShowOverflow overflow;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, overflow, ToTemporalOverflow(isolate, options, method_name),
      DirectHandle<JSTemporalPlainDate>());

  // 8. Let balanceResult be ? BalanceDuration(duration.[[Days]],
  // duration.[[Hours]], duration.[[Minutes]], duration.[[Seconds]],
  // duration.[[Milliseconds]], duration.[[Microseconds]],
  // duration.[[Nanoseconds]], "day").
  TimeDurationRecord balance_result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, balance_result,
      BalanceDuration(isolate, Unit::kDay,
                      {Object::NumberValue(duration->days()),
                       Object::NumberValue(duration->hours()),
                       Object::NumberValue(duration->minutes()),
                       Object::NumberValue(duration->seconds()),
                       Object::NumberValue(duration->milliseconds()),
                       Object::NumberValue(duration->microseconds()),
                       Object::NumberValue(duration->nanoseconds())},
                      method_name),
      DirectHandle<JSTemporalPlainDate>());

  DateRecord result;
  // If calendar.[[Identifier]] is "iso8601", then
  if (calendar->calendar_index() == 0) {
    // 9. Let result be ? AddISODate(date.[[ISOYear]], date.[[ISOMonth]],
    // date.[[ISODay]], duration.[[Years]], duration.[[Months]],
    // duration.[[Weeks]], balanceResult.[[Days]], overflow).
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, result,
        AddISODate(
            isolate, {date->iso_year(), date->iso_month(), date->iso_day()},
            {Object::NumberValue(duration->years()),
             Object::NumberValue(duration->months()),
             Object::NumberValue(duration->weeks()), balance_result.days},
            overflow),
        DirectHandle<JSTemporalPlainDate>());
  } else {
#ifdef V8_INTL_SUPPORT
    // TODO(ftang) add code for other calendar.
    UNIMPLEMENTED();
#else   // V8_INTL_SUPPORT
    UNREACHABLE();
#endif  // V8_INTL_SUPPORT
  }
  // 10. Return ? CreateTemporalDate(result.[[Year]], result.[[Month]],
  // result.[[Day]], calendar).
  return CreateTemporalDate(isolate, result, calendar);
}

// #sec-temporal.calendar.prototype.daysinyear
MaybeDirectHandle<Smi> JSTemporalCalendar::DaysInYear(
    Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
    DirectHandle<Object> temporal_date_like) {
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  // 4. If Type(temporalDateLike) is not Object or temporalDateLike does not
  // have an [[InitializedTemporalDate]], [[InitializedTemporalDateTime]] or
  // [[InitializedTemporalYearMonth]] internal slot, then
  if (!IsPlainDatePlainDateTimeOrPlainYearMonth(temporal_date_like)) {
    // a. Set temporalDateLike to ? ToTemporalDate(temporalDateLike).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, temporal_date_like,
        ToTemporalDate(isolate, temporal_date_like,
                       "Temporal.Calendar.prototype.daysInYear"));
  }

  // a. Let daysInYear be ! ISODaysInYear(temporalDateLike.[[ISOYear]]).
  int32_t year;
  if (IsJSTemporalPlainDate(*temporal_date_like)) {
    year = Cast<JSTemporalPlainDate>(temporal_date_like)->iso_year();
  } else if (IsJSTemporalPlainDateTime(*temporal_date_like)) {
    year = Cast<JSTemporalPlainDateTime>(temporal_date_like)->iso_year();
  } else {
    DCHECK(IsJSTemporalPlainYearMonth(*temporal_date_like));
    year = Cast<JSTemporalPlainYearMonth>(temporal_date_like)->iso_year();
  }
  int32_t days_in_year = ISODaysInYear(isolate, year);
  // 6. Return 𝔽(daysInYear).
  return direct_handle(Smi::FromInt(days_in_year), isolate);
}

// #sec-temporal.calendar.prototype.daysinmonth
MaybeDirectHandle<Smi> JSTemporalCalendar::DaysInMonth(
    Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
    DirectHandle<Object> temporal_date_like) {
  // 1 Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  // 4. If Type(temporalDateLike) is not Object or temporalDateLike does not
  // have an [[InitializedTemporalDate]], [[InitializedTemporalDateTime]] or
  // [[InitializedTemporalYearMonth]] internal slot, then
  if (!IsPlainDatePlainDateTimeOrPlainYearMonth(temporal_date_like)) {
    // a. Set temporalDateLike to ? ToTemporalDate(temporalDateLike).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, temporal_date_like,
        ToTemporalDate(isolate, temporal_date_like,
                       "Temporal.Calendar.prototype.daysInMonth"));
  }

  // 5. Return 𝔽(! ISODaysInMonth(temporalDateLike.[[ISOYear]],
  // temporalDateLike.[[ISOMonth]])).
  int32_t year;
  int32_t month;
  if (IsJSTemporalPlainDate(*temporal_date_like)) {
    year = Cast<JSTemporalPlainDate>(temporal_date_like)->iso_year();
    month = Cast<JSTemporalPlainDate>(temporal_date_like)->iso_month();
  } else if (IsJSTemporalPlainDateTime(*temporal_date_like)) {
    year = Cast<JSTemporalPlainDateTime>(temporal_date_like)->iso_year();
    month = Cast<JSTemporalPlainDateTime>(temporal_date_like)->iso_month();
  } else {
    DCHECK(IsJSTemporalPlainYearMonth(*temporal_date_like));
    year = Cast<JSTemporalPlainYearMonth>(temporal_date_like)->iso_year();
    month = Cast<JSTemporalPlainYearMonth>(temporal_date_like)->iso_month();
  }
  return direct_handle(Smi::FromInt(ISODaysInMonth(isolate, year, month)),
                       isolate);
}

// #sec-temporal.calendar.prototype.year
MaybeDirectHandle<Smi> JSTemporalCalendar::Year(
    Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
    DirectHandle<Object> temporal_date_like) {
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  // 4. If Type(temporalDateLike) is not Object or temporalDateLike does not
  // have an [[InitializedTemporalDate]], [[InitializedTemporalDateTime]],
  // or [[InitializedTemporalYearMonth]]
  // internal slot, then
  if (!IsPlainDatePlainDateTimeOrPlainYearMonth(temporal_date_like)) {
    // a. Set temporalDateLike to ? ToTemporalDate(temporalDateLike).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, temporal_date_like,
        ToTemporalDate(isolate, temporal_date_like,
                       "Temporal.Calendar.prototype.year"));
  }

  // a. Let year be ! ISOYear(temporalDateLike).
  int32_t year;
  if (IsJSTemporalPlainDate(*temporal_date_like)) {
    year = Cast<JSTemporalPlainDate>(temporal_date_like)->iso_year();
  } else if (IsJSTemporalPlainDateTime(*temporal_date_like)) {
    year = Cast<JSTemporalPlainDateTime>(temporal_date_like)->iso_year();
  } else {
    DCHECK(IsJSTemporalPlainYearMonth(*temporal_date_like));
    year = Cast<JSTemporalPlainYearMonth>(temporal_date_like)->iso_year();
  }

  // 6. Return 𝔽(year).
  return direct_handle(Smi::FromInt(year), isolate);
}

// #sec-temporal.calendar.prototype.dayofyear
MaybeDirectHandle<Smi> JSTemporalCalendar::DayOfYear(
    Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
    DirectHandle<Object> temporal_date_like) {
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  // 4. Let temporalDate be ? ToTemporalDate(temporalDateLike).
  DirectHandle<JSTemporalPlainDate> temporal_date;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temporal_date,
      ToTemporalDate(isolate, temporal_date_like,
                     "Temporal.Calendar.prototype.dayOfYear"));
  // a. Let value be ! ToISODayOfYear(temporalDate.[[ISOYear]],
  // temporalDate.[[ISOMonth]], temporalDate.[[ISODay]]).
  int32_t value = ToISODayOfYear(
      isolate, {temporal_date->iso_year(), temporal_date->iso_month(),
                temporal_date->iso_day()});
  return direct_handle(Smi::FromInt(value), isolate);
}

// #sec-temporal.calendar.prototype.dayofweek
MaybeDirectHandle<Smi> JSTemporalCalendar::DayOfWeek(
    Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
    DirectHandle<Object> temporal_date_like) {
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  // 4. Let temporalDate be ? ToTemporalDate(temporalDateLike).
  DirectHandle<JSTemporalPlainDate> temporal_date;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temporal_date,
      ToTemporalDate(isolate, temporal_date_like,
                     "Temporal.Calendar.prototype.dayOfWeek"));
  // a. Let value be ! ToISODayOfWeek(temporalDate.[[ISOYear]],
  // temporalDate.[[ISOMonth]], temporalDate.[[ISODay]]).
  int32_t value = ToISODayOfWeek(
      isolate, {temporal_date->iso_year(), temporal_date->iso_month(),
                temporal_date->iso_day()});
  return direct_handle(Smi::FromInt(value), isolate);
}

// #sec-temporal.calendar.prototype.monthsinyear
MaybeDirectHandle<Smi> JSTemporalCalendar::MonthsInYear(
    Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
    DirectHandle<Object> temporal_date_like) {
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  // 4. If Type(temporalDateLike) is not Object or temporalDateLike does not
  // have an [[InitializedTemporalDate]], [[InitializedTemporalDateTime]], or
  // [[InitializedTemporalYearMonth]] internal slot, then
  if (!IsPlainDatePlainDateTimeOrPlainYearMonth(temporal_date_like)) {
    // a. Set temporalDateLike to ? ToTemporalDate(temporalDateLike).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, temporal_date_like,
        ToTemporalDate(isolate, temporal_date_like,
                       "Temporal.Calendar.prototype.monthsInYear"));
  }

  // a. a. Let monthsInYear be 12.
  int32_t months_in_year = 12;
  // 6. Return 𝔽(monthsInYear).
  return direct_handle(Smi::FromInt(months_in_year), isolate);
}

// #sec-temporal.calendar.prototype.inleapyear
MaybeDirectHandle<Oddball> JSTemporalCalendar::InLeapYear(
    Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
    DirectHandle<Object> temporal_date_like) {
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  // 4. If Type(temporalDateLike) is not Object or temporalDateLike does not
  // have an [[InitializedTemporalDate]], [[InitializedTemporalDateTime]], or
  // [[InitializedTemporalYearMonth]] internal slot, then
  if (!IsPlainDatePlainDateTimeOrPlainYearMonth(temporal_date_like)) {
    // a. Set temporalDateLike to ? ToTemporalDate(temporalDateLike).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, temporal_date_like,
        ToTemporalDate(isolate, temporal_date_like,
                       "Temporal.Calendar.prototype.inLeapYear"));
  }

  // a. Let inLeapYear be ! IsISOLeapYear(temporalDateLike.[[ISOYear]]).
  int32_t year;
  if (IsJSTemporalPlainDate(*temporal_date_like)) {
    year = Cast<JSTemporalPlainDate>(temporal_date_like)->iso_year();
  } else if (IsJSTemporalPlainDateTime(*temporal_date_like)) {
    year = Cast<JSTemporalPlainDateTime>(temporal_date_like)->iso_year();
  } else {
    DCHECK(IsJSTemporalPlainYearMonth(*temporal_date_like));
    year = Cast<JSTemporalPlainYearMonth>(temporal_date_like)->iso_year();
  }
  return isolate->factory()->ToBoolean(IsISOLeapYear(isolate, year));
}

// #sec-temporal.calendar.prototype.daysinweek
MaybeDirectHandle<Smi> JSTemporalCalendar::DaysInWeek(
    Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
    DirectHandle<Object> temporal_date_like) {
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  // 4. Perform ? ToTemporalDate(temporalDateLike).
  DirectHandle<JSTemporalPlainDate> date;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, date,
      ToTemporalDate(isolate, temporal_date_like,
                     "Temporal.Calendar.prototype.daysInWeek"));
  // 5. Return 7𝔽.
  return direct_handle(Smi::FromInt(7), isolate);
}

// #sec-temporal.calendar.prototype.datefromfields
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalCalendar::DateFromFields(
    Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
    DirectHandle<Object> fields_obj, DirectHandle<Object> options_obj) {
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  // 4. If Type(fields) is not Object, throw a TypeError exception.
  const char* method_name = "Temporal.Calendar.prototype.dateFromFields";
  if (!IsJSReceiver(*fields_obj)) {
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kCalledOnNonObject,
                                 isolate->factory()->NewStringFromAsciiChecked(
                                     method_name)));
  }
  DirectHandle<JSReceiver> fields = Cast<JSReceiver>(fields_obj);

  // 5. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));
  if (calendar->calendar_index() == 0) {
    // 6. Let result be ? ISODateFromFields(fields, options).
    DateRecord result;
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, result,
        ISODateFromFields(isolate, fields, options, method_name),
        DirectHandle<JSTemporalPlainDate>());
    // 7. Return ? CreateTemporalDate(result.[[Year]], result.[[Month]],
    // result.[[Day]], calendar).
    return CreateTemporalDate(isolate, result, calendar);
  }
  // TODO(ftang) add intl implementation inside #ifdef V8_INTL_SUPPORT
  UNREACHABLE();
}

// #sec-temporal.calendar.prototype.mergefields
MaybeDirectHandle<JSReceiver> JSTemporalCalendar::MergeFields(
    Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
    DirectHandle<Object> fields_obj,
    DirectHandle<Object> additional_fields_obj) {
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  // 4. Set fields to ? ToObject(fields).
  DirectHandle<JSReceiver> fields;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, fields,
                             Object::ToObject(isolate, fields_obj));

  // 5. Set additionalFields to ? ToObject(additionalFields).
  DirectHandle<JSReceiver> additional_fields;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, additional_fields,
                             Object::ToObject(isolate, additional_fields_obj));
  // 5. If calendar.[[Identifier]] is "iso8601", then
  if (calendar->calendar_index() == 0) {
    // a. Return ? DefaultMergeFields(fields, additionalFields).
    return DefaultMergeFields(isolate, fields, additional_fields);
  }
#ifdef V8_INTL_SUPPORT
  // TODO(ftang) add Intl code.
#endif  // V8_INTL_SUPPORT
  UNREACHABLE();
}

// #sec-temporal.calendar.prototype.dateuntil
MaybeDirectHandle<JSTemporalDuration> JSTemporalCalendar::DateUntil(
    Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
    DirectHandle<Object> one_obj, DirectHandle<Object> two_obj,
    DirectHandle<Object> options_obj) {
  const char* method_name = "Temporal.Calendar.prototype.dateUntil";
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  // 4. Set one to ? ToTemporalDate(one).
  DirectHandle<JSTemporalPlainDate> one;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, one,
                             ToTemporalDate(isolate, one_obj, method_name));
  // 5. Set two to ? ToTemporalDate(two).
  DirectHandle<JSTemporalPlainDate> two;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, two,
                             ToTemporalDate(isolate, two_obj, method_name));
  // 6. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));

  // 7. Let largestUnit be ? GetTemporalUnit(options, "largestUnit", date,
  // "auto").
  Unit largest_unit;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, largest_unit,
      GetTemporalUnit(isolate, options, "largestUnit", UnitGroup::kDate,
                      Unit::kAuto, false, method_name),
      DirectHandle<JSTemporalDuration>());
  // 8. If largestUnit is "auto", set largestUnit to "day".
  if (largest_unit == Unit::kAuto) largest_unit = Unit::kDay;

  // 9. Let result be ! DifferenceISODate(one.[[ISOYear]], one.[[ISOMonth]],
  // one.[[ISODay]], two.[[ISOYear]], two.[[ISOMonth]], two.[[ISODay]],
  // largestUnit).
  DateDurationRecord result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result,
      DifferenceISODate(isolate,
                        {one->iso_year(), one->iso_month(), one->iso_day()},
                        {two->iso_year(), two->iso_month(), two->iso_day()},
                        largest_unit, method_name),
      DirectHandle<JSTemporalDuration>());

  // 10. Return ! CreateTemporalDuration(result.[[Years]], result.[[Months]],
  // result.[[Weeks]], result.[[Days]], 0, 0, 0, 0, 0, 0).
  return CreateTemporalDuration(isolate, {result.years,
                                          result.months,
                                          result.weeks,
                                          {result.days, 0, 0, 0, 0, 0, 0}})
      .ToHandleChecked();
}

// #sec-temporal.calendar.prototype.day
MaybeDirectHandle<Smi> JSTemporalCalendar::Day(
    Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
    DirectHandle<Object> temporal_date_like) {
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  // 4. If Type(temporalDateLike) is not Object or temporalDateLike does not
  // have an [[InitializedTemporalDate]] or [[InitializedTemporalMonthDay]]
  // internal slot, then
  if (!(IsJSTemporalPlainDate(*temporal_date_like) ||
        IsJSTemporalPlainDateTime(*temporal_date_like) ||
        IsJSTemporalPlainMonthDay(*temporal_date_like))) {
    // a. Set temporalDateLike to ? ToTemporalDate(temporalDateLike).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, temporal_date_like,
        ToTemporalDate(isolate, temporal_date_like,
                       "Temporal.Calendar.prototype.day"));
  }

  // 5. Let day be ! ISODay(temporalDateLike).
  int32_t day;
  if (IsJSTemporalPlainDate(*temporal_date_like)) {
    day = Cast<JSTemporalPlainDate>(temporal_date_like)->iso_day();
  } else if (IsJSTemporalPlainDateTime(*temporal_date_like)) {
    day = Cast<JSTemporalPlainDateTime>(temporal_date_like)->iso_day();
  } else {
    DCHECK(IsJSTemporalPlainMonthDay(*temporal_date_like));
    day = Cast<JSTemporalPlainMonthDay>(temporal_date_like)->iso_day();
  }

  // 6. Return 𝔽(day).
  return direct_handle(Smi::FromInt(day), isolate);
}

// #sec-temporal.calendar.prototype.monthcode
MaybeDirectHandle<String> JSTemporalCalendar::MonthCode(
    Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
    DirectHandle<Object> temporal_date_like) {
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  // 4. If Type(temporalDateLike) is not Object or temporalDateLike does not
  // have an [[InitializedTemporalDate]], [[InitializedTemporalDateTime]],
  // [[InitializedTemporalMonthDay]], or
  // [[InitializedTemporalYearMonth]] internal slot, then
  if (!(IsPlainDatePlainDateTimeOrPlainYearMonth(temporal_date_like) ||
        IsJSTemporalPlainMonthDay(*temporal_date_like))) {
    // a. Set temporalDateLike to ? ToTemporalDate(temporalDateLike).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, temporal_date_like,
        ToTemporalDate(isolate, temporal_date_like,
                       "Temporal.Calendar.prototype.monthCode"));
  }

  // 5. Return ! ISOMonthCode(temporalDateLike).
  int32_t month;
  if (IsJSTemporalPlainDate(*temporal_date_like)) {
    month = Cast<JSTemporalPlainDate>(temporal_date_like)->iso_month();
  } else if (IsJSTemporalPlainDateTime(*temporal_date_like)) {
    month = Cast<JSTemporalPlainDateTime>(temporal_date_like)->iso_month();
  } else if (IsJSTemporalPlainMonthDay(*temporal_date_like)) {
    month = Cast<JSTemporalPlainMonthDay>(temporal_date_like)->iso_month();
  } else {
    DCHECK(IsJSTemporalPlainYearMonth(*temporal_date_like));
    month = Cast<JSTemporalPlainYearMonth>(temporal_date_like)->iso_month();
  }
  IncrementalStringBuilder builder(isolate);
  builder.AppendCharacter('M');
  if (month < 10) {
    builder.AppendCharacter('0');
  }
  builder.AppendInt(month);

  return builder.Finish();
}

// #sec-temporal.calendar.prototype.month
MaybeDirectHandle<Smi> JSTemporalCalendar::Month(
    Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
    DirectHandle<Object> temporal_date_like) {
  // 4. If Type(temporalDateLike) is Object and temporalDateLike has an
  // [[InitializedTemporalMonthDay]] internal slot, then
  if (IsJSTemporalPlainMonthDay(*temporal_date_like)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }
  // 5. If Type(temporalDateLike) is not Object or temporalDateLike does not
  // have an [[InitializedTemporalDate]], [[InitializedTemporalDateTime]],
  // or [[InitializedTemporalYearMonth]]
  // internal slot, then
  if (!IsPlainDatePlainDateTimeOrPlainYearMonth(temporal_date_like)) {
    // a. Set temporalDateLike to ? ToTemporalDate(temporalDateLike).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, temporal_date_like,
        ToTemporalDate(isolate, temporal_date_like,
                       "Temporal.Calendar.prototype.month"));
  }

  // 6. Return ! ISOMonth(temporalDateLike).
  int32_t month;
  if (IsJSTemporalPlainDate(*temporal_date_like)) {
    month = Cast<JSTemporalPlainDate>(temporal_date_like)->iso_month();
  } else if (IsJSTemporalPlainDateTime(*temporal_date_like)) {
    month = Cast<JSTemporalPlainDateTime>(temporal_date_like)->iso_month();
  } else {
    DCHECK(IsJSTemporalPlainYearMonth(*temporal_date_like));
    month = Cast<JSTemporalPlainYearMonth>(temporal_date_like)->iso_month();
  }

  // 7. Return 𝔽(month).
  return direct_handle(Smi::FromInt(month), isolate);
}

// #sec-temporal.calendar.prototype.monthdayfromfields
MaybeDirectHandle<JSTemporalPlainMonthDay>
JSTemporalCalendar::MonthDayFromFields(
    Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
    DirectHandle<Object> fields_obj, DirectHandle<Object> options_obj) {
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  const char* method_name = "Temporal.Calendar.prototype.monthDayFromFields";
  // 4. If Type(fields) is not Object, throw a TypeError exception.
  if (!IsJSReceiver(*fields_obj)) {
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kCalledOnNonObject,
                                 isolate->factory()->NewStringFromAsciiChecked(
                                     method_name)));
  }
  DirectHandle<JSReceiver> fields = Cast<JSReceiver>(fields_obj);
  // 5. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));
  // 6. Let result be ? ISOMonthDayFromFields(fields, options).
  if (calendar->calendar_index() == 0) {
    DateRecord result;
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, result,
        ISOMonthDayFromFields(isolate, fields, options, method_name),
        DirectHandle<JSTemporalPlainMonthDay>());
    // 7. Return ? CreateTemporalMonthDay(result.[[Month]], result.[[Day]],
    // calendar, result.[[ReferenceISOYear]]).
    return CreateTemporalMonthDay(isolate, result.month, result.day, calendar,
                                  result.year);
  }
  // TODO(ftang) add intl code inside #ifdef V8_INTL_SUPPORT
  UNREACHABLE();
}

// #sec-temporal.calendar.prototype.yearmonthfromfields
MaybeDirectHandle<JSTemporalPlainYearMonth>
JSTemporalCalendar::YearMonthFromFields(
    Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
    DirectHandle<Object> fields_obj, DirectHandle<Object> options_obj) {
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  const char* method_name = "Temporal.Calendar.prototype.yearMonthFromFields";
  // 4. If Type(fields) is not Object, throw a TypeError exception.
  if (!IsJSReceiver(*fields_obj)) {
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kCalledOnNonObject,
                                 isolate->factory()->NewStringFromAsciiChecked(
                                     method_name)));
  }
  DirectHandle<JSReceiver> fields = Cast<JSReceiver>(fields_obj);
  // 5. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));
  // 6. Let result be ? ISOYearMonthFromFields(fields, options).
  if (calendar->calendar_index() == 0) {
    DateRecord result;
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, result,
        ISOYearMonthFromFields(isolate, fields, options, method_name),
        DirectHandle<JSTemporalPlainYearMonth>());
    // 7. Return ? CreateTemporalYearMonth(result.[[Year]], result.[[Month]],
    // calendar, result.[[ReferenceISODay]]).
    return CreateTemporalYearMonth(isolate, result.year, result.month, calendar,
                                   result.day);
  }
  // TODO(ftang) add intl code inside #ifdef V8_INTL_SUPPORT
  UNREACHABLE();
}

#ifdef V8_INTL_SUPPORT
// #sup-temporal.calendar.prototype.era
MaybeDirectHandle<Object> JSTemporalCalendar::Era(
    Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
    DirectHandle<Object> temporal_date_like) {
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. If Type(temporalDateLike) is not Object or temporalDateLike does not
  // have an [[InitializedTemporalDate]], [[InitializedTemporalDateTime]],
  // or [[InitializedTemporalYearMonth]]
  // internal slot, then
  if (!IsPlainDatePlainDateTimeOrPlainYearMonth(temporal_date_like)) {
    // a. Set temporalDateLike to ? ToTemporalDate(temporalDateLike).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, temporal_date_like,
        ToTemporalDate(isolate, temporal_date_like,
                       "Temporal.Calendar.prototype.era"));
  }
  // 4. If calendar.[[Identifier]] is "iso8601", then
  if (calendar->calendar_index() == 0) {
    // a. Return undefined.
    return isolate->factory()->undefined_value();
  }
  UNIMPLEMENTED();
  // TODO(ftang) implement other calendars
  // 5. Return ! CalendarDateEra(calendar.[[Identifier]], temporalDateLike).
}

// #sup-temporal.calendar.prototype.erayear
MaybeDirectHandle<Object> JSTemporalCalendar::EraYear(
    Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
    DirectHandle<Object> temporal_date_like) {
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. If Type(temporalDateLike) is not Object or temporalDateLike does not
  // have an [[InitializedTemporalDate]], [[InitializedTemporalDateTime]],
  // or [[InitializedTemporalYearMonth]]
  // internal slot, then
  if (!IsPlainDatePlainDateTimeOrPlainYearMonth(temporal_date_like)) {
    // a. Set temporalDateLike to ? ToTemporalDate(temporalDateLike).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, temporal_date_like,
        ToTemporalDate(isolate, temporal_date_like,
                       "Temporal.Calendar.prototype.eraYear"));
  }
  // 4. If calendar.[[Identifier]] is "iso8601", then
  if (calendar->calendar_index() == 0) {
    // a. Return undefined.
    return isolate->factory()->undefined_value();
  }
  UNIMPLEMENTED();
  // TODO(ftang) implement other calendars
  // 5. Let eraYear be ! CalendarDateEraYear(calendar.[[Identifier]],
  // temporalDateLike).
  // 6. If eraYear is undefined, then
  // a. Return undefined.
  // 7. Return 𝔽(eraYear).
}

#endif  // V8_INTL_SUPPORT

// #sec-temporal.calendar.prototype.weekofyear
MaybeDirectHandle<Smi> JSTemporalCalendar::WeekOfYear(
    Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
    DirectHandle<Object> temporal_date_like) {
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  // 4. Let temporalDate be ? ToTemporalDate(temporalDateLike).
  DirectHandle<JSTemporalPlainDate> temporal_date;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temporal_date,
      ToTemporalDate(isolate, temporal_date_like,
                     "Temporal.Calendar.prototype.weekOfYear"));
  // a. Let value be ! ToISOWeekOfYear(temporalDate.[[ISOYear]],
  // temporalDate.[[ISOMonth]], temporalDate.[[ISODay]]).
  int32_t value = ToISOWeekOfYear(
      isolate, {temporal_date->iso_year(), temporal_date->iso_month(),
                temporal_date->iso_day()});
  return direct_handle(Smi::FromInt(value), isolate);
}

// #sec-temporal.calendar.prototype.tostring
MaybeDirectHandle<String> JSTemporalCalendar::ToString(
    Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
    const char* method_name) {
  return CalendarIdentifier(isolate, calendar->calendar_index());
}

// #sec-temporal.now.timezone
MaybeDirectHandle<JSTemporalTimeZone> JSTemporalTimeZone::Now(
    Isolate* isolate) {
  return SystemTimeZone(isolate);
}

// #sec-temporal.timezone
MaybeDirectHandle<JSTemporalTimeZone> JSTemporalTimeZone::Constructor(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, DirectHandle<Object> identifier_obj) {
  // 1. If NewTarget is undefined, then
  if (IsUndefined(*new_target, isolate)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kConstructorNotFunction,
                                 isolate->factory()->NewStringFromAsciiChecked(
                                     "Temporal.TimeZone")));
  }
  // 2. Set identifier to ? ToString(identifier).
  DirectHandle<String> identifier;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, identifier,
                             Object::ToString(isolate, identifier_obj));
  DirectHandle<String> canonical;
  // 3. If identifier satisfies the syntax of a TimeZoneNumericUTCOffset
  // (see 13.33), then
  if (IsValidTimeZoneNumericUTCOffsetString(isolate, identifier)) {
    // a. Let offsetNanoseconds be ? ParseTimeZoneOffsetString(identifier).
    int64_t offset_nanoseconds;
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, offset_nanoseconds,
        ParseTimeZoneOffsetString(isolate, identifier),
        DirectHandle<JSTemporalTimeZone>());

    // b. Let canonical be ! FormatTimeZoneOffsetString(offsetNanoseconds).
    canonical = FormatTimeZoneOffsetString(isolate, offset_nanoseconds);
  } else {
    // 4. Else,
    // a. If ! IsValidTimeZoneName(identifier) is false, then
    if (!IsValidTimeZoneName(isolate, identifier)) {
      // i. Throw a RangeError exception.
      THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kInvalidTimeZone,
                                             identifier));
    }
    // b. Let canonical be ! CanonicalizeTimeZoneName(identifier).
    canonical = CanonicalizeTimeZoneName(isolate, identifier);
  }
  // 5. Return ? CreateTemporalTimeZone(canonical, NewTarget).
  return CreateTemporalTimeZone(isolate, target, new_target, canonical);
}

namespace {

MaybeDirectHandle<JSTemporalPlainDateTime> ToTemporalDateTime(
    Isolate* isolate, DirectHandle<Object> item_obj,
    DirectHandle<Object> options, const char* method_name);

MaybeDirectHandle<JSTemporalPlainDateTime> ToTemporalDateTime(
    Isolate* isolate, DirectHandle<Object> item_obj, const char* method_name) {
  // 1. If options is not present, set options to undefined.
  return ToTemporalDateTime(isolate, item_obj,
                            isolate->factory()->undefined_value(), method_name);
}

}  // namespace

// #sec-temporal.timezone.prototype.getinstantfor
MaybeDirectHandle<JSTemporalInstant> JSTemporalTimeZone::GetInstantFor(
    Isolate* isolate, DirectHandle<JSTemporalTimeZone> time_zone,
    DirectHandle<Object> date_time_obj, DirectHandle<Object> options_obj) {
  const char* method_name = "Temporal.TimeZone.prototype.getInstantFor";
  // 1. Let timeZone be the this value.
  // 2. Perform ? RequireInternalSlot(timeZone,
  // [[InitializedTemporalTimeZone]]).
  // 3. Set dateTime to ? ToTemporalDateTime(dateTime).
  DirectHandle<JSTemporalPlainDateTime> date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, date_time,
      ToTemporalDateTime(isolate, date_time_obj, method_name));

  // 4. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));

  // 5. Let disambiguation be ? ToTemporalDisambiguation(options).
  Disambiguation disambiguation;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, disambiguation,
      ToTemporalDisambiguation(isolate, options, method_name),
      DirectHandle<JSTemporalInstant>());

  // 6. Return ? BuiltinTimeZoneGetInstantFor(timeZone, dateTime,
  // disambiguation).
  return BuiltinTimeZoneGetInstantFor(isolate, time_zone, date_time,
                                      disambiguation, method_name);
}

namespace {

#ifdef V8_INTL_SUPPORT
DirectHandle<Object> GetIANATimeZoneTransition(Isolate* isolate,
                                               DirectHandle<BigInt> nanoseconds,
                                               int32_t time_zone_index,
                                               Intl::Transition transition) {
  if (time_zone_index == JSTemporalTimeZone::kUTCTimeZoneIndex) {
    return isolate->factory()->null_value();
  }
  return Intl::GetTimeZoneOffsetTransitionNanoseconds(isolate, time_zone_index,
                                                      nanoseconds, transition);
}
// #sec-temporal-getianatimezonenexttransition
DirectHandle<Object> GetIANATimeZoneNextTransition(
    Isolate* isolate, DirectHandle<BigInt> nanoseconds,
    int32_t time_zone_index) {
  return GetIANATimeZoneTransition(isolate, nanoseconds, time_zone_index,
                                   Intl::Transition::kNext);
}
// #sec-temporal-getianatimezoneprevioustransition
DirectHandle<Object> GetIANATimeZonePreviousTransition(
    Isolate* isolate, DirectHandle<BigInt> nanoseconds,
    int32_t time_zone_index) {
  return GetIANATimeZoneTransition(isolate, nanoseconds, time_zone_index,
                                   Intl::Transition::kPrevious);
}

DirectHandle<Object> GetIANATimeZoneOffsetNanoseconds(
    Isolate* isolate, DirectHandle<BigInt> nanoseconds,
    int32_t time_zone_index) {
  if (time_zone_index == JSTemporalTimeZone::kUTCTimeZoneIndex) {
    return direct_handle(Smi::zero(), isolate);
  }

  return isolate->factory()->NewNumberFromInt64(
      Intl::GetTimeZoneOffsetNanoseconds(isolate, time_zone_index,
                                         nanoseconds));
}
#else   // V8_INTL_SUPPORT
// #sec-temporal-getianatimezonenexttransition
DirectHandle<Object> GetIANATimeZoneNextTransition(Isolate* isolate,
                                                   DirectHandle<BigInt>,
                                                   int32_t) {
  return isolate->factory()->null_value();
}
// #sec-temporal-getianatimezoneprevioustransition
DirectHandle<Object> GetIANATimeZonePreviousTransition(Isolate* isolate,
                                                       DirectHandle<BigInt>,
                                                       int32_t) {
  return isolate->factory()->null_value();
}
DirectHandle<Object> GetIANATimeZoneOffsetNanoseconds(Isolate* isolate,
                                                      DirectHandle<BigInt>,
                                                      int32_t time_zone_index) {
  DCHECK_EQ(time_zone_index, JSTemporalTimeZone::kUTCTimeZoneIndex);
  return handle(Smi::zero(), isolate);
}
#endif  // V8_INTL_SUPPORT

}  // namespace

// #sec-temporal.timezone.prototype.getplaindatetimefor
MaybeDirectHandle<JSTemporalPlainDateTime>
JSTemporalTimeZone::GetPlainDateTimeFor(
    Isolate* isolate, DirectHandle<JSTemporalTimeZone> time_zone,
    DirectHandle<Object> instant_obj, DirectHandle<Object> calendar_like) {
  TEMPORAL_ENTER_FUNC();
  const char* method_name = "Temporal.TimeZone.prototype.getPlainDateTimeFor";
  // 1. 1. Let timeZone be the this value.
  // 2. Perform ? RequireInternalSlot(timeZone,
  // [[InitializedTemporalTimeZone]]).
  // 3. Set instant to ? ToTemporalInstant(instant).
  DirectHandle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant, ToTemporalInstant(isolate, instant_obj, method_name));
  // 4. Let calendar be ? ToTemporalCalendarWithISODefault(calendarLike).
  DirectHandle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar,
      ToTemporalCalendarWithISODefault(isolate, calendar_like, method_name));

  // 5. Return ? BuiltinTimeZoneGetPlainDateTimeFor(timeZone, instant,
  // calendar).
  return temporal::BuiltinTimeZoneGetPlainDateTimeFor(
      isolate, time_zone, instant, calendar, method_name);
}

// template for shared code of Temporal.TimeZone.prototype.getNextTransition and
// Temporal.TimeZone.prototype.getPreviousTransition
template <DirectHandle<Object> (*iana_func)(Isolate*, DirectHandle<BigInt>,
                                            int32_t)>
MaybeDirectHandle<Object> GetTransition(
    Isolate* isolate, DirectHandle<JSTemporalTimeZone> time_zone,
    DirectHandle<Object> starting_point_obj, const char* method_name) {
  TEMPORAL_ENTER_FUNC();
  // 1. Let timeZone be the this value.
  // 2. Perform ? RequireInternalSlot(timeZone,
  // [[InitializedTemporalTimeZone]]).
  // 3. Set startingPoint to ? ToTemporalInstant(startingPoint).
  DirectHandle<JSTemporalInstant> starting_point;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, starting_point,
      ToTemporalInstant(isolate, starting_point_obj, method_name));
  // 4. If timeZone.[[OffsetNanoseconds]] is not undefined, return null.
  if (time_zone->is_offset()) {
    return isolate->factory()->null_value();
  }
  // 5. Let transition be ?
  // GetIANATimeZoneNextTransition(startingPoint.[[Nanoseconds]],
  // timeZone.[[Identifier]]).
  DirectHandle<Object> transition_obj =
      iana_func(isolate, direct_handle(starting_point->nanoseconds(), isolate),
                time_zone->time_zone_index());
  // 6. If transition is null, return null.
  if (IsNull(*transition_obj)) {
    return isolate->factory()->null_value();
  }
  DCHECK(IsBigInt(*transition_obj));
  DirectHandle<BigInt> transition = Cast<BigInt>(transition_obj);
  // 7. Return ! CreateTemporalInstant(transition).
  return temporal::CreateTemporalInstant(isolate, transition).ToHandleChecked();
}

// #sec-temporal.timezone.prototype.getnexttransition
MaybeDirectHandle<Object> JSTemporalTimeZone::GetNextTransition(
    Isolate* isolate, DirectHandle<JSTemporalTimeZone> time_zone,
    DirectHandle<Object> starting_point_obj) {
  return GetTransition<GetIANATimeZoneNextTransition>(
      isolate, time_zone, starting_point_obj,
      "Temporal.TimeZone.prototype.getNextTransition");
}
// #sec-temporal.timezone.prototype.getprevioustransition
MaybeDirectHandle<Object> JSTemporalTimeZone::GetPreviousTransition(
    Isolate* isolate, DirectHandle<JSTemporalTimeZone> time_zone,
    DirectHandle<Object> starting_point_obj) {
  return GetTransition<GetIANATimeZonePreviousTransition>(
      isolate, time_zone, starting_point_obj,
      "Temporal.TimeZone.prototype.getPreviousTransition");
}

// #sec-temporal.timezone.prototype.getpossibleinstantsfor
// #sec-temporal-getianatimezoneepochvalue
MaybeDirectHandle<JSArray> GetIANATimeZoneEpochValueAsArrayOfInstantForUTC(
    Isolate* isolate, const DateTimeRecord& date_time) {
  Factory* factory = isolate->factory();
  // 6. Let possibleInstants be a new empty List.
  DirectHandle<BigInt> epoch_nanoseconds =
      GetEpochFromISOParts(isolate, date_time);
  DirectHandle<FixedArray> fixed_array = factory->NewFixedArray(1);
  // 7. For each value epochNanoseconds in possibleEpochNanoseconds, do
  // a. If ! IsValidEpochNanoseconds(epochNanoseconds) is false, throw a
  // RangeError exception.
  if (!IsValidEpochNanoseconds(isolate, epoch_nanoseconds)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }
  // b. Let instant be ! CreateTemporalInstant(epochNanoseconds).
  DirectHandle<JSTemporalInstant> instant =
      temporal::CreateTemporalInstant(isolate, epoch_nanoseconds)
          .ToHandleChecked();
  // c. Append instant to possibleInstants.
  fixed_array->set(0, *instant);
  // 8. Return ! CreateArrayFromList(possibleInstants).
  return factory->NewJSArrayWithElements(fixed_array);
}

#ifdef V8_INTL_SUPPORT
MaybeDirectHandle<JSArray> GetIANATimeZoneEpochValueAsArrayOfInstant(
    Isolate* isolate, int32_t time_zone_index,
    const DateTimeRecord& date_time) {
  Factory* factory = isolate->factory();
  if (time_zone_index == JSTemporalTimeZone::kUTCTimeZoneIndex) {
    return GetIANATimeZoneEpochValueAsArrayOfInstantForUTC(isolate, date_time);
  }

  // For TimeZone other than UTC, call ICU indirectly from Intl
  DirectHandle<BigInt> nanoseconds_in_local_time =
      GetEpochFromISOParts(isolate, date_time);

  DirectHandleVector<BigInt> possible_offset =
      Intl::GetTimeZonePossibleOffsetNanoseconds(isolate, time_zone_index,
                                                 nanoseconds_in_local_time);

  int32_t array_length = static_cast<int32_t>(possible_offset.size());
  DirectHandle<FixedArray> fixed_array = factory->NewFixedArray(array_length);

  for (int32_t i = 0; i < array_length; i++) {
    DirectHandle<BigInt> epoch_nanoseconds =
        BigInt::Subtract(isolate, nanoseconds_in_local_time, possible_offset[i])
            .ToHandleChecked();
    // a. If ! IsValidEpochNanoseconds(epochNanoseconds) is false, throw a
    // RangeError exception.
    if (!IsValidEpochNanoseconds(isolate, epoch_nanoseconds)) {
      THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
    }
    // b. Let instant be ! CreateTemporalInstant(epochNanoseconds).
    DirectHandle<JSTemporalInstant> instant =
        temporal::CreateTemporalInstant(isolate, epoch_nanoseconds)
            .ToHandleChecked();
    // b. Append instant to possibleInstants.
    fixed_array->set(i, *(instant));
  }

  // 8. Return ! CreateArrayFromList(possibleInstants).
  return factory->NewJSArrayWithElements(fixed_array);
}

#else   //  V8_INTL_SUPPORT

MaybeDirectHandle<JSArray> GetIANATimeZoneEpochValueAsArrayOfInstant(
    Isolate* isolate, int32_t time_zone_index,
    const DateTimeRecord& date_time) {
  DCHECK_EQ(time_zone_index, JSTemporalTimeZone::kUTCTimeZoneIndex);
  return GetIANATimeZoneEpochValueAsArrayOfInstantForUTC(isolate, date_time);
}
#endif  // V8_INTL_SUPPORT

// #sec-temporal.timezone.prototype.getpossibleinstantsfor
MaybeDirectHandle<JSArray> JSTemporalTimeZone::GetPossibleInstantsFor(
    Isolate* isolate, DirectHandle<JSTemporalTimeZone> time_zone,
    DirectHandle<Object> date_time_obj) {
  Factory* factory = isolate->factory();
  // 1. Let timeZone be the this value.
  // 2. Perform ? RequireInternalSlot(timeZone,
  // [[InitializedTemporalTimezone]]).
  // 3. Set dateTime to ? ToTemporalDateTime(dateTime).
  DirectHandle<JSTemporalPlainDateTime> date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, date_time,
      ToTemporalDateTime(isolate, date_time_obj,
                         "Temporal.TimeZone.prototype.getPossibleInstantsFor"));
  DateTimeRecord date_time_record = {
      {date_time->iso_year(), date_time->iso_month(), date_time->iso_day()},
      {date_time->iso_hour(), date_time->iso_minute(), date_time->iso_second(),
       date_time->iso_millisecond(), date_time->iso_microsecond(),
       date_time->iso_nanosecond()}};
  // 4. If timeZone.[[OffsetNanoseconds]] is not undefined, then
  if (time_zone->is_offset()) {
    // a. Let epochNanoseconds be ! GetEpochFromISOParts(dateTime.[[ISOYear]],
    // dateTime.[[ISOMonth]], dateTime.[[ISODay]], dateTime.[[ISOHour]],
    // dateTime.[[ISOMinute]], dateTime.[[ISOSecond]],
    // dateTime.[[ISOMillisecond]], dateTime.[[ISOMicrosecond]],
    // dateTime.[[ISONanosecond]]).
    DirectHandle<BigInt> epoch_nanoseconds =
        GetEpochFromISOParts(isolate, date_time_record);
    // b. Let possibleEpochNanoseconds be « epochNanoseconds -
    // ℤ(timeZone.[[OffsetNanoseconds]]) ».
    epoch_nanoseconds =
        BigInt::Subtract(
            isolate, epoch_nanoseconds,
            BigInt::FromInt64(isolate, time_zone->offset_nanoseconds()))
            .ToHandleChecked();

    // The following is the step 7 and 8 for the case of step 4 under the if
    // block.

    // a. If ! IsValidEpochNanoseconds(epochNanoseconds) is false, throw a
    // RangeError exception.
    if (!IsValidEpochNanoseconds(isolate, epoch_nanoseconds)) {
      THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
    }

    // b. Let instant be ! CreateTemporalInstant(epochNanoseconds).

    DirectHandle<JSTemporalInstant> instant =
        temporal::CreateTemporalInstant(isolate, epoch_nanoseconds)
            .ToHandleChecked();
    // c. Return ! CreateArrayFromList(« instant »).
    DirectHandle<FixedArray> fixed_array = factory->NewFixedArray(1);
    fixed_array->set(0, *instant);
    return factory->NewJSArrayWithElements(fixed_array);
  }

  // 5. Let possibleEpochNanoseconds be ?
  // GetIANATimeZoneEpochValue(timeZone.[[Identifier]], dateTime.[[ISOYear]],
  // dateTime.[[ISOMonth]], dateTime.[[ISODay]], dateTime.[[ISOHour]],
  // dateTime.[[ISOMinute]], dateTime.[[ISOSecond]],
  // dateTime.[[ISOMillisecond]], dateTime.[[ISOMicrosecond]],
  // dateTime.[[ISONanosecond]]).

  // ... Step 5-8 put into GetIANATimeZoneEpochValueAsArrayOfInstant
  // 8. Return ! CreateArrayFromList(possibleInstants).
  return GetIANATimeZoneEpochValueAsArrayOfInstant(
      isolate, time_zone->time_zone_index(), date_time_record);
}

// #sec-temporal.timezone.prototype.getoffsetnanosecondsfor
MaybeDirectHandle<Object> JSTemporalTimeZone::GetOffsetNanosecondsFor(
    Isolate* isolate, DirectHandle<JSTemporalTimeZone> time_zone,
    DirectHandle<Object> instant_obj) {
  TEMPORAL_ENTER_FUNC();
  // 1. Let timeZone be the this value.
  // 2. Perform ? RequireInternalSlot(timeZone,
  // [[InitializedTemporalTimeZone]]).
  // 3. Set instant to ? ToTemporalInstant(instant).
  DirectHandle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant,
      ToTemporalInstant(isolate, instant_obj,
                        "Temporal.TimeZone.prototype.getOffsetNanosecondsFor"));
  // 4. If timeZone.[[OffsetNanoseconds]] is not undefined, return
  // timeZone.[[OffsetNanoseconds]].
  if (time_zone->is_offset()) {
    return isolate->factory()->NewNumberFromInt64(
        time_zone->offset_nanoseconds());
  }
  // 5. Return ! GetIANATimeZoneOffsetNanoseconds(instant.[[Nanoseconds]],
  // timeZone.[[Identifier]]).
  return GetIANATimeZoneOffsetNanoseconds(
      isolate, direct_handle(instant->nanoseconds(), isolate),
      time_zone->time_zone_index());
}

// #sec-temporal.timezone.prototype.getoffsetstringfor
MaybeDirectHandle<String> JSTemporalTimeZone::GetOffsetStringFor(
    Isolate* isolate, DirectHandle<JSTemporalTimeZone> time_zone,
    DirectHandle<Object> instant_obj) {
  TEMPORAL_ENTER_FUNC();
  const char* method_name = "Temporal.TimeZone.prototype.getOffsetStringFor";
  // 1. Let timeZone be the this value.
  // 2. Perform ? RequireInternalSlot(timeZone,
  // [[InitializedTemporalTimeZone]]).
  // 3. Set instant to ? ToTemporalInstant(instant).
  DirectHandle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant, ToTemporalInstant(isolate, instant_obj, method_name));
  // 4. Return ? BuiltinTimeZoneGetOffsetStringFor(timeZone, instant).
  return BuiltinTimeZoneGetOffsetStringFor(isolate, time_zone, instant,
                                           method_name);
}

// #sec-temporal.timezone.prototype.tostring
MaybeDirectHandle<Object> JSTemporalTimeZone::ToString(
    Isolate* isolate, DirectHandle<JSTemporalTimeZone> time_zone,
    const char* method_name) {
  return time_zone->id(isolate);
}

int32_t JSTemporalTimeZone::time_zone_index() const {
  DCHECK(is_offset() == false);
  return offset_milliseconds_or_time_zone_index();
}

int64_t JSTemporalTimeZone::offset_nanoseconds() const {
  TEMPORAL_ENTER_FUNC();
  DCHECK(is_offset());
  return static_cast<int64_t>(offset_milliseconds()) * 1000000 +
         static_cast<int64_t>(offset_sub_milliseconds());
}

void JSTemporalTimeZone::set_offset_nanoseconds(int64_t ns) {
  this->set_offset_milliseconds(static_cast<int32_t>(ns / 1000000));
  this->set_offset_sub_milliseconds(static_cast<int32_t>(ns % 1000000));
}

MaybeDirectHandle<String> JSTemporalTimeZone::id(Isolate* isolate) const {
  if (is_offset()) {
    return FormatTimeZoneOffsetString(isolate, offset_nanoseconds());
  }
#ifdef V8_INTL_SUPPORT
  std::string id =
      Intl::TimeZoneIdFromIndex(offset_milliseconds_or_time_zone_index());
  return isolate->factory()->NewStringFromAsciiChecked(id.c_str());
#else   // V8_INTL_SUPPORT
  DCHECK_EQ(kUTCTimeZoneIndex, offset_milliseconds_or_time_zone_index());
  return isolate->factory()->UTC_string();
#endif  // V8_INTL_SUPPORT
}

MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainDate::Constructor(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, DirectHandle<Object> iso_year_obj,
    DirectHandle<Object> iso_month_obj, DirectHandle<Object> iso_day_obj,
    DirectHandle<Object> calendar_like) {
  const char* method_name = "Temporal.PlainDate";
  // 1. If NewTarget is undefined, throw a TypeError exception.
  if (IsUndefined(*new_target)) {
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                                 isolate->factory()->NewStringFromAsciiChecked(
                                     method_name)));
  }
#define TO_INT_THROW_ON_INFTY(name, T)                                         \
  int32_t name;                                                                \
  {                                                                            \
    DirectHandle<Object> number_##name;                                        \
    /* x. Let name be ? ToIntegerThrowOnInfinity(name). */                     \
    ASSIGN_RETURN_ON_EXCEPTION(isolate, number_##name,                         \
                               ToIntegerThrowOnInfinity(isolate, name##_obj)); \
    name = NumberToInt32(*number_##name);                                      \
  }

  TO_INT_THROW_ON_INFTY(iso_year, JSTemporalPlainDate);
  TO_INT_THROW_ON_INFTY(iso_month, JSTemporalPlainDate);
  TO_INT_THROW_ON_INFTY(iso_day, JSTemporalPlainDate);

  // 8. Let calendar be ? ToTemporalCalendarWithISODefault(calendarLike).
  DirectHandle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar,
      ToTemporalCalendarWithISODefault(isolate, calendar_like, method_name));

  // 9. Return ? CreateTemporalDate(y, m, d, calendar, NewTarget).
  return CreateTemporalDate(isolate, target, new_target,
                            {iso_year, iso_month, iso_day}, calendar);
}

// #sec-temporal.plaindate.compare
MaybeDirectHandle<Smi> JSTemporalPlainDate::Compare(
    Isolate* isolate, DirectHandle<Object> one_obj,
    DirectHandle<Object> two_obj) {
  const char* method_name = "Temporal.PlainDate.compare";
  // 1. Set one to ? ToTemporalDate(one).
  DirectHandle<JSTemporalPlainDate> one;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, one,
                             ToTemporalDate(isolate, one_obj, method_name));
  // 2. Set two to ? ToTemporalDate(two).
  DirectHandle<JSTemporalPlainDate> two;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, two,
                             ToTemporalDate(isolate, two_obj, method_name));
  // 3. Return 𝔽(! CompareISODate(one.[[ISOYear]], one.[[ISOMonth]],
  // one.[[ISODay]], two.[[ISOYear]], two.[[ISOMonth]], two.[[ISODay]])).
  return DirectHandle<Smi>(
      Smi::FromInt(
          CompareISODate({one->iso_year(), one->iso_month(), one->iso_day()},
                         {two->iso_year(), two->iso_month(), two->iso_day()})),
      isolate);
}

// #sec-temporal.plaindate.prototype.equals
MaybeDirectHandle<Oddball> JSTemporalPlainDate::Equals(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date,
    DirectHandle<Object> other_obj) {
  Factory* factory = isolate->factory();
  // 1. Let temporalDate be the this value.
  // 2. Perform ? RequireInternalSlot(temporalDate,
  // [[InitializedTemporalDate]]).
  // 3. Set other to ? ToTemporalDate(other).
  DirectHandle<JSTemporalPlainDate> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other,
      ToTemporalDate(isolate, other_obj,
                     "Temporal.PlainDate.prototype.equals"));
  // 4. If temporalDate.[[ISOYear]] ≠ other.[[ISOYear]], return false.
  if (temporal_date->iso_year() != other->iso_year()) {
    return factory->false_value();
  }
  // 5. If temporalDate.[[ISOMonth]] ≠ other.[[ISOMonth]], return false.
  if (temporal_date->iso_month() != other->iso_month()) {
    return factory->false_value();
  }
  // 6. If temporalDate.[[ISODay]] ≠ other.[[ISODay]], return false.
  if (temporal_date->iso_day() != other->iso_day()) {
    return factory->false_value();
  }
  // 7. Return ? CalendarEquals(temporalDate.[[Calendar]], other.[[Calendar]]).
  return CalendarEquals(isolate,
                        direct_handle(temporal_date->calendar(), isolate),
                        direct_handle(other->calendar(), isolate));
}

// #sec-temporal.plaindate.prototype.withcalendar
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainDate::WithCalendar(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date,
    DirectHandle<Object> calendar_like) {
  const char* method_name = "Temporal.PlainDate.prototype.withCalendar";
  // 1. Let temporalDate be the this value.
  // 2. Perform ? RequireInternalSlot(temporalDate,
  // [[InitializedTemporalDate]]).
  // 3. Let calendar be ? ToTemporalCalendar(calendar).
  DirectHandle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar,
      temporal::ToTemporalCalendar(isolate, calendar_like, method_name));
  // 4. Return ? CreateTemporalDate(temporalDate.[[ISOYear]],
  // temporalDate.[[ISOMonth]], temporalDate.[[ISODay]], calendar).
  return CreateTemporalDate(
      isolate,
      {temporal_date->iso_year(), temporal_date->iso_month(),
       temporal_date->iso_day()},
      calendar);
}

// Template for common code shared by
// Temporal.PlainDate(Time)?.prototype.toPlain(YearMonth|MonthDay)
// #sec-temporal.plaindate.prototype.toplainmonthday
// #sec-temporal.plaindate.prototype.toplainyearmonth
// #sec-temporal.plaindatetime.prototype.toplainmonthday
// #sec-temporal.plaindatetime.prototype.toplainyearmonth
template <typename T, typename R,
          MaybeDirectHandle<R> (*from_fields)(
              Isolate*, DirectHandle<JSReceiver>, DirectHandle<JSReceiver>,
              DirectHandle<Object>)>
MaybeDirectHandle<R> ToPlain(Isolate* isolate, DirectHandle<T> t,
                             DirectHandle<String> f1, DirectHandle<String> f2) {
  Factory* factory = isolate->factory();
  // 1. Let temporalDate be the this value.
  // 2. Perform ? RequireInternalSlot(t, [[InitializedTemporalDate]]).
  // 3. Let calendar be t.[[Calendar]].
  DirectHandle<JSReceiver> calendar(t->calendar(), isolate);
  // 4. Let fieldNames be ? CalendarFields(calendar, « f1 , f2 »).
  DirectHandle<FixedArray> field_names = factory->NewFixedArray(2);
  field_names->set(0, *f1);
  field_names->set(1, *f2);
  ASSIGN_RETURN_ON_EXCEPTION(isolate, field_names,
                             CalendarFields(isolate, calendar, field_names));
  // 5. Let fields be ? PrepareTemporalFields(t, fieldNames, «»).
  DirectHandle<JSReceiver> fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, fields,
      PrepareTemporalFields(isolate, t, field_names, RequiredFields::kNone));
  // 6. Return ? FromFields(calendar, fields).
  return from_fields(isolate, calendar, fields,
                     isolate->factory()->undefined_value());
}

// #sec-temporal.plaindate.prototype.toplainyearmonth
MaybeDirectHandle<JSTemporalPlainYearMonth>
JSTemporalPlainDate::ToPlainYearMonth(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date) {
  return ToPlain<JSTemporalPlainDate, JSTemporalPlainYearMonth,
                 YearMonthFromFields>(isolate, temporal_date,
                                      isolate->factory()->monthCode_string(),
                                      isolate->factory()->year_string());
}

// #sec-temporal.plaindate.prototype.toplainmonthday
MaybeDirectHandle<JSTemporalPlainMonthDay> JSTemporalPlainDate::ToPlainMonthDay(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date) {
  return ToPlain<JSTemporalPlainDate, JSTemporalPlainMonthDay,
                 MonthDayFromFields>(isolate, temporal_date,
                                     isolate->factory()->day_string(),
                                     isolate->factory()->monthCode_string());
}

// #sec-temporal.plaindate.prototype.toplaindatetime
MaybeDirectHandle<JSTemporalPlainDateTime> JSTemporalPlainDate::ToPlainDateTime(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date,
    DirectHandle<Object> temporal_time_obj) {
  // 1. Let temporalDate be the this value.
  // 2. Perform ? RequireInternalSlot(temporalDate,
  // [[InitializedTemporalDate]]).
  // 3. If temporalTime is undefined, then
  if (IsUndefined(*temporal_time_obj)) {
    // a. Return ? CreateTemporalDateTime(temporalDate.[[ISOYear]],
    // temporalDate.[[ISOMonth]], temporalDate.[[ISODay]], 0, 0, 0, 0, 0, 0,
    // temporalDate.[[Calendar]]).
    return temporal::CreateTemporalDateTime(
        isolate,
        {{temporal_date->iso_year(), temporal_date->iso_month(),
          temporal_date->iso_day()},
         {0, 0, 0, 0, 0, 0}},
        DirectHandle<JSReceiver>(temporal_date->calendar(), isolate));
  }
  // 4. Set temporalTime to ? ToTemporalTime(temporalTime).
  DirectHandle<JSTemporalPlainTime> temporal_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temporal_time,
      temporal::ToTemporalTime(isolate, temporal_time_obj,
                               "Temporal.PlainDate.prototype.toPlainDateTime"));
  // 5. Return ? CreateTemporalDateTime(temporalDate.[[ISOYear]],
  // temporalDate.[[ISOMonth]], temporalDate.[[ISODay]],
  // temporalTime.[[ISOHour]], temporalTime.[[ISOMinute]],
  // temporalTime.[[ISOSecond]], temporalTime.[[ISOMillisecond]],
  // temporalTime.[[ISOMicrosecond]], temporalTime.[[ISONanosecond]],
  // temporalDate.[[Calendar]]).
  return temporal::CreateTemporalDateTime(
      isolate,
      {{temporal_date->iso_year(), temporal_date->iso_month(),
        temporal_date->iso_day()},
       {temporal_time->iso_hour(), temporal_time->iso_minute(),
        temporal_time->iso_second(), temporal_time->iso_millisecond(),
        temporal_time->iso_microsecond(), temporal_time->iso_nanosecond()}},
      DirectHandle<JSReceiver>(temporal_date->calendar(), isolate));
}

namespace {

// #sec-temporal-rejectobjectwithcalendarortimezone
Maybe<bool> RejectObjectWithCalendarOrTimeZone(
    Isolate* isolate, DirectHandle<JSReceiver> object) {
  TEMPORAL_ENTER_FUNC();

  Factory* factory = isolate->factory();
  // 1. Assert: Type(object) is Object.
  // 2. If object has an [[InitializedTemporalDate]],
  // [[InitializedTemporalDateTime]], [[InitializedTemporalMonthDay]],
  // [[InitializedTemporalTime]], [[InitializedTemporalYearMonth]], or
  // [[InitializedTemporalZonedDateTime]] internal slot, then
  if (IsJSTemporalPlainDate(*object) || IsJSTemporalPlainDateTime(*object) ||
      IsJSTemporalPlainMonthDay(*object) || IsJSTemporalPlainTime(*object) ||
      IsJSTemporalPlainYearMonth(*object) ||
      IsJSTemporalZonedDateTime(*object)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR(),
                                 Nothing<bool>());
  }
  // 3. Let calendarProperty be ? Get(object, "calendar").
  DirectHandle<Object> calendar_property;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, calendar_property,
      JSReceiver::GetProperty(isolate, object, factory->calendar_string()),
      Nothing<bool>());
  // 4. If calendarProperty is not undefined, then
  if (!IsUndefined(*calendar_property)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR(),
                                 Nothing<bool>());
  }
  // 5. Let timeZoneProperty be ? Get(object, "timeZone").
  DirectHandle<Object> time_zone_property;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, time_zone_property,
      JSReceiver::GetProperty(isolate, object, factory->timeZone_string()),
      Nothing<bool>());
  // 6. If timeZoneProperty is not undefined, then
  if (!IsUndefined(*time_zone_property)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR(),
                                 Nothing<bool>());
  }
  return Just(true);
}

// #sec-temporal-calendarmergefields
MaybeDirectHandle<JSReceiver> CalendarMergeFields(
    Isolate* isolate, DirectHandle<JSReceiver> calendar,
    DirectHandle<JSReceiver> fields,
    DirectHandle<JSReceiver> additional_fields) {
  // 1. Let mergeFields be ? GetMethod(calendar, "mergeFields").
  DirectHandle<Object> merge_fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, merge_fields,
      Object::GetMethod(isolate, calendar,
                        isolate->factory()->mergeFields_string()));
  // 2. If mergeFields is undefined, then
  if (IsUndefined(*merge_fields)) {
    // a. Return ? DefaultMergeFields(fields, additionalFields).
    return DefaultMergeFields(isolate, fields, additional_fields);
  }
  // 3. Return ? Call(mergeFields, calendar, « fields, additionalFields »).
  DirectHandle<Object> args[] = {fields, additional_fields};
  DirectHandle<Object> result;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, result,
      Execution::Call(isolate, merge_fields, calendar, base::VectorOf(args)));
  // 4. If Type(result) is not Object, throw a TypeError exception.
  if (!IsJSReceiver(*result)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }
  return Cast<JSReceiver>(result);
}

// Common code shared by Temporal.Plain(Date|YearMonth|MonthDay).prototype.with
template <typename T, MaybeDirectHandle<T> (*from_fields_func)(
                          Isolate*, DirectHandle<JSReceiver>,
                          DirectHandle<JSReceiver>, DirectHandle<Object>)>
MaybeDirectHandle<T> PlainDateOrYearMonthOrMonthDayWith(
    Isolate* isolate, DirectHandle<T> temporal,
    DirectHandle<Object> temporal_like_obj, DirectHandle<Object> options_obj,
    DirectHandle<FixedArray> field_names, const char* method_name) {
  // 1. Let temporalDate be the this value.
  // 2. Perform ? RequireInternalSlot(temporalDate,
  // [[InitializedTemporalXXX]]).
  // 3. If Type(temporalXXXLike) is not Object, then
  if (!IsJSReceiver(*temporal_like_obj)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }
  DirectHandle<JSReceiver> temporal_like = Cast<JSReceiver>(temporal_like_obj);
  // 4. Perform ? RejectObjectWithCalendarOrTimeZone(temporalXXXLike).
  MAYBE_RETURN(RejectObjectWithCalendarOrTimeZone(isolate, temporal_like),
               DirectHandle<T>());

  // 5. Let calendar be temporalXXX.[[Calendar]].
  DirectHandle<JSReceiver> calendar(temporal->calendar(), isolate);

  // 6. Let fieldNames be ? CalendarFields(calendar, fieldNames).
  ASSIGN_RETURN_ON_EXCEPTION(isolate, field_names,
                             CalendarFields(isolate, calendar, field_names));
  // 7. Let partialDate be ? PreparePartialTemporalFields(temporalXXXLike,
  // fieldNames).
  DirectHandle<JSReceiver> partial_date;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, partial_date,
      PreparePartialTemporalFields(isolate, temporal_like, field_names));
  // 8. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));
  // 9. Let fields be ? PrepareTemporalFields(temporalXXX, fieldNames, «»).
  DirectHandle<JSReceiver> fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, fields,
      PrepareTemporalFields(isolate, temporal, field_names,
                            RequiredFields::kNone));
  // 10. Set fields to ? CalendarMergeFields(calendar, fields, partialDate).
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, fields,
      CalendarMergeFields(isolate, calendar, fields, partial_date));
  // 11. Set fields to ? PrepareTemporalFields(fields, fieldNames, «»).
  ASSIGN_RETURN_ON_EXCEPTION(isolate, fields,
                             PrepareTemporalFields(isolate, fields, field_names,
                                                   RequiredFields::kNone));
  // 12. Return ? XxxFromFields(calendar, fields, options).
  return from_fields_func(isolate, calendar, fields, options);
}

}  // namespace

// #sec-temporal.plaindate.prototype.with
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainDate::With(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date,
    DirectHandle<Object> temporal_date_like_obj,
    DirectHandle<Object> options_obj) {
  // 6. Let fieldNames be ? CalendarFields(calendar, « "day", "month",
  // "monthCode", "year" »).
  DirectHandle<FixedArray> field_names =
      DayMonthMonthCodeYearInFixedArray(isolate);
  return PlainDateOrYearMonthOrMonthDayWith<JSTemporalPlainDate,
                                            DateFromFields>(
      isolate, temporal_date, temporal_date_like_obj, options_obj, field_names,
      "Temporal.PlainDate.prototype.with");
}

// #sec-temporal.plaindate.prototype.tozoneddatetime
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalPlainDate::ToZonedDateTime(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date,
    DirectHandle<Object> item_obj) {
  const char* method_name = "Temporal.PlainDate.prototype.toZonedDateTime";
  Factory* factory = isolate->factory();
  // 1. Let temporalDate be the this value.
  // 2. Perform ? RequireInternalSlot(temporalDate,
  // [[InitializedTemporalDate]]).
  // 3. If Type(item) is Object, then
  DirectHandle<JSReceiver> time_zone;
  DirectHandle<Object> temporal_time_obj;
  if (IsJSReceiver(*item_obj)) {
    DirectHandle<JSReceiver> item = Cast<JSReceiver>(item_obj);
    // a. Let timeZoneLike be ? Get(item, "timeZone").
    DirectHandle<Object> time_zone_like;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, time_zone_like,
        JSReceiver::GetProperty(isolate, item, factory->timeZone_string()));
    // b. If timeZoneLike is undefined, then
    if (IsUndefined(*time_zone_like)) {
      // i. Let timeZone be ? ToTemporalTimeZone(item).
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, time_zone,
          temporal::ToTemporalTimeZone(isolate, item, method_name));
      // ii. Let temporalTime be undefined.
      temporal_time_obj = factory->undefined_value();
      // c. Else,
    } else {
      // i. Let timeZone be ? ToTemporalTimeZone(timeZoneLike).
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, time_zone,
          temporal::ToTemporalTimeZone(isolate, time_zone_like, method_name));
      // ii. Let temporalTime be ? Get(item, "plainTime").
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, temporal_time_obj,
          JSReceiver::GetProperty(isolate, item, factory->plainTime_string()));
    }
    // 4. Else,
  } else {
    // a. Let timeZone be ? ToTemporalTimeZone(item).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, time_zone,
        temporal::ToTemporalTimeZone(isolate, item_obj, method_name));
    // b. Let temporalTime be undefined.
    temporal_time_obj = factory->undefined_value();
  }
  // 5. If temporalTime is undefined, then
  DirectHandle<JSTemporalPlainDateTime> temporal_date_time;
  DirectHandle<JSReceiver> calendar(temporal_date->calendar(), isolate);
  if (IsUndefined(*temporal_time_obj)) {
    // a. Let temporalDateTime be ?
    // CreateTemporalDateTime(temporalDate.[[ISOYear]],
    // temporalDate.[[ISOMonth]], temporalDate.[[ISODay]], 0, 0, 0, 0, 0, 0,
    // temporalDate.[[Calendar]]).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, temporal_date_time,
        temporal::CreateTemporalDateTime(
            isolate,
            {{temporal_date->iso_year(), temporal_date->iso_month(),
              temporal_date->iso_day()},
             {0, 0, 0, 0, 0, 0}},
            calendar));
    // 6. Else,
  } else {
    DirectHandle<JSTemporalPlainTime> temporal_time;
    // a. Set temporalTime to ? ToTemporalTime(temporalTime).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, temporal_time,
        temporal::ToTemporalTime(isolate, temporal_time_obj, method_name));
    // b. Let temporalDateTime be ?
    // CreateTemporalDateTime(temporalDate.[[ISOYear]],
    // temporalDate.[[ISOMonth]], temporalDate.[[ISODay]],
    // temporalTime.[[ISOHour]], temporalTime.[[ISOMinute]],
    // temporalTime.[[ISOSecond]], temporalTime.[[ISOMillisecond]],
    // temporalTime.[[ISOMicrosecond]], temporalTime.[[ISONanosecond]],
    // temporalDate.[[Calendar]]).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, temporal_date_time,
        temporal::CreateTemporalDateTime(
            isolate,
            {{temporal_date->iso_year(), temporal_date->iso_month(),
              temporal_date->iso_day()},
             {temporal_time->iso_hour(), temporal_time->iso_minute(),
              temporal_time->iso_second(), temporal_time->iso_millisecond(),
              temporal_time->iso_microsecond(),
              temporal_time->iso_nanosecond()}},
            calendar));
  }
  // 7. Let instant be ? BuiltinTimeZoneGetInstantFor(timeZone,
  // temporalDateTime, "compatible").
  DirectHandle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant,
      BuiltinTimeZoneGetInstantFor(isolate, time_zone, temporal_date_time,
                                   Disambiguation::kCompatible, method_name));
  // 8. Return ? CreateTemporalZonedDateTime(instant.[[Nanoseconds]], timeZone,
  // temporalDate.[[Calendar]]).
  return CreateTemporalZonedDateTime(
      isolate, direct_handle(instant->nanoseconds(), isolate), time_zone,
      calendar);
}

// #sec-temporal.plaindate.prototype.add
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainDate::Add(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date,
    DirectHandle<Object> temporal_duration_like,
    DirectHandle<Object> options_obj) {
  const char* method_name = "Temporal.PlainDate.prototype.add";
  // 1. Let temporalDate be the this value.
  // 2. Perform ? RequireInternalSlot(temporalDate,
  // [[InitializedTemporalDate]]).
  // 3. Let duration be ? ToTemporalDuration(temporalDurationLike).
  DirectHandle<JSTemporalDuration> duration;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, duration,
                             temporal::ToTemporalDuration(
                                 isolate, temporal_duration_like, method_name));

  // 4. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));
  // 5. Return ? CalendarDateAdd(temporalDate.[[Calendar]], temporalDate,
  // duration, options).
  return CalendarDateAdd(isolate,
                         direct_handle(temporal_date->calendar(), isolate),
                         temporal_date, duration, options);
}

// #sec-temporal.plaindate.prototype.subtract
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainDate::Subtract(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date,
    DirectHandle<Object> temporal_duration_like,
    DirectHandle<Object> options_obj) {
  const char* method_name = "Temporal.PlainDate.prototype.subtract";
  // 1. Let temporalDate be the this value.
  // 2. Perform ? RequireInternalSlot(temporalDate,
  // [[InitializedTemporalDate]]).
  // 3. Let duration be ? ToTemporalDuration(temporalDurationLike).
  DirectHandle<JSTemporalDuration> duration;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, duration,
                             temporal::ToTemporalDuration(
                                 isolate, temporal_duration_like, method_name));

  // 4. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));

  // 5. Let negatedDuration be ! CreateNegatedTemporalDuration(duration).
  DirectHandle<JSTemporalDuration> negated_duration =
      CreateNegatedTemporalDuration(isolate, duration).ToHandleChecked();

  // 6. Return ? CalendarDateAdd(temporalDate.[[Calendar]], temporalDate,
  // negatedDuration, options).
  return CalendarDateAdd(isolate,
                         direct_handle(temporal_date->calendar(), isolate),
                         temporal_date, negated_duration, options);
}

namespace {
// #sec-temporal-differencetemporalplandate
MaybeDirectHandle<JSTemporalDuration> DifferenceTemporalPlainDate(
    Isolate* isolate, TimePreposition operation,
    DirectHandle<JSTemporalPlainDate> temporal_date,
    DirectHandle<Object> other_obj, DirectHandle<Object> options,
    const char* method_name) {
  TEMPORAL_ENTER_FUNC();
  // 1. If operation is since, let sign be -1. Otherwise, let sign be 1.
  double sign = operation == TimePreposition::kSince ? -1 : 1;
  // 2. Set other to ? ToTemporalDate(other).
  DirectHandle<JSTemporalPlainDate> other;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, other,
                             ToTemporalDate(isolate, other_obj, method_name));
  // 3. If ? CalendarEquals(temporalDate.[[Calendar]], other.[[Calendar]]) is
  // false, throw a RangeError exception.
  bool calendar_equals;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, calendar_equals,
      CalendarEqualsBool(isolate,
                         direct_handle(temporal_date->calendar(), isolate),
                         direct_handle(other->calendar(), isolate)),
      DirectHandle<JSTemporalDuration>());
  if (!calendar_equals) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }

  // 4. Let settings be ? GetDifferenceSettings(operation, options, date, « »,
  // "day", "day").
  DifferenceSettings settings;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, settings,
      GetDifferenceSettings(isolate, operation, options, UnitGroup::kDate,
                            DisallowedUnitsInDifferenceSettings::kNone,
                            Unit::kDay, Unit::kDay, method_name),
      DirectHandle<JSTemporalDuration>());
  // 5. Let untilOptions be ? MergeLargestUnitOption(settings.[[Options]],
  // settings.[[LargestUnit]]).
  DirectHandle<JSReceiver> until_options;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, until_options,
      MergeLargestUnitOption(isolate, settings.options, settings.largest_unit),
      DirectHandle<JSTemporalDuration>());
  // 6. Let result be ? CalendarDateUntil(temporalDate.[[Calendar]],
  // temporalDate, other, untilOptions).
  DirectHandle<JSTemporalDuration> result;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result,
      CalendarDateUntil(isolate,
                        direct_handle(temporal_date->calendar(), isolate),
                        temporal_date, other, until_options),
      DirectHandle<JSTemporalDuration>());
  // 7. If settings.[[SmallestUnit]] is not "day" or
  // settings.[[RoundingIncrement]] ≠ 1, then
  if (settings.smallest_unit != Unit::kDay ||
      settings.rounding_increment != 1) {
    // a. Set result to (? RoundDuration(result.[[Years]], result.[[Months]],
    // result.[[Weeks]], result.[[Days]], 0, 0, 0, 0, 0, 0,
    // settings.[[RoundingIncrement]], settings.[[SmallestUnit]],
    // settings.[[RoundingMode]], temporalDate)).[[DurationRecord]].
    DurationRecordWithRemainder round_result;
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, round_result,
        RoundDuration(isolate,
                      {Object::NumberValue(result->years()),
                       Object::NumberValue(result->months()),
                       Object::NumberValue(result->weeks()),
                       {Object::NumberValue(result->days()), 0, 0, 0, 0, 0, 0}},
                      settings.rounding_increment, settings.smallest_unit,
                      settings.rounding_mode, temporal_date, method_name),
        DirectHandle<JSTemporalDuration>());
    // 8. Return ! CreateTemporalDuration(sign × result.[[Years]], sign ×
    // result.[[Months]], sign × result.[[Weeks]], sign × result.[[Days]], 0, 0,
    // 0, 0, 0, 0).
    round_result.record.years *= sign;
    round_result.record.months *= sign;
    round_result.record.weeks *= sign;
    round_result.record.time_duration.days *= sign;
    round_result.record.time_duration.hours =
        round_result.record.time_duration.minutes =
            round_result.record.time_duration.seconds =
                round_result.record.time_duration.milliseconds =
                    round_result.record.time_duration.microseconds =
                        round_result.record.time_duration.nanoseconds = 0;
    return CreateTemporalDuration(isolate, round_result.record)
        .ToHandleChecked();
  }
  // 8. Return ! CreateTemporalDuration(sign × result.[[Years]], sign ×
  // result.[[Months]], sign × result.[[Weeks]], sign × result.[[Days]], 0, 0,
  // 0, 0, 0, 0).
  return CreateTemporalDuration(
             isolate,
             {sign * Object::NumberValue(result->years()),
              sign * Object::NumberValue(result->months()),
              sign * Object::NumberValue(result->weeks()),
              {sign * Object::NumberValue(result->days()), 0, 0, 0, 0, 0, 0}})
      .ToHandleChecked();
}

}  // namespace

// #sec-temporal.plaindate.prototype.until
MaybeDirectHandle<JSTemporalDuration> JSTemporalPlainDate::Until(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  return DifferenceTemporalPlainDate(isolate, TimePreposition::kUntil, handle,
                                     other, options,
                                     "Temporal.PlainDate.prototype.until");
}

// #sec-temporal.plaindate.prototype.since
MaybeDirectHandle<JSTemporalDuration> JSTemporalPlainDate::Since(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  return DifferenceTemporalPlainDate(isolate, TimePreposition::kSince, handle,
                                     other, options,
                                     "Temporal.PlainDate.prototype.since");
}

// #sec-temporal.now.plaindate
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainDate::Now(
    Isolate* isolate, DirectHandle<Object> calendar_like,
    DirectHandle<Object> temporal_time_zone_like) {
  const char* method_name = "Temporal.Now.plainDate";
  // 1. Let dateTime be ? SystemDateTime(temporalTimeZoneLike, calendarLike).
  DirectHandle<JSTemporalPlainDateTime> date_time;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, date_time,
                             SystemDateTime(isolate, temporal_time_zone_like,
                                            calendar_like, method_name));
  // 2. Return ! CreateTemporalDate(dateTime.[[ISOYear]], dateTime.[[ISOMonth]],
  // dateTime.[[ISODay]], dateTime.[[Calendar]]).
  return CreateTemporalDate(
             isolate,
             {date_time->iso_year(), date_time->iso_month(),
              date_time->iso_day()},
             DirectHandle<JSReceiver>(date_time->calendar(), isolate))
      .ToHandleChecked();
}

// #sec-temporal.now.plaindateiso
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainDate::NowISO(
    Isolate* isolate, DirectHandle<Object> temporal_time_zone_like) {
  const char* method_name = "Temporal.Now.plainDateISO";
  // 1. Let calendar be ! GetISO8601Calendar().
  DirectHandle<JSReceiver> calendar = temporal::GetISO8601Calendar(isolate);
  // 2. Let dateTime be ? SystemDateTime(temporalTimeZoneLike, calendar).
  DirectHandle<JSTemporalPlainDateTime> date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, date_time,
      SystemDateTime(isolate, temporal_time_zone_like, calendar, method_name));
  // 3. Return ! CreateTemporalDate(dateTime.[[ISOYear]], dateTime.[[ISOMonth]],
  // dateTime.[[ISODay]], dateTime.[[Calendar]]).
  return CreateTemporalDate(
             isolate,
             {date_time->iso_year(), date_time->iso_month(),
              date_time->iso_day()},
             DirectHandle<JSReceiver>(date_time->calendar(), isolate))
      .ToHandleChecked();
}

// #sec-temporal.plaindate.from
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainDate::From(
    Isolate* isolate, DirectHandle<Object> item,
    DirectHandle<Object> options_obj) {
  const char* method_name = "Temporal.PlainDate.from";
  // 1. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));
  // 2. If Type(item) is Object and item has an [[InitializedTemporalDate]]
  // internal slot, then
  if (IsJSTemporalPlainDate(*item)) {
    // a. Perform ? ToTemporalOverflow(options).
    MAYBE_RETURN_ON_EXCEPTION_VALUE(
        isolate, ToTemporalOverflow(isolate, options, method_name),
        DirectHandle<JSTemporalPlainDate>());
    // b. Return ? CreateTemporalDate(item.[[ISOYear]], item.[[ISOMonth]],
    // item.[[ISODay]], item.[[Calendar]]).
    auto date = Cast<JSTemporalPlainDate>(item);
    return CreateTemporalDate(
        isolate, {date->iso_year(), date->iso_month(), date->iso_day()},
        DirectHandle<JSReceiver>(date->calendar(), isolate));
  }
  // 3. Return ? ToTemporalDate(item, options).
  return ToTemporalDate(isolate, item, options, method_name);
}

#define DEFINE_INT_FIELD(obj, str, field, item)                      \
  CHECK(JSReceiver::CreateDataProperty(                              \
            isolate, obj, factory->str##_string(),                   \
            DirectHandle<Smi>(Smi::FromInt(item->field()), isolate), \
            Just(kThrowOnError))                                     \
            .FromJust());

// #sec-temporal.plaindate.prototype.getisofields
MaybeDirectHandle<JSReceiver> JSTemporalPlainDate::GetISOFields(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date) {
  Factory* factory = isolate->factory();
  // 1. Let temporalDate be the this value.
  // 2. Perform ? RequireInternalSlot(temporalDate,
  // [[InitializedTemporalDate]]).
  // 3. Let fields be ! OrdinaryObjectCreate(%Object.prototype%).
  DirectHandle<JSObject> fields =
      isolate->factory()->NewJSObject(isolate->object_function());
  // 4. Perform ! CreateDataPropertyOrThrow(fields, "calendar",
  // temporalDate.[[Calendar]]).
  CHECK(JSReceiver::CreateDataProperty(
            isolate, fields, factory->calendar_string(),
            DirectHandle<JSReceiver>(temporal_date->calendar(), isolate),
            Just(kThrowOnError))
            .FromJust());
  // 5. Perform ! CreateDataPropertyOrThrow(fields, "isoDay",
  // 𝔽(temporalDate.[[ISODay]])).
  // 6. Perform ! CreateDataPropertyOrThrow(fields, "isoMonth",
  // 𝔽(temporalDate.[[ISOMonth]])).
  // 7. Perform ! CreateDataPropertyOrThrow(fields, "isoYear",
  // 𝔽(temporalDate.[[ISOYear]])).
  DEFINE_INT_FIELD(fields, isoDay, iso_day, temporal_date)
  DEFINE_INT_FIELD(fields, isoMonth, iso_month, temporal_date)
  DEFINE_INT_FIELD(fields, isoYear, iso_year, temporal_date)
  // 8. Return fields.
  return fields;
}

// #sec-temporal.plaindate.prototype.tojson
MaybeDirectHandle<String> JSTemporalPlainDate::ToJSON(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date) {
  // 1. Let temporalDate be the this value.
  // 2. Perform ? RequireInternalSlot(temporalDate,
  // [[InitializedTemporalDate]]).
  // 3. Return ? TemporalDateToString(temporalDate, "auto").
  return TemporalDateToString(isolate, temporal_date, ShowCalendar::kAuto);
}

namespace {

// #sec-temporal-toshowcalendaroption
Maybe<ShowCalendar> ToShowCalendarOption(Isolate* isolate,
                                         DirectHandle<JSReceiver> options,
                                         const char* method_name) {
  // 1. Return ? GetOption(normalizedOptions, "calendarName", « String », «
  // "auto", "always", "never" », "auto").
  return GetStringOption<ShowCalendar>(
      isolate, options, "calendarName", method_name,
      {"auto", "always", "never"},
      {ShowCalendar::kAuto, ShowCalendar::kAlways, ShowCalendar::kNever},
      ShowCalendar::kAuto);
}

template <typename T, MaybeDirectHandle<String> (*F)(Isolate*, DirectHandle<T>,
                                                     ShowCalendar)>
MaybeDirectHandle<String> TemporalToString(Isolate* isolate,
                                           DirectHandle<T> temporal,
                                           DirectHandle<Object> options_obj,
                                           const char* method_name) {
  // 1. Let temporalDate be the this value.
  // 2. Perform ? RequireInternalSlot(temporalDate,
  // [[InitializedTemporalDate]]).
  // 3. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));
  // 4. Let showCalendar be ? ToShowCalendarOption(options).
  ShowCalendar show_calendar;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, show_calendar,
      ToShowCalendarOption(isolate, options, method_name),
      DirectHandle<String>());
  // 5. Return ? TemporalDateToString(temporalDate, showCalendar).
  return F(isolate, temporal, show_calendar);
}
}  // namespace

// #sec-temporal.plaindate.prototype.tostring
MaybeDirectHandle<String> JSTemporalPlainDate::ToString(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date,
    DirectHandle<Object> options) {
  return TemporalToString<JSTemporalPlainDate, TemporalDateToString>(
      isolate, temporal_date, options, "Temporal.PlainDate.prototype.toString");
}

// #sup-temporal.plaindate.prototype.tolocalestring
MaybeDirectHandle<String> JSTemporalPlainDate::ToLocaleString(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date,
    DirectHandle<Object> locales, DirectHandle<Object> options) {
#ifdef V8_INTL_SUPPORT
  return JSDateTimeFormat::TemporalToLocaleString(
      isolate, temporal_date, locales, options,
      "Temporal.PlainDate.prototype.toLocaleString");
#else   //  V8_INTL_SUPPORT
  return TemporalDateToString(isolate, temporal_date, ShowCalendar::kAuto);
#endif  // V8_INTL_SUPPORT
}

// #sec-temporal-createtemporaldatetime
MaybeDirectHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::Constructor(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, DirectHandle<Object> iso_year_obj,
    DirectHandle<Object> iso_month_obj, DirectHandle<Object> iso_day_obj,
    DirectHandle<Object> hour_obj, DirectHandle<Object> minute_obj,
    DirectHandle<Object> second_obj, DirectHandle<Object> millisecond_obj,
    DirectHandle<Object> microsecond_obj, DirectHandle<Object> nanosecond_obj,
    DirectHandle<Object> calendar_like) {
  const char* method_name = "Temporal.PlainDateTime";
  // 1. If NewTarget is undefined, throw a TypeError exception.
  if (IsUndefined(*new_target)) {
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                                 isolate->factory()->NewStringFromAsciiChecked(
                                     method_name)));
  }

  TO_INT_THROW_ON_INFTY(iso_year, JSTemporalPlainDateTime);
  TO_INT_THROW_ON_INFTY(iso_month, JSTemporalPlainDateTime);
  TO_INT_THROW_ON_INFTY(iso_day, JSTemporalPlainDateTime);
  TO_INT_THROW_ON_INFTY(hour, JSTemporalPlainDateTime);
  TO_INT_THROW_ON_INFTY(minute, JSTemporalPlainDateTime);
  TO_INT_THROW_ON_INFTY(second, JSTemporalPlainDateTime);
  TO_INT_THROW_ON_INFTY(millisecond, JSTemporalPlainDateTime);
  TO_INT_THROW_ON_INFTY(microsecond, JSTemporalPlainDateTime);
  TO_INT_THROW_ON_INFTY(nanosecond, JSTemporalPlainDateTime);

  // 20. Let calendar be ? ToTemporalCalendarWithISODefault(calendarLike).
  DirectHandle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar,
      ToTemporalCalendarWithISODefault(isolate, calendar_like, method_name));

  // 21. Return ? CreateTemporalDateTime(isoYear, isoMonth, isoDay, hour,
  // minute, second, millisecond, microsecond, nanosecond, calendar, NewTarget).
  return CreateTemporalDateTime(
      isolate, target, new_target,
      {{iso_year, iso_month, iso_day},
       {hour, minute, second, millisecond, microsecond, nanosecond}},
      calendar);
}

namespace {

// #sec-temporal-interprettemporaldatetimefields
Maybe<temporal::DateTimeRecord> InterpretTemporalDateTimeFields(
    Isolate* isolate, DirectHandle<JSReceiver> calendar,
    DirectHandle<JSReceiver> fields, DirectHandle<Object> options,
    const char* method_name) {
  TEMPORAL_ENTER_FUNC();

  // 1. Let timeResult be ? ToTemporalTimeRecord(fields).
  TimeRecord time_result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, time_result, ToTemporalTimeRecord(isolate, fields, method_name),
      Nothing<temporal::DateTimeRecord>());

  // 2. Let temporalDate be ? DateFromFields(calendar, fields, options).
  DirectHandle<JSTemporalPlainDate> temporal_date;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, temporal_date,
      DateFromFields(isolate, calendar, fields, options),
      Nothing<temporal::DateTimeRecord>());

  // 3. Let overflow be ? ToTemporalOverflow(options).
  ShowOverflow overflow;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, overflow, ToTemporalOverflow(isolate, options, method_name),
      Nothing<temporal::DateTimeRecord>());

  // 4. Let timeResult be ? RegulateTime(timeResult.[[Hour]],
  // timeResult.[[Minute]], timeResult.[[Second]], timeResult.[[Millisecond]],
  // timeResult.[[Microsecond]], timeResult.[[Nanosecond]], overflow).
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, time_result,
      temporal::RegulateTime(isolate, time_result, overflow),
      Nothing<temporal::DateTimeRecord>());
  // 5. Return the new Record { [[Year]]: temporalDate.[[ISOYear]], [[Month]]:
  // temporalDate.[[ISOMonth]], [[Day]]: temporalDate.[[ISODay]], [[Hour]]:
  // timeResult.[[Hour]], [[Minute]]: timeResult.[[Minute]], [[Second]]:
  // timeResult.[[Second]], [[Millisecond]]: timeResult.[[Millisecond]],
  // [[Microsecond]]: timeResult.[[Microsecond]], [[Nanosecond]]:
  // timeResult.[[Nanosecond]] }.

  temporal::DateTimeRecord result = {
      {temporal_date->iso_year(), temporal_date->iso_month(),
       temporal_date->iso_day()},
      time_result};
  return Just(result);
}

// #sec-temporal-parsetemporaldatetimestring
Maybe<DateTimeRecordWithCalendar> ParseTemporalDateTimeString(
    Isolate* isolate, DirectHandle<String> iso_string) {
  TEMPORAL_ENTER_FUNC();
  // 1. Assert: Type(isoString) is String.
  // 2. If isoString does not satisfy the syntax of a TemporalDateTimeString
  // (see 13.33), then
  std::optional<ParsedISO8601Result> parsed =
      TemporalParser::ParseTemporalDateTimeString(isolate, iso_string);
  if (!parsed.has_value()) {
    // a. Throw a *RangeError* exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<DateTimeRecordWithCalendar>());
  }

  // 3. If _isoString_ contains a |UTCDesignator|, then
  if (parsed->utc_designator) {
    // a. Throw a *RangeError* exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<DateTimeRecordWithCalendar>());
  }

  // 3. Let result be ? ParseISODateTime(isoString).
  // 4. Return result.
  return ParseISODateTime(isolate, iso_string, *parsed);
}

// #sec-temporal-totemporaldatetime
MaybeDirectHandle<JSTemporalPlainDateTime> ToTemporalDateTime(
    Isolate* isolate, DirectHandle<Object> item_obj,
    DirectHandle<Object> options, const char* method_name) {
  TEMPORAL_ENTER_FUNC();
  // 2. Assert: Type(options) is Object or Undefined.
  DCHECK(IsJSReceiver(*options) || IsUndefined(*options));

  DirectHandle<JSReceiver> calendar;
  temporal::DateTimeRecord result;
  // 2. If Type(item) is Object, then
  if (IsJSReceiver(*item_obj)) {
    DirectHandle<JSReceiver> item = Cast<JSReceiver>(item_obj);
    // a. If item has an [[InitializedTemporalDateTime]] internal slot, then
    // i. Return item.
    if (IsJSTemporalPlainDateTime(*item)) {
      return Cast<JSTemporalPlainDateTime>(item);
    }
    // b. If item has an [[InitializedTemporalZonedDateTime]] internal slot,
    // then
    if (IsJSTemporalZonedDateTime(*item)) {
      // i. Perform ? ToTemporalOverflow(options).
      MAYBE_RETURN_ON_EXCEPTION_VALUE(
          isolate, ToTemporalOverflow(isolate, options, method_name),
          DirectHandle<JSTemporalPlainDateTime>());
      // ii. Let instant be ! CreateTemporalInstant(item.[[Nanoseconds]]).
      auto zoned_date_time = Cast<JSTemporalZonedDateTime>(item);
      DirectHandle<JSTemporalInstant> instant =
          temporal::CreateTemporalInstant(
              isolate, direct_handle(zoned_date_time->nanoseconds(), isolate))
              .ToHandleChecked();
      // iii. Return ?
      // temporal::BuiltinTimeZoneGetPlainDateTimeFor(item.[[TimeZone]],
      // instant, item.[[Calendar]]).
      return temporal::BuiltinTimeZoneGetPlainDateTimeFor(
          isolate, direct_handle(zoned_date_time->time_zone(), isolate),
          instant, direct_handle(zoned_date_time->calendar(), isolate),
          method_name);
    }
    // c. If item has an [[InitializedTemporalDate]] internal slot, then
    if (IsJSTemporalPlainDate(*item)) {
      // i. Perform ? ToTemporalOverflow(options).
      MAYBE_RETURN_ON_EXCEPTION_VALUE(
          isolate, ToTemporalOverflow(isolate, options, method_name),
          DirectHandle<JSTemporalPlainDateTime>());
      // ii. Return ? CreateTemporalDateTime(item.[[ISOYear]],
      // item.[[ISOMonth]], item.[[ISODay]], 0, 0, 0, 0, 0, 0,
      // item.[[Calendar]]).
      auto date = Cast<JSTemporalPlainDate>(item);
      return temporal::CreateTemporalDateTime(
          isolate,
          {{date->iso_year(), date->iso_month(), date->iso_day()},
           {0, 0, 0, 0, 0, 0}},
          direct_handle(date->calendar(), isolate));
    }
    // d. Let calendar be ? GetTemporalCalendarWithISODefault(item).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, calendar,
        GetTemporalCalendarWithISODefault(isolate, item, method_name));
    // e. Let fieldNames be ? CalendarFields(calendar, « "day", "hour",
    // "microsecond", "millisecond", "minute", "month", "monthCode",
    // "nanosecond", "second", "year" »).
    DirectHandle<FixedArray> field_names = All10UnitsInFixedArray(isolate);
    ASSIGN_RETURN_ON_EXCEPTION(isolate, field_names,
                               CalendarFields(isolate, calendar, field_names));
    // f. Let fields be ? PrepareTemporalFields(item,
    // PrepareTemporalFields(item, fieldNames, «»).
    DirectHandle<JSReceiver> fields;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, fields,
                               PrepareTemporalFields(isolate, item, field_names,
                                                     RequiredFields::kNone));
    // g. Let result be ?
    // InterpretTemporalDateTimeFields(calendar, fields, options).
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, result,
        InterpretTemporalDateTimeFields(isolate, calendar, fields, options,
                                        method_name),
        DirectHandle<JSTemporalPlainDateTime>());
  } else {
    // 3. Else,
    // a. Perform ? ToTemporalOverflow(options).
    MAYBE_RETURN_ON_EXCEPTION_VALUE(
        isolate, ToTemporalOverflow(isolate, options, method_name),
        DirectHandle<JSTemporalPlainDateTime>());

    // b. Let string be ? ToString(item).
    DirectHandle<String> string;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, string,
                               Object::ToString(isolate, item_obj));
    // c. Let result be ? ParseTemporalDateTimeString(string).
    DateTimeRecordWithCalendar parsed_result;
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, parsed_result, ParseTemporalDateTimeString(isolate, string),
        DirectHandle<JSTemporalPlainDateTime>());
    result = {parsed_result.date, parsed_result.time};
    // d. Assert: ! IsValidISODate(result.[[Year]], result.[[Month]],
    // result.[[Day]]) is true.
    DCHECK(IsValidISODate(isolate, result.date));
    // e. Assert: ! IsValidTime(result.[[Hour]],
    // result.[[Minute]], result.[[Second]], result.[[Millisecond]],
    // result.[[Microsecond]], result.[[Nanosecond]]) is true.
    DCHECK(IsValidTime(isolate, result.time));
    // f. Let calendar
    // be ? ToTemporalCalendarWithISODefault(result.[[Calendar]]).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, calendar,
        ToTemporalCalendarWithISODefault(isolate, parsed_result.calendar,
                                         method_name));
  }
  // 4. Return ? CreateTemporalDateTime(result.[[Year]], result.[[Month]],
  // result.[[Day]], result.[[Hour]], result.[[Minute]], result.[[Second]],
  // result.[[Millisecond]], result.[[Microsecond]], result.[[Nanosecond]],
  // calendar).
  return temporal::CreateTemporalDateTime(isolate, {result.date, result.time},
                                          calendar);
}

}  // namespace

// #sec-temporal.plaindatetime.from
MaybeDirectHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::From(
    Isolate* isolate, DirectHandle<Object> item,
    DirectHandle<Object> options_obj) {
  const char* method_name = "Temporal.PlainDateTime.from";
  // 1. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));
  // 2. If Type(item) is Object and item has an [[InitializedTemporalDateTime]]
  // internal slot, then
  if (IsJSTemporalPlainDateTime(*item)) {
    // a. Perform ? ToTemporalOverflow(options).
    MAYBE_RETURN_ON_EXCEPTION_VALUE(
        isolate, ToTemporalOverflow(isolate, options, method_name),
        DirectHandle<JSTemporalPlainDateTime>());
    // b. Return ? CreateTemporalDateTime(item.[[ISYear]], item.[[ISOMonth]],
    // item.[[ISODay]], item.[[ISOHour]], item.[[ISOMinute]],
    // item.[[ISOSecond]], item.[[ISOMillisecond]], item.[[ISOMicrosecond]],
    // item.[[ISONanosecond]], item.[[Calendar]]).
    auto date_time = Cast<JSTemporalPlainDateTime>(item);
    return temporal::CreateTemporalDateTime(
        isolate,
        {{date_time->iso_year(), date_time->iso_month(), date_time->iso_day()},
         {date_time->iso_hour(), date_time->iso_minute(),
          date_time->iso_second(), date_time->iso_millisecond(),
          date_time->iso_microsecond(), date_time->iso_nanosecond()}},
        direct_handle(date_time->calendar(), isolate));
  }
  // 3. Return ? ToTemporalDateTime(item, options).
  return ToTemporalDateTime(isolate, item, options, method_name);
}

// #sec-temporal.plaindatetime.compare
MaybeDirectHandle<Smi> JSTemporalPlainDateTime::Compare(
    Isolate* isolate, DirectHandle<Object> one_obj,
    DirectHandle<Object> two_obj) {
  const char* method_name = "Temporal.PlainDateTime.compare";
  // 1. Set one to ? ToTemporalDateTime(one).
  DirectHandle<JSTemporalPlainDateTime> one;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, one,
                             ToTemporalDateTime(isolate, one_obj, method_name));
  // 2. Set two to ? ToTemporalDateTime(two).
  DirectHandle<JSTemporalPlainDateTime> two;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, two,
                             ToTemporalDateTime(isolate, two_obj, method_name));
  // 3. Return 𝔽(! CompareISODateTime(one.[[ISOYear]], one.[[ISOMonth]],
  // one.[[ISODay]], one.[[ISOHour]], one.[[ISOMinute]], one.[[ISOSecond]],
  // one.[[ISOMillisecond]], one.[[ISOMicrosecond]], one.[[ISONanosecond]],
  // two.[[ISOYear]], two.[[ISOMonth]], two.[[ISODay]], two.[[ISOHour]],
  // two.[[ISOMinute]], two.[[ISOSecond]], two.[[ISOMillisecond]],
  // two.[[ISOMicrosecond]], two.[[ISONanosecond]])).
  return DirectHandle<Smi>(
      Smi::FromInt(CompareISODateTime(
          {
              {one->iso_year(), one->iso_month(), one->iso_day()},
              {one->iso_hour(), one->iso_minute(), one->iso_second(),
               one->iso_millisecond(), one->iso_microsecond(),
               one->iso_nanosecond()},
          },
          {
              {two->iso_year(), two->iso_month(), two->iso_day()},
              {two->iso_hour(), two->iso_minute(), two->iso_second(),
               two->iso_millisecond(), two->iso_microsecond(),
               two->iso_nanosecond()},
          })),
      isolate);
}

// #sec-temporal.plaindatetime.prototype.equals
MaybeDirectHandle<Oddball> JSTemporalPlainDateTime::Equals(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> other_obj) {
  // 1. Let dateTime be the this value.
  // 2. Perform ? RequireInternalSlot(dateTime,
  // [[InitializedTemporalDateTime]]).
  // 3. Set other to ? ToTemporalDateTime(other).
  DirectHandle<JSTemporalPlainDateTime> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other,
      ToTemporalDateTime(isolate, other_obj,
                         "Temporal.PlainDateTime.prototype.equals"));
  // 4. Let result be ! CompareISODateTime(dateTime.[[ISOYear]],
  // dateTime.[[ISOMonth]], dateTime.[[ISODay]], dateTime.[[ISOHour]],
  // dateTime.[[ISOMinute]], dateTime.[[ISOSecond]],
  // dateTime.[[ISOMillisecond]], dateTime.[[ISOMicrosecond]],
  // dateTime.[[ISONanosecond]], other.[[ISOYear]], other.[[ISOMonth]],
  // other.[[ISODay]], other.[[ISOHour]], other.[[ISOMinute]],
  // other.[[ISOSecond]], other.[[ISOMillisecond]], other.[[ISOMicrosecond]],
  // other.[[ISONanosecond]]).
  int32_t result = CompareISODateTime(
      {
          {date_time->iso_year(), date_time->iso_month(), date_time->iso_day()},
          {date_time->iso_hour(), date_time->iso_minute(),
           date_time->iso_second(), date_time->iso_millisecond(),
           date_time->iso_microsecond(), date_time->iso_nanosecond()},
      },
      {
          {other->iso_year(), other->iso_month(), other->iso_day()},
          {other->iso_hour(), other->iso_minute(), other->iso_second(),
           other->iso_millisecond(), other->iso_microsecond(),
           other->iso_nanosecond()},
      });
  // 5. If result is not 0, return false.
  if (result != 0) return isolate->factory()->false_value();
  // 6. Return ? CalendarEquals(dateTime.[[Calendar]], other.[[Calendar]]).
  return CalendarEquals(isolate, direct_handle(date_time->calendar(), isolate),
                        direct_handle(other->calendar(), isolate));
}

// #sec-temporal.plaindatetime.prototype.with
MaybeDirectHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::With(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> temporal_date_time_like_obj,
    DirectHandle<Object> options_obj) {
  const char* method_name = "Temporal.PlainDateTime.prototype.with";
  // 1. Let dateTime be the this value.
  // 2. Perform ? RequireInternalSlot(dateTime,
  // [[InitializedTemporalDateTime]]).
  // 3. If Type(temporalDateTimeLike) is not Object, then
  if (!IsJSReceiver(*temporal_date_time_like_obj)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }
  DirectHandle<JSReceiver> temporal_date_time_like =
      Cast<JSReceiver>(temporal_date_time_like_obj);
  // 4. Perform ? RejectObjectWithCalendarOrTimeZone(temporalTimeLike).
  MAYBE_RETURN(
      RejectObjectWithCalendarOrTimeZone(isolate, temporal_date_time_like),
      DirectHandle<JSTemporalPlainDateTime>());
  // 5. Let calendar be dateTime.[[Calendar]].
  DirectHandle<JSReceiver> calendar(date_time->calendar(), isolate);
  // 6. Let fieldNames be ? CalendarFields(calendar, « "day", "hour",
  // "microsecond", "millisecond", "minute", "month", "monthCode", "nanosecond",
  // "second", "year" »).
  DirectHandle<FixedArray> field_names = All10UnitsInFixedArray(isolate);
  ASSIGN_RETURN_ON_EXCEPTION(isolate, field_names,
                             CalendarFields(isolate, calendar, field_names));

  // 7. Let partialDateTime be ?
  // PreparePartialTemporalFields(temporalDateTimeLike, fieldNames).
  DirectHandle<JSReceiver> partial_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, partial_date_time,
      PreparePartialTemporalFields(isolate, temporal_date_time_like,
                                   field_names));

  // 8. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));
  // 9. Let fields be ? PrepareTemporalFields(dateTime, fieldNames, «»).
  DirectHandle<JSReceiver> fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, fields,
      PrepareTemporalFields(isolate, date_time, field_names,
                            RequiredFields::kNone));

  // 10. Set fields to ? CalendarMergeFields(calendar, fields, partialDateTime).
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, fields,
      CalendarMergeFields(isolate, calendar, fields, partial_date_time));
  // 11. Set fields to ? PrepareTemporalFields(fields, fieldNames, «»).
  ASSIGN_RETURN_ON_EXCEPTION(isolate, fields,
                             PrepareTemporalFields(isolate, fields, field_names,
                                                   RequiredFields::kNone));
  // 12. Let result be ? InterpretTemporalDateTimeFields(calendar, fields,
  // options).
  temporal::DateTimeRecord result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result,
      InterpretTemporalDateTimeFields(isolate, calendar, fields, options,
                                      method_name),
      DirectHandle<JSTemporalPlainDateTime>());
  // 13. Assert: ! IsValidISODate(result.[[Year]], result.[[Month]],
  // result.[[Day]]) is true.
  DCHECK(IsValidISODate(isolate, result.date));
  // 14. Assert: ! IsValidTime(result.[[Hour]], result.[[Minute]],
  // result.[[Second]], result.[[Millisecond]], result.[[Microsecond]],
  // result.[[Nanosecond]]) is true.
  DCHECK(IsValidTime(isolate, result.time));
  // 15. Return ? CreateTemporalDateTime(result.[[Year]], result.[[Month]],
  // result.[[Day]], result.[[Hour]], result.[[Minute]], result.[[Second]],
  // result.[[Millisecond]], result.[[Microsecond]], result.[[Nanosecond]],
  // calendar).
  return temporal::CreateTemporalDateTime(isolate, {result.date, result.time},
                                          calendar);
}

// #sec-temporal.plaindatetime.prototype.withplaintime
MaybeDirectHandle<JSTemporalPlainDateTime>
JSTemporalPlainDateTime::WithPlainTime(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> plain_time_like) {
  // 1. Let temporalDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(temporalDateTime,
  // [[InitializedTemporalDateTime]]).
  // 3. If plainTimeLike is undefined, then
  if (IsUndefined(*plain_time_like)) {
    // a. Return ? CreateTemporalDateTime(temporalDateTime.[[ISOYear]],
    // temporalDateTime.[[ISOMonth]], temporalDateTime.[[ISODay]], 0, 0, 0, 0,
    // 0, 0, temporalDateTime.[[Calendar]]).
    return temporal::CreateTemporalDateTime(
        isolate,
        {{date_time->iso_year(), date_time->iso_month(), date_time->iso_day()},
         {0, 0, 0, 0, 0, 0}},
        direct_handle(date_time->calendar(), isolate));
  }
  DirectHandle<JSTemporalPlainTime> plain_time;
  // 4. Let plainTime be ? ToTemporalTime(plainTimeLike).
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, plain_time,
      temporal::ToTemporalTime(
          isolate, plain_time_like,
          "Temporal.PlainDateTime.prototype.withPlainTime"));
  // 5. Return ? CreateTemporalDateTime(temporalDateTime.[[ISOYear]],
  // temporalDateTime.[[ISOMonth]], temporalDateTime.[[ISODay]],
  // plainTime.[[ISOHour]], plainTime.[[ISOMinute]], plainTime.[[ISOSecond]],
  // plainTime.[[ISOMillisecond]], plainTime.[[ISOMicrosecond]],
  // plainTime.[[ISONanosecond]], temporalDateTime.[[Calendar]]).
  return temporal::CreateTemporalDateTime(
      isolate,
      {{date_time->iso_year(), date_time->iso_month(), date_time->iso_day()},
       {plain_time->iso_hour(), plain_time->iso_minute(),
        plain_time->iso_second(), plain_time->iso_millisecond(),
        plain_time->iso_microsecond(), plain_time->iso_nanosecond()}},
      direct_handle(date_time->calendar(), isolate));
}

// #sec-temporal.plaindatetime.prototype.withcalendar
MaybeDirectHandle<JSTemporalPlainDateTime>
JSTemporalPlainDateTime::WithCalendar(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> calendar_like) {
  // 1. Let temporalDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(temporalDateTime,
  // [[InitializedTemporalDateTime]]).
  // 3. Let calendar be ? ToTemporalCalendar(calendar).
  DirectHandle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar,
      temporal::ToTemporalCalendar(
          isolate, calendar_like,
          "Temporal.PlainDateTime.prototype.withCalendar"));
  // 4. Return ? CreateTemporalDateTime(temporalDateTime.[[ISOYear]],
  // temporalDateTime.[[ISOMonth]], temporalDateTime.[[ISODay]],
  // temporalDateTime.[[ISOHour]], temporalDateTime.[[ISOMinute]],
  // temporalDateTime.[[ISOSecond]], temporalDateTime.[[ISOMillisecond]],
  // temporalDateTime.[[ISOMicrosecond]], temporalDateTime.[[ISONanosecond]],
  // calendar).
  return temporal::CreateTemporalDateTime(
      isolate,
      {{date_time->iso_year(), date_time->iso_month(), date_time->iso_day()},
       {date_time->iso_hour(), date_time->iso_minute(), date_time->iso_second(),
        date_time->iso_millisecond(), date_time->iso_microsecond(),
        date_time->iso_nanosecond()}},
      calendar);
}

// #sec-temporal.plaindatetime.prototype.toplainyearmonth
MaybeDirectHandle<JSTemporalPlainYearMonth>
JSTemporalPlainDateTime::ToPlainYearMonth(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time) {
  return ToPlain<JSTemporalPlainDateTime, JSTemporalPlainYearMonth,
                 YearMonthFromFields>(isolate, date_time,
                                      isolate->factory()->monthCode_string(),
                                      isolate->factory()->year_string());
}

// #sec-temporal.plaindatetime.prototype.toplainmonthday
MaybeDirectHandle<JSTemporalPlainMonthDay>
JSTemporalPlainDateTime::ToPlainMonthDay(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time) {
  return ToPlain<JSTemporalPlainDateTime, JSTemporalPlainMonthDay,
                 MonthDayFromFields>(isolate, date_time,
                                     isolate->factory()->day_string(),
                                     isolate->factory()->monthCode_string());
}

// #sec-temporal.plaindatetime.prototype.tozoneddatetime
MaybeDirectHandle<JSTemporalZonedDateTime>
JSTemporalPlainDateTime::ToZonedDateTime(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> temporal_time_zone_like,
    DirectHandle<Object> options_obj) {
  const char* method_name = "Temporal.PlainDateTime.prototype.toZonedDateTime";
  // 1. Let dateTime be the this value.
  // 2. Perform ? RequireInternalSlot(dateTime,
  // [[InitializedTemporalDateTime]]).
  // 3. Let timeZone be ? ToTemporalTimeZone(temporalTimeZoneLike).
  DirectHandle<JSReceiver> time_zone;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, time_zone,
      temporal::ToTemporalTimeZone(isolate, temporal_time_zone_like,
                                   method_name));
  // 4. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));
  // 5. Let disambiguation be ? ToTemporalDisambiguation(options).
  Disambiguation disambiguation;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, disambiguation,
      ToTemporalDisambiguation(isolate, options, method_name),
      DirectHandle<JSTemporalZonedDateTime>());

  // 6. Let instant be ? BuiltinTimeZoneGetInstantFor(timeZone, dateTime,
  // disambiguation).
  DirectHandle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant,
      BuiltinTimeZoneGetInstantFor(isolate, time_zone, date_time,
                                   disambiguation, method_name));

  // 7. Return ? CreateTemporalZonedDateTime(instant.[[Nanoseconds]],
  // timeZone, dateTime.[[Calendar]]).
  return CreateTemporalZonedDateTime(
      isolate, direct_handle(instant->nanoseconds(), isolate), time_zone,
      DirectHandle<JSReceiver>(date_time->calendar(), isolate));
}

namespace {

// #sec-temporal-consolidatecalendars
MaybeDirectHandle<JSReceiver> ConsolidateCalendars(
    Isolate* isolate, DirectHandle<JSReceiver> one,
    DirectHandle<JSReceiver> two) {
  Factory* factory = isolate->factory();
  // 1. If one and two are the same Object value, return two.
  if (one.is_identical_to(two)) return two;

  // 2. Let calendarOne be ? ToString(one).
  DirectHandle<String> calendar_one;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, calendar_one,
                             Object::ToString(isolate, one));
  // 3. Let calendarTwo be ? ToString(two).
  DirectHandle<String> calendar_two;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, calendar_two,
                             Object::ToString(isolate, two));
  // 4. If calendarOne is calendarTwo, return two.
  if (String::Equals(isolate, calendar_one, calendar_two)) {
    return two;
  }
  // 5. If calendarOne is "iso8601", return two.
  if (String::Equals(isolate, calendar_one, factory->iso8601_string())) {
    return two;
  }
  // 6. If calendarTwo is "iso8601", return one.
  if (String::Equals(isolate, calendar_two, factory->iso8601_string())) {
    return one;
  }
  // 7. Throw a RangeError exception.
  THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
}

}  // namespace

// #sec-temporal.plaindatetime.prototype.withplaindate
MaybeDirectHandle<JSTemporalPlainDateTime>
JSTemporalPlainDateTime::WithPlainDate(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> temporal_date_like) {
  // 1. Let temporalDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(temporalDateTime,
  // [[InitializedTemporalDateTime]]).
  // 3. Let plainDate be ? ToTemporalDate(plainDateLike).
  DirectHandle<JSTemporalPlainDate> plain_date;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, plain_date,
      ToTemporalDate(isolate, temporal_date_like,
                     "Temporal.PlainDateTime.prototype.withPlainDate"));
  // 4. Let calendar be ? ConsolidateCalendars(temporalDateTime.[[Calendar]],
  // plainDate.[[Calendar]]).
  DirectHandle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar,
      ConsolidateCalendars(isolate,
                           direct_handle(date_time->calendar(), isolate),
                           direct_handle(plain_date->calendar(), isolate)));
  // 5. Return ? CreateTemporalDateTime(plainDate.[[ISOYear]],
  // plainDate.[[ISOMonth]], plainDate.[[ISODay]], temporalDateTime.[[ISOHour]],
  // temporalDateTime.[[ISOMinute]], temporalDateTime.[[ISOSecond]],
  // temporalDateTime.[[ISOMillisecond]], temporalDateTime.[[ISOMicrosecond]],
  // temporalDateTime.[[ISONanosecond]], calendar).
  return temporal::CreateTemporalDateTime(
      isolate,
      {{plain_date->iso_year(), plain_date->iso_month(), plain_date->iso_day()},
       {date_time->iso_hour(), date_time->iso_minute(), date_time->iso_second(),
        date_time->iso_millisecond(), date_time->iso_microsecond(),
        date_time->iso_nanosecond()}},
      calendar);
}

namespace {
MaybeDirectHandle<String> TemporalDateTimeToString(
    Isolate* isolate, const DateTimeRecord& date_time,
    DirectHandle<JSReceiver> calendar, Precision precision,
    ShowCalendar show_calendar) {
  IncrementalStringBuilder builder(isolate);
  // 1. Assert: isoYear, isoMonth, isoDay, hour, minute, second, millisecond,
  // microsecond, and nanosecond are integers.
  // 2. Let year be ! PadISOYear(isoYear).
  PadISOYear(&builder, date_time.date.year);

  // 3. Let month be ToZeroPaddedDecimalString(isoMonth, 2).
  builder.AppendCharacter('-');
  ToZeroPaddedDecimalString(&builder, date_time.date.month, 2);

  // 4. Let day be ToZeroPaddedDecimalString(isoDay, 2).
  builder.AppendCharacter('-');
  ToZeroPaddedDecimalString(&builder, date_time.date.day, 2);
  // 5. Let hour be ToZeroPaddedDecimalString(hour, 2).
  builder.AppendCharacter('T');
  ToZeroPaddedDecimalString(&builder, date_time.time.hour, 2);

  // 6. Let minute be ToZeroPaddedDecimalString(minute, 2).
  builder.AppendCharacter(':');
  ToZeroPaddedDecimalString(&builder, date_time.time.minute, 2);

  // 7. Let seconds be ! FormatSecondsStringPart(second, millisecond,
  // microsecond, nanosecond, precision).
  FormatSecondsStringPart(
      &builder, date_time.time.second, date_time.time.millisecond,
      date_time.time.microsecond, date_time.time.nanosecond, precision);
  // 8. Let calendarString be ? MaybeFormatCalendarAnnotation(calendar,
  // showCalendar).
  DirectHandle<String> calendar_string;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar_string,
      MaybeFormatCalendarAnnotation(isolate, calendar, show_calendar));

  // 9. Return the string-concatenation of year, the code unit 0x002D
  // (HYPHEN-MINUS), month, the code unit 0x002D (HYPHEN-MINUS), day, 0x0054
  // (LATIN CAPITAL LETTER T), hour, the code unit 0x003A (COLON), minute,
  builder.AppendString(calendar_string);
  return builder.Finish().ToHandleChecked();
}
}  // namespace

// #sec-temporal.plaindatetime.prototype.tojson
MaybeDirectHandle<String> JSTemporalPlainDateTime::ToJSON(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time) {
  return TemporalDateTimeToString(
      isolate,
      {{date_time->iso_year(), date_time->iso_month(), date_time->iso_day()},
       {date_time->iso_hour(), date_time->iso_minute(), date_time->iso_second(),
        date_time->iso_millisecond(), date_time->iso_microsecond(),
        date_time->iso_nanosecond()}},
      DirectHandle<JSReceiver>(date_time->calendar(), isolate),
      Precision::kAuto, ShowCalendar::kAuto);
}

// #sec-temporal.plaindatetime.prototype.tolocalestring
MaybeDirectHandle<String> JSTemporalPlainDateTime::ToLocaleString(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> locales, DirectHandle<Object> options) {
#ifdef V8_INTL_SUPPORT
  return JSDateTimeFormat::TemporalToLocaleString(
      isolate, date_time, locales, options,
      "Temporal.PlainDateTime.prototype.toLocaleString");
#else   //  V8_INTL_SUPPORT
  return TemporalDateTimeToString(
      isolate,
      {{date_time->iso_year(), date_time->iso_month(), date_time->iso_day()},
       {date_time->iso_hour(), date_time->iso_minute(), date_time->iso_second(),
        date_time->iso_millisecond(), date_time->iso_microsecond(),
        date_time->iso_nanosecond()}},
      DirectHandle<JSReceiver>(date_time->calendar(), isolate),
      Precision::kAuto, ShowCalendar::kAuto);
#endif  // V8_INTL_SUPPORT
}

namespace {

constexpr double kNsPerDay = 8.64e13;

DateTimeRecord RoundTime(
    Isolate* isolate, const TimeRecord& time, double increment, Unit unit,
    RoundingMode rounding_mode,
    // 3.a a. If dayLengthNs is not present, set dayLengthNs to nsPerDay.
    double day_length_ns = kNsPerDay);

// #sec-temporal-roundisodatetime
DateTimeRecord RoundISODateTime(
    Isolate* isolate, const DateTimeRecord& date_time, double increment,
    Unit unit, RoundingMode rounding_mode,
    // 3. If dayLength is not present, set dayLength to nsPerDay.
    double day_length_ns = kNsPerDay) {
  // 1. Assert: year, month, day, hour, minute, second, millisecond,
  // microsecond, and nanosecond are integers.
  TEMPORAL_ENTER_FUNC();
  // 2. Assert: ISODateTimeWithinLimits(year, month, day, hour, minute, second,
  // millisecond, microsecond, nanosecond) is true.
  DCHECK(ISODateTimeWithinLimits(isolate, date_time));

  // 4. Let roundedTime be ! RoundTime(hour, minute, second, millisecond,
  // microsecond, nanosecond, increment, unit, roundingMode, dayLength).
  DateTimeRecord rounded_time = RoundTime(isolate, date_time.time, increment,
                                          unit, rounding_mode, day_length_ns);
  // 5. Let balanceResult be ! BalanceISODate(year, month, day +
  // roundedTime.[[Days]]).
  rounded_time.date.year = date_time.date.year;
  rounded_time.date.month = date_time.date.month;
  rounded_time.date.day += date_time.date.day;
  DateRecord balance_result = BalanceISODate(isolate, rounded_time.date);

  // 6. Return the Record { [[Year]]: balanceResult.[[Year]], [[Month]]:
  // balanceResult.[[Month]], [[Day]]: balanceResult.[[Day]], [[Hour]]:
  // roundedTime.[[Hour]], [[Minute]]: roundedTime.[[Minute]], [[Second]]:
  // roundedTime.[[Second]], [[Millisecond]]: roundedTime.[[Millisecond]],
  // [[Microsecond]]: roundedTime.[[Microsecond]], [[Nanosecond]]:
  // roundedTime.[[Nanosecond]] }.
  return {balance_result, rounded_time.time};
}

}  // namespace

// #sec-temporal.plaindatetime.prototype.tostring
MaybeDirectHandle<String> JSTemporalPlainDateTime::ToString(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> options_obj) {
  const char* method_name = "Temporal.PlainDateTime.prototype.toString";
  // 1. Let dateTime be the this value.
  // 2. Perform ? RequireInternalSlot(dateTime,
  // [[InitializedTemporalDateTime]]).
  // 3. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));

  // 4. Let precision be ? ToSecondsStringPrecision(options).
  StringPrecision precision;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, precision,
      ToSecondsStringPrecision(isolate, options, method_name),
      DirectHandle<String>());

  // 5. Let roundingMode be ? ToTemporalRoundingMode(options, "trunc").
  RoundingMode rounding_mode;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, rounding_mode,
      ToTemporalRoundingMode(isolate, options, RoundingMode::kTrunc,
                             method_name),
      DirectHandle<String>());

  // 6. Let showCalendar be ? ToShowCalendarOption(options).
  ShowCalendar show_calendar;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, show_calendar,
      ToShowCalendarOption(isolate, options, method_name),
      DirectHandle<String>());

  // 7. Let result be ! RoundISODateTime(dateTime.[[ISOYear]],
  // dateTime.[[ISOMonth]], dateTime.[[ISODay]], dateTime.[[ISOHour]],
  // dateTime.[[ISOMinute]], dateTime.[[ISOSecond]],
  // dateTime.[[ISOMillisecond]], dateTime.[[ISOMicrosecond]],
  // dateTime.[[ISONanosecond]], precision.[[Increment]], precision.[[Unit]],
  // roundingMode).
  DateTimeRecord result = RoundISODateTime(
      isolate,
      {{date_time->iso_year(), date_time->iso_month(), date_time->iso_day()},
       {date_time->iso_hour(), date_time->iso_minute(), date_time->iso_second(),
        date_time->iso_millisecond(), date_time->iso_microsecond(),
        date_time->iso_nanosecond()}},
      precision.increment, precision.unit, rounding_mode);
  // 8. Return ? TemporalDateTimeToString(result.[[Year]], result.[[Month]],
  // result.[[Day]], result.[[Hour]], result.[[Minute]], result.[[Second]],
  // result.[[Millisecond]], result.[[Microsecond]], result.[[Nanosecond]],
  // dateTime.[[Calendar]], precision.[[Precision]], showCalendar).
  return TemporalDateTimeToString(isolate, result,
                                  direct_handle(date_time->calendar(), isolate),
                                  precision.precision, show_calendar);
}

// #sec-temporal.now.plaindatetime
MaybeDirectHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::Now(
    Isolate* isolate, DirectHandle<Object> calendar_like,
    DirectHandle<Object> temporal_time_zone_like) {
  const char* method_name = "Temporal.Now.plainDateTime";
  // 1. Return ? SystemDateTime(temporalTimeZoneLike, calendarLike).
  return SystemDateTime(isolate, temporal_time_zone_like, calendar_like,
                        method_name);
}

// #sec-temporal.now.plaindatetimeiso
MaybeDirectHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::NowISO(
    Isolate* isolate, DirectHandle<Object> temporal_time_zone_like) {
  const char* method_name = "Temporal.Now.plainDateTimeISO";
  // 1. Let calendar be ! GetISO8601Calendar().
  DirectHandle<JSReceiver> calendar = temporal::GetISO8601Calendar(isolate);
  // 2. Return ? SystemDateTime(temporalTimeZoneLike, calendar).
  return SystemDateTime(isolate, temporal_time_zone_like, calendar,
                        method_name);
}

namespace {

// #sec-temporal-totemporaldatetimeroundingincrement
Maybe<double> ToTemporalDateTimeRoundingIncrement(
    Isolate* isolate, DirectHandle<JSReceiver> normalized_option,
    Unit smallest_unit) {
  Maximum maximum;
  // 1. If smallestUnit is "day", then
  if (smallest_unit == Unit::kDay) {
    // a. Let maximum be 1.
    maximum.value = 1;
    maximum.defined = true;
    // 2. Else,
  } else {
    // a. Let maximum be !
    // MaximumTemporalDurationRoundingIncrement(smallestUnit).
    maximum = MaximumTemporalDurationRoundingIncrement(smallest_unit);
    // b. Assert: maximum is not undefined.
    DCHECK(maximum.defined);
  }
  // 3. Return ? ToTemporalRoundingIncrement(normalizedOptions, maximum, false).
  return ToTemporalRoundingIncrement(isolate, normalized_option, maximum.value,
                                     maximum.defined, false);
}

}  // namespace

// #sec-temporal.plaindatetime.prototype.round
MaybeDirectHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::Round(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> round_to_obj) {
  const char* method_name = "Temporal.PlainDateTime.prototype.round";
  Factory* factory = isolate->factory();
  // 1. Let temporalTime be the this value.
  // 2. Perform ? RequireInternalSlot(dateTime,
  // [[InitializedTemporalDateTime]]).
  // 3. If roundTo is undefined, then
  if (IsUndefined(*round_to_obj)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }

  DirectHandle<JSReceiver> round_to;
  // 4. If Type(roundTo) is String, then
  if (IsString(*round_to_obj)) {
    // a. Let paramString be roundTo.
    DirectHandle<String> param_string = Cast<String>(round_to_obj);
    // b. Set roundTo to ! OrdinaryObjectCreate(null).
    round_to = factory->NewJSObjectWithNullProto();
    // c. Perform ! CreateDataPropertyOrThrow(roundTo, "smallestUnit",
    // paramString).
    CHECK(JSReceiver::CreateDataProperty(isolate, round_to,
                                         factory->smallestUnit_string(),
                                         param_string, Just(kThrowOnError))
              .FromJust());
    // 5. Else
  } else {
    // a. Set roundTo to ? GetOptionsObject(roundTo).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, round_to,
        GetOptionsObject(isolate, round_to_obj, method_name));
  }

  // 6. Let smallestUnit be ? GetTemporalUnit(roundTo, "smallestUnit", time,
  // required).
  Unit smallest_unit;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, smallest_unit,
      GetTemporalUnit(isolate, round_to, "smallestUnit", UnitGroup::kTime,
                      Unit::kDay, true, method_name),
      DirectHandle<JSTemporalPlainDateTime>());

  // 7. Let roundingMode be ? ToTemporalRoundingMode(roundTo, "halfExpand").
  RoundingMode rounding_mode;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, rounding_mode,
      ToTemporalRoundingMode(isolate, round_to, RoundingMode::kHalfExpand,
                             method_name),
      DirectHandle<JSTemporalPlainDateTime>());

  // 8. Let roundingIncrement be ? ToTemporalDateTimeRoundingIncrement(roundTo,
  // smallestUnit).
  double rounding_increment;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, rounding_increment,
      ToTemporalDateTimeRoundingIncrement(isolate, round_to, smallest_unit),
      DirectHandle<JSTemporalPlainDateTime>());

  // 9. Let result be ! RoundISODateTime(dateTime.[[ISOYear]],
  // dateTime.[[ISOMonth]], dateTime.[[ISODay]], dateTime.[[ISOHour]],
  // dateTime.[[ISOMinute]], dateTime.[[ISOSecond]],
  // dateTime.[[ISOMillisecond]], dateTime.[[ISOMicrosecond]],
  // dateTime.[[ISONanosecond]], roundingIncrement, smallestUnit, roundingMode).
  DateTimeRecord result = RoundISODateTime(
      isolate,
      {{date_time->iso_year(), date_time->iso_month(), date_time->iso_day()},
       {date_time->iso_hour(), date_time->iso_minute(), date_time->iso_second(),
        date_time->iso_millisecond(), date_time->iso_microsecond(),
        date_time->iso_nanosecond()}},
      rounding_increment, smallest_unit, rounding_mode);

  // 10. Return ? CreateTemporalDateTime(result.[[Year]], result.[[Month]],
  // result.[[Day]], result.[[Hour]], result.[[Minute]], result.[[Second]],
  // result.[[Millisecond]], result.[[Microsecond]], result.[[Nanosecond]],
  // dateTime.[[Calendar]]).
  return temporal::CreateTemporalDateTime(
      isolate, result, direct_handle(date_time->calendar(), isolate));
}

namespace {

MaybeDirectHandle<JSTemporalPlainDateTime>
AddDurationToOrSubtractDurationFromPlainDateTime(
    Isolate* isolate, Arithmetic operation,
    DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> temporal_duration_like,
    DirectHandle<Object> options_obj, const char* method_name) {
  // 1. If operation is subtract, let sign be -1. Otherwise, let sign be 1.
  double sign = operation == Arithmetic::kSubtract ? -1.0 : 1.0;
  // 2. Let duration be ? ToTemporalDurationRecord(temporalDurationLike).
  DurationRecord duration;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, duration,
      temporal::ToTemporalDurationRecord(isolate, temporal_duration_like,
                                         method_name),
      DirectHandle<JSTemporalPlainDateTime>());

  TimeDurationRecord& time_duration = duration.time_duration;

  // 3. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));
  // 4. Let result be ? AddDateTime(dateTime.[[ISOYear]], dateTime.[[ISOMonth]],
  // dateTime.[[ISODay]], dateTime.[[ISOHour]], dateTime.[[ISOMinute]],
  // dateTime.[[ISOSecond]], dateTime.[[ISOMillisecond]],
  // dateTime.[[ISOMicrosecond]], dateTime.[[ISONanosecond]],
  // dateTime.[[Calendar]], duration.[[Years]], duration.[[Months]],
  // duration.[[Weeks]], duration.[[Days]], duration.[[Hours]],
  // duration.[[Minutes]], duration.[[Seconds]], duration.[[Milliseconds]],
  // duration.[[Microseconds]], duration.[[Nanoseconds]], options).
  DateTimeRecord result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result,
      AddDateTime(isolate,
                  {{date_time->iso_year(), date_time->iso_month(),
                    date_time->iso_day()},
                   {date_time->iso_hour(), date_time->iso_minute(),
                    date_time->iso_second(), date_time->iso_millisecond(),
                    date_time->iso_microsecond(), date_time->iso_nanosecond()}},
                  direct_handle(date_time->calendar(), isolate),
                  {sign * duration.years,
                   sign * duration.months,
                   sign * duration.weeks,
                   {sign * time_duration.days, sign * time_duration.hours,
                    sign * time_duration.minutes, sign * time_duration.seconds,
                    sign * time_duration.milliseconds,
                    sign * time_duration.microseconds,
                    sign * time_duration.nanoseconds}},
                  options),
      DirectHandle<JSTemporalPlainDateTime>());

  // 5. Assert: ! IsValidISODate(result.[[Year]], result.[[Month]],
  // result.[[Day]]) is true.
  DCHECK(IsValidISODate(isolate, result.date));
  // 6. Assert: ! IsValidTime(result.[[Hour]], result.[[Minute]],
  // result.[[Second]], result.[[Millisecond]], result.[[Microsecond]],
  // result.[[Nanosecond]]) is true.
  DCHECK(IsValidTime(isolate, result.time));
  // 7. Return ? CreateTemporalDateTime(result.[[Year]], result.[[Month]],
  // result.[[Day]], result.[[Hour]], result.[[Minute]], result.[[Second]],
  // result.[[Millisecond]], result.[[Microsecond]], result.[[Nanosecond]],
  // dateTime.[[Calendar]]).
  return temporal::CreateTemporalDateTime(
      isolate, result, direct_handle(date_time->calendar(), isolate));
}

}  // namespace

// #sec-temporal.plaindatetime.prototype.add
MaybeDirectHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::Add(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> temporal_duration_like, DirectHandle<Object> options) {
  return AddDurationToOrSubtractDurationFromPlainDateTime(
      isolate, Arithmetic::kAdd, date_time, temporal_duration_like, options,
      "Temporal.PlainDateTime.prototype.add");
}

// #sec-temporal.plaindatetime.prototype.subtract
MaybeDirectHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::Subtract(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> temporal_duration_like, DirectHandle<Object> options) {
  return AddDurationToOrSubtractDurationFromPlainDateTime(
      isolate, Arithmetic::kSubtract, date_time, temporal_duration_like,
      options, "Temporal.PlainDateTime.prototype.subtract");
}

namespace {

// #sec-temporal-differencetemporalplaindatetime
MaybeDirectHandle<JSTemporalDuration> DifferenceTemporalPlainDateTime(
    Isolate* isolate, TimePreposition operation,
    DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> other_obj, DirectHandle<Object> options,
    const char* method_name) {
  TEMPORAL_ENTER_FUNC();
  // 1. If operation is since, let sign be -1. Otherwise, let sign be 1.
  double sign = operation == TimePreposition::kSince ? -1 : 1;
  // 2. Set other to ? ToTemporalDateTime(other).
  DirectHandle<JSTemporalPlainDateTime> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other, ToTemporalDateTime(isolate, other_obj, method_name));
  // 3. If ? CalendarEquals(dateTime.[[Calendar]], other.[[Calendar]]) is false,
  // throw a RangeError exception.
  bool calendar_equals;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, calendar_equals,
      CalendarEqualsBool(isolate, direct_handle(date_time->calendar(), isolate),
                         direct_handle(other->calendar(), isolate)),
      DirectHandle<JSTemporalDuration>());
  if (!calendar_equals) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }
  // 4. Let settings be ? GetDifferenceSettings(operation, options, datetime, «
  // », "nanosecond", "day").
  DifferenceSettings settings;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, settings,
      GetDifferenceSettings(isolate, operation, options, UnitGroup::kDateTime,
                            DisallowedUnitsInDifferenceSettings::kNone,
                            Unit::kNanosecond, Unit::kDay, method_name),
      DirectHandle<JSTemporalDuration>());
  // 5. Let diff be ? DifferenceISODateTime(dateTime.[[ISOYear]],
  // dateTime.[[ISOMonth]], dateTime.[[ISODay]], dateTime.[[ISOHour]],
  // dateTime.[[ISOMinute]], dateTime.[[ISOSecond]],
  // dateTime.[[ISOMillisecond]], dateTime.[[ISOMicrosecond]],
  // dateTime.[[ISONanosecond]], other.[[ISOYear]], other.[[ISOMonth]],
  // other.[[ISODay]], other.[[ISOHour]], other.[[ISOMinute]],
  // other.[[ISOSecond]], other.[[ISOMillisecond]], other.[[ISOMicrosecond]],
  // other.[[ISONanosecond]], dateTime.[[Calendar]], settings.[[LargestUnit]],
  // settings.[[Options]]).
  DurationRecord diff;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, diff,
      DifferenceISODateTime(
          isolate,
          {{date_time->iso_year(), date_time->iso_month(),
            date_time->iso_day()},
           {date_time->iso_hour(), date_time->iso_minute(),
            date_time->iso_second(), date_time->iso_millisecond(),
            date_time->iso_microsecond(), date_time->iso_nanosecond()}},
          {{other->iso_year(), other->iso_month(), other->iso_day()},
           {other->iso_hour(), other->iso_minute(), other->iso_second(),
            other->iso_millisecond(), other->iso_microsecond(),
            other->iso_nanosecond()}},
          direct_handle(date_time->calendar(), isolate), settings.largest_unit,
          settings.options, method_name),
      DirectHandle<JSTemporalDuration>());
  // 6. Let relativeTo be ! CreateTemporalDate(dateTime.[[ISOYear]],
  // dateTime.[[ISOMonth]], dateTime.[[ISODay]], dateTime.[[Calendar]]).
  DirectHandle<JSTemporalPlainDate> relative_to;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, relative_to,
      CreateTemporalDate(
          isolate,
          {date_time->iso_year(), date_time->iso_month(), date_time->iso_day()},
          direct_handle(date_time->calendar(), isolate)),
      DirectHandle<JSTemporalDuration>());
  // 7. Let roundResult be (? RoundDuration(diff.[[Years]], diff.[[Months]],
  // diff.[[Weeks]], diff.[[Days]], diff.[[Hours]], diff.[[Minutes]],
  // diff.[[Seconds]], diff.[[Milliseconds]], diff.[[Microseconds]],
  // diff.[[Nanoseconds]], settings.[[RoundingIncrement]],
  // settings.[[SmallestUnit]], settings.[[RoundingMode]],
  // relativeTo)).[[DurationRecord]].
  DurationRecordWithRemainder round_result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, round_result,
      RoundDuration(isolate, diff, settings.rounding_increment,
                    settings.smallest_unit, settings.rounding_mode, relative_to,
                    method_name),
      DirectHandle<JSTemporalDuration>());
  // 8. Let result be ? BalanceDuration(roundResult.[[Days]],
  // roundResult.[[Hours]], roundResult.[[Minutes]], roundResult.[[Seconds]],
  // roundResult.[[Milliseconds]], roundResult.[[Microseconds]],
  // roundResult.[[Nanoseconds]], settings.[[LargestUnit]]).
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, round_result.record.time_duration,
      BalanceDuration(isolate, settings.largest_unit,
                      round_result.record.time_duration, method_name),
      DirectHandle<JSTemporalDuration>());
  // 9. Return ! CreateTemporalDuration(sign × roundResult.[[Years]], sign ×
  // roundResult.[[Months]], sign × roundResult.[[Weeks]], sign ×
  // result.[[Days]], sign × result.[[Hours]], sign × result.[[Minutes]], sign ×
  // result.[[Seconds]], sign × result.[[Milliseconds]], sign ×
  // result.[[Microseconds]], sign × result.[[Nanoseconds]]).
  return CreateTemporalDuration(
             isolate, {sign * round_result.record.years,
                       sign * round_result.record.months,
                       sign * round_result.record.weeks,
                       {sign * round_result.record.time_duration.days,
                        sign * round_result.record.time_duration.hours,
                        sign * round_result.record.time_duration.minutes,
                        sign * round_result.record.time_duration.seconds,
                        sign * round_result.record.time_duration.milliseconds,
                        sign * round_result.record.time_duration.microseconds,
                        sign * round_result.record.time_duration.nanoseconds}})
      .ToHandleChecked();
}

}  // namespace

// #sec-temporal.plaindatetime.prototype.until
MaybeDirectHandle<JSTemporalDuration> JSTemporalPlainDateTime::Until(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  return DifferenceTemporalPlainDateTime(
      isolate, TimePreposition::kUntil, handle, other, options,
      "Temporal.PlainDateTime.prototype.until");
}

// #sec-temporal.plaindatetime.prototype.since
MaybeDirectHandle<JSTemporalDuration> JSTemporalPlainDateTime::Since(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  return DifferenceTemporalPlainDateTime(
      isolate, TimePreposition::kSince, handle, other, options,
      "Temporal.PlainDateTime.prototype.since");
}

// #sec-temporal.plaindatetime.prototype.getisofields
MaybeDirectHandle<JSReceiver> JSTemporalPlainDateTime::GetISOFields(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time) {
  Factory* factory = isolate->factory();
  // 1. Let dateTime be the this value.
  // 2. Perform ? RequireInternalSlot(temporalDateTime,
  // [[InitializedTemporalDateTime]]).
  // 3. Let fields be ! OrdinaryObjectCreate(%Object.prototype%).
  DirectHandle<JSObject> fields =
      isolate->factory()->NewJSObject(isolate->object_function());
  // 4. Perform ! CreateDataPropertyOrThrow(fields, "calendar",
  // temporalTime.[[Calendar]]).
  CHECK(JSReceiver::CreateDataProperty(
            isolate, fields, factory->calendar_string(),
            DirectHandle<JSReceiver>(date_time->calendar(), isolate),
            Just(kThrowOnError))
            .FromJust());
  // 5. Perform ! CreateDataPropertyOrThrow(fields, "isoDay",
  // 𝔽(dateTime.[[ISODay]])).
  // 6. Perform ! CreateDataPropertyOrThrow(fields, "isoHour",
  // 𝔽(temporalTime.[[ISOHour]])).
  // 7. Perform ! CreateDataPropertyOrThrow(fields, "isoMicrosecond",
  // 𝔽(temporalTime.[[ISOMicrosecond]])).
  // 8. Perform ! CreateDataPropertyOrThrow(fields, "isoMillisecond",
  // 𝔽(temporalTime.[[ISOMillisecond]])).
  // 9. Perform ! CreateDataPropertyOrThrow(fields, "isoMinute",
  // 𝔽(temporalTime.[[ISOMinute]])).
  // 10. Perform ! CreateDataPropertyOrThrow(fields, "isoMonth",
  // 𝔽(temporalTime.[[ISOMonth]])).
  // 11. Perform ! CreateDataPropertyOrThrow(fields, "isoNanosecond",
  // 𝔽(temporalTime.[[ISONanosecond]])).
  // 12. Perform ! CreateDataPropertyOrThrow(fields, "isoSecond",
  // 𝔽(temporalTime.[[ISOSecond]])).
  // 13. Perform ! CreateDataPropertyOrThrow(fields, "isoYear",
  // 𝔽(temporalTime.[[ISOYear]])).
  DEFINE_INT_FIELD(fields, isoDay, iso_day, date_time)
  DEFINE_INT_FIELD(fields, isoHour, iso_hour, date_time)
  DEFINE_INT_FIELD(fields, isoMicrosecond, iso_microsecond, date_time)
  DEFINE_INT_FIELD(fields, isoMillisecond, iso_millisecond, date_time)
  DEFINE_INT_FIELD(fields, isoMinute, iso_minute, date_time)
  DEFINE_INT_FIELD(fields, isoMonth, iso_month, date_time)
  DEFINE_INT_FIELD(fields, isoNanosecond, iso_nanosecond, date_time)
  DEFINE_INT_FIELD(fields, isoSecond, iso_second, date_time)
  DEFINE_INT_FIELD(fields, isoYear, iso_year, date_time)
  // 14. Return fields.
  return fields;
}

// #sec-temporal.plaindatetime.prototype.toplaindate
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainDateTime::ToPlainDate(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time) {
  // 1. Let dateTime be the this value.
  // 2. Perform ? RequireInternalSlot(dateTime,
  // [[InitializedTemporalDateTime]]).
  // 3. Return ? CreateTemporalDate(dateTime.[[ISOYear]], dateTime.[[ISOMonth]],
  // dateTime.[[ISODay]], dateTime.[[Calendar]]).
  return CreateTemporalDate(
      isolate,
      {date_time->iso_year(), date_time->iso_month(), date_time->iso_day()},
      DirectHandle<JSReceiver>(date_time->calendar(), isolate));
}

// #sec-temporal.plaindatetime.prototype.toplaintime
MaybeDirectHandle<JSTemporalPlainTime> JSTemporalPlainDateTime::ToPlainTime(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time) {
  // 1. Let dateTime be the this value.
  // 2. Perform ? RequireInternalSlot(dateTime,
  // [[InitializedTemporalDateTime]]).
  // 3. Return ? CreateTemporalTime(dateTime.[[ISOHour]],
  // dateTime.[[ISOMinute]], dateTime.[[ISOSecond]],
  // dateTime.[[ISOMillisecond]], dateTime.[[ISOMicrosecond]],
  // dateTime.[[ISONanosecond]]).
  return CreateTemporalTime(
      isolate, {date_time->iso_hour(), date_time->iso_minute(),
                date_time->iso_second(), date_time->iso_millisecond(),
                date_time->iso_microsecond(), date_time->iso_nanosecond()});
}

// #sec-temporal.plainmonthday
MaybeDirectHandle<JSTemporalPlainMonthDay> JSTemporalPlainMonthDay::Constructor(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, DirectHandle<Object> iso_month_obj,
    DirectHandle<Object> iso_day_obj, DirectHandle<Object> calendar_like,
    DirectHandle<Object> reference_iso_year_obj) {
  const char* method_name = "Temporal.PlainMonthDay";
  // 1. If NewTarget is undefined, throw a TypeError exception.
  if (IsUndefined(*new_target)) {
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                                 isolate->factory()->NewStringFromAsciiChecked(
                                     method_name)));
  }

  // 3. Let m be ? ToIntegerThrowOnInfinity(isoMonth).
  TO_INT_THROW_ON_INFTY(iso_month, JSTemporalPlainMonthDay);
  // 5. Let d be ? ToIntegerThrowOnInfinity(isoDay).
  TO_INT_THROW_ON_INFTY(iso_day, JSTemporalPlainMonthDay);
  // 7. Let calendar be ? ToTemporalCalendarWithISODefault(calendarLike).
  DirectHandle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar,
      ToTemporalCalendarWithISODefault(isolate, calendar_like, method_name));

  // 2. If referenceISOYear is undefined, then
  // a. Set referenceISOYear to 1972𝔽.
  // ...
  // 8. Let ref be ? ToIntegerThrowOnInfinity(referenceISOYear).
  int32_t ref = 1972;
  if (!IsUndefined(*reference_iso_year_obj)) {
    TO_INT_THROW_ON_INFTY(reference_iso_year, JSTemporalPlainMonthDay);
    ref = reference_iso_year;
  }

  // 10. Return ? CreateTemporalMonthDay(y, m, calendar, ref, NewTarget).
  return CreateTemporalMonthDay(isolate, target, new_target, iso_month, iso_day,
                                calendar, ref);
}

namespace {

// #sec-temporal-parsetemporalmonthdaystring
Maybe<DateRecordWithCalendar> ParseTemporalMonthDayString(
    Isolate* isolate, DirectHandle<String> iso_string) {
  TEMPORAL_ENTER_FUNC();

  // 1. Assert: Type(isoString) is String.
  // 2. If isoString does not satisfy the syntax of a TemporalMonthDayString
  // (see 13.33), then
  std::optional<ParsedISO8601Result> parsed =
      TemporalParser::ParseTemporalMonthDayString(isolate, iso_string);
  if (!parsed.has_value()) {
    // a. Throw a *RangeError* exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<DateRecordWithCalendar>());
  }
  // 3. If isoString contains a UTCDesignator, then
  if (parsed->utc_designator) {
    // a. Throw a *RangeError* exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<DateRecordWithCalendar>());
  }

  // 3. Let result be ? ParseISODateTime(isoString).
  DateTimeRecordWithCalendar result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result, ParseISODateTime(isolate, iso_string, *parsed),
      Nothing<DateRecordWithCalendar>());
  // 5. Let year be result.[[Year]].
  // 6. If no part of isoString is produced by the DateYear production, then
  // a. Set year to undefined.

  // 7. Return the Record { [[Year]]: year, [[Month]]: result.[[Month]],
  // [[Day]]: result.[[Day]], [[Calendar]]: result.[[Calendar]] }.
  DateRecordWithCalendar ret({result.date, result.calendar});
  return Just(ret);
}

// #sec-temporal-totemporalmonthday
MaybeDirectHandle<JSTemporalPlainMonthDay> ToTemporalMonthDay(
    Isolate* isolate, DirectHandle<Object> item_obj,
    DirectHandle<Object> options, const char* method_name) {
  TEMPORAL_ENTER_FUNC();

  Factory* factory = isolate->factory();
  // 2. Assert: Type(options) is Object or Undefined.
  DCHECK(IsJSReceiver(*options) || IsUndefined(*options));

  // 3. Let referenceISOYear be 1972 (the first leap year after the Unix epoch).
  constexpr int32_t kReferenceIsoYear = 1972;
  // 4. If Type(item) is Object, then
  if (IsJSReceiver(*item_obj)) {
    DirectHandle<JSReceiver> item = Cast<JSReceiver>(item_obj);
    // a. If item has an [[InitializedTemporalMonthDay]] internal slot, then
    // i. Return item.
    if (IsJSTemporalPlainMonthDay(*item_obj)) {
      return Cast<JSTemporalPlainMonthDay>(item_obj);
    }
    bool calendar_absent = false;
    // b. If item has an [[InitializedTemporalDate]],
    // [[InitializedTemporalDateTime]], [[InitializedTemporalTime]],
    // [[InitializedTemporalYearMonth]], or [[InitializedTemporalZonedDateTime]]
    // internal slot, then
    // i. Let calendar be item.[[Calendar]].
    // ii. Let calendarAbsent be false.
    DirectHandle<JSReceiver> calendar;
    if (IsJSTemporalPlainDate(*item_obj)) {
      calendar = direct_handle(Cast<JSTemporalPlainDate>(item_obj)->calendar(),
                               isolate);
    } else if (IsJSTemporalPlainDateTime(*item_obj)) {
      calendar = direct_handle(
          Cast<JSTemporalPlainDateTime>(item_obj)->calendar(), isolate);
    } else if (IsJSTemporalPlainTime(*item_obj)) {
      calendar = direct_handle(Cast<JSTemporalPlainTime>(item_obj)->calendar(),
                               isolate);
    } else if (IsJSTemporalPlainYearMonth(*item_obj)) {
      calendar = direct_handle(
          Cast<JSTemporalPlainYearMonth>(item_obj)->calendar(), isolate);
    } else if (IsJSTemporalZonedDateTime(*item_obj)) {
      calendar = direct_handle(
          Cast<JSTemporalZonedDateTime>(item_obj)->calendar(), isolate);
      // c. Else,
    } else {
      // i. Let calendar be ? Get(item, "calendar").
      DirectHandle<Object> calendar_obj;
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, calendar_obj,
          JSReceiver::GetProperty(isolate, item, factory->calendar_string()));
      // ii. If calendar is undefined, then
      if (IsUndefined(*calendar_obj)) {
        // 1. Let calendarAbsent be true.
        calendar_absent = true;
      }
      // iv. Set calendar to ? ToTemporalCalendarWithISODefault(calendar).
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, calendar,
          ToTemporalCalendarWithISODefault(isolate, calendar_obj, method_name));
    }
    // d. Let fieldNames be ? CalendarFields(calendar, « "day", "month",
    // "monthCode", "year" »).
    DirectHandle<FixedArray> field_names =
        DayMonthMonthCodeYearInFixedArray(isolate);
    ASSIGN_RETURN_ON_EXCEPTION(isolate, field_names,
                               CalendarFields(isolate, calendar, field_names));
    // e. Let fields be ? PrepareTemporalFields(item, fieldNames, «»).
    DirectHandle<JSReceiver> fields;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, fields,
                               PrepareTemporalFields(isolate, item, field_names,
                                                     RequiredFields::kNone));
    // f. Let month be ? Get(fields, "month").
    DirectHandle<Object> month;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, month,
        JSReceiver::GetProperty(isolate, fields, factory->month_string()),
        DirectHandle<JSTemporalPlainMonthDay>());
    // g. Let monthCode be ? Get(fields, "monthCode").
    DirectHandle<Object> month_code;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, month_code,
        JSReceiver::GetProperty(isolate, fields, factory->monthCode_string()),
        DirectHandle<JSTemporalPlainMonthDay>());
    // h. Let year be ? Get(fields, "year").
    DirectHandle<Object> year;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, year,
        JSReceiver::GetProperty(isolate, fields, factory->year_string()),
        DirectHandle<JSTemporalPlainMonthDay>());
    // i. If calendarAbsent is true, and month is not undefined, and monthCode
    // is undefined and year is undefined, then
    if (calendar_absent && !IsUndefined(*month) && IsUndefined(*month_code) &&
        IsUndefined(*year)) {
      // i. Perform ! CreateDataPropertyOrThrow(fields, "year",
      // 𝔽(referenceISOYear)).
      CHECK(JSReceiver::CreateDataProperty(
                isolate, fields, factory->year_string(),
                direct_handle(Smi::FromInt(kReferenceIsoYear), isolate),
                Just(kThrowOnError))
                .FromJust());
    }
    // j. Return ? MonthDayFromFields(calendar, fields, options).
    return MonthDayFromFields(isolate, calendar, fields, options);
  }
  // 5. Perform ? ToTemporalOverflow(options).
  MAYBE_RETURN_ON_EXCEPTION_VALUE(
      isolate, ToTemporalOverflow(isolate, options, method_name),
      DirectHandle<JSTemporalPlainMonthDay>());

  // 6. Let string be ? ToString(item).
  DirectHandle<String> string;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, string,
                             Object::ToString(isolate, item_obj));

  // 7. Let result be ? ParseTemporalMonthDayString(string).
  DateRecordWithCalendar result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result, ParseTemporalMonthDayString(isolate, string),
      DirectHandle<JSTemporalPlainMonthDay>());

  // 8. Let calendar be ? ToTemporalCalendarWithISODefault(result.[[Calendar]]).
  DirectHandle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar,
      ToTemporalCalendarWithISODefault(isolate, result.calendar, method_name));

  // 9. If result.[[Year]] is undefined, then
  // We use kMintInt31 to represent undefined
  if (result.date.year == kMinInt31) {
    // a. Return ? CreateTemporalMonthDay(result.[[Month]], result.[[Day]],
    // calendar, referenceISOYear).
    return CreateTemporalMonthDay(isolate, result.date.month, result.date.day,
                                  calendar, kReferenceIsoYear);
  }

  DirectHandle<JSTemporalPlainMonthDay> created_result;
  // 10. Set result to ? CreateTemporalMonthDay(result.[[Month]],
  // result.[[Day]], calendar, referenceISOYear).
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, created_result,
      CreateTemporalMonthDay(isolate, result.date.month, result.date.day,
                             calendar, kReferenceIsoYear));
  // 11.  NOTE: The following operation is called without options, in order for
  // the calendar to store a canonical value in the [[ISOYear]] internal slot of
  // the result.
  // 12. Return ? CalendarMonthDayFromFields(calendar, result).
  return MonthDayFromFields(isolate, calendar, created_result);
}

MaybeDirectHandle<JSTemporalPlainMonthDay> ToTemporalMonthDay(
    Isolate* isolate, DirectHandle<Object> item_obj, const char* method_name) {
  // 1. If options is not present, set options to undefined.
  return ToTemporalMonthDay(isolate, item_obj,
                            isolate->factory()->undefined_value(), method_name);
}

}  // namespace

// #sec-temporal.plainmonthday.from
MaybeDirectHandle<JSTemporalPlainMonthDay> JSTemporalPlainMonthDay::From(
    Isolate* isolate, DirectHandle<Object> item,
    DirectHandle<Object> options_obj) {
  const char* method_name = "Temporal.PlainMonthDay.from";
  // 1. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));
  // 2. If Type(item) is Object and item has an [[InitializedTemporalMonthDay]]
  // internal slot, then
  if (IsJSTemporalPlainMonthDay(*item)) {
    // a. Perform ? ToTemporalOverflow(options).
    MAYBE_RETURN_ON_EXCEPTION_VALUE(
        isolate, ToTemporalOverflow(isolate, options, method_name),
        DirectHandle<JSTemporalPlainMonthDay>());
    // b. Return ? CreateTemporalMonthDay(item.[[ISOMonth]], item.[[ISODay]],
    // item.[[Calendar]], item.[[ISOYear]]).
    auto month_day = Cast<JSTemporalPlainMonthDay>(item);
    return CreateTemporalMonthDay(
        isolate, month_day->iso_month(), month_day->iso_day(),
        direct_handle(month_day->calendar(), isolate), month_day->iso_year());
  }
  // 3. Return ? ToTemporalMonthDay(item, options).
  return ToTemporalMonthDay(isolate, item, options, method_name);
}

// #sec-temporal.plainyearmonth.prototype.equals
MaybeDirectHandle<Oddball> JSTemporalPlainMonthDay::Equals(
    Isolate* isolate, DirectHandle<JSTemporalPlainMonthDay> month_day,
    DirectHandle<Object> other_obj) {
  // 1. Let monthDay be the this value.
  // 2. Perform ? RequireInternalSlot(monthDay,
  // [[InitializedTemporalMonthDay]]).
  // 3. Set other to ? ToTemporalMonthDay(other).
  DirectHandle<JSTemporalPlainMonthDay> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other,
      ToTemporalMonthDay(isolate, other_obj,
                         "Temporal.PlainMonthDay.prototype.equals"));
  // 4. If monthDay.[[ISOMonth]] ≠ other.[[ISOMonth]], return false.
  if (month_day->iso_month() != other->iso_month())
    return isolate->factory()->false_value();
  // 5. If monthDay.[[ISODay]] ≠ other.[[ISODay]], return false.
  if (month_day->iso_day() != other->iso_day())
    return isolate->factory()->false_value();
  // 6. If monthDay.[[ISOYear]] ≠ other.[[ISOYear]], return false.
  if (month_day->iso_year() != other->iso_year())
    return isolate->factory()->false_value();
  // 7. Return ? CalendarEquals(monthDay.[[Calendar]], other.[[Calendar]]).
  return CalendarEquals(
      isolate, DirectHandle<JSReceiver>(month_day->calendar(), isolate),
      DirectHandle<JSReceiver>(other->calendar(), isolate));
}

// #sec-temporal.plainmonthday.prototype.with
MaybeDirectHandle<JSTemporalPlainMonthDay> JSTemporalPlainMonthDay::With(
    Isolate* isolate, DirectHandle<JSTemporalPlainMonthDay> temporal_month_day,
    DirectHandle<Object> temporal_month_day_like_obj,
    DirectHandle<Object> options_obj) {
  // 6. Let fieldNames be ? CalendarFields(calendar, « "day", "month",
  // "monthCode", "year" »).
  DirectHandle<FixedArray> field_names =
      DayMonthMonthCodeYearInFixedArray(isolate);
  return PlainDateOrYearMonthOrMonthDayWith<JSTemporalPlainMonthDay,
                                            MonthDayFromFields>(
      isolate, temporal_month_day, temporal_month_day_like_obj, options_obj,
      field_names, "Temporal.PlainMonthDay.prototype.with");
}

namespace {

// Common code shared by PlainMonthDay and PlainYearMonth.prototype.toPlainDate
template <typename T>
MaybeDirectHandle<JSTemporalPlainDate> PlainMonthDayOrYearMonthToPlainDate(
    Isolate* isolate, DirectHandle<T> temporal, DirectHandle<Object> item_obj,
    DirectHandle<String> receiver_field_name_1,
    DirectHandle<String> receiver_field_name_2,
    DirectHandle<String> input_field_name) {
  Factory* factory = isolate->factory();
  // 1. Let monthDay be the this value.
  // 2. Perform ? RequireInternalSlot(monthDay,
  // [[InitializedTemporalXXX]]).
  // 3. If Type(item) is not Object, then
  if (!IsJSReceiver(*item_obj)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }
  DirectHandle<JSReceiver> item = Cast<JSReceiver>(item_obj);
  // 4. Let calendar be Xxx.[[Calendar]].
  DirectHandle<JSReceiver> calendar(temporal->calendar(), isolate);
  // 5. Let receiverFieldNames be ? CalendarFields(calendar, «
  // receiverFieldName1, receiverFieldName2 »).
  DirectHandle<FixedArray> receiver_field_names = factory->NewFixedArray(2);
  receiver_field_names->set(0, *receiver_field_name_1);
  receiver_field_names->set(1, *receiver_field_name_2);
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, receiver_field_names,
      CalendarFields(isolate, calendar, receiver_field_names));
  // 6. Let fields be ? PrepareTemporalFields(temporal, receiverFieldNames, «»).
  DirectHandle<JSReceiver> fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, fields,
      PrepareTemporalFields(isolate, temporal, receiver_field_names,
                            RequiredFields::kNone));
  // 7. Let inputFieldNames be ? CalendarFields(calendar, « inputFieldName »).
  DirectHandle<FixedArray> input_field_names = factory->NewFixedArray(1);
  input_field_names->set(0, *input_field_name);
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, input_field_names,
      CalendarFields(isolate, calendar, input_field_names));
  // 8. Let inputFields be ? PrepareTemporalFields(item, inputFieldNames, «»).
  DirectHandle<JSReceiver> input_fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, input_fields,
      PrepareTemporalFields(isolate, item, input_field_names,
                            RequiredFields::kNone));
  // 9. Let mergedFields be ? CalendarMergeFields(calendar, fields,
  // inputFields).
  DirectHandle<JSReceiver> merged_fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, merged_fields,
      CalendarMergeFields(isolate, calendar, fields, input_fields));
  // 10. Let mergedFieldNames be the List containing all the elements of
  // receiverFieldNames followed by all the elements of inputFieldNames, with
  // duplicate elements removed.
  DirectHandle<FixedArray> merged_field_names = factory->NewFixedArray(
      receiver_field_names->length() + input_field_names->length());
  Handle<StringSet> added = StringSet::New(isolate);
  for (int i = 0; i < receiver_field_names->length(); i++) {
    DirectHandle<Object> field(receiver_field_names->get(i), isolate);
    DCHECK(IsString(*field));
    auto string = Cast<String>(field);
    if (!added->Has(isolate, string)) {
      merged_field_names->set(added->NumberOfElements(), *field);
      added = StringSet::Add(isolate, added, string);
    }
  }
  for (int i = 0; i < input_field_names->length(); i++) {
    DirectHandle<Object> field(input_field_names->get(i), isolate);
    DCHECK(IsString(*field));
    auto string = Cast<String>(field);
    if (!added->Has(isolate, string)) {
      merged_field_names->set(added->NumberOfElements(), *field);
      added = StringSet::Add(isolate, added, string);
    }
  }
  merged_field_names = FixedArray::RightTrimOrEmpty(isolate, merged_field_names,
                                                    added->NumberOfElements());

  // 11. Set mergedFields to ? PrepareTemporalFields(mergedFields,
  // mergedFieldNames, «»).
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, merged_fields,
      PrepareTemporalFields(isolate, merged_fields, merged_field_names,
                            RequiredFields::kNone));
  // 12. Let options be ! OrdinaryObjectCreate(null).
  DirectHandle<JSObject> options = factory->NewJSObjectWithNullProto();
  // 13. Perform ! CreateDataPropertyOrThrow(options, "overflow", "reject").
  CHECK(JSReceiver::CreateDataProperty(
            isolate, options, factory->overflow_string(),
            factory->reject_string(), Just(kThrowOnError))
            .FromJust());
  // 14. Return ? DateFromFields(calendar, mergedFields, options).
  return DateFromFields(isolate, calendar, merged_fields, options);
}

}  // namespace

// #sec-temporal.plainmonthday.prototype.toplaindate
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainMonthDay::ToPlainDate(
    Isolate* isolate, DirectHandle<JSTemporalPlainMonthDay> month_day,
    DirectHandle<Object> item_obj) {
  Factory* factory = isolate->factory();
  // 5. Let receiverFieldNames be ? CalendarFields(calendar, « "day",
  // "monthCode" »).
  // 7. Let inputFieldNames be ? CalendarFields(calendar, « "year" »).
  return PlainMonthDayOrYearMonthToPlainDate<JSTemporalPlainMonthDay>(
      isolate, month_day, item_obj, factory->day_string(),
      factory->monthCode_string(), factory->year_string());
}

// #sec-temporal.plainmonthday.prototype.getisofields
MaybeDirectHandle<JSReceiver> JSTemporalPlainMonthDay::GetISOFields(
    Isolate* isolate, DirectHandle<JSTemporalPlainMonthDay> month_day) {
  Factory* factory = isolate->factory();
  // 1. Let monthDay be the this value.
  // 2. Perform ? RequireInternalSlot(monthDay,
  // [[InitializedTemporalMonthDay]]).
  // 3. Let fields be ! OrdinaryObjectCreate(%Object.prototype%).
  DirectHandle<JSObject> fields =
      factory->NewJSObject(isolate->object_function());
  // 4. Perform ! CreateDataPropertyOrThrow(fields, "calendar",
  // montyDay.[[Calendar]]).
  CHECK(JSReceiver::CreateDataProperty(
            isolate, fields, factory->calendar_string(),
            DirectHandle<JSReceiver>(month_day->calendar(), isolate),
            Just(kThrowOnError))
            .FromJust());

  // 5. Perform ! CreateDataPropertyOrThrow(fields, "isoDay",
  // 𝔽(montyDay.[[ISODay]])).
  // 6. Perform ! CreateDataPropertyOrThrow(fields, "isoMonth",
  // 𝔽(montyDay.[[ISOMonth]])).
  // 7. Perform ! CreateDataPropertyOrThrow(fields, "isoYear",
  // 𝔽(montyDay.[[ISOYear]])).
  DEFINE_INT_FIELD(fields, isoDay, iso_day, month_day)
  DEFINE_INT_FIELD(fields, isoMonth, iso_month, month_day)
  DEFINE_INT_FIELD(fields, isoYear, iso_year, month_day)
  // 8. Return fields.
  return fields;
}

// #sec-temporal.plainmonthday.prototype.tojson
MaybeDirectHandle<String> JSTemporalPlainMonthDay::ToJSON(
    Isolate* isolate, DirectHandle<JSTemporalPlainMonthDay> month_day) {
  return TemporalMonthDayToString(isolate, month_day, ShowCalendar::kAuto);
}

// #sec-temporal.plainmonthday.prototype.tostring
MaybeDirectHandle<String> JSTemporalPlainMonthDay::ToString(
    Isolate* isolate, DirectHandle<JSTemporalPlainMonthDay> month_day,
    DirectHandle<Object> options) {
  return TemporalToString<JSTemporalPlainMonthDay, TemporalMonthDayToString>(
      isolate, month_day, options, "Temporal.PlainMonthDay.prototype.toString");
}

// #sec-temporal.plainmonthday.prototype.tolocalestring
MaybeDirectHandle<String> JSTemporalPlainMonthDay::ToLocaleString(
    Isolate* isolate, DirectHandle<JSTemporalPlainMonthDay> month_day,
    DirectHandle<Object> locales, DirectHandle<Object> options) {
#ifdef V8_INTL_SUPPORT
  return JSDateTimeFormat::TemporalToLocaleString(
      isolate, month_day, locales, options,
      "Temporal.PlainMonthDay.prototype.toLocaleString");
#else   //  V8_INTL_SUPPORT
  return TemporalMonthDayToString(isolate, month_day, ShowCalendar::kAuto);
#endif  //  V8_INTL_SUPPORT
}

MaybeDirectHandle<JSTemporalPlainYearMonth>
JSTemporalPlainYearMonth::Constructor(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, DirectHandle<Object> iso_year_obj,
    DirectHandle<Object> iso_month_obj, DirectHandle<Object> calendar_like,
    DirectHandle<Object> reference_iso_day_obj) {
  const char* method_name = "Temporal.PlainYearMonth";
  // 1. If NewTarget is undefined, throw a TypeError exception.
  if (IsUndefined(*new_target)) {
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                                 isolate->factory()->NewStringFromAsciiChecked(
                                     method_name)));
  }
  // 7. Let calendar be ? ToTemporalCalendarWithISODefault(calendarLike).
  // 10. Return ? CreateTemporalYearMonth(y, m, calendar, ref, NewTarget).

  // 3. Let y be ? ToIntegerThrowOnInfinity(isoYear).
  TO_INT_THROW_ON_INFTY(iso_year, JSTemporalPlainYearMonth);
  // 5. Let m be ? ToIntegerThrowOnInfinity(isoMonth).
  TO_INT_THROW_ON_INFTY(iso_month, JSTemporalPlainYearMonth);
  // 7. Let calendar be ? ToTemporalCalendarWithISODefault(calendarLike).
  DirectHandle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar,
      ToTemporalCalendarWithISODefault(isolate, calendar_like, method_name));

  // 2. If referenceISODay is undefined, then
  // a. Set referenceISODay to 1𝔽.
  // ...
  // 8. Let ref be ? ToIntegerThrowOnInfinity(referenceISODay).
  int32_t ref = 1;
  if (!IsUndefined(*reference_iso_day_obj)) {
    TO_INT_THROW_ON_INFTY(reference_iso_day, JSTemporalPlainYearMonth);
    ref = reference_iso_day;
  }

  // 10. Return ? CreateTemporalYearMonth(y, m, calendar, ref, NewTarget).
  return CreateTemporalYearMonth(isolate, target, new_target, iso_year,
                                 iso_month, calendar, ref);
}

namespace {

// #sec-temporal-parsetemporalyearmonthstring
Maybe<DateRecordWithCalendar> ParseTemporalYearMonthString(
    Isolate* isolate, DirectHandle<String> iso_string) {
  TEMPORAL_ENTER_FUNC();

  // 1. Assert: Type(isoString) is String.
  // 2. If isoString does not satisfy the syntax of a TemporalYearMonthString
  // (see 13.33), then
  std::optional<ParsedISO8601Result> parsed =
      TemporalParser::ParseTemporalYearMonthString(isolate, iso_string);
  if (!parsed.has_value()) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<DateRecordWithCalendar>());
  }

  // 3. If _isoString_ contains a |UTCDesignator|, then
  if (parsed->utc_designator) {
    // a. Throw a *RangeError* exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<DateRecordWithCalendar>());
  }

  // 3. Let result be ? ParseISODateTime(isoString).
  DateTimeRecordWithCalendar result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result, ParseISODateTime(isolate, iso_string, *parsed),
      Nothing<DateRecordWithCalendar>());

  // 4. Return the Record { [[Year]]: result.[[Year]], [[Month]]:
  // result.[[Month]], [[Day]]: result.[[Day]], [[Calendar]]:
  // result.[[Calendar]] }.
  DateRecordWithCalendar ret = {
      {result.date.year, result.date.month, result.date.day}, result.calendar};
  return Just(ret);
}

// #sec-temporal-totemporalyearmonth
MaybeDirectHandle<JSTemporalPlainYearMonth> ToTemporalYearMonth(
    Isolate* isolate, DirectHandle<Object> item_obj,
    DirectHandle<Object> options, const char* method_name) {
  TEMPORAL_ENTER_FUNC();

  // 2. Assert: Type(options) is Object or Undefined.
  DCHECK(IsJSReceiver(*options) || IsUndefined(*options));
  // 3. If Type(item) is Object, then
  if (IsJSReceiver(*item_obj)) {
    DirectHandle<JSReceiver> item = Cast<JSReceiver>(item_obj);
    // a. If item has an [[InitializedTemporalYearMonth]] internal slot, then
    // i. Return item.
    if (IsJSTemporalPlainYearMonth(*item_obj)) {
      return Cast<JSTemporalPlainYearMonth>(item_obj);
    }

    // b. Let calendar be ? GetTemporalCalendarWithISODefault(item).
    DirectHandle<JSReceiver> calendar;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, calendar,
        GetTemporalCalendarWithISODefault(isolate, item, method_name));
    // c. Let fieldNames be ? CalendarFields(calendar, « "month", "monthCode",
    // "year" »).
    DirectHandle<FixedArray> field_names =
        MonthMonthCodeYearInFixedArray(isolate);
    ASSIGN_RETURN_ON_EXCEPTION(isolate, field_names,
                               CalendarFields(isolate, calendar, field_names));
    // d. Let fields be ? PrepareTemporalFields(item, fieldNames, «»).
    DirectHandle<JSReceiver> fields;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, fields,
                               PrepareTemporalFields(isolate, item, field_names,
                                                     RequiredFields::kNone));
    // e. Return ? YearMonthFromFields(calendar, fields, options).
    return YearMonthFromFields(isolate, calendar, fields, options);
  }
  // 4. Perform ? ToTemporalOverflow(options).
  MAYBE_RETURN_ON_EXCEPTION_VALUE(
      isolate, ToTemporalOverflow(isolate, options, method_name),
      DirectHandle<JSTemporalPlainYearMonth>());
  // 5. Let string be ? ToString(item).
  DirectHandle<String> string;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, string,
                             Object::ToString(isolate, item_obj));
  // 6. Let result be ? ParseTemporalYearMonthString(string).
  DateRecordWithCalendar result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result, ParseTemporalYearMonthString(isolate, string),
      DirectHandle<JSTemporalPlainYearMonth>());
  // 7. Let calendar be ? ToTemporalCalendarWithISODefault(result.[[Calendar]]).
  DirectHandle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar,
      ToTemporalCalendarWithISODefault(isolate, result.calendar, method_name));
  // 8. Set result to ? CreateTemporalYearMonth(result.[[Year]],
  // result.[[Month]], calendar, result.[[Day]]).
  DirectHandle<JSTemporalPlainYearMonth> created_result;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, created_result,
      CreateTemporalYearMonth(isolate, result.date.year, result.date.month,
                              calendar, result.date.day));
  // 9. NOTE: The following operation is called without options, in order for
  // the calendar to store a canonical value in the [[ISODay]] internal slot of
  // the result.
  // 10. Return ? CalendarYearMonthFromFields(calendar, result).
  return YearMonthFromFields(isolate, calendar, created_result);
}

MaybeDirectHandle<JSTemporalPlainYearMonth> ToTemporalYearMonth(
    Isolate* isolate, DirectHandle<Object> item_obj, const char* method_name) {
  // 1. If options is not present, set options to undefined.
  return ToTemporalYearMonth(
      isolate, item_obj, isolate->factory()->undefined_value(), method_name);
}

}  // namespace

// #sec-temporal.plainyearmonth.from
MaybeDirectHandle<JSTemporalPlainYearMonth> JSTemporalPlainYearMonth::From(
    Isolate* isolate, DirectHandle<Object> item,
    DirectHandle<Object> options_obj) {
  const char* method_name = "Temporal.PlainYearMonth.from";
  // 1. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));
  // 2. If Type(item) is Object and item has an [[InitializedTemporalYearMonth]]
  // internal slot, then
  if (IsJSTemporalPlainYearMonth(*item)) {
    // a. Perform ? ToTemporalOverflow(options).
    MAYBE_RETURN_ON_EXCEPTION_VALUE(
        isolate, ToTemporalOverflow(isolate, options, method_name),
        DirectHandle<JSTemporalPlainYearMonth>());
    // b. Return ? CreateTemporalYearMonth(item.[[ISOYear]], item.[[ISOMonth]],
    // item.[[Calendar]], item.[[ISODay]]).
    auto year_month = Cast<JSTemporalPlainYearMonth>(item);
    return CreateTemporalYearMonth(
        isolate, year_month->iso_year(), year_month->iso_month(),
        direct_handle(year_month->calendar(), isolate), year_month->iso_day());
  }
  // 3. Return ? ToTemporalYearMonth(item, options).
  return ToTemporalYearMonth(isolate, item, options, method_name);
}

// #sec-temporal.plainyearmonth.compare
MaybeDirectHandle<Smi> JSTemporalPlainYearMonth::Compare(
    Isolate* isolate, DirectHandle<Object> one_obj,
    DirectHandle<Object> two_obj) {
  const char* method_name = "Temporal.PlainYearMonth.compare";
  // 1. Set one to ? ToTemporalYearMonth(one).
  DirectHandle<JSTemporalPlainYearMonth> one;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, one, ToTemporalYearMonth(isolate, one_obj, method_name));
  // 2. Set two to ? ToTemporalYearMonth(two).
  DirectHandle<JSTemporalPlainYearMonth> two;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, two, ToTemporalYearMonth(isolate, two_obj, method_name));
  // 3. Return 𝔽(! CompareISODate(one.[[ISOYear]], one.[[ISOMonth]],
  // one.[[ISODay]], two.[[ISOYear]], two.[[ISOMonth]], two.[[ISODay]])).
  return direct_handle(
      Smi::FromInt(
          CompareISODate({one->iso_year(), one->iso_month(), one->iso_day()},
                         {two->iso_year(), two->iso_month(), two->iso_day()})),
      isolate);
}

// #sec-temporal.plainyearmonth.prototype.equals
MaybeDirectHandle<Oddball> JSTemporalPlainYearMonth::Equals(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month,
    DirectHandle<Object> other_obj) {
  // 1. Let yearMonth be the this value.
  // 2. Perform ? RequireInternalSlot(yearMonth,
  // [[InitializedTemporalYearMonth]]).
  // 3. Set other to ? ToTemporalYearMonth(other).
  DirectHandle<JSTemporalPlainYearMonth> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other,
      ToTemporalYearMonth(isolate, other_obj,
                          "Temporal.PlainYearMonth.prototype.equals"));
  // 4. If yearMonth.[[ISOYear]] ≠ other.[[ISOYear]], return false.
  if (year_month->iso_year() != other->iso_year())
    return isolate->factory()->false_value();
  // 5. If yearMonth.[[ISOMonth]] ≠ other.[[ISOMonth]], return false.
  if (year_month->iso_month() != other->iso_month())
    return isolate->factory()->false_value();
  // 6. If yearMonth.[[ISODay]] ≠ other.[[ISODay]], return false.
  if (year_month->iso_day() != other->iso_day())
    return isolate->factory()->false_value();
  // 7. Return ? CalendarEquals(yearMonth.[[Calendar]], other.[[Calendar]]).
  return CalendarEquals(
      isolate, DirectHandle<JSReceiver>(year_month->calendar(), isolate),
      DirectHandle<JSReceiver>(other->calendar(), isolate));
}

namespace {

MaybeDirectHandle<JSTemporalPlainYearMonth>
AddDurationToOrSubtractDurationFromPlainYearMonth(
    Isolate* isolate, Arithmetic operation,
    DirectHandle<JSTemporalPlainYearMonth> year_month,
    DirectHandle<Object> temporal_duration_like,
    DirectHandle<Object> options_obj, const char* method_name) {
  // 1. Let duration be ? ToTemporalDurationRecord(temporalDurationLike).
  DurationRecord duration;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, duration,
      temporal::ToTemporalDurationRecord(isolate, temporal_duration_like,
                                         method_name),
      DirectHandle<JSTemporalPlainYearMonth>());

  // 2. If operation is subtract, then
  if (operation == Arithmetic::kSubtract) {
    // a. Set duration to ! CreateNegatedDurationRecord(duration).
    duration = CreateNegatedDurationRecord(isolate, duration).ToChecked();
  }
  // 3. Let balanceResult be ? BalanceDuration(duration.[[Days]],
  // duration.[[Hours]], duration.[[Minutes]], duration.[[Seconds]],
  // duration.[[Milliseconds]], duration.[[Microseconds]],
  // duration.[[Nanoseconds]], "day").
  TimeDurationRecord balance_result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, balance_result,
      BalanceDuration(isolate, Unit::kDay, duration.time_duration, method_name),
      DirectHandle<JSTemporalPlainYearMonth>());
  // 4. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));
  // 5. Let calendar be yearMonth.[[Calendar]].
  DirectHandle<JSReceiver> calendar(year_month->calendar(), isolate);

  // 6. Let fieldNames be ? CalendarFields(calendar, « "monthCode", "year" »).
  Factory* factory = isolate->factory();
  DirectHandle<FixedArray> field_names = MonthCodeYearInFixedArray(isolate);
  ASSIGN_RETURN_ON_EXCEPTION(isolate, field_names,
                             CalendarFields(isolate, calendar, field_names));

  // 7. Let fields be ? PrepareTemporalFields(yearMonth, fieldNames, «»).
  DirectHandle<JSReceiver> fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, fields,
      PrepareTemporalFields(isolate, year_month, field_names,
                            RequiredFields::kNone));

  // 8. Set sign to ! DurationSign(duration.[[Years]], duration.[[Months]],
  // duration.[[Weeks]], balanceResult.[[Days]], 0, 0, 0, 0, 0, 0).
  int32_t sign =
      DurationRecord::Sign({duration.years,
                            duration.months,
                            duration.weeks,
                            {balance_result.days, 0, 0, 0, 0, 0, 0}});

  // 9. If sign < 0, then
  DirectHandle<Object> day;
  if (sign < 0) {
    // a. Let dayFromCalendar be ? CalendarDaysInMonth(calendar, yearMonth).
    DirectHandle<Object> day_from_calendar;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, day_from_calendar,
        temporal::CalendarDaysInMonth(isolate, calendar, year_month));

    // b. Let day be ? ToPositiveInteger(dayFromCalendar).
    ASSIGN_RETURN_ON_EXCEPTION(isolate, day,
                               ToPositiveInteger(isolate, day_from_calendar));
    // 10. Else,
  } else {
    // a. Let day be 1.
    day = direct_handle(Smi::FromInt(1), isolate);
  }
  // 11. Perform ! CreateDataPropertyOrThrow(fields, "day", day).
  CHECK(JSReceiver::CreateDataProperty(isolate, fields, factory->day_string(),
                                       day, Just(kThrowOnError))
            .FromJust());

  // 12. Let date be ? CalendarDateFromFields(calendar, fields).
  DirectHandle<JSTemporalPlainDate> date;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, date,
      FromFields<JSTemporalPlainDate>(
          isolate, calendar, fields, isolate->factory()->undefined_value(),
          isolate->factory()->dateFromFields_string(),
          JS_TEMPORAL_PLAIN_DATE_TYPE));

  // 13. Let durationToAdd be ! CreateTemporalDuration(duration.[[Years]],
  // duration.[[Months]], duration.[[Weeks]], balanceResult.[[Days]], 0, 0, 0,
  // 0, 0, 0).
  DirectHandle<JSTemporalDuration> duration_to_add =
      CreateTemporalDuration(isolate, {duration.years,
                                       duration.months,
                                       duration.weeks,
                                       {balance_result.days, 0, 0, 0, 0, 0, 0}})
          .ToHandleChecked();
  // 14. Let optionsCopy be OrdinaryObjectCreate(null).
  DirectHandle<JSReceiver> options_copy =
      isolate->factory()->NewJSObjectWithNullProto();

  // 15. Let entries be ? EnumerableOwnPropertyNames(options, key+value).
  // 16. For each element nextEntry of entries, do
  // a. Perform ! CreateDataPropertyOrThrow(optionsCopy, nextEntry[0],
  // nextEntry[1]).
  bool set;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, set,
      JSReceiver::SetOrCopyDataProperties(
          isolate, options_copy, options,
          PropertiesEnumerationMode::kEnumerationOrder, {}, false),
      DirectHandle<JSTemporalPlainYearMonth>());

  // 17. Let addedDate be ? CalendarDateAdd(calendar, date, durationToAdd,
  // options).
  DirectHandle<JSTemporalPlainDate> added_date;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, added_date,
      CalendarDateAdd(isolate, calendar, date, duration_to_add, options));
  // 18. Let addedDateFields be ? PrepareTemporalFields(addedDate, fieldNames,
  // «»).
  DirectHandle<JSReceiver> added_date_fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, added_date_fields,
      PrepareTemporalFields(isolate, added_date, field_names,
                            RequiredFields::kNone));
  // 19. Return ? CalendarYearMonthFromFields(calendar, addedDateFields,
  // optionsCopy).
  return FromFields<JSTemporalPlainYearMonth>(
      isolate, calendar, added_date_fields, options_copy,
      isolate->factory()->yearMonthFromFields_string(),
      JS_TEMPORAL_PLAIN_YEAR_MONTH_TYPE);
}

}  // namespace

// #sec-temporal.plainyearmonth.prototype.add
MaybeDirectHandle<JSTemporalPlainYearMonth> JSTemporalPlainYearMonth::Add(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month,
    DirectHandle<Object> temporal_duration_like, DirectHandle<Object> options) {
  return AddDurationToOrSubtractDurationFromPlainYearMonth(
      isolate, Arithmetic::kAdd, year_month, temporal_duration_like, options,
      "Temporal.PlainYearMonth.prototype.add");
}

// #sec-temporal.plainyearmonth.prototype.subtract
MaybeDirectHandle<JSTemporalPlainYearMonth> JSTemporalPlainYearMonth::Subtract(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month,
    DirectHandle<Object> temporal_duration_like, DirectHandle<Object> options) {
  return AddDurationToOrSubtractDurationFromPlainYearMonth(
      isolate, Arithmetic::kSubtract, year_month, temporal_duration_like,
      options, "Temporal.PlainYearMonth.prototype.subtract");
}

namespace {
// #sec-temporal-differencetemporalplandyearmonth
MaybeDirectHandle<JSTemporalDuration> DifferenceTemporalPlainYearMonth(
    Isolate* isolate, TimePreposition operation,
    DirectHandle<JSTemporalPlainYearMonth> year_month,
    DirectHandle<Object> other_obj, DirectHandle<Object> options,
    const char* method_name) {
  TEMPORAL_ENTER_FUNC();
  // 1. If operation is since, let sign be -1. Otherwise, let sign be 1.
  double sign = operation == TimePreposition::kSince ? -1 : 1;
  // 2. Set other to ? ToTemporalDateTime(other).
  DirectHandle<JSTemporalPlainYearMonth> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other, ToTemporalYearMonth(isolate, other_obj, method_name));
  // 3. Let calendar be yearMonth.[[Calendar]].
  DirectHandle<JSReceiver> calendar(year_month->calendar(), isolate);

  // 4. If ? CalendarEquals(calendar, other.[[Calendar]]) is false, throw a
  // RangeError exception.
  bool calendar_equals;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, calendar_equals,
      CalendarEqualsBool(isolate, calendar,
                         direct_handle(other->calendar(), isolate)),
      DirectHandle<JSTemporalDuration>());
  if (!calendar_equals) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }

  // 5. Let settings be ? GetDifferenceSettings(operation, options, date, «
  // "week", "day" », "month", "year").
  DifferenceSettings settings;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, settings,
      GetDifferenceSettings(isolate, operation, options, UnitGroup::kDate,
                            DisallowedUnitsInDifferenceSettings::kWeekAndDay,
                            Unit::kMonth, Unit::kYear, method_name),
      DirectHandle<JSTemporalDuration>());
  // 6. Let fieldNames be ? CalendarFields(calendar, « "monthCode", "year" »).
  Factory* factory = isolate->factory();
  DirectHandle<FixedArray> field_names = MonthCodeYearInFixedArray(isolate);
  ASSIGN_RETURN_ON_EXCEPTION(isolate, field_names,
                             CalendarFields(isolate, calendar, field_names));

  // 7. Let otherFields be ? PrepareTemporalFields(other, fieldNames, «»).
  DirectHandle<JSReceiver> other_fields;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, other_fields,
                             PrepareTemporalFields(isolate, other, field_names,
                                                   RequiredFields::kNone));
  // 8. Perform ! CreateDataPropertyOrThrow(otherFields, "day", 1𝔽).
  DirectHandle<Object> one(Smi::FromInt(1), isolate);
  CHECK(JSReceiver::CreateDataProperty(isolate, other_fields,
                                       factory->day_string(), one,
                                       Just(kThrowOnError))
            .FromJust());
  // 9. Let otherDate be ? CalendarDateFromFields(calendar, otherFields).
  //  DateFromFields(Isolate* isolate,
  DirectHandle<JSTemporalPlainDate> other_date;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other_date,
      DateFromFields(isolate, calendar, other_fields,
                     isolate->factory()->undefined_value()));
  // 10. Let thisFields be ? PrepareTemporalFields(yearMonth, fieldNames, «»).
  DirectHandle<JSReceiver> this_fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, this_fields,
      PrepareTemporalFields(isolate, year_month, field_names,
                            RequiredFields::kNone));
  // 11. Perform ! CreateDataPropertyOrThrow(thisFields, "day", 1𝔽).
  CHECK(JSReceiver::CreateDataProperty(isolate, this_fields,
                                       factory->day_string(), one,
                                       Just(kThrowOnError))
            .FromJust());
  // 12. Let thisDate be ? CalendarDateFromFields(calendar, thisFields).
  DirectHandle<JSTemporalPlainDate> this_date;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, this_date,
      DateFromFields(isolate, calendar, this_fields,
                     isolate->factory()->undefined_value()));
  // 13. Let untilOptions be ? MergeLargestUnitOption(settings.[[Options]],
  // settings.[[LargestUnit]]).
  DirectHandle<JSReceiver> until_options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, until_options,
      MergeLargestUnitOption(isolate, settings.options, settings.largest_unit));
  // 14. Let result be ? CalendarDateUntil(calendar, thisDate, otherDate,
  // untilOptions).
  DirectHandle<JSTemporalDuration> result;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, result,
                             CalendarDateUntil(isolate, calendar, this_date,
                                               other_date, until_options));

  // 15. If settings.[[SmallestUnit]] is not "month" or
  // settings.[[RoundingIncrement]] ≠ 1, then
  if (settings.smallest_unit != Unit::kMonth ||
      settings.rounding_increment != 1) {
    // a. Set result to (? RoundDuration(result.[[Years]], result.[[Months]], 0,
    // 0, 0, 0, 0, 0, 0, 0, settings.[[RoundingIncrement]],
    // settings.[[SmallestUnit]], settings.[[RoundingMode]],
    // thisDate)).[[DurationRecord]].
    DurationRecordWithRemainder round_result;
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, round_result,
        RoundDuration(isolate,
                      {Object::NumberValue(result->years()),
                       Object::NumberValue(result->months()),
                       0,
                       {0, 0, 0, 0, 0, 0, 0}},
                      settings.rounding_increment, settings.smallest_unit,
                      settings.rounding_mode, this_date, method_name),
        DirectHandle<JSTemporalDuration>());
    // 16. Return ! CreateTemporalDuration(sign × result.[[Years]], sign ×
    // result.[[Months]], 0, 0, 0, 0, 0, 0, 0, 0).
    return CreateTemporalDuration(isolate, {round_result.record.years * sign,
                                            round_result.record.months * sign,
                                            0,
                                            {0, 0, 0, 0, 0, 0, 0}})
        .ToHandleChecked();
  }
  // 16. Return ! CreateTemporalDuration(sign × result.[[Years]], sign ×
  // result.[[Months]], 0, 0, 0, 0, 0, 0, 0, 0).
  return CreateTemporalDuration(isolate,
                                {Object::NumberValue(result->years()) * sign,
                                 Object::NumberValue(result->months()) * sign,
                                 0,
                                 {0, 0, 0, 0, 0, 0, 0}})
      .ToHandleChecked();
}

}  // namespace

// #sec-temporal.plainyearmonth.prototype.until
MaybeDirectHandle<JSTemporalDuration> JSTemporalPlainYearMonth::Until(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  return DifferenceTemporalPlainYearMonth(
      isolate, TimePreposition::kUntil, handle, other, options,
      "Temporal.PlainYearMonth.prototype.until");
}

// #sec-temporal.plainyearmonth.prototype.since
MaybeDirectHandle<JSTemporalDuration> JSTemporalPlainYearMonth::Since(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  return DifferenceTemporalPlainYearMonth(
      isolate, TimePreposition::kSince, handle, other, options,
      "Temporal.PlainYearMonth.prototype.since");
}

// #sec-temporal.plainyearmonth.prototype.with
MaybeDirectHandle<JSTemporalPlainYearMonth> JSTemporalPlainYearMonth::With(
    Isolate* isolate,
    DirectHandle<JSTemporalPlainYearMonth> temporal_year_month,
    DirectHandle<Object> temporal_year_month_like_obj,
    DirectHandle<Object> options_obj) {
  // 6. Let fieldNames be ? CalendarFields(calendar, « "month", "monthCode",
  // "year" »).
  DirectHandle<FixedArray> field_names =
      MonthMonthCodeYearInFixedArray(isolate);
  return PlainDateOrYearMonthOrMonthDayWith<JSTemporalPlainYearMonth,
                                            YearMonthFromFields>(
      isolate, temporal_year_month, temporal_year_month_like_obj, options_obj,
      field_names, "Temporal.PlainYearMonth.prototype.with");
}

// #sec-temporal.plainyearmonth.prototype.toplaindate
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainYearMonth::ToPlainDate(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month,
    DirectHandle<Object> item_obj) {
  Factory* factory = isolate->factory();
  // 5. Let receiverFieldNames be ? CalendarFields(calendar, « "monthCode",
  // "year" »).
  // 7. Let inputFieldNames be ? CalendarFields(calendar, « "day" »).
  return PlainMonthDayOrYearMonthToPlainDate<JSTemporalPlainYearMonth>(
      isolate, year_month, item_obj, factory->monthCode_string(),
      factory->year_string(), factory->day_string());
}

// #sec-temporal.plainyearmonth.prototype.getisofields
MaybeDirectHandle<JSReceiver> JSTemporalPlainYearMonth::GetISOFields(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month) {
  Factory* factory = isolate->factory();
  // 1. Let yearMonth be the this value.
  // 2. Perform ? RequireInternalSlot(yearMonth,
  // [[InitializedTemporalYearMonth]]).
  // 3. Let fields be ! OrdinaryObjectCreate(%Object.prototype%).
  DirectHandle<JSObject> fields =
      isolate->factory()->NewJSObject(isolate->object_function());
  // 4. Perform ! CreateDataPropertyOrThrow(fields, "calendar",
  // yearMonth.[[Calendar]]).
  CHECK(JSReceiver::CreateDataProperty(
            isolate, fields, factory->calendar_string(),
            DirectHandle<JSReceiver>(year_month->calendar(), isolate),
            Just(kThrowOnError))
            .FromJust());
  // 5. Perform ! CreateDataPropertyOrThrow(fields, "isoDay",
  // 𝔽(yearMonth.[[ISODay]])).
  // 6. Perform ! CreateDataPropertyOrThrow(fields, "isoMonth",
  // 𝔽(yearMonth.[[ISOMonth]])).
  // 7. Perform ! CreateDataPropertyOrThrow(fields, "isoYear",
  // 𝔽(yearMonth.[[ISOYear]])).
  DEFINE_INT_FIELD(fields, isoDay, iso_day, year_month)
  DEFINE_INT_FIELD(fields, isoMonth, iso_month, year_month)
  DEFINE_INT_FIELD(fields, isoYear, iso_year, year_month)
  // 8. Return fields.
  return fields;
}

// #sec-temporal.plainyearmonth.prototype.tojson
MaybeDirectHandle<String> JSTemporalPlainYearMonth::ToJSON(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month) {
  return TemporalYearMonthToString(isolate, year_month, ShowCalendar::kAuto);
}

// #sec-temporal.plainyearmonth.prototype.tostring
MaybeDirectHandle<String> JSTemporalPlainYearMonth::ToString(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month,
    DirectHandle<Object> options) {
  return TemporalToString<JSTemporalPlainYearMonth, TemporalYearMonthToString>(
      isolate, year_month, options,
      "Temporal.PlainYearMonth.prototype.toString");
}

// #sec-temporal.plainyearmonth.prototype.tolocalestring
MaybeDirectHandle<String> JSTemporalPlainYearMonth::ToLocaleString(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month,
    DirectHandle<Object> locales, DirectHandle<Object> options) {
#ifdef V8_INTL_SUPPORT
  return JSDateTimeFormat::TemporalToLocaleString(
      isolate, year_month, locales, options,
      "Temporal.PlainYearMonth.prototype.toLocaleString");
#else   //  V8_INTL_SUPPORT
  return TemporalYearMonthToString(isolate, year_month, ShowCalendar::kAuto);
#endif  //  V8_INTL_SUPPORT
}

// #sec-temporal-plaintime-constructor
MaybeDirectHandle<JSTemporalPlainTime> JSTemporalPlainTime::Constructor(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, DirectHandle<Object> hour_obj,
    DirectHandle<Object> minute_obj, DirectHandle<Object> second_obj,
    DirectHandle<Object> millisecond_obj, DirectHandle<Object> microsecond_obj,
    DirectHandle<Object> nanosecond_obj) {
  const char* method_name = "Temporal.PlainTime";
  // 1. If NewTarget is undefined, then
  // a. Throw a TypeError exception.
  if (IsUndefined(*new_target)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                                 isolate->factory()->NewStringFromAsciiChecked(
                                     method_name)));
  }

  TO_INT_THROW_ON_INFTY(hour, JSTemporalPlainTime);
  TO_INT_THROW_ON_INFTY(minute, JSTemporalPlainTime);
  TO_INT_THROW_ON_INFTY(second, JSTemporalPlainTime);
  TO_INT_THROW_ON_INFTY(millisecond, JSTemporalPlainTime);
  TO_INT_THROW_ON_INFTY(microsecond, JSTemporalPlainTime);
  TO_INT_THROW_ON_INFTY(nanosecond, JSTemporalPlainTime);

  // 14. Return ? CreateTemporalTime(hour, minute, second, millisecond,
  // microsecond, nanosecond, NewTarget).
  return CreateTemporalTime(
      isolate, target, new_target,
      {hour, minute, second, millisecond, microsecond, nanosecond});
}

// #sec-temporal.plaintime.prototype.tozoneddatetime
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalPlainTime::ToZonedDateTime(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time,
    DirectHandle<Object> item_obj) {
  const char* method_name = "Temporal.PlainTime.prototype.toZonedDateTime";
  Factory* factory = isolate->factory();
  // 1. Let temporalTime be the this value.
  // 2. Perform ? RequireInternalSlot(temporalTime,
  // [[InitializedTemporalTime]]).
  // 3. If Type(item) is not Object, then
  if (!IsJSReceiver(*item_obj)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }
  DirectHandle<JSReceiver> item = Cast<JSReceiver>(item_obj);
  // 4. Let temporalDateLike be ? Get(item, "plainDate").
  DirectHandle<Object> temporal_date_like;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temporal_date_like,
      JSReceiver::GetProperty(isolate, item, factory->plainDate_string()));
  // 5. If temporalDateLike is undefined, then
  if (IsUndefined(*temporal_date_like)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }
  // 6. Let temporalDate be ? ToTemporalDate(temporalDateLike).
  DirectHandle<JSTemporalPlainDate> temporal_date;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temporal_date,
      ToTemporalDate(isolate, temporal_date_like, method_name));
  // 7. Let temporalTimeZoneLike be ? Get(item, "timeZone").
  DirectHandle<Object> temporal_time_zone_like;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temporal_time_zone_like,
      JSReceiver::GetProperty(isolate, item, factory->timeZone_string()));
  // 8. If temporalTimeZoneLike is undefined, then
  if (IsUndefined(*temporal_time_zone_like)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }
  // 9. Let timeZone be ? ToTemporalTimeZone(temporalTimeZoneLike).
  DirectHandle<JSReceiver> time_zone;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, time_zone,
      temporal::ToTemporalTimeZone(isolate, temporal_time_zone_like,
                                   method_name));
  // 10. Let temporalDateTime be ?
  // CreateTemporalDateTime(temporalDate.[[ISOYear]], temporalDate.[[ISOMonth]],
  // temporalDate.[[ISODay]], temporalTime.[[ISOHour]],
  // temporalTime.[[ISOMinute]], temporalTime.[[ISOSecond]],
  // temporalTime.[[ISOMillisecond]], temporalTime.[[ISOMicrosecond]],
  // temporalTime.[[ISONanosecond]], temporalDate.[[Calendar]]).
  DirectHandle<JSReceiver> calendar(temporal_date->calendar(), isolate);
  DirectHandle<JSTemporalPlainDateTime> temporal_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temporal_date_time,
      temporal::CreateTemporalDateTime(
          isolate,
          {{temporal_date->iso_year(), temporal_date->iso_month(),
            temporal_date->iso_day()},
           {temporal_time->iso_hour(), temporal_time->iso_minute(),
            temporal_time->iso_second(), temporal_time->iso_millisecond(),
            temporal_time->iso_microsecond(), temporal_time->iso_nanosecond()}},
          calendar));
  // 11. Let instant be ? BuiltinTimeZoneGetInstantFor(timeZone,
  // temporalDateTime, "compatible").
  DirectHandle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant,
      BuiltinTimeZoneGetInstantFor(isolate, time_zone, temporal_date_time,
                                   Disambiguation::kCompatible, method_name));
  // 12. Return ? CreateTemporalZonedDateTime(instant.[[Nanoseconds]], timeZone,
  // temporalDate.[[Calendar]]).
  return CreateTemporalZonedDateTime(
      isolate, direct_handle(instant->nanoseconds(), isolate), time_zone,
      calendar);
}

namespace {
// #sec-temporal-comparetemporaltime
int32_t CompareTemporalTime(const TimeRecord& time1, const TimeRecord& time2) {
  TEMPORAL_ENTER_FUNC();

  // 1. Assert: h1, min1, s1, ms1, mus1, ns1, h2, min2, s2, ms2, mus2, and ns2
  // are integers.
  // 2. If h1 > h2, return 1.
  if (time1.hour > time2.hour) return 1;
  // 3. If h1 < h2, return -1.
  if (time1.hour < time2.hour) return -1;
  // 4. If min1 > min2, return 1.
  if (time1.minute > time2.minute) return 1;
  // 5. If min1 < min2, return -1.
  if (time1.minute < time2.minute) return -1;
  // 6. If s1 > s2, return 1.
  if (time1.second > time2.second) return 1;
  // 7. If s1 < s2, return -1.
  if (time1.second < time2.second) return -1;
  // 8. If ms1 > ms2, return 1.
  if (time1.millisecond > time2.millisecond) return 1;
  // 9. If ms1 < ms2, return -1.
  if (time1.millisecond < time2.millisecond) return -1;
  // 10. If mus1 > mus2, return 1.
  if (time1.microsecond > time2.microsecond) return 1;
  // 11. If mus1 < mus2, return -1.
  if (time1.microsecond < time2.microsecond) return -1;
  // 12. If ns1 > ns2, return 1.
  if (time1.nanosecond > time2.nanosecond) return 1;
  // 13. If ns1 < ns2, return -1.
  if (time1.nanosecond < time2.nanosecond) return -1;
  // 14. Return 0.
  return 0;
}
}  // namespace

// #sec-temporal.plaintime.compare
MaybeDirectHandle<Smi> JSTemporalPlainTime::Compare(
    Isolate* isolate, DirectHandle<Object> one_obj,
    DirectHandle<Object> two_obj) {
  const char* method_name = "Temporal.PainTime.compare";
  // 1. Set one to ? ToTemporalTime(one).
  DirectHandle<JSTemporalPlainTime> one;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, one, temporal::ToTemporalTime(isolate, one_obj, method_name));
  // 2. Set two to ? ToTemporalTime(two).
  DirectHandle<JSTemporalPlainTime> two;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, two, temporal::ToTemporalTime(isolate, two_obj, method_name));
  // 3. Return 𝔽(! CompareTemporalTime(one.[[ISOHour]], one.[[ISOMinute]],
  // one.[[ISOSecond]], one.[[ISOMillisecond]], one.[[ISOMicrosecond]],
  // one.[[ISONanosecond]], two.[[ISOHour]], two.[[ISOMinute]],
  // two.[[ISOSecond]], two.[[ISOMillisecond]], two.[[ISOMicrosecond]],
  // two.[[ISONanosecond]])).
  return direct_handle(Smi::FromInt(CompareTemporalTime(
                           {one->iso_hour(), one->iso_minute(),
                            one->iso_second(), one->iso_millisecond(),
                            one->iso_microsecond(), one->iso_nanosecond()},
                           {two->iso_hour(), two->iso_minute(),
                            two->iso_second(), two->iso_millisecond(),
                            two->iso_microsecond(), two->iso_nanosecond()})),
                       isolate);
}

// #sec-temporal.plaintime.prototype.equals
MaybeDirectHandle<Oddball> JSTemporalPlainTime::Equals(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time,
    DirectHandle<Object> other_obj) {
  // 1. Let temporalTime be the this value.
  // 2. Perform ? RequireInternalSlot(temporalTime,
  // [[InitializedTemporalTime]]).
  // 3. Set other to ? ToTemporalTime(other).
  DirectHandle<JSTemporalPlainTime> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other,
      temporal::ToTemporalTime(isolate, other_obj,
                               "Temporal.PlainTime.prototype.equals"));
  // 4. If temporalTime.[[ISOHour]] ≠ other.[[ISOHour]], return false.
  if (temporal_time->iso_hour() != other->iso_hour())
    return isolate->factory()->false_value();
  // 5. If temporalTime.[[ISOMinute]] ≠ other.[[ISOMinute]], return false.
  if (temporal_time->iso_minute() != other->iso_minute())
    return isolate->factory()->false_value();
  // 6. If temporalTime.[[ISOSecond]] ≠ other.[[ISOSecond]], return false.
  if (temporal_time->iso_second() != other->iso_second())
    return isolate->factory()->false_value();
  // 7. If temporalTime.[[ISOMillisecond]] ≠ other.[[ISOMillisecond]], return
  // false.
  if (temporal_time->iso_millisecond() != other->iso_millisecond())
    return isolate->factory()->false_value();
  // 8. If temporalTime.[[ISOMicrosecond]] ≠ other.[[ISOMicrosecond]], return
  // false.
  if (temporal_time->iso_microsecond() != other->iso_microsecond())
    return isolate->factory()->false_value();
  // 9. If temporalTime.[[ISONanosecond]] ≠ other.[[ISONanosecond]], return
  // false.
  if (temporal_time->iso_nanosecond() != other->iso_nanosecond())
    return isolate->factory()->false_value();
  // 10. Return true.
  return isolate->factory()->true_value();
}

namespace {

// #sec-temporal-maximumtemporaldurationroundingincrement
Maximum MaximumTemporalDurationRoundingIncrement(Unit unit) {
  switch (unit) {
    // 1. If unit is "year", "month", "week", or "day", then
    case Unit::kYear:
    case Unit::kMonth:
    case Unit::kWeek:
    case Unit::kDay:
      // a. Return undefined.
      return {false, 0};
    // 2. If unit is "hour", then
    case Unit::kHour:
      // a. Return 24.
      return {true, 24};
    // 3. If unit is "minute" or "second", then
    case Unit::kMinute:
    case Unit::kSecond:
      // a. Return 60.
      return {true, 60};
    // 4. Assert: unit is one of "millisecond", "microsecond", or "nanosecond".
    case Unit::kMillisecond:
    case Unit::kMicrosecond:
    case Unit::kNanosecond:
      // 5. Return 1000.
      return {true, 1000};
    default:
      UNREACHABLE();
  }
}

}  // namespace

// #sec-temporal.plaintime.prototype.round
MaybeDirectHandle<JSTemporalPlainTime> JSTemporalPlainTime::Round(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time,
    DirectHandle<Object> round_to_obj) {
  const char* method_name = "Temporal.PlainTime.prototype.round";
  Factory* factory = isolate->factory();
  // 1. Let temporalTime be the this value.
  // 2. Perform ? RequireInternalSlot(temporalTime,
  // [[InitializedTemporalTime]]).
  // 3. If roundTo is undefined, then
  if (IsUndefined(*round_to_obj)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }

  DirectHandle<JSReceiver> round_to;
  // 4. If Type(roundTo) is String, then
  if (IsString(*round_to_obj)) {
    // a. Let paramString be roundTo.
    DirectHandle<String> param_string = Cast<String>(round_to_obj);
    // b. Set roundTo to ! OrdinaryObjectCreate(null).
    round_to = factory->NewJSObjectWithNullProto();
    // c. Perform ! CreateDataPropertyOrThrow(roundTo, "smallestUnit",
    // paramString).
    CHECK(JSReceiver::CreateDataProperty(isolate, round_to,
                                         factory->smallestUnit_string(),
                                         param_string, Just(kThrowOnError))
              .FromJust());
  } else {
    // 5. Set roundTo to ? GetOptionsObject(roundTo).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, round_to,
        GetOptionsObject(isolate, round_to_obj, method_name));
  }

  // 6. Let smallestUnit be ? GetTemporalUnit(roundTo, "smallestUnit", time,
  // required).
  Unit smallest_unit;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, smallest_unit,
      GetTemporalUnit(isolate, round_to, "smallestUnit", UnitGroup::kTime,
                      Unit::kNotPresent, true, method_name),
      DirectHandle<JSTemporalPlainTime>());

  // 7. Let roundingMode be ? ToTemporalRoundingMode(roundTo, "halfExpand").
  RoundingMode rounding_mode;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, rounding_mode,
      ToTemporalRoundingMode(isolate, round_to, RoundingMode::kHalfExpand,
                             method_name),
      DirectHandle<JSTemporalPlainTime>());

  // 8. Let maximum be ! MaximumTemporalDurationRoundingIncrement(smallestUnit).
  Maximum maximum = MaximumTemporalDurationRoundingIncrement(smallest_unit);

  // 9. Let roundingIncrement be ? ToTemporalRoundingIncrement(roundTo,
  // maximum, false).
  double rounding_increment;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, rounding_increment,
      ToTemporalRoundingIncrement(isolate, round_to, maximum.value,
                                  maximum.defined, false),
      DirectHandle<JSTemporalPlainTime>());

  // 12. Let result be ! RoundTime(temporalTime.[[ISOHour]],
  // temporalTime.[[ISOMinute]], temporalTime.[[ISOSecond]],
  // temporalTime.[[ISOMillisecond]], temporalTime.[[ISOMicrosecond]],
  // temporalTime.[[ISONanosecond]], roundingIncrement, smallestUnit,
  // roundingMode).
  DateTimeRecord result = RoundTime(
      isolate,
      {temporal_time->iso_hour(), temporal_time->iso_minute(),
       temporal_time->iso_second(), temporal_time->iso_millisecond(),
       temporal_time->iso_microsecond(), temporal_time->iso_nanosecond()},
      rounding_increment, smallest_unit, rounding_mode);
  // 13. Return ? CreateTemporalTime(result.[[Hour]], result.[[Minute]],
  // result.[[Second]], result.[[Millisecond]], result.[[Microsecond]],
  // result.[[Nanosecond]]).
  return CreateTemporalTime(isolate, result.time);
}

// #sec-temporal.plaintime.prototype.with
MaybeDirectHandle<JSTemporalPlainTime> JSTemporalPlainTime::With(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time,
    DirectHandle<Object> temporal_time_like_obj,
    DirectHandle<Object> options_obj) {
  const char* method_name = "Temporal.PlainTime.prototype.with";
  // 1. Let temporalTime be the this value.
  // 2. Perform ? RequireInternalSlot(temporalTime,
  // [[InitializedTemporalTime]]).
  // 3. If Type(temporalTimeLike) is not Object, then
  if (!IsJSReceiver(*temporal_time_like_obj)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }
  DirectHandle<JSReceiver> temporal_time_like =
      Cast<JSReceiver>(temporal_time_like_obj);
  // 4. Perform ? RejectObjectWithCalendarOrTimeZone(temporalTimeLike).
  MAYBE_RETURN(RejectObjectWithCalendarOrTimeZone(isolate, temporal_time_like),
               DirectHandle<JSTemporalPlainTime>());
  // 5. Let partialTime be ? ToPartialTime(temporalTimeLike).
  TimeRecord result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result,
      ToPartialTime(
          isolate, temporal_time_like,
          {temporal_time->iso_hour(), temporal_time->iso_minute(),
           temporal_time->iso_second(), temporal_time->iso_millisecond(),
           temporal_time->iso_microsecond(), temporal_time->iso_nanosecond()},
          method_name),
      DirectHandle<JSTemporalPlainTime>());

  // 6. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));
  // 7. Let overflow be ? ToTemporalOverflow(options).
  ShowOverflow overflow;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, overflow, ToTemporalOverflow(isolate, options, method_name),
      DirectHandle<JSTemporalPlainTime>());

  // 20. Let result be ? RegulateTime(hour, minute, second, millisecond,
  // microsecond, nanosecond, overflow).
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result, temporal::RegulateTime(isolate, result, overflow),
      DirectHandle<JSTemporalPlainTime>());
  // 25. Return ? CreateTemporalTime(result.[[Hour]], result.[[Minute]],
  // result.[[Second]], result.[[Millisecond]], result.[[Microsecond]],
  // result.[[Nanosecond]]).
  return CreateTemporalTime(isolate, result);
}

// #sec-temporal.now.plaintimeiso
MaybeDirectHandle<JSTemporalPlainTime> JSTemporalPlainTime::NowISO(
    Isolate* isolate, DirectHandle<Object> temporal_time_zone_like) {
  const char* method_name = "Temporal.Now.plainTimeISO";
  // 1. Let calendar be ! GetISO8601Calendar().
  DirectHandle<JSReceiver> calendar = temporal::GetISO8601Calendar(isolate);
  // 2. Let dateTime be ? SystemDateTime(temporalTimeZoneLike, calendar).
  DirectHandle<JSTemporalPlainDateTime> date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, date_time,
      SystemDateTime(isolate, temporal_time_zone_like, calendar, method_name));
  // 3. Return ! CreateTemporalTime(dateTime.[[ISOHour]],
  // dateTime.[[ISOMinute]], dateTime.[[ISOSecond]],
  // dateTime.[[ISOMillisecond]], dateTime.[[ISOMicrosecond]],
  // dateTime.[[ISONanosecond]]).
  return CreateTemporalTime(
             isolate,
             {date_time->iso_hour(), date_time->iso_minute(),
              date_time->iso_second(), date_time->iso_millisecond(),
              date_time->iso_microsecond(), date_time->iso_nanosecond()})
      .ToHandleChecked();
}

// #sec-temporal.plaintime.from
MaybeDirectHandle<JSTemporalPlainTime> JSTemporalPlainTime::From(
    Isolate* isolate, DirectHandle<Object> item_obj,
    DirectHandle<Object> options_obj) {
  const char* method_name = "Temporal.PlainTime.from";
  // 1. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));
  // 2. Let overflow be ? ToTemporalOverflow(options).
  ShowOverflow overflow;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, overflow, ToTemporalOverflow(isolate, options, method_name),
      DirectHandle<JSTemporalPlainTime>());
  // 3. If Type(item) is Object and item has an [[InitializedTemporalTime]]
  // internal slot, then
  if (IsJSTemporalPlainTime(*item_obj)) {
    // a. Return ? CreateTemporalTime(item.[[ISOHour]], item.[[ISOMinute]],
    // item.[[ISOSecond]], item.[[ISOMillisecond]], item.[[ISOMicrosecond]],
    // item.[[ISONanosecond]]).
    auto item = Cast<JSTemporalPlainTime>(item_obj);
    return CreateTemporalTime(
        isolate, {item->iso_hour(), item->iso_minute(), item->iso_second(),
                  item->iso_millisecond(), item->iso_microsecond(),
                  item->iso_nanosecond()});
  }
  // 4. Return ? ToTemporalTime(item, overflow).
  return temporal::ToTemporalTime(isolate, item_obj, method_name, overflow);
}

// #sec-temporal.plaintime.prototype.toplaindatetime
MaybeDirectHandle<JSTemporalPlainDateTime> JSTemporalPlainTime::ToPlainDateTime(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time,
    DirectHandle<Object> temporal_date_like) {
  // 1. Let temporalTime be the this value.
  // 2. Perform ? RequireInternalSlot(temporalTime,
  // [[InitializedTemporalTime]]).
  // 3. Set temporalDate to ? ToTemporalDate(temporalDate).
  DirectHandle<JSTemporalPlainDate> temporal_date;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temporal_date,
      ToTemporalDate(isolate, temporal_date_like,
                     "Temporal.PlainTime.prototype.toPlainDateTime"));
  // 4. Return ? CreateTemporalDateTime(temporalDate.[[ISOYear]],
  // temporalDate.[[ISOMonth]], temporalDate.[[ISODay]],
  // temporalTime.[[ISOHour]], temporalTime.[[ISOMinute]],
  // temporalTime.[[ISOSecond]], temporalTime.[[ISOMillisecond]],
  // temporalTime.[[ISOMicrosecond]], temporalTime.[[ISONanosecond]],
  // temporalDate.[[Calendar]]).
  return temporal::CreateTemporalDateTime(
      isolate,
      {{temporal_date->iso_year(), temporal_date->iso_month(),
        temporal_date->iso_day()},
       {temporal_time->iso_hour(), temporal_time->iso_minute(),
        temporal_time->iso_second(), temporal_time->iso_millisecond(),
        temporal_time->iso_microsecond(), temporal_time->iso_nanosecond()}},
      direct_handle(temporal_date->calendar(), isolate));
}

namespace {

// #sec-temporal-adddurationtoorsubtractdurationfromplaintime
MaybeDirectHandle<JSTemporalPlainTime>
AddDurationToOrSubtractDurationFromPlainTime(
    Isolate* isolate, Arithmetic operation,
    DirectHandle<JSTemporalPlainTime> temporal_time,
    DirectHandle<Object> temporal_duration_like, const char* method_name) {
  // 1. If operation is subtract, let sign be -1. Otherwise, let sign be 1.
  double sign = operation == Arithmetic::kSubtract ? -1.0 : 1.0;
  // 2. Let duration be ? ToTemporalDurationRecord(temporalDurationLike).
  DurationRecord duration;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, duration,
      temporal::ToTemporalDurationRecord(isolate, temporal_duration_like,
                                         method_name),
      DirectHandle<JSTemporalPlainTime>());
  TimeDurationRecord& time_duration = duration.time_duration;

  // 3. Let result be ! AddTime(temporalTime.[[ISOHour]],
  // temporalTime.[[ISOMinute]], temporalTime.[[ISOSecond]],
  // temporalTime.[[ISOMillisecond]], temporalTime.[[ISOMicrosecond]],
  // temporalTime.[[ISONanosecond]], sign x duration.[[Hours]], sign x
  // duration.[[Minutes]], sign x duration.[[Seconds]], sign x
  // duration.[[Milliseconds]], sign x duration.[[Microseconds]], sign x
  // duration.[[Nanoseconds]]).
  DateTimeRecord result = AddTime(
      isolate,
      {temporal_time->iso_hour(), temporal_time->iso_minute(),
       temporal_time->iso_second(), temporal_time->iso_millisecond(),
       temporal_time->iso_microsecond(), temporal_time->iso_nanosecond()},
      {0, sign * time_duration.hours, sign * time_duration.minutes,
       sign * time_duration.seconds, sign * time_duration.milliseconds,
       sign * time_duration.microseconds, sign * time_duration.nanoseconds});
  // 4. Assert: ! IsValidTime(result.[[Hour]], result.[[Minute]],
  // result.[[Second]], result.[[Millisecond]], result.[[Microsecond]],
  // result.[[Nanosecond]]) is true.
  DCHECK(IsValidTime(isolate, result.time));
  // 5. Return ? CreateTemporalTime(result.[[Hour]], result.[[Minute]],
  // result.[[Second]], result.[[Millisecond]], result.[[Microsecond]],
  // result.[[Nanosecond]]).
  return CreateTemporalTime(isolate, result.time);
}

}  // namespace

// #sec-temporal.plaintime.prototype.add
MaybeDirectHandle<JSTemporalPlainTime> JSTemporalPlainTime::Add(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time,
    DirectHandle<Object> temporal_duration_like) {
  return AddDurationToOrSubtractDurationFromPlainTime(
      isolate, Arithmetic::kAdd, temporal_time, temporal_duration_like,
      "Temporal.PlainTime.prototype.add");
}

// #sec-temporal.plaintime.prototype.subtract
MaybeDirectHandle<JSTemporalPlainTime> JSTemporalPlainTime::Subtract(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time,
    DirectHandle<Object> temporal_duration_like) {
  return AddDurationToOrSubtractDurationFromPlainTime(
      isolate, Arithmetic::kSubtract, temporal_time, temporal_duration_like,
      "Temporal.PlainTime.prototype.subtract");
}

namespace {
// #sec-temporal-differencetemporalplantime
MaybeDirectHandle<JSTemporalDuration> DifferenceTemporalPlainTime(
    Isolate* isolate, TimePreposition operation,
    DirectHandle<JSTemporalPlainTime> temporal_time,
    DirectHandle<Object> other_obj, DirectHandle<Object> options,
    const char* method_name) {
  TEMPORAL_ENTER_FUNC();
  // 1. If operation is since, let sign be -1. Otherwise, let sign be 1.
  double sign = operation == TimePreposition::kSince ? -1 : 1;
  // 2. Set other to ? ToTemporalDate(other).
  DirectHandle<JSTemporalPlainTime> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other,
      temporal::ToTemporalTime(isolate, other_obj, method_name));

  // 3. Let settings be ? GetDifferenceSettings(operation, options, time, « »,
  // "nanosecond", "hour").
  DifferenceSettings settings;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, settings,
      GetDifferenceSettings(isolate, operation, options, UnitGroup::kTime,
                            DisallowedUnitsInDifferenceSettings::kNone,
                            Unit::kNanosecond, Unit::kHour, method_name),
      DirectHandle<JSTemporalDuration>());
  // 4. Let result be ! DifferenceTime(temporalTime.[[ISOHour]],
  // temporalTime.[[ISOMinute]], temporalTime.[[ISOSecond]],
  // temporalTime.[[ISOMillisecond]], temporalTime.[[ISOMicrosecond]],
  // temporalTime.[[ISONanosecond]], other.[[ISOHour]], other.[[ISOMinute]],
  // other.[[ISOSecond]], other.[[ISOMillisecond]], other.[[ISOMicrosecond]],
  // other.[[ISONanosecond]]).
  DurationRecordWithRemainder result;
  result.record.time_duration =
      DifferenceTime(
          isolate,
          {temporal_time->iso_hour(), temporal_time->iso_minute(),
           temporal_time->iso_second(), temporal_time->iso_millisecond(),
           temporal_time->iso_microsecond(), temporal_time->iso_nanosecond()},
          {other->iso_hour(), other->iso_minute(), other->iso_second(),
           other->iso_millisecond(), other->iso_microsecond(),
           other->iso_nanosecond()})
          .ToChecked();
  // 5. Set result to (! RoundDuration(0, 0, 0, 0, result.[[Hours]],
  // result.[[Minutes]], result.[[Seconds]], result.[[Milliseconds]],
  // result.[[Microseconds]], result.[[Nanoseconds]],
  // settings.[[RoundingIncrement]], settings.[[SmallestUnit]],
  // settings.[[RoundingMode]])).[[DurationRecord]].
  result.record.years = result.record.months = result.record.weeks =
      result.record.time_duration.days = 0;
  result =
      RoundDuration(isolate, result.record, settings.rounding_increment,
                    settings.smallest_unit, settings.rounding_mode, method_name)
          .ToChecked();
  // 6. Set result to ! BalanceDuration(0, result.[[Hours]], result.[[Minutes]],
  // result.[[Seconds]], result.[[Milliseconds]], result.[[Microseconds]],
  // result.[[Nanoseconds]], settings.[[LargestUnit]]).
  result.record.time_duration.days = 0;
  result.record.time_duration =
      BalanceDuration(isolate, settings.largest_unit,
                      result.record.time_duration, method_name)
          .ToChecked();

  // 7. Return ! CreateTemporalDuration(0, 0, 0, 0, sign × result.[[Hours]],
  // sign × result.[[Minutes]], sign × result.[[Seconds]], sign ×
  // result.[[Milliseconds]], sign × result.[[Microseconds]], sign ×
  // result.[[Nanoseconds]]).
  result.record.years = result.record.months = result.record.weeks =
      result.record.time_duration.days = 0;
  result.record.time_duration.hours *= sign;
  result.record.time_duration.minutes *= sign;
  result.record.time_duration.seconds *= sign;
  result.record.time_duration.milliseconds *= sign;
  result.record.time_duration.microseconds *= sign;
  result.record.time_duration.nanoseconds *= sign;
  return CreateTemporalDuration(isolate, result.record).ToHandleChecked();
}

}  // namespace

// #sec-temporal.plaintime.prototype.until
MaybeDirectHandle<JSTemporalDuration> JSTemporalPlainTime::Until(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  return DifferenceTemporalPlainTime(isolate, TimePreposition::kUntil, handle,
                                     other, options,
                                     "Temporal.PlainTime.prototype.until");
}

// #sec-temporal.plaintime.prototype.since
MaybeDirectHandle<JSTemporalDuration> JSTemporalPlainTime::Since(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  return DifferenceTemporalPlainTime(isolate, TimePreposition::kSince, handle,
                                     other, options,
                                     "Temporal.PlainTime.prototype.since");
}

// #sec-temporal.plaintime.prototype.getisofields
MaybeDirectHandle<JSReceiver> JSTemporalPlainTime::GetISOFields(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time) {
  Factory* factory = isolate->factory();
  // 1. Let temporalTime be the this value.
  // 2. Perform ? RequireInternalSlot(temporalTime,
  // [[InitializedTemporalTime]]).
  // 3. Let fields be ! OrdinaryObjectCreate(%Object.prototype%).
  DirectHandle<JSObject> fields =
      isolate->factory()->NewJSObject(isolate->object_function());
  // 4. Perform ! CreateDataPropertyOrThrow(fields, "calendar",
  // temporalTime.[[Calendar]]).
  DirectHandle<JSTemporalCalendar> iso8601_calendar =
      temporal::GetISO8601Calendar(isolate);
  CHECK(JSReceiver::CreateDataProperty(isolate, fields,
                                       factory->calendar_string(),
                                       iso8601_calendar, Just(kThrowOnError))
            .FromJust());

  // 5. Perform ! CreateDataPropertyOrThrow(fields, "isoHour",
  // 𝔽(temporalTime.[[ISOHour]])).
  // 6. Perform ! CreateDataPropertyOrThrow(fields, "isoMicrosecond",
  // 𝔽(temporalTime.[[ISOMicrosecond]])).
  // 7. Perform ! CreateDataPropertyOrThrow(fields, "isoMillisecond",
  // 𝔽(temporalTime.[[ISOMillisecond]])).
  // 8. Perform ! CreateDataPropertyOrThrow(fields, "isoMinute",
  // 𝔽(temporalTime.[[ISOMinute]])).
  // 9. Perform ! CreateDataPropertyOrThrow(fields, "isoNanosecond",
  // 𝔽(temporalTime.[[ISONanosecond]])).
  // 10. Perform ! CreateDataPropertyOrThrow(fields, "isoSecond",
  // 𝔽(temporalTime.[[ISOSecond]])).
  DEFINE_INT_FIELD(fields, isoHour, iso_hour, temporal_time)
  DEFINE_INT_FIELD(fields, isoMicrosecond, iso_microsecond, temporal_time)
  DEFINE_INT_FIELD(fields, isoMillisecond, iso_millisecond, temporal_time)
  DEFINE_INT_FIELD(fields, isoMinute, iso_minute, temporal_time)
  DEFINE_INT_FIELD(fields, isoNanosecond, iso_nanosecond, temporal_time)
  DEFINE_INT_FIELD(fields, isoSecond, iso_second, temporal_time)
  // 11. Return fields.
  return fields;
}

// #sec-temporal.plaintime.prototype.tojson
MaybeDirectHandle<String> JSTemporalPlainTime::ToJSON(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time) {
  return TemporalTimeToString(isolate, temporal_time, Precision::kAuto);
}

// #sup-temporal.plaintime.prototype.tolocalestring
MaybeDirectHandle<String> JSTemporalPlainTime::ToLocaleString(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time,
    DirectHandle<Object> locales, DirectHandle<Object> options) {
#ifdef V8_INTL_SUPPORT
  return JSDateTimeFormat::TemporalToLocaleString(
      isolate, temporal_time, locales, options,
      "Temporal.PlainTime.prototype.toLocaleString");
#else   //  V8_INTL_SUPPORT
  return TemporalTimeToString(isolate, temporal_time, Precision::kAuto);
#endif  //  V8_INTL_SUPPORT
}

namespace {

// #sec-temporal-getunsignedroundingmode
UnsignedRoundingMode GetUnsignedRoundingMode(RoundingMode rounding_mode,
                                             bool is_negative) {
  // 1. If isNegative is true, return the specification type in the third column
  // of Table 14 where the first column is roundingMode and the second column is
  // "negative".
  if (is_negative) {
    switch (rounding_mode) {
      case RoundingMode::kCeil:
        return UnsignedRoundingMode::kZero;
      case RoundingMode::kFloor:
        return UnsignedRoundingMode::kInfinity;
      case RoundingMode::kExpand:
        return UnsignedRoundingMode::kInfinity;
      case RoundingMode::kTrunc:
        return UnsignedRoundingMode::kZero;
      case RoundingMode::kHalfCeil:
        return UnsignedRoundingMode::kHalfZero;
      case RoundingMode::kHalfFloor:
        return UnsignedRoundingMode::kHalfInfinity;
      case RoundingMode::kHalfExpand:
        return UnsignedRoundingMode::kHalfInfinity;
      case RoundingMode::kHalfTrunc:
        return UnsignedRoundingMode::kHalfZero;
      case RoundingMode::kHalfEven:
        return UnsignedRoundingMode::kHalfEven;
    }
  }
  // 2. Else, return the specification type in the third column of Table 14
  // where the first column is roundingMode and the second column is "positive".
  switch (rounding_mode) {
    case RoundingMode::kCeil:
      return UnsignedRoundingMode::kInfinity;
    case RoundingMode::kFloor:
      return UnsignedRoundingMode::kZero;
    case RoundingMode::kExpand:
      return UnsignedRoundingMode::kInfinity;
    case RoundingMode::kTrunc:
      return UnsignedRoundingMode::kZero;
    case RoundingMode::kHalfCeil:
      return UnsignedRoundingMode::kHalfInfinity;
    case RoundingMode::kHalfFloor:
      return UnsignedRoundingMode::kHalfZero;
    case RoundingMode::kHalfExpand:
      return UnsignedRoundingMode::kHalfInfinity;
    case RoundingMode::kHalfTrunc:
      return UnsignedRoundingMode::kHalfZero;
    case RoundingMode::kHalfEven:
      return UnsignedRoundingMode::kHalfEven;
  }
}

// #sec-temporal-applyunsignedroundingmode
double ApplyUnsignedRoundingMode(double x, double r1, double r2,
                                 UnsignedRoundingMode unsigned_rounding_mode) {
  // 1. If x is equal to r1, return r1.
  if (x == r1) return r1;
  // 2. Assert: r1 < x < r2.
  DCHECK_LT(r1, x);
  DCHECK_LT(x, r2);
  // 3. Assert: unsignedRoundingMode is not undefined.
  // 4. If unsignedRoundingMode is zero, return r1.
  if (unsigned_rounding_mode == UnsignedRoundingMode::kZero) return r1;
  // 5. If unsignedRoundingMode is infinity, return r2.
  if (unsigned_rounding_mode == UnsignedRoundingMode::kInfinity) return r2;
  // 6. Let d1 be x – r1.
  double d1 = x - r1;
  // 7. Let d2 be r2 – x.
  double d2 = r2 - x;
  // 8. If d1 < d2, return r1.
  if (d1 < d2) return r1;
  // 9. If d2 < d1, return r2.
  if (d2 < d1) return r2;
  // 10. Assert: d1 is equal to d2.
  DCHECK_EQ(d1, d2);
  // 11. If unsignedRoundingMode is half-zero, return r1.
  if (unsigned_rounding_mode == UnsignedRoundingMode::kHalfZero) return r1;
  // 12. If unsignedRoundingMode is half-infinity, return r2.
  if (unsigned_rounding_mode == UnsignedRoundingMode::kHalfInfinity) return r2;
  // 13. Assert: unsignedRoundingMode is half-even.
  DCHECK_EQ(unsigned_rounding_mode, UnsignedRoundingMode::kHalfEven);
  // 14. Let cardinality be (r1 / (r2 – r1)) modulo 2.
  int64_t cardinality = static_cast<int64_t>(r1) % 2;
  // 15. If cardinality is 0, return r1.
  if (cardinality == 0) return r1;
  // 16. Return r2.
  return r2;
}

// #sec-temporal-applyunsignedroundingmode
DirectHandle<BigInt> ApplyUnsignedRoundingMode(
    Isolate* isolate, DirectHandle<BigInt> num, DirectHandle<BigInt> increment,
    DirectHandle<BigInt> r1, DirectHandle<BigInt> r2,
    UnsignedRoundingMode unsigned_rounding_mode) {
  // 1. If x is equal to r1, return r1.
  DirectHandle<BigInt> rr1 =
      BigInt::Multiply(isolate, increment, r1).ToHandleChecked();
  DirectHandle<BigInt> rr2 =
      BigInt::Multiply(isolate, increment, r2).ToHandleChecked();
  if (BigInt::EqualToBigInt(*num, *rr1)) return r1;
  // 2. Assert: r1 < x < r2.
  DCHECK_EQ(BigInt::CompareToBigInt(rr1, num), ComparisonResult::kLessThan);
  DCHECK_EQ(BigInt::CompareToBigInt(num, rr2), ComparisonResult::kLessThan);
  // 3. Assert: unsignedRoundingMode is not undefined.
  // 4. If unsignedRoundingMode is zero, return r1.
  if (unsigned_rounding_mode == UnsignedRoundingMode::kZero) return r1;
  // 5. If unsignedRoundingMode is infinity, return r2.
  if (unsigned_rounding_mode == UnsignedRoundingMode::kInfinity) return r2;
  // 6. Let d1 be x – r1.
  DirectHandle<BigInt> dd1 =
      BigInt::Subtract(isolate, num, rr1).ToHandleChecked();
  // 7. Let d2 be r2 – x.
  DirectHandle<BigInt> dd2 =
      BigInt::Subtract(isolate, rr2, num).ToHandleChecked();
  // 8. If d1 < d2, return r1.
  if (BigInt::CompareToBigInt(dd1, dd2) == ComparisonResult::kLessThan) {
    return r1;
  }
  // 9. If d2 < d1, return r2.
  if (BigInt::CompareToBigInt(dd2, dd1) == ComparisonResult::kLessThan) {
    return r2;
  }
  // 10. Assert: d1 is equal to d2.
  DCHECK_EQ(BigInt::CompareToBigInt(dd1, dd2), ComparisonResult::kEqual);
  // 11. If unsignedRoundingMode is half-zero, return r1.
  if (unsigned_rounding_mode == UnsignedRoundingMode::kHalfZero) return r1;
  // 12. If unsignedRoundingMode is half-infinity, return r2.
  if (unsigned_rounding_mode == UnsignedRoundingMode::kHalfInfinity) return r2;
  // 13. Assert: unsignedRoundingMode is half-even.
  DCHECK_EQ(unsigned_rounding_mode, UnsignedRoundingMode::kHalfEven);
  // 14. Let cardinality be (r1 / (r2 – r1)) modulo 2.
  DirectHandle<BigInt> cardinality =
      BigInt::Remainder(isolate, r1, BigInt::FromInt64(isolate, 2))
          .ToHandleChecked();
  // 15. If cardinality is 0, return r1.
  if (!cardinality->ToBoolean()) return r1;
  // 16. Return r2.
  return r2;
}

// #sec-temporal-roundnumbertoincrement
// For the case that x is double.
double RoundNumberToIncrement(Isolate* isolate, double x, double increment,
                              RoundingMode rounding_mode) {
  TEMPORAL_ENTER_FUNC();

  // 1. Let quotient be x / increment.
  double quotient = x / increment;
  bool is_negative;
  // 2. If quotient < 0, then
  if (quotient < 0) {
    // a. Let isNegative be true.
    is_negative = true;
    // b. Set quotient to -quotient.
    quotient = -quotient;
    // 3. Else,
  } else {
    // a. Let isNegative be false.
    is_negative = false;
  }
  // 4. Let unsignedRoundingMode be GetUnsignedRoundingMode(roundingMode,
  // isNegative).
  UnsignedRoundingMode unsigned_rounding_mode =
      GetUnsignedRoundingMode(rounding_mode, is_negative);

  // 5. Let r1 be the largest integer such that r1 ≤ quotient.
  double r1 = std::floor(quotient);
  // 6. Let r2 be the smallest integer such that r2 > quotient.
  double r2 = std::floor(quotient + 1);
  // 7. Let rounded be ApplyUnsignedRoundingMode(quotient, r1, r2,
  // unsignedRoundingMode).
  double rounded =
      ApplyUnsignedRoundingMode(quotient, r1, r2, unsigned_rounding_mode);
  // 8. If isNegative is true, set rounded to -rounded.
  if (is_negative) {
    rounded = -rounded;
  }
  // 9. Return rounded × increment.
  return rounded * increment;
}

// #sec-temporal-roundnumbertoincrementasifpositive
DirectHandle<BigInt> RoundNumberToIncrementAsIfPositive(
    Isolate* isolate, DirectHandle<BigInt> x, double increment,
    RoundingMode rounding_mode) {
  TEMPORAL_ENTER_FUNC();

  // 1. Let quotient be x / increment.
  // 2. Let unsignedRoundingMode be GetUnsignedRoundingMode(roundingMode,
  // false).
  UnsignedRoundingMode unsigned_rounding_mode =
      GetUnsignedRoundingMode(rounding_mode, false);

  DirectHandle<BigInt> increment_bigint =
      BigInt::FromNumber(isolate, isolate->factory()->NewNumber(increment))
          .ToHandleChecked();
  // 3. Let r1 be the largest integer such that r1 ≤ quotient.
  DirectHandle<BigInt> r1 =
      BigInt::Divide(isolate, x, increment_bigint).ToHandleChecked();

  // Adjust for negative quotient.
  if (r1->IsNegative() && BigInt::Remainder(isolate, x, increment_bigint)
                              .ToHandleChecked()
                              ->ToBoolean()) {
    r1 = BigInt::Decrement(isolate, r1).ToHandleChecked();
  }

  // 4. Let r2 be the smallest integer such that r2 > quotient.
  DirectHandle<BigInt> r2 = BigInt::Increment(isolate, r1).ToHandleChecked();
  // 5. Let rounded be ApplyUnsignedRoundingMode(quotient, r1, r2,
  // unsignedRoundingMode).
  DirectHandle<BigInt> rounded = ApplyUnsignedRoundingMode(
      isolate, x, increment_bigint, r1, r2, unsigned_rounding_mode);
  // 6. Return rounded × increment.
  DirectHandle<BigInt> result =
      BigInt::Multiply(isolate, rounded, increment_bigint).ToHandleChecked();
  return result;
}

DateTimeRecord RoundTime(Isolate* isolate, const TimeRecord& time,
                         double increment, Unit unit,
                         RoundingMode rounding_mode, double day_length_ns) {
  TEMPORAL_ENTER_FUNC();

  // 1. Assert: hour, minute, second, millisecond, microsecond, nanosecond, and
  // increment are integers.
  // 2. Let fractionalSecond be nanosecond × 10^−9 + microsecond × 10^−6 +
  // millisecond × 10−3 + second.
  double fractional_second =
      static_cast<double>(time.nanosecond) / 100000000.0 +
      static_cast<double>(time.microsecond) / 1000000.0 +
      static_cast<double>(time.millisecond) / 1000.0 +
      static_cast<double>(time.second);
  double quantity;
  switch (unit) {
    // 3. If unit is "day", then
    case Unit::kDay:
      // a. If dayLengthNs is not present, set it to 8.64 × 10^13.
      // b. Let quantity be (((((hour × 60 + minute) × 60 + second) × 1000 +
      // millisecond) × 1000 + microsecond) × 1000 + nanosecond) / dayLengthNs.
      quantity =
          (((((time.hour * 60.0 + time.minute) * 60.0 + time.second) * 1000.0 +
             time.millisecond) *
                1000.0 +
            time.microsecond) *
               1000.0 +
           time.nanosecond) /
          day_length_ns;
      break;
    // 4. Else if unit is "hour", then
    case Unit::kHour:
      // a. Let quantity be (fractionalSecond / 60 + minute) / 60 + hour.
      quantity = (fractional_second / 60.0 + time.minute) / 60.0 + time.hour;
      break;
    // 5. Else if unit is "minute", then
    case Unit::kMinute:
      // a. Let quantity be fractionalSecond / 60 + minute.
      quantity = fractional_second / 60.0 + time.minute;
      break;
    // 6. Else if unit is "second", then
    case Unit::kSecond:
      // a. Let quantity be fractionalSecond.
      quantity = fractional_second;
      break;
    // 7. Else if unit is "millisecond", then
    case Unit::kMillisecond:
      // a. Let quantity be nanosecond × 10^−6 + microsecond × 10^−3 +
      // millisecond.
      quantity = time.nanosecond / 1000000.0 + time.microsecond / 1000.0 +
                 time.millisecond;
      break;
    // 8. Else if unit is "microsecond", then
    case Unit::kMicrosecond:
      // a. Let quantity be nanosecond × 10^−3 + microsecond.
      quantity = time.nanosecond / 1000.0 + time.microsecond;
      break;
    // 9. Else,
    default:
      // a. Assert: unit is "nanosecond".
      DCHECK_EQ(unit, Unit::kNanosecond);
      // b. Let quantity be nanosecond.
      quantity = time.nanosecond;
      break;
  }
  // 10. Let result be ! RoundNumberToIncrement(quantity, increment,
  // roundingMode).
  int32_t result =
      RoundNumberToIncrement(isolate, quantity, increment, rounding_mode);

  switch (unit) {
    // 11. If unit is "day", then
    case Unit::kDay:
      // a. Return the Record { [[Days]]: result, [[Hour]]: 0, [[Minute]]: 0,
      // [[Second]]: 0, [[Millisecond]]: 0, [[Microsecond]]: 0, [[Nanosecond]]:
      // 0 }.
      return {{0, 0, result}, {0, 0, 0, 0, 0, 0}};
    // 12. If unit is "hour", then
    case Unit::kHour:
      // a. Return ! BalanceTime(result, 0, 0, 0, 0, 0).
      return BalanceTime({static_cast<double>(result), 0, 0, 0, 0, 0});
    // 13. If unit is "minute", then
    case Unit::kMinute:
      // a. Return ! BalanceTime(hour, result, 0, 0, 0, 0).
      return BalanceTime({static_cast<double>(time.hour),
                          static_cast<double>(result), 0, 0, 0, 0});
    // 14. If unit is "second", then
    case Unit::kSecond:
      // a. Return ! BalanceTime(hour, minute, result, 0, 0, 0).
      return BalanceTime({static_cast<double>(time.hour),
                          static_cast<double>(time.minute),
                          static_cast<double>(result), 0, 0, 0});
    // 15. If unit is "millisecond", then
    case Unit::kMillisecond:
      // a. Return ! BalanceTime(hour, minute, second, result, 0, 0).
      return BalanceTime({static_cast<double>(time.hour),
                          static_cast<double>(time.minute),
                          static_cast<double>(time.second),
                          static_cast<double>(result), 0, 0});
    // 16. If unit is "microsecond", then
    case Unit::kMicrosecond:
      // a. Return ! BalanceTime(hour, minute, second, millisecond, result, 0).
      return BalanceTime({static_cast<double>(time.hour),
                          static_cast<double>(time.minute),
                          static_cast<double>(time.second),
                          static_cast<double>(time.millisecond),
                          static_cast<double>(result), 0});
    default:
      // 17. Assert: unit is "nanosecond".
      DCHECK_EQ(unit, Unit::kNanosecond);
      // 18. Return ! BalanceTime(hour, minute, second, millisecond,
      // microsecond, result).
      return BalanceTime(
          {static_cast<double>(time.hour), static_cast<double>(time.minute),
           static_cast<double>(time.second),
           static_cast<double>(time.millisecond),
           static_cast<double>(time.microsecond), static_cast<double>(result)});
  }
}

// #sec-temporal-tosecondsstringprecision
Maybe<StringPrecision> ToSecondsStringPrecision(
    Isolate* isolate, DirectHandle<JSReceiver> normalized_options,
    const char* method_name) {
  // 1. Let smallestUnit be ? GetTemporalUnit(normalizedOptions, "smallestUnit",
  // time, undefined).
  Unit smallest_unit;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, smallest_unit,
      GetTemporalUnit(isolate, normalized_options, "smallestUnit",
                      UnitGroup::kTime, Unit::kNotPresent, false, method_name),
      Nothing<StringPrecision>());

  switch (smallest_unit) {
    // 2. If smallestUnit is "hour", throw a RangeError exception.
    case Unit::kHour:
      THROW_NEW_ERROR_RETURN_VALUE(
          isolate,
          NewRangeError(MessageTemplate::kPropertyValueOutOfRange,
                        isolate->factory()->smallestUnit_string()),
          Nothing<StringPrecision>());
    // 2. If smallestUnit is "minute", then
    case Unit::kMinute:
      // a. Return the new Record { [[Precision]]: "minute", [[Unit]]: "minute",
      // [[Increment]]: 1 }.
      return Just(StringPrecision({Precision::kMinute, Unit::kMinute, 1}));
    // 3. If smallestUnit is "second", then
    case Unit::kSecond:
      // a. Return the new Record { [[Precision]]: 0, [[Unit]]: "second",
      // [[Increment]]: 1 }.
      return Just(StringPrecision({Precision::k0, Unit::kSecond, 1}));
    // 4. If smallestUnit is "millisecond", then
    case Unit::kMillisecond:
      // a. Return the new Record { [[Precision]]: 3, [[Unit]]: "millisecond",
      // [[Increment]]: 1 }.
      return Just(StringPrecision({Precision::k3, Unit::kMillisecond, 1}));
    // 5. If smallestUnit is "microsecond", then
    case Unit::kMicrosecond:
      // a. Return the new Record { [[Precision]]: 6, [[Unit]]: "microsecond",
      // [[Increment]]: 1 }.
      return Just(StringPrecision({Precision::k6, Unit::kMicrosecond, 1}));
    // 6. If smallestUnit is "nanosecond", then
    case Unit::kNanosecond:
      // a. Return the new Record { [[Precision]]: 9, [[Unit]]: "nanosecond",
      // [[Increment]]: 1 }.
      return Just(StringPrecision({Precision::k9, Unit::kNanosecond, 1}));
    default:
      break;
  }
  Factory* factory = isolate->factory();
  // 8. Assert: smallestUnit is undefined.
  DCHECK(smallest_unit == Unit::kNotPresent);
  // 9. Let fractionalDigitsVal be ? Get(normalizedOptions,
  // "fractionalSecondDigits").
  DirectHandle<Object> fractional_digits_val;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, fractional_digits_val,
      JSReceiver::GetProperty(isolate, normalized_options,
                              factory->fractionalSecondDigits_string()),
      Nothing<StringPrecision>());

  // 10. If Type(fractionalDigitsVal) is not Number, then
  if (!IsNumber(*fractional_digits_val)) {
    // a. If fractionalDigitsVal is not undefined, then
    if (!IsUndefined(*fractional_digits_val)) {
      // i. If ? ToString(fractionalDigitsVal) is not "auto", throw a RangeError
      // exception.
      DirectHandle<String> string;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, string, Object::ToString(isolate, fractional_digits_val),
          Nothing<StringPrecision>());
      if (!String::Equals(isolate, string, factory->auto_string())) {
        THROW_NEW_ERROR_RETURN_VALUE(
            isolate,
            NewRangeError(MessageTemplate::kPropertyValueOutOfRange,
                          factory->fractionalSecondDigits_string()),
            Nothing<StringPrecision>());
      }
    }
    // b. Return the Record { [[Precision]]: "auto", [[Unit]]: "nanosecond",
    // [[Increment]]: 1 }.
    return Just(StringPrecision({Precision::kAuto, Unit::kNanosecond, 1}));
  }
  // 11. If fractionalDigitsVal is NaN, +∞𝔽, or -∞𝔽, throw a RangeError
  // exception.
  if (IsNaN(*fractional_digits_val) ||
      std::isinf(Object::NumberValue(Cast<Number>(*fractional_digits_val)))) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewRangeError(MessageTemplate::kPropertyValueOutOfRange,
                      factory->fractionalSecondDigits_string()),
        Nothing<StringPrecision>());
  }
  // 12. Let fractionalDigitCount be RoundTowardsZero(ℝ(fractionalDigitsVal)).
  int64_t fractional_digit_count = RoundTowardsZero(
      Object::NumberValue(Cast<Number>(*fractional_digits_val)));
  // 13. If fractionalDigitCount < 0 or fractionalDigitCount > 9, throw a
  // RangeError exception.
  if (fractional_digit_count < 0 || fractional_digit_count > 9) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewRangeError(MessageTemplate::kPropertyValueOutOfRange,
                      factory->fractionalSecondDigits_string()),
        Nothing<StringPrecision>());
  }
  // 14. If fractionalDigitCount is 0, then
  switch (fractional_digit_count) {
    case 0:
      // a. Return the Record { [[Precision]]: 0, [[Unit]]: "second",
      // [[Increment]]: 1 }.
      return Just(StringPrecision({Precision::k0, Unit::kSecond, 1}));
    // 15. If fractionalDigitCount is 1, 2, or 3, then
    // a. Return the Record { [[Precision]]: fractionalDigitCount, [[Unit]]:
    // "millisecond", [[Increment]]: 10^(3 - fractionalDigitCount) }.
    case 1:
      return Just(StringPrecision({Precision::k1, Unit::kMillisecond, 100}));
    case 2:
      return Just(StringPrecision({Precision::k2, Unit::kMillisecond, 10}));
    case 3:
      return Just(StringPrecision({Precision::k3, Unit::kMillisecond, 1}));
    // 16. If fractionalDigitCount is 4, 5, or 6, then
    // a. Return the Record { [[Precision]]: fractionalDigitCount, [[Unit]]:
    // "microsecond", [[Increment]]: 10^(6 - fractionalDigitCount) }.
    case 4:
      return Just(StringPrecision({Precision::k4, Unit::kMicrosecond, 100}));
    case 5:
      return Just(StringPrecision({Precision::k5, Unit::kMicrosecond, 10}));
    case 6:
      return Just(StringPrecision({Precision::k6, Unit::kMicrosecond, 1}));
    // 17. Assert: fractionalDigitCount is 7, 8, or 9.
    // 18. Return the Record { [[Precision]]: fractionalDigitCount, [[Unit]]:
    // "nanosecond", [[Increment]]: 109 - fractionalDigitCount }.
    case 7:
      return Just(StringPrecision({Precision::k7, Unit::kNanosecond, 100}));
    case 8:
      return Just(StringPrecision({Precision::k8, Unit::kNanosecond, 10}));
    case 9:
      return Just(StringPrecision({Precision::k9, Unit::kNanosecond, 1}));
    default:
      UNREACHABLE();
  }
}

// #sec-temporal-compareepochnanoseconds
MaybeDirectHandle<Smi> CompareEpochNanoseconds(Isolate* isolate,
                                               DirectHandle<BigInt> one,
                                               DirectHandle<BigInt> two) {
  TEMPORAL_ENTER_FUNC();

  // 1. If epochNanosecondsOne > epochNanosecondsTwo, return 1.
  // 2. If epochNanosecondsOne < epochNanosecondsTwo, return -1.
  // 3. Return 0.
  return direct_handle(
      Smi::FromInt(CompareResultToSign(BigInt::CompareToBigInt(one, two))),
      isolate);
}

}  // namespace

// #sec-temporal.plaintime.prototype.tostring
MaybeDirectHandle<String> JSTemporalPlainTime::ToString(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time,
    DirectHandle<Object> options_obj) {
  const char* method_name = "Temporal.PlainTime.prototype.toString";
  // 1. Let temporalTime be the this value.
  // 2. Perform ? RequireInternalSlot(temporalTime,
  // [[InitializedTemporalTime]]).
  // 3. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));

  // 4. Let precision be ? ToSecondsStringPrecision(options).
  StringPrecision precision;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, precision,
      ToSecondsStringPrecision(isolate, options, method_name),
      DirectHandle<String>());

  // 5. Let roundingMode be ? ToTemporalRoundingMode(options, "trunc").
  RoundingMode rounding_mode;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, rounding_mode,
      ToTemporalRoundingMode(isolate, options, RoundingMode::kTrunc,
                             method_name),
      DirectHandle<String>());

  // 6. Let roundResult be ! RoundTime(temporalTime.[[ISOHour]],
  // temporalTime.[[ISOMinute]], temporalTime.[[ISOSecond]],
  // temporalTime.[[ISOMillisecond]], temporalTime.[[ISOMicrosecond]],
  // temporalTime.[[ISONanosecond]], precision.[[Increment]],
  // precision.[[Unit]], roundingMode).

  DateTimeRecord round_result = RoundTime(
      isolate,
      {temporal_time->iso_hour(), temporal_time->iso_minute(),
       temporal_time->iso_second(), temporal_time->iso_millisecond(),
       temporal_time->iso_microsecond(), temporal_time->iso_nanosecond()},
      precision.increment, precision.unit, rounding_mode);
  // 7. Return ! TemporalTimeToString(roundResult.[[Hour]],
  // roundResult.[[Minute]], roundResult.[[Second]],
  // roundResult.[[Millisecond]], roundResult.[[Microsecond]],
  // roundResult.[[Nanosecond]], precision.[[Precision]]).
  return TemporalTimeToString(isolate, round_result.time, precision.precision);
}

// #sec-temporal.zoneddatetime
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::Constructor(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target,
    DirectHandle<Object> epoch_nanoseconds_obj,
    DirectHandle<Object> time_zone_like, DirectHandle<Object> calendar_like) {
  const char* method_name = "Temporal.ZonedDateTime";
  // 1. If NewTarget is undefined, then
  if (IsUndefined(*new_target)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                                 isolate->factory()->NewStringFromAsciiChecked(
                                     method_name)));
  }
  // 2. Set epochNanoseconds to ? ToBigInt(epochNanoseconds).
  DirectHandle<BigInt> epoch_nanoseconds;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, epoch_nanoseconds,
      BigInt::FromObject(isolate, epoch_nanoseconds_obj));
  // 3. If ! IsValidEpochNanoseconds(epochNanoseconds) is false, throw a
  // RangeError exception.
  if (!IsValidEpochNanoseconds(isolate, epoch_nanoseconds)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }

  // 4. Let timeZone be ? ToTemporalTimeZone(timeZoneLike).
  DirectHandle<JSReceiver> time_zone;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, time_zone,
      temporal::ToTemporalTimeZone(isolate, time_zone_like, method_name));

  // 5. Let calendar be ? ToTemporalCalendarWithISODefault(calendarLike).
  DirectHandle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar,
      ToTemporalCalendarWithISODefault(isolate, calendar_like, method_name));

  // 6. Return ? CreateTemporalZonedDateTime(epochNanoseconds, timeZone,
  // calendar, NewTarget).
  return CreateTemporalZonedDateTime(isolate, target, new_target,
                                     epoch_nanoseconds, time_zone, calendar);
}

// #sec-get-temporal.zoneddatetime.prototype.hoursinday
MaybeDirectHandle<Object> JSTemporalZonedDateTime::HoursInDay(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC();
  const char* method_name = "Temporal.ZonedDateTime.prototype.hoursInDay";
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Let timeZone be zonedDateTime.[[TimeZone]].
  DirectHandle<JSReceiver> time_zone(zoned_date_time->time_zone(), isolate);

  // 4. Let instant be ! CreateTemporalInstant(zonedDateTime.[[Nanoseconds]]).
  DirectHandle<JSTemporalInstant> instant =
      temporal::CreateTemporalInstant(
          isolate, direct_handle(zoned_date_time->nanoseconds(), isolate))
          .ToHandleChecked();

  // 5. Let isoCalendar be ! GetISO8601Calendar().
  DirectHandle<JSReceiver> iso_calendar = temporal::GetISO8601Calendar(isolate);

  // 6. Let temporalDateTime be ? BuiltinTimeZoneGetPlainDateTimeFor(timeZone,
  // instant, isoCalendar).
  DirectHandle<JSTemporalPlainDateTime> temporal_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temporal_date_time,
      temporal::BuiltinTimeZoneGetPlainDateTimeFor(isolate, time_zone, instant,
                                                   iso_calendar, method_name));
  // 7. Let year be temporalDateTime.[[ISOYear]].
  // 8. Let month be temporalDateTime.[[ISOMonth]].
  // 9. Let day be temporalDateTime.[[ISODay]].
  // 10. Let today be ? CreateTemporalDateTime(year, month, day, 0, 0, 0, 0, 0,
  // 0, isoCalendar).
  DirectHandle<JSTemporalPlainDateTime> today;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, today,
      temporal::CreateTemporalDateTime(
          isolate,
          {{temporal_date_time->iso_year(), temporal_date_time->iso_month(),
            temporal_date_time->iso_day()},
           {0, 0, 0, 0, 0, 0}},
          iso_calendar));
  // 11. Let tomorrowFields be BalanceISODate(year, month, day + 1).
  DateRecord tomorrow_fields = BalanceISODate(
      isolate, {temporal_date_time->iso_year(), temporal_date_time->iso_month(),
                temporal_date_time->iso_day() + 1});

  // 12. Let tomorrow be ? CreateTemporalDateTime(tomorrowFields.[[Year]],
  // tomorrowFields.[[Month]], tomorrowFields.[[Day]], 0, 0, 0, 0, 0, 0,
  // isoCalendar).
  DirectHandle<JSTemporalPlainDateTime> tomorrow;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, tomorrow,
      temporal::CreateTemporalDateTime(
          isolate, {tomorrow_fields, {0, 0, 0, 0, 0, 0}}, iso_calendar));
  // 13. Let todayInstant be ? BuiltinTimeZoneGetInstantFor(timeZone, today,
  // "compatible").
  DirectHandle<JSTemporalInstant> today_instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, today_instant,
      BuiltinTimeZoneGetInstantFor(isolate, time_zone, today,
                                   Disambiguation::kCompatible, method_name));
  // 14. Let tomorrowInstant be ? BuiltinTimeZoneGetInstantFor(timeZone,
  // tomorrow, "compatible").
  DirectHandle<JSTemporalInstant> tomorrow_instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, tomorrow_instant,
      BuiltinTimeZoneGetInstantFor(isolate, time_zone, tomorrow,
                                   Disambiguation::kCompatible, method_name));
  // 15. Let diffNs be tomorrowInstant.[[Nanoseconds]] −
  // todayInstant.[[Nanoseconds]].
  DirectHandle<BigInt> diff_ns =
      BigInt::Subtract(isolate,
                       direct_handle(tomorrow_instant->nanoseconds(), isolate),
                       direct_handle(today_instant->nanoseconds(), isolate))
          .ToHandleChecked();
  // 16. Return 𝔽(diffNs / (3.6 × 10^12)).
  //
  // Note: The result of the division may be non integer for TimeZone which
  // change fractional hours. Perform this division in two steps:
  // First convert it to seconds in BigInt, then perform floating point
  // division (seconds / 3600) to convert to hours.
  int64_t diff_seconds =
      BigInt::Divide(isolate, diff_ns, BigInt::FromUint64(isolate, 1000000000))
          .ToHandleChecked()
          ->AsInt64();
  double hours_in_that_day = static_cast<double>(diff_seconds) / 3600.0;
  return isolate->factory()->NewNumber(hours_in_that_day);
}

namespace {

// #sec-temporal-totemporalzoneddatetime
MaybeDirectHandle<JSTemporalZonedDateTime> ToTemporalZonedDateTime(
    Isolate* isolate, DirectHandle<Object> item_obj,
    DirectHandle<Object> options, const char* method_name) {
  TEMPORAL_ENTER_FUNC();

  Factory* factory = isolate->factory();
  // 2. Assert: Type(options) is Object or Undefined.
  DCHECK(IsUndefined(*options) || IsJSReceiver(*options));
  // 3. Let offsetBehaviour be option.
  OffsetBehaviour offset_behaviour = OffsetBehaviour::kOption;
  // 4. Let matchBehaviour be match exactly.
  MatchBehaviour match_behaviour = MatchBehaviour::kMatchExactly;

  DirectHandle<Object> offset_string;
  DirectHandle<JSReceiver> time_zone;
  DirectHandle<JSReceiver> calendar;

  temporal::DateTimeRecord result;

  // 5. If Type(item) is Object, then
  if (IsJSReceiver(*item_obj)) {
    DirectHandle<JSReceiver> item = Cast<JSReceiver>(item_obj);
    // a. If item has an [[InitializedTemporalZonedDateTime]] internal slot,
    // then
    if (IsJSTemporalZonedDateTime(*item_obj)) {
      // i. Return item.
      return Cast<JSTemporalZonedDateTime>(item_obj);
    }
    // b. Let calendar be ? GetTemporalCalendarWithISODefault(item).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, calendar,
        GetTemporalCalendarWithISODefault(isolate, item, method_name));
    // c. Let fieldNames be ? CalendarFields(calendar, « "day", "hour",
    // "microsecond", "millisecond", "minute", "month", "monthCode",
    // "nanosecond", "second", "year" »).
    DirectHandle<FixedArray> field_names = All10UnitsInFixedArray(isolate);
    ASSIGN_RETURN_ON_EXCEPTION(isolate, field_names,
                               CalendarFields(isolate, calendar, field_names));

    // d. Append "timeZone" to fieldNames.
    int32_t field_length = field_names->length();
    field_names = FixedArray::SetAndGrow(isolate, field_names, field_length++,
                                         factory->timeZone_string());

    // e. Append "offset" to fieldNames.
    field_names = FixedArray::SetAndGrow(isolate, field_names, field_length++,
                                         factory->offset_string());
    field_names->RightTrim(isolate, field_length);

    // f. Let fields be ? PrepareTemporalFields(item, fieldNames, « "timeZone"
    // »).
    DirectHandle<JSReceiver> fields;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, fields,
        PrepareTemporalFields(isolate, item, field_names,
                              RequiredFields::kTimeZone));

    // g. Let timeZone be ? Get(fields, "timeZone").
    DirectHandle<Object> time_zone_obj;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, time_zone_obj,
        JSReceiver::GetProperty(isolate, fields, factory->timeZone_string()));

    // h. Set timeZone to ? ToTemporalTimeZone(timeZone).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, time_zone,
        temporal::ToTemporalTimeZone(isolate, time_zone_obj, method_name));
    // i. Let offsetString be ? Get(fields, "offset").
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, offset_string,
        JSReceiver::GetProperty(isolate, fields, factory->offset_string()));

    // j. If offsetString is undefined, then
    if (IsUndefined(*offset_string)) {
      // i. Set offsetBehaviour to wall.
      offset_behaviour = OffsetBehaviour::kWall;
      // k. Else,
    } else {
      // i. Set offsetString to ? ToString(offsetString).
      ASSIGN_RETURN_ON_EXCEPTION(isolate, offset_string,
                                 Object::ToString(isolate, offset_string));
    }

    // l. Let result be ? InterpretTemporalDateTimeFields(calendar, fields,
    // options).
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, result,
        InterpretTemporalDateTimeFields(isolate, calendar, fields, options,
                                        method_name),
        DirectHandle<JSTemporalZonedDateTime>());
    // 5. Else,
  } else {
    // a. Perform ? ToTemporalOverflow(options).
    MAYBE_RETURN_ON_EXCEPTION_VALUE(
        isolate, ToTemporalOverflow(isolate, options, method_name),
        DirectHandle<JSTemporalZonedDateTime>());
    // b. Let string be ? ToString(item).
    DirectHandle<String> string;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, string,
                               Object::ToString(isolate, item_obj));
    // c. Let result be ? ParseTemporalZonedDateTimeString(string).
    DateTimeRecordWithCalendar parsed_result;
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, parsed_result,
        ParseTemporalZonedDateTimeString(isolate, string),
        DirectHandle<JSTemporalZonedDateTime>());
    result = {parsed_result.date, parsed_result.time};

    // d. Let timeZoneName be result.[[TimeZone]].[[Name]].
    // e. Assert: timeZoneName is not undefined.
    DCHECK(!IsUndefined(*parsed_result.time_zone.name));
    DirectHandle<String> time_zone_name =
        Cast<String>(parsed_result.time_zone.name);

    // f. If ParseText(StringToCodePoints(timeZoneName),
    // TimeZoneNumericUTCOffset) is a List of errors, then
    std::optional<ParsedISO8601Result> parsed =
        TemporalParser::ParseTimeZoneNumericUTCOffset(isolate, time_zone_name);
    if (!parsed.has_value()) {
      // i. If ! IsValidTimeZoneName(timeZoneName) is false, throw a RangeError
      // exception.
      if (!IsValidTimeZoneName(isolate, time_zone_name)) {
        THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                     NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                     DirectHandle<JSTemporalZonedDateTime>());
      }
      // ii. Set timeZoneName to ! CanonicalizeTimeZoneName(timeZoneName).
      time_zone_name = CanonicalizeTimeZoneName(isolate, time_zone_name);
    }
    // g. Let offsetString be result.[[TimeZone]].[[OffsetString]].
    offset_string = parsed_result.time_zone.offset_string;

    // h. If result.[[TimeZone]].[[Z]] is true, then
    if (parsed_result.time_zone.z) {
      // i. Set offsetBehaviour to exact.
      offset_behaviour = OffsetBehaviour::kExact;
      // i. Else if offsetString is undefined, then
    } else if (IsUndefined(*offset_string)) {
      // i. Set offsetBehaviour to wall.
      offset_behaviour = OffsetBehaviour::kWall;
    }
    // j. Let timeZone be ! CreateTemporalTimeZone(timeZoneName).
    time_zone = temporal::CreateTemporalTimeZone(isolate, time_zone_name)
                    .ToHandleChecked();
    // k. Let calendar be ?
    // ToTemporalCalendarWithISODefault(result.[[Calendar]]).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, calendar,
        ToTemporalCalendarWithISODefault(isolate, parsed_result.calendar,
                                         method_name));
    // j. Set matchBehaviour to match minutes.
    match_behaviour = MatchBehaviour::kMatchMinutes;
  }
  // 7. Let offsetNanoseconds be 0.
  int64_t offset_nanoseconds = 0;

  // 6. If offsetBehaviour is option, then
  if (offset_behaviour == OffsetBehaviour::kOption) {
    // a. Set offsetNanoseconds to ? ParseTimeZoneOffsetString(offsetString).
    DCHECK(IsString(*offset_string));
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, offset_nanoseconds,
        ParseTimeZoneOffsetString(isolate, Cast<String>(offset_string)),
        DirectHandle<JSTemporalZonedDateTime>());
  }

  // 7. Let disambiguation be ? ToTemporalDisambiguation(options).
  Disambiguation disambiguation;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, disambiguation,
      ToTemporalDisambiguation(isolate, options, method_name),
      DirectHandle<JSTemporalZonedDateTime>());

  // 8. Let offset be ? ToTemporalOffset(options, "reject").
  enum Offset offset;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, offset,
      ToTemporalOffset(isolate, options, Offset::kReject, method_name),
      DirectHandle<JSTemporalZonedDateTime>());

  // 9. Let epochNanoseconds be ? InterpretISODateTimeOffset(result.[[Year]],
  // result.[[Month]], result.[[Day]], result.[[Hour]], result.[[Minute]],
  // result.[[Second]], result.[[Millisecond]], result.[[Microsecond]],
  // result.[[Nanosecond]], offsetBehaviour, offsetNanoseconds, timeZone,
  // disambiguation, offset, matchBehaviour).
  //
  DirectHandle<BigInt> epoch_nanoseconds;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, epoch_nanoseconds,
      InterpretISODateTimeOffset(isolate, result, offset_behaviour,
                                 offset_nanoseconds, time_zone, disambiguation,
                                 offset, match_behaviour, method_name));

  // 8. Return ? CreateTemporalZonedDateTime(epochNanoseconds, timeZone,
  // calendar).
  return CreateTemporalZonedDateTime(isolate, epoch_nanoseconds, time_zone,
                                     calendar);
}

MaybeDirectHandle<JSTemporalZonedDateTime> ToTemporalZonedDateTime(
    Isolate* isolate, DirectHandle<Object> item_obj, const char* method_name) {
  // 1. If options is not present, set options to undefined.
  return ToTemporalZonedDateTime(
      isolate, item_obj, isolate->factory()->undefined_value(), method_name);
}

}  // namespace

// #sec-temporal.zoneddatetime.from
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::From(
    Isolate* isolate, DirectHandle<Object> item,
    DirectHandle<Object> options_obj) {
  const char* method_name = "Temporal.ZonedDateTime.from";
  // 1. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));

  // 2. If Type(item) is Object and item has an
  // [[InitializedTemporalZonedDateTime]] internal slot, then
  if (IsJSTemporalZonedDateTime(*item)) {
    // a. Perform ? ToTemporalOverflow(options).
    MAYBE_RETURN_ON_EXCEPTION_VALUE(
        isolate, ToTemporalOverflow(isolate, options, method_name),
        DirectHandle<JSTemporalZonedDateTime>());

    // b. Perform ? ToTemporalDisambiguation(options).
    {
      Disambiguation disambiguation;
      MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, disambiguation,
          ToTemporalDisambiguation(isolate, options, method_name),
          DirectHandle<JSTemporalZonedDateTime>());
      USE(disambiguation);
    }

    // c. Perform ? ToTemporalOffset(options, "reject").
    {
      enum Offset offset;
      MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, offset,
          ToTemporalOffset(isolate, options, Offset::kReject, method_name),
          DirectHandle<JSTemporalZonedDateTime>());
      USE(offset);
    }

    // d. Return ? CreateTemporalZonedDateTime(item.[[Nanoseconds]],
    // item.[[TimeZone]], item.[[Calendar]]).
    auto zoned_date_time = Cast<JSTemporalZonedDateTime>(item);
    return CreateTemporalZonedDateTime(
        isolate, direct_handle(zoned_date_time->nanoseconds(), isolate),
        direct_handle(zoned_date_time->time_zone(), isolate),
        direct_handle(zoned_date_time->calendar(), isolate));
  }
  // 3. Return ? ToTemporalZonedDateTime(item, options).
  return ToTemporalZonedDateTime(isolate, item, options, method_name);
}

// #sec-temporal.zoneddatetime.compare
MaybeDirectHandle<Smi> JSTemporalZonedDateTime::Compare(
    Isolate* isolate, DirectHandle<Object> one_obj,
    DirectHandle<Object> two_obj) {
  TEMPORAL_ENTER_FUNC();
  const char* method_name = "Temporal.ZonedDateTime.compare";
  // 1. Set one to ? ToTemporalZonedDateTime(one).
  DirectHandle<JSTemporalZonedDateTime> one;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, one, ToTemporalZonedDateTime(isolate, one_obj, method_name));
  // 2. Set two to ? ToTemporalZonedDateTime(two).
  DirectHandle<JSTemporalZonedDateTime> two;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, two, ToTemporalZonedDateTime(isolate, two_obj, method_name));
  // 3. Return 𝔽(! CompareEpochNanoseconds(one.[[Nanoseconds]],
  // two.[[Nanoseconds]])).
  return CompareEpochNanoseconds(isolate,
                                 direct_handle(one->nanoseconds(), isolate),
                                 direct_handle(two->nanoseconds(), isolate));
}

namespace {

// #sec-temporal-timezoneequals
Maybe<bool> TimeZoneEquals(Isolate* isolate, DirectHandle<JSReceiver> one,
                           DirectHandle<JSReceiver> two) {
  // 1. If one and two are the same Object value, return true.
  if (one.is_identical_to(two)) {
    return Just(true);
  }

  // 2. Let timeZoneOne be ? ToString(one).
  DirectHandle<String> time_zone_one;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, time_zone_one, Object::ToString(isolate, one), Nothing<bool>());
  // 3. Let timeZoneTwo be ? ToString(two).
  DirectHandle<String> time_zone_two;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, time_zone_two, Object::ToString(isolate, two), Nothing<bool>());
  // 4. If timeZoneOne is timeZoneTwo, return true.
  if (String::Equals(isolate, time_zone_one, time_zone_two)) {
    return Just(true);
  }
  // 5. Return false.
  return Just(false);
}

}  // namespace

// #sec-temporal.zoneddatetime.prototype.equals
MaybeDirectHandle<Oddball> JSTemporalZonedDateTime::Equals(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> other_obj) {
  TEMPORAL_ENTER_FUNC();
  const char* method_name = "Temporal.ZonedDateTime.prototype.equals";
  Factory* factory = isolate->factory();
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Set other to ? ToTemporalZonedDateTime(other).
  DirectHandle<JSTemporalZonedDateTime> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other, ToTemporalZonedDateTime(isolate, other_obj, method_name));
  // 4. If zonedDateTime.[[Nanoseconds]] ≠ other.[[Nanoseconds]], return false.
  if (!BigInt::EqualToBigInt(zoned_date_time->nanoseconds(),
                             other->nanoseconds())) {
    return factory->false_value();
  }
  // 5. If ? TimeZoneEquals(zonedDateTime.[[TimeZone]], other.[[TimeZone]]) is
  // false, return false.
  bool equals;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, equals,
      TimeZoneEquals(isolate,
                     direct_handle(zoned_date_time->time_zone(), isolate),
                     direct_handle(other->time_zone(), isolate)),
      DirectHandle<Oddball>());
  if (!equals) {
    return factory->false_value();
  }
  // 6. Return ? CalendarEquals(zonedDateTime.[[Calendar]], other.[[Calendar]]).
  return CalendarEquals(isolate,
                        direct_handle(zoned_date_time->calendar(), isolate),
                        direct_handle(other->calendar(), isolate));
}

namespace {

// #sec-temporal-interpretisodatetimeoffset
MaybeDirectHandle<BigInt> InterpretISODateTimeOffset(
    Isolate* isolate, const DateTimeRecord& data,
    OffsetBehaviour offset_behaviour, int64_t offset_nanoseconds,
    DirectHandle<JSReceiver> time_zone, Disambiguation disambiguation,
    Offset offset_option, MatchBehaviour match_behaviour,
    const char* method_name) {
  TEMPORAL_ENTER_FUNC();

  // 1. Assert: offsetNanoseconds is an integer or undefined.
  // 2. Let calendar be ! GetISO8601Calendar().
  DirectHandle<JSReceiver> calendar = temporal::GetISO8601Calendar(isolate);

  // 3. Let dateTime be ? CreateTemporalDateTime(year, month, day, hour, minute,
  // second, millisecond, microsecond, nanosecond, calendar).
  DirectHandle<JSTemporalPlainDateTime> date_time;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, date_time,
                             temporal::CreateTemporalDateTime(
                                 isolate, {data.date, data.time}, calendar));

  // 4. If offsetBehaviour is wall, or offsetOption is "ignore", then
  if (offset_behaviour == OffsetBehaviour::kWall ||
      offset_option == Offset::kIgnore) {
    // a. Let instant be ? BuiltinTimeZoneGetInstantFor(timeZone, dateTime,
    // disambiguation).
    DirectHandle<JSTemporalInstant> instant;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, instant,
        BuiltinTimeZoneGetInstantFor(isolate, time_zone, date_time,
                                     disambiguation, method_name));
    // b. Return instant.[[Nanoseconds]].
    return direct_handle(instant->nanoseconds(), isolate);
  }
  // 5. If offsetBehaviour is exact, or offsetOption is "use", then
  if (offset_behaviour == OffsetBehaviour::kExact ||
      offset_option == Offset::kUse) {
    // a. Let epochNanoseconds be ? GetEpochFromISOParts(year, month, day, hour,
    // minute, second, millisecond, microsecond, nanosecond).
    DirectHandle<BigInt> epoch_nanoseconds =
        GetEpochFromISOParts(isolate, {data.date, data.time});

    // b. Set epochNanoseconds to epochNanoseconds - ℤ(offsetNanoseconds).
    epoch_nanoseconds =
        BigInt::Subtract(isolate, epoch_nanoseconds,
                         BigInt::FromInt64(isolate, offset_nanoseconds))
            .ToHandleChecked();
    // c. If ! IsValidEpochNanoseconds(epochNanoseconds) is false, throw a
    // RangeError exception.
    if (!IsValidEpochNanoseconds(isolate, epoch_nanoseconds)) {
      THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
    }
    // d. Return epochNanoseconds.
    return epoch_nanoseconds;
  }
  // 6. Assert: offsetBehaviour is option.
  DCHECK_EQ(offset_behaviour, OffsetBehaviour::kOption);
  // 7. Assert: offsetOption is "prefer" or "reject".
  DCHECK(offset_option == Offset::kPrefer || offset_option == Offset::kReject);
  // 8. Let possibleInstants be ? GetPossibleInstantsFor(timeZone, dateTime).
  DirectHandle<FixedArray> possible_instants;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, possible_instants,
      GetPossibleInstantsFor(isolate, time_zone, date_time));

  // 9. For each element candidate of possibleInstants, do
  for (int i = 0; i < possible_instants->length(); i++) {
    DCHECK(IsJSTemporalInstant(possible_instants->get(i)));
    DirectHandle<JSTemporalInstant> candidate(
        Cast<JSTemporalInstant>(possible_instants->get(i)), isolate);
    // a. Let candidateNanoseconds be ? GetOffsetNanosecondsFor(timeZone,
    // candidate).
    int64_t candidate_nanoseconds;
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, candidate_nanoseconds,
        GetOffsetNanosecondsFor(isolate, time_zone, candidate, method_name),
        DirectHandle<BigInt>());
    // b. If candidateNanoseconds = offsetNanoseconds, then
    if (candidate_nanoseconds == offset_nanoseconds) {
      // i. Return candidate.[[Nanoseconds]].
      return DirectHandle<BigInt>(candidate->nanoseconds(), isolate);
    }
    // c. If matchBehaviour is match minutes, then
    if (match_behaviour == MatchBehaviour::kMatchMinutes) {
      // i. Let roundedCandidateNanoseconds be !
      // RoundNumberToIncrement(candidateNanoseconds, 60 × 10^9, "halfExpand").
      double rounded_candidate_nanoseconds = RoundNumberToIncrement(
          isolate, candidate_nanoseconds, 6e10, RoundingMode::kHalfExpand);
      // ii. If roundedCandidateNanoseconds = offsetNanoseconds, then
      if (rounded_candidate_nanoseconds == offset_nanoseconds) {
        // 1. Return candidate.[[Nanoseconds]].
        return DirectHandle<BigInt>(candidate->nanoseconds(), isolate);
      }
    }
  }
  // 10. If offsetOption is "reject", throw a RangeError exception.
  if (offset_option == Offset::kReject) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }
  // 11. Let instant be ? DisambiguatePossibleInstants(possibleInstants,
  // timeZone, dateTime, disambiguation).
  DirectHandle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant,
      DisambiguatePossibleInstants(isolate, possible_instants, time_zone,
                                   date_time, disambiguation, method_name));
  // 12. Return instant.[[Nanoseconds]].
  return DirectHandle<BigInt>(instant->nanoseconds(), isolate);
}

}  // namespace

// #sec-temporal.zoneddatetime.prototype.with
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::With(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> temporal_zoned_date_time_like_obj,
    DirectHandle<Object> options_obj) {
  TEMPORAL_ENTER_FUNC();
  const char* method_name = "Temporal.ZonedDateTime.prototype.with";
  Factory* factory = isolate->factory();
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. If Type(temporalZonedDateTimeLike) is not Object, then
  if (!IsJSReceiver(*temporal_zoned_date_time_like_obj)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }
  DirectHandle<JSReceiver> temporal_zoned_date_time_like =
      Cast<JSReceiver>(temporal_zoned_date_time_like_obj);
  // 4. Perform ? RejectObjectWithCalendarOrTimeZone(temporalZonedDateTimeLike).
  MAYBE_RETURN(RejectObjectWithCalendarOrTimeZone(
                   isolate, temporal_zoned_date_time_like),
               DirectHandle<JSTemporalZonedDateTime>());

  // 5. Let calendar be zonedDateTime.[[Calendar]].
  DirectHandle<JSReceiver> calendar(zoned_date_time->calendar(), isolate);

  // 6. Let fieldNames be ? CalendarFields(calendar, « "day", "hour",
  // "microsecond", "millisecond", "minute", "month", "monthCode", "nanosecond",
  // "second", "year" »).
  DirectHandle<FixedArray> field_names;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, field_names,
      CalendarFields(isolate, calendar, All10UnitsInFixedArray(isolate)));

  // 7. Append "offset" to fieldNames.
  int32_t field_length = field_names->length();
  field_names = FixedArray::SetAndGrow(isolate, field_names, field_length++,
                                       factory->offset_string());
  field_names->RightTrim(isolate, field_length);

  // 8. Let partialZonedDateTime be ?
  // PreparePartialTemporalFields(temporalZonedDateTimeLike, fieldNames).
  DirectHandle<JSReceiver> partial_zoned_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, partial_zoned_date_time,
      PreparePartialTemporalFields(isolate, temporal_zoned_date_time_like,
                                   field_names));
  // 9. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));

  // 10. Let disambiguation be ? ToTemporalDisambiguation(options).
  Disambiguation disambiguation;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, disambiguation,
      ToTemporalDisambiguation(isolate, options, method_name),
      DirectHandle<JSTemporalZonedDateTime>());

  // 11. Let offset be ? ToTemporalOffset(options, "prefer").
  enum Offset offset;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, offset,
      ToTemporalOffset(isolate, options, Offset::kPrefer, method_name),
      DirectHandle<JSTemporalZonedDateTime>());

  // 12. Let timeZone be zonedDateTime.[[TimeZone]].
  DirectHandle<JSReceiver> time_zone(zoned_date_time->time_zone(), isolate);

  // 13. Append "timeZone" to fieldNames.
  field_length = field_names->length();
  field_names = FixedArray::SetAndGrow(isolate, field_names, field_length++,
                                       factory->timeZone_string());
  field_names->RightTrim(isolate, field_length);

  // 14. Let fields be ? PrepareTemporalFields(zonedDateTime, fieldNames, «
  // "timeZone", "offset"»).
  DirectHandle<JSReceiver> fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, fields,
      PrepareTemporalFields(isolate, zoned_date_time, field_names,
                            RequiredFields::kTimeZoneAndOffset));
  // 15. Set fields to ? CalendarMergeFields(calendar, fields,
  // partialZonedDateTime).
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, fields,
      CalendarMergeFields(isolate, calendar, fields, partial_zoned_date_time));

  // 16. Set fields to ? PrepareTemporalFields(fields, fieldNames, « "timeZone"
  // , "offset"»).
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, fields,
      PrepareTemporalFields(isolate, fields, field_names,
                            RequiredFields::kTimeZoneAndOffset));

  // 17. Let offsetString be ? Get(fields, "offset").
  DirectHandle<Object> offset_string;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, offset_string,
      JSReceiver::GetProperty(isolate, fields, factory->offset_string()));

  // 18. Assert: Type(offsetString) is String.
  DCHECK(IsString(*offset_string));

  // 19. Let dateTimeResult be ? InterpretTemporalDateTimeFields(calendar,
  // fields, options).
  temporal::DateTimeRecord date_time_result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, date_time_result,
      InterpretTemporalDateTimeFields(isolate, calendar, fields, options,
                                      method_name),
      DirectHandle<JSTemporalZonedDateTime>());

  // 20. Let offsetNanoseconds be ? ParseTimeZoneOffsetString(offsetString).
  int64_t offset_nanoseconds;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, offset_nanoseconds,
      ParseTimeZoneOffsetString(isolate, Cast<String>(offset_string)),
      DirectHandle<JSTemporalZonedDateTime>());

  // 21. Let epochNanoseconds be ?
  // InterpretISODateTimeOffset(dateTimeResult.[[Year]],
  // dateTimeResult.[[Month]], dateTimeResult.[[Day]], dateTimeResult.[[Hour]],
  // dateTimeResult.[[Minute]], dateTimeResult.[[Second]],
  // dateTimeResult.[[Millisecond]], dateTimeResult.[[Microsecond]],
  // dateTimeResult.[[Nanosecond]], option, offsetNanoseconds, timeZone,
  // disambiguation, offset, match exactly).
  DirectHandle<BigInt> epoch_nanoseconds;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, epoch_nanoseconds,
      InterpretISODateTimeOffset(
          isolate, {date_time_result.date, date_time_result.time},
          OffsetBehaviour::kOption, offset_nanoseconds, time_zone,
          disambiguation, offset, MatchBehaviour::kMatchExactly, method_name));

  // 27. Return ? CreateTemporalZonedDateTime(epochNanoseconds, timeZone,
  // calendar).
  return CreateTemporalZonedDateTime(isolate, epoch_nanoseconds, time_zone,
                                     calendar);
}

// #sec-temporal.zoneddatetime.prototype.withcalendar
MaybeDirectHandle<JSTemporalZonedDateTime>
JSTemporalZonedDateTime::WithCalendar(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> calendar_like) {
  TEMPORAL_ENTER_FUNC();
  const char* method_name = "Temporal.ZonedDateTime.prototype.withCalendar";
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Let calendar be ? ToTemporalCalendar(calendarLike).
  DirectHandle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar,
      temporal::ToTemporalCalendar(isolate, calendar_like, method_name));

  // 4. Return ? CreateTemporalZonedDateTime(zonedDateTime.[[Nanoseconds]],
  // zonedDateTime.[[TimeZone]], calendar).
  DirectHandle<BigInt> nanoseconds(zoned_date_time->nanoseconds(), isolate);
  DirectHandle<JSReceiver> time_zone(zoned_date_time->time_zone(), isolate);
  return CreateTemporalZonedDateTime(isolate, nanoseconds, time_zone, calendar);
}

// #sec-temporal.zoneddatetime.prototype.withplaindate
MaybeDirectHandle<JSTemporalZonedDateTime>
JSTemporalZonedDateTime::WithPlainDate(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> plain_date_like) {
  TEMPORAL_ENTER_FUNC();
  const char* method_name = "Temporal.ZonedDateTime.prototype.withPlainDate";
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Let plainDate be ? ToTemporalDate(plainDateLike).
  DirectHandle<JSTemporalPlainDate> plain_date;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, plain_date,
      ToTemporalDate(isolate, plain_date_like, method_name));

  // 4. Let timeZone be zonedDateTime.[[TimeZone]].
  DirectHandle<JSReceiver> time_zone(zoned_date_time->time_zone(), isolate);
  // 5. Let instant be ! CreateTemporalInstant(zonedDateTime.[[Nanoseconds]]).
  DirectHandle<JSTemporalInstant> instant =
      temporal::CreateTemporalInstant(
          isolate, direct_handle(zoned_date_time->nanoseconds(), isolate))
          .ToHandleChecked();

  // 6. Let plainDateTime be ?
  // temporal::BuiltinTimeZoneGetPlainDateTimeFor(timeZone, instant,
  // zonedDateTime.[[Calendar]]).
  DirectHandle<JSTemporalPlainDateTime> plain_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, plain_date_time,
      temporal::BuiltinTimeZoneGetPlainDateTimeFor(
          isolate, time_zone, instant,
          direct_handle(zoned_date_time->calendar(), isolate), method_name));
  // 7. Let calendar be ? ConsolidateCalendars(zonedDateTime.[[Calendar]],
  // plainDate.[[Calendar]]).
  DirectHandle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar,
      ConsolidateCalendars(isolate,
                           direct_handle(zoned_date_time->calendar(), isolate),
                           direct_handle(plain_date->calendar(), isolate)));

  // 8. Let resultPlainDateTime be ?
  // CreateTemporalDateTime(plainDate.[[ISOYear]], plainDate.[[ISOMonth]],
  // plainDate.[[ISODay]], plainDateTime.[[ISOHour]],
  // plainDateTime.[[ISOMinute]], plainDateTime.[[ISOSecond]],
  // plainDateTime.[[ISOMillisecond]], plainDateTime.[[ISOMicrosecond]],
  // plainDateTime.[[ISONanosecond]], calendar).
  DirectHandle<JSTemporalPlainDateTime> result_plain_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, result_plain_date_time,
      temporal::CreateTemporalDateTime(
          isolate,
          {{plain_date->iso_year(), plain_date->iso_month(),
            plain_date->iso_day()},
           {plain_date_time->iso_hour(), plain_date_time->iso_minute(),
            plain_date_time->iso_second(), plain_date_time->iso_millisecond(),
            plain_date_time->iso_microsecond(),
            plain_date_time->iso_nanosecond()}},
          calendar));
  // 9. Set instant to ? BuiltinTimeZoneGetInstantFor(timeZone,
  // resultPlainDateTime, "compatible").
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant,
      BuiltinTimeZoneGetInstantFor(isolate, time_zone, result_plain_date_time,
                                   Disambiguation::kCompatible, method_name));
  // 10. Return ? CreateTemporalZonedDateTime(instant.[[Nanoseconds]], timeZone,
  // calendar).
  return CreateTemporalZonedDateTime(
      isolate, direct_handle(instant->nanoseconds(), isolate), time_zone,
      calendar);
}

// #sec-temporal.zoneddatetime.prototype.withplaintime
MaybeDirectHandle<JSTemporalZonedDateTime>
JSTemporalZonedDateTime::WithPlainTime(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> plain_time_like) {
  TEMPORAL_ENTER_FUNC();
  const char* method_name = "Temporal.ZonedDateTime.prototype.withPlainTime";
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. If plainTimeLike is undefined, then
  DirectHandle<JSTemporalPlainTime> plain_time;
  if (IsUndefined(*plain_time_like)) {
    // a. Let plainTime be ? CreateTemporalTime(0, 0, 0, 0, 0, 0).
    ASSIGN_RETURN_ON_EXCEPTION(isolate, plain_time,
                               CreateTemporalTime(isolate, {0, 0, 0, 0, 0, 0}));
    // 4. Else,
  } else {
    // a. Let plainTime be ? ToTemporalTime(plainTimeLike).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, plain_time,
        temporal::ToTemporalTime(isolate, plain_time_like, method_name));
  }
  // 5. Let timeZone be zonedDateTime.[[TimeZone]].
  DirectHandle<JSReceiver> time_zone(zoned_date_time->time_zone(), isolate);
  // 6. Let instant be ! CreateTemporalInstant(zonedDateTime.[[Nanoseconds]]).
  DirectHandle<JSTemporalInstant> instant =
      temporal::CreateTemporalInstant(
          isolate, direct_handle(zoned_date_time->nanoseconds(), isolate))
          .ToHandleChecked();
  // 7. Let calendar be zonedDateTime.[[Calendar]].
  DirectHandle<JSReceiver> calendar(zoned_date_time->calendar(), isolate);
  // 8. Let plainDateTime be ?
  // temporal::BuiltinTimeZoneGetPlainDateTimeFor(timeZone, instant, calendar).
  DirectHandle<JSTemporalPlainDateTime> plain_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, plain_date_time,
      temporal::BuiltinTimeZoneGetPlainDateTimeFor(isolate, time_zone, instant,
                                                   calendar, method_name));
  // 9. Let resultPlainDateTime be ?
  // CreateTemporalDateTime(plainDateTime.[[ISOYear]],
  // plainDateTime.[[ISOMonth]], plainDateTime.[[ISODay]],
  // plainTime.[[ISOHour]], plainTime.[[ISOMinute]], plainTime.[[ISOSecond]],
  // plainTime.[[ISOMillisecond]], plainTime.[[ISOMicrosecond]],
  // plainTime.[[ISONanosecond]], calendar).
  DirectHandle<JSTemporalPlainDateTime> result_plain_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, result_plain_date_time,
      temporal::CreateTemporalDateTime(
          isolate,
          {{plain_date_time->iso_year(), plain_date_time->iso_month(),
            plain_date_time->iso_day()},
           {plain_time->iso_hour(), plain_time->iso_minute(),
            plain_time->iso_second(), plain_time->iso_millisecond(),
            plain_time->iso_microsecond(), plain_time->iso_nanosecond()}},
          calendar));
  // 10. Let instant be ? BuiltinTimeZoneGetInstantFor(timeZone,
  // resultPlainDateTime, "compatible").
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant,
      BuiltinTimeZoneGetInstantFor(isolate, time_zone, result_plain_date_time,
                                   Disambiguation::kCompatible, method_name));
  // 11. Return ? CreateTemporalZonedDateTime(instant.[[Nanoseconds]], timeZone,
  // calendar).
  return CreateTemporalZonedDateTime(
      isolate, direct_handle(instant->nanoseconds(), isolate), time_zone,
      calendar);
}

// #sec-temporal.zoneddatetime.prototype.withtimezone
MaybeDirectHandle<JSTemporalZonedDateTime>
JSTemporalZonedDateTime::WithTimeZone(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> time_zone_like) {
  TEMPORAL_ENTER_FUNC();
  const char* method_name = "Temporal.ZonedDateTime.prototype.withTimeZone";
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Let timeZone be ? ToTemporalTimeZone(timeZoneLike).
  DirectHandle<JSReceiver> time_zone;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, time_zone,
      temporal::ToTemporalTimeZone(isolate, time_zone_like, method_name));

  // 4. Return ? CreateTemporalZonedDateTime(zonedDateTime.[[Nanoseconds]],
  // timeZone, zonedDateTime.[[Calendar]]).
  DirectHandle<BigInt> nanoseconds(zoned_date_time->nanoseconds(), isolate);
  DirectHandle<JSReceiver> calendar(zoned_date_time->calendar(), isolate);
  return CreateTemporalZonedDateTime(isolate, nanoseconds, time_zone, calendar);
}

// Common code shared by ZonedDateTime.prototype.toPlainYearMonth and
// toPlainMonthDay
template <typename T, MaybeDirectHandle<T> (*from_fields_func)(
                          Isolate*, DirectHandle<JSReceiver>,
                          DirectHandle<JSReceiver>, DirectHandle<Object>)>
MaybeDirectHandle<T> ZonedDateTimeToPlainYearMonthOrMonthDay(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<String> field_name_1, DirectHandle<String> field_name_2,
    const char* method_name) {
  TEMPORAL_ENTER_FUNC();
  Factory* factory = isolate->factory();
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Let timeZone be zonedDateTime.[[TimeZone]].
  DirectHandle<JSReceiver> time_zone(zoned_date_time->time_zone(), isolate);
  // 4. Let instant be ! CreateTemporalInstant(zonedDateTime.[[Nanoseconds]]).
  DirectHandle<JSTemporalInstant> instant =
      temporal::CreateTemporalInstant(
          isolate, direct_handle(zoned_date_time->nanoseconds(), isolate))
          .ToHandleChecked();
  // 5. Let calendar be zonedDateTime.[[Calendar]].
  DirectHandle<JSReceiver> calendar(zoned_date_time->calendar(), isolate);
  // 6. Let temporalDateTime be ?
  // temporal::BuiltinTimeZoneGetPlainDateTimeFor(timeZone, instant, calendar).
  DirectHandle<JSTemporalPlainDateTime> temporal_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temporal_date_time,
      temporal::BuiltinTimeZoneGetPlainDateTimeFor(isolate, time_zone, instant,
                                                   calendar, method_name));
  // 7. Let fieldNames be ? CalendarFields(calendar, « field_name_1,
  // field_name_2 »).
  DirectHandle<FixedArray> field_names = factory->NewFixedArray(2);
  field_names->set(0, *field_name_1);
  field_names->set(1, *field_name_2);
  ASSIGN_RETURN_ON_EXCEPTION(isolate, field_names,
                             CalendarFields(isolate, calendar, field_names));
  // 8. Let fields be ? PrepareTemporalFields(temporalDateTime, fieldNames, «»).
  DirectHandle<JSReceiver> fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, fields,
      PrepareTemporalFields(isolate, temporal_date_time, field_names,
                            RequiredFields::kNone));
  // 9. Return ? XxxFromFields(calendar, fields).
  return from_fields_func(isolate, calendar, fields,
                          factory->undefined_value());
}

// #sec-temporal.zoneddatetime.prototype.toplainyearmonth
MaybeDirectHandle<JSTemporalPlainYearMonth>
JSTemporalZonedDateTime::ToPlainYearMonth(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  return ZonedDateTimeToPlainYearMonthOrMonthDay<JSTemporalPlainYearMonth,
                                                 YearMonthFromFields>(
      isolate, zoned_date_time, isolate->factory()->monthCode_string(),
      isolate->factory()->year_string(),
      "Temporal.ZonedDateTime.prototype.toPlainYearMonth");
}

// #sec-temporal.zoneddatetime.prototype.toplainmonthday
MaybeDirectHandle<JSTemporalPlainMonthDay>
JSTemporalZonedDateTime::ToPlainMonthDay(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  return ZonedDateTimeToPlainYearMonthOrMonthDay<JSTemporalPlainMonthDay,
                                                 MonthDayFromFields>(
      isolate, zoned_date_time, isolate->factory()->day_string(),
      isolate->factory()->monthCode_string(),
      "Temporal.ZonedDateTime.prototype.toPlainMonthDay");
}

namespace {

// #sec-temporal-temporalzoneddatetimetostring
MaybeDirectHandle<String> TemporalZonedDateTimeToString(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    Precision precision, ShowCalendar show_calendar,
    ShowTimeZone show_time_zone, ShowOffset show_offset, double increment,
    Unit unit, RoundingMode rounding_mode, const char* method_name) {
  // 4. Let ns be ! RoundTemporalInstant(zonedDateTime.[[Nanoseconds]],
  // increment, unit, roundingMode).
  DirectHandle<BigInt> ns = RoundTemporalInstant(
      isolate, direct_handle(zoned_date_time->nanoseconds(), isolate),
      increment, unit, rounding_mode);

  // 5. Let timeZone be zonedDateTime.[[TimeZone]].
  DirectHandle<JSReceiver> time_zone(zoned_date_time->time_zone(), isolate);
  // 6. Let instant be ! CreateTemporalInstant(ns).
  DirectHandle<JSTemporalInstant> instant =
      temporal::CreateTemporalInstant(isolate, ns).ToHandleChecked();

  // 7. Let isoCalendar be ! GetISO8601Calendar().
  DirectHandle<JSTemporalCalendar> iso_calendar =
      temporal::GetISO8601Calendar(isolate);

  // 8. Let temporalDateTime be ?
  // BuiltinTimeZoneGetPlainDateTimeFor(timeZone, instant,
  // isoCalendar).
  DirectHandle<JSTemporalPlainDateTime> temporal_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temporal_date_time,
      temporal::BuiltinTimeZoneGetPlainDateTimeFor(isolate, time_zone, instant,
                                                   iso_calendar, method_name));
  // 9. Let dateTimeString be ?
  // TemporalDateTimeToString(temporalDateTime.[[ISOYear]],
  // temporalDateTime.[[ISOMonth]], temporalDateTime.[[ISODay]],
  // temporalDateTime.[[ISOHour]], temporalDateTime.[[ISOMinute]],
  // temporalDateTime.[[ISOSecond]], temporalDateTime.[[ISOMillisecond]],
  // temporalDateTime.[[ISOMicrosecond]], temporalDateTime.[[ISONanosecond]],
  // isoCalendar, precision, "never").
  DirectHandle<String> date_time_string;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, date_time_string,
      TemporalDateTimeToString(
          isolate,
          {{temporal_date_time->iso_year(), temporal_date_time->iso_month(),
            temporal_date_time->iso_day()},
           {temporal_date_time->iso_hour(), temporal_date_time->iso_minute(),
            temporal_date_time->iso_second(),
            temporal_date_time->iso_millisecond(),
            temporal_date_time->iso_microsecond(),
            temporal_date_time->iso_nanosecond()}},
          iso_calendar, precision, ShowCalendar::kNever));

  IncrementalStringBuilder builder(isolate);
  builder.AppendString(date_time_string);

  // 10. If showOffset is "never", then
  if (show_offset == ShowOffset::kNever) {
    // a. Let offsetString be the empty String.
    // 11. Else,
  } else {
    // a. Let offsetNs be ? GetOffsetNanosecondsFor(timeZone, instant).
    int64_t offset_ns;
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, offset_ns,
        GetOffsetNanosecondsFor(isolate, time_zone, instant, method_name),
        DirectHandle<String>());
    // b. Let offsetString be ! FormatISOTimeZoneOffsetString(offsetNs).
    builder.AppendString(FormatISOTimeZoneOffsetString(isolate, offset_ns));
  }

  // 12. If showTimeZone is "never", then
  if (show_time_zone == ShowTimeZone::kNever) {
    // a. Let timeZoneString be the empty String.
    // 13. Else,
  } else {
    // a. Let timeZoneID be ? ToString(timeZone).
    DirectHandle<String> time_zone_id;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, time_zone_id,
                               Object::ToString(isolate, time_zone));
    // b. Let timeZoneString be the string-concatenation of the code unit 0x005B
    // (LEFT SQUARE BRACKET), timeZoneID, and the code unit 0x005D (RIGHT SQUARE
    // BRACKET).
    builder.AppendCStringLiteral("[");
    builder.AppendString(time_zone_id);
    builder.AppendCStringLiteral("]");
  }
  // 14. Let calendarString be ?
  // MaybeFormatCalendarAnnotation(zonedDateTime.[[Calendar]], showCalendar).
  DirectHandle<String> calendar_string;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar_string,
      MaybeFormatCalendarAnnotation(
          isolate, direct_handle(zoned_date_time->calendar(), isolate),
          show_calendar));

  // 15. Return the string-concatenation of dateTimeString, offsetString,
  // timeZoneString, and calendarString.
  builder.AppendString(calendar_string);
  return builder.Finish();
}

// #sec-temporal-temporalzoneddatetimetostring
MaybeDirectHandle<String> TemporalZonedDateTimeToString(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    Precision precision, ShowCalendar show_calendar,
    ShowTimeZone show_time_zone, ShowOffset show_offset,
    const char* method_name) {
  // 1. Assert: Type(zonedDateTime) is Object and zonedDateTime has an
  // [[InitializedTemporalZonedDateTime]] internal slot.
  // 2. If increment is not present, set it to 1.
  // 3. If unit is not present, set it to "nanosecond".
  // 4. If roundingMode is not present, set it to "trunc".
  return TemporalZonedDateTimeToString(
      isolate, zoned_date_time, precision, show_calendar, show_time_zone,
      show_offset, 1, Unit::kNanosecond, RoundingMode::kTrunc, method_name);
}

}  // namespace
// #sec-temporal.zoneddatetime.prototype.tojson
MaybeDirectHandle<String> JSTemporalZonedDateTime::ToJSON(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC();
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Return ? TemporalZonedDateTimeToString(zonedDateTime, "auto", "auto",
  // "auto", "auto").
  return TemporalZonedDateTimeToString(
      isolate, zoned_date_time, Precision::kAuto, ShowCalendar::kAuto,
      ShowTimeZone::kAuto, ShowOffset::kAuto,
      "Temporal.ZonedDateTime.prototype.toJSON");
}

// #sec-temporal.zoneddatetime.prototype.tolocalestring
MaybeDirectHandle<String> JSTemporalZonedDateTime::ToLocaleString(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> locales, DirectHandle<Object> options) {
  const char* method_name = "Temporal.ZonedDateTime.prototype.toLocaleString";
#ifdef V8_INTL_SUPPORT
  return JSDateTimeFormat::TemporalToLocaleString(
      isolate, zoned_date_time, locales, options, method_name);
#else   //  V8_INTL_SUPPORT
  return TemporalZonedDateTimeToString(
      isolate, zoned_date_time, Precision::kAuto, ShowCalendar::kAuto,
      ShowTimeZone::kAuto, ShowOffset::kAuto, method_name);
#endif  //  V8_INTL_SUPPORT
}

// #sec-temporal.zoneddatetime.prototype.tostring
MaybeDirectHandle<String> JSTemporalZonedDateTime::ToString(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> options_obj) {
  const char* method_name = "Temporal.ZonedDateTime.prototype.toString";
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));

  // 4. Let precision be ? ToSecondsStringPrecision(options).
  StringPrecision precision;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, precision,
      ToSecondsStringPrecision(isolate, options, method_name),
      DirectHandle<String>());

  // 5. Let roundingMode be ? ToTemporalRoundingMode(options, "trunc").
  RoundingMode rounding_mode;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, rounding_mode,
      ToTemporalRoundingMode(isolate, options, RoundingMode::kTrunc,
                             method_name),
      DirectHandle<String>());

  // 6. Let showCalendar be ? ToShowCalendarOption(options).
  ShowCalendar show_calendar;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, show_calendar,
      ToShowCalendarOption(isolate, options, method_name),
      DirectHandle<String>());

  // 7. Let showTimeZone be ? ToShowTimeZoneNameOption(options).
  ShowTimeZone show_time_zone;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, show_time_zone,
      ToShowTimeZoneNameOption(isolate, options, method_name),
      DirectHandle<String>());

  // 8. Let showOffset be ? ToShowOffsetOption(options).
  ShowOffset show_offset;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, show_offset, ToShowOffsetOption(isolate, options, method_name),
      DirectHandle<String>());

  // 9. Return ? TemporalZonedDateTimeToString(zonedDateTime,
  // precision.[[Precision]], showCalendar, showTimeZone, showOffset,
  // precision.[[Increment]], precision.[[Unit]], roundingMode).
  return TemporalZonedDateTimeToString(
      isolate, zoned_date_time, precision.precision, show_calendar,
      show_time_zone, show_offset, precision.increment, precision.unit,
      rounding_mode, method_name);
}

// #sec-temporal.now.zoneddatetime
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::Now(
    Isolate* isolate, DirectHandle<Object> calendar_like,
    DirectHandle<Object> temporal_time_zone_like) {
  const char* method_name = "Temporal.Now.zonedDateTime";
  // 1. Return ? SystemZonedDateTime(temporalTimeZoneLike, calendarLike).
  return SystemZonedDateTime(isolate, temporal_time_zone_like, calendar_like,
                             method_name);
}

// #sec-temporal.now.zoneddatetimeiso
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::NowISO(
    Isolate* isolate, DirectHandle<Object> temporal_time_zone_like) {
  TEMPORAL_ENTER_FUNC();
  const char* method_name = "Temporal.Now.zonedDateTimeISO";
  // 1. Let calendar be ! GetISO8601Calendar().
  DirectHandle<JSReceiver> calendar = temporal::GetISO8601Calendar(isolate);
  // 2. Return ? SystemZonedDateTime(temporalTimeZoneLike, calendar).
  return SystemZonedDateTime(isolate, temporal_time_zone_like, calendar,
                             method_name);
}

// #sec-temporal.zoneddatetime.prototype.round
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::Round(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> round_to_obj) {
  const char* method_name = "Temporal.ZonedDateTime.prototype.round";
  Factory* factory = isolate->factory();
  // 1. Let temporalTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. If roundTo is undefined, then
  if (IsUndefined(*round_to_obj)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }

  DirectHandle<JSReceiver> round_to;
  // 4. If Type(roundTo) is String, then
  if (IsString(*round_to_obj)) {
    // a. Let paramString be roundTo.
    DirectHandle<String> param_string = Cast<String>(round_to_obj);
    // b. Set roundTo to ! OrdinaryObjectCreate(null).
    round_to = factory->NewJSObjectWithNullProto();
    // c. Perform ! CreateDataPropertyOrThrow(roundTo, "smallestUnit",
    // paramString).
    CHECK(JSReceiver::CreateDataProperty(isolate, round_to,
                                         factory->smallestUnit_string(),
                                         param_string, Just(kThrowOnError))
              .FromJust());
    // 5. Else
  } else {
    // a. Set roundTo to ? GetOptionsObject(roundTo).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, round_to,
        GetOptionsObject(isolate, round_to_obj, method_name));
  }

  // 6. Let smallestUnit be ? GetTemporalUnit(roundTo, "smallestUnit", time,
  // required, « "day" »).
  Unit smallest_unit;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, smallest_unit,
      GetTemporalUnit(isolate, round_to, "smallestUnit", UnitGroup::kTime,
                      Unit::kDay, true, method_name, Unit::kDay),
      DirectHandle<JSTemporalZonedDateTime>());

  // 7. Let roundingMode be ? ToTemporalRoundingMode(roundTo, "halfExpand").
  RoundingMode rounding_mode;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, rounding_mode,
      ToTemporalRoundingMode(isolate, round_to, RoundingMode::kHalfExpand,
                             method_name),
      DirectHandle<JSTemporalZonedDateTime>());

  // 8. Let roundingIncrement be ? ToTemporalDateTimeRoundingIncrement(roundTo,
  // smallestUnit).
  double rounding_increment;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, rounding_increment,
      ToTemporalDateTimeRoundingIncrement(isolate, round_to, smallest_unit),
      DirectHandle<JSTemporalZonedDateTime>());

  // 9. Let timeZone be zonedDateTime.[[TimeZone]].
  DirectHandle<JSReceiver> time_zone(zoned_date_time->time_zone(), isolate);
  // 10. Let instant be ! CreateTemporalInstant(zonedDateTime.[[Nanoseconds]]).
  DirectHandle<JSTemporalInstant> instant =
      temporal::CreateTemporalInstant(
          isolate, direct_handle(zoned_date_time->nanoseconds(), isolate))
          .ToHandleChecked();
  // 11. Let calendar be zonedDateTime.[[Calendar]].
  DirectHandle<JSReceiver> calendar(zoned_date_time->calendar(), isolate);
  // 12. Let temporalDateTime be ? BuiltinTimeZoneGetPlainDateTimeFor(timeZone,
  // instant, calendar).
  DirectHandle<JSTemporalPlainDateTime> temporal_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temporal_date_time,
      temporal::BuiltinTimeZoneGetPlainDateTimeFor(isolate, time_zone, instant,
                                                   calendar, method_name));
  // 13. Let isoCalendar be ! GetISO8601Calendar().
  DirectHandle<JSReceiver> iso_calendar = temporal::GetISO8601Calendar(isolate);

  // 14. Let dtStart be ? CreateTemporalDateTime(temporalDateTime.[[ISOYear]],
  // temporalDateTime.[[ISOMonth]], temporalDateTime.[[ISODay]], 0, 0, 0, 0, 0,
  // 0, isoCalendar).
  DirectHandle<JSTemporalPlainDateTime> dt_start;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, dt_start,
      temporal::CreateTemporalDateTime(
          isolate,
          {{temporal_date_time->iso_year(), temporal_date_time->iso_month(),
            temporal_date_time->iso_day()},
           {0, 0, 0, 0, 0, 0}},
          iso_calendar));
  // 15. Let instantStart be ? BuiltinTimeZoneGetInstantFor(timeZone, dtStart,
  // "compatible").
  DirectHandle<JSTemporalInstant> instant_start;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant_start,
      BuiltinTimeZoneGetInstantFor(isolate, time_zone, dt_start,
                                   Disambiguation::kCompatible, method_name));
  // 16. Let startNs be instantStart.[[Nanoseconds]].
  DirectHandle<BigInt> start_ns(instant_start->nanoseconds(), isolate);
  // 17. Let endNs be ? AddZonedDateTime(startNs, timeZone, calendar, 0, 0, 0,
  // 1, 0, 0, 0, 0, 0, 0).
  DirectHandle<BigInt> end_ns;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, end_ns,
      AddZonedDateTime(isolate, start_ns, time_zone, calendar,
                       {0, 0, 0, {1, 0, 0, 0, 0, 0, 0}}, method_name));
  // 18. Let dayLengthNs be ℝ(endNs - startNs).
  DirectHandle<BigInt> day_length_ns =
      BigInt::Subtract(isolate, end_ns, start_ns).ToHandleChecked();
  // 19. If dayLengthNs ≤ 0, then
  if (day_length_ns->IsNegative() || !day_length_ns->ToBoolean()) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }
  // 20. Let roundResult be ! RoundISODateTime(temporalDateTime.[[ISOYear]],
  // temporalDateTime.[[ISOMonth]], temporalDateTime.[[ISODay]],
  // temporalDateTime.[[ISOHour]], temporalDateTime.[[ISOMinute]],
  // temporalDateTime.[[ISOSecond]], temporalDateTime.[[ISOMillisecond]],
  // temporalDateTime.[[ISOMicrosecond]], temporalDateTime.[[ISONanosecond]],
  // roundingIncrement, smallestUnit, roundingMode, dayLengthNs).
  DateTimeRecord round_result = RoundISODateTime(
      isolate,
      {{temporal_date_time->iso_year(), temporal_date_time->iso_month(),
        temporal_date_time->iso_day()},
       {temporal_date_time->iso_hour(), temporal_date_time->iso_minute(),
        temporal_date_time->iso_second(), temporal_date_time->iso_millisecond(),
        temporal_date_time->iso_microsecond(),
        temporal_date_time->iso_nanosecond()}},
      rounding_increment, smallest_unit, rounding_mode,
      Object::NumberValue(*BigInt::ToNumber(isolate, day_length_ns)));
  // 21. Let offsetNanoseconds be ? GetOffsetNanosecondsFor(timeZone, instant).
  int64_t offset_nanoseconds;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, offset_nanoseconds,
      GetOffsetNanosecondsFor(isolate, time_zone, instant, method_name),
      DirectHandle<JSTemporalZonedDateTime>());
  // 22. Let epochNanoseconds be ?
  // InterpretISODateTimeOffset(roundResult.[[Year]], roundResult.[[Month]],
  // roundResult.[[Day]], roundResult.[[Hour]], roundResult.[[Minute]],
  // roundResult.[[Second]], roundResult.[[Millisecond]],
  // roundResult.[[Microsecond]], roundResult.[[Nanosecond]], option,
  // offsetNanoseconds, timeZone, "compatible", "prefer", match exactly).
  DirectHandle<BigInt> epoch_nanoseconds;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, epoch_nanoseconds,
      InterpretISODateTimeOffset(
          isolate, round_result, OffsetBehaviour::kOption, offset_nanoseconds,
          time_zone, Disambiguation::kCompatible, Offset::kPrefer,
          MatchBehaviour::kMatchExactly, method_name));

  // 23. Return ! CreateTemporalZonedDateTime(epochNanoseconds, timeZone,
  // calendar).
  return CreateTemporalZonedDateTime(isolate, epoch_nanoseconds, time_zone,
                                     calendar)
      .ToHandleChecked();
}

namespace {

// #sec-temporal-adddurationtoOrsubtractdurationfromzoneddatetime
MaybeDirectHandle<JSTemporalZonedDateTime>
AddDurationToOrSubtractDurationFromZonedDateTime(
    Isolate* isolate, Arithmetic operation,
    DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> temporal_duration_like,
    DirectHandle<Object> options_obj, const char* method_name) {
  TEMPORAL_ENTER_FUNC();
  // 1. If operation is subtract, let sign be -1. Otherwise, let sign be 1.
  double sign = operation == Arithmetic::kSubtract ? -1.0 : 1.0;
  // 2. Let duration be ? ToTemporalDurationRecord(temporalDurationLike).
  DurationRecord duration;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, duration,
      temporal::ToTemporalDurationRecord(isolate, temporal_duration_like,
                                         method_name),
      DirectHandle<JSTemporalZonedDateTime>());

  TimeDurationRecord& time_duration = duration.time_duration;

  // 3. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));

  // 4. Let timeZone be zonedDateTime.[[TimeZone]].
  DirectHandle<JSReceiver> time_zone(zoned_date_time->time_zone(), isolate);
  // 5. Let calendar be zonedDateTime.[[Calendar]].
  DirectHandle<JSReceiver> calendar(zoned_date_time->calendar(), isolate);
  // 6. Let epochNanoseconds be ?
  // AddZonedDateTime(zonedDateTime.[[Nanoseconds]], timeZone, calendar,
  // sign x duration.[[Years]], sign x duration.[[Months]], sign x
  // duration.[[Weeks]], sign x duration.[[Days]], sign x duration.[[Hours]],
  // sign x duration.[[Minutes]], sign x duration.[[Seconds]], sign x
  // duration.[[Milliseconds]], sign x duration.[[Microseconds]], sign x
  // duration.[[Nanoseconds]], options).
  DirectHandle<BigInt> nanoseconds(zoned_date_time->nanoseconds(), isolate);
  duration.years *= sign;
  duration.months *= sign;
  duration.weeks *= sign;
  time_duration.days *= sign;
  time_duration.hours *= sign;
  time_duration.minutes *= sign;
  time_duration.seconds *= sign;
  time_duration.milliseconds *= sign;
  time_duration.microseconds *= sign;
  time_duration.nanoseconds *= sign;
  DirectHandle<BigInt> epoch_nanoseconds;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, epoch_nanoseconds,
      AddZonedDateTime(isolate, nanoseconds, time_zone, calendar, duration,
                       options, method_name));

  // 7. Return ? CreateTemporalZonedDateTime(epochNanoseconds, timeZone,
  // calendar).
  return CreateTemporalZonedDateTime(isolate, epoch_nanoseconds, time_zone,
                                     calendar);
}

}  // namespace

// #sec-temporal.zoneddatetime.prototype.add
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::Add(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> temporal_duration_like, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  return AddDurationToOrSubtractDurationFromZonedDateTime(
      isolate, Arithmetic::kAdd, zoned_date_time, temporal_duration_like,
      options, "Temporal.ZonedDateTime.prototype.add");
}
// #sec-temporal.zoneddatetime.prototype.subtract
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::Subtract(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> temporal_duration_like, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  return AddDurationToOrSubtractDurationFromZonedDateTime(
      isolate, Arithmetic::kSubtract, zoned_date_time, temporal_duration_like,
      options, "Temporal.ZonedDateTime.prototype.subtract");
}

namespace {

// #sec-temporal-differencetemporalzoneddatetime
MaybeDirectHandle<JSTemporalDuration> DifferenceTemporalZonedDateTime(
    Isolate* isolate, TimePreposition operation,
    DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> other_obj, DirectHandle<Object> options,
    const char* method_name) {
  TEMPORAL_ENTER_FUNC();
  // 1. If operation is since, let sign be -1. Otherwise, let sign be 1.
  double sign = operation == TimePreposition::kSince ? -1 : 1;
  // 2. Set other to ? ToTemporalZonedDateTime(other).
  DirectHandle<JSTemporalZonedDateTime> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other, ToTemporalZonedDateTime(isolate, other_obj, method_name));
  // 3. If ? CalendarEquals(zonedDateTime.[[Calendar]], other.[[Calendar]]) is
  // false, then
  bool calendar_equals;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, calendar_equals,
      CalendarEqualsBool(isolate,
                         direct_handle(zoned_date_time->calendar(), isolate),
                         direct_handle(other->calendar(), isolate)),
      DirectHandle<JSTemporalDuration>());
  if (!calendar_equals) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }
  // 4. Let settings be ? GetDifferenceSettings(operation, options, datetime, «
  // », "nanosecond", "hour").
  DifferenceSettings settings;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, settings,
      GetDifferenceSettings(isolate, operation, options, UnitGroup::kDateTime,
                            DisallowedUnitsInDifferenceSettings::kNone,
                            Unit::kNanosecond, Unit::kHour, method_name),
      DirectHandle<JSTemporalDuration>());

  // 5. If settings.[[LargestUnit]] is not one of "year", "month", "week", or
  // "day", then
  if (settings.largest_unit != Unit::kYear &&
      settings.largest_unit != Unit::kMonth &&
      settings.largest_unit != Unit::kWeek &&
      settings.largest_unit != Unit::kDay) {
    // 1. Let result be ! DifferenceInstant(zonedDateTime.[[Nanoseconds]],
    // other.[[Nanoseconds]], settings.[[RoundingIncrement]],
    // settings.[[SmallestUnit]], settings.[[LargestUnit]],
    // settings.[[RoundingMode]]).
    TimeDurationRecord balance_result = DifferenceInstant(
        isolate, direct_handle(zoned_date_time->nanoseconds(), isolate),
        direct_handle(other->nanoseconds(), isolate),
        settings.rounding_increment, settings.smallest_unit,
        settings.largest_unit, settings.rounding_mode, method_name);
    // d. Return ! CreateTemporalDuration(0, 0, 0, 0, sign ×
    // balanceResult.[[Hours]], sign × balanceResult.[[Minutes]], sign ×
    // balanceResult.[[Seconds]], sign × balanceResult.[[Milliseconds]], sign ×
    // balanceResult.[[Microseconds]], sign × balanceResult.[[Nanoseconds]]).
    return CreateTemporalDuration(
               isolate,
               {0,
                0,
                0,
                {0, sign * balance_result.hours, sign * balance_result.minutes,
                 sign * balance_result.seconds,
                 sign * balance_result.milliseconds,
                 sign * balance_result.microseconds,
                 sign * balance_result.nanoseconds}})
        .ToHandleChecked();
  }
  // 6. If ? TimeZoneEquals(zonedDateTime.[[TimeZone]], other.[[TimeZone]]) is
  // false, then
  bool equals;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, equals,
      TimeZoneEquals(isolate,
                     direct_handle(zoned_date_time->time_zone(), isolate),
                     direct_handle(other->time_zone(), isolate)),
      DirectHandle<JSTemporalDuration>());
  if (!equals) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }
  // 7. Let untilOptions be ? MergeLargestUnitOption(settings.[[Options]],
  // settings.[[LargestUnit]]).
  DirectHandle<JSReceiver> until_options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, until_options,
      MergeLargestUnitOption(isolate, settings.options, settings.largest_unit));
  // 8. Let difference be ?
  // DifferenceZonedDateTime(zonedDateTime.[[Nanoseconds]],
  // other.[[Nanoseconds]], zonedDateTime.[[TimeZone]],
  // zonedDateTime.[[Calendar]], settings.[[LargestUnit]], untilOptions).
  DurationRecord difference;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, difference,
      DifferenceZonedDateTime(
          isolate, direct_handle(zoned_date_time->nanoseconds(), isolate),
          direct_handle(other->nanoseconds(), isolate),
          direct_handle(zoned_date_time->time_zone(), isolate),
          direct_handle(zoned_date_time->calendar(), isolate),
          settings.largest_unit, until_options, method_name),
      DirectHandle<JSTemporalDuration>());

  // 9. Let roundResult be (? RoundDuration(difference.[[Years]],
  // difference.[[Months]], difference.[[Weeks]], difference.[[Days]],
  // difference.[[Hours]], difference.[[Minutes]], difference.[[Seconds]],
  // difference.[[Milliseconds]], difference.[[Microseconds]],
  // difference.[[Nanoseconds]], settings.[[RoundingIncrement]],
  // settings.[[SmallestUnit]], settings.[[RoundingMode]],
  // zonedDateTime)).[[DurationRecord]].
  DurationRecordWithRemainder round_result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, round_result,
      RoundDuration(isolate, difference, settings.rounding_increment,
                    settings.smallest_unit, settings.rounding_mode,
                    zoned_date_time, method_name),
      DirectHandle<JSTemporalDuration>());
  // 10. Let result be ? AdjustRoundedDurationDays(roundResult.[[Years]],
  // roundResult.[[Months]], roundResult.[[Weeks]], roundResult.[[Days]],
  // roundResult.[[Hours]], roundResult.[[Minutes]], roundResult.[[Seconds]],
  // roundResult.[[Milliseconds]], roundResult.[[Microseconds]],
  // roundResult.[[Nanoseconds]], settings.[[RoundingIncrement]],
  // settings.[[SmallestUnit]], settings.[[RoundingMode]], zonedDateTime).
  DurationRecord result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result,
      AdjustRoundedDurationDays(isolate, round_result.record,
                                settings.rounding_increment,
                                settings.smallest_unit, settings.rounding_mode,
                                zoned_date_time, method_name),
      DirectHandle<JSTemporalDuration>());

  // 11. Return ! CreateTemporalDuration(sign × result.[[Years]], sign ×
  // result.[[Months]], sign × result.[[Weeks]], sign × result.[[Days]], sign ×
  // result.[[Hours]], sign × result.[[Minutes]], sign × result.[[Seconds]],
  // sign × result.[[Milliseconds]], sign × result.[[Microseconds]], sign ×
  // result.[[Nanoseconds]]).
  return CreateTemporalDuration(isolate,
                                {sign * result.years,
                                 sign * result.months,
                                 sign * result.weeks,
                                 {sign * result.time_duration.days,
                                  sign * result.time_duration.hours,
                                  sign * result.time_duration.minutes,
                                  sign * result.time_duration.seconds,
                                  sign * result.time_duration.milliseconds,
                                  sign * result.time_duration.microseconds,
                                  sign * result.time_duration.nanoseconds}})
      .ToHandleChecked();
}

}  // namespace

// #sec-temporal.zoneddatetime.prototype.until
MaybeDirectHandle<JSTemporalDuration> JSTemporalZonedDateTime::Until(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  return DifferenceTemporalZonedDateTime(
      isolate, TimePreposition::kUntil, handle, other, options,
      "Temporal.ZonedDateTime.prototype.until");
}

// #sec-temporal.zoneddatetime.prototype.since
MaybeDirectHandle<JSTemporalDuration> JSTemporalZonedDateTime::Since(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  return DifferenceTemporalZonedDateTime(
      isolate, TimePreposition::kSince, handle, other, options,
      "Temporal.ZonedDateTime.prototype.since");
}

// #sec-temporal.zoneddatetime.prototype.getisofields
MaybeDirectHandle<JSReceiver> JSTemporalZonedDateTime::GetISOFields(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC();
  const char* method_name = "Temporal.ZonedDateTime.prototype.getISOFields";
  Factory* factory = isolate->factory();
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Let fields be ! OrdinaryObjectCreate(%Object.prototype%).
  DirectHandle<JSObject> fields =
      isolate->factory()->NewJSObject(isolate->object_function());
  // 4. Let timeZone be zonedDateTime.[[TimeZone]].
  DirectHandle<JSReceiver> time_zone(zoned_date_time->time_zone(), isolate);
  // 5. Let instant be ? CreateTemporalInstant(zonedDateTime.[[Nanoseconds]]).
  DirectHandle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant,
      temporal::CreateTemporalInstant(
          isolate, direct_handle(zoned_date_time->nanoseconds(), isolate)));

  // 6. Let calendar be zonedDateTime.[[Calendar]].
  DirectHandle<JSReceiver> calendar(zoned_date_time->calendar(), isolate);
  // 7. Let dateTime be ? BuiltinTimeZoneGetPlainDateTimeFor(timeZone,
  // instant, calendar).
  DirectHandle<JSTemporalPlainDateTime> date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, date_time,
      temporal::BuiltinTimeZoneGetPlainDateTimeFor(isolate, time_zone, instant,
                                                   calendar, method_name));
  // 8. Let offset be ? BuiltinTimeZoneGetOffsetStringFor(timeZone, instant).
  DirectHandle<String> offset;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, offset,
                             BuiltinTimeZoneGetOffsetStringFor(
                                 isolate, time_zone, instant, method_name));

#define DEFINE_STRING_FIELD(obj, str, field)                                  \
  CHECK(JSReceiver::CreateDataProperty(isolate, obj, factory->str##_string(), \
                                       field, Just(kThrowOnError))            \
            .FromJust());

  // 9. Perform ! CreateDataPropertyOrThrow(fields, "calendar", calendar).
  // 10. Perform ! CreateDataPropertyOrThrow(fields, "isoDay",
  // 𝔽(dateTime.[[ISODay]])).
  // 11. Perform ! CreateDataPropertyOrThrow(fields, "isoHour",
  // 𝔽(temporalTime.[[ISOHour]])).
  // 12. Perform ! CreateDataPropertyOrThrow(fields, "isoMicrosecond",
  // 𝔽(temporalTime.[[ISOMicrosecond]])).
  // 13. Perform ! CreateDataPropertyOrThrow(fields, "isoMillisecond",
  // 𝔽(temporalTime.[[ISOMillisecond]])).
  // 14. Perform ! CreateDataPropertyOrThrow(fields, "isoMinute",
  // 𝔽(temporalTime.[[ISOMinute]])).
  // 15. Perform ! CreateDataPropertyOrThrow(fields, "isoMonth",
  // 𝔽(temporalTime.[[ISOMonth]])).
  // 16. Perform ! CreateDataPropertyOrThrow(fields, "isoNanosecond",
  // 𝔽(temporalTime.[[ISONanosecond]])).
  // 17. Perform ! CreateDataPropertyOrThrow(fields, "isoSecond",
  // 𝔽(temporalTime.[[ISOSecond]])).
  // 18. Perform ! CreateDataPropertyOrThrow(fields, "isoYear",
  // 𝔽(temporalTime.[[ISOYear]])).
  // 19. Perform ! CreateDataPropertyOrThrow(fields, "offset", offset).
  // 20. Perform ! CreateDataPropertyOrThrow(fields, "timeZone", timeZone).
  DEFINE_STRING_FIELD(fields, calendar, calendar)
  DEFINE_INT_FIELD(fields, isoDay, iso_day, date_time)
  DEFINE_INT_FIELD(fields, isoHour, iso_hour, date_time)
  DEFINE_INT_FIELD(fields, isoMicrosecond, iso_microsecond, date_time)
  DEFINE_INT_FIELD(fields, isoMillisecond, iso_millisecond, date_time)
  DEFINE_INT_FIELD(fields, isoMinute, iso_minute, date_time)
  DEFINE_INT_FIELD(fields, isoMonth, iso_month, date_time)
  DEFINE_INT_FIELD(fields, isoNanosecond, iso_nanosecond, date_time)
  DEFINE_INT_FIELD(fields, isoSecond, iso_second, date_time)
  DEFINE_INT_FIELD(fields, isoYear, iso_year, date_time)
  DEFINE_STRING_FIELD(fields, offset, offset)
  DEFINE_STRING_FIELD(fields, timeZone, time_zone)
  // 21. Return fields.
  return fields;
}

// #sec-temporal.now.instant
MaybeDirectHandle<JSTemporalInstant> JSTemporalInstant::Now(Isolate* isolate) {
  TEMPORAL_ENTER_FUNC();
  return SystemInstant(isolate);
}

// #sec-get-temporal.zoneddatetime.prototype.offsetnanoseconds
MaybeDirectHandle<Object> JSTemporalZonedDateTime::OffsetNanoseconds(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC();
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Let timeZone be zonedDateTime.[[TimeZone]].
  DirectHandle<JSReceiver> time_zone(zoned_date_time->time_zone(), isolate);
  // 4. Let instant be ! CreateTemporalInstant(zonedDateTime.[[Nanoseconds]]).
  DirectHandle<JSTemporalInstant> instant =
      temporal::CreateTemporalInstant(
          isolate, direct_handle(zoned_date_time->nanoseconds(), isolate))
          .ToHandleChecked();
  // 5. Return 𝔽(? GetOffsetNanosecondsFor(timeZone, instant)).
  int64_t result;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result,
      GetOffsetNanosecondsFor(
          isolate, time_zone, instant,
          "Temporal.ZonedDateTime.prototype.offsetNanoseconds"),
      DirectHandle<Object>());
  return isolate->factory()->NewNumberFromInt64(result);
}

// #sec-get-temporal.zoneddatetime.prototype.offset
MaybeDirectHandle<String> JSTemporalZonedDateTime::Offset(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC();
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Let instant be ! CreateTemporalInstant(zonedDateTime.[[Nanoseconds]]).
  DirectHandle<JSTemporalInstant> instant =
      temporal::CreateTemporalInstant(
          isolate, direct_handle(zoned_date_time->nanoseconds(), isolate))
          .ToHandleChecked();
  // 4. Return ? BuiltinTimeZoneGetOffsetStringFor(zonedDateTime.[[TimeZone]],
  // instant).
  return BuiltinTimeZoneGetOffsetStringFor(
      isolate, direct_handle(zoned_date_time->time_zone(), isolate), instant,
      "Temporal.ZonedDateTime.prototype.offset");
}

// #sec-temporal.zoneddatetime.prototype.startofday
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::StartOfDay(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC();
  const char* method_name = "Temporal.ZonedDateTime.prototype.startOfDay";
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Let timeZone be zonedDateTime.[[TimeZone]].
  DirectHandle<JSReceiver> time_zone(zoned_date_time->time_zone(), isolate);
  // 4. Let calendar be zonedDateTime.[[Calendar]].
  DirectHandle<JSReceiver> calendar(zoned_date_time->calendar(), isolate);
  // 5. Let instant be ! CreateTemporalInstant(zonedDateTime.[[Nanoseconds]]).
  DirectHandle<JSTemporalInstant> instant =
      temporal::CreateTemporalInstant(
          isolate, direct_handle(zoned_date_time->nanoseconds(), isolate))
          .ToHandleChecked();
  // 6. Let temporalDateTime be ?
  // BuiltinTimeZoneGetPlainDateTimeFor(timeZone, instant, calendar).
  DirectHandle<JSTemporalPlainDateTime> temporal_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temporal_date_time,
      temporal::BuiltinTimeZoneGetPlainDateTimeFor(isolate, time_zone, instant,
                                                   calendar, method_name));
  // 7. Let startDateTime be ?
  // CreateTemporalDateTime(temporalDateTime.[[ISOYear]],
  // temporalDateTime.[[ISOMonth]], temporalDateTime.[[ISODay]], 0, 0, 0, 0, 0,
  // 0, calendar).
  DirectHandle<JSTemporalPlainDateTime> start_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, start_date_time,
      temporal::CreateTemporalDateTime(
          isolate,
          {{temporal_date_time->iso_year(), temporal_date_time->iso_month(),
            temporal_date_time->iso_day()},
           {0, 0, 0, 0, 0, 0}},
          calendar));
  // 8. Let startInstant be ? BuiltinTimeZoneGetInstantFor(timeZone,
  // startDateTime, "compatible").
  DirectHandle<JSTemporalInstant> start_instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, start_instant,
      BuiltinTimeZoneGetInstantFor(isolate, time_zone, start_date_time,
                                   Disambiguation::kCompatible, method_name));
  // 9. Return ? CreateTemporalZonedDateTime(startInstant.[[Nanoseconds]],
  // timeZone, calendar).
  return CreateTemporalZonedDateTime(
      isolate, direct_handle(start_instant->nanoseconds(), isolate), time_zone,
      calendar);
}

// #sec-temporal.zoneddatetime.prototype.toinstant
MaybeDirectHandle<JSTemporalInstant> JSTemporalZonedDateTime::ToInstant(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC();
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Return ! CreateTemporalInstant(zonedDateTime.[[Nanoseconds]]).
  return temporal::CreateTemporalInstant(
             isolate, direct_handle(zoned_date_time->nanoseconds(), isolate))
      .ToHandleChecked();
}

namespace {

// Function implement shared steps of toplaindate, toplaintime, toplaindatetime
MaybeDirectHandle<JSTemporalPlainDateTime> ZonedDateTimeToPlainDateTime(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    const char* method_name) {
  TEMPORAL_ENTER_FUNC();
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Let timeZone be zonedDateTime.[[TimeZone]].
  DirectHandle<JSReceiver> time_zone(zoned_date_time->time_zone(), isolate);
  // 4. Let instant be ! CreateTemporalInstant(zonedDateTime.[[Nanoseconds]]).
  DirectHandle<JSTemporalInstant> instant =
      temporal::CreateTemporalInstant(
          isolate, direct_handle(zoned_date_time->nanoseconds(), isolate))
          .ToHandleChecked();
  // 5. 5. Return ? BuiltinTimeZoneGetPlainDateTimeFor(timeZone, instant,
  // zonedDateTime.[[Calendar]]).
  return temporal::BuiltinTimeZoneGetPlainDateTimeFor(
      isolate, time_zone, instant,
      direct_handle(zoned_date_time->calendar(), isolate), method_name);
}

}  // namespace

// #sec-temporal.zoneddatetime.prototype.toplaindate
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalZonedDateTime::ToPlainDate(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  // Step 1-6 are the same as toplaindatetime
  DirectHandle<JSTemporalPlainDateTime> temporal_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temporal_date_time,
      ZonedDateTimeToPlainDateTime(
          isolate, zoned_date_time,
          "Temporal.ZonedDateTime.prototype.toPlainDate"));
  // 7. Return ? CreateTemporalDate(temporalDateTime.[[ISOYear]],
  // temporalDateTime.[[ISOMonth]], temporalDateTime.[[ISODay]], calendar).
  return CreateTemporalDate(
      isolate,
      {temporal_date_time->iso_year(), temporal_date_time->iso_month(),
       temporal_date_time->iso_day()},
      direct_handle(zoned_date_time->calendar(), isolate));
}

// #sec-temporal.zoneddatetime.prototype.toplaintime
MaybeDirectHandle<JSTemporalPlainTime> JSTemporalZonedDateTime::ToPlainTime(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  // Step 1-6 are the same as toplaindatetime
  DirectHandle<JSTemporalPlainDateTime> temporal_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temporal_date_time,
      ZonedDateTimeToPlainDateTime(
          isolate, zoned_date_time,
          "Temporal.ZonedDateTime.prototype.toPlainTime"));
  // 7. Return ?  CreateTemporalTime(temporalDateTime.[[ISOHour]],
  // temporalDateTime.[[ISOMinute]], temporalDateTime.[[ISOSecond]],
  // temporalDateTime.[[ISOMillisecond]], temporalDateTime.[[ISOMicrosecond]],
  // temporalDateTime.[[ISONanosecond]]).
  return CreateTemporalTime(
      isolate,
      {temporal_date_time->iso_hour(), temporal_date_time->iso_minute(),
       temporal_date_time->iso_second(), temporal_date_time->iso_millisecond(),
       temporal_date_time->iso_microsecond(),
       temporal_date_time->iso_nanosecond()});
}

// #sec-temporal.zoneddatetime.prototype.toplaindatetime
MaybeDirectHandle<JSTemporalPlainDateTime>
JSTemporalZonedDateTime::ToPlainDateTime(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  return ZonedDateTimeToPlainDateTime(
      isolate, zoned_date_time,
      "Temporal.ZonedDateTime.prototype.toPlainDateTime");
}

// #sec-temporal.instant
MaybeDirectHandle<JSTemporalInstant> JSTemporalInstant::Constructor(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target,
    DirectHandle<Object> epoch_nanoseconds_obj) {
  TEMPORAL_ENTER_FUNC();
  // 1. If NewTarget is undefined, then
  if (IsUndefined(*new_target)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                                 isolate->factory()->NewStringFromAsciiChecked(
                                     "Temporal.Instant")));
  }
  // 2. Let epochNanoseconds be ? ToBigInt(epochNanoseconds).
  DirectHandle<BigInt> epoch_nanoseconds;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, epoch_nanoseconds,
      BigInt::FromObject(isolate, epoch_nanoseconds_obj));
  // 3. If ! IsValidEpochNanoseconds(epochNanoseconds) is false, throw a
  // RangeError exception.
  if (!IsValidEpochNanoseconds(isolate, epoch_nanoseconds)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }
  // 4. Return ? CreateTemporalInstant(epochNanoseconds, NewTarget).
  return temporal::CreateTemporalInstant(isolate, target, new_target,
                                         epoch_nanoseconds);
}

namespace {

// The logic in Temporal.Instant.fromEpochSeconds and fromEpochMilliseconds,
// are the same except a scaling factor, code all of them into the follow
// function.
MaybeDirectHandle<JSTemporalInstant> ScaleNumberToNanosecondsVerifyAndMake(
    Isolate* isolate, DirectHandle<BigInt> bigint, uint32_t scale) {
  TEMPORAL_ENTER_FUNC();
  DCHECK(scale == 1 || scale == 1000 || scale == 1000000 ||
         scale == 1000000000);
  // 2. Let epochNanoseconds be epochXseconds × scaleℤ.
  DirectHandle<BigInt> epoch_nanoseconds;
  if (scale == 1) {
    epoch_nanoseconds = bigint;
  } else {
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, epoch_nanoseconds,
        BigInt::Multiply(isolate, BigInt::FromUint64(isolate, scale), bigint));
  }
  // 3. If ! IsValidEpochNanoseconds(epochNanoseconds) is false, throw a
  // RangeError exception.
  if (!IsValidEpochNanoseconds(isolate, epoch_nanoseconds)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }
  return temporal::CreateTemporalInstant(isolate, epoch_nanoseconds);
}

MaybeDirectHandle<JSTemporalInstant> ScaleNumberToNanosecondsVerifyAndMake(
    Isolate* isolate, DirectHandle<Object> epoch_Xseconds, uint32_t scale) {
  TEMPORAL_ENTER_FUNC();
  // 1. Set epochXseconds to ? ToNumber(epochXseconds).
  ASSIGN_RETURN_ON_EXCEPTION(isolate, epoch_Xseconds,
                             Object::ToNumber(isolate, epoch_Xseconds));
  // 2. Set epochMilliseconds to ? NumberToBigInt(epochMilliseconds).
  DirectHandle<BigInt> bigint;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, bigint,
                             BigInt::FromNumber(isolate, epoch_Xseconds));
  return ScaleNumberToNanosecondsVerifyAndMake(isolate, bigint, scale);
}

MaybeDirectHandle<JSTemporalInstant> ScaleToNanosecondsVerifyAndMake(
    Isolate* isolate, DirectHandle<Object> epoch_Xseconds, uint32_t scale) {
  TEMPORAL_ENTER_FUNC();
  // 1. Set epochMicroseconds to ? ToBigInt(epochMicroseconds).
  DirectHandle<BigInt> bigint;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, bigint,
                             BigInt::FromObject(isolate, epoch_Xseconds));
  return ScaleNumberToNanosecondsVerifyAndMake(isolate, bigint, scale);
}

}  // namespace

// #sec-temporal.instant.fromepochseconds
MaybeDirectHandle<JSTemporalInstant> JSTemporalInstant::FromEpochSeconds(
    Isolate* isolate, DirectHandle<Object> epoch_seconds) {
  TEMPORAL_ENTER_FUNC();
  return ScaleNumberToNanosecondsVerifyAndMake(isolate, epoch_seconds,
                                               1000000000);
}

// #sec-temporal.instant.fromepochmilliseconds
MaybeDirectHandle<JSTemporalInstant> JSTemporalInstant::FromEpochMilliseconds(
    Isolate* isolate, DirectHandle<Object> epoch_milliseconds) {
  TEMPORAL_ENTER_FUNC();
  return ScaleNumberToNanosecondsVerifyAndMake(isolate, epoch_milliseconds,
                                               1000000);
}

// #sec-temporal.instant.fromepochmicroseconds
MaybeDirectHandle<JSTemporalInstant> JSTemporalInstant::FromEpochMicroseconds(
    Isolate* isolate, DirectHandle<Object> epoch_microseconds) {
  TEMPORAL_ENTER_FUNC();
  return ScaleToNanosecondsVerifyAndMake(isolate, epoch_microseconds, 1000);
}

// #sec-temporal.instant.fromepochnanoeconds
MaybeDirectHandle<JSTemporalInstant> JSTemporalInstant::FromEpochNanoseconds(
    Isolate* isolate, DirectHandle<Object> epoch_nanoseconds) {
  TEMPORAL_ENTER_FUNC();
  return ScaleToNanosecondsVerifyAndMake(isolate, epoch_nanoseconds, 1);
}

// #sec-temporal.instant.compare
MaybeDirectHandle<Smi> JSTemporalInstant::Compare(
    Isolate* isolate, DirectHandle<Object> one_obj,
    DirectHandle<Object> two_obj) {
  TEMPORAL_ENTER_FUNC();
  const char* method_name = "Temporal.Instant.compare";
  // 1. Set one to ? ToTemporalInstant(one).
  DirectHandle<JSTemporalInstant> one;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, one,
                             ToTemporalInstant(isolate, one_obj, method_name));
  // 2. Set two to ? ToTemporalInstant(two).
  DirectHandle<JSTemporalInstant> two;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, two,
                             ToTemporalInstant(isolate, two_obj, method_name));
  // 3. Return 𝔽(! CompareEpochNanoseconds(one.[[Nanoseconds]],
  // two.[[Nanoseconds]])).
  return CompareEpochNanoseconds(isolate,
                                 direct_handle(one->nanoseconds(), isolate),
                                 direct_handle(two->nanoseconds(), isolate));
}

// #sec-temporal.instant.prototype.equals
MaybeDirectHandle<Oddball> JSTemporalInstant::Equals(
    Isolate* isolate, DirectHandle<JSTemporalInstant> handle,
    DirectHandle<Object> other_obj) {
  TEMPORAL_ENTER_FUNC();
  // 1. Let instant be the this value.
  // 2. Perform ? RequireInternalSlot(instant, [[InitializedTemporalInstant]]).
  // 3. Set other to ? ToTemporalInstant(other).
  DirectHandle<JSTemporalInstant> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other,
      ToTemporalInstant(isolate, other_obj,
                        "Temporal.Instant.prototype.equals"));
  // 4. If instant.[[Nanoseconds]] ≠ other.[[Nanoseconds]], return false.
  // 5. Return true.
  return isolate->factory()->ToBoolean(
      BigInt::EqualToBigInt(handle->nanoseconds(), other->nanoseconds()));
}

namespace {

// #sec-temporal-totemporalroundingincrement
Maybe<double> ToTemporalRoundingIncrement(
    Isolate* isolate, DirectHandle<JSReceiver> normalized_options,
    double dividend, bool dividend_is_defined, bool inclusive) {
  double maximum;
  // 1. If dividend is undefined, then
  if (!dividend_is_defined) {
    // a. Let maximum be +∞.
    maximum = std::numeric_limits<double>::infinity();
    // 2. Else if inclusive is true, then
  } else if (inclusive) {
    // a. Let maximum be 𝔽(dividend).
    maximum = dividend;
    // 3. Else if dividend is more than 1, then
  } else if (dividend > 1) {
    // a. Let maximum be 𝔽(dividend-1).
    maximum = dividend - 1;
    // 4. Else,
  } else {
    // a. Let maximum be 1.
    maximum = 1;
  }
  // 5. Let increment be ? GetOption(normalizedOptions, "roundingIncrement", «
  // Number », empty, 1).
  double increment;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, increment,
      GetNumberOptionAsDouble(isolate, normalized_options,
                              isolate->factory()->roundingIncrement_string(),
                              1),
      Nothing<double>());

  // 6. If increment < 1 or increment > maximum, throw a RangeError exception.
  if (increment < 1 || increment > maximum) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(), Nothing<double>());
  }
  // 7. Set increment to floor(ℝ(increment)).
  increment = std::floor(increment);

  // 8. If dividend is not undefined and dividend modulo increment is not zero,
  // then
  if ((dividend_is_defined) && (std::fmod(dividend, increment) != 0)) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(), Nothing<double>());
  }
  // 9. Return increment.
  return Just(increment);
}

// #sec-temporal-roundtemporalinstant
DirectHandle<BigInt> RoundTemporalInstant(Isolate* isolate,
                                          DirectHandle<BigInt> ns,
                                          double increment, Unit unit,
                                          RoundingMode rounding_mode) {
  TEMPORAL_ENTER_FUNC();
  // 1. Assert: Type(ns) is BigInt.
  double increment_ns;
  switch (unit) {
    // 2. If unit is "hour", then
    case Unit::kHour:
      // a. Let incrementNs be increment × 3.6 × 10^12.
      increment_ns = increment * 3.6e12;
      break;
    // 3. Else if unit is "minute", then
    case Unit::kMinute:
      // a. Let incrementNs be increment × 6 × 10^10.
      increment_ns = increment * 6e10;
      break;
    // 4. Else if unit is "second", then
    case Unit::kSecond:
      // a. Let incrementNs be increment × 10^9.
      increment_ns = increment * 1e9;
      break;
    // 5. Else if unit is "millisecond", then
    case Unit::kMillisecond:
      // a. Let incrementNs be increment × 10^6.
      increment_ns = increment * 1e6;
      break;
    // 6. Else if unit is "microsecond", then
    case Unit::kMicrosecond:
      // a. Let incrementNs be increment × 10^3.
      increment_ns = increment * 1e3;
      break;
    // 7. Else,
    // a. Assert: unit is "nanosecond".
    case Unit::kNanosecond:
      // b. Let incrementNs be increment.
      increment_ns = increment;
      break;
    default:
      UNREACHABLE();
  }
  // 8. Return ! RoundNumberToIncrementAsIfPositive(ℝ(ns), incrementNs,
  // roundingMode).
  return RoundNumberToIncrementAsIfPositive(isolate, ns, increment_ns,
                                            rounding_mode);
}

}  // namespace

// #sec-temporal.instant.prototype.round
MaybeDirectHandle<JSTemporalInstant> JSTemporalInstant::Round(
    Isolate* isolate, DirectHandle<JSTemporalInstant> handle,
    DirectHandle<Object> round_to_obj) {
  TEMPORAL_ENTER_FUNC();
  const char* method_name = "Temporal.Instant.prototype.round";
  Factory* factory = isolate->factory();
  // 1. Let instant be the this value.
  // 2. Perform ? RequireInternalSlot(instant, [[InitializedTemporalInstant]]).
  // 3. If roundTo is undefined, then
  if (IsUndefined(*round_to_obj)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }
  DirectHandle<JSReceiver> round_to;
  // 4. If Type(roundTo) is String, then
  if (IsString(*round_to_obj)) {
    // a. Let paramString be roundTo.
    DirectHandle<String> param_string = Cast<String>(round_to_obj);
    // b. Set roundTo to ! OrdinaryObjectCreate(null).
    round_to = factory->NewJSObjectWithNullProto();
    // c. Perform ! CreateDataPropertyOrThrow(roundTo, "smallestUnit",
    // paramString).
    CHECK(JSReceiver::CreateDataProperty(isolate, round_to,
                                         factory->smallestUnit_string(),
                                         param_string, Just(kThrowOnError))
              .FromJust());
  } else {
    // a. Set roundTo to ? GetOptionsObject(roundTo).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, round_to,
        GetOptionsObject(isolate, round_to_obj, method_name));
  }

  // 6. Let smallestUnit be ? GetTemporalUnit(roundTo, "smallestUnit", time,
  // required).
  Unit smallest_unit;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, smallest_unit,
      GetTemporalUnit(isolate, round_to, "smallestUnit", UnitGroup::kTime,
                      Unit::kNotPresent, true, method_name),
      DirectHandle<JSTemporalInstant>());

  // 7. Let roundingMode be ? ToTemporalRoundingMode(roundTo, "halfExpand").
  RoundingMode rounding_mode;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, rounding_mode,
      ToTemporalRoundingMode(isolate, round_to, RoundingMode::kHalfExpand,
                             method_name),
      DirectHandle<JSTemporalInstant>());
  double maximum;
  switch (smallest_unit) {
    // 8. If smallestUnit is "hour", then
    case Unit::kHour:
      // a. Let maximum be 24.
      maximum = 24;
      break;
    // 9. Else if smallestUnit is "minute", then
    case Unit::kMinute:
      // a. Let maximum be 1440.
      maximum = 1440;
      break;
    // 10. Else if smallestUnit is "second", then
    case Unit::kSecond:
      // a. Let maximum be 86400.
      maximum = 86400;
      break;
    // 11. Else if smallestUnit is "millisecond", then
    case Unit::kMillisecond:
      // a. Let maximum be 8.64 × 10^7.
      maximum = 8.64e7;
      break;
    // 12. Else if smallestUnit is "microsecond", then
    case Unit::kMicrosecond:
      // a. Let maximum be 8.64 × 10^10.
      maximum = 8.64e10;
      break;
    // 13. Else,
    case Unit::kNanosecond:
      // b. Let maximum be nsPerDay.
      maximum = kNsPerDay;
      break;
      // a. Assert: smallestUnit is "nanosecond".
    default:
      UNREACHABLE();
  }
  // 14. Let roundingIncrement be ? ToTemporalRoundingIncrement(roundTo,
  // maximum, true).
  double rounding_increment;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, rounding_increment,
      ToTemporalRoundingIncrement(isolate, round_to, maximum, true, true),
      DirectHandle<JSTemporalInstant>());
  // 15. Let roundedNs be ! RoundTemporalInstant(instant.[[Nanoseconds]],
  // roundingIncrement, smallestUnit, roundingMode).
  DirectHandle<BigInt> rounded_ns = RoundTemporalInstant(
      isolate, DirectHandle<BigInt>(handle->nanoseconds(), isolate),
      rounding_increment, smallest_unit, rounding_mode);
  // 16. Return ! CreateTemporalInstant(roundedNs).
  return temporal::CreateTemporalInstant(isolate, rounded_ns).ToHandleChecked();
}

// #sec-temporal.instant.from
MaybeDirectHandle<JSTemporalInstant> JSTemporalInstant::From(
    Isolate* isolate, DirectHandle<Object> item) {
  TEMPORAL_ENTER_FUNC();
  //  1. If Type(item) is Object and item has an [[InitializedTemporalInstant]]
  //  internal slot, then
  if (IsJSTemporalInstant(*item)) {
    // a. Return ? CreateTemporalInstant(item.[[Nanoseconds]]).
    return temporal::CreateTemporalInstant(
        isolate,
        direct_handle(Cast<JSTemporalInstant>(*item)->nanoseconds(), isolate));
  }
  // 2. Return ? ToTemporalInstant(item).
  return ToTemporalInstant(isolate, item, "Temporal.Instant.from");
}

// #sec-temporal.instant.prototype.tozoneddatetime
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalInstant::ToZonedDateTime(
    Isolate* isolate, DirectHandle<JSTemporalInstant> handle,
    DirectHandle<Object> item_obj) {
  TEMPORAL_ENTER_FUNC();
  const char* method_name = "Temporal.Instant.prototype.toZonedDateTime";
  Factory* factory = isolate->factory();
  // 1. Let instant be the this value.
  // 2. Perform ? RequireInternalSlot(instant, [[InitializedTemporalInstant]]).
  // 3. If Type(item) is not Object, then
  if (!IsJSReceiver(*item_obj)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }
  DirectHandle<JSReceiver> item = Cast<JSReceiver>(item_obj);
  // 4. Let calendarLike be ? Get(item, "calendar").
  DirectHandle<Object> calendar_like;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar_like,
      JSReceiver::GetProperty(isolate, item, factory->calendar_string()));
  // 5. If calendarLike is undefined, then
  if (IsUndefined(*calendar_like)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }
  // 6. Let calendar be ? ToTemporalCalendar(calendarLike).
  DirectHandle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar,
      temporal::ToTemporalCalendar(isolate, calendar_like, method_name));

  // 7. Let temporalTimeZoneLike be ? Get(item, "timeZone").
  DirectHandle<Object> temporal_time_zone_like;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temporal_time_zone_like,
      JSReceiver::GetProperty(isolate, item, factory->timeZone_string()));
  // 8. If temporalTimeZoneLike is undefined, then
  if (IsUndefined(*calendar_like)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }
  // 9. Let timeZone be ? ToTemporalTimeZone(temporalTimeZoneLike).
  DirectHandle<JSReceiver> time_zone;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, time_zone,
      temporal::ToTemporalTimeZone(isolate, temporal_time_zone_like,
                                   method_name));
  // 10. Return ? CreateTemporalZonedDateTime(instant.[[Nanoseconds]], timeZone,
  // calendar).
  return CreateTemporalZonedDateTime(
      isolate, DirectHandle<BigInt>(handle->nanoseconds(), isolate), time_zone,
      calendar);
}

namespace {

// #sec-temporal-temporalinstanttostring
MaybeDirectHandle<String> TemporalInstantToString(
    Isolate* isolate, DirectHandle<JSTemporalInstant> instant,
    DirectHandle<Object> time_zone_obj, Precision precision,
    const char* method_name) {
  IncrementalStringBuilder builder(isolate);
  // 1. Assert: Type(instant) is Object.
  // 2. Assert: instant has an [[InitializedTemporalInstant]] internal slot.
  // 3. Let outputTimeZone be timeZone.
  DirectHandle<JSReceiver> output_time_zone;

  // 4. If outputTimeZone is undefined, then
  if (IsUndefined(*time_zone_obj)) {
    // a. Set outputTimeZone to ! CreateTemporalTimeZone("UTC").
    output_time_zone = CreateTemporalTimeZoneUTC(isolate);
  } else {
    DCHECK(IsJSReceiver(*time_zone_obj));
    output_time_zone = Cast<JSReceiver>(time_zone_obj);
  }

  // 5. Let isoCalendar be ! GetISO8601Calendar().
  DirectHandle<JSTemporalCalendar> iso_calendar =
      temporal::GetISO8601Calendar(isolate);
  // 6. Let dateTime be ?
  // BuiltinTimeZoneGetPlainDateTimeFor(outputTimeZone, instant,
  // isoCalendar).
  DirectHandle<JSTemporalPlainDateTime> date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, date_time,
      temporal::BuiltinTimeZoneGetPlainDateTimeFor(
          isolate, output_time_zone, instant, iso_calendar, method_name));
  // 7. Let dateTimeString be ? TemporalDateTimeToString(dateTime.[[ISOYear]],
  // dateTime.[[ISOMonth]], dateTime.[[ISODay]], dateTime.[[ISOHour]],
  // dateTime.[[ISOMinute]], dateTime.[[ISOSecond]],
  // dateTime.[[ISOMillisecond]], dateTime.[[ISOMicrosecond]],
  // dateTime.[[ISONanosecond]], undefined, precision, "never").

  DirectHandle<String> date_time_string;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, date_time_string,
      TemporalDateTimeToString(
          isolate,
          {{date_time->iso_year(), date_time->iso_month(),
            date_time->iso_day()},
           {date_time->iso_hour(), date_time->iso_minute(),
            date_time->iso_second(), date_time->iso_millisecond(),
            date_time->iso_microsecond(), date_time->iso_nanosecond()}},
          iso_calendar,  // Unimportant due to ShowCalendar::kNever
          precision, ShowCalendar::kNever));
  builder.AppendString(date_time_string);

  // 8. If timeZone is undefined, then
  if (IsUndefined(*time_zone_obj)) {
    // a. Let timeZoneString be "Z".
    builder.AppendCharacter('Z');
  } else {
    // 9. Else,
    DCHECK(IsJSReceiver(*time_zone_obj));
    DirectHandle<JSReceiver> time_zone = Cast<JSReceiver>(time_zone_obj);

    // a. Let offsetNs be ? GetOffsetNanosecondsFor(timeZone, instant).
    int64_t offset_ns;
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, offset_ns,
        GetOffsetNanosecondsFor(isolate, time_zone, instant, method_name),
        DirectHandle<String>());
    // b. Let timeZoneString be ! FormatISOTimeZoneOffsetString(offsetNs).
    DirectHandle<String> time_zone_string =
        FormatISOTimeZoneOffsetString(isolate, offset_ns);
    builder.AppendString(time_zone_string);
  }

  // 10. Return the string-concatenation of dateTimeString and timeZoneString.
  return builder.Finish();
}

}  // namespace

// #sec-temporal.instant.prototype.tojson
MaybeDirectHandle<String> JSTemporalInstant::ToJSON(
    Isolate* isolate, DirectHandle<JSTemporalInstant> instant) {
  TEMPORAL_ENTER_FUNC();
  // 1. Let instant be the this value.
  // 2. Perform ? RequireInternalSlot(instant, [[InitializedTemporalInstant]]).
  // 3. Return ? TemporalInstantToString(instant, undefined, "auto").
  return TemporalInstantToString(
      isolate, instant, isolate->factory()->undefined_value(), Precision::kAuto,
      "Temporal.Instant.prototype.toJSON");
}

// #sec-temporal.instant.prototype.tolocalestring
MaybeDirectHandle<String> JSTemporalInstant::ToLocaleString(
    Isolate* isolate, DirectHandle<JSTemporalInstant> instant,
    DirectHandle<Object> locales, DirectHandle<Object> options) {
  const char* method_name = "Temporal.Instant.prototype.toLocaleString";
#ifdef V8_INTL_SUPPORT
  return JSDateTimeFormat::TemporalToLocaleString(isolate, instant, locales,
                                                  options, method_name);
#else   //  V8_INTL_SUPPORT
  return TemporalInstantToString(isolate, instant,
                                 isolate->factory()->undefined_value(),
                                 Precision::kAuto, method_name);
#endif  //  V8_INTL_SUPPORT
}

// #sec-temporal.instant.prototype.tostring
MaybeDirectHandle<String> JSTemporalInstant::ToString(
    Isolate* isolate, DirectHandle<JSTemporalInstant> instant,
    DirectHandle<Object> options_obj) {
  Factory* factory = isolate->factory();
  const char* method_name = "Temporal.Instant.prototype.toString";
  // 1. Let instant be the this value.
  // 2. Perform ? RequireInternalSlot(instant, [[InitializedTemporalInstant]]).
  // 3. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));
  // 4. Let timeZone be ? Get(options, "timeZone").
  DirectHandle<Object> time_zone;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, time_zone,
      JSReceiver::GetProperty(isolate, options, factory->timeZone_string()));

  // 5. If timeZone is not undefined, then
  if (!IsUndefined(*time_zone)) {
    // a. Set timeZone to ? ToTemporalTimeZone(timeZone).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, time_zone,
        temporal::ToTemporalTimeZone(isolate, time_zone, method_name));
  }
  // 6. Let precision be ? ToSecondsStringPrecision(options).
  StringPrecision precision;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, precision,
      ToSecondsStringPrecision(isolate, options, method_name),
      DirectHandle<String>());
  // 7. Let roundingMode be ? ToTemporalRoundingMode(options, "trunc").
  RoundingMode rounding_mode;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, rounding_mode,
      ToTemporalRoundingMode(isolate, options, RoundingMode::kTrunc,
                             method_name),
      DirectHandle<String>());
  // 8. Let roundedNs be ! RoundTemporalInstant(instant.[[Nanoseconds]],
  // precision.[[Increment]], precision.[[Unit]], roundingMode).
  DirectHandle<BigInt> rounded_ns = RoundTemporalInstant(
      isolate, direct_handle(instant->nanoseconds(), isolate),
      precision.increment, precision.unit, rounding_mode);

  // 9. Let roundedInstant be ! CreateTemporalInstant(roundedNs).
  DirectHandle<JSTemporalInstant> rounded_instant =
      temporal::CreateTemporalInstant(isolate, rounded_ns).ToHandleChecked();

  // 10. Return ? TemporalInstantToString(roundedInstant, timeZone,
  // precision.[[Precision]]).
  return TemporalInstantToString(isolate, rounded_instant, time_zone,
                                 precision.precision,
                                 "Temporal.Instant.prototype.toString");
}

// #sec-temporal.instant.prototype.tozoneddatetimeiso
MaybeDirectHandle<JSTemporalZonedDateTime>
JSTemporalInstant::ToZonedDateTimeISO(Isolate* isolate,
                                      DirectHandle<JSTemporalInstant> handle,
                                      DirectHandle<Object> item_obj) {
  TEMPORAL_ENTER_FUNC();
  Factory* factory = isolate->factory();
  // 1. Let instant be the this value.
  // 2. Perform ? RequireInternalSlot(instant, [[InitializedTemporalInstant]]).
  // 3. If Type(item) is Object, then
  if (IsJSReceiver(*item_obj)) {
    DirectHandle<JSReceiver> item = Cast<JSReceiver>(item_obj);
    // a. Let timeZoneProperty be ? Get(item, "timeZone").
    DirectHandle<Object> time_zone_property;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, time_zone_property,
        JSReceiver::GetProperty(isolate, item, factory->timeZone_string()));
    // b. If timeZoneProperty is not undefined, then
    if (!IsUndefined(*time_zone_property)) {
      // i. Set item to timeZoneProperty.
      item_obj = time_zone_property;
    }
  }
  // 4. Let timeZone be ? ToTemporalTimeZone(item).
  DirectHandle<JSReceiver> time_zone;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, time_zone,
      temporal::ToTemporalTimeZone(
          isolate, item_obj, "Temporal.Instant.prototype.toZonedDateTimeISO"));
  // 5. Let calendar be ! GetISO8601Calendar().
  DirectHandle<JSTemporalCalendar> calendar =
      temporal::GetISO8601Calendar(isolate);
  // 6. Return ? CreateTemporalZonedDateTime(instant.[[Nanoseconds]], timeZone,
  // calendar).
  return CreateTemporalZonedDateTime(
      isolate, DirectHandle<BigInt>(handle->nanoseconds(), isolate), time_zone,
      calendar);
}

namespace {

// #sec-temporal-adddurationtoorsubtractdurationfrominstant
MaybeDirectHandle<JSTemporalInstant> AddDurationToOrSubtractDurationFromInstant(
    Isolate* isolate, Arithmetic operation,
    DirectHandle<JSTemporalInstant> handle,
    DirectHandle<Object> temporal_duration_like, const char* method_name) {
  TEMPORAL_ENTER_FUNC();
  // 1. If operation is subtract, let sign be -1. Otherwise, let sign be 1.
  double sign = operation == Arithmetic::kSubtract ? -1.0 : 1.0;

  // See https://github.com/tc39/proposal-temporal/pull/2253
  // 2. Let duration be ? ToTemporalDurationRecord(temporalDurationLike).
  DurationRecord duration;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, duration,
      temporal::ToTemporalDurationRecord(isolate, temporal_duration_like,
                                         method_name),
      DirectHandle<JSTemporalInstant>());

  TimeDurationRecord& time_duration = duration.time_duration;
  if (time_duration.days != 0 || duration.months != 0 || duration.weeks != 0 ||
      duration.years != 0) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 DirectHandle<JSTemporalInstant>());
  }

  // 3. Let ns be ? AddInstant(instant.[[EpochNanoseconds]], sign x
  // duration.[[Hours]], sign x duration.[[Minutes]], sign x
  // duration.[[Seconds]], sign x duration.[[Milliseconds]], sign x
  // duration.[[Microseconds]], sign x duration.[[Nanoseconds]]).
  DirectHandle<BigInt> ns;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, ns,
      AddInstant(
          isolate, DirectHandle<BigInt>(handle->nanoseconds(), isolate),
          {0, sign * time_duration.hours, sign * time_duration.minutes,
           sign * time_duration.seconds, sign * time_duration.milliseconds,
           sign * time_duration.microseconds,
           sign * time_duration.nanoseconds}));
  // 4. Return ! CreateTemporalInstant(ns).
  return temporal::CreateTemporalInstant(isolate, ns);
}

// #sec-temporal-negatetemporalroundingmode
RoundingMode NegateTemporalRoundingMode(RoundingMode rounding_mode) {
  switch (rounding_mode) {
    // 1. If roundingMode is "ceil", return "floor".
    case RoundingMode::kCeil:
      return RoundingMode::kFloor;
    // 2. If roundingMode is "floor", return "ceil".
    case RoundingMode::kFloor:
      return RoundingMode::kCeil;
    // 3. If roundingMode is "halfCeil", return "halfFloor".
    case RoundingMode::kHalfCeil:
      return RoundingMode::kHalfFloor;
    // 4. If roundingMode is "halfFloor", return "halfCeil".
    case RoundingMode::kHalfFloor:
      return RoundingMode::kHalfCeil;
    // 5. Return roundingMode.
    default:
      return rounding_mode;
  }
}

// #sec-temporal-getdifferencesettings
Maybe<DifferenceSettings> GetDifferenceSettings(
    Isolate* isolate, TimePreposition operation, DirectHandle<Object> options,
    UnitGroup unit_group, DisallowedUnitsInDifferenceSettings disallowed_units,
    Unit fallback_smallest_unit, Unit smallest_largest_default_unit,
    const char* method_name) {
  DifferenceSettings record;
  // 1. Set options to ? GetOptionsObject(options).
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, record.options, GetOptionsObject(isolate, options, method_name),
      Nothing<DifferenceSettings>());
  // 2. Let smallestUnit be ? GetTemporalUnit(options, "smallestUnit",
  // unitGroup, fallbackSmallestUnit).
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, record.smallest_unit,
      GetTemporalUnit(isolate, record.options, "smallestUnit", unit_group,
                      fallback_smallest_unit,
                      fallback_smallest_unit == Unit::kNotPresent, method_name),
      Nothing<DifferenceSettings>());
  // 3. If disallowedUnits contains smallestUnit, throw a RangeError exception.
  if (disallowed_units == DisallowedUnitsInDifferenceSettings::kWeekAndDay) {
    if (record.smallest_unit == Unit::kWeek) {
      THROW_NEW_ERROR_RETURN_VALUE(
          isolate,
          NewRangeError(MessageTemplate::kInvalidUnit,
                        isolate->factory()->smallestUnit_string(),
                        isolate->factory()->week_string()),
          Nothing<DifferenceSettings>());
    }
    if (record.smallest_unit == Unit::kDay) {
      THROW_NEW_ERROR_RETURN_VALUE(
          isolate,
          NewRangeError(MessageTemplate::kInvalidUnit,
                        isolate->factory()->smallestUnit_string(),
                        isolate->factory()->day_string()),
          Nothing<DifferenceSettings>());
    }
  }
  // 4. Let defaultLargestUnit be !
  // LargerOfTwoTemporalUnits(smallestLargestDefaultUnit, smallestUnit).
  Unit default_largest_unit = LargerOfTwoTemporalUnits(
      smallest_largest_default_unit, record.smallest_unit);
  // 5. Let largestUnit be ? GetTemporalUnit(options, "largestUnit", unitGroup,
  // "auto").
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, record.largest_unit,
      GetTemporalUnit(isolate, record.options, "largestUnit", unit_group,
                      Unit::kAuto, false, method_name),
      Nothing<DifferenceSettings>());
  // 6. If disallowedUnits contains largestUnit, throw a RangeError exception.
  if (disallowed_units == DisallowedUnitsInDifferenceSettings::kWeekAndDay) {
    if (record.largest_unit == Unit::kWeek) {
      THROW_NEW_ERROR_RETURN_VALUE(
          isolate,
          NewRangeError(MessageTemplate::kInvalidUnit,
                        isolate->factory()->largestUnit_string(),
                        isolate->factory()->week_string()),
          Nothing<DifferenceSettings>());
    }
    if (record.largest_unit == Unit::kDay) {
      THROW_NEW_ERROR_RETURN_VALUE(
          isolate,
          NewRangeError(MessageTemplate::kInvalidUnit,
                        isolate->factory()->largestUnit_string(),
                        isolate->factory()->day_string()),
          Nothing<DifferenceSettings>());
    }
  }
  // 7. If largestUnit is "auto", set largestUnit to defaultLargestUnit.
  if (record.largest_unit == Unit::kAuto) {
    record.largest_unit = default_largest_unit;
  }
  // 8. If LargerOfTwoTemporalUnits(largestUnit, smallestUnit) is not
  // largestUnit, throw a RangeError exception.
  if (LargerOfTwoTemporalUnits(record.largest_unit, record.smallest_unit) !=
      record.largest_unit) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewRangeError(MessageTemplate::kInvalidArgumentForTemporal,
                      isolate->factory()->largestUnit_string()),
        Nothing<DifferenceSettings>());
  }
  // 9. Let roundingMode be ? ToTemporalRoundingMode(options, "trunc").
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, record.rounding_mode,
      ToTemporalRoundingMode(isolate, record.options, RoundingMode::kTrunc,
                             method_name),
      Nothing<DifferenceSettings>());
  // 10. If operation is since, then
  if (operation == TimePreposition::kSince) {
    // a. Set roundingMode to ! NegateTemporalRoundingMode(roundingMode).
    record.rounding_mode = NegateTemporalRoundingMode(record.rounding_mode);
  }
  // 11. Let maximum be !
  // MaximumTemporalDurationRoundingIncrement(smallestUnit).
  Maximum maximum =
      MaximumTemporalDurationRoundingIncrement(record.smallest_unit);
  // 12. Let roundingIncrement be ? ToTemporalRoundingIncrement(options,
  // maximum, false).
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, record.rounding_increment,
      ToTemporalRoundingIncrement(isolate, record.options, maximum.value,
                                  maximum.defined, false),
      Nothing<DifferenceSettings>());
  // 13. Return the Record { [[SmallestUnit]]: smallestUnit, [[LargestUnit]]:
  // largestUnit, [[RoundingMode]]: roundingMode, [[RoundingIncrement]]:
  // roundingIncrement, [[Options]]: options }.
  return Just(record);
}

// #sec-temporal-differenceinstant
TimeDurationRecord DifferenceInstant(Isolate* isolate, DirectHandle<BigInt> ns1,
                                     DirectHandle<BigInt> ns2,
                                     double rounding_increment,
                                     Unit smallest_unit, Unit largest_unit,
                                     RoundingMode rounding_mode,
                                     const char* method_name) {
  // 1. Assert: Type(ns1) is BigInt.
  // 2. Assert: Type(ns2) is BigInt.
  // 3. Assert: The following step cannot fail due to overflow in the Number
  // domain because abs(ns2 - ns1) <= 2 x nsMaxInstant.

  // 4. Let roundResult be ! RoundDuration(0, 0, 0, 0, 0, 0, 0, 0, 0, ns2 - ns1,
  // roundingIncrement, smallestUnit, roundingMode).[[DurationRecord]].
  DirectHandle<BigInt> diff =
      BigInt::Subtract(isolate, ns2, ns1).ToHandleChecked();
  // Note: Since diff could be very big and over the precision of double can
  // hold, break diff into diff_hours and diff_nanoseconds before pass into
  // RoundDuration.
  DirectHandle<BigInt> nanoseconds_in_a_hour =
      BigInt::FromUint64(isolate, 3600000000000);
  double diff_hours = Object::NumberValue(*BigInt::ToNumber(
      isolate,
      BigInt::Divide(isolate, diff, nanoseconds_in_a_hour).ToHandleChecked()));
  double diff_nanoseconds = Object::NumberValue(*BigInt::ToNumber(
      isolate, BigInt::Remainder(isolate, diff, nanoseconds_in_a_hour)
                   .ToHandleChecked()));
  DurationRecordWithRemainder round_record =
      RoundDuration(
          isolate, {0, 0, 0, {0, diff_hours, 0, 0, 0, 0, diff_nanoseconds}},
          rounding_increment, smallest_unit, rounding_mode, method_name)
          .ToChecked();
  // 5. Assert: roundResult.[[Days]] is 0.
  DCHECK_EQ(0, round_record.record.time_duration.days);
  // 6. Return ! BalanceDuration(0, roundResult.[[Hours]],
  // roundResult.[[Minutes]], roundResult.[[Seconds]],
  // roundResult.[[Milliseconds]], roundResult.[[Microseconds]],
  // roundResult.[[Nanoseconds]], largestUnit).
  return BalanceDuration(isolate, largest_unit,
                         isolate->factory()->undefined_value(),
                         round_record.record.time_duration, method_name)
      .ToChecked();
}

// #sec-temporal-differencetemporalinstant
MaybeDirectHandle<JSTemporalDuration> DifferenceTemporalInstant(
    Isolate* isolate, TimePreposition operation,
    DirectHandle<JSTemporalInstant> instant, DirectHandle<Object> other_obj,
    DirectHandle<Object> options, const char* method_name) {
  TEMPORAL_ENTER_FUNC();
  // 1. If operation is since, let sign be -1. Otherwise, let sign be 1.
  double sign = operation == TimePreposition::kSince ? -1 : 1;
  // 2. Set other to ? ToTemporalInstant(other).
  DirectHandle<JSTemporalInstant> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other, ToTemporalInstant(isolate, other_obj, method_name));
  // 3. Let settings be ? GetDifferenceSettings(operation, options, time, « »,
  // "nanosecond", "second").
  DifferenceSettings settings;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, settings,
      GetDifferenceSettings(isolate, operation, options, UnitGroup::kTime,
                            DisallowedUnitsInDifferenceSettings::kNone,
                            Unit::kNanosecond, Unit::kSecond, method_name),
      DirectHandle<JSTemporalDuration>());
  // 4. Let result be ! DifferenceInstant(instant.[[Nanoseconds]],
  // other.[[Nanoseconds]], settings.[[RoundingIncrement]],
  // settings.[[SmallestUnit]], settings.[[LargestUnit]],
  // settings.[[RoundingMode]]).
  TimeDurationRecord result = DifferenceInstant(
      isolate, direct_handle(instant->nanoseconds(), isolate),
      direct_handle(other->nanoseconds(), isolate), settings.rounding_increment,
      settings.smallest_unit, settings.largest_unit, settings.rounding_mode,
      method_name);
  // 5. Return ! CreateTemporalDuration(0, 0, 0, 0, sign × result.[[Hours]],
  // sign × result.[[Minutes]], sign × result.[[Seconds]], sign ×
  // result.[[Milliseconds]], sign × result.[[Microseconds]], sign ×
  // result.[[Nanoseconds]]).
  return CreateTemporalDuration(
             isolate, {0,
                       0,
                       0,
                       {0, sign * result.hours, sign * result.minutes,
                        sign * result.seconds, sign * result.milliseconds,
                        sign * result.microseconds, sign * result.nanoseconds}})
      .ToHandleChecked();
}
}  // namespace

// #sec-temporal.instant.prototype.add
MaybeDirectHandle<JSTemporalInstant> JSTemporalInstant::Add(
    Isolate* isolate, DirectHandle<JSTemporalInstant> handle,
    DirectHandle<Object> temporal_duration_like) {
  TEMPORAL_ENTER_FUNC();
  return AddDurationToOrSubtractDurationFromInstant(
      isolate, Arithmetic::kAdd, handle, temporal_duration_like,
      "Temporal.Instant.prototype.add");
}

// #sec-temporal.instant.prototype.subtract
MaybeDirectHandle<JSTemporalInstant> JSTemporalInstant::Subtract(
    Isolate* isolate, DirectHandle<JSTemporalInstant> handle,
    DirectHandle<Object> temporal_duration_like) {
  TEMPORAL_ENTER_FUNC();
  return AddDurationToOrSubtractDurationFromInstant(
      isolate, Arithmetic::kSubtract, handle, temporal_duration_like,
      "Temporal.Instant.prototype.subtract");
}

// #sec-temporal.instant.prototype.until
MaybeDirectHandle<JSTemporalDuration> JSTemporalInstant::Until(
    Isolate* isolate, DirectHandle<JSTemporalInstant> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  return DifferenceTemporalInstant(isolate, TimePreposition::kUntil, handle,
                                   other, options,
                                   "Temporal.Instant.prototype.until");
}

// #sec-temporal.instant.prototype.since
MaybeDirectHandle<JSTemporalDuration> JSTemporalInstant::Since(
    Isolate* isolate, DirectHandle<JSTemporalInstant> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  return DifferenceTemporalInstant(isolate, TimePreposition::kSince, handle,
                                   other, options,
                                   "Temporal.Instant.prototype.since");
}
namespace temporal {

// Step iii and iv of #sec-temporal.calendar.prototype.fields
MaybeDirectHandle<Oddball> IsInvalidTemporalCalendarField(
    Isolate* isolate, DirectHandle<String> next_value,
    DirectHandle<FixedArray> fields_name) {
  Factory* factory = isolate->factory();
  // iii. iii. If fieldNames contains nextValue, then
  for (int i = 0; i < fields_name->length(); i++) {
    Tagged<Object> item = fields_name->get(i);
    DCHECK(IsString(item));
    if (String::Equals(isolate, next_value,
                       direct_handle(Cast<String>(item), isolate))) {
      return isolate->factory()->true_value();
    }
  }
  // iv. If nextValue is not one of "year", "month", "monthCode", "day", "hour",
  // "minute", "second", "millisecond", "microsecond", "nanosecond", then
  if (!(String::Equals(isolate, next_value, factory->year_string()) ||
        String::Equals(isolate, next_value, factory->month_string()) ||
        String::Equals(isolate, next_value, factory->monthCode_string()) ||
        String::Equals(isolate, next_value, factory->day_string()) ||
        String::Equals(isolate, next_value, factory->hour_string()) ||
        String::Equals(isolate, next_value, factory->minute_string()) ||
        String::Equals(isolate, next_value, factory->second_string()) ||
        String::Equals(isolate, next_value, factory->millisecond_string()) ||
        String::Equals(isolate, next_value, factory->microsecond_string()) ||
        String::Equals(isolate, next_value, factory->nanosecond_string()))) {
    return isolate->factory()->true_value();
  }
  return isolate->factory()->false_value();
}

// #sec-temporal-getbuiltincalendar
MaybeDirectHandle<JSTemporalCalendar> GetBuiltinCalendar(
    Isolate* isolate, DirectHandle<String> id) {
  return JSTemporalCalendar::Constructor(isolate, CONSTRUCTOR(calendar),
                                         CONSTRUCTOR(calendar), id);
}

// A simple convenient function to avoid the need to unnecessarily exposing
// the definition of enum Disambiguation.
MaybeDirectHandle<JSTemporalInstant> BuiltinTimeZoneGetInstantForCompatible(
    Isolate* isolate, DirectHandle<JSReceiver> time_zone,
    DirectHandle<JSTemporalPlainDateTime> date_time, const char* method_name) {
  return BuiltinTimeZoneGetInstantFor(isolate, time_zone, date_time,
                                      Disambiguation::kCompatible, method_name);
}

}  // namespace temporal
}  // namespace v8::internal
