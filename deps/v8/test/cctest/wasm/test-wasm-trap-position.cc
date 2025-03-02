// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-function.h"
#include "src/api/api-inl.h"
#include "src/codegen/assembler-inl.h"
#include "src/objects/call-site-info-inl.h"
#include "src/trap-handler/trap-handler.h"
#include "test/cctest/cctest.h"
#include "test/cctest/wasm/wasm-run-utils.h"
#include "test/common/value-helper.h"
#include "test/common/wasm/test-signatures.h"
#include "test/common/wasm/wasm-macro-gen.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace test_wasm_trap_position {

using v8::Local;
using v8::Utils;

namespace {

#define CHECK_CSTREQ(exp, found)                                              \
  do {                                                                        \
    const char* exp_ = (exp);                                                 \
    const char* found_ = (found);                                             \
    DCHECK_NOT_NULL(exp);                                                     \
    if (V8_UNLIKELY(found_ == nullptr || strcmp(exp_, found_) != 0)) {        \
      FATAL("Check failed: (%s) != (%s) ('%s' vs '%s').", #exp, #found, exp_, \
            found_ ? found_ : "<null>");                                      \
    }                                                                         \
  } while (false)

struct ExceptionInfo {
  const char* func_name;
  int line_nr;
  int column;
};

template <int N>
void CheckExceptionInfos(v8::internal::Isolate* isolate,
                         DirectHandle<Object> exc,
                         const ExceptionInfo (&excInfos)[N]) {
  // Check that it's indeed an Error object.
  CHECK(IsJSError(*exc));

  Print(*exc);
  // Extract stack frame from the exception.
  auto stack = isolate->GetSimpleStackTrace(Cast<JSObject>(exc));
  CHECK_EQ(N, stack->length());

  for (int i = 0; i < N; ++i) {
    DirectHandle<CallSiteInfo> info(Cast<CallSiteInfo>(stack->get(i)), isolate);
    auto func_name =
        Cast<String>(CallSiteInfo::GetFunctionName(info))->ToCString();
    CHECK_CSTREQ(excInfos[i].func_name, func_name.get());
    CHECK_EQ(excInfos[i].line_nr, CallSiteInfo::GetLineNumber(info));
    CHECK_EQ(excInfos[i].column, CallSiteInfo::GetColumnNumber(info));
  }
}

#undef CHECK_CSTREQ

}  // namespace

// Trigger a trap for executing unreachable.
WASM_COMPILED_EXEC_TEST(Unreachable) {
  // Create a WasmRunner with stack checks and traps enabled.
  WasmRunner<void> r(execution_tier, kWasmOrigin, nullptr, "main");

  r.Build({WASM_UNREACHABLE});
  uint32_t wasm_index = r.function()->func_index;

  DirectHandle<JSFunction> js_wasm_wrapper = r.builder().WrapCode(wasm_index);

  DirectHandle<JSFunction> js_trampoline =
      Cast<JSFunction>(v8::Utils::OpenHandle(*v8::Local<v8::Function>::Cast(
          CompileRun("(function callFn(fn) { fn(); })"))));

  Isolate* isolate = js_wasm_wrapper->GetIsolate();
  isolate->SetCaptureStackTraceForUncaughtExceptions(true, 10,
                                                     v8::StackTrace::kOverview);
  DirectHandle<Object> global(isolate->context()->global_object(), isolate);
  MaybeDirectHandle<Object> maybe_exc;
  DirectHandle<Object> args[] = {js_wasm_wrapper};
  MaybeDirectHandle<Object> returnObjMaybe =
      Execution::TryCall(isolate, js_trampoline, global, base::VectorOf(args),
                         Execution::MessageHandling::kReport, &maybe_exc);
  CHECK(returnObjMaybe.is_null());

  ExceptionInfo expected_exceptions[] = {
      {"main", 1, 7},    // --
      {"callFn", 1, 24}  // --
  };
  CheckExceptionInfos(isolate, maybe_exc.ToHandleChecked(),
                      expected_exceptions);
}

// Trigger a trap for loading from out-of-bounds.
WASM_COMPILED_EXEC_TEST(IllegalLoad) {
  WasmRunner<void> r(execution_tier, kWasmOrigin, nullptr, "main");

  r.builder().AddMemory(0L);

  r.Build({WASM_IF(
      WASM_ONE, WASM_SEQ(WASM_LOAD_MEM(MachineType::Int32(), WASM_I32V_1(-3)),
                         WASM_DROP))});
  uint32_t wasm_index_1 = r.function()->func_index;

  WasmFunctionCompiler& f2 = r.NewFunction<void>("call_main");
  // Insert a NOP such that the position of the call is not one.
  f2.Build({WASM_NOP, WASM_CALL_FUNCTION0(wasm_index_1)});
  uint32_t wasm_index_2 = f2.function_index();

  DirectHandle<JSFunction> js_wasm_wrapper = r.builder().WrapCode(wasm_index_2);

  DirectHandle<JSFunction> js_trampoline =
      Cast<JSFunction>(v8::Utils::OpenHandle(*v8::Local<v8::Function>::Cast(
          CompileRun("(function callFn(fn) { fn(); })"))));

  Isolate* isolate = js_wasm_wrapper->GetIsolate();
  isolate->SetCaptureStackTraceForUncaughtExceptions(true, 10,
                                                     v8::StackTrace::kOverview);
  DirectHandle<Object> global(isolate->context()->global_object(), isolate);
  MaybeDirectHandle<Object> maybe_exc;
  DirectHandle<Object> args[] = {js_wasm_wrapper};
  MaybeDirectHandle<Object> returnObjMaybe =
      Execution::TryCall(isolate, js_trampoline, global, base::VectorOf(args),
                         Execution::MessageHandling::kReport, &maybe_exc);
  CHECK(returnObjMaybe.is_null());

  ExceptionInfo expected_exceptions[] = {
      {"main", 1, 13},       // --
      {"call_main", 1, 30},  // --
      {"callFn", 1, 24}      // --
  };
  CheckExceptionInfos(isolate, maybe_exc.ToHandleChecked(),
                      expected_exceptions);
}

}  // namespace test_wasm_trap_position
}  // namespace wasm
}  // namespace internal
}  // namespace v8
