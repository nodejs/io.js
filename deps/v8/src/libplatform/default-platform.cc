// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/libplatform/default-platform.h"

#include <algorithm>
#include <queue>

#include "include/libplatform/libplatform.h"
#include "src/base/debug/stack_trace.h"
#include "src/base/logging.h"
#include "src/base/page-allocator.h"
#include "src/base/platform/platform.h"
#include "src/base/platform/time.h"
#include "src/base/sys-info.h"
#include "src/libplatform/default-foreground-task-runner.h"
#include "src/libplatform/default-job.h"
#include "src/libplatform/default-worker-threads-task-runner.h"

namespace v8 {
namespace platform {

namespace {

void PrintStackTrace() {
  v8::base::debug::StackTrace trace;
  trace.Print();
  // Avoid dumping duplicate stack trace on abort signal.
  v8::base::debug::DisableSignalStackDump();
}

constexpr int kMaxThreadPoolSize = 16;

int GetActualThreadPoolSize(int thread_pool_size) {
  DCHECK_GE(thread_pool_size, 0);
  if (thread_pool_size < 1) {
    thread_pool_size = base::SysInfo::NumberOfProcessors() - 1;
  }
  return std::max(std::min(thread_pool_size, kMaxThreadPoolSize), 1);
}

}  // namespace

std::unique_ptr<v8::Platform> NewDefaultPlatform(
    int thread_pool_size, IdleTaskSupport idle_task_support,
    InProcessStackDumping in_process_stack_dumping,
    std::unique_ptr<v8::TracingController> tracing_controller,
    PriorityMode priority_mode) {
  if (in_process_stack_dumping == InProcessStackDumping::kEnabled) {
    v8::base::debug::EnableInProcessStackDumping();
  }
  thread_pool_size = GetActualThreadPoolSize(thread_pool_size);
  auto platform = std::make_unique<DefaultPlatform>(
      thread_pool_size, idle_task_support, std::move(tracing_controller),
      priority_mode);
  return platform;
}

std::unique_ptr<v8::Platform> NewSingleThreadedDefaultPlatform(
    IdleTaskSupport idle_task_support,
    InProcessStackDumping in_process_stack_dumping,
    std::unique_ptr<v8::TracingController> tracing_controller) {
  if (in_process_stack_dumping == InProcessStackDumping::kEnabled) {
    v8::base::debug::EnableInProcessStackDumping();
  }
  auto platform = std::make_unique<DefaultPlatform>(
      0, idle_task_support, std::move(tracing_controller));
  return platform;
}

V8_PLATFORM_EXPORT std::unique_ptr<JobHandle> NewDefaultJobHandle(
    Platform* platform, TaskPriority priority,
    std::unique_ptr<JobTask> job_task, size_t num_worker_threads) {
  return std::make_unique<DefaultJobHandle>(std::make_shared<DefaultJobState>(
      platform, std::move(job_task), priority, num_worker_threads));
}

bool PumpMessageLoop(v8::Platform* platform, v8::Isolate* isolate,
                     MessageLoopBehavior behavior) {
  return static_cast<DefaultPlatform*>(platform)->PumpMessageLoop(isolate,
                                                                  behavior);
}

void RunIdleTasks(v8::Platform* platform, v8::Isolate* isolate,
                  double idle_time_in_seconds) {
  static_cast<DefaultPlatform*>(platform)->RunIdleTasks(isolate,
                                                        idle_time_in_seconds);
}

void NotifyIsolateShutdown(v8::Platform* platform, Isolate* isolate) {
  static_cast<DefaultPlatform*>(platform)->NotifyIsolateShutdown(isolate);
}

DefaultPlatform::DefaultPlatform(
    int thread_pool_size, IdleTaskSupport idle_task_support,
    std::unique_ptr<v8::TracingController> tracing_controller,
    PriorityMode priority_mode)
    : thread_pool_size_(thread_pool_size),
      idle_task_support_(idle_task_support),
      tracing_controller_(std::move(tracing_controller)),
      page_allocator_(std::make_unique<v8::base::PageAllocator>()),
      priority_mode_(priority_mode) {
  if (!tracing_controller_) {
    tracing::TracingController* controller = new tracing::TracingController();
#if !defined(V8_USE_PERFETTO)
    controller->Initialize(nullptr);
#endif
    tracing_controller_.reset(controller);
  }
  if (thread_pool_size_ > 0) {
    EnsureBackgroundTaskRunnerInitialized();
  }
}

DefaultPlatform::~DefaultPlatform() {
  base::MutexGuard guard(&lock_);
  if (worker_threads_task_runners_[0]) {
    for (int i = 0; i < num_worker_runners(); i++) {
      worker_threads_task_runners_[i]->Terminate();
    }
  }
  for (const auto& it : foreground_task_runner_map_) {
    it.second->Terminate();
  }
}

namespace {

double DefaultTimeFunction() {
  return base::TimeTicks::Now().ToInternalValue() /
         static_cast<double>(base::Time::kMicrosecondsPerSecond);
}

}  // namespace

void DefaultPlatform::EnsureBackgroundTaskRunnerInitialized() {
  DCHECK_NULL(worker_threads_task_runners_[0]);
  for (int i = 0; i < num_worker_runners(); i++) {
    worker_threads_task_runners_[i] =
        std::make_shared<DefaultWorkerThreadsTaskRunner>(
            thread_pool_size_,
            time_function_for_testing_ ? time_function_for_testing_
                                       : DefaultTimeFunction,
            priority_from_index(i));
  }
  DCHECK_NOT_NULL(worker_threads_task_runners_[0]);
}

void DefaultPlatform::SetTimeFunctionForTesting(
    DefaultPlatform::TimeFunction time_function) {
  base::MutexGuard guard(&lock_);
  time_function_for_testing_ = time_function;
  // The time function has to be right after the construction of the platform.
  DCHECK(foreground_task_runner_map_.empty());
}

bool DefaultPlatform::PumpMessageLoop(v8::Isolate* isolate,
                                      MessageLoopBehavior wait_for_work) {
  bool failed_result = wait_for_work == MessageLoopBehavior::kWaitForWork;
  std::shared_ptr<DefaultForegroundTaskRunner> task_runner;
  {
    base::MutexGuard guard(&lock_);
    auto it = foreground_task_runner_map_.find(isolate);
    if (it == foreground_task_runner_map_.end()) return failed_result;
    task_runner = it->second;
  }

  std::unique_ptr<Task> task = task_runner->PopTaskFromQueue(wait_for_work);
  if (!task) return failed_result;

  DefaultForegroundTaskRunner::RunTaskScope scope(task_runner);
  task->Run();
  return true;
}

void DefaultPlatform::RunIdleTasks(v8::Isolate* isolate,
                                   double idle_time_in_seconds) {
  DCHECK_EQ(IdleTaskSupport::kEnabled, idle_task_support_);
  std::shared_ptr<DefaultForegroundTaskRunner> task_runner;
  {
    base::MutexGuard guard(&lock_);
    if (foreground_task_runner_map_.find(isolate) ==
        foreground_task_runner_map_.end()) {
      return;
    }
    task_runner = foreground_task_runner_map_[isolate];
  }
  double deadline_in_seconds =
      MonotonicallyIncreasingTime() + idle_time_in_seconds;

  while (deadline_in_seconds > MonotonicallyIncreasingTime()) {
    std::unique_ptr<IdleTask> task = task_runner->PopTaskFromIdleQueue();
    if (!task) return;
    DefaultForegroundTaskRunner::RunTaskScope scope(task_runner);
    task->Run(deadline_in_seconds);
  }
}

std::shared_ptr<TaskRunner> DefaultPlatform::GetForegroundTaskRunner(
    v8::Isolate* isolate) {
  base::MutexGuard guard(&lock_);
  if (foreground_task_runner_map_.find(isolate) ==
      foreground_task_runner_map_.end()) {
    foreground_task_runner_map_.insert(std::make_pair(
        isolate, std::make_shared<DefaultForegroundTaskRunner>(
                     idle_task_support_, time_function_for_testing_
                                             ? time_function_for_testing_
                                             : DefaultTimeFunction)));
  }
  return foreground_task_runner_map_[isolate];
}

void DefaultPlatform::PostTaskOnWorkerThreadImpl(
    TaskPriority priority, std::unique_ptr<Task> task,
    const SourceLocation& location) {
  // If this DCHECK fires, then this means that either
  // - V8 is running without the --single-threaded flag but
  //   but the platform was created as a single-threaded platform.
  // - or some component in V8 is ignoring --single-threaded
  //   and posting a background task.
  int index = priority_to_index(priority);
  DCHECK_NOT_NULL(worker_threads_task_runners_[index]);
  worker_threads_task_runners_[index]->PostTask(std::move(task));
}

void DefaultPlatform::PostDelayedTaskOnWorkerThreadImpl(
    TaskPriority priority, std::unique_ptr<Task> task, double delay_in_seconds,
    const SourceLocation& location) {
  // If this DCHECK fires, then this means that either
  // - V8 is running without the --single-threaded flag but
  //   but the platform was created as a single-threaded platform.
  // - or some component in V8 is ignoring --single-threaded
  //   and posting a background task.
  int index = priority_to_index(priority);
  DCHECK_NOT_NULL(worker_threads_task_runners_[index]);
  worker_threads_task_runners_[index]->PostDelayedTask(std::move(task),
                                                       delay_in_seconds);
}

bool DefaultPlatform::IdleTasksEnabled(Isolate* isolate) {
  return idle_task_support_ == IdleTaskSupport::kEnabled;
}

std::unique_ptr<JobHandle> DefaultPlatform::CreateJobImpl(
    TaskPriority priority, std::unique_ptr<JobTask> job_task,
    const SourceLocation& location) {
  size_t num_worker_threads = NumberOfWorkerThreads();
  if (priority == TaskPriority::kBestEffort && num_worker_threads > 2) {
    num_worker_threads = 2;
  }
  return NewDefaultJobHandle(this, priority, std::move(job_task),
                             num_worker_threads);
}

double DefaultPlatform::MonotonicallyIncreasingTime() {
  if (time_function_for_testing_) return time_function_for_testing_();
  return DefaultTimeFunction();
}

double DefaultPlatform::CurrentClockTimeMillis() {
  return base::OS::TimeCurrentMillis();
}

TracingController* DefaultPlatform::GetTracingController() {
  return tracing_controller_.get();
}

void DefaultPlatform::SetTracingController(
    std::unique_ptr<v8::TracingController> tracing_controller) {
  DCHECK_NOT_NULL(tracing_controller.get());
  tracing_controller_ = std::move(tracing_controller);
}

int DefaultPlatform::NumberOfWorkerThreads() { return thread_pool_size_; }

Platform::StackTracePrinter DefaultPlatform::GetStackTracePrinter() {
  return PrintStackTrace;
}

v8::PageAllocator* DefaultPlatform::GetPageAllocator() {
  return page_allocator_.get();
}

v8::ThreadIsolatedAllocator* DefaultPlatform::GetThreadIsolatedAllocator() {
  if (thread_isolated_allocator_.Valid()) {
    return &thread_isolated_allocator_;
  }
  return nullptr;
}

void DefaultPlatform::NotifyIsolateShutdown(Isolate* isolate) {
  std::shared_ptr<DefaultForegroundTaskRunner> taskrunner;
  {
    base::MutexGuard guard(&lock_);
    auto it = foreground_task_runner_map_.find(isolate);
    if (it != foreground_task_runner_map_.end()) {
      taskrunner = it->second;
      foreground_task_runner_map_.erase(it);
    }
  }
  taskrunner->Terminate();
}

}  // namespace platform
}  // namespace v8
