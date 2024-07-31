// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/inspector/task-runner.h"

#include "include/v8-exception.h"
#include "include/v8-local-handle.h"
#include "include/v8-primitive.h"
#include "src/flags/flags.h"
#include "src/init/v8.h"
#include "src/libplatform/default-platform.h"
#include "src/utils/locked-queue-inl.h"

#if !defined(V8_OS_WIN)
#include <unistd.h>
#endif  // !defined(V8_OS_WIN)

namespace v8 {
namespace internal {

namespace {

void ReportUncaughtException(v8::Isolate* isolate,
                             const v8::TryCatch& try_catch) {
  CHECK(try_catch.HasCaught());
  v8::HandleScope handle_scope(isolate);
  std::string message =
      *v8::String::Utf8Value(isolate, try_catch.Message()->Get());
  int line = try_catch.Message()
                 ->GetLineNumber(isolate->GetCurrentContext())
                 .FromJust();
  std::string source_line = *v8::String::Utf8Value(
      isolate, try_catch.Message()
                   ->GetSourceLine(isolate->GetCurrentContext())
                   .ToLocalChecked());
  fprintf(stderr, "Unhandled exception: %s @%s[%d]\n", message.data(),
          source_line.data(), line);
}

}  //  namespace

TaskRunner::TaskRunner(
    InspectorIsolateData::SetupGlobalTasks setup_global_tasks,
    CatchExceptions catch_exceptions, v8::base::Semaphore* ready_semaphore,
    v8::StartupData* startup_data, WithInspector with_inspector)
    : Thread(Options("Task Runner")),
      setup_global_tasks_(std::move(setup_global_tasks)),
      startup_data_(startup_data),
      with_inspector_(with_inspector),
      catch_exceptions_(catch_exceptions),
      ready_semaphore_(ready_semaphore),
      data_(nullptr),
      process_queue_semaphore_(0),
      nested_loop_count_(0),
      is_terminated_(0) {
  CHECK(Start());
}

TaskRunner::~TaskRunner() {}

void TaskRunner::Run() {
  data_.reset(new InspectorIsolateData(this, std::move(setup_global_tasks_),
                                       startup_data_, with_inspector_));
  if (ready_semaphore_) ready_semaphore_->Signal();
  RunMessageLoop(false);
}

void TaskRunner::RunMessageLoop(bool only_protocol) {
  int loop_number = ++nested_loop_count_;
  while (nested_loop_count_ == loop_number && !is_terminated_ &&
         !isolate()->IsExecutionTerminating()) {
    std::unique_ptr<TaskRunner::Task> task = GetNext(only_protocol);
    if (!task) return;
    v8::Isolate::Scope isolate_scope(isolate());
    v8::TryCatch try_catch(isolate());
    if (catch_exceptions_ == kStandardPropagateUncaughtExceptions) {
      try_catch.SetVerbose(true);
    }
    task->Run(data_.get());
    if (catch_exceptions_ == kFailOnUncaughtExceptions &&
        try_catch.HasCaught()) {
      ReportUncaughtException(isolate(), try_catch);
      base::OS::ExitProcess(0);
    }
    try_catch.Reset();
    task.reset();
    // Also pump isolate's foreground task queue to ensure progress.
    // This can be removed once https://crbug.com/v8/10747 is fixed.
    // TODO(10748): Enable --stress-incremental-marking after the existing
    // tests are fixed.
    if (!i::v8_flags.stress_incremental_marking &&
        !isolate()->IsExecutionTerminating()) {
      while (v8::platform::PumpMessageLoop(
          v8::internal::V8::GetCurrentPlatform(), isolate(),
          isolate()->HasPendingBackgroundTasks()
              ? platform::MessageLoopBehavior::kWaitForWork
              : platform::MessageLoopBehavior::kDoNotWait)) {
      }
    }
  }
}

static void RunMessageLoopInInterrupt(v8::Isolate* isolate, void* task_runner) {
  TaskRunner* runner = reinterpret_cast<TaskRunner*>(task_runner);
  runner->RunMessageLoop(true);
}

void TaskRunner::InterruptForMessages() {
  isolate()->RequestInterrupt(&RunMessageLoopInInterrupt, this);
}

void TaskRunner::QuitMessageLoop() {
  DCHECK_LT(0, nested_loop_count_);
  --nested_loop_count_;
}

void TaskRunner::Append(std::unique_ptr<Task> task) {
  queue_.Enqueue(std::move(task));
  process_queue_semaphore_.Signal();
}

void TaskRunner::Terminate() {
  is_terminated_++;
  isolate()->TerminateExecution();
  process_queue_semaphore_.Signal();
}

std::unique_ptr<TaskRunner::Task> TaskRunner::GetNext(bool only_protocol) {
  for (;;) {
    if (is_terminated_) return nullptr;
    if (only_protocol) {
      std::unique_ptr<Task> task;
      if (queue_.Dequeue(&task)) {
        if (task->is_priority_task()) return task;
        deferred_queue_.Enqueue(std::move(task));
      }
    } else {
      std::unique_ptr<Task> task;
      if (deferred_queue_.Dequeue(&task)) return task;
      if (queue_.Dequeue(&task)) return task;
    }
    process_queue_semaphore_.Wait();
  }
}

}  // namespace internal
}  // namespace v8
