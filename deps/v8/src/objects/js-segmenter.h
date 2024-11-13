// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef V8_OBJECTS_JS_SEGMENTER_H_
#define V8_OBJECTS_JS_SEGMENTER_H_

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

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
class BreakIterator;
}  // namespace U_ICU_NAMESPACE

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-segmenter-tq.inc"

class JSSegmenter : public TorqueGeneratedJSSegmenter<JSSegmenter, JSObject> {
 public:
  // Creates segmenter object with properties derived from input locales and
  // options.
  V8_WARN_UNUSED_RESULT static MaybeHandle<JSSegmenter> New(
      Isolate* isolate, DirectHandle<Map> map, Handle<Object> locales,
      Handle<Object> options);

  V8_WARN_UNUSED_RESULT static Handle<JSObject> ResolvedOptions(
      Isolate* isolate, DirectHandle<JSSegmenter> segmenter_holder);

  V8_EXPORT_PRIVATE static const std::set<std::string>& GetAvailableLocales();

  Handle<String> GranularityAsString(Isolate* isolate) const;

  // Segmenter accessors.
  DECL_ACCESSORS(icu_break_iterator, Tagged<Managed<icu::BreakIterator>>)

  // Granularity: identifying the segmenter used.
  //
  // ecma402 #sec-segmenter-internal-slots
  enum class Granularity {
    GRAPHEME,  // for character-breaks
    WORD,      // for word-breaks
    SENTENCE   // for sentence-breaks
  };
  inline void set_granularity(Granularity granularity);
  inline Granularity granularity() const;

  Handle<String> static GetGranularityString(Isolate* isolate,
                                             Granularity granularity);

  // Bit positions in |flags|.
  DEFINE_TORQUE_GENERATED_JS_SEGMENTER_FLAGS()

  static_assert(Granularity::GRAPHEME <= GranularityBits::kMax);
  static_assert(Granularity::WORD <= GranularityBits::kMax);
  static_assert(Granularity::SENTENCE <= GranularityBits::kMax);

  DECL_PRINTER(JSSegmenter)

  TQ_OBJECT_CONSTRUCTORS(JSSegmenter)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_SEGMENTER_H_
