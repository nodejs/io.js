// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/baseline/baseline-batch-compiler.h"

#include <algorithm>

#include "src/base/fpu.h"
#include "src/baseline/baseline-compiler.h"
#include "src/codegen/compiler.h"
#include "src/execution/isolate.h"
#include "src/handles/global-handles-inl.h"
#include "src/heap/factory-inl.h"
#include "src/heap/heap-inl.h"
#include "src/heap/local-heap-inl.h"
#include "src/heap/parked-scope.h"
#include "src/objects/fixed-array-inl.h"
#include "src/objects/js-function-inl.h"
#include "src/utils/locked-queue-inl.h"

namespace v8 {
namespace internal {
namespace baseline {

static bool CanCompileWithConcurrentBaseline(Tagged<SharedFunctionInfo> shared,
                                             Isolate* isolate) {
  return !shared->HasBaselineCode() && CanCompileWithBaseline(isolate, shared);
}

class BaselineCompilerTask {
 public:
  BaselineCompilerTask(Isolate* isolate, PersistentHandles* handles,
                       Tagged<SharedFunctionInfo> sfi)
      : shared_function_info_(handles->NewHandle(sfi)),
        bytecode_(handles->NewHandle(sfi->GetBytecodeArray(isolate))) {
    DCHECK(sfi->is_compiled());
    shared_function_info_->set_is_sparkplug_compiling(true);
  }

  BaselineCompilerTask(const BaselineCompilerTask&) V8_NOEXCEPT = delete;
  BaselineCompilerTask(BaselineCompilerTask&&) V8_NOEXCEPT = default;

  // Executed in the background thread.
  void Compile(LocalIsolate* local_isolate) {
    RCS_SCOPE(local_isolate, RuntimeCallCounterId::kCompileBackgroundBaseline);
    base::ScopedTimer timer(v8_flags.log_function_events ? &time_taken_
                                                         : nullptr);
    BaselineCompiler compiler(local_isolate, shared_function_info_, bytecode_);
    compiler.GenerateCode();
    maybe_code_ =
        local_isolate->heap()->NewPersistentMaybeHandle(compiler.Build());
  }

  // Executed in the main thread.
  void Install(Isolate* isolate) {
    shared_function_info_->set_is_sparkplug_compiling(false);
    Handle<Code> code;
    if (!maybe_code_.ToHandle(&code)) return;
    if (v8_flags.print_code) {
      Print(*code);
    }
    // Don't install the code if the bytecode has been flushed or has
    // already some baseline code installed.
    if (!CanCompileWithConcurrentBaseline(*shared_function_info_, isolate)) {
      return;
    }

    shared_function_info_->set_baseline_code(*code, kReleaseStore);
    shared_function_info_->set_age(0);
    if (v8_flags.trace_baseline) {
      CodeTracer::Scope scope(isolate->GetCodeTracer());
      std::stringstream ss;
      ss << "[Concurrent Sparkplug Off Thread] Function ";
      ShortPrint(*shared_function_info_, ss);
      ss << " installed\n";
      OFStream os(scope.file());
      os << ss.str();
    }
    if (IsScript(shared_function_info_->script())) {
      Compiler::LogFunctionCompilation(
          isolate, LogEventListener::CodeTag::kFunction,
          direct_handle(Cast<Script>(shared_function_info_->script()), isolate),
          shared_function_info_, DirectHandle<FeedbackVector>(),
          Cast<AbstractCode>(code), CodeKind::BASELINE,
          time_taken_.InMillisecondsF());
    }
  }

 private:
  IndirectHandle<SharedFunctionInfo> shared_function_info_;
  IndirectHandle<BytecodeArray> bytecode_;
  MaybeIndirectHandle<Code> maybe_code_;
  base::TimeDelta time_taken_;
};

class BaselineBatchCompilerJob {
 public:
  BaselineBatchCompilerJob(Isolate* isolate,
                           DirectHandle<WeakFixedArray> task_queue,
                           int batch_size) {
    handles_ = isolate->NewPersistentHandles();
    tasks_.reserve(batch_size);
    for (int i = 0; i < batch_size; i++) {
      Tagged<MaybeObject> maybe_sfi = task_queue->get(i);
      // TODO(victorgomes): Do I need to clear the value?
      task_queue->set(i, ClearedValue(isolate));
      Tagged<HeapObject> obj;
      // Skip functions where weak reference is no longer valid.
      if (!maybe_sfi.GetHeapObjectIfWeak(&obj)) continue;
      // Skip functions where the bytecode has been flushed.
      Tagged<SharedFunctionInfo> shared = Cast<SharedFunctionInfo>(obj);
      if (!CanCompileWithConcurrentBaseline(shared, isolate)) continue;
      // Skip functions that are already being compiled.
      if (shared->is_sparkplug_compiling()) continue;
      tasks_.emplace_back(isolate, handles_.get(), shared);
    }
    if (v8_flags.trace_baseline) {
      CodeTracer::Scope scope(isolate->GetCodeTracer());
      PrintF(scope.file(), "[Concurrent Sparkplug] compiling %zu functions\n",
             tasks_.size());
    }
  }

  // Executed in the background thread.
  void Compile(LocalIsolate* local_isolate) {
    local_isolate->heap()->AttachPersistentHandles(std::move(handles_));
    for (auto& task : tasks_) {
      task.Compile(local_isolate);
    }
    // Get the handle back since we'd need them to install the code later.
    handles_ = local_isolate->heap()->DetachPersistentHandles();
  }

  // Executed in the main thread.
  void Install(Isolate* isolate) {
    HandleScope local_scope(isolate);
    for (auto& task : tasks_) {
      task.Install(isolate);
    }
  }

 private:
  std::vector<BaselineCompilerTask> tasks_;
  std::unique_ptr<PersistentHandles> handles_;
};

class ConcurrentBaselineCompiler {
 public:
  class JobDispatcher : public v8::JobTask {
   public:
    JobDispatcher(
        Isolate* isolate,
        LockedQueue<std::unique_ptr<BaselineBatchCompilerJob>>* incoming_queue,
        LockedQueue<std::unique_ptr<BaselineBatchCompilerJob>>* outcoming_queue)
        : isolate_(isolate),
          incoming_queue_(incoming_queue),
          outgoing_queue_(outcoming_queue) {}

    void Run(JobDelegate* delegate) override {
      base::FlushDenormalsScope flush_denormals_scope(
          isolate_->flush_denormals());
      LocalIsolate local_isolate(isolate_, ThreadKind::kBackground);
      UnparkedScope unparked_scope(&local_isolate);
      LocalHandleScope handle_scope(&local_isolate);

      while (!incoming_queue_->IsEmpty() && !delegate->ShouldYield()) {
        std::unique_ptr<BaselineBatchCompilerJob> job;
        if (!incoming_queue_->Dequeue(&job)) break;
        DCHECK_NOT_NULL(job);
        job->Compile(&local_isolate);
        outgoing_queue_->Enqueue(std::move(job));
      }
      isolate_->stack_guard()->RequestInstallBaselineCode();
    }

    size_t GetMaxConcurrency(size_t worker_count) const override {
      size_t max_threads = v8_flags.concurrent_sparkplug_max_threads;
      size_t num_tasks = incoming_queue_->size() + worker_count;
      if (max_threads > 0) {
        return std::min(max_threads, num_tasks);
      }
      return num_tasks;
    }

   private:
    Isolate* isolate_;
    LockedQueue<std::unique_ptr<BaselineBatchCompilerJob>>* incoming_queue_;
    LockedQueue<std::unique_ptr<BaselineBatchCompilerJob>>* outgoing_queue_;
  };

  explicit ConcurrentBaselineCompiler(Isolate* isolate) : isolate_(isolate) {
    if (v8_flags.concurrent_sparkplug) {
      TaskPriority priority =
          v8_flags.concurrent_sparkplug_high_priority_threads
              ? TaskPriority::kUserBlocking
              : TaskPriority::kUserVisible;
      job_handle_ = V8::GetCurrentPlatform()->PostJob(
          priority, std::make_unique<JobDispatcher>(isolate_, &incoming_queue_,
                                                    &outgoing_queue_));
    }
  }

  ~ConcurrentBaselineCompiler() {
    if (job_handle_ && job_handle_->IsValid()) {
      // Wait for the job handle to complete, so that we know the queue
      // pointers are safe.
      job_handle_->Cancel();
    }
  }

  void CompileBatch(Handle<WeakFixedArray> task_queue, int batch_size) {
    DCHECK(v8_flags.concurrent_sparkplug);
    RCS_SCOPE(isolate_, RuntimeCallCounterId::kCompileBaseline);
    incoming_queue_.Enqueue(std::make_unique<BaselineBatchCompilerJob>(
        isolate_, task_queue, batch_size));
    job_handle_->NotifyConcurrencyIncrease();
  }

  void InstallBatch() {
    while (!outgoing_queue_.IsEmpty()) {
      std::unique_ptr<BaselineBatchCompilerJob> job;
      outgoing_queue_.Dequeue(&job);
      job->Install(isolate_);
    }
  }

 private:
  Isolate* isolate_;
  std::unique_ptr<JobHandle> job_handle_ = nullptr;
  LockedQueue<std::unique_ptr<BaselineBatchCompilerJob>> incoming_queue_;
  LockedQueue<std::unique_ptr<BaselineBatchCompilerJob>> outgoing_queue_;
};

BaselineBatchCompiler::BaselineBatchCompiler(Isolate* isolate)
    : isolate_(isolate),
      compilation_queue_(Handle<WeakFixedArray>::null()),
      last_index_(0),
      estimated_instruction_size_(0),
      enabled_(true) {
  if (v8_flags.concurrent_sparkplug) {
    concurrent_compiler_ =
        std::make_unique<ConcurrentBaselineCompiler>(isolate_);
  }
}

BaselineBatchCompiler::~BaselineBatchCompiler() {
  if (!compilation_queue_.is_null()) {
    GlobalHandles::Destroy(compilation_queue_.location());
    compilation_queue_ = Handle<WeakFixedArray>::null();
  }
}

bool BaselineBatchCompiler::concurrent() const {
  return v8_flags.concurrent_sparkplug &&
         !isolate_->EfficiencyModeEnabledForTiering();
}

void BaselineBatchCompiler::EnqueueFunction(DirectHandle<JSFunction> function) {
  DirectHandle<SharedFunctionInfo> shared(function->shared(), isolate_);
  // Immediately compile the function if batch compilation is disabled.
  if (!is_enabled()) {
    IsCompiledScope is_compiled_scope(
        function->shared()->is_compiled_scope(isolate_));
    Compiler::CompileBaseline(isolate_, function, Compiler::CLEAR_EXCEPTION,
                              &is_compiled_scope);
    return;
  }
  if (ShouldCompileBatch(*shared)) {
    if (concurrent()) {
      CompileBatchConcurrent(*shared);
    } else {
      CompileBatch(function);
    }
  } else {
    Enqueue(shared);
  }
}

void BaselineBatchCompiler::EnqueueSFI(Tagged<SharedFunctionInfo> shared) {
  if (!v8_flags.concurrent_sparkplug || !is_enabled()) return;
  if (ShouldCompileBatch(shared)) {
    CompileBatchConcurrent(shared);
  } else {
    Enqueue(DirectHandle<SharedFunctionInfo>(shared, isolate_));
  }
}

void BaselineBatchCompiler::Enqueue(DirectHandle<SharedFunctionInfo> shared) {
  EnsureQueueCapacity();
  compilation_queue_->set(last_index_++, MakeWeak(*shared));
}

void BaselineBatchCompiler::InstallBatch() {
  DCHECK(v8_flags.concurrent_sparkplug);
  concurrent_compiler_->InstallBatch();
}

void BaselineBatchCompiler::EnsureQueueCapacity() {
  if (compilation_queue_.is_null()) {
    compilation_queue_ = isolate_->global_handles()->Create(
        *isolate_->factory()->NewWeakFixedArray(kInitialQueueSize,
                                                AllocationType::kOld));
    return;
  }
  if (last_index_ >= compilation_queue_->length()) {
    DirectHandle<WeakFixedArray> new_queue =
        isolate_->factory()->CopyWeakFixedArrayAndGrow(compilation_queue_,
                                                       last_index_);
    GlobalHandles::Destroy(compilation_queue_.location());
    compilation_queue_ = isolate_->global_handles()->Create(*new_queue);
  }
}

void BaselineBatchCompiler::CompileBatch(DirectHandle<JSFunction> function) {
  {
    IsCompiledScope is_compiled_scope(
        function->shared()->is_compiled_scope(isolate_));
    Compiler::CompileBaseline(isolate_, function, Compiler::CLEAR_EXCEPTION,
                              &is_compiled_scope);
  }
  for (int i = 0; i < last_index_; i++) {
    Tagged<MaybeObject> maybe_sfi = compilation_queue_->get(i);
    MaybeCompileFunction(maybe_sfi);
    compilation_queue_->set(i, ClearedValue(isolate_));
  }
  ClearBatch();
}

void BaselineBatchCompiler::CompileBatchConcurrent(
    Tagged<SharedFunctionInfo> shared) {
  Enqueue(DirectHandle<SharedFunctionInfo>(shared, isolate_));
  concurrent_compiler_->CompileBatch(compilation_queue_, last_index_);
  ClearBatch();
}

bool BaselineBatchCompiler::ShouldCompileBatch(
    Tagged<SharedFunctionInfo> shared) {
  // Early return if the function is compiled with baseline already or it is not
  // suitable for baseline compilation.
  if (shared->HasBaselineCode()) return false;
  // If we're already compiling this function, return.
  if (shared->is_sparkplug_compiling()) return false;
  if (!CanCompileWithBaseline(isolate_, shared)) return false;

  int estimated_size;
  {
    DisallowHeapAllocation no_gc;
    estimated_size = BaselineCompiler::EstimateInstructionSize(
        shared->GetBytecodeArray(isolate_));
  }
  estimated_instruction_size_ += estimated_size;
  if (v8_flags.trace_baseline_batch_compilation) {
    CodeTracer::Scope trace_scope(isolate_->GetCodeTracer());
    PrintF(trace_scope.file(), "[Baseline batch compilation] Enqueued SFI %s",
           shared->DebugNameCStr().get());
    PrintF(trace_scope.file(),
           " with estimated size %d (current budget: %d/%d)\n", estimated_size,
           estimated_instruction_size_,
           v8_flags.baseline_batch_compilation_threshold.value());
  }
  if (estimated_instruction_size_ >=
      v8_flags.baseline_batch_compilation_threshold) {
    if (v8_flags.trace_baseline_batch_compilation) {
      CodeTracer::Scope trace_scope(isolate_->GetCodeTracer());
      PrintF(trace_scope.file(),
             "[Baseline batch compilation] Compiling current batch of %d "
             "functions\n",
             (last_index_ + 1));
    }
    return true;
  }
  return false;
}

bool BaselineBatchCompiler::MaybeCompileFunction(
    Tagged<MaybeObject> maybe_sfi) {
  Tagged<HeapObject> heapobj;
  // Skip functions where the weak reference is no longer valid.
  if (!maybe_sfi.GetHeapObjectIfWeak(&heapobj)) return false;
  Handle<SharedFunctionInfo> shared =
      handle(Cast<SharedFunctionInfo>(heapobj), isolate_);
  // Skip functions where the bytecode has been flushed.
  if (!shared->is_compiled()) return false;

  IsCompiledScope is_compiled_scope(shared->is_compiled_scope(isolate_));
  return Compiler::CompileSharedWithBaseline(
      isolate_, shared, Compiler::CLEAR_EXCEPTION, &is_compiled_scope);
}

void BaselineBatchCompiler::ClearBatch() {
  estimated_instruction_size_ = 0;
  last_index_ = 0;
}

}  // namespace baseline
}  // namespace internal
}  // namespace v8
