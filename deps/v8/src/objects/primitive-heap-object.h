// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_PRIMITIVE_HEAP_OBJECT_H_
#define V8_OBJECTS_PRIMITIVE_HEAP_OBJECT_H_

#include "src/objects/heap-object.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

// An abstract superclass for classes representing JavaScript primitive values
// other than Smi. It doesn't carry any functionality but allows primitive
// classes to be identified in the type system.
V8_OBJECT class PrimitiveHeapObject : public HeapObjectLayout {
 public:
  DECL_VERIFIER(PrimitiveHeapObject)
} V8_OBJECT_END;

static_assert(sizeof(PrimitiveHeapObject) == sizeof(HeapObjectLayout));
static_assert(is_subtype_v<PrimitiveHeapObject, HeapObject>);

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_PRIMITIVE_HEAP_OBJECT_H_
