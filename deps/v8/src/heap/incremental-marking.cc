// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/incremental-marking.h"

#include <inttypes.h>

#include <cmath>
#include <optional>

#include "src/base/logging.h"
#include "src/base/platform/time.h"
#include "src/common/globals.h"
#include "src/execution/vm-state-inl.h"
#include "src/flags/flags.h"
#include "src/handles/global-handles.h"
#include "src/heap/base/incremental-marking-schedule.h"
#include "src/heap/concurrent-marking.h"
#include "src/heap/gc-tracer-inl.h"
#include "src/heap/gc-tracer.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap.h"
#include "src/heap/incremental-marking-job.h"
#include "src/heap/mark-compact.h"
#include "src/heap/marking-barrier.h"
#include "src/heap/marking-visitor-inl.h"
#include "src/heap/marking-visitor.h"
#include "src/heap/memory-chunk-layout.h"
#include "src/heap/minor-mark-sweep.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/heap/objects-visiting-inl.h"
#include "src/heap/objects-visiting.h"
#include "src/heap/safepoint.h"
#include "src/init/v8.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/numbers/conversions.h"
#include "src/objects/data-handler-inl.h"
#include "src/objects/slots-inl.h"
#include "src/objects/visitors.h"
#include "src/tracing/trace-event.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {

namespace {

static constexpr size_t kMajorGCYoungGenerationAllocationObserverStep = 64 * KB;
static constexpr size_t kMajorGCOldGenerationAllocationObserverStep = 256 * KB;

static constexpr v8::base::TimeDelta kMaxStepSizeOnTask =
    v8::base::TimeDelta::FromMilliseconds(1);
static constexpr v8::base::TimeDelta kMaxStepSizeOnAllocation =
    v8::base::TimeDelta::FromMilliseconds(5);

#ifndef DEBUG
static constexpr size_t kV8ActivationThreshold = 8 * MB;
static constexpr size_t kEmbedderActivationThreshold = 8 * MB;
#else
static constexpr size_t kV8ActivationThreshold = 0;
static constexpr size_t kEmbedderActivationThreshold = 0;
#endif  // DEBUG

base::TimeDelta GetMaxDuration(StepOrigin step_origin) {
  if (v8_flags.predictable) {
    return base::TimeDelta::Max();
  }
  switch (step_origin) {
    case StepOrigin::kTask:
      return kMaxStepSizeOnTask;
    case StepOrigin::kV8:
      return kMaxStepSizeOnAllocation;
  }
}

}  // namespace

IncrementalMarking::Observer::Observer(IncrementalMarking* incremental_marking,
                                       intptr_t step_size)
    : AllocationObserver(step_size),
      incremental_marking_(incremental_marking) {}

void IncrementalMarking::Observer::Step(int, Address, size_t) {
  Heap* heap = incremental_marking_->heap();
  VMState<GC> state(heap->isolate());
  RCS_SCOPE(heap->isolate(),
            RuntimeCallCounterId::kGC_Custom_IncrementalMarkingObserver);
  incremental_marking_->AdvanceOnAllocation();
}

IncrementalMarking::IncrementalMarking(Heap* heap, WeakObjects* weak_objects)
    : heap_(heap),
      major_collector_(heap->mark_compact_collector()),
      minor_collector_(heap->minor_mark_sweep_collector()),
      weak_objects_(weak_objects),
      marking_state_(heap->marking_state()),
      incremental_marking_job_(
          v8_flags.incremental_marking_task
              ? std::make_unique<IncrementalMarkingJob>(heap)
              : nullptr),
      new_generation_observer_(this,
                               kMajorGCYoungGenerationAllocationObserverStep),
      old_generation_observer_(this,
                               kMajorGCOldGenerationAllocationObserverStep) {}

void IncrementalMarking::MarkBlackBackground(Tagged<HeapObject> obj,
                                             int object_size) {
  CHECK(marking_state()->TryMark(obj));
  base::MutexGuard guard(&background_live_bytes_mutex_);
  background_live_bytes_[MutablePageMetadata::FromHeapObject(obj)] +=
      static_cast<intptr_t>(object_size);
}

bool IncrementalMarking::CanAndShouldBeStarted() const {
  return CanBeStarted() && heap_->ShouldUseIncrementalMarking();
}

bool IncrementalMarking::CanBeStarted() const {
  // Only start incremental marking in a safe state:
  //   1) when incremental marking is turned on
  //   2) when we are currently not in a GC, and
  //   3) when we are currently not serializing or deserializing the heap, and
  //   4) not a shared heap.
  return v8_flags.incremental_marking && heap_->gc_state() == Heap::NOT_IN_GC &&
         heap_->deserialization_complete() && !isolate()->serializer_enabled();
}

bool IncrementalMarking::IsBelowActivationThresholds() const {
  return heap_->OldGenerationSizeOfObjects() <= kV8ActivationThreshold &&
         heap_->EmbedderSizeOfObjects() <= kEmbedderActivationThreshold;
}

void IncrementalMarking::Start(GarbageCollector garbage_collector,
                               GarbageCollectionReason gc_reason) {
  CHECK(IsStopped());
  CHECK_IMPLIES(garbage_collector == GarbageCollector::MARK_COMPACTOR,
                !heap_->sweeping_in_progress());
  CHECK_IMPLIES(garbage_collector == GarbageCollector::MINOR_MARK_SWEEPER,
                !heap_->minor_sweeping_in_progress());
  // Do not invoke CanAndShouldBeStarted() here again because its return value
  // might change across multiple invocations (its internal state could be
  // updated concurrently from another thread between invocations).
  CHECK(CanBeStarted());

  if (V8_UNLIKELY(v8_flags.trace_incremental_marking)) {
    const size_t old_generation_size_mb =
        heap()->OldGenerationSizeOfObjects() / MB;
    const size_t old_generation_waste_mb =
        heap()->OldGenerationWastedBytes() / MB;
    const size_t old_generation_limit_mb =
        heap()->old_generation_allocation_limit() / MB;
    const size_t global_size_mb = heap()->GlobalSizeOfObjects() / MB;
    const size_t global_waste_mb = heap()->GlobalWastedBytes() / MB;
    const size_t global_limit_mb = heap()->global_allocation_limit() / MB;
    isolate()->PrintWithTimestamp(
        "[IncrementalMarking] Start (%s): (size/waste/limit/slack) v8: %zuMB / "
        "%zuMB / %zuMB "
        "/ %zuMB global: %zuMB / %zuMB / %zuMB / %zuMB\n",
        ToString(gc_reason), old_generation_size_mb, old_generation_waste_mb,
        old_generation_limit_mb,
        old_generation_size_mb + old_generation_waste_mb >
                old_generation_limit_mb
            ? 0
            : old_generation_limit_mb - old_generation_size_mb,
        global_size_mb, global_waste_mb, global_limit_mb,
        global_size_mb + global_waste_mb > global_limit_mb
            ? 0
            : global_limit_mb - global_size_mb);
  }

  Counters* counters = isolate()->counters();
  const bool is_major = garbage_collector == GarbageCollector::MARK_COMPACTOR;
  if (is_major) {
    // Reasons are only reported for major GCs
    counters->incremental_marking_reason()->AddSample(
        static_cast<int>(gc_reason));
  }
  NestedTimedHistogramScope incremental_marking_scope(
      is_major ? counters->gc_incremental_marking_start()
               : counters->gc_minor_incremental_marking_start());
  const auto scope_id = is_major ? GCTracer::Scope::MC_INCREMENTAL_START
                                 : GCTracer::Scope::MINOR_MS_INCREMENTAL_START;
  DCHECK(!current_trace_id_.has_value());
  current_trace_id_.emplace(reinterpret_cast<uint64_t>(this) ^
                            heap_->tracer()->CurrentEpoch(scope_id));
  TRACE_EVENT2("v8",
               is_major ? "V8.GCIncrementalMarkingStart"
                        : "V8.GCMinorIncrementalMarkingStart",
               "epoch", heap_->tracer()->CurrentEpoch(scope_id), "reason",
               ToString(gc_reason));
  TRACE_GC_EPOCH_WITH_FLOW(heap()->tracer(), scope_id, ThreadKind::kMain,
                           current_trace_id_.value(),
                           TRACE_EVENT_FLAG_FLOW_OUT);
  heap_->tracer()->NotifyIncrementalMarkingStart();

  start_time_ = v8::base::TimeTicks::Now();
  completion_task_scheduled_ = false;
  completion_task_timeout_ = v8::base::TimeTicks();
  main_thread_marked_bytes_ = 0;
  bytes_marked_concurrently_ = 0;

  if (is_major) {
    StartMarkingMajor();
    heap_->allocator()->AddAllocationObserver(&old_generation_observer_,
                                              &new_generation_observer_);
    if (incremental_marking_job()) {
      incremental_marking_job()->ScheduleTask();
    }
    DCHECK_NULL(schedule_);
    schedule_ =
        v8_flags.incremental_marking_bailout_when_ahead_of_schedule
            ? ::heap::base::IncrementalMarkingSchedule::
                  CreateWithZeroMinimumMarkedBytesPerStep(v8_flags.predictable)
            : ::heap::base::IncrementalMarkingSchedule::
                  CreateWithDefaultMinimumMarkedBytesPerStep(
                      v8_flags.predictable);
    schedule_->NotifyIncrementalMarkingStart();
  } else {
    // Allocation observers are not currently used by MinorMS because we don't
    // do incremental marking.
    StartMarkingMinor();
  }
}

void IncrementalMarking::MarkRoots() {
  if (IsMajorMarking()) {
    RootMarkingVisitor root_visitor(heap_->mark_compact_collector());
    heap_->IterateRoots(
        &root_visitor,
        base::EnumSet<SkipRoot>{SkipRoot::kStack, SkipRoot::kMainThreadHandles,
                                SkipRoot::kTracedHandles, SkipRoot::kWeak,
                                SkipRoot::kReadOnlyBuiltins});
  } else {
    DCHECK(IsMinorMarking());
    YoungGenerationRootMarkingVisitor root_visitor(
        heap_->minor_mark_sweep_collector());
    heap_->IterateRoots(
        &root_visitor,
        base::EnumSet<SkipRoot>{
            SkipRoot::kStack, SkipRoot::kMainThreadHandles, SkipRoot::kWeak,
            SkipRoot::kExternalStringTable, SkipRoot::kGlobalHandles,
            SkipRoot::kTracedHandles, SkipRoot::kOldGeneration,
            SkipRoot::kReadOnlyBuiltins});
    isolate()->global_handles()->IterateYoungStrongAndDependentRoots(
        &root_visitor);
  }
}

void IncrementalMarking::MarkRootsForTesting() { MarkRoots(); }

void IncrementalMarking::StartMarkingMajor() {
  if (isolate()->serializer_enabled()) {
    // Black allocation currently starts when we start incremental marking,
    // but we cannot enable black allocation while deserializing. Hence, we
    // have to delay the start of incremental marking in that case.
    if (v8_flags.trace_incremental_marking) {
      isolate()->PrintWithTimestamp(
          "[IncrementalMarking] Start delayed - serializer\n");
    }
    return;
  }
  if (v8_flags.trace_incremental_marking) {
    isolate()->PrintWithTimestamp("[IncrementalMarking] Start marking\n");
  }

  heap_->InvokeIncrementalMarkingPrologueCallbacks();

  // Free all existing LABs in the heap such that selecting evacuation
  // candidates does not need to deal with LABs on a page. While we don't need
  // this for correctness, we want to avoid creating additional work for
  // evacuation.
  heap_->FreeLinearAllocationAreas();

  is_compacting_ = major_collector_->StartCompaction(
      MarkCompactCollector::StartCompactionMode::kIncremental);

  major_collector_->StartMarking();
  current_local_marking_worklists_ =
      major_collector_->local_marking_worklists();

  marking_mode_ = MarkingMode::kMajorMarking;
  heap_->SetIsMarkingFlag(true);

  MarkingBarrier::ActivateAll(heap(), is_compacting_);
  isolate()->traced_handles()->SetIsMarking(true);

  StartBlackAllocation();

  {
    TRACE_GC(heap()->tracer(), GCTracer::Scope::MC_MARK_ROOTS);
    MarkRoots();
  }

  if (v8_flags.concurrent_marking && !heap_->IsTearingDown()) {
    heap_->concurrent_marking()->TryScheduleJob(
        GarbageCollector::MARK_COMPACTOR);
  }

  // Ready to start incremental marking.
  if (v8_flags.trace_incremental_marking) {
    isolate()->PrintWithTimestamp("[IncrementalMarking] Running\n");
  }

  if (heap()->cpp_heap()) {
    // StartTracing may call back into V8 in corner cases, requiring that
    // marking (including write barriers) is fully set up.
    TRACE_GC(heap()->tracer(), GCTracer::Scope::MC_MARK_EMBEDDER_PROLOGUE);
    CppHeap::From(heap()->cpp_heap())->StartMarking();
  }

  heap_->InvokeIncrementalMarkingEpilogueCallbacks();
}

void IncrementalMarking::StartMarkingMinor() {
  // Removed serializer_enabled() check because we don't do black allocation.

  if (v8_flags.trace_incremental_marking) {
    isolate()->PrintWithTimestamp(
        "[IncrementalMarking] (MinorMS) Start marking\n");
  }

  // We only reach this code if Heap::ShouldUseBackgroundThreads() returned
  // true. So we can force the use of background threads here.
  minor_collector_->StartMarking(true);
  current_local_marking_worklists_ =
      minor_collector_->local_marking_worklists();

  marking_mode_ = MarkingMode::kMinorMarking;
  heap_->SetIsMarkingFlag(true);
  heap_->SetIsMinorMarkingFlag(true);

  {
    Sweeper::PauseMajorSweepingScope pause_sweeping_scope(heap_->sweeper());
    MarkingBarrier::ActivateYoung(heap());
  }

  {
    TRACE_GC(heap()->tracer(), GCTracer::Scope::MINOR_MS_MARK_INCREMENTAL_SEED);
    MarkRoots();
  }

  if (v8_flags.concurrent_minor_ms_marking && !heap_->IsTearingDown()) {
    local_marking_worklists()->PublishWork();
    heap_->concurrent_marking()->TryScheduleJob(
        GarbageCollector::MINOR_MARK_SWEEPER);
  }

  if (v8_flags.trace_incremental_marking) {
    isolate()->PrintWithTimestamp("[IncrementalMarking] (MinorMS) Running\n");
  }

  DCHECK(!is_compacting_);
}

void IncrementalMarking::StartBlackAllocation() {
  DCHECK(!black_allocation_);
  DCHECK(IsMajorMarking());
  black_allocation_ = true;
  heap()->allocator()->MarkLinearAllocationAreasBlack();
  if (isolate()->is_shared_space_isolate()) {
    isolate()->global_safepoint()->IterateSharedSpaceAndClientIsolates(
        [](Isolate* client) {
          client->heap()->MarkSharedLinearAllocationAreasBlack();
        });
  }
  heap()->safepoint()->IterateLocalHeaps([](LocalHeap* local_heap) {
    local_heap->MarkLinearAllocationAreasBlack();
  });
  StartPointerTableBlackAllocation();
  if (v8_flags.trace_incremental_marking) {
    isolate()->PrintWithTimestamp(
        "[IncrementalMarking] Black allocation started\n");
  }
}

void IncrementalMarking::PauseBlackAllocation() {
  DCHECK(IsMajorMarking());
  heap()->allocator()->UnmarkLinearAllocationsArea();
  if (isolate()->is_shared_space_isolate()) {
    isolate()->global_safepoint()->IterateSharedSpaceAndClientIsolates(
        [](Isolate* client) {
          client->heap()->UnmarkSharedLinearAllocationAreas();
        });
  }
  heap()->safepoint()->IterateLocalHeaps(
      [](LocalHeap* local_heap) { local_heap->UnmarkLinearAllocationsArea(); });
  StopPointerTableBlackAllocation();
  if (v8_flags.trace_incremental_marking) {
    isolate()->PrintWithTimestamp(
        "[IncrementalMarking] Black allocation paused\n");
  }
  black_allocation_ = false;
}

void IncrementalMarking::FinishBlackAllocation() {
  if (black_allocation_) {
    black_allocation_ = false;
    StopPointerTableBlackAllocation();
    if (v8_flags.trace_incremental_marking) {
      isolate()->PrintWithTimestamp(
          "[IncrementalMarking] Black allocation finished\n");
    }
  }
}

void IncrementalMarking::StartPointerTableBlackAllocation() {
#ifdef V8_ENABLE_SANDBOX
  heap()->code_pointer_space()->set_allocate_black(true);
  heap()->trusted_pointer_space()->set_allocate_black(true);
#endif  // V8_ENABLE_SANDBOX
#ifdef V8_ENABLE_LEAPTIERING
  heap()->js_dispatch_table_space()->set_allocate_black(true);
#endif  // V8_ENABLE_LEAPTIERING
}

void IncrementalMarking::StopPointerTableBlackAllocation() {
#ifdef V8_ENABLE_SANDBOX
  heap()->code_pointer_space()->set_allocate_black(false);
  heap()->trusted_pointer_space()->set_allocate_black(false);
#endif  // V8_ENABLE_SANDBOX
#ifdef V8_ENABLE_LEAPTIERING
  heap()->js_dispatch_table_space()->set_allocate_black(false);
#endif  // V8_ENABLE_LEAPTIERING
}

void IncrementalMarking::UpdateMarkingWorklistAfterScavenge() {
  if (!IsMajorMarking()) return;
  DCHECK(!v8_flags.separate_gc_phases);
  DCHECK(IsMajorMarking());
  // Minor MS never runs during incremental marking.
  DCHECK(!v8_flags.minor_ms);

  Tagged<Map> filler_map = ReadOnlyRoots(heap_).one_pointer_filler_map();

  MarkingState* marking_state = heap()->marking_state();

  major_collector_->local_marking_worklists()->Publish();
  MarkingBarrier::PublishAll(heap());
  PtrComprCageBase cage_base(isolate());
  major_collector_->marking_worklists()->Update([this, marking_state, cage_base,
                                                 filler_map](
                                                    Tagged<HeapObject> obj,
                                                    Tagged<HeapObject>* out)
                                                    -> bool {
    DCHECK(IsHeapObject(obj));
    USE(marking_state);

    // Only pointers to from space have to be updated.
    if (Heap::InFromPage(obj)) {
      MapWord map_word = obj->map_word(cage_base, kRelaxedLoad);
      if (!map_word.IsForwardingAddress()) {
        // There may be objects on the marking deque that do not exist
        // anymore, e.g. left trimmed objects or objects from the root set
        // (frames). If these object are dead at scavenging time, their
        // marking deque entries will not point to forwarding addresses.
        // Hence, we can discard them.
        return false;
      }
      // Live young large objects are not relocated and directly promoted into
      // the old generation before invoking this method. So they looke like any
      // other pointer into the old space and we won't encounter them here in
      // this code path.
      DCHECK(!Heap::IsLargeObject(obj));
      Tagged<HeapObject> dest = map_word.ToForwardingAddress(obj);
      DCHECK_IMPLIES(marking_state->IsUnmarked(obj), IsFreeSpaceOrFiller(obj));
      if (InWritableSharedSpace(dest) &&
          !isolate()->is_shared_space_isolate()) {
        // Object got promoted into the shared heap. Drop it from the client
        // heap marking worklist.
        return false;
      }
      // For any object not a DescriptorArray, transferring the object always
      // increments live bytes as the marked state cannot distinguish fully
      // processed from to-be-processed. Decrement the counter for such objects
      // here.
      if (!IsDescriptorArray(dest)) {
        MutablePageMetadata::FromHeapObject(dest)->IncrementLiveBytesAtomically(
            -ALIGN_TO_ALLOCATION_ALIGNMENT(dest->Size()));
      }
      *out = dest;
      return true;
    } else {
      DCHECK(!Heap::InToPage(obj));
      DCHECK_IMPLIES(marking_state->IsUnmarked(obj),
                     IsFreeSpaceOrFiller(obj, cage_base));
      // Skip one word filler objects that appear on the
      // stack when we perform in place array shift.
      if (obj->map(cage_base) != filler_map) {
        *out = obj;
        return true;
      }
      return false;
    }
  });

  major_collector_->local_weak_objects()->Publish();
  weak_objects_->UpdateAfterScavenge();
}

void IncrementalMarking::UpdateExternalPointerTableAfterScavenge() {
#ifdef V8_COMPRESS_POINTERS
  if (!IsMajorMarking()) return;
  DCHECK(!v8_flags.separate_gc_phases);

  heap_->isolate()->external_pointer_table().UpdateAllEvacuationEntries(
      heap_->young_external_pointer_space(), [](Address old_handle_location) {
        // 1) Resolve object start from the marking bitmap. Note that it's safe
        //    since there is no black allocation for the young space (and hence
        //    no range or page marking).
        // 2) Get a relocated object from the forwaring reference stored in the
        //    map.
        // 3) Compute offset from the original object start to the handle
        //    location.
        // 4) Compute and return the new handle location.
        //
        // Please note that instead of updating the evacuation entries, we
        // could simply clobber them all, which would still work, but limit
        // compaction to some extent. We can reconsider this in the future, if
        // relying on the marking bitmap becomes an issue (e.g. with inlined
        // mark-bits).
        const MemoryChunk* chunk =
            MemoryChunk::FromAddress(old_handle_location);
        if (!chunk->InYoungGeneration()) {
          return old_handle_location;
        }
        // TODO(358485426): Check that the page is not black.

        Address base = MarkingBitmap::FindPreviousValidObject(
            static_cast<const PageMetadata*>(chunk->Metadata()),
            old_handle_location);
        Tagged<HeapObject> object(HeapObject::FromAddress(base));

        MapWord map_word = object->map_word(kRelaxedLoad);
        if (!map_word.IsForwardingAddress()) {
      // There may be objects in the EPT that do not exist anymore. If these
      // objects are dead at scavenging time, their marking deque entries will
      // not point to forwarding addresses. Hence, we can discard them.
#if DEBUG
          // Check that the handle did reside inside the original dead object.
          const int object_size = object->Size();
          // Map slots can never contain external pointers.
          DCHECK_LT(object.address(), old_handle_location);
          DCHECK_LT(old_handle_location, object.address() + object_size);
#endif  // DEBUG
          return kNullAddress;
        }

        Tagged<HeapObject> moved_object = map_word.ToForwardingAddress(object);
#if DEBUG
        const int object_size = moved_object->Size();
        // Map slots can never contain external pointers.
        DCHECK_LT(object.address(), old_handle_location);
        DCHECK_LT(old_handle_location, object.address() + object_size);
#endif  // DEBUG

        const ptrdiff_t handle_offset = old_handle_location - base;
        return moved_object.address() + handle_offset;
      });
#endif  // V8_COMPRESS_POINTERS
}

void IncrementalMarking::UpdateMarkedBytesAfterScavenge(
    size_t dead_bytes_in_new_space) {
  if (!IsMajorMarking()) return;
  // When removing the call, adjust the marking schedule to only support
  // monotonically increasing mutator marked bytes.
  main_thread_marked_bytes_ -=
      std::min(main_thread_marked_bytes_, dead_bytes_in_new_space);
}

v8::base::TimeDelta IncrementalMarking::EmbedderStep(
    v8::base::TimeDelta expected_duration) {
  DCHECK(IsMarking());
  auto* cpp_heap = CppHeap::From(heap_->cpp_heap());
  DCHECK_NOT_NULL(cpp_heap);
  if (!cpp_heap->incremental_marking_supported()) {
    return {};
  }

  TRACE_GC(heap()->tracer(), GCTracer::Scope::MC_INCREMENTAL_EMBEDDER_TRACING);
  const auto start = v8::base::TimeTicks::Now();
  cpp_heap->AdvanceTracing(expected_duration);
  return v8::base::TimeTicks::Now() - start;
}

bool IncrementalMarking::Stop() {
  if (IsStopped()) return false;

  if (v8_flags.trace_incremental_marking) {
    int old_generation_size_mb =
        static_cast<int>(heap()->OldGenerationSizeOfObjects() / MB);
    int old_generation_waste_mb =
        static_cast<int>(heap()->OldGenerationWastedBytes() / MB);
    int old_generation_limit_mb =
        static_cast<int>(heap()->old_generation_allocation_limit() / MB);
    isolate()->PrintWithTimestamp(
        "[IncrementalMarking] Stopping: old generation size %dMB, waste %dMB, "
        "limit %dMB, "
        "overshoot %dMB\n",
        old_generation_size_mb, old_generation_waste_mb,
        old_generation_limit_mb,
        std::max(0, old_generation_size_mb + old_generation_waste_mb -
                        old_generation_limit_mb));
  }

  if (IsMajorMarking()) {
    heap()->allocator()->RemoveAllocationObserver(&old_generation_observer_,
                                                  &new_generation_observer_);
    major_collection_requested_via_stack_guard_ = false;
    isolate()->stack_guard()->ClearGC();
  }

  marking_mode_ = MarkingMode::kNoMarking;
  current_local_marking_worklists_ = nullptr;
  current_trace_id_.reset();

  if (isolate()->has_shared_space() && !isolate()->is_shared_space_isolate()) {
    // When disabling local incremental marking in a client isolate (= worker
    // isolate), the marking barrier needs to stay enabled when incremental
    // marking in the shared heap is running.
    const bool is_marking = isolate()
                                ->shared_space_isolate()
                                ->heap()
                                ->incremental_marking()
                                ->IsMajorMarking();
    heap_->SetIsMarkingFlag(is_marking);
  } else {
    heap_->SetIsMarkingFlag(false);
  }

  heap_->SetIsMinorMarkingFlag(false);
  is_compacting_ = false;
  FinishBlackAllocation();

  // Merge live bytes counters of background threads
  for (const auto& pair : background_live_bytes_) {
    MutablePageMetadata* memory_chunk = pair.first;
    intptr_t live_bytes = pair.second;
    if (live_bytes) {
      memory_chunk->IncrementLiveBytesAtomically(live_bytes);
    }
  }
  background_live_bytes_.clear();
  schedule_.reset();

  return true;
}

size_t IncrementalMarking::OldGenerationSizeOfObjects() const {
  // TODO(v8:14140): This is different to Heap::OldGenerationSizeOfObjects() in
  // that it only considers shared space for the shared space isolate. Consider
  // adjusting the Heap version.
  const bool is_shared_space_isolate =
      heap_->isolate()->is_shared_space_isolate();
  size_t total = 0;
  PagedSpaceIterator spaces(heap_);
  for (PagedSpace* space = spaces.Next(); space != nullptr;
       space = spaces.Next()) {
    if (space->identity() == SHARED_SPACE && !is_shared_space_isolate) continue;
    total += space->SizeOfObjects();
  }
  total += heap_->lo_space()->SizeOfObjects();
  total += heap_->code_lo_space()->SizeOfObjects();
  if (heap_->shared_lo_space() && is_shared_space_isolate) {
    total += heap_->shared_lo_space()->SizeOfObjects();
  }
  return total;
}

bool IncrementalMarking::ShouldWaitForTask() {
  if (!completion_task_scheduled_) {
    if (!incremental_marking_job()) {
      return false;
    }
    incremental_marking_job()->ScheduleTask();
    completion_task_scheduled_ = true;
    if (!TryInitializeTaskTimeout()) {
      return false;
    }
  }

  const auto now = v8::base::TimeTicks::Now();
  const bool wait_for_task = now < completion_task_timeout_;
  if (V8_UNLIKELY(v8_flags.trace_incremental_marking)) {
    isolate()->PrintWithTimestamp(
        "[IncrementalMarking] Completion: %s GC via stack guard, time left: "
        "%.1fms\n",
        wait_for_task ? "Delaying" : "Not delaying",
        (completion_task_timeout_ - now).InMillisecondsF());
  }
  return wait_for_task;
}

bool IncrementalMarking::TryInitializeTaskTimeout() {
  DCHECK_NOT_NULL(incremental_marking_job());
  // Allowed overshoot percentage of incremental marking walltime.
  constexpr double kAllowedOvershootPercentBasedOnWalltime = 0.1;
  // Minimum overshoot in ms. This is used to allow moving away from stack
  // when marking was fast.
  constexpr auto kMinAllowedOvershoot =
      v8::base::TimeDelta::FromMilliseconds(50);
  const auto now = v8::base::TimeTicks::Now();
  const auto allowed_overshoot = std::max(
      kMinAllowedOvershoot, v8::base::TimeDelta::FromMillisecondsD(
                                (now - start_time_).InMillisecondsF() *
                                kAllowedOvershootPercentBasedOnWalltime));
  const auto optional_avg_time_to_marking_task =
      incremental_marking_job()->AverageTimeToTask();
  // Only allowed to delay if the recorded average exists and is below the
  // threshold.
  bool delaying =
      optional_avg_time_to_marking_task.has_value() &&
      optional_avg_time_to_marking_task.value() <= allowed_overshoot;
  const auto optional_time_to_current_task =
      incremental_marking_job()->CurrentTimeToTask();
  // Don't bother delaying if the currently scheduled task is already waiting
  // too long.
  delaying =
      delaying && (!optional_time_to_current_task.has_value() ||
                   optional_time_to_current_task.value() <= allowed_overshoot);
  if (delaying) {
    const auto delta =
        !optional_time_to_current_task.has_value()
            ? allowed_overshoot
            : allowed_overshoot - optional_time_to_current_task.value();
    completion_task_timeout_ = now + delta;
  }
  DCHECK_IMPLIES(!delaying, completion_task_timeout_ <= now);
  if (V8_UNLIKELY(v8_flags.trace_incremental_marking)) {
    isolate()->PrintWithTimestamp(
        "[IncrementalMarking] Completion: %s GC via stack guard, "
        "avg time to task: %.1fms, current time to task: %.1fms allowed "
        "overshoot: %.1fms\n",
        delaying ? "Delaying" : "Not delaying",
        optional_avg_time_to_marking_task.has_value()
            ? optional_avg_time_to_marking_task->InMillisecondsF()
            : NAN,
        optional_time_to_current_task.has_value()
            ? optional_time_to_current_task->InMillisecondsF()
            : NAN,
        allowed_overshoot.InMillisecondsF());
  }
  return delaying;
}

size_t IncrementalMarking::GetScheduledBytes(StepOrigin step_origin) {
  FetchBytesMarkedConcurrently();
  // TODO(v8:14140): Consider the size including young generation here as well
  // as the full marker marks both the young and old generations.
  const size_t max_bytes_to_process =
      schedule_->GetNextIncrementalStepDuration(OldGenerationSizeOfObjects());
  if (V8_UNLIKELY(v8_flags.trace_incremental_marking)) {
    const auto step_info = schedule_->GetCurrentStepInfo();
    isolate()->PrintWithTimestamp(
        "[IncrementalMarking] Schedule: %zuKB to mark, origin: %s, elapsed: "
        "%.1f, marked: %zuKB (mutator: %zuKB, concurrent %zuKB), expected "
        "marked: %zuKB, estimated live: %zuKB, schedule delta: %+" PRIi64
        "KB\n",
        max_bytes_to_process / KB, ToString(step_origin),
        step_info.elapsed_time.InMillisecondsF(), step_info.marked_bytes() / KB,
        step_info.mutator_marked_bytes / KB,
        step_info.concurrent_marked_bytes / KB,
        step_info.expected_marked_bytes / KB,
        step_info.estimated_live_bytes / KB,
        step_info.scheduled_delta_bytes() / KB);
  }
  return max_bytes_to_process;
}

void IncrementalMarking::AdvanceAndFinalizeIfComplete() {
  const size_t max_bytes_to_process = GetScheduledBytes(StepOrigin::kTask);
  Step(GetMaxDuration(StepOrigin::kTask), max_bytes_to_process,
       StepOrigin::kTask);
  if (IsMajorMarkingComplete()) {
    heap()->FinalizeIncrementalMarkingAtomically(
        GarbageCollectionReason::kFinalizeMarkingViaTask);
  }
}

void IncrementalMarking::AdvanceAndFinalizeIfNecessary() {
  if (!IsMajorMarking()) return;
  DCHECK(!heap_->always_allocate());
  AdvanceOnAllocation();
  if (major_collection_requested_via_stack_guard_ && IsMajorMarkingComplete()) {
    heap()->FinalizeIncrementalMarkingAtomically(
        GarbageCollectionReason::kFinalizeMarkingViaStackGuard);
  }
}

void IncrementalMarking::AdvanceForTesting(v8::base::TimeDelta max_duration,
                                           size_t max_bytes_to_mark) {
  Step(max_duration, max_bytes_to_mark, StepOrigin::kV8);
}

bool IncrementalMarking::IsAheadOfSchedule() const {
  DCHECK(IsMajorMarking());

  const ::heap::base::IncrementalMarkingSchedule* v8_schedule = schedule_.get();
  if (v8_schedule->GetCurrentStepInfo().is_behind_expectation()) {
    return false;
  }
  if (auto* cpp_heap = CppHeap::From(heap()->cpp_heap())) {
    if (!cpp_heap->marker()->IsAheadOfSchedule()) {
      return false;
    }
  }
  return true;
}

void IncrementalMarking::AdvanceOnAllocation() {
  DCHECK_EQ(heap_->gc_state(), Heap::NOT_IN_GC);
  DCHECK(v8_flags.incremental_marking);
  DCHECK(IsMajorMarking());

  const size_t max_bytes_to_process = GetScheduledBytes(StepOrigin::kV8);
  Step(GetMaxDuration(StepOrigin::kV8), max_bytes_to_process, StepOrigin::kV8);

  // Bail out when an AlwaysAllocateScope is active as the assumption is that
  // there's no GC being triggered. Check this condition at last position to
  // allow a completion task to be scheduled.
  if (IsMajorMarkingComplete() && !ShouldWaitForTask() &&
      !heap()->always_allocate()) {
    // When completion task isn't run soon enough, fall back to stack guard to
    // force completion.
    major_collection_requested_via_stack_guard_ = true;
    isolate()->stack_guard()->RequestGC();
  }
}

bool IncrementalMarking::ShouldFinalize() const {
  DCHECK(IsMarking());

  const auto* cpp_heap = CppHeap::From(heap_->cpp_heap());
  return heap()
             ->mark_compact_collector()
             ->local_marking_worklists()
             ->IsEmpty() &&
         (!cpp_heap || cpp_heap->ShouldFinalizeIncrementalMarking());
}

void IncrementalMarking::FetchBytesMarkedConcurrently() {
  if (!v8_flags.concurrent_marking) return;

  const size_t current_bytes_marked_concurrently =
      heap()->concurrent_marking()->TotalMarkedBytes();
  // The concurrent_marking()->TotalMarkedBytes() is not monotonic for a
  // short period of time when a concurrent marking task is finishing.
  if (current_bytes_marked_concurrently > bytes_marked_concurrently_) {
    const size_t delta =
        current_bytes_marked_concurrently - bytes_marked_concurrently_;
    schedule_->AddConcurrentlyMarkedBytes(delta);
    bytes_marked_concurrently_ = current_bytes_marked_concurrently;
  }
}

void IncrementalMarking::Step(v8::base::TimeDelta max_duration,
                              size_t max_bytes_to_process,
                              StepOrigin step_origin) {
  NestedTimedHistogramScope incremental_marking_scope(
      isolate()->counters()->gc_incremental_marking());
  TRACE_EVENT1("v8", "V8.GCIncrementalMarking", "epoch",
               heap_->tracer()->CurrentEpoch(GCTracer::Scope::MC_INCREMENTAL));
  TRACE_GC_EPOCH_WITH_FLOW(
      heap_->tracer(), GCTracer::Scope::MC_INCREMENTAL, ThreadKind::kMain,
      current_trace_id_.value(),
      TRACE_EVENT_FLAG_FLOW_IN | TRACE_EVENT_FLAG_FLOW_OUT);
  DCHECK(IsMajorMarking());
  const auto start = v8::base::TimeTicks::Now();

  std::optional<SafepointScope> safepoint_scope;
  // Conceptually an incremental marking step (even though it always runs on the
  // main thread) may introduce a form of concurrent marking when background
  // threads access the heap concurrently (e.g. concurrent compilation). On
  // builds that verify concurrent heap accesses this may lead to false positive
  // reports. We can avoid this by stopping background threads just in this
  // configuration. This should not hide potential issues because the concurrent
  // marker doesn't rely on correct synchronization but e.g. on black allocation
  // and the on_hold worklist.
#ifndef V8_ATOMIC_OBJECT_FIELD_WRITES
  DCHECK(!v8_flags.concurrent_marking);
  safepoint_scope.emplace(isolate(), SafepointKind::kIsolate);
#endif

  size_t v8_bytes_processed = 0;
  v8::base::TimeDelta embedder_duration;
  v8::base::TimeDelta max_embedder_duration;

  if (v8_flags.concurrent_marking) {
    // It is safe to merge back all objects that were on hold to the shared
    // work list at Step because we are at a safepoint where all objects
    // are properly initialized. The exception is the last allocated object
    // before invoking an AllocationObserver. This allocation had no way to
    // escape and get marked though.
    local_marking_worklists()->MergeOnHold();

    heap()->mark_compact_collector()->MaybeEnableBackgroundThreadsInCycle(
        MarkCompactCollector::CallOrigin::kIncrementalMarkingStep);
  }
  if (step_origin == StepOrigin::kTask) {
    // We cannot publish the pending allocations for V8 step origin because the
    // last object was allocated before invoking the step.
    heap()->PublishMainThreadPendingAllocations();
  }

  // Perform a single V8 and a single embedder step. In case both have been
  // observed as empty back to back, we can finalize.
  //
  // This ignores that case where the embedder finds new V8-side objects. The
  // assumption is that large graphs are well connected and can mostly be
  // processed on their own. For small graphs, helping is not necessary.
  std::tie(v8_bytes_processed, std::ignore) =
      major_collector_->ProcessMarkingWorklist(
          max_duration, max_bytes_to_process,
          MarkCompactCollector::MarkingWorklistProcessingMode::kDefault);
  main_thread_marked_bytes_ += v8_bytes_processed;
  schedule_->UpdateMutatorThreadMarkedBytes(main_thread_marked_bytes_);
  const auto v8_time = v8::base::TimeTicks::Now() - start;
  if (heap_->cpp_heap() && (v8_time < max_duration)) {
    // The CppHeap only gets the remaining slice and not the exact same time.
    // This is fine because CppHeap will schedule its own incremental steps. We
    // want to help out here to be able to fully finalize when all worklists
    // have been drained.
    max_embedder_duration = max_duration - v8_time;
    embedder_duration = EmbedderStep(max_embedder_duration);
  }

  if (v8_flags.concurrent_marking) {
    local_marking_worklists()->ShareWork();
    heap_->concurrent_marking()->RescheduleJobIfNeeded(
        GarbageCollector::MARK_COMPACTOR);
  }

  heap_->tracer()->AddIncrementalMarkingStep(v8_time.InMillisecondsF(),
                                             v8_bytes_processed);

  if (V8_UNLIKELY(v8_flags.trace_incremental_marking)) {
    isolate()->PrintWithTimestamp(
        "[IncrementalMarking] Step: origin: %s, V8: %zuKB (%zuKB) in %.1f, "
        "embedder: %fms (%fms) in %.1f (%.1f), V8 marking speed: %.fMB/s\n",
        ToString(step_origin), v8_bytes_processed / KB,
        max_bytes_to_process / KB, v8_time.InMillisecondsF(),
        embedder_duration.InMillisecondsF(),
        max_embedder_duration.InMillisecondsF(),
        (v8::base::TimeTicks::Now() - start).InMillisecondsF(),
        max_duration.InMillisecondsF(),
        heap()->tracer()->IncrementalMarkingSpeedInBytesPerMillisecond() *
            1000 / MB);
  }
}

Isolate* IncrementalMarking::isolate() const { return heap_->isolate(); }

IncrementalMarking::PauseBlackAllocationScope::PauseBlackAllocationScope(
    IncrementalMarking* marking)
    : marking_(marking) {
  if (marking_->black_allocation()) {
    paused_ = true;
    marking_->PauseBlackAllocation();
  }
}

IncrementalMarking::PauseBlackAllocationScope::~PauseBlackAllocationScope() {
  if (paused_) {
    marking_->StartBlackAllocation();
  }
}

}  // namespace internal
}  // namespace v8
