// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler-dispatcher/optimizing-compile-dispatcher.h"

#include "src/base/atomicops.h"
#include "src/base/fpu.h"
#include "src/base/logging.h"
#include "src/base/platform/mutex.h"
#include "src/base/vector.h"
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
  explicit CompileTask(OptimizingCompileDispatcher* dispatcher)
      : dispatcher_(dispatcher) {}

  void Run(JobDelegate* delegate) override {
    TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"), "V8.TurbofanTask");
    DCHECK_LT(delegate->GetTaskId(), dispatcher_->task_states_.size());
    OptimizingCompileTaskState& task_state =
        dispatcher_->task_states_[delegate->GetTaskId()];
    bool should_yield = delegate->ShouldYield();

    while (!should_yield) {
      // NextInput() sets the isolate for task_state to job->isolate() while
      // holding the lock.
      TurbofanCompilationJob* job = dispatcher_->NextInput(task_state);
      if (!job) break;

      Isolate* const isolate = job->isolate();

      {
        base::FlushDenormalsScope flush_denormals_scope(
            isolate->flush_denormals());

        // Note that LocalIsolate's lifetime is shorter than the isolate value
        // in task_state which is only cleared after this LocalIsolate instance
        // was destroyed.
        LocalIsolate local_isolate(isolate, ThreadKind::kBackground);
        DCHECK(local_isolate.heap()->IsParked());

        do {
          RunCompilationJob(isolate, local_isolate, job);

          should_yield = delegate->ShouldYield();
          if (should_yield) break;

          // Reuse the LocalIsolate if the next worklist item has the same
          // isolate.
          job = dispatcher_->NextInputIfIsolateMatches(isolate);
        } while (job);
      }

      // Reset the isolate in the task state to nullptr. Only do this after the
      // LocalIsolate was destroyed. This invariant is used by
      // WaitUntilTasksStoppedForIsolate() to ensure all tasks are stopped for
      // an isolate.
      dispatcher_->ClearTaskState(task_state);
    }

    // Here we are allowed to read the isolate without holding a lock because
    // only this thread here will ever change this field and the main thread
    // will only ever read it.
    DCHECK_NULL(task_state.isolate);
  }

  void RunCompilationJob(Isolate* isolate, LocalIsolate& local_isolate,
                         TurbofanCompilationJob* job) {
    TRACE_EVENT_WITH_FLOW0(
        TRACE_DISABLED_BY_DEFAULT("v8.compile"), "V8.OptimizeBackground",
        job->trace_id(), TRACE_EVENT_FLAG_FLOW_IN | TRACE_EVENT_FLAG_FLOW_OUT);
    TimerEventScope<TimerEventRecompileConcurrent> timer(isolate);

    if (dispatcher_->recompilation_delay_ != 0) {
      base::OS::Sleep(
          base::TimeDelta::FromMilliseconds(dispatcher_->recompilation_delay_));
    }

    RCS_SCOPE(&local_isolate,
              RuntimeCallCounterId::kOptimizeBackgroundTurbofan);

    dispatcher_->CompileNext(job, &local_isolate);
  }

  size_t GetMaxConcurrency(size_t worker_count) const override {
    size_t num_tasks = dispatcher_->input_queue_.Length() + worker_count;
    return std::min(num_tasks, dispatcher_->task_states_.size());
  }

 private:
  OptimizingCompileDispatcher* dispatcher_;
};

OptimizingCompileDispatcher::OptimizingCompileDispatcher(Isolate* isolate)
    : isolate_(isolate),
      input_queue_(v8_flags.concurrent_recompilation_queue_length),
      recompilation_delay_(v8_flags.concurrent_recompilation_delay) {
  if (v8_flags.concurrent_recompilation ||
      (v8_flags.concurrent_builtin_generation &&
       isolate->IsGeneratingEmbeddedBuiltins())) {
    int max_tasks;

    if (v8_flags.concurrent_turbofan_max_threads == 0) {
      max_tasks = V8::GetCurrentPlatform()->NumberOfWorkerThreads();
    } else {
      max_tasks = v8_flags.concurrent_turbofan_max_threads;
    }

    task_states_ =
        base::OwnedVector<OptimizingCompileTaskState>::New(max_tasks);
    job_handle_ = V8::GetCurrentPlatform()->PostJob(
        kTaskPriority, std::make_unique<CompileTask>(this));
  }
}

OptimizingCompileDispatcher::~OptimizingCompileDispatcher() {
  DCHECK_EQ(0, input_queue_.Length());
  DCHECK_EQ(output_queue_.size(), 0);

  if (job_handle_ && job_handle_->IsValid()) {
    // Wait for the job handle to complete, so that we know the queue
    // pointers are safe.
    job_handle_->Cancel();
  }
}

TurbofanCompilationJob* OptimizingCompileDispatcher::NextInput(
    OptimizingCompileTaskState& task_state) {
  return input_queue_.Dequeue(task_state);
}

TurbofanCompilationJob* OptimizingCompileDispatcher::NextInputIfIsolateMatches(
    Isolate* isolate) {
  return input_queue_.DequeueIfIsolateMatches(isolate);
}

void OptimizingCompileDispatcher::ClearTaskState(
    OptimizingCompileTaskState& task_state) {
  base::MutexGuard guard(&input_queue_.mutex_);
  DCHECK_NOT_NULL(task_state.isolate);
  task_state.isolate = nullptr;
  input_queue_.task_finished_.NotifyAll();
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
    base::SpinningMutexGuard access_output_queue_(&output_queue_mutex_);
    output_queue_.push_back(job);
  }

  if (finalize()) isolate_->stack_guard()->RequestInstallCode();
}

void OptimizingCompileDispatcher::FlushOutputQueue() {
  for (;;) {
    std::unique_ptr<TurbofanCompilationJob> job;
    {
      base::SpinningMutexGuard access_output_queue_(&output_queue_mutex_);
      if (output_queue_.empty()) return;
      job.reset(output_queue_.front());
      output_queue_.pop_front();
    }

    Compiler::DisposeTurbofanCompilationJob(isolate_, job.get());
  }
}

void OptimizingCompileDispatcherQueue::Flush(Isolate* isolate) {
  base::MutexGuard access(&mutex_);
  while (length_ > 0) {
    std::unique_ptr<TurbofanCompilationJob> job(queue_[QueueIndex(0)]);
    DCHECK_NOT_NULL(job);
    shift_ = QueueIndex(1);
    length_--;
    Compiler::DisposeTurbofanCompilationJob(isolate, job.get());
  }
}

TurbofanCompilationJob* OptimizingCompileDispatcherQueue::Dequeue(
    OptimizingCompileTaskState& task_state) {
  base::MutexGuard access(&mutex_);
  DCHECK_NULL(task_state.isolate);
  if (length_ == 0) return nullptr;
  TurbofanCompilationJob* job = queue_[QueueIndex(0)];
  DCHECK_NOT_NULL(job);
  shift_ = QueueIndex(1);
  length_--;
  task_state.isolate = job->isolate();
  return job;
}

TurbofanCompilationJob*
OptimizingCompileDispatcherQueue::DequeueIfIsolateMatches(Isolate* isolate) {
  base::MutexGuard access(&mutex_);
  if (length_ == 0) return nullptr;
  TurbofanCompilationJob* job = queue_[QueueIndex(0)];
  DCHECK_NOT_NULL(job);
  if (job->isolate() != isolate) return nullptr;
  shift_ = QueueIndex(1);
  length_--;
  return job;
}

void OptimizingCompileDispatcher::FinishTearDown() {
  // Once we have ensured that no task is working on the given isolate, we also
  // know that there are no more LocalHeaps for this isolate from CompileTask.
  // This is because CompileTask::Run() only updates the isolate once the
  // LocalIsolate/LocalHeap for it was destroyed.
  {
    base::MutexGuard guard(&input_queue_.mutex_);

    while (IsTaskRunningForIsolate(isolate_)) {
      input_queue_.task_finished_.Wait(&input_queue_.mutex_);
    }
  }

  HandleScope handle_scope(isolate_);
  FlushOutputQueue();
}

bool OptimizingCompileDispatcher::IsTaskRunningForIsolate(Isolate* isolate) {
  input_queue_.mutex_.AssertHeld();

  for (auto& task_state : task_states_) {
    if (task_state.isolate == isolate) {
      return true;
    }
  }

  return false;
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
      kTaskPriority, std::make_unique<CompileTask>(this));

#ifdef DEBUG
  CHECK_EQ(input_queue_.Length(), 0);
#endif  // DEBUG
}

void OptimizingCompileDispatcher::FlushQueues(
    BlockingBehavior blocking_behavior) {
  FlushInputQueue();
  if (blocking_behavior == BlockingBehavior::kBlock) AwaitCompileTasks();
  FlushOutputQueue();
}

void OptimizingCompileDispatcher::Flush(BlockingBehavior blocking_behavior) {
  HandleScope handle_scope(isolate_);
  FlushQueues(blocking_behavior);
  if (v8_flags.trace_concurrent_recompilation) {
    PrintF("  ** Flushed concurrent recompilation queues. (mode: %s)\n",
           (blocking_behavior == BlockingBehavior::kBlock) ? "blocking"
                                                           : "non blocking");
  }
}

void OptimizingCompileDispatcher::StartTearDown() {
  HandleScope handle_scope(isolate_);
  FlushInputQueue();
}

void OptimizingCompileDispatcher::InstallOptimizedFunctions() {
  HandleScope handle_scope(isolate_);

  for (;;) {
    std::unique_ptr<TurbofanCompilationJob> job;
    {
      base::SpinningMutexGuard access_output_queue_(&output_queue_mutex_);
      if (output_queue_.empty()) return;
      job.reset(output_queue_.front());
      output_queue_.pop_front();
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
      Compiler::DisposeTurbofanCompilationJob(isolate_, job.get());
      continue;
    }
    // Discard code compiled for a discarded native context without
    // finalization.
    if (function->native_context()->global_object()->IsDetached()) {
      Compiler::DisposeTurbofanCompilationJob(isolate_, job.get());
      continue;
    }

    Compiler::FinalizeTurbofanCompilationJob(job.get(), isolate_);
  }
}

int OptimizingCompileDispatcher::InstallGeneratedBuiltins(int installed_count) {
  // Builtin generation needs to be deterministic, meaning heap allocations must
  // happen in a deterministic order. To ensure determinism with concurrent
  // compilation, only finalize contiguous builtins in ascending order of their
  // finalization order, which is set at job creation time.

  CHECK(isolate_->IsGeneratingEmbeddedBuiltins());

  base::SpinningMutexGuard access_output_queue_(&output_queue_mutex_);

  std::sort(output_queue_.begin(), output_queue_.end(),
            [](const TurbofanCompilationJob* job1,
               const TurbofanCompilationJob* job2) {
              return job1->FinalizeOrder() < job2->FinalizeOrder();
            });

  while (!output_queue_.empty()) {
    int current = output_queue_.front()->FinalizeOrder();
    CHECK_EQ(installed_count, current);
    std::unique_ptr<TurbofanCompilationJob> job(output_queue_.front());
    output_queue_.pop_front();
    CHECK_EQ(CompilationJob::SUCCEEDED, job->FinalizeJob(isolate_));
    installed_count = current + 1;
  }
  return installed_count;
}

bool OptimizingCompileDispatcher::HasJobs() {
  DCHECK_EQ(ThreadId::Current(), isolate_->thread_id());
  return job_handle_->IsActive() || !output_queue_.empty();
}

bool OptimizingCompileDispatcher::TryQueueForOptimization(
    std::unique_ptr<TurbofanCompilationJob>& job) {
  if (input_queue_.Enqueue(job)) {
    if (job_handle_->UpdatePriorityEnabled()) {
      job_handle_->UpdatePriority(isolate_->EfficiencyModeEnabledForTiering()
                                      ? kEfficiencyTaskPriority
                                      : kTaskPriority);
    }
    job_handle_->NotifyConcurrencyIncrease();
    return true;
  } else {
    return false;
  }
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

}  // namespace internal
}  // namespace v8
