// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#ifndef V8_OBJECTS_JS_DISPLAY_NAMES_INL_H_
#define V8_OBJECTS_JS_DISPLAY_NAMES_INL_H_

#include "src/objects/js-display-names.h"
#include "src/objects/objects-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-display-names-tq-inl.inc"

ACCESSORS(JSDisplayNames, internal, Tagged<Managed<DisplayNamesInternal>>,
          kInternalOffset)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSDisplayNames)

inline void JSDisplayNames::set_style(Style style) {
  DCHECK(StyleBits::is_valid(style));
  set_flags(StyleBits::update(flags(), style));
}

inline JSDisplayNames::Style JSDisplayNames::style() const {
  return StyleBits::decode(flags());
}

inline void JSDisplayNames::set_fallback(Fallback fallback) {
  DCHECK(FallbackBit::is_valid(fallback));
  set_flags(FallbackBit::update(flags(), fallback));
}

inline JSDisplayNames::Fallback JSDisplayNames::fallback() const {
  return FallbackBit::decode(flags());
}

inline void JSDisplayNames::set_language_display(
    LanguageDisplay language_display) {
  DCHECK(LanguageDisplayBit::is_valid(language_display));
  set_flags(LanguageDisplayBit::update(flags(), language_display));
}

inline JSDisplayNames::LanguageDisplay JSDisplayNames::language_display()
    const {
  return LanguageDisplayBit::decode(flags());
}
}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_DISPLAY_NAMES_INL_H_
