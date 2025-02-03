// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_IC_IC_INL_H_
#define V8_IC_IC_INL_H_

#include "src/ic/ic.h"

#include "src/codegen/assembler-inl.h"
#include "src/debug/debug.h"
#include "src/execution/frames-inl.h"
#include "src/handles/handles-inl.h"
#include "src/objects/prototype.h"

namespace v8 {
namespace internal {

void IC::update_lookup_start_object_map(DirectHandle<Object> object) {
  if (IsSmi(*object)) {
    lookup_start_object_map_ = isolate_->factory()->heap_number_map();
  } else {
    lookup_start_object_map_ =
        handle(Cast<HeapObject>(*object)->map(), isolate_);
  }
}

bool IC::IsHandler(Tagged<MaybeObject> object) {
  Tagged<HeapObject> heap_object;
  return (IsSmi(object) && (object.ptr() != kNullAddress)) ||
         (object.GetHeapObjectIfWeak(&heap_object) &&
          (IsMap(heap_object) || IsPropertyCell(heap_object) ||
           IsAccessorPair(heap_object))) ||
         (object.GetHeapObjectIfStrong(&heap_object) &&
          (IsDataHandler(heap_object) || IsCode(heap_object)));
}

bool IC::vector_needs_update() {
  if (state() == InlineCacheState::NO_FEEDBACK) return false;
  return (!vector_set_ && (state() != InlineCacheState::MEGAMORPHIC ||
                           nexus()->GetKeyType() != IcCheckType::kElement));
}

}  // namespace internal
}  // namespace v8

#endif  // V8_IC_IC_INL_H_
