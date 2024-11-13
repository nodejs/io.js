// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#ifndef V8_OBJECTS_JS_PLURAL_RULES_H_
#define V8_OBJECTS_JS_PLURAL_RULES_H_

#include <set>
#include <string>

#include "src/base/bit-field.h"
#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/objects/intl-objects.h"
#include "src/objects/managed.h"
#include "src/objects/objects.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace U_ICU_NAMESPACE {
class PluralRules;
namespace number {
class LocalizedNumberFormatter;
class LocalizedNumberRangeFormatter;
}  // namespace number
}  // namespace U_ICU_NAMESPACE

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-plural-rules-tq.inc"

class JSPluralRules
    : public TorqueGeneratedJSPluralRules<JSPluralRules, JSObject> {
 public:
  V8_WARN_UNUSED_RESULT static MaybeHandle<JSPluralRules> New(
      Isolate* isolate, DirectHandle<Map> map, Handle<Object> locales,
      Handle<Object> options);

  static Handle<JSObject> ResolvedOptions(
      Isolate* isolate, DirectHandle<JSPluralRules> plural_rules);

  V8_WARN_UNUSED_RESULT static MaybeHandle<String> ResolvePlural(
      Isolate* isolate, DirectHandle<JSPluralRules> plural_rules,
      double number);

  V8_WARN_UNUSED_RESULT static MaybeHandle<String> ResolvePluralRange(
      Isolate* isolate, DirectHandle<JSPluralRules> plural_rules, double x,
      double y);

  V8_EXPORT_PRIVATE static const std::set<std::string>& GetAvailableLocales();

  // [[Type]] is one of the values "cardinal" or "ordinal",
  // identifying the plural rules used.
  enum class Type { CARDINAL, ORDINAL };
  inline void set_type(Type type);
  inline Type type() const;

  Handle<String> TypeAsString() const;

  DECL_PRINTER(JSPluralRules)

  // Bit positions in |flags|.
  DEFINE_TORQUE_GENERATED_JS_PLURAL_RULES_FLAGS()

  static_assert(Type::CARDINAL <= TypeBit::kMax);
  static_assert(Type::ORDINAL <= TypeBit::kMax);

  DECL_ACCESSORS(icu_plural_rules, Tagged<Managed<icu::PluralRules>>)
  DECL_ACCESSORS(icu_number_formatter,
                 Tagged<Managed<icu::number::LocalizedNumberFormatter>>)

  TQ_OBJECT_CONSTRUCTORS(JSPluralRules)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_PLURAL_RULES_H_
