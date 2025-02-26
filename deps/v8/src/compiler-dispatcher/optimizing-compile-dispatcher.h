// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_DISPATCHER_OPTIMIZING_COMPILE_DISPATCHER_H_
#define V8_COMPILER_DISPATCHER_OPTIMIZING_COMPILE_DISPATCHER_H_

#include <atomic>
#include <queue>

#include "src/base/platform/condition-variable.h"
#include "src/base/platform/mutex.h"
#include "src/base/vector.h"
#include "src/common/globals.h"
#include "src/flags/flags.h"
#include "src/heap/parked-scope.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

class LocalHeap;
class TurbofanCompilationJob;
class RuntimeCallStats;
class SharedFunctionInfo;

// Align the structure to the cache line size to prevent false sharing. While a
// task state is owned by a single thread, all tasks states are kept in a vector
// in OptimizingCompileDispatcher::task_states.
struct alignas(PROCESSOR_CACHE_LINE_SIZE) OptimizingCompileTaskState {
  Isolate* isolate;
};

// Circular queue of incoming recompilation tasks (including OSR).
class V8_EXPORT OptimizingCompileDispatcherQueue {
 public:
  inline bool IsAvailable() {
    base::MutexGuard access(&mutex_);
    return length_ < capacity_;
  }

  inline int Length() {
    base::MutexGuard access_queue(&mutex_);
    return length_;
  }

  explicit OptimizingCompileDispatcherQueue(int capacity)
      : capacity_(capacity), length_(0), shift_(0) {
    queue_ = NewArray<TurbofanCompilationJob*>(capacity_);
  }

  ~OptimizingCompileDispatcherQueue() { DeleteArray(queue_); }

  TurbofanCompilationJob* Dequeue(OptimizingCompileTaskState& task_state);
  TurbofanCompilationJob* DequeueIfIsolateMatches(Isolate* isolate);

  bool Enqueue(std::unique_ptr<TurbofanCompilationJob>& job) {
    base::MutexGuard access(&mutex_);
    if (length_ < capacity_) {
      queue_[QueueIndex(length_)] = job.release();
      length_++;
      return true;
    } else {
      return false;
    }
  }

  void Flush(Isolate* isolate);

  void Prioritize(Tagged<SharedFunctionInfo> function);

 private:
  inline int QueueIndex(int i) {
    int result = (i + shift_) % capacity_;
    DCHECK_LE(0, result);
    DCHECK_LT(result, capacity_);
    return result;
  }

  TurbofanCompilationJob** queue_;
  int capacity_;
  int length_;
  int shift_;
  base::Mutex mutex_;
  base::ConditionVariable task_finished_;

  friend class OptimizingCompileDispatcher;
};

class V8_EXPORT_PRIVATE OptimizingCompileDispatcher {
 public:
  explicit OptimizingCompileDispatcher(Isolate* isolate);

  ~OptimizingCompileDispatcher();

  void Flush(BlockingBehavior blocking_behavior);
  // Takes ownership of |job|.
  bool TryQueueForOptimization(std::unique_ptr<TurbofanCompilationJob>& job);
  void AwaitCompileTasks();
  void InstallOptimizedFunctions();

  // Install generated builtins in the output queue in contiguous finalization
  // order, starting with installed_count. Returns the finalization order of the
  // last job that was finalized.
  int InstallGeneratedBuiltins(int installed_count);

  inline bool IsQueueAvailable() { return input_queue_.IsAvailable(); }

  static bool Enabled() { return v8_flags.concurrent_recompilation; }

  // This method must be called on the main thread.
  bool HasJobs();

  // Whether to finalize and thus install the optimized code.  Defaults to true.
  // Only set to false for testing (where finalization is then manually
  // requested using %FinalizeOptimization) and when compiling embedded builtins
  // concurrently. For the latter, builtins are installed manually using
  // InstallGeneratedBuiltins().
  bool finalize() const { return finalize_; }
  void set_finalize(bool finalize) {
    CHECK(!HasJobs());
    finalize_ = finalize;
  }

  void Prioritize(Tagged<SharedFunctionInfo> function);

  void StartTearDown();
  void FinishTearDown();

 private:
  class CompileTask;

  enum ModeFlag { COMPILE, FLUSH };
  static constexpr TaskPriority kTaskPriority = TaskPriority::kUserVisible;
  static constexpr TaskPriority kEfficiencyTaskPriority =
      TaskPriority::kBestEffort;

  void FlushQueues(BlockingBehavior blocking_behavior);
  void FlushInputQueue();
  void FlushOutputQueue();
  void CompileNext(TurbofanCompilationJob* job, LocalIsolate* local_isolate);
  TurbofanCompilationJob* NextInput(OptimizingCompileTaskState& task_state);
  TurbofanCompilationJob* NextInputIfIsolateMatches(Isolate* isolate);
  void ClearTaskState(OptimizingCompileTaskState& task_state);
  bool IsTaskRunningForIsolate(Isolate* isolate);

  Isolate* isolate_;

  OptimizingCompileDispatcherQueue input_queue_;

  // Queue of recompilation tasks ready to be installed (excluding OSR).
  std::deque<TurbofanCompilationJob*> output_queue_;
  // Used for job based recompilation which has multiple producers on
  // different threads.
  base::SpinningMutex output_queue_mutex_;

  std::unique_ptr<JobHandle> job_handle_;

  base::OwnedVector<OptimizingCompileTaskState> task_states_;

  // Copy of v8_flags.concurrent_recompilation_delay that will be used from the
  // background thread.
  //
  // Since flags might get modified while the background thread is running, it
  // is not safe to access them directly.
  int recompilation_delay_;

  bool finalize_ = true;
};
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_DISPATCHER_OPTIMIZING_COMPILE_DISPATCHER_H_
