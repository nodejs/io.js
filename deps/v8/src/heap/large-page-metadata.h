// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_LARGE_PAGE_METADATA_H_
#define V8_HEAP_LARGE_PAGE_METADATA_H_

#include "src/heap/mutable-page-metadata.h"

namespace v8 {
namespace internal {

class LargePageMetadata : public MutablePageMetadata {
 public:
  // A limit to guarantee that we do not overflow typed slot offset in the old
  // to old remembered set. Note that this limit is higher than what assembler
  // already imposes on x64 and ia32 architectures.
  static constexpr int kMaxCodePageSize = 512 * MB;

  static LargePageMetadata* cast(MutablePageMetadata* metadata) {
    DCHECK_IMPLIES(metadata, metadata->Chunk()->IsLargePage());
    return static_cast<LargePageMetadata*>(metadata);
  }

  static LargePageMetadata* cast(MemoryChunkMetadata* metadata) {
    return cast(MutablePageMetadata::cast(metadata));
  }

  V8_INLINE static LargePageMetadata* FromHeapObject(Tagged<HeapObject> o);

  LargePageMetadata(Heap* heap, BaseSpace* space, size_t chunk_size,
                    Address area_start, Address area_end,
                    VirtualMemory reservation, Executability executable);

  MemoryChunk::MainThreadFlags InitialFlags(Executability executable) const;

  Tagged<HeapObject> GetObject() const {
    return HeapObject::FromAddress(area_start());
  }

  LargePageMetadata* next_page() {
    return LargePageMetadata::cast(list_node_.next());
  }
  const LargePageMetadata* next_page() const {
    return static_cast<const LargePageMetadata*>(list_node_.next());
  }

  void ClearOutOfLiveRangeSlots(Address free_start);

 private:
  friend class MemoryAllocator;
};

}  // namespace internal

namespace base {
// Define special hash function for page pointers, to be used with std data
// structures, e.g. std::unordered_set<LargePageMetadata*,
// base::hash<LargePageMetadata*>
template <>
struct hash<i::LargePageMetadata*> : hash<i::MemoryChunkMetadata*> {};
template <>
struct hash<const i::LargePageMetadata*> : hash<const i::MemoryChunkMetadata*> {
};
}  // namespace base

}  // namespace v8

#endif  // V8_HEAP_LARGE_PAGE_METADATA_H_
