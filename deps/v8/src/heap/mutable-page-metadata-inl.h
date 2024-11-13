// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MUTABLE_PAGE_METADATA_INL_H_
#define V8_HEAP_MUTABLE_PAGE_METADATA_INL_H_

#include "src/heap/memory-chunk-metadata-inl.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/heap/spaces-inl.h"

namespace v8 {
namespace internal {

// static
MutablePageMetadata* MutablePageMetadata::FromAddress(Address a) {
  return cast(MemoryChunkMetadata::FromAddress(a));
}

// static
MutablePageMetadata* MutablePageMetadata::FromHeapObject(Tagged<HeapObject> o) {
  return cast(MemoryChunkMetadata::FromHeapObject(o));
}

void MutablePageMetadata::IncrementExternalBackingStoreBytes(
    ExternalBackingStoreType type, size_t amount) {
#ifndef V8_ENABLE_THIRD_PARTY_HEAP
  base::CheckedIncrement(&external_backing_store_bytes_[static_cast<int>(type)],
                         amount);
  owner()->IncrementExternalBackingStoreBytes(type, amount);
#endif
}

void MutablePageMetadata::DecrementExternalBackingStoreBytes(
    ExternalBackingStoreType type, size_t amount) {
#ifndef V8_ENABLE_THIRD_PARTY_HEAP
  base::CheckedDecrement(&external_backing_store_bytes_[static_cast<int>(type)],
                         amount);
  owner()->DecrementExternalBackingStoreBytes(type, amount);
#endif
}

void MutablePageMetadata::MoveExternalBackingStoreBytes(
    ExternalBackingStoreType type, MutablePageMetadata* from,
    MutablePageMetadata* to, size_t amount) {
  DCHECK_NOT_NULL(from->owner());
  DCHECK_NOT_NULL(to->owner());
  base::CheckedDecrement(
      &(from->external_backing_store_bytes_[static_cast<int>(type)]), amount);
  base::CheckedIncrement(
      &(to->external_backing_store_bytes_[static_cast<int>(type)]), amount);
  Space::MoveExternalBackingStoreBytes(type, from->owner(), to->owner(),
                                       amount);
}

AllocationSpace MutablePageMetadata::owner_identity() const {
  DCHECK_EQ(owner() == nullptr, Chunk()->InReadOnlySpace());
  if (!owner()) return RO_SPACE;
  return owner()->identity();
}

void MutablePageMetadata::SetOldGenerationPageFlags(MarkingMode marking_mode) {
  return Chunk()->SetOldGenerationPageFlags(marking_mode, owner_identity());
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MUTABLE_PAGE_METADATA_INL_H_
