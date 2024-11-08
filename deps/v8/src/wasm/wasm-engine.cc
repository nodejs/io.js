// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-engine.h"

#include <optional>

#include "src/base/functional.h"
#include "src/base/platform/time.h"
#include "src/base/small-vector.h"
#include "src/common/assert-scope.h"
#include "src/common/globals.h"
#include "src/debug/debug.h"
#include "src/diagnostics/code-tracer.h"
#include "src/diagnostics/compilation-statistics.h"
#include "src/execution/frames.h"
#include "src/execution/v8threads.h"
#include "src/handles/global-handles-inl.h"
#include "src/logging/counters.h"
#include "src/logging/metrics.h"
#include "src/objects/heap-number.h"
#include "src/objects/managed-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/objects.h"
#include "src/objects/primitive-heap-object.h"
#include "src/utils/ostreams.h"
#include "src/wasm/function-compiler.h"
#include "src/wasm/module-compiler.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/module-instantiate.h"
#include "src/wasm/pgo.h"
#include "src/wasm/stacks.h"
#include "src/wasm/std-object-sizes.h"
#include "src/wasm/streaming-decoder.h"
#include "src/wasm/wasm-debug.h"
#include "src/wasm/wasm-limits.h"
#include "src/wasm/wasm-objects-inl.h"

#if V8_ENABLE_DRUMBRAKE
#include "src/wasm/interpreter/wasm-interpreter-inl.h"
#endif  // V8_ENABLE_DRUMBRAKE

#ifdef V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING
#include "src/debug/wasm/gdb-server/gdb-server.h"
#endif  // V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING

namespace v8::internal::wasm {

#define TRACE_CODE_GC(...)                                             \
  do {                                                                 \
    if (v8_flags.trace_wasm_code_gc) PrintF("[wasm-gc] " __VA_ARGS__); \
  } while (false)

// This class exists in order to solve a shutdown ordering problem.
// The basic situation is that the process-global WasmEngine has, for each
// Isolate that it knows about, a map from NativeModule to Script, using
// WeakScriptHandles to make sure that the NativeModules, which are shared
// across the process, don't keep the (Isolate-specific) Scripts alive.
// In the other direction, the Scripts keep the NativeModule alive, IOW
// usually the Scripts die first, and the WeakScriptHandles are cleared
// before being freed.
// In case of asm.js modules and in case of Isolate shutdown, it can happen
// that the NativeModule dies first, so the WeakScriptHandles are no longer
// needed and should be destroyed. That can only happen on the main thread of
// the Isolate they belong to, whereas the last thread that releases a
// NativeModule might be any other thread, so we post a
// ClearWeakScriptHandleTask to that isolate's foreground task runner.
// In case of Isolate shutdown at an inconvenient moment, this task runner can
// destroy all waiting tasks; and *afterwards* global handles are freed, which
// writes to the memory location backing the handle, so this bit of memory must
// not be owned by (and die with) the ClearWeakScriptHandleTask.
// The solution is this class here: its instances form a linked list owned by
// the Isolate to which the referenced Scripts belong. Its name refers to the
// fact that it stores global handles that used to have a purpose but are now
// just waiting for the right thread to destroy them.
// If the ClearWeakScriptHandleTask gets to run (i.e. in the regular case),
// it destroys the weak global handle and then the WasmOrphanedGlobalHandle
// container, removing it from the isolate's list.
// If the ClearWeakScriptHandleTask is destroyed before it runs, the isolate's
// list of WasmOrphanedGlobalHandles isn't modified, so the indirection cell
// is still around when all remaining global handles are freed; nevertheless
// it won't leak because the Isolate owns it and will free it.
class WasmOrphanedGlobalHandle {
 public:
  WasmOrphanedGlobalHandle() = default;

  void InitializeLocation(std::unique_ptr<Address*> location) {
    location_ = std::move(location);
  }

  static void Destroy(WasmOrphanedGlobalHandle* that) {
    // Destroy the global handle if it still exists.
    Address** location = that->location_.get();
    if (location) GlobalHandles::Destroy(*location);
    that->location_.reset();
    // Unlink and free the container.
    *that->prev_ptr_ = that->next_;
    if (that->next_ != nullptr) that->next_->prev_ptr_ = that->prev_ptr_;
    // This function could be a non-static method, but then the next line
    // would read "delete this", which is UB.
    delete that;
  }

 private:
  friend class WasmEngine;

  // This is a doubly linked list with a twist: the {next_} pointer is just
  // what you would expect, whereas {prev_ptr_} points at the slot inside
  // the previous element that's pointing at the current element. The purpose
  // of this design is to make it possible for the previous element to be
  // the {Isolate::wasm_orphaned_handle_} field, without requiring any
  // special-casing in the insert and delete operations.
  WasmOrphanedGlobalHandle* next_ = nullptr;
  WasmOrphanedGlobalHandle** prev_ptr_ = nullptr;
  std::unique_ptr<Address*> location_;
};

// static
WasmOrphanedGlobalHandle* WasmEngine::NewOrphanedGlobalHandle(
    WasmOrphanedGlobalHandle** pointer) {
  // No need for additional locking: this is only ever called indirectly
  // from {WasmEngine::ClearWeakScriptHandle()}, which holds the engine-wide
  // {mutex_}.
  WasmOrphanedGlobalHandle* orphan = new WasmOrphanedGlobalHandle();
  orphan->next_ = *pointer;
  orphan->prev_ptr_ = pointer;
  if (orphan->next_ != nullptr) orphan->next_->prev_ptr_ = &orphan->next_;
  *pointer = orphan;
  return orphan;
}

// static
void WasmEngine::FreeAllOrphanedGlobalHandles(WasmOrphanedGlobalHandle* start) {
  // This is meant to be called from ~Isolate, so we no longer care about
  // maintaining invariants: the only task is to free memory to prevent leaks.
  while (start != nullptr) {
    WasmOrphanedGlobalHandle* next = start->next_;
    delete start;
    start = next;
  }
}

// A task to log a set of {WasmCode} objects in an isolate. It does not own any
// data itself, since it is owned by the platform, so lifetime is not really
// bound to the wasm engine.
class WasmEngine::LogCodesTask : public CancelableTask {
  friend class WasmEngine;

 public:
  explicit LogCodesTask(Isolate* isolate)
      : CancelableTask(isolate), isolate_(isolate) {}

  void RunInternal() override {
    GetWasmEngine()->LogOutstandingCodesForIsolate(isolate_);
  }

 private:
  Isolate* const isolate_;
};

namespace {
void CheckNoArchivedThreads(Isolate* isolate) {
  class ArchivedThreadsVisitor : public ThreadVisitor {
    void VisitThread(Isolate* isolate, ThreadLocalTop* top) override {
      // Archived threads are rarely used, and not combined with Wasm at the
      // moment. Implement this and test it properly once we have a use case for
      // that.
      FATAL("archived threads in combination with wasm not supported");
    }
  } archived_threads_visitor;
  isolate->thread_manager()->IterateArchivedThreads(&archived_threads_visitor);
}

class WasmGCForegroundTask : public CancelableTask {
 public:
  explicit WasmGCForegroundTask(Isolate* isolate)
      : CancelableTask(isolate->cancelable_task_manager()), isolate_(isolate) {}

  void RunInternal() final {
    // The stack can contain live frames, for instance when this is invoked
    // during a pause or a breakpoint.
    GetWasmEngine()->ReportLiveCodeFromStackForGC(isolate_);
  }

 private:
  Isolate* isolate_;
};

class ClearWeakScriptHandleTask : public CancelableTask {
 public:
  explicit ClearWeakScriptHandleTask(Isolate* isolate,
                                     std::unique_ptr<Address*> location)
      : CancelableTask(isolate->cancelable_task_manager()) {
    handle_ = isolate->NewWasmOrphanedGlobalHandle();
    handle_->InitializeLocation(std::move(location));
  }

  // We don't override the destructor, because there is nothing to do:
  // if the task is deleted before it was run, then everything is shutting
  // down anyway, so destroying the GlobalHandle is no longer relevant (and
  // it might well be too late to do that safely).

  void RunInternal() override {
    WasmOrphanedGlobalHandle::Destroy(handle_);
    handle_ = nullptr;
  }

 private:
  // This is owned by the Isolate to ensure correct shutdown ordering.
  WasmOrphanedGlobalHandle* handle_;
};

class WeakScriptHandle {
 public:
  WeakScriptHandle(DirectHandle<Script> script, Isolate* isolate)
      : script_id_(script->id()), isolate_(isolate) {
    DCHECK(IsString(script->name()) || IsUndefined(script->name()));
    if (IsString(script->name())) {
      source_url_ = Cast<String>(script->name())->ToCString();
    }
    auto global_handle =
        script->GetIsolate()->global_handles()->Create(*script);
    location_ = std::make_unique<Address*>(global_handle.location());
    GlobalHandles::MakeWeak(location_.get());
  }

  ~WeakScriptHandle() {
    // Usually the destructor of this class is called after the weak callback,
    // because the Script keeps the NativeModule alive. In that case,
    // {location_} is already cleared, and there is nothing to do.
    if (location_ == nullptr || *location_ == nullptr) return;
    // For asm.js modules, the Script usually outlives the NativeModule.
    // We must destroy the GlobalHandle before freeing the memory that's
    // backing {location_}, so that when the Script does die eventually, there
    // is no lingering weak GlobalHandle that would try to clear {location_}.
    // We can't do that from arbitrary threads, so we must post a task to the
    // main thread.
    GetWasmEngine()->ClearWeakScriptHandle(isolate_, std::move(location_));
  }

  WeakScriptHandle(WeakScriptHandle&&) V8_NOEXCEPT = default;

  Handle<Script> handle() const { return Handle<Script>(*location_); }

  // Called by ~IsolateInfo. When the Isolate is shutting down, cleaning
  // up properly is both no longer necessary and no longer safe to do.
  void Clear() { location_.reset(); }

  int script_id() const { return script_id_; }

  const std::shared_ptr<const char[]>& source_url() const {
    return source_url_;
  }

 private:
  // Store the location in a unique_ptr so that its address stays the same even
  // when this object is moved/copied.
  std::unique_ptr<Address*> location_;

  // Store the script ID independent of the weak handle, such that it's always
  // available.
  int script_id_;

  // Similar for the source URL. We cannot dereference the Handle from arbitrary
  // threads, but we need the URL available for code logging.
  // The shared pointer is kept alive by unlogged code, even if this entry is
  // collected in the meantime.
  // TODO(chromium:1132260): Revisit this for huge URLs.
  std::shared_ptr<const char[]> source_url_;

  // The Isolate that the handled script belongs to.
  Isolate* isolate_;
};

// If PGO data is being collected, keep all native modules alive, so repeated
// runs of a benchmark (with different configuration) all use the same module.
// This vector is protected by the global WasmEngine's mutex, but not defined in
// the header because it's a private implementation detail.
std::vector<std::shared_ptr<NativeModule>>* native_modules_kept_alive_for_pgo;

}  // namespace

std::shared_ptr<NativeModule> NativeModuleCache::MaybeGetNativeModule(
    ModuleOrigin origin, base::Vector<const uint8_t> wire_bytes,
    const CompileTimeImports& compile_imports) {
  if (!v8_flags.wasm_native_module_cache_enabled) return nullptr;
  if (origin != kWasmOrigin) return nullptr;
  base::MutexGuard lock(&mutex_);
  size_t prefix_hash = PrefixHash(wire_bytes);
  NativeModuleCache::Key key{prefix_hash, compile_imports, wire_bytes};
  while (true) {
    auto it = map_.find(key);
    if (it == map_.end()) {
      // Even though this exact key is not in the cache, there might be a
      // matching prefix hash indicating that a streaming compilation is
      // currently compiling a module with the same prefix. {OnFinishedStream}
      // happens on the main thread too, so waiting for streaming compilation to
      // finish would create a deadlock. Instead, compile the module twice and
      // handle the conflict in {UpdateNativeModuleCache}.

      // Insert a {nullopt} entry to let other threads know that this
      // {NativeModule} is already being created on another thread.
      [[maybe_unused]] auto [iterator, inserted] =
          map_.emplace(key, std::nullopt);
      DCHECK(inserted);
      return nullptr;
    }
    if (it->second.has_value()) {
      if (auto shared_native_module = it->second.value().lock()) {
        DCHECK_EQ(
            shared_native_module->compile_imports().compare(compile_imports),
            0);
        DCHECK_EQ(shared_native_module->wire_bytes(), wire_bytes);
        return shared_native_module;
      }
    }
    // TODO(11858): This deadlocks in predictable mode, because there is only a
    // single thread.
    cache_cv_.Wait(&mutex_);
  }
}

bool NativeModuleCache::GetStreamingCompilationOwnership(
    size_t prefix_hash, const CompileTimeImports& compile_imports) {
  base::MutexGuard lock(&mutex_);
  auto it = map_.lower_bound(Key{prefix_hash, compile_imports, {}});
  if (it != map_.end() && it->first.prefix_hash == prefix_hash) {
    DCHECK_IMPLIES(!it->first.bytes.empty(),
                   PrefixHash(it->first.bytes) == prefix_hash);
    return false;
  }
  Key key{prefix_hash, compile_imports, {}};
  DCHECK_EQ(0, map_.count(key));
  map_.emplace(key, std::nullopt);
  return true;
}

void NativeModuleCache::StreamingCompilationFailed(
    size_t prefix_hash, const CompileTimeImports& compile_imports) {
  base::MutexGuard lock(&mutex_);
  Key key{prefix_hash, compile_imports, {}};
  map_.erase(key);
  cache_cv_.NotifyAll();
}

std::shared_ptr<NativeModule> NativeModuleCache::Update(
    std::shared_ptr<NativeModule> native_module, bool error) {
  DCHECK_NOT_NULL(native_module);
  if (!v8_flags.wasm_native_module_cache_enabled) return native_module;
  if (native_module->module()->origin != kWasmOrigin) return native_module;
  base::Vector<const uint8_t> wire_bytes = native_module->wire_bytes();
  DCHECK(!wire_bytes.empty());
  size_t prefix_hash = PrefixHash(native_module->wire_bytes());
  base::MutexGuard lock(&mutex_);
  const CompileTimeImports& compile_imports = native_module->compile_imports();
  map_.erase(Key{prefix_hash, compile_imports, {}});
  const Key key{prefix_hash, compile_imports, wire_bytes};
  auto it = map_.find(key);
  if (it != map_.end()) {
    if (it->second.has_value()) {
      auto conflicting_module = it->second.value().lock();
      if (conflicting_module != nullptr) {
        DCHECK_EQ(conflicting_module->wire_bytes(), wire_bytes);
        // This return might delete {native_module} if we were the last holder.
        // That in turn can call {NativeModuleCache::Erase}, which takes the
        // mutex. This is not a problem though, since the {MutexGuard} above is
        // released before the {native_module}, per the definition order.
        return conflicting_module;
      }
    }
    map_.erase(it);
  }
  if (!error) {
    // The key now points to the new native module's owned copy of the bytes,
    // so that it stays valid until the native module is freed and erased from
    // the map.
    [[maybe_unused]] auto [iterator, inserted] = map_.emplace(
        key, std::optional<std::weak_ptr<NativeModule>>(native_module));
    DCHECK(inserted);
  }
  cache_cv_.NotifyAll();
  return native_module;
}

void NativeModuleCache::Erase(NativeModule* native_module) {
  if (!v8_flags.wasm_native_module_cache_enabled) return;
  if (native_module->module()->origin != kWasmOrigin) return;
  // Happens in some tests where bytes are set directly.
  if (native_module->wire_bytes().empty()) return;
  base::MutexGuard lock(&mutex_);
  size_t prefix_hash = PrefixHash(native_module->wire_bytes());
  map_.erase(Key{prefix_hash, native_module->compile_imports(),
                 native_module->wire_bytes()});
  cache_cv_.NotifyAll();
}

// static
size_t NativeModuleCache::PrefixHash(base::Vector<const uint8_t> wire_bytes) {
  // Compute the hash as a combined hash of the sections up to the code section
  // header, to mirror the way streaming compilation does it.
  Decoder decoder(wire_bytes.begin(), wire_bytes.end());
  decoder.consume_bytes(8, "module header");
  size_t hash = GetWireBytesHash(wire_bytes.SubVector(0, 8));
  SectionCode section_id = SectionCode::kUnknownSectionCode;
  while (decoder.ok() && decoder.more()) {
    section_id = static_cast<SectionCode>(decoder.consume_u8());
    uint32_t section_size = decoder.consume_u32v("section size");
    if (section_id == SectionCode::kCodeSectionCode) {
      hash = base::hash_combine(hash, section_size);
      break;
    }
    const uint8_t* payload_start = decoder.pc();
    decoder.consume_bytes(section_size, "section payload");
    size_t section_hash =
        GetWireBytesHash(base::VectorOf(payload_start, section_size));
    hash = base::hash_combine(hash, section_hash);
  }
  return hash;
}

struct WasmEngine::CurrentGCInfo {
  explicit CurrentGCInfo(int8_t gc_sequence_index)
      : gc_sequence_index(gc_sequence_index) {
    DCHECK_NE(0, gc_sequence_index);
  }

  // Set of isolates that did not scan their stack yet for used WasmCode, and
  // their scheduled foreground task.
  std::unordered_map<Isolate*, WasmGCForegroundTask*> outstanding_isolates;

  // Set of dead code. Filled with all potentially dead code on initialization.
  // Code that is still in-use is removed by the individual isolates.
  std::unordered_set<WasmCode*> dead_code;

  // The number of GCs triggered in the native module that triggered this GC.
  // This is stored in the histogram for each participating isolate during
  // execution of that isolate's foreground task.
  const int8_t gc_sequence_index;

  // If during this GC, another GC was requested, we skipped that other GC (we
  // only run one GC at a time). Remember though to trigger another one once
  // this one finishes. {next_gc_sequence_index} is 0 if no next GC is needed,
  // and >0 otherwise. It stores the {num_code_gcs_triggered} of the native
  // module which triggered the next GC.
  int8_t next_gc_sequence_index = 0;

  // The start time of this GC; used for tracing and sampled via {Counters}.
  // Can be null ({TimeTicks::IsNull()}) if timer is not high resolution.
  base::TimeTicks start_time;
};

struct WasmEngine::IsolateInfo {
  explicit IsolateInfo(Isolate* isolate)
      : log_codes(WasmCode::ShouldBeLogged(isolate)),
        async_counters(isolate->async_counters()),
        wrapper_compilation_barrier_(std::make_shared<OperationsBarrier>()) {
    v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(isolate);
    v8::Platform* platform = V8::GetCurrentPlatform();
    foreground_task_runner = platform->GetForegroundTaskRunner(v8_isolate);
  }

  ~IsolateInfo() {
    // Before destructing, the {WasmEngine} must have cleared outstanding code
    // to log.
    DCHECK_EQ(0, code_to_log.size());

    // We need the {~WeakScriptHandle} destructor in {scripts} to behave
    // differently depending on whether the Isolate is in the process of
    // being destroyed. That's the only situation where we would run the
    // {~IsolateInfo} destructor, and in that case, we can no longer post
    // the task that would destroy the {WeakScriptHandle}'s {GlobalHandle};
    // whereas if only individual entries of {scripts} get deleted, then
    // we can and should post such tasks.
    for (auto& [native_module, script_handle] : scripts) {
      script_handle.Clear();
    }
  }

  // All native modules that are being used by this Isolate.
  std::unordered_set<NativeModule*> native_modules;

  // Scripts created for each native module in this isolate.
  std::unordered_map<NativeModule*, WeakScriptHandle> scripts;

  // Caches whether code needs to be logged on this isolate.
  bool log_codes;

  // Maps script ID to vector of code objects that still need to be logged, and
  // the respective source URL.
  struct CodeToLogPerScript {
    std::vector<WasmCode*> code;
    std::shared_ptr<const char[]> source_url;
  };
  std::unordered_map<int, CodeToLogPerScript> code_to_log;

  // The foreground task runner of the isolate (can be called from background).
  std::shared_ptr<v8::TaskRunner> foreground_task_runner;

  const std::shared_ptr<Counters> async_counters;

  // Keep new modules in debug state.
  bool keep_in_debug_state = false;

  // Keep track whether we already added a sample for PKU support (we only want
  // one sample per Isolate).
  bool pku_support_sampled = false;

  // Operations barrier to synchronize on wrapper compilation on isolate
  // shutdown.
  // TODO(wasm): Remove this once we can use the generic js-to-wasm wrapper
  // everywhere.
  std::shared_ptr<OperationsBarrier> wrapper_compilation_barrier_;
};

void WasmEngine::ClearWeakScriptHandle(Isolate* isolate,
                                       std::unique_ptr<Address*> location) {
  // This function is designed for one targeted use case, which always
  // acquires a lock on {mutex_} before calling here.
  mutex_.AssertHeld();
  IsolateInfo* isolate_info = isolates_[isolate].get();
  std::shared_ptr<TaskRunner> runner = isolate_info->foreground_task_runner;
  runner->PostTask(std::make_unique<ClearWeakScriptHandleTask>(
      isolate, std::move(location)));
}

struct WasmEngine::NativeModuleInfo {
  explicit NativeModuleInfo(std::weak_ptr<NativeModule> native_module)
      : weak_ptr(std::move(native_module)) {}

  // Weak pointer, to gain back a shared_ptr if needed.
  std::weak_ptr<NativeModule> weak_ptr;

  // Set of isolates using this NativeModule.
  std::unordered_set<Isolate*> isolates;
};

WasmEngine::WasmEngine() : call_descriptors_(&allocator_) {}

WasmEngine::~WasmEngine() {
#ifdef V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING
  // Synchronize on the GDB-remote thread, if running.
  gdb_server_.reset();
#endif  // V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING

  // Free all modules that were kept alive for collecting PGO. This is to avoid
  // memory leaks.
  if (V8_UNLIKELY(native_modules_kept_alive_for_pgo)) {
    delete native_modules_kept_alive_for_pgo;
  }

  operations_barrier_->CancelAndWait();

  // All AsyncCompileJobs have been canceled.
  DCHECK(async_compile_jobs_.empty());
  // All Isolates have been deregistered.
  DCHECK(isolates_.empty());
  // All NativeModules did die.
  DCHECK(native_modules_.empty());
  // Native module cache does not leak.
  DCHECK(native_module_cache_.empty());
}

bool WasmEngine::SyncValidate(Isolate* isolate, WasmEnabledFeatures enabled,
                              CompileTimeImports compile_imports,
                              ModuleWireBytes bytes) {
  TRACE_EVENT0("v8.wasm", "wasm.SyncValidate");
  if (bytes.length() == 0) return false;

  auto result = DecodeWasmModule(
      enabled, bytes.module_bytes(), true, kWasmOrigin, isolate->counters(),
      isolate->metrics_recorder(),
      isolate->GetOrRegisterRecorderContextId(isolate->native_context()),
      DecodingMethod::kSync);
  if (result.failed()) return false;
  WasmError link_error = ValidateAndSetBuiltinImports(
      result.value().get(), bytes.module_bytes(), compile_imports);
  return !link_error.has_error();
}

MaybeHandle<AsmWasmData> WasmEngine::SyncCompileTranslatedAsmJs(
    Isolate* isolate, ErrorThrower* thrower, ModuleWireBytes bytes,
    DirectHandle<Script> script,
    base::Vector<const uint8_t> asm_js_offset_table_bytes,
    DirectHandle<HeapNumber> uses_bitset, LanguageMode language_mode) {
  int compilation_id = next_compilation_id_.fetch_add(1);
  TRACE_EVENT1("v8.wasm", "wasm.SyncCompileTranslatedAsmJs", "id",
               compilation_id);
  ModuleOrigin origin = language_mode == LanguageMode::kSloppy
                            ? kAsmJsSloppyOrigin
                            : kAsmJsStrictOrigin;
  // TODO(leszeks): If we want asm.js in UKM, we should figure out a way to pass
  // the context id in here.
  v8::metrics::Recorder::ContextId context_id =
      v8::metrics::Recorder::ContextId::Empty();
  ModuleResult result = DecodeWasmModule(
      WasmEnabledFeatures::ForAsmjs(), bytes.module_bytes(), false, origin,
      isolate->counters(), isolate->metrics_recorder(), context_id,
      DecodingMethod::kSync);
  if (result.failed()) {
    // This happens once in a while when we have missed some limit check
    // in the asm parser. Output an error message to help diagnose, but crash.
    std::cout << result.error().message();
    UNREACHABLE();
  }

  result.value()->asm_js_offset_information =
      std::make_unique<AsmJsOffsetInformation>(asm_js_offset_table_bytes);

  // Transfer ownership of the WasmModule to the {Managed<WasmModule>} generated
  // in {CompileToNativeModule}.
  constexpr ProfileInformation* kNoProfileInformation = nullptr;
  std::shared_ptr<NativeModule> native_module = CompileToNativeModule(
      isolate, WasmEnabledFeatures::ForAsmjs(), CompileTimeImports{}, thrower,
      std::move(result).value(), bytes, compilation_id, context_id,
      kNoProfileInformation);
  if (!native_module) return {};

  native_module->LogWasmCodes(isolate, *script);
  {
    // Register the script with the isolate. We do this unconditionally for
    // consistency; it is in particular required for logging lazy-compiled code.
    base::MutexGuard guard(&mutex_);
    DCHECK_EQ(1, isolates_.count(isolate));
    auto& scripts = isolates_[isolate]->scripts;
    // If the same asm.js module is instantiated repeatedly, then we
    // deduplicate the NativeModule, so the script exists already.
    if (scripts.count(native_module.get()) == 0) {
      scripts.emplace(native_module.get(), WeakScriptHandle(script, isolate));
    }
  }

  return AsmWasmData::New(isolate, std::move(native_module), uses_bitset);
}

Handle<WasmModuleObject> WasmEngine::FinalizeTranslatedAsmJs(
    Isolate* isolate, DirectHandle<AsmWasmData> asm_wasm_data,
    DirectHandle<Script> script) {
  std::shared_ptr<NativeModule> native_module =
      asm_wasm_data->managed_native_module()->get();
  Handle<WasmModuleObject> module_object =
      WasmModuleObject::New(isolate, std::move(native_module), script);
  return module_object;
}

MaybeHandle<WasmModuleObject> WasmEngine::SyncCompile(
    Isolate* isolate, WasmEnabledFeatures enabled,
    CompileTimeImports compile_imports, ErrorThrower* thrower,
    ModuleWireBytes bytes) {
  int compilation_id = next_compilation_id_.fetch_add(1);
  TRACE_EVENT1("v8.wasm", "wasm.SyncCompile", "id", compilation_id);
  v8::metrics::Recorder::ContextId context_id =
      isolate->GetOrRegisterRecorderContextId(isolate->native_context());
  std::shared_ptr<WasmModule> module;
  {
    // Normally modules are validated in {CompileToNativeModule} but in jitless
    // mode the only opportunity of validatiom is during decoding.
    bool validate_module = v8_flags.wasm_jitless;
    ModuleResult result = DecodeWasmModule(
        enabled, bytes.module_bytes(), validate_module, kWasmOrigin,
        isolate->counters(), isolate->metrics_recorder(), context_id,
        DecodingMethod::kSync);
    if (result.failed()) {
      thrower->CompileFailed(result.error());
      return {};
    }
    module = std::move(result).value();
    if (WasmError error = ValidateAndSetBuiltinImports(
            module.get(), bytes.module_bytes(), compile_imports)) {
      thrower->LinkError("%s @+%u", error.message().c_str(), error.offset());
      return {};
    }
  }

  // If experimental PGO via files is enabled, load profile information now.
  std::unique_ptr<ProfileInformation> pgo_info;
  if (V8_UNLIKELY(v8_flags.experimental_wasm_pgo_from_file)) {
    pgo_info = LoadProfileFromFile(module.get(), bytes.module_bytes());
  }

  // Transfer ownership of the WasmModule to the {Managed<WasmModule>} generated
  // in {CompileToNativeModule}.
  std::shared_ptr<NativeModule> native_module = CompileToNativeModule(
      isolate, enabled, std::move(compile_imports), thrower, std::move(module),
      bytes, compilation_id, context_id, pgo_info.get());
  if (!native_module) return {};

#ifdef DEBUG
  // Ensure that code GC will check this isolate for live code.
  {
    base::MutexGuard lock(&mutex_);
    DCHECK_EQ(1, isolates_.count(isolate));
    DCHECK_EQ(1, isolates_[isolate]->native_modules.count(native_module.get()));
    DCHECK_EQ(1, native_modules_.count(native_module.get()));
    DCHECK_EQ(1, native_modules_[native_module.get()]->isolates.count(isolate));
  }
#endif

  constexpr base::Vector<const char> kNoSourceUrl;
  DirectHandle<Script> script =
      GetOrCreateScript(isolate, native_module, kNoSourceUrl);

  native_module->LogWasmCodes(isolate, *script);

  // Create the compiled module object and populate with compiled functions
  // and information needed at instantiation time. This object needs to be
  // serializable. Instantiation may occur off a deserialized version of this
  // object.
  Handle<WasmModuleObject> module_object =
      WasmModuleObject::New(isolate, std::move(native_module), script);

  // Finish the Wasm script now and make it public to the debugger.
  isolate->debug()->OnAfterCompile(script);
  return module_object;
}

MaybeHandle<WasmInstanceObject> WasmEngine::SyncInstantiate(
    Isolate* isolate, ErrorThrower* thrower,
    Handle<WasmModuleObject> module_object, MaybeHandle<JSReceiver> imports,
    MaybeHandle<JSArrayBuffer> memory) {
  TRACE_EVENT0("v8.wasm", "wasm.SyncInstantiate");
  return InstantiateToInstanceObject(isolate, thrower, module_object, imports,
                                     memory);
}

void WasmEngine::AsyncInstantiate(
    Isolate* isolate, std::unique_ptr<InstantiationResultResolver> resolver,
    Handle<WasmModuleObject> module_object, MaybeHandle<JSReceiver> imports) {
  ErrorThrower thrower(isolate, "WebAssembly.instantiate()");
  TRACE_EVENT0("v8.wasm", "wasm.AsyncInstantiate");
  // Instantiate a TryCatch so that caught exceptions won't progagate out.
  // They will still be set as exceptions on the isolate.
  // TODO(clemensb): Avoid TryCatch, use Execution::TryCall internally to invoke
  // start function and report thrown exception explicitly via out argument.
  v8::TryCatch catcher(reinterpret_cast<v8::Isolate*>(isolate));
  catcher.SetVerbose(false);
  catcher.SetCaptureMessage(false);

  MaybeHandle<WasmInstanceObject> instance_object = SyncInstantiate(
      isolate, &thrower, module_object, imports, Handle<JSArrayBuffer>::null());

  if (!instance_object.is_null()) {
    resolver->OnInstantiationSucceeded(instance_object.ToHandleChecked());
    return;
  }

  if (isolate->has_exception()) {
    // The JS code executed during instantiation has thrown an exception.
    // We have to move the exception to the promise chain.
    Handle<Object> exception(isolate->exception(), isolate);
    isolate->clear_exception();
    resolver->OnInstantiationFailed(exception);
    thrower.Reset();
  } else {
    DCHECK(thrower.error());
    resolver->OnInstantiationFailed(thrower.Reify());
  }
}

void WasmEngine::AsyncCompile(
    Isolate* isolate, WasmEnabledFeatures enabled,
    CompileTimeImports compile_imports,
    std::shared_ptr<CompilationResultResolver> resolver, ModuleWireBytes bytes,
    bool is_shared, const char* api_method_name_for_errors) {
  int compilation_id = next_compilation_id_.fetch_add(1);
  TRACE_EVENT1("v8.wasm", "wasm.AsyncCompile", "id", compilation_id);
  if (!v8_flags.wasm_async_compilation || v8_flags.wasm_jitless) {
    // Asynchronous compilation disabled; fall back on synchronous compilation.
    ErrorThrower thrower(isolate, api_method_name_for_errors);
    MaybeHandle<WasmModuleObject> module_object;
    if (is_shared) {
      // Make a copy of the wire bytes to avoid concurrent modification.
      std::unique_ptr<uint8_t[]> copy(new uint8_t[bytes.length()]);
      memcpy(copy.get(), bytes.start(), bytes.length());
      ModuleWireBytes bytes_copy(copy.get(), copy.get() + bytes.length());
      module_object = SyncCompile(isolate, enabled, std::move(compile_imports),
                                  &thrower, bytes_copy);
    } else {
      // The wire bytes are not shared, OK to use them directly.
      module_object = SyncCompile(isolate, enabled, std::move(compile_imports),
                                  &thrower, bytes);
    }
    if (thrower.error()) {
      resolver->OnCompilationFailed(thrower.Reify());
      return;
    }
    Handle<WasmModuleObject> module = module_object.ToHandleChecked();
    resolver->OnCompilationSucceeded(module);
    return;
  }

  if (v8_flags.wasm_test_streaming) {
    std::shared_ptr<StreamingDecoder> streaming_decoder =
        StartStreamingCompilation(isolate, enabled, std::move(compile_imports),
                                  handle(isolate->context(), isolate),
                                  api_method_name_for_errors,
                                  std::move(resolver));

    auto* rng = isolate->random_number_generator();
    base::SmallVector<base::Vector<const uint8_t>, 16> ranges;
    if (!bytes.module_bytes().empty()) ranges.push_back(bytes.module_bytes());
    // Split into up to 16 ranges (2^4).
    for (int round = 0; round < 4; ++round) {
      for (auto it = ranges.begin(); it != ranges.end(); ++it) {
        auto range = *it;
        if (range.size() < 2 || !rng->NextBool()) continue;  // Do not split.
        // Choose split point within [1, range.size() - 1].
        static_assert(kV8MaxWasmModuleSize <= kMaxInt);
        size_t split_point =
            1 + rng->NextInt(static_cast<int>(range.size() - 1));
        // Insert first sub-range *before* {it} and make {it} point after it.
        it = ranges.insert(it, range.SubVector(0, split_point)) + 1;
        *it = range.SubVectorFrom(split_point);
      }
    }
    for (auto range : ranges) {
      streaming_decoder->OnBytesReceived(range);
    }
    streaming_decoder->Finish();
    return;
  }
  // Make a copy of the wire bytes in case the user program changes them
  // during asynchronous compilation.
  base::OwnedVector<const uint8_t> copy =
      base::OwnedVector<const uint8_t>::Of(bytes.module_bytes());

  AsyncCompileJob* job = CreateAsyncCompileJob(
      isolate, enabled, std::move(compile_imports), std::move(copy),
      isolate->native_context(), api_method_name_for_errors,
      std::move(resolver), compilation_id);
  job->Start();
}

std::shared_ptr<StreamingDecoder> WasmEngine::StartStreamingCompilation(
    Isolate* isolate, WasmEnabledFeatures enabled,
    CompileTimeImports compile_imports, Handle<Context> context,
    const char* api_method_name,
    std::shared_ptr<CompilationResultResolver> resolver) {
  int compilation_id = next_compilation_id_.fetch_add(1);
  TRACE_EVENT1("v8.wasm", "wasm.StartStreamingCompilation", "id",
               compilation_id);
  if (v8_flags.wasm_async_compilation) {
    AsyncCompileJob* job = CreateAsyncCompileJob(
        isolate, enabled, std::move(compile_imports), {}, context,
        api_method_name, std::move(resolver), compilation_id);
    return job->CreateStreamingDecoder();
  }
  return StreamingDecoder::CreateSyncStreamingDecoder(
      isolate, enabled, std::move(compile_imports), context, api_method_name,
      std::move(resolver));
}

void WasmEngine::CompileFunction(Counters* counters,
                                 NativeModule* native_module,
                                 uint32_t function_index, ExecutionTier tier) {
  DCHECK(!v8_flags.wasm_jitless);

  // Note we assume that "one-off" compilations can discard detected features.
  WasmDetectedFeatures detected;
  WasmCompilationUnit::CompileWasmFunction(
      counters, native_module, &detected,
      &native_module->module()->functions[function_index], tier);
}

void WasmEngine::EnterDebuggingForIsolate(Isolate* isolate) {
  if (v8_flags.wasm_jitless) return;

  std::vector<std::shared_ptr<NativeModule>> native_modules;
  // {mutex_} gets taken both here and in {RemoveCompiledCode} in
  // {AddPotentiallyDeadCode}. Therefore {RemoveCompiledCode} has to be
  // called outside the lock.
  {
    base::MutexGuard lock(&mutex_);
    if (isolates_[isolate]->keep_in_debug_state) return;
    isolates_[isolate]->keep_in_debug_state = true;
    for (auto* native_module : isolates_[isolate]->native_modules) {
      DCHECK_EQ(1, native_modules_.count(native_module));
      if (auto shared_ptr = native_modules_[native_module]->weak_ptr.lock()) {
        native_modules.emplace_back(std::move(shared_ptr));
      }
      native_module->SetDebugState(kDebugging);
    }
  }
  WasmCodeRefScope ref_scope;
  for (auto& native_module : native_modules) {
    native_module->RemoveCompiledCode(
        NativeModule::RemoveFilter::kRemoveNonDebugCode);
  }
}

void WasmEngine::LeaveDebuggingForIsolate(Isolate* isolate) {
  // Only trigger recompilation after releasing the mutex, otherwise we risk
  // deadlocks because of lock inversion. The bool tells whether the module
  // needs recompilation for tier up.
  std::vector<std::pair<std::shared_ptr<NativeModule>, bool>> native_modules;
  {
    base::MutexGuard lock(&mutex_);
    isolates_[isolate]->keep_in_debug_state = false;
    auto can_remove_debug_code = [this](NativeModule* native_module) {
      DCHECK_EQ(1, native_modules_.count(native_module));
      for (auto* isolate : native_modules_[native_module]->isolates) {
        DCHECK_EQ(1, isolates_.count(isolate));
        if (isolates_[isolate]->keep_in_debug_state) return false;
      }
      return true;
    };
    for (auto* native_module : isolates_[isolate]->native_modules) {
      DCHECK_EQ(1, native_modules_.count(native_module));
      auto shared_ptr = native_modules_[native_module]->weak_ptr.lock();
      if (!shared_ptr) continue;  // The module is not used any more.
      if (!native_module->IsInDebugState()) continue;
      // Only start tier-up if no other isolate needs this module in tiered
      // down state.
      bool remove_debug_code = can_remove_debug_code(native_module);
      if (remove_debug_code) native_module->SetDebugState(kNotDebugging);
      native_modules.emplace_back(std::move(shared_ptr), remove_debug_code);
    }
  }
  for (auto& entry : native_modules) {
    auto& native_module = entry.first;
    bool remove_debug_code = entry.second;
    // Remove all breakpoints set by this isolate.
    if (native_module->HasDebugInfo()) {
      native_module->GetDebugInfo()->RemoveIsolate(isolate);
    }
    if (remove_debug_code) {
      WasmCodeRefScope ref_scope;
      native_module->RemoveCompiledCode(
          NativeModule::RemoveFilter::kRemoveDebugCode);
    }
  }
}

namespace {
Handle<Script> CreateWasmScript(Isolate* isolate,
                                std::shared_ptr<NativeModule> native_module,
                                base::Vector<const char> source_url) {
  base::Vector<const uint8_t> wire_bytes = native_module->wire_bytes();

  // The source URL of the script is
  // - the original source URL if available (from the streaming API),
  // - wasm://wasm/<module name>-<hash> if a module name has been set, or
  // - wasm://wasm/<hash> otherwise.
  const WasmModule* module = native_module->module();
  Handle<String> url_str;
  if (!source_url.empty()) {
    url_str = isolate->factory()
                  ->NewStringFromUtf8(source_url, AllocationType::kOld)
                  .ToHandleChecked();
  } else {
    // Limit the printed hash to 8 characters.
    uint32_t hash = static_cast<uint32_t>(GetWireBytesHash(wire_bytes));
    base::EmbeddedVector<char, 32> buffer;
    if (module->name.is_empty()) {
      // Build the URL in the form "wasm://wasm/<hash>".
      int url_len = SNPrintF(buffer, "wasm://wasm/%08x", hash);
      DCHECK(url_len >= 0 && url_len < buffer.length());
      url_str = isolate->factory()
                    ->NewStringFromUtf8(buffer.SubVector(0, url_len),
                                        AllocationType::kOld)
                    .ToHandleChecked();
    } else {
      // Build the URL in the form "wasm://wasm/<module name>-<hash>".
      int hash_len = SNPrintF(buffer, "-%08x", hash);
      DCHECK(hash_len >= 0 && hash_len < buffer.length());
      Handle<String> prefix =
          isolate->factory()->NewStringFromStaticChars("wasm://wasm/");
      Handle<String> module_name =
          WasmModuleObject::ExtractUtf8StringFromModuleBytes(
              isolate, wire_bytes, module->name, kNoInternalize);
      Handle<String> hash_str =
          isolate->factory()
              ->NewStringFromUtf8(buffer.SubVector(0, hash_len))
              .ToHandleChecked();
      // Concatenate the three parts.
      url_str = isolate->factory()
                    ->NewConsString(prefix, module_name)
                    .ToHandleChecked();
      url_str = isolate->factory()
                    ->NewConsString(url_str, hash_str)
                    .ToHandleChecked();
    }
  }
  DirectHandle<PrimitiveHeapObject> source_map_url =
      isolate->factory()->undefined_value();
  const WasmDebugSymbols& debug_symbols = module->debug_symbols;
  if (debug_symbols.type == WasmDebugSymbols::Type::SourceMap &&
      !debug_symbols.external_url.is_empty()) {
    base::Vector<const char> external_url =
        ModuleWireBytes(wire_bytes).GetNameOrNull(debug_symbols.external_url);
    MaybeHandle<String> src_map_str = isolate->factory()->NewStringFromUtf8(
        external_url, AllocationType::kOld);
    source_map_url = src_map_str.ToHandleChecked();
  }

  // Use the given shared {NativeModule}, but increase its reference count by
  // allocating a new {Managed<T>} that the {Script} references.
  size_t code_size_estimate = native_module->committed_code_space();
  size_t memory_estimate =
      code_size_estimate +
      wasm::WasmCodeManager::EstimateNativeModuleMetaDataSize(module);
  DirectHandle<Managed<wasm::NativeModule>> managed_native_module =
      Managed<wasm::NativeModule>::From(isolate, memory_estimate,
                                        std::move(native_module));

  Handle<Script> script =
      isolate->factory()->NewScript(isolate->factory()->undefined_value());
  {
    DisallowGarbageCollection no_gc;
    Tagged<Script> raw_script = *script;
    raw_script->set_compilation_state(Script::CompilationState::kCompiled);
    raw_script->set_context_data(isolate->native_context()->debug_context_id());
    raw_script->set_name(*url_str);
    raw_script->set_type(Script::Type::kWasm);
    raw_script->set_source_mapping_url(*source_map_url);
    raw_script->set_line_ends(ReadOnlyRoots(isolate).empty_fixed_array(),
                              SKIP_WRITE_BARRIER);
    raw_script->set_wasm_managed_native_module(*managed_native_module);
    raw_script->set_wasm_breakpoint_infos(
        ReadOnlyRoots(isolate).empty_fixed_array(), SKIP_WRITE_BARRIER);
    raw_script->set_wasm_weak_instance_list(
        ReadOnlyRoots(isolate).empty_weak_array_list(), SKIP_WRITE_BARRIER);

    // For correct exception handling (in particular, the onunhandledrejection
    // callback), we must set the origin options from the nearest calling JS
    // frame.
    // Considering all Wasm modules as shared across origins isn't a privacy
    // issue, because in order to instantiate and use them, a site needs to
    // already have access to their wire bytes anyway.
    static constexpr bool kIsSharedCrossOrigin = true;
    static constexpr bool kIsOpaque = false;
    static constexpr bool kIsWasm = true;
    static constexpr bool kIsModule = false;
    raw_script->set_origin_options(ScriptOriginOptions(
        kIsSharedCrossOrigin, kIsOpaque, kIsWasm, kIsModule));
  }

  return script;
}
}  // namespace

Handle<WasmModuleObject> WasmEngine::ImportNativeModule(
    Isolate* isolate, std::shared_ptr<NativeModule> shared_native_module,
    base::Vector<const char> source_url) {
  NativeModule* native_module = shared_native_module.get();
  ModuleWireBytes wire_bytes(native_module->wire_bytes());
  DirectHandle<Script> script =
      GetOrCreateScript(isolate, shared_native_module, source_url);
  native_module->LogWasmCodes(isolate, *script);
  Handle<WasmModuleObject> module_object =
      WasmModuleObject::New(isolate, std::move(shared_native_module), script);
  {
    base::MutexGuard lock(&mutex_);
    DCHECK_EQ(1, isolates_.count(isolate));
    IsolateInfo* isolate_info = isolates_.find(isolate)->second.get();
    isolate_info->native_modules.insert(native_module);
    DCHECK_EQ(1, native_modules_.count(native_module));
    native_modules_[native_module]->isolates.insert(isolate);
    if (isolate_info->log_codes) native_module->EnableCodeLogging();
  }

  // Finish the Wasm script now and make it public to the debugger.
  isolate->debug()->OnAfterCompile(script);
  return module_object;
}

std::pair<size_t, size_t> WasmEngine::FlushLiftoffCode() {
  WasmCodeRefScope ref_scope;
  base::MutexGuard guard(&mutex_);
  size_t removed_code_size = 0;
  size_t removed_metadata_size = 0;
  for (auto& [native_module, info] : native_modules_) {
    auto [code_size, metadata_size] = native_module->RemoveCompiledCode(
        NativeModule::RemoveFilter::kRemoveLiftoffCode);
    removed_code_size += code_size;
    removed_metadata_size += metadata_size;
  }
  return {removed_code_size, removed_metadata_size};
}

size_t WasmEngine::GetLiftoffCodeSizeForTesting() {
  base::MutexGuard guard(&mutex_);
  size_t codesize_liftoff = 0;
  for (auto& [native_module, info] : native_modules_) {
    codesize_liftoff += native_module->SumLiftoffCodeSizeForTesting();
  }
  return codesize_liftoff;
}

std::shared_ptr<CompilationStatistics>
WasmEngine::GetOrCreateTurboStatistics() {
  base::MutexGuard guard(&mutex_);
  if (compilation_stats_ == nullptr) {
    compilation_stats_.reset(new CompilationStatistics());
  }
  return compilation_stats_;
}

void WasmEngine::DumpAndResetTurboStatistics() {
  base::MutexGuard guard(&mutex_);
  if (compilation_stats_ != nullptr) {
    StdoutStream os;
    os << AsPrintableStatistics{"Turbofan Wasm", *compilation_stats_, false}
       << std::endl;
  }
  compilation_stats_.reset();
}

void WasmEngine::DumpTurboStatistics() {
  base::MutexGuard guard(&mutex_);
  if (compilation_stats_ != nullptr) {
    StdoutStream os;
    os << AsPrintableStatistics{"Turbofan Wasm", *compilation_stats_, false}
       << std::endl;
  }
}

CodeTracer* WasmEngine::GetCodeTracer() {
  base::MutexGuard guard(&mutex_);
  if (code_tracer_ == nullptr) code_tracer_.reset(new CodeTracer(-1));
  return code_tracer_.get();
}

AsyncCompileJob* WasmEngine::CreateAsyncCompileJob(
    Isolate* isolate, WasmEnabledFeatures enabled,
    CompileTimeImports compile_imports, base::OwnedVector<const uint8_t> bytes,
    DirectHandle<Context> context, const char* api_method_name,
    std::shared_ptr<CompilationResultResolver> resolver, int compilation_id) {
  DirectHandle<NativeContext> incumbent_context =
      isolate->GetIncumbentContext();
  AsyncCompileJob* job = new AsyncCompileJob(
      isolate, enabled, std::move(compile_imports), std::move(bytes), context,
      incumbent_context, api_method_name, std::move(resolver), compilation_id);
  // Pass ownership to the unique_ptr in {async_compile_jobs_}.
  base::MutexGuard guard(&mutex_);
  async_compile_jobs_[job] = std::unique_ptr<AsyncCompileJob>(job);
  return job;
}

std::unique_ptr<AsyncCompileJob> WasmEngine::RemoveCompileJob(
    AsyncCompileJob* job) {
  base::MutexGuard guard(&mutex_);
  auto item = async_compile_jobs_.find(job);
  DCHECK(item != async_compile_jobs_.end());
  std::unique_ptr<AsyncCompileJob> result = std::move(item->second);
  async_compile_jobs_.erase(item);
  return result;
}

bool WasmEngine::HasRunningCompileJob(Isolate* isolate) {
  base::MutexGuard guard(&mutex_);
  DCHECK_EQ(1, isolates_.count(isolate));
  for (auto& entry : async_compile_jobs_) {
    if (entry.first->isolate() == isolate) return true;
  }
  return false;
}

void WasmEngine::DeleteCompileJobsOnContext(Handle<Context> context) {
  // Under the mutex get all jobs to delete. Then delete them without holding
  // the mutex, such that deletion can reenter the WasmEngine.
  std::vector<std::unique_ptr<AsyncCompileJob>> jobs_to_delete;
  {
    base::MutexGuard guard(&mutex_);
    for (auto it = async_compile_jobs_.begin();
         it != async_compile_jobs_.end();) {
      if (!it->first->context().is_identical_to(context)) {
        ++it;
        continue;
      }
      jobs_to_delete.push_back(std::move(it->second));
      it = async_compile_jobs_.erase(it);
    }
  }
}

void WasmEngine::DeleteCompileJobsOnIsolate(Isolate* isolate) {
  // Under the mutex get all jobs to delete. Then delete them without holding
  // the mutex, such that deletion can reenter the WasmEngine.
  std::vector<std::unique_ptr<AsyncCompileJob>> jobs_to_delete;
  std::vector<std::weak_ptr<NativeModule>> modules_in_isolate;
  std::shared_ptr<OperationsBarrier> wrapper_compilation_barrier;
  {
    base::MutexGuard guard(&mutex_);
    for (auto it = async_compile_jobs_.begin();
         it != async_compile_jobs_.end();) {
      if (it->first->isolate() != isolate) {
        ++it;
        continue;
      }
      jobs_to_delete.push_back(std::move(it->second));
      it = async_compile_jobs_.erase(it);
    }
    DCHECK_EQ(1, isolates_.count(isolate));
    auto* isolate_info = isolates_[isolate].get();
    wrapper_compilation_barrier = isolate_info->wrapper_compilation_barrier_;
    for (auto* native_module : isolate_info->native_modules) {
      DCHECK_EQ(1, native_modules_.count(native_module));
      modules_in_isolate.emplace_back(native_modules_[native_module]->weak_ptr);
    }
  }

  // All modules that have not finished initial compilation yet cannot be
  // shared with other isolates. Hence we cancel their compilation. In
  // particular, this will cancel wrapper compilation which is bound to this
  // isolate (this would be a UAF otherwise).
  for (auto& weak_module : modules_in_isolate) {
    if (auto shared_module = weak_module.lock()) {
      shared_module->compilation_state()->CancelInitialCompilation();
    }
  }

  // After cancelling, wait for all current wrapper compilation to actually
  // finish.
  wrapper_compilation_barrier->CancelAndWait();
}

OperationsBarrier::Token WasmEngine::StartWrapperCompilation(Isolate* isolate) {
  base::MutexGuard guard(&mutex_);
  auto isolate_info_it = isolates_.find(isolate);
  if (isolate_info_it == isolates_.end()) return {};
  return isolate_info_it->second->wrapper_compilation_barrier_->TryLock();
}

void WasmEngine::AddIsolate(Isolate* isolate) {
  // Create the IsolateInfo.
  {
    // Create the IsolateInfo outside the mutex to reduce the size of the
    // critical section and to avoid lock-order-inversion issues.
    auto isolate_info = std::make_unique<IsolateInfo>(isolate);
    base::MutexGuard guard(&mutex_);
    DCHECK_EQ(0, isolates_.count(isolate));
    isolates_.emplace(isolate, std::move(isolate_info));
  }

  // Install sampling GC callback.
  // TODO(v8:7424): For now we sample module sizes in a GC callback. This will
  // bias samples towards apps with high memory pressure. We should switch to
  // using sampling based on regular intervals independent of the GC.
  auto callback = [](v8::Isolate* v8_isolate, v8::GCType type,
                     v8::GCCallbackFlags flags, void* data) {
    Isolate* isolate = reinterpret_cast<Isolate*>(v8_isolate);
    Counters* counters = isolate->counters();
    WasmEngine* engine = GetWasmEngine();
    {
      base::MutexGuard lock(&engine->mutex_);
      DCHECK_EQ(1, engine->isolates_.count(isolate));
      for (auto* native_module : engine->isolates_[isolate]->native_modules) {
        native_module->SampleCodeSize(counters);
      }
    }
    // Also sample overall metadata size (this includes the metadata size of
    // individual NativeModules; we are summing that up twice, which could be
    // improved performance-wise).
    // The engine-wide metadata also includes global storage e.g. for the type
    // canonicalizer.
    Histogram* metadata_histogram = counters->wasm_engine_metadata_size_kb();
    if (metadata_histogram->Enabled()) {
      size_t engine_meta_data = engine->EstimateCurrentMemoryConsumption();
      metadata_histogram->AddSample(static_cast<int>(engine_meta_data / KB));
    }
  };
  isolate->heap()->AddGCEpilogueCallback(callback, v8::kGCTypeMarkSweepCompact,
                                         nullptr);

#ifdef V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING
  if (gdb_server_) {
    gdb_server_->AddIsolate(isolate);
  }
#endif  // V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING
}

void WasmEngine::RemoveIsolate(Isolate* isolate) {
#ifdef V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING
  if (gdb_server_) {
    gdb_server_->RemoveIsolate(isolate);
  }
#endif  // V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING

  // Keep a WasmCodeRefScope which dies after the {mutex_} is released, to avoid
  // deadlock when code actually dies, as that requires taking the {mutex_}.
  // Also, keep the NativeModules themselves alive. The isolate is shutting
  // down, so the heap will not do that any more.
  std::map<NativeModule*, std::shared_ptr<NativeModule>>
      native_modules_with_code_to_log;
  WasmCodeRefScope code_ref_scope_for_dead_code;

  base::MutexGuard guard(&mutex_);

  // Lookup the IsolateInfo; do not remove it yet (that happens below).
  auto isolates_it = isolates_.find(isolate);
  DCHECK_NE(isolates_.end(), isolates_it);
  IsolateInfo* isolate_info = isolates_it->second.get();

  // Remove the isolate from the per-native-module info, and other cleanup.
  for (auto* native_module : isolate_info->native_modules) {
    DCHECK_EQ(1, native_modules_.count(native_module));
    NativeModuleInfo* native_module_info =
        native_modules_.find(native_module)->second.get();

    // Check that the {NativeModule::log_code_} field has the expected value,
    // and update if the dying isolate was the last one with code logging
    // enabled.
    auto has_isolate_with_code_logging = [this, native_module_info] {
      return std::any_of(native_module_info->isolates.begin(),
                         native_module_info->isolates.end(),
                         [this](Isolate* isolate) {
                           return isolates_.find(isolate)->second->log_codes;
                         });
    };
    DCHECK_EQ(native_module->log_code(), has_isolate_with_code_logging());
    DCHECK_EQ(1, native_module_info->isolates.count(isolate));
    native_module_info->isolates.erase(isolate);
    if (!has_isolate_with_code_logging()) {
      native_module->DisableCodeLogging();
    }

    // Remove any debug code and other info for this isolate.
    if (native_module->HasDebugInfo()) {
      native_module->GetDebugInfo()->RemoveIsolate(isolate);
    }
  }

  // Abort any outstanding GC.
  if (current_gc_info_) {
    if (RemoveIsolateFromCurrentGC(isolate)) PotentiallyFinishCurrentGC();
  }

  // Clear the {code_to_log} vector.
  for (auto& [script_id, code_to_log] : isolate_info->code_to_log) {
    for (WasmCode* code : code_to_log.code) {
      if (!native_modules_with_code_to_log.count(code->native_module())) {
        std::shared_ptr<NativeModule> shared_native_module =
            native_modules_[code->native_module()]->weak_ptr.lock();
        if (!shared_native_module) {
          // The module is dying already; there's no need to decrement the ref
          // count and add the code to the WasmCodeRefScope.
          continue;
        }
        native_modules_with_code_to_log.insert(std::make_pair(
            code->native_module(), std::move(shared_native_module)));
      }
      // Keep a reference in the {code_ref_scope_for_dead_code} such that the
      // code cannot become dead immediately.
      WasmCodeRefScope::AddRef(code);
      code->DecRefOnLiveCode();
    }
  }
  isolate_info->code_to_log.clear();

  // Finally remove the {IsolateInfo} for this isolate.
  isolates_.erase(isolates_it);
}

void WasmEngine::LogCode(base::Vector<WasmCode*> code_vec) {
  if (code_vec.empty()) return;
  NativeModule* native_module = code_vec[0]->native_module();
  if (!native_module->log_code()) return;
  using TaskToSchedule =
      std::pair<std::shared_ptr<v8::TaskRunner>, std::unique_ptr<LogCodesTask>>;
  std::vector<TaskToSchedule> to_schedule;
  {
    base::MutexGuard guard(&mutex_);
    DCHECK_EQ(1, native_modules_.count(native_module));
    NativeModuleInfo* native_module_info =
        native_modules_.find(native_module)->second.get();
    for (Isolate* isolate : native_module_info->isolates) {
      DCHECK_EQ(1, isolates_.count(isolate));
      IsolateInfo* info = isolates_[isolate].get();
      if (info->log_codes == false) continue;

      auto script_it = info->scripts.find(native_module);
      // If the script does not yet exist, logging will happen later. If the
      // weak handle is cleared already, we also don't need to log any more.
      if (script_it == info->scripts.end()) continue;

      // If there is no code scheduled to be logged already in that isolate,
      // then schedule a new task and also set an interrupt to log the newly
      // added code as soon as possible.
      if (info->code_to_log.empty()) {
        isolate->stack_guard()->RequestLogWasmCode();
        to_schedule.emplace_back(info->foreground_task_runner,
                                 std::make_unique<LogCodesTask>(isolate));
      }

      WeakScriptHandle& weak_script_handle = script_it->second;
      auto& log_entry = info->code_to_log[weak_script_handle.script_id()];
      if (!log_entry.source_url) {
        log_entry.source_url = weak_script_handle.source_url();
      }
      log_entry.code.insert(log_entry.code.end(), code_vec.begin(),
                            code_vec.end());

      // Increment the reference count for the added {log_entry.code} entries.
      for (WasmCode* code : code_vec) {
        DCHECK_EQ(native_module, code->native_module());
        code->IncRef();
      }
    }
  }
  for (auto& [runner, task] : to_schedule) {
    runner->PostTask(std::move(task));
  }
}

void WasmEngine::EnableCodeLogging(Isolate* isolate) {
  base::MutexGuard guard(&mutex_);
  auto it = isolates_.find(isolate);
  DCHECK_NE(isolates_.end(), it);
  IsolateInfo* info = it->second.get();
  info->log_codes = true;
  // Also set {NativeModule::log_code_} for all native modules currently used by
  // this isolate.
  for (NativeModule* native_module : info->native_modules) {
    native_module->EnableCodeLogging();
  }
}

void WasmEngine::LogOutstandingCodesForIsolate(Isolate* isolate) {
  // Under the mutex, get the vector of wasm code to log. Then log and decrement
  // the ref count without holding the mutex.
  std::unordered_map<int, IsolateInfo::CodeToLogPerScript> code_to_log;
  {
    base::MutexGuard guard(&mutex_);
    DCHECK_EQ(1, isolates_.count(isolate));
    code_to_log.swap(isolates_[isolate]->code_to_log);
  }

  // Check again whether we still need to log code.
  bool should_log = WasmCode::ShouldBeLogged(isolate);

  TRACE_EVENT0("v8.wasm", "wasm.LogCode");
  for (auto& [script_id, code_to_log] : code_to_log) {
    for (WasmCode* code : code_to_log.code) {
      if (should_log) {
        const char* source_url = code_to_log.source_url.get();
        // The source URL can be empty for eval()'ed scripts.
        if (!source_url) source_url = "";
        code->LogCode(isolate, source_url, script_id);
      }
    }
    WasmCode::DecrementRefCount(base::VectorOf(code_to_log.code));
  }
}

std::shared_ptr<NativeModule> WasmEngine::NewNativeModule(
    Isolate* isolate, WasmEnabledFeatures enabled,
    CompileTimeImports compile_imports,
    std::shared_ptr<const WasmModule> module, size_t code_size_estimate) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.wasm.detailed"),
               "wasm.NewNativeModule");
#ifdef V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING
  if (v8_flags.wasm_gdb_remote && !gdb_server_) {
    gdb_server_ = gdb_server::GdbServer::Create();
    gdb_server_->AddIsolate(isolate);
  }
#endif  // V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING

  std::shared_ptr<NativeModule> native_module =
      GetWasmCodeManager()->NewNativeModule(
          isolate, enabled, std::move(compile_imports), code_size_estimate,
          std::move(module));
  base::MutexGuard lock(&mutex_);
  if (V8_UNLIKELY(v8_flags.experimental_wasm_pgo_to_file)) {
    if (!native_modules_kept_alive_for_pgo) {
      native_modules_kept_alive_for_pgo =
          new std::vector<std::shared_ptr<NativeModule>>;
    }
    native_modules_kept_alive_for_pgo->emplace_back(native_module);
  }
  auto [iterator, inserted] = native_modules_.insert(std::make_pair(
      native_module.get(), std::make_unique<NativeModuleInfo>(native_module)));
  DCHECK(inserted);
  NativeModuleInfo* native_module_info = iterator->second.get();
  native_module_info->isolates.insert(isolate);
  DCHECK_EQ(1, isolates_.count(isolate));
  IsolateInfo* isolate_info = isolates_.find(isolate)->second.get();
  isolate_info->native_modules.insert(native_module.get());
  if (isolate_info->keep_in_debug_state) {
    native_module->SetDebugState(kDebugging);
  }
  if (isolate_info->log_codes) {
    native_module->EnableCodeLogging();
  }

  // Record memory protection key support.
  if (!isolate_info->pku_support_sampled) {
    isolate_info->pku_support_sampled = true;
    auto* histogram =
        isolate->counters()->wasm_memory_protection_keys_support();
    bool has_mpk = WasmCodeManager::HasMemoryProtectionKeySupport();
    histogram->AddSample(has_mpk ? 1 : 0);
  }

  isolate->counters()->wasm_modules_per_isolate()->AddSample(
      static_cast<int>(isolate_info->native_modules.size()));
  isolate->counters()->wasm_modules_per_engine()->AddSample(
      static_cast<int>(native_modules_.size()));
  return native_module;
}

std::shared_ptr<NativeModule> WasmEngine::MaybeGetNativeModule(
    ModuleOrigin origin, base::Vector<const uint8_t> wire_bytes,
    const CompileTimeImports& compile_imports, Isolate* isolate) {
  TRACE_EVENT1("v8.wasm", "wasm.GetNativeModuleFromCache", "wire_bytes",
               wire_bytes.size());
  std::shared_ptr<NativeModule> native_module =
      native_module_cache_.MaybeGetNativeModule(origin, wire_bytes,
                                                compile_imports);
  bool remove_all_code = false;
  if (native_module) {
    TRACE_EVENT0("v8.wasm", "CacheHit");
    base::MutexGuard guard(&mutex_);
    auto& native_module_info = native_modules_[native_module.get()];
    if (!native_module_info) {
      native_module_info = std::make_unique<NativeModuleInfo>(native_module);
    }
    native_module_info->isolates.insert(isolate);
    auto* isolate_data = isolates_[isolate].get();
    isolate_data->native_modules.insert(native_module.get());
    if (isolate_data->keep_in_debug_state && !native_module->IsInDebugState()) {
      remove_all_code = true;
      native_module->SetDebugState(kDebugging);
    }
    if (isolate_data->log_codes && !native_module->log_code()) {
      native_module->EnableCodeLogging();
    }
  }
  if (remove_all_code) {
    WasmCodeRefScope ref_scope;
    native_module->RemoveCompiledCode(
        NativeModule::RemoveFilter::kRemoveNonDebugCode);
  }
  return native_module;
}

std::shared_ptr<NativeModule> WasmEngine::UpdateNativeModuleCache(
    bool has_error, std::shared_ptr<NativeModule> native_module,
    Isolate* isolate) {
  // Keep the previous pointer, but as a `void*`, because we only want to use it
  // later to compare pointers, and never need to dereference it.
  void* prev = native_module.get();
  native_module =
      native_module_cache_.Update(std::move(native_module), has_error);
  if (prev == native_module.get()) return native_module;
  bool remove_all_code = false;
  {
    base::MutexGuard guard(&mutex_);
    DCHECK_EQ(1, native_modules_.count(native_module.get()));
    native_modules_[native_module.get()]->isolates.insert(isolate);
    DCHECK_EQ(1, isolates_.count(isolate));
    auto* isolate_data = isolates_[isolate].get();
    isolate_data->native_modules.insert(native_module.get());
    if (isolate_data->keep_in_debug_state && !native_module->IsInDebugState()) {
      remove_all_code = true;
      native_module->SetDebugState(kDebugging);
    }
    if (isolate_data->log_codes && !native_module->log_code()) {
      native_module->EnableCodeLogging();
    }
  }
  if (remove_all_code) {
    WasmCodeRefScope ref_scope;
    native_module->RemoveCompiledCode(
        NativeModule::RemoveFilter::kRemoveNonDebugCode);
  }
  return native_module;
}

bool WasmEngine::GetStreamingCompilationOwnership(
    size_t prefix_hash, const CompileTimeImports& compile_imports) {
  TRACE_EVENT0("v8.wasm", "wasm.GetStreamingCompilationOwnership");
  if (native_module_cache_.GetStreamingCompilationOwnership(prefix_hash,
                                                            compile_imports)) {
    return true;
  }
  // This is only a marker, not for tracing execution time. There should be a
  // later "wasm.GetNativeModuleFromCache" event for trying to get the module
  // from the cache.
  TRACE_EVENT0("v8.wasm", "CacheHit");
  return false;
}

void WasmEngine::StreamingCompilationFailed(
    size_t prefix_hash, const CompileTimeImports& compile_imports) {
  native_module_cache_.StreamingCompilationFailed(prefix_hash, compile_imports);
}

void WasmEngine::FreeNativeModule(NativeModule* native_module) {
  base::MutexGuard guard(&mutex_);
  auto module = native_modules_.find(native_module);
  DCHECK_NE(native_modules_.end(), module);
  auto part_of_native_module = [native_module](WasmCode* code) {
    return code->native_module() == native_module;
  };
  for (Isolate* isolate : module->second->isolates) {
    DCHECK_EQ(1, isolates_.count(isolate));
    IsolateInfo* info = isolates_[isolate].get();
    DCHECK_EQ(1, info->native_modules.count(native_module));
    info->native_modules.erase(native_module);
    info->scripts.erase(native_module);

    // Flush the Wasm code lookup cache, since it may refer to some
    // code within native modules that we are going to release (if a
    // Managed<wasm::NativeModule> object is no longer referenced).
    GetWasmCodeManager()->FlushCodeLookupCache(isolate);

    // If there are {WasmCode} objects of the deleted {NativeModule}
    // outstanding to be logged in this isolate, remove them. Decrementing the
    // ref count is not needed, since the {NativeModule} dies anyway.
    for (auto& log_entry : info->code_to_log) {
      std::vector<WasmCode*>& code = log_entry.second.code;
      auto new_end =
          std::remove_if(code.begin(), code.end(), part_of_native_module);
      code.erase(new_end, code.end());
    }
    // Now remove empty entries in {code_to_log}.
    for (auto it = info->code_to_log.begin(), end = info->code_to_log.end();
         it != end;) {
      if (it->second.code.empty()) {
        it = info->code_to_log.erase(it);
      } else {
        ++it;
      }
    }
  }
  // If there is a GC running which has references to code contained in the
  // deleted {NativeModule}, remove those references.
  if (current_gc_info_) {
    for (auto it = current_gc_info_->dead_code.begin(),
              end = current_gc_info_->dead_code.end();
         it != end;) {
      if ((*it)->native_module() == native_module) {
        it = current_gc_info_->dead_code.erase(it);
      } else {
        ++it;
      }
    }
    TRACE_CODE_GC("Native module %p died, reducing dead code objects to %zu.\n",
                  native_module, current_gc_info_->dead_code.size());
  }
  // If any code objects are currently tracked as dead or near-dead, remove
  // references belonging to the NativeModule that's being deleted.
  std::erase_if(dead_code_, part_of_native_module);
  std::erase_if(potentially_dead_code_, part_of_native_module);

  native_module_cache_.Erase(native_module);
  native_modules_.erase(module);
}

void WasmEngine::ReportLiveCodeForGC(Isolate* isolate,
                                     base::Vector<WasmCode*> live_code) {
  TRACE_EVENT0("v8.wasm", "wasm.ReportLiveCodeForGC");
  TRACE_CODE_GC("Isolate %d reporting %zu live code objects.\n", isolate->id(),
                live_code.size());
  base::MutexGuard guard(&mutex_);
  // This report might come in late (note that we trigger both a stack guard and
  // a foreground task). In that case, ignore it.
  if (current_gc_info_ == nullptr) return;
  if (!RemoveIsolateFromCurrentGC(isolate)) return;
  isolate->counters()->wasm_module_num_triggered_code_gcs()->AddSample(
      current_gc_info_->gc_sequence_index);
  for (WasmCode* code : live_code) current_gc_info_->dead_code.erase(code);
  PotentiallyFinishCurrentGC();
}

namespace {
void ReportLiveCodeFromFrameForGC(
    Isolate* isolate, StackFrame* frame,
    std::unordered_set<wasm::WasmCode*>& live_wasm_code) {
  if (frame->type() != StackFrame::WASM) return;
  live_wasm_code.insert(WasmFrame::cast(frame)->wasm_code());
#if V8_TARGET_ARCH_X64
    if (WasmFrame::cast(frame)->wasm_code()->for_debugging()) {
      Address osr_target = base::Memory<Address>(WasmFrame::cast(frame)->fp() -
                                                 kOSRTargetOffset);
      if (osr_target) {
        WasmCode* osr_code =
            GetWasmCodeManager()->LookupCode(isolate, osr_target);
        DCHECK_NOT_NULL(osr_code);
        live_wasm_code.insert(osr_code);
      }
    }
#endif
}
}  // namespace

void WasmEngine::ReportLiveCodeFromStackForGC(Isolate* isolate) {
  wasm::WasmCodeRefScope code_ref_scope;
  std::unordered_set<wasm::WasmCode*> live_wasm_code;

  for (const std::unique_ptr<StackMemory>& stack : isolate->wasm_stacks()) {
    if (stack->IsActive()) {
      // The active stack's jump buffer does not match the current state, use
      // the thread info below instead.
      continue;
    }
    for (StackFrameIterator it(isolate, stack.get()); !it.done();
         it.Advance()) {
      StackFrame* const frame = it.frame();
      ReportLiveCodeFromFrameForGC(isolate, frame, live_wasm_code);
    }
  }

  for (StackFrameIterator it(isolate); !it.done(); it.Advance()) {
    StackFrame* const frame = it.frame();
    ReportLiveCodeFromFrameForGC(isolate, frame, live_wasm_code);
  }

  CheckNoArchivedThreads(isolate);

  // Flush the code lookup cache, since it may refer to some code we
  // are going to release.
  GetWasmCodeManager()->FlushCodeLookupCache(isolate);

  ReportLiveCodeForGC(
      isolate, base::OwnedVector<WasmCode*>::Of(live_wasm_code).as_vector());
}

bool WasmEngine::AddPotentiallyDeadCode(WasmCode* code) {
  base::MutexGuard guard(&mutex_);
  if (dead_code_.contains(code)) return false;  // Code is already dead.
  auto added = potentially_dead_code_.insert(code);
  if (!added.second) return false;  // An entry already existed.
  new_potentially_dead_code_size_ += code->instructions().size();
  if (v8_flags.wasm_code_gc) {
    // Trigger a GC if 64kB plus 10% of committed code are potentially dead.
    size_t dead_code_limit =
        v8_flags.stress_wasm_code_gc
            ? 0
            : 64 * KB + GetWasmCodeManager()->committed_code_space() / 10;
    if (new_potentially_dead_code_size_ > dead_code_limit) {
      bool inc_gc_count =
          num_code_gcs_triggered_ < std::numeric_limits<int8_t>::max();
      if (current_gc_info_ == nullptr) {
        if (inc_gc_count) ++num_code_gcs_triggered_;
        TRACE_CODE_GC(
            "Triggering GC (potentially dead: %zu bytes; limit: %zu bytes).\n",
            new_potentially_dead_code_size_, dead_code_limit);
        TriggerGC(num_code_gcs_triggered_);
      } else if (current_gc_info_->next_gc_sequence_index == 0) {
        if (inc_gc_count) ++num_code_gcs_triggered_;
        TRACE_CODE_GC(
            "Scheduling another GC after the current one (potentially dead: "
            "%zu bytes; limit: %zu bytes).\n",
            new_potentially_dead_code_size_, dead_code_limit);
        current_gc_info_->next_gc_sequence_index = num_code_gcs_triggered_;
        DCHECK_NE(0, current_gc_info_->next_gc_sequence_index);
      }
    }
  }
  return true;
}

void WasmEngine::FreeDeadCode(const DeadCodeMap& dead_code) {
  base::MutexGuard guard(&mutex_);
  FreeDeadCodeLocked(dead_code);
}

void WasmEngine::FreeDeadCodeLocked(const DeadCodeMap& dead_code) {
  TRACE_EVENT0("v8.wasm", "wasm.FreeDeadCode");
  DCHECK(!mutex_.TryLock());
  for (auto& dead_code_entry : dead_code) {
    NativeModule* native_module = dead_code_entry.first;
    const std::vector<WasmCode*>& code_vec = dead_code_entry.second;
    DCHECK(native_modules_.contains(native_module));
    TRACE_CODE_GC("Freeing %zu code object%s of module %p.\n", code_vec.size(),
                  code_vec.size() == 1 ? "" : "s", native_module);
    for (WasmCode* code : code_vec) {
      DCHECK(dead_code_.contains(code));
      dead_code_.erase(code);
    }
    native_module->FreeCode(base::VectorOf(code_vec));
  }
}

Handle<Script> WasmEngine::GetOrCreateScript(
    Isolate* isolate, const std::shared_ptr<NativeModule>& native_module,
    base::Vector<const char> source_url) {
  {
    base::MutexGuard guard(&mutex_);
    DCHECK_EQ(1, isolates_.count(isolate));
    auto& scripts = isolates_[isolate]->scripts;
    auto it = scripts.find(native_module.get());
    if (it != scripts.end()) {
      Handle<Script> weak_global_handle = it->second.handle();
      if (weak_global_handle.is_null()) {
        scripts.erase(it);
      } else {
        return Handle<Script>::New(*weak_global_handle, isolate);
      }
    }
  }
  // Temporarily release the mutex to let the GC collect native modules.
  auto script = CreateWasmScript(isolate, native_module, source_url);
  {
    base::MutexGuard guard(&mutex_);
    DCHECK_EQ(1, isolates_.count(isolate));
    auto& scripts = isolates_[isolate]->scripts;
    DCHECK_EQ(0, scripts.count(native_module.get()));
    scripts.emplace(native_module.get(), WeakScriptHandle(script, isolate));
    return script;
  }
}

std::shared_ptr<OperationsBarrier>
WasmEngine::GetBarrierForBackgroundCompile() {
  return operations_barrier_;
}

void WasmEngine::TriggerGC(int8_t gc_sequence_index) {
  DCHECK(!mutex_.TryLock());
  DCHECK_NULL(current_gc_info_);
  DCHECK(v8_flags.wasm_code_gc);
  new_potentially_dead_code_size_ = 0;
  current_gc_info_.reset(new CurrentGCInfo(gc_sequence_index));
  // Add all potentially dead code to this GC, and trigger a GC task in each
  // known isolate. We can't limit the isolates to those that contributed
  // potentially-dead WasmCode objects, because wrappers don't point back
  // at a NativeModule or Isolate.
  for (WasmCode* code : potentially_dead_code_) {
    current_gc_info_->dead_code.insert(code);
  }
  for (const auto& entry : isolates_) {
    Isolate* isolate = entry.first;
    auto& gc_task = current_gc_info_->outstanding_isolates[isolate];
    if (!gc_task) {
      auto new_task = std::make_unique<WasmGCForegroundTask>(isolate);
      gc_task = new_task.get();
      DCHECK_EQ(1, isolates_.count(isolate));
      isolates_[isolate]->foreground_task_runner->PostTask(std::move(new_task));
    }
    isolate->stack_guard()->RequestWasmCodeGC();
  }
  TRACE_CODE_GC(
      "Starting GC (nr %d). Number of potentially dead code objects: %zu\n",
      current_gc_info_->gc_sequence_index, current_gc_info_->dead_code.size());
  // Ensure that there are outstanding isolates that will eventually finish this
  // GC. If there are no outstanding isolates, we finish the GC immediately.
  PotentiallyFinishCurrentGC();
  DCHECK(current_gc_info_ == nullptr ||
         !current_gc_info_->outstanding_isolates.empty());
}

bool WasmEngine::RemoveIsolateFromCurrentGC(Isolate* isolate) {
  DCHECK(!mutex_.TryLock());
  DCHECK_NOT_NULL(current_gc_info_);
  return current_gc_info_->outstanding_isolates.erase(isolate) != 0;
}

void WasmEngine::PotentiallyFinishCurrentGC() {
  DCHECK(!mutex_.TryLock());
  TRACE_CODE_GC(
      "Remaining dead code objects: %zu; outstanding isolates: %zu.\n",
      current_gc_info_->dead_code.size(),
      current_gc_info_->outstanding_isolates.size());

  // If there are more outstanding isolates, return immediately.
  if (!current_gc_info_->outstanding_isolates.empty()) return;

  // All remaining code in {current_gc_info->dead_code} is really dead.
  // Move it from the set of potentially dead code to the set of dead code,
  // and decrement its ref count.
  size_t num_freed = 0;
  DeadCodeMap dead_code;
  for (WasmCode* code : current_gc_info_->dead_code) {
    DCHECK(potentially_dead_code_.contains(code));
    potentially_dead_code_.erase(code);
    DCHECK(!dead_code_.contains(code));
    dead_code_.insert(code);
    if (code->DecRefOnDeadCode()) {
      dead_code[code->native_module()].push_back(code);
      ++num_freed;
    }
  }

  FreeDeadCodeLocked(dead_code);

  TRACE_CODE_GC("Found %zu dead code objects, freed %zu.\n",
                current_gc_info_->dead_code.size(), num_freed);
  USE(num_freed);

  int8_t next_gc_sequence_index = current_gc_info_->next_gc_sequence_index;
  current_gc_info_.reset();
  if (next_gc_sequence_index != 0) TriggerGC(next_gc_sequence_index);
}

size_t WasmEngine::EstimateCurrentMemoryConsumption() const {
  UPDATE_WHEN_CLASS_CHANGES(WasmEngine, 800);
  UPDATE_WHEN_CLASS_CHANGES(IsolateInfo, 184);
  UPDATE_WHEN_CLASS_CHANGES(NativeModuleInfo, 56);
  UPDATE_WHEN_CLASS_CHANGES(CurrentGCInfo, 96);
  size_t result = sizeof(WasmEngine);
  result += type_canonicalizer_.EstimateCurrentMemoryConsumption();
  {
    base::MutexGuard lock(&mutex_);
    result += ContentSize(async_compile_jobs_);
    result += async_compile_jobs_.size() * sizeof(AsyncCompileJob);
    result += ContentSize(potentially_dead_code_);
    result += ContentSize(dead_code_);

    // TODO(14106): Do we care about {compilation_stats_}?
    // TODO(14106): Do we care about {code_tracer_}?

    result += ContentSize(isolates_);
    result += isolates_.size() * sizeof(IsolateInfo);
    for (const auto& [isolate, isolate_info] : isolates_) {
      result += ContentSize(isolate_info->native_modules);
      result += ContentSize(isolate_info->scripts);
      result += ContentSize(isolate_info->code_to_log);
    }

    result += ContentSize(native_modules_);
    result += native_modules_.size() * sizeof(NativeModuleInfo);
    for (const auto& [native_module, native_module_info] : native_modules_) {
      result += native_module->EstimateCurrentMemoryConsumption();
      result += ContentSize(native_module_info->isolates);
    }

    if (current_gc_info_) {
      result += sizeof(CurrentGCInfo);
      result += ContentSize(current_gc_info_->outstanding_isolates);
      result += ContentSize(current_gc_info_->dead_code);
    }
  }
  if (v8_flags.trace_wasm_offheap_memory) {
    PrintF("WasmEngine: %zu\n", result);
  }
  return result;
}

int WasmEngine::GetDeoptsExecutedCount() const {
  return deopts_executed_.load(std::memory_order::relaxed);
}

int WasmEngine::IncrementDeoptsExecutedCount() {
  int previous_value = deopts_executed_.fetch_add(1, std::memory_order_relaxed);
  return previous_value + 1;
}

namespace {

struct GlobalWasmState {
  // Note: The order of fields is important here, as the WasmEngine's destructor
  // must run first. It contains a barrier which ensures that background threads
  // finished, and that has to happen before the WasmCodeManager gets destroyed.
  WasmCodeManager code_manager;
  WasmEngine engine;
};

GlobalWasmState* global_wasm_state = nullptr;

}  // namespace

// static
void WasmEngine::InitializeOncePerProcess() {
  DCHECK_NULL(global_wasm_state);
  global_wasm_state = new GlobalWasmState();

#ifdef V8_ENABLE_DRUMBRAKE
  if (v8_flags.wasm_jitless) {
    WasmInterpreter::InitializeOncePerProcess();
  }
#endif  // V8_ENABLE_DRUMBRAKE
}

// static
void WasmEngine::GlobalTearDown() {
#ifdef V8_ENABLE_DRUMBRAKE
  if (v8_flags.wasm_jitless) {
    WasmInterpreter::GlobalTearDown();
  }
#endif  // V8_ENABLE_DRUMBRAKE

  // Note: This can be called multiple times in a row (see
  // test-api/InitializeAndDisposeMultiple). This is fine, as
  // {global_wasm_engine} will be nullptr then.
  delete global_wasm_state;
  global_wasm_state = nullptr;
}

WasmEngine* GetWasmEngine() {
  DCHECK_NOT_NULL(global_wasm_state);
  return &global_wasm_state->engine;
}

WasmCodeManager* GetWasmCodeManager() {
  DCHECK_NOT_NULL(global_wasm_state);
  return &global_wasm_state->code_manager;
}

// {max_mem_pages} is declared in wasm-limits.h.
uint32_t max_mem32_pages() {
  static_assert(
      kV8MaxWasmMemory32Pages * kWasmPageSize <= JSArrayBuffer::kMaxByteLength,
      "Wasm memories must not be bigger than JSArrayBuffers");
  static_assert(kV8MaxWasmMemory32Pages <= kMaxUInt32);
  return std::min(uint32_t{kV8MaxWasmMemory32Pages},
                  v8_flags.wasm_max_mem_pages.value());
}

uint32_t max_mem64_pages() {
  static_assert(
      kV8MaxWasmMemory64Pages * kWasmPageSize <= JSArrayBuffer::kMaxByteLength,
      "Wasm memories must not be bigger than JSArrayBuffers");
  static_assert(kV8MaxWasmMemory64Pages <= kMaxUInt32);
  return std::min(uint32_t{kV8MaxWasmMemory64Pages},
                  v8_flags.wasm_max_mem_pages.value());
}

// {max_table_init_entries} is declared in wasm-limits.h.
uint32_t max_table_init_entries() {
  return std::min(uint32_t{kV8MaxWasmTableInitEntries},
                  v8_flags.wasm_max_table_size.value());
}

// {max_module_size} is declared in wasm-limits.h.
size_t max_module_size() {
  // Clamp the value of --wasm-max-module-size between 16 and the maximum
  // that the implementation supports.
  constexpr size_t kMin = 16;
  constexpr size_t kMax = kV8MaxWasmModuleSize;
  static_assert(kMin <= kV8MaxWasmModuleSize);
  return std::clamp(v8_flags.wasm_max_module_size.value(), kMin, kMax);
}

#undef TRACE_CODE_GC

}  // namespace v8::internal::wasm
