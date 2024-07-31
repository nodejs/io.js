// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_MODULE_COMPILER_H_
#define V8_WASM_MODULE_COMPILER_H_

#include <atomic>
#include <functional>
#include <memory>

#include "include/v8-metrics.h"
#include "src/base/optional.h"
#include "src/base/platform/elapsed-timer.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/time.h"
#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/logging/counters.h"
#include "src/tasks/cancelable-task.h"
#include "src/wasm/compilation-environment.h"
#include "src/wasm/wasm-features.h"
#include "src/wasm/wasm-import-wrapper-cache.h"
#include "src/wasm/wasm-module.h"

namespace v8 {

namespace base {
template <typename T>
class Vector;
}  // namespace base

namespace internal {

class JSArrayBuffer;
class JSPromise;
class Counters;
class WasmModuleObject;
class WasmInstanceObject;
class WasmTrustedInstanceData;

namespace wasm {

struct CompilationEnv;
class CompilationResultResolver;
class ErrorThrower;
class ModuleCompiler;
class NativeModule;
class ProfileInformation;
class StreamingDecoder;
class WasmCode;
struct WasmModule;

V8_EXPORT_PRIVATE
std::shared_ptr<NativeModule> CompileToNativeModule(
    Isolate* isolate, WasmFeatures enabled_features,
    CompileTimeImports compile_imports, ErrorThrower* thrower,
    std::shared_ptr<const WasmModule> module, ModuleWireBytes wire_bytes,
    int compilation_id, v8::metrics::Recorder::ContextId context_id,
    ProfileInformation* pgo_info);

V8_EXPORT_PRIVATE
void CompileJsToWasmWrappers(Isolate* isolate, const WasmModule* module);

V8_EXPORT_PRIVATE WasmError ValidateAndSetBuiltinImports(
    const WasmModule* module, base::Vector<const uint8_t> wire_bytes,
    CompileTimeImports imports);

// Compiles the wrapper for this (kind, sig) pair and sets the corresponding
// cache entry. Assumes the key already exists in the cache but has not been
// compiled yet.
V8_EXPORT_PRIVATE
WasmCode* CompileImportWrapper(
    NativeModule* native_module, Counters* counters, ImportCallKind kind,
    const FunctionSig* sig, uint32_t canonical_type_index, int expected_arity,
    Suspend suspend, WasmImportWrapperCache::ModificationScope* cache_scope);

// Triggered by the WasmCompileLazy builtin. The return value indicates whether
// compilation was successful. Lazy compilation can fail only if validation is
// also lazy.
bool CompileLazy(Isolate*, Tagged<WasmTrustedInstanceData>, int func_index);

// Throws the compilation error after failed lazy compilation.
void ThrowLazyCompilationError(Isolate* isolate,
                               const NativeModule* native_module,
                               int func_index);

// Trigger tier-up of a particular function to TurboFan. If tier-up was already
// triggered, we instead increase the priority with exponential back-off.
V8_EXPORT_PRIVATE void TriggerTierUp(Isolate*, Tagged<WasmTrustedInstanceData>,
                                     int func_index);
// Synchronous version of the above.
void TierUpNowForTesting(Isolate*, Tagged<WasmTrustedInstanceData>,
                         int func_index);
// Same, but all functions.
V8_EXPORT_PRIVATE void TierUpAllForTesting(Isolate*,
                                           Tagged<WasmTrustedInstanceData>);

template <typename Key, typename KeyInfo, typename Hash>
class WrapperQueue {
 public:
  // Removes an arbitrary key from the queue and returns it.
  // If the queue is empty, returns nullopt.
  // Thread-safe.
  base::Optional<std::pair<Key, KeyInfo>> pop() {
    base::Optional<std::pair<Key, KeyInfo>> key = base::nullopt;
    base::MutexGuard lock(&mutex_);
    auto it = queue_.begin();
    if (it != queue_.end()) {
      key = *it;
      queue_.erase(it);
    }
    return key;
  }

  // Add the given key to the queue and returns true iff the insert was
  // successful.
  // Not thread-safe.
  bool insert(const Key& key, KeyInfo key_info) {
    return queue_.insert({key, key_info}).second;
  }

  size_t size() {
    base::MutexGuard lock(&mutex_);
    return queue_.size();
  }

 private:
  base::Mutex mutex_;
  std::unordered_map<Key, KeyInfo, Hash> queue_;
};

// Encapsulates all the state and steps of an asynchronous compilation.
// An asynchronous compile job consists of a number of tasks that are executed
// as foreground and background tasks. Any phase that touches the V8 heap or
// allocates on the V8 heap (e.g. creating the module object) must be a
// foreground task. All other tasks (e.g. decoding and validating, the majority
// of the work of compilation) can be background tasks.
// TODO(wasm): factor out common parts of this with the synchronous pipeline.
class AsyncCompileJob {
 public:
  AsyncCompileJob(Isolate* isolate, WasmFeatures enabled_features,
                  CompileTimeImports compile_imports,
                  base::OwnedVector<const uint8_t> bytes,
                  Handle<Context> context,
                  Handle<NativeContext> incumbent_context,
                  const char* api_method_name,
                  std::shared_ptr<CompilationResultResolver> resolver,
                  int compilation_id);
  ~AsyncCompileJob();

  void Start();

  std::shared_ptr<StreamingDecoder> CreateStreamingDecoder();

  void Abort();
  void CancelPendingForegroundTask();

  Isolate* isolate() const { return isolate_; }

  Handle<NativeContext> context() const { return native_context_; }
  v8::metrics::Recorder::ContextId context_id() const { return context_id_; }

 private:
  class CompileTask;
  class CompileStep;
  class CompilationStateCallback;

  // States of the AsyncCompileJob.
  // Step 1 (async). Decodes the wasm module.
  // --> Fail on decoding failure,
  // --> PrepareAndStartCompile on success.
  class DecodeModule;

  // Step 2 (sync). Prepares runtime objects and starts background compilation.
  // --> finish directly on native module cache hit,
  // --> finish directly on validation error,
  // --> trigger eager compilation, if any; FinishCompile is triggered when
  // done.
  class PrepareAndStartCompile;

  // Step 3 (sync). Compilation finished. Finalize the module and resolve the
  // promise.
  class FinishCompilation;

  // Step 4 (sync). Decoding, validation or compilation failed. Reject the
  // promise.
  class Fail;

  friend class AsyncStreamingProcessor;

  enum FinishingComponent { kStreamingDecoder, kCompilation };

  // Decrements the number of outstanding finishers. The last caller of this
  // function should finish the asynchronous compilation, see the comment on
  // {outstanding_finishers_}.
  V8_WARN_UNUSED_RESULT bool DecrementAndCheckFinisherCount(
      FinishingComponent component) {
    base::MutexGuard guard(&check_finisher_mutex_);
    DCHECK_LT(0, outstanding_finishers_);
    if (outstanding_finishers_-- == 2) {
      // The first component finished, we just start a timer for a histogram.
      streaming_until_finished_timer_.Start();
      return false;
    }
    // The timer has only been started above in the case of streaming
    // compilation.
    if (streaming_until_finished_timer_.IsStarted()) {
      // We measure the time delta from when the StreamingDecoder finishes until
      // when module compilation finishes. Depending on whether streaming or
      // compilation finishes first we add the delta to the according histogram.
      int elapsed = static_cast<int>(
          streaming_until_finished_timer_.Elapsed().InMilliseconds());
      if (component == kStreamingDecoder) {
        isolate_->counters()
            ->wasm_compilation_until_streaming_finished()
            ->AddSample(elapsed);
      } else {
        isolate_->counters()
            ->wasm_streaming_until_compilation_finished()
            ->AddSample(elapsed);
      }
    }
    DCHECK_EQ(0, outstanding_finishers_);
    return true;
  }

  void CreateNativeModule(std::shared_ptr<const WasmModule> module,
                          size_t code_size_estimate);
  // Return true for cache hit, false for cache miss.
  bool GetOrCreateNativeModule(std::shared_ptr<const WasmModule> module,
                               size_t code_size_estimate);
  void PrepareRuntimeObjects();

  void FinishCompile(bool is_after_cache_hit);

  void Failed();

  void AsyncCompileSucceeded(Handle<WasmModuleObject> result);

  void FinishSuccessfully();

  void StartForegroundTask();
  void ExecuteForegroundTaskImmediately();

  void StartBackgroundTask();

  enum UseExistingForegroundTask : bool {
    kUseExistingForegroundTask = true,
    kAssertNoExistingForegroundTask = false
  };
  // Switches to the compilation step {Step} and starts a foreground task to
  // execute it. Most of the time we know that there cannot be a running
  // foreground task. If there might be one, then pass
  // kUseExistingForegroundTask to avoid spawning a second one.
  template <typename Step,
            UseExistingForegroundTask = kAssertNoExistingForegroundTask,
            typename... Args>
  void DoSync(Args&&... args);

  // Switches to the compilation step {Step} and immediately executes that step.
  template <typename Step, typename... Args>
  void DoImmediately(Args&&... args);

  // Switches to the compilation step {Step} and starts a background task to
  // execute it.
  template <typename Step, typename... Args>
  void DoAsync(Args&&... args);

  // Switches to the compilation step {Step} but does not start a task to
  // execute it.
  template <typename Step, typename... Args>
  void NextStep(Args&&... args);

  Isolate* const isolate_;
  const char* const api_method_name_;
  const WasmFeatures enabled_features_;
  const CompileTimeImports compile_imports_;
  const DynamicTiering dynamic_tiering_;
  base::TimeTicks start_time_;
  // Copy of the module wire bytes, moved into the {native_module_} on its
  // creation.
  base::OwnedVector<const uint8_t> bytes_copy_;
  // Reference to the wire bytes (held in {bytes_copy_} or as part of
  // {native_module_}).
  ModuleWireBytes wire_bytes_;
  Handle<NativeContext> native_context_;
  Handle<NativeContext> incumbent_context_;
  v8::metrics::Recorder::ContextId context_id_;
  v8::metrics::WasmModuleDecoded metrics_event_;
  const std::shared_ptr<CompilationResultResolver> resolver_;

  Handle<WasmModuleObject> module_object_;
  std::shared_ptr<NativeModule> native_module_;

  std::unique_ptr<CompileStep> step_;
  CancelableTaskManager background_task_manager_;

  std::shared_ptr<v8::TaskRunner> foreground_task_runner_;

  // For async compilation the AsyncCompileJob is the only finisher. For
  // streaming compilation also the AsyncStreamingProcessor has to finish before
  // compilation can be finished.
  int32_t outstanding_finishers_ = 1;
  base::ElapsedTimer streaming_until_finished_timer_;
  base::Mutex check_finisher_mutex_;

  // A reference to a pending foreground task, or {nullptr} if none is pending.
  CompileTask* pending_foreground_task_ = nullptr;

  // The AsyncCompileJob owns the StreamingDecoder because the StreamingDecoder
  // contains data which is needed by the AsyncCompileJob for streaming
  // compilation. The AsyncCompileJob does not actively use the
  // StreamingDecoder.
  std::shared_ptr<StreamingDecoder> stream_;

  // The compilation id to identify trace events linked to this compilation.
  const int compilation_id_;
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_MODULE_COMPILER_H_
