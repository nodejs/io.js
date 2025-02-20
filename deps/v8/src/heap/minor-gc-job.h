// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MINOR_GC_JOB_H_
#define V8_HEAP_MINOR_GC_JOB_H_

#include <memory>

#include "src/common/globals.h"
#include "src/heap/allocation-observer.h"
#include "src/tasks/cancelable-task.h"

namespace v8::internal {

class Heap;

// The job allows for running young generation garbage collection via platform
// foreground tasks.
//
// The job automatically adds an observer to schedule a task when
// `--minor_gc_task_trigger` in percent of the regular limit is reached. The job
// itself never checks the schedule.
class MinorGCJob final {
 public:
  explicit MinorGCJob(Heap* heap) V8_NOEXCEPT;

  // Tries to schedule a new minor GC task.
  void TryScheduleTask();

  // Cancels any previously scheduled minor GC tasks that have not yet run.
  void CancelTaskIfScheduled();

 private:
  class Task;

  bool IsScheduled() const {
    return current_task_id_ != CancelableTaskManager::kInvalidTaskId;
  }

  Heap* const heap_;
  CancelableTaskManager::Id current_task_id_ =
      CancelableTaskManager::kInvalidTaskId;
  std::unique_ptr<AllocationObserver> minor_gc_task_observer_;
};

}  // namespace v8::internal

#endif  // V8_HEAP_MINOR_GC_JOB_H_
