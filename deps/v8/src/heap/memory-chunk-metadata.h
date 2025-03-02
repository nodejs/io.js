// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MEMORY_CHUNK_METADATA_H_
#define V8_HEAP_MEMORY_CHUNK_METADATA_H_

#include <bit>
#include <type_traits>
#include <unordered_map>

#include "src/base/atomic-utils.h"
#include "src/base/flags.h"
#include "src/base/hashing.h"
#include "src/common/globals.h"
#include "src/flags/flags.h"
#include "src/heap/marking.h"
#include "src/heap/memory-chunk-layout.h"
#include "src/heap/memory-chunk.h"
#include "src/objects/heap-object.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

namespace debug_helper_internal {
class ReadStringVisitor;
}  // namespace  debug_helper_internal

class BaseSpace;

class MemoryChunkMetadata {
 public:
  // Only works if the pointer is in the first kPageSize of the MemoryChunk.
  V8_INLINE static MemoryChunkMetadata* FromAddress(Address a);

  // Only works if the object is in the first kPageSize of the MemoryChunk.
  V8_INLINE static MemoryChunkMetadata* FromHeapObject(Tagged<HeapObject> o);

  // Only works if the object is in the first kPageSize of the MemoryChunk.
  V8_INLINE static MemoryChunkMetadata* FromHeapObject(
      const HeapObjectLayout* o);

  V8_INLINE static void UpdateHighWaterMark(Address mark);

  MemoryChunkMetadata(Heap* heap, BaseSpace* space, size_t chunk_size,
                      Address area_start, Address area_end,
                      VirtualMemory reservation);
  ~MemoryChunkMetadata();

  Address ChunkAddress() const { return Chunk()->address(); }
  Address MetadataAddress() const { return reinterpret_cast<Address>(this); }

  // Returns the offset of a given address to this page.
  inline size_t Offset(Address a) const { return Chunk()->Offset(a); }

  size_t size() const { return size_; }
  void set_size(size_t size) { size_ = size; }

  Address area_start() const { return area_start_; }

  Address area_end() const { return area_end_; }
  void set_area_end(Address area_end) { area_end_ = area_end; }

  size_t area_size() const {
    return static_cast<size_t>(area_end() - area_start());
  }

  Heap* heap() const {
    DCHECK_NOT_NULL(heap_);
    return heap_;
  }

  // Gets the chunk's owner or null if the space has been detached.
  BaseSpace* owner() const { return owner_; }
  void set_owner(BaseSpace* space) { owner_ = space; }

  bool InSharedSpace() const;
  bool InTrustedSpace() const;

  bool IsWritable() const {
    // If this is a read-only space chunk but heap_ is non-null, it has not yet
    // been sealed and can be written to.
    return !Chunk()->InReadOnlySpace() || heap_ != nullptr;
  }

  bool IsMutablePageMetadata() const { return owner() != nullptr; }

  bool Contains(Address addr) const {
    return addr >= area_start() && addr < area_end();
  }

  // Checks whether |addr| can be a limit of addresses in this page. It's a
  // limit if it's in the page, or if it's just after the last byte of the page.
  bool ContainsLimit(Address addr) const {
    return addr >= area_start() && addr <= area_end();
  }

  size_t wasted_memory() const { return wasted_memory_; }
  void add_wasted_memory(size_t waste) { wasted_memory_ += waste; }
  size_t allocated_bytes() const { return allocated_bytes_; }

  Address HighWaterMark() const { return ChunkAddress() + high_water_mark_; }

  VirtualMemory* reserved_memory() { return &reservation_; }

  void ResetAllocationStatistics() {
    allocated_bytes_ = area_size();
    wasted_memory_ = 0;
  }

  void IncreaseAllocatedBytes(size_t bytes) {
    DCHECK_LE(bytes, area_size());
    allocated_bytes_ += bytes;
  }

  void DecreaseAllocatedBytes(size_t bytes) {
    DCHECK_LE(bytes, area_size());
    DCHECK_GE(allocated_bytes(), bytes);
    allocated_bytes_ -= bytes;
  }

  MemoryChunk* Chunk() { return MemoryChunk::FromAddress(area_start()); }
  const MemoryChunk* Chunk() const {
    return MemoryChunk::FromAddress(area_start());
  }

 protected:
#ifdef THREAD_SANITIZER
  // Perform a dummy acquire load to tell TSAN that there is no data race in
  // mark-bit initialization. See MutablePageMetadata::Initialize for the
  // corresponding release store.
  void SynchronizedHeapLoad() const;
  void SynchronizedHeapStore();
  friend class MemoryChunk;
#endif

  // If the chunk needs to remember its memory reservation, it is stored here.
  VirtualMemory reservation_;

  // Byte allocated on the page, which includes all objects on the page and the
  // linear allocation area.
  size_t allocated_bytes_;
  // Freed memory that was not added to the free list.
  size_t wasted_memory_ = 0;

  // Assuming the initial allocation on a page is sequential, count highest
  // number of bytes ever allocated on the page.
  std::atomic<intptr_t> high_water_mark_;

  // Overall size of the chunk, including the header and guards.
  size_t size_;

  Address area_end_;

  // The most accessed fields start at heap_ and end at
  // MutablePageMetadata::slot_set_. See
  // MutablePageMetadata::MutablePageMetadata() for details.

  // The heap this chunk belongs to. May be null for read-only chunks.
  Heap* heap_;

  // Start and end of allocatable memory on this chunk.
  Address area_start_;

  // The space owning this memory chunk.
  std::atomic<BaseSpace*> owner_;

 private:
  static constexpr intptr_t HeapOffset() {
    return offsetof(MemoryChunkMetadata, heap_);
  }

  static constexpr intptr_t AreaStartOffset() {
    return offsetof(MemoryChunkMetadata, area_start_);
  }

  // For HeapOffset().
  friend class debug_helper_internal::ReadStringVisitor;
  // For AreaStartOffset().
  friend class CodeStubAssembler;
  friend class MacroAssembler;
};

}  // namespace internal

namespace base {

// Define special hash function for chunk pointers, to be used with std data
// structures, e.g.
// std::unordered_set<MemoryChunkMetadata*, base::hash<MemoryChunkMetadata*>
// This hash function discards the trailing zero bits (chunk alignment).
// Notice that, when pointer compression is enabled, it also discards the
// cage base.
template <>
struct hash<const i::MemoryChunkMetadata*> {
  V8_INLINE size_t
  operator()(const i::MemoryChunkMetadata* chunk_metadata) const {
    return hash<const i::MemoryChunk*>()(chunk_metadata->Chunk());
  }
};

template <>
struct hash<i::MemoryChunkMetadata*> {
  V8_INLINE size_t operator()(i::MemoryChunkMetadata* chunk_metadata) const {
    return hash<const i::MemoryChunkMetadata*>()(chunk_metadata);
  }
};

}  // namespace base
}  // namespace v8

#endif  // V8_HEAP_MEMORY_CHUNK_METADATA_H_
