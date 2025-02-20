// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/minor-gc-job.h"

#include <memory>

#include "src/execution/isolate.h"
#include "src/execution/vm-state-inl.h"
#include "src/flags/flags.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap.h"
#include "src/init/v8.h"
#include "src/tasks/cancelable-task.h"

namespace v8::internal {

namespace {

size_t YoungGenerationTaskTriggerSize(Heap* heap) {
  size_t young_capacity = 0;
  if (v8_flags.sticky_mark_bits) {
    // TODO(333906585): Adjust parameters.
    young_capacity = heap->sticky_space()->Capacity() -
                     heap->sticky_space()->old_objects_size();
  } else {
    young_capacity = heap->new_space()->TotalCapacity();
  }
  return young_capacity * v8_flags.minor_gc_task_trigger / 100;
}

size_t YoungGenerationSize(Heap* heap) {
  return v8_flags.sticky_mark_bits ? heap->sticky_space()->young_objects_size()
                                   : heap->new_space()->Size();
}

}  // namespace

// Task observer that is registered on creation and automatically unregistered
// on first step when a task is scheduled. The idea here is to not interrupt the
// mutator with slow-path allocations for small allocation observer steps but
// rather only interrupt a single time.
//
// A task should be scheduled when young generation size reaches the task
// trigger, but may also occur before the trigger is reached. For example,
// this method is called from the allocation observer for new space. The
// observer step size is detemine based on the current task trigger. However,
// due to refining allocated bytes after sweeping (allocated bytes after
// sweeping may be less than live bytes during marking), new space size may
// decrease while the observer step size remains the same.
//
// The job only checks the schedule on step and relies on the minor GC
// cancelling the job in case it is run.
class ScheduleMinorGCTaskObserver final : public AllocationObserver {
 public:
  ScheduleMinorGCTaskObserver(MinorGCJob* job, Heap* heap)
      : AllocationObserver(kNotUsingFixedStepSize), heap_(heap), job_(job) {
    // Register GC callback for all atomic pause types.
    heap_->main_thread_local_heap()->AddGCEpilogueCallback(
        &GCEpilogueCallback, this, GCCallbacksInSafepoint::GCType::kLocal);
    AddToNewSpace();
  }

  ~ScheduleMinorGCTaskObserver() final {
    RemoveFromNewSpace();
    heap_->main_thread_local_heap()->RemoveGCEpilogueCallback(
        &GCEpilogueCallback, this);
  }

  intptr_t GetNextStepSize() final {
    const size_t new_space_threshold = YoungGenerationTaskTriggerSize(heap_);
    const size_t new_space_size = YoungGenerationSize(heap_);
    if (new_space_size < new_space_threshold) {
      return new_space_threshold - new_space_size;
    }
    // Force a step on next allocation.
    return 1;
  }

  void Step(int, Address, size_t) final {
    job_->TryScheduleTask();
    // Remove this observer. It will be re-added after a GC.
    heap_->allocator()->new_space_allocator()->RemoveAllocationObserver(this);
    DCHECK(was_added_to_space_);
    was_added_to_space_ = false;
  }

 private:
  static void GCEpilogueCallback(void* data) {
    ScheduleMinorGCTaskObserver* observer =
        reinterpret_cast<ScheduleMinorGCTaskObserver*>(data);
    observer->RemoveFromNewSpace();
    observer->AddToNewSpace();
  }

  void AddToNewSpace() {
    DCHECK(!was_added_to_space_);
    DCHECK_IMPLIES(v8_flags.minor_ms,
                   !heap_->allocator()->new_space_allocator()->IsLabValid());
    heap_->allocator()->new_space_allocator()->AddAllocationObserver(this);
    was_added_to_space_ = true;
  }

  void RemoveFromNewSpace() {
    if (!was_added_to_space_) {
      return;
    }
    heap_->allocator()->new_space_allocator()->RemoveAllocationObserver(this);
    was_added_to_space_ = false;
  }

  Heap* heap_;
  MinorGCJob* const job_;
  bool was_added_to_space_ = false;
};

class MinorGCJob::Task : public CancelableTask {
 public:
  Task(Isolate* isolate, MinorGCJob* job)
      : CancelableTask(isolate), isolate_(isolate), job_(job) {}

  // CancelableTask overrides.
  void RunInternal() override;

  Isolate* isolate() const { return isolate_; }

 private:
  Isolate* const isolate_;
  MinorGCJob* const job_;
};

MinorGCJob::MinorGCJob(Heap* heap) V8_NOEXCEPT
    : heap_(heap),
      minor_gc_task_observer_(new ScheduleMinorGCTaskObserver(this, heap)) {}

void MinorGCJob::TryScheduleTask() {
  if (!v8_flags.minor_gc_task || IsScheduled() || heap_->IsTearingDown()) {
    return;
  }
  const auto priority = v8_flags.minor_gc_task_with_lower_priority
                            ? TaskPriority::kUserVisible
                            : TaskPriority::kUserBlocking;
  std::shared_ptr<v8::TaskRunner> taskrunner =
      heap_->GetForegroundTaskRunner(priority);
  if (taskrunner->NonNestableTasksEnabled()) {
    std::unique_ptr<Task> task = std::make_unique<Task>(heap_->isolate(), this);
    current_task_id_ = task->id();
    taskrunner->PostNonNestableTask(std::move(task));
  }
}

void MinorGCJob::CancelTaskIfScheduled() {
  if (!IsScheduled()) {
    return;
  }
  // The task may have ran and bailed out already if major incremental marking
  // was running, in which `TryAbort` will return `kTaskRemoved`.
  heap_->isolate()->cancelable_task_manager()->TryAbort(current_task_id_);
  current_task_id_ = CancelableTaskManager::kInvalidTaskId;
}

void MinorGCJob::Task::RunInternal() {
  VMState<GC> state(isolate());
  TRACE_EVENT_CALL_STATS_SCOPED(isolate(), "v8", "V8.MinorGCJob.Task");

  DCHECK_EQ(job_->current_task_id_, id());
  job_->current_task_id_ = CancelableTaskManager::kInvalidTaskId;

  Heap* heap = isolate()->heap();

  if (v8_flags.separate_gc_phases &&
      heap->incremental_marking()->IsMajorMarking()) {
    // Don't trigger a minor GC while major incremental marking is active.
    return;
  }

  heap->CollectGarbage(NEW_SPACE, GarbageCollectionReason::kTask);
}

}  // namespace v8::internal
