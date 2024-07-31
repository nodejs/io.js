// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#include "src/objects/js-segment-iterator.h"

#include <map>
#include <memory>
#include <string>

#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/objects/intl-objects.h"
#include "src/objects/js-segment-iterator-inl.h"
#include "src/objects/js-segments.h"
#include "src/objects/managed-inl.h"
#include "src/objects/objects-inl.h"
#include "unicode/brkiter.h"

namespace v8 {
namespace internal {

Handle<String> JSSegmentIterator::GranularityAsString(Isolate* isolate) const {
  return JSSegmenter::GetGranularityString(isolate, granularity());
}

// ecma402 #sec-createsegmentiterator
MaybeHandle<JSSegmentIterator> JSSegmentIterator::Create(
    Isolate* isolate, Handle<String> input_string,
    icu::BreakIterator* break_iterator, JSSegmenter::Granularity granularity) {
  // Clone a copy for both the ownership and not sharing with containing and
  // other calls to the iterator because icu::BreakIterator keep the iteration
  // position internally and cannot be shared across multiple calls to
  // JSSegmentIterator::Create and JSSegments::Containing.
  break_iterator = break_iterator->clone();
  DCHECK_NOT_NULL(break_iterator);
  Handle<Map> map = Handle<Map>(
      isolate->native_context()->intl_segment_iterator_map(), isolate);

  // 5. Set iterator.[[IteratedStringNextSegmentCodeUnitIndex]] to 0.
  break_iterator->first();
  Handle<Managed<icu::BreakIterator>> managed_break_iterator =
      Managed<icu::BreakIterator>::FromRawPtr(isolate, 0, break_iterator);

  icu::UnicodeString* string = new icu::UnicodeString();
  break_iterator->getText().getText(*string);
  Handle<Managed<icu::UnicodeString>> unicode_string =
      Managed<icu::UnicodeString>::FromRawPtr(isolate, 0, string);

  break_iterator->setText(*string);

  // Now all properties are ready, so we can allocate the result object.
  Handle<JSObject> result = isolate->factory()->NewJSObjectFromMap(map);
  DisallowGarbageCollection no_gc;
  Handle<JSSegmentIterator> segment_iterator =
      Handle<JSSegmentIterator>::cast(result);

  segment_iterator->set_flags(0);
  segment_iterator->set_granularity(granularity);
  segment_iterator->set_icu_break_iterator(*managed_break_iterator);
  segment_iterator->set_raw_string(*input_string);
  segment_iterator->set_unicode_string(*unicode_string);

  return segment_iterator;
}

// ecma402 #sec-%segmentiteratorprototype%.next
MaybeHandle<JSReceiver> JSSegmentIterator::Next(
    Isolate* isolate, Handle<JSSegmentIterator> segment_iterator) {
  Factory* factory = isolate->factory();
  icu::BreakIterator* icu_break_iterator =
      segment_iterator->icu_break_iterator()->raw();
  // 5. Let startIndex be iterator.[[IteratedStringNextSegmentCodeUnitIndex]].
  int32_t start_index = icu_break_iterator->current();
  // 6. Let endIndex be ! FindBoundary(segmenter, string, startIndex, after).
  int32_t end_index = icu_break_iterator->next();

  // 7. If endIndex is not finite, then
  if (end_index == icu::BreakIterator::DONE) {
    // a. Return ! CreateIterResultObject(undefined, true).
    return factory->NewJSIteratorResult(isolate->factory()->undefined_value(),
                                        true);
  }

  // 8. Set iterator.[[IteratedStringNextSegmentCodeUnitIndex]] to endIndex.

  // 9. Let segmentData be ! CreateSegmentDataObject(segmenter, string,
  // startIndex, endIndex).

  Handle<Object> segment_data;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, segment_data,
      JSSegments::CreateSegmentDataObject(
          isolate, segment_iterator->granularity(), icu_break_iterator,
          handle(segment_iterator->raw_string(), isolate),
          *segment_iterator->unicode_string()->raw(), start_index, end_index),
      JSReceiver);

  // 10. Return ! CreateIterResultObject(segmentData, false).
  return factory->NewJSIteratorResult(segment_data, false);
}

}  // namespace internal
}  // namespace v8
