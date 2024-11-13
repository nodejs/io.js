// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/mutable-page-metadata.h"

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

namespace v8 {
namespace internal {

void MutablePageMetadata::DiscardUnusedMemory(Address addr, size_t size) {
  base::AddressRegion memory_area =
      MemoryAllocator::ComputeDiscardMemoryArea(addr, size);
  if (memory_area.size() != 0) {
    MemoryAllocator* memory_allocator = heap_->memory_allocator();
    v8::PageAllocator* page_allocator =
        memory_allocator->page_allocator(owner_identity());
    CHECK(page_allocator->DiscardSystemPages(
        reinterpret_cast<void*>(memory_area.begin()), memory_area.size()));
  }
}

MutablePageMetadata::MutablePageMetadata(Heap* heap, BaseSpace* space,
                                         size_t chunk_size, Address area_start,
                                         Address area_end,
                                         VirtualMemory reservation,
                                         PageSize page_size)
    : MemoryChunkMetadata(heap, space, chunk_size, area_start, area_end,
                          std::move(reservation)),
      mutex_(new base::Mutex()),
      shared_mutex_(new base::SharedMutex()),
      page_protection_change_mutex_(new base::Mutex()) {
  DCHECK_NE(space->identity(), RO_SPACE);

  if (page_size == PageSize::kRegular) {
    active_system_pages_ = new ActiveSystemPages;
    active_system_pages_->Init(MemoryChunkLayout::kMemoryChunkHeaderSize,
                               MemoryAllocator::GetCommitPageSizeBits(),
                               size());
  } else {
    // We do not track active system pages for large pages.
    active_system_pages_ = nullptr;
  }

#ifdef DEBUG
  ValidateOffsets(this);
#endif
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
  if (mutex_ != nullptr) {
    delete mutex_;
    mutex_ = nullptr;
  }
  if (shared_mutex_) {
    delete shared_mutex_;
    shared_mutex_ = nullptr;
  }
  if (page_protection_change_mutex_ != nullptr) {
    delete page_protection_change_mutex_;
    page_protection_change_mutex_ = nullptr;
  }

  if (active_system_pages_ != nullptr) {
    delete active_system_pages_;
    active_system_pages_ = nullptr;
  }

  possibly_empty_buckets_.Release();
  ReleaseSlotSet(OLD_TO_NEW);
  ReleaseSlotSet(OLD_TO_NEW_BACKGROUND);
  ReleaseSlotSet(OLD_TO_OLD);
  ReleaseSlotSet(OLD_TO_CODE);
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
  SlotSet* new_slot_set = SlotSet::Allocate(buckets());
  SlotSet* old_slot_set = base::AsAtomicPointer::AcquireRelease_CompareAndSwap(
      &slot_set_[type], nullptr, new_slot_set);
  if (old_slot_set) {
    SlotSet::Delete(new_slot_set, buckets());
    new_slot_set = old_slot_set;
  }
  DCHECK_NOT_NULL(new_slot_set);
  return new_slot_set;
}

void MutablePageMetadata::ReleaseSlotSet(RememberedSetType type) {
  SlotSet* slot_set = slot_set_[type];
  if (slot_set) {
    slot_set_[type] = nullptr;
    SlotSet::Delete(slot_set, buckets());
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

void MutablePageMetadata::ClearLiveness() {
  marking_bitmap()->Clear<AccessMode::NON_ATOMIC>();
  SetLiveBytes(0);
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

#ifdef DEBUG
void MutablePageMetadata::ValidateOffsets(MutablePageMetadata* chunk) {
  // Note that we cannot use offsetof because MutablePageMetadata is not a POD.
  DCHECK_EQ(
      reinterpret_cast<Address>(&chunk->slot_set_) - chunk->MetadataAddress(),
      MemoryChunkLayout::kSlotSetOffset);
  DCHECK_EQ(reinterpret_cast<Address>(&chunk->progress_bar_) -
                chunk->MetadataAddress(),
            MemoryChunkLayout::kProgressBarOffset);
  DCHECK_EQ(reinterpret_cast<Address>(&chunk->live_byte_count_) -
                chunk->MetadataAddress(),
            MemoryChunkLayout::kLiveByteCountOffset);
  DCHECK_EQ(reinterpret_cast<Address>(&chunk->typed_slot_set_) -
                chunk->MetadataAddress(),
            MemoryChunkLayout::kTypedSlotSetOffset);
  DCHECK_EQ(
      reinterpret_cast<Address>(&chunk->mutex_) - chunk->MetadataAddress(),
      MemoryChunkLayout::kMutexOffset);
  DCHECK_EQ(reinterpret_cast<Address>(&chunk->shared_mutex_) -
                chunk->MetadataAddress(),
            MemoryChunkLayout::kSharedMutexOffset);
  DCHECK_EQ(reinterpret_cast<Address>(&chunk->concurrent_sweeping_) -
                chunk->MetadataAddress(),
            MemoryChunkLayout::kConcurrentSweepingOffset);
  DCHECK_EQ(reinterpret_cast<Address>(&chunk->page_protection_change_mutex_) -
                chunk->MetadataAddress(),
            MemoryChunkLayout::kPageProtectionChangeMutexOffset);
  DCHECK_EQ(reinterpret_cast<Address>(&chunk->external_backing_store_bytes_) -
                chunk->MetadataAddress(),
            MemoryChunkLayout::kExternalBackingStoreBytesOffset);
  DCHECK_EQ(
      reinterpret_cast<Address>(&chunk->list_node_) - chunk->MetadataAddress(),
      MemoryChunkLayout::kListNodeOffset);
  DCHECK_EQ(
      reinterpret_cast<Address>(&chunk->categories_) - chunk->MetadataAddress(),
      MemoryChunkLayout::kCategoriesOffset);
  DCHECK_EQ(reinterpret_cast<Address>(&chunk->possibly_empty_buckets_) -
                chunk->MetadataAddress(),
            MemoryChunkLayout::kPossiblyEmptyBucketsOffset);
  DCHECK_EQ(reinterpret_cast<Address>(&chunk->active_system_pages_) -
                chunk->MetadataAddress(),
            MemoryChunkLayout::kActiveSystemPagesOffset);
  DCHECK_EQ(reinterpret_cast<Address>(&chunk->allocated_lab_size_) -
                chunk->MetadataAddress(),
            MemoryChunkLayout::kAllocatedLabSizeOffset);
  DCHECK_EQ(reinterpret_cast<Address>(&chunk->age_in_new_space_) -
                chunk->MetadataAddress(),
            MemoryChunkLayout::kAgeInNewSpaceOffset);
}
#endif

}  // namespace internal
}  // namespace v8
