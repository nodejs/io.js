// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#ifndef V8_OBJECTS_JS_LIST_FORMAT_H_
#define V8_OBJECTS_JS_LIST_FORMAT_H_

#include <set>
#include <string>

#include "src/base/bit-field.h"
#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/objects/managed.h"
#include "src/objects/objects.h"
#include "unicode/uversion.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace U_ICU_NAMESPACE {
class ListFormatter;
}  // namespace U_ICU_NAMESPACE

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-list-format-tq.inc"

class JSListFormat
    : public TorqueGeneratedJSListFormat<JSListFormat, JSObject> {
 public:
  // Creates relative time format object with properties derived from input
  // locales and options.
  static MaybeHandle<JSListFormat> New(Isolate* isolate, DirectHandle<Map> map,
                                       Handle<Object> locales,
                                       Handle<Object> options);

  static Handle<JSObject> ResolvedOptions(
      Isolate* isolate, DirectHandle<JSListFormat> format_holder);

  // ecma402 #sec-formatlist
  V8_WARN_UNUSED_RESULT static MaybeHandle<String> FormatList(
      Isolate* isolate, DirectHandle<JSListFormat> format_holder,
      DirectHandle<FixedArray> list);

  // ecma42 #sec-formatlisttoparts
  V8_WARN_UNUSED_RESULT static MaybeHandle<JSArray> FormatListToParts(
      Isolate* isolate, DirectHandle<JSListFormat> format_holder,
      DirectHandle<FixedArray> list);

  V8_EXPORT_PRIVATE static const std::set<std::string>& GetAvailableLocales();

  Handle<String> StyleAsString() const;
  Handle<String> TypeAsString() const;

  // ListFormat accessors.
  DECL_ACCESSORS(icu_formatter, Tagged<Managed<icu::ListFormatter>>)

  // Style: identifying the relative time format style used.
  //
  // ecma402/#sec-properties-of-intl-listformat-instances
  enum class Style {
    LONG,   // Everything spelled out.
    SHORT,  // Abbreviations used when possible.
    NARROW  // Use the shortest possible form.
  };
  inline void set_style(Style style);
  inline Style style() const;

  // Type: identifying the list of types used.
  //
  // ecma402/#sec-properties-of-intl-listformat-instances
  enum class Type {
    CONJUNCTION,  // for "and"-based lists (e.g., "A, B and C")
    DISJUNCTION,  // for "or"-based lists (e.g., "A, B or C"),
    UNIT  // for lists of values with units (e.g., "5 pounds, 12 ounces").
  };
  inline void set_type(Type type);
  inline Type type() const;

  // Bit positions in |flags|.
  DEFINE_TORQUE_GENERATED_JS_LIST_FORMAT_FLAGS()

  static_assert(Style::LONG <= StyleBits::kMax);
  static_assert(Style::SHORT <= StyleBits::kMax);
  static_assert(Style::NARROW <= StyleBits::kMax);
  static_assert(Type::CONJUNCTION <= TypeBits::kMax);
  static_assert(Type::DISJUNCTION <= TypeBits::kMax);
  static_assert(Type::UNIT <= TypeBits::kMax);

  DECL_PRINTER(JSListFormat)

  TQ_OBJECT_CONSTRUCTORS(JSListFormat)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_LIST_FORMAT_H_
