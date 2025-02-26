// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_DATE_TIME_FORMAT_H_
#define V8_OBJECTS_JS_DATE_TIME_FORMAT_H_

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#include <set>
#include <string>

#include "src/base/bit-field.h"
#include "src/execution/isolate.h"
#include "src/objects/intl-objects.h"
#include "src/objects/managed.h"
#include "unicode/uversion.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace U_ICU_NAMESPACE {
class DateIntervalFormat;
class Locale;
class SimpleDateFormat;
class TimeZone;
}  // namespace U_ICU_NAMESPACE

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-date-time-format-tq.inc"

class JSDateTimeFormat
    : public TorqueGeneratedJSDateTimeFormat<JSDateTimeFormat, JSObject> {
 public:
  // ecma-402/#sec-todatetimeoptions
  enum class RequiredOption { kDate, kTime, kAny };
  enum class DefaultsOption { kDate, kTime, kAll };

  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSDateTimeFormat> New(
      Isolate* isolate, DirectHandle<Map> map, DirectHandle<Object> locales,
      DirectHandle<Object> options, const char* service);

  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSDateTimeFormat>
  CreateDateTimeFormat(Isolate* isolate, DirectHandle<Map> map,
                       DirectHandle<Object> locales,
                       DirectHandle<Object> options, RequiredOption required,
                       DefaultsOption defaults, const char* service);

  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSObject> ResolvedOptions(
      Isolate* isolate, DirectHandle<JSDateTimeFormat> date_time_format);

  V8_WARN_UNUSED_RESULT static DirectHandle<String> Calendar(
      Isolate* isolate, DirectHandle<JSDateTimeFormat> date_time_format);

  V8_WARN_UNUSED_RESULT static DirectHandle<Object> TimeZone(
      Isolate* isolate, DirectHandle<JSDateTimeFormat> date_time_format);

  // ecma402/#sec-unwrapdatetimeformat
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSDateTimeFormat>
  UnwrapDateTimeFormat(Isolate* isolate, Handle<JSReceiver> format_holder);

  // ecma402/#sec-datetime-format-functions
  // DateTime Format Functions
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> DateTimeFormat(
      Isolate* isolate, DirectHandle<JSDateTimeFormat> date_time_format,
      DirectHandle<Object> date, const char* method_name);

  // ecma402/#sec-Intl.DateTimeFormat.prototype.formatToParts
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSArray> FormatToParts(
      Isolate* isolate, DirectHandle<JSDateTimeFormat> date_time_format,
      DirectHandle<Object> x, bool output_source, const char* method_name);

  // ecma402/#sec-intl.datetimeformat.prototype.formatRange
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> FormatRange(
      Isolate* isolate, DirectHandle<JSDateTimeFormat> date_time_format,
      DirectHandle<Object> x_date_value, DirectHandle<Object> y_date_value,
      const char* method_name);

  // ecma402/sec-Intl.DateTimeFormat.prototype.formatRangeToParts
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSArray> FormatRangeToParts(
      Isolate* isolate, DirectHandle<JSDateTimeFormat> date_time_format,
      DirectHandle<Object> x_date_value, DirectHandle<Object> y_date_value,
      const char* method_name);

  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> ToLocaleDateTime(
      Isolate* isolate, DirectHandle<Object> date, DirectHandle<Object> locales,
      DirectHandle<Object> options, RequiredOption required,
      DefaultsOption defaults, const char* method_name);

  // Function to support Temporal
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> TemporalToLocaleString(
      Isolate* isolate, DirectHandle<JSReceiver> temporal,
      DirectHandle<Object> locales, DirectHandle<Object> options,
      const char* method_name);

  V8_EXPORT_PRIVATE static const std::set<std::string>& GetAvailableLocales();

  DirectHandle<Object> static TimeZoneId(Isolate* isolate,
                                         const icu::TimeZone& tz);
  V8_WARN_UNUSED_RESULT static MaybeHandle<String> TimeZoneIdToString(
      Isolate* isolate, const icu::UnicodeString& id);

  std::unique_ptr<icu::TimeZone> static CreateTimeZone(
      Isolate* isolate, DirectHandle<String> time_zone);

  V8_EXPORT_PRIVATE static std::string CanonicalizeTimeZoneID(
      const std::string& input);

  Handle<String> HourCycleAsString(Isolate* isolate) const;

  // ecma-402/#sec-properties-of-intl-datetimeformat-instances
  enum class DateTimeStyle { kUndefined, kFull, kLong, kMedium, kShort };

  // enum for "hourCycle" option.
  enum class HourCycle { kUndefined, kH11, kH12, kH23, kH24 };

  inline void set_hour_cycle(HourCycle hour_cycle);
  inline HourCycle hour_cycle() const;

  inline void set_date_style(DateTimeStyle date_style);
  inline DateTimeStyle date_style() const;

  inline void set_time_style(DateTimeStyle time_style);
  inline DateTimeStyle time_style() const;

  // Bit positions in |flags|.
  DEFINE_TORQUE_GENERATED_JS_DATE_TIME_FORMAT_FLAGS()

  static_assert(HourCycleBits::is_valid(HourCycle::kUndefined));
  static_assert(HourCycleBits::is_valid(HourCycle::kH11));
  static_assert(HourCycleBits::is_valid(HourCycle::kH12));
  static_assert(HourCycleBits::is_valid(HourCycle::kH23));
  static_assert(HourCycleBits::is_valid(HourCycle::kH24));

  static_assert(DateStyleBits::is_valid(DateTimeStyle::kUndefined));
  static_assert(DateStyleBits::is_valid(DateTimeStyle::kFull));
  static_assert(DateStyleBits::is_valid(DateTimeStyle::kLong));
  static_assert(DateStyleBits::is_valid(DateTimeStyle::kMedium));
  static_assert(DateStyleBits::is_valid(DateTimeStyle::kShort));

  static_assert(TimeStyleBits::is_valid(DateTimeStyle::kUndefined));
  static_assert(TimeStyleBits::is_valid(DateTimeStyle::kFull));
  static_assert(TimeStyleBits::is_valid(DateTimeStyle::kLong));
  static_assert(TimeStyleBits::is_valid(DateTimeStyle::kMedium));
  static_assert(TimeStyleBits::is_valid(DateTimeStyle::kShort));

  DECL_ACCESSORS(icu_locale, Tagged<Managed<icu::Locale>>)
  DECL_ACCESSORS(icu_simple_date_format, Tagged<Managed<icu::SimpleDateFormat>>)
  DECL_ACCESSORS(icu_date_interval_format,
                 Tagged<Managed<icu::DateIntervalFormat>>)

  DECL_PRINTER(JSDateTimeFormat)

  TQ_OBJECT_CONSTRUCTORS(JSDateTimeFormat)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_DATE_TIME_FORMAT_H_
