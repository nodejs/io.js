// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#ifndef V8_OBJECTS_JS_BREAK_ITERATOR_H_
#define V8_OBJECTS_JS_BREAK_ITERATOR_H_

#include <set>
#include <string>

#include "src/objects/intl-objects.h"
#include "src/objects/managed.h"
#include "src/objects/objects.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace U_ICU_NAMESPACE {
class BreakIterator;
}  // namespace U_ICU_NAMESPACE

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-break-iterator-tq.inc"

class JSV8BreakIterator
    : public TorqueGeneratedJSV8BreakIterator<JSV8BreakIterator, JSObject> {
 public:
  V8_WARN_UNUSED_RESULT static MaybeHandle<JSV8BreakIterator> New(
      Isolate* isolate, DirectHandle<Map> map, Handle<Object> input_locales,
      Handle<Object> input_options, const char* service);

  static Handle<JSObject> ResolvedOptions(
      Isolate* isolate, DirectHandle<JSV8BreakIterator> break_iterator);

  V8_EXPORT_PRIVATE static const std::set<std::string>& GetAvailableLocales();

  static void AdoptText(Isolate* isolate,
                        DirectHandle<JSV8BreakIterator> break_iterator,
                        Handle<String> text);

  static Handle<Object> Current(Isolate* isolate,
                                DirectHandle<JSV8BreakIterator> break_iterator);
  static Handle<Object> First(Isolate* isolate,
                              DirectHandle<JSV8BreakIterator> break_iterator);
  static Handle<Object> Next(Isolate* isolate,
                             DirectHandle<JSV8BreakIterator> break_iterator);
  static Tagged<String> BreakType(
      Isolate* isolate, DirectHandle<JSV8BreakIterator> break_iterator);

  DECL_PRINTER(JSV8BreakIterator)

  DECL_ACCESSORS(break_iterator, Tagged<Managed<icu::BreakIterator>>)
  DECL_ACCESSORS(unicode_string, Tagged<Managed<icu::UnicodeString>>)

  TQ_OBJECT_CONSTRUCTORS(JSV8BreakIterator)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_BREAK_ITERATOR_H_
