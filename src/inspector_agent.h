#pragma once

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#if !HAVE_INSPECTOR
#error("This header can only be used when inspector is enabled")
#endif

#include "node_options.h"
#include "v8.h"

#include <cstddef>
#include <memory>

namespace v8_inspector {
class StringView;
}  // namespace v8_inspector

namespace node {
// Forward declaration to break recursive dependency chain with src/env.h.
class Environment;
struct ContextInfo;

namespace inspector {
class InspectorIo;
class ParentInspectorHandle;
class NodeInspectorClient;
class WorkerManager;

class InspectorSession {
 public:
  virtual ~InspectorSession() = default;
  virtual void Dispatch(const v8_inspector::StringView& message) = 0;
};

class InspectorSessionDelegate {
 public:
  virtual ~InspectorSessionDelegate() = default;
  virtual void SendMessageToFrontend(const v8_inspector::StringView& message)
                                     = 0;
};

class Agent {
 public:
  explicit Agent(node::Environment* env);
  ~Agent();

  // Create client_, may create io_ if option enabled
  bool Start(const std::string& path,
             const DebugOptions& options,
             std::shared_ptr<ExclusiveAccess<HostPort>> host_port,
             bool is_main);
  // Stop and destroy io_
  void Stop();

  bool IsListening() { return io_ != nullptr; }
  // Returns true if the Node inspector is actually in use. It will be true
  // if either the user explicitly opted into inspector (e.g. with the
  // --inspect command line flag) or if inspector JS API had been used.
  bool IsActive();

  // Blocks till frontend connects and sends "runIfWaitingForDebugger"
  void WaitForConnect();
  bool WaitForConnectByOptions();
  void StopIfWaitingForConnect();

  // Blocks till all the sessions with "WaitForDisconnectOnShutdown" disconnect
  void WaitForDisconnect();
  void ReportUncaughtException(v8::Local<v8::Value> error,
                               v8::Local<v8::Message> message);

  void EmitProtocolEvent(v8::Local<v8::Context> context,
                         const v8_inspector::StringView& event,
                         v8::Local<v8::Object> params);

  void SetupNetworkTracking(v8::Local<v8::Function> enable_function,
                            v8::Local<v8::Function> disable_function);
  void EnableNetworkTracking();
  void DisableNetworkTracking();

  // Async stack traces instrumentation.
  void AsyncTaskScheduled(const v8_inspector::StringView& taskName, void* task,
                          bool recurring);
  void AsyncTaskCanceled(void* task);
  void AsyncTaskStarted(void* task);
  void AsyncTaskFinished(void* task);
  void AllAsyncTasksCanceled();

  void RegisterAsyncHook(v8::Isolate* isolate,
    v8::Local<v8::Function> enable_function,
    v8::Local<v8::Function> disable_function);
  void EnableAsyncHook();
  void DisableAsyncHook();

  void SetParentHandle(std::unique_ptr<ParentInspectorHandle> parent_handle);
  std::unique_ptr<ParentInspectorHandle> GetParentHandle(
      uint64_t thread_id, const std::string& url, const std::string& name);

  // Called to create inspector sessions that can be used from the same thread.
  // The inspector responds by using the delegate to send messages back.
  std::unique_ptr<InspectorSession> Connect(
      std::unique_ptr<InspectorSessionDelegate> delegate,
      bool prevent_shutdown);

  // Called from the worker to create inspector sessions that is connected
  // to the main thread.
  // The inspector responds by using the delegate to send messages back.
  std::unique_ptr<InspectorSession> ConnectToMainThread(
      std::unique_ptr<InspectorSessionDelegate> delegate,
      bool prevent_shutdown);

  void PauseOnNextJavascriptStatement(const std::string& reason);

  std::string GetWsUrl() const;

  // Can only be called from the main thread.
  bool StartIoThread();

  // Calls StartIoThread() from off the main thread.
  void RequestIoThreadStart();

  const DebugOptions& options() { return debug_options_; }
  std::shared_ptr<ExclusiveAccess<HostPort>> host_port() { return host_port_; }
  void ContextCreated(v8::Local<v8::Context> context, const ContextInfo& info);

  // Interface for interacting with inspectors in worker threads
  std::shared_ptr<WorkerManager> GetWorkerManager();

  inline Environment* env() const { return parent_env_; }

 private:
  void ToggleAsyncHook(v8::Isolate* isolate, v8::Local<v8::Function> fn);
  void ToggleNetworkTracking(v8::Isolate* isolate, v8::Local<v8::Function> fn);

  node::Environment* parent_env_;
  // Encapsulates majority of the Inspector functionality
  std::shared_ptr<NodeInspectorClient> client_;
  // Interface for transports, e.g. WebSocket server
  std::unique_ptr<InspectorIo> io_;
  std::unique_ptr<ParentInspectorHandle> parent_handle_;
  std::string path_;

  // This is a copy of the debug options parsed from CLI in the Environment.
  // Do not use the host_port in that, instead manipulate the shared host_port_
  // pointer which is meant to store the actual host and port of the inspector
  // server.
  DebugOptions debug_options_;
  std::shared_ptr<ExclusiveAccess<HostPort>> host_port_;

  bool pending_enable_async_hook_ = false;
  bool pending_disable_async_hook_ = false;

  bool network_tracking_enabled_ = false;
  bool pending_enable_network_tracking = false;
  bool pending_disable_network_tracking = false;
};

}  // namespace inspector
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
