//
// Description: C-based API for embedding Node.js.
//
// !!! WARNING !!! WARNING !!! WARNING !!!
// This is a new API and is subject to change.
// While it is C-based, it is not ABI safe yet.
// Consider all functions and data structures as experimental.
// !!! WARNING !!! WARNING !!! WARNING !!!
//
// This file contains the C-based API for embedding Node.js in a host
// application. The API is designed to be used by applications that want to
// embed Node.js as a shared library (.so or .dll) and can interop with
// C-based API.
//

#ifndef SRC_NODE_EMBEDDING_API_CPP_H_
#define SRC_NODE_EMBEDDING_API_CPP_H_

//==============================================================================
// The C++ wrappers for the Node.js embedding API.
//==============================================================================

#include "node_embedding_api.h"

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace node::embedding {

//==============================================================================
// C++ convenience functions for the C API.
// These functions are not ABI safe and can be changed in future versions.
//==============================================================================

// Move-only pointer wrapper.
// The class does not own the pointer and does not delete it.
// It simplifies implementation of the C++ API classes that wrap pointers.
template <typename TPointer>
class NodePointer {
 public:
  NodePointer() = default;

  explicit NodePointer(TPointer ptr) : ptr_(ptr) {}

  NodePointer(const NodePointer&) = delete;
  NodePointer& operator=(const NodePointer&) = delete;

  NodePointer(NodePointer&& other) noexcept
      : ptr_(std::exchange(other.ptr_, nullptr)) {}

  NodePointer& operator=(NodePointer&& other) noexcept {
    if (this != &other) {
      ptr_ = std::exchange(other.ptr_, nullptr);
    }
    return *this;
  }

  NodePointer& operator=(std::nullptr_t) {
    ptr_ = nullptr;
    return *this;
  }

  TPointer ptr() const { return ptr_; }

  explicit operator bool() const { return ptr_ != nullptr; }

 private:
  TPointer ptr_{};
};

template <typename T>
class [[nodiscard]] NodeExpected {
 public:
  explicit NodeExpected(T value) : value_(std::move(value)) {}

  explicit NodeExpected(NodeStatus status) : status_(status) {}

  NodeExpected(const NodeExpected&) = delete;
  NodeExpected& operator=(const NodeExpected&) = delete;

  NodeExpected(NodeExpected&& other) noexcept : status_(other.status_) {
    if (other.has_value()) {
      new (std::addressof(value_)) T(std::move(other.value_));
    }
  }

  NodeExpected& operator=(NodeExpected&& other) noexcept {
    if (this != &other) {
      if (has_value()) {
        value_.~T();
      }
      status_ = other.status_;
      if (other.has_value()) {
        new (std::addressof(value_)) T(std::move(other.value_));
      }
    }
    return *this;
  }

  ~NodeExpected() {
    if (has_value()) {
      value_.~T();
    }
  }

  bool has_value() const { return status_ == NodeStatus::kOk; }
  bool has_error() const { return status_ != NodeStatus::kOk; }

  T& value() & { return value_; }
  const T& value() const& { return value_; }
  T&& value() && { return std::move(value_); }
  const T&& value() const&& { return std::move(value_); }

  NodeStatus status() const { return status_; }

  int32_t exit_code() const {
    if (status_ == NodeStatus::kOk) {
      return 0;
    } else if ((static_cast<int32_t>(status_) &
                static_cast<int32_t>(NodeStatus::kErrorExitCode)) != 0) {
      return static_cast<int32_t>(status_) &
             ~static_cast<int32_t>(NodeStatus::kErrorExitCode);
    }
    return 1;
  }

 private:
  NodeStatus status_{NodeStatus::kOk};
  union {
    T value_;  // The value is uninitialized if status_ is not kOk.
    char padding_[sizeof(T)];
  };
};

template <>
class [[nodiscard]] NodeExpected<void> {
 public:
  NodeExpected() = default;

  explicit NodeExpected(NodeStatus status) : status_(status) {}

  NodeExpected(const NodeExpected&) = delete;
  NodeExpected& operator=(const NodeExpected&) = delete;

  NodeExpected(NodeExpected&& other) = default;
  NodeExpected& operator=(NodeExpected&& other) = default;

  bool has_value() const { return status_ == NodeStatus::kOk; }
  bool has_error() const { return status_ != NodeStatus::kOk; }

  NodeStatus status() const { return status_; }

  int32_t exit_code() const {
    if (status_ == NodeStatus::kOk) {
      return 0;
    } else if ((static_cast<int32_t>(status_) &
                static_cast<int32_t>(NodeStatus::kErrorExitCode)) != 0) {
      return static_cast<int32_t>(status_) &
             ~static_cast<int32_t>(NodeStatus::kErrorExitCode);
    }
    return 1;
  }

 private:
  NodeStatus status_{NodeStatus::kOk};
};

// A helper class to convert std::vector<std::string> to an array of C strings.
// If the number of strings is less than kInplaceBufferSize, the strings are
// stored in the inplace_buffer_ array. Otherwise, the strings are stored in the
// allocated_buffer_ array.
// Ideally the class must be allocated on the stack.
// In any case it must not outlive the passed vector since it keeps only the
// string pointers returned by std::string::c_str() method.
template <size_t kInplaceBufferSize = 32>
class NodeCStringArray {
 public:
  NodeCStringArray() = default;

  explicit NodeCStringArray(const std::vector<std::string>& strings) noexcept
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

  NodeCStringArray(const NodeCStringArray&) = delete;
  NodeCStringArray& operator=(const NodeCStringArray&) = delete;

  NodeCStringArray(NodeCStringArray&& other) noexcept
      : size_(std::exchange(other.size_, 0)),
        c_strs_(std::exchange(other.c_strs_, 0)),
        allocated_buffer_(std::exchange(other.allocated_buffer_, nullptr)) {
    if (size_ <= inplace_buffer_.size()) {
      c_strs_ = inplace_buffer_.data();
      std::memcpy(inplace_buffer_.data(),
                  other.inplace_buffer_.data(),
                  size_ * sizeof(const char*));
    }
  }

  NodeCStringArray& operator=(NodeCStringArray&& other) noexcept {
    if (this != &other) {
      size_ = std::exchange(other.size_, 0);
      c_strs_ = std::exchange(other.c_strs_, nullptr);
      allocated_buffer_ = std::exchange(other.allocated_buffer_, nullptr);
      if (size_ <= inplace_buffer_.size()) {
        c_strs_ = inplace_buffer_.data();
        std::memcpy(inplace_buffer_.data(),
                    other.inplace_buffer_.data(),
                    size_ * sizeof(const char*));
      }
    }
    return *this;
  }

  int32_t size() const { return static_cast<int32_t>(size_); }
  const char** c_strs() const { return c_strs_; }

 private:
  size_t size_{};
  const char** c_strs_{};
  std::array<const char*, kInplaceBufferSize> inplace_buffer_;
  std::unique_ptr<const char*[]> allocated_buffer_;
};

// Wraps command line arguments.
class NodeArgs {
 public:
  NodeArgs(int32_t argc, const char* argv[]) : argc_(argc), argv_(argv) {}

  NodeArgs(int32_t argc, char* argv[])
      : argc_(argc), argv_(const_cast<const char**>(argv)) {}

  NodeArgs(const NodeCStringArray<>& string_array_view)
      : argc_(string_array_view.size()), argv_(string_array_view.c_strs()) {}

  int32_t argc() const { return argc_; }
  const char** argv() const { return argv_; }

 private:
  int32_t argc_{};
  const char** argv_{};
};

template <typename TCallback, typename TFunctor, typename TEnable = void>
class NodeFunctorInvoker;

template <typename TCallback>
class NodeFunctorRef;

template <typename TResult, typename... TArgs>
class NodeFunctorRef<TResult (*)(void*, TArgs...)> {
  using TCallback = TResult (*)(void*, TArgs...);

 public:
  NodeFunctorRef(std::nullptr_t) {}

  NodeFunctorRef(TCallback callback, void* data)
      : callback_(callback), data_(data) {}

  template <typename TFunctor>
  NodeFunctorRef(TFunctor&& functor)
      : callback_(&NodeFunctorInvoker<TCallback, TFunctor>::Invoke),
        data_(&functor) {}

  NodeFunctorRef(NodeFunctorRef&& other) = default;
  NodeFunctorRef& operator=(NodeFunctorRef&& other) = default;

  TCallback callback() const { return callback_.ptr(); }

  void* data() const { return data_.ptr(); }

  explicit operator bool() const { return static_cast<bool>(callback_); }

 private:
  NodePointer<TCallback> callback_;
  NodePointer<void*> data_;
};

template <typename TCallback>
class NodeFunctor;

template <typename TResult, typename... TArgs>
class NodeFunctor<TResult (*)(void*, TArgs...)> {
  using TCallback = TResult (*)(void*, TArgs...);

 public:
  NodeFunctor() = default;
  NodeFunctor(std::nullptr_t) {}

  NodeFunctor(TCallback callback,
              void* data,
              node_embedding_data_release_callback data_release)
      : callback_(callback), data_(data), data_release_(data_release) {}

  // TODO: add overload for stateless lambdas.
  template <typename TFunctor>
  NodeFunctor(TFunctor&& functor)
      : callback_(&NodeFunctorInvoker<TCallback, TFunctor>::Invoke),
        data_(new TFunctor(std::forward<TFunctor>(functor))),
        data_release_(&ReleaseFunctor<TFunctor>) {}

  NodeFunctor(NodeFunctor&& other) = default;
  NodeFunctor& operator=(NodeFunctor&& other) = default;

  TCallback callback() const { return callback_.ptr(); }

  void* data() const { return data_.ptr(); }

  node_embedding_data_release_callback data_release() const {
    return data_release_.ptr();
  }

  explicit operator bool() const { return static_cast<bool>(callback_); }

  TResult operator()(TArgs... args) const {
    return (*callback_.ptr())(data_.ptr(), args...);
  }

 private:
  template <typename TFunctor>
  static NodeStatus ReleaseFunctor(void* data) {
    // TODO: Handle exceptions.
    delete reinterpret_cast<TFunctor*>(data);
    return NodeStatus::kOk;
  }

 private:
  NodePointer<TCallback> callback_;
  NodePointer<void*> data_;
  NodePointer<node_embedding_data_release_callback> data_release_;
};

// NodeConfigurePlatformCallback supported signatures:
// - NodeExpected<void>(const NodePlatformConfig& platform_config);
using NodeConfigurePlatformCallback =
    NodeFunctorRef<node_embedding_platform_configure_callback>;

// NodeConfigureRuntimeCallback supported signatures:
// - NodeExpected<void>(const NodePlatform& platform,
//                      const NodeRuntimeConfig& runtime_config);
using NodeConfigureRuntimeCallback =
    NodeFunctorRef<node_embedding_runtime_configure_callback>;

// NodePreloadCallback supported signatures:
// - void(const NodeRuntime& runtime,
//        napi_env env,
//        napi_value process,
//        napi_value require);
using NodePreloadCallback =
    NodeFunctor<node_embedding_runtime_preload_callback>;

// NodeStartExecutionCallback supported signatures:
// - napi_value(const NodeRuntime& runtime,
//              napi_env env,
//              napi_value process,
//              napi_value require,
//              napi_value run_cjs);
using NodeStartExecutionCallback =
    NodeFunctor<node_embedding_runtime_loading_callback>;

// NodeHandleExecutionResultCallback supported signatures:
// - void(const NodeRuntime& runtime,
//        napi_env env,
//        napi_value execution_result);
using NodeHandleExecutionResultCallback =
    NodeFunctor<node_embedding_runtime_loaded_callback>;

// NodeInitializeModuleCallback supported signatures:
// - napi_value(const NodeRuntime& runtime,
//              napi_env env,
//              std::string_view module_name,
//              napi_value exports);
using NodeInitializeModuleCallback =
    NodeFunctor<node_embedding_module_initialize_callback>;

// NodeRunTaskCallback supported signatures:
// - NodeExpected<void>();
using NodeRunTaskCallback = NodeFunctor<node_embedding_task_run_callback>;

// NodePostTaskCallback supported signatures:
// - NodeExpected<bool>(NodeRunTaskCallback run_task);
using NodePostTaskCallback = NodeFunctor<node_embedding_task_post_callback>;

// NodeRunNodeApiCallback supported signatures:
// - void(const NodeRuntime& runtime, napi_env env);
using NodeRunNodeApiCallback =
    NodeFunctorRef<node_embedding_node_api_run_callback>;

inline std::string NodeFormatStringHelper(const char* format, va_list args) {
  va_list args2;  // Required for some compilers like GCC since we go over the
                  // args twice.
  va_copy(args2, args);
  std::string result(std::vsnprintf(nullptr, 0, format, args), '\0');
  std::vsnprintf(&result[0], result.size() + 1, format, args2);
  va_end(args2);
  return result;
}

inline std::string NodeFormatString(const char* format, ...) {
  va_list args;
  va_start(args, format);
  std::string result = NodeFormatStringHelper(format, args);
  va_end(args);
  return result;
}

class NodeErrorInfo {
 public:
  static const char* GetLastErrorMessage() {
    return node_embedding_last_error_message_get();
  }

  static void SetLastErrorMessage(const char* message) {
    return node_embedding_last_error_message_set(message);
  }

  static void SetLastErrorMessage(std::string_view message,
                                  std::string_view filename,
                                  int32_t line) {
    SetLastErrorMessage(
        NodeFormatString(
            "Error: %s at %s:%d", message.data(), filename.data(), line)
            .c_str());
  }

  static void SetLastErrorMessage(const std::vector<std::string>& message) {
    std::string message_str;
    bool first = true;
    for (const std::string& part : message) {
      if (!first) {
        message_str += '\n';
      } else {
        first = false;
      }
      message_str += part;
    }
    SetLastErrorMessage(message_str.c_str());
  }

  static void ClearLastErrorMessage() {
    node_embedding_last_error_message_set(nullptr);
  }

  static std::string GetAndClearLastErrorMessage() {
    std::string result = GetLastErrorMessage();
    ClearLastErrorMessage();
    return result;
  }
};

class NodeEmbeddingErrorHandler {
 public:
  NodeEmbeddingErrorHandler() = default;

  ~NodeEmbeddingErrorHandler() { current_internal() = previous_handler_; }

  void SetLastErrorMessage(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::string message = NodeFormatString(format, args);
    va_end(args);
    node_embedding_last_error_message_set(message.c_str());
    embedding_status_ = NodeStatus::kGenericError;
  }

  NodeExpected<void> ReportResult() const {
    return NodeExpected<void>(embedding_status_);
  }

  bool has_embedding_error() const {
    return embedding_status_ != NodeStatus::kOk;
  }

  NodeStatus embedding_status() const { return embedding_status_; }

  void set_embedding_status(NodeStatus status) { embedding_status_ = status; }

  void set_embedding_status(const NodeExpected<void>& expected) {
    embedding_status_ = expected.status();
  }

  static NodeEmbeddingErrorHandler* current() { return current_internal(); }

  static void CurrentSetEmbeddingStatus(NodeStatus status) {
    if (NodeEmbeddingErrorHandler* current = current_internal()) {
      current->set_embedding_status(status);
    }
  }

  NodeEmbeddingErrorHandler(const NodeEmbeddingErrorHandler&) = delete;
  NodeEmbeddingErrorHandler& operator=(const NodeEmbeddingErrorHandler&) =
      delete;

 private:
  // Declaring operator new and delete as deleted is not spec compliant.
  // Therefore declare them private instead to disable dynamic alloc
  void* operator new(size_t size) { std::abort(); }
  void* operator new[](size_t size) { std::abort(); }
  void operator delete(void*, size_t) { std::abort(); }
  void operator delete[](void*, size_t) { std::abort(); }

  static NodeEmbeddingErrorHandler*& current_internal() {
    static thread_local NodeEmbeddingErrorHandler* current_handler = nullptr;
    return current_handler;
  }

 private:
  NodeEmbeddingErrorHandler* previous_handler_{
      std::exchange(current_internal(), this)};
  NodeStatus embedding_status_{NodeStatus::kOk};
};

class NodeApiErrorHandlerBase {
 public:
  void SetLastErrorMessage(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::string message = NodeFormatString(format, args);
    va_end(args);
    ThrowLastErrorMessage(env_, message.c_str());
    node_api_status_ = napi_pending_exception;
  }

  static void GetAndThrowLastErrorMessage(napi_env env) {
    const napi_extended_error_info* error_info;
    napi_get_last_error_info(env, &error_info);
    ThrowLastErrorMessage(env, error_info->error_message);
  }

  static void ThrowLastErrorMessage(napi_env env, const char* message) {
    bool is_pending;
    napi_is_exception_pending(env, &is_pending);
    /* If an exception is already pending, don't rethrow it */
    if (!is_pending) {
      const char* error_message =
          message != nullptr ? message : "empty error message";
      napi_throw_error(env, nullptr, error_message);
    }
  }

  napi_status ReportResult() const { return node_api_status_; }

  napi_env env() const { return env_; }

  bool has_node_api_error() const { return node_api_status_ != napi_ok; }

  napi_status node_api_status() const { return node_api_status_; }

  void set_node_api_status(napi_status status) { node_api_status_ = status; }

  bool has_embedding_error() const {
    return embedding_status_ != NodeStatus::kOk;
  }

  NodeStatus embedding_status() const { return embedding_status_; }

  void set_embedding_status(NodeStatus status) {
    embedding_status_ = status;
    ThrowLastErrorMessage(env_, node_embedding_last_error_message_get());
    node_api_status_ = napi_pending_exception;
  }

  void set_embedding_status(const NodeExpected<void>& expected) {
    set_embedding_status(expected.status());
  }

  NodeApiErrorHandlerBase(const NodeApiErrorHandlerBase&) = delete;
  NodeApiErrorHandlerBase& operator=(const NodeApiErrorHandlerBase&) = delete;

 protected:
  NodeApiErrorHandlerBase(napi_env env) : env_(env) {}

 private:
  // Declaring operator new and delete as deleted is not spec compliant.
  // Therefore declare them private instead to disable dynamic alloc
  void* operator new(size_t size) { std::abort(); }
  void* operator new[](size_t size) { std::abort(); }
  void operator delete(void*, size_t) { std::abort(); }
  void operator delete[](void*, size_t) { std::abort(); }

 private:
  napi_env env_{nullptr};
  napi_status node_api_status_{napi_ok};
  NodeStatus embedding_status_{NodeStatus::kOk};
};

template <typename TValue>
class NodeApiErrorHandler : public NodeApiErrorHandlerBase {
 public:
  NodeApiErrorHandler(napi_env env) : NodeApiErrorHandlerBase(env) {}

  TValue ReportResult() const {
    if constexpr (std::is_same_v<TValue, napi_status>) {
      return NodeApiErrorHandlerBase::ReportResult();
    } else {
      GetAndThrowLastErrorMessage(env());
      return TValue();
    }
  }
};

// Wraps the Node.js platform instance.
class NodePlatform {
 public:
  explicit NodePlatform(node_embedding_platform platform)
      : platform_(platform) {}

  NodePlatform(NodePlatform&& other) = default;
  NodePlatform& operator=(NodePlatform&& other) = default;

  ~NodePlatform() {
    if (platform_) {
      NodeStatus status = node_embedding_platform_delete(platform_.ptr());
      if (NodeEmbeddingErrorHandler* current =
              NodeEmbeddingErrorHandler::current()) {
        current->set_embedding_status(status);
      }
    }
  }

  explicit operator bool() const { return static_cast<bool>(platform_); }

  operator node_embedding_platform() const { return platform_.ptr(); }

  node_embedding_platform Detach() {
    return std::exchange(platform_, nullptr).ptr();
  }

  static NodeExpected<void> RunMain(
      NodeArgs args,
      NodeConfigurePlatformCallback configure_platform,
      NodeConfigureRuntimeCallback configure_runtime) {
    return NodeExpected<void>(
        node_embedding_main_run(NODE_EMBEDDING_VERSION,
                                args.argc(),
                                args.argv(),
                                configure_platform.callback(),
                                configure_platform.data(),
                                configure_runtime.callback(),
                                configure_runtime.data()));
  }

  static NodeExpected<NodePlatform> Create(
      NodeArgs args, NodeConfigurePlatformCallback configure_platform) {
    node_embedding_platform platform{};
    NodeStatus status =
        node_embedding_platform_create(NODE_EMBEDDING_VERSION,
                                       args.argc(),
                                       args.argv(),
                                       configure_platform.callback(),
                                       configure_platform.data(),
                                       &platform);
    if (status != NodeStatus::kOk) {
      return NodeExpected<NodePlatform>(status);
    }
    return NodeExpected<NodePlatform>(NodePlatform(platform));
  }

  NodeExpected<std::vector<std::string>> GetArgs() const {
    int32_t argc = 0;
    const char** argv = nullptr;
    NodeStatus status = node_embedding_platform_get_parsed_args(
        platform_.ptr(), &argc, &argv, nullptr, nullptr);
    if (status != NodeStatus::kOk) {
      return NodeExpected<std::vector<std::string>>(status);
    }
    return NodeExpected<std::vector<std::string>>(
        std::vector<std::string>(argv, argv + argc));
  }

  NodeExpected<std::vector<std::string>> GetRuntimeArgs() const {
    int32_t argc = 0;
    const char** argv = nullptr;
    NodeStatus status = node_embedding_platform_get_parsed_args(
        platform_.ptr(), nullptr, nullptr, &argc, &argv);
    if (status != NodeStatus::kOk) {
      return NodeExpected<std::vector<std::string>>(status);
    }
    return NodeExpected<std::vector<std::string>>(
        std::vector<std::string>(argv, argv + argc));
  }

 private:
  NodePointer<node_embedding_platform> platform_;
};

// The NodePlatform that does not delete the platform on destruction.
class NodeDetachedPlatform : public NodePlatform {
 public:
  explicit NodeDetachedPlatform(node_embedding_platform platform)
      : NodePlatform(platform) {}

  ~NodeDetachedPlatform() { Detach(); }
};

class NodePlatformConfig {
 public:
  explicit NodePlatformConfig(node_embedding_platform_config platform_config)
      : platform_config_(platform_config) {}

  NodePlatformConfig(NodePlatformConfig&& other) = default;
  NodePlatformConfig& operator=(NodePlatformConfig&& other) = default;

  operator node_embedding_platform_config() const {
    return platform_config_.ptr();
  }

  NodeExpected<void> SetFlags(NodePlatformFlags flags) const {
    return NodeExpected<void>(node_embedding_platform_config_set_flags(
        platform_config_.ptr(), flags));
  }

 private:
  NodePointer<node_embedding_platform_config> platform_config_{};
};

class NodeApiScope {
 public:
  static NodeExpected<NodeApiScope> Open(node_embedding_runtime runtime) {
    node_embedding_node_api_scope node_api_scope{};
    napi_env env{};
    NodeStatus status = node_embedding_runtime_node_api_scope_open(
        runtime, &node_api_scope, &env);
    if (status != NodeStatus::kOk) {
      return NodeExpected<NodeApiScope>(status);
    }
    return NodeExpected<NodeApiScope>(
        NodeApiScope(runtime, node_api_scope, env));
  }

  explicit NodeApiScope(node_embedding_runtime runtime,
                        node_embedding_node_api_scope node_api_scope,
                        napi_env env)
      : runtime_(runtime), node_api_scope_(node_api_scope), env_(env) {}

  NodeApiScope(NodeApiScope&&) = default;
  NodeApiScope& operator=(NodeApiScope&&) = default;

  ~NodeApiScope() {
    if (runtime_) {
      NodeEmbeddingErrorHandler::CurrentSetEmbeddingStatus(
          node_embedding_runtime_node_api_scope_close(runtime_.ptr(),
                                                      node_api_scope_.ptr()));
    }
  }

  napi_env env() const { return env_.ptr(); }

 private:
  NodePointer<node_embedding_runtime> runtime_;
  NodePointer<node_embedding_node_api_scope> node_api_scope_;
  NodePointer<napi_env> env_;
};

class NodeRuntime {
 public:
  static NodeExpected<void> Run(
      const NodePlatform& platform,
      NodeConfigureRuntimeCallback configure_runtime) {
    return NodeExpected<void>(node_embedding_runtime_run(
        static_cast<node_embedding_platform>(platform),
        configure_runtime.callback(),
        configure_runtime.data()));
  }

  static NodeExpected<NodeRuntime> Create(
      const NodePlatform& platform,
      NodeConfigureRuntimeCallback configure_runtime) {
    node_embedding_runtime runtime;
    NodeStatus status =
        node_embedding_runtime_create(platform,
                                      configure_runtime.callback(),
                                      configure_runtime.data(),
                                      &runtime);
    if (status != NodeStatus::kOk) {
      return NodeExpected<NodeRuntime>(status);
    }
    return NodeExpected<NodeRuntime>(NodeRuntime(runtime));
  }

  explicit NodeRuntime(node_embedding_runtime runtime) : runtime_(runtime) {}

  NodeRuntime(NodeRuntime&& other) = default;
  NodeRuntime& operator=(NodeRuntime&& other) = default;

  ~NodeRuntime() {
    if (runtime_) {
      NodeEmbeddingErrorHandler::CurrentSetEmbeddingStatus(
          node_embedding_runtime_delete(runtime_.ptr()));
      ;
    }
  }

  operator node_embedding_runtime() const { return runtime_.ptr(); }

  node_embedding_runtime Detach() {
    return std::exchange(runtime_, nullptr).ptr();
  }

  NodeExpected<void> RunEventLoop() const {
    return NodeExpected<void>(
        node_embedding_runtime_event_loop_run(runtime_.ptr()));
  }

  NodeExpected<void> TerminateEventLoop() const {
    return NodeExpected<void>(
        node_embedding_runtime_event_loop_terminate(runtime_.ptr()));
  }

  NodeExpected<bool> RunEventLoopOnce() const {
    bool has_more_work{};
    NodeStatus status = node_embedding_runtime_event_loop_run_once(
        runtime_.ptr(), &has_more_work);
    if (status != NodeStatus::kOk) {
      return NodeExpected<bool>(status);
    }
    return NodeExpected<bool>(has_more_work);
  }

  NodeExpected<bool> RunEventLoopNoWait() const {
    bool has_more_work{};
    NodeStatus status = node_embedding_runtime_event_loop_run_no_wait(
        runtime_.ptr(), &has_more_work);
    if (status != NodeStatus::kOk) {
      return NodeExpected<bool>(status);
    }
    return NodeExpected<bool>(has_more_work);
  }

  NodeExpected<void> RunNodeApi(NodeRunNodeApiCallback run_node_api) const {
    return NodeExpected<void>(node_embedding_runtime_node_api_run(
        runtime_.ptr(), run_node_api.callback(), run_node_api.data()));
  }

  NodeExpected<NodeApiScope> OpenNodeApiScope() const {
    return NodeApiScope::Open(runtime_.ptr());
  }

 private:
  NodePointer<node_embedding_runtime> runtime_{};
};

class NodeDetachedRuntime : public NodeRuntime {
 public:
  explicit NodeDetachedRuntime(node_embedding_runtime runtime)
      : NodeRuntime(runtime) {}
  ~NodeDetachedRuntime() { Detach(); }
};

class NodeRuntimeConfig {
 public:
  explicit NodeRuntimeConfig(node_embedding_runtime_config runtime_config)
      : runtime_config_(runtime_config) {}

  NodeRuntimeConfig(NodeRuntimeConfig&& other) = default;
  NodeRuntimeConfig& operator=(NodeRuntimeConfig&& other) = default;

  operator node_embedding_runtime_config() const {
    return runtime_config_.ptr();
  }

  NodeExpected<void> SetNodeApiVersion(int32_t node_api_version) const {
    return NodeExpected<void>(
        node_embedding_runtime_config_set_node_api_version(
            runtime_config_.ptr(), node_api_version));
  }

  NodeExpected<void> SetFlags(NodeRuntimeFlags flags) const {
    return NodeExpected<void>(
        node_embedding_runtime_config_set_flags(runtime_config_.ptr(), flags));
  }

  NodeExpected<void> SetArgs(NodeArgs args, NodeArgs runtime_args) const {
    return NodeExpected<void>(
        node_embedding_runtime_config_set_args(runtime_config_.ptr(),
                                               args.argc(),
                                               args.argv(),
                                               runtime_args.argc(),
                                               runtime_args.argv()));
  }

  NodeExpected<void> OnPreload(NodePreloadCallback preload) const {
    return NodeExpected<void>(
        node_embedding_runtime_config_on_preload(runtime_config_.ptr(),
                                                 preload.callback(),
                                                 preload.data(),
                                                 preload.data_release()));
  }

  NodeExpected<void> OnStartExecution(
      NodeStartExecutionCallback start_execution) const {
    return NodeExpected<void>(node_embedding_runtime_config_on_loading(
        runtime_config_.ptr(),
        start_execution.callback(),
        start_execution.data(),
        start_execution.data_release()));
  }

  NodeExpected<void> OnLoaded(
      NodeHandleExecutionResultCallback handle_start_result) const {
    return NodeExpected<void>(node_embedding_runtime_config_on_loaded(
        runtime_config_.ptr(),
        handle_start_result.callback(),
        handle_start_result.data(),
        handle_start_result.data_release()));
  }

  NodeExpected<void> AddModule(std::string_view module_name,
                               NodeInitializeModuleCallback init_module,
                               int32_t moduleNodeApiVersion) const {
    return NodeExpected<void>(
        node_embedding_runtime_config_add_module(runtime_config_.ptr(),
                                                 module_name.data(),
                                                 init_module.callback(),
                                                 init_module.data(),
                                                 init_module.data_release(),
                                                 moduleNodeApiVersion));
  }

  NodeExpected<void> SetTaskRunner(NodePostTaskCallback post_task) const {
    return NodeExpected<void>(node_embedding_runtime_config_set_task_runner(
        runtime_config_.ptr(),
        post_task.callback(),
        post_task.data(),
        post_task.data_release()));
  }

 private:
  NodePointer<node_embedding_runtime_config> runtime_config_{};
};

template <typename TFunctor>
class NodeFunctorInvoker<
    node_embedding_platform_configure_callback,
    TFunctor,
    std::enable_if_t<std::is_invocable_r_v<NodeExpected<void>,
                                           TFunctor,
                                           const NodePlatformConfig&>>> {
 public:
  static NodeStatus Invoke(void* cb_data,
                           node_embedding_platform_config platform_config) {
    TFunctor* callback = reinterpret_cast<TFunctor*>(cb_data);
    NodePlatformConfig platform_config_cpp(platform_config);
    NodeExpected<void> result_cpp = (*callback)(platform_config_cpp);
    return result_cpp.status();
  }
};

template <typename TFunctor>
class NodeFunctorInvoker<
    node_embedding_runtime_configure_callback,
    TFunctor,
    std::enable_if_t<std::is_invocable_r_v<NodeExpected<void>,
                                           TFunctor,
                                           const NodePlatform&,
                                           const NodeRuntimeConfig&>>> {
 public:
  static NodeStatus Invoke(void* cb_data,
                           node_embedding_platform platform,
                           node_embedding_runtime_config runtime_config) {
    TFunctor* callback = reinterpret_cast<TFunctor*>(cb_data);
    NodeDetachedPlatform platform_cpp(platform);
    NodeRuntimeConfig runtime_config_cpp(runtime_config);
    NodeExpected<void> result_cpp =
        (*callback)(platform_cpp, runtime_config_cpp);
    return result_cpp.status();
  }
};

template <typename TFunctor>
class NodeFunctorInvoker<
    node_embedding_runtime_preload_callback,
    TFunctor,
    std::enable_if_t<std::is_invocable_r_v<void,
                                           TFunctor,
                                           const NodeRuntime&,
                                           napi_env,
                                           napi_value,
                                           napi_value>>> {
 public:
  static void Invoke(void* cb_data,
                     node_embedding_runtime runtime,
                     napi_env env,
                     napi_value process,
                     napi_value require) {
    TFunctor* callback = reinterpret_cast<TFunctor*>(cb_data);
    NodeDetachedRuntime runtime_cpp(runtime);
    (*callback)(runtime_cpp, env, process, require);
  }
};

template <typename TFunctor>
class NodeFunctorInvoker<
    node_embedding_runtime_loading_callback,
    TFunctor,
    std::enable_if_t<std::is_invocable_r_v<napi_value,
                                           TFunctor,
                                           const NodeRuntime&,
                                           napi_env,
                                           napi_value,
                                           napi_value,
                                           napi_value>>> {
 public:
  static napi_value Invoke(void* cb_data,
                           node_embedding_runtime runtime,
                           napi_env env,
                           napi_value process,
                           napi_value require,
                           napi_value run_cjs) {
    TFunctor* callback = reinterpret_cast<TFunctor*>(cb_data);
    NodeDetachedRuntime runtime_cpp(runtime);
    return (*callback)(runtime_cpp, env, process, require, run_cjs);
  }
};

template <typename TFunctor>
class NodeFunctorInvoker<
    node_embedding_runtime_loaded_callback,
    TFunctor,
    std::enable_if_t<std::is_invocable_r_v<void,
                                           TFunctor,
                                           const NodeRuntime&,
                                           napi_env,
                                           napi_value>>> {
 public:
  static void Invoke(void* cb_data,
                     node_embedding_runtime runtime,
                     napi_env env,
                     napi_value execution_result) {
    TFunctor* callback = reinterpret_cast<TFunctor*>(cb_data);
    NodeDetachedRuntime runtime_cpp(runtime);
    (*callback)(runtime_cpp, env, execution_result);
  }
};

template <typename TFunctor>
class NodeFunctorInvoker<
    node_embedding_module_initialize_callback,
    TFunctor,
    std::enable_if_t<std::is_invocable_r_v<napi_value,
                                           TFunctor,
                                           const NodeRuntime&,
                                           napi_env,
                                           std::string_view,
                                           napi_value>>> {
 public:
  static napi_value Invoke(void* cb_data,
                           node_embedding_runtime runtime,
                           napi_env env,
                           const char* module_name,
                           napi_value exports) {
    TFunctor* callback = reinterpret_cast<TFunctor*>(cb_data);
    NodeDetachedRuntime runtime_cpp(runtime);
    return (*callback)(runtime_cpp, env, module_name, exports);
  }
};

template <typename TFunctor>
class NodeFunctorInvoker<
    node_embedding_task_run_callback,
    TFunctor,
    std::enable_if_t<std::is_invocable_r_v<NodeExpected<void>, TFunctor>>> {
 public:
  static NodeStatus Invoke(void* cb_data) {
    TFunctor* callback = reinterpret_cast<TFunctor*>(cb_data);
    return (*callback)().status();
  }
};

template <typename TFunctor>
class NodeFunctorInvoker<
    node_embedding_task_post_callback,
    TFunctor,
    std::enable_if_t<std::is_invocable_r_v<NodeExpected<bool>,
                                           TFunctor,
                                           NodeRunTaskCallback>>> {
 public:
  static NodeStatus Invoke(
      void* cb_data,
      node_embedding_task_run_callback run_task,
      void* task_data,
      node_embedding_data_release_callback release_task_data,
      bool* is_posted) {
    TFunctor* callback = reinterpret_cast<TFunctor*>(cb_data);
    NodeExpected<bool> result_cpp = (*callback)(
        NodeRunTaskCallback(run_task, task_data, release_task_data));
    if (is_posted != nullptr) {
      *is_posted = result_cpp.value();
    }
    return result_cpp.status();
  }
};

template <typename TFunctor>
class NodeFunctorInvoker<
    node_embedding_node_api_run_callback,
    TFunctor,
    std::enable_if_t<std::is_invocable_r_v<void, TFunctor, napi_env>>> {
 public:
  static void Invoke(void* cb_data, napi_env env) {
    TFunctor* callback = reinterpret_cast<TFunctor*>(cb_data);
    (*callback)(env);
  }
};

}  // namespace node::embedding

#endif  // SRC_NODE_EMBEDDING_API_CPP_H_
