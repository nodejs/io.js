// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>

#include "include/v8-function.h"
#include "src/api/api-inl.h"
#include "src/codegen/compiler.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/pipeline.h"
#include "src/debug/debug.h"
#include "src/handles/handles.h"
#include "src/logging/counters.h"
#include "src/objects/js-function.h"
#include "src/objects/shared-function-info.h"
#include "src/utils/utils-inl.h"
#include "src/zone/zone.h"
#include "test/cctest/cctest.h"
#include "test/common/flag-utils.h"

namespace v8 {
namespace internal {
namespace compiler {

enum class SfiState {
  Compiled,
  DebugInfo,
  PreparedForDebugExecution,
};

void ExpectSharedFunctionInfoState(Isolate* isolate,
                                   Tagged<SharedFunctionInfo> sfi,
                                   SfiState expectedState) {
  Tagged<Object> function_data = sfi->GetTrustedData(isolate);
  Tagged<HeapObject> script = sfi->script(kAcquireLoad);
  switch (expectedState) {
    case SfiState::Compiled:
      CHECK(IsBytecodeArray(function_data) ||
            (IsCode(function_data) &&
             Cast<Code>(function_data)->kind() == CodeKind::BASELINE));
      CHECK(IsScript(script));
      break;
    case SfiState::DebugInfo: {
      CHECK(IsBytecodeArray(function_data) ||
            (IsCode(function_data) &&
             Cast<Code>(function_data)->kind() == CodeKind::BASELINE));
      CHECK(IsScript(script));
      Tagged<DebugInfo> debug_info = sfi->GetDebugInfo(isolate);
      CHECK(!debug_info->HasInstrumentedBytecodeArray());
      break;
    }
    case SfiState::PreparedForDebugExecution: {
      CHECK(IsBytecodeArray(function_data));
      CHECK(IsScript(script));
      Tagged<DebugInfo> debug_info = sfi->GetDebugInfo(isolate);
      CHECK(debug_info->HasInstrumentedBytecodeArray());
      break;
    }
  }
}

class BackgroundCompilationThread final : public v8::base::Thread {
 public:
  BackgroundCompilationThread(Isolate* isolate,
                              base::Semaphore* sema_execute_start,
                              base::Semaphore* sema_execute_complete,
                              OptimizedCompilationJob* job)
      : base::Thread(base::Thread::Options("BackgroundCompilationThread")),
        isolate_(isolate),
        sema_execute_start_(sema_execute_start),
        sema_execute_complete_(sema_execute_complete),
        job_(job) {}

  void Run() override {
    RuntimeCallStats stats(RuntimeCallStats::kWorkerThread);
    LocalIsolate local_isolate(isolate_, ThreadKind::kBackground);
    sema_execute_start_->Wait();
    const CompilationJob::Status status =
        job_->ExecuteJob(&stats, &local_isolate);
    CHECK_EQ(status, CompilationJob::SUCCEEDED);
    sema_execute_complete_->Signal();
  }

 private:
  Isolate* isolate_;
  base::Semaphore* sema_execute_start_;
  base::Semaphore* sema_execute_complete_;
  OptimizedCompilationJob* job_;
};

TEST(TestConcurrentSharedFunctionInfo) {
  FlagScope<bool> allow_natives_syntax(&i::v8_flags.allow_natives_syntax, true);

  HandleAndZoneScope scope;
  Isolate* isolate = scope.main_isolate();

  Zone zone(isolate->allocator(), ZONE_NAME);
  HandleScope handle_scope(isolate);

  const char* source_code =
      "function f(x, y) { return x + y; }\n"
      "function test(x) { return f(f(1, x), f(x, 1)); }\n"
      "%PrepareFunctionForOptimization(f);\n"
      "%PrepareFunctionForOptimization(test);\n"
      "test(3);\n"
      "test(-9);\n";

  CompileRun(source_code);

  // Get function "test"
  Local<Function> function_test = Local<Function>::Cast(
      CcTest::global()
          ->Get(CcTest::isolate()->GetCurrentContext(), v8_str("test"))
          .ToLocalChecked());
  Handle<JSFunction> test =
      Cast<JSFunction>(v8::Utils::OpenHandle(*function_test));
  Handle<SharedFunctionInfo> test_sfi(test->shared(), isolate);
  DCHECK(test_sfi->HasBytecodeArray());
  IsCompiledScope compiled_scope_test(*test_sfi, isolate);
  JSFunction::EnsureFeedbackVector(isolate, test, &compiled_scope_test);

  // Get function "f"
  Local<Function> function_f = Local<Function>::Cast(
      CcTest::global()
          ->Get(CcTest::isolate()->GetCurrentContext(), v8_str("f"))
          .ToLocalChecked());
  Handle<JSFunction> f = Cast<JSFunction>(v8::Utils::OpenHandle(*function_f));
  Handle<SharedFunctionInfo> f_sfi(f->shared(), isolate);
  DCHECK(f_sfi->HasBytecodeArray());
  OptimizedCompilationInfo f_info(&zone, isolate, f_sfi, f,
                                  CodeKind::TURBOFAN_JS);
  DirectHandle<Code> f_code =
      Pipeline::GenerateCodeForTesting(&f_info, isolate).ToHandleChecked();
  f->UpdateOptimizedCode(isolate, *f_code);
  IsCompiledScope compiled_scope_f(*f_sfi, isolate);
  JSFunction::EnsureFeedbackVector(isolate, f, &compiled_scope_f);

  ExpectSharedFunctionInfoState(isolate, *test_sfi, SfiState::Compiled);

  auto job =
      Pipeline::NewCompilationJob(isolate, test, CodeKind::TURBOFAN_JS, true);

  // Prepare job.
  {
    CompilationHandleScope compilation(isolate, job->compilation_info());
    job->compilation_info()->ReopenAndCanonicalizeHandlesInNewScope(isolate);
    const CompilationJob::Status status = job->PrepareJob(isolate);
    CHECK_EQ(status, CompilationJob::SUCCEEDED);
  }

  // Start a background thread to execute the compilation job.
  base::Semaphore sema_execute_start(0);
  base::Semaphore sema_execute_complete(0);
  BackgroundCompilationThread thread(isolate, &sema_execute_start,
                                     &sema_execute_complete, job.get());
  CHECK(thread.Start());

  sema_execute_start.Signal();
  // Background thread is running, now mess with test's SFI.
  ExpectSharedFunctionInfoState(isolate, *test_sfi, SfiState::Compiled);

  // Compiled ==> DebugInfo
  {
    isolate->debug()->GetOrCreateDebugInfo(test_sfi);
    ExpectSharedFunctionInfoState(isolate, *test_sfi, SfiState::DebugInfo);
  }

  for (int i = 0; i < 100; ++i) {
    // DebugInfo ==> PreparedForDebugExecution
    {
      int breakpoint_id;
      CHECK(isolate->debug()->SetBreakpointForFunction(
          test_sfi, isolate->factory()->empty_string(), &breakpoint_id));
      ExpectSharedFunctionInfoState(isolate, *test_sfi,
                                    SfiState::PreparedForDebugExecution);
    }

    // PreparedForDebugExecution ==> DebugInfo
    {
      Tagged<DebugInfo> debug_info = test_sfi->GetDebugInfo(isolate);
      debug_info->ClearBreakInfo(isolate);
      ExpectSharedFunctionInfoState(isolate, *test_sfi, SfiState::DebugInfo);
    }
  }

  sema_execute_complete.Wait();
  thread.Join();

  // Finalize job.
  {
    // Cannot assert successful completion here since concurrent modifications
    // may have invalidated compilation dependencies (e.g. since the serialized
    // JSFunctionRef no longer matches the actual JSFunction state).
    const CompilationJob::Status status = job->FinalizeJob(isolate);
    if (status == CompilationJob::SUCCEEDED) {
      CHECK(job->compilation_info()->has_bytecode_array());
    } else {
      CHECK_EQ(status, CompilationJob::FAILED);
    }
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
