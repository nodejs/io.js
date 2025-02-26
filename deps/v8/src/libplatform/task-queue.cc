// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/libplatform/task-queue.h"

#include "include/v8-platform.h"
#include "src/base/logging.h"
#include "src/base/platform/platform.h"
#include "src/base/platform/time.h"

namespace v8 {
namespace platform {

TaskQueue::TaskQueue() : process_queue_semaphore_(0), terminated_(false) {}


TaskQueue::~TaskQueue() {
  base::SpinningMutexGuard guard(&lock_);
  DCHECK(terminated_);
  DCHECK(task_queue_.empty());
}

void TaskQueue::Append(std::unique_ptr<Task> task) {
  base::SpinningMutexGuard guard(&lock_);
  DCHECK(!terminated_);
  task_queue_.push(std::move(task));
  process_queue_semaphore_.Signal();
}

std::unique_ptr<Task> TaskQueue::GetNext() {
  for (;;) {
    {
      base::SpinningMutexGuard guard(&lock_);
      if (!task_queue_.empty()) {
        std::unique_ptr<Task> result = std::move(task_queue_.front());
        task_queue_.pop();
        return result;
      }
      if (terminated_) {
        process_queue_semaphore_.Signal();
        return nullptr;
      }
    }
    process_queue_semaphore_.Wait();
  }
}


void TaskQueue::Terminate() {
  base::SpinningMutexGuard guard(&lock_);
  DCHECK(!terminated_);
  terminated_ = true;
  process_queue_semaphore_.Signal();
}

void TaskQueue::BlockUntilQueueEmptyForTesting() {
  for (;;) {
    {
      base::SpinningMutexGuard guard(&lock_);
      if (task_queue_.empty()) return;
    }
    base::OS::Sleep(base::TimeDelta::FromMilliseconds(5));
  }
}

}  // namespace platform
}  // namespace v8
