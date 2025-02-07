// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_INCREMENTAL_MARKING_JOB_H_
#define V8_HEAP_INCREMENTAL_MARKING_JOB_H_

#include <optional>

#include "include/v8-platform.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/time.h"

namespace v8::internal {

class Heap;
class Isolate;

// The incremental marking job uses platform tasks to perform incremental
// marking actions (start, step, finalize). The job posts regular foreground
// tasks or delayed foreground tasks if marking progress allows.
class IncrementalMarkingJob final {
 public:
  explicit IncrementalMarkingJob(Heap* heap);

  IncrementalMarkingJob(const IncrementalMarkingJob&) = delete;
  IncrementalMarkingJob& operator=(const IncrementalMarkingJob&) = delete;

  // Schedules a task with the given `priority`. Safe to be called from any
  // thread.
  void ScheduleTask(TaskPriority priority = TaskPriority::kUserBlocking);

  // Returns a weighted average of time to task. For delayed tasks the time to
  // task is only recorded after the initial delay. In case a task is currently
  // running, it is added to the average.
  std::optional<v8::base::TimeDelta> AverageTimeToTask() const;

  std::optional<v8::base::TimeDelta> CurrentTimeToTask() const;

 private:
  class Task;

  Heap* const heap_;
  const std::shared_ptr<v8::TaskRunner> user_blocking_task_runner_;
  const std::shared_ptr<v8::TaskRunner> user_visible_task_runner_;
  mutable base::Mutex mutex_;
  v8::base::TimeTicks scheduled_time_;
  bool pending_task_ = false;
};

}  // namespace v8::internal

#endif  // V8_HEAP_INCREMENTAL_MARKING_JOB_H_
