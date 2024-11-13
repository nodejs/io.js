// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MEMORY_CHUNK_METADATA_INL_H_
#define V8_HEAP_MEMORY_CHUNK_METADATA_INL_H_

#include "src/heap/memory-chunk-inl.h"
#include "src/heap/memory-chunk-metadata.h"

namespace v8 {
namespace internal {

// static
MemoryChunkMetadata* MemoryChunkMetadata::FromAddress(Address a) {
  DCHECK(!V8_ENABLE_THIRD_PARTY_HEAP_BOOL);
  return MemoryChunk::FromAddress(a)->Metadata();
}

// static
MemoryChunkMetadata* MemoryChunkMetadata::FromHeapObject(Tagged<HeapObject> o) {
  DCHECK(!V8_ENABLE_THIRD_PARTY_HEAP_BOOL);
  return FromAddress(o.ptr());
}

// static
MemoryChunkMetadata* MemoryChunkMetadata::FromHeapObject(
    const HeapObjectLayout* o) {
  DCHECK(!V8_ENABLE_THIRD_PARTY_HEAP_BOOL);
  return FromAddress(reinterpret_cast<Address>(o));
}

// static
void MemoryChunkMetadata::UpdateHighWaterMark(Address mark) {
  if (mark == kNullAddress) return;
  // Need to subtract one from the mark because when a chunk is full the
  // top points to the next address after the chunk, which effectively belongs
  // to another chunk. See the comment to
  // PageMetadata::FromAllocationAreaAddress.
  MemoryChunkMetadata* chunk = MemoryChunkMetadata::FromAddress(mark - 1);
  intptr_t new_mark = static_cast<intptr_t>(mark - chunk->ChunkAddress());
  intptr_t old_mark = chunk->high_water_mark_.load(std::memory_order_relaxed);
  while ((new_mark > old_mark) &&
         !chunk->high_water_mark_.compare_exchange_weak(
             old_mark, new_mark, std::memory_order_acq_rel)) {
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MEMORY_CHUNK_METADATA_INL_H_
