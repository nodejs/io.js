// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_HOLE_INL_H_
#define V8_OBJECTS_HOLE_INL_H_

#include "src/handles/handles.h"
#include "src/heap/heap-write-barrier-inl.h"
#include "src/objects/heap-number-inl.h"
#include "src/objects/hole.h"
#include "src/objects/objects-inl.h"
#include "src/objects/smi-inl.h"
#include "src/objects/tagged-field-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

OBJECT_CONSTRUCTORS_IMPL(Hole, HeapObject)

CAST_ACCESSOR(Hole)

void Hole::set_raw_numeric_value(uint64_t bits) {
  base::WriteUnalignedValue<uint64_t>(field_address(kRawNumericValueOffset),
                                      bits);
}

void Hole::Initialize(Isolate* isolate, Handle<Hole> hole,
                      Handle<HeapNumber> numeric_value) {
  hole->set_raw_numeric_value(numeric_value->value_as_bits());
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_HOLE_INL_H_
