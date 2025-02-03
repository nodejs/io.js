// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/memory-chunk-metadata.h"

#include <cstdlib>

#include "src/heap/heap-write-barrier-inl.h"
#include "src/heap/marking-inl.h"
#include "src/objects/heap-object.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

MemoryChunkMetadata::MemoryChunkMetadata(Heap* heap, BaseSpace* space,
                                         size_t chunk_size, Address area_start,
                                         Address area_end,
                                         VirtualMemory reservation)
    : reservation_(std::move(reservation)),
      allocated_bytes_(area_end - area_start),
      high_water_mark_(area_start -
                       MemoryChunk::FromAddress(area_start)->address()),
      size_(chunk_size),
      area_end_(area_end),
      heap_(heap),
      area_start_(area_start),
      owner_(space) {}

MemoryChunkMetadata::~MemoryChunkMetadata() {
#ifdef V8_ENABLE_SANDBOX
  MemoryChunk::ClearMetadataPointer(this);
#endif
}

bool MemoryChunkMetadata::InSharedSpace() const {
  return IsAnySharedSpace(owner()->identity());
}

bool MemoryChunkMetadata::InTrustedSpace() const {
  return IsAnyTrustedSpace(owner()->identity());
}

#ifdef THREAD_SANITIZER
void MemoryChunkMetadata::SynchronizedHeapLoad() const {
  CHECK(reinterpret_cast<Heap*>(
            base::Acquire_Load(reinterpret_cast<base::AtomicWord*>(&(
                const_cast<MemoryChunkMetadata*>(this)->heap_)))) != nullptr ||
        Chunk()->IsFlagSet(MemoryChunk::READ_ONLY_HEAP));
}

void MemoryChunkMetadata::SynchronizedHeapStore() {
  // Since TSAN does not process memory fences, we use the following annotation
  // to tell TSAN that there is no data race when emitting a
  // InitializationMemoryFence. Note that the other thread still needs to
  // perform MutablePageMetadata::synchronized_heap().
  base::Release_Store(reinterpret_cast<base::AtomicWord*>(&heap_),
                      reinterpret_cast<base::AtomicWord>(heap_));
}
#endif

class MemoryChunkValidator {
  // Computed offsets should match the compiler generated ones.
  static_assert(MemoryChunkLayout::kFlagsOffset ==
                offsetof(MemoryChunk, main_thread_flags_));
#ifdef V8_ENABLE_SANDBOX
  static_assert(MemoryChunkLayout::kMetadataIndexOffset ==
                offsetof(MemoryChunk, metadata_index_));
#else
  static_assert(MemoryChunkLayout::kMetadataOffset ==
                offsetof(MemoryChunk, metadata_));
#endif

  static_assert(MemoryChunkLayout::kSizeOffset ==
                offsetof(MemoryChunkMetadata, size_));
  static_assert(MemoryChunkLayout::kHeapOffset ==
                offsetof(MemoryChunkMetadata, heap_));
  static_assert(offsetof(MemoryChunkMetadata, size_) ==
                MemoryChunkLayout::kSizeOffset);
  static_assert(offsetof(MemoryChunkMetadata, heap_) ==
                MemoryChunkLayout::kHeapOffset);
  static_assert(offsetof(MemoryChunkMetadata, area_start_) ==
                MemoryChunkLayout::kAreaStartOffset);
  static_assert(offsetof(MemoryChunkMetadata, area_end_) ==
                MemoryChunkLayout::kAreaEndOffset);
  static_assert(offsetof(MemoryChunkMetadata, allocated_bytes_) ==
                MemoryChunkLayout::kAllocatedBytesOffset);
  static_assert(offsetof(MemoryChunkMetadata, wasted_memory_) ==
                MemoryChunkLayout::kWastedMemoryOffset);
  static_assert(offsetof(MemoryChunkMetadata, high_water_mark_) ==
                MemoryChunkLayout::kHighWaterMarkOffset);
  static_assert(offsetof(MemoryChunkMetadata, owner_) ==
                MemoryChunkLayout::kOwnerOffset);
  static_assert(offsetof(MemoryChunkMetadata, reservation_) ==
                MemoryChunkLayout::kReservationOffset);
};

}  // namespace internal
}  // namespace v8
