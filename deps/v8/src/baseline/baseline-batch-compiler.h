// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASELINE_BASELINE_BATCH_COMPILER_H_
#define V8_BASELINE_BASELINE_BATCH_COMPILER_H_

#include <atomic>

#include "src/handles/global-handles.h"
#include "src/handles/handles.h"

namespace v8 {
namespace internal {
namespace baseline {

class BaselineCompiler;
class ConcurrentBaselineCompiler;

class BaselineBatchCompiler {
 public:
  static const int kInitialQueueSize = 32;

  explicit BaselineBatchCompiler(Isolate* isolate);
  ~BaselineBatchCompiler();
  // Enqueues SharedFunctionInfo of |function| for compilation.
  void EnqueueFunction(Handle<JSFunction> function);
  void EnqueueSFI(Tagged<SharedFunctionInfo> shared);

  void set_enabled(bool enabled) { enabled_ = enabled; }
  bool is_enabled() { return enabled_; }

  void InstallBatch();

 private:
  bool concurrent() const;

  // Ensure there is enough space in the compilation queue to enqueue another
  // function, growing the queue if necessary.
  void EnsureQueueCapacity();

  // Enqueues SharedFunctionInfo.
  void Enqueue(Handle<SharedFunctionInfo> shared);

  // Returns true if the current batch exceeds the threshold and should be
  // compiled.
  bool ShouldCompileBatch(Tagged<SharedFunctionInfo> shared);

  // Compiles the current batch.
  void CompileBatch(Handle<JSFunction> function);

  // Compiles the current batch concurrently.
  void CompileBatchConcurrent(Tagged<SharedFunctionInfo> shared);

  // Resets the current batch.
  void ClearBatch();

  // Tries to compile |maybe_sfi|. Returns false if compilation was not possible
  // (e.g. bytecode was fushed, weak handle no longer valid, ...).
  bool MaybeCompileFunction(Tagged<MaybeObject> maybe_sfi);

  Isolate* isolate_;

  // Global handle to shared function infos enqueued for compilation in the
  // current batch.
  Handle<WeakFixedArray> compilation_queue_;

  // Last index set in compilation_queue_;
  int last_index_;

  // Estimated insturction size of current batch.
  int estimated_instruction_size_;

  // Flag indicating whether batch compilation is enabled.
  // Batch compilation can be dynamically disabled e.g. when creating snapshots.
  bool enabled_;

  // Handle to the background compilation jobs.
  std::unique_ptr<ConcurrentBaselineCompiler> concurrent_compiler_;
};

}  // namespace baseline
}  // namespace internal
}  // namespace v8

#endif  // V8_BASELINE_BASELINE_BATCH_COMPILER_H_
