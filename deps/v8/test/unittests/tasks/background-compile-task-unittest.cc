// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "include/v8-platform.h"
#include "src/api/api-inl.h"
#include "src/ast/ast.h"
#include "src/ast/scopes.h"
#include "src/base/platform/semaphore.h"
#include "src/codegen/compiler.h"
#include "src/execution/isolate-inl.h"
#include "src/flags/flags.h"
#include "src/init/v8.h"
#include "src/objects/smi.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/parser.h"
#include "src/parsing/preparse-data.h"
#include "src/zone/zone-list-inl.h"
#include "test/unittests/test-helpers.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

class BackgroundCompileTaskTest : public TestWithNativeContext {
 public:
  BackgroundCompileTaskTest() : allocator_(isolate()->allocator()) {}
  ~BackgroundCompileTaskTest() override = default;
  BackgroundCompileTaskTest(const BackgroundCompileTaskTest&) = delete;
  BackgroundCompileTaskTest& operator=(const BackgroundCompileTaskTest&) =
      delete;

  AccountingAllocator* allocator() { return allocator_; }

  static void SetUpTestSuite() {
    CHECK_NULL(save_flags_);
    save_flags_ = new SaveFlags();
    TestWithNativeContext::SetUpTestSuite();
  }

  static void TearDownTestSuite() {
    TestWithNativeContext::TearDownTestSuite();
    CHECK_NOT_NULL(save_flags_);
    delete save_flags_;
    save_flags_ = nullptr;
  }

  BackgroundCompileTask* NewBackgroundCompileTask(
      Isolate* isolate, Handle<SharedFunctionInfo> shared,
      size_t stack_size = v8_flags.stack_size) {
    return new BackgroundCompileTask(
        isolate, shared, test::SourceCharacterStreamForShared(isolate, shared),
        isolate->counters()->worker_thread_runtime_call_stats(),
        isolate->counters()->compile_function_on_background(),
        v8_flags.stack_size);
  }

 private:
  AccountingAllocator* allocator_;
  static SaveFlags* save_flags_;
};

SaveFlags* BackgroundCompileTaskTest::save_flags_ = nullptr;

TEST_F(BackgroundCompileTaskTest, Construct) {
  Handle<SharedFunctionInfo> shared =
      test::CreateSharedFunctionInfo(isolate(), nullptr);
  ASSERT_FALSE(shared->is_compiled());
  std::unique_ptr<BackgroundCompileTask> task(
      NewBackgroundCompileTask(isolate(), shared));
}

TEST_F(BackgroundCompileTaskTest, SyntaxError) {
  test::ScriptResource* script =
      new test::ScriptResource("^^^", strlen("^^^"), JSParameterCount(0));
  Handle<SharedFunctionInfo> shared =
      test::CreateSharedFunctionInfo(isolate(), script);
  std::unique_ptr<BackgroundCompileTask> task(
      NewBackgroundCompileTask(isolate(), shared));

  task->RunOnMainThread(isolate());
  ASSERT_FALSE(Compiler::FinalizeBackgroundCompileTask(
      task.get(), isolate(), Compiler::KEEP_EXCEPTION));
  ASSERT_TRUE(isolate()->has_exception());

  isolate()->clear_exception();
}

TEST_F(BackgroundCompileTaskTest, CompileAndRun) {
  const char raw_script[] =
      "function g() {\n"
      "  f = function(a) {\n"
      "        for (var i = 0; i < 3; i++) { a += 20; }\n"
      "        return a;\n"
      "      }\n"
      "  return f;\n"
      "}\n"
      "g();";
  test::ScriptResource* script = new test::ScriptResource(
      raw_script, strlen(raw_script), JSParameterCount(0));
  DirectHandle<JSFunction> f = RunJS<JSFunction>(script);
  Handle<SharedFunctionInfo> shared = handle(f->shared(), isolate());
  ASSERT_FALSE(shared->is_compiled());
  std::unique_ptr<BackgroundCompileTask> task(
      NewBackgroundCompileTask(isolate(), shared));

  task->RunOnMainThread(isolate());
  ASSERT_TRUE(Compiler::FinalizeBackgroundCompileTask(
      task.get(), isolate(), Compiler::KEEP_EXCEPTION));
  ASSERT_TRUE(shared->is_compiled());

  Tagged<Smi> value = Cast<Smi>(*RunJS("f(100);"));
  ASSERT_TRUE(value == Smi::FromInt(160));
}

TEST_F(BackgroundCompileTaskTest, CompileFailure) {
  std::string raw_script("() { var a = ");
  for (int i = 0; i < 10000; i++) {
    // TODO(leszeks): Figure out a more "unit-test-y" way of forcing an analysis
    // failure than a binop stack overflow.

    // Alternate + and - to avoid n-ary operation nodes.
    raw_script += "'x' + 'x' - ";
  }
  raw_script += " 'x'; }";
  test::ScriptResource* script = new test::ScriptResource(
      raw_script.c_str(), strlen(raw_script.c_str()), JSParameterCount(0));
  Handle<SharedFunctionInfo> shared =
      test::CreateSharedFunctionInfo(isolate(), script);
  std::unique_ptr<BackgroundCompileTask> task(
      NewBackgroundCompileTask(isolate(), shared, 100));

  task->RunOnMainThread(isolate());
  ASSERT_FALSE(Compiler::FinalizeBackgroundCompileTask(
      task.get(), isolate(), Compiler::KEEP_EXCEPTION));
  ASSERT_TRUE(isolate()->has_exception());

  isolate()->clear_exception();
}

class CompileTask : public Task {
 public:
  CompileTask(BackgroundCompileTask* task, base::Semaphore* semaphore)
      : task_(task), semaphore_(semaphore) {}
  ~CompileTask() override = default;
  CompileTask(const CompileTask&) = delete;
  CompileTask& operator=(const CompileTask&) = delete;

  void Run() override {
    task_->Run();
    semaphore_->Signal();
  }

 private:
  BackgroundCompileTask* task_;
  base::Semaphore* semaphore_;
};

TEST_F(BackgroundCompileTaskTest, CompileOnBackgroundThread) {
  const char* raw_script =
      "(a, b) {\n"
      "  var c = a + b;\n"
      "  function bar() { return b }\n"
      "  var d = { foo: 100, bar : bar() }\n"
      "  return bar;"
      "}";
  test::ScriptResource* script = new test::ScriptResource(
      raw_script, strlen(raw_script), JSParameterCount(2));
  Handle<SharedFunctionInfo> shared =
      test::CreateSharedFunctionInfo(isolate(), script);
  std::unique_ptr<BackgroundCompileTask> task(
      NewBackgroundCompileTask(isolate(), shared));

  base::Semaphore semaphore(0);
  auto background_task = std::make_unique<CompileTask>(task.get(), &semaphore);

  V8::GetCurrentPlatform()->PostTaskOnWorkerThread(TaskPriority::kUserVisible,
                                                   std::move(background_task));
  semaphore.Wait();
  ASSERT_TRUE(Compiler::FinalizeBackgroundCompileTask(
      task.get(), isolate(), Compiler::KEEP_EXCEPTION));
  ASSERT_TRUE(shared->is_compiled());
}

TEST_F(BackgroundCompileTaskTest, EagerInnerFunctions) {
  const char raw_script[] =
      "function g() {\n"
      "  f = function() {\n"
      "    // Simulate an eager IIFE with brackets.\n "
      "    var e = (function () { return 42; });\n"
      "    return e;\n"
      "  }\n"
      "  return f;\n"
      "}\n"
      "g();";
  test::ScriptResource* script = new test::ScriptResource(
      raw_script, strlen(raw_script), JSParameterCount(0));
  DirectHandle<JSFunction> f = RunJS<JSFunction>(script);
  Handle<SharedFunctionInfo> shared = handle(f->shared(), isolate());
  ASSERT_FALSE(shared->is_compiled());
  std::unique_ptr<BackgroundCompileTask> task(
      NewBackgroundCompileTask(isolate(), shared));

  task->RunOnMainThread(isolate());
  ASSERT_TRUE(Compiler::FinalizeBackgroundCompileTask(
      task.get(), isolate(), Compiler::KEEP_EXCEPTION));
  ASSERT_TRUE(shared->is_compiled());

  DirectHandle<JSFunction> e = RunJS<JSFunction>("f();");

  ASSERT_TRUE(e->shared()->is_compiled());
}

TEST_F(BackgroundCompileTaskTest, LazyInnerFunctions) {
  const char raw_script[] =
      "function g() {\n"
      "  f = function() {\n"
      "    function e() { return 42; };\n"
      "    return e;\n"
      "  }\n"
      "  return f;\n"
      "}\n"
      "g();";
  test::ScriptResource* script = new test::ScriptResource(
      raw_script, strlen(raw_script), JSParameterCount(0));
  DirectHandle<JSFunction> f = RunJS<JSFunction>(script);
  Handle<SharedFunctionInfo> shared = handle(f->shared(), isolate());
  ASSERT_FALSE(shared->is_compiled());
  std::unique_ptr<BackgroundCompileTask> task(
      NewBackgroundCompileTask(isolate(), shared));

  // There's already a task for this SFI.

  task->RunOnMainThread(isolate());
  ASSERT_TRUE(Compiler::FinalizeBackgroundCompileTask(
      task.get(), isolate(), Compiler::KEEP_EXCEPTION));
  ASSERT_TRUE(shared->is_compiled());

  DirectHandle<JSFunction> e = RunJS<JSFunction>("f();");

  ASSERT_FALSE(e->shared()->is_compiled());
}

}  // namespace internal
}  // namespace v8
