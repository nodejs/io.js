// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler-dispatcher/optimizing-compile-dispatcher.h"

#include "src/base/atomicops.h"
#include "src/codegen/compiler.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/execution/isolate.h"
#include "src/execution/local-isolate-inl.h"
#include "src/handles/handles-inl.h"
#include "src/heap/local-heap-inl.h"
#include "src/init/v8.h"
#include "src/logging/counters.h"
#include "src/logging/log.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/objects/js-function.h"
#include "src/tasks/cancelable-task.h"
#include "src/tracing/trace-event.h"

namespace v8 {
namespace internal {

class OptimizingCompileDispatcher::CompileTask : public v8::JobTask {
 public:
  explicit CompileTask(Isolate* isolate,
                       OptimizingCompileDispatcher* dispatcher)
      : isolate_(isolate),
        worker_thread_runtime_call_stats_(
            isolate->counters()->worker_thread_runtime_call_stats()),
        dispatcher_(dispatcher) {}

  void Run(JobDelegate* delegate) override {
    LocalIsolate local_isolate(isolate_, ThreadKind::kBackground);
    DCHECK(local_isolate.heap()->IsParked());

    {
      RCS_SCOPE(&local_isolate,
                RuntimeCallCounterId::kOptimizeBackgroundDispatcherJob);

      TimerEventScope<TimerEventRecompileConcurrent> timer(isolate_);
      while (!delegate->ShouldYield()) {
        TurbofanCompilationJob* job = dispatcher_->NextInput(&local_isolate);
        if (!job) break;
        TRACE_EVENT_WITH_FLOW0(
            TRACE_DISABLED_BY_DEFAULT("v8.compile"), "V8.OptimizeBackground",
            job->trace_id(),
            TRACE_EVENT_FLAG_FLOW_IN | TRACE_EVENT_FLAG_FLOW_OUT);

        if (dispatcher_->recompilation_delay_ != 0) {
          base::OS::Sleep(base::TimeDelta::FromMilliseconds(
              dispatcher_->recompilation_delay_));
        }

        dispatcher_->CompileNext(job, &local_isolate);
      }
    }
  }

  size_t GetMaxConcurrency(size_t worker_count) const override {
    size_t num_tasks = dispatcher_->input_queue_.Length() + worker_count;
    size_t max_threads = v8_flags.concurrent_turbofan_max_threads;
    if (max_threads > 0) {
      return std::min(max_threads, num_tasks);
    }
    return num_tasks;
  }

 private:
  Isolate* isolate_;
  WorkerThreadRuntimeCallStats* worker_thread_runtime_call_stats_;
  OptimizingCompileDispatcher* dispatcher_;
};

OptimizingCompileDispatcher::~OptimizingCompileDispatcher() {
  DCHECK_EQ(0, input_queue_.Length());
  if (job_handle_ && job_handle_->IsValid()) {
    // Wait for the job handle to complete, so that we know the queue
    // pointers are safe.
    job_handle_->Cancel();
  }
}

TurbofanCompilationJob* OptimizingCompileDispatcher::NextInput(
    LocalIsolate* local_isolate) {
  return input_queue_.Dequeue();
}

void OptimizingCompileDispatcher::CompileNext(TurbofanCompilationJob* job,
                                              LocalIsolate* local_isolate) {
  if (!job) return;

  // The function may have already been optimized by OSR.  Simply continue.
  CompilationJob::Status status =
      job->ExecuteJob(local_isolate->runtime_call_stats(), local_isolate);
  USE(status);  // Prevent an unused-variable error.

  {
    // The function may have already been optimized by OSR.  Simply continue.
    // Use a mutex to make sure that functions marked for install
    // are always also queued.
    base::MutexGuard access_output_queue_(&output_queue_mutex_);
    output_queue_.push(job);
  }

  if (finalize()) isolate_->stack_guard()->RequestInstallCode();
}

void OptimizingCompileDispatcher::FlushOutputQueue(bool restore_function_code) {
  for (;;) {
    std::unique_ptr<TurbofanCompilationJob> job;
    {
      base::MutexGuard access_output_queue_(&output_queue_mutex_);
      if (output_queue_.empty()) return;
      job.reset(output_queue_.front());
      output_queue_.pop();
    }

    Compiler::DisposeTurbofanCompilationJob(isolate_, job.get(),
                                            restore_function_code);
  }
}

void OptimizingCompileDispatcherQueue::Flush(Isolate* isolate) {
  base::MutexGuard access(&mutex_);
  while (length_ > 0) {
    std::unique_ptr<TurbofanCompilationJob> job(queue_[QueueIndex(0)]);
    DCHECK_NOT_NULL(job);
    shift_ = QueueIndex(1);
    length_--;
    Compiler::DisposeTurbofanCompilationJob(isolate, job.get(), true);
  }
}

void OptimizingCompileDispatcher::FlushInputQueue() {
  input_queue_.Flush(isolate_);
}

void OptimizingCompileDispatcher::AwaitCompileTasks() {
  {
    AllowGarbageCollection allow_before_parking;
    isolate_->main_thread_local_isolate()->ExecuteMainThreadWhileParked(
        [this]() { job_handle_->Join(); });
  }
  // Join kills the job handle, so drop it and post a new one.
  job_handle_ = V8::GetCurrentPlatform()->PostJob(
      kTaskPriority, std::make_unique<CompileTask>(isolate_, this));

#ifdef DEBUG
  CHECK_EQ(input_queue_.Length(), 0);
#endif  // DEBUG
}

void OptimizingCompileDispatcher::FlushQueues(
    BlockingBehavior blocking_behavior, bool restore_function_code) {
  FlushInputQueue();
  if (blocking_behavior == BlockingBehavior::kBlock) AwaitCompileTasks();
  FlushOutputQueue(restore_function_code);
}

void OptimizingCompileDispatcher::Flush(BlockingBehavior blocking_behavior) {
  HandleScope handle_scope(isolate_);
  FlushQueues(blocking_behavior, true);
  if (v8_flags.trace_concurrent_recompilation) {
    PrintF("  ** Flushed concurrent recompilation queues. (mode: %s)\n",
           (blocking_behavior == BlockingBehavior::kBlock) ? "blocking"
                                                           : "non blocking");
  }
}

void OptimizingCompileDispatcher::Stop() {
  HandleScope handle_scope(isolate_);
  FlushQueues(BlockingBehavior::kBlock, false);
  // At this point the optimizing compiler thread's event loop has stopped.
  // There is no need for a mutex when reading input_queue_length_.
  DCHECK_EQ(input_queue_.Length(), 0);
}

void OptimizingCompileDispatcher::InstallOptimizedFunctions() {
  HandleScope handle_scope(isolate_);

  for (;;) {
    std::unique_ptr<TurbofanCompilationJob> job;
    {
      base::MutexGuard access_output_queue_(&output_queue_mutex_);
      if (output_queue_.empty()) return;
      job.reset(output_queue_.front());
      output_queue_.pop();
    }
    OptimizedCompilationInfo* info = job->compilation_info();
    DirectHandle<JSFunction> function(*info->closure(), isolate_);

    // If another racing task has already finished compiling and installing the
    // requested code kind on the function, throw out the current job.
    if (!info->is_osr() &&
        function->HasAvailableCodeKind(isolate_, info->code_kind())) {
      if (v8_flags.trace_concurrent_recompilation) {
        PrintF("  ** Aborting compilation for ");
        ShortPrint(*function);
        PrintF(" as it has already been optimized.\n");
      }
      Compiler::DisposeTurbofanCompilationJob(isolate_, job.get(), false);
      continue;
    }

    Compiler::FinalizeTurbofanCompilationJob(job.get(), isolate_);
  }
}

bool OptimizingCompileDispatcher::HasJobs() {
  DCHECK_EQ(ThreadId::Current(), isolate_->thread_id());
  return job_handle_->IsActive() || !output_queue_.empty();
}

void OptimizingCompileDispatcher::QueueForOptimization(
    TurbofanCompilationJob* job) {
  DCHECK(input_queue_.IsAvailable());
  input_queue_.Enqueue(job);
  if (job_handle_->UpdatePriorityEnabled()) {
    job_handle_->UpdatePriority(isolate_->EfficiencyModeEnabledForTiering()
                                    ? kEfficiencyTaskPriority
                                    : kTaskPriority);
  }
  job_handle_->NotifyConcurrencyIncrease();
}

void OptimizingCompileDispatcherQueue::Prioritize(
    Tagged<SharedFunctionInfo> function) {
  base::MutexGuard access(&mutex_);
  if (length_ > 1) {
    for (int i = length_ - 1; i > 1; --i) {
      if (*queue_[QueueIndex(i)]->compilation_info()->shared_info() ==
          function) {
        std::swap(queue_[QueueIndex(i)], queue_[QueueIndex(0)]);
        return;
      }
    }
  }
}

void OptimizingCompileDispatcher::Prioritize(
    Tagged<SharedFunctionInfo> function) {
  input_queue_.Prioritize(function);
}

OptimizingCompileDispatcher::OptimizingCompileDispatcher(Isolate* isolate)
    : isolate_(isolate),
      input_queue_(v8_flags.concurrent_recompilation_queue_length),
      recompilation_delay_(v8_flags.concurrent_recompilation_delay) {
  if (v8_flags.concurrent_recompilation) {
    job_handle_ = V8::GetCurrentPlatform()->PostJob(
        kTaskPriority, std::make_unique<CompileTask>(isolate, this));
  }
}

}  // namespace internal
}  // namespace v8
