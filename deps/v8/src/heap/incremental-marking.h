// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_INCREMENTAL_MARKING_H_
#define V8_HEAP_INCREMENTAL_MARKING_H_

#include <cstdint>

#include "src/base/functional.h"
#include "src/base/logging.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/time.h"
#include "src/common/globals.h"
#include "src/heap/heap.h"
#include "src/heap/incremental-marking-job.h"
#include "src/heap/mark-compact.h"
#include "src/heap/mutable-page.h"
#include "src/tasks/cancelable-task.h"

namespace v8 {
namespace internal {

class HeapObject;
class MarkBit;
class Map;
class Object;
class PagedSpace;

// Describes in which context IncrementalMarking::Step() is used in. This
// information is used when marking finishes and for marking progress
// heuristics.
enum class StepOrigin {
  // The caller of Step() is not allowed to complete marking right away. A task
  // is scheduled to complete the GC. When the task isn't
  // run soon enough, the stack guard mechanism will be used.
  kV8,

  // The caller of Step() will complete marking by running the GC right
  // afterwards.
  kTask
};

constexpr const char* ToString(StepOrigin step_origin) {
  switch (step_origin) {
    case StepOrigin::kV8:
      return "V8";
    case StepOrigin::kTask:
      return "task";
  }
}

class V8_EXPORT_PRIVATE IncrementalMarking final {
 public:
  class V8_NODISCARD PauseBlackAllocationScope final {
   public:
    explicit PauseBlackAllocationScope(IncrementalMarking* marking);
    ~PauseBlackAllocationScope();

    PauseBlackAllocationScope(const PauseBlackAllocationScope&) = delete;
    PauseBlackAllocationScope& operator=(const PauseBlackAllocationScope&) =
        delete;

   private:
    IncrementalMarking* const marking_;
    bool paused_ = false;
  };

  V8_INLINE void TransferColor(Tagged<HeapObject> from, Tagged<HeapObject> to);

  IncrementalMarking(Heap* heap, WeakObjects* weak_objects);

  IncrementalMarking(const IncrementalMarking&) = delete;
  IncrementalMarking& operator=(const IncrementalMarking&) = delete;

  MarkingMode marking_mode() const { return marking_mode_; }

  bool IsMinorMarking() const {
    return marking_mode_ == MarkingMode::kMinorMarking;
  }
  bool IsMajorMarking() const {
    return marking_mode_ == MarkingMode::kMajorMarking;
  }

  bool IsStopped() const { return !IsMarking(); }
  bool IsMarking() const { return marking_mode_ != MarkingMode::kNoMarking; }
  bool IsMajorMarkingComplete() const {
    return IsMajorMarking() && ShouldFinalize();
  }

  bool MajorCollectionRequested() const {
    return major_collection_requested_via_stack_guard_;
  }

  bool CanBeStarted() const;
  void Start(GarbageCollector garbage_collector,
             GarbageCollectionReason gc_reason);
  // Returns true if incremental marking was running and false otherwise.
  bool Stop();

  void UpdateMarkingWorklistAfterScavenge();
  void UpdateMarkedBytesAfterScavenge(size_t dead_bytes_in_new_space);

  // Performs incremental marking step and finalizes marking if complete.
  void AdvanceAndFinalizeIfComplete();

  // Performs incremental marking step and finalizes marking if the stack guard
  // was already armed. If marking is complete but the stack guard wasn't armed
  // yet, a finalization task is scheduled.
  void AdvanceAndFinalizeIfNecessary();

  // Performs incremental marking step and schedules job for finalization if
  // marking completes.
  void AdvanceOnAllocation();

  bool IsAheadOfSchedule() const;

  bool IsCompacting() { return IsMajorMarking() && is_compacting_; }

  Heap* heap() const { return heap_; }
  Isolate* isolate() const;

  IncrementalMarkingJob* incremental_marking_job() const {
    return incremental_marking_job_.get();
  }

  bool black_allocation() { return black_allocation_; }

  bool IsBelowActivationThresholds() const;

  void MarkBlackBackground(Tagged<HeapObject> obj, int object_size);

  void MarkRootsForTesting();

  // Performs incremental marking step for unit tests.
  void AdvanceForTesting(v8::base::TimeDelta max_duration,
                         size_t max_bytes_to_mark = SIZE_MAX);

  uint64_t current_trace_id() const { return current_trace_id_.value(); }

 private:
  class IncrementalMarkingRootMarkingVisitor;

  class Observer final : public AllocationObserver {
   public:
    Observer(IncrementalMarking* incremental_marking, intptr_t step_size);
    ~Observer() override = default;
    void Step(int bytes_allocated, Address, size_t) override;

   private:
    IncrementalMarking* const incremental_marking_;
  };

  void StartMarkingMajor();
  void StartMarkingMinor();

  void StartBlackAllocation();
  void PauseBlackAllocation();
  void FinishBlackAllocation();

  void StartPointerTableBlackAllocation();
  void StopPointerTableBlackAllocation();

  void MarkRoots();
  // Returns true if the function succeeds in transitioning the object
  // from white to grey.
  bool WhiteToGreyAndPush(Tagged<HeapObject> obj);
  void PublishWriteBarrierWorklists();

  // Fetches marked byte counters from the concurrent marker.
  void FetchBytesMarkedConcurrently();
  size_t GetScheduledBytes(StepOrigin step_origin);

  bool ShouldFinalize() const;

  bool ShouldWaitForTask();
  bool TryInitializeTaskTimeout();

  // Returns the actual used time.
  v8::base::TimeDelta EmbedderStep(v8::base::TimeDelta expected_duration);
  void Step(v8::base::TimeDelta max_duration, size_t max_bytes_to_process,
            StepOrigin step_origin);

  size_t OldGenerationSizeOfObjects() const;

  MarkingState* marking_state() { return marking_state_; }
  MarkingWorklists::Local* local_marking_worklists() const {
    return current_local_marking_worklists_;
  }

  Heap* const heap_;
  MarkCompactCollector* const major_collector_;
  MinorMarkSweepCollector* const minor_collector_;
  WeakObjects* weak_objects_;
  MarkingWorklists::Local* current_local_marking_worklists_ = nullptr;
  MarkingState* const marking_state_;
  v8::base::TimeTicks start_time_;
  size_t main_thread_marked_bytes_ = 0;
  // A sample of concurrent_marking()->TotalMarkedBytes() at the last
  // incremental marking step.
  size_t bytes_marked_concurrently_ = 0;
  MarkingMode marking_mode_ = MarkingMode::kNoMarking;

  bool is_compacting_ = false;
  bool black_allocation_ = false;
  bool completion_task_scheduled_ = false;
  v8::base::TimeTicks completion_task_timeout_;
  bool major_collection_requested_via_stack_guard_ = false;
  std::unique_ptr<IncrementalMarkingJob> incremental_marking_job_;
  Observer new_generation_observer_;
  Observer old_generation_observer_;
  base::Mutex background_live_bytes_mutex_;
  std::unordered_map<MutablePageMetadata*, intptr_t,
                     base::hash<MutablePageMetadata*>>
      background_live_bytes_;
  std::unique_ptr<::heap::base::IncrementalMarkingSchedule> schedule_;
  base::Optional<uint64_t> current_trace_id_;

  friend class IncrementalMarkingJob;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_INCREMENTAL_MARKING_H_
