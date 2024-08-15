#include "inspector_io.h"

#include "base_object-inl.h"
#include "crypto/crypto_util.h"
#include "debug_utils-inl.h"
#include "inspector/main_thread_interface.h"
#include "inspector/node_string.h"
#include "inspector_socket_server.h"
#include "ncrypto.h"
#include "node.h"
#include "node_internals.h"
#include "node_mutex.h"
#include "util-inl.h"
#include "v8-inspector.h"
#include "zlib.h"

#include <deque>
#include <cstring>
#include <vector>

namespace node {
namespace inspector {
namespace {
using v8_inspector::StringBuffer;
using v8_inspector::StringView;

// kKill closes connections and stops the server, kStop only stops the server
enum class TransportAction { kKill, kSendMessage, kStop };

std::string ScriptPath(uv_loop_t* loop, const std::string& script_name) {
  std::string script_path;

  if (!script_name.empty()) {
    uv_fs_t req;
    req.ptr = nullptr;
    if (0 == uv_fs_realpath(loop, &req, script_name.c_str(), nullptr)) {
      CHECK_NOT_NULL(req.ptr);
      script_path = std::string(static_cast<char*>(req.ptr));
    }
    uv_fs_req_cleanup(&req);
  }

  return script_path;
}

// UUID RFC: https://www.ietf.org/rfc/rfc4122.txt
// Used ver 4 - with numbers
std::string GenerateID() {
  uint16_t buffer[8];
  CHECK(ncrypto::CSPRNG(buffer, sizeof(buffer)));

  char uuid[256];
  snprintf(uuid, sizeof(uuid), "%04x%04x-%04x-%04x-%04x-%04x%04x%04x",
           buffer[0],  // time_low
           buffer[1],  // time_mid
           buffer[2],  // time_low
           (buffer[3] & 0x0fff) | 0x4000,  // time_hi_and_version
           (buffer[4] & 0x3fff) | 0x8000,  // clk_seq_hi clk_seq_low
           buffer[5],  // node
           buffer[6],
           buffer[7]);
  return uuid;
}

class RequestToServer {
 public:
  RequestToServer(TransportAction action,
                  int session_id,
                  std::unique_ptr<v8_inspector::StringBuffer> message)
                  : action_(action),
                    session_id_(session_id),
                    message_(std::move(message)) {}

  void Dispatch(InspectorSocketServer* server) const {
    switch (action_) {
      case TransportAction::kKill:
        server->TerminateConnections();
        [[fallthrough]];
      case TransportAction::kStop:
        server->Stop();
        break;
      case TransportAction::kSendMessage:
        server->Send(
            session_id_,
            protocol::StringUtil::StringViewToUtf8(message_->string()));
        break;
    }
  }

 private:
  TransportAction action_;
  int session_id_;
  std::unique_ptr<v8_inspector::StringBuffer> message_;
};

class RequestQueueData {
 public:
  using MessageQueue = std::deque<RequestToServer>;

  explicit RequestQueueData(uv_loop_t* loop)
                            : handle_(std::make_shared<RequestQueue>(this)) {
    int err = uv_async_init(loop, &async_, [](uv_async_t* async) {
      RequestQueueData* wrapper =
          node::ContainerOf(&RequestQueueData::async_, async);
      wrapper->DoDispatch();
    });
    CHECK_EQ(0, err);
  }

  static void CloseAndFree(RequestQueueData* queue);

  void Post(int session_id,
            TransportAction action,
            std::unique_ptr<StringBuffer> message) {
    Mutex::ScopedLock scoped_lock(state_lock_);
    bool notify = messages_.empty();
    messages_.emplace_back(action, session_id, std::move(message));
    if (notify) {
      CHECK_EQ(0, uv_async_send(&async_));
      incoming_message_cond_.Broadcast(scoped_lock);
    }
  }

  void Wait() {
    Mutex::ScopedLock scoped_lock(state_lock_);
    if (messages_.empty()) {
      incoming_message_cond_.Wait(scoped_lock);
    }
  }

  void SetServer(InspectorSocketServer* server) {
    server_ = server;
  }

  std::shared_ptr<RequestQueue> handle() {
    return handle_;
  }

 private:
  ~RequestQueueData() = default;

  MessageQueue GetMessages() {
    Mutex::ScopedLock scoped_lock(state_lock_);
    MessageQueue messages;
    messages_.swap(messages);
    return messages;
  }

  void DoDispatch() {
    if (server_ == nullptr)
      return;
    for (const auto& request : GetMessages()) {
      request.Dispatch(server_);
    }
  }

  std::shared_ptr<RequestQueue> handle_;
  uv_async_t async_;
  InspectorSocketServer* server_ = nullptr;
  MessageQueue messages_;
  Mutex state_lock_;  // Locked before mutating the queue.
  ConditionVariable incoming_message_cond_;
};
}  // namespace

class RequestQueue {
 public:
  explicit RequestQueue(RequestQueueData* data) : data_(data) {}

  void Reset() {
    Mutex::ScopedLock scoped_lock(lock_);
    data_ = nullptr;
  }

  void Post(int session_id,
            TransportAction action,
            std::unique_ptr<StringBuffer> message) {
    Mutex::ScopedLock scoped_lock(lock_);
    if (data_ != nullptr)
      data_->Post(session_id, action, std::move(message));
  }

  bool Expired() {
    Mutex::ScopedLock scoped_lock(lock_);
    return data_ == nullptr;
  }

 private:
  RequestQueueData* data_;
  Mutex lock_;
};

class IoSessionDelegate : public InspectorSessionDelegate {
 public:
  explicit IoSessionDelegate(std::shared_ptr<RequestQueue> queue, int id)
                             : request_queue_(queue), id_(id) { }
  void SendMessageToFrontend(const v8_inspector::StringView& message) override {
    request_queue_->Post(id_, TransportAction::kSendMessage,
                         StringBuffer::create(message));
  }

 private:
  std::shared_ptr<RequestQueue> request_queue_;
  int id_;
};

// Passed to InspectorSocketServer to handle WS inspector protocol events,
// mostly session start, message received, and session end.
class InspectorIoDelegate: public node::inspector::SocketServerDelegate {
 public:
  InspectorIoDelegate(std::shared_ptr<RequestQueueData> queue,
                      std::shared_ptr<MainThreadHandle> main_thread,
                      const std::string& target_id,
                      const std::string& script_path,
                      const std::string& script_name);
  ~InspectorIoDelegate() override = default;

  void StartSession(int session_id, const std::string& target_id) override;
  void MessageReceived(int session_id, const std::string& message) override;
  void EndSession(int session_id) override;

  std::vector<std::string> GetTargetIds() override;
  std::string GetTargetTitle(const std::string& id) override;
  std::string GetTargetUrl(const std::string& id) override;
  void AssignServer(InspectorSocketServer* server) override {
    request_queue_->SetServer(server);
  }

 private:
  std::shared_ptr<RequestQueueData> request_queue_;
  std::shared_ptr<MainThreadHandle> main_thread_;
  std::unordered_map<int, std::unique_ptr<InspectorSession>> sessions_;
  const std::string script_name_;
  const std::string script_path_;
  const std::string target_id_;
};

// static
std::unique_ptr<InspectorIo> InspectorIo::Start(
    std::shared_ptr<MainThreadHandle> main_thread,
    const std::string& path,
    std::shared_ptr<ExclusiveAccess<HostPort>> host_port,
    const InspectPublishUid& inspect_publish_uid) {
  auto io = std::unique_ptr<InspectorIo>(
      new InspectorIo(main_thread,
                      path,
                      host_port,
                      inspect_publish_uid));
  if (io->request_queue_->Expired()) {  // Thread is not running
    return nullptr;
  }
  return io;
}

InspectorIo::InspectorIo(std::shared_ptr<MainThreadHandle> main_thread,
                         const std::string& path,
                         std::shared_ptr<ExclusiveAccess<HostPort>> host_port,
                         const InspectPublishUid& inspect_publish_uid)
    : main_thread_(main_thread),
      host_port_(host_port),
      inspect_publish_uid_(inspect_publish_uid),
      thread_(),
      script_name_(path),
      id_(GenerateID()) {
  Mutex::ScopedLock scoped_lock(thread_start_lock_);
  CHECK_EQ(uv_thread_create(&thread_, InspectorIo::ThreadMain, this), 0);
  thread_start_condition_.Wait(scoped_lock);
}

InspectorIo::~InspectorIo() {
  request_queue_->Post(0, TransportAction::kKill, nullptr);
  int err = uv_thread_join(&thread_);
  CHECK_EQ(err, 0);
}

void InspectorIo::StopAcceptingNewConnections() {
  request_queue_->Post(0, TransportAction::kStop, nullptr);
}

// static
void InspectorIo::ThreadMain(void* io) {
  static_cast<InspectorIo*>(io)->ThreadMain();
}

void InspectorIo::ThreadMain() {
  uv_loop_t loop;
  loop.data = nullptr;
  int err = uv_loop_init(&loop);
  CHECK_EQ(err, 0);
  std::shared_ptr<RequestQueueData> queue(new RequestQueueData(&loop),
                                          RequestQueueData::CloseAndFree);
  std::string script_path = ScriptPath(&loop, script_name_);
  std::unique_ptr<InspectorIoDelegate> delegate(
      new InspectorIoDelegate(queue, main_thread_, id_,
                              script_path, script_name_));
  std::string host;
  int port;
  {
    ExclusiveAccess<HostPort>::Scoped host_port(host_port_);
    host = host_port->host();
    port = host_port->port();
  }
  InspectorSocketServer server(std::move(delegate),
                               &loop,
                               std::move(host),
                               port,
                               inspect_publish_uid_);
  request_queue_ = queue->handle();
  // Its lifetime is now that of the server delegate
  queue.reset();
  {
    Mutex::ScopedLock scoped_lock(thread_start_lock_);
    if (server.Start()) {
      ExclusiveAccess<HostPort>::Scoped host_port(host_port_);
      host_port->set_port(server.Port());
    }
    thread_start_condition_.Broadcast(scoped_lock);
  }
  uv_run(&loop, UV_RUN_DEFAULT);
  CheckedUvLoopClose(&loop);
}

std::string InspectorIo::GetWsUrl() const {
  ExclusiveAccess<HostPort>::Scoped host_port(host_port_);
  return FormatWsAddress(host_port->host(), host_port->port(), id_, true);
}

InspectorIoDelegate::InspectorIoDelegate(
    std::shared_ptr<RequestQueueData> queue,
    std::shared_ptr<MainThreadHandle> main_thread,
    const std::string& target_id,
    const std::string& script_path,
    const std::string& script_name)
    : request_queue_(queue), main_thread_(main_thread),
      script_name_(script_name), script_path_(script_path),
      target_id_(target_id) {}

void InspectorIoDelegate::StartSession(int session_id,
                                       const std::string& target_id) {
  auto session = main_thread_->Connect(
      std::unique_ptr<InspectorSessionDelegate>(
          new IoSessionDelegate(request_queue_->handle(), session_id)), true);
  if (session) {
    sessions_[session_id] = std::move(session);
    fprintf(stderr, "Debugger attached.\n");
  }
}

void InspectorIoDelegate::MessageReceived(int session_id,
                                          const std::string& message) {
  auto session = sessions_.find(session_id);
  if (session != sessions_.end())
    session->second->Dispatch(Utf8ToStringView(message)->string());
}

void InspectorIoDelegate::EndSession(int session_id) {
  sessions_.erase(session_id);
}

std::vector<std::string> InspectorIoDelegate::GetTargetIds() {
  return { target_id_ };
}

std::string InspectorIoDelegate::GetTargetTitle(const std::string& id) {
  return script_name_.empty() ? GetHumanReadableProcessName() : script_name_;
}

std::string InspectorIoDelegate::GetTargetUrl(const std::string& id) {
  return "file://" + script_path_;
}

// static
void RequestQueueData::CloseAndFree(RequestQueueData* queue) {
  queue->handle_->Reset();
  queue->handle_.reset();
  uv_close(reinterpret_cast<uv_handle_t*>(&queue->async_),
           [](uv_handle_t* handle) {
    uv_async_t* async = reinterpret_cast<uv_async_t*>(handle);
    RequestQueueData* wrapper =
        node::ContainerOf(&RequestQueueData::async_, async);
    delete wrapper;
  });
}
}  // namespace inspector
}  // namespace node
