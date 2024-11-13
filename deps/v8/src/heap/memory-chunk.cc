// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/memory-chunk.h"

#include "src/common/code-memory-access-inl.h"
#include "src/heap/base-space.h"
#include "src/heap/large-page-metadata.h"
#include "src/heap/page-metadata.h"
#include "src/heap/read-only-spaces.h"
#include "src/heap/trusted-range.h"

namespace v8 {
namespace internal {

// static
constexpr MemoryChunk::MainThreadFlags MemoryChunk::kAllFlagsMask;
// static
constexpr MemoryChunk::MainThreadFlags
    MemoryChunk::kPointersToHereAreInterestingMask;
// static
constexpr MemoryChunk::MainThreadFlags
    MemoryChunk::kPointersFromHereAreInterestingMask;
// static
constexpr MemoryChunk::MainThreadFlags MemoryChunk::kEvacuationCandidateMask;
// static
constexpr MemoryChunk::MainThreadFlags MemoryChunk::kIsInYoungGenerationMask;
// static
constexpr MemoryChunk::MainThreadFlags MemoryChunk::kIsLargePageMask;
// static
constexpr MemoryChunk::MainThreadFlags
    MemoryChunk::kSkipEvacuationSlotsRecordingMask;

MemoryChunk::MemoryChunk(MainThreadFlags flags, MemoryChunkMetadata* metadata)
    : main_thread_flags_(flags),
#ifdef V8_ENABLE_SANDBOX
      metadata_index_(MetadataTableIndex(address()))
#else
      metadata_(metadata)
#endif
{
#ifdef V8_ENABLE_SANDBOX
  DCHECK_IMPLIES(metadata_pointer_table_[metadata_index_] != nullptr,
                 metadata_pointer_table_[metadata_index_] == metadata);
  metadata_pointer_table_[metadata_index_] = metadata;
#endif
}

#ifdef V8_ENABLE_SANDBOX

MemoryChunkMetadata* MemoryChunk::metadata_pointer_table_[] = {nullptr};

// static
void MemoryChunk::ClearMetadataPointer(MemoryChunkMetadata* metadata) {
  uint32_t metadata_index = MetadataTableIndex(metadata->ChunkAddress());
  DCHECK_EQ(metadata_pointer_table_[metadata_index], metadata);
  metadata_pointer_table_[metadata_index] = nullptr;
}

// static
uint32_t MemoryChunk::MetadataTableIndex(Address chunk_address) {
  uint32_t index;
  if (V8HeapCompressionScheme::GetPtrComprCageBaseAddress(chunk_address) ==
      V8HeapCompressionScheme::base()) {
    static_assert(kPtrComprCageReservationSize == kPtrComprCageBaseAlignment);
    Tagged_t offset = V8HeapCompressionScheme::CompressAny(chunk_address);
    DCHECK_LT(offset >> kPageSizeBits, kPagesInMainCage);
    index = kMainCageMetadataOffset + (offset >> kPageSizeBits);
  } else if (TrustedRange::GetProcessWideTrustedRange()->region().contains(
                 chunk_address)) {
    Tagged_t offset = TrustedSpaceCompressionScheme::CompressAny(chunk_address);
    DCHECK_LT(offset >> kPageSizeBits, kPagesInTrustedCage);
    index = kTrustedSpaceMetadataOffset + (offset >> kPageSizeBits);
  } else {
    CodeRange* code_range = IsolateGroup::current()->GetCodeRange();
    DCHECK(code_range->region().contains(chunk_address));
    uint32_t offset = static_cast<uint32_t>(chunk_address - code_range->base());
    DCHECK_LT(offset >> kPageSizeBits, kPagesInCodeCage);
    index = kCodeRangeMetadataOffset + (offset >> kPageSizeBits);
  }
  DCHECK_LT(index, kMetadataPointerTableSize);
  return index;
}

#endif

void MemoryChunk::InitializationMemoryFence() {
  base::SeqCst_MemoryFence();

#ifdef THREAD_SANITIZER
  // Since TSAN does not process memory fences, we use the following annotation
  // to tell TSAN that there is no data race when emitting a
  // InitializationMemoryFence. Note that the other thread still needs to
  // perform MutablePageMetadata::synchronized_heap().
  Metadata()->SynchronizedHeapStore();
#ifndef V8_ENABLE_SANDBOX
  base::Release_Store(reinterpret_cast<base::AtomicWord*>(&metadata_),
                      reinterpret_cast<base::AtomicWord>(metadata_));
#else
  static_assert(sizeof(base::AtomicWord) == sizeof(metadata_pointer_table_[0]));
  static_assert(sizeof(base::Atomic32) == sizeof(metadata_index_));
  base::Release_Store(reinterpret_cast<base::AtomicWord*>(
                          &metadata_pointer_table_[metadata_index_]),
                      reinterpret_cast<base::AtomicWord>(
                          metadata_pointer_table_[metadata_index_]));
  base::Release_Store(reinterpret_cast<base::Atomic32*>(&metadata_index_),
                      metadata_index_);
#endif
#endif
}

#ifdef THREAD_SANITIZER

void MemoryChunk::SynchronizedLoad() const {
#ifndef V8_ENABLE_SANDBOX
  MemoryChunkMetadata* metadata = reinterpret_cast<MemoryChunkMetadata*>(
      base::Acquire_Load(reinterpret_cast<base::AtomicWord*>(
          &(const_cast<MemoryChunk*>(this)->metadata_))));
#else
  static_assert(sizeof(base::AtomicWord) == sizeof(metadata_pointer_table_[0]));
  static_assert(sizeof(base::Atomic32) == sizeof(metadata_index_));
  uint32_t metadata_index =
      base::Acquire_Load(reinterpret_cast<base::Atomic32*>(
          &(const_cast<MemoryChunk*>(this)->metadata_index_)));
  MemoryChunkMetadata* metadata = reinterpret_cast<MemoryChunkMetadata*>(
      base::Acquire_Load(reinterpret_cast<base::AtomicWord*>(
          &metadata_pointer_table_[metadata_index])));
#endif
  metadata->SynchronizedHeapLoad();
}

bool MemoryChunk::InReadOnlySpace() const {
  // This is needed because TSAN does not process the memory fence
  // emitted after page initialization.
  SynchronizedLoad();
  return IsFlagSet(READ_ONLY_HEAP);
}

#endif  // THREAD_SANITIZER

#ifdef DEBUG

bool MemoryChunk::IsTrusted() const {
  bool is_trusted = IsFlagSet(IS_TRUSTED);
#if DEBUG
  AllocationSpace id = Metadata()->owner()->identity();
  DCHECK_EQ(is_trusted, IsAnyTrustedSpace(id) || IsAnyCodeSpace(id));
#endif
  return is_trusted;
}

size_t MemoryChunk::Offset(Address addr) const {
  DCHECK_GE(addr, Metadata()->area_start());
  DCHECK_LE(addr, address() + Metadata()->size());
  return addr - address();
}

size_t MemoryChunk::OffsetMaybeOutOfRange(Address addr) const {
  DCHECK_GE(addr, Metadata()->area_start());
  return addr - address();
}

#endif  // DEBUG

void MemoryChunk::SetFlagSlow(Flag flag) {
  if (executable()) {
    RwxMemoryWriteScope scope("Set a MemoryChunk flag in executable memory.");
    SetFlagUnlocked(flag);
  } else {
    SetFlagNonExecutable(flag);
  }
}

void MemoryChunk::ClearFlagSlow(Flag flag) {
  if (executable()) {
    RwxMemoryWriteScope scope("Clear a MemoryChunk flag in executable memory.");
    ClearFlagUnlocked(flag);
  } else {
    ClearFlagNonExecutable(flag);
  }
}

// static
MemoryChunk::MainThreadFlags MemoryChunk::OldGenerationPageFlags(
    MarkingMode marking_mode, AllocationSpace space) {
  MainThreadFlags flags_to_set = NO_FLAGS;

  if (!v8_flags.sticky_mark_bits || (space != OLD_SPACE)) {
    flags_to_set |= MemoryChunk::CONTAINS_ONLY_OLD;
  }

  if (marking_mode == MarkingMode::kMajorMarking) {
    flags_to_set |= MemoryChunk::POINTERS_TO_HERE_ARE_INTERESTING |
                    MemoryChunk::POINTERS_FROM_HERE_ARE_INTERESTING |
                    MemoryChunk::INCREMENTAL_MARKING |
                    MemoryChunk::IS_MAJOR_GC_IN_PROGRESS;
  } else if (IsAnySharedSpace(space)) {
    // We need to track pointers into the SHARED_SPACE for OLD_TO_SHARED.
    flags_to_set |= MemoryChunk::POINTERS_TO_HERE_ARE_INTERESTING;
  } else {
    flags_to_set |= MemoryChunk::POINTERS_FROM_HERE_ARE_INTERESTING;
    if (marking_mode == MarkingMode::kMinorMarking) {
      flags_to_set |= MemoryChunk::INCREMENTAL_MARKING;
    }
  }

  return flags_to_set;
}

// static
MemoryChunk::MainThreadFlags MemoryChunk::YoungGenerationPageFlags(
    MarkingMode marking_mode) {
  MainThreadFlags flags = MemoryChunk::POINTERS_TO_HERE_ARE_INTERESTING;
  if (marking_mode != MarkingMode::kNoMarking) {
    flags |= MemoryChunk::POINTERS_FROM_HERE_ARE_INTERESTING;
    flags |= MemoryChunk::INCREMENTAL_MARKING;
    if (marking_mode == MarkingMode::kMajorMarking) {
      flags |= MemoryChunk::IS_MAJOR_GC_IN_PROGRESS;
    }
  }
  return flags;
}

void MemoryChunk::SetOldGenerationPageFlags(MarkingMode marking_mode,
                                            AllocationSpace space) {
  MainThreadFlags flags_to_set = OldGenerationPageFlags(marking_mode, space);
  MainThreadFlags flags_to_clear = NO_FLAGS;

  if (marking_mode != MarkingMode::kMajorMarking) {
    if (IsAnySharedSpace(space)) {
      // No need to track OLD_TO_NEW or OLD_TO_SHARED within the shared space.
      flags_to_clear |= MemoryChunk::POINTERS_FROM_HERE_ARE_INTERESTING |
                        MemoryChunk::INCREMENTAL_MARKING;
    } else {
      flags_to_clear |= MemoryChunk::POINTERS_TO_HERE_ARE_INTERESTING;
      if (marking_mode != MarkingMode::kMinorMarking) {
        flags_to_clear |= MemoryChunk::INCREMENTAL_MARKING;
      }
    }
  }

  SetFlagsUnlocked(flags_to_set, flags_to_set);
  ClearFlagsUnlocked(flags_to_clear);
}

void MemoryChunk::SetYoungGenerationPageFlags(MarkingMode marking_mode) {
  MainThreadFlags flags_to_set = YoungGenerationPageFlags(marking_mode);
  MainThreadFlags flags_to_clear = NO_FLAGS;

  if (marking_mode == MarkingMode::kNoMarking) {
    flags_to_clear |= MemoryChunk::POINTERS_FROM_HERE_ARE_INTERESTING;
    flags_to_clear |= MemoryChunk::INCREMENTAL_MARKING;
  }

  SetFlagsNonExecutable(flags_to_set, flags_to_set);
  ClearFlagsNonExecutable(flags_to_clear);
}

#ifdef V8_ENABLE_SANDBOX
bool MemoryChunk::SandboxSafeInReadOnlySpace() const {
  // For the sandbox only flags from writable pages can be corrupted so we can
  // use the flag check as a fast path in this case.
  // It also helps making TSAN happy, since it doesn't like the way we
  // initialize the MemoryChunks.
  // (See MemoryChunkMetadata::SynchronizedHeapLoad).
  if (!InReadOnlySpace()) {
    return false;
  }

  // When the sandbox is enabled, only the ReadOnlyPageMetadata are stored
  // inline in the MemoryChunk.
  // ReadOnlyPageMetadata::ChunkAddress() is a special version that boils down
  // to `metadata_address - kMemoryChunkHeaderSize`.
  MemoryChunkMetadata* metadata =
      metadata_pointer_table_[metadata_index_ & kMetadataPointerTableSizeMask];
  SBXCHECK_EQ(
      static_cast<const ReadOnlyPageMetadata*>(metadata)->ChunkAddress(),
      address());

  return true;
}
#endif

}  // namespace internal
}  // namespace v8
