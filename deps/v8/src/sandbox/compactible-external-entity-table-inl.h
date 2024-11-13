// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_COMPACTIBLE_EXTERNAL_ENTITY_TABLE_INL_H_
#define V8_SANDBOX_COMPACTIBLE_EXTERNAL_ENTITY_TABLE_INL_H_

#include "src/logging/counters.h"
#include "src/sandbox/compactible-external-entity-table.h"
#include "src/sandbox/external-entity-table-inl.h"
#include "src/sandbox/external-pointer.h"

#ifdef V8_COMPRESS_POINTERS

namespace v8 {
namespace internal {

template <typename Entry, size_t size>
uint32_t CompactibleExternalEntityTable<Entry, size>::AllocateEntry(
    Space* space) {
  uint32_t index = Base::AllocateEntry(space);

  // When we're compacting a space, we're trying to move all entries above a
  // threshold index (the start of the evacuation area) into segments below
  // that threshold. However, if the freelist becomes too short and we start
  // allocating entries inside the area that is supposed to be evacuated, we
  // need to abort compaction. This is not just an optimization but is also
  // required for correctness: during sweeping we might otherwise assume that
  // all entries inside the evacuation area have been moved and that these
  // segments can therefore be deallocated. In particular, this check will also
  // make sure that we abort compaction if we extend the space with a new
  // segment and allocate at least one entry in it (if that segment is located
  // after the threshold, otherwise it is unproblematic).
  uint32_t start_of_evacuation_area =
      space->start_of_evacuation_area_.load(std::memory_order_relaxed);
  if (V8_UNLIKELY(index >= start_of_evacuation_area)) {
    space->AbortCompacting(start_of_evacuation_area);
  }

  return index;
}

template <typename Entry, size_t size>
typename CompactibleExternalEntityTable<Entry, size>::CompactionResult
CompactibleExternalEntityTable<Entry, size>::FinishCompaction(
    Space* space, Histogram* counter) {
  DCHECK(space->BelongsTo(this));
  DCHECK(!space->is_internal_read_only_space());

  // When compacting, we can compute the number of unused segments at the end of
  // the table and deallocate those after sweeping.
  uint32_t start_of_evacuation_area =
      space->start_of_evacuation_area_.load(std::memory_order_relaxed);
  bool evacuation_was_successful = false;
  if (space->IsCompacting()) {
    auto outcome = ExternalEntityTableCompactionOutcome::kAborted;
    if (space->CompactingWasAborted()) {
      // Compaction was aborted during marking because the freelist grew to
      // short. In this case, it is not guaranteed that any segments will now be
      // completely free.  Extract the original start_of_evacuation_area value.
      start_of_evacuation_area &= ~Space::kCompactionAbortedMarker;
    } else {
      // Entry evacuation was successful so all segments inside the evacuation
      // area are now guaranteed to be free and so can be deallocated.
      evacuation_was_successful = true;
      outcome = ExternalEntityTableCompactionOutcome::kSuccess;
    }
    DCHECK(IsAligned(start_of_evacuation_area,
                     ExternalEntityTable<Entry, size>::kEntriesPerSegment));

    space->StopCompacting();
    counter->AddSample(static_cast<int>(outcome));
  }

  return {start_of_evacuation_area, evacuation_was_successful};
}

template <typename Entry, size_t size>
void CompactibleExternalEntityTable<Entry, size>::MaybeCreateEvacuationEntry(
    Space* space, uint32_t index, Address handle_location) {
  // Check if the entry should be evacuated for table compaction.
  // The current value of the start of the evacuation area is cached in a local
  // variable here as it otherwise may be changed by another marking thread
  // while this method runs, causing non-optimal behaviour (for example, the
  // allocation of an evacuation entry _after_ the entry that is evacuated).
  uint32_t start_of_evacuation_area =
      space->start_of_evacuation_area_.load(std::memory_order_relaxed);
  if (index >= start_of_evacuation_area) {
    DCHECK(space->IsCompacting());
    uint32_t new_index =
        Base::AllocateEntryBelow(space, start_of_evacuation_area);
    if (new_index) {
      DCHECK_LT(new_index, start_of_evacuation_area);
      DCHECK(space->Contains(new_index));
      // Even though the new entry will only be accessed during sweeping, this
      // still needs to be an atomic write as another thread may attempt (and
      // fail) to allocate the same table entry, thereby causing a read from
      // this memory location. Without an atomic store here, TSan would then
      // complain about a data race.
      Base::at(new_index).MakeEvacuationEntry(handle_location);
    } else {
      // In this case, the application has allocated a sufficiently large
      // number of entries from the freelist so that new entries would now be
      // allocated inside the area that is being compacted. While it would be
      // possible to shrink that area and continue compacting, we probably do
      // not want to put more pressure on the freelist and so instead simply
      // abort compaction here. Entries that have already been visited will
      // still be compacted during Sweep, but there is no guarantee that any
      // blocks at the end of the table will now be completely free.
      space->AbortCompacting(start_of_evacuation_area);
    }
  }
}

template <typename Entry, size_t size>
void CompactibleExternalEntityTable<Entry, size>::Space::StartCompacting(
    uint32_t start_of_evacuation_area) {
  DCHECK_EQ(invalidated_fields_.size(), 0);
  start_of_evacuation_area_.store(start_of_evacuation_area,
                                  std::memory_order_relaxed);
}

template <typename Entry, size_t size>
void CompactibleExternalEntityTable<Entry, size>::Space::StopCompacting() {
  start_of_evacuation_area_.store(kNotCompactingMarker,
                                  std::memory_order_relaxed);
}

template <typename Entry, size_t size>
void CompactibleExternalEntityTable<Entry, size>::Space::AbortCompacting(
    uint32_t start_of_evacuation_area) {
  uint32_t compaction_aborted_marker =
      start_of_evacuation_area | kCompactionAbortedMarker;
  DCHECK_NE(compaction_aborted_marker, kNotCompactingMarker);
  start_of_evacuation_area_.store(compaction_aborted_marker,
                                  std::memory_order_relaxed);
}

template <typename Entry, size_t size>
bool CompactibleExternalEntityTable<Entry, size>::Space::IsCompacting() {
  return start_of_evacuation_area_.load(std::memory_order_relaxed) !=
         kNotCompactingMarker;
}

template <typename Entry, size_t size>
bool CompactibleExternalEntityTable<Entry,
                                    size>::Space::CompactingWasAborted() {
  auto value = start_of_evacuation_area_.load(std::memory_order_relaxed);
  return (value & kCompactionAbortedMarker) == kCompactionAbortedMarker;
}

template <typename Entry, size_t size>
bool CompactibleExternalEntityTable<Entry, size>::Space::FieldWasInvalidated(
    Address field_address) const {
  invalidated_fields_mutex_.AssertHeld();
  return std::find(invalidated_fields_.begin(), invalidated_fields_.end(),
                   field_address) != invalidated_fields_.end();
}

template <typename Entry, size_t size>
void CompactibleExternalEntityTable<Entry,
                                    size>::Space::ClearInvalidatedFields() {
  invalidated_fields_mutex_.AssertHeld();
  invalidated_fields_.clear();
}

template <typename Entry, size_t size>
void CompactibleExternalEntityTable<Entry, size>::Space::AddInvalidatedField(
    Address field_address) {
  if (IsCompacting()) {
    base::MutexGuard guard(&invalidated_fields_mutex_);
    invalidated_fields_.push_back(field_address);
  }
}

template <typename Entry, size_t size>
void CompactibleExternalEntityTable<Entry,
                                    size>::Space::StartCompactingIfNeeded() {
  // Take the lock so that we can be sure that no other thread modifies the
  // segments set concurrently.
  base::MutexGuard guard(&this->mutex_);

  // This method may be executed while other threads allocate entries from the
  // freelist. In that case, this method may use incorrect data to determine if
  // table compaction is necessary. That's fine however since in the worst
  // case, compaction will simply be aborted right away if the freelist became
  // too small.
  uint32_t num_free_entries = this->freelist_length();
  uint32_t num_total_entries = this->capacity();

  // Current (somewhat arbitrary) heuristic: need compacting if the space is
  // more than 1MB in size, is at least 10% empty, and if at least one segment
  // can be freed after successful compaction.
  double free_ratio = static_cast<double>(num_free_entries) /
                      static_cast<double>(num_total_entries);
  uint32_t num_segments_to_evacuate =
      (num_free_entries / 2) / Base::kEntriesPerSegment;
  uint32_t space_size = num_total_entries * Base::kEntrySize;
  bool should_compact = (space_size >= 1 * MB) && (free_ratio >= 0.10) &&
                        (num_segments_to_evacuate >= 1);

  // However, if --stress-compaction is enabled, we compact whenever possible:
  // whenever we have at least one segment worth of empty entries.
  if (v8_flags.stress_compaction) {
    should_compact = num_free_entries > Base::kEntriesPerSegment;
  }

  if (should_compact) {
    // If we're compacting, attempt to free up the last N segments so that they
    // can be decommitted afterwards.
    auto first_segment_to_evacuate =
        *std::prev(this->segments_.end(), num_segments_to_evacuate);
    uint32_t start_of_evacuation_area = first_segment_to_evacuate.first_entry();
    StartCompacting(start_of_evacuation_area);
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPRESS_POINTERS

#endif  // V8_SANDBOX_COMPACTIBLE_EXTERNAL_ENTITY_TABLE_INL_H_
