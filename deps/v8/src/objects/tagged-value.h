// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_TAGGED_VALUE_H_
#define V8_OBJECTS_TAGGED_VALUE_H_

#include "src/objects/objects.h"

#include "include/v8-internal.h"
#include "src/objects/tagged-impl.h"

namespace v8 {
namespace internal {

// Almost same as Object but this one deals with in-heap and potentially
// compressed representation of Objects and provide only limited functionality
// which doesn't require decompression.
class StrongTaggedValue
    : public TaggedImpl<HeapObjectReferenceType::STRONG, Tagged_t> {
 public:
  constexpr StrongTaggedValue() : TaggedImpl() {}
  explicit constexpr StrongTaggedValue(Tagged_t ptr) : TaggedImpl(ptr) {}
  explicit StrongTaggedValue(Tagged<Object> o);

  inline static Tagged<Object> ToObject(Isolate* isolate,
                                        StrongTaggedValue object);
};

// Almost same as Tagged<MaybeObject> but this one deals with in-heap and
// potentially compressed representation of Objects and provide only limited
// functionality which doesn't require decompression.
class TaggedValue : public TaggedImpl<HeapObjectReferenceType::WEAK, Tagged_t> {
 public:
  constexpr TaggedValue() : TaggedImpl() {}
  explicit constexpr TaggedValue(Tagged_t ptr) : TaggedImpl(ptr) {}
  explicit TaggedValue(Tagged<MaybeObject> o);

  inline static Tagged<MaybeObject> ToMaybeObject(Isolate* isolate,
                                                  TaggedValue object);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_TAGGED_VALUE_H_
