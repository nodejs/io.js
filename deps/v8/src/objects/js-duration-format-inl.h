// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#ifndef V8_OBJECTS_JS_DURATION_FORMAT_INL_H_
#define V8_OBJECTS_JS_DURATION_FORMAT_INL_H_

#include "src/objects/js-duration-format.h"
#include "src/objects/objects-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-duration-format-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(JSDurationFormat)

ACCESSORS(JSDurationFormat, icu_locale, Tagged<Managed<icu::Locale>>,
          kIcuLocaleOffset)

#define IMPL_INLINE_SETTER_GETTER(T, n, B, f, M)           \
  inline void JSDurationFormat::set_##n(T value) {         \
    DCHECK_GE(B::kMax, value);                             \
    DCHECK_GE(T::M, value);                                \
    set_##f(B::update(f(), value));                        \
  }                                                        \
  inline JSDurationFormat::T JSDurationFormat::n() const { \
    return B::decode(f());                                 \
  }

#define IMPL_INLINE_DISPLAY_SETTER_GETTER(f, R)                  \
  IMPL_INLINE_SETTER_GETTER(Display, f##_display, R##DisplayBit, \
                            display_flags, kAlways)

#define IMPL_INLINE_FIELD_STYLE3_SETTER_GETTER(f, R)                          \
  IMPL_INLINE_SETTER_GETTER(FieldStyle, f##_style, R##StyleBits, style_flags, \
                            kNarrow)

#define IMPL_INLINE_FIELD_STYLE4_SETTER_GETTER(f, R)                          \
  IMPL_INLINE_SETTER_GETTER(FieldStyle, f##_style, R##StyleBits, style_flags, \
                            kNumeric)

#define IMPL_INLINE_FIELD_STYLE5_SETTER_GETTER(f, R)                          \
  IMPL_INLINE_SETTER_GETTER(FieldStyle, f##_style, R##StyleBits, style_flags, \
                            k2Digit)

IMPL_INLINE_DISPLAY_SETTER_GETTER(years, Years)
IMPL_INLINE_DISPLAY_SETTER_GETTER(months, Months)
IMPL_INLINE_DISPLAY_SETTER_GETTER(weeks, Weeks)
IMPL_INLINE_DISPLAY_SETTER_GETTER(days, Days)
IMPL_INLINE_DISPLAY_SETTER_GETTER(hours, Hours)
IMPL_INLINE_DISPLAY_SETTER_GETTER(minutes, Minutes)
IMPL_INLINE_DISPLAY_SETTER_GETTER(seconds, Seconds)
IMPL_INLINE_DISPLAY_SETTER_GETTER(milliseconds, Milliseconds)
IMPL_INLINE_DISPLAY_SETTER_GETTER(microseconds, Microseconds)
IMPL_INLINE_DISPLAY_SETTER_GETTER(nanoseconds, Nanoseconds)

IMPL_INLINE_SETTER_GETTER(Style, style, StyleBits, style_flags, kDigital)
IMPL_INLINE_SETTER_GETTER(Separator, separator, SeparatorBits, style_flags,
                          kMax)

IMPL_INLINE_FIELD_STYLE3_SETTER_GETTER(years, Years)
IMPL_INLINE_FIELD_STYLE3_SETTER_GETTER(months, Months)
IMPL_INLINE_FIELD_STYLE3_SETTER_GETTER(weeks, Weeks)
IMPL_INLINE_FIELD_STYLE3_SETTER_GETTER(days, Days)
IMPL_INLINE_FIELD_STYLE5_SETTER_GETTER(hours, Hours)
IMPL_INLINE_FIELD_STYLE5_SETTER_GETTER(minutes, Minutes)
IMPL_INLINE_FIELD_STYLE5_SETTER_GETTER(seconds, Seconds)
IMPL_INLINE_FIELD_STYLE4_SETTER_GETTER(milliseconds, Milliseconds)
IMPL_INLINE_FIELD_STYLE4_SETTER_GETTER(microseconds, Microseconds)
IMPL_INLINE_FIELD_STYLE4_SETTER_GETTER(nanoseconds, Nanoseconds)

#undef IMPL_INLINE_SETTER_GETTER
#undef IMPL_INLINE_DISPLAY_SETTER_GETTER
#undef IMPL_INLINE_FIELD_STYLE3_SETTER_GETTER
#undef IMPL_INLINE_FIELD_STYLE5_SETTER_GETTER

inline void JSDurationFormat::set_fractional_digits(int32_t digits) {
  DCHECK((0 <= digits && digits <= 9) || digits == kUndefinedFractionalDigits);
  int hints = display_flags();
  hints = FractionalDigitsBits::update(hints, digits);
  set_display_flags(hints);
}
inline int32_t JSDurationFormat::fractional_digits() const {
  int32_t v = FractionalDigitsBits::decode(display_flags());
  DCHECK((0 <= v && v <= 9) || v == kUndefinedFractionalDigits);
  return v;
}

ACCESSORS(JSDurationFormat, icu_number_formatter,
          Tagged<Managed<icu::number::LocalizedNumberFormatter>>,
          kIcuNumberFormatterOffset)
}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_DURATION_FORMAT_INL_H_
