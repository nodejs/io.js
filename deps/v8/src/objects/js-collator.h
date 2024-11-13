// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#ifndef V8_OBJECTS_JS_COLLATOR_H_
#define V8_OBJECTS_JS_COLLATOR_H_

#include <set>
#include <string>

#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/objects/intl-objects.h"
#include "src/objects/js-objects.h"
#include "src/objects/managed.h"
#include "src/objects/objects.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace U_ICU_NAMESPACE {
class Collator;
}  // namespace U_ICU_NAMESPACE

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-collator-tq.inc"

class JSCollator : public TorqueGeneratedJSCollator<JSCollator, JSObject> {
 public:
  // ecma402/#sec-initializecollator
  V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT static MaybeHandle<JSCollator> New(
      Isolate* isolate, DirectHandle<Map> map, Handle<Object> locales,
      Handle<Object> options, const char* service);

  // ecma402/#sec-intl.collator.prototype.resolvedoptions
  static Handle<JSObject> ResolvedOptions(Isolate* isolate,
                                          DirectHandle<JSCollator> collator);

  V8_EXPORT_PRIVATE static const std::set<std::string>& GetAvailableLocales();

  DECL_PRINTER(JSCollator)

  DECL_ACCESSORS(icu_collator, Tagged<Managed<icu::Collator>>)

  TQ_OBJECT_CONSTRUCTORS(JSCollator)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_COLLATOR_H_
