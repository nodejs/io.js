// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_EVACUATION_ALLOCATOR_INL_H_
#define V8_HEAP_EVACUATION_ALLOCATOR_INL_H_

#include "src/common/globals.h"
#include "src/heap/evacuation-allocator.h"
#include "src/heap/spaces-inl.h"

namespace v8 {
namespace internal {

AllocationResult EvacuationAllocator::Allocate(AllocationSpace space,
                                               int object_size,
                                               AllocationAlignment alignment) {
  object_size = ALIGN_TO_ALLOCATION_ALIGNMENT(object_size);
  switch (space) {
    case NEW_SPACE:
      return new_space_allocator()->AllocateRaw(object_size, alignment,
                                                AllocationOrigin::kGC);
    case OLD_SPACE:
      return old_space_allocator()->AllocateRaw(object_size, alignment,
                                                AllocationOrigin::kGC);
    case CODE_SPACE:
      return code_space_allocator()->AllocateRaw(object_size, alignment,
                                                 AllocationOrigin::kGC);
    case SHARED_SPACE:
      return shared_space_allocator()->AllocateRaw(object_size, alignment,
                                                   AllocationOrigin::kGC);
    case TRUSTED_SPACE:
      return trusted_space_allocator()->AllocateRaw(object_size, alignment,
                                                    AllocationOrigin::kGC);
    default:
      UNREACHABLE();
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_EVACUATION_ALLOCATOR_INL_H_
