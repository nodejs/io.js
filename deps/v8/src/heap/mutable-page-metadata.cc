// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/mutable-page-metadata.h"

#include <new>

#include "src/base/logging.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/platform.h"
#include "src/common/globals.h"
#include "src/heap/incremental-marking.h"
#include "src/heap/marking-state-inl.h"
#include "src/heap/memory-allocator.h"
#include "src/heap/memory-chunk-layout.h"
#include "src/heap/memory-chunk-metadata.h"
#include "src/heap/mutable-page-metadata-inl.h"
#include "src/heap/spaces.h"
#include "src/objects/heap-object.h"

namespace v8::internal {

MutablePageMetadata::MutablePageMetadata(Heap* heap, BaseSpace* space,
                                         size_t chunk_size, Address area_start,
                                         Address area_end,
                                         VirtualMemory reservation,
                                         PageSize page_size)
    : MemoryChunkMetadata(heap, space, chunk_size, area_start, area_end,
                          std::move(reservation)) {
  DCHECK_NE(space->identity(), RO_SPACE);

  if (page_size == PageSize::kRegular) {
    active_system_pages_ = std::make_unique<ActiveSystemPages>();
    active_system_pages_->Init(
        sizeof(MemoryChunk), MemoryAllocator::GetCommitPageSizeBits(), size());
  }

  // We do not track active system pages for large pages and use this fact for
  // `IsLargePage()`.
  DCHECK_EQ(page_size == PageSize::kLarge, IsLargePage());

  // TODO(sroettger): The following fields are accessed most often (AFAICT) and
  // are moved to the end to occupy the same cache line as the slot set array.
  // Without this change, there was a 0.5% performance impact after cache line
  // aligning the metadata on x64 (before, the metadata started at offset 0x10).
  // After reordering, the impact is still 0.1%/0.2% on jetstream2/speedometer3,
  // so there should be some more optimization potential here.
  // TODO(mlippautz): Replace 64 below with
  // `hardware_destructive_interference_size` once supported.
  static constexpr auto kOffsetOfFirstFastField =
      offsetof(MutablePageMetadata, heap_);
  static constexpr auto kOffsetOfLastFastField =
      offsetof(MutablePageMetadata, slot_set_) +
      sizeof(SlotSet*) * RememberedSetType::OLD_TO_NEW;
  // This assert is merely necessary but not sufficient to guarantee that the
  // fields sit on the same cacheline as the metadata object itself is
  // dynamically allocated without alignment restrictions.
  static_assert(kOffsetOfFirstFastField / 64 == kOffsetOfLastFastField / 64);
}

MemoryChunk::MainThreadFlags MutablePageMetadata::InitialFlags(
    Executability executable) const {
  MemoryChunk::MainThreadFlags flags = MemoryChunk::NO_FLAGS;

  if (owner()->identity() == NEW_SPACE || owner()->identity() == NEW_LO_SPACE) {
    flags |= MemoryChunk::YoungGenerationPageFlags(
        heap()->incremental_marking()->marking_mode());
  } else {
    flags |= MemoryChunk::OldGenerationPageFlags(
        heap()->incremental_marking()->marking_mode(), owner()->identity());
  }

  if (executable == EXECUTABLE) {
    flags |= MemoryChunk::IS_EXECUTABLE;
    // Executable chunks are also trusted as they contain machine code and live
    // outside the sandbox (when it is enabled). While mostly symbolic, this is
    // needed for two reasons:
    // 1. We have the invariant that IsTrustedObject(obj) implies
    //    IsTrustedSpaceObject(obj), where IsTrustedSpaceObject checks the
    //   MemoryChunk::IS_TRUSTED flag on the host chunk. As InstructionStream
    //   objects are
    //    trusted, their host chunks must also be marked as such.
    // 2. References between trusted objects must use the TRUSTED_TO_TRUSTED
    //    remembered set. However, that will only be used if both the host
    //    and the value chunk are marked as IS_TRUSTED.
    flags |= MemoryChunk::IS_TRUSTED;
  }

  // All pages of a shared heap need to be marked with this flag.
  if (InSharedSpace()) {
    flags |= MemoryChunk::IN_WRITABLE_SHARED_SPACE;
  }

  // All pages belonging to a trusted space need to be marked with this flag.
  if (InTrustedSpace()) {
    flags |= MemoryChunk::IS_TRUSTED;
  }

  // "Trusted" chunks should never be located inside the sandbox as they
  // couldn't be trusted in that case.
  DCHECK_IMPLIES(flags & MemoryChunk::IS_TRUSTED,
                 !InsideSandbox(ChunkAddress()));

  return flags;
}

size_t MutablePageMetadata::CommittedPhysicalMemory() const {
  if (!base::OS::HasLazyCommits() || Chunk()->IsLargePage()) return size();
  return active_system_pages_->Size(MemoryAllocator::GetCommitPageSizeBits());
}

// -----------------------------------------------------------------------------
// MutablePageMetadata implementation

void MutablePageMetadata::ReleaseAllocatedMemoryNeededForWritableChunk() {
  DCHECK(SweepingDone());

  active_system_pages_.reset();

  possibly_empty_buckets_.Release();
  ReleaseSlotSet(OLD_TO_NEW);
  ReleaseSlotSet(OLD_TO_NEW_BACKGROUND);
  ReleaseSlotSet(OLD_TO_OLD);
  ReleaseSlotSet(TRUSTED_TO_CODE);
  ReleaseSlotSet(OLD_TO_SHARED);
  ReleaseSlotSet(TRUSTED_TO_TRUSTED);
  ReleaseSlotSet(TRUSTED_TO_SHARED_TRUSTED);
  ReleaseSlotSet(SURVIVOR_TO_EXTERNAL_POINTER);
  ReleaseTypedSlotSet(OLD_TO_NEW);
  ReleaseTypedSlotSet(OLD_TO_OLD);
  ReleaseTypedSlotSet(OLD_TO_SHARED);

  if (!Chunk()->IsLargePage()) {
    PageMetadata* page = static_cast<PageMetadata*>(this);
    page->ReleaseFreeListCategories();
  }
}

void MutablePageMetadata::ReleaseAllAllocatedMemory() {
  ReleaseAllocatedMemoryNeededForWritableChunk();
}

SlotSet* MutablePageMetadata::AllocateSlotSet(RememberedSetType type) {
  SlotSet* new_slot_set = SlotSet::Allocate(BucketsInSlotSet());
  SlotSet* old_slot_set = base::AsAtomicPointer::AcquireRelease_CompareAndSwap(
      &slot_set_[type], nullptr, new_slot_set);
  if (old_slot_set) {
    SlotSet::Delete(new_slot_set);
    new_slot_set = old_slot_set;
  }
  DCHECK_NOT_NULL(new_slot_set);
  return new_slot_set;
}

void MutablePageMetadata::ReleaseSlotSet(RememberedSetType type) {
  SlotSet* slot_set = slot_set_[type];
  if (slot_set) {
    slot_set_[type] = nullptr;
    SlotSet::Delete(slot_set);
  }
}

TypedSlotSet* MutablePageMetadata::AllocateTypedSlotSet(
    RememberedSetType type) {
  TypedSlotSet* typed_slot_set = new TypedSlotSet(ChunkAddress());
  TypedSlotSet* old_value = base::AsAtomicPointer::Release_CompareAndSwap(
      &typed_slot_set_[type], nullptr, typed_slot_set);
  if (old_value) {
    delete typed_slot_set;
    typed_slot_set = old_value;
  }
  DCHECK(typed_slot_set);
  return typed_slot_set;
}

void MutablePageMetadata::ReleaseTypedSlotSet(RememberedSetType type) {
  TypedSlotSet* typed_slot_set = typed_slot_set_[type];
  if (typed_slot_set) {
    typed_slot_set_[type] = nullptr;
    delete typed_slot_set;
  }
}

bool MutablePageMetadata::ContainsAnySlots() const {
  for (int rs_type = 0; rs_type < NUMBER_OF_REMEMBERED_SET_TYPES; rs_type++) {
    if (slot_set_[rs_type] || typed_slot_set_[rs_type]) {
      return true;
    }
  }
  return false;
}

int MutablePageMetadata::ComputeFreeListsLength() {
  int length = 0;
  for (int cat = kFirstCategory; cat <= owner()->free_list()->last_category();
       cat++) {
    if (categories_[cat] != nullptr) {
      length += categories_[cat]->FreeListLength();
    }
  }
  return length;
}

bool MutablePageMetadata::IsLivenessClear() const {
  CHECK_IMPLIES(marking_bitmap()->IsClean(), live_bytes() == 0);
  return marking_bitmap()->IsClean();
}

}  // namespace v8::internal
