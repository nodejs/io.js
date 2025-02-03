// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sandbox/external-pointer-table.h"

#include "src/execution/isolate.h"
#include "src/heap/read-only-spaces.h"
#include "src/logging/counters.h"
#include "src/sandbox/external-pointer-table-inl.h"

#ifdef V8_COMPRESS_POINTERS

namespace v8 {
namespace internal {

void ExternalPointerTable::SetUpFromReadOnlyArtifacts(
    Space* read_only_space, const ReadOnlyArtifacts* artifacts) {
  UnsealReadOnlySegmentScope unseal_scope(this);
  for (const auto& registry_entry : artifacts->external_pointer_registry()) {
    ExternalPointerHandle handle = AllocateAndInitializeEntry(
        read_only_space, registry_entry.value, registry_entry.tag);
    CHECK_EQ(handle, registry_entry.handle);
  }
}

// An iterator over a set of sets of segments that returns a total ordering of
// segments in highest to lowest address order.  This lets us easily build a
// sorted singly-linked freelist.
//
// When given a single set of segments, it's the same as iterating over
// std::set<Segment> in reverse order.
//
// With multiple segment sets, we still produce a total order.  Sets are
// annotated so that we can associate some data with their segments.  This is
// useful when evacuating the young ExternalPointerTable::Space into the old
// generation in a major collection, as both spaces could have been compacting,
// with different starts to the evacuation area.
template <typename Segment, typename Data>
class SegmentsIterator {
  using iterator = typename std::set<Segment>::reverse_iterator;
  using const_iterator = typename std::set<Segment>::const_reverse_iterator;

 public:
  SegmentsIterator() = default;

  void AddSegments(const std::set<Segment>& segments, Data data) {
    streams_.emplace_back(segments.rbegin(), segments.rend(), data);
  }

  std::optional<std::pair<Segment, Data>> Next() {
    int stream = -1;
    int min_stream = -1;
    std::optional<std::pair<Segment, Data>> result;
    for (auto [iter, end, data] : streams_) {
      stream++;
      if (iter != end) {
        Segment segment = *iter;
        if (!result || result.value().first < segment) {
          min_stream = stream;
          result.emplace(segment, data);
        }
      }
    }
    if (result) {
      streams_[min_stream].iter++;
      return result;
    }
    return {};
  }

 private:
  struct Stream {
    iterator iter;
    const_iterator end;
    Data data;

    Stream(iterator iter, const_iterator end, Data data)
        : iter(iter), end(end), data(data) {}
  };

  std::vector<Stream> streams_;
};

uint32_t ExternalPointerTable::EvacuateAndSweepAndCompact(Space* space,
                                                          Space* from_space,
                                                          Counters* counters) {
  DCHECK(space->BelongsTo(this));
  DCHECK(!space->is_internal_read_only_space());

  DCHECK_IMPLIES(from_space, from_space->BelongsTo(this));
  DCHECK_IMPLIES(from_space, !from_space->is_internal_read_only_space());

  // Lock the space. Technically this is not necessary since no other thread can
  // allocate entries at this point, but some of the methods we call on the
  // space assert that the lock is held.
  base::MutexGuard guard(&space->mutex_);
  // Same for the invalidated fields mutex.
  base::MutexGuard invalidated_fields_guard(&space->invalidated_fields_mutex_);

  // There must not be any entry allocations while the table is being swept as
  // that would not be safe. Set the freelist to this special marker value to
  // easily catch any violation of this requirement.
  space->freelist_head_.store(kEntryAllocationIsForbiddenMarker,
                              std::memory_order_relaxed);

  SegmentsIterator<Segment, CompactionResult> segments_iter;
  Histogram* counter = counters->external_pointer_table_compaction_outcome();
  CompactionResult space_compaction = FinishCompaction(space, counter);
  segments_iter.AddSegments(space->segments_, space_compaction);

  // If from_space is present, take its segments and add them to the sweep
  // iterator.  Wait until after the sweep to actually give from_space's
  // segments to the other space, to avoid invalidating the iterator.
  std::set<Segment> from_space_segments;
  if (from_space) {
    base::MutexGuard from_space_guard(&from_space->mutex_);
    base::MutexGuard from_space_invalidated_fields_guard(
        &from_space->invalidated_fields_mutex_);

    std::swap(from_space->segments_, from_space_segments);
    DCHECK(from_space->segments_.empty());

    CompactionResult from_space_compaction =
        FinishCompaction(from_space, counter);
    segments_iter.AddSegments(from_space_segments, from_space_compaction);

    FreelistHead empty_freelist;
    from_space->freelist_head_.store(empty_freelist, std::memory_order_relaxed);

    for (Address field : from_space->invalidated_fields_)
      space->invalidated_fields_.push_back(field);
    from_space->ClearInvalidatedFields();
  }

  // Sweep top to bottom and rebuild the freelist from newly dead and
  // previously freed entries while also clearing the marking bit on live
  // entries and resolving evacuation entries table when compacting the table.
  // This way, the freelist ends up sorted by index which already makes the
  // table somewhat self-compacting and is required for the compaction
  // algorithm so that evacuated entries are evacuated to the start of a space.
  // This method must run either on the mutator thread or while the mutator is
  // stopped.
  uint32_t current_freelist_head = 0;
  uint32_t current_freelist_length = 0;
  auto AddToFreelist = [&](uint32_t entry_index) {
    at(entry_index).MakeFreelistEntry(current_freelist_head);
    current_freelist_head = entry_index;
    current_freelist_length++;
  };

  std::vector<Segment> segments_to_deallocate;
  while (auto current = segments_iter.Next()) {
    Segment segment = current->first;
    CompactionResult compaction = current->second;

    bool segment_will_be_evacuated =
        compaction.success &&
        segment.first_entry() >= compaction.start_of_evacuation_area;

    // Remember the state of the freelist before this segment in case this
    // segment turns out to be completely empty and we deallocate it.
    uint32_t previous_freelist_head = current_freelist_head;
    uint32_t previous_freelist_length = current_freelist_length;

    // Process every entry in this segment, again going top to bottom.
    for (uint32_t i = segment.last_entry(); i >= segment.first_entry(); i--) {
      auto payload = at(i).GetRawPayload();
      if (payload.ContainsEvacuationEntry()) {
        // Segments that will be evacuated cannot contain evacuation entries
        // into which other entries would be evacuated.
        DCHECK(!segment_will_be_evacuated);

        // An evacuation entry contains the address of the external pointer
        // field that owns the entry that is to be evacuated.
        Address handle_location =
            payload.ExtractEvacuationEntryHandleLocation();

        // The evacuation entry may be invalidated by the Scavenger that has
        // freed the object.
        if (handle_location == kNullAddress) {
          AddToFreelist(i);
          continue;
        }

        // The external pointer field may have been invalidated in the meantime
        // (for example if the host object has been in-place converted to a
        // different type of object). In that case, the field no longer
        // contains an external pointer handle and we therefore cannot evacuate
        // the old entry. This is fine as the entry is guaranteed to be dead.
        if (space->FieldWasInvalidated(handle_location)) {
          // In this case, we must, however, free the evacuation entry.
          // Otherwise, we would be left with effectively a stale evacuation
          // entry that we'd try to process again during the next GC.
          AddToFreelist(i);
          continue;
        }

        // Resolve the evacuation entry: take the pointer to the handle from the
        // evacuation entry, copy the entry to its new location, and finally
        // update the handle to point to the new entry.
        //
        // While we now know that the entry being evacuated is free, we don't
        // add it to (the start of) the freelist because that would immediately
        // cause new fragmentation when the next entry is allocated. Instead, we
        // assume that the segments out of which entries are evacuated will all
        // be decommitted anyway after this loop, which is usually the case
        // unless compaction was already aborted during marking.
        ResolveEvacuationEntryDuringSweeping(
            i, reinterpret_cast<ExternalPointerHandle*>(handle_location),
            compaction.start_of_evacuation_area);

        // The entry must now contain an external pointer and be unmarked as
        // the entry that was evacuated must have been processed already (it
        // is in an evacuated segment, which are processed first as they are
        // at the end of the space). This will have cleared the marking bit.
        DCHECK(at(i).GetRawPayload().ContainsPointer());
        DCHECK(!at(i).GetRawPayload().HasMarkBitSet());
      } else if (!payload.HasMarkBitSet()) {
        FreeManagedResourceIfPresent(i);
        AddToFreelist(i);
      } else {
        auto new_payload = payload;
        new_payload.ClearMarkBit();
        at(i).SetRawPayload(new_payload);
      }

      // We must have resolved all evacuation entries. Otherwise, we'll try to
      // process them again during the next GC, which would cause problems.
      DCHECK(!at(i).HasEvacuationEntry());
    }

    // If a segment is completely empty, or if all live entries will be
    // evacuated out of it at the end of this loop, free the segment.
    // Note: for segments that will be evacuated, we could avoid building up a
    // freelist, but it's probably not worth the effort.
    uint32_t free_entries = current_freelist_length - previous_freelist_length;
    bool segment_is_empty = free_entries == kEntriesPerSegment;
    if (segment_is_empty || segment_will_be_evacuated) {
      segments_to_deallocate.push_back(segment);
      // Restore the state of the freelist before this segment.
      current_freelist_head = previous_freelist_head;
      current_freelist_length = previous_freelist_length;
    }
  }

  space->segments_.merge(from_space_segments);

  // We cannot deallocate the segments during the above loop, so do it now.
  for (auto segment : segments_to_deallocate) {
    FreeTableSegment(segment);
    space->segments_.erase(segment);
  }

  space->ClearInvalidatedFields();

  FreelistHead new_freelist(current_freelist_head, current_freelist_length);
  space->freelist_head_.store(new_freelist, std::memory_order_release);
  DCHECK_EQ(space->freelist_length(), current_freelist_length);

  uint32_t num_live_entries = space->capacity() - current_freelist_length;
  counters->external_pointers_count()->AddSample(num_live_entries);
  return num_live_entries;
}

uint32_t ExternalPointerTable::SweepAndCompact(Space* space,
                                               Counters* counters) {
  return EvacuateAndSweepAndCompact(space, nullptr, counters);
}

uint32_t ExternalPointerTable::Sweep(Space* space, Counters* counters) {
  DCHECK(!space->IsCompacting());
  return SweepAndCompact(space, counters);
}

void ExternalPointerTable::ResolveEvacuationEntryDuringSweeping(
    uint32_t new_index, ExternalPointerHandle* handle_location,
    uint32_t start_of_evacuation_area) {
  // We must have a valid handle here. If this fails, it might mean that an
  // object with external pointers was in-place converted to another type of
  // object without informing the external pointer table.
  ExternalPointerHandle old_handle = *handle_location;
  CHECK(IsValidHandle(old_handle));

  uint32_t old_index = HandleToIndex(old_handle);
  ExternalPointerHandle new_handle = IndexToHandle(new_index);

  // The compaction algorithm always moves an entry from the evacuation area to
  // the front of the table. These DCHECKs verify this invariant.
  DCHECK_GE(old_index, start_of_evacuation_area);
  DCHECK_LT(new_index, start_of_evacuation_area);
  auto& new_entry = at(new_index);
  at(old_index).Evacuate(new_entry, EvacuateMarkMode::kLeaveUnmarked);
  *handle_location = new_handle;

  // If this entry references a managed resource, update the resource to
  // reference the new entry.
  if (Address addr = at(new_index).ExtractManagedResourceOrNull()) {
    ManagedResource* resource = reinterpret_cast<ManagedResource*>(addr);
    DCHECK_EQ(resource->ept_entry_, old_handle);
    resource->ept_entry_ = new_handle;
  }
}

void ExternalPointerTable::UpdateAllEvacuationEntries(
    Space* space, std::function<Address(Address)> function) {
  DCHECK(space->BelongsTo(this));
  DCHECK(!space->is_internal_read_only_space());

  if (!space->IsCompacting()) return;

  // Lock the space. Technically this is not necessary since no other thread can
  // allocate entries at this point, but some of the methods we call on the
  // space assert that the lock is held.
  base::MutexGuard guard(&space->mutex_);
  // Same for the invalidated fields mutex.
  base::MutexGuard invalidated_fields_guard(&space->invalidated_fields_mutex_);

  const uint32_t start_of_evacuation_area =
      space->start_of_evacuation_area_.load(std::memory_order_relaxed);

  // Iterate until the start of evacuation area.
  for (auto& segment : space->segments_) {
    if (segment.first_entry() == start_of_evacuation_area) return;
    for (uint32_t i = segment.first_entry(); i < segment.last_entry() + 1;
         ++i) {
      ExternalPointerTableEntry& entry = at(i);
      ExternalPointerTableEntry::Payload payload = entry.GetRawPayload();
      if (!payload.ContainsEvacuationEntry()) {
        continue;
      }
      Address new_location =
          function(payload.ExtractEvacuationEntryHandleLocation());
      entry.MakeEvacuationEntry(new_location);
    }
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPRESS_POINTERS
