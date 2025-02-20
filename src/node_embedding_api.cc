#include "node_version.h"  // define NODE_VERSION first

#include "node_embedding_api_cpp.h"

#include "env-inl.h"
#include "js_native_api_v8.h"
#include "node_api_internals.h"
#include "util-inl.h"
#include "uv.h"

#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#if defined(__APPLE__)
#include <sys/select.h>
#elif defined(__linux)
#include <sys/epoll.h>
#endif

// Use macros to handle errors since they can record the failing argument name
// or expression and their location in the source code.

#define CAST_NOT_NULL_TO(value, type)                                          \
  (value) == nullptr ? EmbeddedErrorHandling::HandleError(                     \
                           NodeStatus::kNullArg,                               \
                           "Argument must not be null: " #value,               \
                           __FILE__,                                           \
                           __LINE__)                                           \
                     : reinterpret_cast<type*>(value)

#define EMBEDDED_PLATFORM(platform) CAST_NOT_NULL_TO(platform, EmbeddedPlatform)

#define EMBEDDED_RUNTIME(runtime) CAST_NOT_NULL_TO(runtime, EmbeddedRuntime)

#define ASSERT_ARG_NOT_NULL(arg)                                               \
  do {                                                                         \
    if ((arg) == nullptr) {                                                    \
      return EmbeddedErrorHandling::HandleError(                               \
          NodeStatus::kNullArg,                                                \
          "Argument must not be null: " #arg,                                  \
          __FILE__,                                                            \
          __LINE__);                                                           \
    }                                                                          \
  } while (false)

#define ASSERT_ARG(arg, expr)                                                  \
  do {                                                                         \
    if (!(expr)) {                                                             \
      return EmbeddedErrorHandling::HandleError(NodeStatus::kBadArg,           \
                                                "Arg: " #arg                   \
                                                " failed: " #expr,             \
                                                __FILE__,                      \
                                                __LINE__);                     \
    }                                                                          \
  } while (false)

#define ASSERT_EXPR(expr)                                                      \
  do {                                                                         \
    if (!(expr)) {                                                             \
      return EmbeddedErrorHandling::HandleError(                               \
          NodeStatus::kGenericError,                                           \
          "Expression returned false: " #expr,                                 \
          __FILE__,                                                            \
          __LINE__);                                                           \
    }                                                                          \
  } while (false)

#define CHECK_STATUS(expr)                                                     \
  do {                                                                         \
    if (NodeStatus status = (expr); status != NodeStatus::kOk) {               \
      return status;                                                           \
    }                                                                          \
  } while (false)

namespace v8impl {

// Creates new Node-API environment. It is defined in node_api.cc.
napi_env NewEnv(v8::Local<v8::Context> context,
                const std::string& module_filename,
                int32_t module_api_version);

}  // namespace v8impl

namespace node {

// Declare functions implemented in embed_helpers.cc
v8::Maybe<ExitCode> SpinEventLoopWithoutCleanup(Environment* env,
                                                uv_run_mode run_mode);
}  // namespace node

namespace node::embedding {

//------------------------------------------------------------------------------
// Convenience functor struct adapter for C++ function object or lambdas.
//------------------------------------------------------------------------------

template <typename TCallback>
std::shared_ptr<NodeFunctor<TCallback>> MakeSharedFunctorPtr(
    TCallback callback,
    void* callback_data,
    node_embedding_data_release_callback release_callback_data) {
  return callback ? std::make_shared<NodeFunctor<TCallback>>(
                        callback, callback_data, release_callback_data)
                  : nullptr;
}

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
  SmallTrivialStack() noexcept : stack_(this->inplace_entries_.data()) {}

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
  std::array<T, kInplaceEntryCount> inplace_entries_;
  T* stack_{};     // Points to either inplace_entries_ or allocated_entries_.
  size_t size_{};  // Number of elements in the stack.
  size_t capacity_{kInplaceEntryCount};
  std::unique_ptr<T[]> allocated_entries_;
};

class EmbeddedErrorHandling {
 public:
  static NodeStatus HandleError(NodeStatus status, std::string_view message);

  static NodeStatus HandleError(NodeStatus status,
                                const std::vector<std::string>& messages);

  static NodeStatus HandleError(NodeStatus status,
                                std::string_view message,
                                std::string_view filename,
                                int32_t line);

  static std::string FormatString(const char* format, ...);

  static const char* GetLastErrorMessage();
  static void SetLastErrorMessage(std::string message);
  static void ClearLastErrorMessage();

  static NodeStatus ExitCodeToStatus(int32_t exit_code);

 private:
  enum class ErrorMessageAction {
    kGet,
    kSet,
    kClear,
  };

 private:
  static const char* DoErrorMessage(ErrorMessageAction action,
                                    std::string message);
};

class EmbeddedPlatform {
 public:
  EmbeddedPlatform(int32_t argc, const char* argv[]) noexcept
      : args_(argv, argv + argc) {}

  EmbeddedPlatform(const EmbeddedPlatform&) = delete;
  EmbeddedPlatform& operator=(const EmbeddedPlatform&) = delete;

  static NodeStatus RunMain(
      int32_t embedding_api_version,
      int32_t argc,
      const char* argv[],
      node_embedding_platform_configure_callback configure_platform,
      void* configure_platform_data,
      node_embedding_runtime_configure_callback configure_runtime,
      void* configure_runtime_data);

  static NodeStatus Create(
      int32_t embedding_api_version,
      int32_t argc,
      const char* argv[],
      node_embedding_platform_configure_callback configure_platform,
      void* configure_platform_data,
      node_embedding_platform* result);

  NodeStatus DeleteMe();

  NodeStatus SetApiVersion(int32_t embedding_api_version);

  NodeStatus SetFlags(NodePlatformFlags flags);

  NodeStatus Initialize(
      node_embedding_platform_configure_callback configure_platform,
      void* configure_platform_data,
      bool* early_return);

  NodeStatus GetParsedArgs(int32_t* args_count,
                           const char** args[],
                           int32_t* runtime_args_count,
                           const char** runtime_args[]);

  node::InitializationResult* init_result() { return init_result_.get(); }

  node::MultiIsolatePlatform* get_v8_platform() { return v8_platform_.get(); }

  int32_t embedding_api_version() { return embedding_api_version_; }

 private:
  static node::ProcessInitializationFlags::Flags GetProcessInitializationFlags(
      NodePlatformFlags flags);

 private:
  int32_t embedding_api_version_{0};
  bool is_initialized_{false};
  bool v8_is_initialized_{false};
  bool v8_is_uninitialized_{false};
  NodePlatformFlags flags_;
  std::vector<std::string> args_;
  struct {
    bool flags : 1;
  } optional_bits_{};

  std::shared_ptr<node::InitializationResult> init_result_;
  std::unique_ptr<node::MultiIsolatePlatform> v8_platform_;
  NodeCStringArray<> c_args_;
  NodeCStringArray<> c_runtime_args_;
};

class EmbeddedRuntime {
 public:
  explicit EmbeddedRuntime(EmbeddedPlatform* platform);

  EmbeddedRuntime(const EmbeddedRuntime&) = delete;
  EmbeddedRuntime& operator=(const EmbeddedRuntime&) = delete;

  static NodeStatus Run(
      node_embedding_platform platform,
      node_embedding_runtime_configure_callback configure_runtime,
      void* configure_runtime_data);

  static NodeStatus Create(
      node_embedding_platform platform,
      node_embedding_runtime_configure_callback configure_runtime,
      void* configure_runtime_data,
      node_embedding_runtime* result);

  NodeStatus DeleteMe();

  NodeStatus SetNodeApiVersion(int32_t node_api_version);

  NodeStatus SetFlags(NodeRuntimeFlags flags);

  NodeStatus SetArgs(int32_t argc,
                     const char* argv[],
                     int32_t exec_argc,
                     const char* exec_argv[]);

  NodeStatus OnPreload(
      node_embedding_runtime_preload_callback run_preload,
      void* preload_data,
      node_embedding_data_release_callback release_preload_data);

  NodeStatus OnStartExecution(
      node_embedding_runtime_loading_callback start_execution,
      void* start_execution_data,
      node_embedding_data_release_callback release_start_execution_data);

  NodeStatus OnHandleExecutionResult(
      node_embedding_runtime_loaded_callback handle_result,
      void* handle_result_data,
      node_embedding_data_release_callback release_handle_result_data);

  NodeStatus AddModule(
      const char* module_name,
      node_embedding_module_initialize_callback init_module,
      void* init_module_data,
      node_embedding_data_release_callback release_init_module_data,
      int32_t module_node_api_version);

  NodeStatus Initialize(
      node_embedding_runtime_configure_callback configure_runtime,
      void* configure_runtime_data);

  NodeStatus SetTaskRunner(
      node_embedding_task_post_callback post_task,
      void* post_task_data,
      node_embedding_data_release_callback release_post_task_data);

  NodeStatus SetUserData(
      void* user_data, node_embedding_data_release_callback release_user_data);

  NodeStatus GetUserData(void** user_data);

  NodeStatus RunEventLoop();

  NodeStatus TerminateEventLoop();

  NodeStatus RunEventLoopOnce(bool* has_more_work);

  NodeStatus RunEventLoopNoWait(bool* has_more_work);

  NodeStatus RunNodeApi(node_embedding_node_api_run_callback run_node_api,
                        void* run_node_api_data);

  NodeStatus OpenNodeApiScope(node_embedding_node_api_scope* node_api_scope,
                              napi_env* env);
  NodeStatus CloseNodeApiScope(node_embedding_node_api_scope node_api_scope);
  bool IsNodeApiScopeOpened() const;

  napi_env GetOrCreateNodeApiEnv(node::Environment* node_env,
                                 const std::string& module_filename);

  size_t OpenV8Scope();
  void CloseV8Scope(size_t nest_level);

 private:
  static void TriggerFatalException(napi_env env,
                                    v8::Local<v8::Value> local_err);
  static node::EnvironmentFlags::Flags GetEnvironmentFlags(
      NodeRuntimeFlags flags);

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
    NodeInitializeModuleCallback init_module;
    int32_t module_node_api_version;
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

  class ReleaseCallbackDeleter {
   public:
    explicit ReleaseCallbackDeleter(
        node_embedding_data_release_callback release_callback)
        : release_callback_(release_callback) {}

    void operator()(void* data) {
      if (release_callback_) {
        release_callback_(data);
      }
    }

   private:
    node_embedding_data_release_callback release_callback_;
  };

 private:
  EmbeddedPlatform* platform_;
  bool is_initialized_{false};
  int32_t node_api_version_{NODE_API_DEFAULT_MODULE_API_VERSION};
  NodeRuntimeFlags flags_{NodeRuntimeFlags::kDefault};
  std::vector<std::string> args_;
  std::vector<std::string> exec_args_;
  node::EmbedderPreloadCallback preload_cb_{};
  node::StartExecutionCallback start_execution_cb_{};
  NodeHandleExecutionResultCallback handle_result_{};

  struct {
    bool flags : 1;
    bool args : 1;
    bool exec_args : 1;
  } optional_bits_{};

  std::unordered_map<std::string, ModuleInfo> modules_;

  std::unique_ptr<node::CommonEnvironmentSetup> env_setup_;
  std::optional<V8ScopeData> v8_scope_data_;

  std::unique_ptr<void, ReleaseCallbackDeleter> user_data_{
      nullptr, ReleaseCallbackDeleter(nullptr)};

  NodePostTaskCallback post_task_{};
  uv_async_t polling_async_handle_{};
  uv_sem_t polling_sem_{};
  uv_thread_t polling_thread_{};
  bool polling_thread_closed_{false};
#if defined(__linux)
  // Epoll to poll for uv's backend fd.
  int epoll_{epoll_create(1)};
#endif

  SmallTrivialStack<NodeApiScopeData> node_api_scope_data_{};

  // The node API associated with the runtime's environment.
  napi_env node_api_env_{};

  // Map from worker thread node::Environment* to napi_env.
  std::mutex worker_env_mutex_;
  std::unordered_map<node::Environment*, napi_env> worker_env_to_node_api_;
};

//-----------------------------------------------------------------------------
// EmbeddedErrorHandling implementation.
//-----------------------------------------------------------------------------

/*static*/ NodeStatus EmbeddedErrorHandling::HandleError(
    NodeStatus status, std::string_view message) {
  if (status == NodeStatus::kOk) {
    ClearLastErrorMessage();
  } else {
    SetLastErrorMessage(message.data());
  }
  return status;
}

/*static*/ NodeStatus EmbeddedErrorHandling::HandleError(
    NodeStatus status, const std::vector<std::string>& messages) {
  if (status == NodeStatus::kOk) {
    ClearLastErrorMessage();
  } else {
    NodeErrorInfo::SetLastErrorMessage(messages);
  }
  return status;
}

/*static*/ NodeStatus EmbeddedErrorHandling::HandleError(
    NodeStatus status,
    std::string_view message,
    std::string_view filename,
    int32_t line) {
  return HandleError(
      status,
      FormatString(
          "Error: %s at %s:%d", message.data(), filename.data(), line));
}

/*static*/ std::string EmbeddedErrorHandling::FormatString(const char* format,
                                                           ...) {
  va_list args1;
  va_start(args1, format);
  va_list args2;  // Required for some compilers like GCC since we go over the
                  // args twice.
  va_copy(args2, args1);
  std::string result(std::vsnprintf(nullptr, 0, format, args1), '\0');
  va_end(args1);
  std::vsnprintf(&result[0], result.size() + 1, format, args2);
  va_end(args2);
  return result;
}

/*static*/ const char* EmbeddedErrorHandling::GetLastErrorMessage() {
  return DoErrorMessage(ErrorMessageAction::kGet, "");
}

/*static*/ void EmbeddedErrorHandling::SetLastErrorMessage(
    std::string message) {
  DoErrorMessage(ErrorMessageAction::kSet, std::move(message));
}

/*static*/ void EmbeddedErrorHandling::ClearLastErrorMessage() {
  DoErrorMessage(ErrorMessageAction::kClear, "");
}

/*static*/ const char* EmbeddedErrorHandling::DoErrorMessage(
    EmbeddedErrorHandling::ErrorMessageAction action, std::string message) {
  static thread_local const char* thread_message_ptr = nullptr;
  static std::unordered_map<std::thread::id, std::unique_ptr<std::string>>
      thread_to_message;
  static std::mutex mutex;

  switch (action) {
    case ErrorMessageAction::kGet:
      break;  // Just return the message.
    case ErrorMessageAction::kSet: {
      auto message_ptr = std::make_unique<std::string>(std::move(message));
      thread_message_ptr = message_ptr->c_str();
      std::scoped_lock lock(mutex);
      thread_to_message[std::this_thread::get_id()] = std::move(message_ptr);
      break;
    }
    case ErrorMessageAction::kClear: {
      if (thread_message_ptr != nullptr) {
        std::scoped_lock lock(mutex);
        thread_to_message.erase(std::this_thread::get_id());
        thread_message_ptr = nullptr;
      }
      break;
    }
  }

  return thread_message_ptr != nullptr ? thread_message_ptr : "";
}

/*static*/ NodeStatus EmbeddedErrorHandling::ExitCodeToStatus(
    int32_t exit_code) {
  return exit_code != 0
             ? static_cast<NodeStatus>(
                   static_cast<int32_t>(NodeStatus::kErrorExitCode) | exit_code)
             : NodeStatus::kOk;
}

//-----------------------------------------------------------------------------
// EmbeddedPlatform implementation.
//-----------------------------------------------------------------------------

NodeStatus EmbeddedPlatform::RunMain(
    int32_t embedding_api_version,
    int32_t argc,
    const char* argv[],
    node_embedding_platform_configure_callback configure_platform,
    void* configure_platform_data,
    node_embedding_runtime_configure_callback configure_runtime,
    void* configure_runtime_data) {
  node_embedding_platform platform{};
  CHECK_STATUS(EmbeddedPlatform::Create(embedding_api_version,
                                        argc,
                                        argv,
                                        configure_platform,
                                        configure_platform_data,
                                        &platform));
  if (platform == nullptr) {
    return NodeStatus::kOk;  // early return
  }
  return EmbeddedRuntime::Run(
      platform, configure_runtime, configure_runtime_data);
}

/*static*/ NodeStatus EmbeddedPlatform::Create(
    int32_t embedding_api_version,
    int32_t argc,
    const char* argv[],
    node_embedding_platform_configure_callback configure_platform,
    void* configure_platform_data,
    node_embedding_platform* result) {
  ASSERT_ARG_NOT_NULL(result);

  // Hack around with the argv pointer. Used for process.title = "blah".
  argv =
      const_cast<const char**>(uv_setup_args(argc, const_cast<char**>(argv)));

  auto platform_ptr = std::make_unique<EmbeddedPlatform>(argc, argv);
  platform_ptr->SetApiVersion(embedding_api_version);
  bool early_return = false;
  CHECK_STATUS(platform_ptr->Initialize(
      configure_platform, configure_platform_data, &early_return));
  if (early_return) {
    return platform_ptr.release()->DeleteMe();
  }

  // The initialization was successful, the caller is responsible for deleting
  // the platform instance.
  *result = reinterpret_cast<node_embedding_platform>(platform_ptr.release());
  return NodeStatus::kOk;
}

NodeStatus EmbeddedPlatform::DeleteMe() {
  if (v8_is_initialized_ && !v8_is_uninitialized_) {
    v8_is_uninitialized_ = true;
    v8::V8::Dispose();
    v8::V8::DisposePlatform();
    node::TearDownOncePerProcess();
  }

  delete this;
  return NodeStatus::kOk;
}

NodeStatus EmbeddedPlatform::SetApiVersion(int32_t embedding_api_version) {
  ASSERT_ARG(embedding_api_version,
             embedding_api_version > 0 &&
                 embedding_api_version <= NODE_EMBEDDING_VERSION);
  return NodeStatus::kOk;
}

NodeStatus EmbeddedPlatform::SetFlags(NodePlatformFlags flags) {
  ASSERT_EXPR(!is_initialized_);
  flags_ = flags;
  optional_bits_.flags = true;
  return NodeStatus::kOk;
}

NodeStatus EmbeddedPlatform::Initialize(
    node_embedding_platform_configure_callback configure_platform,
    void* configure_platform_data,
    bool* early_return) {
  ASSERT_EXPR(!is_initialized_);

  node_embedding_platform_config platform_config =
      reinterpret_cast<node_embedding_platform_config>(this);
  if (configure_platform != nullptr) {
    CHECK_STATUS(configure_platform(configure_platform_data, platform_config));
  }

  is_initialized_ = true;

  if (!optional_bits_.flags) {
    flags_ = NodePlatformFlags::kNone;
  }

  init_result_ = node::InitializeOncePerProcess(
      args_, GetProcessInitializationFlags(flags_));
  int32_t exit_code = init_result_->exit_code();
  if (exit_code != 0) {
    NodeErrorInfo::SetLastErrorMessage(init_result_->errors());
    return EmbeddedErrorHandling::ExitCodeToStatus(exit_code);
  }

  if (init_result_->early_return()) {
    *early_return = true;
    NodeErrorInfo::SetLastErrorMessage(init_result_->errors());
    return NodeStatus::kOk;
  }

  c_args_ = NodeCStringArray<>(init_result_->args());
  c_runtime_args_ = NodeCStringArray<>(init_result_->exec_args());

  int32_t thread_pool_size =
      static_cast<int32_t>(node::per_process::cli_options->v8_thread_pool_size);
  v8_platform_ = node::MultiIsolatePlatform::Create(thread_pool_size);
  v8::V8::InitializePlatform(v8_platform_.get());
  v8::V8::Initialize();

  v8_is_initialized_ = true;

  return NodeStatus::kOk;
}

NodeStatus EmbeddedPlatform::GetParsedArgs(int32_t* args_count,
                                           const char** args[],
                                           int32_t* runtime_args_count,
                                           const char** runtime_args[]) {
  ASSERT_EXPR(is_initialized_);

  if (args_count != nullptr) {
    *args_count = c_args_.size();
  }
  if (args != nullptr) {
    *args = c_args_.c_strs();
  }
  if (runtime_args_count != nullptr) {
    *runtime_args_count = c_runtime_args_.size();
  }
  if (runtime_args != nullptr) {
    *runtime_args = c_runtime_args_.c_strs();
  }
  return NodeStatus::kOk;
}

node::ProcessInitializationFlags::Flags
EmbeddedPlatform::GetProcessInitializationFlags(NodePlatformFlags flags) {
  uint32_t result = node::ProcessInitializationFlags::kNoFlags;
  if (embedding::IsFlagSet(flags, NodePlatformFlags::kEnableStdioInheritance)) {
    result |= node::ProcessInitializationFlags::kEnableStdioInheritance;
  }
  if (embedding::IsFlagSet(flags, NodePlatformFlags::kDisableNodeOptionsEnv)) {
    result |= node::ProcessInitializationFlags::kDisableNodeOptionsEnv;
  }
  if (embedding::IsFlagSet(flags, NodePlatformFlags::kDisableCliOptions)) {
    result |= node::ProcessInitializationFlags::kDisableCLIOptions;
  }
  if (embedding::IsFlagSet(flags, NodePlatformFlags::kNoICU)) {
    result |= node::ProcessInitializationFlags::kNoICU;
  }
  if (embedding::IsFlagSet(flags, NodePlatformFlags::kNoStdioInitialization)) {
    result |= node::ProcessInitializationFlags::kNoStdioInitialization;
  }
  if (embedding::IsFlagSet(flags,
                           NodePlatformFlags::kNoDefaultSignalHandling)) {
    result |= node::ProcessInitializationFlags::kNoDefaultSignalHandling;
  }
  result |= node::ProcessInitializationFlags::kNoInitializeV8;
  result |= node::ProcessInitializationFlags::kNoInitializeNodeV8Platform;
  if (embedding::IsFlagSet(flags, NodePlatformFlags::kNoInitOpenSSL)) {
    result |= node::ProcessInitializationFlags::kNoInitOpenSSL;
  }
  if (embedding::IsFlagSet(flags,
                           NodePlatformFlags::kNoParseGlobalDebugVariables)) {
    result |= node::ProcessInitializationFlags::kNoParseGlobalDebugVariables;
  }
  if (embedding::IsFlagSet(flags, NodePlatformFlags::kNoAdjustResourceLimits)) {
    result |= node::ProcessInitializationFlags::kNoAdjustResourceLimits;
  }
  if (embedding::IsFlagSet(flags, NodePlatformFlags::kNoUseLargePages)) {
    result |= node::ProcessInitializationFlags::kNoUseLargePages;
  }
  if (embedding::IsFlagSet(flags,
                           NodePlatformFlags::kNoPrintHelpOrVersionOutput)) {
    result |= node::ProcessInitializationFlags::kNoPrintHelpOrVersionOutput;
  }
  if (embedding::IsFlagSet(flags,
                           NodePlatformFlags::kGeneratePredictableSnapshot)) {
    result |= node::ProcessInitializationFlags::kGeneratePredictableSnapshot;
  }
  return static_cast<node::ProcessInitializationFlags::Flags>(result);
}

//-----------------------------------------------------------------------------
// EmbeddedRuntime implementation.
//-----------------------------------------------------------------------------

/*static*/ NodeStatus EmbeddedRuntime::Run(
    node_embedding_platform platform,
    node_embedding_runtime_configure_callback configure_runtime,
    void* configure_runtime_data) {
  node_embedding_runtime runtime{};
  CHECK_STATUS(
      Create(platform, configure_runtime, configure_runtime_data, &runtime));
  CHECK_STATUS(node_embedding_runtime_event_loop_run(runtime));
  CHECK_STATUS(node_embedding_runtime_delete(runtime));
  return NodeStatus::kOk;
}

/*static*/ NodeStatus EmbeddedRuntime::Create(
    node_embedding_platform platform,
    node_embedding_runtime_configure_callback configure_runtime,
    void* configure_runtime_data,
    node_embedding_runtime* result) {
  ASSERT_ARG_NOT_NULL(platform);
  ASSERT_ARG_NOT_NULL(result);

  EmbeddedPlatform* platform_ptr =
      reinterpret_cast<EmbeddedPlatform*>(platform);
  std::unique_ptr<EmbeddedRuntime> runtime_ptr =
      std::make_unique<EmbeddedRuntime>(platform_ptr);

  CHECK_STATUS(
      runtime_ptr->Initialize(configure_runtime, configure_runtime_data));

  *result = reinterpret_cast<node_embedding_runtime>(runtime_ptr.release());

  return NodeStatus::kOk;
}

EmbeddedRuntime::EmbeddedRuntime(EmbeddedPlatform* platform)
    : platform_(platform) {}

NodeStatus EmbeddedRuntime::DeleteMe() {
  ASSERT_EXPR(!IsNodeApiScopeOpened());

  std::unique_ptr<node::CommonEnvironmentSetup> env_setup =
      std::move(env_setup_);
  if (env_setup != nullptr) {
    node::Stop(env_setup->env());
  }

  delete this;
  return NodeStatus::kOk;
}

NodeStatus EmbeddedRuntime::SetNodeApiVersion(int32_t node_api_version) {
  ASSERT_ARG(node_api_version,
             node_api_version >= NODE_API_DEFAULT_MODULE_API_VERSION &&
                 (node_api_version <= NAPI_VERSION ||
                  node_api_version == NAPI_VERSION_EXPERIMENTAL));

  node_api_version_ = node_api_version;

  return NodeStatus::kOk;
}

NodeStatus EmbeddedRuntime::SetFlags(NodeRuntimeFlags flags) {
  ASSERT_EXPR(!is_initialized_);
  flags_ = flags;
  optional_bits_.flags = true;
  return NodeStatus::kOk;
}

NodeStatus EmbeddedRuntime::SetArgs(int32_t argc,
                                    const char* argv[],
                                    int32_t exec_argc,
                                    const char* exec_argv[]) {
  ASSERT_EXPR(!is_initialized_);
  if (argv != nullptr) {
    args_.assign(argv, argv + argc);
    optional_bits_.args = true;
  }
  if (exec_argv != nullptr) {
    exec_args_.assign(exec_argv, exec_argv + exec_argc);
    optional_bits_.exec_args = true;
  }
  return NodeStatus::kOk;
}

NodeStatus EmbeddedRuntime::OnPreload(
    node_embedding_runtime_preload_callback run_preload,
    void* preload_data,
    node_embedding_data_release_callback release_preload_data) {
  ASSERT_EXPR(!is_initialized_);

  if (run_preload != nullptr) {
    preload_cb_ = node::EmbedderPreloadCallback(
        [this,
         run_preload_ptr = MakeSharedFunctorPtr(
             run_preload, preload_data, release_preload_data)](
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
                (*run_preload_ptr)(
                    reinterpret_cast<node_embedding_runtime>(this),
                    env,
                    process_value,
                    require_value);
              },
              TriggerFatalException);
        });
  } else {
    preload_cb_ = {};
  }

  return NodeStatus::kOk;
}

NodeStatus EmbeddedRuntime::OnStartExecution(
    node_embedding_runtime_loading_callback start_execution,
    void* start_execution_data,
    node_embedding_data_release_callback release_start_execution_data) {
  ASSERT_EXPR(!is_initialized_);

  if (start_execution != nullptr) {
    start_execution_cb_ = node::StartExecutionCallback(
        [this,
         start_execution_ptr =
             MakeSharedFunctorPtr(start_execution,
                                  start_execution_data,
                                  release_start_execution_data)](
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
                result = (*start_execution_ptr)(
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

  return NodeStatus::kOk;
}

NodeStatus EmbeddedRuntime::OnHandleExecutionResult(
    node_embedding_runtime_loaded_callback handle_result,
    void* handle_result_data,
    node_embedding_data_release_callback release_handle_result_data) {
  ASSERT_EXPR(!is_initialized_);

  handle_result_ = NodeHandleExecutionResultCallback(
      handle_result, handle_result_data, release_handle_result_data);

  return NodeStatus::kOk;
}

NodeStatus EmbeddedRuntime::AddModule(
    const char* module_name,
    node_embedding_module_initialize_callback init_module,
    void* init_module_data,
    node_embedding_data_release_callback release_init_module_data,
    int32_t module_node_api_version) {
  ASSERT_ARG_NOT_NULL(module_name);
  ASSERT_ARG_NOT_NULL(init_module);
  ASSERT_EXPR(!is_initialized_);

  auto insert_result = modules_.try_emplace(
      module_name,
      ModuleInfo{reinterpret_cast<node_embedding_runtime>(this),
                 module_name,
                 NodeInitializeModuleCallback{
                     init_module, init_module_data, release_init_module_data},
                 module_node_api_version});
  if (!insert_result.second) {
    return EmbeddedErrorHandling::HandleError(
        NodeStatus::kBadArg,
        EmbeddedErrorHandling::FormatString(
            "Module with name '%s' is already added.", module_name));
  }

  return NodeStatus::kOk;
}

NodeStatus EmbeddedRuntime::Initialize(
    node_embedding_runtime_configure_callback configure_runtime,
    void* configure_runtime_data) {
  ASSERT_EXPR(!is_initialized_);

  if (configure_runtime != nullptr) {
    CHECK_STATUS(configure_runtime(
        configure_runtime_data,
        reinterpret_cast<node_embedding_platform>(platform_),
        reinterpret_cast<node_embedding_runtime_config>(this)));
  }

  is_initialized_ = true;

  node::EnvironmentFlags::Flags flags = GetEnvironmentFlags(
      optional_bits_.flags ? flags_ : NodeRuntimeFlags::kDefault);

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
    return EmbeddedErrorHandling::HandleError(NodeStatus::kGenericError,
                                              errors);
  }

  V8ScopeLocker v8_scope_locker(*this);

  std::string filename = args_.size() > 1 ? args_[1] : "<internal>";
  node_api_env_ =
      v8impl::NewEnv(env_setup_->env()->context(), filename, node_api_version_);

  node::Environment* node_env = env_setup_->env();

  RegisterModules();

  v8::MaybeLocal<v8::Value> ret =
      node::LoadEnvironment(node_env, start_execution_cb_, preload_cb_);

  if (ret.IsEmpty()) {
    return EmbeddedErrorHandling::HandleError(NodeStatus::kGenericError,
                                              "Failed to load environment");
  }

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

  return NodeStatus::kOk;
}

NodeStatus EmbeddedRuntime::SetUserData(
    void* user_data, node_embedding_data_release_callback release_user_data) {
  user_data_.release();  // Do not call the release callback on set
  user_data_ = std::unique_ptr<void, ReleaseCallbackDeleter>(
      user_data, ReleaseCallbackDeleter(release_user_data));
  return NodeStatus::kOk;
}

NodeStatus EmbeddedRuntime::GetUserData(void** user_data) {
  ASSERT_ARG_NOT_NULL(user_data);
  *user_data = user_data_.get();
  return NodeStatus::kOk;
}

uv_loop_t* EmbeddedRuntime::EventLoop() {
  return env_setup_->env()->event_loop();
}

void EmbeddedRuntime::InitializePollingThread() {
  if (!post_task_) return;

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
    struct epoll_event ev = {};
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
  if (!post_task_) return;
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
  if (!post_task_) return;
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
    bool succeeded = false;
    NodeStatus post_result = runtime->post_task_(
        [](void* task_data) {
          auto* runtime = static_cast<EmbeddedRuntime*>(task_data);
          return runtime->RunEventLoopNoWait(nullptr);
        },
        runtime,
        nullptr,
        &succeeded);

    // TODO: Handle post_result
    (void)post_result;
    if (!succeeded) {
      // The task runner is shutting down.
      break;
    }
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

NodeStatus EmbeddedRuntime::SetTaskRunner(
    node_embedding_task_post_callback post_task,
    void* post_task_data,
    node_embedding_data_release_callback release_post_task_data) {
  ASSERT_EXPR(!is_initialized_);
  post_task_ =
      NodePostTaskCallback{post_task, post_task_data, release_post_task_data};
  return NodeStatus::kOk;
}

NodeStatus EmbeddedRuntime::RunEventLoop() {
  ASSERT_EXPR(is_initialized_);

  V8ScopeLocker v8_scope_locker(*this);

  DestroyPollingThread();

  int32_t exit_code = node::SpinEventLoop(env_setup_->env()).FromMaybe(1);
  return EmbeddedErrorHandling::HandleError(
      EmbeddedErrorHandling::ExitCodeToStatus(exit_code),
      "Failed while closing the runtime");
}

NodeStatus EmbeddedRuntime::TerminateEventLoop() {
  ASSERT_EXPR(is_initialized_);

  V8ScopeLocker v8_scope_locker(*this);
  int32_t exit_code = node::Stop(env_setup_->env(), node::StopFlags::kNoFlags);
  return EmbeddedErrorHandling::HandleError(
      EmbeddedErrorHandling::ExitCodeToStatus(exit_code),
      "Failed while stopping the runtime");
}

NodeStatus EmbeddedRuntime::RunEventLoopOnce(bool* has_more_work) {
  ASSERT_EXPR(is_initialized_);

  V8ScopeLocker v8_scope_locker(*this);

  node::ExitCode exit_code =
      node::SpinEventLoopWithoutCleanup(env_setup_->env(), UV_RUN_ONCE)
          .FromMaybe(node::ExitCode::kGenericUserError);
  if (exit_code != node::ExitCode::kNoFailure) {
    return EmbeddedErrorHandling::HandleError(
        EmbeddedErrorHandling::ExitCodeToStatus(
            static_cast<int32_t>(exit_code)),
        "Failed running the event loop");
  }

  if (has_more_work != nullptr) {
    *has_more_work = uv_loop_alive(env_setup_->env()->event_loop());
  }

  WakeupPollingThread();

  return NodeStatus::kOk;
}

NodeStatus EmbeddedRuntime::RunEventLoopNoWait(bool* has_more_work) {
  ASSERT_EXPR(is_initialized_);

  V8ScopeLocker v8_scope_locker(*this);

  node::ExitCode exit_code =
      node::SpinEventLoopWithoutCleanup(env_setup_->env(), UV_RUN_ONCE)
          .FromMaybe(node::ExitCode::kGenericUserError);
  if (exit_code != node::ExitCode::kNoFailure) {
    return EmbeddedErrorHandling::HandleError(
        EmbeddedErrorHandling::ExitCodeToStatus(
            static_cast<int32_t>(exit_code)),
        "Failed running the event loop");
  }

  if (has_more_work != nullptr) {
    *has_more_work = uv_loop_alive(env_setup_->env()->event_loop());
  }

  WakeupPollingThread();

  return NodeStatus::kOk;
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

NodeStatus EmbeddedRuntime::RunNodeApi(
    node_embedding_node_api_run_callback run_node_api,
    void* run_node_api_data) {
  ASSERT_ARG_NOT_NULL(run_node_api);

  node_embedding_node_api_scope node_api_scope{};
  napi_env env{};
  CHECK_STATUS(OpenNodeApiScope(&node_api_scope, &env));
  auto nodeApiScopeLeave =
      node::OnScopeLeave([&]() { CloseNodeApiScope(node_api_scope); });

  run_node_api(run_node_api_data, env);

  return NodeStatus::kOk;
}

NodeStatus EmbeddedRuntime::OpenNodeApiScope(
    node_embedding_node_api_scope* node_api_scope, napi_env* env) {
  ASSERT_ARG_NOT_NULL(node_api_scope);
  ASSERT_ARG_NOT_NULL(env);

  size_t v8_scope_nest_level = OpenV8Scope();
  node_api_scope_data_.Push(
      {node_api_env_->OpenCallModuleScope(), v8_scope_nest_level});

  *node_api_scope = reinterpret_cast<node_embedding_node_api_scope>(
      node_api_scope_data_.size());
  *env = node_api_env_;
  return NodeStatus::kOk;
}

NodeStatus EmbeddedRuntime::CloseNodeApiScope(
    node_embedding_node_api_scope node_api_scope) {
  CHECK_EQ(node_api_scope_data_.size(),
           reinterpret_cast<size_t>(node_api_scope));
  size_t v8_scope_nest_level = node_api_scope_data_.top().v8_scope_nest_level_;

  node_api_env_->CloseCallModuleScope(
      node_api_scope_data_.top().module_scope_data_);
  node_api_scope_data_.Pop();
  CloseV8Scope(v8_scope_nest_level);

  return NodeStatus::kOk;
}

bool EmbeddedRuntime::IsNodeApiScopeOpened() const {
  return node_api_scope_data_.size() > 0;
}

napi_env EmbeddedRuntime::GetOrCreateNodeApiEnv(
    node::Environment* node_env, const std::string& module_filename) {
  // Check if this is the main environment associated with the runtime.
  if (node_env == env_setup_->env()) {
    return node_api_env_;
  }

  {
    // Return Node-API env if it already exists.
    std::scoped_lock<std::mutex> lock(worker_env_mutex_);
    auto it = worker_env_to_node_api_.find(node_env);
    if (it != worker_env_to_node_api_.end()) {
      return it->second;
    }
  }

  // Create new Node-API env. We avoid creating the environment under the lock.
  napi_env env =
      v8impl::NewEnv(node_env->context(), module_filename, node_api_version_);

  // In case if we cannot insert the new env, we are just going to have an
  // unused env which will be deleted in the end with other environments.
  std::scoped_lock<std::mutex> lock(worker_env_mutex_);
  auto insert_result = worker_env_to_node_api_.try_emplace(node_env, env);
  if (insert_result.second) {
    // If the environment is successfully inserted, add a cleanup hook to delete
    // it from the worker_env_to_node_api_ later.
    struct CleanupContext {
      EmbeddedRuntime* runtime_;
      node::Environment* node_env_;
    };
    node_env->AddCleanupHook(
        [](void* arg) {
          std::unique_ptr<CleanupContext> context{
              static_cast<CleanupContext*>(arg)};
          std::scoped_lock<std::mutex> lock(
              context->runtime_->worker_env_mutex_);
          context->runtime_->worker_env_to_node_api_.erase(context->node_env_);
        },
        static_cast<void*>(new CleanupContext{this, node_env}));
  }

  // Return either the inserted or the existing environment.
  return insert_result.first->second;
}

node::EnvironmentFlags::Flags EmbeddedRuntime::GetEnvironmentFlags(
    NodeRuntimeFlags flags) {
  uint64_t result = node::EnvironmentFlags::kNoFlags;
  if (embedding::IsFlagSet(flags, NodeRuntimeFlags::kDefault)) {
    result |= node::EnvironmentFlags::kDefaultFlags;
  }
  if (embedding::IsFlagSet(flags, NodeRuntimeFlags::kOwnsProcessState)) {
    result |= node::EnvironmentFlags::kOwnsProcessState;
  }
  if (embedding::IsFlagSet(flags, NodeRuntimeFlags::kOwnsInspector)) {
    result |= node::EnvironmentFlags::kOwnsInspector;
  }
  if (embedding::IsFlagSet(flags, NodeRuntimeFlags::kNoRegisterEsmLoader)) {
    result |= node::EnvironmentFlags::kNoRegisterESMLoader;
  }
  if (embedding::IsFlagSet(flags, NodeRuntimeFlags::kTrackUnmanagedFds)) {
    result |= node::EnvironmentFlags::kTrackUnmanagedFds;
  }
  if (embedding::IsFlagSet(flags, NodeRuntimeFlags::kHideConsoleWindows)) {
    result |= node::EnvironmentFlags::kHideConsoleWindows;
  }
  if (embedding::IsFlagSet(flags, NodeRuntimeFlags::kNoNativeAddons)) {
    result |= node::EnvironmentFlags::kNoNativeAddons;
  }
  if (embedding::IsFlagSet(flags, NodeRuntimeFlags::kNoGlobalSearchPaths)) {
    result |= node::EnvironmentFlags::kNoGlobalSearchPaths;
  }
  if (embedding::IsFlagSet(flags, NodeRuntimeFlags::kNoBrowserGlobals)) {
    result |= node::EnvironmentFlags::kNoBrowserGlobals;
  }
  if (embedding::IsFlagSet(flags, NodeRuntimeFlags::kNoCreateInspector)) {
    result |= node::EnvironmentFlags::kNoCreateInspector;
  }
  if (embedding::IsFlagSet(flags,
                           NodeRuntimeFlags::kNoStartDebugSignalHandler)) {
    result |= node::EnvironmentFlags::kNoStartDebugSignalHandler;
  }
  if (embedding::IsFlagSet(flags,
                           NodeRuntimeFlags::kNoWaitForInspectorFrontend)) {
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

}  // namespace node::embedding

using namespace node::embedding;

const char* NAPI_CDECL node_embedding_last_error_message_get() {
  return EmbeddedErrorHandling::GetLastErrorMessage();
}

void NAPI_CDECL node_embedding_last_error_message_set(const char* message) {
  if (message == nullptr) {
    EmbeddedErrorHandling::ClearLastErrorMessage();
  } else {
    EmbeddedErrorHandling::SetLastErrorMessage(message);
  }
}

void NAPI_CDECL node_embedding_last_error_message_set_format(const char* format,
                                                             ...) {
  constexpr size_t buffer_size = 1024;
  char buffer[buffer_size];
  std::unique_ptr<char[]> dynamic_buffer;

  va_list args1;
  va_start(args1, format);
  va_list args2;  // Required for some compilers like GCC since we go over the
                  // args twice.
  va_copy(args2, args1);

  size_t string_size = std::vsnprintf(nullptr, 0, format, args1);
  va_end(args1);
  char* message = buffer;
  if (string_size >= buffer_size) {
    dynamic_buffer = std::make_unique<char[]>(string_size + 1);
    message = dynamic_buffer.get();
  }
  std::vsnprintf(&message[0], string_size + 1, format, args2);
  va_end(args2);

  EmbeddedErrorHandling::SetLastErrorMessage(message);
}

node_embedding_status NAPI_CDECL node_embedding_main_run(
    int32_t embedding_api_version,
    int32_t argc,
    const char* argv[],
    node_embedding_platform_configure_callback configure_platform,
    void* configure_platform_data,
    node_embedding_runtime_configure_callback configure_runtime,
    void* configure_runtime_data) {
  return EmbeddedPlatform::RunMain(embedding_api_version,
                                   argc,
                                   argv,
                                   configure_platform,
                                   configure_platform_data,
                                   configure_runtime,
                                   configure_runtime_data);
}

node_embedding_status NAPI_CDECL node_embedding_platform_create(
    int32_t embedding_api_version,
    int32_t argc,
    const char* argv[],
    node_embedding_platform_configure_callback configure_platform,
    void* configure_platform_data,
    node_embedding_platform* result) {
  return EmbeddedPlatform::Create(embedding_api_version,
                                  argc,
                                  argv,
                                  configure_platform,
                                  configure_platform_data,
                                  result);
}

node_embedding_status NAPI_CDECL
node_embedding_platform_delete(node_embedding_platform platform) {
  return EMBEDDED_PLATFORM(platform)->DeleteMe();
}

node_embedding_status NAPI_CDECL node_embedding_platform_config_set_flags(
    node_embedding_platform_config platform_config,
    node_embedding_platform_flags flags) {
  return EMBEDDED_PLATFORM(platform_config)->SetFlags(flags);
}

node_embedding_status NAPI_CDECL
node_embedding_platform_get_parsed_args(node_embedding_platform platform,
                                        int32_t* args_count,
                                        const char** args[],
                                        int32_t* runtime_args_count,
                                        const char** runtime_args[]) {
  return EMBEDDED_PLATFORM(platform)->GetParsedArgs(
      args_count, args, runtime_args_count, runtime_args);
}

node_embedding_status NAPI_CDECL node_embedding_runtime_run(
    node_embedding_platform platform,
    node_embedding_runtime_configure_callback configure_runtime,
    void* configure_runtime_data) {
  return EmbeddedRuntime::Run(
      platform, configure_runtime, configure_runtime_data);
}

node_embedding_status NAPI_CDECL node_embedding_runtime_create(
    node_embedding_platform platform,
    node_embedding_runtime_configure_callback configure_runtime,
    void* configure_runtime_data,
    node_embedding_runtime* result) {
  return EmbeddedRuntime::Create(
      platform, configure_runtime, configure_runtime_data, result);
}

node_embedding_status NAPI_CDECL
node_embedding_runtime_delete(node_embedding_runtime runtime) {
  return EMBEDDED_RUNTIME(runtime)->DeleteMe();
}

node_embedding_status NAPI_CDECL
node_embedding_runtime_config_set_node_api_version(
    node_embedding_runtime_config runtime_config, int32_t node_api_version) {
  return EMBEDDED_RUNTIME(runtime_config)->SetNodeApiVersion(node_api_version);
}

node_embedding_status NAPI_CDECL node_embedding_runtime_config_set_flags(
    node_embedding_runtime_config runtime_config,
    node_embedding_runtime_flags flags) {
  return EMBEDDED_RUNTIME(runtime_config)->SetFlags(flags);
}

node_embedding_status NAPI_CDECL node_embedding_runtime_config_set_args(
    node_embedding_runtime_config runtime_config,
    int32_t argc,
    const char* argv[],
    int32_t runtime_argc,
    const char* runtime_argv[]) {
  return EMBEDDED_RUNTIME(runtime_config)
      ->SetArgs(argc, argv, runtime_argc, runtime_argv);
}

node_embedding_status NAPI_CDECL node_embedding_runtime_config_on_preload(
    node_embedding_runtime_config runtime_config,
    node_embedding_runtime_preload_callback run_preload,
    void* preload_data,
    node_embedding_data_release_callback release_preload_data) {
  return EMBEDDED_RUNTIME(runtime_config)
      ->OnPreload(run_preload, preload_data, release_preload_data);
}

node_embedding_status NAPI_CDECL node_embedding_runtime_config_on_loading(
    node_embedding_runtime_config runtime_config,
    node_embedding_runtime_loading_callback start_execution,
    void* start_execution_data,
    node_embedding_data_release_callback release_start_execution_data) {
  return EMBEDDED_RUNTIME(runtime_config)
      ->OnStartExecution(
          start_execution, start_execution_data, release_start_execution_data);
}

node_embedding_status NAPI_CDECL node_embedding_runtime_config_on_loaded(
    node_embedding_runtime_config runtime_config,
    node_embedding_runtime_loaded_callback handle_result,
    void* handle_result_data,
    node_embedding_data_release_callback release_handle_result_data) {
  return EMBEDDED_RUNTIME(runtime_config)
      ->OnHandleExecutionResult(
          handle_result, handle_result_data, release_handle_result_data);
}

node_embedding_status NAPI_CDECL node_embedding_runtime_config_add_module(
    node_embedding_runtime_config runtime_config,
    const char* module_name,
    node_embedding_module_initialize_callback init_module,
    void* init_module_data,
    node_embedding_data_release_callback release_init_module_data,
    int32_t module_node_api_version) {
  return EMBEDDED_RUNTIME(runtime_config)
      ->AddModule(module_name,
                  init_module,
                  init_module_data,
                  release_init_module_data,
                  module_node_api_version);
}

node_embedding_status NAPI_CDECL node_embedding_runtime_user_data_set(
    node_embedding_runtime runtime,
    void* user_data,
    node_embedding_data_release_callback release_user_data) {
  return EMBEDDED_RUNTIME(runtime)->SetUserData(user_data, release_user_data);
}

node_embedding_status NAPI_CDECL node_embedding_runtime_user_data_get(
    node_embedding_runtime runtime, void** user_data) {
  return EMBEDDED_RUNTIME(runtime)->GetUserData(user_data);
}

node_embedding_status NAPI_CDECL node_embedding_runtime_config_set_task_runner(
    node_embedding_runtime_config runtime_config,
    node_embedding_task_post_callback post_task,
    void* post_task_data,
    node_embedding_data_release_callback release_post_task_data) {
  return EMBEDDED_RUNTIME(runtime_config)
      ->SetTaskRunner(post_task, post_task_data, release_post_task_data);
}

node_embedding_status NAPI_CDECL
node_embedding_runtime_event_loop_run(node_embedding_runtime runtime) {
  return EMBEDDED_RUNTIME(runtime)->RunEventLoop();
}

node_embedding_status NAPI_CDECL
node_embedding_runtime_event_loop_terminate(node_embedding_runtime runtime) {
  return EMBEDDED_RUNTIME(runtime)->TerminateEventLoop();
}

node_embedding_status NAPI_CDECL node_embedding_runtime_event_loop_run_once(
    node_embedding_runtime runtime, bool* has_more_work) {
  return EMBEDDED_RUNTIME(runtime)->RunEventLoopOnce(has_more_work);
}

node_embedding_status NAPI_CDECL node_embedding_runtime_event_loop_run_no_wait(
    node_embedding_runtime runtime, bool* has_more_work) {
  return EMBEDDED_RUNTIME(runtime)->RunEventLoopNoWait(has_more_work);
}

node_embedding_status NAPI_CDECL node_embedding_runtime_node_api_run(
    node_embedding_runtime runtime,
    node_embedding_node_api_run_callback run_node_api,
    void* run_node_api_data) {
  return EMBEDDED_RUNTIME(runtime)->RunNodeApi(run_node_api, run_node_api_data);
}

node_embedding_status NAPI_CDECL node_embedding_runtime_node_api_scope_open(
    node_embedding_runtime runtime,
    node_embedding_node_api_scope* node_api_scope,
    napi_env* env) {
  return EMBEDDED_RUNTIME(runtime)->OpenNodeApiScope(node_api_scope, env);
}

node_embedding_status NAPI_CDECL node_embedding_runtime_node_api_scope_close(
    node_embedding_runtime runtime,
    node_embedding_node_api_scope node_api_scope) {
  return EMBEDDED_RUNTIME(runtime)->CloseNodeApiScope(node_api_scope);
}
