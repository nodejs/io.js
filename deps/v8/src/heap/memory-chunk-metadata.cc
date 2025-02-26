// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/memory-chunk-metadata.h"

#include <cstdlib>

#include "src/heap/heap-write-barrier-inl.h"
#include "src/heap/marking-inl.h"
#include "src/objects/heap-object.h"
#include "src/utils/allocation.h"

namespace v8::internal {

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

}  // namespace v8::internal
