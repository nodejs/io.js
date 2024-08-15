#include "inspector_agent.h"

#include "env-inl.h"
#include "inspector/main_thread_interface.h"
#include "inspector/network_inspector.h"
#include "inspector/node_string.h"
#include "inspector/runtime_agent.h"
#include "inspector/tracing_agent.h"
#include "inspector/worker_agent.h"
#include "inspector/worker_inspector.h"
#include "inspector_io.h"
#include "node/inspector/protocol/Protocol.h"
#include "node_errors.h"
#include "node_internals.h"
#include "node_options-inl.h"
#include "node_process-inl.h"
#include "node_url.h"
#include "permission/permission.h"
#include "timer_wrap-inl.h"
#include "util-inl.h"
#include "v8-inspector.h"
#include "v8-platform.h"

#include "libplatform/libplatform.h"

#ifdef __POSIX__
#include <pthread.h>
#include <climits>  // PTHREAD_STACK_MIN
#endif  // __POSIX__

#include <algorithm>
#include <cstring>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace node {
namespace inspector {
namespace {
using v8::Context;
using v8::Function;
using v8::HandleScope;
using v8::Isolate;
using v8::Local;
using v8::Message;
using v8::Object;
using v8::Value;

using v8_inspector::StringBuffer;
using v8_inspector::StringView;
using v8_inspector::V8Inspector;
using v8_inspector::V8InspectorClient;

#ifdef __POSIX__
static uv_sem_t start_io_thread_semaphore;
#endif  // __POSIX__
static uv_async_t start_io_thread_async;
// This is just an additional check to make sure start_io_thread_async
// is not accidentally re-used or used when uninitialized.
static std::atomic_bool start_io_thread_async_initialized { false };
// Protects the Agent* stored in start_io_thread_async.data.
static Mutex start_io_thread_async_mutex;

std::unique_ptr<StringBuffer> ToProtocolString(Isolate* isolate,
                                               Local<Value> value) {
  TwoByteValue buffer(isolate, value);
  return StringBuffer::create(StringView(*buffer, buffer.length()));
}

// Called on the main thread.
void StartIoThreadAsyncCallback(uv_async_t* handle) {
  static_cast<Agent*>(handle->data)->StartIoThread();
}


#ifdef __POSIX__
static void StartIoThreadWakeup(int signo, siginfo_t* info, void* ucontext) {
  uv_sem_post(&start_io_thread_semaphore);
}

inline void* StartIoThreadMain(void* unused) {
  for (;;) {
    uv_sem_wait(&start_io_thread_semaphore);
    Mutex::ScopedLock lock(start_io_thread_async_mutex);

    CHECK(start_io_thread_async_initialized);
    Agent* agent = static_cast<Agent*>(start_io_thread_async.data);
    if (agent != nullptr)
      agent->RequestIoThreadStart();
  }
}

static int StartDebugSignalHandler() {
  // Start a watchdog thread for calling v8::Debug::DebugBreak() because
  // it's not safe to call directly from the signal handler, it can
  // deadlock with the thread it interrupts.
  CHECK_EQ(0, uv_sem_init(&start_io_thread_semaphore, 0));
  pthread_attr_t attr;
  CHECK_EQ(0, pthread_attr_init(&attr));
#if defined(PTHREAD_STACK_MIN) && !defined(__FreeBSD__)
  // PTHREAD_STACK_MIN is 2 KiB with musl libc, which is too small to safely
  // receive signals. PTHREAD_STACK_MIN + MINSIGSTKSZ is 8 KiB on arm64, which
  // is the musl architecture with the biggest MINSIGSTKSZ so let's use that
  // as a lower bound and let's quadruple it just in case. The goal is to avoid
  // creating a big 2 or 4 MiB address space gap (problematic on 32 bits
  // because of fragmentation), not squeeze out every last byte.
  // Omitted on FreeBSD because it doesn't seem to like small stacks.
  const size_t stack_size = std::max(static_cast<size_t>(4 * 8192),
                                     static_cast<size_t>(PTHREAD_STACK_MIN));
  CHECK_EQ(0, pthread_attr_setstacksize(&attr, stack_size));
#endif  // defined(PTHREAD_STACK_MIN) && !defined(__FreeBSD__)
  CHECK_EQ(0, pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED));
  sigset_t sigmask;
  // Mask all signals.
  sigfillset(&sigmask);
  sigset_t savemask;
  CHECK_EQ(0, pthread_sigmask(SIG_SETMASK, &sigmask, &savemask));
  sigmask = savemask;
  pthread_t thread;
  const int err = pthread_create(&thread, &attr,
                                 StartIoThreadMain, nullptr);
  // Restore original mask
  CHECK_EQ(0, pthread_sigmask(SIG_SETMASK, &sigmask, nullptr));
  CHECK_EQ(0, pthread_attr_destroy(&attr));
  if (err != 0) {
    fprintf(stderr, "node[%u]: pthread_create: %s\n",
            uv_os_getpid(), strerror(err));
    fflush(stderr);
    // Leave SIGUSR1 blocked.  We don't install a signal handler,
    // receiving the signal would terminate the process.
    return -err;
  }
  RegisterSignalHandler(SIGUSR1, StartIoThreadWakeup);
  // Unblock SIGUSR1.  A pending SIGUSR1 signal will now be delivered.
  sigemptyset(&sigmask);
  sigaddset(&sigmask, SIGUSR1);
  CHECK_EQ(0, pthread_sigmask(SIG_UNBLOCK, &sigmask, nullptr));
  return 0;
}
#endif  // __POSIX__


#ifdef _WIN32
DWORD WINAPI StartIoThreadProc(void* arg) {
  Mutex::ScopedLock lock(start_io_thread_async_mutex);
  CHECK(start_io_thread_async_initialized);
  Agent* agent = static_cast<Agent*>(start_io_thread_async.data);
  if (agent != nullptr)
    agent->RequestIoThreadStart();
  return 0;
}

static int GetDebugSignalHandlerMappingName(DWORD pid, wchar_t* buf,
                                            size_t buf_len) {
  return _snwprintf(buf, buf_len, L"node-debug-handler-%u", pid);
}

static int StartDebugSignalHandler() {
  wchar_t mapping_name[32];
  HANDLE mapping_handle;
  DWORD pid;
  LPTHREAD_START_ROUTINE* handler;

  pid = uv_os_getpid();

  if (GetDebugSignalHandlerMappingName(pid,
                                       mapping_name,
                                       arraysize(mapping_name)) < 0) {
    return -1;
  }

  mapping_handle = CreateFileMappingW(INVALID_HANDLE_VALUE,
                                      nullptr,
                                      PAGE_READWRITE,
                                      0,
                                      sizeof *handler,
                                      mapping_name);
  if (mapping_handle == nullptr) {
    return -1;
  }

  handler = reinterpret_cast<LPTHREAD_START_ROUTINE*>(
      MapViewOfFile(mapping_handle,
                    FILE_MAP_ALL_ACCESS,
                    0,
                    0,
                    sizeof *handler));
  if (handler == nullptr) {
    CloseHandle(mapping_handle);
    return -1;
  }

  *handler = StartIoThreadProc;

  UnmapViewOfFile(static_cast<void*>(handler));

  return 0;
}
#endif  // _WIN32


const int CONTEXT_GROUP_ID = 1;

std::string GetWorkerLabel(node::Environment* env) {
  std::ostringstream result;
  result << "Worker[" << env->thread_id() << "]";
  return result.str();
}

class ChannelImpl final : public v8_inspector::V8Inspector::Channel,
                          public protocol::FrontendChannel {
 public:
  explicit ChannelImpl(Environment* env,
                       const std::unique_ptr<V8Inspector>& inspector,
                       std::shared_ptr<WorkerManager> worker_manager,
                       std::unique_ptr<InspectorSessionDelegate> delegate,
                       std::shared_ptr<MainThreadHandle> main_thread_,
                       bool prevent_shutdown)
      : delegate_(std::move(delegate)), prevent_shutdown_(prevent_shutdown),
        retaining_context_(false) {
    session_ = inspector->connect(CONTEXT_GROUP_ID,
                                  this,
                                  StringView(),
                                  V8Inspector::ClientTrustLevel::kFullyTrusted);
    node_dispatcher_ = std::make_unique<protocol::UberDispatcher>(this);
    tracing_agent_ =
        std::make_unique<protocol::TracingAgent>(env, main_thread_);
    tracing_agent_->Wire(node_dispatcher_.get());
    if (worker_manager) {
      worker_agent_ = std::make_unique<protocol::WorkerAgent>(worker_manager);
      worker_agent_->Wire(node_dispatcher_.get());
    }
    runtime_agent_ = std::make_unique<protocol::RuntimeAgent>();
    runtime_agent_->Wire(node_dispatcher_.get());
    network_inspector_ = std::make_unique<NetworkInspector>(env);
    network_inspector_->Wire(node_dispatcher_.get());
  }

  ~ChannelImpl() override {
    tracing_agent_->disable();
    tracing_agent_.reset();  // Dispose before the dispatchers
    if (worker_agent_) {
      worker_agent_->disable();
      worker_agent_.reset();  // Dispose before the dispatchers
    }
    runtime_agent_->disable();
    runtime_agent_.reset();  // Dispose before the dispatchers
    network_inspector_->Disable();
    network_inspector_.reset();  // Dispose before the dispatchers
  }

  void emitNotificationFromBackend(const StringView& event,
                                   const StringView& params) {
    std::unique_ptr<protocol::DictionaryValue> value =
        protocol::DictionaryValue::cast(
            protocol::StringUtil::parseJSON(params));
    std::string raw_event = protocol::StringUtil::StringViewToUtf8(event);
    std::string domain_name = raw_event.substr(0, raw_event.find('.'));
    std::string event_name = raw_event.substr(raw_event.find('.') + 1);
    if (network_inspector_->canEmit(domain_name)) {
      network_inspector_->emitNotification(
          domain_name, event_name, std::move(value));
    } else {
      UNREACHABLE("Unknown domain for emitNotificationFromBackend");
    }
  }

  void dispatchProtocolMessage(const StringView& message) {
    std::string raw_message = protocol::StringUtil::StringViewToUtf8(message);
    per_process::Debug(DebugCategory::INSPECTOR_SERVER,
                       "[inspector received] %s\n",
                       raw_message);
    std::unique_ptr<protocol::DictionaryValue> value =
        protocol::DictionaryValue::cast(
            protocol::StringUtil::parseJSON(message));
    int call_id;
    std::string method;
    node_dispatcher_->parseCommand(value.get(), &call_id, &method);
    if (v8_inspector::V8InspectorSession::canDispatchMethod(
            Utf8ToStringView(method)->string())) {
      session_->dispatchProtocolMessage(message);
    } else {
      node_dispatcher_->dispatch(
          call_id, method, std::move(value), raw_message);
    }
  }

  void schedulePauseOnNextStatement(const std::string& reason) {
    std::unique_ptr<StringBuffer> buffer = Utf8ToStringView(reason);
    session_->schedulePauseOnNextStatement(buffer->string(), buffer->string());
  }

  bool preventShutdown() {
    return prevent_shutdown_;
  }

  bool notifyWaitingForDisconnect() {
    retaining_context_ = runtime_agent_->notifyWaitingForDisconnect();
    return retaining_context_;
  }

  void setWaitingForDebugger() { runtime_agent_->setWaitingForDebugger(); }

  void unsetWaitingForDebugger() { runtime_agent_->unsetWaitingForDebugger(); }

  bool retainingContext() {
    return retaining_context_;
  }

 private:
  void sendResponse(
      int callId,
      std::unique_ptr<v8_inspector::StringBuffer> message) override {
    sendMessageToFrontend(message->string());
  }

  void sendNotification(
      std::unique_ptr<v8_inspector::StringBuffer> message) override {
    sendMessageToFrontend(message->string());
  }

  void flushProtocolNotifications() override { }

  void sendMessageToFrontend(const StringView& message) {
    if (per_process::enabled_debug_list.enabled(
            DebugCategory::INSPECTOR_SERVER)) {
      std::string raw_message = protocol::StringUtil::StringViewToUtf8(message);
      per_process::Debug(DebugCategory::INSPECTOR_SERVER,
                         "[inspector send] %s\n",
                         raw_message);
    }
    delegate_->SendMessageToFrontend(message);
  }

  void sendMessageToFrontend(const std::string& message) {
    sendMessageToFrontend(Utf8ToStringView(message)->string());
  }

  using Serializable = protocol::Serializable;

  void sendProtocolResponse(int callId,
                            std::unique_ptr<Serializable> message) override {
    sendMessageToFrontend(message->serializeToJSON());
  }
  void sendProtocolNotification(
      std::unique_ptr<Serializable> message) override {
    sendMessageToFrontend(message->serializeToJSON());
  }

  void fallThrough(int callId,
                   const std::string& method,
                   const std::string& message) override {
    DCHECK(false);
  }

  std::unique_ptr<protocol::RuntimeAgent> runtime_agent_;
  std::unique_ptr<protocol::TracingAgent> tracing_agent_;
  std::unique_ptr<protocol::WorkerAgent> worker_agent_;
  std::unique_ptr<NetworkInspector> network_inspector_;
  std::unique_ptr<InspectorSessionDelegate> delegate_;
  std::unique_ptr<v8_inspector::V8InspectorSession> session_;
  std::unique_ptr<protocol::UberDispatcher> node_dispatcher_;
  bool prevent_shutdown_;
  bool retaining_context_;
};

class SameThreadInspectorSession : public InspectorSession {
 public:
  SameThreadInspectorSession(
      int session_id, std::shared_ptr<NodeInspectorClient> client)
      : session_id_(session_id), client_(client) {}
  ~SameThreadInspectorSession() override;
  void Dispatch(const v8_inspector::StringView& message) override;

 private:
  int session_id_;
  std::weak_ptr<NodeInspectorClient> client_;
};

void NotifyClusterWorkersDebugEnabled(Environment* env) {
  Isolate* isolate = env->isolate();
  HandleScope handle_scope(isolate);
  Local<Context> context = env->context();

  // Send message to enable debug in cluster workers
  Local<Object> message = Object::New(isolate);
  message->Set(context, FIXED_ONE_BYTE_STRING(isolate, "cmd"),
               FIXED_ONE_BYTE_STRING(isolate, "NODE_DEBUG_ENABLED")).Check();
  ProcessEmit(env, "internalMessage", message);
}

#ifdef _WIN32
bool IsFilePath(const std::string& path) {
  // '\\'
  if (path.length() > 2 && path[0] == '\\' && path[1] == '\\')
    return true;
  // '[A-Z]:[/\\]'
  if (path.length() < 3)
    return false;
  if ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'))
    return path[1] == ':' && (path[2] == '/' || path[2] == '\\');
  return false;
}
#else
bool IsFilePath(const std::string& path) {
  return !path.empty() && path[0] == '/';
}
#endif  // __POSIX__

void ThrowUninitializedInspectorError(Environment* env) {
  HandleScope scope(env->isolate());

  const char* msg = "This Environment was initialized without a V8::Inspector";
  Local<Value> exception =
    v8::String::NewFromUtf8(env->isolate(), msg).ToLocalChecked();

  env->isolate()->ThrowException(exception);
}

}  // namespace

class NodeInspectorClient : public V8InspectorClient {
 public:
  explicit NodeInspectorClient(node::Environment* env, bool is_main)
      : env_(env), is_main_(is_main) {
    client_ = V8Inspector::create(env->isolate(), this);
    // TODO(bnoordhuis) Make name configurable from src/node.cc.
    std::string name =
        is_main_ ? GetHumanReadableProcessName() : GetWorkerLabel(env);
    ContextInfo info(name);
    info.is_default = true;
    contextCreated(env->context(), info);
  }

  void runMessageLoopOnPause(int context_group_id) override {
    waiting_for_resume_ = true;
    runMessageLoop();
  }

  void waitForSessionsDisconnect() {
    waiting_for_sessions_disconnect_ = true;
    runMessageLoop();
  }

  void waitForFrontend() {
    waiting_for_frontend_ = true;
    for (const auto& id_channel : channels_) {
      id_channel.second->setWaitingForDebugger();
    }
    runMessageLoop();
  }

  void maxAsyncCallStackDepthChanged(int depth) override {
    if (waiting_for_sessions_disconnect_) {
      // V8 isolate is mostly done and is only letting Inspector protocol
      // clients gather data.
      return;
    }
    if (auto agent = env_->inspector_agent()) {
      if (depth == 0) {
        agent->DisableAsyncHook();
      } else {
        agent->EnableAsyncHook();
      }
    }
  }

  void contextCreated(Local<Context> context, const ContextInfo& info) {
    auto name_buffer = Utf8ToStringView(info.name);
    auto origin_buffer = Utf8ToStringView(info.origin);
    std::unique_ptr<StringBuffer> aux_data_buffer;

    v8_inspector::V8ContextInfo v8info(
        context, CONTEXT_GROUP_ID, name_buffer->string());
    v8info.origin = origin_buffer->string();

    if (info.is_default) {
      aux_data_buffer = Utf8ToStringView("{\"isDefault\":true}");
    } else {
      aux_data_buffer = Utf8ToStringView("{\"isDefault\":false}");
    }
    v8info.auxData = aux_data_buffer->string();

    client_->contextCreated(v8info);
  }

  void contextDestroyed(Local<Context> context) {
    client_->contextDestroyed(context);
  }

  void quitMessageLoopOnPause() override {
    waiting_for_resume_ = false;
  }

  void runIfWaitingForDebugger(int context_group_id) override {
    waiting_for_frontend_ = false;
    for (const auto& id_channel : channels_) {
      id_channel.second->unsetWaitingForDebugger();
    }
  }

  void StopIfWaitingForFrontendEvent() {
    if (!waiting_for_frontend_) {
      return;
    }
    waiting_for_frontend_ = false;
    for (const auto& id_channel : channels_) {
      id_channel.second->unsetWaitingForDebugger();
    }

    if (interface_) {
      per_process::Debug(DebugCategory::INSPECTOR_CLIENT,
                         "Stopping waiting for frontend events\n");
      interface_->StopWaitingForFrontendEvent();
    }
  }

  int connectFrontend(std::unique_ptr<InspectorSessionDelegate> delegate,
                      bool prevent_shutdown) {
    int session_id = next_session_id_++;
    channels_[session_id] = std::make_unique<ChannelImpl>(env_,
                                                          client_,
                                                          getWorkerManager(),
                                                          std::move(delegate),
                                                          getThreadHandle(),
                                                          prevent_shutdown);
    if (waiting_for_frontend_) {
      channels_[session_id]->setWaitingForDebugger();
    }
    return session_id;
  }

  void disconnectFrontend(int session_id) {
    auto it = channels_.find(session_id);
    if (it == channels_.end())
      return;
    bool retaining_context = it->second->retainingContext();
    channels_.erase(it);
    if (retaining_context) {
      for (const auto& id_channel : channels_) {
        if (id_channel.second->retainingContext())
          return;
      }
      contextDestroyed(env_->context());
    }
    if (waiting_for_sessions_disconnect_ && !is_main_)
      waiting_for_sessions_disconnect_ = false;
  }

  void dispatchMessageFromFrontend(int session_id, const StringView& message) {
    channels_[session_id]->dispatchProtocolMessage(message);
  }

  Local<Context> ensureDefaultContextInGroup(int contextGroupId) override {
    return env_->context();
  }

  void installAdditionalCommandLineAPI(Local<Context> context,
                                       Local<Object> target) override {
    Local<Function> installer = env_->inspector_console_extension_installer();
    if (!installer.IsEmpty()) {
      Local<Value> argv[] = {target};
      // If there is an exception, proceed in JS land
      USE(installer->Call(context, target, arraysize(argv), argv));
    }
  }

  void ReportUncaughtException(Local<Value> error, Local<Message> message) {
    Isolate* isolate = env_->isolate();
    Local<Context> context = env_->context();

    int script_id = message->GetScriptOrigin().ScriptId();

    Local<v8::StackTrace> stack_trace = message->GetStackTrace();

    if (!stack_trace.IsEmpty() && stack_trace->GetFrameCount() > 0 &&
        script_id == stack_trace->GetFrame(isolate, 0)->GetScriptId()) {
      script_id = 0;
    }

    const uint8_t DETAILS[] = "Uncaught";

    client_->exceptionThrown(
        context,
        StringView(DETAILS, sizeof(DETAILS) - 1),
        error,
        ToProtocolString(isolate, message->Get())->string(),
        ToProtocolString(isolate, message->GetScriptResourceName())->string(),
        message->GetLineNumber(context).FromMaybe(0),
        message->GetStartColumn(context).FromMaybe(0),
        client_->createStackTrace(stack_trace),
        script_id);
  }

  void startRepeatingTimer(double interval_s,
                           TimerCallback callback,
                           void* data) override {
    auto result =
        timers_.emplace(std::piecewise_construct, std::make_tuple(data),
                        std::make_tuple(env_, [=]() { callback(data); }));
    CHECK(result.second);
    uint64_t interval = static_cast<uint64_t>(1000 * interval_s);
    result.first->second.Update(interval, interval);
  }

  void cancelTimer(void* data) override {
    timers_.erase(data);
  }

  // Async stack traces instrumentation.
  void AsyncTaskScheduled(const StringView& task_name, void* task,
                          bool recurring) {
    client_->asyncTaskScheduled(task_name, task, recurring);
  }

  void AsyncTaskCanceled(void* task) {
    client_->asyncTaskCanceled(task);
  }

  void AsyncTaskStarted(void* task) {
    client_->asyncTaskStarted(task);
  }

  void AsyncTaskFinished(void* task) {
    client_->asyncTaskFinished(task);
  }

  void AllAsyncTasksCanceled() {
    client_->allAsyncTasksCanceled();
  }

  void schedulePauseOnNextStatement(const std::string& reason) {
    for (const auto& id_channel : channels_) {
      id_channel.second->schedulePauseOnNextStatement(reason);
    }
  }

  bool hasConnectedSessions() {
    for (const auto& id_channel : channels_) {
      // Other sessions are "invisible" more most purposes
      if (id_channel.second->preventShutdown())
        return true;
    }
    return false;
  }

  bool notifyWaitingForDisconnect() {
    bool retaining_context = false;
    for (const auto& id_channel : channels_) {
      if (id_channel.second->notifyWaitingForDisconnect())
        retaining_context = true;
    }
    return retaining_context;
  }

  void emitNotification(const StringView& event, const StringView& params) {
    for (const auto& id_channel : channels_) {
      id_channel.second->emitNotificationFromBackend(event, params);
    }
  }

  std::shared_ptr<MainThreadHandle> getThreadHandle() {
    if (!interface_) {
      interface_ = std::make_shared<MainThreadInterface>(
          env_->inspector_agent());
    }
    return interface_->GetHandle();
  }

  std::shared_ptr<WorkerManager> getWorkerManager() {
    if (!is_main_) {
      return nullptr;
    }
    if (worker_manager_ == nullptr) {
      worker_manager_ =
          std::make_shared<WorkerManager>(getThreadHandle());
    }
    return worker_manager_;
  }

  bool IsActive() {
    return !channels_.empty();
  }

 private:
  bool shouldRunMessageLoop() {
    if (waiting_for_frontend_)
      return true;
    if (waiting_for_sessions_disconnect_ || waiting_for_resume_) {
      return hasConnectedSessions();
    }
    return false;
  }

  void runMessageLoop() {
    if (running_nested_loop_)
      return;

    running_nested_loop_ = true;

    per_process::Debug(DebugCategory::INSPECTOR_CLIENT,
                       "Entering nested loop\n");

    while (shouldRunMessageLoop()) {
      if (interface_) interface_->WaitForFrontendEvent();
      env_->RunAndClearInterrupts();
    }
    running_nested_loop_ = false;

    per_process::Debug(DebugCategory::INSPECTOR_CLIENT, "Exited nested loop\n");
  }

  double currentTimeMS() override {
    return env_->isolate_data()->platform()->CurrentClockTimeMillis();
  }

  std::unique_ptr<StringBuffer> resourceNameToUrl(
      const StringView& resource_name_view) override {
    std::string resource_name =
        protocol::StringUtil::StringViewToUtf8(resource_name_view);
    if (!IsFilePath(resource_name))
      return nullptr;

    std::string url = node::url::FromFilePath(resource_name);
    return Utf8ToStringView(url);
  }

  node::Environment* env_;
  bool is_main_;
  bool running_nested_loop_ = false;
  std::unique_ptr<V8Inspector> client_;
  // Note: ~ChannelImpl may access timers_ so timers_ has to come first.
  std::unordered_map<void*, TimerWrapHandle> timers_;
  std::unordered_map<int, std::unique_ptr<ChannelImpl>> channels_;
  int next_session_id_ = 1;
  bool waiting_for_resume_ = false;
  bool waiting_for_frontend_ = false;
  bool waiting_for_sessions_disconnect_ = false;
  // Allows accessing Inspector from non-main threads
  std::shared_ptr<MainThreadInterface> interface_;
  std::shared_ptr<WorkerManager> worker_manager_;
};

Agent::Agent(Environment* env)
    : parent_env_(env),
      debug_options_(env->options()->debug_options()),
      host_port_(env->inspector_host_port()) {}

Agent::~Agent() = default;

bool Agent::Start(const std::string& path,
                  const DebugOptions& options,
                  std::shared_ptr<ExclusiveAccess<HostPort>> host_port,
                  bool is_main) {
  path_ = path;
  debug_options_ = options;
  CHECK_NOT_NULL(host_port);
  host_port_ = host_port;

  client_ = std::make_shared<NodeInspectorClient>(parent_env_, is_main);
  if (parent_env_->owns_inspector()) {
    Mutex::ScopedLock lock(start_io_thread_async_mutex);
    CHECK_EQ(start_io_thread_async_initialized.exchange(true), false);
    CHECK_EQ(0, uv_async_init(parent_env_->event_loop(),
                              &start_io_thread_async,
                              StartIoThreadAsyncCallback));
    uv_unref(reinterpret_cast<uv_handle_t*>(&start_io_thread_async));
    start_io_thread_async.data = this;
    if (parent_env_->should_start_debug_signal_handler()) {
      // Ignore failure, SIGUSR1 won't work, but that should not block node
      // start.
      StartDebugSignalHandler();
    }

    parent_env_->AddCleanupHook([](void* data) {
      Environment* env = static_cast<Environment*>(data);

      {
        Mutex::ScopedLock lock(start_io_thread_async_mutex);
        start_io_thread_async.data = nullptr;
      }

      // This is global, will never get freed
      env->CloseHandle(&start_io_thread_async, [](uv_async_t*) {
        CHECK(start_io_thread_async_initialized.exchange(false));
      });
    }, parent_env_);
  }

  AtExit(parent_env_, [](void* env) {
    Agent* agent = static_cast<Environment*>(env)->inspector_agent();
    if (agent->IsActive()) {
      agent->WaitForDisconnect();
    }
  }, parent_env_);

  if (!parent_handle_ &&
      (!options.inspector_enabled || !options.allow_attaching_debugger ||
       !StartIoThread())) {
    return false;
  }
  return true;
}

bool Agent::StartIoThread() {
  if (io_ != nullptr)
    return true;

  THROW_IF_INSUFFICIENT_PERMISSIONS(parent_env_,
                                    permission::PermissionScope::kInspector,
                                    "StartIoThread",
                                    false);
  if (!parent_env_->should_create_inspector() && !client_) {
    ThrowUninitializedInspectorError(parent_env_);
    return false;
  }

  CHECK_NOT_NULL(client_);

  io_ = InspectorIo::Start(client_->getThreadHandle(),
                           path_,
                           host_port_,
                           debug_options_.inspect_publish_uid);
  if (io_ == nullptr) {
    return false;
  }
  NotifyClusterWorkersDebugEnabled(parent_env_);
  return true;
}

void Agent::Stop() {
  io_.reset();
}

std::unique_ptr<InspectorSession> Agent::Connect(
    std::unique_ptr<InspectorSessionDelegate> delegate,
    bool prevent_shutdown) {
  THROW_IF_INSUFFICIENT_PERMISSIONS(parent_env_,
                                    permission::PermissionScope::kInspector,
                                    "Connect",
                                    std::unique_ptr<InspectorSession>{});
  if (!parent_env_->should_create_inspector() && !client_) {
    ThrowUninitializedInspectorError(parent_env_);
    return std::unique_ptr<InspectorSession>{};
  }

  CHECK_NOT_NULL(client_);

  int session_id = client_->connectFrontend(std::move(delegate),
                                            prevent_shutdown);
  return std::unique_ptr<InspectorSession>(
      new SameThreadInspectorSession(session_id, client_));
}

std::unique_ptr<InspectorSession> Agent::ConnectToMainThread(
    std::unique_ptr<InspectorSessionDelegate> delegate,
    bool prevent_shutdown) {
  THROW_IF_INSUFFICIENT_PERMISSIONS(parent_env_,
                                    permission::PermissionScope::kInspector,
                                    "ConnectToMainThread",
                                    std::unique_ptr<InspectorSession>{});
  if (!parent_env_->should_create_inspector() && !client_) {
    ThrowUninitializedInspectorError(parent_env_);
    return std::unique_ptr<InspectorSession>{};
  }

  CHECK_NOT_NULL(parent_handle_);
  CHECK_NOT_NULL(client_);
  auto thread_safe_delegate =
      client_->getThreadHandle()->MakeDelegateThreadSafe(std::move(delegate));
  return parent_handle_->Connect(std::move(thread_safe_delegate),
                                 prevent_shutdown);
}

void Agent::EmitProtocolEvent(const StringView& event,
                              const StringView& params) {
  if (!env()->options()->experimental_network_inspection) return;
  client_->emitNotification(event, params);
}

void Agent::SetupNetworkTracking(Local<Function> enable_function,
                                 Local<Function> disable_function) {
  parent_env_->set_inspector_enable_network_tracking(enable_function);
  parent_env_->set_inspector_disable_network_tracking(disable_function);
  if (pending_enable_network_tracking) {
    pending_enable_network_tracking = false;
    EnableNetworkTracking();
  } else if (pending_disable_network_tracking) {
    pending_disable_network_tracking = false;
    DisableNetworkTracking();
  }
}

void Agent::EnableNetworkTracking() {
  if (network_tracking_enabled_) {
    return;
  }
  HandleScope scope(parent_env_->isolate());
  Local<Function> enable = parent_env_->inspector_enable_network_tracking();
  if (enable.IsEmpty()) {
    pending_enable_network_tracking = true;
  } else {
    ToggleNetworkTracking(parent_env_->isolate(), enable);
    network_tracking_enabled_ = true;
  }
}

void Agent::DisableNetworkTracking() {
  if (!network_tracking_enabled_) {
    return;
  }
  HandleScope scope(parent_env_->isolate());
  Local<Function> disable = parent_env_->inspector_disable_network_tracking();
  if (disable.IsEmpty()) {
    pending_disable_network_tracking = true;
  } else if (!client_->hasConnectedSessions()) {
    ToggleNetworkTracking(parent_env_->isolate(), disable);
    network_tracking_enabled_ = false;
  }
}

void Agent::ToggleNetworkTracking(Isolate* isolate, Local<Function> fn) {
  if (!parent_env_->can_call_into_js()) return;
  auto context = parent_env_->context();
  HandleScope scope(isolate);
  CHECK(!fn.IsEmpty());
  v8::TryCatch try_catch(isolate);
  USE(fn->Call(context, Undefined(isolate), 0, nullptr));
  if (try_catch.HasCaught() && !try_catch.HasTerminated()) {
    PrintCaughtException(isolate, context, try_catch);
    UNREACHABLE("Cannot toggle network tracking, please report this.");
  }
}

void Agent::WaitForDisconnect() {
  THROW_IF_INSUFFICIENT_PERMISSIONS(parent_env_,
                                    permission::PermissionScope::kInspector,
                                    "WaitForDisconnect");
  if (!parent_env_->should_create_inspector() && !client_) {
    ThrowUninitializedInspectorError(parent_env_);
    return;
  }

  CHECK_NOT_NULL(client_);
  bool is_worker = parent_handle_ != nullptr;
  parent_handle_.reset();
  if (client_->hasConnectedSessions() && !is_worker) {
    fprintf(stderr, "Waiting for the debugger to disconnect...\n");
    fflush(stderr);
  }
  if (!client_->notifyWaitingForDisconnect()) {
    client_->contextDestroyed(parent_env_->context());
  } else if (is_worker) {
    client_->waitForSessionsDisconnect();
  }
  if (io_ != nullptr) {
    io_->StopAcceptingNewConnections();
    client_->waitForSessionsDisconnect();
  }
}

void Agent::ReportUncaughtException(Local<Value> error,
                                    Local<Message> message) {
  if (!IsListening())
    return;
  client_->ReportUncaughtException(error, message);
  WaitForDisconnect();
}

void Agent::PauseOnNextJavascriptStatement(const std::string& reason) {
  client_->schedulePauseOnNextStatement(reason);
}

void Agent::RegisterAsyncHook(Isolate* isolate,
                              Local<Function> enable_function,
                              Local<Function> disable_function) {
  parent_env_->set_inspector_enable_async_hooks(enable_function);
  parent_env_->set_inspector_disable_async_hooks(disable_function);
  if (pending_enable_async_hook_) {
    CHECK(!pending_disable_async_hook_);
    pending_enable_async_hook_ = false;
    EnableAsyncHook();
  } else if (pending_disable_async_hook_) {
    CHECK(!pending_enable_async_hook_);
    pending_disable_async_hook_ = false;
    DisableAsyncHook();
  }
}

void Agent::EnableAsyncHook() {
  HandleScope scope(parent_env_->isolate());
  Local<Function> enable = parent_env_->inspector_enable_async_hooks();
  if (!enable.IsEmpty()) {
    ToggleAsyncHook(parent_env_->isolate(), enable);
  } else if (pending_disable_async_hook_) {
    CHECK(!pending_enable_async_hook_);
    pending_disable_async_hook_ = false;
  } else {
    pending_enable_async_hook_ = true;
  }
}

void Agent::DisableAsyncHook() {
  HandleScope scope(parent_env_->isolate());
  Local<Function> disable = parent_env_->inspector_disable_async_hooks();
  if (!disable.IsEmpty()) {
    ToggleAsyncHook(parent_env_->isolate(), disable);
  } else if (pending_enable_async_hook_) {
    CHECK(!pending_disable_async_hook_);
    pending_enable_async_hook_ = false;
  } else {
    pending_disable_async_hook_ = true;
  }
}

void Agent::ToggleAsyncHook(Isolate* isolate, Local<Function> fn) {
  // Guard against running this during cleanup -- no async events will be
  // emitted anyway at that point anymore, and calling into JS is not possible.
  // This should probably not be something we're attempting in the first place,
  // Refs: https://github.com/nodejs/node/pull/34362#discussion_r456006039
  if (!parent_env_->can_call_into_js()) return;
  CHECK(parent_env_->has_run_bootstrapping_code());
  HandleScope handle_scope(isolate);
  CHECK(!fn.IsEmpty());
  auto context = parent_env_->context();
  v8::TryCatch try_catch(isolate);
  USE(fn->Call(context, Undefined(isolate), 0, nullptr));
  if (try_catch.HasCaught() && !try_catch.HasTerminated()) {
    PrintCaughtException(isolate, context, try_catch);
    UNREACHABLE("Cannot toggle Inspector's AsyncHook, please report this.");
  }
}

void Agent::AsyncTaskScheduled(const StringView& task_name, void* task,
                               bool recurring) {
  client_->AsyncTaskScheduled(task_name, task, recurring);
}

void Agent::AsyncTaskCanceled(void* task) {
  client_->AsyncTaskCanceled(task);
}

void Agent::AsyncTaskStarted(void* task) {
  client_->AsyncTaskStarted(task);
}

void Agent::AsyncTaskFinished(void* task) {
  client_->AsyncTaskFinished(task);
}

void Agent::AllAsyncTasksCanceled() {
  client_->AllAsyncTasksCanceled();
}

void Agent::RequestIoThreadStart() {
  // We need to attempt to interrupt V8 flow (in case Node is running
  // continuous JS code) and to wake up libuv thread (in case Node is waiting
  // for IO events)
  if (!options().allow_attaching_debugger) {
    return;
  }
  CHECK(start_io_thread_async_initialized);
  uv_async_send(&start_io_thread_async);
  parent_env_->RequestInterrupt([this](Environment*) {
    StartIoThread();
  });

  CHECK(start_io_thread_async_initialized);
  uv_async_send(&start_io_thread_async);
}

void Agent::ContextCreated(Local<Context> context, const ContextInfo& info) {
  if (client_ == nullptr)  // This happens for a main context
    return;
  client_->contextCreated(context, info);
}

bool Agent::IsActive() {
  if (client_ == nullptr)
    return false;
  return io_ != nullptr || client_->IsActive();
}

void Agent::SetParentHandle(
    std::unique_ptr<ParentInspectorHandle> parent_handle) {
  parent_handle_ = std::move(parent_handle);
}

std::unique_ptr<ParentInspectorHandle> Agent::GetParentHandle(
    uint64_t thread_id, const std::string& url, const std::string& name) {
  THROW_IF_INSUFFICIENT_PERMISSIONS(parent_env_,
                                    permission::PermissionScope::kInspector,
                                    "GetParentHandle",
                                    std::unique_ptr<ParentInspectorHandle>{});
  if (!parent_env_->should_create_inspector() && !client_) {
    ThrowUninitializedInspectorError(parent_env_);
    return std::unique_ptr<ParentInspectorHandle>{};
  }

  CHECK_NOT_NULL(client_);
  if (!parent_handle_) {
    return client_->getWorkerManager()->NewParentHandle(thread_id, url, name);
  } else {
    return parent_handle_->NewParentInspectorHandle(thread_id, url, name);
  }
}

void Agent::WaitForConnect() {
  THROW_IF_INSUFFICIENT_PERMISSIONS(
      parent_env_, permission::PermissionScope::kInspector, "WaitForConnect");
  if (!parent_env_->should_create_inspector() && !client_) {
    ThrowUninitializedInspectorError(parent_env_);
    return;
  }

  CHECK_NOT_NULL(client_);
  client_->waitForFrontend();
}

bool Agent::WaitForConnectByOptions() {
  if (client_ == nullptr) {
    return false;
  }

  bool wait_for_connect = debug_options_.wait_for_connect();
  bool should_break_first_line = debug_options_.should_break_first_line();
  if (parent_handle_) {
    should_break_first_line = parent_handle_->WaitForConnect();
    parent_handle_->WorkerStarted(client_->getThreadHandle(),
                                  should_break_first_line);
  }

  if (wait_for_connect || should_break_first_line) {
    // Patch the debug options to implement waitForDebuggerOnStart for
    // the NodeWorker.enable method.
    if (should_break_first_line) {
      CHECK(!parent_env_->has_serialized_options());
      debug_options_.EnableBreakFirstLine();
      parent_env_->options()->get_debug_options()->EnableBreakFirstLine();
    }
    client_->waitForFrontend();
    return true;
  }
  return false;
}

void Agent::StopIfWaitingForConnect() {
  if (client_ == nullptr) {
    return;
  }
  client_->StopIfWaitingForFrontendEvent();
}

std::shared_ptr<WorkerManager> Agent::GetWorkerManager() {
  THROW_IF_INSUFFICIENT_PERMISSIONS(parent_env_,
                                    permission::PermissionScope::kInspector,
                                    "GetWorkerManager",
                                    std::unique_ptr<WorkerManager>{});
  if (!parent_env_->should_create_inspector() && !client_) {
    ThrowUninitializedInspectorError(parent_env_);
    return std::unique_ptr<WorkerManager>{};
  }

  CHECK_NOT_NULL(client_);
  return client_->getWorkerManager();
}

std::string Agent::GetWsUrl() const {
  if (io_ == nullptr)
    return "";
  return io_->GetWsUrl();
}

SameThreadInspectorSession::~SameThreadInspectorSession() {
  auto client = client_.lock();
  if (client)
    client->disconnectFrontend(session_id_);
}

void SameThreadInspectorSession::Dispatch(
    const v8_inspector::StringView& message) {
  auto client = client_.lock();
  if (client)
    client->dispatchMessageFromFrontend(session_id_, message);
}

}  // namespace inspector
}  // namespace node
