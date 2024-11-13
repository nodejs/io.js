// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_WASM_ENGINE_H_
#define V8_WASM_WASM_ENGINE_H_

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "src/base/platform/condition-variable.h"
#include "src/base/platform/mutex.h"
#include "src/compiler/wasm-call-descriptors.h"
#include "src/tasks/cancelable-task.h"
#include "src/tasks/operations-barrier.h"
#include "src/wasm/canonical-types.h"
#include "src/wasm/stacks.h"
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-tier.h"
#include "src/zone/accounting-allocator.h"

namespace v8 {
namespace internal {

class AsmWasmData;
class CodeTracer;
class CompilationStatistics;
class HeapNumber;
class WasmInstanceObject;
class WasmModuleObject;
class JSArrayBuffer;

namespace wasm {

#ifdef V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING
namespace gdb_server {
class GdbServer;
}  // namespace gdb_server
#endif  // V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING

class AsyncCompileJob;
class ErrorThrower;
struct ModuleWireBytes;
class StreamingDecoder;
class WasmEnabledFeatures;
class WasmOrphanedGlobalHandle;

class V8_EXPORT_PRIVATE CompilationResultResolver {
 public:
  virtual void OnCompilationSucceeded(Handle<WasmModuleObject> result) = 0;
  virtual void OnCompilationFailed(Handle<Object> error_reason) = 0;
  virtual ~CompilationResultResolver() = default;
};

class V8_EXPORT_PRIVATE InstantiationResultResolver {
 public:
  virtual void OnInstantiationSucceeded(Handle<WasmInstanceObject> result) = 0;
  virtual void OnInstantiationFailed(Handle<Object> error_reason) = 0;
  virtual ~InstantiationResultResolver() = default;
};

// Native modules cached by their wire bytes and compile-time imports.
class NativeModuleCache {
 public:
  struct Key {
    Key(size_t prefix_hash, CompileTimeImports compile_imports,
        const base::Vector<const uint8_t>& bytes)
        : prefix_hash(prefix_hash),
          compile_imports(std::move(compile_imports)),
          bytes(bytes) {}

    // Store the prefix hash as part of the key for faster lookup, and to
    // quickly check existing prefixes for streaming compilation.
    size_t prefix_hash;
    CompileTimeImports compile_imports;
    base::Vector<const uint8_t> bytes;

    bool operator==(const Key& other) const {
      bool eq = bytes == other.bytes &&
                compile_imports.compare(other.compile_imports) == 0;
      DCHECK_IMPLIES(eq, prefix_hash == other.prefix_hash);
      return eq;
    }

    bool operator<(const Key& other) const {
      if (prefix_hash != other.prefix_hash) {
        DCHECK_IMPLIES(!bytes.empty() && !other.bytes.empty(),
                       bytes != other.bytes);
        return prefix_hash < other.prefix_hash;
      }
      if (bytes.size() != other.bytes.size()) {
        return bytes.size() < other.bytes.size();
      }
      if (int cmp = compile_imports.compare(other.compile_imports)) {
        return cmp < 0;
      }
      // Fast path when the base pointers are the same.
      // Also handles the {nullptr} case which would be UB for memcmp.
      if (bytes.begin() == other.bytes.begin()) {
        DCHECK_EQ(prefix_hash, other.prefix_hash);
        return false;
      }
      DCHECK_NOT_NULL(bytes.begin());
      DCHECK_NOT_NULL(other.bytes.begin());
      return memcmp(bytes.begin(), other.bytes.begin(), bytes.size()) < 0;
    }
  };

  std::shared_ptr<NativeModule> MaybeGetNativeModule(
      ModuleOrigin origin, base::Vector<const uint8_t> wire_bytes,
      const CompileTimeImports& compile_imports);
  bool GetStreamingCompilationOwnership(
      size_t prefix_hash, const CompileTimeImports& compile_imports);
  void StreamingCompilationFailed(size_t prefix_hash,
                                  const CompileTimeImports& compile_imports);
  std::shared_ptr<NativeModule> Update(
      std::shared_ptr<NativeModule> native_module, bool error);
  void Erase(NativeModule* native_module);

  bool empty() { return map_.empty(); }

  // Hash the wire bytes up to the code section header. Used as a heuristic to
  // avoid streaming compilation of modules that are likely already in the
  // cache. See {GetStreamingCompilationOwnership}. Assumes that the bytes have
  // already been validated.
  static size_t PrefixHash(base::Vector<const uint8_t> wire_bytes);

 private:
  // Each key points to the corresponding native module's wire bytes, so they
  // should always be valid as long as the native module is alive.  When
  // the native module dies, {FreeNativeModule} deletes the entry from the
  // map, so that we do not leave any dangling key pointing to an expired
  // weak_ptr. This also serves as a way to regularly clean up the map, which
  // would otherwise accumulate expired entries.
  // A {nullopt} value is inserted to indicate that this native module is
  // currently being created in some thread, and that other threads should wait
  // before trying to get it from the cache.
  // By contrast, an expired {weak_ptr} indicates that the native module died
  // and will soon be cleaned up from the cache.
  std::map<Key, std::optional<std::weak_ptr<NativeModule>>> map_;

  base::Mutex mutex_;

  // This condition variable is used to synchronize threads compiling the same
  // module. Only one thread will create the {NativeModule}. Other threads
  // will wait on this variable until the first thread wakes them up.
  base::ConditionVariable cache_cv_;
};

// The central data structure that represents an engine instance capable of
// loading, instantiating, and executing Wasm code.
class V8_EXPORT_PRIVATE WasmEngine {
  class LogCodesTask;

 public:
  WasmEngine();
  WasmEngine(const WasmEngine&) = delete;
  WasmEngine& operator=(const WasmEngine&) = delete;
  ~WasmEngine();

  // Synchronously validates the given bytes. Returns whether the bytes
  // represent a valid encoded Wasm module.
  bool SyncValidate(Isolate* isolate, WasmEnabledFeatures enabled,
                    CompileTimeImports compile_imports, ModuleWireBytes bytes);

  // Synchronously compiles the given bytes that represent a translated
  // asm.js module.
  MaybeHandle<AsmWasmData> SyncCompileTranslatedAsmJs(
      Isolate* isolate, ErrorThrower* thrower, ModuleWireBytes bytes,
      DirectHandle<Script> script,
      base::Vector<const uint8_t> asm_js_offset_table_bytes,
      DirectHandle<HeapNumber> uses_bitset, LanguageMode language_mode);
  Handle<WasmModuleObject> FinalizeTranslatedAsmJs(
      Isolate* isolate, DirectHandle<AsmWasmData> asm_wasm_data,
      DirectHandle<Script> script);

  // Synchronously compiles the given bytes that represent an encoded Wasm
  // module.
  MaybeHandle<WasmModuleObject> SyncCompile(Isolate* isolate,
                                            WasmEnabledFeatures enabled,
                                            CompileTimeImports compile_imports,
                                            ErrorThrower* thrower,
                                            ModuleWireBytes bytes);

  // Synchronously instantiate the given Wasm module with the given imports.
  // If the module represents an asm.js module, then the supplied {memory}
  // should be used as the memory of the instance.
  MaybeHandle<WasmInstanceObject> SyncInstantiate(
      Isolate* isolate, ErrorThrower* thrower,
      Handle<WasmModuleObject> module_object, MaybeHandle<JSReceiver> imports,
      MaybeHandle<JSArrayBuffer> memory);

  // Begin an asynchronous compilation of the given bytes that represent an
  // encoded Wasm module.
  // The {is_shared} flag indicates if the bytes backing the module could
  // be shared across threads, i.e. could be concurrently modified.
  void AsyncCompile(Isolate* isolate, WasmEnabledFeatures enabled,
                    CompileTimeImports compile_imports,
                    std::shared_ptr<CompilationResultResolver> resolver,
                    ModuleWireBytes bytes, bool is_shared,
                    const char* api_method_name_for_errors);

  // Begin an asynchronous instantiation of the given Wasm module.
  void AsyncInstantiate(Isolate* isolate,
                        std::unique_ptr<InstantiationResultResolver> resolver,
                        Handle<WasmModuleObject> module_object,
                        MaybeHandle<JSReceiver> imports);

  std::shared_ptr<StreamingDecoder> StartStreamingCompilation(
      Isolate* isolate, WasmEnabledFeatures enabled,
      CompileTimeImports compile_imports, Handle<Context> context,
      const char* api_method_name,
      std::shared_ptr<CompilationResultResolver> resolver);

  // Compiles the function with the given index at a specific compilation tier.
  // Errors are stored internally in the CompilationState.
  // This is mostly used for testing to force a function into a specific tier.
  void CompileFunction(Counters* counters, NativeModule* native_module,
                       uint32_t function_index, ExecutionTier tier);

  void EnterDebuggingForIsolate(Isolate* isolate);

  void LeaveDebuggingForIsolate(Isolate* isolate);

  // Imports the shared part of a module from a different Context/Isolate using
  // the the same engine, recreating a full module object in the given Isolate.
  Handle<WasmModuleObject> ImportNativeModule(
      Isolate* isolate, std::shared_ptr<NativeModule> shared_module,
      base::Vector<const char> source_url);

  // Flushes all Liftoff code and returns the sizes of the removed
  // (executable) code and the removed metadata.
  std::pair<size_t, size_t> FlushLiftoffCode();

  // Returns the code size of all Liftoff compiled functions in all modules.
  size_t GetLiftoffCodeSizeForTesting();

  AccountingAllocator* allocator() { return &allocator_; }

  // Compilation statistics for TurboFan compilations. Returns a shared_ptr
  // so that background compilation jobs can hold on to it while the main thread
  // shuts down.
  std::shared_ptr<CompilationStatistics> GetOrCreateTurboStatistics();

  // Prints the gathered compilation statistics, then resets them.
  void DumpAndResetTurboStatistics();
  // Same, but no reset.
  void DumpTurboStatistics();

  // Used to redirect tracing output from {stdout} to a file.
  CodeTracer* GetCodeTracer();

  // Remove {job} from the list of active compile jobs.
  std::unique_ptr<AsyncCompileJob> RemoveCompileJob(AsyncCompileJob* job);

  // Returns true if at least one AsyncCompileJob that belongs to the given
  // Isolate is currently running.
  bool HasRunningCompileJob(Isolate* isolate);

  // Deletes all AsyncCompileJobs that belong to the given context. All
  // compilation is aborted, no more callbacks will be triggered. This is used
  // when a context is disposed, e.g. because of browser navigation.
  void DeleteCompileJobsOnContext(Handle<Context> context);

  // Deletes all AsyncCompileJobs that belong to the given Isolate. All
  // compilation is aborted, no more callbacks will be triggered. This is used
  // for tearing down an isolate, or to clean it up to be reused.
  void DeleteCompileJobsOnIsolate(Isolate* isolate);

  // Get a token for compiling wrappers for an Isolate. The token is used to
  // synchronize background tasks on isolate shutdown. The caller should only
  // hold the token while compiling export wrappers. If the isolate is already
  // shutting down, this method will return an invalid token.
  OperationsBarrier::Token StartWrapperCompilation(Isolate*);

  // Manage the set of Isolates that use this WasmEngine.
  void AddIsolate(Isolate* isolate);
  void RemoveIsolate(Isolate* isolate);

  // Trigger code logging for the given code objects in all Isolates which have
  // access to the NativeModule containing this code. This method can be called
  // from background threads.
  void LogCode(base::Vector<WasmCode*>);

  // Enable code logging for the given Isolate. Initially, code logging is
  // enabled if {WasmCode::ShouldBeLogged(Isolate*)} returns true during
  // {AddIsolate}.
  void EnableCodeLogging(Isolate*);

  // This is called from the foreground thread of the Isolate to log all
  // outstanding code objects (added via {LogCode}).
  void LogOutstandingCodesForIsolate(Isolate*);

  // Create a new NativeModule. The caller is responsible for its
  // lifetime. The native module will be given some memory for code,
  // which will be page size aligned. The size of the initial memory
  // is determined by {code_size_estimate}. The native module may later request
  // more memory.
  // TODO(wasm): isolate is only required here for CompilationState.
  std::shared_ptr<NativeModule> NewNativeModule(
      Isolate* isolate, WasmEnabledFeatures enabled_features,
      CompileTimeImports compile_imports,
      std::shared_ptr<const WasmModule> module, size_t code_size_estimate);

  // Try getting a cached {NativeModule}, or get ownership for its creation.
  // Return {nullptr} if no {NativeModule} exists for these bytes. In this case,
  // a {nullopt} entry is added to let other threads know that a {NativeModule}
  // for these bytes is currently being created. The caller should eventually
  // call {UpdateNativeModuleCache} to update the entry and wake up other
  // threads. The {wire_bytes}' underlying array should be valid at least until
  // the call to {UpdateNativeModuleCache}.
  // The provided {CompileTimeImports} are considered part of the caching key,
  // because they change the generated code as well as the behavior of the
  // {imports()} function of any WasmModuleObjects we'll create for this
  // NativeModule later.
  std::shared_ptr<NativeModule> MaybeGetNativeModule(
      ModuleOrigin origin, base::Vector<const uint8_t> wire_bytes,
      const CompileTimeImports& compile_imports, Isolate* isolate);

  // Replace the temporary {nullopt} with the new native module, or
  // erase it if any error occurred. Wake up blocked threads waiting for this
  // module.
  // To avoid a deadlock on the main thread between synchronous and streaming
  // compilation, two compilation jobs might compile the same native module at
  // the same time. In this case the first call to {UpdateNativeModuleCache}
  // will insert the native module in the cache, and the last call will receive
  // the existing entry from the cache.
  // Return the cached entry, or {native_module} if there was no previously
  // cached module.
  std::shared_ptr<NativeModule> UpdateNativeModuleCache(
      bool has_error, std::shared_ptr<NativeModule> native_module,
      Isolate* isolate);

  // Register this prefix hash for a streaming compilation job.
  // If the hash is not in the cache yet, the function returns true and the
  // caller owns the compilation of this module.
  // Otherwise another compilation job is currently preparing or has already
  // prepared a module with the same prefix hash. The caller should wait until
  // the stream is finished and call {MaybeGetNativeModule} to either get the
  // module from the cache or get ownership for the compilation of these bytes.
  bool GetStreamingCompilationOwnership(
      size_t prefix_hash, const CompileTimeImports& compile_imports);

  // Remove the prefix hash from the cache when compilation failed. If
  // compilation succeeded, {UpdateNativeModuleCache} should be called instead.
  void StreamingCompilationFailed(size_t prefix_hash,
                                  const CompileTimeImports& compile_imports);

  void FreeNativeModule(NativeModule*);
  void ClearWeakScriptHandle(Isolate* isolate,
                             std::unique_ptr<Address*> location);

  // Sample the code size of the given {NativeModule} in all isolates that have
  // access to it. Call this after top-tier compilation finished.
  // This will spawn foreground tasks that do *not* keep the NativeModule alive.
  void SampleTopTierCodeSizeInAllIsolates(const std::shared_ptr<NativeModule>&);

  // Called by each Isolate to report its live code for a GC cycle. First
  // version reports an externally determined set of live code (might be empty),
  // second version gets live code from the execution stack of that isolate.
  void ReportLiveCodeForGC(Isolate*, base::Vector<WasmCode*>);
  void ReportLiveCodeFromStackForGC(Isolate*);

  // Add potentially dead code. The occurrence in the set of potentially dead
  // code counts as a reference, and is decremented on the next GC.
  // Returns {true} if the code was added to the set of potentially dead code,
  // {false} if an entry already exists. The ref count is *unchanged* in any
  // case.
  V8_WARN_UNUSED_RESULT bool AddPotentiallyDeadCode(WasmCode*);

  // Free dead code.
  using DeadCodeMap = std::unordered_map<NativeModule*, std::vector<WasmCode*>>;
  void FreeDeadCode(const DeadCodeMap&);
  void FreeDeadCodeLocked(const DeadCodeMap&);

  Handle<Script> GetOrCreateScript(Isolate*,
                                   const std::shared_ptr<NativeModule>&,
                                   base::Vector<const char> source_url);

  // Returns a barrier allowing background compile operations if valid and
  // preventing this object from being destroyed.
  std::shared_ptr<OperationsBarrier> GetBarrierForBackgroundCompile();

  TypeCanonicalizer* type_canonicalizer() { return &type_canonicalizer_; }

  compiler::WasmCallDescriptors* call_descriptors() {
    return &call_descriptors_;
  }

  // Returns an approximation of current off-heap memory used by this engine,
  // excluding code space.
  size_t EstimateCurrentMemoryConsumption() const;

  int GetDeoptsExecutedCount() const;
  int IncrementDeoptsExecutedCount();

  // Call on process start and exit.
  static void InitializeOncePerProcess();
  static void GlobalTearDown();

  static WasmOrphanedGlobalHandle* NewOrphanedGlobalHandle(
      WasmOrphanedGlobalHandle** pointer);
  static void FreeAllOrphanedGlobalHandles(WasmOrphanedGlobalHandle* start);

 private:
  struct CurrentGCInfo;
  struct IsolateInfo;
  struct NativeModuleInfo;

  AsyncCompileJob* CreateAsyncCompileJob(
      Isolate* isolate, WasmEnabledFeatures enabled,
      CompileTimeImports compile_imports,
      base::OwnedVector<const uint8_t> bytes, DirectHandle<Context> context,
      const char* api_method_name,
      std::shared_ptr<CompilationResultResolver> resolver, int compilation_id);

  void TriggerGC(int8_t gc_sequence_index);

  // Remove an isolate from the outstanding isolates of the current GC. Returns
  // true if the isolate was still outstanding, false otherwise. Hold {mutex_}
  // when calling this method.
  bool RemoveIsolateFromCurrentGC(Isolate*);

  // Finish a GC if there are no more outstanding isolates. Hold {mutex_} when
  // calling this method.
  void PotentiallyFinishCurrentGC();

  AccountingAllocator allocator_;

#ifdef V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING
  // Implements a GDB-remote stub for WebAssembly debugging.
  std::unique_ptr<gdb_server::GdbServer> gdb_server_;
#endif  // V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING

  std::atomic<int> next_compilation_id_{0};

  // Counter for number of times a deopt was executed.
  std::atomic<int> deopts_executed_{0};

  TypeCanonicalizer type_canonicalizer_;

  compiler::WasmCallDescriptors call_descriptors_;

  // This mutex protects all information which is mutated concurrently or
  // fields that are initialized lazily on the first access.
  mutable base::Mutex mutex_;

  //////////////////////////////////////////////////////////////////////////////
  // Protected by {mutex_}:

  // We use an AsyncCompileJob as the key for itself so that we can delete the
  // job from the map when it is finished.
  std::unordered_map<AsyncCompileJob*, std::unique_ptr<AsyncCompileJob>>
      async_compile_jobs_;

  std::shared_ptr<CompilationStatistics> compilation_stats_;
  std::unique_ptr<CodeTracer> code_tracer_;

  // Set of isolates which use this WasmEngine.
  std::unordered_map<Isolate*, std::unique_ptr<IsolateInfo>> isolates_;

  // Set of native modules managed by this engine.
  std::unordered_map<NativeModule*, std::unique_ptr<NativeModuleInfo>>
      native_modules_;

  std::shared_ptr<OperationsBarrier> operations_barrier_{
      std::make_shared<OperationsBarrier>()};

  // Size of code that became dead since the last GC. If this exceeds a certain
  // threshold, a new GC is triggered.
  size_t new_potentially_dead_code_size_ = 0;
  // Set of potentially dead code. This set holds one ref for each code object,
  // until code is detected to be really dead. At that point, the ref count is
  // decremented and code is moved to the {dead_code} set. If the code is
  // finally deleted, it is also removed from {dead_code}.
  std::unordered_set<WasmCode*> potentially_dead_code_;
  // Code that is not being executed in any isolate any more, but the ref count
  // did not drop to zero yet.
  std::unordered_set<WasmCode*> dead_code_;
  int8_t num_code_gcs_triggered_ = 0;

  // If an engine-wide GC is currently running, this pointer stores information
  // about that.
  std::unique_ptr<CurrentGCInfo> current_gc_info_;

  NativeModuleCache native_module_cache_;

  // End of fields protected by {mutex_}.
  //////////////////////////////////////////////////////////////////////////////
};

// Returns a reference to the WasmEngine shared by the entire process.
V8_EXPORT_PRIVATE WasmEngine* GetWasmEngine();

// Returns a reference to the WasmCodeManager shared by the entire process.
V8_EXPORT_PRIVATE WasmCodeManager* GetWasmCodeManager();

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_WASM_ENGINE_H_
