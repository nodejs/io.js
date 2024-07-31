// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/inspector/tasks.h"

#include <vector>

#include "include/v8-isolate.h"
#include "include/v8-script.h"
#include "test/inspector/isolate-data.h"
#include "test/inspector/utils.h"

namespace v8 {
namespace internal {

void RunSyncTask(TaskRunner* task_runner,
                 std::function<void(InspectorIsolateData*)> callback) {
  class SyncTask : public TaskRunner::Task {
   public:
    SyncTask(v8::base::Semaphore* ready_semaphore,
             std::function<void(InspectorIsolateData*)> callback)
        : ready_semaphore_(ready_semaphore), callback_(callback) {}
    ~SyncTask() override = default;
    bool is_priority_task() final { return true; }

   private:
    void Run(InspectorIsolateData* data) override {
      callback_(data);
      if (ready_semaphore_) ready_semaphore_->Signal();
    }

    v8::base::Semaphore* ready_semaphore_;
    std::function<void(InspectorIsolateData*)> callback_;
  };

  v8::base::Semaphore ready_semaphore(0);
  task_runner->Append(std::make_unique<SyncTask>(&ready_semaphore, callback));
  ready_semaphore.Wait();
}

void RunSimpleAsyncTask(TaskRunner* task_runner,
                        std::function<void(InspectorIsolateData* data)> task,
                        v8::Local<v8::Function> callback) {
  class DispatchResponseTask : public TaskRunner::Task {
   public:
    explicit DispatchResponseTask(v8::Local<v8::Function> callback)
        : context_(callback->GetIsolate(),
                   callback->GetIsolate()->GetCurrentContext()),
          client_callback_(callback->GetIsolate(), callback) {}
    ~DispatchResponseTask() override = default;

   private:
    bool is_priority_task() final { return true; }
    void Run(InspectorIsolateData* data) override {
      v8::HandleScope handle_scope(data->isolate());
      v8::Local<v8::Context> context = context_.Get(data->isolate());
      v8::MicrotasksScope microtasks_scope(context,
                                           v8::MicrotasksScope::kRunMicrotasks);
      v8::Context::Scope context_scope(context);
      USE(client_callback_.Get(data->isolate())
              ->Call(context, context->Global(), 0, nullptr));
    }
    v8::Global<v8::Context> context_;
    v8::Global<v8::Function> client_callback_;
  };

  using TaskCallback = std::function<void(InspectorIsolateData * data)>;

  class TaskWrapper : public TaskRunner::Task {
   public:
    TaskWrapper(TaskCallback task, TaskRunner* client_task_runner,
                std::unique_ptr<TaskRunner::Task> response_task)
        : task_(std::move(task)),
          client_task_runner_(client_task_runner),
          response_task_(std::move(response_task)) {}

    ~TaskWrapper() override = default;

   private:
    bool is_priority_task() final { return true; }
    void Run(InspectorIsolateData* data) override {
      task_(data);
      client_task_runner_->Append(std::move(response_task_));
    }

    TaskCallback task_;
    TaskRunner* client_task_runner_;
    std::unique_ptr<TaskRunner::Task> response_task_;
  };

  v8::Local<v8::Context> context = callback->GetIsolate()->GetCurrentContext();
  TaskRunner* response_task_runner =
      InspectorIsolateData::FromContext(context)->task_runner();

  auto response_task = std::make_unique<DispatchResponseTask>(callback);
  task_runner->Append(std::make_unique<TaskWrapper>(
      std::move(task), response_task_runner, std::move(response_task)));
}

void ExecuteStringTask::Run(InspectorIsolateData* data) {
  v8::HandleScope handle_scope(data->isolate());
  v8::Local<v8::Context> context = data->GetDefaultContext(context_group_id_);
  v8::MicrotasksScope microtasks_scope(context,
                                       v8::MicrotasksScope::kRunMicrotasks);
  v8::Context::Scope context_scope(context);
  v8::ScriptOrigin origin(ToV8String(data->isolate(), name_), line_offset_,
                          column_offset_,
                          /* resource_is_shared_cross_origin */ false,
                          /* script_id */ -1,
                          /* source_map_url */ v8::Local<v8::Value>(),
                          /* resource_is_opaque */ false,
                          /* is_wasm */ false, is_module_);
  v8::Local<v8::String> source;
  if (expression_.size() != 0)
    source = ToV8String(data->isolate(), expression_);
  else
    source = ToV8String(data->isolate(), expression_utf8_);

  v8::ScriptCompiler::Source scriptSource(source, origin);
  if (!is_module_) {
    v8::Local<v8::Script> script;
    if (!v8::ScriptCompiler::Compile(context, &scriptSource).ToLocal(&script))
      return;
    v8::MaybeLocal<v8::Value> result;
    result = script->Run(context);
  } else {
    data->RegisterModule(context, name_, &scriptSource);
  }
}

}  // namespace internal
}  // namespace v8
