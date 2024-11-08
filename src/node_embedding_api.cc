#define NAPI_EXPERIMENTAL
#include "node_embedding_api.h"

#include "env-inl.h"
#include "js_native_api_v8.h"
#include "node_api_internals.h"
#include "util-inl.h"
#include "uv.h"

#include <mutex>
#include <string>

// Use macros to handle errors since they can record the failing argument name
// or expression and their location in the source code.

#define CAST_NOT_NULL_TO(value, type)                                          \
  (value) == nullptr ? node::EmbeddedErrorHandling::HandleError(               \
                           "Argument must not be null: " #value,               \
                           __FILE__,                                           \
                           __LINE__,                                           \
                           node_embedding_status_null_arg)                     \
                     : reinterpret_cast<type*>(value)

#define EMBEDDED_PLATFORM(platform)                                            \
  CAST_NOT_NULL_TO(platform, node::EmbeddedPlatform)

#define EMBEDDED_RUNTIME(runtime)                                              \
  CAST_NOT_NULL_TO(runtime, node::EmbeddedRuntime)

#define CHECK_ARG_NOT_NULL(arg)                                                \
  do {                                                                         \
    if ((arg) == nullptr) {                                                    \
      return node::EmbeddedErrorHandling::HandleError(                         \
          "Argument must not be null: " #arg,                                  \
          __FILE__,                                                            \
          __LINE__,                                                            \
          node_embedding_status_null_arg);                                     \
    }                                                                          \
  } while (false)

#define ASSERT_ARG(arg, expr)                                                  \
  do {                                                                         \
    if (!(expr)) {                                                             \
      return node::EmbeddedErrorHandling::HandleError(                         \
          "Arg: " #arg " failed: " #expr,                                      \
          __FILE__,                                                            \
          __LINE__,                                                            \
          node_embedding_status_bad_arg);                                      \
    }                                                                          \
  } while (false)

#define ASSERT(expr)                                                           \
  do {                                                                         \
    if (!(expr)) {                                                             \
      return node::EmbeddedErrorHandling::HandleError(                         \
          "Expression returned false: " #expr,                                 \
          __FILE__,                                                            \
          __LINE__,                                                            \
          node_embedding_status_generic_error);                                \
    }                                                                          \
  } while (false)

#define CHECK_STATUS(expr)                                                     \
  do {                                                                         \
    node_embedding_status status = (expr);                                     \
    if (status != node_embedding_status_ok) {                                  \
      return status;                                                           \
    }                                                                          \
  } while (false)

namespace v8impl {

napi_env NewEnv(v8::Local<v8::Context> context,
                const std::string& module_filename,
                int32_t module_api_version);

}  // namespace v8impl

namespace node {

// Declare functions implemented in embed_helpers.cc
v8::Maybe<ExitCode> SpinEventLoopWithoutCleanup(Environment* env,
                                                uv_run_mode run_mode);

namespace {

// A helper class to convert std::vector<std::string> to an array of C strings.
// If the number of strings is less than kInplaceBufferSize, the strings are
// stored in the inplace_buffer_ array. Otherwise, the strings are stored in the
// allocated_buffer_ array.
// Ideally the class must be allocated on the stack.
// In any case it must not outlive the passed vector since it keeps only the
// string pointers returned by std::string::c_str() method.
template <size_t kInplaceBufferSize = 32>
class CStringArray {
 public:
  explicit CStringArray(const std::vector<std::string>& strings) noexcept
      : size_(strings.size()) {
    if (size_ <= inplace_buffer_.size()) {
      c_strs_ = inplace_buffer_.data();
    } else {
      allocated_buffer_ = std::make_unique<const char*[]>(size_);
      c_strs_ = allocated_buffer_.get();
    }
    for (size_t i = 0; i < size_; ++i) {
      c_strs_[i] = strings[i].c_str();
    }
  }

  CStringArray(const CStringArray&) = delete;
  CStringArray& operator=(const CStringArray&) = delete;

  const char** c_strs() const { return c_strs_; }
  size_t size() const { return size_; }

  const char** argv() const { return c_strs_; }
  int32_t argc() const { return static_cast<int32_t>(size_); }

 private:
  const char** c_strs_{};
  size_t size_{};
  std::array<const char*, kInplaceBufferSize> inplace_buffer_;
  std::unique_ptr<const char*[]> allocated_buffer_;
};

// Stack implementation that works only with trivially constructible,
// destructible, and copyable types. It uses the small value optimization where
// several elements are stored in the in-place array.
template <typename T, size_t kInplaceEntryCount = 8>
class SmallTrivialStack {
  static_assert(std::is_trivially_constructible_v<T>,
                "T must be trivially constructible");
  static_assert(std::is_trivially_destructible_v<T>,
                "T must be trivially destructible");
  static_assert(std::is_trivially_copyable_v<T>,
                "T must be trivially copyable");

 public:
  SmallTrivialStack() noexcept : stack_(inplace_entries_.data()) {}

  void Push(T&& value) {
    EnsureCapacity(size_ + 1);
    stack_[size_++] = std::move(value);
  }

  void Pop() {
    CHECK_GT(size_, 0);
    --size_;
  }

  size_t size() const { return size_; }

  const T& top() const {
    CHECK_GT(size_, 0);
    return stack_[size_ - 1];
  }

  SmallTrivialStack(const SmallTrivialStack&) = delete;
  SmallTrivialStack& operator=(const SmallTrivialStack&) = delete;

 private:
  void EnsureCapacity(size_t new_size) {
    if (new_size <= capacity_) {
      return;
    }

    size_t new_capacity = capacity_ + capacity_ / 2;
    std::unique_ptr<T[]> new_allocated_entries =
        std::make_unique<T[]>(new_capacity);
    std::memcpy(new_allocated_entries.get(), stack_, size_ * sizeof(T));
    allocated_entries_ = std::move(new_allocated_entries);
    stack_ = allocated_entries_.get();
    capacity_ = new_capacity;
  }

 private:
  T* stack_{};     // Points to either inplace_entries_ or allocated_entries_.
  size_t size_{};  // Number of elements in the stack.
  size_t capacity_{kInplaceEntryCount};
  std::array<T, kInplaceEntryCount> inplace_entries_;
  std::unique_ptr<T[]> allocated_entries_;
};

class EmbeddedErrorHandling {
 public:
  static node_embedding_status SetErrorHandler(
      const node_embedding_handle_error_functor& error_handler);

  static node_embedding_status HandleError(const std::string& message,
                                           node_embedding_status status);

  static node_embedding_status HandleError(
      const std::vector<std::string>& messages, node_embedding_status status);

  static node_embedding_status HandleError(const char* message,
                                           const char* filename,
                                           int32_t line,
                                           node_embedding_status status);

  static std::string FormatString(const char* format, ...);

  static StdFunction<node_embedding_handle_error_functor>& ErrorHandler();

 private:
  static node_embedding_status DefaultErrorHandler(
      void* handler_data,
      const char* messages[],
      size_t messages_size,
      node_embedding_status status);
};

class EmbeddedPlatform {
 public:
  EmbeddedPlatform(int32_t argc, char* argv[]) noexcept
      : args_(argv, argv + argc) {}

  EmbeddedPlatform(const EmbeddedPlatform&) = delete;
  EmbeddedPlatform& operator=(const EmbeddedPlatform&) = delete;

  static node_embedding_status SetApiVersion(int32_t embedding_api_version,
                                             int32_t node_api_version);

  static node_embedding_status RunMain(
      int32_t argc,
      char* argv[],
      const node_embedding_configure_platform_functor_ref& configure_platform,
      const node_embedding_configure_runtime_functor_ref& configure_runtime);

  static node_embedding_status Create(
      int32_t argc,
      char* argv[],
      const node_embedding_configure_platform_functor_ref& configure_platform,
      node_embedding_platform* result);

  node_embedding_status DeleteMe();

  node_embedding_status SetFlags(node_embedding_platform_flags flags);

  node_embedding_status Initialize(
      const node_embedding_configure_platform_functor_ref& configure_platform,
      bool* early_return);

  node_embedding_status GetParsedArgs(
      const node_embedding_get_args_functor_ref& get_args,
      const node_embedding_get_args_functor_ref& get_exec_args);

  node::InitializationResult* init_result() { return init_result_.get(); }

  node::MultiIsolatePlatform* get_v8_platform() { return v8_platform_.get(); }

  static int32_t embedding_api_version() {
    return embedding_api_version_ == 0 ? NODE_EMBEDDING_VERSION
                                       : embedding_api_version_;
  }

  static int32_t node_api_version() {
    return node_api_version_ == 0 ? NAPI_VERSION : node_api_version_;
  }

 private:
  static node::ProcessInitializationFlags::Flags GetProcessInitializationFlags(
      node_embedding_platform_flags flags);

 private:
  bool is_initialized_{false};
  bool v8_is_initialized_{false};
  bool v8_is_uninitialized_{false};
  node_embedding_platform_flags flags_;
  std::vector<std::string> args_;
  struct {
    bool flags : 1;
  } optional_bits_{};

  std::shared_ptr<node::InitializationResult> init_result_;
  std::unique_ptr<node::MultiIsolatePlatform> v8_platform_;

  static int32_t embedding_api_version_;
  static int32_t node_api_version_;
};

int32_t EmbeddedPlatform::embedding_api_version_{};
int32_t EmbeddedPlatform::node_api_version_{};

class EmbeddedRuntime {
 public:
  explicit EmbeddedRuntime(EmbeddedPlatform* platform);

  EmbeddedRuntime(const EmbeddedRuntime&) = delete;
  EmbeddedRuntime& operator=(const EmbeddedRuntime&) = delete;

  static node_embedding_status Run(
      node_embedding_platform platform,
      const node_embedding_configure_runtime_functor_ref& configure_runtime);

  static node_embedding_status Create(
      node_embedding_platform platform,
      const node_embedding_configure_runtime_functor_ref& configure_runtime,
      node_embedding_runtime* result);

  node_embedding_status DeleteMe();

  node_embedding_status SetFlags(node_embedding_runtime_flags flags);

  node_embedding_status SetArgs(int32_t argc,
                                const char* argv[],
                                int32_t exec_argc,
                                const char* exec_argv[]);

  node_embedding_status OnPreload(
      const node_embedding_preload_functor& run_preload);

  node_embedding_status OnStartExecution(
      const node_embedding_start_execution_functor& start_execution,
      const node_embedding_handle_result_functor& handle_result);

  node_embedding_status AddModule(
      const char* module_name,
      const node_embedding_initialize_module_functor& init_module,
      int32_t module_node_api_version);

  node_embedding_status Initialize(
      const node_embedding_configure_runtime_functor_ref& configure_runtime);

  node_embedding_status SetTaskRunner(
      const node_embedding_post_task_functor& post_task);

  node_embedding_status RunEventLoop(
      node_embedding_event_loop_run_mode run_mode, bool* has_more_work);

  node_embedding_status CompleteEventLoop();
  node_embedding_status TerminateEventLoop();

  node_embedding_status RunNodeApi(
      const node_embedding_run_node_api_functor_ref& run_node_api);

  node_embedding_status OpenNodeApiScope(
      node_embedding_node_api_scope* node_api_scope, napi_env* env);
  node_embedding_status CloseNodeApiScope(
      node_embedding_node_api_scope node_api_scope);
  bool IsNodeApiScopeOpened() const;

  static napi_env GetOrCreateNodeApiEnv(node::Environment* node_env,
                                        const std::string& module_filename);

  size_t OpenV8Scope();
  void CloseV8Scope(size_t nest_level);

 private:
  static void TriggerFatalException(napi_env env,
                                    v8::Local<v8::Value> local_err);
  static node::EnvironmentFlags::Flags GetEnvironmentFlags(
      node_embedding_runtime_flags flags);

  void RegisterModules();

  static void RegisterModule(v8::Local<v8::Object> exports,
                             v8::Local<v8::Value> module,
                             v8::Local<v8::Context> context,
                             void* priv);

  uv_loop_t* EventLoop();
  void InitializePollingThread();
  void DestroyPollingThread();
  void WakeupPollingThread();
  static void RunPollingThread(void* data);
  void PollEvents();

 private:
  struct ModuleInfo {
    node_embedding_runtime runtime;
    std::string module_name;
    StdFunction<node_embedding_initialize_module_functor> init_module;
    int32_t module_node_api_version;
  };

  struct SharedData {
    std::mutex mutex;
    std::unordered_map<node::Environment*, napi_env> node_env_to_node_api_env;

    static SharedData& Get() {
      static SharedData shared_data;
      return shared_data;
    }
  };

  struct V8ScopeLocker {
    explicit V8ScopeLocker(EmbeddedRuntime& runtime)
        : runtime_(runtime), nest_level_(runtime_.OpenV8Scope()) {}

    ~V8ScopeLocker() { runtime_.CloseV8Scope(nest_level_); }

    V8ScopeLocker(const V8ScopeLocker&) = delete;
    V8ScopeLocker& operator=(const V8ScopeLocker&) = delete;

   private:
    EmbeddedRuntime& runtime_;
    size_t nest_level_;
  };

  struct V8ScopeData {
    V8ScopeData(node::CommonEnvironmentSetup* env_setup)
        : isolate_(env_setup->isolate()),
          v8_locker_(env_setup->isolate()),
          isolate_scope_(env_setup->isolate()),
          handle_scope_(env_setup->isolate()),
          context_scope_(env_setup->context()) {}

    bool IsLocked() const { return v8::Locker::IsLocked(isolate_); }

    size_t IncrementNestLevel() { return ++nest_level_; }

    bool DecrementNestLevel() { return --nest_level_ == 0; }

    size_t nest_level() const { return nest_level_; }

   private:
    int32_t nest_level_{1};
    v8::Isolate* isolate_;
    v8::Locker v8_locker_;  // TODO(vmoroz): can we remove it?
    v8::Isolate::Scope isolate_scope_;
    v8::HandleScope handle_scope_;
    v8::Context::Scope context_scope_;
  };

  struct NodeApiScopeData {
    napi_env__::CallModuleScopeData module_scope_data_;
    size_t v8_scope_nest_level_;
  };

 private:
  EmbeddedPlatform* platform_;
  bool is_initialized_{false};
  node_embedding_runtime_flags flags_{node_embedding_runtime_flags_default};
  std::vector<std::string> args_;
  std::vector<std::string> exec_args_;
  node::EmbedderPreloadCallback preload_cb_{};
  node::StartExecutionCallback start_execution_cb_{};
  StdFunction<node_embedding_handle_result_functor> handle_result_{};
  napi_env node_api_env_{};

  struct {
    bool flags : 1;
    bool args : 1;
    bool exec_args : 1;
  } optional_bits_{};

  std::unordered_map<std::string, ModuleInfo> modules_;

  std::unique_ptr<node::CommonEnvironmentSetup> env_setup_;
  std::optional<V8ScopeData> v8_scope_data_;

  StdFunction<node_embedding_post_task_functor> post_task_{};
  uv_async_t polling_async_handle_{};
  uv_sem_t polling_sem_{};
  uv_thread_t polling_thread_{};
  bool polling_thread_closed_{false};

  SmallTrivialStack<NodeApiScopeData> node_api_scope_data_{};
};

//-----------------------------------------------------------------------------
// EmbeddedErrorHandling implementation.
//-----------------------------------------------------------------------------

node_embedding_status EmbeddedErrorHandling::SetErrorHandler(
    const node_embedding_handle_error_functor& error_handler) {
  ErrorHandler() = AsStdFunction(error_handler);
  return node_embedding_status_ok;
}

node_embedding_status EmbeddedErrorHandling::HandleError(
    const std::string& message, node_embedding_status status) {
  const char* message_c_str = message.c_str();
  return ErrorHandler()(&message_c_str, 1, status);
}

node_embedding_status EmbeddedErrorHandling::HandleError(
    const std::vector<std::string>& messages, node_embedding_status status) {
  CStringArray message_arr(messages);
  return ErrorHandler()(message_arr.c_strs(), message_arr.size(), status);
}

node_embedding_status EmbeddedErrorHandling::HandleError(
    const char* message,
    const char* filename,
    int32_t line,
    node_embedding_status status) {
  return HandleError(
      FormatString("Error: %s at %s:%d", message, filename, line), status);
}

node_embedding_status EmbeddedErrorHandling::DefaultErrorHandler(
    void* /*handler_data*/,
    const char* messages[],
    size_t messages_size,
    node_embedding_status status) {
  FILE* stream = status != node_embedding_status_ok ? stderr : stdout;
  for (size_t i = 0; i < messages_size; ++i) {
    fprintf(stream, "%s\n", messages[i]);
  }
  fflush(stream);
  return status;
}

std::string EmbeddedErrorHandling::FormatString(const char* format, ...) {
  va_list args1;
  va_start(args1, format);
  va_list args2;
  va_copy(args2, args1);  // Required for some compilers like GCC.
  std::string result(std::vsnprintf(nullptr, 0, format, args1), '\0');
  va_end(args1);
  std::vsnprintf(&result[0], result.size() + 1, format, args2);
  va_end(args2);
  return result;
}

StdFunction<node_embedding_handle_error_functor>&
EmbeddedErrorHandling::ErrorHandler() {
  static StdFunction<node_embedding_handle_error_functor> error_handler =
      AsStdFunction(node_embedding_handle_error_functor{
          nullptr, &DefaultErrorHandler, nullptr});
  return error_handler;
}

//-----------------------------------------------------------------------------
// EmbeddedPlatform implementation.
//-----------------------------------------------------------------------------

/*static*/ node_embedding_status EmbeddedPlatform::SetApiVersion(
    int32_t embedding_api_version, int32_t node_api_version) {
  ASSERT_ARG(embedding_api_version,
             embedding_api_version > 0 &&
                 embedding_api_version <= NODE_EMBEDDING_VERSION);
  ASSERT_ARG(node_api_version,
             node_api_version >= NODE_API_DEFAULT_MODULE_API_VERSION &&
                 (node_api_version <= NAPI_VERSION ||
                  node_api_version == NAPI_VERSION_EXPERIMENTAL));

  embedding_api_version_ = embedding_api_version;
  node_api_version_ = node_api_version;

  return node_embedding_status_ok;
}

node_embedding_status EmbeddedPlatform::RunMain(
    int32_t argc,
    char* argv[],
    const node_embedding_configure_platform_functor_ref& configure_platform,
    const node_embedding_configure_runtime_functor_ref& configure_runtime) {
  node_embedding_platform platform{};
  CHECK_STATUS(
      EmbeddedPlatform::Create(argc, argv, configure_platform, &platform));
  if (platform == nullptr) {
    return node_embedding_status_ok;  // early return
  }
  return EmbeddedRuntime::Run(platform, configure_runtime);
}

/*static*/ node_embedding_status EmbeddedPlatform::Create(
    int32_t argc,
    char* argv[],
    const node_embedding_configure_platform_functor_ref& configure_platform,
    node_embedding_platform* result) {
  CHECK_ARG_NOT_NULL(result);

  // Hack around with the argv pointer. Used for process.title = "blah".
  argv = uv_setup_args(argc, argv);

  auto platform_ptr = std::make_unique<EmbeddedPlatform>(argc, argv);
  bool early_return = false;
  CHECK_STATUS(platform_ptr->Initialize(configure_platform, &early_return));
  if (early_return) {
    return platform_ptr.release()->DeleteMe();
  }

  // The initialization was successful, the caller is responsible for deleting
  // the platform instance.
  *result = reinterpret_cast<node_embedding_platform>(platform_ptr.release());
  return node_embedding_status_ok;
}

node_embedding_status EmbeddedPlatform::DeleteMe() {
  if (v8_is_initialized_ && !v8_is_uninitialized_) {
    v8_is_uninitialized_ = true;
    v8::V8::Dispose();
    v8::V8::DisposePlatform();
    node::TearDownOncePerProcess();
  }

  delete this;
  return node_embedding_status_ok;
}

node_embedding_status EmbeddedPlatform::SetFlags(
    node_embedding_platform_flags flags) {
  ASSERT(!is_initialized_);
  flags_ = flags;
  optional_bits_.flags = true;
  return node_embedding_status_ok;
}

node_embedding_status EmbeddedPlatform::Initialize(
    const node_embedding_configure_platform_functor_ref& configure_platform,
    bool* early_return) {
  ASSERT(!is_initialized_);

  node_embedding_platform_config platform_config =
      reinterpret_cast<node_embedding_platform_config>(this);
  if (configure_platform.invoke != nullptr) {
    CHECK_STATUS(
        configure_platform.invoke(configure_platform.data, platform_config));
  }

  is_initialized_ = true;

  if (!optional_bits_.flags) {
    flags_ = node_embedding_platform_flags_none;
  }

  init_result_ = node::InitializeOncePerProcess(
      args_, GetProcessInitializationFlags(flags_));
  int32_t exit_code = init_result_->exit_code();
  if (exit_code != 0 || !init_result_->errors().empty()) {
    CHECK_STATUS(EmbeddedErrorHandling::HandleError(
        init_result_->errors(),
        exit_code != 0 ? static_cast<node_embedding_status>(
                             node_embedding_status_error_exit_code + exit_code)
                       : node_embedding_status_ok));
  }

  if (init_result_->early_return()) {
    *early_return = true;
    return node_embedding_status_ok;
  }

  int32_t thread_pool_size =
      static_cast<int32_t>(node::per_process::cli_options->v8_thread_pool_size);
  v8_platform_ = node::MultiIsolatePlatform::Create(thread_pool_size);
  v8::V8::InitializePlatform(v8_platform_.get());
  v8::V8::Initialize();

  v8_is_initialized_ = true;

  return node_embedding_status_ok;
}

node_embedding_status EmbeddedPlatform::GetParsedArgs(
    const node_embedding_get_args_functor_ref& get_args,
    const node_embedding_get_args_functor_ref& get_exec_args) {
  ASSERT(is_initialized_);

  if (get_args.invoke != nullptr) {
    node::CStringArray args(init_result_->args());
    get_args.invoke(get_args.data, args.argc(), args.argv());
  }

  if (get_exec_args.invoke != nullptr) {
    node::CStringArray exec_args(init_result_->exec_args());
    get_exec_args.invoke(
        get_exec_args.data, exec_args.argc(), exec_args.argv());
  }

  return node_embedding_status_ok;
}

node::ProcessInitializationFlags::Flags
EmbeddedPlatform::GetProcessInitializationFlags(
    node_embedding_platform_flags flags) {
  uint32_t result = node::ProcessInitializationFlags::kNoFlags;
  if ((flags & node_embedding_platform_flags_enable_stdio_inheritance) != 0) {
    result |= node::ProcessInitializationFlags::kEnableStdioInheritance;
  }
  if ((flags & node_embedding_platform_flags_disable_node_options_env) != 0) {
    result |= node::ProcessInitializationFlags::kDisableNodeOptionsEnv;
  }
  if ((flags & node_embedding_platform_flags_disable_cli_options) != 0) {
    result |= node::ProcessInitializationFlags::kDisableCLIOptions;
  }
  if ((flags & node_embedding_platform_flags_no_icu) != 0) {
    result |= node::ProcessInitializationFlags::kNoICU;
  }
  if ((flags & node_embedding_platform_flags_no_stdio_initialization) != 0) {
    result |= node::ProcessInitializationFlags::kNoStdioInitialization;
  }
  if ((flags & node_embedding_platform_flags_no_default_signal_handling) != 0) {
    result |= node::ProcessInitializationFlags::kNoDefaultSignalHandling;
  }
  result |= node::ProcessInitializationFlags::kNoInitializeV8;
  result |= node::ProcessInitializationFlags::kNoInitializeNodeV8Platform;
  if ((flags & node_embedding_platform_flags_no_init_openssl) != 0) {
    result |= node::ProcessInitializationFlags::kNoInitOpenSSL;
  }
  if ((flags & node_embedding_platform_flags_no_parse_global_debug_variables) !=
      0) {
    result |= node::ProcessInitializationFlags::kNoParseGlobalDebugVariables;
  }
  if ((flags & node_embedding_platform_flags_no_adjust_resource_limits) != 0) {
    result |= node::ProcessInitializationFlags::kNoAdjustResourceLimits;
  }
  if ((flags & node_embedding_platform_flags_no_use_large_pages) != 0) {
    result |= node::ProcessInitializationFlags::kNoUseLargePages;
  }
  if ((flags & node_embedding_platform_flags_no_print_help_or_version_output) !=
      0) {
    result |= node::ProcessInitializationFlags::kNoPrintHelpOrVersionOutput;
  }
  if ((flags & node_embedding_platform_flags_generate_predictable_snapshot) !=
      0) {
    result |= node::ProcessInitializationFlags::kGeneratePredictableSnapshot;
  }
  return static_cast<node::ProcessInitializationFlags::Flags>(result);
}

//-----------------------------------------------------------------------------
// EmbeddedRuntime implementation.
//-----------------------------------------------------------------------------

/*static*/ node_embedding_status EmbeddedRuntime::Run(
    node_embedding_platform platform,
    const node_embedding_configure_runtime_functor_ref& configure_runtime) {
  node_embedding_runtime runtime{};
  CHECK_STATUS(Create(platform, configure_runtime, &runtime));
  CHECK_STATUS(node_embedding_complete_event_loop(runtime));
  CHECK_STATUS(node_embedding_delete_runtime(runtime));
  return node_embedding_status_ok;
}

/*static*/ node_embedding_status EmbeddedRuntime::Create(
    node_embedding_platform platform,
    const node_embedding_configure_runtime_functor_ref& configure_runtime,
    node_embedding_runtime* result) {
  CHECK_ARG_NOT_NULL(platform);
  CHECK_ARG_NOT_NULL(result);

  EmbeddedPlatform* platform_ptr =
      reinterpret_cast<EmbeddedPlatform*>(platform);
  std::unique_ptr<EmbeddedRuntime> runtime_ptr =
      std::make_unique<EmbeddedRuntime>(platform_ptr);

  CHECK_STATUS(runtime_ptr->Initialize(configure_runtime));

  *result = reinterpret_cast<node_embedding_runtime>(runtime_ptr.release());

  return node_embedding_status_ok;
}

EmbeddedRuntime::EmbeddedRuntime(EmbeddedPlatform* platform)
    : platform_(platform) {}

node_embedding_status EmbeddedRuntime::DeleteMe() {
  ASSERT(!IsNodeApiScopeOpened());

  std::unique_ptr<node::CommonEnvironmentSetup> env_setup =
      std::move(env_setup_);
  if (env_setup != nullptr) {
    node::Stop(env_setup->env());
  }

  delete this;
  return node_embedding_status_ok;
}

node_embedding_status EmbeddedRuntime::SetFlags(
    node_embedding_runtime_flags flags) {
  ASSERT(!is_initialized_);
  flags_ = flags;
  optional_bits_.flags = true;
  return node_embedding_status_ok;
}

node_embedding_status EmbeddedRuntime::SetArgs(int32_t argc,
                                               const char* argv[],
                                               int32_t exec_argc,
                                               const char* exec_argv[]) {
  ASSERT(!is_initialized_);
  if (argv != nullptr) {
    args_.assign(argv, argv + argc);
    optional_bits_.args = true;
  }
  if (exec_argv != nullptr) {
    exec_args_.assign(exec_argv, exec_argv + exec_argc);
    optional_bits_.exec_args = true;
  }
  return node_embedding_status_ok;
}

node_embedding_status EmbeddedRuntime::OnPreload(
    const node_embedding_preload_functor& run_preload) {
  ASSERT(!is_initialized_);

  if (run_preload.invoke != nullptr) {
    preload_cb_ = node::EmbedderPreloadCallback(
        [runtime = reinterpret_cast<node_embedding_runtime>(this),
         run_preload_ptr = MakeSharedFunctorPtr(run_preload)](
            node::Environment* node_env,
            v8::Local<v8::Value> process,
            v8::Local<v8::Value> require) {
          napi_env env = GetOrCreateNodeApiEnv(node_env, "<worker thread>");
          env->CallIntoModule(
              [&](napi_env env) {
                napi_value process_value =
                    v8impl::JsValueFromV8LocalValue(process);
                napi_value require_value =
                    v8impl::JsValueFromV8LocalValue(require);
                run_preload_ptr->invoke(run_preload_ptr->data,
                                        runtime,
                                        env,
                                        process_value,
                                        require_value);
              },
              TriggerFatalException);
        });
  } else {
    preload_cb_ = {};
  }

  return node_embedding_status_ok;
}

node_embedding_status EmbeddedRuntime::OnStartExecution(
    const node_embedding_start_execution_functor& start_execution,
    const node_embedding_handle_result_functor& handle_result) {
  ASSERT(!is_initialized_);

  if (start_execution.invoke != nullptr) {
    start_execution_cb_ = node::StartExecutionCallback(
        [this, start_execution_ptr = MakeSharedFunctorPtr(start_execution)](
            const node::StartExecutionCallbackInfo& info)
            -> v8::MaybeLocal<v8::Value> {
          napi_value result{};
          node_api_env_->CallIntoModule(
              [&](napi_env env) {
                napi_value process_value =
                    v8impl::JsValueFromV8LocalValue(info.process_object);
                napi_value require_value =
                    v8impl::JsValueFromV8LocalValue(info.native_require);
                napi_value run_cjs_value =
                    v8impl::JsValueFromV8LocalValue(info.run_cjs);
                result = start_execution_ptr->invoke(
                    start_execution_ptr->data,
                    reinterpret_cast<node_embedding_runtime>(this),
                    env,
                    process_value,
                    require_value,
                    run_cjs_value);
              },
              TriggerFatalException);

          if (result == nullptr)
            return {};
          else
            return v8impl::V8LocalValueFromJsValue(result);
        });
  } else {
    start_execution_cb_ = {};
  }

  handle_result_ = AsStdFunction(handle_result);

  return node_embedding_status_ok;
}

node_embedding_status EmbeddedRuntime::AddModule(
    const char* module_name,
    const node_embedding_initialize_module_functor& init_module,
    int32_t module_node_api_version) {
  CHECK_ARG_NOT_NULL(module_name);
  CHECK_ARG_NOT_NULL(init_module.invoke);
  ASSERT(!is_initialized_);

  auto insert_result =
      modules_.try_emplace(module_name,
                           reinterpret_cast<node_embedding_runtime>(this),
                           module_name,
                           AsStdFunction(init_module),
                           module_node_api_version);
  if (!insert_result.second) {
    return EmbeddedErrorHandling::HandleError(
        EmbeddedErrorHandling::FormatString(
            "Module with name '%s' is already added.", module_name),
        node_embedding_status_bad_arg);
  }

  return node_embedding_status_ok;
}

node_embedding_status EmbeddedRuntime::Initialize(
    const node_embedding_configure_runtime_functor_ref& configure_runtime) {
  ASSERT(!is_initialized_);

  if (configure_runtime.invoke != nullptr) {
    CHECK_STATUS(configure_runtime.invoke(
        configure_runtime.data,
        reinterpret_cast<node_embedding_platform>(platform_),
        reinterpret_cast<node_embedding_runtime_config>(this)));
  }

  is_initialized_ = true;

  node::EnvironmentFlags::Flags flags = GetEnvironmentFlags(
      optional_bits_.flags ? flags_ : node_embedding_runtime_flags_default);

  const std::vector<std::string>& args =
      optional_bits_.args ? args_ : platform_->init_result()->args();

  const std::vector<std::string>& exec_args =
      optional_bits_.exec_args ? exec_args_
                               : platform_->init_result()->exec_args();

  node::MultiIsolatePlatform* v8_platform = platform_->get_v8_platform();

  std::vector<std::string> errors;
  env_setup_ = node::CommonEnvironmentSetup::Create(
      v8_platform, &errors, args, exec_args, flags);

  if (env_setup_ == nullptr || !errors.empty()) {
    return EmbeddedErrorHandling::HandleError(
        errors, node_embedding_status_generic_error);
  }

  V8ScopeLocker v8_scope_locker(*this);

  std::string filename = args_.size() > 1 ? args_[1] : "<internal>";
  node_api_env_ = GetOrCreateNodeApiEnv(env_setup_->env(), filename);

  node::Environment* node_env = env_setup_->env();

  RegisterModules();

  v8::MaybeLocal<v8::Value> ret =
      node::LoadEnvironment(node_env, start_execution_cb_, preload_cb_);

  if (ret.IsEmpty())
    return EmbeddedErrorHandling::HandleError(
        "Failed to load environment", node_embedding_status_generic_error);

  if (handle_result_) {
    node_api_env_->CallIntoModule(
        [&](napi_env env) {
          handle_result_(reinterpret_cast<node_embedding_runtime>(this),
                         env,
                         v8impl::JsValueFromV8LocalValue(ret.ToLocalChecked()));
        },
        TriggerFatalException);
  }

  InitializePollingThread();
  WakeupPollingThread();

  return node_embedding_status_ok;
}

uv_loop_t* EmbeddedRuntime::EventLoop() {
  return env_setup_->env()->event_loop();
}

void EmbeddedRuntime::InitializePollingThread() {
  if (post_task_ == nullptr) return;

  uv_loop_t* event_loop = EventLoop();

  {
#if defined(_WIN32)

    SYSTEM_INFO system_info = {};
    ::GetNativeSystemInfo(&system_info);

    // on single-core the IO completion port NumberOfConcurrentThreads needs to
    // be 2 to avoid CPU pegging likely caused by a busy loop in PollEvents
    if (system_info.dwNumberOfProcessors == 1) {
      // the expectation is the event_loop has just been initialized
      // which makes IOCP replacement safe
      CHECK_EQ(0u, event_loop->active_handles);
      CHECK_EQ(0u, event_loop->active_reqs.count);

      if (event_loop->iocp && event_loop->iocp != INVALID_HANDLE_VALUE)
        ::CloseHandle(event_loop->iocp);
      event_loop->iocp =
          ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 2);
    }

#elif defined(__APPLE__)

    // Do nothing

#elif defined(__linux)

    int backend_fd = uv_backend_fd(event_loop);
    struct epoll_event ev = {0};
    ev.events = EPOLLIN;
    ev.data.fd = backend_fd;
    epoll_ctl(epoll_, EPOLL_CTL_ADD, backend_fd, &ev);

#else
    ERROR_AND_ABORT("The platform is not supported yet.");
#endif
  }

  // keep the loop alive and allow waking up the polling thread
  uv_async_init(event_loop, &polling_async_handle_, nullptr);

  // Start worker thread that will post to the task runner when new uv events
  // arrive.
  polling_thread_closed_ = false;
  uv_sem_init(&polling_sem_, 0);
  uv_thread_create(&polling_thread_, RunPollingThread, this);
}

void EmbeddedRuntime::DestroyPollingThread() {
  if (post_task_ == nullptr) return;
  if (polling_thread_closed_) return;

  polling_thread_closed_ = true;
  uv_sem_post(&polling_sem_);
  // Wake up polling thread.
  uv_async_send(&polling_async_handle_);
  // Wait for polling thread to complete.
  uv_thread_join(&polling_thread_);

  // Clear uv.
  uv_sem_destroy(&polling_sem_);
  uv_close(reinterpret_cast<uv_handle_t*>(&polling_async_handle_), nullptr);
}

void EmbeddedRuntime::WakeupPollingThread() {
  if (post_task_ == nullptr) return;
  if (polling_thread_closed_) return;

  uv_sem_post(&polling_sem_);
}

void EmbeddedRuntime::RunPollingThread(void* data) {
  EmbeddedRuntime* runtime = static_cast<EmbeddedRuntime*>(data);
  for (;;) {
    // Wait for the task runner to deal with events.
    uv_sem_wait(&runtime->polling_sem_);
    if (runtime->polling_thread_closed_) break;

    // Wait for something to happen in uv loop.
    runtime->PollEvents();
    if (runtime->polling_thread_closed_) break;

    // Deal with event in the task runner thread.
    runtime->post_task_(
        node::AsFunctor<node_embedding_run_task_functor>([runtime]() {
          runtime->RunEventLoop(node_embedding_event_loop_run_mode_nowait,
                                nullptr);
        }));
  }
}

void EmbeddedRuntime::PollEvents() {
  uv_loop_t* event_loop = EventLoop();

  // If there are other kinds of events pending, uv_backend_timeout will
  // instruct us not to wait.
  int timeout = uv_backend_timeout(event_loop);

#if defined(_WIN32)

  DWORD timeout_msec = static_cast<DWORD>(timeout);
  DWORD byte_count;
  ULONG_PTR completion_key;
  OVERLAPPED* overlapped;
  ::GetQueuedCompletionStatus(event_loop->iocp,
                              &byte_count,
                              &completion_key,
                              &overlapped,
                              timeout_msec);

  // Give the event back so libuv can deal with it.
  if (overlapped != nullptr)
    ::PostQueuedCompletionStatus(
        event_loop->iocp, byte_count, completion_key, overlapped);

#elif defined(__APPLE__)

  struct timeval tv;
  if (timeout != -1) {
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;
  }

  fd_set readset;
  int fd = uv_backend_fd(event_loop);
  FD_ZERO(&readset);
  FD_SET(fd, &readset);

  // Wait for new libuv events.
  int r;
  do {
    r = select(
        fd + 1, &readset, nullptr, nullptr, timeout == -1 ? nullptr : &tv);
  } while (r == -1 && errno == EINTR);

#elif defined(__linux)

  // Wait for new libuv events.
  int r;
  do {
    struct epoll_event ev;
    r = epoll_wait(epoll_, &ev, 1, timeout);
  } while (r == -1 && errno == EINTR);

#else
  ERROR_AND_ABORT("The platform is not supported yet.");
#endif
}

node_embedding_status EmbeddedRuntime::SetTaskRunner(
    const node_embedding_post_task_functor& post_task) {
  ASSERT(!is_initialized_);
  post_task_ = AsStdFunction(post_task);
  return node_embedding_status_ok;
}

node_embedding_status EmbeddedRuntime::RunEventLoop(
    node_embedding_event_loop_run_mode run_mode, bool* has_more_work) {
  ASSERT(is_initialized_);

  V8ScopeLocker v8_scope_locker(*this);

  node::ExitCode exit_code =
      node::SpinEventLoopWithoutCleanup(env_setup_->env(),
                                        static_cast<uv_run_mode>(run_mode))
          .FromMaybe(node::ExitCode::kGenericUserError);
  if (exit_code != node::ExitCode::kNoFailure) {
    return EmbeddedErrorHandling::HandleError(
        "Failed running the event loop",
        static_cast<node_embedding_status>(
            node_embedding_status_error_exit_code +
            static_cast<node_embedding_status>(exit_code)));
  }

  if (has_more_work != nullptr) {
    *has_more_work = uv_loop_alive(env_setup_->env()->event_loop());
  }

  WakeupPollingThread();

  return node_embedding_status_ok;
}

node_embedding_status EmbeddedRuntime::CompleteEventLoop() {
  ASSERT(is_initialized_);

  V8ScopeLocker v8_scope_locker(*this);

  DestroyPollingThread();

  int32_t exit_code = node::SpinEventLoop(env_setup_->env()).FromMaybe(1);
  if (exit_code != 0) {
    return EmbeddedErrorHandling::HandleError(
        "Failed while closing the runtime",
        static_cast<node_embedding_status>(
            node_embedding_status_error_exit_code + exit_code));
  }

  return node_embedding_status_ok;
}

node_embedding_status EmbeddedRuntime::TerminateEventLoop() {
  ASSERT(is_initialized_);

  V8ScopeLocker v8_scope_locker(*this);
  int32_t exit_code = node::Stop(env_setup_->env(), node::StopFlags::kNoFlags);
  if (exit_code != 0) {
    return EmbeddedErrorHandling::HandleError(
        "Failed while stopping the runtime",
        static_cast<node_embedding_status>(
            node_embedding_status_error_exit_code + exit_code));
  }
  return node_embedding_status_ok;
}

/*static*/ void EmbeddedRuntime::TriggerFatalException(
    napi_env env, v8::Local<v8::Value> local_err) {
  node_napi_env__* node_napi_env = static_cast<node_napi_env__*>(env);
  if (node_napi_env->terminatedOrTerminating()) {
    return;
  }
  // If there was an unhandled exception while calling Node-API,
  // report it as a fatal exception. (There is no JavaScript on the
  // call stack that can possibly handle it.)
  node_napi_env->trigger_fatal_exception(local_err);
}

size_t EmbeddedRuntime::OpenV8Scope() {
  if (v8_scope_data_.has_value()) {
    CHECK(v8_scope_data_->IsLocked());
    return v8_scope_data_->IncrementNestLevel();
  }

  v8_scope_data_.emplace(env_setup_.get());
  return 1;
}

void EmbeddedRuntime::CloseV8Scope(size_t nest_level) {
  CHECK(v8_scope_data_.has_value());
  CHECK_EQ(v8_scope_data_->nest_level(), nest_level);
  if (v8_scope_data_->DecrementNestLevel()) {
    v8_scope_data_.reset();
  }
}

node_embedding_status EmbeddedRuntime::RunNodeApi(
    const node_embedding_run_node_api_functor_ref& run_node_api) {
  CHECK_ARG_NOT_NULL(run_node_api.invoke);

  node_embedding_node_api_scope node_api_scope{};
  napi_env env{};
  CHECK_STATUS(OpenNodeApiScope(&node_api_scope, &env));
  auto nodeApiScopeLeave =
      node::OnScopeLeave([&]() { CloseNodeApiScope(node_api_scope); });

  run_node_api.invoke(
      run_node_api.data, reinterpret_cast<node_embedding_runtime>(this), env);

  return node_embedding_status_ok;
}

node_embedding_status EmbeddedRuntime::OpenNodeApiScope(
    node_embedding_node_api_scope* node_api_scope, napi_env* env) {
  CHECK_ARG_NOT_NULL(node_api_scope);
  CHECK_ARG_NOT_NULL(env);

  size_t v8_scope_nest_level = OpenV8Scope();
  node_api_scope_data_.Push(
      {node_api_env_->OpenCallModuleScope(), v8_scope_nest_level});

  *node_api_scope = reinterpret_cast<node_embedding_node_api_scope>(
      node_api_scope_data_.size());
  *env = node_api_env_;
  return node_embedding_status_ok;
}

node_embedding_status EmbeddedRuntime::CloseNodeApiScope(
    node_embedding_node_api_scope node_api_scope) {
  CHECK_EQ(node_api_scope_data_.size(),
           reinterpret_cast<size_t>(node_api_scope));
  size_t v8_scope_nest_level = node_api_scope_data_.top().v8_scope_nest_level_;

  node_api_env_->CloseCallModuleScope(
      node_api_scope_data_.top().module_scope_data_);
  node_api_scope_data_.Pop();
  CloseV8Scope(v8_scope_nest_level);

  return node_embedding_status_ok;
}

bool EmbeddedRuntime::IsNodeApiScopeOpened() const {
  return node_api_scope_data_.size() > 0;
}

napi_env EmbeddedRuntime::GetOrCreateNodeApiEnv(
    node::Environment* node_env, const std::string& module_filename) {
  SharedData& shared_data = SharedData::Get();

  {
    std::scoped_lock<std::mutex> lock(shared_data.mutex);
    auto it = shared_data.node_env_to_node_api_env.find(node_env);
    if (it != shared_data.node_env_to_node_api_env.end()) return it->second;
  }

  // Avoid creating the environment under the lock.
  napi_env env = v8impl::NewEnv(node_env->context(),
                                module_filename,
                                EmbeddedPlatform::node_api_version());

  // In case if we cannot insert the new env, we are just going to have an
  // unused env which will be deleted in the end with other environments.
  std::scoped_lock<std::mutex> lock(shared_data.mutex);
  auto insert_result =
      shared_data.node_env_to_node_api_env.try_emplace(node_env, env);

  // Return either the inserted or the existing environment.
  return insert_result.first->second;
}

node::EnvironmentFlags::Flags EmbeddedRuntime::GetEnvironmentFlags(
    node_embedding_runtime_flags flags) {
  uint64_t result = node::EnvironmentFlags::kNoFlags;
  if ((flags & node_embedding_runtime_flags_default) != 0) {
    result |= node::EnvironmentFlags::kDefaultFlags;
  }
  if ((flags & node_embedding_runtime_flags_owns_process_state) != 0) {
    result |= node::EnvironmentFlags::kOwnsProcessState;
  }
  if ((flags & node_embedding_runtime_flags_owns_inspector) != 0) {
    result |= node::EnvironmentFlags::kOwnsInspector;
  }
  if ((flags & node_embedding_runtime_flags_no_register_esm_loader) != 0) {
    result |= node::EnvironmentFlags::kNoRegisterESMLoader;
  }
  if ((flags & node_embedding_runtime_flags_track_unmanaged_fds) != 0) {
    result |= node::EnvironmentFlags::kTrackUnmanagedFds;
  }
  if ((flags & node_embedding_runtime_flags_hide_console_windows) != 0) {
    result |= node::EnvironmentFlags::kHideConsoleWindows;
  }
  if ((flags & node_embedding_runtime_flags_no_native_addons) != 0) {
    result |= node::EnvironmentFlags::kNoNativeAddons;
  }
  if ((flags & node_embedding_runtime_flags_no_global_search_paths) != 0) {
    result |= node::EnvironmentFlags::kNoGlobalSearchPaths;
  }
  if ((flags & node_embedding_runtime_flags_no_browser_globals) != 0) {
    result |= node::EnvironmentFlags::kNoBrowserGlobals;
  }
  if ((flags & node_embedding_runtime_flags_no_create_inspector) != 0) {
    result |= node::EnvironmentFlags::kNoCreateInspector;
  }
  if ((flags & node_embedding_runtime_flags_no_start_debug_signal_handler) !=
      0) {
    result |= node::EnvironmentFlags::kNoStartDebugSignalHandler;
  }
  if ((flags & node_embedding_runtime_flags_no_wait_for_inspector_frontend) !=
      0) {
    result |= node::EnvironmentFlags::kNoWaitForInspectorFrontend;
  }
  return static_cast<node::EnvironmentFlags::Flags>(result);
}

void EmbeddedRuntime::RegisterModules() {
  for (const auto& [module_name, module_info] : modules_) {
    node::node_module mod = {
        -1,                                     // nm_version for Node-API
        NM_F_LINKED,                            // nm_flags
        nullptr,                                // nm_dso_handle
        nullptr,                                // nm_filename
        nullptr,                                // nm_register_func
        RegisterModule,                         // nm_context_register_func
        module_name.c_str(),                    // nm_modname
        const_cast<ModuleInfo*>(&module_info),  // nm_priv
        nullptr                                 // nm_link
    };
    node::AddLinkedBinding(env_setup_->env(), mod);
  }
}

/*static*/ void EmbeddedRuntime::RegisterModule(v8::Local<v8::Object> exports,
                                                v8::Local<v8::Value> module,
                                                v8::Local<v8::Context> context,
                                                void* priv) {
  ModuleInfo* module_info = static_cast<ModuleInfo*>(priv);

  // Create a new napi_env for this specific module.
  napi_env env = v8impl::NewEnv(
      context, module_info->module_name, module_info->module_node_api_version);

  napi_value node_api_exports = nullptr;
  env->CallIntoModule([&](napi_env env) {
    node_api_exports =
        module_info->init_module(module_info->runtime,
                                 env,
                                 module_info->module_name.c_str(),
                                 v8impl::JsValueFromV8LocalValue(exports));
  });

  // If register function returned a non-null exports object different from
  // the exports object we passed it, set that as the "exports" property of
  // the module.
  if (node_api_exports != nullptr &&
      node_api_exports != v8impl::JsValueFromV8LocalValue(exports)) {
    napi_value node_api_module = v8impl::JsValueFromV8LocalValue(module);
    napi_set_named_property(env, node_api_module, "exports", node_api_exports);
  }
}

}  // end of anonymous namespace
}  // namespace node

node_embedding_status NAPI_CDECL
node_embedding_on_error(node_embedding_handle_error_functor error_handler) {
  return node::EmbeddedErrorHandling::SetErrorHandler(error_handler);
}

NAPI_EXTERN node_embedding_status NAPI_CDECL node_embedding_set_api_version(
    int32_t embedding_api_version, int32_t node_api_version) {
  return node::EmbeddedPlatform::SetApiVersion(embedding_api_version,
                                               node_api_version);
}

node_embedding_status NAPI_CDECL node_embedding_run_main(
    int32_t argc,
    char* argv[],
    node_embedding_configure_platform_functor_ref configure_platform,
    node_embedding_configure_runtime_functor_ref configure_runtime) {
  return node::EmbeddedPlatform::RunMain(
      argc, argv, configure_platform, configure_runtime);
}

node_embedding_status NAPI_CDECL node_embedding_create_platform(
    int32_t argc,
    char* argv[],
    node_embedding_configure_platform_functor_ref configure_platform,
    node_embedding_platform* result) {
  return node::EmbeddedPlatform::Create(argc, argv, configure_platform, result);
}

node_embedding_status NAPI_CDECL
node_embedding_delete_platform(node_embedding_platform platform) {
  return EMBEDDED_PLATFORM(platform)->DeleteMe();
}

node_embedding_status NAPI_CDECL node_embedding_platform_set_flags(
    node_embedding_platform_config platform_config,
    node_embedding_platform_flags flags) {
  return EMBEDDED_PLATFORM(platform_config)->SetFlags(flags);
}

node_embedding_status NAPI_CDECL node_embedding_platform_get_parsed_args(
    node_embedding_platform platform,
    node_embedding_get_args_functor_ref get_args,
    node_embedding_get_args_functor_ref get_runtime_args) {
  return EMBEDDED_PLATFORM(platform)->GetParsedArgs(get_args, get_runtime_args);
}

node_embedding_status NAPI_CDECL node_embedding_run_runtime(
    node_embedding_platform platform,
    node_embedding_configure_runtime_functor_ref configure_runtime) {
  return node::EmbeddedRuntime::Run(platform, configure_runtime);
}

node_embedding_status NAPI_CDECL node_embedding_create_runtime(
    node_embedding_platform platform,
    node_embedding_configure_runtime_functor_ref configure_runtime,
    node_embedding_runtime* result) {
  return node::EmbeddedRuntime::Create(platform, configure_runtime, result);
}

node_embedding_status NAPI_CDECL
node_embedding_delete_runtime(node_embedding_runtime runtime) {
  return EMBEDDED_RUNTIME(runtime)->DeleteMe();
}

node_embedding_status NAPI_CDECL
node_embedding_runtime_set_flags(node_embedding_runtime_config runtime_config,
                                 node_embedding_runtime_flags flags) {
  return EMBEDDED_RUNTIME(runtime_config)->SetFlags(flags);
}

node_embedding_status NAPI_CDECL
node_embedding_runtime_set_args(node_embedding_runtime_config runtime_config,
                                int32_t argc,
                                const char* argv[],
                                int32_t runtime_argc,
                                const char* runtime_argv[]) {
  return EMBEDDED_RUNTIME(runtime_config)
      ->SetArgs(argc, argv, runtime_argc, runtime_argv);
}

node_embedding_status NAPI_CDECL
node_embedding_runtime_on_preload(node_embedding_runtime_config runtime_config,
                                  node_embedding_preload_functor run_preload) {
  return EMBEDDED_RUNTIME(runtime_config)->OnPreload(run_preload);
}

node_embedding_status NAPI_CDECL node_embedding_runtime_on_start_execution(
    node_embedding_runtime_config runtime_config,
    node_embedding_start_execution_functor start_execution,
    node_embedding_handle_result_functor handle_result) {
  return EMBEDDED_RUNTIME(runtime_config)
      ->OnStartExecution(start_execution, handle_result);
}

node_embedding_status NAPI_CDECL node_embedding_runtime_add_module(
    node_embedding_runtime_config runtime_config,
    const char* module_name,
    node_embedding_initialize_module_functor init_module,
    int32_t module_node_api_version) {
  return EMBEDDED_RUNTIME(runtime_config)
      ->AddModule(module_name, init_module, module_node_api_version);
}

node_embedding_status NAPI_CDECL node_embedding_runtime_set_task_runner(
    node_embedding_runtime_config runtime_config,
    node_embedding_post_task_functor post_task) {
  return EMBEDDED_RUNTIME(runtime_config)->SetTaskRunner(post_task);
}

node_embedding_status NAPI_CDECL
node_embedding_run_event_loop(node_embedding_runtime runtime,
                              node_embedding_event_loop_run_mode run_mode,
                              bool* has_more_work) {
  return EMBEDDED_RUNTIME(runtime)->RunEventLoop(run_mode, has_more_work);
}

node_embedding_status NAPI_CDECL
node_embedding_complete_event_loop(node_embedding_runtime runtime) {
  return EMBEDDED_RUNTIME(runtime)->CompleteEventLoop();
}

node_embedding_status NAPI_CDECL
node_embedding_terminate_event_loop(node_embedding_runtime runtime) {
  return EMBEDDED_RUNTIME(runtime)->TerminateEventLoop();
}

node_embedding_status NAPI_CDECL node_embedding_run_node_api(
    node_embedding_runtime runtime,
    node_embedding_run_node_api_functor_ref run_node_api) {
  return EMBEDDED_RUNTIME(runtime)->RunNodeApi(run_node_api);
}

node_embedding_status NAPI_CDECL node_embedding_open_node_api_scope(
    node_embedding_runtime runtime,
    node_embedding_node_api_scope* node_api_scope,
    napi_env* env) {
  return EMBEDDED_RUNTIME(runtime)->OpenNodeApiScope(node_api_scope, env);
}

node_embedding_status NAPI_CDECL node_embedding_close_node_api_scope(
    node_embedding_runtime runtime,
    node_embedding_node_api_scope node_api_scope) {
  return EMBEDDED_RUNTIME(runtime)->CloseNodeApiScope(node_api_scope);
}
