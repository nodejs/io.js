// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#ifndef V8_OBJECTS_JS_RELATIVE_TIME_FORMAT_INL_H_
#define V8_OBJECTS_JS_RELATIVE_TIME_FORMAT_INL_H_

#include "src/objects/js-relative-time-format.h"
#include "src/objects/objects-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-relative-time-format-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(JSRelativeTimeFormat)

// Base relative time format accessors.
ACCESSORS(JSRelativeTimeFormat, icu_formatter,
          Tagged<Managed<icu::RelativeDateTimeFormatter>>, kIcuFormatterOffset)

inline void JSRelativeTimeFormat::set_numeric(Numeric numeric) {
  DCHECK(NumericBit::is_valid(numeric));
  int hints = flags();
  hints = NumericBit::update(hints, numeric);
  set_flags(hints);
}

inline JSRelativeTimeFormat::Numeric JSRelativeTimeFormat::numeric() const {
  return NumericBit::decode(flags());
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_RELATIVE_TIME_FORMAT_INL_H_
