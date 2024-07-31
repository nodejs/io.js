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

  inline bool IsQueueAvailable() {
    base::MutexGuard access_input_queue(&input_queue_mutex_);
    return input_queue_length_ < input_queue_capacity_;
  }

  inline int InputQueueLength() {
    base::MutexGuard access_input_queue(&input_queue_mutex_);
    return input_queue_length_;
  }

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

  inline int InputQueueIndex(int i) {
    int result = (i + input_queue_shift_) % input_queue_capacity_;
    DCHECK_LE(0, result);
    DCHECK_LT(result, input_queue_capacity_);
    return result;
  }

  Isolate* isolate_;

  // Circular queue of incoming recompilation tasks (including OSR).
  TurbofanCompilationJob** input_queue_;
  int input_queue_capacity_;
  int input_queue_length_;
  int input_queue_shift_;
  base::Mutex input_queue_mutex_;

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
