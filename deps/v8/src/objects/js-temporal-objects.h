// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_TEMPORAL_OBJECTS_H_
#define V8_OBJECTS_JS_TEMPORAL_OBJECTS_H_

#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/objects/objects.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-temporal-objects-tq.inc"

#define DECLARE_TEMPORAL_INLINE_GETTER_SETTER(field) \
  inline void set_##field(int32_t field);            \
  inline int32_t field() const;

#define DECLARE_TEMPORAL_TIME_INLINE_GETTER_SETTER()     \
  DECLARE_TEMPORAL_INLINE_GETTER_SETTER(iso_hour)        \
  DECLARE_TEMPORAL_INLINE_GETTER_SETTER(iso_minute)      \
  DECLARE_TEMPORAL_INLINE_GETTER_SETTER(iso_second)      \
  DECLARE_TEMPORAL_INLINE_GETTER_SETTER(iso_millisecond) \
  DECLARE_TEMPORAL_INLINE_GETTER_SETTER(iso_microsecond) \
  DECLARE_TEMPORAL_INLINE_GETTER_SETTER(iso_nanosecond)

#define DECLARE_TEMPORAL_DATE_INLINE_GETTER_SETTER() \
  DECLARE_TEMPORAL_INLINE_GETTER_SETTER(iso_year)    \
  DECLARE_TEMPORAL_INLINE_GETTER_SETTER(iso_month)   \
  DECLARE_TEMPORAL_INLINE_GETTER_SETTER(iso_day)

#define TEMPORAL_UNIMPLEMENTED(T)            \
  {                                          \
    printf("TBW %s\n", __PRETTY_FUNCTION__); \
    UNIMPLEMENTED();                         \
  }

class JSTemporalPlainDate;
class JSTemporalPlainMonthDay;
class JSTemporalPlainYearMonth;

class JSTemporalCalendar
    : public TorqueGeneratedJSTemporalCalendar<JSTemporalCalendar, JSObject> {
 public:
  // #sec-temporal.calendar
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalCalendar>
  Constructor(Isolate* isolate, DirectHandle<JSFunction> target,
              DirectHandle<HeapObject> new_target,
              DirectHandle<Object> identifier);

  // #sec-temporal.calendar.prototype.year
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Smi> Year(
      Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
      DirectHandle<Object> temporal_date_like);

  // #sec-temporal.calendar.prototype.dateadd
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainDate> DateAdd(
      Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
      DirectHandle<Object> date, DirectHandle<Object> durations,
      DirectHandle<Object> options);

  // #sec-temporal.calendar.prototype.daysinyear
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Smi> DaysInYear(
      Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
      DirectHandle<Object> temporal_date_like);

  // #sec-temporal.calendar.prototype.dayofweek
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Smi> DayOfWeek(
      Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
      DirectHandle<Object> temporal_date_like);

  // #sec-temporal.calendar.prototype.dayofyear
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Smi> DayOfYear(
      Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
      DirectHandle<Object> temporal_date_like);

  // #sec-temporal.calendar.prototype.monthsinyear
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Smi> MonthsInYear(
      Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
      DirectHandle<Object> temporal_date_like);

  // #sec-temporal.calendar.prototype.inleapyear
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Oddball> InLeapYear(
      Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
      DirectHandle<Object> temporal_date_like);

  // #sec-temporal.calendar.prototype.dateuntil
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalDuration> DateUntil(
      Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
      DirectHandle<Object> one, DirectHandle<Object> two,
      DirectHandle<Object> options);

  // #sec-temporal.calendar.prototype.daysinmonth
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Smi> DaysInMonth(
      Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
      DirectHandle<Object> temporal_date_like);

  // #sec-temporal.calendar.prototype.daysinweek
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Smi> DaysInWeek(
      Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
      DirectHandle<Object> temporal_date_like);

  // #sec-temporal.calendar.prototype.datefromfields
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainDate>
  DateFromFields(Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
                 DirectHandle<Object> fields, DirectHandle<Object> options);

  // #sec-temporal.calendar.prototype.monthdayfromfields
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainMonthDay>
  MonthDayFromFields(Isolate* isolate,
                     DirectHandle<JSTemporalCalendar> calendar,
                     DirectHandle<Object> fields, DirectHandle<Object> options);

  // #sec-temporal.calendar.prototype.yearmonthfromfields
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainYearMonth>
  YearMonthFromFields(Isolate* isolate,
                      DirectHandle<JSTemporalCalendar> calendar,
                      DirectHandle<Object> fields,
                      DirectHandle<Object> options);

  // #sec-temporal.calendar.prototype.mergefields
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSReceiver> MergeFields(
      Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
      DirectHandle<Object> fields, DirectHandle<Object> additional_fields);

  // #sec-temporal.calendar.prototype.monthcode
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> MonthCode(
      Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
      DirectHandle<Object> temporal_date_like);

  // #sec-temporal.calendar.prototype.month
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Smi> Month(
      Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
      DirectHandle<Object> temporal_date_like);

  // #sec-temporal.calendar.prototype.day
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Smi> Day(
      Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
      DirectHandle<Object> temporal_date_like);

  // #sec-temporal.calendar.prototype.weekofyear
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Smi> WeekOfYear(
      Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
      DirectHandle<Object> temporal_date_like);

  // #sec-temporal.calendar.prototype.tostring
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> ToString(
      Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
      const char* method_name);

#ifdef V8_INTL_SUPPORT
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Object> Era(
      Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
      DirectHandle<Object> temporal_date_like);

  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Object> EraYear(
      Isolate* isolate, DirectHandle<JSTemporalCalendar> calendar,
      DirectHandle<Object> temporal_date_like);
#endif  // V8_INTL_SUPPORT

  DECL_PRINTER(JSTemporalCalendar)

  DEFINE_TORQUE_GENERATED_JS_TEMPORAL_CALENDAR_FLAGS()

  DECL_INT_ACCESSORS(calendar_index)

  TQ_OBJECT_CONSTRUCTORS(JSTemporalCalendar)
};

class JSTemporalDuration
    : public TorqueGeneratedJSTemporalDuration<JSTemporalDuration, JSObject> {
 public:
  // #sec-temporal.duration
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalDuration>
  Constructor(Isolate* isolate, DirectHandle<JSFunction> target,
              DirectHandle<HeapObject> new_target, DirectHandle<Object> years,
              DirectHandle<Object> months, DirectHandle<Object> weeks,
              DirectHandle<Object> days, DirectHandle<Object> hours,
              DirectHandle<Object> minutes, DirectHandle<Object> seconds,
              DirectHandle<Object> milliseconds,
              DirectHandle<Object> microseconds,
              DirectHandle<Object> nanoseconds);

  // #sec-temporal.duration.compare
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Smi> Compare(
      Isolate* isolate, DirectHandle<Object> one, DirectHandle<Object> two,
      DirectHandle<Object> options);

  // #sec-temporal.duration.from
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalDuration> From(
      Isolate* isolate, DirectHandle<Object> item);

  // #sec-get-temporal.duration.prototype.sign
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Smi> Sign(
      Isolate* isolate, DirectHandle<JSTemporalDuration> duration);

  // #sec-get-temporal.duration.prototype.blank
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Oddball> Blank(
      Isolate* isolate, DirectHandle<JSTemporalDuration> duration);

  // #sec-temporal.duration.prototype.negated
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalDuration> Negated(
      Isolate* isolate, DirectHandle<JSTemporalDuration> duration);

  // #sec-temporal.duration.prototype.abs
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalDuration> Abs(
      Isolate* isolate, DirectHandle<JSTemporalDuration> duration);

  // #sec-temporal.duration.prototype.add
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalDuration> Add(
      Isolate* isolate, DirectHandle<JSTemporalDuration> duration,
      DirectHandle<Object> other, DirectHandle<Object> options);

  // #sec-temporal.duration.prototype.subtract
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalDuration> Subtract(
      Isolate* isolate, DirectHandle<JSTemporalDuration> duration,
      DirectHandle<Object> other, DirectHandle<Object> options);

  // #sec-temporal.duration.prototype.round
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalDuration> Round(
      Isolate* isolate, DirectHandle<JSTemporalDuration> duration,
      DirectHandle<Object> round_to_obj);

  // #sec-temporal.duration.prototype.total
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Object> Total(
      Isolate* isolate, DirectHandle<JSTemporalDuration> duration,
      DirectHandle<Object> total_of);

  // #sec-temporal.duration.prototype.tojson
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> ToJSON(
      Isolate* isolate, DirectHandle<JSTemporalDuration> duration);

  // #sec-temporal.duration.prototype.tolocalestring
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> ToLocaleString(
      Isolate* isolate, DirectHandle<JSTemporalDuration> duration,
      DirectHandle<Object> locales, DirectHandle<Object> options);

  // #sec-temporal.duration.prototype.tostring
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> ToString(
      Isolate* isolate, DirectHandle<JSTemporalDuration> duration,
      DirectHandle<Object> options);

  // #sec-temporal.duration.prototype.with
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalDuration> With(
      Isolate* isolate, DirectHandle<JSTemporalDuration> duration,
      DirectHandle<Object> temporal_duration_like);

  DECL_PRINTER(JSTemporalDuration)

  TQ_OBJECT_CONSTRUCTORS(JSTemporalDuration)
};

class JSTemporalInstant
    : public TorqueGeneratedJSTemporalInstant<JSTemporalInstant, JSObject> {
 public:
  // #sec-temporal-instant-constructor
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalInstant> Constructor(
      Isolate* isolate, DirectHandle<JSFunction> target,
      DirectHandle<HeapObject> new_target,
      DirectHandle<Object> epoch_nanoseconds);

  // #sec-temporal.now.instant
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalInstant> Now(
      Isolate* isolate);

  // #sec-temporal.instant.fromepochseconds
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalInstant>
  FromEpochSeconds(Isolate* isolate, DirectHandle<Object> epoch_seconds);

  // #sec-temporal.instant.fromepochmilliseconds
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalInstant>
  FromEpochMilliseconds(Isolate* isolate,
                        DirectHandle<Object> epoch_milliseconds);

  // #sec-temporal.instant.fromepochmicroseconds
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalInstant>
  FromEpochMicroseconds(Isolate* isolate,
                        DirectHandle<Object> epoch_microseconds);

  // #sec-temporal.instant.fromepochnanoeconds
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalInstant>
  FromEpochNanoseconds(Isolate* isolate,
                       DirectHandle<Object> epoch_nanoseconds);

  // #sec-temporal.instant.prototype.round
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalInstant> Round(
      Isolate* isolate, DirectHandle<JSTemporalInstant> instant,
      DirectHandle<Object> round_to);

  // #sec-temporal.instant.from
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalInstant> From(
      Isolate* isolate, DirectHandle<Object> item);

  // #sec-temporal.instant.prototype.tozoneddatetime
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalZonedDateTime>
  ToZonedDateTime(Isolate* isolate, DirectHandle<JSTemporalInstant> instant,
                  DirectHandle<Object> item);

  // #sec-temporal.instant.prototype.tozoneddatetimeiso
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalZonedDateTime>
  ToZonedDateTimeISO(Isolate* isolate, DirectHandle<JSTemporalInstant> instant,
                     DirectHandle<Object> item);

  // #sec-temporal.instant.compare
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Smi> Compare(
      Isolate* isolate, DirectHandle<Object> one, DirectHandle<Object> two);

  // #sec-temporal.instant.prototype.equals
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Oddball> Equals(
      Isolate* isolate, DirectHandle<JSTemporalInstant> instant,
      DirectHandle<Object> other);

  // #sec-temporal.instant.prototype.add
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalInstant> Add(
      Isolate* isolate, DirectHandle<JSTemporalInstant> instant,
      DirectHandle<Object> temporal_duration_like);

  // #sec-temporal.instant.prototype.subtract
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalInstant> Subtract(
      Isolate* isolate, DirectHandle<JSTemporalInstant> instant,
      DirectHandle<Object> temporal_duration_like);

  // #sec-temporal.instant.prototype.tojson
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> ToJSON(
      Isolate* isolate, DirectHandle<JSTemporalInstant> instant);

  // #sec-temporal.instant.prototype.tolocalestring
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> ToLocaleString(
      Isolate* isolate, DirectHandle<JSTemporalInstant> instant,
      DirectHandle<Object> locales, DirectHandle<Object> options);

  // #sec-temporal.instant.prototype.tostring
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> ToString(
      Isolate* isolate, DirectHandle<JSTemporalInstant> instant,
      DirectHandle<Object> options);

  // #sec-temporal.instant.prototype.until
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalDuration> Until(
      Isolate* isolate, DirectHandle<JSTemporalInstant> instant,
      DirectHandle<Object> other, DirectHandle<Object> options);

  // #sec-temporal.instant.prototype.since
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalDuration> Since(
      Isolate* isolate, DirectHandle<JSTemporalInstant> instant,
      DirectHandle<Object> other, DirectHandle<Object> options);

  DECL_PRINTER(JSTemporalInstant)

  TQ_OBJECT_CONSTRUCTORS(JSTemporalInstant)
};

class JSTemporalPlainDate
    : public TorqueGeneratedJSTemporalPlainDate<JSTemporalPlainDate, JSObject> {
 public:
  // #sec-temporal-createtemporaldate
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainDate>
  Constructor(Isolate* isolate, DirectHandle<JSFunction> target,
              DirectHandle<HeapObject> new_target,
              DirectHandle<Object> iso_year, DirectHandle<Object> iso_month,
              DirectHandle<Object> iso_day, DirectHandle<Object> calendar_like);

  // #sec-temporal.plaindate.compare
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Smi> Compare(
      Isolate* isolate, DirectHandle<Object> one, DirectHandle<Object> two);

  // #sec-temporal.plaindate.prototype.equals
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Oddball> Equals(
      Isolate* isolate, DirectHandle<JSTemporalPlainDate> plain_date,
      DirectHandle<Object> other);

  // #sec-temporal.plaindate.prototype.withcalendar
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainDate>
  WithCalendar(Isolate* isolate, DirectHandle<JSTemporalPlainDate> plain_date,
               DirectHandle<Object> calendar_like);

  // #sec-temporal.plaindate.prototype.toplaindatetime
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainDateTime>
  ToPlainDateTime(Isolate* isolate,
                  DirectHandle<JSTemporalPlainDate> plain_date,
                  DirectHandle<Object> temporal_time);

  // #sec-temporal.plaindate.prototype.with
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainDate> With(
      Isolate* isolate, DirectHandle<JSTemporalPlainDate> plain_date,
      DirectHandle<Object> temporal_duration_like,
      DirectHandle<Object> options);

  // #sec-temporal.plaindate.from
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainDate> From(
      Isolate* isolate, DirectHandle<Object> item,
      DirectHandle<Object> options);

  // #sec-temporal.plaindate.prototype.add
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainDate> Add(
      Isolate* isolate, DirectHandle<JSTemporalPlainDate> plain_date,
      DirectHandle<Object> temporal_duration_like,
      DirectHandle<Object> options);

  // #sec-temporal.plaindate.prototype.subtract
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainDate> Subtract(
      Isolate* isolate, DirectHandle<JSTemporalPlainDate> plain_date,
      DirectHandle<Object> temporal_duration_like,
      DirectHandle<Object> options);

  // #sec-temporal.plaindate.prototype.until
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalDuration> Until(
      Isolate* isolate, DirectHandle<JSTemporalPlainDate> plain_date,
      DirectHandle<Object> other, DirectHandle<Object> options);

  // #sec-temporal.plaindate.prototype.since
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalDuration> Since(
      Isolate* isolate, DirectHandle<JSTemporalPlainDate> plain_date,
      DirectHandle<Object> other, DirectHandle<Object> options);

  // #sec-temporal.plaindate.prototype.getisofields
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSReceiver> GetISOFields(
      Isolate* isolate, DirectHandle<JSTemporalPlainDate> plain_date);

  // #sec-temporal.plaindate.prototype.toplainyearmonth
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainYearMonth>
  ToPlainYearMonth(Isolate* isolate,
                   DirectHandle<JSTemporalPlainDate> plain_date);

  // #sec-temporal.plaindate.prototype.toplainmonthday
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainMonthDay>
  ToPlainMonthDay(Isolate* isolate,
                  DirectHandle<JSTemporalPlainDate> plain_date);

  // #sec-temporal.plaindate.prototype.tozoneddatetime
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalZonedDateTime>
  ToZonedDateTime(Isolate* isolate,
                  DirectHandle<JSTemporalPlainDate> plain_date,
                  DirectHandle<Object> item);

  // #sec-temporal.now.plaindate
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainDate> Now(
      Isolate* isolate, DirectHandle<Object> calendar_like,
      DirectHandle<Object> temporal_time_zone_like);

  // #sec-temporal.now.plaindateiso
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainDate> NowISO(
      Isolate* isolate, DirectHandle<Object> temporal_time_zone_like);

  // #sec-temporal.plaindate.prototype.tostring
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> ToString(
      Isolate* isolate, DirectHandle<JSTemporalPlainDate> plain_date,
      DirectHandle<Object> options);

  // #sec-temporal.plaindate.prototype.tojson
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> ToJSON(
      Isolate* isolate, DirectHandle<JSTemporalPlainDate> plain_date);

  // #sec-temporal.plaindate.prototype.tolocalestring
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> ToLocaleString(
      Isolate* isolate, DirectHandle<JSTemporalPlainDate> plain_date,
      DirectHandle<Object> locales, DirectHandle<Object> options);

  DECL_PRINTER(JSTemporalPlainDate)

  DEFINE_TORQUE_GENERATED_JS_TEMPORAL_YEAR_MONTH_DAY()

  DECLARE_TEMPORAL_DATE_INLINE_GETTER_SETTER()

  TQ_OBJECT_CONSTRUCTORS(JSTemporalPlainDate)
};

class JSTemporalPlainDateTime
    : public TorqueGeneratedJSTemporalPlainDateTime<JSTemporalPlainDateTime,
                                                    JSObject> {
 public:
  // #sec-temporal-createtemporaldatetime
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainDateTime>
  Constructor(Isolate* isolate, DirectHandle<JSFunction> target,
              DirectHandle<HeapObject> new_target,
              DirectHandle<Object> iso_year, DirectHandle<Object> iso_month,
              DirectHandle<Object> iso_day, DirectHandle<Object> hour,
              DirectHandle<Object> minute, DirectHandle<Object> second,
              DirectHandle<Object> millisecond,
              DirectHandle<Object> microsecond, DirectHandle<Object> nanosecond,
              DirectHandle<Object> calendar_like);

  // #sec-temporal.plaindatetime.prototype.withplaintime
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainDateTime>
  WithPlainTime(Isolate* isolate,
                DirectHandle<JSTemporalPlainDateTime> date_time,
                DirectHandle<Object> temporal_time_like);

  // #sec-temporal.plaindatetime.prototype.withcalendar
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainDateTime>
  WithCalendar(Isolate* isolate,
               DirectHandle<JSTemporalPlainDateTime> date_time,
               DirectHandle<Object> calendar_like);

  // #sec-temporal.plaindatetime.from
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainDateTime> From(
      Isolate* isolate, DirectHandle<Object> item,
      DirectHandle<Object> options);

  // #sec-temporal.plaindatetime.compare
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Smi> Compare(
      Isolate* isolate, DirectHandle<Object> one, DirectHandle<Object> two);

  // #sec-temporal.plaindatetime.prototype.equals
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Oddball> Equals(
      Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> plain_date,
      DirectHandle<Object> other);

  // #sec-temporal.plaindatetime.prototype.toplainyearmonth
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainYearMonth>
  ToPlainYearMonth(Isolate* isolate,
                   DirectHandle<JSTemporalPlainDateTime> date_time);

  // #sec-temporal.plaindatetime.prototype.toplainmonthday
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainMonthDay>
  ToPlainMonthDay(Isolate* isolate,
                  DirectHandle<JSTemporalPlainDateTime> date_time);

  // #sec-temporal.plaintime.prototype.tozoneddatetime
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalZonedDateTime>
  ToZonedDateTime(Isolate* isolate,
                  DirectHandle<JSTemporalPlainDateTime> date_time,
                  DirectHandle<Object> temporal_time_zone_like,
                  DirectHandle<Object> options_obj);

  // #sec-temporal.plaindatetime.prototype.withplaindate
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainDateTime>
  WithPlainDate(Isolate* isolate,
                DirectHandle<JSTemporalPlainDateTime> date_time,
                DirectHandle<Object> temporal_date_date_like);

  // #sec-temporal.plaindatetime.prototype.getisofields
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSReceiver> GetISOFields(
      Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time);

  // #sec-temporal.plaindatetime.prototype.with
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainDateTime> With(
      Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
      DirectHandle<Object> temporal_date_time_like,
      DirectHandle<Object> options);

  // #sec-temporal.plaindatetime.prototype.tojson
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> ToJSON(
      Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time);

  // #sec-temporal.plaindatetime.prototype.tolocalestring
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> ToLocaleString(
      Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
      DirectHandle<Object> locales, DirectHandle<Object> options);

  // #sec-temporal.plaindatetime.prototype.tostring
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> ToString(
      Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
      DirectHandle<Object> options);

  // #sec-temporal.plaindatetime.prototype.round
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainDateTime> Round(
      Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
      DirectHandle<Object> round_to);

  // #sec-temporal.plaindatetime.prototype.until
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalDuration> Until(
      Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
      DirectHandle<Object> other, DirectHandle<Object> options);

  // #sec-temporal.plaindatetime.prototype.since
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalDuration> Since(
      Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
      DirectHandle<Object> other, DirectHandle<Object> options);

  // #sec-temporal.plaindatetime.prototype.add
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainDateTime> Add(
      Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
      DirectHandle<Object> temporal_duration_like,
      DirectHandle<Object> options);

  // #sec-temporal.plaindatetime.prototype.subtract
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainDateTime>
  Subtract(Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
           DirectHandle<Object> temporal_duration_like,
           DirectHandle<Object> options);

  // #sec-temporal.now.plaindatetime
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainDateTime> Now(
      Isolate* isolate, DirectHandle<Object> calendar_like,
      DirectHandle<Object> temporal_time_zone_like);

  // #sec-temporal.now.plaindatetimeiso
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainDateTime>
  NowISO(Isolate* isolate, DirectHandle<Object> temporal_time_zone_like);

  // #sec-temporal.plaindatetime.prototype.toplaindate
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainDate>
  ToPlainDate(Isolate* isolate,
              DirectHandle<JSTemporalPlainDateTime> date_time);

  // #sec-temporal.plaindatetime.prototype.toplaintime
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainTime>
  ToPlainTime(Isolate* isolate,
              DirectHandle<JSTemporalPlainDateTime> date_time);

  DECL_PRINTER(JSTemporalPlainDateTime)

  DEFINE_TORQUE_GENERATED_JS_TEMPORAL_YEAR_MONTH_DAY()
  DEFINE_TORQUE_GENERATED_JS_TEMPORAL_HOUR_MINUTE_SECOND()
  DEFINE_TORQUE_GENERATED_JS_TEMPORAL_SECOND_PARTS()

  DECLARE_TEMPORAL_DATE_INLINE_GETTER_SETTER()
  DECLARE_TEMPORAL_TIME_INLINE_GETTER_SETTER()

  TQ_OBJECT_CONSTRUCTORS(JSTemporalPlainDateTime)
};

class JSTemporalPlainMonthDay
    : public TorqueGeneratedJSTemporalPlainMonthDay<JSTemporalPlainMonthDay,
                                                    JSObject> {
 public:
  // ##sec-temporal.plainmonthday
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainMonthDay>
  Constructor(Isolate* isolate, DirectHandle<JSFunction> target,
              DirectHandle<HeapObject> new_target,
              DirectHandle<Object> iso_month, DirectHandle<Object> iso_day,
              DirectHandle<Object> calendar_like,
              DirectHandle<Object> reference_iso_year);

  // #sec-temporal.plainmonthday.from
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainMonthDay> From(
      Isolate* isolate, DirectHandle<Object> item,
      DirectHandle<Object> options);

  // #sec-temporal.plainmonthday.prototype.equals
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Oddball> Equals(
      Isolate* isolate, DirectHandle<JSTemporalPlainMonthDay> month_day,
      DirectHandle<Object> other);

  // #sec-temporal.plainmonthday.prototype.with
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainMonthDay> With(
      Isolate* isolate, DirectHandle<JSTemporalPlainMonthDay> month_day,
      DirectHandle<Object> temporal_month_day_like,
      DirectHandle<Object> options);

  // #sec-temporal.plainmonthday.prototype.toplaindate
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainDate>
  ToPlainDate(Isolate* isolate, DirectHandle<JSTemporalPlainMonthDay> month_day,
              DirectHandle<Object> item);

  // #sec-temporal.plainmonthday.prototype.getisofields
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSReceiver> GetISOFields(
      Isolate* isolate, DirectHandle<JSTemporalPlainMonthDay> month_day);

  // #sec-temporal.plainmonthday.prototype.tostring
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> ToString(
      Isolate* isolate, DirectHandle<JSTemporalPlainMonthDay> month_day,
      DirectHandle<Object> options);

  // #sec-temporal.plainmonthday.prototype.tojson
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> ToJSON(
      Isolate* isolate, DirectHandle<JSTemporalPlainMonthDay> month_day);

  // #sec-temporal.plainmonthday.prototype.tolocalestring
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> ToLocaleString(
      Isolate* isolate, DirectHandle<JSTemporalPlainMonthDay> plain_date,
      DirectHandle<Object> locales, DirectHandle<Object> options);

  DECL_PRINTER(JSTemporalPlainMonthDay)

  DEFINE_TORQUE_GENERATED_JS_TEMPORAL_YEAR_MONTH_DAY()

  DECLARE_TEMPORAL_DATE_INLINE_GETTER_SETTER()

  TQ_OBJECT_CONSTRUCTORS(JSTemporalPlainMonthDay)
};

class JSTemporalPlainTime
    : public TorqueGeneratedJSTemporalPlainTime<JSTemporalPlainTime, JSObject> {
 public:
  // #sec-temporal-plaintime-constructor
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainTime>
  Constructor(Isolate* isolate, DirectHandle<JSFunction> target,
              DirectHandle<HeapObject> new_target, DirectHandle<Object> hour,
              DirectHandle<Object> minute, DirectHandle<Object> second,
              DirectHandle<Object> millisecond,
              DirectHandle<Object> microsecond,
              DirectHandle<Object> nanosecond);

  // #sec-temporal.plaintime.compare
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Smi> Compare(
      Isolate* isolate, DirectHandle<Object> one, DirectHandle<Object> two);

  // #sec-temporal.plaintime.prototype.equals
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Oddball> Equals(
      Isolate* isolate, DirectHandle<JSTemporalPlainTime> plain_date,
      DirectHandle<Object> other);

  // #sec-temporal.plaintime.from
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainTime> From(
      Isolate* isolate, DirectHandle<Object> item,
      DirectHandle<Object> options);

  // #sec-temporal.plaintime.prototype.tozoneddatetime
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalZonedDateTime>
  ToZonedDateTime(Isolate* isolate,
                  DirectHandle<JSTemporalPlainTime> plain_time,
                  DirectHandle<Object> item);

  // #sec-temporal.plaintime.prototype.add
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainTime> Add(
      Isolate* isolate, DirectHandle<JSTemporalPlainTime> plain_time,
      DirectHandle<Object> temporal_duration_like);

  // #sec-temporal.plaintime.prototype.subtract
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainTime> Subtract(
      Isolate* isolate, DirectHandle<JSTemporalPlainTime> plain_time,
      DirectHandle<Object> temporal_duration_like);

  // #sec-temporal.plaintime.prototype.until
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalDuration> Until(
      Isolate* isolate, DirectHandle<JSTemporalPlainTime> plain_time,
      DirectHandle<Object> other, DirectHandle<Object> options);

  // #sec-temporal.plaintime.prototype.since
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalDuration> Since(
      Isolate* isolate, DirectHandle<JSTemporalPlainTime> plain_time,
      DirectHandle<Object> other, DirectHandle<Object> options);

  // #sec-temporal.plaintime.prototype.round
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainTime> Round(
      Isolate* isolate, DirectHandle<JSTemporalPlainTime> plain_time,
      DirectHandle<Object> round_to);

  // #sec-temporal.plaintime.prototype.getisofields
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSReceiver> GetISOFields(
      Isolate* isolate, DirectHandle<JSTemporalPlainTime> plain_time);

  // #sec-temporal.plaintime.prototype.toplaindatetime
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainDateTime>
  ToPlainDateTime(Isolate* isolate,
                  DirectHandle<JSTemporalPlainTime> plain_time,
                  DirectHandle<Object> temporal_date);

  // #sec-temporal.plaintime.prototype.with
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainTime> With(
      Isolate* isolate, DirectHandle<JSTemporalPlainTime> plain_time,
      DirectHandle<Object> temporal_time_like, DirectHandle<Object> options);

  // #sec-temporal.now.plaintimeiso
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainTime> NowISO(
      Isolate* isolate, DirectHandle<Object> temporal_time_zone_like);

  // #sec-temporal.plaintime.prototype.tojson
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> ToJSON(
      Isolate* isolate, DirectHandle<JSTemporalPlainTime> plain_time);

  // #sec-temporal.plaintime.prototype.tostring
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> ToString(
      Isolate* isolate, DirectHandle<JSTemporalPlainTime> plain_time,
      DirectHandle<Object> options);

  // #sec-temporal.plaintime.prototype.tolocalestring
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> ToLocaleString(
      Isolate* isolate, DirectHandle<JSTemporalPlainTime> plain_time,
      DirectHandle<Object> locales, DirectHandle<Object> options);

  DECL_PRINTER(JSTemporalPlainTime)

  DEFINE_TORQUE_GENERATED_JS_TEMPORAL_HOUR_MINUTE_SECOND()
  DEFINE_TORQUE_GENERATED_JS_TEMPORAL_SECOND_PARTS()

  DECLARE_TEMPORAL_TIME_INLINE_GETTER_SETTER()

  TQ_OBJECT_CONSTRUCTORS(JSTemporalPlainTime)
};

class JSTemporalPlainYearMonth
    : public TorqueGeneratedJSTemporalPlainYearMonth<JSTemporalPlainYearMonth,
                                                     JSObject> {
 public:
  // ##sec-temporal.plainyearmonth
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainYearMonth>
  Constructor(Isolate* isolate, DirectHandle<JSFunction> target,
              DirectHandle<HeapObject> new_target,
              DirectHandle<Object> iso_year, DirectHandle<Object> iso_month,
              DirectHandle<Object> calendar_like,
              DirectHandle<Object> reference_iso_day);

  // #sec-temporal.plainyearmonth.from
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainYearMonth> From(
      Isolate* isolate, DirectHandle<Object> item,
      DirectHandle<Object> options);

  // #sec-temporal.plainyearmonth.compare
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Smi> Compare(
      Isolate* isolate, DirectHandle<Object> one, DirectHandle<Object> two);

  // #sec-temporal.plainyearmonth.prototype.equals
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Oddball> Equals(
      Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month,
      DirectHandle<Object> other);

  // #sec-temporal.plainyearmonth.prototype.with
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainYearMonth> With(
      Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month,
      DirectHandle<Object> temporal_year_month_like,
      DirectHandle<Object> options);

  // #sec-temporal.plainyearmonth.prototype.toplaindate
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainDate>
  ToPlainDate(Isolate* isolate,
              DirectHandle<JSTemporalPlainYearMonth> year_month,
              DirectHandle<Object> item);

  // #sec-temporal.plainyearmonth.prototype.getisofields
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSReceiver> GetISOFields(
      Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month);

  // #sec-temporal.plainyearmonth.prototype.add
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainYearMonth> Add(
      Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month,
      DirectHandle<Object> temporal_duration_like,
      DirectHandle<Object> options);

  // #sec-temporal.plainyearmonth.prototype.subtract
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainYearMonth>
  Subtract(Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month,
           DirectHandle<Object> temporal_duration_like,
           DirectHandle<Object> options);

  // #sec-temporal.plainyearmonth.prototype.until
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalDuration> Until(
      Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month,
      DirectHandle<Object> other, DirectHandle<Object> options);

  // #sec-temporal.plaindyearmonth.prototype.since
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalDuration> Since(
      Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month,
      DirectHandle<Object> other, DirectHandle<Object> options);

  // #sec-temporal.plainyearmonth.prototype.tostring
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> ToString(
      Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month,
      DirectHandle<Object> options);

  // #sec-temporal.plainyearmonth.prototype.tojson
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> ToJSON(
      Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month);

  // #sec-temporal.plainyearmonth.prototype.tolocalestring
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> ToLocaleString(
      Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> plain_date,
      DirectHandle<Object> locales, DirectHandle<Object> options);

  // Abstract Operations

  DECL_PRINTER(JSTemporalPlainYearMonth)

  DEFINE_TORQUE_GENERATED_JS_TEMPORAL_YEAR_MONTH_DAY()

  DECLARE_TEMPORAL_DATE_INLINE_GETTER_SETTER()

  TQ_OBJECT_CONSTRUCTORS(JSTemporalPlainYearMonth)
};

class JSTemporalTimeZone
    : public TorqueGeneratedJSTemporalTimeZone<JSTemporalTimeZone, JSObject> {
 public:
  // #sec-temporal.now.timezone
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalTimeZone> Now(
      Isolate* isolate);

  // #sec-temporal.timezone
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalTimeZone>
  Constructor(Isolate* isolate, DirectHandle<JSFunction> target,
              DirectHandle<HeapObject> new_target,
              DirectHandle<Object> identifier);

  // #sec-temporal.timezone.prototype.getinstantfor
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalInstant>
  GetInstantFor(Isolate* isolate, DirectHandle<JSTemporalTimeZone> time_zone,
                DirectHandle<Object> dateTime, DirectHandle<Object> options);

  // #sec-temporal.timezone.prototype.getplaindatetimefor
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainDateTime>
  GetPlainDateTimeFor(Isolate* isolate,
                      DirectHandle<JSTemporalTimeZone> time_zone,
                      DirectHandle<Object> instance,
                      DirectHandle<Object> calendar_like);

  // #sec-temporal.timezone.prototype.getnexttransition
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Object> GetNextTransition(
      Isolate* isolate, DirectHandle<JSTemporalTimeZone> time_zone,
      DirectHandle<Object> starting_point);

  // #sec-temporal.timezone.prototype.getprevioustransition
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Object> GetPreviousTransition(
      Isolate* isolate, DirectHandle<JSTemporalTimeZone> time_zone,
      DirectHandle<Object> starting_point);

  // #sec-temporal.timezone.prototype.getpossibleinstantsfor
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSArray>
  GetPossibleInstantsFor(Isolate* isolate,
                         DirectHandle<JSTemporalTimeZone> time_zone,
                         DirectHandle<Object> date_time);

  // #sec-temporal.timezone.prototype.getoffsetnanosecondsfor
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Object>
  GetOffsetNanosecondsFor(Isolate* isolate,
                          DirectHandle<JSTemporalTimeZone> time_zone,
                          DirectHandle<Object> instance);

  // #sec-temporal.timezone.prototype.getoffsetstringfor
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> GetOffsetStringFor(
      Isolate* isolate, DirectHandle<JSTemporalTimeZone> time_zone,
      DirectHandle<Object> instance);

  // #sec-temporal.timezone.prototype.tostring
  static MaybeDirectHandle<Object> ToString(
      Isolate* isolate, DirectHandle<JSTemporalTimeZone> time_zone,
      const char* method_name);

  DECL_PRINTER(JSTemporalTimeZone)

  DEFINE_TORQUE_GENERATED_JS_TEMPORAL_TIME_ZONE_FLAGS()
  DEFINE_TORQUE_GENERATED_JS_TEMPORAL_TIME_ZONE_SUB_MILLISECONDS()

  DECL_BOOLEAN_ACCESSORS(is_offset)
  DECL_INT_ACCESSORS(offset_milliseconds_or_time_zone_index)

  DECLARE_TEMPORAL_INLINE_GETTER_SETTER(offset_milliseconds)
  DECLARE_TEMPORAL_INLINE_GETTER_SETTER(offset_sub_milliseconds)

  int32_t time_zone_index() const;
  static constexpr int32_t kUTCTimeZoneIndex = 0;

  int64_t offset_nanoseconds() const;
  void set_offset_nanoseconds(int64_t offset_nanoseconds);

  MaybeDirectHandle<String> id(Isolate* isolate) const;

  TQ_OBJECT_CONSTRUCTORS(JSTemporalTimeZone)
};

class JSTemporalZonedDateTime
    : public TorqueGeneratedJSTemporalZonedDateTime<JSTemporalZonedDateTime,
                                                    JSObject> {
 public:
  // #sec-temporal.zoneddatetime
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalZonedDateTime>
  Constructor(Isolate* isolate, DirectHandle<JSFunction> target,
              DirectHandle<HeapObject> new_target,
              DirectHandle<Object> epoch_nanoseconds,
              DirectHandle<Object> time_zone_like,
              DirectHandle<Object> calendar_like);

  // #sec-temporal.zoneddatetime.from
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalZonedDateTime> From(
      Isolate* isolate, DirectHandle<Object> item,
      DirectHandle<Object> options);

  // #sec-temporal.zoneddatetime.compare
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Smi> Compare(
      Isolate* isolate, DirectHandle<Object> one, DirectHandle<Object> two);

  // #sec-temporal.zoneddatetime.prototype.equals
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Oddball> Equals(
      Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
      DirectHandle<Object> other);

  // #sec-temporal.zoneddatetime.prototype.with
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalZonedDateTime> With(
      Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
      DirectHandle<Object> temporal_zoned_date_time_like,
      DirectHandle<Object> options);

  // #sec-temporal.zoneddatetime.prototype.withcalendar
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalZonedDateTime>
  WithCalendar(Isolate* isolate,
               DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
               DirectHandle<Object> calendar_like);

  // #sec-temporal.zoneddatetime.prototype.withplaindate
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalZonedDateTime>
  WithPlainDate(Isolate* isolate,
                DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
                DirectHandle<Object> plain_date_like);

  // #sec-temporal.zoneddatetime.prototype.withplaintime
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalZonedDateTime>
  WithPlainTime(Isolate* isolate,
                DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
                DirectHandle<Object> plain_time_like);

  // #sec-temporal.zoneddatetime.prototype.withtimezone
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalZonedDateTime>
  WithTimeZone(Isolate* isolate,
               DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
               DirectHandle<Object> time_zone_like);

  // #sec-get-temporal.zoneddatetime.prototype.hoursinday
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Object> HoursInDay(
      Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time);

  // #sec-temporal.zoneddatetime.prototype.round
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalZonedDateTime> Round(
      Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
      DirectHandle<Object> round_to);

  // #sec-temporal.zoneddatetime.prototype.until
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalDuration> Until(
      Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> date_time,
      DirectHandle<Object> other, DirectHandle<Object> options);

  // #sec-temporal.zoneddatetime.prototype.since
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalDuration> Since(
      Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> date_time,
      DirectHandle<Object> other, DirectHandle<Object> options);

  // #sec-temporal.zoneddatetime.prototype.add
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalZonedDateTime> Add(
      Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
      DirectHandle<Object> temporal_duration_like,
      DirectHandle<Object> options);

  // #sec-temporal.zoneddatetime.prototype.subtract
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalZonedDateTime>
  Subtract(Isolate* isolate,
           DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
           DirectHandle<Object> temporal_duration_like,
           DirectHandle<Object> options);

  // #sec-temporal.zoneddatetime.prototype.getisofields
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSReceiver> GetISOFields(
      Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time);

  // #sec-temporal.zoneddatetime.prototype.toplainyearmonth
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainYearMonth>
  ToPlainYearMonth(Isolate* isolate,
                   DirectHandle<JSTemporalZonedDateTime> zoned_date_time);

  // #sec-temporal.zoneddatetime.prototype.toplainmonthday
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainMonthDay>
  ToPlainMonthDay(Isolate* isolate,
                  DirectHandle<JSTemporalZonedDateTime> zoned_date_time);

  // #sec-temporal.now.zoneddatetime
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalZonedDateTime> Now(
      Isolate* isolate, DirectHandle<Object> calendar_like,
      DirectHandle<Object> temporal_time_zone_like);

  // #sec-temporal.now.zoneddatetimeiso
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalZonedDateTime>
  NowISO(Isolate* isolate, DirectHandle<Object> temporal_time_zone_like);

  // #sec-get-temporal.zoneddatetime.prototype.offsetnanoseconds
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Object> OffsetNanoseconds(
      Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time);

  // #sec-get-temporal.zoneddatetime.prototype.offset
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> Offset(
      Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time);

  // #sec-temporal.zoneddatetime.prototype.startofday
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalZonedDateTime>
  StartOfDay(Isolate* isolate,
             DirectHandle<JSTemporalZonedDateTime> zoned_date_time);

  // #sec-temporal.zoneddatetime.prototype.toinstant
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalInstant> ToInstant(
      Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time);

  // #sec-temporal.zoneddatetime.prototype.toplaindate
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainDate>
  ToPlainDate(Isolate* isolate,
              DirectHandle<JSTemporalZonedDateTime> zoned_date_time);

  // #sec-temporal.zoneddatetime.prototype.toplaintime
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainTime>
  ToPlainTime(Isolate* isolate,
              DirectHandle<JSTemporalZonedDateTime> zoned_date_time);

  // #sec-temporal.zoneddatetime.prototype.toplaindatetime
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSTemporalPlainDateTime>
  ToPlainDateTime(Isolate* isolate,
                  DirectHandle<JSTemporalZonedDateTime> zoned_date_time);

  // #sec-temporal.zoneddatetime.prototype.tojson
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> ToJSON(
      Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time);

  // #sec-temporal.zoneddatetime.prototype.tolocalestring
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> ToLocaleString(
      Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
      DirectHandle<Object> locales, DirectHandle<Object> options);

  // #sec-temporal.zoneddatetime.prototype.tostring
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> ToString(
      Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
      DirectHandle<Object> options);

  DECL_PRINTER(JSTemporalZonedDateTime)

  TQ_OBJECT_CONSTRUCTORS(JSTemporalZonedDateTime)
};

namespace temporal {

struct DateRecord {
  int32_t year;
  int32_t month;
  int32_t day;
};

struct TimeRecord {
  int32_t hour;
  int32_t minute;
  int32_t second;
  int32_t millisecond;
  int32_t microsecond;
  int32_t nanosecond;
};

struct DateTimeRecord {
  DateRecord date;
  TimeRecord time;
};

// #sec-temporal-createtemporaldatetime
V8_WARN_UNUSED_RESULT MaybeDirectHandle<JSTemporalPlainDateTime>
CreateTemporalDateTime(Isolate* isolate, const DateTimeRecord& date_time,
                       DirectHandle<JSReceiver> calendar);

// #sec-temporal-createtemporaltimezone
MaybeDirectHandle<JSTemporalTimeZone> CreateTemporalTimeZone(
    Isolate* isolate, DirectHandle<String> identifier);

// #sec-temporal-createtemporalinstant
V8_WARN_UNUSED_RESULT MaybeDirectHandle<JSTemporalInstant>
CreateTemporalInstant(Isolate* isolate, DirectHandle<JSFunction> target,
                      DirectHandle<HeapObject> new_target,
                      DirectHandle<BigInt> epoch_nanoseconds);
V8_WARN_UNUSED_RESULT MaybeDirectHandle<JSTemporalInstant>
CreateTemporalInstant(Isolate* isolate, DirectHandle<BigInt> epoch_nanoseconds);

// #sec-temporal-calendaryear
#define DECLARE_CALENDAR_ABSTRACT_INT_OPERATION(Name)          \
  V8_WARN_UNUSED_RESULT MaybeDirectHandle<Smi> Calendar##Name( \
      Isolate* isolate, DirectHandle<JSReceiver> calendar,     \
      DirectHandle<JSReceiver> date_like);
#define DECLARE_CALENDAR_ABSTRACT_OPERATION(Name)                 \
  V8_WARN_UNUSED_RESULT MaybeDirectHandle<Object> Calendar##Name( \
      Isolate* isolate, DirectHandle<JSReceiver> calendar,        \
      DirectHandle<JSReceiver> date_like);
DECLARE_CALENDAR_ABSTRACT_INT_OPERATION(Year)
DECLARE_CALENDAR_ABSTRACT_INT_OPERATION(Month)
DECLARE_CALENDAR_ABSTRACT_OPERATION(MonthCode)
DECLARE_CALENDAR_ABSTRACT_INT_OPERATION(Day)
DECLARE_CALENDAR_ABSTRACT_OPERATION(DayOfWeek)
DECLARE_CALENDAR_ABSTRACT_OPERATION(DayOfYear)
DECLARE_CALENDAR_ABSTRACT_OPERATION(WeekOfYear)
DECLARE_CALENDAR_ABSTRACT_OPERATION(DaysInWeek)
DECLARE_CALENDAR_ABSTRACT_OPERATION(DaysInMonth)
DECLARE_CALENDAR_ABSTRACT_OPERATION(DaysInYear)
DECLARE_CALENDAR_ABSTRACT_OPERATION(MonthsInYear)
DECLARE_CALENDAR_ABSTRACT_OPERATION(InLeapYear)

#ifdef V8_INTL_SUPPORT
DECLARE_CALENDAR_ABSTRACT_OPERATION(Era)
DECLARE_CALENDAR_ABSTRACT_OPERATION(EraYear)
#endif  //  V8_INTL_SUPPORT

#undef DECLARE_CALENDAR_ABSTRACT_OPERATION

// #sec-temporal-getiso8601calendar
DirectHandle<JSTemporalCalendar> GetISO8601Calendar(Isolate* isolate);

// #sec-temporal-builtintimezonegetplaindatetimefor
V8_WARN_UNUSED_RESULT MaybeDirectHandle<JSTemporalPlainDateTime>
BuiltinTimeZoneGetPlainDateTimeFor(Isolate* isolate,
                                   DirectHandle<JSReceiver> time_zone,
                                   DirectHandle<JSTemporalInstant> instant,
                                   DirectHandle<JSReceiver> calendar,
                                   const char* method_name);

V8_WARN_UNUSED_RESULT MaybeDirectHandle<Object> InvokeCalendarMethod(
    Isolate* isolate, DirectHandle<JSReceiver> calendar,
    DirectHandle<String> name, DirectHandle<JSReceiver> temporal_like);

V8_WARN_UNUSED_RESULT MaybeDirectHandle<JSReceiver> ToTemporalCalendar(
    Isolate* isolate, DirectHandle<Object> temporal_calendar_like,
    const char* method_name);

V8_WARN_UNUSED_RESULT MaybeDirectHandle<JSReceiver> ToTemporalTimeZone(
    Isolate* isolate, DirectHandle<Object> temporal_time_zone_like,
    const char* method_name);

V8_WARN_UNUSED_RESULT MaybeDirectHandle<Oddball> IsInvalidTemporalCalendarField(
    Isolate* isolate, DirectHandle<String> string,
    DirectHandle<FixedArray> field_names);

// #sec-temporal-getbuiltincalendar
V8_WARN_UNUSED_RESULT MaybeDirectHandle<JSTemporalCalendar> GetBuiltinCalendar(
    Isolate* isolate, DirectHandle<String> id);

MaybeDirectHandle<JSTemporalInstant> BuiltinTimeZoneGetInstantForCompatible(
    Isolate* isolate, DirectHandle<JSReceiver> time_zone,
    DirectHandle<JSTemporalPlainDateTime> date_time, const char* method_name);

// For Intl.DurationFormat

// #sec-temporal-time-duration-records
struct TimeDurationRecord {
  double days;
  double hours;
  double minutes;
  double seconds;
  double milliseconds;
  double microseconds;
  double nanoseconds;

  // #sec-temporal-createtimedurationrecord
  static Maybe<TimeDurationRecord> Create(Isolate* isolate, double days,
                                          double hours, double minutes,
                                          double seconds, double milliseconds,
                                          double microseconds,
                                          double nanoseconds);
};

// #sec-temporal-duration-records
// Cannot reuse DateDurationRecord here due to duplicate days.
struct DurationRecord {
  double years;
  double months;
  double weeks;
  TimeDurationRecord time_duration;
  // #sec-temporal-createdurationrecord
  static Maybe<DurationRecord> Create(Isolate* isolate, double years,
                                      double months, double weeks, double days,
                                      double hours, double minutes,
                                      double seconds, double milliseconds,
                                      double microseconds, double nanoseconds);

  static int32_t Sign(const DurationRecord& dur);
};

// #sec-temporal-topartialduration
Maybe<DurationRecord> ToPartialDuration(
    Isolate* isolate, DirectHandle<Object> temporal_duration_like_obj,
    const DurationRecord& input);

// #sec-temporal-isvalidduration
bool IsValidDuration(Isolate* isolate, const DurationRecord& dur);

}  // namespace temporal
}  // namespace internal
}  // namespace v8
#include "src/objects/object-macros-undef.h"
#endif  // V8_OBJECTS_JS_TEMPORAL_OBJECTS_H_
