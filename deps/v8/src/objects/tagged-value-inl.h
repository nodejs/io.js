// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_TAGGED_VALUE_INL_H_
#define V8_OBJECTS_TAGGED_VALUE_INL_H_

#include "src/objects/tagged-value.h"

#include "include/v8-internal.h"
#include "src/common/ptr-compr-inl.h"
#include "src/objects/maybe-object.h"
#include "src/objects/objects.h"
#include "src/objects/oddball.h"
#include "src/objects/tagged-impl-inl.h"
#include "src/roots/roots-inl.h"

namespace v8 {
namespace internal {

inline StrongTaggedValue::StrongTaggedValue(Tagged<Object> o)
    :
#ifdef V8_COMPRESS_POINTERS
      TaggedImpl(CompressionScheme::CompressObject(o.ptr()))
#else
      TaggedImpl(o.ptr())
#endif
{
}

Tagged<Object> StrongTaggedValue::ToObject(Isolate* isolate,
                                           StrongTaggedValue object) {
#ifdef V8_COMPRESS_POINTERS
  return Tagged<Object>(
      CompressionScheme::DecompressTagged(isolate, object.ptr()));
#else
  return Tagged<Object>(object.ptr());
#endif
}

inline TaggedValue::TaggedValue(Tagged<MaybeObject> o)
    :
#ifdef V8_COMPRESS_POINTERS
      TaggedImpl(CompressionScheme::CompressAny(o.ptr()))
#else
      TaggedImpl(o.ptr())
#endif
{
}

Tagged<MaybeObject> TaggedValue::ToMaybeObject(Isolate* isolate,
                                               TaggedValue object) {
#ifdef V8_COMPRESS_POINTERS
  return Tagged<MaybeObject>(
      CompressionScheme::DecompressTagged(isolate, object.ptr()));
#else
  return Tagged<MaybeObject>(object.ptr());
#endif
}

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_TAGGED_VALUE_INL_H_
