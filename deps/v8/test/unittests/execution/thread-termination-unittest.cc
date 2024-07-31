// Copyright 2009 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "include/v8-function.h"
#include "include/v8-locker.h"
#include "src/api/api-inl.h"
#include "src/base/platform/platform.h"
#include "src/execution/interrupts-scope.h"
#include "src/execution/isolate.h"
#include "src/init/v8.h"
#include "src/objects/objects-inl.h"
#include "test/unittests/test-utils.h"
#include "testing/gmock-support.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {

base::Semaphore* semaphore = nullptr;

class TerminatorThread : public base::Thread {
 public:
  explicit TerminatorThread(i::Isolate* isolate)
      : Thread(Options("TerminatorThread")),
        isolate_(reinterpret_cast<Isolate*>(isolate)) {}
  void Run() override {
    semaphore->Wait();
    CHECK(!isolate_->IsExecutionTerminating());
    isolate_->TerminateExecution();
  }

 private:
  Isolate* isolate_;
};

void Signal(const FunctionCallbackInfo<Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  semaphore->Signal();
}

MaybeLocal<Value> CompileRun(Local<Context> context, Local<String> source) {
  Local<Script> script = Script::Compile(context, source).ToLocalChecked();
  return script->Run(context);
}

MaybeLocal<Value> CompileRun(Local<Context> context, const char* source) {
  return CompileRun(
      context,
      String::NewFromUtf8(context->GetIsolate(), source).ToLocalChecked());
}

void DoLoop(const FunctionCallbackInfo<Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  TryCatch try_catch(info.GetIsolate());
  CHECK(!info.GetIsolate()->IsExecutionTerminating());
  MaybeLocal<Value> result = CompileRun(info.GetIsolate()->GetCurrentContext(),
                                        "function f() {"
                                        "  var term = true;"
                                        "  try {"
                                        "    while(true) {"
                                        "      if (term) terminate();"
                                        "      term = false;"
                                        "    }"
                                        "    fail();"
                                        "  } catch(e) {"
                                        "    fail();"
                                        "  }"
                                        "}"
                                        "f()");
  CHECK(result.IsEmpty());
  CHECK(try_catch.HasCaught());
  CHECK(try_catch.Exception()->IsNull());
  CHECK(try_catch.Message().IsEmpty());
  CHECK(!try_catch.CanContinue());
  CHECK(info.GetIsolate()->IsExecutionTerminating());
}

void Fail(const FunctionCallbackInfo<Value>& info) { UNREACHABLE(); }

void Loop(const FunctionCallbackInfo<Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  CHECK(!info.GetIsolate()->IsExecutionTerminating());
  MaybeLocal<Value> result =
      CompileRun(info.GetIsolate()->GetCurrentContext(),
                 "try { doloop(); fail(); } catch(e) { fail(); }");
  CHECK(result.IsEmpty());
  CHECK(info.GetIsolate()->IsExecutionTerminating());
}

void TerminateCurrentThread(const FunctionCallbackInfo<Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  CHECK(!info.GetIsolate()->IsExecutionTerminating());
  info.GetIsolate()->TerminateExecution();
}

class ThreadTerminationTest : public TestWithIsolate {
 public:
  void TestTerminatingFromOtherThread(const char* source) {
    semaphore = new base::Semaphore(0);
    TerminatorThread thread(i_isolate());
    CHECK(thread.Start());

    HandleScope scope(isolate());
    Local<ObjectTemplate> global =
        CreateGlobalTemplate(isolate(), Signal, DoLoop);
    Local<Context> context = Context::New(isolate(), nullptr, global);
    Context::Scope context_scope(context);
    CHECK(!isolate()->IsExecutionTerminating());
    MaybeLocal<Value> result = TryRunJS(source);
    CHECK(result.IsEmpty());
    thread.Join();
    delete semaphore;
    semaphore = nullptr;
  }

  void TestTerminatingFromCurrentThread(const char* source) {
    HandleScope scope(isolate());
    Local<ObjectTemplate> global =
        CreateGlobalTemplate(isolate(), TerminateCurrentThread, DoLoop);
    Local<Context> context = Context::New(isolate(), nullptr, global);
    Context::Scope context_scope(context);
    CHECK(!isolate()->IsExecutionTerminating());
    MaybeLocal<Value> result = TryRunJS(source);
    CHECK(result.IsEmpty());
  }

  Local<ObjectTemplate> CreateGlobalTemplate(Isolate* isolate,
                                             FunctionCallback terminate,
                                             FunctionCallback doloop) {
    Local<ObjectTemplate> global = ObjectTemplate::New(isolate);
    global->Set(NewString("terminate"),
                FunctionTemplate::New(isolate, terminate));
    global->Set(NewString("fail"), FunctionTemplate::New(isolate, Fail));
    global->Set(NewString("loop"), FunctionTemplate::New(isolate, Loop));
    global->Set(NewString("doloop"), FunctionTemplate::New(isolate, doloop));
    return global;
  }
};

void DoLoopNoCall(const FunctionCallbackInfo<Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  TryCatch try_catch(info.GetIsolate());
  CHECK(!info.GetIsolate()->IsExecutionTerminating());
  MaybeLocal<Value> result = CompileRun(info.GetIsolate()->GetCurrentContext(),
                                        "var term = true;"
                                        "while(true) {"
                                        "  if (term) terminate();"
                                        "  term = false;"
                                        "}");
  CHECK(result.IsEmpty());
  CHECK(try_catch.HasCaught());
  CHECK(try_catch.Exception()->IsNull());
  CHECK(try_catch.Message().IsEmpty());
  CHECK(!try_catch.CanContinue());
  CHECK(info.GetIsolate()->IsExecutionTerminating());
}

// Test that a single thread of JavaScript execution can terminate
// itself.
TEST_F(ThreadTerminationTest, TerminateOnlyV8ThreadFromThreadItself) {
  HandleScope scope(isolate());
  Local<ObjectTemplate> global =
      CreateGlobalTemplate(isolate(), TerminateCurrentThread, DoLoop);
  Local<Context> context = Context::New(isolate(), nullptr, global);
  Context::Scope context_scope(context);
  CHECK(!isolate()->IsExecutionTerminating());
  // Run a loop that will be infinite if thread termination does not work.
  MaybeLocal<Value> result =
      TryRunJS("try { loop(); fail(); } catch(e) { fail(); }");
  CHECK(result.IsEmpty());
  // Test that we can run the code again after thread termination.
  CHECK(!isolate()->IsExecutionTerminating());
  result = TryRunJS("try { loop(); fail(); } catch(e) { fail(); }");
  CHECK(result.IsEmpty());
}

// Test that a single thread of JavaScript execution can terminate
// itself in a loop that performs no calls.
TEST_F(ThreadTerminationTest, TerminateOnlyV8ThreadFromThreadItselfNoLoop) {
  HandleScope scope(isolate());
  Local<ObjectTemplate> global =
      CreateGlobalTemplate(isolate(), TerminateCurrentThread, DoLoopNoCall);
  Local<Context> context = Context::New(isolate(), nullptr, global);
  Context::Scope context_scope(context);
  CHECK(!isolate()->IsExecutionTerminating());
  // Run a loop that will be infinite if thread termination does not work.
  static const char* source = "try { loop(); fail(); } catch(e) { fail(); }";
  MaybeLocal<Value> result = TryRunJS(source);
  CHECK(result.IsEmpty());
  CHECK(!isolate()->IsExecutionTerminating());
  // Test that we can run the code again after thread termination.
  result = TryRunJS(source);
  CHECK(result.IsEmpty());
}

// Test that a single thread of JavaScript execution can be terminated
// from the side by another thread.
TEST_F(ThreadTerminationTest, TerminateOnlyV8ThreadFromOtherThread) {
  // Run a loop that will be infinite if thread termination does not work.
  TestTerminatingFromOtherThread(
      "try { loop(); fail(); } catch(e) { fail(); }");
}

// Test that execution can be terminated from within JSON.stringify.
TEST_F(ThreadTerminationTest, TerminateJsonStringify) {
  TestTerminatingFromCurrentThread(
      "var x = [];"
      "x[2**31]=1;"
      "terminate();"
      "JSON.stringify(x);"
      "fail();");
}

TEST_F(ThreadTerminationTest, TerminateBigIntMultiplication) {
  TestTerminatingFromCurrentThread(
      "terminate();"
      "var a = 5n ** 555555n;"
      "var b = 3n ** 3333333n;"
      "a * b;"
      "fail();");
}

TEST_F(ThreadTerminationTest, TerminateOptimizedBigIntMultiplication) {
  i::v8_flags.allow_natives_syntax = true;
  TestTerminatingFromCurrentThread(
      "function foo(a, b) { return a * b; }"
      "%PrepareFunctionForOptimization(foo);"
      "foo(1n, 2n);"
      "foo(1n, 2n);"
      "%OptimizeFunctionOnNextCall(foo);"
      "foo(1n, 2n);"
      "var a = 5n ** 555555n;"
      "var b = 3n ** 3333333n;"
      "terminate();"
      "foo(a, b);"
      "fail();");
}

TEST_F(ThreadTerminationTest, TerminateBigIntDivision) {
  TestTerminatingFromCurrentThread(
      "var a = 2n ** 2222222n;"
      "var b = 3n ** 333333n;"
      "terminate();"
      "a / b;"
      "fail();");
}

TEST_F(ThreadTerminationTest, TerminateOptimizedBigIntDivision) {
  i::v8_flags.allow_natives_syntax = true;
  TestTerminatingFromCurrentThread(
      "function foo(a, b) { return a / b; }"
      "%PrepareFunctionForOptimization(foo);"
      "foo(3n, 2n);"
      "foo(3n, 2n);"
      "%OptimizeFunctionOnNextCall(foo);"
      "foo(3n, 2n);"
      "var a = 2n ** 2222222n;"
      "var b = 3n ** 333333n;"
      "terminate();"
      "foo(a, b);"
      "fail();");
}

TEST_F(ThreadTerminationTest, TerminateBigIntToString) {
  TestTerminatingFromCurrentThread(
      "var a = 2n ** 2222222n;"
      "terminate();"
      "a.toString();"
      "fail();");
}

TEST_F(ThreadTerminationTest, TerminateBigIntFromString) {
  TestTerminatingFromCurrentThread(
      "var a = '12344567890'.repeat(100000);\n"
      "terminate();\n"
      "BigInt(a);\n"
      "fail();\n");
}

void LoopGetProperty(const FunctionCallbackInfo<Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  TryCatch try_catch(info.GetIsolate());
  CHECK(!info.GetIsolate()->IsExecutionTerminating());
  MaybeLocal<Value> result =
      CompileRun(info.GetIsolate()->GetCurrentContext(),
                 "function f() {"
                 "  try {"
                 "    while(true) {"
                 "      terminate_or_return_object().x;"
                 "    }"
                 "    fail();"
                 "  } catch(e) {"
                 "    (function() {})();"  // trigger stack check.
                 "    fail();"
                 "  }"
                 "}"
                 "f()");
  CHECK(result.IsEmpty());
  CHECK(try_catch.HasCaught());
  CHECK(try_catch.Exception()->IsNull());
  CHECK(try_catch.Message().IsEmpty());
  CHECK(!try_catch.CanContinue());
  CHECK(info.GetIsolate()->IsExecutionTerminating());
}

int call_count = 0;

void TerminateOrReturnObject(const FunctionCallbackInfo<Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  if (++call_count == 10) {
    CHECK(!info.GetIsolate()->IsExecutionTerminating());
    info.GetIsolate()->TerminateExecution();
    return;
  }
  Local<Object> result = Object::New(info.GetIsolate());
  Local<Context> context = info.GetIsolate()->GetCurrentContext();
  Maybe<bool> val = result->Set(
      context, String::NewFromUtf8(context->GetIsolate(), "x").ToLocalChecked(),
      Integer::New(info.GetIsolate(), 42));
  CHECK(val.FromJust());
  info.GetReturnValue().Set(result);
}

// Test that we correctly handle termination exceptions if they are
// triggered by the creation of error objects in connection with ICs.
TEST_F(ThreadTerminationTest, TerminateLoadICException) {
  HandleScope scope(isolate());
  Local<ObjectTemplate> global = ObjectTemplate::New(isolate());
  global->Set(NewString("terminate_or_return_object"),
              FunctionTemplate::New(isolate(), TerminateOrReturnObject));
  global->Set(NewString("fail"), FunctionTemplate::New(isolate(), Fail));
  global->Set(NewString("loop"),
              FunctionTemplate::New(isolate(), LoopGetProperty));

  Local<Context> context = Context::New(isolate(), nullptr, global);
  Context::Scope context_scope(context);
  CHECK(!isolate()->IsExecutionTerminating());
  // Run a loop that will be infinite if thread termination does not work.
  static const char* source = "try { loop(); fail(); } catch(e) { fail(); }";
  call_count = 0;
  MaybeLocal<Value> result = CompileRun(isolate()->GetCurrentContext(), source);
  CHECK(result.IsEmpty());
  // Test that we can run the code again after thread termination.
  CHECK(!isolate()->IsExecutionTerminating());
  call_count = 0;
  result = CompileRun(isolate()->GetCurrentContext(), source);
  CHECK(result.IsEmpty());
}

Persistent<String> reenter_script_1;
Persistent<String> reenter_script_2;

void DoLoopCancelTerminate(const FunctionCallbackInfo<Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  Isolate* isolate = info.GetIsolate();
  TryCatch try_catch(isolate);
  CHECK(!isolate->IsExecutionTerminating());
  MaybeLocal<Value> result = CompileRun(isolate->GetCurrentContext(),
                                        "var term = true;"
                                        "while(true) {"
                                        "  if (term) terminate();"
                                        "  term = false;"
                                        "}"
                                        "fail();");
  CHECK(result.IsEmpty());
  CHECK(try_catch.HasCaught());
  CHECK(try_catch.Exception()->IsNull());
  CHECK(try_catch.Message().IsEmpty());
  CHECK(!try_catch.CanContinue());
  CHECK(isolate->IsExecutionTerminating());
  CHECK(try_catch.HasTerminated());
  isolate->CancelTerminateExecution();
  CHECK(!isolate->IsExecutionTerminating());
}

// Test that a single thread of JavaScript execution can terminate
// itself and then resume execution.
TEST_F(ThreadTerminationTest, TerminateCancelTerminateFromThreadItself) {
  HandleScope scope(isolate());
  Local<ObjectTemplate> global = CreateGlobalTemplate(
      isolate(), TerminateCurrentThread, DoLoopCancelTerminate);
  Local<Context> context = Context::New(isolate(), nullptr, global);
  Context::Scope context_scope(context);
  CHECK(!isolate()->IsExecutionTerminating());
  // Check that execution completed with correct return value.
  Local<Value> result =
      CompileRun(isolate()->GetCurrentContext(),
                 "try { doloop(); } catch(e) { fail(); } 'completed';")
          .ToLocalChecked();
  CHECK(result->Equals(isolate()->GetCurrentContext(), NewString("completed"))
            .FromJust());
}

void MicrotaskShouldNotRun(const FunctionCallbackInfo<Value>& info) {
  UNREACHABLE();
}

void MicrotaskLoopForever(const FunctionCallbackInfo<Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  Isolate* isolate = info.GetIsolate();
  HandleScope scope(isolate);
  // Enqueue another should-not-run task to ensure we clean out the queue
  // when we terminate.
  isolate->EnqueueMicrotask(
      Function::New(isolate->GetCurrentContext(), MicrotaskShouldNotRun)
          .ToLocalChecked());
  CompileRun(isolate->GetCurrentContext(), "terminate(); while (true) { }");
  CHECK(isolate->IsExecutionTerminating());
}

TEST_F(ThreadTerminationTest, TerminateFromOtherThreadWhileMicrotaskRunning) {
  semaphore = new base::Semaphore(0);
  TerminatorThread thread(i_isolate());
  CHECK(thread.Start());

  isolate()->SetMicrotasksPolicy(MicrotasksPolicy::kExplicit);
  HandleScope scope(isolate());
  Local<ObjectTemplate> global =
      CreateGlobalTemplate(isolate(), Signal, DoLoop);
  Local<Context> context = Context::New(isolate(), nullptr, global);
  Context::Scope context_scope(context);
  isolate()->EnqueueMicrotask(
      Function::New(isolate()->GetCurrentContext(), MicrotaskLoopForever)
          .ToLocalChecked());
  // The second task should never be run because we bail out if we're
  // terminating.
  isolate()->EnqueueMicrotask(
      Function::New(isolate()->GetCurrentContext(), MicrotaskShouldNotRun)
          .ToLocalChecked());
  isolate()->PerformMicrotaskCheckpoint();

  isolate()->CancelTerminateExecution();
  // Should not run MicrotaskShouldNotRun.
  isolate()->PerformMicrotaskCheckpoint();

  thread.Join();
  delete semaphore;
  semaphore = nullptr;
}

static int callback_counter = 0;

static void CounterCallback(Isolate* isolate, void* data) {
  callback_counter++;
}

TEST_F(ThreadTerminationTest, PostponeTerminateException) {
  HandleScope scope(isolate());
  Local<ObjectTemplate> global =
      CreateGlobalTemplate(isolate(), TerminateCurrentThread, DoLoop);
  Local<Context> context = Context::New(isolate(), nullptr, global);
  Context::Scope context_scope(context);

  TryCatch try_catch(isolate());
  static const char* terminate_and_loop =
      "terminate(); for (var i = 0; i < 10000; i++);";

  {  // Postpone terminate execution interrupts.
    i::PostponeInterruptsScope p1(i_isolate(),
                                  i::StackGuard::TERMINATE_EXECUTION);

    // API interrupts should still be triggered.
    isolate()->RequestInterrupt(&CounterCallback, nullptr);
    CHECK_EQ(0, callback_counter);
    RunJS(terminate_and_loop);
    CHECK(!try_catch.HasTerminated());
    CHECK_EQ(1, callback_counter);

    {  // Postpone API interrupts as well.
      i::PostponeInterruptsScope p2(i_isolate(), i::StackGuard::API_INTERRUPT);

      // None of the two interrupts should trigger.
      isolate()->RequestInterrupt(&CounterCallback, nullptr);
      RunJS(terminate_and_loop);
      CHECK(!try_catch.HasTerminated());
      CHECK_EQ(1, callback_counter);
    }

    // Now the previously requested API interrupt should trigger.
    RunJS(terminate_and_loop);
    CHECK(!try_catch.HasTerminated());
    CHECK_EQ(2, callback_counter);
  }

  // Now the previously requested terminate execution interrupt should trigger.
  TryRunJS("for (var i = 0; i < 10000; i++);");
  CHECK(try_catch.HasTerminated());
  CHECK_EQ(2, callback_counter);
}

static void AssertFinishedCodeRun(Isolate* isolate) {
  TryCatch try_catch(isolate);
  CompileRun(isolate->GetCurrentContext(), "for (var i = 0; i < 10000; i++);");
  CHECK(!try_catch.HasTerminated());
}

void RequestTermianteAndCallAPI(const FunctionCallbackInfo<Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  info.GetIsolate()->TerminateExecution();
  AssertFinishedCodeRun(info.GetIsolate());
}

TEST_F(ThreadTerminationTest, ErrorObjectAfterTermination) {
  HandleScope scope(isolate());
  Local<Context> context = Context::New(isolate());
  Context::Scope context_scope(context);
  isolate()->TerminateExecution();
  Local<Value> error = Exception::Error(NewString("error"));
  CHECK(error->IsNativeError());
}

void InnerTryCallTerminate(const FunctionCallbackInfo<Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  CHECK(!info.GetIsolate()->IsExecutionTerminating());
  Isolate* isolate = info.GetIsolate();
  Local<Object> global = isolate->GetCurrentContext()->Global();
  Local<Function> loop = Local<Function>::Cast(
      global
          ->Get(isolate->GetCurrentContext(),
                String::NewFromUtf8(isolate, "loop").ToLocalChecked())
          .ToLocalChecked());
  i::MaybeHandle<i::Object> exception;
  i::MaybeHandle<i::Object> result = i::Execution::TryCall(
      reinterpret_cast<i::Isolate*>(isolate), Utils::OpenHandle((*loop)),
      Utils::OpenHandle((*global)), 0, nullptr,
      i::Execution::MessageHandling::kReport, &exception);
  CHECK(result.is_null());
  CHECK(exception.is_null());
  // TryCall reschedules the termination exception.
  CHECK(info.GetIsolate()->IsExecutionTerminating());
}

TEST_F(ThreadTerminationTest, TerminationInInnerTryCall) {
  HandleScope scope(isolate());
  Local<ObjectTemplate> global_template =
      CreateGlobalTemplate(isolate(), TerminateCurrentThread, DoLoopNoCall);
  global_template->Set(NewString("inner_try_call_terminate"),
                       FunctionTemplate::New(isolate(), InnerTryCallTerminate));
  Local<Context> context = Context::New(isolate(), nullptr, global_template);
  Context::Scope context_scope(context);
  {
    TryCatch try_catch(isolate());
    TryRunJS("inner_try_call_terminate()");
    CHECK(try_catch.HasTerminated());
    // Any further exectutions in this TryCatch scope would fail.
    CHECK(isolate()->IsExecutionTerminating());
  }
  // Leaving the TryCatch cleared the termination exception.
  Maybe<int32_t> result =
      RunJS("2 + 2")->Int32Value(isolate()->GetCurrentContext());
  CHECK_EQ(4, result.FromJust());
  CHECK(!isolate()->IsExecutionTerminating());
}

TEST_F(ThreadTerminationTest, TerminateAndTryCall) {
  i::v8_flags.allow_natives_syntax = true;
  HandleScope scope(isolate());
  Local<ObjectTemplate> global = CreateGlobalTemplate(
      isolate(), TerminateCurrentThread, DoLoopCancelTerminate);
  Local<Context> context = Context::New(isolate(), nullptr, global);
  Context::Scope context_scope(context);
  CHECK(!isolate()->IsExecutionTerminating());
  {
    TryCatch try_catch(isolate());
    CHECK(!isolate()->IsExecutionTerminating());
    // Terminate execution has been triggered inside TryCall, but re-requested
    // to trigger later.
    CHECK(TryRunJS("terminate(); reference_error();").IsEmpty());
    CHECK(try_catch.HasCaught());
    CHECK(!isolate()->IsExecutionTerminating());
    Local<Value> value =
        context->Global()
            ->Get(isolate()->GetCurrentContext(), NewString("terminate"))
            .ToLocalChecked();
    CHECK(value->IsFunction());
    // Any further executions in this TryCatch scope fail.
    CHECK(!isolate()->IsExecutionTerminating());
    CHECK(TryRunJS("1 + 1").IsEmpty());
    CHECK(isolate()->IsExecutionTerminating());
  }
  // Leaving the TryCatch cleared the termination exception.
  Maybe<int32_t> result =
      RunJS("2 + 2")->Int32Value(isolate()->GetCurrentContext());
  CHECK_EQ(4, result.FromJust());
  CHECK(!isolate()->IsExecutionTerminating());
}

class ConsoleImpl : public debug::ConsoleDelegate {
 private:
  void Log(const debug::ConsoleCallArguments& info,
           const debug::ConsoleContext&) override {
    CompileRun(Isolate::GetCurrent()->GetCurrentContext(), "1 + 1");
  }
};

TEST_F(ThreadTerminationTest, TerminateConsole) {
  i::v8_flags.allow_natives_syntax = true;
  ConsoleImpl console;
  debug::SetConsoleDelegate(isolate(), &console);
  HandleScope scope(isolate());
  Local<ObjectTemplate> global = CreateGlobalTemplate(
      isolate(), TerminateCurrentThread, DoLoopCancelTerminate);
  Local<Context> context = Context::New(isolate(), nullptr, global);
  Context::Scope context_scope(context);
  {
    // setup console global.
    HandleScope scope(isolate());
    Local<String> name = String::NewFromUtf8Literal(
        isolate(), "console", NewStringType::kInternalized);
    Local<Value> console =
        context->GetExtrasBindingObject()->Get(context, name).ToLocalChecked();
    context->Global()->Set(context, name, console).FromJust();
  }

  CHECK(!isolate()->IsExecutionTerminating());
  TryCatch try_catch(isolate());
  CHECK(!isolate()->IsExecutionTerminating());
  CHECK(TryRunJS("terminate(); console.log(); fail();").IsEmpty());
  CHECK(try_catch.HasCaught());
  CHECK(isolate()->IsExecutionTerminating());
}

TEST_F(ThreadTerminationTest, TerminationClearArrayJoinStack) {
  internal::v8_flags.allow_natives_syntax = true;
  HandleScope scope(isolate());
  Local<ObjectTemplate> global_template =
      CreateGlobalTemplate(isolate(), TerminateCurrentThread, DoLoopNoCall);
  {
    Local<Context> context = Context::New(isolate(), nullptr, global_template);
    Context::Scope context_scope(context);
    {
      TryCatch try_catch(isolate());
      TryRunJS(
          "var error = false;"
          "var a = [{toString(){if(error)loop()}}];"
          "function Join(){ return a.join();}; "
          "%PrepareFunctionForOptimization(Join);"
          "Join();"
          "%OptimizeFunctionOnNextCall(Join);"
          "error = true;"
          "Join();");
      CHECK(try_catch.HasTerminated());
      CHECK(isolate()->IsExecutionTerminating());
    }
    EXPECT_THAT(RunJS("a[0] = 1; Join();"), testing::IsString("1"));
  }
  {
    Local<Context> context = Context::New(isolate(), nullptr, global_template);
    Context::Scope context_scope(context);
    {
      TryCatch try_catch(isolate());
      TryRunJS(
          "var a = [{toString(){loop()}}];"
          "function Join(){ return a.join();}; "
          "Join();");
      CHECK(try_catch.HasTerminated());
      CHECK(isolate()->IsExecutionTerminating());
    }
    EXPECT_THAT(RunJS("a[0] = 1; Join();"), testing::IsString("1"));
  }
  {
    ConsoleImpl console;
    debug::SetConsoleDelegate(isolate(), &console);
    HandleScope scope(isolate());
    Local<Context> context = Context::New(isolate(), nullptr, global_template);
    Context::Scope context_scope(context);
    {
      // setup console global.
      HandleScope scope(isolate());
      Local<String> name = String::NewFromUtf8Literal(
          isolate(), "console", NewStringType::kInternalized);
      Local<Value> console = context->GetExtrasBindingObject()
                                 ->Get(context, name)
                                 .ToLocalChecked();
      context->Global()->Set(context, name, console).FromJust();
    }
    CHECK(!isolate()->IsExecutionTerminating());
    {
      TryCatch try_catch(isolate());
      CHECK(!isolate()->IsExecutionTerminating());
      CHECK(TryRunJS("var a = [{toString(){terminate();console.log();fail()}}];"
                     "function Join() {return a.join();}"
                     "Join();")
                .IsEmpty());
      CHECK(try_catch.HasCaught());
      CHECK(isolate()->IsExecutionTerminating());
    }
    EXPECT_THAT(RunJS("a[0] = 1; Join();"), testing::IsString("1"));
  }
}

class TerminatorSleeperThread : public base::Thread {
 public:
  explicit TerminatorSleeperThread(Isolate* isolate, int sleep_ms)
      : Thread(Options("TerminatorSlepperThread")),
        isolate_(isolate),
        sleep_ms_(sleep_ms) {}
  void Run() override {
    base::OS::Sleep(base::TimeDelta::FromMilliseconds(sleep_ms_));
    isolate_->TerminateExecution();
  }

 private:
  Isolate* isolate_;
  int sleep_ms_;
};

TEST_F(ThreadTerminationTest, TerminateRegExp) {
  i::v8_flags.allow_natives_syntax = true;
  // We want to be stuck regexp execution, so no fallback to linear-time
  // engine.
  // TODO(mbid,v8:10765): Find a way to test interrupt support of the
  // experimental engine.
  i::v8_flags.enable_experimental_regexp_engine_on_excessive_backtracks = false;

  HandleScope scope(isolate());
  Local<ObjectTemplate> global = CreateGlobalTemplate(
      isolate(), TerminateCurrentThread, DoLoopCancelTerminate);
  Local<Context> context = Context::New(isolate(), nullptr, global);
  Context::Scope context_scope(context);
  CHECK(!isolate()->IsExecutionTerminating());
  TryCatch try_catch(isolate());
  CHECK(!isolate()->IsExecutionTerminating());
  CHECK(!RunJS("var re = /(x+)+y$/; re.test('x');").IsEmpty());
  CHECK(!isolate()->IsExecutionTerminating());
  TerminatorSleeperThread terminator(isolate(), 100);
  CHECK(terminator.Start());
  CHECK(TryRunJS("re.test('xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx'); fail();")
            .IsEmpty());
  CHECK(try_catch.HasCaught());
  CHECK(isolate()->IsExecutionTerminating());
}

TEST_F(ThreadTerminationTest, TerminateInMicrotask) {
  Locker locker(isolate());
  isolate()->SetMicrotasksPolicy(MicrotasksPolicy::kExplicit);
  HandleScope scope(isolate());
  Local<ObjectTemplate> global = CreateGlobalTemplate(
      isolate(), TerminateCurrentThread, DoLoopCancelTerminate);
  Local<Context> context1 = Context::New(isolate(), nullptr, global);
  Local<Context> context2 = Context::New(isolate(), nullptr, global);
  {
    TryCatch try_catch(isolate());
    {
      Context::Scope context_scope(context1);
      CHECK(!isolate()->IsExecutionTerminating());
      CHECK(!RunJS("Promise.resolve().then(function() {"
                   "terminate(); loop(); fail();})")
                 .IsEmpty());
      CHECK(!try_catch.HasCaught());
    }
    {
      Context::Scope context_scope(context2);
      CHECK(context2 == isolate()->GetCurrentContext());
      CHECK(context2 == isolate()->GetEnteredOrMicrotaskContext());
      CHECK(!isolate()->IsExecutionTerminating());
      isolate()->PerformMicrotaskCheckpoint();
      CHECK(context2 == isolate()->GetCurrentContext());
      CHECK(context2 == isolate()->GetEnteredOrMicrotaskContext());
      CHECK(try_catch.HasCaught());
      CHECK(try_catch.HasTerminated());
      // Any further exectutions in this TryCatch scope would fail.
      CHECK(isolate()->IsExecutionTerminating());
      CHECK(!i_isolate()->stack_guard()->CheckTerminateExecution());
    }
  }
  CHECK(!i_isolate()->stack_guard()->CheckTerminateExecution());
  CHECK(!isolate()->IsExecutionTerminating());
}

void TerminationMicrotask(void* data) {
  Isolate::GetCurrent()->TerminateExecution();
  CompileRun(Isolate::GetCurrent()->GetCurrentContext(), "");
}

void UnreachableMicrotask(void* data) { UNREACHABLE(); }

TEST_F(ThreadTerminationTest, TerminateInApiMicrotask) {
  Locker locker(isolate());
  isolate()->SetMicrotasksPolicy(MicrotasksPolicy::kExplicit);
  HandleScope scope(isolate());
  Local<ObjectTemplate> global = CreateGlobalTemplate(
      isolate(), TerminateCurrentThread, DoLoopCancelTerminate);
  Local<Context> context = Context::New(isolate(), nullptr, global);
  {
    TryCatch try_catch(isolate());
    Context::Scope context_scope(context);
    CHECK(!isolate()->IsExecutionTerminating());
    isolate()->EnqueueMicrotask(TerminationMicrotask);
    isolate()->EnqueueMicrotask(UnreachableMicrotask);
    isolate()->PerformMicrotaskCheckpoint();
    CHECK(try_catch.HasCaught());
    CHECK(try_catch.HasTerminated());
    CHECK(isolate()->IsExecutionTerminating());
  }
  CHECK(!isolate()->IsExecutionTerminating());
}

}  // namespace v8
