// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_DISPATCHER_OPTIMIZING_COMPILE_DISPATCHER_H_
#define V8_COMPILER_DISPATCHER_OPTIMIZING_COMPILE_DISPATCHER_H_

#include <atomic>
#include <queue>

#include "src/base/platform/condition-variable.h"
#include "src/base/platform/mutex.h"
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

  TurbofanCompilationJob* Dequeue() {
    base::MutexGuard access(&mutex_);
    if (length_ == 0) return nullptr;
    TurbofanCompilationJob* job = queue_[QueueIndex(0)];
    DCHECK_NOT_NULL(job);
    shift_ = QueueIndex(1);
    length_--;
    return job;
  }

  void Enqueue(TurbofanCompilationJob* job) {
    base::MutexGuard access(&mutex_);
    DCHECK_LT(length_, capacity_);
    queue_[QueueIndex(length_)] = job;
    length_++;
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
};

class V8_EXPORT_PRIVATE OptimizingCompileDispatcher {
 public:
  explicit OptimizingCompileDispatcher(Isolate* isolate);

  ~OptimizingCompileDispatcher();

  void Stop();
  void Flush(BlockingBehavior blocking_behavior);
  // Takes ownership of |job|.
  void QueueForOptimization(TurbofanCompilationJob* job);
  void AwaitCompileTasks();
  void InstallOptimizedFunctions();

  inline bool IsQueueAvailable() { return input_queue_.IsAvailable(); }

  static bool Enabled() { return v8_flags.concurrent_recompilation; }

  // This method must be called on the main thread.
  bool HasJobs();

  // Whether to finalize and thus install the optimized code.  Defaults to true.
  // Only set to false for testing (where finalization is then manually
  // requested using %FinalizeOptimization).
  bool finalize() const { return finalize_; }
  void set_finalize(bool finalize) {
    CHECK(!HasJobs());
    finalize_ = finalize;
  }

  void Prioritize(Tagged<SharedFunctionInfo> function);

 private:
  class CompileTask;

  enum ModeFlag { COMPILE, FLUSH };
  static constexpr TaskPriority kTaskPriority = TaskPriority::kUserVisible;
  static constexpr TaskPriority kEfficiencyTaskPriority =
      TaskPriority::kBestEffort;

  void FlushQueues(BlockingBehavior blocking_behavior,
                   bool restore_function_code);
  void FlushInputQueue();
  void FlushOutputQueue(bool restore_function_code);
  void CompileNext(TurbofanCompilationJob* job, LocalIsolate* local_isolate);
  TurbofanCompilationJob* NextInput(LocalIsolate* local_isolate);

  Isolate* isolate_;

  OptimizingCompileDispatcherQueue input_queue_;

  // Queue of recompilation tasks ready to be installed (excluding OSR).
  std::queue<TurbofanCompilationJob*> output_queue_;
  // Used for job based recompilation which has multiple producers on
  // different threads.
  base::Mutex output_queue_mutex_;

  std::unique_ptr<JobHandle> job_handle_;

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
