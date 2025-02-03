// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_DISPATCHER_LAZY_COMPILE_DISPATCHER_H_
#define V8_COMPILER_DISPATCHER_LAZY_COMPILE_DISPATCHER_H_

#include <cstdint>
#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

#include "src/base/atomic-utils.h"
#include "src/base/macros.h"
#include "src/base/platform/condition-variable.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/semaphore.h"
#include "src/common/globals.h"
#include "src/utils/identity-map.h"
#include "testing/gtest/include/gtest/gtest_prod.h"  // nogncheck

namespace v8 {

class JobDelegate;
class JobHandle;
class Platform;
class TaskRunner;
enum class MemoryPressureLevel;

namespace internal {

class AstRawString;
class AstValueFactory;
class BackgroundCompileTask;
class CancelableTaskManager;
class UnoptimizedCompileJob;
class UnoptimizedCompileState;
class FunctionLiteral;
class ParseInfo;
class ProducedPreparseData;
class SharedFunctionInfo;
class TimedHistogram;
class Utf16CharacterStream;
class WorkerThreadRuntimeCallStats;
class Zone;


// The LazyCompileDispatcher uses a combination of idle tasks and background
// tasks to parse and compile lazily parsed functions.
//
// As both parsing and compilation currently requires a preparation and
// finalization step that happens on the main thread, every task has to be
// advanced during idle time first. Depending on the properties of the task, it
// can then be parsed or compiled on either background threads, or during idle
// time. Last, it has to be finalized during idle time again.
//
// LazyCompileDispatcher::jobs_ maintains the list of all
// LazyCompilerDispatcherJobs the LazyCompileDispatcher knows about.
//
// LazyCompileDispatcher::pending_background_jobs_ contains the set of
// LazyCompilerDispatcherJobs that can be processed on a background thread.
//
// LazyCompileDispatcher::running_background_jobs_ contains the set of
// LazyCompilerDispatcherJobs that are currently being processed on a background
// thread.
//
// LazyCompileDispatcher::DoIdleWork tries to advance as many jobs out of jobs_
// as possible during idle time. If a job can't be advanced, but is suitable for
// background processing, it fires off background threads.
//
// LazyCompileDispatcher::DoBackgroundWork advances one of the pending jobs,
// and then spins of another idle task to potentially do the final step on the
// main thread.
class V8_EXPORT_PRIVATE LazyCompileDispatcher {
 public:
  using JobId = uintptr_t;

  LazyCompileDispatcher(Isolate* isolate, Platform* platform,
                        size_t max_stack_size);
  LazyCompileDispatcher(const LazyCompileDispatcher&) = delete;
  LazyCompileDispatcher& operator=(const LazyCompileDispatcher&) = delete;
  ~LazyCompileDispatcher();

  void Enqueue(LocalIsolate* isolate, Handle<SharedFunctionInfo> shared_info,
               std::unique_ptr<Utf16CharacterStream> character_stream);

  // Returns true if there is a pending job registered for the given function.
  bool IsEnqueued(DirectHandle<SharedFunctionInfo> function) const;

  // Blocks until the given function is compiled (and does so as fast as
  // possible). Returns true if the compile job was successful.
  bool FinishNow(DirectHandle<SharedFunctionInfo> function);

  // Aborts compilation job for the given function.
  void AbortJob(DirectHandle<SharedFunctionInfo> function);

  // Aborts all jobs, blocking until all jobs are aborted.
  void AbortAll();

 private:
  FRIEND_TEST(LazyCompileDispatcherTest, IdleTaskNoIdleTime);
  FRIEND_TEST(LazyCompileDispatcherTest, IdleTaskSmallIdleTime);
  FRIEND_TEST(LazyCompileDispatcherTest, FinishNowWithWorkerTask);
  FRIEND_TEST(LazyCompileDispatcherTest, AbortJobNotStarted);
  FRIEND_TEST(LazyCompileDispatcherTest, AbortJobAlreadyStarted);
  FRIEND_TEST(LazyCompileDispatcherTest, AsyncAbortAllPendingWorkerTask);
  FRIEND_TEST(LazyCompileDispatcherTest, AsyncAbortAllRunningWorkerTask);
  FRIEND_TEST(LazyCompileDispatcherTest, CompileMultipleOnBackgroundThread);

  // JobTask for PostJob API.
  class JobTask;

  struct Job {
    enum class State {
      // Background thread states (Enqueue + DoBackgroundWork)
      // ---

      // In the pending task queue.
      kPending,
      // Currently running on a background thread.
      kRunning,
      kAbortRequested,  // ... but we want to drop the result.
      // In the finalizable task queue.
      kReadyToFinalize,
      kAborted,

      // Main thread states (FinishNow and FinalizeSingleJob)
      // ---

      // Popped off the pending task queue.
      kPendingToRunOnForeground,
      // Popped off the finalizable task queue.
      kFinalizingNow,
      kAbortingNow,  // ... and we want to abort

      // Finished finalizing, ready for deletion.
      kFinalized,
    };

    explicit Job(std::unique_ptr<BackgroundCompileTask> task);
    ~Job();

    bool is_running_on_background() const {
      return state == State::kRunning || state == State::kAbortRequested;
    }

    std::unique_ptr<BackgroundCompileTask> task;
    State state = State::kPending;
  };

  using SharedToJobMap = IdentityMap<Job*, FreeStoreAllocationPolicy>;

  void WaitForJobIfRunningOnBackground(Job* job, const base::MutexGuard&);
  Job* GetJobFor(DirectHandle<SharedFunctionInfo> shared,
                 const base::MutexGuard&) const;
  Job* PopSingleFinalizeJob();
  void ScheduleIdleTaskFromAnyThread(const base::MutexGuard&);
  bool FinalizeSingleJob();
  void DoBackgroundWork(JobDelegate* delegate);
  void DoIdleWork(double deadline_in_seconds);

  // DeleteJob without the mutex held.
  void DeleteJob(Job* job);
  // DeleteJob with the mutex already held.
  void DeleteJob(Job* job, const base::MutexGuard&);

  void NotifyAddedBackgroundJob(const base::MutexGuard& lock) {
    ++num_jobs_for_background_;
    VerifyBackgroundTaskCount(lock);
  }
  void NotifyRemovedBackgroundJob(const base::MutexGuard& lock) {
    --num_jobs_for_background_;
    VerifyBackgroundTaskCount(lock);
  }

#ifdef DEBUG
  void VerifyBackgroundTaskCount(const base::MutexGuard&);
#else
  void VerifyBackgroundTaskCount(const base::MutexGuard&) {}
#endif

  Isolate* isolate_;
  WorkerThreadRuntimeCallStats* worker_thread_runtime_call_stats_;
  TimedHistogram* background_compile_timer_;
  std::shared_ptr<TaskRunner> taskrunner_;
  Platform* platform_;
  size_t max_stack_size_;

  std::unique_ptr<JobHandle> job_handle_;

  // Copy of v8_flags.trace_compiler_dispatcher to allow for access from any
  // thread.
  bool trace_compiler_dispatcher_;

  std::unique_ptr<CancelableTaskManager> idle_task_manager_;

  // The following members can be accessed from any thread. Methods need to hold
  // the mutex |mutex_| while accessing them.
  mutable base::Mutex mutex_;

  // True if an idle task is scheduled to be run.
  bool idle_task_scheduled_;

  // The set of jobs that can be run on a background thread.
  std::vector<Job*> pending_background_jobs_;

  // The set of jobs that can be finalized on the main thread.
  std::vector<Job*> finalizable_jobs_;

  // The total number of jobs ready to execute on background, both those pending
  // and those currently running.
  std::atomic<size_t> num_jobs_for_background_;

#ifdef DEBUG
  // The set of all allocated jobs, used for verification of the various queues
  // and counts.
  std::unordered_set<Job*> all_jobs_;
#endif

  // A queue of jobs to delete on the background thread(s). Jobs in this queue
  // are considered dead as far as the rest of the system is concerned, so they
  // won't be pointed to by any SharedFunctionInfo and won't be in the all_jobs
  // set above.
  std::vector<Job*> jobs_to_dispose_;

  // If not nullptr, then the main thread waits for the task processing
  // this job, and blocks on the ConditionVariable main_thread_blocking_signal_.
  Job* main_thread_blocking_on_job_;
  base::ConditionVariable main_thread_blocking_signal_;

  // Test support.
  base::AtomicValue<bool> block_for_testing_;
  base::Semaphore semaphore_for_testing_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_DISPATCHER_LAZY_COMPILE_DISPATCHER_H_
