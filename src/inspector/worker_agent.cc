#include "worker_agent.h"

#include "main_thread_interface.h"
#include "worker_inspector.h"
#include "util-inl.h"

namespace node {
namespace inspector {
namespace protocol {

class NodeWorkers
    : public std::enable_shared_from_this<NodeWorkers> {
 public:
  explicit NodeWorkers(std::weak_ptr<NodeWorker::Frontend> frontend,
                      std::shared_ptr<MainThreadHandle> thread)
                      : frontend_(frontend), thread_(thread) {}
  void WorkerCreated(const std::string& title,
                     const std::string& url,
                     bool waiting,
                     std::shared_ptr<MainThreadHandle> target);
  void Receive(const std::string& id, const std::string& message);
  void Send(const std::string& id, const std::string& message);
  void Detached(const std::string& id);

 private:
  std::weak_ptr<NodeWorker::Frontend> frontend_;
  std::shared_ptr<MainThreadHandle> thread_;
  std::unordered_map<std::string, std::unique_ptr<InspectorSession>> sessions_;
  int next_target_id_ = 0;
};

namespace {
class AgentWorkerInspectorDelegate : public WorkerDelegate {
 public:
  explicit AgentWorkerInspectorDelegate(std::shared_ptr<NodeWorkers> workers)
                                        : workers_(workers) {}

  void WorkerCreated(const std::string& title,
                     const std::string& url,
                     bool waiting,
                     std::shared_ptr<MainThreadHandle> target) override {
    workers_->WorkerCreated(title, url, waiting, target);
  }

 private:
  std::shared_ptr<NodeWorkers> workers_;
};

class ParentInspectorSessionDelegate : public InspectorSessionDelegate {
 public:
  ParentInspectorSessionDelegate(const std::string& id,
                                 std::shared_ptr<NodeWorkers> workers)
                                 : id_(id), workers_(workers) {}

  ~ParentInspectorSessionDelegate() override {
    workers_->Detached(id_);
  }

  void SendMessageToFrontend(const v8_inspector::StringView& msg) override {
    std::string message = protocol::StringUtil::StringViewToUtf8(msg);
    workers_->Send(id_, message);
  }

 private:
  std::string id_;
  std::shared_ptr<NodeWorkers> workers_;
};

std::unique_ptr<NodeWorker::WorkerInfo> WorkerInfo(const std::string& id,
                                                   const std::string& title,
                                                   const std::string& url) {
  return NodeWorker::WorkerInfo::create()
      .setWorkerId(id)
      .setTitle(title)
      .setUrl(url)
      .setType("worker").build();
}
}  // namespace

WorkerAgent::WorkerAgent(std::weak_ptr<WorkerManager> manager)
                         : manager_(manager) {}


void WorkerAgent::Wire(UberDispatcher* dispatcher) {
  frontend_.reset(new NodeWorker::Frontend(dispatcher->channel()));
  NodeWorker::Dispatcher::wire(dispatcher, this);
  auto manager = manager_.lock();
  CHECK_NOT_NULL(manager);
  workers_ =
      std::make_shared<NodeWorkers>(frontend_, manager->MainThread());
}

DispatchResponse WorkerAgent::sendMessageToWorker(const String& message,
                                                  const String& sessionId) {
  workers_->Receive(sessionId, message);
  return DispatchResponse::Success();
}

DispatchResponse WorkerAgent::enable(bool waitForDebuggerOnStart) {
  auto manager = manager_.lock();
  if (!manager) {
    return DispatchResponse::Success();
  }
  if (!event_handle_) {
    std::unique_ptr<AgentWorkerInspectorDelegate> delegate(
            new AgentWorkerInspectorDelegate(workers_));
    event_handle_ = manager->SetAutoAttach(std::move(delegate));
  }
  event_handle_->SetWaitOnStart(waitForDebuggerOnStart);
  return DispatchResponse::Success();
}

DispatchResponse WorkerAgent::disable() {
  event_handle_.reset();
  return DispatchResponse::Success();
}

DispatchResponse WorkerAgent::detach(const String& sessionId) {
  workers_->Detached(sessionId);
  return DispatchResponse::Success();
}

void NodeWorkers::WorkerCreated(const std::string& title,
                                const std::string& url,
                                bool waiting,
                                std::shared_ptr<MainThreadHandle> target) {
  auto frontend = frontend_.lock();
  if (!frontend)
    return;
  std::string id = std::to_string(++next_target_id_);
  auto delegate = thread_->MakeDelegateThreadSafe(
      std::unique_ptr<InspectorSessionDelegate>(
          new ParentInspectorSessionDelegate(id, shared_from_this())));
  sessions_[id] = target->Connect(std::move(delegate), true);
  frontend->attachedToWorker(id, WorkerInfo(id, title, url), waiting);
}

void NodeWorkers::Send(const std::string& id, const std::string& message) {
  auto frontend = frontend_.lock();
  if (frontend)
    frontend->receivedMessageFromWorker(id, message);
}

void NodeWorkers::Receive(const std::string& id, const std::string& message) {
  auto it = sessions_.find(id);
  if (it != sessions_.end())
    it->second->Dispatch(Utf8ToStringView(message)->string());
}

void NodeWorkers::Detached(const std::string& id) {
  if (sessions_.erase(id) == 0)
    return;
  auto frontend = frontend_.lock();
  if (frontend) {
    frontend->detachedFromWorker(id);
  }
}
}  // namespace protocol
}  // namespace inspector
}  // namespace node
