// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MEMORY_CHUNK_LAYOUT_H_
#define V8_HEAP_MEMORY_CHUNK_LAYOUT_H_

#include "src/common/globals.h"
#include "src/heap/memory-chunk.h"
#include "src/objects/instruction-stream.h"

namespace v8::internal {

class V8_EXPORT_PRIVATE MemoryChunkLayout final {
 public:
  // Code pages have padding on the first page for code alignment, so the
  // ObjectStartOffset will not be page aligned.
  static constexpr intptr_t ObjectStartOffsetInCodePage() {
    // The instruction stream data (so after the header) should be aligned to
    // kCodeAlignment.
    return RoundUp(sizeof(MemoryChunk) + InstructionStream::kHeaderSize,
                   kCodeAlignment) -
           InstructionStream::kHeaderSize;
  }

  static constexpr size_t AllocatableMemoryInCodePage() {
    return kRegularPageSize - ObjectStartOffsetInCodePage();
  }

  static constexpr size_t ObjectStartOffsetInDataPage() {
    return RoundUp(sizeof(MemoryChunk),
                   ALIGN_TO_ALLOCATION_ALIGNMENT(kDoubleSize));
  }

  static constexpr size_t AllocatableMemoryInDataPage() {
    constexpr size_t kAllocatableMemoryInDataPage =
        kRegularPageSize - ObjectStartOffsetInDataPage();
    static_assert(kMaxRegularHeapObjectSize <= kAllocatableMemoryInDataPage);
    return kAllocatableMemoryInDataPage;
  }

  static constexpr size_t ObjectStartOffsetInMemoryChunk(
      AllocationSpace space) {
    if (IsAnyCodeSpace(space)) {
      return ObjectStartOffsetInCodePage();
    }
    // Read-only pages use the same layout as regular pages.
    return ObjectStartOffsetInDataPage();
  }

  static constexpr size_t AllocatableMemoryInMemoryChunk(
      AllocationSpace space) {
    DCHECK_NE(space, CODE_LO_SPACE);
    if (space == CODE_SPACE) {
      return AllocatableMemoryInCodePage();
    }
    // Read-only pages use the same layout as regular pages.
    return AllocatableMemoryInDataPage();
  }

  static constexpr int MaxRegularCodeObjectSize() {
    constexpr int kMaxRegularCodeObjectSize = static_cast<int>(
        RoundDown(AllocatableMemoryInCodePage() / 2, kTaggedSize));
    static_assert(kMaxRegularCodeObjectSize <= kMaxRegularHeapObjectSize);
    return kMaxRegularCodeObjectSize;
  }
};

}  // namespace v8::internal

#endif  // V8_HEAP_MEMORY_CHUNK_LAYOUT_H_
