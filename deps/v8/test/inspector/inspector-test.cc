// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <locale.h>

#include <optional>
#include <string>
#include <vector>

#include "include/libplatform/libplatform.h"
#include "include/v8-exception.h"
#include "include/v8-initialization.h"
#include "include/v8-local-handle.h"
#include "include/v8-snapshot.h"
#include "src/base/platform/platform.h"
#include "src/base/small-vector.h"
#include "src/flags/flags.h"
#include "src/utils/utils.h"
#include "test/inspector/frontend-channel.h"
#include "test/inspector/isolate-data.h"
#include "test/inspector/task-runner.h"
#include "test/inspector/tasks.h"
#include "test/inspector/utils.h"

#if !defined(V8_OS_WIN)
#include <unistd.h>
#endif  // !defined(V8_OS_WIN)

namespace v8 {
namespace internal {

extern void DisableEmbeddedBlobRefcounting();
extern void FreeCurrentEmbeddedBlob();

extern v8::StartupData CreateSnapshotDataBlobInternalForInspectorTest(
    v8::SnapshotCreator::FunctionCodeHandling function_code_handling,
    const char* embedded_source);

namespace {

base::SmallVector<TaskRunner*, 2> task_runners;

class UtilsExtension : public InspectorIsolateData::SetupGlobalTask {
 public:
  ~UtilsExtension() override = default;
  void Run(v8::Isolate* isolate,
           v8::Local<v8::ObjectTemplate> global) override {
    v8::Local<v8::ObjectTemplate> utils = v8::ObjectTemplate::New(isolate);
    utils->Set(isolate, "print",
               v8::FunctionTemplate::New(isolate, &UtilsExtension::Print));
    utils->Set(isolate, "quit",
               v8::FunctionTemplate::New(isolate, &UtilsExtension::Quit));
    utils->Set(isolate, "setlocale",
               v8::FunctionTemplate::New(isolate, &UtilsExtension::Setlocale));
    utils->Set(isolate, "read",
               v8::FunctionTemplate::New(isolate, &UtilsExtension::Read));
    utils->Set(isolate, "load",
               v8::FunctionTemplate::New(isolate, &UtilsExtension::Load));
    utils->Set(isolate, "compileAndRunWithOrigin",
               v8::FunctionTemplate::New(
                   isolate, &UtilsExtension::CompileAndRunWithOrigin));
    utils->Set(isolate, "setCurrentTimeMSForTest",
               v8::FunctionTemplate::New(
                   isolate, &UtilsExtension::SetCurrentTimeMSForTest));
    utils->Set(isolate, "setMemoryInfoForTest",
               v8::FunctionTemplate::New(
                   isolate, &UtilsExtension::SetMemoryInfoForTest));
    utils->Set(isolate, "schedulePauseOnNextStatement",
               v8::FunctionTemplate::New(
                   isolate, &UtilsExtension::SchedulePauseOnNextStatement));
    utils->Set(isolate, "cancelPauseOnNextStatement",
               v8::FunctionTemplate::New(
                   isolate, &UtilsExtension::CancelPauseOnNextStatement));
    utils->Set(isolate, "stop",
               v8::FunctionTemplate::New(isolate, &UtilsExtension::Stop));
    utils->Set(isolate, "setLogConsoleApiMessageCalls",
               v8::FunctionTemplate::New(
                   isolate, &UtilsExtension::SetLogConsoleApiMessageCalls));
    utils->Set(isolate, "setAdditionalConsoleApi",
               v8::FunctionTemplate::New(
                   isolate, &UtilsExtension::SetAdditionalConsoleApi));
    utils->Set(
        isolate, "setLogMaxAsyncCallStackDepthChanged",
        v8::FunctionTemplate::New(
            isolate, &UtilsExtension::SetLogMaxAsyncCallStackDepthChanged));
    utils->Set(isolate, "createContextGroup",
               v8::FunctionTemplate::New(isolate,
                                         &UtilsExtension::CreateContextGroup));
    utils->Set(
        isolate, "createContext",
        v8::FunctionTemplate::New(isolate, &UtilsExtension::CreateContext));
    utils->Set(
        isolate, "resetContextGroup",
        v8::FunctionTemplate::New(isolate, &UtilsExtension::ResetContextGroup));
    utils->Set(
        isolate, "connectSession",
        v8::FunctionTemplate::New(isolate, &UtilsExtension::ConnectSession));
    utils->Set(
        isolate, "disconnectSession",
        v8::FunctionTemplate::New(isolate, &UtilsExtension::DisconnectSession));
    utils->Set(isolate, "sendMessageToBackend",
               v8::FunctionTemplate::New(
                   isolate, &UtilsExtension::SendMessageToBackend));
    utils->Set(isolate, "interruptForMessages",
               v8::FunctionTemplate::New(
                   isolate, &UtilsExtension::InterruptForMessages));
    utils->Set(
        isolate, "waitForDebugger",
        v8::FunctionTemplate::New(isolate, &UtilsExtension::WaitForDebugger));
    global->Set(isolate, "utils", utils);
  }

  static void set_backend_task_runner(TaskRunner* runner) {
    backend_runner_ = runner;
  }

 private:
  static TaskRunner* backend_runner_;

  static void Print(const v8::FunctionCallbackInfo<v8::Value>& info) {
    for (int i = 0; i < info.Length(); i++) {
      v8::HandleScope handle_scope(info.GetIsolate());
      if (i != 0) {
        printf(" ");
      }

      // Explicitly catch potential exceptions in toString().
      v8::TryCatch try_catch(info.GetIsolate());
      v8::Local<v8::Value> arg = info[i];
      v8::Local<v8::String> str_obj;

      if (arg->IsSymbol()) {
        arg = v8::Local<v8::Symbol>::Cast(arg)->Description(info.GetIsolate());
      }
      if (!arg->ToString(info.GetIsolate()->GetCurrentContext())
               .ToLocal(&str_obj)) {
        try_catch.ReThrow();
        return;
      }

      v8::String::Utf8Value str(info.GetIsolate(), str_obj);
      size_t n = fwrite(*str, sizeof(**str), str.length(), stdout);
      if (n != str.length()) {
        FATAL("Error in fwrite\n");
      }
    }
    printf("\n");
    fflush(stdout);
  }

  static void Quit(const v8::FunctionCallbackInfo<v8::Value>& info) {
    fflush(stdout);
    fflush(stderr);
    // Only terminate, so not join the threads here, since joining concurrently
    // from multiple threads can be undefined behaviour (see pthread_join).
    for (TaskRunner* task_runner : task_runners) task_runner->Terminate();
  }

  static void Setlocale(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 1 || !info[0]->IsString()) {
      FATAL("Internal error: setlocale get one string argument.");
    }

    v8::String::Utf8Value str(info.GetIsolate(), info[1]);
    setlocale(LC_NUMERIC, *str);
  }

  static bool ReadFile(v8::Isolate* isolate, v8::Local<v8::Value> name,
                       std::string* chars) {
    v8::String::Utf8Value str(isolate, name);
    bool exists = false;
    std::string filename(*str, str.length());
    *chars = v8::internal::ReadFile(filename.c_str(), &exists);
    if (!exists) {
      isolate->ThrowError("Error reading file");
      return false;
    }
    return true;
  }

  static void Read(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 1 || !info[0]->IsString()) {
      FATAL("Internal error: read gets one string argument.");
    }
    std::string chars;
    v8::Isolate* isolate = info.GetIsolate();
    if (ReadFile(isolate, info[0], &chars)) {
      info.GetReturnValue().Set(ToV8String(isolate, chars));
    }
  }

  static void Load(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 1 || !info[0]->IsString()) {
      FATAL("Internal error: load gets one string argument.");
    }
    std::string chars;
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    InspectorIsolateData* data = InspectorIsolateData::FromContext(context);
    int context_group_id = data->GetContextGroupId(context);
    if (ReadFile(isolate, info[0], &chars)) {
      ExecuteStringTask(chars, context_group_id).Run(data);
    }
  }

  static void CompileAndRunWithOrigin(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 6 || !info[0]->IsInt32() || !info[1]->IsString() ||
        !info[2]->IsString() || !info[3]->IsInt32() || !info[4]->IsInt32() ||
        !info[5]->IsBoolean()) {
      FATAL(
          "Internal error: compileAndRunWithOrigin(context_group_id, source, "
          "name, line, column, is_module).");
    }

    backend_runner_->Append(std::make_unique<ExecuteStringTask>(
        info.GetIsolate(), info[0].As<v8::Int32>()->Value(),
        ToVector(info.GetIsolate(), info[1].As<v8::String>()),
        info[2].As<v8::String>(), info[3].As<v8::Int32>(),
        info[4].As<v8::Int32>(), info[5].As<v8::Boolean>()));
  }

  static void SetCurrentTimeMSForTest(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 1 || !info[0]->IsNumber()) {
      FATAL("Internal error: setCurrentTimeMSForTest(time).");
    }
    backend_runner_->data()->SetCurrentTimeMS(
        info[0].As<v8::Number>()->Value());
  }

  static void SetMemoryInfoForTest(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 1) {
      FATAL("Internal error: setMemoryInfoForTest(value).");
    }
    backend_runner_->data()->SetMemoryInfo(info[0]);
  }

  static void SchedulePauseOnNextStatement(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 3 || !info[0]->IsInt32() || !info[1]->IsString() ||
        !info[2]->IsString()) {
      FATAL(
          "Internal error: schedulePauseOnNextStatement(context_group_id, "
          "'reason', 'details').");
    }
    std::vector<uint16_t> reason =
        ToVector(info.GetIsolate(), info[1].As<v8::String>());
    std::vector<uint16_t> details =
        ToVector(info.GetIsolate(), info[2].As<v8::String>());
    int context_group_id = info[0].As<v8::Int32>()->Value();
    RunSyncTask(backend_runner_,
                [&context_group_id, &reason,
                 &details](InspectorIsolateData* data) {
                  data->SchedulePauseOnNextStatement(
                      context_group_id,
                      v8_inspector::StringView(reason.data(), reason.size()),
                      v8_inspector::StringView(details.data(), details.size()));
                });
  }

  static void CancelPauseOnNextStatement(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 1 || !info[0]->IsInt32()) {
      FATAL("Internal error: cancelPauseOnNextStatement(context_group_id).");
    }
    int context_group_id = info[0].As<v8::Int32>()->Value();
    RunSyncTask(backend_runner_,
                [&context_group_id](InspectorIsolateData* data) {
                  data->CancelPauseOnNextStatement(context_group_id);
                });
  }

  static void Stop(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 1 || !info[0]->IsInt32()) {
      FATAL("Internal error: stop(session_id).");
    }
    int session_id = info[0].As<v8::Int32>()->Value();
    RunSyncTask(backend_runner_, [&session_id](InspectorIsolateData* data) {
      data->Stop(session_id);
    });
  }

  static void SetLogConsoleApiMessageCalls(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 1 || !info[0]->IsBoolean()) {
      FATAL("Internal error: setLogConsoleApiMessageCalls(bool).");
    }
    backend_runner_->data()->SetLogConsoleApiMessageCalls(
        info[0].As<v8::Boolean>()->Value());
  }

  static void SetLogMaxAsyncCallStackDepthChanged(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 1 || !info[0]->IsBoolean()) {
      FATAL("Internal error: setLogMaxAsyncCallStackDepthChanged(bool).");
    }
    backend_runner_->data()->SetLogMaxAsyncCallStackDepthChanged(
        info[0].As<v8::Boolean>()->Value());
  }

  static void SetAdditionalConsoleApi(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 1 || !info[0]->IsString()) {
      FATAL("Internal error: SetAdditionalConsoleApi(string).");
    }
    std::vector<uint16_t> script =
        ToVector(info.GetIsolate(), info[0].As<v8::String>());
    RunSyncTask(backend_runner_, [&script](InspectorIsolateData* data) {
      data->SetAdditionalConsoleApi(
          v8_inspector::StringView(script.data(), script.size()));
    });
  }

  static void CreateContextGroup(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 0) {
      FATAL("Internal error: createContextGroup().");
    }
    int context_group_id = 0;
    RunSyncTask(backend_runner_,
                [&context_group_id](InspectorIsolateData* data) {
                  context_group_id = data->CreateContextGroup();
                });
    info.GetReturnValue().Set(
        v8::Int32::New(info.GetIsolate(), context_group_id));
  }

  static void CreateContext(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 2) {
      FATAL("Internal error: createContext(context, name).");
    }
    int context_group_id = info[0].As<v8::Int32>()->Value();
    std::vector<uint16_t> name =
        ToVector(info.GetIsolate(), info[1].As<v8::String>());

    RunSyncTask(backend_runner_, [&context_group_id,
                                  name](InspectorIsolateData* data) {
      CHECK(data->CreateContext(
          context_group_id,
          v8_inspector::StringView(name.data(), name.size())));
    });
  }

  static void ResetContextGroup(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 1 || !info[0]->IsInt32()) {
      FATAL("Internal error: resetContextGroup(context_group_id).");
    }
    int context_group_id = info[0].As<v8::Int32>()->Value();
    RunSyncTask(backend_runner_,
                [&context_group_id](InspectorIsolateData* data) {
                  data->ResetContextGroup(context_group_id);
                });
  }

  static bool IsValidConnectSessionArgs(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() < 3 || info.Length() > 4) return false;
    if (!info[0]->IsInt32() || !info[1]->IsString() || !info[2]->IsFunction()) {
      return false;
    }
    return info.Length() == 3 || info[3]->IsBoolean();
  }

  static void ConnectSession(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (!IsValidConnectSessionArgs(info)) {
      FATAL(
          "Internal error: connectionSession(context_group_id, state, "
          "dispatch, is_fully_trusted).");
    }
    v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();
    std::unique_ptr<FrontendChannelImpl> channel =
        std::make_unique<FrontendChannelImpl>(
            InspectorIsolateData::FromContext(context)->task_runner(),
            InspectorIsolateData::FromContext(context)->GetContextGroupId(
                context),
            info.GetIsolate(), info[2].As<v8::Function>());

    std::vector<uint8_t> state =
        ToBytes(info.GetIsolate(), info[1].As<v8::String>());
    int context_group_id = info[0].As<v8::Int32>()->Value();
    bool is_fully_trusted =
        info.Length() == 3 || info[3].As<v8::Boolean>()->Value();
    std::optional<int> session_id;
    RunSyncTask(backend_runner_,
                [context_group_id, &session_id, &channel, &state,
                 is_fully_trusted](InspectorIsolateData* data) {
                  session_id = data->ConnectSession(
                      context_group_id,
                      v8_inspector::StringView(state.data(), state.size()),
                      std::move(channel), is_fully_trusted);
                });

    CHECK(session_id.has_value());
    info.GetReturnValue().Set(v8::Int32::New(info.GetIsolate(), *session_id));
  }

  static void DisconnectSession(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 1 || !info[0]->IsInt32()) {
      FATAL("Internal error: disconnectionSession(session_id).");
    }
    v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();
    TaskRunner* context_task_runner =
        InspectorIsolateData::FromContext(context)->task_runner();
    int session_id = info[0].As<v8::Int32>()->Value();
    std::vector<uint8_t> state;
    RunSyncTask(backend_runner_, [&session_id, &context_task_runner,
                                  &state](InspectorIsolateData* data) {
      state = data->DisconnectSession(session_id, context_task_runner);
    });

    info.GetReturnValue().Set(ToV8String(info.GetIsolate(), state));
  }

  static void SendMessageToBackend(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 2 || !info[0]->IsInt32() || !info[1]->IsString()) {
      FATAL("Internal error: sendMessageToBackend(session_id, message).");
    }
    backend_runner_->Append(std::make_unique<SendMessageToBackendTask>(
        info[0].As<v8::Int32>()->Value(),
        ToVector(info.GetIsolate(), info[1].As<v8::String>())));
  }

  static void InterruptForMessages(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    backend_runner_->InterruptForMessages();
  }

  static void WaitForDebugger(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 2 || !info[0]->IsInt32() || !info[1]->IsFunction()) {
      FATAL("Internal error: waitForDebugger(context_group_id, callback).");
    }
    int context_group_id = info[0].As<v8::Int32>()->Value();
    RunSimpleAsyncTask(
        backend_runner_,
        [context_group_id](InspectorIsolateData* data) {
          data->WaitForDebugger(context_group_id);
        },
        info[1].As<v8::Function>());
  }
};

TaskRunner* UtilsExtension::backend_runner_ = nullptr;

bool StrictAccessCheck(v8::Local<v8::Context> accessing_context,
                       v8::Local<v8::Object> accessed_object,
                       v8::Local<v8::Value> data) {
  CHECK(accessing_context.IsEmpty());
  return accessing_context.IsEmpty();
}

class ConsoleExtension : public InspectorIsolateData::SetupGlobalTask {
 public:
  ~ConsoleExtension() override = default;
  void Run(v8::Isolate* isolate,
           v8::Local<v8::ObjectTemplate> global) override {
    v8::Local<v8::String> name =
        v8::String::NewFromUtf8Literal(isolate, "console");
    global->SetNativeDataProperty(name, &ConsoleGetterCallback, nullptr, {},
                                  v8::DontEnum);
  }

 private:
  static void ConsoleGetterCallback(
      v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::HandleScope scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::String> name =
        v8::String::NewFromUtf8Literal(isolate, "console");
    v8::Local<v8::Object> console = context->GetExtrasBindingObject()
                                        ->Get(context, name)
                                        .ToLocalChecked()
                                        .As<v8::Object>();
    info.GetReturnValue().Set(console);
  }
};

class InspectorExtension : public InspectorIsolateData::SetupGlobalTask {
 public:
  ~InspectorExtension() override = default;
  void Run(v8::Isolate* isolate,
           v8::Local<v8::ObjectTemplate> global) override {
    v8::Local<v8::ObjectTemplate> inspector = v8::ObjectTemplate::New(isolate);
    inspector->Set(isolate, "fireContextCreated",
                   v8::FunctionTemplate::New(
                       isolate, &InspectorExtension::FireContextCreated));
    inspector->Set(isolate, "fireContextDestroyed",
                   v8::FunctionTemplate::New(
                       isolate, &InspectorExtension::FireContextDestroyed));
    inspector->Set(
        isolate, "freeContext",
        v8::FunctionTemplate::New(isolate, &InspectorExtension::FreeContext));
    inspector->Set(isolate, "addInspectedObject",
                   v8::FunctionTemplate::New(
                       isolate, &InspectorExtension::AddInspectedObject));
    inspector->Set(isolate, "setMaxAsyncTaskStacks",
                   v8::FunctionTemplate::New(
                       isolate, &InspectorExtension::SetMaxAsyncTaskStacks));
    inspector->Set(
        isolate, "dumpAsyncTaskStacksStateForTest",
        v8::FunctionTemplate::New(
            isolate, &InspectorExtension::DumpAsyncTaskStacksStateForTest));
    inspector->Set(
        isolate, "breakProgram",
        v8::FunctionTemplate::New(isolate, &InspectorExtension::BreakProgram));
    inspector->Set(
        isolate, "createObjectWithStrictCheck",
        v8::FunctionTemplate::New(
            isolate, &InspectorExtension::CreateObjectWithStrictCheck));
    inspector->Set(isolate, "callWithScheduledBreak",
                   v8::FunctionTemplate::New(
                       isolate, &InspectorExtension::CallWithScheduledBreak));
    inspector->Set(
        isolate, "markObjectAsNotInspectable",
        v8::FunctionTemplate::New(
            isolate, &InspectorExtension::MarkObjectAsNotInspectable));
    inspector->Set(
        isolate, "createObjectWithNativeDataProperty",
        v8::FunctionTemplate::New(
            isolate, &InspectorExtension::CreateObjectWithNativeDataProperty));
    inspector->Set(isolate, "storeCurrentStackTrace",
                   v8::FunctionTemplate::New(
                       isolate, &InspectorExtension::StoreCurrentStackTrace));
    inspector->Set(isolate, "externalAsyncTaskStarted",
                   v8::FunctionTemplate::New(
                       isolate, &InspectorExtension::ExternalAsyncTaskStarted));
    inspector->Set(
        isolate, "externalAsyncTaskFinished",
        v8::FunctionTemplate::New(
            isolate, &InspectorExtension::ExternalAsyncTaskFinished));
    inspector->Set(isolate, "scheduleWithAsyncStack",
                   v8::FunctionTemplate::New(
                       isolate, &InspectorExtension::ScheduleWithAsyncStack));
    inspector->Set(
        isolate, "setAllowCodeGenerationFromStrings",
        v8::FunctionTemplate::New(
            isolate, &InspectorExtension::SetAllowCodeGenerationFromStrings));
    inspector->Set(isolate, "setResourceNamePrefix",
                   v8::FunctionTemplate::New(
                       isolate, &InspectorExtension::SetResourceNamePrefix));
    inspector->Set(isolate, "newExceptionWithMetaData",
                   v8::FunctionTemplate::New(
                       isolate, &InspectorExtension::newExceptionWithMetaData));
    inspector->Set(isolate, "callbackForTests",
                   v8::FunctionTemplate::New(
                       isolate, &InspectorExtension::CallbackForTests));
    inspector->Set(isolate, "runNestedMessageLoop",
                   v8::FunctionTemplate::New(
                       isolate, &InspectorExtension::RunNestedMessageLoop));
    global->Set(isolate, "inspector", inspector);
  }

 private:
  static void FireContextCreated(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();
    InspectorIsolateData* data = InspectorIsolateData::FromContext(context);
    data->FireContextCreated(context, data->GetContextGroupId(context),
                             v8_inspector::StringView());
  }

  static void FireContextDestroyed(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();
    InspectorIsolateData* data = InspectorIsolateData::FromContext(context);
    data->FireContextDestroyed(context);
  }

  static void FreeContext(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();
    InspectorIsolateData* data = InspectorIsolateData::FromContext(context);
    data->FreeContext(context);
  }

  static void AddInspectedObject(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 2 || !info[0]->IsInt32()) {
      FATAL("Internal error: addInspectedObject(session_id, object).");
    }
    v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();
    InspectorIsolateData* data = InspectorIsolateData::FromContext(context);
    data->AddInspectedObject(info[0].As<v8::Int32>()->Value(), info[1]);
  }

  static void SetMaxAsyncTaskStacks(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 1 || !info[0]->IsInt32()) {
      FATAL("Internal error: setMaxAsyncTaskStacks(max).");
    }
    InspectorIsolateData::FromContext(info.GetIsolate()->GetCurrentContext())
        ->SetMaxAsyncTaskStacksForTest(info[0].As<v8::Int32>()->Value());
  }

  static void DumpAsyncTaskStacksStateForTest(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 0) {
      FATAL("Internal error: dumpAsyncTaskStacksStateForTest().");
    }
    InspectorIsolateData::FromContext(info.GetIsolate()->GetCurrentContext())
        ->DumpAsyncTaskStacksStateForTest();
  }

  static void BreakProgram(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 2 || !info[0]->IsString() || !info[1]->IsString()) {
      FATAL("Internal error: breakProgram('reason', 'details').");
    }
    v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();
    InspectorIsolateData* data = InspectorIsolateData::FromContext(context);
    std::vector<uint16_t> reason =
        ToVector(info.GetIsolate(), info[0].As<v8::String>());
    v8_inspector::StringView reason_view(reason.data(), reason.size());
    std::vector<uint16_t> details =
        ToVector(info.GetIsolate(), info[1].As<v8::String>());
    v8_inspector::StringView details_view(details.data(), details.size());
    data->BreakProgram(data->GetContextGroupId(context), reason_view,
                       details_view);
  }

  static void CreateObjectWithStrictCheck(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 0) {
      FATAL("Internal error: createObjectWithStrictCheck().");
    }
    v8::Local<v8::ObjectTemplate> templ =
        v8::ObjectTemplate::New(info.GetIsolate());
    templ->SetAccessCheckCallback(&StrictAccessCheck);
    info.GetReturnValue().Set(
        templ->NewInstance(info.GetIsolate()->GetCurrentContext())
            .ToLocalChecked());
  }

  static void CallWithScheduledBreak(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 3 || !info[0]->IsFunction() || !info[1]->IsString() ||
        !info[2]->IsString()) {
      FATAL("Internal error: callWithScheduledBreak('reason', 'details').");
    }
    std::vector<uint16_t> reason =
        ToVector(info.GetIsolate(), info[1].As<v8::String>());
    v8_inspector::StringView reason_view(reason.data(), reason.size());
    std::vector<uint16_t> details =
        ToVector(info.GetIsolate(), info[2].As<v8::String>());
    v8_inspector::StringView details_view(details.data(), details.size());
    v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();
    InspectorIsolateData* data = InspectorIsolateData::FromContext(context);
    int context_group_id = data->GetContextGroupId(context);
    data->SchedulePauseOnNextStatement(context_group_id, reason_view,
                                       details_view);
    v8::MaybeLocal<v8::Value> result;
    result = info[0].As<v8::Function>()->Call(context, context->Global(), 0,
                                              nullptr);
    data->CancelPauseOnNextStatement(context_group_id);
  }

  static void MarkObjectAsNotInspectable(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 1 || !info[0]->IsObject()) {
      FATAL("Internal error: markObjectAsNotInspectable(object).");
    }
    v8::Local<v8::Object> object = info[0].As<v8::Object>();
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Private> notInspectablePrivate =
        v8::Private::ForApi(isolate, ToV8String(isolate, "notInspectable"));
    object
        ->SetPrivate(isolate->GetCurrentContext(), notInspectablePrivate,
                     v8::True(isolate))
        .ToChecked();
  }

  static void CreateObjectWithNativeDataProperty(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 2 || !info[0]->IsString() || !info[1]->IsBoolean()) {
      FATAL(
          "Internal error: createObjectWithNativeDataProperty('accessor name', "
          "hasSetter)\n");
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::ObjectTemplate> templ = v8::ObjectTemplate::New(isolate);
    if (info[1].As<v8::Boolean>()->Value()) {
      templ->SetNativeDataProperty(v8::Local<v8::String>::Cast(info[0]),
                                   AccessorGetter, AccessorSetter);
    } else {
      templ->SetNativeDataProperty(v8::Local<v8::String>::Cast(info[0]),
                                   AccessorGetter);
    }
    info.GetReturnValue().Set(
        templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked());
  }

  static void AccessorGetter(v8::Local<v8::Name> property,
                             const v8::PropertyCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    isolate->ThrowError("Getter is called");
  }

  static void AccessorSetter(v8::Local<v8::Name> property,
                             v8::Local<v8::Value> value,
                             const v8::PropertyCallbackInfo<void>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    isolate->ThrowError("Setter is called");
  }

  static void StoreCurrentStackTrace(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 1 || !info[0]->IsString()) {
      FATAL("Internal error: storeCurrentStackTrace('description')\n");
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    InspectorIsolateData* data = InspectorIsolateData::FromContext(context);
    std::vector<uint16_t> description =
        ToVector(isolate, info[0].As<v8::String>());
    v8_inspector::StringView description_view(description.data(),
                                              description.size());
    v8_inspector::V8StackTraceId id =
        data->StoreCurrentStackTrace(description_view);
    v8::Local<v8::ArrayBuffer> buffer =
        v8::ArrayBuffer::New(isolate, sizeof(id));
    *static_cast<v8_inspector::V8StackTraceId*>(
        buffer->GetBackingStore()->Data()) = id;
    info.GetReturnValue().Set(buffer);
  }

  static void ExternalAsyncTaskStarted(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 1 || !info[0]->IsArrayBuffer()) {
      FATAL("Internal error: externalAsyncTaskStarted(id)\n");
    }
    v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();
    InspectorIsolateData* data = InspectorIsolateData::FromContext(context);
    v8_inspector::V8StackTraceId* id =
        static_cast<v8_inspector::V8StackTraceId*>(
            info[0].As<v8::ArrayBuffer>()->GetBackingStore()->Data());
    data->ExternalAsyncTaskStarted(*id);
  }

  static void ExternalAsyncTaskFinished(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 1 || !info[0]->IsArrayBuffer()) {
      FATAL("Internal error: externalAsyncTaskFinished(id)\n");
    }
    v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();
    InspectorIsolateData* data = InspectorIsolateData::FromContext(context);
    v8_inspector::V8StackTraceId* id =
        static_cast<v8_inspector::V8StackTraceId*>(
            info[0].As<v8::ArrayBuffer>()->GetBackingStore()->Data());
    data->ExternalAsyncTaskFinished(*id);
  }

  static void ScheduleWithAsyncStack(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 3 || !info[0]->IsFunction() || !info[1]->IsString() ||
        !info[2]->IsBoolean()) {
      FATAL(
          "Internal error: scheduleWithAsyncStack(function, 'task-name', "
          "with_empty_stack).");
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    InspectorIsolateData* data = InspectorIsolateData::FromContext(context);
    int context_group_id = data->GetContextGroupId(context);
    bool with_empty_stack = info[2].As<v8::Boolean>()->Value();
    if (with_empty_stack) context->Exit();

    std::vector<uint16_t> task_name =
        ToVector(isolate, info[1].As<v8::String>());
    v8_inspector::StringView task_name_view(task_name.data(), task_name.size());

    RunAsyncTask(
        data->task_runner(), task_name_view,
        std::make_unique<SetTimeoutTask>(
            context_group_id, isolate, v8::Local<v8::Function>::Cast(info[0])));
    if (with_empty_stack) context->Enter();
  }

  static void SetAllowCodeGenerationFromStrings(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 1 || !info[0]->IsBoolean()) {
      FATAL("Internal error: setAllowCodeGenerationFromStrings(allow).");
    }
    info.GetIsolate()->GetCurrentContext()->AllowCodeGenerationFromStrings(
        info[0].As<v8::Boolean>()->Value());
  }
  static void SetResourceNamePrefix(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 1 || !info[0]->IsString()) {
      FATAL("Internal error: setResourceNamePrefix('prefix').");
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    InspectorIsolateData* data = InspectorIsolateData::FromContext(context);
    data->SetResourceNamePrefix(v8::Local<v8::String>::Cast(info[0]));
  }

  static void newExceptionWithMetaData(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 3 || !info[0]->IsString() || !info[1]->IsString() ||
        !info[2]->IsString()) {
      FATAL(
          "Internal error: newExceptionWithMetaData('message', 'key', "
          "'value').");
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    InspectorIsolateData* data = InspectorIsolateData::FromContext(context);

    auto error = v8::Exception::Error(info[0].As<v8::String>());
    CHECK(data->AssociateExceptionData(error, info[1].As<v8::String>(),
                                       info[2].As<v8::String>()));
    info.GetReturnValue().Set(error);
  }

  static void CallbackForTests(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 1 || !info[0]->IsFunction()) {
      FATAL("Internal error: callbackForTests(function).");
    }

    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();

    v8::Local<v8::Function> callback = v8::Local<v8::Function>::Cast(info[0]);
    v8::Local<v8::Value> result;
    if (callback->Call(context, v8::Undefined(isolate), 0, nullptr)
            .ToLocal(&result)) {
      info.GetReturnValue().Set(result);
    }
  }

  static void RunNestedMessageLoop(
      const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    InspectorIsolateData* data = InspectorIsolateData::FromContext(context);

    data->task_runner()->RunMessageLoop(true);
  }
};

int InspectorTestMain(int argc, char* argv[]) {
  v8::V8::InitializeICUDefaultLocation(argv[0]);
  std::unique_ptr<Platform> platform(platform::NewDefaultPlatform());
  v8::V8::InitializePlatform(platform.get());
  v8_flags.abort_on_contradictory_flags = true;
  v8::V8::SetFlagsFromCommandLine(&argc, argv, true);
  v8::V8::InitializeExternalStartupData(argv[0]);
  v8::V8::Initialize();
  i::DisableEmbeddedBlobRefcounting();

  base::Semaphore ready_semaphore(0);

  StartupData startup_data = {nullptr, 0};
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--embed") == 0) {
      argv[i++] = nullptr;
      printf("Embedding script '%s'\n", argv[i]);
      startup_data = i::CreateSnapshotDataBlobInternalForInspectorTest(
          SnapshotCreator::FunctionCodeHandling::kClear, argv[i]);
      argv[i] = nullptr;
    }
  }

  {
    InspectorIsolateData::SetupGlobalTasks frontend_extensions;
    frontend_extensions.emplace_back(new UtilsExtension());
    frontend_extensions.emplace_back(new ConsoleExtension());
    TaskRunner frontend_runner(std::move(frontend_extensions),
                               kFailOnUncaughtExceptions, &ready_semaphore,
                               startup_data.data ? &startup_data : nullptr,
                               kNoInspector);
    ready_semaphore.Wait();

    int frontend_context_group_id = 0;
    RunSyncTask(&frontend_runner,
                [&frontend_context_group_id](InspectorIsolateData* data) {
                  frontend_context_group_id = data->CreateContextGroup();
                });

    InspectorIsolateData::SetupGlobalTasks backend_extensions;
    backend_extensions.emplace_back(new SetTimeoutExtension());
    backend_extensions.emplace_back(new ConsoleExtension());
    backend_extensions.emplace_back(new InspectorExtension());
    TaskRunner backend_runner(
        std::move(backend_extensions), kStandardPropagateUncaughtExceptions,
        &ready_semaphore, startup_data.data ? &startup_data : nullptr,
        kWithInspector);
    ready_semaphore.Wait();
    UtilsExtension::set_backend_task_runner(&backend_runner);

    task_runners = {&frontend_runner, &backend_runner};

    for (int i = 1; i < argc; ++i) {
      // Ignore unknown flags.
      if (argv[i] == nullptr || argv[i][0] == '-') continue;

      bool exists = false;
      std::string chars = ReadFile(argv[i], &exists, true);
      if (!exists) {
        FATAL("Internal error: script file doesn't exists: %s\n", argv[i]);
      }
      frontend_runner.Append(std::make_unique<ExecuteStringTask>(
          chars, frontend_context_group_id));
    }

    frontend_runner.Join();
    backend_runner.Join();

    delete[] startup_data.data;

    // TaskRunners go out of scope here, which causes Isolate teardown and all
    // running background tasks to be properly joined.
  }

  i::FreeCurrentEmbeddedBlob();
  return 0;
}
}  //  namespace

}  // namespace internal
}  // namespace v8

int main(int argc, char* argv[]) {
  return v8::internal::InspectorTestMain(argc, argv);
}
