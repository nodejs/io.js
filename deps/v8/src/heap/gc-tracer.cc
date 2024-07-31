// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/gc-tracer.h"

#include <cstdarg>

#include "include/v8-metrics.h"
#include "src/base/atomic-utils.h"
#include "src/base/logging.h"
#include "src/base/optional.h"
#include "src/base/platform/time.h"
#include "src/base/strings.h"
#include "src/common/globals.h"
#include "src/execution/thread-id.h"
#include "src/heap/cppgc-js/cpp-heap.h"
#include "src/heap/cppgc/metric-recorder.h"
#include "src/heap/gc-tracer-inl.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap.h"
#include "src/heap/incremental-marking.h"
#include "src/heap/memory-balancer.h"
#include "src/heap/spaces.h"
#include "src/logging/counters.h"
#include "src/logging/metrics.h"
#include "src/logging/tracing-flags.h"
#include "src/tracing/tracing-category-observer.h"

namespace v8 {
namespace internal {

static size_t CountTotalHolesSize(Heap* heap) {
  size_t holes_size = 0;
  PagedSpaceIterator spaces(heap);
  for (PagedSpace* space = spaces.Next(); space != nullptr;
       space = spaces.Next()) {
    DCHECK_GE(holes_size + space->Waste() + space->Available(), holes_size);
    holes_size += space->Waste() + space->Available();
  }
  return holes_size;
}

namespace {

std::atomic<CollectionEpoch> global_epoch{0};

CollectionEpoch next_epoch() {
  return global_epoch.fetch_add(1, std::memory_order_relaxed) + 1;
}

using BytesAndDuration = ::heap::base::BytesAndDuration;

double BoundedAverageSpeed(
    const base::RingBuffer<BytesAndDuration>& buffer,
    v8::base::Optional<v8::base::TimeDelta> selected_duration) {
  constexpr size_t kMinNonEmptySpeedInBytesPerMs = 1;
  constexpr size_t kMaxSpeedInBytesPerMs = GB;
  return ::heap::base::AverageSpeed(
      buffer, BytesAndDuration(), selected_duration,
      kMinNonEmptySpeedInBytesPerMs, kMaxSpeedInBytesPerMs);
}

double BoundedAverageSpeed(const base::RingBuffer<BytesAndDuration>& buffer) {
  return BoundedAverageSpeed(buffer, base::nullopt);
}

}  // namespace

GCTracer::Event::Event(Type type, State state,
                       GarbageCollectionReason gc_reason,
                       const char* collector_reason)
    : type(type),
      state(state),
      gc_reason(gc_reason),
      collector_reason(collector_reason) {}

const char* ToString(GCTracer::Event::Type type, bool short_name) {
  switch (type) {
    case GCTracer::Event::Type::SCAVENGER:
      return (short_name) ? "s" : "Scavenge";
    case GCTracer::Event::Type::MARK_COMPACTOR:
    case GCTracer::Event::Type::INCREMENTAL_MARK_COMPACTOR:
      return (short_name) ? "mc" : "Mark-Compact";
    case GCTracer::Event::Type::MINOR_MARK_SWEEPER:
    case GCTracer::Event::Type::INCREMENTAL_MINOR_MARK_SWEEPER:
      return (short_name) ? "mms" : "Minor Mark-Sweep";
    case GCTracer::Event::Type::START:
      return (short_name) ? "st" : "Start";
  }
}

GCTracer::RecordGCPhasesInfo::RecordGCPhasesInfo(
    Heap* heap, GarbageCollector collector, GarbageCollectionReason reason) {
  if (Heap::IsYoungGenerationCollector(collector)) {
    type_timer_ = nullptr;
    type_priority_timer_ = nullptr;
    if (!v8_flags.minor_ms) {
      mode_ = Mode::Scavenger;
      trace_event_name_ = "V8.GCScavenger";
    } else {
      mode_ = Mode::None;
      trace_event_name_ = "V8.GCMinorMS";
    }
  } else {
    DCHECK_EQ(GarbageCollector::MARK_COMPACTOR, collector);
    Counters* counters = heap->isolate()->counters();
    const bool in_background = heap->isolate()->IsIsolateInBackground();
    const bool is_incremental = !heap->incremental_marking()->IsStopped();
    mode_ = Mode::None;
    // The following block selects histogram counters to emit. The trace event
    // name should be changed when metrics are updated.
    //
    // Memory reducing GCs take priority over memory measurement GCs. They can
    // happen at the same time when measuring memory is folded into a memory
    // reducing GC.
    if (is_incremental) {
      if (heap->ShouldReduceMemory()) {
        type_timer_ = counters->gc_finalize_incremental_memory_reducing();
        type_priority_timer_ =
            in_background
                ? counters->gc_finalize_incremental_memory_reducing_background()
                : counters
                      ->gc_finalize_incremental_memory_reducing_foreground();
        trace_event_name_ = "V8.GCFinalizeMCReduceMemory";
      } else if (reason == GarbageCollectionReason::kMeasureMemory) {
        type_timer_ = counters->gc_finalize_incremental_memory_measure();
        type_priority_timer_ =
            in_background
                ? counters->gc_finalize_incremental_memory_measure_background()
                : counters->gc_finalize_incremental_memory_measure_foreground();
        trace_event_name_ = "V8.GCFinalizeMCMeasureMemory";
      } else {
        type_timer_ = counters->gc_finalize_incremental_regular();
        type_priority_timer_ =
            in_background
                ? counters->gc_finalize_incremental_regular_background()
                : counters->gc_finalize_incremental_regular_foreground();
        trace_event_name_ = "V8.GCFinalizeMC";
        mode_ = Mode::Finalize;
      }
    } else {
      trace_event_name_ = "V8.GCCompactor";
      if (heap->ShouldReduceMemory()) {
        type_timer_ = counters->gc_finalize_non_incremental_memory_reducing();
        type_priority_timer_ =
            in_background
                ? counters
                      ->gc_finalize_non_incremental_memory_reducing_background()
                : counters
                      ->gc_finalize_non_incremental_memory_reducing_foreground();
      } else if (reason == GarbageCollectionReason::kMeasureMemory) {
        type_timer_ = counters->gc_finalize_non_incremental_memory_measure();
        type_priority_timer_ =
            in_background
                ? counters
                      ->gc_finalize_non_incremental_memory_measure_background()
                : counters
                      ->gc_finalize_non_incremental_memory_measure_foreground();
      } else {
        type_timer_ = counters->gc_finalize_non_incremental_regular();
        type_priority_timer_ =
            in_background
                ? counters->gc_finalize_non_incremental_regular_background()
                : counters->gc_finalize_non_incremental_regular_foreground();
      }
    }
  }
}

GCTracer::GCTracer(Heap* heap, base::TimeTicks startup_time,
                   GarbageCollectionReason initial_gc_reason)
    : heap_(heap),
      current_(Event::Type::START, Event::State::NOT_RUNNING, initial_gc_reason,
               nullptr),
      previous_(current_),
      allocation_time_(startup_time),
      previous_mark_compact_end_time_(startup_time) {
  // All accesses to incremental_marking_scope assume that incremental marking
  // scopes come first.
  static_assert(0 == Scope::FIRST_INCREMENTAL_SCOPE);
  // We assume that MC_INCREMENTAL is the first scope so that we can properly
  // map it to RuntimeCallStats.
  static_assert(0 == Scope::MC_INCREMENTAL);
  // Starting a new cycle will make the current event the previous event.
  // Setting the current end time here allows us to refer back to a previous
  // event's end time to compute time spent in mutator.
  current_.end_time = previous_mark_compact_end_time_;
}

void GCTracer::ResetForTesting() {
  this->~GCTracer();
  new (this) GCTracer(heap_, base::TimeTicks::Now(),
                      GarbageCollectionReason::kTesting);
}

void GCTracer::StartObservablePause(base::TimeTicks time) {
  DCHECK(!IsInObservablePause());
  start_of_observable_pause_.emplace(time);
}

void GCTracer::UpdateCurrentEvent(GarbageCollectionReason gc_reason,
                                  const char* collector_reason) {
  // For incremental marking, the event has already been created and we just
  // need to update a few fields.
  DCHECK(current_.type == Event::Type::INCREMENTAL_MARK_COMPACTOR ||
         current_.type == Event::Type::INCREMENTAL_MINOR_MARK_SWEEPER);
  DCHECK_EQ(Event::State::ATOMIC, current_.state);
  DCHECK(IsInObservablePause());
  current_.gc_reason = gc_reason;
  current_.collector_reason = collector_reason;
  // TODO(chromium:1154636): The start_time of the current event contains
  // currently the start time of the observable pause. This should be
  // reconsidered.
  current_.start_time = start_of_observable_pause_.value();
  current_.reduce_memory = heap_->ShouldReduceMemory();
}

void GCTracer::StartCycle(GarbageCollector collector,
                          GarbageCollectionReason gc_reason,
                          const char* collector_reason, MarkingType marking) {
  // We cannot start a new cycle while there's another one in its atomic pause.
  DCHECK_NE(Event::State::ATOMIC, current_.state);
  // We cannot start a new cycle while a young generation GC cycle has
  // already interrupted a full GC cycle.
  DCHECK(!young_gc_while_full_gc_);

  young_gc_while_full_gc_ = current_.state != Event::State::NOT_RUNNING;
  if (young_gc_while_full_gc_) {
    // The cases for interruption are: Scavenger, MinorMS interrupting sweeping.
    // In both cases we are fine with fetching background counters now and
    // fixing them up later in StopAtomicPause().
    FetchBackgroundCounters();
  }

  DCHECK_IMPLIES(young_gc_while_full_gc_,
                 Heap::IsYoungGenerationCollector(collector));
  DCHECK_IMPLIES(young_gc_while_full_gc_,
                 !Event::IsYoungGenerationEvent(current_.type));

  Event::Type type;
  switch (collector) {
    case GarbageCollector::SCAVENGER:
      type = Event::Type::SCAVENGER;
      break;
    case GarbageCollector::MINOR_MARK_SWEEPER:
      type = marking == MarkingType::kIncremental
                 ? Event::Type::INCREMENTAL_MINOR_MARK_SWEEPER
                 : Event::Type::MINOR_MARK_SWEEPER;
      break;
    case GarbageCollector::MARK_COMPACTOR:
      type = marking == MarkingType::kIncremental
                 ? Event::Type::INCREMENTAL_MARK_COMPACTOR
                 : Event::Type::MARK_COMPACTOR;
      break;
  }

  DCHECK_IMPLIES(!young_gc_while_full_gc_,
                 current_.state == Event::State::NOT_RUNNING);
  DCHECK_EQ(Event::State::NOT_RUNNING, previous_.state);

  previous_ = current_;
  current_ = Event(type, Event::State::MARKING, gc_reason, collector_reason);

  switch (marking) {
    case MarkingType::kAtomic:
      DCHECK(IsInObservablePause());
      // TODO(chromium:1154636): The start_time of the current event contains
      // currently the start time of the observable pause. This should be
      // reconsidered.
      current_.start_time = start_of_observable_pause_.value();
      current_.reduce_memory = heap_->ShouldReduceMemory();
      break;
    case MarkingType::kIncremental:
      // The current event will be updated later.
      DCHECK_IMPLIES(Heap::IsYoungGenerationCollector(collector),
                     (v8_flags.minor_ms &&
                      collector == GarbageCollector::MINOR_MARK_SWEEPER));
      DCHECK(!IsInObservablePause());
      break;
  }

  if (Heap::IsYoungGenerationCollector(collector)) {
    epoch_young_ = next_epoch();
  } else {
    epoch_full_ = next_epoch();
  }
}

void GCTracer::StartAtomicPause() {
  DCHECK_EQ(Event::State::MARKING, current_.state);
  current_.state = Event::State::ATOMIC;
}

void GCTracer::StartInSafepoint(base::TimeTicks time) {
  SampleAllocation(current_.start_time, heap_->NewSpaceAllocationCounter(),
                   heap_->OldGenerationAllocationCounter(),
                   heap_->EmbedderAllocationCounter());

  current_.start_object_size = heap_->SizeOfObjects();
  current_.start_memory_size = heap_->memory_allocator()->Size();
  current_.start_holes_size = CountTotalHolesSize(heap_);
  size_t new_space_size = (heap_->new_space() ? heap_->new_space()->Size() : 0);
  size_t new_lo_space_size =
      (heap_->new_lo_space() ? heap_->new_lo_space()->SizeOfObjects() : 0);
  current_.young_object_size = new_space_size + new_lo_space_size;
  current_.start_atomic_pause_time = time;
}

void GCTracer::StopInSafepoint(base::TimeTicks time) {
  current_.end_object_size = heap_->SizeOfObjects();
  current_.end_memory_size = heap_->memory_allocator()->Size();
  current_.end_holes_size = CountTotalHolesSize(heap_);
  current_.survived_young_object_size = heap_->SurvivedYoungObjectSize();
  current_.end_atomic_pause_time = time;

  // Do not include the GC pause for calculating the allocation rate. GC pause
  // with heap verification can decrease the allocation rate significantly.
  allocation_time_ = time;

  if (v8_flags.memory_balancer) {
    UpdateMemoryBalancerGCSpeed();
  }
}

void GCTracer::StopObservablePause(GarbageCollector collector,
                                   base::TimeTicks time) {
  DCHECK(IsConsistentWithCollector(collector));
  DCHECK(IsInObservablePause());
  start_of_observable_pause_.reset();

  // TODO(chromium:1154636): The end_time of the current event contains
  // currently the end time of the observable pause. This should be
  // reconsidered.
  current_.end_time = time;

  FetchBackgroundCounters();

  const base::TimeDelta duration = current_.end_time - current_.start_time;
  auto* long_task_stats = heap_->isolate()->GetCurrentLongTaskStats();
  const bool is_young = Heap::IsYoungGenerationCollector(collector);
  if (is_young) {
    recorded_minor_gcs_total_.Push(
        BytesAndDuration(current_.young_object_size, duration));
    recorded_minor_gcs_survived_.Push(
        BytesAndDuration(current_.survived_young_object_size, duration));
    long_task_stats->gc_young_wall_clock_duration_us +=
        duration.InMicroseconds();
  } else {
    if (current_.type == Event::Type::INCREMENTAL_MARK_COMPACTOR) {
      RecordIncrementalMarkingSpeed(current_.incremental_marking_bytes,
                                    current_.incremental_marking_duration);
      recorded_incremental_mark_compacts_.Push(
          BytesAndDuration(current_.end_object_size, duration));
      for (int i = 0; i < Scope::NUMBER_OF_INCREMENTAL_SCOPES; i++) {
        current_.incremental_scopes[i] = incremental_scopes_[i];
        current_.scopes[i] = incremental_scopes_[i].duration;
        new (&incremental_scopes_[i]) IncrementalInfos;
      }
    } else {
      recorded_mark_compacts_.Push(
          BytesAndDuration(current_.end_object_size, duration));
      DCHECK_EQ(0u, current_.incremental_marking_bytes);
      DCHECK(current_.incremental_marking_duration.IsZero());
    }
    RecordGCSumCounters();
    combined_mark_compact_speed_cache_ = 0.0;
    long_task_stats->gc_full_atomic_wall_clock_duration_us +=
        duration.InMicroseconds();
    RecordMutatorUtilization(current_.end_time,
                             duration + current_.incremental_marking_duration);
  }

  heap_->UpdateTotalGCTime(duration);

  if (v8_flags.trace_gc_ignore_scavenger && is_young) return;

  if (v8_flags.trace_gc_nvp) {
    PrintNVP();
  } else {
    Print();
  }

  if (v8_flags.trace_gc) {
    heap_->PrintShortHeapStatistics();
  }

  if (V8_UNLIKELY(TracingFlags::gc.load(std::memory_order_relaxed) &
                  v8::tracing::TracingCategoryObserver::ENABLED_BY_TRACING)) {
    TRACE_GC_NOTE("V8.GC_HEAP_DUMP_STATISTICS");
    std::stringstream heap_stats;
    heap_->DumpJSONHeapStatistics(heap_stats);

    TRACE_EVENT_INSTANT1(TRACE_DISABLED_BY_DEFAULT("v8.gc"), "V8.GC_Heap_Stats",
                         TRACE_EVENT_SCOPE_THREAD, "stats",
                         TRACE_STR_COPY(heap_stats.str().c_str()));
  }
}

void GCTracer::UpdateMemoryBalancerGCSpeed() {
  DCHECK(v8_flags.memory_balancer);
  size_t major_gc_bytes = current_.start_object_size;
  const base::TimeDelta atomic_pause_duration =
      current_.end_atomic_pause_time - current_.start_atomic_pause_time;
  const base::TimeDelta blocked_time_taken =
      atomic_pause_duration + current_.incremental_marking_duration;
  base::TimeDelta concurrent_gc_time;
  {
    base::MutexGuard guard(&background_scopes_mutex_);
    concurrent_gc_time =
        background_scopes_[Scope::MC_BACKGROUND_EVACUATE_COPY] +
        background_scopes_[Scope::MC_BACKGROUND_EVACUATE_UPDATE_POINTERS] +
        background_scopes_[Scope::MC_BACKGROUND_MARKING] +
        background_scopes_[Scope::MC_BACKGROUND_SWEEPING];
  }
  const base::TimeDelta major_gc_duration =
      blocked_time_taken + concurrent_gc_time;
  const base::TimeDelta major_allocation_duration =
      (current_.end_atomic_pause_time - previous_mark_compact_end_time_) -
      blocked_time_taken;
  CHECK_GE(major_allocation_duration, base::TimeDelta());

  heap_->mb_->UpdateGCSpeed(major_gc_bytes, major_gc_duration);
}

void GCTracer::StopAtomicPause() {
  DCHECK_EQ(Event::State::ATOMIC, current_.state);
  current_.state = Event::State::SWEEPING;
}

void GCTracer::StopCycle(GarbageCollector collector) {
  DCHECK_EQ(Event::State::SWEEPING, current_.state);
  current_.state = Event::State::NOT_RUNNING;

  DCHECK(IsConsistentWithCollector(collector));

  FetchBackgroundCounters();

  if (Heap::IsYoungGenerationCollector(collector)) {
    ReportYoungCycleToRecorder();

    // If a young generation GC interrupted an unfinished full GC cycle, restore
    // the event corresponding to the full GC cycle.
    if (young_gc_while_full_gc_) {
      // Sweeping for full GC could have occured during the young GC. Copy over
      // any sweeping scope values to the previous_ event. The full GC sweeping
      // scopes are never reported by young cycles.
      previous_.scopes[Scope::MC_SWEEP] += current_.scopes[Scope::MC_SWEEP];
      previous_.scopes[Scope::MC_BACKGROUND_SWEEPING] +=
          current_.scopes[Scope::MC_BACKGROUND_SWEEPING];
      std::swap(current_, previous_);
      young_gc_while_full_gc_ = false;
    }
  } else {
    ReportFullCycleToRecorder();

    heap_->isolate()->counters()->mark_compact_reason()->AddSample(
        static_cast<int>(current_.gc_reason));

    if (v8_flags.trace_gc_freelists) {
      PrintIsolate(heap_->isolate(),
                   "FreeLists statistics before collection:\n");
      heap_->PrintFreeListsStats();
    }
  }
}

void GCTracer::StopFullCycleIfNeeded() {
  if (current_.state != Event::State::SWEEPING) return;
  if (!notified_full_sweeping_completed_) return;
  if (heap_->cpp_heap() && !notified_full_cppgc_completed_) return;
  StopCycle(GarbageCollector::MARK_COMPACTOR);
  notified_full_sweeping_completed_ = false;
  notified_full_cppgc_completed_ = false;
  full_cppgc_completed_during_minor_gc_ = false;
}

void GCTracer::StopYoungCycleIfNeeded() {
  DCHECK(Event::IsYoungGenerationEvent(current_.type));
  if (current_.state != Event::State::SWEEPING) return;
  if ((current_.type == Event::Type::MINOR_MARK_SWEEPER ||
       current_.type == Event::Type::INCREMENTAL_MINOR_MARK_SWEEPER) &&
      !notified_young_sweeping_completed_)
    return;
  // Check if young cppgc was scheduled but hasn't completed yet.
  if (heap_->cpp_heap() && notified_young_cppgc_running_ &&
      !notified_young_cppgc_completed_)
    return;
  bool was_young_gc_while_full_gc_ = young_gc_while_full_gc_;
  StopCycle(current_.type == Event::Type::SCAVENGER
                ? GarbageCollector::SCAVENGER
                : GarbageCollector::MINOR_MARK_SWEEPER);
  notified_young_sweeping_completed_ = false;
  notified_young_cppgc_running_ = false;
  notified_young_cppgc_completed_ = false;
  if (was_young_gc_while_full_gc_) {
    // Check if the full gc cycle is ready to be stopped.
    StopFullCycleIfNeeded();
  }
}

void GCTracer::NotifyFullSweepingCompleted() {
  // Notifying twice that V8 sweeping is finished for the same cycle is possible
  // only if Oilpan sweeping is still in progress.
  DCHECK_IMPLIES(
      notified_full_sweeping_completed_,
      !notified_full_cppgc_completed_ || full_cppgc_completed_during_minor_gc_);

  if (Event::IsYoungGenerationEvent(current_.type)) {
    bool was_young_gc_while_full_gc = young_gc_while_full_gc_;
    bool was_full_sweeping_notified = notified_full_sweeping_completed_;
    NotifyYoungSweepingCompleted();
    // NotifyYoungSweepingCompleted checks if the full cycle needs to be stopped
    // as well. If full sweeping was already notified, nothing more needs to be
    // done here.
    if (!was_young_gc_while_full_gc || was_full_sweeping_notified) return;
  }

  DCHECK(!Event::IsYoungGenerationEvent(current_.type));
  // Sweeping finalization can also be triggered from inside a full GC cycle's
  // atomic pause.
  DCHECK(current_.state == Event::State::SWEEPING ||
         current_.state == Event::State::ATOMIC);

  // Stop a full GC cycle only when both v8 and cppgc (if available) GCs have
  // finished sweeping. This method is invoked by v8.
  if (v8_flags.trace_gc_freelists) {
    PrintIsolate(heap_->isolate(),
                 "FreeLists statistics after sweeping completed:\n");
    heap_->PrintFreeListsStats();
  }
  notified_full_sweeping_completed_ = true;
  StopFullCycleIfNeeded();
}

void GCTracer::NotifyYoungSweepingCompleted() {
  if (!Event::IsYoungGenerationEvent(current_.type)) return;
  if (v8_flags.verify_heap) {
    // If heap verification is enabled, sweeping finalization can also be
    // triggered from inside a full GC cycle's atomic pause.
    DCHECK(current_.type == Event::Type::MINOR_MARK_SWEEPER ||
           current_.type == Event::Type::INCREMENTAL_MINOR_MARK_SWEEPER);
    DCHECK(current_.state == Event::State::SWEEPING ||
           current_.state == Event::State::ATOMIC);
  } else {
    DCHECK(IsSweepingInProgress());
  }

  DCHECK(!notified_young_sweeping_completed_);
  notified_young_sweeping_completed_ = true;
  StopYoungCycleIfNeeded();
}

void GCTracer::NotifyFullCppGCCompleted() {
  // Stop a full GC cycle only when both v8 and cppgc (if available) GCs have
  // finished sweeping. This method is invoked by cppgc.
  DCHECK(heap_->cpp_heap());
  const auto* metric_recorder =
      CppHeap::From(heap_->cpp_heap())->GetMetricRecorder();
  USE(metric_recorder);
  DCHECK(metric_recorder->FullGCMetricsReportPending());
  DCHECK(!notified_full_cppgc_completed_);
  notified_full_cppgc_completed_ = true;
  // Cppgc sweeping may finalize during MinorMS sweeping. In that case, delay
  // stopping the cycle until the nested MinorMS cycle is stopped.
  if (Event::IsYoungGenerationEvent(current_.type)) {
    DCHECK(young_gc_while_full_gc_);
    full_cppgc_completed_during_minor_gc_ = true;
    return;
  }
  StopFullCycleIfNeeded();
}

void GCTracer::NotifyYoungCppGCCompleted() {
  // Stop a young GC cycle only when both v8 and cppgc (if available) GCs have
  // finished sweeping. This method is invoked by cppgc.
  DCHECK(heap_->cpp_heap());
  DCHECK(notified_young_cppgc_running_);
  const auto* metric_recorder =
      CppHeap::From(heap_->cpp_heap())->GetMetricRecorder();
  USE(metric_recorder);
  DCHECK(metric_recorder->YoungGCMetricsReportPending());
  DCHECK(!notified_young_cppgc_completed_);
  notified_young_cppgc_completed_ = true;
  StopYoungCycleIfNeeded();
}

void GCTracer::NotifyYoungCppGCRunning() {
  DCHECK(!notified_young_cppgc_running_);
  notified_young_cppgc_running_ = true;
}

void GCTracer::SampleAllocation(base::TimeTicks current,
                                size_t new_space_counter_bytes,
                                size_t old_generation_counter_bytes,
                                size_t embedder_counter_bytes) {
  // This assumes that counters are unsigned integers so that the subtraction
  // below works even if the new counter is less than the old counter.
  size_t new_space_allocated_bytes =
      new_space_counter_bytes - new_space_allocation_counter_bytes_;
  size_t old_generation_allocated_bytes =
      old_generation_counter_bytes - old_generation_allocation_counter_bytes_;
  size_t embedder_allocated_bytes =
      embedder_counter_bytes - embedder_allocation_counter_bytes_;
  const base::TimeDelta allocation_duration = current - allocation_time_;
  allocation_time_ = current;

  new_space_allocation_counter_bytes_ = new_space_counter_bytes;
  old_generation_allocation_counter_bytes_ = old_generation_counter_bytes;
  embedder_allocation_counter_bytes_ = embedder_counter_bytes;

  recorded_new_generation_allocations_.Push(
      BytesAndDuration(new_space_allocated_bytes, allocation_duration));
  recorded_old_generation_allocations_.Push(
      BytesAndDuration(old_generation_allocated_bytes, allocation_duration));
  recorded_embedder_generation_allocations_.Push(
      BytesAndDuration(embedder_allocated_bytes, allocation_duration));

  if (v8_flags.memory_balancer) {
    heap_->mb_->UpdateAllocationRate(old_generation_allocated_bytes,
                                     allocation_duration);
  }
}

void GCTracer::NotifyMarkingStart() {
  const auto marking_start = base::TimeTicks::Now();

  uint16_t result = 1;
  if (last_marking_start_time_.has_value()) {
    const double diff_in_seconds = std::round(
        (marking_start - last_marking_start_time_.value()).InSecondsF());
    if (diff_in_seconds > UINT16_MAX) {
      result = UINT16_MAX;
    } else if (diff_in_seconds >= 1) {
      result = static_cast<uint16_t>(diff_in_seconds);
    }
  }
  DCHECK_GT(result, 0);
  DCHECK_LE(result, UINT16_MAX);

  code_flushing_increase_s_ = result;
  last_marking_start_time_ = marking_start;

  if (V8_UNLIKELY(v8_flags.trace_flush_code)) {
    PrintIsolate(heap_->isolate(), "code flushing time: %d second(s)\n",
                 code_flushing_increase_s_);
  }
}

uint16_t GCTracer::CodeFlushingIncrease() const {
  return code_flushing_increase_s_;
}

void GCTracer::AddCompactionEvent(double duration,
                                  size_t live_bytes_compacted) {
  recorded_compactions_.Push(BytesAndDuration(
      live_bytes_compacted, base::TimeDelta::FromMillisecondsD(duration)));
}

void GCTracer::AddSurvivalRatio(double promotion_ratio) {
  recorded_survival_ratios_.Push(promotion_ratio);
}

void GCTracer::AddIncrementalMarkingStep(double duration, size_t bytes) {
  if (bytes > 0) {
    current_.incremental_marking_bytes += bytes;
    current_.incremental_marking_duration +=
        base::TimeDelta::FromMillisecondsD(duration);
  }
  ReportIncrementalMarkingStepToRecorder(duration);
}

void GCTracer::AddIncrementalSweepingStep(double duration) {
  ReportIncrementalSweepingStepToRecorder(duration);
}

void GCTracer::Output(const char* format, ...) const {
  if (v8_flags.trace_gc) {
    va_list arguments;
    va_start(arguments, format);
    base::OS::VPrint(format, arguments);
    va_end(arguments);
  }

  const int kBufferSize = 256;
  char raw_buffer[kBufferSize];
  base::Vector<char> buffer(raw_buffer, kBufferSize);
  va_list arguments2;
  va_start(arguments2, format);
  base::VSNPrintF(buffer, format, arguments2);
  va_end(arguments2);

  heap_->AddToRingBuffer(buffer.begin());
}

void GCTracer::Print() const {
  const base::TimeDelta duration = current_.end_time - current_.start_time;
  const size_t kIncrementalStatsSize = 128;
  char incremental_buffer[kIncrementalStatsSize] = {0};

  if (current_.type == Event::Type::INCREMENTAL_MARK_COMPACTOR) {
    base::OS::SNPrintF(
        incremental_buffer, kIncrementalStatsSize,
        " (+ %.1f ms in %d steps since start of marking, "
        "biggest step %.1f ms, walltime since start of marking %.f ms)",
        current_scope(Scope::MC_INCREMENTAL),
        incremental_scope(Scope::MC_INCREMENTAL).steps,
        incremental_scope(Scope::MC_INCREMENTAL).longest_step.InMillisecondsF(),
        (current_.end_time - current_.incremental_marking_start_time)
            .InMillisecondsF());
  }

  const double total_external_time =
      current_scope(Scope::HEAP_EXTERNAL_WEAK_GLOBAL_HANDLES) +
      current_scope(Scope::HEAP_EXTERNAL_EPILOGUE) +
      current_scope(Scope::HEAP_EXTERNAL_PROLOGUE) +
      current_scope(Scope::MC_INCREMENTAL_EXTERNAL_EPILOGUE) +
      current_scope(Scope::MC_INCREMENTAL_EXTERNAL_PROLOGUE);

  // Avoid PrintF as Output also appends the string to the tracing ring buffer
  // that gets printed on OOM failures.
  DCHECK_IMPLIES(young_gc_while_full_gc_,
                 Event::IsYoungGenerationEvent(current_.type));
  Output(
      "[%d:%p] "
      "%8.0f ms: "
      "%s%s%s %.1f (%.1f) -> %.1f (%.1f) MB, "
      "pooled: %1.f MB, "
      "%.2f / %.2f ms %s (average mu = %.3f, current mu = %.3f) %s; %s\n",
      base::OS::GetCurrentProcessId(),
      reinterpret_cast<void*>(heap_->isolate()),
      heap_->isolate()->time_millis_since_init(),
      ToString(current_.type, false), current_.reduce_memory ? " (reduce)" : "",
      young_gc_while_full_gc_ ? " (interleaved)" : "",
      static_cast<double>(current_.start_object_size) / MB,
      static_cast<double>(current_.start_memory_size) / MB,
      static_cast<double>(current_.end_object_size) / MB,
      static_cast<double>(current_.end_memory_size) / MB,
      static_cast<double>(
          heap_->memory_allocator()->pool()->CommittedBufferedMemory()) /
          MB,
      duration.InMillisecondsF(), total_external_time, incremental_buffer,
      AverageMarkCompactMutatorUtilization(),
      CurrentMarkCompactMutatorUtilization(), ToString(current_.gc_reason),
      current_.collector_reason != nullptr ? current_.collector_reason : "");
}

void GCTracer::PrintNVP() const {
  const base::TimeDelta duration = current_.end_time - current_.start_time;
  const base::TimeDelta spent_in_mutator =
      current_.start_time - previous_.end_time;
  size_t allocated_since_last_gc =
      current_.start_object_size - previous_.end_object_size;

  base::TimeDelta incremental_walltime_duration;
  if (current_.type == Event::Type::INCREMENTAL_MARK_COMPACTOR) {
    incremental_walltime_duration =
        current_.end_time - current_.incremental_marking_start_time;
  }

  // Avoid data races when printing the background scopes.
  base::MutexGuard guard(&background_scopes_mutex_);

  switch (current_.type) {
    case Event::Type::SCAVENGER:
      heap_->isolate()->PrintWithTimestamp(
          "pause=%.1f "
          "mutator=%.1f "
          "gc=%s "
          "reduce_memory=%d "
          "interleaved=%d "
          "time_to_safepoint=%.2f "
          "heap.prologue=%.2f "
          "heap.epilogue=%.2f "
          "heap.epilogue.reduce_new_space=%.2f "
          "heap.external.prologue=%.2f "
          "heap.external.epilogue=%.2f "
          "heap.external_weak_global_handles=%.2f "
          "complete.sweep_array_buffers=%.2f "
          "scavenge=%.2f "
          "scavenge.free_remembered_set=%.2f "
          "scavenge.roots=%.2f "
          "scavenge.weak=%.2f "
          "scavenge.weak_global_handles.identify=%.2f "
          "scavenge.weak_global_handles.process=%.2f "
          "scavenge.parallel=%.2f "
          "scavenge.update_refs=%.2f "
          "scavenge.sweep_array_buffers=%.2f "
          "background.scavenge.parallel=%.2f "
          "incremental.steps_count=%d "
          "incremental.steps_took=%.1f "
          "scavenge_throughput=%.f "
          "total_size_before=%zu "
          "total_size_after=%zu "
          "holes_size_before=%zu "
          "holes_size_after=%zu "
          "allocated=%zu "
          "promoted=%zu "
          "new_space_survived=%zu "
          "nodes_died_in_new=%d "
          "nodes_copied_in_new=%d "
          "nodes_promoted=%d "
          "promotion_ratio=%.1f%% "
          "average_survival_ratio=%.1f%% "
          "promotion_rate=%.1f%% "
          "new_space_survive_rate_=%.1f%% "
          "new_space_allocation_throughput=%.1f "
          "pool_chunks=%d\n",
          duration.InMillisecondsF(), spent_in_mutator.InMillisecondsF(),
          ToString(current_.type, true), current_.reduce_memory,
          young_gc_while_full_gc_,
          current_.scopes[Scope::TIME_TO_SAFEPOINT].InMillisecondsF(),
          current_scope(Scope::HEAP_PROLOGUE),
          current_scope(Scope::HEAP_EPILOGUE),
          current_scope(Scope::HEAP_EPILOGUE_REDUCE_NEW_SPACE),
          current_scope(Scope::HEAP_EXTERNAL_PROLOGUE),
          current_scope(Scope::HEAP_EXTERNAL_EPILOGUE),
          current_scope(Scope::HEAP_EXTERNAL_WEAK_GLOBAL_HANDLES),
          current_scope(Scope::SCAVENGER_COMPLETE_SWEEP_ARRAY_BUFFERS),
          current_scope(Scope::SCAVENGER_SCAVENGE),
          current_scope(Scope::SCAVENGER_FREE_REMEMBERED_SET),
          current_scope(Scope::SCAVENGER_SCAVENGE_ROOTS),
          current_scope(Scope::SCAVENGER_SCAVENGE_WEAK),
          current_scope(Scope::SCAVENGER_SCAVENGE_WEAK_GLOBAL_HANDLES_IDENTIFY),
          current_scope(Scope::SCAVENGER_SCAVENGE_WEAK_GLOBAL_HANDLES_PROCESS),
          current_scope(Scope::SCAVENGER_SCAVENGE_PARALLEL),
          current_scope(Scope::SCAVENGER_SCAVENGE_UPDATE_REFS),
          current_scope(Scope::SCAVENGER_SWEEP_ARRAY_BUFFERS),
          current_scope(Scope::SCAVENGER_BACKGROUND_SCAVENGE_PARALLEL),
          incremental_scope(GCTracer::Scope::MC_INCREMENTAL).steps,
          current_scope(Scope::MC_INCREMENTAL),
          ScavengeSpeedInBytesPerMillisecond(), current_.start_object_size,
          current_.end_object_size, current_.start_holes_size,
          current_.end_holes_size, allocated_since_last_gc,
          heap_->promoted_objects_size(),
          heap_->new_space_surviving_object_size(),
          heap_->nodes_died_in_new_space_, heap_->nodes_copied_in_new_space_,
          heap_->nodes_promoted_, heap_->promotion_ratio_,
          AverageSurvivalRatio(), heap_->promotion_rate_,
          heap_->new_space_surviving_rate_,
          NewSpaceAllocationThroughputInBytesPerMillisecond(),
          heap_->memory_allocator()->pool()->NumberOfChunks());
      break;
    case Event::Type::MINOR_MARK_SWEEPER:
    case Event::Type::INCREMENTAL_MINOR_MARK_SWEEPER:
      heap_->isolate()->PrintWithTimestamp(
          "pause=%.1f "
          "mutator=%.1f "
          "gc=%s "
          "reduce_memory=%d "
          "minor_ms=%.2f "
          "time_to_safepoint=%.2f "
          "mark=%.2f "
          "mark.incremental_seed=%.2f "
          "mark.finish_incremental=%.2f "
          "mark.seed=%.2f "
          "mark.traced_handles=%.2f "
          "mark.closure_parallel=%.2f "
          "mark.closure=%.2f "
          "mark.conservative_stack=%.2f "
          "clear=%.2f "
          "clear.string_forwarding_table=%.2f "
          "clear.string_table=%.2f "
          "clear.global_handles=%.2f "
          "complete.sweep_array_buffers=%.2f "
          "complete.sweeping=%.2f "
          "sweep=%.2f "
          "sweep.new=%.2f "
          "sweep.new_lo=%.2f "
          "sweep.update_string_table=%.2f "
          "sweep.start_jobs=%.2f "
          "sweep.array_buffers=%.2f "
          "finish=%.2f "
          "finish.ensure_capacity=%.2f "
          "finish.sweep_array_buffers=%.2f "
          "background.mark=%.2f "
          "background.sweep=%.2f "
          "background.sweep.array_buffers=%.2f "
          "conservative_stack_scanning=%.2f "
          "total_size_before=%zu "
          "total_size_after=%zu "
          "holes_size_before=%zu "
          "holes_size_after=%zu "
          "allocated=%zu "
          "promoted=%zu "
          "new_space_survived=%zu "
          "nodes_died_in_new=%d "
          "nodes_copied_in_new=%d "
          "nodes_promoted=%d "
          "promotion_ratio=%.1f%% "
          "average_survival_ratio=%.1f%% "
          "promotion_rate=%.1f%% "
          "new_space_survive_rate_=%.1f%% "
          "new_space_allocation_throughput=%.1f\n",
          duration.InMillisecondsF(), spent_in_mutator.InMillisecondsF(), "mms",
          current_.reduce_memory, current_scope(Scope::MINOR_MS),
          current_scope(Scope::TIME_TO_SAFEPOINT),
          current_scope(Scope::MINOR_MS_MARK),
          current_scope(Scope::MINOR_MS_MARK_INCREMENTAL_SEED),
          current_scope(Scope::MINOR_MS_MARK_FINISH_INCREMENTAL),
          current_scope(Scope::MINOR_MS_MARK_SEED),
          current_scope(Scope::MINOR_MS_MARK_TRACED_HANDLES),
          current_scope(Scope::MINOR_MS_MARK_CLOSURE_PARALLEL),
          current_scope(Scope::MINOR_MS_MARK_CLOSURE),
          current_scope(Scope::MINOR_MS_MARK_CONSERVATIVE_STACK),
          current_scope(Scope::MINOR_MS_CLEAR),
          current_scope(Scope::MINOR_MS_CLEAR_STRING_FORWARDING_TABLE),
          current_scope(Scope::MINOR_MS_CLEAR_STRING_TABLE),
          current_scope(Scope::MINOR_MS_CLEAR_WEAK_GLOBAL_HANDLES),
          current_scope(Scope::MINOR_MS_COMPLETE_SWEEP_ARRAY_BUFFERS),
          current_scope(Scope::MINOR_MS_COMPLETE_SWEEPING),
          current_scope(Scope::MINOR_MS_SWEEP),
          current_scope(Scope::MINOR_MS_SWEEP_NEW),
          current_scope(Scope::MINOR_MS_SWEEP_NEW_LO),
          current_scope(Scope::MINOR_MS_SWEEP_UPDATE_STRING_TABLE),
          current_scope(Scope::MINOR_MS_SWEEP_START_JOBS),
          current_scope(Scope::YOUNG_ARRAY_BUFFER_SWEEP),
          current_scope(Scope::MINOR_MS_FINISH),
          current_scope(Scope::MINOR_MS_FINISH_ENSURE_CAPACITY),
          current_scope(Scope::MINOR_MS_FINISH_SWEEP_ARRAY_BUFFERS),
          current_scope(Scope::MINOR_MS_BACKGROUND_MARKING),
          current_scope(Scope::MINOR_MS_BACKGROUND_SWEEPING),
          current_scope(Scope::BACKGROUND_YOUNG_ARRAY_BUFFER_SWEEP),
          current_scope(Scope::CONSERVATIVE_STACK_SCANNING),
          current_.start_object_size, current_.end_object_size,
          current_.start_holes_size, current_.end_holes_size,
          allocated_since_last_gc, heap_->promoted_objects_size(),
          heap_->new_space_surviving_object_size(),
          heap_->nodes_died_in_new_space_, heap_->nodes_copied_in_new_space_,
          heap_->nodes_promoted_, heap_->promotion_ratio_,
          AverageSurvivalRatio(), heap_->promotion_rate_,
          heap_->new_space_surviving_rate_,
          NewSpaceAllocationThroughputInBytesPerMillisecond());
      break;
    case Event::Type::MARK_COMPACTOR:
    case Event::Type::INCREMENTAL_MARK_COMPACTOR:
      heap_->isolate()->PrintWithTimestamp(
          "pause=%.1f "
          "mutator=%.1f "
          "gc=%s "
          "reduce_memory=%d "
          "time_to_safepoint=%.2f "
          "heap.prologue=%.2f "
          "heap.embedder_tracing_epilogue=%.2f "
          "heap.epilogue=%.2f "
          "heap.epilogue.reduce_new_space=%.2f "
          "heap.external.prologue=%.1f "
          "heap.external.epilogue=%.1f "
          "heap.external.weak_global_handles=%.1f "
          "clear=%1.f "
          "clear.external_string_table=%.1f "
          "clear.string_forwarding_table=%.1f "
          "clear.weak_global_handles=%.1f "
          "clear.dependent_code=%.1f "
          "clear.maps=%.1f "
          "clear.slots_buffer=%.1f "
          "clear.weak_collections=%.1f "
          "clear.weak_lists=%.1f "
          "clear.weak_references=%.1f "
          "clear.join_job=%.1f "
          "complete.sweep_array_buffers=%.1f "
          "complete.sweeping=%.1f "
          "epilogue=%.1f "
          "evacuate=%.1f "
          "evacuate.candidates=%.1f "
          "evacuate.clean_up=%.1f "
          "evacuate.copy=%.1f "
          "evacuate.prologue=%.1f "
          "evacuate.epilogue=%.1f "
          "evacuate.rebalance=%.1f "
          "evacuate.update_pointers=%.1f "
          "evacuate.update_pointers.to_new_roots=%.1f "
          "evacuate.update_pointers.slots.main=%.1f "
          "evacuate.update_pointers.weak=%.1f "
          "finish=%.1f "
          "finish.sweep_array_buffers=%.1f "
          "mark=%.1f "
          "mark.finish_incremental=%.1f "
          "mark.roots=%.1f "
          "mark.full_closure_parallel=%.1f "
          "mark.full_closure=%.1f "
          "mark.ephemeron.marking=%.1f "
          "mark.ephemeron.linear=%.1f "
          "mark.embedder_prologue=%.1f "
          "mark.embedder_tracing=%.1f "
          "prologue=%.1f "
          "sweep=%.1f "
          "sweep.code=%.1f "
          "sweep.map=%.1f "
          "sweep.new=%.1f "
          "sweep.new_lo=%.1f "
          "sweep.old=%.1f "
          "sweep.start_jobs=%.1f "
          "incremental=%.1f "
          "incremental.finalize=%.1f "
          "incremental.finalize.external.prologue=%.1f "
          "incremental.finalize.external.epilogue=%.1f "
          "incremental.layout_change=%.1f "
          "incremental.sweep_array_buffers=%.1f "
          "incremental.sweeping=%.1f "
          "incremental.embedder_tracing=%.1f "
          "incremental_wrapper_tracing_longest_step=%.1f "
          "incremental_longest_step=%.1f "
          "incremental_steps_count=%d "
          "incremental_marking_throughput=%.f "
          "incremental_walltime_duration=%.f "
          "background.mark=%.1f "
          "background.sweep=%.1f "
          "background.evacuate.copy=%.1f "
          "background.evacuate.update_pointers=%.1f "
          "conservative_stack_scanning=%.2f "
          "total_size_before=%zu "
          "total_size_after=%zu "
          "holes_size_before=%zu "
          "holes_size_after=%zu "
          "allocated=%zu "
          "promoted=%zu "
          "new_space_survived=%zu "
          "nodes_died_in_new=%d "
          "nodes_copied_in_new=%d "
          "nodes_promoted=%d "
          "promotion_ratio=%.1f%% "
          "average_survival_ratio=%.1f%% "
          "promotion_rate=%.1f%% "
          "new_space_survive_rate=%.1f%% "
          "new_space_allocation_throughput=%.1f "
          "pool_chunks=%d "
          "compaction_speed=%.f\n",
          duration.InMillisecondsF(), spent_in_mutator.InMillisecondsF(),
          ToString(current_.type, true), current_.reduce_memory,
          current_scope(Scope::TIME_TO_SAFEPOINT),
          current_scope(Scope::HEAP_PROLOGUE),
          current_scope(Scope::HEAP_EMBEDDER_TRACING_EPILOGUE),
          current_scope(Scope::HEAP_EPILOGUE),
          current_scope(Scope::HEAP_EPILOGUE_REDUCE_NEW_SPACE),
          current_scope(Scope::HEAP_EXTERNAL_PROLOGUE),
          current_scope(Scope::HEAP_EXTERNAL_EPILOGUE),
          current_scope(Scope::HEAP_EXTERNAL_WEAK_GLOBAL_HANDLES),
          current_scope(Scope::MC_CLEAR),
          current_scope(Scope::MC_CLEAR_EXTERNAL_STRING_TABLE),
          current_scope(Scope::MC_CLEAR_STRING_FORWARDING_TABLE),
          current_scope(Scope::MC_CLEAR_WEAK_GLOBAL_HANDLES),
          current_scope(Scope::MC_CLEAR_DEPENDENT_CODE),
          current_scope(Scope::MC_CLEAR_MAPS),
          current_scope(Scope::MC_CLEAR_SLOTS_BUFFER),
          current_scope(Scope::MC_CLEAR_WEAK_COLLECTIONS),
          current_scope(Scope::MC_CLEAR_WEAK_LISTS),
          current_scope(Scope::MC_CLEAR_WEAK_REFERENCES),
          current_scope(Scope::MC_CLEAR_JOIN_JOB),
          current_scope(Scope::MC_COMPLETE_SWEEP_ARRAY_BUFFERS),
          current_scope(Scope::MC_COMPLETE_SWEEPING),
          current_scope(Scope::MC_EPILOGUE), current_scope(Scope::MC_EVACUATE),
          current_scope(Scope::MC_EVACUATE_CANDIDATES),
          current_scope(Scope::MC_EVACUATE_CLEAN_UP),
          current_scope(Scope::MC_EVACUATE_COPY),
          current_scope(Scope::MC_EVACUATE_PROLOGUE),
          current_scope(Scope::MC_EVACUATE_EPILOGUE),
          current_scope(Scope::MC_EVACUATE_REBALANCE),
          current_scope(Scope::MC_EVACUATE_UPDATE_POINTERS),
          current_scope(Scope::MC_EVACUATE_UPDATE_POINTERS_TO_NEW_ROOTS),
          current_scope(Scope::MC_EVACUATE_UPDATE_POINTERS_SLOTS_MAIN),
          current_scope(Scope::MC_EVACUATE_UPDATE_POINTERS_WEAK),
          current_scope(Scope::MC_FINISH),
          current_scope(Scope::MC_FINISH_SWEEP_ARRAY_BUFFERS),
          current_scope(Scope::MC_MARK),
          current_scope(Scope::MC_MARK_FINISH_INCREMENTAL),
          current_scope(Scope::MC_MARK_ROOTS),
          current_scope(Scope::MC_MARK_FULL_CLOSURE_PARALLEL),
          current_scope(Scope::MC_MARK_FULL_CLOSURE),
          current_scope(Scope::MC_MARK_WEAK_CLOSURE_EPHEMERON_MARKING),
          current_scope(Scope::MC_MARK_WEAK_CLOSURE_EPHEMERON_LINEAR),
          current_scope(Scope::MC_MARK_EMBEDDER_PROLOGUE),
          current_scope(Scope::MC_MARK_EMBEDDER_TRACING),
          current_scope(Scope::MC_PROLOGUE), current_scope(Scope::MC_SWEEP),
          current_scope(Scope::MC_SWEEP_CODE),
          current_scope(Scope::MC_SWEEP_MAP),
          current_scope(Scope::MC_SWEEP_NEW),
          current_scope(Scope::MC_SWEEP_NEW_LO),
          current_scope(Scope::MC_SWEEP_OLD),
          current_scope(Scope::MC_SWEEP_START_JOBS),
          current_scope(Scope::MC_INCREMENTAL),
          current_scope(Scope::MC_INCREMENTAL_FINALIZE),
          current_scope(Scope::MC_INCREMENTAL_EXTERNAL_PROLOGUE),
          current_scope(Scope::MC_INCREMENTAL_EXTERNAL_EPILOGUE),
          current_scope(Scope::MC_INCREMENTAL_LAYOUT_CHANGE),
          current_scope(Scope::MC_INCREMENTAL_START),
          current_scope(Scope::MC_INCREMENTAL_SWEEPING),
          current_scope(Scope::MC_INCREMENTAL_EMBEDDER_TRACING),
          incremental_scope(Scope::MC_INCREMENTAL_EMBEDDER_TRACING)
              .longest_step.InMillisecondsF(),
          incremental_scope(Scope::MC_INCREMENTAL)
              .longest_step.InMillisecondsF(),
          incremental_scope(Scope::MC_INCREMENTAL).steps,
          IncrementalMarkingSpeedInBytesPerMillisecond(),
          incremental_walltime_duration.InMillisecondsF(),
          current_scope(Scope::MC_BACKGROUND_MARKING),
          current_scope(Scope::MC_BACKGROUND_SWEEPING),
          current_scope(Scope::MC_BACKGROUND_EVACUATE_COPY),
          current_scope(Scope::MC_BACKGROUND_EVACUATE_UPDATE_POINTERS),
          current_scope(Scope::CONSERVATIVE_STACK_SCANNING),
          current_.start_object_size, current_.end_object_size,
          current_.start_holes_size, current_.end_holes_size,
          allocated_since_last_gc, heap_->promoted_objects_size(),
          heap_->new_space_surviving_object_size(),
          heap_->nodes_died_in_new_space_, heap_->nodes_copied_in_new_space_,
          heap_->nodes_promoted_, heap_->promotion_ratio_,
          AverageSurvivalRatio(), heap_->promotion_rate_,
          heap_->new_space_surviving_rate_,
          NewSpaceAllocationThroughputInBytesPerMillisecond(),
          heap_->memory_allocator()->pool()->NumberOfChunks(),
          CompactionSpeedInBytesPerMillisecond());
      break;
    case Event::Type::START:
      break;
  }
}

void GCTracer::RecordIncrementalMarkingSpeed(size_t bytes,
                                             base::TimeDelta duration) {
  DCHECK(!Event::IsYoungGenerationEvent(current_.type));
  if (duration.IsZero() || bytes == 0) return;
  double current_speed =
      static_cast<double>(bytes) / duration.InMillisecondsF();
  if (recorded_major_incremental_marking_speed_ == 0) {
    recorded_major_incremental_marking_speed_ = current_speed;
  } else {
    recorded_major_incremental_marking_speed_ =
        (recorded_major_incremental_marking_speed_ + current_speed) / 2;
  }
}

void GCTracer::RecordTimeToIncrementalMarkingTask(
    base::TimeDelta time_to_task) {
  if (!average_time_to_incremental_marking_task_.has_value()) {
    average_time_to_incremental_marking_task_.emplace(time_to_task);
  } else {
    average_time_to_incremental_marking_task_ =
        (average_time_to_incremental_marking_task_.value() + time_to_task) / 2;
  }
}

base::Optional<base::TimeDelta> GCTracer::AverageTimeToIncrementalMarkingTask()
    const {
  return average_time_to_incremental_marking_task_;
}

void GCTracer::RecordEmbedderSpeed(size_t bytes, double duration) {
  if (duration == 0 || bytes == 0) return;
  double current_speed = bytes / duration;
  if (recorded_embedder_speed_ == 0.0) {
    recorded_embedder_speed_ = current_speed;
  } else {
    recorded_embedder_speed_ = (recorded_embedder_speed_ + current_speed) / 2;
  }
}

void GCTracer::RecordMutatorUtilization(base::TimeTicks mark_compact_end_time,
                                        base::TimeDelta mark_compact_duration) {
  const base::TimeDelta total_duration =
      mark_compact_end_time - previous_mark_compact_end_time_;
  DCHECK_GE(total_duration, base::TimeDelta());
  const base::TimeDelta mutator_duration =
      total_duration - mark_compact_duration;
  DCHECK_GE(mutator_duration, base::TimeDelta());
  if (average_mark_compact_duration_ == 0 && average_mutator_duration_ == 0) {
    // This is the first event with mutator and mark-compact durations.
    average_mark_compact_duration_ = mark_compact_duration.InMillisecondsF();
    average_mutator_duration_ = mutator_duration.InMillisecondsF();
  } else {
    average_mark_compact_duration_ = (average_mark_compact_duration_ +
                                      mark_compact_duration.InMillisecondsF()) /
                                     2;
    average_mutator_duration_ =
        (average_mutator_duration_ + mutator_duration.InMillisecondsF()) / 2;
  }
  current_mark_compact_mutator_utilization_ =
      !total_duration.IsZero() ? mutator_duration.InMillisecondsF() /
                                     total_duration.InMillisecondsF()
                               : 0;
  previous_mark_compact_end_time_ = mark_compact_end_time;
}

double GCTracer::AverageMarkCompactMutatorUtilization() const {
  double average_total_duration =
      average_mark_compact_duration_ + average_mutator_duration_;
  if (average_total_duration == 0) return 1.0;
  return average_mutator_duration_ / average_total_duration;
}

double GCTracer::CurrentMarkCompactMutatorUtilization() const {
  return current_mark_compact_mutator_utilization_;
}

double GCTracer::IncrementalMarkingSpeedInBytesPerMillisecond() const {
  if (recorded_major_incremental_marking_speed_ != 0) {
    return recorded_major_incremental_marking_speed_;
  }
  if (!current_.incremental_marking_duration.IsZero()) {
    return current_.incremental_marking_bytes /
           current_.incremental_marking_duration.InMillisecondsF();
  }
  return kConservativeSpeedInBytesPerMillisecond;
}

double GCTracer::EmbedderSpeedInBytesPerMillisecond() const {
  // Note: Returning 0 is ok here as callers check for whether embedder speeds
  // have been recorded at all.
  return recorded_embedder_speed_;
}

double GCTracer::ScavengeSpeedInBytesPerMillisecond(
    ScavengeSpeedMode mode) const {
  if (mode == kForAllObjects) {
    return BoundedAverageSpeed(recorded_minor_gcs_total_);
  } else {
    return BoundedAverageSpeed(recorded_minor_gcs_survived_);
  }
}

double GCTracer::CompactionSpeedInBytesPerMillisecond() const {
  return BoundedAverageSpeed(recorded_compactions_);
}

double GCTracer::MarkCompactSpeedInBytesPerMillisecond() const {
  return BoundedAverageSpeed(recorded_mark_compacts_);
}

double GCTracer::FinalIncrementalMarkCompactSpeedInBytesPerMillisecond() const {
  return BoundedAverageSpeed(recorded_incremental_mark_compacts_);
}

double GCTracer::CombinedMarkCompactSpeedInBytesPerMillisecond() {
  const double kMinimumMarkingSpeed = 0.5;
  if (combined_mark_compact_speed_cache_ > 0)
    return combined_mark_compact_speed_cache_;
  // MarkCompact speed is more stable than incremental marking speed, because
  // there might not be many incremental marking steps because of concurrent
  // marking.
  combined_mark_compact_speed_cache_ = MarkCompactSpeedInBytesPerMillisecond();
  if (combined_mark_compact_speed_cache_ > 0)
    return combined_mark_compact_speed_cache_;
  double speed1 = IncrementalMarkingSpeedInBytesPerMillisecond();
  double speed2 = FinalIncrementalMarkCompactSpeedInBytesPerMillisecond();
  if (speed1 < kMinimumMarkingSpeed || speed2 < kMinimumMarkingSpeed) {
    // No data for the incremental marking speed.
    // Return the non-incremental mark-compact speed.
    combined_mark_compact_speed_cache_ =
        MarkCompactSpeedInBytesPerMillisecond();
  } else {
    // Combine the speed of incremental step and the speed of the final step.
    // 1 / (1 / speed1 + 1 / speed2) = speed1 * speed2 / (speed1 + speed2).
    combined_mark_compact_speed_cache_ = speed1 * speed2 / (speed1 + speed2);
  }
  return combined_mark_compact_speed_cache_;
}

double GCTracer::CombineSpeedsInBytesPerMillisecond(double default_speed,
                                                    double optional_speed) {
  constexpr double kMinimumSpeed = 0.5;
  if (optional_speed < kMinimumSpeed) {
    return default_speed;
  }
  return default_speed * optional_speed / (default_speed + optional_speed);
}

double GCTracer::NewSpaceAllocationThroughputInBytesPerMillisecond(
    base::Optional<base::TimeDelta> selected_duration) const {
  return BoundedAverageSpeed(
      recorded_new_generation_allocations_,
      selected_duration);
}

double GCTracer::OldGenerationAllocationThroughputInBytesPerMillisecond(
    base::Optional<base::TimeDelta> selected_duration) const {
  return BoundedAverageSpeed(
      recorded_old_generation_allocations_,
      selected_duration);
}

double GCTracer::EmbedderAllocationThroughputInBytesPerMillisecond(
    base::Optional<base::TimeDelta> selected_duration) const {
  return BoundedAverageSpeed(
      recorded_embedder_generation_allocations_,
      selected_duration);
}

double GCTracer::AllocationThroughputInBytesPerMillisecond(
    base::Optional<base::TimeDelta> selected_duration) const {
  return NewSpaceAllocationThroughputInBytesPerMillisecond(selected_duration) +
         OldGenerationAllocationThroughputInBytesPerMillisecond(
             selected_duration);
}

double GCTracer::CurrentAllocationThroughputInBytesPerMillisecond() const {
  return AllocationThroughputInBytesPerMillisecond(kThroughputTimeFrame);
}

double GCTracer::CurrentOldGenerationAllocationThroughputInBytesPerMillisecond()
    const {
  return OldGenerationAllocationThroughputInBytesPerMillisecond(
      kThroughputTimeFrame);
}

double GCTracer::CurrentEmbedderAllocationThroughputInBytesPerMillisecond()
    const {
  return EmbedderAllocationThroughputInBytesPerMillisecond(
      kThroughputTimeFrame);
}

double GCTracer::AverageSurvivalRatio() const {
  if (recorded_survival_ratios_.Empty()) return 0.0;
  double sum = recorded_survival_ratios_.Reduce(
      [](double a, double b) { return a + b; }, 0.0);
  return sum / recorded_survival_ratios_.Size();
}

bool GCTracer::SurvivalEventsRecorded() const {
  return !recorded_survival_ratios_.Empty();
}

void GCTracer::ResetSurvivalEvents() { recorded_survival_ratios_.Clear(); }

void GCTracer::NotifyIncrementalMarkingStart() {
  current_.incremental_marking_start_time = base::TimeTicks::Now();
}

void GCTracer::FetchBackgroundCounters() {
  base::MutexGuard guard(&background_scopes_mutex_);
  for (int i = Scope::FIRST_BACKGROUND_SCOPE; i <= Scope::LAST_BACKGROUND_SCOPE;
       i++) {
    current_.scopes[i] += background_scopes_[i];
    background_scopes_[i] = base::TimeDelta();
  }
}

namespace {

V8_INLINE int TruncateToMs(base::TimeDelta delta) {
  return static_cast<int>(delta.InMilliseconds());
}

}  // namespace

void GCTracer::RecordGCPhasesHistograms(RecordGCPhasesInfo::Mode mode) {
  Counters* counters = heap_->isolate()->counters();
  if (mode == RecordGCPhasesInfo::Mode::Finalize) {
    DCHECK_EQ(Scope::FIRST_TOP_MC_SCOPE, Scope::MC_CLEAR);
    counters->gc_finalize_clear()->AddSample(
        TruncateToMs(current_.scopes[Scope::MC_CLEAR]));
    counters->gc_finalize_epilogue()->AddSample(
        TruncateToMs(current_.scopes[Scope::MC_EPILOGUE]));
    counters->gc_finalize_evacuate()->AddSample(
        TruncateToMs(current_.scopes[Scope::MC_EVACUATE]));
    counters->gc_finalize_finish()->AddSample(
        TruncateToMs(current_.scopes[Scope::MC_FINISH]));
    counters->gc_finalize_mark()->AddSample(
        TruncateToMs(current_.scopes[Scope::MC_MARK]));
    counters->gc_finalize_prologue()->AddSample(
        TruncateToMs(current_.scopes[Scope::MC_PROLOGUE]));
    counters->gc_finalize_sweep()->AddSample(
        TruncateToMs(current_.scopes[Scope::MC_SWEEP]));
    if (!current_.incremental_marking_duration.IsZero()) {
      heap_->isolate()->counters()->incremental_marking_sum()->AddSample(
          TruncateToMs(current_.incremental_marking_duration));
    }
    const base::TimeDelta overall_marking_time =
        current_.incremental_marking_duration + current_.scopes[Scope::MC_MARK];
    heap_->isolate()->counters()->gc_marking_sum()->AddSample(
        TruncateToMs(overall_marking_time));

    DCHECK_EQ(Scope::LAST_TOP_MC_SCOPE, Scope::MC_SWEEP);
  } else if (mode == RecordGCPhasesInfo::Mode::Scavenger) {
    counters->gc_scavenger_scavenge_main()->AddSample(
        TruncateToMs(current_.scopes[Scope::SCAVENGER_SCAVENGE_PARALLEL]));
    counters->gc_scavenger_scavenge_roots()->AddSample(
        TruncateToMs(current_.scopes[Scope::SCAVENGER_SCAVENGE_ROOTS]));
  }
}

void GCTracer::RecordGCSumCounters() {
  const base::TimeDelta atomic_pause_duration =
      current_.scopes[Scope::MARK_COMPACTOR];
  const base::TimeDelta incremental_marking =
      incremental_scopes_[Scope::MC_INCREMENTAL_LAYOUT_CHANGE].duration +
      incremental_scopes_[Scope::MC_INCREMENTAL_START].duration +
      current_.incremental_marking_duration +
      incremental_scopes_[Scope::MC_INCREMENTAL_FINALIZE].duration;
  const base::TimeDelta incremental_sweeping =
      incremental_scopes_[Scope::MC_INCREMENTAL_SWEEPING].duration;
  const base::TimeDelta overall_duration =
      atomic_pause_duration + incremental_marking + incremental_sweeping;
  const base::TimeDelta atomic_marking_duration =
      current_.scopes[Scope::MC_PROLOGUE] + current_.scopes[Scope::MC_MARK];
  const base::TimeDelta marking_duration =
      atomic_marking_duration + incremental_marking;
  base::TimeDelta background_duration;
  base::TimeDelta marking_background_duration;
  {
    base::MutexGuard guard(&background_scopes_mutex_);
    background_duration =
        background_scopes_[Scope::MC_BACKGROUND_EVACUATE_COPY] +
        background_scopes_[Scope::MC_BACKGROUND_EVACUATE_UPDATE_POINTERS] +
        background_scopes_[Scope::MC_BACKGROUND_MARKING] +
        background_scopes_[Scope::MC_BACKGROUND_SWEEPING];
    marking_background_duration =
        background_scopes_[Scope::MC_BACKGROUND_MARKING];
  }

  // Emit trace event counters.
  TRACE_EVENT_INSTANT2(
      TRACE_DISABLED_BY_DEFAULT("v8.gc"), "V8.GCMarkCompactorSummary",
      TRACE_EVENT_SCOPE_THREAD, "duration", overall_duration.InMillisecondsF(),
      "background_duration", background_duration.InMillisecondsF());
  TRACE_EVENT_INSTANT2(
      TRACE_DISABLED_BY_DEFAULT("v8.gc"), "V8.GCMarkCompactorMarkingSummary",
      TRACE_EVENT_SCOPE_THREAD, "duration", marking_duration.InMillisecondsF(),
      "background_duration", marking_background_duration.InMillisecondsF());
}

namespace {

void CopyTimeMetrics(
    ::v8::metrics::GarbageCollectionPhases& metrics,
    const cppgc::internal::MetricRecorder::GCCycle::IncrementalPhases&
        cppgc_metrics) {
  // Allow for uninitialized values (-1), in case incremental marking/sweeping
  // were not used.
  DCHECK_LE(-1, cppgc_metrics.mark_duration_us);
  metrics.mark_wall_clock_duration_in_us = cppgc_metrics.mark_duration_us;
  DCHECK_LE(-1, cppgc_metrics.sweep_duration_us);
  metrics.sweep_wall_clock_duration_in_us = cppgc_metrics.sweep_duration_us;
  // The total duration is initialized, even if both incremental
  // marking and sweeping were not used.
  metrics.total_wall_clock_duration_in_us =
      std::max(INT64_C(0), metrics.mark_wall_clock_duration_in_us) +
      std::max(INT64_C(0), metrics.sweep_wall_clock_duration_in_us);
}

void CopyTimeMetrics(
    ::v8::metrics::GarbageCollectionPhases& metrics,
    const cppgc::internal::MetricRecorder::GCCycle::Phases& cppgc_metrics) {
  DCHECK_NE(-1, cppgc_metrics.compact_duration_us);
  metrics.compact_wall_clock_duration_in_us = cppgc_metrics.compact_duration_us;
  DCHECK_NE(-1, cppgc_metrics.mark_duration_us);
  metrics.mark_wall_clock_duration_in_us = cppgc_metrics.mark_duration_us;
  DCHECK_NE(-1, cppgc_metrics.sweep_duration_us);
  metrics.sweep_wall_clock_duration_in_us = cppgc_metrics.sweep_duration_us;
  DCHECK_NE(-1, cppgc_metrics.weak_duration_us);
  metrics.weak_wall_clock_duration_in_us = cppgc_metrics.weak_duration_us;
  metrics.total_wall_clock_duration_in_us =
      metrics.compact_wall_clock_duration_in_us +
      metrics.mark_wall_clock_duration_in_us +
      metrics.sweep_wall_clock_duration_in_us +
      metrics.weak_wall_clock_duration_in_us;
}

void CopySizeMetrics(
    ::v8::metrics::GarbageCollectionSizes& metrics,
    const cppgc::internal::MetricRecorder::GCCycle::Sizes& cppgc_metrics) {
  DCHECK_NE(-1, cppgc_metrics.after_bytes);
  metrics.bytes_after = cppgc_metrics.after_bytes;
  DCHECK_NE(-1, cppgc_metrics.before_bytes);
  metrics.bytes_before = cppgc_metrics.before_bytes;
  DCHECK_NE(-1, cppgc_metrics.freed_bytes);
  metrics.bytes_freed = cppgc_metrics.freed_bytes;
}

::v8::metrics::Recorder::ContextId GetContextId(
    v8::internal::Isolate* isolate) {
  DCHECK_NOT_NULL(isolate);
  if (isolate->context().is_null())
    return v8::metrics::Recorder::ContextId::Empty();
  HandleScope scope(isolate);
  return isolate->GetOrRegisterRecorderContextId(isolate->native_context());
}

template <typename EventType>
void FlushBatchedEvents(
    v8::metrics::GarbageCollectionBatchedEvents<EventType>& batched_events,
    Isolate* isolate) {
  DCHECK_NOT_NULL(isolate->metrics_recorder());
  DCHECK(!batched_events.events.empty());
  isolate->metrics_recorder()->AddMainThreadEvent(std::move(batched_events),
                                                  GetContextId(isolate));
  batched_events = {};
}

}  // namespace

void GCTracer::ReportFullCycleToRecorder() {
  DCHECK(!Event::IsYoungGenerationEvent(current_.type));
  DCHECK_EQ(Event::State::NOT_RUNNING, current_.state);
  auto* cpp_heap = v8::internal::CppHeap::From(heap_->cpp_heap());
  DCHECK_IMPLIES(cpp_heap,
                 cpp_heap->GetMetricRecorder()->FullGCMetricsReportPending());
  const std::shared_ptr<metrics::Recorder>& recorder =
      heap_->isolate()->metrics_recorder();
  DCHECK_NOT_NULL(recorder);
  if (!recorder->HasEmbedderRecorder()) {
    incremental_mark_batched_events_ = {};
    incremental_sweep_batched_events_ = {};
    if (cpp_heap) {
      cpp_heap->GetMetricRecorder()->ClearCachedEvents();
    }
    return;
  }
  if (!incremental_mark_batched_events_.events.empty()) {
    FlushBatchedEvents(incremental_mark_batched_events_, heap_->isolate());
  }
  if (!incremental_sweep_batched_events_.events.empty()) {
    FlushBatchedEvents(incremental_sweep_batched_events_, heap_->isolate());
  }

  v8::metrics::GarbageCollectionFullCycle event;
  event.reason = static_cast<int>(current_.gc_reason);

  // Managed C++ heap statistics:
  if (cpp_heap) {
    cpp_heap->GetMetricRecorder()->FlushBatchedIncrementalEvents();
    const base::Optional<cppgc::internal::MetricRecorder::GCCycle>
        optional_cppgc_event =
            cpp_heap->GetMetricRecorder()->ExtractLastFullGcEvent();
    DCHECK(optional_cppgc_event.has_value());
    DCHECK(!cpp_heap->GetMetricRecorder()->FullGCMetricsReportPending());
    const cppgc::internal::MetricRecorder::GCCycle& cppgc_event =
        optional_cppgc_event.value();
    DCHECK_EQ(cppgc_event.type,
              cppgc::internal::MetricRecorder::GCCycle::Type::kMajor);
    CopyTimeMetrics(event.total_cpp, cppgc_event.total);
    CopyTimeMetrics(event.main_thread_cpp, cppgc_event.main_thread);
    CopyTimeMetrics(event.main_thread_atomic_cpp,
                    cppgc_event.main_thread_atomic);
    CopyTimeMetrics(event.main_thread_incremental_cpp,
                    cppgc_event.main_thread_incremental);
    CopySizeMetrics(event.objects_cpp, cppgc_event.objects);
    CopySizeMetrics(event.memory_cpp, cppgc_event.memory);
    DCHECK_NE(-1, cppgc_event.collection_rate_in_percent);
    event.collection_rate_cpp_in_percent =
        cppgc_event.collection_rate_in_percent;
    DCHECK_NE(-1, cppgc_event.efficiency_in_bytes_per_us);
    event.efficiency_cpp_in_bytes_per_us =
        cppgc_event.efficiency_in_bytes_per_us;
    DCHECK_NE(-1, cppgc_event.main_thread_efficiency_in_bytes_per_us);
    event.main_thread_efficiency_cpp_in_bytes_per_us =
        cppgc_event.main_thread_efficiency_in_bytes_per_us;
  }

  // Unified heap statistics:
  const base::TimeDelta atomic_pause_duration =
      current_.scopes[Scope::MARK_COMPACTOR];
  const base::TimeDelta incremental_marking =
      current_.incremental_scopes[Scope::MC_INCREMENTAL_LAYOUT_CHANGE]
          .duration +
      current_.incremental_scopes[Scope::MC_INCREMENTAL_START].duration +
      current_.incremental_marking_duration +
      current_.incremental_scopes[Scope::MC_INCREMENTAL_FINALIZE].duration;
  const base::TimeDelta incremental_sweeping =
      current_.incremental_scopes[Scope::MC_INCREMENTAL_SWEEPING].duration;
  const base::TimeDelta overall_duration =
      atomic_pause_duration + incremental_marking + incremental_sweeping;
  const base::TimeDelta marking_background_duration =
      current_.scopes[Scope::MC_BACKGROUND_MARKING];
  const base::TimeDelta sweeping_background_duration =
      current_.scopes[Scope::MC_BACKGROUND_SWEEPING];
  const base::TimeDelta compact_background_duration =
      current_.scopes[Scope::MC_BACKGROUND_EVACUATE_COPY] +
      current_.scopes[Scope::MC_BACKGROUND_EVACUATE_UPDATE_POINTERS];
  const base::TimeDelta background_duration = marking_background_duration +
                                              sweeping_background_duration +
                                              compact_background_duration;
  const base::TimeDelta atomic_marking_duration =
      current_.scopes[Scope::MC_PROLOGUE] + current_.scopes[Scope::MC_MARK];
  const base::TimeDelta marking_duration =
      atomic_marking_duration + incremental_marking;
  const base::TimeDelta weak_duration = current_.scopes[Scope::MC_CLEAR];
  const base::TimeDelta compact_duration = current_.scopes[Scope::MC_EVACUATE] +
                                           current_.scopes[Scope::MC_FINISH] +
                                           current_.scopes[Scope::MC_EPILOGUE];
  const base::TimeDelta atomic_sweeping_duration =
      current_.scopes[Scope::MC_SWEEP];
  const base::TimeDelta sweeping_duration =
      atomic_sweeping_duration + incremental_sweeping;

  event.main_thread_atomic.total_wall_clock_duration_in_us =
      atomic_pause_duration.InMicroseconds();
  event.main_thread.total_wall_clock_duration_in_us =
      overall_duration.InMicroseconds();
  event.total.total_wall_clock_duration_in_us =
      (overall_duration + background_duration).InMicroseconds();
  event.main_thread_atomic.mark_wall_clock_duration_in_us =
      atomic_marking_duration.InMicroseconds();
  event.main_thread.mark_wall_clock_duration_in_us =
      marking_duration.InMicroseconds();
  event.total.mark_wall_clock_duration_in_us =
      (marking_duration + marking_background_duration).InMicroseconds();
  event.main_thread_atomic.weak_wall_clock_duration_in_us =
      event.main_thread.weak_wall_clock_duration_in_us =
          event.total.weak_wall_clock_duration_in_us =
              weak_duration.InMicroseconds();
  event.main_thread_atomic.compact_wall_clock_duration_in_us =
      event.main_thread.compact_wall_clock_duration_in_us =
          compact_duration.InMicroseconds();
  event.total.compact_wall_clock_duration_in_us =
      (compact_duration + compact_background_duration).InMicroseconds();
  event.main_thread_atomic.sweep_wall_clock_duration_in_us =
      atomic_sweeping_duration.InMicroseconds();
  event.main_thread.sweep_wall_clock_duration_in_us =
      sweeping_duration.InMicroseconds();
  event.total.sweep_wall_clock_duration_in_us =
      (sweeping_duration + sweeping_background_duration).InMicroseconds();
  if (current_.type == Event::Type::INCREMENTAL_MARK_COMPACTOR) {
    event.main_thread_incremental.mark_wall_clock_duration_in_us =
        incremental_marking.InMicroseconds();
    event.incremental_marking_start_stop_wall_clock_duration_in_us =
        (current_.start_time - current_.incremental_marking_start_time)
            .InMicroseconds();
  } else {
    DCHECK(incremental_marking.IsZero());
    event.main_thread_incremental.mark_wall_clock_duration_in_us = -1;
  }
  // TODO(chromium:1154636): We always report the value of incremental sweeping,
  // even if it is zero.
  event.main_thread_incremental.sweep_wall_clock_duration_in_us =
      incremental_sweeping.InMicroseconds();

  // TODO(chromium:1154636): Populate the following:
  // - event.objects
  // - event.memory
  // - event.collection_rate_in_percent
  // - event.efficiency_in_bytes_per_us
  // - event.main_thread_efficiency_in_bytes_per_us

  recorder->AddMainThreadEvent(event, GetContextId(heap_->isolate()));
}

void GCTracer::ReportIncrementalMarkingStepToRecorder(double v8_duration) {
  DCHECK_EQ(Event::Type::INCREMENTAL_MARK_COMPACTOR, current_.type);
  static constexpr int kMaxBatchedEvents =
      CppHeap::MetricRecorderAdapter::kMaxBatchedEvents;
  const std::shared_ptr<metrics::Recorder>& recorder =
      heap_->isolate()->metrics_recorder();
  DCHECK_NOT_NULL(recorder);
  if (!recorder->HasEmbedderRecorder()) return;
  incremental_mark_batched_events_.events.emplace_back();
  if (heap_->cpp_heap()) {
    const base::Optional<
        cppgc::internal::MetricRecorder::MainThreadIncrementalMark>
        cppgc_event = v8::internal::CppHeap::From(heap_->cpp_heap())
                          ->GetMetricRecorder()
                          ->ExtractLastIncrementalMarkEvent();
    if (cppgc_event.has_value()) {
      DCHECK_NE(-1, cppgc_event.value().duration_us);
      incremental_mark_batched_events_.events.back()
          .cpp_wall_clock_duration_in_us = cppgc_event.value().duration_us;
    }
  }
  incremental_mark_batched_events_.events.back().wall_clock_duration_in_us =
      static_cast<int64_t>(v8_duration *
                           base::Time::kMicrosecondsPerMillisecond);
  if (incremental_mark_batched_events_.events.size() == kMaxBatchedEvents) {
    FlushBatchedEvents(incremental_mark_batched_events_, heap_->isolate());
  }
}

void GCTracer::ReportIncrementalSweepingStepToRecorder(double v8_duration) {
  static constexpr int kMaxBatchedEvents =
      CppHeap::MetricRecorderAdapter::kMaxBatchedEvents;
  const std::shared_ptr<metrics::Recorder>& recorder =
      heap_->isolate()->metrics_recorder();
  DCHECK_NOT_NULL(recorder);
  if (!recorder->HasEmbedderRecorder()) return;
  incremental_sweep_batched_events_.events.emplace_back();
  incremental_sweep_batched_events_.events.back().wall_clock_duration_in_us =
      static_cast<int64_t>(v8_duration *
                           base::Time::kMicrosecondsPerMillisecond);
  if (incremental_sweep_batched_events_.events.size() == kMaxBatchedEvents) {
    FlushBatchedEvents(incremental_sweep_batched_events_, heap_->isolate());
  }
}

void GCTracer::ReportYoungCycleToRecorder() {
  DCHECK(Event::IsYoungGenerationEvent(current_.type));
  DCHECK_EQ(Event::State::NOT_RUNNING, current_.state);
  const std::shared_ptr<metrics::Recorder>& recorder =
      heap_->isolate()->metrics_recorder();
  DCHECK_NOT_NULL(recorder);
  if (!recorder->HasEmbedderRecorder()) return;

  v8::metrics::GarbageCollectionYoungCycle event;
  // Reason:
  event.reason = static_cast<int>(current_.gc_reason);
#if defined(CPPGC_YOUNG_GENERATION)
  // Managed C++ heap statistics:
  auto* cpp_heap = v8::internal::CppHeap::From(heap_->cpp_heap());
  if (cpp_heap && cpp_heap->generational_gc_supported()) {
    auto* metric_recorder = cpp_heap->GetMetricRecorder();
    const base::Optional<cppgc::internal::MetricRecorder::GCCycle>
        optional_cppgc_event = metric_recorder->ExtractLastYoungGcEvent();
    // We bail out from Oilpan's young GC if the full GC is already in progress.
    // Check here if the young generation event was reported.
    if (optional_cppgc_event) {
      DCHECK(!metric_recorder->YoungGCMetricsReportPending());
      const cppgc::internal::MetricRecorder::GCCycle& cppgc_event =
          optional_cppgc_event.value();
      DCHECK_EQ(cppgc_event.type,
                cppgc::internal::MetricRecorder::GCCycle::Type::kMinor);
      CopyTimeMetrics(event.total_cpp, cppgc_event.total);
      CopySizeMetrics(event.objects_cpp, cppgc_event.objects);
      CopySizeMetrics(event.memory_cpp, cppgc_event.memory);
      DCHECK_NE(-1, cppgc_event.collection_rate_in_percent);
      event.collection_rate_cpp_in_percent =
          cppgc_event.collection_rate_in_percent;
      DCHECK_NE(-1, cppgc_event.efficiency_in_bytes_per_us);
      event.efficiency_cpp_in_bytes_per_us =
          cppgc_event.efficiency_in_bytes_per_us;
      DCHECK_NE(-1, cppgc_event.main_thread_efficiency_in_bytes_per_us);
      event.main_thread_efficiency_cpp_in_bytes_per_us =
          cppgc_event.main_thread_efficiency_in_bytes_per_us;
    }
  }
#endif  // defined(CPPGC_YOUNG_GENERATION)

  // Total:
  const base::TimeDelta total_wall_clock_duration =
      current_.scopes[Scope::SCAVENGER] +
      current_.scopes[Scope::MINOR_MARK_SWEEPER] +
      current_.scopes[Scope::SCAVENGER_BACKGROUND_SCAVENGE_PARALLEL] +
      current_.scopes[Scope::MINOR_MS_BACKGROUND_MARKING];
  // TODO(chromium:1154636): Consider adding BACKGROUND_YOUNG_ARRAY_BUFFER_SWEEP
  // (both for the case of the scavenger and the minor mark-sweeper).
  event.total_wall_clock_duration_in_us =
      total_wall_clock_duration.InMicroseconds();
  // MainThread:
  const base::TimeDelta main_thread_wall_clock_duration =
      current_.scopes[Scope::SCAVENGER] +
      current_.scopes[Scope::MINOR_MARK_SWEEPER];
  event.main_thread_wall_clock_duration_in_us =
      main_thread_wall_clock_duration.InMicroseconds();
  // Collection Rate:
  if (current_.young_object_size == 0) {
    event.collection_rate_in_percent = 0;
  } else {
    event.collection_rate_in_percent =
        static_cast<double>(current_.survived_young_object_size) /
        current_.young_object_size;
  }
  // Efficiency:
  //
  // It's possible that time durations are rounded/clamped to zero, in which
  // case we report infinity efficiency.
  const double freed_bytes = static_cast<double>(
      current_.young_object_size - current_.survived_young_object_size);
  event.efficiency_in_bytes_per_us =
      total_wall_clock_duration.IsZero()
          ? std::numeric_limits<double>::infinity()
          : freed_bytes / total_wall_clock_duration.InMicroseconds();
  event.main_thread_efficiency_in_bytes_per_us =
      main_thread_wall_clock_duration.IsZero()
          ? std::numeric_limits<double>::infinity()
          : freed_bytes / main_thread_wall_clock_duration.InMicroseconds();
  recorder->AddMainThreadEvent(event, GetContextId(heap_->isolate()));
}

GarbageCollector GCTracer::GetCurrentCollector() const {
  switch (current_.type) {
    case Event::Type::SCAVENGER:
      return GarbageCollector::SCAVENGER;
    case Event::Type::MARK_COMPACTOR:
    case Event::Type::INCREMENTAL_MARK_COMPACTOR:
      return GarbageCollector::MARK_COMPACTOR;
    case Event::Type::MINOR_MARK_SWEEPER:
    case Event::Type::INCREMENTAL_MINOR_MARK_SWEEPER:
      return GarbageCollector::MINOR_MARK_SWEEPER;
    case Event::Type::START:
      UNREACHABLE();
  }
}

#ifdef DEBUG
bool GCTracer::IsInObservablePause() const {
  return start_of_observable_pause_.has_value();
}

bool GCTracer::IsInAtomicPause() const {
  return current_.state == Event::State::ATOMIC;
}

bool GCTracer::IsConsistentWithCollector(GarbageCollector collector) const {
  switch (collector) {
    case GarbageCollector::SCAVENGER:
      return current_.type == Event::Type::SCAVENGER;
    case GarbageCollector::MARK_COMPACTOR:
      return current_.type == Event::Type::MARK_COMPACTOR ||
             current_.type == Event::Type::INCREMENTAL_MARK_COMPACTOR;
    case GarbageCollector::MINOR_MARK_SWEEPER:
      return current_.type == Event::Type::MINOR_MARK_SWEEPER ||
             current_.type == Event::Type::INCREMENTAL_MINOR_MARK_SWEEPER;
  }
}

bool GCTracer::IsSweepingInProgress() const {
  return (current_.type == Event::Type::MARK_COMPACTOR ||
          current_.type == Event::Type::INCREMENTAL_MARK_COMPACTOR ||
          current_.type == Event::Type::MINOR_MARK_SWEEPER ||
          current_.type == Event::Type::INCREMENTAL_MINOR_MARK_SWEEPER) &&
         current_.state == Event::State::SWEEPING;
}
#endif

}  // namespace internal
}  // namespace v8
