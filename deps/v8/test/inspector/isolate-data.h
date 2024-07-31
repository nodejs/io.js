// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TEST_INSPECTOR_PROTOCOL_ISOLATE_DATA_H_
#define V8_TEST_INSPECTOR_PROTOCOL_ISOLATE_DATA_H_

#include <map>
#include <memory>
#include <set>

#include "include/v8-array-buffer.h"
#include "include/v8-inspector.h"
#include "include/v8-local-handle.h"
#include "include/v8-locker.h"
#include "include/v8-script.h"
#include "src/base/optional.h"

namespace v8 {

class Context;
class Isolate;
class ObjectTemplate;
class StartupData;

namespace internal {

class FrontendChannelImpl;
class TaskRunner;

enum WithInspector : bool { kWithInspector = true, kNoInspector = false };

class InspectorIsolateData : public v8_inspector::V8InspectorClient {
 public:
  class SetupGlobalTask {
   public:
    virtual ~SetupGlobalTask() = default;
    virtual void Run(v8::Isolate* isolate,
                     v8::Local<v8::ObjectTemplate> global) = 0;
  };
  using SetupGlobalTasks = std::vector<std::unique_ptr<SetupGlobalTask>>;

  InspectorIsolateData(const InspectorIsolateData&) = delete;
  InspectorIsolateData& operator=(const InspectorIsolateData&) = delete;
  InspectorIsolateData(TaskRunner* task_runner,
                       SetupGlobalTasks setup_global_tasks,
                       v8::StartupData* startup_data,
                       WithInspector with_inspector);
  static InspectorIsolateData* FromContext(v8::Local<v8::Context> context);

  ~InspectorIsolateData() override;

  v8::Isolate* isolate() const { return isolate_.get(); }
  TaskRunner* task_runner() const { return task_runner_; }

  // Setting things up.
  int CreateContextGroup();
  V8_NODISCARD bool CreateContext(int context_group_id,
                                  v8_inspector::StringView name);
  void ResetContextGroup(int context_group_id);
  v8::Local<v8::Context> GetDefaultContext(int context_group_id);
  int GetContextGroupId(v8::Local<v8::Context> context);
  void RegisterModule(v8::Local<v8::Context> context,
                      std::vector<uint16_t> name,
                      v8::ScriptCompiler::Source* source);

  // Working with V8Inspector api.
  base::Optional<int> ConnectSession(
      int context_group_id, const v8_inspector::StringView& state,
      std::unique_ptr<FrontendChannelImpl> channel, bool is_fully_trusted);
  std::vector<uint8_t> DisconnectSession(int session_id,
                                         TaskRunner* context_task_runner);
  void SendMessage(int session_id, const v8_inspector::StringView& message);
  void BreakProgram(int context_group_id,
                    const v8_inspector::StringView& reason,
                    const v8_inspector::StringView& details);
  void Stop(int session_id);
  void SchedulePauseOnNextStatement(int context_group_id,
                                    const v8_inspector::StringView& reason,
                                    const v8_inspector::StringView& details);
  void CancelPauseOnNextStatement(int context_group_id);
  void AsyncTaskScheduled(const v8_inspector::StringView& name, void* task,
                          bool recurring);
  void AsyncTaskStarted(void* task);
  void AsyncTaskFinished(void* task);

  v8_inspector::V8StackTraceId StoreCurrentStackTrace(
      const v8_inspector::StringView& description);
  void ExternalAsyncTaskStarted(const v8_inspector::V8StackTraceId& parent);
  void ExternalAsyncTaskFinished(const v8_inspector::V8StackTraceId& parent);

  void AddInspectedObject(int session_id, v8::Local<v8::Value> object);

  // Test utilities.
  void SetCurrentTimeMS(double time);
  void SetMemoryInfo(v8::Local<v8::Value> memory_info);
  void SetLogConsoleApiMessageCalls(bool log);
  void SetLogMaxAsyncCallStackDepthChanged(bool log);
  void SetAdditionalConsoleApi(v8_inspector::StringView api_script);
  void SetMaxAsyncTaskStacksForTest(int limit);
  void DumpAsyncTaskStacksStateForTest();
  void FireContextCreated(v8::Local<v8::Context> context, int context_group_id,
                          v8_inspector::StringView name);
  void FireContextDestroyed(v8::Local<v8::Context> context);
  void FreeContext(v8::Local<v8::Context> context);
  void SetResourceNamePrefix(v8::Local<v8::String> prefix);
  bool AssociateExceptionData(v8::Local<v8::Value> exception,
                              v8::Local<v8::Name> key,
                              v8::Local<v8::Value> value);
  void WaitForDebugger(int context_group_id);

 private:
  static v8::MaybeLocal<v8::Module> ModuleResolveCallback(
      v8::Local<v8::Context> context, v8::Local<v8::String> specifier,
      v8::Local<v8::FixedArray> import_attributes,
      v8::Local<v8::Module> referrer);
  static void MessageHandler(v8::Local<v8::Message> message,
                             v8::Local<v8::Value> exception);
  static void PromiseRejectHandler(v8::PromiseRejectMessage data);
  static int HandleMessage(v8::Local<v8::Message> message,
                           v8::Local<v8::Value> exception);
  std::vector<int> GetSessionIds(int context_group_id);

  // V8InspectorClient implementation.
  v8::Local<v8::Context> ensureDefaultContextInGroup(
      int context_group_id) override;
  double currentTimeMS() override;
  v8::MaybeLocal<v8::Value> memoryInfo(v8::Isolate* isolate,
                                       v8::Local<v8::Context>) override;
  void runMessageLoopOnPause(int context_group_id) override;
  void runIfWaitingForDebugger(int context_group_id) override;
  void quitMessageLoopOnPause() override;
  void installAdditionalCommandLineAPI(v8::Local<v8::Context>,
                                       v8::Local<v8::Object>) override;
  void consoleAPIMessage(int contextGroupId,
                         v8::Isolate::MessageErrorLevel level,
                         const v8_inspector::StringView& message,
                         const v8_inspector::StringView& url,
                         unsigned lineNumber, unsigned columnNumber,
                         v8_inspector::V8StackTrace*) override;
  bool isInspectableHeapObject(v8::Local<v8::Object>) override;
  void maxAsyncCallStackDepthChanged(int depth) override;
  std::unique_ptr<v8_inspector::StringBuffer> resourceNameToUrl(
      const v8_inspector::StringView& resourceName) override;
  int64_t generateUniqueId() override;

  // The isolate gets deleted by its {Dispose} method, not by the default
  // deleter. Therefore we have to define a custom deleter for the unique_ptr to
  // call {Dispose}. We have to use the unique_ptr so that the isolate get
  // disposed in the right order, relative to other member variables.
  struct IsolateDeleter {
    void operator()(v8::Isolate* isolate) const {
      // Exit the isolate after it was entered by ~InspectorIsolateData.
      isolate->Exit();
      isolate->Dispose();
    }
  };

  TaskRunner* task_runner_;
  SetupGlobalTasks setup_global_tasks_;
  std::unique_ptr<v8::ArrayBuffer::Allocator> array_buffer_allocator_;
  std::unique_ptr<v8::Isolate, IsolateDeleter> isolate_;
  // The locker_ field has to come after isolate_ because the locker has to
  // outlive the isolate.
  base::Optional<v8::Locker> locker_;
  std::unique_ptr<v8_inspector::V8Inspector> inspector_;
  int last_context_group_id_ = 0;
  std::map<int, std::vector<v8::Global<v8::Context>>> contexts_;
  std::map<std::vector<uint16_t>, v8::Global<v8::Module>> modules_;
  int last_session_id_ = 0;
  std::map<int, std::unique_ptr<v8_inspector::V8InspectorSession>> sessions_;
  std::map<v8_inspector::V8InspectorSession*, int> context_group_by_session_;
  std::set<int> session_ids_for_cleanup_;
  v8::Global<v8::Value> memory_info_;
  bool current_time_set_ = false;
  double current_time_ = 0.0;
  bool log_console_api_message_calls_ = false;
  bool log_max_async_call_stack_depth_changed_ = false;
  bool waiting_for_debugger_ = false;
  v8::Global<v8::Private> not_inspectable_private_;
  v8::Global<v8::String> resource_name_prefix_;
  v8::Global<v8::String> additional_console_api_;
};

// Stores all the channels.
//
// `InspectorIsolateData` is per isolate and a channel connects
// the backend Isolate with the frontend Isolate. The backend registers and
// sets up the isolate, but the frontend needs it to send responses and
// notifications. This is why we use a separate "class" (just a static wrapper
// around std::map).
class ChannelHolder {
 public:
  static void AddChannel(int session_id,
                         std::unique_ptr<FrontendChannelImpl> channel);
  static FrontendChannelImpl* GetChannel(int session_id);
  static void RemoveChannel(int session_id);

 private:
  static std::map<int, std::unique_ptr<FrontendChannelImpl>> channels_;
};

}  // namespace internal
}  // namespace v8

#endif  //  V8_TEST_INSPECTOR_PROTOCOL_ISOLATE_DATA_H_
