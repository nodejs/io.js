// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/inspector/isolate-data.h"

#include <optional>

#include "include/v8-context.h"
#include "include/v8-exception.h"
#include "include/v8-microtask-queue.h"
#include "include/v8-template.h"
#include "src/execution/isolate.h"
#include "src/heap/heap.h"
#include "src/init/v8.h"
#include "src/inspector/test-interface.h"
#include "test/inspector/frontend-channel.h"
#include "test/inspector/task-runner.h"
#include "test/inspector/utils.h"

namespace v8 {
namespace internal {

namespace {

const int kIsolateDataIndex = 2;
const int kContextGroupIdIndex = 3;

void Print(v8::Isolate* isolate, const v8_inspector::StringView& string) {
  v8::Local<v8::String> v8_string = ToV8String(isolate, string);
  v8::String::Utf8Value utf8_string(isolate, v8_string);
  fwrite(*utf8_string, sizeof(**utf8_string), utf8_string.length(), stdout);
}

class Inspectable : public v8_inspector::V8InspectorSession::Inspectable {
 public:
  Inspectable(v8::Isolate* isolate, v8::Local<v8::Value> object)
      : object_(isolate, object) {}
  ~Inspectable() override = default;
  v8::Local<v8::Value> get(v8::Local<v8::Context> context) override {
    return object_.Get(context->GetIsolate());
  }

 private:
  v8::Global<v8::Value> object_;
};

}  //  namespace

InspectorIsolateData::InspectorIsolateData(
    TaskRunner* task_runner,
    InspectorIsolateData::SetupGlobalTasks setup_global_tasks,
    v8::StartupData* startup_data, WithInspector with_inspector)
    : task_runner_(task_runner),
      setup_global_tasks_(std::move(setup_global_tasks)) {
  v8::Isolate::CreateParams params;
  array_buffer_allocator_.reset(
      v8::ArrayBuffer::Allocator::NewDefaultAllocator());
  params.array_buffer_allocator = array_buffer_allocator_.get();
  params.snapshot_blob = startup_data;
  isolate_.reset(v8::Isolate::New(params));
  v8::Isolate::Scope isolate_scope(isolate_.get());
  isolate_->SetMicrotasksPolicy(v8::MicrotasksPolicy::kScoped);
  if (with_inspector) {
    isolate_->AddMessageListener(&InspectorIsolateData::MessageHandler);
    isolate_->SetPromiseRejectCallback(
        &InspectorIsolateData::PromiseRejectHandler);
    inspector_ = v8_inspector::V8Inspector::create(isolate_.get(), this);
  }
  v8::HandleScope handle_scope(isolate_.get());
  not_inspectable_private_.Reset(
      isolate_.get(),
      v8::Private::ForApi(
          isolate_.get(),
          v8::String::NewFromUtf8Literal(isolate_.get(), "notInspectable")));
}

InspectorIsolateData* InspectorIsolateData::FromContext(
    v8::Local<v8::Context> context) {
  return static_cast<InspectorIsolateData*>(
      context->GetAlignedPointerFromEmbedderData(kIsolateDataIndex));
}

InspectorIsolateData::~InspectorIsolateData() {
  // Enter the isolate before destructing this InspectorIsolateData, so that
  // destructors that run before the Isolate's destructor still see it as
  // entered. Use a v8::Locker, in case the thread destroying the isolate is
  // not the last one that entered it.
  locker_.emplace(isolate());
  isolate()->Enter();

  // Sessions need to be deleted before channels can be cleaned up, and channels
  // must be deleted before the isolate gets cleaned up. This means we first
  // clean up all the sessions and immedatly after all the channels used by
  // those sessions.
  for (const auto& pair : sessions_) {
    session_ids_for_cleanup_.insert(pair.first);
  }

  context_group_by_session_.clear();
  sessions_.clear();

  for (int session_id : session_ids_for_cleanup_) {
    ChannelHolder::RemoveChannel(session_id);
  }
}

int InspectorIsolateData::CreateContextGroup() {
  int context_group_id = ++last_context_group_id_;
  if (!CreateContext(context_group_id, v8_inspector::StringView())) {
    DCHECK(isolate_->IsExecutionTerminating());
    return -1;
  }
  return context_group_id;
}

bool InspectorIsolateData::CreateContext(int context_group_id,
                                         v8_inspector::StringView name) {
  v8::HandleScope handle_scope(isolate_.get());
  v8::Local<v8::ObjectTemplate> global_template =
      v8::ObjectTemplate::New(isolate_.get());
  for (auto it = setup_global_tasks_.begin(); it != setup_global_tasks_.end();
       ++it) {
    (*it)->Run(isolate_.get(), global_template);
  }
  v8::Local<v8::Context> context =
      v8::Context::New(isolate_.get(), nullptr, global_template);
  if (context.IsEmpty()) return false;
  context->SetAlignedPointerInEmbedderData(kIsolateDataIndex, this);
  // Should be 2-byte aligned.
  context->SetAlignedPointerInEmbedderData(
      kContextGroupIdIndex, reinterpret_cast<void*>(context_group_id * 2));
  contexts_[context_group_id].emplace_back(isolate_.get(), context);
  if (inspector_) FireContextCreated(context, context_group_id, name);
  return true;
}

v8::Local<v8::Context> InspectorIsolateData::GetDefaultContext(
    int context_group_id) {
  return contexts_[context_group_id].begin()->Get(isolate_.get());
}

void InspectorIsolateData::ResetContextGroup(int context_group_id) {
  v8::SealHandleScope seal_handle_scope(isolate());
  inspector_->resetContextGroup(context_group_id);
}

int InspectorIsolateData::GetContextGroupId(v8::Local<v8::Context> context) {
  return static_cast<int>(
      reinterpret_cast<intptr_t>(
          context->GetAlignedPointerFromEmbedderData(kContextGroupIdIndex)) /
      2);
}

void InspectorIsolateData::RegisterModule(v8::Local<v8::Context> context,
                                          std::vector<uint16_t> name,
                                          v8::ScriptCompiler::Source* source) {
  v8::Local<v8::Module> module;
  if (!v8::ScriptCompiler::CompileModule(isolate(), source).ToLocal(&module))
    return;
  if (!module
           ->InstantiateModule(context,
                               &InspectorIsolateData::ModuleResolveCallback)
           .FromMaybe(false)) {
    return;
  }
  v8::Local<v8::Value> result;
  if (!module->Evaluate(context).ToLocal(&result)) return;
  modules_[name] = v8::Global<v8::Module>(isolate_.get(), module);
}

// static
v8::MaybeLocal<v8::Module> InspectorIsolateData::ModuleResolveCallback(
    v8::Local<v8::Context> context, v8::Local<v8::String> specifier,
    v8::Local<v8::FixedArray> import_attributes,
    v8::Local<v8::Module> referrer) {
  // TODO(v8:11189) Consider JSON modules support in the InspectorClient
  InspectorIsolateData* data = InspectorIsolateData::FromContext(context);
  std::string str = *v8::String::Utf8Value(data->isolate(), specifier);
  v8::MaybeLocal<v8::Module> maybe_module =
      data->modules_[ToVector(data->isolate(), specifier)].Get(data->isolate());
  if (maybe_module.IsEmpty()) {
    data->isolate()->ThrowError(v8::String::Concat(
        data->isolate(),
        ToV8String(data->isolate(), "Failed to resolve module: "), specifier));
  }
  return maybe_module;
}

std::optional<int> InspectorIsolateData::ConnectSession(
    int context_group_id, const v8_inspector::StringView& state,
    std::unique_ptr<FrontendChannelImpl> channel, bool is_fully_trusted) {
  if (contexts_.find(context_group_id) == contexts_.end()) return std::nullopt;

  v8::SealHandleScope seal_handle_scope(isolate());
  int session_id = ++last_session_id_;
  // It's important that we register the channel before the `connect` as the
  // inspector will already send notifications.
  auto* c = channel.get();
  ChannelHolder::AddChannel(session_id, std::move(channel));
  sessions_[session_id] = inspector_->connect(
      context_group_id, c, state,
      is_fully_trusted ? v8_inspector::V8Inspector::kFullyTrusted
                       : v8_inspector::V8Inspector::kUntrusted,
      waiting_for_debugger_
          ? v8_inspector::V8Inspector::kWaitingForDebugger
          : v8_inspector::V8Inspector::kNotWaitingForDebugger);
  context_group_by_session_[sessions_[session_id].get()] = context_group_id;
  return session_id;
}

namespace {

class RemoveChannelTask : public TaskRunner::Task {
 public:
  explicit RemoveChannelTask(int session_id) : session_id_(session_id) {}
  ~RemoveChannelTask() override = default;
  bool is_priority_task() final { return false; }

 private:
  void Run(InspectorIsolateData* data) override {
    ChannelHolder::RemoveChannel(session_id_);
  }
  int session_id_;
};

}  // namespace

std::vector<uint8_t> InspectorIsolateData::DisconnectSession(
    int session_id, TaskRunner* context_task_runner) {
  v8::SealHandleScope seal_handle_scope(isolate());
  auto it = sessions_.find(session_id);
  CHECK(it != sessions_.end());
  context_group_by_session_.erase(it->second.get());
  std::vector<uint8_t> result = it->second->state();
  sessions_.erase(it);

  // The InspectorSession destructor does cleanup work like disabling agents.
  // This could send some more notifications. We'll delay removing the channel
  // so notification tasks have time to get sent.
  // Note: This only works for tasks scheduled immediately by the desctructor.
  //       Any task scheduled in turn by one of the "cleanup tasks" will run
  //       AFTER the channel was removed.
  context_task_runner->Append(std::make_unique<RemoveChannelTask>(session_id));

  // In case we shutdown the test runner before the above task can run, we
  // let the desctructor clean up the channel.
  session_ids_for_cleanup_.insert(session_id);
  return result;
}

void InspectorIsolateData::SendMessage(
    int session_id, const v8_inspector::StringView& message) {
  v8::SealHandleScope seal_handle_scope(isolate());
  auto it = sessions_.find(session_id);
  if (it != sessions_.end()) it->second->dispatchProtocolMessage(message);
}

void InspectorIsolateData::BreakProgram(
    int context_group_id, const v8_inspector::StringView& reason,
    const v8_inspector::StringView& details) {
  v8::SealHandleScope seal_handle_scope(isolate());
  for (int session_id : GetSessionIds(context_group_id)) {
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) it->second->breakProgram(reason, details);
  }
}

void InspectorIsolateData::Stop(int session_id) {
  v8::SealHandleScope seal_handle_scope(isolate());
  auto it = sessions_.find(session_id);
  if (it != sessions_.end()) it->second->stop();
}

void InspectorIsolateData::SchedulePauseOnNextStatement(
    int context_group_id, const v8_inspector::StringView& reason,
    const v8_inspector::StringView& details) {
  v8::SealHandleScope seal_handle_scope(isolate());
  for (int session_id : GetSessionIds(context_group_id)) {
    auto it = sessions_.find(session_id);
    if (it != sessions_.end())
      it->second->schedulePauseOnNextStatement(reason, details);
  }
}

void InspectorIsolateData::CancelPauseOnNextStatement(int context_group_id) {
  v8::SealHandleScope seal_handle_scope(isolate());
  for (int session_id : GetSessionIds(context_group_id)) {
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) it->second->cancelPauseOnNextStatement();
  }
}

void InspectorIsolateData::AsyncTaskScheduled(
    const v8_inspector::StringView& name, void* task, bool recurring) {
  v8::SealHandleScope seal_handle_scope(isolate());
  inspector_->asyncTaskScheduled(name, task, recurring);
}

void InspectorIsolateData::AsyncTaskStarted(void* task) {
  v8::SealHandleScope seal_handle_scope(isolate());
  inspector_->asyncTaskStarted(task);
}

void InspectorIsolateData::AsyncTaskFinished(void* task) {
  v8::SealHandleScope seal_handle_scope(isolate());
  inspector_->asyncTaskFinished(task);
}

v8_inspector::V8StackTraceId InspectorIsolateData::StoreCurrentStackTrace(
    const v8_inspector::StringView& description) {
  v8::SealHandleScope seal_handle_scope(isolate());
  return inspector_->storeCurrentStackTrace(description);
}

void InspectorIsolateData::ExternalAsyncTaskStarted(
    const v8_inspector::V8StackTraceId& parent) {
  v8::SealHandleScope seal_handle_scope(isolate());
  inspector_->externalAsyncTaskStarted(parent);
}

void InspectorIsolateData::ExternalAsyncTaskFinished(
    const v8_inspector::V8StackTraceId& parent) {
  v8::SealHandleScope seal_handle_scope(isolate());
  inspector_->externalAsyncTaskFinished(parent);
}

void InspectorIsolateData::AddInspectedObject(int session_id,
                                              v8::Local<v8::Value> object) {
  v8::SealHandleScope seal_handle_scope(isolate());
  auto it = sessions_.find(session_id);
  if (it == sessions_.end()) return;
  std::unique_ptr<Inspectable> inspectable(
      new Inspectable(isolate_.get(), object));
  it->second->addInspectedObject(std::move(inspectable));
}

void InspectorIsolateData::SetMaxAsyncTaskStacksForTest(int limit) {
  v8::SealHandleScope seal_handle_scope(isolate());
  v8_inspector::SetMaxAsyncTaskStacksForTest(inspector_.get(), limit);
}

void InspectorIsolateData::DumpAsyncTaskStacksStateForTest() {
  v8::SealHandleScope seal_handle_scope(isolate());
  v8_inspector::DumpAsyncTaskStacksStateForTest(inspector_.get());
}

// static
int InspectorIsolateData::HandleMessage(v8::Local<v8::Message> message,
                                        v8::Local<v8::Value> exception) {
  v8::Isolate* isolate = message->GetIsolate();
  v8::Local<v8::Context> context = isolate->GetEnteredOrMicrotaskContext();
  if (context.IsEmpty()) return 0;
  v8_inspector::V8Inspector* inspector =
      InspectorIsolateData::FromContext(context)->inspector_.get();

  v8::Local<v8::StackTrace> stack = message->GetStackTrace();
  int script_id = message->GetScriptOrigin().ScriptId();
  if (!stack.IsEmpty() && stack->GetFrameCount() > 0) {
    int top_script_id = stack->GetFrame(isolate, 0)->GetScriptId();
    if (top_script_id == script_id) script_id = 0;
  }
  int line_number = message->GetLineNumber(context).FromMaybe(0);
  int column_number = 0;
  if (message->GetStartColumn(context).IsJust())
    column_number = message->GetStartColumn(context).FromJust() + 1;

  v8_inspector::StringView detailed_message;
  std::vector<uint16_t> message_text_string = ToVector(isolate, message->Get());
  v8_inspector::StringView message_text(message_text_string.data(),
                                        message_text_string.size());
  std::vector<uint16_t> url_string;
  if (message->GetScriptOrigin().ResourceName()->IsString()) {
    url_string = ToVector(
        isolate, message->GetScriptOrigin().ResourceName().As<v8::String>());
  }
  v8_inspector::StringView url(url_string.data(), url_string.size());

  v8::SealHandleScope seal_handle_scope(isolate);
  return inspector->exceptionThrown(
      context, message_text, exception, detailed_message, url, line_number,
      column_number, inspector->createStackTrace(stack), script_id);
}

// static
void InspectorIsolateData::MessageHandler(v8::Local<v8::Message> message,
                                          v8::Local<v8::Value> exception) {
  HandleMessage(message, exception);
}

// static
void InspectorIsolateData::PromiseRejectHandler(v8::PromiseRejectMessage data) {
  v8::Isolate* isolate = data.GetPromise()->GetIsolate();
  v8::Local<v8::Context> context = isolate->GetEnteredOrMicrotaskContext();
  if (context.IsEmpty()) return;
  v8::Local<v8::Promise> promise = data.GetPromise();
  v8::Local<v8::Private> id_private = v8::Private::ForApi(
      isolate, v8::String::NewFromUtf8Literal(isolate, "id"));

  if (data.GetEvent() == v8::kPromiseHandlerAddedAfterReject) {
    v8::Local<v8::Value> id;
    if (!promise->GetPrivate(context, id_private).ToLocal(&id)) return;
    if (!id->IsInt32()) return;
    v8_inspector::V8Inspector* inspector =
        InspectorIsolateData::FromContext(context)->inspector_.get();
    v8::SealHandleScope seal_handle_scope(isolate);
    const char* reason_str = "Handler added to rejected promise";
    inspector->exceptionRevoked(
        context, id.As<v8::Int32>()->Value(),
        v8_inspector::StringView(reinterpret_cast<const uint8_t*>(reason_str),
                                 strlen(reason_str)));
    return;
  } else if (data.GetEvent() == v8::kPromiseRejectAfterResolved ||
             data.GetEvent() == v8::kPromiseResolveAfterResolved) {
    // Ignore reject/resolve after resolved, like the blink handler.
    return;
  }

  v8::Local<v8::Value> exception = data.GetValue();
  int exception_id = HandleMessage(
      v8::Exception::CreateMessage(isolate, exception), exception);
  if (exception_id) {
    if (promise
            ->SetPrivate(isolate->GetCurrentContext(), id_private,
                         v8::Int32::New(isolate, exception_id))
            .IsNothing()) {
      // Handling the |message| above calls back into JavaScript (by reporting
      // it via CDP) in case of `inspector-test`, and can lead to terminating
      // execution on the |isolate|, in which case the API call above will
      // return immediately.
      DCHECK(isolate->IsExecutionTerminating());
    }
  }
}

void InspectorIsolateData::FireContextCreated(v8::Local<v8::Context> context,
                                              int context_group_id,
                                              v8_inspector::StringView name) {
  v8_inspector::V8ContextInfo info(context, context_group_id, name);
  info.hasMemoryOnConsole = true;
  v8::SealHandleScope seal_handle_scope(isolate());
  inspector_->contextCreated(info);
}

void InspectorIsolateData::FireContextDestroyed(
    v8::Local<v8::Context> context) {
  v8::SealHandleScope seal_handle_scope(isolate());
  inspector_->contextDestroyed(context);
}

void InspectorIsolateData::FreeContext(v8::Local<v8::Context> context) {
  int context_group_id = GetContextGroupId(context);
  auto it = contexts_.find(context_group_id);
  if (it == contexts_.end()) return;
  contexts_.erase(it);
}

std::vector<int> InspectorIsolateData::GetSessionIds(int context_group_id) {
  std::vector<int> result;
  for (auto& it : sessions_) {
    if (context_group_by_session_[it.second.get()] == context_group_id)
      result.push_back(it.first);
  }
  return result;
}

bool InspectorIsolateData::isInspectableHeapObject(
    v8::Local<v8::Object> object) {
  v8::Local<v8::Context> context = isolate()->GetCurrentContext();
  v8::MicrotasksScope microtasks_scope(
      context, v8::MicrotasksScope::kDoNotRunMicrotasks);
  return !object->HasPrivate(context, not_inspectable_private_.Get(isolate()))
              .FromMaybe(false);
}

v8::Local<v8::Context> InspectorIsolateData::ensureDefaultContextInGroup(
    int context_group_id) {
  return GetDefaultContext(context_group_id);
}

void InspectorIsolateData::SetCurrentTimeMS(double time) {
  current_time_ = time;
  current_time_set_ = true;
}

double InspectorIsolateData::currentTimeMS() {
  if (current_time_set_) return current_time_;
  return V8::GetCurrentPlatform()->CurrentClockTimeMillisecondsHighResolution();
}

void InspectorIsolateData::SetMemoryInfo(v8::Local<v8::Value> memory_info) {
  memory_info_.Reset(isolate_.get(), memory_info);
}

void InspectorIsolateData::SetLogConsoleApiMessageCalls(bool log) {
  log_console_api_message_calls_ = log;
}

void InspectorIsolateData::SetLogMaxAsyncCallStackDepthChanged(bool log) {
  log_max_async_call_stack_depth_changed_ = log;
}

void InspectorIsolateData::SetAdditionalConsoleApi(
    v8_inspector::StringView api_script) {
  v8::HandleScope handle_scope(isolate());
  additional_console_api_.Reset(isolate(), ToV8String(isolate(), api_script));
}

v8::MaybeLocal<v8::Value> InspectorIsolateData::memoryInfo(
    v8::Isolate* isolate, v8::Local<v8::Context>) {
  if (memory_info_.IsEmpty()) return v8::MaybeLocal<v8::Value>();
  return memory_info_.Get(isolate);
}

void InspectorIsolateData::runMessageLoopOnPause(int) {
  v8::SealHandleScope seal_handle_scope(isolate());
  // Pumping the message loop below may trigger the execution of a stackless
  // GC. We need to override the embedder stack state, to force scanning the
  // stack, if this happens.
  i::Heap* heap =
      reinterpret_cast<i::Isolate*>(task_runner_->isolate())->heap();
  i::EmbedderStackStateScope scope(
      heap, i::EmbedderStackStateOrigin::kExplicitInvocation,
      StackState::kMayContainHeapPointers);
  task_runner_->RunMessageLoop(true);
}

void InspectorIsolateData::runIfWaitingForDebugger(int) {
  quitMessageLoopOnPause();
}

void InspectorIsolateData::quitMessageLoopOnPause() {
  v8::SealHandleScope seal_handle_scope(isolate());
  task_runner_->QuitMessageLoop();
}

void InspectorIsolateData::installAdditionalCommandLineAPI(
    v8::Local<v8::Context> context, v8::Local<v8::Object> object) {
  if (additional_console_api_.IsEmpty()) return;
  CHECK(context->GetIsolate() == isolate());
  v8::HandleScope handle_scope(isolate());
  v8::Context::Scope context_scope(context);
  v8::ScriptOrigin origin(
      v8::String::NewFromUtf8Literal(isolate(), "internal-console-api"));
  v8::ScriptCompiler::Source scriptSource(
      additional_console_api_.Get(isolate()), origin);
  v8::MaybeLocal<v8::Script> script =
      v8::ScriptCompiler::Compile(context, &scriptSource);
  CHECK(!script.ToLocalChecked()->Run(context).IsEmpty());
}

void InspectorIsolateData::consoleAPIMessage(
    int contextGroupId, v8::Isolate::MessageErrorLevel level,
    const v8_inspector::StringView& message,
    const v8_inspector::StringView& url, unsigned lineNumber,
    unsigned columnNumber, v8_inspector::V8StackTrace* stack) {
  if (!log_console_api_message_calls_) return;
  switch (level) {
    case v8::Isolate::kMessageLog:
      fprintf(stdout, "log: ");
      break;
    case v8::Isolate::kMessageDebug:
      fprintf(stdout, "debug: ");
      break;
    case v8::Isolate::kMessageInfo:
      fprintf(stdout, "info: ");
      break;
    case v8::Isolate::kMessageError:
      fprintf(stdout, "error: ");
      break;
    case v8::Isolate::kMessageWarning:
      fprintf(stdout, "warning: ");
      break;
    case v8::Isolate::kMessageAll:
      break;
  }
  Print(isolate_.get(), message);
  fprintf(stdout, " (");
  Print(isolate_.get(), url);
  fprintf(stdout, ":%d:%d)", lineNumber, columnNumber);
  Print(isolate_.get(), stack->toString()->string());
  fprintf(stdout, "\n");
}

void InspectorIsolateData::maxAsyncCallStackDepthChanged(int depth) {
  if (!log_max_async_call_stack_depth_changed_) return;
  fprintf(stdout, "maxAsyncCallStackDepthChanged: %d\n", depth);
}

void InspectorIsolateData::SetResourceNamePrefix(v8::Local<v8::String> prefix) {
  resource_name_prefix_.Reset(isolate(), prefix);
}

bool InspectorIsolateData::AssociateExceptionData(
    v8::Local<v8::Value> exception, v8::Local<v8::Name> key,
    v8::Local<v8::Value> value) {
  return inspector_->associateExceptionData(
      this->isolate()->GetCurrentContext(), exception, key, value);
}

void InspectorIsolateData::WaitForDebugger(int context_group_id) {
  DCHECK(!waiting_for_debugger_);
  waiting_for_debugger_ = true;
  runMessageLoopOnPause(context_group_id);
  waiting_for_debugger_ = false;
}

namespace {
class StringBufferImpl : public v8_inspector::StringBuffer {
 public:
  StringBufferImpl(v8::Isolate* isolate, v8::Local<v8::String> string)
      : data_(ToVector(isolate, string)) {}

  v8_inspector::StringView string() const override {
    return v8_inspector::StringView(data_.data(), data_.size());
  }

 private:
  std::vector<uint16_t> data_;
};
}  // anonymous namespace

std::unique_ptr<v8_inspector::StringBuffer>
InspectorIsolateData::resourceNameToUrl(
    const v8_inspector::StringView& resourceName) {
  if (resource_name_prefix_.IsEmpty()) return nullptr;
  v8::HandleScope handle_scope(isolate());
  v8::Local<v8::String> name = ToV8String(isolate(), resourceName);
  v8::Local<v8::String> prefix = resource_name_prefix_.Get(isolate());
  v8::Local<v8::String> url = v8::String::Concat(isolate(), prefix, name);
  return std::make_unique<StringBufferImpl>(isolate(), url);
}

int64_t InspectorIsolateData::generateUniqueId() {
  static int64_t last_unique_id = 0L;
  // Keep it not too random for tests.
  return ++last_unique_id;
}

// static
void ChannelHolder::AddChannel(int session_id,
                               std::unique_ptr<FrontendChannelImpl> channel) {
  CHECK_NE(channel.get(), nullptr);
  channel->set_session_id(session_id);
  channels_[session_id] = std::move(channel);
}

// static
FrontendChannelImpl* ChannelHolder::GetChannel(int session_id) {
  auto it = channels_.find(session_id);
  return it != channels_.end() ? it->second.get() : nullptr;
}

// static
void ChannelHolder::RemoveChannel(int session_id) {
  channels_.erase(session_id);
}

// static
std::map<int, std::unique_ptr<FrontendChannelImpl>> ChannelHolder::channels_;

}  // namespace internal
}  // namespace v8
