// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_FREE_SPACE_INL_H_
#define V8_OBJECTS_FREE_SPACE_INL_H_

#include "src/execution/isolate.h"
#include "src/heap/heap-write-barrier-inl.h"
#include "src/heap/heap.h"
#include "src/objects/free-space.h"
#include "src/objects/objects-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/free-space-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(FreeSpace)

RELAXED_SMI_ACCESSORS(FreeSpace, size, kSizeOffset)

// static
inline void FreeSpace::SetSize(const WritableFreeSpace& writable_free_space,
                               int size, RelaxedStoreTag tag) {
  writable_free_space.WriteHeaderSlot<Smi, kSizeOffset>(Smi::FromInt(size),
                                                        tag);
}

int FreeSpace::Size() { return size(kRelaxedLoad); }

Tagged<FreeSpace> FreeSpace::next() const {
  DCHECK(IsValid());
#ifdef V8_EXTERNAL_CODE_SPACE
  intptr_t diff_to_next =
      static_cast<intptr_t>(TaggedField<Smi, kNextOffset>::load(*this).value());
  if (diff_to_next == 0) {
    return FreeSpace();
  }
  Address next_ptr = ptr() + diff_to_next * kObjectAlignment;
  return UncheckedCast<FreeSpace>(Tagged<Object>(next_ptr));
#else
  return UncheckedCast<FreeSpace>(
      TaggedField<Object, kNextOffset>::load(*this));
#endif  // V8_EXTERNAL_CODE_SPACE
}

void FreeSpace::SetNext(const WritableFreeSpace& writable_free_space,
                        Tagged<FreeSpace> next) {
  DCHECK(IsValid());

#ifdef V8_EXTERNAL_CODE_SPACE
  if (next.is_null()) {
    writable_free_space.WriteHeaderSlot<Smi, kNextOffset>(Smi::zero(),
                                                          kRelaxedStore);
    return;
  }
  intptr_t diff_to_next = next.ptr() - ptr();
  DCHECK(IsAligned(diff_to_next, kObjectAlignment));
  writable_free_space.WriteHeaderSlot<Smi, kNextOffset>(
      Smi::FromIntptr(diff_to_next / kObjectAlignment), kRelaxedStore);
#else
  writable_free_space.WriteHeaderSlot<Object, kNextOffset>(next, kRelaxedStore);
#endif  // V8_EXTERNAL_CODE_SPACE
}

bool FreeSpace::IsValid() const {
  Heap* heap = GetHeapFromWritableObject(*this);
  Tagged<Object> free_space_map =
      Isolate::FromHeap(heap)->root(RootIndex::kFreeSpaceMap);
  CHECK(!heap->deserialization_complete() ||
        map_slot().contains_map_value(free_space_map.ptr()));
  CHECK_LE(kNextOffset + kTaggedSize, size(kRelaxedLoad));
  return true;
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_FREE_SPACE_INL_H_
