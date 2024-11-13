// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_EXTERNAL_ENTITY_TABLE_INL_H_
#define V8_SANDBOX_EXTERNAL_ENTITY_TABLE_INL_H_

#include "src/base/atomicops.h"
#include "src/base/emulated-virtual-address-subspace.h"
#include "src/base/iterator.h"
#include "src/common/assert-scope.h"
#include "src/common/segmented-table-inl.h"
#include "src/sandbox/external-entity-table.h"
#include "src/utils/allocation.h"

#ifdef V8_COMPRESS_POINTERS

namespace v8 {
namespace internal {

template <typename Entry, size_t size>
ExternalEntityTable<Entry, size>::Space::~Space() {
  // The segments belonging to this space must have already been deallocated
  // (through TearDownSpace()), otherwise we may leak memory.
  DCHECK(segments_.empty());
}

template <typename Entry, size_t size>
uint32_t ExternalEntityTable<Entry, size>::Space::freelist_length() const {
  auto freelist = freelist_head_.load(std::memory_order_relaxed);
  return freelist.length();
}

template <typename Entry, size_t size>
uint32_t ExternalEntityTable<Entry, size>::Space::num_segments() {
  mutex_.AssertHeld();
  return static_cast<uint32_t>(segments_.size());
}

template <typename Entry, size_t size>
bool ExternalEntityTable<Entry, size>::Space::Contains(uint32_t index) {
  base::MutexGuard guard(&mutex_);
  Segment segment = Segment::Containing(index);
  return segments_.find(segment) != segments_.end();
}

template <typename Entry, size_t size>
void ExternalEntityTable<Entry, size>::Initialize() {
  Base::Initialize();

  // Allocate the read-only segment of the table. This segment is always
  // located at offset 0, and contains the null entry (pointing at
  // kNullAddress) at index 0. It may later be temporarily marked read-write,
  // see UnsealedReadOnlySegmentScope.
  Address first_segment = this->vas_->AllocatePages(
      this->vas_->base(), kSegmentSize, kSegmentSize, PagePermissions::kRead);
  if (first_segment != this->vas_->base()) {
    V8::FatalProcessOutOfMemory(
        nullptr,
        "ExternalEntityTable::InitializeTable (first segment allocation)");
  }
  DCHECK_EQ(first_segment - this->vas_->base(), kInternalReadOnlySegmentOffset);
}

template <typename Entry, size_t size>
void ExternalEntityTable<Entry, size>::TearDown() {
  DCHECK(this->is_initialized());

  // Deallocate the (read-only) first segment.
  this->vas_->FreePages(this->vas_->base(), kSegmentSize);

  Base::TearDown();
}

template <typename Entry, size_t size>
void ExternalEntityTable<Entry, size>::InitializeSpace(Space* space) {
#ifdef DEBUG
  DCHECK_EQ(space->owning_table_, nullptr);
  space->owning_table_ = this;
#endif
}

template <typename Entry, size_t size>
void ExternalEntityTable<Entry, size>::TearDownSpace(Space* space) {
  DCHECK(this->is_initialized());
  DCHECK(space->BelongsTo(this));
  for (auto segment : space->segments_) {
    this->FreeTableSegment(segment);
  }
  space->segments_.clear();
}

template <typename Entry, size_t size>
void ExternalEntityTable<Entry, size>::AttachSpaceToReadOnlySegment(
    Space* space) {
  DCHECK(this->is_initialized());
  DCHECK(space->BelongsTo(this));

  DCHECK(!space->is_internal_read_only_space());
  space->is_internal_read_only_space_ = true;

  UnsealReadOnlySegmentScope unseal_scope(this);

  // Physically attach the segment.
  FreelistHead freelist;
  {
    base::MutexGuard guard(&space->mutex_);
    DCHECK_EQ(space->segments_.size(), 0);
    Segment segment = Segment::At(kInternalReadOnlySegmentOffset);
    DCHECK_EQ(segment.first_entry(), kInternalNullEntryIndex);

    // For the internal read-only segment, index 0 is reserved for the `null`
    // entry, so start the freelist at offset 1.
    freelist = Base::InitializeFreeList(segment, 1);

    Extend(space, segment, freelist);
  }

  DCHECK(!freelist.is_empty());
  DCHECK_EQ(freelist.next(), kInternalNullEntryIndex + 1);
  DCHECK(space->Contains(freelist.next()));
}

template <typename Entry, size_t size>
void ExternalEntityTable<Entry, size>::DetachSpaceFromReadOnlySegment(
    Space* space) {
  DCHECK(this->is_initialized());
  DCHECK(space->BelongsTo(this));
  // Remove the RO segment from the space's segment list without freeing it.
  // The table itself manages the RO segment's lifecycle.
  base::MutexGuard guard(&space->mutex_);
  DCHECK_EQ(space->segments_.size(), 1);
  space->segments_.clear();
}

template <typename Entry, size_t size>
void ExternalEntityTable<Entry, size>::UnsealReadOnlySegment() {
  DCHECK(this->is_initialized());
  bool success = this->vas_->SetPagePermissions(
      this->vas_->base(), kSegmentSize, PagePermissions::kReadWrite);
  CHECK(success);
}

template <typename Entry, size_t size>
void ExternalEntityTable<Entry, size>::SealReadOnlySegment() {
  DCHECK(this->is_initialized());
  bool success = this->vas_->SetPagePermissions(
      this->vas_->base(), kSegmentSize, PagePermissions::kRead);
  CHECK(success);
}

template <typename Entry, size_t size>
uint32_t ExternalEntityTable<Entry, size>::AllocateEntry(Space* space) {
  DCHECK(this->is_initialized());
  DCHECK(space->BelongsTo(this));

  // We currently don't want entry allocation to trigger garbage collection as
  // this may cause seemingly harmless pointer field assignments to trigger
  // garbage collection. This is especially true for lazily-initialized
  // external pointer slots which will typically only allocate the external
  // pointer table entry when the pointer is first set to a non-null value.
  DisallowGarbageCollection no_gc;

  FreelistHead freelist;
  bool success = false;
  while (!success) {
    // This is essentially DCLP (see
    // https://preshing.com/20130930/double-checked-locking-is-fixed-in-cpp11/)
    // and so requires an acquire load as well as a release store in Grow() to
    // prevent reordering of memory accesses, which could for example cause one
    // thread to read a freelist entry before it has been properly initialized.
    freelist = space->freelist_head_.load(std::memory_order_acquire);
    if (freelist.is_empty()) {
      // Freelist is empty. Need to take the lock, then attempt to allocate a
      // new segment if no other thread has done it in the meantime.
      base::MutexGuard guard(&space->mutex_);

      // Reload freelist head in case another thread already grew the table.
      freelist = space->freelist_head_.load(std::memory_order_relaxed);

      if (freelist.is_empty()) {
        // Freelist is (still) empty so extend this space by another segment.
        freelist = Extend(space);
        // Extend() adds one segment to the space and so to its freelist.
        DCHECK_EQ(freelist.length(), kEntriesPerSegment);
      }
    }

    success = TryAllocateEntryFromFreelist(space, freelist);
  }

  uint32_t allocated_entry = freelist.next();
  DCHECK(space->Contains(allocated_entry));
  DCHECK_IMPLIES(!space->is_internal_read_only_space(), allocated_entry != 0);
  return allocated_entry;
}

template <typename Entry, size_t size>
uint32_t ExternalEntityTable<Entry, size>::AllocateEntryBelow(
    Space* space, uint32_t threshold_index) {
  DCHECK(this->is_initialized());

  FreelistHead freelist;
  bool success = false;
  while (!success) {
    freelist = space->freelist_head_.load(std::memory_order_acquire);
    // Check that the next free entry is below the threshold.
    if (freelist.is_empty() || freelist.next() >= threshold_index) return 0;

    success = TryAllocateEntryFromFreelist(space, freelist);
  }

  uint32_t allocated_entry = freelist.next();
  DCHECK(space->Contains(allocated_entry));
  DCHECK_NE(allocated_entry, 0);
  DCHECK_LT(allocated_entry, threshold_index);
  return allocated_entry;
}

template <typename Entry, size_t size>
bool ExternalEntityTable<Entry, size>::TryAllocateEntryFromFreelist(
    Space* space, FreelistHead freelist) {
  DCHECK(!freelist.is_empty());
  DCHECK(space->Contains(freelist.next()));

  Entry& freelist_entry = this->at(freelist.next());
  uint32_t next_freelist_entry = freelist_entry.GetNextFreelistEntryIndex();
  FreelistHead new_freelist(next_freelist_entry, freelist.length() - 1);
  bool success = space->freelist_head_.compare_exchange_strong(
      freelist, new_freelist, std::memory_order_relaxed);

  // When the CAS succeeded, the entry must've been a freelist entry.
  // Otherwise, this is not guaranteed as another thread may have allocated
  // and overwritten the same entry in the meantime.
  if (success) {
    DCHECK_IMPLIES(freelist.length() > 1, !new_freelist.is_empty());
    DCHECK_IMPLIES(freelist.length() == 1, new_freelist.is_empty());
  }
  return success;
}

template <typename Entry, size_t size>
typename ExternalEntityTable<Entry, size>::FreelistHead
ExternalEntityTable<Entry, size>::Extend(Space* space) {
  // Freelist should be empty when calling this method.
  DCHECK_EQ(space->freelist_length(), 0);
  // The caller must lock the space's mutex before extending it.
  space->mutex_.AssertHeld();
  // The read-only space must never be extended with a newly-allocated segment.
  DCHECK(!space->is_internal_read_only_space());

  // Allocate the new segment.
  auto [segment, freelist_head] = this->AllocateAndInitializeSegment();
  Extend(space, segment, freelist_head);
  return freelist_head;
}

template <typename Entry, size_t size>
void ExternalEntityTable<Entry, size>::Extend(Space* space, Segment segment,
                                              FreelistHead freelist) {
  // Freelist should be empty when calling this method.
  DCHECK_EQ(space->freelist_length(), 0);
  // The caller must lock the space's mutex before extending it.
  space->mutex_.AssertHeld();

  space->segments_.insert(segment);
  DCHECK_EQ(space->is_internal_read_only_space(), segment.number() == 0);
  DCHECK_EQ(space->is_internal_read_only_space(),
            segment.offset() == kInternalReadOnlySegmentOffset);

  if (V8_UNLIKELY(space->is_internal_read_only_space())) {
    // For the internal read-only segment, index 0 is reserved for the `null`
    // entry. The underlying memory has been nulled by allocation, and is
    // therefore already initialized.
#ifdef DEBUG
    uint32_t first = segment.first_entry();
    CHECK_EQ(first, kInternalNullEntryIndex);
    static constexpr uint8_t kNullBytes[kEntrySize] = {0};
    CHECK_EQ(memcmp(&this->at(first), kNullBytes, kEntrySize), 0);
#endif  // DEBUG
  }

  // This must be a release store to prevent reordering of  of earlier stores to
  // the freelist (for example during initialization of the segment) from being
  // reordered past this store. See AllocateEntry() for more details.
  space->freelist_head_.store(freelist, std::memory_order_release);
}

template <typename Entry, size_t size>
uint32_t ExternalEntityTable<Entry, size>::GenericSweep(Space* space) {
  DCHECK(space->BelongsTo(this));

  // Lock the space. Technically this is not necessary since no other thread can
  // allocate entries at this point, but some of the methods we call on the
  // space assert that the lock is held.
  base::MutexGuard guard(&space->mutex_);

  // There must not be any entry allocations while the table is being swept as
  // that would not be safe. Set the freelist to this special marker value to
  // easily catch any violation of this requirement.
  space->freelist_head_.store(kEntryAllocationIsForbiddenMarker,
                              std::memory_order_relaxed);

  // Here we can iterate over the segments collection without taking a lock
  // because no other thread can currently allocate entries in this space.
  uint32_t current_freelist_head = 0;
  uint32_t current_freelist_length = 0;
  std::vector<Segment> segments_to_deallocate;

  for (auto segment : base::Reversed(space->segments_)) {
    // Remember the state of the freelist before this segment in case this
    // segment turns out to be completely empty and we deallocate it.
    uint32_t previous_freelist_head = current_freelist_head;
    uint32_t previous_freelist_length = current_freelist_length;

    // Process every entry in this segment, again going top to bottom.
    for (WriteIterator it = this->iter_at(segment.last_entry());
         it.index() >= segment.first_entry(); --it) {
      if (!it->IsMarked()) {
        it->MakeFreelistEntry(current_freelist_head);
        current_freelist_head = it.index();
        current_freelist_length++;
      } else {
        it->Unmark();
      }
    }

    // If a segment is completely empty, free it.
    uint32_t free_entries = current_freelist_length - previous_freelist_length;
    bool segment_is_empty = free_entries == kEntriesPerSegment;
    if (segment_is_empty) {
      segments_to_deallocate.push_back(segment);
      // Restore the state of the freelist before this segment.
      current_freelist_head = previous_freelist_head;
      current_freelist_length = previous_freelist_length;
    }
  }

  // We cannot remove the segments while iterating over the segments set, so
  // defer that until now.
  for (auto segment : segments_to_deallocate) {
    // Segment zero is reserved.
    DCHECK_NE(segment.number(), 0);
    this->FreeTableSegment(segment);
    space->segments_.erase(segment);
  }

  FreelistHead new_freelist(current_freelist_head, current_freelist_length);
  space->freelist_head_.store(new_freelist, std::memory_order_release);
  DCHECK_EQ(space->freelist_length(), current_freelist_length);

  uint32_t num_live_entries = space->capacity() - current_freelist_length;
  return num_live_entries;
}

template <typename Entry, size_t size>
template <typename Callback>
void ExternalEntityTable<Entry, size>::IterateEntriesIn(Space* space,
                                                        Callback callback) {
  DCHECK(space->BelongsTo(this));

  base::MutexGuard guard(&space->mutex_);
  for (auto segment : space->segments_) {
    for (uint32_t i = segment.first_entry(); i <= segment.last_entry(); i++) {
      callback(i);
    }
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPRESS_POINTERS

#endif  // V8_SANDBOX_EXTERNAL_ENTITY_TABLE_INL_H_
