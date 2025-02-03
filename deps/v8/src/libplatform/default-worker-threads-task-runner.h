// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_LIBPLATFORM_DEFAULT_WORKER_THREADS_TASK_RUNNER_H_
#define V8_LIBPLATFORM_DEFAULT_WORKER_THREADS_TASK_RUNNER_H_

#include <memory>
#include <vector>

#include "include/libplatform/libplatform-export.h"
#include "include/v8-platform.h"
#include "src/base/platform/condition-variable.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/platform.h"
#include "src/libplatform/delayed-task-queue.h"

namespace v8 {
namespace platform {

class V8_PLATFORM_EXPORT DefaultWorkerThreadsTaskRunner
    : public NON_EXPORTED_BASE(TaskRunner) {
 public:
  using TimeFunction = double (*)();

  DefaultWorkerThreadsTaskRunner(
      uint32_t thread_pool_size, TimeFunction time_function,
      base::Thread::Priority priority = base::Thread::Priority::kDefault);

  ~DefaultWorkerThreadsTaskRunner() override;

  void Terminate();

  double MonotonicallyIncreasingTime();

  // v8::TaskRunner implementation.
  bool IdleTasksEnabled() override;

 private:
  // v8::TaskRunner implementation.
  void PostTaskImpl(std::unique_ptr<Task> task,
                    const SourceLocation& location) override;

  void PostDelayedTaskImpl(std::unique_ptr<Task> task, double delay_in_seconds,
                           const SourceLocation& location) override;

  void PostIdleTaskImpl(std::unique_ptr<IdleTask> task,
                        const SourceLocation& location) override;

  class WorkerThread : public base::Thread {
   public:
    explicit WorkerThread(DefaultWorkerThreadsTaskRunner* runner,
                          base::Thread::Priority priority);
    ~WorkerThread() override;

    WorkerThread(const WorkerThread&) = delete;
    WorkerThread& operator=(const WorkerThread&) = delete;

    // This thread attempts to get tasks in a loop from |runner_| and run them.
    void Run() override;

    void Notify();

   private:
    DefaultWorkerThreadsTaskRunner* runner_;
    base::ConditionVariable condition_var_;
  };

  // Called by the WorkerThread. Gets the next take (delayed or immediate) to be
  // executed. Blocks if no task is available.
  std::unique_ptr<Task> GetNext();

  bool terminated_ = false;
  base::Mutex lock_;
  // Vector of idle threads -- these are pushed in LIFO order, so that the most
  // recently active thread is the first to be reactivated.
  std::vector<WorkerThread*> idle_threads_;
  std::vector<std::unique_ptr<WorkerThread>> thread_pool_;
  // Worker threads access this queue, so we can only destroy it after all
  // workers stopped.
  DelayedTaskQueue queue_;
  std::queue<std::unique_ptr<Task>> task_queue_;
  TimeFunction time_function_;
};

}  // namespace platform
}  // namespace v8

#endif  // V8_LIBPLATFORM_DEFAULT_WORKER_THREADS_TASK_RUNNER_H_
