// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/concurrent-marking.h"

#include <algorithm>
#include <atomic>
#include <stack>
#include <unordered_map>

#include "include/v8config.h"
#include "src/base/logging.h"
#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/flags/flags.h"
#include "src/heap/base/cached-unordered-map.h"
#include "src/heap/ephemeron-remembered-set.h"
#include "src/heap/gc-tracer-inl.h"
#include "src/heap/gc-tracer.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap.h"
#include "src/heap/mark-compact-inl.h"
#include "src/heap/mark-compact.h"
#include "src/heap/marking-state-inl.h"
#include "src/heap/marking-visitor-inl.h"
#include "src/heap/marking-visitor.h"
#include "src/heap/marking.h"
#include "src/heap/memory-chunk-metadata.h"
#include "src/heap/memory-measurement-inl.h"
#include "src/heap/memory-measurement.h"
#include "src/heap/minor-mark-sweep-inl.h"
#include "src/heap/minor-mark-sweep.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/heap/object-lock.h"
#include "src/heap/objects-visiting-inl.h"
#include "src/heap/objects-visiting.h"
#include "src/heap/pretenuring-handler.h"
#include "src/heap/weak-object-worklists.h"
#include "src/heap/young-generation-marking-visitor.h"
#include "src/init/v8.h"
#include "src/objects/data-handler-inl.h"
#include "src/objects/embedder-data-array-inl.h"
#include "src/objects/hash-table-inl.h"
#include "src/objects/heap-object.h"
#include "src/objects/js-array-buffer-inl.h"
#include "src/objects/slots-inl.h"
#include "src/objects/transitions-inl.h"
#include "src/utils/utils-inl.h"

namespace v8 {
namespace internal {

struct MemoryChunkData final {
  intptr_t live_bytes = 0;
  std::unique_ptr<TypedSlots> typed_slots;
};

using MemoryChunkDataMap =
    ::heap::base::CachedUnorderedMap<MutablePageMetadata*, MemoryChunkData>;

class ConcurrentMarkingVisitor final
    : public FullMarkingVisitorBase<ConcurrentMarkingVisitor> {
 public:
  ConcurrentMarkingVisitor(MarkingWorklists::Local* local_marking_worklists,
                           WeakObjects::Local* local_weak_objects, Heap* heap,
                           unsigned mark_compact_epoch,
                           base::EnumSet<CodeFlushMode> code_flush_mode,
                           bool should_keep_ages_unchanged,
                           uint16_t code_flushing_increase,
                           MemoryChunkDataMap* memory_chunk_data)
      : FullMarkingVisitorBase(local_marking_worklists, local_weak_objects,
                               heap, mark_compact_epoch, code_flush_mode,
                               should_keep_ages_unchanged,
                               code_flushing_increase),
        memory_chunk_data_(memory_chunk_data) {}

  using FullMarkingVisitorBase<
      ConcurrentMarkingVisitor>::VisitMapPointerIfNeeded;

  static constexpr bool EnableConcurrentVisitation() { return true; }

  // Implements ephemeron semantics: Marks value if key is already reachable.
  // Returns true if value was actually marked.
  bool ProcessEphemeron(Tagged<HeapObject> key, Tagged<HeapObject> value) {
    if (marking_state()->IsMarked(key)) {
      const auto target_worklist =
          MarkingHelper::ShouldMarkObject(heap_, value);
      DCHECK(target_worklist.has_value());
      if (MarkObject(key, value, target_worklist.value())) {
        return true;
      }
    } else if (marking_state()->IsUnmarked(value)) {
      local_weak_objects_->next_ephemerons_local.Push(Ephemeron{key, value});
    }
    return false;
  }

  template <typename TSlot>
  void RecordSlot(Tagged<HeapObject> object, TSlot slot,
                  Tagged<HeapObject> target) {
    MarkCompactCollector::RecordSlot(object, slot, target);
  }

  void IncrementLiveBytesCached(MutablePageMetadata* chunk, intptr_t by) {
    DCHECK_IMPLIES(V8_COMPRESS_POINTERS_8GB_BOOL,
                   IsAligned(by, kObjectAlignment8GbHeap));
    (*memory_chunk_data_)[chunk].live_bytes += by;
  }

 private:
  void RecordRelocSlot(Tagged<InstructionStream> host, RelocInfo* rinfo,
                       Tagged<HeapObject> target) {
    if (!MarkCompactCollector::ShouldRecordRelocSlot(host, rinfo, target)) {
      return;
    }

    MarkCompactCollector::RecordRelocSlotInfo info =
        MarkCompactCollector::ProcessRelocInfo(host, rinfo, target);

    MemoryChunkData& data = (*memory_chunk_data_)[info.page_metadata];
    if (!data.typed_slots) {
      data.typed_slots.reset(new TypedSlots());
    }
    data.typed_slots->Insert(info.slot_type, info.offset);
  }

  MemoryChunkDataMap* memory_chunk_data_;

  friend class MarkingVisitorBase<ConcurrentMarkingVisitor>;
};

struct ConcurrentMarking::TaskState {
  size_t marked_bytes = 0;
  MemoryChunkDataMap memory_chunk_data;
  NativeContextStats native_context_stats;
  PretenuringHandler::PretenuringFeedbackMap local_pretenuring_feedback{
      PretenuringHandler::kInitialFeedbackCapacity};
};

class ConcurrentMarking::JobTaskMajor : public v8::JobTask {
 public:
  JobTaskMajor(ConcurrentMarking* concurrent_marking,
               unsigned mark_compact_epoch,
               base::EnumSet<CodeFlushMode> code_flush_mode,
               bool should_keep_ages_unchanged)
      : concurrent_marking_(concurrent_marking),
        mark_compact_epoch_(mark_compact_epoch),
        code_flush_mode_(code_flush_mode),
        should_keep_ages_unchanged_(should_keep_ages_unchanged),
        trace_id_(reinterpret_cast<uint64_t>(concurrent_marking) ^
                  concurrent_marking->heap_->tracer()->CurrentEpoch(
                      GCTracer::Scope::MC_BACKGROUND_MARKING)) {}

  ~JobTaskMajor() override = default;
  JobTaskMajor(const JobTaskMajor&) = delete;
  JobTaskMajor& operator=(const JobTaskMajor&) = delete;

  // v8::JobTask overrides.
  void Run(JobDelegate* delegate) override {
    // In case multi-cage pointer compression mode is enabled ensure that
    // current thread's cage base values are properly initialized.
    PtrComprCageAccessScope ptr_compr_cage_access_scope(
        concurrent_marking_->heap_->isolate());

    if (delegate->IsJoiningThread()) {
      // TRACE_GC is not needed here because the caller opens the right scope.
      concurrent_marking_->RunMajor(delegate, code_flush_mode_,
                                    mark_compact_epoch_,
                                    should_keep_ages_unchanged_);
    } else {
      TRACE_GC_EPOCH_WITH_FLOW(concurrent_marking_->heap_->tracer(),
                               GCTracer::Scope::MC_BACKGROUND_MARKING,
                               ThreadKind::kBackground, trace_id_,
                               TRACE_EVENT_FLAG_FLOW_IN);
      concurrent_marking_->RunMajor(delegate, code_flush_mode_,
                                    mark_compact_epoch_,
                                    should_keep_ages_unchanged_);
    }
  }

  size_t GetMaxConcurrency(size_t worker_count) const override {
    return concurrent_marking_->GetMajorMaxConcurrency(worker_count);
  }

  uint64_t trace_id() const { return trace_id_; }

 private:
  ConcurrentMarking* concurrent_marking_;
  const unsigned mark_compact_epoch_;
  base::EnumSet<CodeFlushMode> code_flush_mode_;
  const bool should_keep_ages_unchanged_;
  const uint64_t trace_id_;
};

class ConcurrentMarking::JobTaskMinor : public v8::JobTask {
 public:
  explicit JobTaskMinor(ConcurrentMarking* concurrent_marking)
      : concurrent_marking_(concurrent_marking),
        trace_id_(reinterpret_cast<uint64_t>(concurrent_marking) ^
                  concurrent_marking->heap_->tracer()->CurrentEpoch(
                      GCTracer::Scope::MINOR_MS_MARK_PARALLEL)) {}

  ~JobTaskMinor() override = default;
  JobTaskMinor(const JobTaskMinor&) = delete;
  JobTaskMinor& operator=(const JobTaskMinor&) = delete;

  // v8::JobTask overrides.
  void Run(JobDelegate* delegate) override {
    // In case multi-cage pointer compression mode is enabled ensure that
    // current thread's cage base values are properly initialized.
    PtrComprCageAccessScope ptr_compr_cage_access_scope(
        concurrent_marking_->heap_->isolate());

    if (delegate->IsJoiningThread()) {
      TRACE_GC_WITH_FLOW(concurrent_marking_->heap_->tracer(),
                         GCTracer::Scope::MINOR_MS_MARK_PARALLEL, trace_id_,
                         TRACE_EVENT_FLAG_FLOW_IN);
      // TRACE_GC is not needed here because the caller opens the right scope.
      concurrent_marking_->RunMinor(delegate);
    } else {
      TRACE_GC_EPOCH_WITH_FLOW(concurrent_marking_->heap_->tracer(),
                               GCTracer::Scope::MINOR_MS_BACKGROUND_MARKING,
                               ThreadKind::kBackground, trace_id_,
                               TRACE_EVENT_FLAG_FLOW_IN);
      concurrent_marking_->RunMinor(delegate);
    }
  }

  size_t GetMaxConcurrency(size_t worker_count) const override {
    return concurrent_marking_->GetMinorMaxConcurrency(worker_count);
  }

  uint64_t trace_id() const { return trace_id_; }

 private:
  ConcurrentMarking* concurrent_marking_;
  const uint64_t trace_id_;
};

ConcurrentMarking::ConcurrentMarking(Heap* heap, WeakObjects* weak_objects)
    : heap_(heap), weak_objects_(weak_objects) {
#ifndef V8_ATOMIC_OBJECT_FIELD_WRITES
  // Concurrent marking requires atomic object field writes.
  CHECK(!v8_flags.concurrent_marking);
#endif
  int max_tasks;
  if (v8_flags.concurrent_marking_max_worker_num == 0) {
    max_tasks = V8::GetCurrentPlatform()->NumberOfWorkerThreads();
  } else {
    max_tasks = v8_flags.concurrent_marking_max_worker_num;
  }

  task_state_.reserve(max_tasks + 1);
  for (int i = 0; i <= max_tasks; ++i) {
    task_state_.emplace_back(std::make_unique<TaskState>());
  }
}

ConcurrentMarking::~ConcurrentMarking() = default;

void ConcurrentMarking::RunMajor(JobDelegate* delegate,
                                 base::EnumSet<CodeFlushMode> code_flush_mode,
                                 unsigned mark_compact_epoch,
                                 bool should_keep_ages_unchanged) {
  size_t kBytesUntilInterruptCheck = 64 * KB;
  int kObjectsUntilInterruptCheck = 1000;
  uint8_t task_id = delegate->GetTaskId() + 1;
  TaskState* task_state = task_state_[task_id].get();
  auto* cpp_heap = CppHeap::From(heap_->cpp_heap());
  MarkingWorklists::Local local_marking_worklists(
      marking_worklists_, cpp_heap
                              ? cpp_heap->CreateCppMarkingState()
                              : MarkingWorklists::Local::kNoCppMarkingState);
  WeakObjects::Local local_weak_objects(weak_objects_);
  ConcurrentMarkingVisitor visitor(
      &local_marking_worklists, &local_weak_objects, heap_, mark_compact_epoch,
      code_flush_mode, should_keep_ages_unchanged,
      heap_->tracer()->CodeFlushingIncrease(), &task_state->memory_chunk_data);
  NativeContextInferrer native_context_inferrer;
  NativeContextStats& native_context_stats = task_state->native_context_stats;
  double time_ms;
  size_t marked_bytes = 0;
  Isolate* isolate = heap_->isolate();
  if (v8_flags.trace_concurrent_marking) {
    isolate->PrintWithTimestamp("Starting major concurrent marking task %d\n",
                                task_id);
  }
  bool another_ephemeron_iteration = false;
  MainAllocator* const new_space_allocator =
      heap_->use_new_space() ? heap_->allocator()->new_space_allocator()
                             : nullptr;

  {
    TimedScope scope(&time_ms);

    {
      Ephemeron ephemeron;
      while (local_weak_objects.current_ephemerons_local.Pop(&ephemeron)) {
        if (visitor.ProcessEphemeron(ephemeron.key, ephemeron.value)) {
          another_ephemeron_iteration = true;
        }
      }
    }
    PtrComprCageBase cage_base(isolate);
    bool is_per_context_mode = local_marking_worklists.IsPerContextMode();
    bool done = false;
    while (!done) {
      size_t current_marked_bytes = 0;
      int objects_processed = 0;
      while (current_marked_bytes < kBytesUntilInterruptCheck &&
             objects_processed < kObjectsUntilInterruptCheck) {
        Tagged<HeapObject> object;
        if (!local_marking_worklists.Pop(&object)) {
          done = true;
          break;
        }
        DCHECK(!InReadOnlySpace(object));
        DCHECK_EQ(GetIsolateFromWritableObject(object), isolate);
        objects_processed++;

        Address new_space_top = kNullAddress;
        Address new_space_limit = kNullAddress;
        Address new_large_object = kNullAddress;

        if (new_space_allocator) {
          // The order of the two loads is important.
          new_space_top = new_space_allocator->original_top_acquire();
          new_space_limit = new_space_allocator->original_limit_relaxed();
        }

        if (heap_->new_lo_space()) {
          new_large_object = heap_->new_lo_space()->pending_object();
        }

        Address addr = object.address();

        if ((new_space_top <= addr && addr < new_space_limit) ||
            addr == new_large_object) {
          local_marking_worklists.PushOnHold(object);
        } else {
          Tagged<Map> map = object->map(cage_base, kAcquireLoad);
          // The marking worklist should never contain filler objects.
          CHECK(!IsFreeSpaceOrFillerMap(map));
          if (is_per_context_mode) {
            Address context;
            if (native_context_inferrer.Infer(cage_base, map, object,
                                              &context)) {
              local_marking_worklists.SwitchToContext(context);
            }
          }
          const auto visited_size = visitor.Visit(map, object);
          visitor.IncrementLiveBytesCached(
              MutablePageMetadata::cast(
                  MemoryChunkMetadata::FromHeapObject(object)),
              ALIGN_TO_ALLOCATION_ALIGNMENT(visited_size));
          if (is_per_context_mode) {
            native_context_stats.IncrementSize(
                local_marking_worklists.Context(), map, object, visited_size);
          }
          current_marked_bytes += visited_size;
        }
      }
      if (objects_processed > 0) another_ephemeron_iteration = true;
      marked_bytes += current_marked_bytes;
      base::AsAtomicWord::Relaxed_Store<size_t>(&task_state->marked_bytes,
                                                marked_bytes);
      if (delegate->ShouldYield()) {
        TRACE_GC_NOTE("ConcurrentMarking::RunMajor Preempted");
        break;
      }
    }

    if (done) {
      Ephemeron ephemeron;
      while (local_weak_objects.discovered_ephemerons_local.Pop(&ephemeron)) {
        if (visitor.ProcessEphemeron(ephemeron.key, ephemeron.value)) {
          another_ephemeron_iteration = true;
        }
      }
    }

    local_marking_worklists.Publish();
    local_weak_objects.Publish();
    base::AsAtomicWord::Relaxed_Store<size_t>(&task_state->marked_bytes, 0);
    total_marked_bytes_ += marked_bytes;

    if (another_ephemeron_iteration) {
      set_another_ephemeron_iteration(true);
    }
  }
  if (v8_flags.trace_concurrent_marking) {
    heap_->isolate()->PrintWithTimestamp(
        "Major task %d concurrently marked %dKB in %.2fms\n", task_id,
        static_cast<int>(marked_bytes / KB), time_ms);
  }

  DCHECK(task_state->local_pretenuring_feedback.empty());
}

class ConcurrentMarking::MinorMarkingState {
 public:
  ~MinorMarkingState() { DCHECK_EQ(0, active_markers_); }

  V8_INLINE void MarkerStarted() {
    active_markers_.fetch_add(1, std::memory_order_relaxed);
  }

  // Returns true if all markers are done.
  V8_INLINE bool MarkerDone() {
    return active_markers_.fetch_sub(1, std::memory_order_relaxed) == 1;
  }

 private:
  std::atomic<int> active_markers_{0};
};

namespace {

V8_INLINE bool IsYoungObjectInLab(MainAllocator* new_space_allocator,
                                  NewLargeObjectSpace* new_lo_space,
                                  Tagged<HeapObject> heap_object) {
  // The order of the two loads is important.
  Address new_space_top = new_space_allocator->original_top_acquire();
  Address new_space_limit = new_space_allocator->original_limit_relaxed();
  Address new_large_object = new_lo_space->pending_object();

  Address addr = heap_object.address();

  return (new_space_top <= addr && addr < new_space_limit) ||
         addr == new_large_object;
}

}  // namespace

template <YoungGenerationMarkingVisitationMode marking_mode>
V8_INLINE size_t ConcurrentMarking::RunMinorImpl(JobDelegate* delegate,
                                                 TaskState* task_state) {
  static constexpr size_t kBytesUntilInterruptCheck = 64 * KB;
  static constexpr int kObjectsUntilInterruptCheck = 1000;
  size_t marked_bytes = 0;
  size_t current_marked_bytes = 0;
  int objects_processed = 0;
  YoungGenerationMarkingVisitor<marking_mode> visitor(
      heap_, &task_state->local_pretenuring_feedback);
  YoungGenerationRememberedSetsMarkingWorklist::Local remembered_sets(
      heap_->minor_mark_sweep_collector()->remembered_sets_marking_handler());
  auto& marking_worklists_local = visitor.marking_worklists_local();
  Isolate* isolate = heap_->isolate();
  minor_marking_state_->MarkerStarted();
  MainAllocator* const new_space_allocator =
      heap_->allocator()->new_space_allocator();
  NewLargeObjectSpace* const new_lo_space = heap_->new_lo_space();

  do {
    if (delegate->IsJoiningThread()) {
      marking_worklists_local.MergeOnHold();
    }
    Tagged<HeapObject> heap_object;
    TRACE_GC_EPOCH(heap_->tracer(),
                   GCTracer::Scope::MINOR_MS_BACKGROUND_MARKING_CLOSURE,
                   ThreadKind::kBackground);
    while (marking_worklists_local.Pop(&heap_object)) {
      if (IsYoungObjectInLab(new_space_allocator, new_lo_space, heap_object)) {
        visitor.marking_worklists_local().PushOnHold(heap_object);
      } else {
        Tagged<Map> map = heap_object->map(isolate);
        const auto visited_size = visitor.Visit(map, heap_object);
        if (visited_size) {
          current_marked_bytes += visited_size;
          visitor.IncrementLiveBytesCached(
              MutablePageMetadata::FromHeapObject(heap_object),
              ALIGN_TO_ALLOCATION_ALIGNMENT(visited_size));
        }
      }

      if (current_marked_bytes >= kBytesUntilInterruptCheck ||
          ++objects_processed >= kObjectsUntilInterruptCheck) {
        marked_bytes += current_marked_bytes;
        if (delegate->ShouldYield()) {
          TRACE_GC_NOTE("ConcurrentMarking::RunMinor Preempted");
          minor_marking_state_->MarkerDone();
          return marked_bytes;
        }
        objects_processed = 0;
        current_marked_bytes = 0;
      }
    }
  } while (remembered_sets.ProcessNextItem(&visitor));
  if (minor_marking_state_->MarkerDone()) {
    // This is the last active marker and it ran out of work. Request GC
    // finalization.
    heap_->minor_mark_sweep_collector()->RequestGC();
  }
  return marked_bytes + current_marked_bytes;
}

void ConcurrentMarking::RunMinor(JobDelegate* delegate) {
  DCHECK(heap_->use_new_space());
  DCHECK_NOT_NULL(heap_->new_lo_space());
  uint8_t task_id = delegate->GetTaskId() + 1;
  DCHECK_LT(task_id, task_state_.size());
  TaskState* task_state = task_state_[task_id].get();
  double time_ms;
  size_t marked_bytes = 0;
  Isolate* isolate = heap_->isolate();
  if (v8_flags.trace_concurrent_marking) {
    isolate->PrintWithTimestamp("Starting minor concurrent marking task %d\n",
                                task_id);
  }

  {
    TimedScope scope(&time_ms);
    if (heap_->minor_mark_sweep_collector()->is_in_atomic_pause()) {
      // This gets a lower bound for estimated concurrency as we may have marked
      // most of the graph concurrently already and may not be using parallism
      // as much.
      estimate_concurrency_.fetch_add(1, std::memory_order_relaxed);
      marked_bytes =
          RunMinorImpl<YoungGenerationMarkingVisitationMode::kParallel>(
              delegate, task_state);
    } else {
      marked_bytes =
          RunMinorImpl<YoungGenerationMarkingVisitationMode::kConcurrent>(
              delegate, task_state);
    }
  }

  if (v8_flags.trace_concurrent_marking) {
    heap_->isolate()->PrintWithTimestamp(
        "Minor task %d concurrently marked %dKB in %.2fms\n", task_id,
        static_cast<int>(marked_bytes / KB), time_ms);
  }

  DCHECK(task_state->memory_chunk_data.empty());
  DCHECK(task_state->native_context_stats.Empty());
  DCHECK_EQ(0, task_state->marked_bytes);
}

size_t ConcurrentMarking::GetMajorMaxConcurrency(size_t worker_count) {
  size_t marking_items = marking_worklists_->shared()->Size();
  marking_items += marking_worklists_->other()->Size();
  for (auto& worklist : marking_worklists_->context_worklists()) {
    marking_items += worklist.worklist->Size();
  }
  const size_t work = std::max<size_t>(
      {marking_items, weak_objects_->discovered_ephemerons.Size(),
       weak_objects_->current_ephemerons.Size()});
  size_t jobs = worker_count + work;
  jobs = std::min<size_t>(task_state_.size() - 1, jobs);
  if (heap_->ShouldOptimizeForBattery()) {
    return std::min<size_t>(jobs, 1);
  }
  return jobs;
}

size_t ConcurrentMarking::GetMinorMaxConcurrency(size_t worker_count) {
  const size_t marking_items = marking_worklists_->shared()->Size() +
                               heap_->minor_mark_sweep_collector()
                                   ->remembered_sets_marking_handler()
                                   ->RemainingRememberedSetsMarkingIteams();
  DCHECK(marking_worklists_->other()->IsEmpty());
  DCHECK(!marking_worklists_->IsUsingContextWorklists());
  size_t jobs = worker_count + marking_items;
  jobs = std::min<size_t>(task_state_.size() - 1, jobs);
  if (heap_->ShouldOptimizeForBattery()) {
    return std::min<size_t>(jobs, 1);
  }
  return jobs;
}

void ConcurrentMarking::TryScheduleJob(GarbageCollector garbage_collector,
                                       TaskPriority priority) {
  DCHECK(v8_flags.parallel_marking || v8_flags.concurrent_marking ||
         v8_flags.concurrent_minor_ms_marking);
  DCHECK(!heap_->IsTearingDown());
  DCHECK(IsStopped());

  DCHECK_NE(garbage_collector, GarbageCollector::SCAVENGER);
  if (garbage_collector == GarbageCollector::MARK_COMPACTOR &&
      !heap_->mark_compact_collector()->UseBackgroundThreadsInCycle()) {
    return;
  }
  if (garbage_collector == GarbageCollector::MINOR_MARK_SWEEPER &&
      !heap_->minor_mark_sweep_collector()->UseBackgroundThreadsInCycle()) {
    return;
  }

  if (v8_flags.concurrent_marking_high_priority_threads) {
    priority = TaskPriority::kUserBlocking;
  }

  // Marking state can only be alive if the concurrent marker was previously
  // stopped.
  DCHECK_IMPLIES(
      minor_marking_state_,
      garbage_collector_.has_value() &&
          (*garbage_collector_ == garbage_collector) &&
          (garbage_collector == GarbageCollector::MINOR_MARK_SWEEPER));
  DCHECK_IMPLIES(
      !garbage_collector_.has_value() ||
          *garbage_collector_ == GarbageCollector::MARK_COMPACTOR,
      std::all_of(task_state_.begin(), task_state_.end(), [](auto& task_state) {
        return task_state->local_pretenuring_feedback.empty();
      }));
  garbage_collector_ = garbage_collector;
  if (garbage_collector == GarbageCollector::MARK_COMPACTOR) {
    heap_->mark_compact_collector()->local_marking_worklists()->Publish();
    marking_worklists_ = heap_->mark_compact_collector()->marking_worklists();
    auto job = std::make_unique<JobTaskMajor>(
        this, heap_->mark_compact_collector()->epoch(),
        heap_->mark_compact_collector()->code_flush_mode(),
        heap_->ShouldCurrentGCKeepAgesUnchanged());
    current_job_trace_id_.emplace(job->trace_id());
    TRACE_GC_NOTE_WITH_FLOW("Major concurrent marking started", job->trace_id(),
                            TRACE_EVENT_FLAG_FLOW_OUT);
    job_handle_ = V8::GetCurrentPlatform()->PostJob(priority, std::move(job));
  } else {
    DCHECK(garbage_collector == GarbageCollector::MINOR_MARK_SWEEPER);
    minor_marking_state_ = std::make_unique<MinorMarkingState>();
    heap_->minor_mark_sweep_collector()->local_marking_worklists()->Publish();
    marking_worklists_ =
        heap_->minor_mark_sweep_collector()->marking_worklists();
    auto job = std::make_unique<JobTaskMinor>(this);
    current_job_trace_id_.emplace(job->trace_id());
    TRACE_GC_NOTE_WITH_FLOW("Minor concurrent marking started", job->trace_id(),
                            TRACE_EVENT_FLAG_FLOW_OUT);
    job_handle_ = V8::GetCurrentPlatform()->PostJob(priority, std::move(job));
  }
  DCHECK(job_handle_->IsValid());
}

bool ConcurrentMarking::IsWorkLeft() const {
  DCHECK(garbage_collector_.has_value());
  if (garbage_collector_ == GarbageCollector::MARK_COMPACTOR) {
    return !marking_worklists_->shared()->IsEmpty() ||
           !weak_objects_->current_ephemerons.IsEmpty() ||
           !weak_objects_->discovered_ephemerons.IsEmpty();
  }
  DCHECK_EQ(GarbageCollector::MINOR_MARK_SWEEPER, garbage_collector_);
  return !marking_worklists_->shared()->IsEmpty() ||
         (heap_->minor_mark_sweep_collector()
              ->remembered_sets_marking_handler()
              ->RemainingRememberedSetsMarkingIteams() > 0);
}

void ConcurrentMarking::RescheduleJobIfNeeded(
    GarbageCollector garbage_collector, TaskPriority priority) {
  DCHECK(v8_flags.parallel_marking || v8_flags.concurrent_marking ||
         v8_flags.concurrent_minor_ms_marking);

  if (garbage_collector == GarbageCollector::MARK_COMPACTOR &&
      !heap_->mark_compact_collector()->UseBackgroundThreadsInCycle()) {
    return;
  }

  if (garbage_collector == GarbageCollector::MINOR_MARK_SWEEPER &&
      !heap_->minor_mark_sweep_collector()->UseBackgroundThreadsInCycle()) {
    return;
  }

  if (heap_->IsTearingDown()) return;

  if (IsStopped()) {
    // This DCHECK is for the case that concurrent marking was paused.
    DCHECK_IMPLIES(garbage_collector_.has_value(),
                   garbage_collector == garbage_collector_);
    TryScheduleJob(garbage_collector, priority);
  } else {
    DCHECK(garbage_collector_.has_value());
    DCHECK_EQ(garbage_collector, garbage_collector_.value());
    if (garbage_collector == GarbageCollector::MARK_COMPACTOR) {
      heap_->mark_compact_collector()->local_marking_worklists()->Publish();
    } else {
      heap_->minor_mark_sweep_collector()->local_marking_worklists()->Publish();
    }
    if (!IsWorkLeft()) return;
    if (priority != TaskPriority::kUserVisible)
      job_handle_->UpdatePriority(priority);
    DCHECK(current_job_trace_id_.has_value());
    TRACE_GC_NOTE_WITH_FLOW(
        garbage_collector_ == GarbageCollector::MARK_COMPACTOR
            ? "Major concurrent marking rescheduled"
            : "Minor concurrent marking rescheduled",
        current_job_trace_id_.value(),
        TRACE_EVENT_FLAG_FLOW_IN | TRACE_EVENT_FLAG_FLOW_OUT);
    job_handle_->NotifyConcurrencyIncrease();
  }
}

void ConcurrentMarking::FlushPretenuringFeedback() {
  PretenuringHandler* pretenuring_handler = heap_->pretenuring_handler();
  for (auto& task_state : task_state_) {
    pretenuring_handler->MergeAllocationSitePretenuringFeedback(
        task_state->local_pretenuring_feedback);
    task_state->local_pretenuring_feedback.clear();
  }
}

void ConcurrentMarking::Join() {
  DCHECK(v8_flags.parallel_marking || v8_flags.concurrent_marking ||
         v8_flags.concurrent_minor_ms_marking);
  DCHECK_IMPLIES(
      garbage_collector_ == GarbageCollector::MARK_COMPACTOR,
      heap_->mark_compact_collector()->UseBackgroundThreadsInCycle());
  if (!job_handle_ || !job_handle_->IsValid()) return;
  job_handle_->Join();
  current_job_trace_id_.reset();
  garbage_collector_.reset();
  minor_marking_state_.reset();
}

bool ConcurrentMarking::Pause() {
  DCHECK(v8_flags.parallel_marking || v8_flags.concurrent_marking);
  if (!job_handle_ || !job_handle_->IsValid()) return false;

  job_handle_->Cancel();
  DCHECK(current_job_trace_id_.has_value());
  TRACE_GC_NOTE_WITH_FLOW(garbage_collector_ == GarbageCollector::MARK_COMPACTOR
                              ? "Major concurrent marking paused"
                              : "Minor concurrent marking paused",
                          current_job_trace_id_.value(),
                          TRACE_EVENT_FLAG_FLOW_IN | TRACE_EVENT_FLAG_FLOW_OUT);
  return true;
}

bool ConcurrentMarking::IsStopped() {
  if (!v8_flags.concurrent_marking && !v8_flags.parallel_marking) return true;

  return !job_handle_ || !job_handle_->IsValid();
}

void ConcurrentMarking::Resume() {
  DCHECK(garbage_collector_.has_value());
  DCHECK(current_job_trace_id_.has_value());
  TRACE_GC_NOTE_WITH_FLOW(garbage_collector_ == GarbageCollector::MARK_COMPACTOR
                              ? "Major concurrent marking resumed"
                              : "Minor concurrent marking resumed",
                          current_job_trace_id_.value(),
                          TRACE_EVENT_FLAG_FLOW_IN | TRACE_EVENT_FLAG_FLOW_OUT);
  RescheduleJobIfNeeded(garbage_collector_.value());
}

void ConcurrentMarking::FlushNativeContexts(NativeContextStats* main_stats) {
  DCHECK(!job_handle_ || !job_handle_->IsValid());
  for (size_t i = 1; i < task_state_.size(); i++) {
    main_stats->Merge(task_state_[i]->native_context_stats);
    task_state_[i]->native_context_stats.Clear();
  }
}

void ConcurrentMarking::FlushMemoryChunkData() {
  DCHECK(!job_handle_ || !job_handle_->IsValid());
  for (size_t i = 1; i < task_state_.size(); i++) {
    MemoryChunkDataMap& memory_chunk_data = task_state_[i]->memory_chunk_data;
    for (auto& pair : memory_chunk_data) {
      // ClearLiveness sets the live bytes to zero.
      // Pages with zero live bytes might be already unmapped.
      MutablePageMetadata* memory_chunk = pair.first;
      MemoryChunkData& data = pair.second;
      if (data.live_bytes) {
        memory_chunk->IncrementLiveBytesAtomically(data.live_bytes);
      }
      if (data.typed_slots) {
        RememberedSet<OLD_TO_OLD>::MergeTyped(memory_chunk,
                                              std::move(data.typed_slots));
      }
    }
    memory_chunk_data.clear();
    task_state_[i]->marked_bytes = 0;
  }
  total_marked_bytes_ = 0;
}

void ConcurrentMarking::ClearMemoryChunkData(MutablePageMetadata* chunk) {
  DCHECK(!job_handle_ || !job_handle_->IsValid());
  for (size_t i = 1; i < task_state_.size(); i++) {
    task_state_[i]->memory_chunk_data.erase(chunk);
  }
}

size_t ConcurrentMarking::TotalMarkedBytes() {
  size_t result = 0;
  for (size_t i = 1; i < task_state_.size(); i++) {
    result +=
        base::AsAtomicWord::Relaxed_Load<size_t>(&task_state_[i]->marked_bytes);
  }
  result += total_marked_bytes_;
  return result;
}

ConcurrentMarking::PauseScope::PauseScope(ConcurrentMarking* concurrent_marking)
    : concurrent_marking_(concurrent_marking),
      resume_on_exit_(v8_flags.concurrent_marking &&
                      concurrent_marking_->Pause()) {
  DCHECK(!v8_flags.minor_ms);
  DCHECK_IMPLIES(resume_on_exit_, v8_flags.concurrent_marking);
}

ConcurrentMarking::PauseScope::~PauseScope() {
  if (resume_on_exit_) {
    DCHECK_EQ(concurrent_marking_->garbage_collector_,
              GarbageCollector::MARK_COMPACTOR);
    concurrent_marking_->Resume();
  }
}

}  // namespace internal
}  // namespace v8
