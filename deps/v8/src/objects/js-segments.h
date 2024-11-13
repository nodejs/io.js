// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef V8_OBJECTS_JS_SEGMENTS_H_
#define V8_OBJECTS_JS_SEGMENTS_H_

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#include "src/base/bit-field.h"
#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/objects/js-segmenter.h"
#include "src/objects/managed.h"
#include "src/objects/objects.h"
#include "unicode/uversion.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace U_ICU_NAMESPACE {
class BreakIterator;
class UnicodeString;
}  // namespace U_ICU_NAMESPACE

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-segments-tq.inc"

class JSSegments : public TorqueGeneratedJSSegments<JSSegments, JSObject> {
 public:
  // ecma402 #sec-createsegmentsobject
  V8_WARN_UNUSED_RESULT static MaybeHandle<JSSegments> Create(
      Isolate* isolate, DirectHandle<JSSegmenter> segmenter,
      Handle<String> string);

  // ecma402 #sec-%segmentsprototype%.containing
  V8_WARN_UNUSED_RESULT static MaybeHandle<Object> Containing(
      Isolate* isolate, DirectHandle<JSSegments> segments_holder, double n);

  // ecma402 #sec-createsegmentdataobject
  V8_WARN_UNUSED_RESULT static MaybeHandle<JSSegmentDataObject>
  CreateSegmentDataObject(Isolate* isolate,
                          JSSegmenter::Granularity granularity,
                          icu::BreakIterator* break_iterator,
                          DirectHandle<String> input_string,
                          const icu::UnicodeString& unicode_string,
                          int32_t start_index, int32_t end_index);

  Handle<String> GranularityAsString(Isolate* isolate) const;

  // SegmentIterator accessors.
  DECL_ACCESSORS(icu_break_iterator, Tagged<Managed<icu::BreakIterator>>)
  DECL_ACCESSORS(raw_string, Tagged<String>)
  DECL_ACCESSORS(unicode_string, Tagged<Managed<icu::UnicodeString>>)

  DECL_PRINTER(JSSegments)

  inline void set_granularity(JSSegmenter::Granularity granularity);
  inline JSSegmenter::Granularity granularity() const;

  // Bit positions in |flags|.
  DEFINE_TORQUE_GENERATED_JS_SEGMENT_ITERATOR_FLAGS()

  static_assert(JSSegmenter::Granularity::GRAPHEME <= GranularityBits::kMax);
  static_assert(JSSegmenter::Granularity::WORD <= GranularityBits::kMax);
  static_assert(JSSegmenter::Granularity::SENTENCE <= GranularityBits::kMax);

  TQ_OBJECT_CONSTRUCTORS(JSSegments)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_SEGMENTS_H_
