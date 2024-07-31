// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MINOR_GC_JOB_H_
#define V8_HEAP_MINOR_GC_JOB_H_

#include "src/common/globals.h"
#include "src/tasks/cancelable-task.h"

namespace v8 {
namespace internal {

class Heap;
class Isolate;

// The scavenge job uses platform tasks to perform a young generation
// Scavenge garbage collection. The job posts a foreground task.
class MinorGCJob {
 public:
  explicit MinorGCJob(Heap* heap) V8_NOEXCEPT : heap_(heap) {}

  void ScheduleTask();

  void CancelTaskIfScheduled();

  static size_t YoungGenerationTaskTriggerSize(Heap* heap);

 private:
  class Task;

  static bool YoungGenerationSizeTaskTriggerReached(Heap* heap);

  Heap* const heap_;
  CancelableTaskManager::Id current_task_id_ =
      CancelableTaskManager::kInvalidTaskId;
};
}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MINOR_GC_JOB_H_
