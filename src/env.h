// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

#ifndef SRC_ENV_H_
#define SRC_ENV_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "aliased_buffer.h"
#if HAVE_INSPECTOR
#include "inspector_agent.h"
#include "inspector_profiler.h"
#endif
#include "callback_queue.h"
#include "cleanup_queue-inl.h"
#include "compile_cache.h"
#include "debug_utils.h"
#include "env_properties.h"
#include "handle_wrap.h"
#include "node.h"
#include "node_binding.h"
#include "node_builtins.h"
#include "node_exit_code.h"
#include "node_main_instance.h"
#include "node_options.h"
#include "node_perf_common.h"
#include "node_realm.h"
#include "node_snapshotable.h"
#include "permission/permission.h"
#include "req_wrap.h"
#include "util.h"
#include "uv.h"
#include "v8.h"

#if HAVE_OPENSSL
#include <openssl/evp.h>
#endif

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace v8 {
class CppHeap;
}

namespace node {

namespace shadow_realm {
class ShadowRealm;
}
namespace contextify {
class ContextifyScript;
class CompiledFnEntry;
}

namespace performance {
class PerformanceState;
}

namespace tracing {
class AgentWriterHandle;
}

#if HAVE_INSPECTOR
namespace profiler {
class V8CoverageConnection;
class V8CpuProfilerConnection;
class V8HeapProfilerConnection;
}  // namespace profiler

namespace inspector {
class ParentInspectorHandle;
}
#endif  // HAVE_INSPECTOR

namespace worker {
class Worker;
}

namespace loader {
class ModuleWrap;
}  // namespace loader

class Environment;
class Realm;

// Disables zero-filling for ArrayBuffer allocations in this scope. This is
// similar to how we implement Buffer.allocUnsafe() in JS land.
class NoArrayBufferZeroFillScope {
 public:
  inline explicit NoArrayBufferZeroFillScope(IsolateData* isolate_data);
  inline ~NoArrayBufferZeroFillScope();

 private:
  NodeArrayBufferAllocator* node_allocator_;

  friend class Environment;
};

struct IsolateDataSerializeInfo {
  std::vector<SnapshotIndex> primitive_values;
  std::vector<PropInfo> template_values;

  friend std::ostream& operator<<(std::ostream& o,
                                  const IsolateDataSerializeInfo& i);
};

struct PerIsolateWrapperData {
  uint16_t cppgc_id;
  uint16_t non_cppgc_id;
};

class NODE_EXTERN_PRIVATE IsolateData : public MemoryRetainer {
 private:
  IsolateData(v8::Isolate* isolate,
              uv_loop_t* event_loop,
              MultiIsolatePlatform* platform,
              ArrayBufferAllocator* node_allocator,
              const SnapshotData* snapshot_data,
              std::shared_ptr<PerIsolateOptions> options);

 public:
  static IsolateData* CreateIsolateData(
      v8::Isolate* isolate,
      uv_loop_t* event_loop,
      MultiIsolatePlatform* platform = nullptr,
      ArrayBufferAllocator* node_allocator = nullptr,
      const EmbedderSnapshotData* embedder_snapshot_data = nullptr,
      std::shared_ptr<PerIsolateOptions> options = nullptr);
  ~IsolateData();

  SET_MEMORY_INFO_NAME(IsolateData)
  SET_SELF_SIZE(IsolateData)
  void MemoryInfo(MemoryTracker* tracker) const override;
  IsolateDataSerializeInfo Serialize(v8::SnapshotCreator* creator);

  bool is_building_snapshot() const { return snapshot_config_.has_value(); }
  const SnapshotConfig* snapshot_config() const {
    return snapshot_config_.has_value() ? &(snapshot_config_.value()) : nullptr;
  }
  void set_snapshot_config(const SnapshotConfig* config) {
    if (config != nullptr) {
      snapshot_config_ = *config;  // Copy the config.
    }
  }

  uint16_t* embedder_id_for_cppgc() const;
  uint16_t* embedder_id_for_non_cppgc() const;

  inline uv_loop_t* event_loop() const;
  inline MultiIsolatePlatform* platform() const;
  inline const SnapshotData* snapshot_data() const;
  inline std::shared_ptr<PerIsolateOptions> options();

  inline NodeArrayBufferAllocator* node_allocator() const;

  inline worker::Worker* worker_context() const;
  inline void set_worker_context(worker::Worker* context);

#define VP(PropertyName, StringValue) V(v8::Private, PropertyName)
#define VY(PropertyName, StringValue) V(v8::Symbol, PropertyName)
#define VS(PropertyName, StringValue) V(v8::String, PropertyName)
#define VR(PropertyName, TypeName) V(v8::Private, per_realm_##PropertyName)
#define V(TypeName, PropertyName)                                             \
  inline v8::Local<TypeName> PropertyName() const;
  PER_ISOLATE_PRIVATE_SYMBOL_PROPERTIES(VP)
  PER_ISOLATE_SYMBOL_PROPERTIES(VY)
  PER_ISOLATE_STRING_PROPERTIES(VS)
  PER_REALM_STRONG_PERSISTENT_VALUES(VR)
#undef V
#undef VR
#undef VY
#undef VS
#undef VP

#define VM(PropertyName) V(PropertyName##_binding_template, v8::ObjectTemplate)
#define V(PropertyName, TypeName)                                              \
  inline v8::Local<TypeName> PropertyName() const;                             \
  inline void set_##PropertyName(v8::Local<TypeName> value);
  PER_ISOLATE_TEMPLATE_PROPERTIES(V)
  NODE_BINDINGS_WITH_PER_ISOLATE_INIT(VM)
#undef V
#undef VM

  inline v8::Local<v8::String> async_wrap_provider(int index) const;

  size_t max_young_gen_size = 1;
  std::unordered_map<const char*, v8::Eternal<v8::String>> static_str_map;

  inline v8::Isolate* isolate() const;
  IsolateData(const IsolateData&) = delete;
  IsolateData& operator=(const IsolateData&) = delete;
  IsolateData(IsolateData&&) = delete;
  IsolateData& operator=(IsolateData&&) = delete;

 private:
  void DeserializeProperties(const IsolateDataSerializeInfo* isolate_data_info);
  void CreateProperties();

#define VP(PropertyName, StringValue) V(v8::Private, PropertyName)
#define VY(PropertyName, StringValue) V(v8::Symbol, PropertyName)
#define VS(PropertyName, StringValue) V(v8::String, PropertyName)
#define VR(PropertyName, TypeName) V(v8::Private, per_realm_##PropertyName)
#define VM(PropertyName) V(v8::ObjectTemplate, PropertyName##_binding_template)
#define VT(PropertyName, TypeName) V(TypeName, PropertyName)
#define V(TypeName, PropertyName)                                             \
  v8::Eternal<TypeName> PropertyName ## _;
  PER_ISOLATE_PRIVATE_SYMBOL_PROPERTIES(VP)
  PER_ISOLATE_SYMBOL_PROPERTIES(VY)
  PER_ISOLATE_STRING_PROPERTIES(VS)
  PER_ISOLATE_TEMPLATE_PROPERTIES(VT)
  PER_REALM_STRONG_PERSISTENT_VALUES(VR)
  NODE_BINDINGS_WITH_PER_ISOLATE_INIT(VM)
#undef V
#undef VM
#undef VR
#undef VT
#undef VS
#undef VY
#undef VP
  // Keep a list of all Persistent strings used for AsyncWrap Provider types.
  std::array<v8::Eternal<v8::String>, AsyncWrap::PROVIDERS_LENGTH>
      async_wrap_providers_;

  v8::Isolate* const isolate_;
  uv_loop_t* const event_loop_;
  NodeArrayBufferAllocator* const node_allocator_;
  MultiIsolatePlatform* platform_;

  const SnapshotData* snapshot_data_;
  std::optional<SnapshotConfig> snapshot_config_;

  std::unique_ptr<v8::CppHeap> cpp_heap_;
  std::shared_ptr<PerIsolateOptions> options_;
  worker::Worker* worker_context_ = nullptr;
  PerIsolateWrapperData* wrapper_data_;

  static Mutex isolate_data_mutex_;
  static std::unordered_map<uint16_t, std::unique_ptr<PerIsolateWrapperData>>
      wrapper_data_map_;
};

struct ContextInfo {
  explicit ContextInfo(const std::string& name) : name(name) {}
  const std::string name;
  std::string origin;
  bool is_default = false;
};

class EnabledDebugList;

namespace per_process {
extern std::shared_ptr<KVStore> system_environment;
}

struct EnvSerializeInfo;

class AsyncHooks : public MemoryRetainer {
 public:
  SET_MEMORY_INFO_NAME(AsyncHooks)
  SET_SELF_SIZE(AsyncHooks)
  void MemoryInfo(MemoryTracker* tracker) const override;

  // Reason for both UidFields and Fields are that one is stored as a double*
  // and the other as a uint32_t*.
  enum Fields {
    kInit,
    kBefore,
    kAfter,
    kDestroy,
    kPromiseResolve,
    kTotals,
    kCheck,
    kStackLength,
    kUsesExecutionAsyncResource,
    kFieldsCount,
  };

  enum UidFields {
    kExecutionAsyncId,
    kTriggerAsyncId,
    kAsyncIdCounter,
    kDefaultTriggerAsyncId,
    kUidFieldsCount,
  };

  inline AliasedUint32Array& fields();
  inline AliasedFloat64Array& async_id_fields();
  inline AliasedFloat64Array& async_ids_stack();
  inline v8::Local<v8::Array> js_execution_async_resources();
  // Returns the native executionAsyncResource value at stack index `index`.
  // Resources provided on the JS side are not stored on the native stack,
  // in which case an empty `Local<>` is returned.
  // The `js_execution_async_resources` array contains the value in that case.
  inline v8::Local<v8::Object> native_execution_async_resource(size_t index);

  void InstallPromiseHooks(v8::Local<v8::Context> ctx);
  void ResetPromiseHooks(v8::Local<v8::Function> init,
                         v8::Local<v8::Function> before,
                         v8::Local<v8::Function> after,
                         v8::Local<v8::Function> resolve);
  // Used for testing since V8 doesn't provide API for retrieving configured
  // JS promise hooks.
  v8::Local<v8::Array> GetPromiseHooks(v8::Isolate* isolate) const;
  inline v8::Local<v8::String> provider_string(int idx);

  inline void no_force_checks();
  inline Environment* env();

  // NB: This call does not take (co-)ownership of `execution_async_resource`.
  // The lifetime of the `v8::Local<>` pointee must last until
  // `pop_async_context()` or `clear_async_id_stack()` are called.
  void push_async_context(double async_id,
                          double trigger_async_id,
                          v8::Local<v8::Object> execution_async_resource);
  bool pop_async_context(double async_id);
  void clear_async_id_stack();  // Used in fatal exceptions.

  AsyncHooks(const AsyncHooks&) = delete;
  AsyncHooks& operator=(const AsyncHooks&) = delete;
  AsyncHooks(AsyncHooks&&) = delete;
  AsyncHooks& operator=(AsyncHooks&&) = delete;
  ~AsyncHooks() = default;

  // Used to set the kDefaultTriggerAsyncId in a scope. This is instead of
  // passing the trigger_async_id along with other constructor arguments.
  class DefaultTriggerAsyncIdScope {
   public:
    DefaultTriggerAsyncIdScope() = delete;
    explicit DefaultTriggerAsyncIdScope(Environment* env,
                                        double init_trigger_async_id);
    explicit DefaultTriggerAsyncIdScope(AsyncWrap* async_wrap);
    ~DefaultTriggerAsyncIdScope();

    DefaultTriggerAsyncIdScope(const DefaultTriggerAsyncIdScope&) = delete;
    DefaultTriggerAsyncIdScope& operator=(const DefaultTriggerAsyncIdScope&) =
        delete;
    DefaultTriggerAsyncIdScope(DefaultTriggerAsyncIdScope&&) = delete;
    DefaultTriggerAsyncIdScope& operator=(DefaultTriggerAsyncIdScope&&) =
        delete;

   private:
    AsyncHooks* async_hooks_;
    double old_default_trigger_async_id_;
  };

  struct SerializeInfo {
    AliasedBufferIndex async_ids_stack;
    AliasedBufferIndex fields;
    AliasedBufferIndex async_id_fields;
    SnapshotIndex js_execution_async_resources;
    std::vector<SnapshotIndex> native_execution_async_resources;
  };

  SerializeInfo Serialize(v8::Local<v8::Context> context,
                          v8::SnapshotCreator* creator);
  void Deserialize(v8::Local<v8::Context> context);

 private:
  friend class Environment;  // So we can call the constructor.
  explicit AsyncHooks(v8::Isolate* isolate, const SerializeInfo* info);

  [[noreturn]] void FailWithCorruptedAsyncStack(double expected_async_id);

  // Stores the ids of the current execution context stack.
  AliasedFloat64Array async_ids_stack_;
  // Attached to a Uint32Array that tracks the number of active hooks for
  // each type.
  AliasedUint32Array fields_;
  // Attached to a Float64Array that tracks the state of async resources.
  AliasedFloat64Array async_id_fields_;

  void grow_async_ids_stack();

  v8::Global<v8::Array> js_execution_async_resources_;

  // TODO(@jasnell): Note that this is technically illegal use of
  // v8::Locals which should be kept on the stack. Here, the entries
  // in this object grows and shrinks with the C stack, and entries
  // will be in the right handle scopes, but v8::Locals are supposed
  // to remain on the stack and not the heap. For general purposes
  // this *should* be ok but may need to be looked at further should
  // v8 become stricter in the future about v8::Locals being held in
  // the stack.
  v8::LocalVector<v8::Object> native_execution_async_resources_;

  // Non-empty during deserialization
  const SerializeInfo* info_ = nullptr;

  std::array<v8::Global<v8::Function>, 4> js_promise_hooks_;
};

class ImmediateInfo : public MemoryRetainer {
 public:
  inline AliasedUint32Array& fields();
  inline uint32_t count() const;
  inline uint32_t ref_count() const;
  inline bool has_outstanding() const;
  inline void ref_count_inc(uint32_t increment);
  inline void ref_count_dec(uint32_t decrement);

  ImmediateInfo(const ImmediateInfo&) = delete;
  ImmediateInfo& operator=(const ImmediateInfo&) = delete;
  ImmediateInfo(ImmediateInfo&&) = delete;
  ImmediateInfo& operator=(ImmediateInfo&&) = delete;
  ~ImmediateInfo() = default;

  SET_MEMORY_INFO_NAME(ImmediateInfo)
  SET_SELF_SIZE(ImmediateInfo)
  void MemoryInfo(MemoryTracker* tracker) const override;

  struct SerializeInfo {
    AliasedBufferIndex fields;
  };
  SerializeInfo Serialize(v8::Local<v8::Context> context,
                          v8::SnapshotCreator* creator);
  void Deserialize(v8::Local<v8::Context> context);

 private:
  friend class Environment;  // So we can call the constructor.
  explicit ImmediateInfo(v8::Isolate* isolate, const SerializeInfo* info);

  enum Fields { kCount, kRefCount, kHasOutstanding, kFieldsCount };

  AliasedUint32Array fields_;
};

class TickInfo : public MemoryRetainer {
 public:
  inline AliasedUint8Array& fields();
  inline bool has_tick_scheduled() const;
  inline bool has_rejection_to_warn() const;

  SET_MEMORY_INFO_NAME(TickInfo)
  SET_SELF_SIZE(TickInfo)
  void MemoryInfo(MemoryTracker* tracker) const override;

  TickInfo(const TickInfo&) = delete;
  TickInfo& operator=(const TickInfo&) = delete;
  TickInfo(TickInfo&&) = delete;
  TickInfo& operator=(TickInfo&&) = delete;
  ~TickInfo() = default;

  struct SerializeInfo {
    AliasedBufferIndex fields;
  };
  SerializeInfo Serialize(v8::Local<v8::Context> context,
                          v8::SnapshotCreator* creator);
  void Deserialize(v8::Local<v8::Context> context);

 private:
  friend class Environment;  // So we can call the constructor.
  explicit TickInfo(v8::Isolate* isolate, const SerializeInfo* info);

  enum Fields { kHasTickScheduled = 0, kHasRejectionToWarn, kFieldsCount };

  AliasedUint8Array fields_;
};

class TrackingTraceStateObserver :
    public v8::TracingController::TraceStateObserver {
 public:
  explicit TrackingTraceStateObserver(Environment* env) : env_(env) {}

  void OnTraceEnabled() override {
    UpdateTraceCategoryState();
  }

  void OnTraceDisabled() override {
    UpdateTraceCategoryState();
  }

 private:
  void UpdateTraceCategoryState();

  Environment* env_;
};

class ShouldNotAbortOnUncaughtScope {
 public:
  explicit inline ShouldNotAbortOnUncaughtScope(Environment* env);
  inline void Close();
  inline ~ShouldNotAbortOnUncaughtScope();
  ShouldNotAbortOnUncaughtScope(const ShouldNotAbortOnUncaughtScope&) = delete;
  ShouldNotAbortOnUncaughtScope& operator=(
      const ShouldNotAbortOnUncaughtScope&) = delete;
  ShouldNotAbortOnUncaughtScope(ShouldNotAbortOnUncaughtScope&&) = delete;
  ShouldNotAbortOnUncaughtScope& operator=(ShouldNotAbortOnUncaughtScope&&) =
      delete;

 private:
  Environment* env_;
};

typedef void (*DeserializeRequestCallback)(v8::Local<v8::Context> context,
                                           v8::Local<v8::Object> holder,
                                           int index,
                                           InternalFieldInfoBase* info);
struct DeserializeRequest {
  DeserializeRequestCallback cb;
  v8::Global<v8::Object> holder;
  int index;
  InternalFieldInfoBase* info = nullptr;  // Owned by the request
};

struct EnvSerializeInfo {
  AsyncHooks::SerializeInfo async_hooks;
  TickInfo::SerializeInfo tick_info;
  ImmediateInfo::SerializeInfo immediate_info;
  AliasedBufferIndex timeout_info;
  performance::PerformanceState::SerializeInfo performance_state;
  AliasedBufferIndex exit_info;
  AliasedBufferIndex stream_base_state;
  AliasedBufferIndex should_abort_on_uncaught_toggle;

  RealmSerializeInfo principal_realm;
  friend std::ostream& operator<<(std::ostream& o, const EnvSerializeInfo& i);
};

struct SnapshotMetadata {
  // For now kFullyCustomized is only built with the --build-snapshot CLI flag.
  // We might want to add more types of snapshots in the future.
  enum class Type : uint8_t { kDefault, kFullyCustomized };

  Type type;
  std::string node_version;
  std::string node_arch;
  std::string node_platform;
  SnapshotFlags flags;
};

struct SnapshotData {
  enum class DataOwnership { kOwned, kNotOwned };

  static const uint32_t kMagic = 0x143da19;
  static const SnapshotIndex kNodeVMContextIndex = 0;
  static const SnapshotIndex kNodeBaseContextIndex = kNodeVMContextIndex + 1;
  static const SnapshotIndex kNodeMainContextIndex = kNodeBaseContextIndex + 1;

  DataOwnership data_ownership = DataOwnership::kOwned;

  SnapshotMetadata metadata;

  // The result of v8::SnapshotCreator::CreateBlob() during the snapshot
  // building process.
  v8::StartupData v8_snapshot_blob_data{nullptr, 0};

  IsolateDataSerializeInfo isolate_data_info;
  // TODO(joyeecheung): there should be a vector of env_info once we snapshot
  // the worker environments.
  EnvSerializeInfo env_info;

  // A vector of built-in ids and v8::ScriptCompiler::CachedData, this can be
  // shared across Node.js instances because they are supposed to share the
  // read only space. We use builtins::CodeCacheInfo because
  // v8::ScriptCompiler::CachedData is not copyable.
  std::vector<builtins::CodeCacheInfo> code_cache;

  void ToFile(FILE* out) const;
  std::vector<char> ToBlob() const;
  // If returns false, the metadata doesn't match the current Node.js binary,
  // and the caller should not consume the snapshot data.
  bool Check() const;
  static bool FromFile(SnapshotData* out, FILE* in);
  static bool FromBlob(SnapshotData* out, const std::vector<char>& in);
  static bool FromBlob(SnapshotData* out, std::string_view in);
  static const SnapshotData* FromEmbedderWrapper(
      const EmbedderSnapshotData* data);
  EmbedderSnapshotData::Pointer AsEmbedderWrapper() const;

  ~SnapshotData();
};

void DefaultProcessExitHandlerInternal(Environment* env, ExitCode exit_code);
v8::Maybe<ExitCode> SpinEventLoopInternal(Environment* env);
v8::Maybe<ExitCode> EmitProcessExitInternal(Environment* env);

class Cleanable {
 public:
  virtual ~Cleanable() = default;

 protected:
  ListNode<Cleanable> cleanable_queue_;

 private:
  virtual void Clean() = 0;
  friend class Environment;
};

/**
 * Environment is a per-isolate data structure that represents an execution
 * environment. Each environment has a principal realm. An environment can
 * create multiple subsidiary synthetic realms.
 */
class Environment final : public MemoryRetainer {
 public:
  Environment(const Environment&) = delete;
  Environment& operator=(const Environment&) = delete;
  Environment(Environment&&) = delete;
  Environment& operator=(Environment&&) = delete;

  SET_MEMORY_INFO_NAME(Environment)

  static std::string GetExecPath(const std::vector<std::string>& argv);
  static std::string GetCwd(const std::string& exec_path);

  inline size_t SelfSize() const override;
  bool IsRootNode() const override { return true; }
  void MemoryInfo(MemoryTracker* tracker) const override;

  EnvSerializeInfo Serialize(v8::SnapshotCreator* creator);
  void DeserializeProperties(const EnvSerializeInfo* info);

  void PrintInfoForSnapshotIfDebug();
  void EnqueueDeserializeRequest(DeserializeRequestCallback cb,
                                 v8::Local<v8::Object> holder,
                                 int index,
                                 InternalFieldInfoBase* info);
  void RunDeserializeRequests();
  // Should be called before InitializeInspector()
  void InitializeDiagnostics();

#if HAVE_INSPECTOR
  // If the environment is created for a worker, pass parent_handle and
  // the ownership if transferred into the Environment.
  void InitializeInspector(
      std::unique_ptr<inspector::ParentInspectorHandle> parent_handle);
  void WaitForInspectorFrontendByOptions();
#endif

  inline size_t async_callback_scope_depth() const;
  inline void PushAsyncCallbackScope();
  inline void PopAsyncCallbackScope();

  static inline Environment* GetCurrent(v8::Isolate* isolate);
  static inline Environment* GetCurrent(v8::Local<v8::Context> context);
  static inline Environment* GetCurrent(
      const v8::FunctionCallbackInfo<v8::Value>& info);

  template <typename T>
  static inline Environment* GetCurrent(
      const v8::PropertyCallbackInfo<T>& info);

  // Create an Environment without initializing a main Context. Use
  // InitializeMainContext() to initialize a main context for it.
  Environment(IsolateData* isolate_data,
              v8::Isolate* isolate,
              const std::vector<std::string>& args,
              const std::vector<std::string>& exec_args,
              const EnvSerializeInfo* env_info,
              EnvironmentFlags::Flags flags,
              ThreadId thread_id);
  void InitializeMainContext(v8::Local<v8::Context> context,
                             const EnvSerializeInfo* env_info);
  ~Environment() override;

  void InitializeLibuv();
  inline const std::vector<std::string>& exec_argv();
  inline const std::vector<std::string>& argv();
  const std::string& exec_path() const;

  void CleanupHandles();
  void Exit(ExitCode code);
  void ExitEnv(StopFlags::Flags flags);
  void ClosePerEnvHandles();

  template <typename T, typename OnCloseCallback>
  inline void CloseHandle(T* handle, OnCloseCallback callback);

  void ResetPromiseHooks(v8::Local<v8::Function> init,
                         v8::Local<v8::Function> before,
                         v8::Local<v8::Function> after,
                         v8::Local<v8::Function> resolve);
  void AssignToContext(v8::Local<v8::Context> context,
                       Realm* realm,
                       const ContextInfo& info);
  void UnassignFromContext(v8::Local<v8::Context> context);
  void TrackShadowRealm(shadow_realm::ShadowRealm* realm);
  void UntrackShadowRealm(shadow_realm::ShadowRealm* realm);

  void StartProfilerIdleNotifier();

  inline v8::Isolate* isolate() const;
  inline uv_loop_t* event_loop() const;
  void TryLoadAddon(const char* filename,
                    int flags,
                    const std::function<bool(binding::DLib*)>& was_loaded);

  static inline Environment* from_timer_handle(uv_timer_t* handle);
  inline uv_timer_t* timer_handle();

  static inline Environment* from_immediate_check_handle(uv_check_t* handle);
  inline uv_check_t* immediate_check_handle();
  inline uv_idle_t* immediate_idle_handle();

  inline void IncreaseWaitingRequestCounter();
  inline void DecreaseWaitingRequestCounter();

  inline AsyncHooks* async_hooks();
  inline ImmediateInfo* immediate_info();
  inline AliasedInt32Array& timeout_info();
  inline TickInfo* tick_info();
  inline uint64_t timer_base() const;
  inline permission::Permission* permission();
  inline std::shared_ptr<KVStore> env_vars();
  inline void set_env_vars(std::shared_ptr<KVStore> env_vars);

  inline IsolateData* isolate_data() const;

  inline bool printed_error() const;
  inline void set_printed_error(bool value);

  void PrintSyncTrace() const;
  inline void set_trace_sync_io(bool value);

  inline void set_force_context_aware(bool value);
  inline bool force_context_aware() const;

  // This contains fields that are a pseudo-boolean that keeps track of whether
  // the process is exiting, an integer representing the process exit code, and
  // a pseudo-boolean to indicate whether the exit code is undefined.
  inline AliasedInt32Array& exit_info();
  inline void set_exiting(bool value);
  bool exiting() const;
  inline ExitCode exit_code(const ExitCode default_code) const;

  // This stores whether the --abort-on-uncaught-exception flag was passed
  // to Node.
  inline bool abort_on_uncaught_exception() const;
  inline void set_abort_on_uncaught_exception(bool value);
  // This is a pseudo-boolean that keeps track of whether an uncaught exception
  // should abort the process or not if --abort-on-uncaught-exception was
  // passed to Node. If the flag was not passed, it is ignored.
  inline AliasedUint32Array& should_abort_on_uncaught_toggle();

  inline AliasedInt32Array& stream_base_state();

  // The necessary API for async_hooks.
  inline double new_async_id();
  inline double execution_async_id();
  inline double trigger_async_id();
  inline double get_default_trigger_async_id();

  // List of id's that have been destroyed and need the destroy() cb called.
  inline std::vector<double>* destroy_async_id_list();

  builtins::BuiltinLoader* builtin_loader();

  std::unordered_multimap<int, loader::ModuleWrap*> hash_to_module_map;

  EnabledDebugList* enabled_debug_list() { return &enabled_debug_list_; }

  inline performance::PerformanceState* performance_state();

  void CollectUVExceptionInfo(v8::Local<v8::Value> context,
                              int errorno,
                              const char* syscall = nullptr,
                              const char* message = nullptr,
                              const char* path = nullptr,
                              const char* dest = nullptr);

  // If this flag is set, calls into JS (if they would be observable
  // from userland) must be avoided.  This flag does not indicate whether
  // calling into JS is allowed from a VM perspective at this point.
  inline bool can_call_into_js() const;
  inline void set_can_call_into_js(bool can_call_into_js);

  // Increase or decrease a counter that manages whether this Environment
  // keeps the event loop alive on its own or not. The counter starts out at 0,
  // meaning it does not, and any positive value will make it keep the event
  // loop alive.
  // This is used by Workers to manage their own .ref()/.unref() implementation,
  // as Workers aren't directly associated with their own libuv handles.
  void add_refs(int64_t diff);

  // Convenient getter of the principal realm's has_run_bootstrapping_code().
  inline bool has_run_bootstrapping_code() const;

  inline bool has_serialized_options() const;
  inline void set_has_serialized_options(bool has_serialized_options);

  inline bool is_main_thread() const;
  inline bool no_native_addons() const;
  inline bool should_not_register_esm_loader() const;
  inline bool should_create_inspector() const;
  inline bool should_wait_for_inspector_frontend() const;
  inline bool owns_process_state() const;
  inline bool owns_inspector() const;
  inline bool tracks_unmanaged_fds() const;
  inline bool hide_console_windows() const;
  inline bool no_global_search_paths() const;
  inline bool should_start_debug_signal_handler() const;
  inline bool no_browser_globals() const;
  inline uint64_t thread_id() const;
  inline worker::Worker* worker_context() const;
  Environment* worker_parent_env() const;
  inline void add_sub_worker_context(worker::Worker* context);
  inline void remove_sub_worker_context(worker::Worker* context);
  void stop_sub_worker_contexts();
  template <typename Fn>
  inline void ForEachWorker(Fn&& iterator);
  // Determine if the environment is stopping. This getter is thread-safe.
  inline bool is_stopping() const;
  inline void set_stopping(bool value);
  inline std::list<node_module>* extra_linked_bindings();
  inline node_module* extra_linked_bindings_head();
  inline node_module* extra_linked_bindings_tail();
  inline const Mutex& extra_linked_bindings_mutex() const;

  inline bool filehandle_close_warning() const;
  inline void set_filehandle_close_warning(bool on);

  inline void set_source_maps_enabled(bool on);
  inline bool source_maps_enabled() const;

  inline void ThrowError(const char* errmsg);
  inline void ThrowTypeError(const char* errmsg);
  inline void ThrowRangeError(const char* errmsg);
  inline void ThrowErrnoException(int errorno,
                                  const char* syscall = nullptr,
                                  const char* message = nullptr,
                                  const char* path = nullptr);
  inline void ThrowUVException(int errorno,
                               const char* syscall = nullptr,
                               const char* message = nullptr,
                               const char* path = nullptr,
                               const char* dest = nullptr);

  void AtExit(void (*cb)(void* arg), void* arg);
  void RunAtExitCallbacks();

  v8::Maybe<bool> CheckUnsettledTopLevelAwait() const;
  void RunWeakRefCleanup();

  v8::MaybeLocal<v8::Value> RunSnapshotSerializeCallback() const;
  v8::MaybeLocal<v8::Value> RunSnapshotDeserializeCallback() const;
  v8::MaybeLocal<v8::Value> RunSnapshotDeserializeMain() const;

  // Primitive values are shared across realms.
  // The getters simply proxy to the per-isolate primitive.
#define VP(PropertyName, StringValue) V(v8::Private, PropertyName)
#define VY(PropertyName, StringValue) V(v8::Symbol, PropertyName)
#define VS(PropertyName, StringValue) V(v8::String, PropertyName)
#define V(TypeName, PropertyName)                                             \
  inline v8::Local<TypeName> PropertyName() const;
  PER_ISOLATE_PRIVATE_SYMBOL_PROPERTIES(VP)
  PER_ISOLATE_SYMBOL_PROPERTIES(VY)
  PER_ISOLATE_STRING_PROPERTIES(VS)
#undef V
#undef VS
#undef VY
#undef VP

#define V(PropertyName, TypeName)                                             \
  inline v8::Local<TypeName> PropertyName() const;                            \
  inline void set_ ## PropertyName(v8::Local<TypeName> value);
  PER_ISOLATE_TEMPLATE_PROPERTIES(V)
  // Per-realm strong persistent values of the principal realm.
  // Get/set the value with an explicit realm instead when possible.
  // Deprecate soon.
  PER_REALM_STRONG_PERSISTENT_VALUES(V)
#undef V

  // Return the context of the principal realm.
  // Get the context with an explicit realm instead when possible.
  // Deprecate soon.
  inline v8::Local<v8::Context> context() const;
  inline Realm* principal_realm() const;

#if HAVE_INSPECTOR
  inline inspector::Agent* inspector_agent() const {
    return inspector_agent_.get();
  }
  inline void StopInspector() {
    inspector_agent_.reset();
  }

  inline bool is_in_inspector_console_call() const;
  inline void set_is_in_inspector_console_call(bool value);
#endif

  typedef ListHead<HandleWrap, &HandleWrap::handle_wrap_queue_> HandleWrapQueue;
  typedef ListHead<ReqWrapBase, &ReqWrapBase::req_wrap_queue_> ReqWrapQueue;
  typedef ListHead<Cleanable, &Cleanable::cleanable_queue_> CleanableQueue;

  inline HandleWrapQueue* handle_wrap_queue() { return &handle_wrap_queue_; }
  inline CleanableQueue* cleanable_queue() {
    return &cleanable_queue_;
  }
  inline ReqWrapQueue* req_wrap_queue() { return &req_wrap_queue_; }

  // https://w3c.github.io/hr-time/#dfn-time-origin
  inline uint64_t time_origin() {
    return time_origin_;
  }
  // https://w3c.github.io/hr-time/#dfn-get-time-origin-timestamp
  inline double time_origin_timestamp() {
    return time_origin_timestamp_;
  }

  inline bool EmitProcessEnvWarning() {
    bool current_value = emit_env_nonstring_warning_;
    emit_env_nonstring_warning_ = false;
    return current_value;
  }

  inline bool EmitErrNameWarning() {
    bool current_value = emit_err_name_warning_;
    emit_err_name_warning_ = false;
    return current_value;
  }

  // cb will be called as cb(env) on the next event loop iteration.
  // Unlike the JS setImmediate() function, nested SetImmediate() calls will
  // be run without returning control to the event loop, similar to nextTick().
  template <typename Fn>
  inline void SetImmediate(
      Fn&& cb, CallbackFlags::Flags flags = CallbackFlags::kRefed);
  template <typename Fn>
  // This behaves like SetImmediate() but can be called from any thread.
  inline void SetImmediateThreadsafe(
      Fn&& cb, CallbackFlags::Flags flags = CallbackFlags::kRefed);
  // This behaves like V8's Isolate::RequestInterrupt(), but also accounts for
  // the event loop (i.e. combines the V8 function with SetImmediate()).
  // The passed callback may not throw exceptions.
  // This function can be called from any thread.
  template <typename Fn>
  inline void RequestInterrupt(Fn&& cb);
  // This needs to be available for the JS-land setImmediate().
  void ToggleImmediateRef(bool ref);

  inline void PushShouldNotAbortOnUncaughtScope();
  inline void PopShouldNotAbortOnUncaughtScope();
  inline bool inside_should_not_abort_on_uncaught_scope() const;

  static inline Environment* ForAsyncHooks(AsyncHooks* hooks);

  v8::Local<v8::Value> GetNow();
  uint64_t GetNowUint64();

  void ScheduleTimer(int64_t duration);
  void ToggleTimerRef(bool ref);

  inline void AddCleanupHook(CleanupQueue::Callback cb, void* arg);
  inline void RemoveCleanupHook(CleanupQueue::Callback cb, void* arg);
  void RunCleanup();

  static void TracePromises(v8::PromiseHookType type,
                            v8::Local<v8::Promise> promise,
                            v8::Local<v8::Value> parent);
  static size_t NearHeapLimitCallback(void* data,
                                      size_t current_heap_limit,
                                      size_t initial_heap_limit);
  static void BuildEmbedderGraph(v8::Isolate* isolate,
                                 v8::EmbedderGraph* graph,
                                 void* data);

  inline std::shared_ptr<EnvironmentOptions> options();
  inline std::shared_ptr<ExclusiveAccess<HostPort>> inspector_host_port();

  inline int64_t stack_trace_limit() const;

#if HAVE_INSPECTOR
  void set_coverage_connection(
      std::unique_ptr<profiler::V8CoverageConnection> connection);
  profiler::V8CoverageConnection* coverage_connection();

  inline void set_coverage_directory(const char* directory);
  inline const std::string& coverage_directory() const;

  void set_cpu_profiler_connection(
      std::unique_ptr<profiler::V8CpuProfilerConnection> connection);
  profiler::V8CpuProfilerConnection* cpu_profiler_connection();

  inline void set_cpu_prof_name(const std::string& name);
  inline const std::string& cpu_prof_name() const;

  inline void set_cpu_prof_interval(uint64_t interval);
  inline uint64_t cpu_prof_interval() const;

  inline void set_cpu_prof_dir(const std::string& dir);
  inline const std::string& cpu_prof_dir() const;

  void set_heap_profiler_connection(
      std::unique_ptr<profiler::V8HeapProfilerConnection> connection);
  profiler::V8HeapProfilerConnection* heap_profiler_connection();

  inline void set_heap_prof_name(const std::string& name);
  inline const std::string& heap_prof_name() const;

  inline void set_heap_prof_dir(const std::string& dir);
  inline const std::string& heap_prof_dir() const;

  inline void set_heap_prof_interval(uint64_t interval);
  inline uint64_t heap_prof_interval() const;

#endif  // HAVE_INSPECTOR

  inline const EmbedderPreloadCallback& embedder_preload() const;
  inline void set_embedder_preload(EmbedderPreloadCallback fn);

  inline void set_process_exit_handler(
      std::function<void(Environment*, ExitCode)>&& handler);

  inline CompileCacheHandler* compile_cache_handler();
  inline bool use_compile_cache() const;
  void InitializeCompileCache();
  // Enable built-in compile cache if it has not yet been enabled.
  // The cache will be persisted to disk on exit.
  CompileCacheEnableResult EnableCompileCache(const std::string& cache_dir);
  void FlushCompileCache();

  void RunAndClearNativeImmediates(bool only_refed = false);
  void RunAndClearInterrupts();

  uv_buf_t allocate_managed_buffer(const size_t suggested_size);
  std::unique_ptr<v8::BackingStore> release_managed_buffer(const uv_buf_t& buf);

  void AddUnmanagedFd(int fd);
  void RemoveUnmanagedFd(int fd);

  template <typename T>
  void ForEachRealm(T&& iterator) const;

  inline void set_heap_snapshot_near_heap_limit(uint32_t limit);
  inline bool is_in_heapsnapshot_heap_limit_callback() const;

  inline bool report_exclude_env() const;

  inline void AddHeapSnapshotNearHeapLimitCallback();

  inline void RemoveHeapSnapshotNearHeapLimitCallback(size_t heap_limit);

  // Field identifiers for exit_info_
  enum ExitInfoField {
    kExiting = 0,
    kExitCode,
    kHasExitCode,
    kExitInfoFieldCount
  };

#if HAVE_OPENSSL
#if OPENSSL_VERSION_MAJOR >= 3
  // We declare another alias here to avoid having to include crypto_util.h
  using EVPMDPointer = DeleteFnPtr<EVP_MD, EVP_MD_free>;
  std::vector<EVPMDPointer> evp_md_cache;
#endif  // OPENSSL_VERSION_MAJOR >= 3
  std::unordered_map<std::string, size_t> alias_to_md_id_map;
  std::vector<std::string> supported_hash_algorithms;
#endif  // HAVE_OPENSSL

  v8::Global<v8::Module> temporary_required_module_facade_original;

 private:
  inline void ThrowError(v8::Local<v8::Value> (*fun)(v8::Local<v8::String>,
                                                     v8::Local<v8::Value>),
                         const char* errmsg);
  void TrackContext(v8::Local<v8::Context> context);
  void UntrackContext(v8::Local<v8::Context> context);
  void PurgeTrackedEmptyContexts();

  std::list<binding::DLib> loaded_addons_;
  v8::Isolate* const isolate_;
  IsolateData* const isolate_data_;

  bool env_handle_initialized_ = false;
  uv_timer_t timer_handle_;
  uv_check_t immediate_check_handle_;
  uv_idle_t immediate_idle_handle_;
  uv_prepare_t idle_prepare_handle_;
  uv_check_t idle_check_handle_;
  uv_async_t task_queues_async_;
  int64_t task_queues_async_refs_ = 0;

  // These may be read by ctors and should be listed before complex fields.
  std::atomic_bool is_stopping_{false};
  std::atomic_bool can_call_into_js_{true};

  AsyncHooks async_hooks_;
  ImmediateInfo immediate_info_;
  AliasedInt32Array timeout_info_;
  TickInfo tick_info_;
  permission::Permission permission_;
  const uint64_t timer_base_;
  std::shared_ptr<KVStore> env_vars_;
  bool printed_error_ = false;
  bool trace_sync_io_ = false;
  bool emit_env_nonstring_warning_ = true;
  bool emit_err_name_warning_ = true;
  bool emit_filehandle_warning_ = true;
  bool source_maps_enabled_ = false;

  size_t async_callback_scope_depth_ = 0;
  std::vector<double> destroy_async_id_list_;
  std::unordered_set<shadow_realm::ShadowRealm*> shadow_realms_;

#if HAVE_INSPECTOR
  std::unique_ptr<profiler::V8CoverageConnection> coverage_connection_;
  std::unique_ptr<profiler::V8CpuProfilerConnection> cpu_profiler_connection_;
  std::string coverage_directory_;
  std::string cpu_prof_dir_;
  std::string cpu_prof_name_;
  uint64_t cpu_prof_interval_;
  std::unique_ptr<profiler::V8HeapProfilerConnection> heap_profiler_connection_;
  std::string heap_prof_dir_;
  std::string heap_prof_name_;
  uint64_t heap_prof_interval_;
#endif  // HAVE_INSPECTOR

  std::unique_ptr<CompileCacheHandler> compile_cache_handler_;
  std::shared_ptr<EnvironmentOptions> options_;
  // options_ contains debug options parsed from CLI arguments,
  // while inspector_host_port_ stores the actual inspector host
  // and port being used. For example the port is -1 by default
  // and can be specified as 0 (meaning any port allocated when the
  // server starts listening), but when the inspector server starts
  // the inspector_host_port_->port() will be the actual port being
  // used.
  std::shared_ptr<ExclusiveAccess<HostPort>> inspector_host_port_;
  std::vector<std::string> exec_argv_;
  std::vector<std::string> argv_;
  std::string exec_path_;

  bool is_in_heapsnapshot_heap_limit_callback_ = false;
  uint32_t heap_limit_snapshot_taken_ = 0;
  uint32_t heap_snapshot_near_heap_limit_ = 0;
  bool heapsnapshot_near_heap_limit_callback_added_ = false;

  uint32_t module_id_counter_ = 0;
  uint32_t script_id_counter_ = 0;
  uint32_t function_id_counter_ = 0;
  uint32_t trace_promise_id_counter_ = 0;

  AliasedInt32Array exit_info_;

  AliasedUint32Array should_abort_on_uncaught_toggle_;
  int should_not_abort_scope_counter_ = 0;

  std::unique_ptr<TrackingTraceStateObserver> trace_state_observer_;

  AliasedInt32Array stream_base_state_;

  // As PerformanceNodeTiming is exposed in worker_threads, the per_process
  // time origin is exposed in the worker threads. This is an intentional
  // diverge from the HTML spec of web workers.
  // Process start time from the monotonic clock. This should not be used as an
  // absolute time, but only as a time relative to another monotonic clock time.
  const uint64_t time_origin_;
  // Process start timestamp from the wall clock. This is an absolute time
  // exposed as `performance.timeOrigin`.
  const double time_origin_timestamp_;
  // This is the time when the environment is created.
  const uint64_t environment_start_;
  std::unique_ptr<performance::PerformanceState> performance_state_;

  bool has_serialized_options_ = false;

  uint64_t flags_;
  uint64_t thread_id_;
  std::unordered_set<worker::Worker*> sub_worker_contexts_;

#if HAVE_INSPECTOR
  std::unique_ptr<inspector::Agent> inspector_agent_;
  bool is_in_inspector_console_call_ = false;
#endif

  std::list<DeserializeRequest> deserialize_requests_;

  // handle_wrap_queue_ and req_wrap_queue_ needs to be at a fixed offset from
  // the start of the class because it is used by
  // src/node_postmortem_metadata.cc to calculate offsets and generate debug
  // symbols for Environment, which assumes that the position of members in
  // memory are predictable. For more information please refer to
  // `doc/contributing/node-postmortem-support.md`
  friend int GenDebugSymbols();
  CleanableQueue cleanable_queue_;
  HandleWrapQueue handle_wrap_queue_;
  ReqWrapQueue req_wrap_queue_;
  int handle_cleanup_waiting_ = 0;
  int request_waiting_ = 0;

  EnabledDebugList enabled_debug_list_;

  std::vector<v8::Global<v8::Context>> contexts_;
  std::list<node_module> extra_linked_bindings_;
  Mutex extra_linked_bindings_mutex_;

  static void RunTimers(uv_timer_t* handle);

  struct ExitCallback {
    void (*cb_)(void* arg);
    void* arg_;
  };

  std::list<ExitCallback> at_exit_functions_;

  typedef CallbackQueue<void, Environment*> NativeImmediateQueue;
  NativeImmediateQueue native_immediates_;
  Mutex native_immediates_threadsafe_mutex_;
  NativeImmediateQueue native_immediates_threadsafe_;
  NativeImmediateQueue native_immediates_interrupts_;
  // Also guarded by native_immediates_threadsafe_mutex_. This can be used when
  // trying to post tasks from other threads to an Environment, as the libuv
  // handle for the immediate queues (task_queues_async_) may not be initialized
  // yet or already have been destroyed.
  bool task_queues_async_initialized_ = false;

  std::atomic<Environment**> interrupt_data_ {nullptr};
  void RequestInterruptFromV8();
  static void CheckImmediate(uv_check_t* handle);

  CleanupQueue cleanup_queue_;
  bool started_cleanup_ = false;

  std::unordered_set<int> unmanaged_fds_;

  std::function<void(Environment*, ExitCode)> process_exit_handler_{
      DefaultProcessExitHandlerInternal};

  std::unique_ptr<PrincipalRealm> principal_realm_ = nullptr;

  builtins::BuiltinLoader builtin_loader_;
  EmbedderPreloadCallback embedder_preload_;

  // Used by allocate_managed_buffer() and release_managed_buffer() to keep
  // track of the BackingStore for a given pointer.
  std::unordered_map<char*, std::unique_ptr<v8::BackingStore>>
      released_allocated_buffers_;
};

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_ENV_H_
