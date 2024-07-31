#include "tracing_agent.h"
#include "main_thread_interface.h"
#include "node_internals.h"
#include "node_v8_platform-inl.h"
#include "v8.h"

#include <set>
#include <sstream>

namespace node {
namespace inspector {
namespace protocol {

namespace {
using v8::platform::tracing::TraceWriter;

class DeletableFrontendWrapper : public Deletable {
 public:
  explicit DeletableFrontendWrapper(
      std::weak_ptr<NodeTracing::Frontend> frontend)
      : frontend_(frontend) {}

  // This should only be called from the main thread, meaning frontend should
  // not be destroyed concurrently.
  NodeTracing::Frontend* get() { return frontend_.lock().get(); }

 private:
  std::weak_ptr<NodeTracing::Frontend> frontend_;
};

class CreateFrontendWrapperRequest : public Request {
 public:
  CreateFrontendWrapperRequest(int object_id,
                               std::weak_ptr<NodeTracing::Frontend> frontend)
      : object_id_(object_id) {
    frontend_wrapper_ = std::make_unique<DeletableFrontendWrapper>(frontend);
  }

  void Call(MainThreadInterface* thread) override {
    thread->AddObject(object_id_, std::move(frontend_wrapper_));
  }

 private:
  int object_id_;
  std::unique_ptr<DeletableFrontendWrapper> frontend_wrapper_;
};

class DestroyFrontendWrapperRequest : public Request {
 public:
  explicit DestroyFrontendWrapperRequest(int object_id)
      : object_id_(object_id) {}

  void Call(MainThreadInterface* thread) override {
    thread->RemoveObject(object_id_);
  }

 private:
  int object_id_;
};

class SendMessageRequest : public Request {
 public:
  explicit SendMessageRequest(int object_id, const std::string& message)
      : object_id_(object_id), message_(message) {}

  void Call(MainThreadInterface* thread) override {
    DeletableFrontendWrapper* frontend_wrapper =
        static_cast<DeletableFrontendWrapper*>(
            thread->GetObjectIfExists(object_id_));
    if (frontend_wrapper == nullptr) return;
    auto frontend = frontend_wrapper->get();
    if (frontend != nullptr) {
      frontend->sendRawJSONNotification(message_);
    }
  }

 private:
  int object_id_;
  std::string message_;
};

class InspectorTraceWriter : public node::tracing::AsyncTraceWriter {
 public:
  explicit InspectorTraceWriter(int frontend_object_id,
                                std::shared_ptr<MainThreadHandle> main_thread)
      : frontend_object_id_(frontend_object_id), main_thread_(main_thread) {}

  void AppendTraceEvent(
      v8::platform::tracing::TraceObject* trace_event) override {
    if (!json_writer_)
      json_writer_.reset(TraceWriter::CreateJSONTraceWriter(stream_, "value"));
    json_writer_->AppendTraceEvent(trace_event);
  }

  void Flush(bool) override {
    if (!json_writer_)
      return;
    json_writer_.reset();
    std::ostringstream result(
        "{\"method\":\"NodeTracing.dataCollected\",\"params\":",
        std::ostringstream::ate);
    result << stream_.str();
    result << "}";
    main_thread_->Post(std::make_unique<SendMessageRequest>(frontend_object_id_,
                                                            result.str()));
    stream_.str("");
  }

 private:
  std::unique_ptr<TraceWriter> json_writer_;
  std::ostringstream stream_;
  int frontend_object_id_;
  std::shared_ptr<MainThreadHandle> main_thread_;
};
}  // namespace

TracingAgent::TracingAgent(Environment* env,
                           std::shared_ptr<MainThreadHandle> main_thread)
    : env_(env), main_thread_(main_thread) {}

TracingAgent::~TracingAgent() {
  trace_writer_.reset();
  main_thread_->Post(
      std::make_unique<DestroyFrontendWrapperRequest>(frontend_object_id_));
}

void TracingAgent::Wire(UberDispatcher* dispatcher) {
  // Note that frontend is still owned by TracingAgent
  frontend_ = std::make_shared<NodeTracing::Frontend>(dispatcher->channel());
  frontend_object_id_ = main_thread_->newObjectId();
  main_thread_->Post(std::make_unique<CreateFrontendWrapperRequest>(
      frontend_object_id_, frontend_));
  NodeTracing::Dispatcher::wire(dispatcher, this);
}

DispatchResponse TracingAgent::start(
    std::unique_ptr<protocol::NodeTracing::TraceConfig> traceConfig) {
  if (!trace_writer_.empty()) {
    return DispatchResponse::Error(
        "Call NodeTracing::end to stop tracing before updating the config");
  }
  if (!env_->owns_process_state()) {
    return DispatchResponse::Error(
        "Tracing properties can only be changed through main thread sessions");
  }

  std::set<std::string> categories_set;
  protocol::Array<std::string>* categories =
      traceConfig->getIncludedCategories();
  for (size_t i = 0; i < categories->size(); i++)
    categories_set.insert((*categories)[i]);

  if (categories_set.empty())
    return DispatchResponse::Error("At least one category should be enabled");

  tracing::AgentWriterHandle* writer = GetTracingAgentWriter();
  if (writer != nullptr) {
    trace_writer_ =
        writer->agent()->AddClient(categories_set,
                                   std::make_unique<InspectorTraceWriter>(
                                       frontend_object_id_, main_thread_),
                                   tracing::Agent::kIgnoreDefaultCategories);
  }
  return DispatchResponse::OK();
}

DispatchResponse TracingAgent::stop() {
  trace_writer_.reset();
  frontend_->tracingComplete();
  return DispatchResponse::OK();
}

DispatchResponse TracingAgent::getCategories(
    std::unique_ptr<protocol::Array<String>>* categories) {
  *categories = std::make_unique<Array<String>>();
  protocol::Array<String>* categories_list = categories->get();
  // In alphabetical order
  categories_list->emplace_back("node");
  categories_list->emplace_back("node.async_hooks");
  categories_list->emplace_back("node.bootstrap");
  categories_list->emplace_back("node.console");
  categories_list->emplace_back("node.dns.native");
  categories_list->emplace_back("node.environment");
  categories_list->emplace_back("node.fs.async");
  categories_list->emplace_back("node.fs.sync");
  categories_list->emplace_back("node.fs_dir.async");
  categories_list->emplace_back("node.fs_dir.sync");
  categories_list->emplace_back("node.http");
  categories_list->emplace_back("node.net.native");
  categories_list->emplace_back("node.perf");
  categories_list->emplace_back("node.perf.timerify");
  categories_list->emplace_back("node.perf.usertiming");
  categories_list->emplace_back("node.promises.rejections");
  categories_list->emplace_back("node.threadpoolwork.async");
  categories_list->emplace_back("node.threadpoolwork.sync");
  categories_list->emplace_back("node.vm.script");
  categories_list->emplace_back("v8");
  return DispatchResponse::OK();
}

}  // namespace protocol
}  // namespace inspector
}  // namespace node
