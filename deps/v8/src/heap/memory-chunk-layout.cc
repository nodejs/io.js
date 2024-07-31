// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/memory-chunk-layout.h"

#include "src/common/globals.h"
#include "src/heap/marking.h"
#include "src/heap/memory-allocator.h"
#include "src/heap/mutable-page.h"
#include "src/objects/instruction-stream.h"

namespace v8 {
namespace internal {

size_t MemoryChunkLayout::CodePageGuardStartOffset() {
  // We are guarding code pages: the first OS page after the header
  // will be protected as non-writable.
  return ::RoundUp(MutablePageMetadata::kHeaderSize,
                   MemoryAllocator::GetCommitPageSize());
}

size_t MemoryChunkLayout::CodePageGuardSize() {
  return MemoryAllocator::GetCommitPageSize();
}

intptr_t MemoryChunkLayout::ObjectStartOffsetInCodePage() {
  // The first page also includes padding for code alignment.
  return ObjectPageOffsetInCodePage() +
         InstructionStream::kCodeAlignmentMinusCodeHeader;
}

intptr_t MemoryChunkLayout::ObjectPageOffsetInCodePage() {
  // We are guarding code pages: the first OS page after the header
  // will be protected as non-writable.
  return CodePageGuardStartOffset() + CodePageGuardSize();
}

intptr_t MemoryChunkLayout::ObjectEndOffsetInCodePage() {
  // We are guarding code pages: the last OS page will be protected as
  // non-writable.
  return MutablePageMetadata::kPageSize -
         static_cast<int>(MemoryAllocator::GetCommitPageSize());
}

size_t MemoryChunkLayout::AllocatableMemoryInCodePage() {
  size_t memory = ObjectEndOffsetInCodePage() - ObjectStartOffsetInCodePage();
  return memory;
}

size_t MemoryChunkLayout::ObjectStartOffsetInDataPage() {
  return RoundUp(MutablePageMetadata::kHeaderSize,
                 ALIGN_TO_ALLOCATION_ALIGNMENT(kDoubleSize));
}

intptr_t MemoryChunkLayout::ObjectStartOffsetInReadOnlyPage() {
  return RoundUp(
      static_cast<size_t>(MemoryChunkLayout::kBasicMemoryChunkHeaderSize),
      ALIGN_TO_ALLOCATION_ALIGNMENT(kDoubleSize));
}

size_t MemoryChunkLayout::ObjectStartOffsetInMemoryChunk(
    AllocationSpace space) {
  if (IsAnyCodeSpace(space)) {
    return ObjectStartOffsetInCodePage();
  }
  if (space == RO_SPACE) {
    return ObjectStartOffsetInReadOnlyPage();
  }
  return ObjectStartOffsetInDataPage();
}

size_t MemoryChunkLayout::AllocatableMemoryInDataPage() {
  size_t memory =
      MutablePageMetadata::kPageSize - ObjectStartOffsetInDataPage();
  DCHECK_LE(kMaxRegularHeapObjectSize, memory);
  return memory;
}

size_t MemoryChunkLayout::AllocatableMemoryInReadOnlyPage() {
  size_t memory =
      MutablePageMetadata::kPageSize - ObjectStartOffsetInReadOnlyPage();
  DCHECK_LE(kMaxRegularHeapObjectSize, memory);
  return memory;
}

size_t MemoryChunkLayout::AllocatableMemoryInMemoryChunk(
    AllocationSpace space) {
  if (space == CODE_SPACE) {
    return AllocatableMemoryInCodePage();
  }
  if (space == RO_SPACE) {
    return AllocatableMemoryInReadOnlyPage();
  }
  return AllocatableMemoryInDataPage();
}

int MemoryChunkLayout::MaxRegularCodeObjectSize() {
  int size = static_cast<int>(
      RoundDown(AllocatableMemoryInCodePage() / 2, kTaggedSize));
  DCHECK_LE(size, kMaxRegularHeapObjectSize);
  return size;
}

}  // namespace internal
}  // namespace v8
