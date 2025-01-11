#ifndef SRC_NODE_V8_PLATFORM_INL_H_
#define SRC_NODE_V8_PLATFORM_INL_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include <memory>
#include <string_view>

#include "env-inl.h"
#include "node.h"
#include "node_metadata.h"
#include "node_platform.h"
#include "node_options.h"
#include "tracing/node_trace_writer.h"
#include "tracing/trace_event.h"
#include "tracing/traced_value.h"
#include "util.h"

namespace node {

// Ensures that __metadata trace events are only emitted
// when tracing is enabled.
class NodeTraceStateObserver
    : public v8::TracingController::TraceStateObserver {
 public:
  inline void OnTraceEnabled() override {
    std::string title = GetProcessTitle("");
    if (!title.empty()) {
      // Only emit the metadata event if the title can be retrieved
      // successfully. Ignore it otherwise.
      TRACE_EVENT_METADATA1(
          "__metadata", "process_name", "name", TRACE_STR_COPY(title.c_str()));
    }
    TRACE_EVENT_METADATA1("__metadata",
                          "version",
                          "node",
                          per_process::metadata.versions.node.c_str());
    TRACE_EVENT_METADATA1(
        "__metadata", "thread_name", "name", "JavaScriptMainThread");

    auto trace_process = tracing::TracedValue::Create();
    trace_process->BeginDictionary("versions");

#define V(key)                                                                 \
  trace_process->SetString(#key, per_process::metadata.versions.key.c_str());

    NODE_VERSIONS_KEYS(V)
#undef V

    trace_process->EndDictionary();

    trace_process->SetString("arch", per_process::metadata.arch.c_str());
    trace_process->SetString("platform",
                             per_process::metadata.platform.c_str());

    trace_process->BeginDictionary("release");
    trace_process->SetString("name",
                             per_process::metadata.release.name.c_str());
#if NODE_VERSION_IS_LTS
    trace_process->SetString("lts", per_process::metadata.release.lts.c_str());
#endif
    trace_process->EndDictionary();
    TRACE_EVENT_METADATA1(
        "__metadata", "node", "process", std::move(trace_process));

    // This only runs the first time tracing is enabled
    controller_->RemoveTraceStateObserver(this);
  }

  inline void OnTraceDisabled() override {
    // Do nothing here. This should never be called because the
    // observer removes itself when OnTraceEnabled() is called.
    UNREACHABLE();
  }

  explicit NodeTraceStateObserver(v8::TracingController* controller)
      : controller_(controller) {}
  ~NodeTraceStateObserver() override = default;

 private:
  v8::TracingController* controller_;
};

struct V8Platform {
  bool initialized_ = false;

#if NODE_USE_V8_PLATFORM
  inline void Initialize(int thread_pool_size) {
    CHECK(!initialized_);
    initialized_ = true;
    tracing_agent_ = std::make_unique<tracing::Agent>();
    node::tracing::TraceEventHelper::SetAgent(tracing_agent_.get());
    node::tracing::TracingController* controller =
        tracing_agent_->GetTracingController();
    trace_state_observer_ =
        std::make_unique<NodeTraceStateObserver>(controller);
    controller->AddTraceStateObserver(trace_state_observer_.get());
    tracing_file_writer_ = tracing_agent_->DefaultHandle();
    // Only start the tracing agent if we enabled any tracing categories.
    if (!per_process::cli_options->trace_event_categories.empty()) {
      StartTracingAgent();
    }
    // Tracing must be initialized before platform threads are created.
    platform_ = new NodePlatform(thread_pool_size, controller);
    v8::V8::InitializePlatform(platform_);
  }
  // Make sure V8Platform don not call into Libuv threadpool,
  // see DefaultProcessExitHandlerInternal in environment.cc
  inline void Dispose() {
    if (!initialized_)
      return;
    initialized_ = false;
    node::tracing::TraceEventHelper::SetAgent(nullptr);
    StopTracingAgent();
    platform_->Shutdown();
    delete platform_;
    platform_ = nullptr;
    // Destroy tracing after the platform (and platform threads) have been
    // stopped.
    tracing_agent_.reset(nullptr);
    // The observer remove itself in OnTraceEnabled
    trace_state_observer_.reset(nullptr);
  }

  inline void DrainVMTasks(v8::Isolate* isolate) {
    platform_->DrainTasks(isolate);
  }

  inline void StartTracingAgent() {
    constexpr auto convert_to_set =
        [](auto& categories) -> std::set<std::string> {
      std::set<std::string> out;
      for (const auto& s : categories) {
        out.emplace(std::string(s.data(), s.size()));
      }
      return out;
    };
    // Attach a new NodeTraceWriter only if this function hasn't been called
    // before.
    if (tracing_file_writer_.IsDefaultHandle()) {
      using std::operator""sv;
      auto categories = std::views::split(
          per_process::cli_options->trace_event_categories, ","sv);

      tracing_file_writer_ = tracing_agent_->AddClient(
          convert_to_set(categories),
          std::unique_ptr<tracing::AsyncTraceWriter>(
              new tracing::NodeTraceWriter(
                  per_process::cli_options->trace_event_file_pattern)),
          tracing::Agent::kUseDefaultCategories);
    }
  }

  inline void StopTracingAgent() { tracing_file_writer_.reset(); }

  inline tracing::AgentWriterHandle* GetTracingAgentWriter() {
    return &tracing_file_writer_;
  }

  inline NodePlatform* Platform() { return platform_; }

  std::unique_ptr<NodeTraceStateObserver> trace_state_observer_;
  std::unique_ptr<tracing::Agent> tracing_agent_;
  tracing::AgentWriterHandle tracing_file_writer_;
  NodePlatform* platform_;
#else   // !NODE_USE_V8_PLATFORM
  inline void Initialize(int thread_pool_size) {}
  inline void Dispose() {}
  inline void DrainVMTasks(v8::Isolate* isolate) {}
  inline void StartTracingAgent() {
    if (!per_process::cli_options->trace_event_categories.empty()) {
      fprintf(stderr,
              "Node compiled with NODE_USE_V8_PLATFORM=0, "
              "so event tracing is not available.\n");
    }
  }
  inline void StopTracingAgent() {}

  inline tracing::AgentWriterHandle* GetTracingAgentWriter() { return nullptr; }

  inline NodePlatform* Platform() { return nullptr; }
#endif  // !NODE_USE_V8_PLATFORM
};

namespace per_process {
extern struct V8Platform v8_platform;
}

inline void StartTracingAgent() {
  return per_process::v8_platform.StartTracingAgent();
}

inline tracing::AgentWriterHandle* GetTracingAgentWriter() {
  return per_process::v8_platform.GetTracingAgentWriter();
}

inline void DisposePlatform() {
  per_process::v8_platform.Dispose();
}

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_NODE_V8_PLATFORM_INL_H_
