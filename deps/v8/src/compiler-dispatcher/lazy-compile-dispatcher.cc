// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler-dispatcher/lazy-compile-dispatcher.h"

#include <atomic>

#include "include/v8-platform.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/time.h"
#include "src/codegen/compiler.h"
#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/flags/flags.h"
#include "src/heap/parked-scope.h"
#include "src/logging/counters.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/objects/instance-type.h"
#include "src/objects/objects-inl.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/scanner.h"
#include "src/tasks/cancelable-task.h"
#include "src/tasks/task-utils.h"
#include "src/zone/zone-list-inl.h"  // crbug.com/v8/8816

namespace v8 {
namespace internal {

// The maximum amount of time we should allow a single function's FinishNow to
// spend opportunistically finalizing other finalizable jobs.
static constexpr int kMaxOpportunisticFinalizeTimeMs = 1;

class LazyCompileDispatcher::JobTask : public v8::JobTask {
 public:
  explicit JobTask(LazyCompileDispatcher* lazy_compile_dispatcher)
      : lazy_compile_dispatcher_(lazy_compile_dispatcher) {}

  void Run(JobDelegate* delegate) final {
    lazy_compile_dispatcher_->DoBackgroundWork(delegate);
  }

  size_t GetMaxConcurrency(size_t worker_count) const final {
    size_t n = lazy_compile_dispatcher_->num_jobs_for_background_.load(
        std::memory_order_relaxed);
    if (v8_flags.lazy_compile_dispatcher_max_threads == 0) return n;
    return std::min(
        n, static_cast<size_t>(v8_flags.lazy_compile_dispatcher_max_threads));
  }

 private:
  LazyCompileDispatcher* lazy_compile_dispatcher_;
};

LazyCompileDispatcher::Job::Job(std::unique_ptr<BackgroundCompileTask> task)
    : task(std::move(task)), state(Job::State::kPending) {}

LazyCompileDispatcher::Job::~Job() = default;

LazyCompileDispatcher::LazyCompileDispatcher(Isolate* isolate,
                                             Platform* platform,
                                             size_t max_stack_size)
    : isolate_(isolate),
      worker_thread_runtime_call_stats_(
          isolate->counters()->worker_thread_runtime_call_stats()),
      background_compile_timer_(
          isolate->counters()->compile_function_on_background()),
      taskrunner_(platform->GetForegroundTaskRunner(
          reinterpret_cast<v8::Isolate*>(isolate))),
      platform_(platform),
      max_stack_size_(max_stack_size),
      trace_compiler_dispatcher_(v8_flags.trace_compiler_dispatcher),
      idle_task_manager_(new CancelableTaskManager()),
      idle_task_scheduled_(false),
      num_jobs_for_background_(0),
      main_thread_blocking_on_job_(nullptr),
      block_for_testing_(false),
      semaphore_for_testing_(0) {
  job_handle_ = platform_->PostJob(TaskPriority::kUserVisible,
                                   std::make_unique<JobTask>(this));
}

LazyCompileDispatcher::~LazyCompileDispatcher() {
  // AbortAll must be called before LazyCompileDispatcher is destroyed.
  CHECK(!job_handle_->IsValid());
}

namespace {

// If the SharedFunctionInfo's UncompiledData has a job slot, then write into
// it. Otherwise, allocate a new UncompiledData with a job slot, and then write
// into that. Since we have two optional slots (preparse data and job), this
// gets a little messy.
void SetUncompiledDataJobPointer(LocalIsolate* isolate,
                                 DirectHandle<SharedFunctionInfo> shared_info,
                                 Address job_address) {
  Tagged<UncompiledData> uncompiled_data =
      shared_info->uncompiled_data(isolate);
  switch (uncompiled_data->map(isolate)->instance_type()) {
    // The easy cases -- we already have a job slot, so can write into it and
    // return.
    case UNCOMPILED_DATA_WITH_PREPARSE_DATA_AND_JOB_TYPE:
      Cast<UncompiledDataWithPreparseDataAndJob>(uncompiled_data)
          ->set_job(job_address);
      break;
    case UNCOMPILED_DATA_WITHOUT_PREPARSE_DATA_WITH_JOB_TYPE:
      Cast<UncompiledDataWithoutPreparseDataWithJob>(uncompiled_data)
          ->set_job(job_address);
      break;

    // Otherwise, we'll have to allocate a new UncompiledData (with or without
    // preparse data as appropriate), set the job pointer on that, and update
    // the SharedFunctionInfo to use the new UncompiledData
    case UNCOMPILED_DATA_WITH_PREPARSE_DATA_TYPE: {
      Handle<String> inferred_name(uncompiled_data->inferred_name(), isolate);
      Handle<PreparseData> preparse_data(
          Cast<UncompiledDataWithPreparseData>(uncompiled_data)
              ->preparse_data(),
          isolate);
      DirectHandle<UncompiledDataWithPreparseDataAndJob> new_uncompiled_data =
          isolate->factory()->NewUncompiledDataWithPreparseDataAndJob(
              inferred_name, uncompiled_data->start_position(),
              uncompiled_data->end_position(), preparse_data);

      new_uncompiled_data->set_job(job_address);
      shared_info->set_uncompiled_data(*new_uncompiled_data);
      break;
    }
    case UNCOMPILED_DATA_WITHOUT_PREPARSE_DATA_TYPE: {
      DCHECK(IsUncompiledDataWithoutPreparseData(uncompiled_data));
      Handle<String> inferred_name(uncompiled_data->inferred_name(), isolate);
      DirectHandle<UncompiledDataWithoutPreparseDataWithJob>
          new_uncompiled_data =
              isolate->factory()->NewUncompiledDataWithoutPreparseDataWithJob(
                  inferred_name, uncompiled_data->start_position(),
                  uncompiled_data->end_position());

      new_uncompiled_data->set_job(job_address);
      shared_info->set_uncompiled_data(*new_uncompiled_data);
      break;
    }

    default:
      UNREACHABLE();
  }
}

}  // namespace

void LazyCompileDispatcher::Enqueue(
    LocalIsolate* isolate, Handle<SharedFunctionInfo> shared_info,
    std::unique_ptr<Utf16CharacterStream> character_stream) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
               "V8.LazyCompilerDispatcherEnqueue");
  RCS_SCOPE(isolate, RuntimeCallCounterId::kCompileEnqueueOnDispatcher);

  Job* job = new Job(std::make_unique<BackgroundCompileTask>(
      isolate_, shared_info, std::move(character_stream),
      worker_thread_runtime_call_stats_, background_compile_timer_,
      static_cast<int>(max_stack_size_)));

  SetUncompiledDataJobPointer(isolate, shared_info,
                              reinterpret_cast<Address>(job));

  // Post a background worker task to perform the compilation on the worker
  // thread.
  {
    base::MutexGuard lock(&mutex_);
    if (trace_compiler_dispatcher_) {
      PrintF("LazyCompileDispatcher: enqueued job for ");
      ShortPrint(*shared_info);
      PrintF("\n");
    }

#ifdef DEBUG
    all_jobs_.insert(job);
#endif
    pending_background_jobs_.push_back(job);
    NotifyAddedBackgroundJob(lock);
  }
  // This is not in NotifyAddedBackgroundJob to avoid being inside the mutex.
  job_handle_->NotifyConcurrencyIncrease();
}

bool LazyCompileDispatcher::IsEnqueued(
    DirectHandle<SharedFunctionInfo> shared) const {
  if (!shared->HasUncompiledData()) return false;
  Job* job = nullptr;
  Tagged<UncompiledData> data = shared->uncompiled_data(isolate_);
  if (IsUncompiledDataWithPreparseDataAndJob(data)) {
    job = reinterpret_cast<Job*>(
        Cast<UncompiledDataWithPreparseDataAndJob>(data)->job());
  } else if (IsUncompiledDataWithoutPreparseDataWithJob(data)) {
    job = reinterpret_cast<Job*>(
        Cast<UncompiledDataWithoutPreparseDataWithJob>(data)->job());
  }
  return job != nullptr;
}

void LazyCompileDispatcher::WaitForJobIfRunningOnBackground(
    Job* job, const base::MutexGuard& lock) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
               "V8.LazyCompilerDispatcherWaitForBackgroundJob");
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kCompileWaitForDispatcher);

  if (!job->is_running_on_background()) {
    if (job->state == Job::State::kPending) {
      DCHECK_EQ(std::count(pending_background_jobs_.begin(),
                           pending_background_jobs_.end(), job),
                1);

      // TODO(leszeks): Remove from pending jobs without walking the whole
      // vector.
      pending_background_jobs_.erase(
          std::remove(pending_background_jobs_.begin(),
                      pending_background_jobs_.end(), job),
          pending_background_jobs_.end());
      job->state = Job::State::kPendingToRunOnForeground;
      NotifyRemovedBackgroundJob(lock);
    } else {
      DCHECK_EQ(job->state, Job::State::kReadyToFinalize);
      DCHECK_EQ(
          std::count(finalizable_jobs_.begin(), finalizable_jobs_.end(), job),
          1);

      // TODO(leszeks): Remove from finalizable jobs without walking the whole
      // vector.
      finalizable_jobs_.erase(
          std::remove(finalizable_jobs_.begin(), finalizable_jobs_.end(), job),
          finalizable_jobs_.end());
      job->state = Job::State::kFinalizingNow;
    }
    return;
  }
  DCHECK_NULL(main_thread_blocking_on_job_);
  main_thread_blocking_on_job_ = job;
  while (main_thread_blocking_on_job_ != nullptr) {
    main_thread_blocking_signal_.Wait(&mutex_);
  }

  DCHECK_EQ(job->state, Job::State::kReadyToFinalize);
  DCHECK_EQ(std::count(finalizable_jobs_.begin(), finalizable_jobs_.end(), job),
            1);

  // TODO(leszeks): Remove from finalizable jobs without walking the whole
  // vector.
  finalizable_jobs_.erase(
      std::remove(finalizable_jobs_.begin(), finalizable_jobs_.end(), job),
      finalizable_jobs_.end());
  job->state = Job::State::kFinalizingNow;
}

bool LazyCompileDispatcher::FinishNow(
    DirectHandle<SharedFunctionInfo> function) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
               "V8.LazyCompilerDispatcherFinishNow");
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kCompileFinishNowOnDispatcher);
  if (trace_compiler_dispatcher_) {
    PrintF("LazyCompileDispatcher: finishing ");
    ShortPrint(*function);
    PrintF(" now\n");
  }

  Job* job;

  {
    base::MutexGuard lock(&mutex_);
    job = GetJobFor(function, lock);
    WaitForJobIfRunningOnBackground(job, lock);
  }

  if (job->state == Job::State::kPendingToRunOnForeground) {
    job->task->RunOnMainThread(isolate_);
    job->state = Job::State::kFinalizingNow;
  }

  if (DEBUG_BOOL) {
    base::MutexGuard lock(&mutex_);
    DCHECK_EQ(std::count(pending_background_jobs_.begin(),
                         pending_background_jobs_.end(), job),
              0);
    DCHECK_EQ(
        std::count(finalizable_jobs_.begin(), finalizable_jobs_.end(), job), 0);
    DCHECK_EQ(job->state, Job::State::kFinalizingNow);
  }

  bool success = Compiler::FinalizeBackgroundCompileTask(
      job->task.get(), isolate_, Compiler::KEEP_EXCEPTION);
  job->state = Job::State::kFinalized;

  DCHECK_NE(success, isolate_->has_exception());
  DeleteJob(job);

  // Opportunistically finalize all other jobs for a maximum time of
  // kMaxOpportunisticFinalizeTimeMs.
  double deadline_in_seconds = platform_->MonotonicallyIncreasingTime() +
                               kMaxOpportunisticFinalizeTimeMs / 1000.0;
  while (deadline_in_seconds > platform_->MonotonicallyIncreasingTime()) {
    if (!FinalizeSingleJob()) break;
  }

  return success;
}

void LazyCompileDispatcher::AbortJob(
    DirectHandle<SharedFunctionInfo> shared_info) {
  if (trace_compiler_dispatcher_) {
    PrintF("LazyCompileDispatcher: aborting job for ");
    ShortPrint(*shared_info);
    PrintF("\n");
  }
  base::LockGuard<base::Mutex> lock(&mutex_);

  Job* job = GetJobFor(shared_info, lock);
  if (job->is_running_on_background()) {
    // Job is currently running on the background thread, wait until it's done
    // and remove job then.
    job->state = Job::State::kAbortRequested;
  } else {
    if (job->state == Job::State::kPending) {
      DCHECK_EQ(std::count(pending_background_jobs_.begin(),
                           pending_background_jobs_.end(), job),
                1);

      pending_background_jobs_.erase(
          std::remove(pending_background_jobs_.begin(),
                      pending_background_jobs_.end(), job),
          pending_background_jobs_.end());
      job->state = Job::State::kAbortingNow;
      NotifyRemovedBackgroundJob(lock);
    } else if (job->state == Job::State::kReadyToFinalize) {
      DCHECK_EQ(
          std::count(finalizable_jobs_.begin(), finalizable_jobs_.end(), job),
          1);

      finalizable_jobs_.erase(
          std::remove(finalizable_jobs_.begin(), finalizable_jobs_.end(), job),
          finalizable_jobs_.end());
      job->state = Job::State::kAbortingNow;
    } else {
      UNREACHABLE();
    }
    job->task->AbortFunction();
    job->state = Job::State::kFinalized;
    DeleteJob(job, lock);
  }
}

void LazyCompileDispatcher::AbortAll() {
  idle_task_manager_->TryAbortAll();
  job_handle_->Cancel();

  {
    base::MutexGuard lock(&mutex_);
    for (Job* job : pending_background_jobs_) {
      job->task->AbortFunction();
      job->state = Job::State::kFinalized;
      DeleteJob(job, lock);
    }
    pending_background_jobs_.clear();
    for (Job* job : finalizable_jobs_) {
      job->task->AbortFunction();
      job->state = Job::State::kFinalized;
      DeleteJob(job, lock);
    }
    finalizable_jobs_.clear();
    for (Job* job : jobs_to_dispose_) {
      delete job;
    }
    jobs_to_dispose_.clear();

    DCHECK_EQ(all_jobs_.size(), 0);
    num_jobs_for_background_ = 0;
    VerifyBackgroundTaskCount(lock);
  }

  idle_task_manager_->CancelAndWait();
}

LazyCompileDispatcher::Job* LazyCompileDispatcher::GetJobFor(
    DirectHandle<SharedFunctionInfo> shared, const base::MutexGuard&) const {
  if (!shared->HasUncompiledData()) return nullptr;
  Tagged<UncompiledData> data = shared->uncompiled_data(isolate_);
  if (IsUncompiledDataWithPreparseDataAndJob(data)) {
    return reinterpret_cast<Job*>(
        Cast<UncompiledDataWithPreparseDataAndJob>(data)->job());
  } else if (IsUncompiledDataWithoutPreparseDataWithJob(data)) {
    return reinterpret_cast<Job*>(
        Cast<UncompiledDataWithoutPreparseDataWithJob>(data)->job());
  }
  return nullptr;
}

void LazyCompileDispatcher::ScheduleIdleTaskFromAnyThread(
    const base::MutexGuard&) {
  if (!taskrunner_->IdleTasksEnabled()) return;
  if (idle_task_scheduled_) return;

  idle_task_scheduled_ = true;
  // TODO(leszeks): Using a full task manager for a single cancellable task is
  // overkill, we could probably do the cancelling ourselves.
  taskrunner_->PostIdleTask(MakeCancelableIdleTask(
      idle_task_manager_.get(),
      [this](double deadline_in_seconds) { DoIdleWork(deadline_in_seconds); }));
}

void LazyCompileDispatcher::DoBackgroundWork(JobDelegate* delegate) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
               "V8.LazyCompileDispatcherDoBackgroundWork");

  LocalIsolate isolate(isolate_, ThreadKind::kBackground);
  UnparkedScope unparked_scope(&isolate);
  LocalHandleScope handle_scope(&isolate);

  ReusableUnoptimizedCompileState reusable_state(&isolate);

  while (true) {
    // Return immediately on yield, avoiding the second loop.
    if (delegate->ShouldYield()) return;

    Job* job = nullptr;
    {
      base::MutexGuard lock(&mutex_);

      if (pending_background_jobs_.empty()) break;
      job = pending_background_jobs_.back();
      pending_background_jobs_.pop_back();
      DCHECK_EQ(job->state, Job::State::kPending);

      job->state = Job::State::kRunning;
    }

    if (V8_UNLIKELY(block_for_testing_.Value())) {
      block_for_testing_.SetValue(false);
      semaphore_for_testing_.Wait();
    }

    if (trace_compiler_dispatcher_) {
      PrintF("LazyCompileDispatcher: doing background work\n");
    }

    job->task->Run(&isolate, &reusable_state);

    {
      base::MutexGuard lock(&mutex_);
      if (job->state == Job::State::kRunning) {
        job->state = Job::State::kReadyToFinalize;
        // Schedule an idle task to finalize the compilation on the main thread
        // if the job has a shared function info registered.
      } else {
        DCHECK_EQ(job->state, Job::State::kAbortRequested);
        job->state = Job::State::kAborted;
      }
      finalizable_jobs_.push_back(job);
      NotifyRemovedBackgroundJob(lock);

      if (main_thread_blocking_on_job_ == job) {
        main_thread_blocking_on_job_ = nullptr;
        main_thread_blocking_signal_.NotifyOne();
      } else {
        ScheduleIdleTaskFromAnyThread(lock);
      }
    }
  }

  while (!delegate->ShouldYield()) {
    Job* job = nullptr;
    {
      base::MutexGuard lock(&mutex_);
      if (jobs_to_dispose_.empty()) break;
      job = jobs_to_dispose_.back();
      jobs_to_dispose_.pop_back();
      if (jobs_to_dispose_.empty()) {
        num_jobs_for_background_--;
      }
    }
    delete job;
  }

  // Don't touch |this| anymore after this point, as it might have been
  // deleted.
}

LazyCompileDispatcher::Job* LazyCompileDispatcher::PopSingleFinalizeJob() {
  base::MutexGuard lock(&mutex_);

  if (finalizable_jobs_.empty()) return nullptr;

  Job* job = finalizable_jobs_.back();
  finalizable_jobs_.pop_back();
  DCHECK(job->state == Job::State::kReadyToFinalize ||
         job->state == Job::State::kAborted);
  if (job->state == Job::State::kReadyToFinalize) {
    job->state = Job::State::kFinalizingNow;
  } else {
    DCHECK_EQ(job->state, Job::State::kAborted);
    job->state = Job::State::kAbortingNow;
  }
  return job;
}

bool LazyCompileDispatcher::FinalizeSingleJob() {
  Job* job = PopSingleFinalizeJob();
  if (job == nullptr) return false;

  if (trace_compiler_dispatcher_) {
    PrintF("LazyCompileDispatcher: idle finalizing job\n");
  }

  if (job->state == Job::State::kFinalizingNow) {
    HandleScope scope(isolate_);
    Compiler::FinalizeBackgroundCompileTask(job->task.get(), isolate_,
                                            Compiler::CLEAR_EXCEPTION);
  } else {
    DCHECK_EQ(job->state, Job::State::kAbortingNow);
    job->task->AbortFunction();
  }
  job->state = Job::State::kFinalized;
  DeleteJob(job);
  return true;
}

void LazyCompileDispatcher::DoIdleWork(double deadline_in_seconds) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
               "V8.LazyCompilerDispatcherDoIdleWork");
  {
    base::MutexGuard lock(&mutex_);
    idle_task_scheduled_ = false;
  }

  if (trace_compiler_dispatcher_) {
    PrintF("LazyCompileDispatcher: received %0.1lfms of idle time\n",
           (deadline_in_seconds - platform_->MonotonicallyIncreasingTime()) *
               static_cast<double>(base::Time::kMillisecondsPerSecond));
  }
  while (deadline_in_seconds > platform_->MonotonicallyIncreasingTime()) {
    // Find a job which is pending finalization and has a shared function info
    auto there_was_a_job = FinalizeSingleJob();
    if (!there_was_a_job) return;
  }

  // We didn't return above so there still might be jobs to finalize.
  {
    base::MutexGuard lock(&mutex_);
    ScheduleIdleTaskFromAnyThread(lock);
  }
}

void LazyCompileDispatcher::DeleteJob(Job* job) {
  DCHECK(job->state == Job::State::kFinalized);
  base::MutexGuard lock(&mutex_);
  DeleteJob(job, lock);
}

void LazyCompileDispatcher::DeleteJob(Job* job, const base::MutexGuard&) {
  DCHECK(job->state == Job::State::kFinalized);
#ifdef DEBUG
  all_jobs_.erase(job);
#endif
  jobs_to_dispose_.push_back(job);
  if (jobs_to_dispose_.size() == 1) {
    num_jobs_for_background_++;
  }
}

#ifdef DEBUG
void LazyCompileDispatcher::VerifyBackgroundTaskCount(const base::MutexGuard&) {
  size_t pending_jobs = 0;
  size_t running_jobs = 0;
  size_t finalizable_jobs = 0;

  for (Job* job : all_jobs_) {
    switch (job->state) {
      case Job::State::kPending:
        pending_jobs++;
        break;
      case Job::State::kRunning:
      case Job::State::kAbortRequested:
        running_jobs++;
        break;
      case Job::State::kReadyToFinalize:
      case Job::State::kAborted:
        finalizable_jobs++;
        break;
      case Job::State::kPendingToRunOnForeground:
      case Job::State::kFinalizingNow:
      case Job::State::kAbortingNow:
      case Job::State::kFinalized:
        // Ignore.
        break;
    }
  }

  CHECK_EQ(pending_background_jobs_.size(), pending_jobs);
  CHECK_EQ(finalizable_jobs_.size(), finalizable_jobs);
  CHECK_EQ(num_jobs_for_background_.load(),
           pending_jobs + running_jobs + (jobs_to_dispose_.empty() ? 0 : 1));
}
#endif

}  // namespace internal
}  // namespace v8
