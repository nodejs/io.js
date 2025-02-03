// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/marking-state.h"

#include <unordered_set>

#include "src/heap/cppgc/heap-base.h"
#include "src/heap/cppgc/stats-collector.h"

namespace cppgc {
namespace internal {

void MarkingStateBase::Publish() { marking_worklist_.Publish(); }

BasicMarkingState::BasicMarkingState(HeapBase& heap,
                                     MarkingWorklists& marking_worklists,
                                     CompactionWorklists* compaction_worklists)
    : MarkingStateBase(heap, marking_worklists),
      previously_not_fully_constructed_worklist_(
          *marking_worklists.previously_not_fully_constructed_worklist()),
      weak_container_callback_worklist_(
          *marking_worklists.weak_container_callback_worklist()),
      parallel_weak_callback_worklist_(
          *marking_worklists.parallel_weak_callback_worklist()),
      weak_custom_callback_worklist_(
          *marking_worklists.weak_custom_callback_worklist()),
      write_barrier_worklist_(*marking_worklists.write_barrier_worklist()),
      concurrent_marking_bailout_worklist_(
          *marking_worklists.concurrent_marking_bailout_worklist()),
      discovered_ephemeron_pairs_worklist_(
          *marking_worklists.discovered_ephemeron_pairs_worklist()),
      ephemeron_pairs_for_processing_worklist_(
          *marking_worklists.ephemeron_pairs_for_processing_worklist()),
      weak_containers_worklist_(*marking_worklists.weak_containers_worklist()) {
  if (compaction_worklists) {
    movable_slots_worklist_ =
        std::make_unique<CompactionWorklists::MovableReferencesWorklist::Local>(
            *compaction_worklists->movable_slots_worklist());
  }
}

void BasicMarkingState::Publish() {
  MarkingStateBase::Publish();
  previously_not_fully_constructed_worklist_.Publish();
  weak_container_callback_worklist_.Publish();
  parallel_weak_callback_worklist_.Publish();
  weak_custom_callback_worklist_.Publish();
  write_barrier_worklist_.Publish();
  concurrent_marking_bailout_worklist_.Publish();
  discovered_ephemeron_pairs_worklist_.Publish();
  ephemeron_pairs_for_processing_worklist_.Publish();
  if (movable_slots_worklist_) movable_slots_worklist_->Publish();

  for (const auto& entry : marked_bytes_map_.Take()) {
    entry.first->IncrementMarkedBytes(static_cast<size_t>(entry.second));
  }
}

void MutatorMarkingState::FlushNotFullyConstructedObjects() {
  std::unordered_set<HeapObjectHeader*> objects =
      not_fully_constructed_worklist_.Extract<AccessMode::kAtomic>();
  for (HeapObjectHeader* object : objects) {
    if (MarkNoPush(*object))
      previously_not_fully_constructed_worklist_.Push(object);
  }
}

void MutatorMarkingState::FlushDiscoveredEphemeronPairs() {
  StatsCollector::EnabledScope stats_scope(
      heap_.stats_collector(), StatsCollector::kMarkFlushEphemerons);
  discovered_ephemeron_pairs_worklist_.Publish();
  if (!discovered_ephemeron_pairs_worklist_.IsGlobalEmpty()) {
    ephemeron_pairs_for_processing_worklist_.Merge(
        discovered_ephemeron_pairs_worklist_);
  }
}

void MutatorMarkingState::Publish() {
  BasicMarkingState::Publish();
  retrace_marked_objects_worklist_.Publish();
}

}  // namespace internal
}  // namespace cppgc
