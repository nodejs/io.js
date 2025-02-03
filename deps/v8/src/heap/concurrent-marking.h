// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CONCURRENT_MARKING_H_
#define V8_HEAP_CONCURRENT_MARKING_H_

#include <atomic>
#include <memory>
#include <optional>

#include "include/v8-platform.h"
#include "src/base/atomic-utils.h"
#include "src/base/platform/condition-variable.h"
#include "src/base/platform/mutex.h"
#include "src/heap/marking-visitor.h"
#include "src/heap/marking-worklist.h"
#include "src/heap/memory-measurement.h"
#include "src/heap/slot-set.h"
#include "src/heap/spaces.h"
#include "src/heap/young-generation-marking-visitor.h"
#include "src/init/v8.h"
#include "src/tasks/cancelable-task.h"
#include "src/utils/allocation.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {

class Heap;
class Isolate;
class NonAtomicMarkingState;
class MutablePageMetadata;
class WeakObjects;

class V8_EXPORT_PRIVATE ConcurrentMarking {
 public:
  // When the scope is entered, the concurrent marking tasks
  // are preempted and are not looking at the heap objects, concurrent marking
  // is resumed when the scope is exited.
  class V8_NODISCARD PauseScope {
   public:
    explicit PauseScope(ConcurrentMarking* concurrent_marking);
    ~PauseScope();

   private:
    ConcurrentMarking* const concurrent_marking_;
    const bool resume_on_exit_;
  };

  ConcurrentMarking(Heap* heap, WeakObjects* weak_objects);
  ~ConcurrentMarking();

  // Schedules asynchronous job to perform concurrent marking at |priority|.
  // Objects in the heap should not be moved while these are active (can be
  // stopped safely via Stop() or PauseScope).
  void TryScheduleJob(GarbageCollector garbage_collector,
                      TaskPriority priority = TaskPriority::kUserVisible);

  // Waits for scheduled job to complete.
  void Join();
  // Preempts ongoing job ASAP. Returns true if concurrent marking was in
  // progress, false otherwise.
  bool Pause();

  // Schedules asynchronous job to perform concurrent marking at |priority| if
  // not already running, otherwise adjusts the number of workers running job
  // and the priority if different from the default kUserVisible.
  void RescheduleJobIfNeeded(
      GarbageCollector garbage_collector,
      TaskPriority priority = TaskPriority::kUserVisible);
  // Flushes native context sizes to the given table of the main thread.
  void FlushNativeContexts(NativeContextStats* main_stats);
  // Flushes memory chunk data.
  void FlushMemoryChunkData();
  // This function is called for a new space page that was cleared after
  // scavenge and is going to be re-used.
  void ClearMemoryChunkData(MutablePageMetadata* chunk);
  // Flushes pretenuring feedback.
  void FlushPretenuringFeedback();

  // Checks if all threads are stopped.
  bool IsStopped();

  size_t TotalMarkedBytes();

  void set_another_ephemeron_iteration(bool another_ephemeron_iteration) {
    another_ephemeron_iteration_.store(another_ephemeron_iteration);
  }
  bool another_ephemeron_iteration() {
    return another_ephemeron_iteration_.load();
  }

  GarbageCollector garbage_collector() const {
    DCHECK(garbage_collector_.has_value());
    return garbage_collector_.value();
  }

  bool IsWorkLeft() const;

  size_t FetchAndResetConcurrencyEstimate() {
    const size_t estimate =
        estimate_concurrency_.exchange(0, std::memory_order_relaxed);
    return estimate ? estimate : 1;
  }

 private:
  struct TaskState;
  class JobTaskMinor;
  class JobTaskMajor;
  class MinorMarkingState;

  void RunMinor(JobDelegate* delegate);
  template <YoungGenerationMarkingVisitationMode marking_mode>
  size_t RunMinorImpl(JobDelegate* delegate, TaskState* task_state);
  void RunMajor(JobDelegate* delegate,
                base::EnumSet<CodeFlushMode> code_flush_mode,
                unsigned mark_compact_epoch, bool should_keep_ages_unchanged);
  size_t GetMajorMaxConcurrency(size_t worker_count);
  size_t GetMinorMaxConcurrency(size_t worker_count);
  void Resume();

  std::unique_ptr<JobHandle> job_handle_;
  Heap* const heap_;
  std::optional<GarbageCollector> garbage_collector_;
  MarkingWorklists* marking_worklists_;
  WeakObjects* const weak_objects_;
  std::vector<std::unique_ptr<TaskState>> task_state_;
  std::atomic<size_t> total_marked_bytes_{0};
  std::atomic<bool> another_ephemeron_iteration_{false};
  std::optional<uint64_t> current_job_trace_id_;
  std::unique_ptr<MinorMarkingState> minor_marking_state_;
  std::atomic<size_t> estimate_concurrency_{0};

  friend class Heap;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_CONCURRENT_MARKING_H_
