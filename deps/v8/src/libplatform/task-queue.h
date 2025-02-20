// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_LIBPLATFORM_TASK_QUEUE_H_
#define V8_LIBPLATFORM_TASK_QUEUE_H_

#include <memory>
#include <queue>

#include "include/libplatform/libplatform-export.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/semaphore.h"
#include "testing/gtest/include/gtest/gtest_prod.h"  // nogncheck

namespace v8 {

class Task;

namespace platform {

class V8_PLATFORM_EXPORT TaskQueue {
 public:
  TaskQueue();
  ~TaskQueue();

  TaskQueue(const TaskQueue&) = delete;
  TaskQueue& operator=(const TaskQueue&) = delete;

  // Appends a task to the queue. The queue takes ownership of |task|.
  void Append(std::unique_ptr<Task> task);

  // Returns the next task to process. Blocks if no task is available. Returns
  // nullptr if the queue is terminated.
  std::unique_ptr<Task> GetNext();

  // Terminate the queue.
  void Terminate();

 private:
  FRIEND_TEST(WorkerThreadTest, PostSingleTask);

  void BlockUntilQueueEmptyForTesting();

  base::Semaphore process_queue_semaphore_;
  base::SpinningMutex lock_;
  std::queue<std::unique_ptr<Task>> task_queue_;
  bool terminated_;
};

}  // namespace platform
}  // namespace v8


#endif  // V8_LIBPLATFORM_TASK_QUEUE_H_
