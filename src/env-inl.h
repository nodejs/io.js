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

#ifndef SRC_ENV_INL_H_
#define SRC_ENV_INL_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "aliased_buffer-inl.h"
#include "callback_queue-inl.h"
#include "env.h"
#include "node.h"
#include "node_context_data.h"
#include "node_internals.h"
#include "node_perf_common.h"
#include "node_realm-inl.h"
#include "util-inl.h"
#include "uv.h"
#include "v8-cppgc.h"
#include "v8.h"

#include <cstddef>
#include <cstdint>

#include <utility>

namespace node {

NoArrayBufferZeroFillScope::NoArrayBufferZeroFillScope(
    IsolateData* isolate_data)
    : node_allocator_(isolate_data->node_allocator()) {
  if (node_allocator_ != nullptr) node_allocator_->zero_fill_field()[0] = 0;
}

NoArrayBufferZeroFillScope::~NoArrayBufferZeroFillScope() {
  if (node_allocator_ != nullptr) node_allocator_->zero_fill_field()[0] = 1;
}

inline v8::Isolate* IsolateData::isolate() const {
  return isolate_;
}

inline uv_loop_t* IsolateData::event_loop() const {
  return event_loop_;
}

inline uint16_t* IsolateData::embedder_id_for_cppgc() const {
  return &(wrapper_data_->cppgc_id);
}

inline uint16_t* IsolateData::embedder_id_for_non_cppgc() const {
  return &(wrapper_data_->non_cppgc_id);
}

inline NodeArrayBufferAllocator* IsolateData::node_allocator() const {
  return node_allocator_;
}

inline MultiIsolatePlatform* IsolateData::platform() const {
  return platform_;
}

inline const SnapshotData* IsolateData::snapshot_data() const {
  return snapshot_data_;
}

inline void IsolateData::set_worker_context(worker::Worker* context) {
  CHECK_NULL(worker_context_);  // Should be set only once.
  worker_context_ = context;
}

inline worker::Worker* IsolateData::worker_context() const {
  return worker_context_;
}

inline v8::Local<v8::String> IsolateData::async_wrap_provider(int index) const {
  return async_wrap_providers_[index].Get(isolate_);
}

inline AliasedUint32Array& AsyncHooks::fields() {
  return fields_;
}

inline AliasedFloat64Array& AsyncHooks::async_id_fields() {
  return async_id_fields_;
}

inline AliasedFloat64Array& AsyncHooks::async_ids_stack() {
  return async_ids_stack_;
}

v8::Local<v8::Array> AsyncHooks::js_execution_async_resources() {
  if (js_execution_async_resources_.IsEmpty()) [[unlikely]] {
    js_execution_async_resources_.Reset(
        env()->isolate(), v8::Array::New(env()->isolate()));
  }
  return PersistentToLocal::Strong(js_execution_async_resources_);
}

v8::Local<v8::Object> AsyncHooks::native_execution_async_resource(size_t i) {
  if (i >= native_execution_async_resources_.size()) return {};
  return native_execution_async_resources_[i];
}

inline v8::Local<v8::String> AsyncHooks::provider_string(int idx) {
  return env()->isolate_data()->async_wrap_provider(idx);
}

inline void AsyncHooks::no_force_checks() {
  fields_[kCheck] -= 1;
}

inline Environment* AsyncHooks::env() {
  return Environment::ForAsyncHooks(this);
}

Environment* Environment::ForAsyncHooks(AsyncHooks* hooks) {
  return ContainerOf(&Environment::async_hooks_, hooks);
}

inline size_t Environment::async_callback_scope_depth() const {
  return async_callback_scope_depth_;
}

inline void Environment::PushAsyncCallbackScope() {
  async_callback_scope_depth_++;
}

inline void Environment::PopAsyncCallbackScope() {
  async_callback_scope_depth_--;
}

inline AliasedUint32Array& ImmediateInfo::fields() {
  return fields_;
}

inline uint32_t ImmediateInfo::count() const {
  return fields_[kCount];
}

inline uint32_t ImmediateInfo::ref_count() const {
  return fields_[kRefCount];
}

inline bool ImmediateInfo::has_outstanding() const {
  return fields_[kHasOutstanding] == 1;
}

inline void ImmediateInfo::ref_count_inc(uint32_t increment) {
  fields_[kRefCount] += increment;
}

inline void ImmediateInfo::ref_count_dec(uint32_t decrement) {
  fields_[kRefCount] -= decrement;
}

inline AliasedUint8Array& TickInfo::fields() {
  return fields_;
}

inline bool TickInfo::has_tick_scheduled() const {
  return fields_[kHasTickScheduled] == 1;
}

inline bool TickInfo::has_rejection_to_warn() const {
  return fields_[kHasRejectionToWarn] == 1;
}

inline Environment* Environment::GetCurrent(v8::Isolate* isolate) {
  if (!isolate->InContext()) [[unlikely]]
    return nullptr;
  v8::HandleScope handle_scope(isolate);
  return GetCurrent(isolate->GetCurrentContext());
}

inline Environment* Environment::GetCurrent(v8::Local<v8::Context> context) {
  if (!ContextEmbedderTag::IsNodeContext(context)) [[unlikely]] {
    return nullptr;
  }
  return static_cast<Environment*>(
      context->GetAlignedPointerFromEmbedderData(
          ContextEmbedderIndex::kEnvironment));
}

inline Environment* Environment::GetCurrent(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  return GetCurrent(info.GetIsolate()->GetCurrentContext());
}

template <typename T>
inline Environment* Environment::GetCurrent(
    const v8::PropertyCallbackInfo<T>& info) {
  return GetCurrent(info.GetIsolate()->GetCurrentContext());
}

inline v8::Isolate* Environment::isolate() const {
  return isolate_;
}

inline Environment* Environment::from_timer_handle(uv_timer_t* handle) {
  return ContainerOf(&Environment::timer_handle_, handle);
}

inline uv_timer_t* Environment::timer_handle() {
  return &timer_handle_;
}

inline Environment* Environment::from_immediate_check_handle(
    uv_check_t* handle) {
  return ContainerOf(&Environment::immediate_check_handle_, handle);
}

inline uv_check_t* Environment::immediate_check_handle() {
  return &immediate_check_handle_;
}

inline uv_idle_t* Environment::immediate_idle_handle() {
  return &immediate_idle_handle_;
}

template <typename T, typename OnCloseCallback>
inline void Environment::CloseHandle(T* handle, OnCloseCallback callback) {
  handle_cleanup_waiting_++;
  static_assert(sizeof(T) >= sizeof(uv_handle_t), "T is a libuv handle");
  static_assert(offsetof(T, data) == offsetof(uv_handle_t, data),
                "T is a libuv handle");
  static_assert(offsetof(T, close_cb) == offsetof(uv_handle_t, close_cb),
                "T is a libuv handle");
  struct CloseData {
    Environment* env;
    OnCloseCallback callback;
    void* original_data;
  };
  handle->data = new CloseData { this, callback, handle->data };
  uv_close(reinterpret_cast<uv_handle_t*>(handle), [](uv_handle_t* handle) {
    std::unique_ptr<CloseData> data { static_cast<CloseData*>(handle->data) };
    data->env->handle_cleanup_waiting_--;
    handle->data = data->original_data;
    data->callback(reinterpret_cast<T*>(handle));
  });
}

void Environment::IncreaseWaitingRequestCounter() {
  request_waiting_++;
}

void Environment::DecreaseWaitingRequestCounter() {
  request_waiting_--;
  CHECK_GE(request_waiting_, 0);
}

inline uv_loop_t* Environment::event_loop() const {
  return isolate_data()->event_loop();
}

#if HAVE_INSPECTOR
inline bool Environment::is_in_inspector_console_call() const {
  return is_in_inspector_console_call_;
}

inline void Environment::set_is_in_inspector_console_call(bool value) {
  is_in_inspector_console_call_ = value;
}
#endif

inline AsyncHooks* Environment::async_hooks() {
  return &async_hooks_;
}

inline ImmediateInfo* Environment::immediate_info() {
  return &immediate_info_;
}

inline AliasedInt32Array& Environment::timeout_info() {
  return timeout_info_;
}

inline TickInfo* Environment::tick_info() {
  return &tick_info_;
}

inline permission::Permission* Environment::permission() {
  return &permission_;
}

inline uint64_t Environment::timer_base() const {
  return timer_base_;
}

inline std::shared_ptr<KVStore> Environment::env_vars() {
  return env_vars_;
}

inline void Environment::set_env_vars(std::shared_ptr<KVStore> env_vars) {
  env_vars_ = env_vars;
}

inline bool Environment::printed_error() const {
  return printed_error_;
}

inline void Environment::set_printed_error(bool value) {
  printed_error_ = value;
}

inline void Environment::set_trace_sync_io(bool value) {
  trace_sync_io_ = value;
}

inline bool Environment::abort_on_uncaught_exception() const {
  return options_->abort_on_uncaught_exception;
}

inline void Environment::set_force_context_aware(bool value) {
  options_->force_context_aware = value;
}

inline bool Environment::force_context_aware() const {
  return options_->force_context_aware;
}

inline void Environment::set_exiting(bool value) {
  exit_info_[kExiting] = value ? 1 : 0;
}

inline bool Environment::exiting() const {
  return exit_info_[kExiting] == 1;
}

inline ExitCode Environment::exit_code(const ExitCode default_code) const {
  return exit_info_[kHasExitCode] == 0
             ? default_code
             : static_cast<ExitCode>(exit_info_[kExitCode]);
}

inline AliasedInt32Array& Environment::exit_info() {
  return exit_info_;
}

inline void Environment::set_abort_on_uncaught_exception(bool value) {
  options_->abort_on_uncaught_exception = value;
}

inline AliasedUint32Array& Environment::should_abort_on_uncaught_toggle() {
  return should_abort_on_uncaught_toggle_;
}

inline AliasedInt32Array& Environment::stream_base_state() {
  return stream_base_state_;
}

ShouldNotAbortOnUncaughtScope::ShouldNotAbortOnUncaughtScope(
    Environment* env)
    : env_(env) {
  env_->PushShouldNotAbortOnUncaughtScope();
}

ShouldNotAbortOnUncaughtScope::~ShouldNotAbortOnUncaughtScope() {
  Close();
}

void ShouldNotAbortOnUncaughtScope::Close() {
  if (env_ != nullptr) {
    env_->PopShouldNotAbortOnUncaughtScope();
    env_ = nullptr;
  }
}

inline void Environment::PushShouldNotAbortOnUncaughtScope() {
  should_not_abort_scope_counter_++;
}

inline void Environment::PopShouldNotAbortOnUncaughtScope() {
  should_not_abort_scope_counter_--;
}

inline bool Environment::inside_should_not_abort_on_uncaught_scope() const {
  return should_not_abort_scope_counter_ > 0;
}

inline std::vector<double>* Environment::destroy_async_id_list() {
  return &destroy_async_id_list_;
}

inline builtins::BuiltinLoader* Environment::builtin_loader() {
  return &builtin_loader_;
}

inline const EmbedderPreloadCallback& Environment::embedder_preload() const {
  return embedder_preload_;
}

inline void Environment::set_embedder_preload(EmbedderPreloadCallback fn) {
  embedder_preload_ = std::move(fn);
}

inline double Environment::new_async_id() {
  async_hooks()->async_id_fields()[AsyncHooks::kAsyncIdCounter] += 1;
  return async_hooks()->async_id_fields()[AsyncHooks::kAsyncIdCounter];
}

inline double Environment::execution_async_id() {
  return async_hooks()->async_id_fields()[AsyncHooks::kExecutionAsyncId];
}

inline double Environment::trigger_async_id() {
  return async_hooks()->async_id_fields()[AsyncHooks::kTriggerAsyncId];
}

inline double Environment::get_default_trigger_async_id() {
  double default_trigger_async_id =
    async_hooks()->async_id_fields()[AsyncHooks::kDefaultTriggerAsyncId];
  // If defaultTriggerAsyncId isn't set, use the executionAsyncId
  if (default_trigger_async_id < 0)
    default_trigger_async_id = execution_async_id();
  return default_trigger_async_id;
}

inline int64_t Environment::stack_trace_limit() const {
  return isolate_data_->options()->stack_trace_limit;
}

inline std::shared_ptr<EnvironmentOptions> Environment::options() {
  return options_;
}

inline const std::vector<std::string>& Environment::argv() {
  return argv_;
}

inline const std::vector<std::string>& Environment::exec_argv() {
  return exec_argv_;
}

inline const std::string& Environment::exec_path() const {
  return exec_path_;
}

inline CompileCacheHandler* Environment::compile_cache_handler() {
  auto* result = compile_cache_handler_.get();
  DCHECK_NOT_NULL(result);
  return result;
}

inline bool Environment::use_compile_cache() const {
  return compile_cache_handler_.get() != nullptr;
}

#if HAVE_INSPECTOR
inline void Environment::set_coverage_directory(const char* dir) {
  coverage_directory_ = std::string(dir);
}

inline void Environment::set_coverage_connection(
    std::unique_ptr<profiler::V8CoverageConnection> connection) {
  CHECK_NULL(coverage_connection_);
  std::swap(coverage_connection_, connection);
}

inline profiler::V8CoverageConnection* Environment::coverage_connection() {
  return coverage_connection_.get();
}

inline const std::string& Environment::coverage_directory() const {
  return coverage_directory_;
}

inline void Environment::set_cpu_profiler_connection(
    std::unique_ptr<profiler::V8CpuProfilerConnection> connection) {
  CHECK_NULL(cpu_profiler_connection_);
  std::swap(cpu_profiler_connection_, connection);
}

inline profiler::V8CpuProfilerConnection*
Environment::cpu_profiler_connection() {
  return cpu_profiler_connection_.get();
}

inline void Environment::set_cpu_prof_interval(uint64_t interval) {
  cpu_prof_interval_ = interval;
}

inline uint64_t Environment::cpu_prof_interval() const {
  return cpu_prof_interval_;
}

inline void Environment::set_cpu_prof_name(const std::string& name) {
  cpu_prof_name_ = name;
}

inline const std::string& Environment::cpu_prof_name() const {
  return cpu_prof_name_;
}

inline void Environment::set_cpu_prof_dir(const std::string& dir) {
  cpu_prof_dir_ = dir;
}

inline const std::string& Environment::cpu_prof_dir() const {
  return cpu_prof_dir_;
}

inline void Environment::set_heap_profiler_connection(
    std::unique_ptr<profiler::V8HeapProfilerConnection> connection) {
  CHECK_NULL(heap_profiler_connection_);
  std::swap(heap_profiler_connection_, connection);
}

inline profiler::V8HeapProfilerConnection*
Environment::heap_profiler_connection() {
  return heap_profiler_connection_.get();
}

inline void Environment::set_heap_prof_name(const std::string& name) {
  heap_prof_name_ = name;
}

inline const std::string& Environment::heap_prof_name() const {
  return heap_prof_name_;
}

inline void Environment::set_heap_prof_dir(const std::string& dir) {
  heap_prof_dir_ = dir;
}

inline const std::string& Environment::heap_prof_dir() const {
  return heap_prof_dir_;
}

inline void Environment::set_heap_prof_interval(uint64_t interval) {
  heap_prof_interval_ = interval;
}

inline uint64_t Environment::heap_prof_interval() const {
  return heap_prof_interval_;
}

#endif  // HAVE_INSPECTOR

inline
std::shared_ptr<ExclusiveAccess<HostPort>> Environment::inspector_host_port() {
  return inspector_host_port_;
}

inline std::shared_ptr<PerIsolateOptions> IsolateData::options() {
  return options_;
}

template <typename Fn>
void Environment::SetImmediate(Fn&& cb, CallbackFlags::Flags flags) {
  auto callback = native_immediates_.CreateCallback(std::move(cb), flags);
  native_immediates_.Push(std::move(callback));

  if (flags & CallbackFlags::kRefed) {
    if (immediate_info()->ref_count() == 0)
      ToggleImmediateRef(true);
    immediate_info()->ref_count_inc(1);
  }
}

template <typename Fn>
void Environment::SetImmediateThreadsafe(Fn&& cb, CallbackFlags::Flags flags) {
  auto callback = native_immediates_threadsafe_.CreateCallback(
      std::move(cb), flags);
  {
    Mutex::ScopedLock lock(native_immediates_threadsafe_mutex_);
    native_immediates_threadsafe_.Push(std::move(callback));
    if (task_queues_async_initialized_)
      uv_async_send(&task_queues_async_);
  }
}

template <typename Fn>
void Environment::RequestInterrupt(Fn&& cb) {
  auto callback = native_immediates_interrupts_.CreateCallback(
      std::move(cb), CallbackFlags::kRefed);
  {
    Mutex::ScopedLock lock(native_immediates_threadsafe_mutex_);
    native_immediates_interrupts_.Push(std::move(callback));
    if (task_queues_async_initialized_)
      uv_async_send(&task_queues_async_);
  }
  RequestInterruptFromV8();
}

inline bool Environment::can_call_into_js() const {
  return can_call_into_js_ && !is_stopping();
}

inline void Environment::set_can_call_into_js(bool can_call_into_js) {
  can_call_into_js_ = can_call_into_js;
}

inline bool Environment::has_run_bootstrapping_code() const {
  return principal_realm_->has_run_bootstrapping_code();
}

inline bool Environment::has_serialized_options() const {
  return has_serialized_options_;
}

inline void Environment::set_has_serialized_options(bool value) {
  has_serialized_options_ = value;
}

inline bool Environment::is_main_thread() const {
  return worker_context() == nullptr;
}

inline bool Environment::no_native_addons() const {
  return (flags_ & EnvironmentFlags::kNoNativeAddons) ||
          !options_->allow_native_addons;
}

inline bool Environment::should_not_register_esm_loader() const {
  return flags_ & EnvironmentFlags::kNoRegisterESMLoader;
}

inline bool Environment::owns_process_state() const {
  return flags_ & EnvironmentFlags::kOwnsProcessState;
}

inline bool Environment::owns_inspector() const {
  return flags_ & EnvironmentFlags::kOwnsInspector;
}

inline bool Environment::should_create_inspector() const {
  return (flags_ & EnvironmentFlags::kNoCreateInspector) == 0 &&
         !(options_->test_runner && options_->test_isolation == "process") &&
         !options_->watch_mode;
}

inline bool Environment::should_wait_for_inspector_frontend() const {
  return (flags_ & EnvironmentFlags::kNoWaitForInspectorFrontend) == 0;
}

inline bool Environment::tracks_unmanaged_fds() const {
  return flags_ & EnvironmentFlags::kTrackUnmanagedFds;
}

inline bool Environment::hide_console_windows() const {
  return flags_ & EnvironmentFlags::kHideConsoleWindows;
}

inline bool Environment::no_global_search_paths() const {
  return (flags_ & EnvironmentFlags::kNoGlobalSearchPaths) ||
         !options_->global_search_paths;
}

inline bool Environment::should_start_debug_signal_handler() const {
  return ((flags_ & EnvironmentFlags::kNoStartDebugSignalHandler) == 0) &&
         !options_->disable_sigusr1;
}

inline bool Environment::no_browser_globals() const {
  // configure --no-browser-globals
#ifdef NODE_NO_BROWSER_GLOBALS
  return true;
#else
  return flags_ & EnvironmentFlags::kNoBrowserGlobals;
#endif
}

bool Environment::filehandle_close_warning() const {
  return emit_filehandle_warning_;
}

void Environment::set_filehandle_close_warning(bool on) {
  emit_filehandle_warning_ = on;
}

void Environment::set_source_maps_enabled(bool on) {
  source_maps_enabled_ = on;
}

bool Environment::source_maps_enabled() const {
  return source_maps_enabled_;
}

inline uint64_t Environment::thread_id() const {
  return thread_id_;
}

inline worker::Worker* Environment::worker_context() const {
  return isolate_data()->worker_context();
}

inline void Environment::add_sub_worker_context(worker::Worker* context) {
  sub_worker_contexts_.insert(context);
}

inline void Environment::remove_sub_worker_context(worker::Worker* context) {
  sub_worker_contexts_.erase(context);
}

template <typename Fn>
inline void Environment::ForEachWorker(Fn&& iterator) {
  for (worker::Worker* w : sub_worker_contexts_) iterator(w);
}

inline bool Environment::is_stopping() const {
  return is_stopping_.load();
}

inline void Environment::set_stopping(bool value) {
  is_stopping_.store(value);
}

inline std::list<node_module>* Environment::extra_linked_bindings() {
  return &extra_linked_bindings_;
}

inline node_module* Environment::extra_linked_bindings_head() {
  return extra_linked_bindings_.size() > 0 ?
      &extra_linked_bindings_.front() : nullptr;
}

inline node_module* Environment::extra_linked_bindings_tail() {
  return extra_linked_bindings_.size() > 0 ?
      &extra_linked_bindings_.back() : nullptr;
}

inline const Mutex& Environment::extra_linked_bindings_mutex() const {
  return extra_linked_bindings_mutex_;
}

inline performance::PerformanceState* Environment::performance_state() {
  return performance_state_.get();
}

inline IsolateData* Environment::isolate_data() const {
  return isolate_data_;
}

template <typename T>
inline void Environment::ForEachRealm(T&& iterator) const {
  // TODO(legendecas): iterate over more realms bound to the environment.
  iterator(principal_realm());
}

inline void Environment::ThrowError(const char* errmsg) {
  ThrowError(v8::Exception::Error, errmsg);
}

inline void Environment::ThrowTypeError(const char* errmsg) {
  ThrowError(v8::Exception::TypeError, errmsg);
}

inline void Environment::ThrowRangeError(const char* errmsg) {
  ThrowError(v8::Exception::RangeError, errmsg);
}

inline void Environment::ThrowError(
    v8::Local<v8::Value> (*fun)(v8::Local<v8::String>, v8::Local<v8::Value>),
    const char* errmsg) {
  v8::HandleScope handle_scope(isolate());
  isolate()->ThrowException(fun(OneByteString(isolate(), errmsg), {}));
}

inline void Environment::ThrowErrnoException(int errorno,
                                             const char* syscall,
                                             const char* message,
                                             const char* path) {
  isolate()->ThrowException(
      ErrnoException(isolate(), errorno, syscall, message, path));
}

inline void Environment::ThrowUVException(int errorno,
                                          const char* syscall,
                                          const char* message,
                                          const char* path,
                                          const char* dest) {
  isolate()->ThrowException(
      UVException(isolate(), errorno, syscall, message, path, dest));
}

void Environment::AddCleanupHook(CleanupQueue::Callback fn, void* arg) {
  cleanup_queue_.Add(fn, arg);
}

void Environment::RemoveCleanupHook(CleanupQueue::Callback fn, void* arg) {
  cleanup_queue_.Remove(fn, arg);
}

void Environment::set_process_exit_handler(
    std::function<void(Environment*, ExitCode)>&& handler) {
  process_exit_handler_ = std::move(handler);
}

#define VP(PropertyName, StringValue) V(v8::Private, PropertyName)
#define VY(PropertyName, StringValue) V(v8::Symbol, PropertyName)
#define VS(PropertyName, StringValue) V(v8::String, PropertyName)
#define VR(PropertyName, TypeName) V(v8::Private, per_realm_##PropertyName)
#define V(TypeName, PropertyName)                                             \
  inline                                                                      \
  v8::Local<TypeName> IsolateData::PropertyName() const {                     \
    return PropertyName ## _ .Get(isolate_);                                  \
  }
  PER_ISOLATE_PRIVATE_SYMBOL_PROPERTIES(VP)
  PER_ISOLATE_SYMBOL_PROPERTIES(VY)
  PER_ISOLATE_STRING_PROPERTIES(VS)
  PER_REALM_STRONG_PERSISTENT_VALUES(VR)
#undef V
#undef VR
#undef VS
#undef VY
#undef VP

#define VM(PropertyName) V(PropertyName##_binding_template, v8::ObjectTemplate)
#define V(PropertyName, TypeName)                                              \
  inline v8::Local<TypeName> IsolateData::PropertyName() const {               \
    return PropertyName##_.Get(isolate_);                                      \
  }                                                                            \
  inline void IsolateData::set_##PropertyName(v8::Local<TypeName> value) {     \
    PropertyName##_.Set(isolate_, value);                                      \
  }
  PER_ISOLATE_TEMPLATE_PROPERTIES(V)
  NODE_BINDINGS_WITH_PER_ISOLATE_INIT(VM)
#undef V
#undef VM

#define VP(PropertyName, StringValue) V(v8::Private, PropertyName)
#define VY(PropertyName, StringValue) V(v8::Symbol, PropertyName)
#define VS(PropertyName, StringValue) V(v8::String, PropertyName)
#define V(TypeName, PropertyName)                                             \
  inline v8::Local<TypeName> Environment::PropertyName() const {              \
    return isolate_data()->PropertyName();                                    \
  }
  PER_ISOLATE_PRIVATE_SYMBOL_PROPERTIES(VP)
  PER_ISOLATE_SYMBOL_PROPERTIES(VY)
  PER_ISOLATE_STRING_PROPERTIES(VS)
#undef V
#undef VS
#undef VY
#undef VP

#define V(PropertyName, TypeName)                                              \
  inline v8::Local<TypeName> Environment::PropertyName() const {               \
    return isolate_data()->PropertyName();                                     \
  }                                                                            \
  inline void Environment::set_##PropertyName(v8::Local<TypeName> value) {     \
    DCHECK(isolate_data()->PropertyName().IsEmpty());                          \
    isolate_data()->set_##PropertyName(value);                                 \
  }
  PER_ISOLATE_TEMPLATE_PROPERTIES(V)
#undef V

#define V(PropertyName, TypeName)                                              \
  inline v8::Local<TypeName> Environment::PropertyName() const {               \
    DCHECK_NOT_NULL(principal_realm_);                                         \
    return principal_realm_->PropertyName();                                   \
  }                                                                            \
  inline void Environment::set_##PropertyName(v8::Local<TypeName> value) {     \
    DCHECK_NOT_NULL(principal_realm_);                                         \
    principal_realm_->set_##PropertyName(value);                               \
  }
  PER_REALM_STRONG_PERSISTENT_VALUES(V)
#undef V

v8::Local<v8::Context> Environment::context() const {
  return principal_realm()->context();
}

Realm* Environment::principal_realm() const {
  return principal_realm_.get();
}

inline void Environment::set_heap_snapshot_near_heap_limit(uint32_t limit) {
  heap_snapshot_near_heap_limit_ = limit;
}

inline bool Environment::is_in_heapsnapshot_heap_limit_callback() const {
  return is_in_heapsnapshot_heap_limit_callback_;
}

inline bool Environment::report_exclude_env() const {
  return options_->report_exclude_env;
}

inline void Environment::AddHeapSnapshotNearHeapLimitCallback() {
  DCHECK(!heapsnapshot_near_heap_limit_callback_added_);
  heapsnapshot_near_heap_limit_callback_added_ = true;
  isolate_->AddNearHeapLimitCallback(Environment::NearHeapLimitCallback, this);
}

inline void Environment::RemoveHeapSnapshotNearHeapLimitCallback(
    size_t heap_limit) {
  DCHECK(heapsnapshot_near_heap_limit_callback_added_);
  heapsnapshot_near_heap_limit_callback_added_ = false;
  isolate_->RemoveNearHeapLimitCallback(Environment::NearHeapLimitCallback,
                                        heap_limit);
}

}  // namespace node

// These two files depend on each other. Including base_object-inl.h after this
// file is the easiest way to avoid issues with that circular dependency.
#include "base_object-inl.h"

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_ENV_INL_H_
