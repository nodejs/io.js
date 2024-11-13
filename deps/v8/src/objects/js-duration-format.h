// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#ifndef V8_OBJECTS_JS_DURATION_FORMAT_H_
#define V8_OBJECTS_JS_DURATION_FORMAT_H_

#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/objects/managed.h"
#include "src/objects/objects.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace U_ICU_NAMESPACE {
class Locale;
namespace number {
class LocalizedNumberFormatter;
}  // namespace number
}  // namespace U_ICU_NAMESPACE

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-duration-format-tq.inc"

class JSDurationFormat
    : public TorqueGeneratedJSDurationFormat<JSDurationFormat, JSObject> {
 public:
  // Creates duration format object with properties derived from input
  // locales and options.
  V8_WARN_UNUSED_RESULT static MaybeHandle<JSDurationFormat> New(
      Isolate* isolate, DirectHandle<Map> map, Handle<Object> locales,
      Handle<Object> options);

  V8_WARN_UNUSED_RESULT static Handle<JSObject> ResolvedOptions(
      Isolate* isolate, DirectHandle<JSDurationFormat> format_holder);

  V8_WARN_UNUSED_RESULT static MaybeHandle<String> Format(
      Isolate* isolate, Handle<JSDurationFormat> df, Handle<Object> duration);

  V8_WARN_UNUSED_RESULT static MaybeHandle<JSArray> FormatToParts(
      Isolate* isolate, Handle<JSDurationFormat> df, Handle<Object> duration);

  V8_EXPORT_PRIVATE static const std::set<std::string>& GetAvailableLocales();

  enum class Display {
    kAuto,
    kAlways,

    kMax = kAlways
  };

  enum class Style {
    kLong,
    kShort,
    kNarrow,
    kDigital,

    kMax = kDigital
  };

  // To optimize per object memory usage, we use 2 bits
  // to store one of the four possible time separators encoded in
  // icu/source/data/locales/$locale/NumberElements/$numberingSystem/timeSeparator
  enum class Separator {
    kColon,                   // U+003A
    kFullStop,                // U+002E
    kFullwidthColon,          // U+FF1A
    kArabicDecimalSeparator,  // U+066B

    kMax = kArabicDecimalSeparator
  };

  // The ordering of these values is significant, because sub-ranges are
  // encoded using bitfields.
  enum class FieldStyle {
    kLong,
    kShort,
    kNarrow,
    kNumeric,
    k2Digit,
    kFractional,
    kUndefined,

    kStyle3Max = kNarrow,
    kStyle4Max = kFractional,
    kStyle5Max = k2Digit,
  };

#define DECLARE_INLINE_SETTER_GETTER(T, n) \
  inline void set_##n(T);                  \
  inline T n() const;

#define DECLARE_INLINE_DISPLAY_SETTER_GETTER(f) \
  DECLARE_INLINE_SETTER_GETTER(Display, f##_display)

#define DECLARE_INLINE_FIELD_STYLE_SETTER_GETTER(f) \
  DECLARE_INLINE_SETTER_GETTER(FieldStyle, f##_style)

  DECLARE_INLINE_DISPLAY_SETTER_GETTER(years)
  DECLARE_INLINE_DISPLAY_SETTER_GETTER(months)
  DECLARE_INLINE_DISPLAY_SETTER_GETTER(weeks)
  DECLARE_INLINE_DISPLAY_SETTER_GETTER(days)
  DECLARE_INLINE_DISPLAY_SETTER_GETTER(hours)
  DECLARE_INLINE_DISPLAY_SETTER_GETTER(minutes)
  DECLARE_INLINE_DISPLAY_SETTER_GETTER(seconds)
  DECLARE_INLINE_DISPLAY_SETTER_GETTER(milliseconds)
  DECLARE_INLINE_DISPLAY_SETTER_GETTER(microseconds)
  DECLARE_INLINE_DISPLAY_SETTER_GETTER(nanoseconds)

  DECLARE_INLINE_SETTER_GETTER(Style, style)
  DECLARE_INLINE_SETTER_GETTER(Separator, separator)

  DECLARE_INLINE_FIELD_STYLE_SETTER_GETTER(years)
  DECLARE_INLINE_FIELD_STYLE_SETTER_GETTER(months)
  DECLARE_INLINE_FIELD_STYLE_SETTER_GETTER(weeks)
  DECLARE_INLINE_FIELD_STYLE_SETTER_GETTER(days)
  DECLARE_INLINE_FIELD_STYLE_SETTER_GETTER(hours)
  DECLARE_INLINE_FIELD_STYLE_SETTER_GETTER(minutes)
  DECLARE_INLINE_FIELD_STYLE_SETTER_GETTER(seconds)
  DECLARE_INLINE_FIELD_STYLE_SETTER_GETTER(milliseconds)
  DECLARE_INLINE_FIELD_STYLE_SETTER_GETTER(microseconds)
  DECLARE_INLINE_FIELD_STYLE_SETTER_GETTER(nanoseconds)

#undef DECLARE_INLINE_SETTER_GETTER
#undef DECLARE_INLINE_STYLE_SETTER_GETTER
#undef DECLARE_INLINE_FIELD_STYLE_SETTER_GETTER

  // Since we store the fractional_digits in 4 bits but only use 0-9 as valid
  // value. We use value 15 (max of 4 bits) to denote "undefined".
  static const uint32_t kUndefinedFractionalDigits = 15;
  inline void set_fractional_digits(int32_t digits);
  inline int32_t fractional_digits() const;

  // Bit positions in |flags|.
  DEFINE_TORQUE_GENERATED_JS_DURATION_FORMAT_DISPLAY_FLAGS()
  DEFINE_TORQUE_GENERATED_JS_DURATION_FORMAT_STYLE_FLAGS()

  static_assert(YearsDisplayBit::is_valid(Display::kMax));
  static_assert(MonthsDisplayBit::is_valid(Display::kMax));
  static_assert(WeeksDisplayBit::is_valid(Display::kMax));
  static_assert(DaysDisplayBit::is_valid(Display::kMax));
  static_assert(HoursDisplayBit::is_valid(Display::kMax));
  static_assert(MinutesDisplayBit::is_valid(Display::kMax));
  static_assert(SecondsDisplayBit::is_valid(Display::kMax));
  static_assert(MillisecondsDisplayBit::is_valid(Display::kMax));
  static_assert(MicrosecondsDisplayBit::is_valid(Display::kMax));
  static_assert(NanosecondsDisplayBit::is_valid(Display::kMax));

  static_assert(StyleBits::is_valid(Style::kMax));

  static_assert(YearsStyleBits::is_valid(FieldStyle::kStyle3Max));
  static_assert(MonthsStyleBits::is_valid(FieldStyle::kStyle3Max));
  static_assert(WeeksStyleBits::is_valid(FieldStyle::kStyle3Max));
  static_assert(DaysStyleBits::is_valid(FieldStyle::kStyle3Max));
  static_assert(HoursStyleBits::is_valid(FieldStyle::kStyle5Max));
  static_assert(MinutesStyleBits::is_valid(FieldStyle::kStyle5Max));
  static_assert(SecondsStyleBits::is_valid(FieldStyle::kStyle5Max));
  static_assert(MillisecondsStyleBits::is_valid(FieldStyle::kStyle4Max));
  static_assert(MicrosecondsStyleBits::is_valid(FieldStyle::kStyle4Max));
  static_assert(NanosecondsStyleBits::is_valid(FieldStyle::kStyle4Max));

  DECL_ACCESSORS(icu_locale, Tagged<Managed<icu::Locale>>)
  DECL_ACCESSORS(icu_number_formatter,
                 Tagged<Managed<icu::number::LocalizedNumberFormatter>>)

  DECL_PRINTER(JSDurationFormat)

  TQ_OBJECT_CONSTRUCTORS(JSDurationFormat)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_DURATION_FORMAT_H_
