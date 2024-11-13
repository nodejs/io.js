// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-function.h"
#include "src/api/api-inl.h"
#include "test/cctest/wasm/wasm-atomics-utils.h"
#include "test/common/wasm/test-signatures.h"
#include "test/common/wasm/wasm-macro-gen.h"

namespace v8::internal::wasm {

WASM_EXEC_TEST(TryCatchThrow) {
  TestSignatures sigs;
  WasmRunner<uint32_t, uint32_t> r(execution_tier);
  uint8_t except = r.builder().AddException(sigs.v_v());
  constexpr uint32_t kResult0 = 23;
  constexpr uint32_t kResult1 = 42;

  // Build the main test function.
  r.Build({WASM_TRY_CATCH_T(
      kWasmI32,
      WASM_STMTS(WASM_I32V(kResult1),
                 WASM_IF(WASM_I32_EQZ(WASM_LOCAL_GET(0)), WASM_THROW(except))),
      WASM_STMTS(WASM_I32V(kResult0)), except)});

  // Need to call through JS to allow for creation of stack traces.
  r.CheckCallViaJS(kResult0, 0);
  r.CheckCallViaJS(kResult1, 1);
}

WASM_EXEC_TEST(TryCatchThrowWithValue) {
  TestSignatures sigs;
  WasmRunner<uint32_t, uint32_t> r(execution_tier);
  uint8_t except = r.builder().AddException(sigs.v_i());
  constexpr uint32_t kResult0 = 23;
  constexpr uint32_t kResult1 = 42;

  // Build the main test function.
  r.Build({WASM_TRY_CATCH_T(
      kWasmI32,
      WASM_STMTS(WASM_I32V(kResult1),
                 WASM_IF(WASM_I32_EQZ(WASM_LOCAL_GET(0)), WASM_I32V(kResult0),
                         WASM_THROW(except))),
      WASM_STMTS(kExprNop), except)});

  // Need to call through JS to allow for creation of stack traces.
  r.CheckCallViaJS(kResult0, 0);
  r.CheckCallViaJS(kResult1, 1);
}

WASM_EXEC_TEST(TryMultiCatchThrow) {
  TestSignatures sigs;
  WasmRunner<uint32_t, uint32_t> r(execution_tier);
  uint8_t except1 = r.builder().AddException(sigs.v_v());
  uint8_t except2 = r.builder().AddException(sigs.v_v());
  constexpr uint32_t kResult0 = 23;
  constexpr uint32_t kResult1 = 42;
  constexpr uint32_t kResult2 = 51;

  // Build the main test function.
  r.Build(
      {kExprTry, static_cast<uint8_t>((kWasmI32).value_type_code()),
       WASM_STMTS(WASM_I32V(kResult2),
                  WASM_IF(WASM_I32_EQZ(WASM_LOCAL_GET(0)), WASM_THROW(except1)),
                  WASM_IF(WASM_I32_EQ(WASM_LOCAL_GET(0), WASM_I32V(1)),
                          WASM_THROW(except2))),
       kExprCatch, except1, WASM_STMTS(WASM_I32V(kResult0)), kExprCatch,
       except2, WASM_STMTS(WASM_I32V(kResult1)), kExprEnd});

  // Need to call through JS to allow for creation of stack traces.
  r.CheckCallViaJS(kResult0, 0);
  r.CheckCallViaJS(kResult1, 1);
  r.CheckCallViaJS(kResult2, 2);
}

WASM_EXEC_TEST(TryCatchAllThrow) {
  TestSignatures sigs;
  WasmRunner<uint32_t, uint32_t> r(execution_tier);
  uint8_t except = r.builder().AddException(sigs.v_v());
  constexpr uint32_t kResult0 = 23;
  constexpr uint32_t kResult1 = 42;

  // Build the main test function.
  r.Build(
      {kExprTry, static_cast<uint8_t>((kWasmI32).value_type_code()),
       WASM_STMTS(WASM_I32V(kResult1),
                  WASM_IF(WASM_I32_EQZ(WASM_LOCAL_GET(0)), WASM_THROW(except))),
       kExprCatchAll, WASM_I32V(kResult0), kExprEnd});

  // Need to call through JS to allow for creation of stack traces.
  r.CheckCallViaJS(kResult0, 0);
  r.CheckCallViaJS(kResult1, 1);
}

WASM_EXEC_TEST(TryCatchCatchAllThrow) {
  TestSignatures sigs;
  WasmRunner<uint32_t, uint32_t> r(execution_tier);
  uint8_t except1 = r.builder().AddException(sigs.v_v());
  uint8_t except2 = r.builder().AddException(sigs.v_v());
  constexpr uint32_t kResult0 = 23;
  constexpr uint32_t kResult1 = 42;
  constexpr uint32_t kResult2 = 51;

  // Build the main test function.
  r.Build(
      {kExprTry, static_cast<uint8_t>((kWasmI32).value_type_code()),
       WASM_STMTS(WASM_I32V(kResult2),
                  WASM_IF(WASM_I32_EQZ(WASM_LOCAL_GET(0)), WASM_THROW(except1)),
                  WASM_IF(WASM_I32_EQ(WASM_LOCAL_GET(0), WASM_I32V(1)),
                          WASM_THROW(except2))),
       kExprCatch, except1, WASM_I32V(kResult0), kExprCatchAll,
       WASM_I32V(kResult1), kExprEnd});

  // Need to call through JS to allow for creation of stack traces.
  r.CheckCallViaJS(kResult0, 0);
  r.CheckCallViaJS(kResult1, 1);
  r.CheckCallViaJS(kResult2, 2);
}

WASM_EXEC_TEST(TryImplicitRethrow) {
  TestSignatures sigs;
  WasmRunner<uint32_t, uint32_t> r(execution_tier);
  uint8_t except1 = r.builder().AddException(sigs.v_v());
  uint8_t except2 = r.builder().AddException(sigs.v_v());
  constexpr uint32_t kResult0 = 23;
  constexpr uint32_t kResult1 = 42;
  constexpr uint32_t kResult2 = 51;

  // Build the main test function.
  r.Build({WASM_TRY_CATCH_T(
      kWasmI32,
      WASM_TRY_CATCH_T(kWasmI32,
                       WASM_STMTS(WASM_I32V(kResult1),
                                  WASM_IF(WASM_I32_EQZ(WASM_LOCAL_GET(0)),
                                          WASM_THROW(except2))),
                       WASM_STMTS(WASM_I32V(kResult2)), except1),
      WASM_I32V(kResult0), except2)});

  // Need to call through JS to allow for creation of stack traces.
  r.CheckCallViaJS(kResult0, 0);
  r.CheckCallViaJS(kResult1, 1);
}

WASM_EXEC_TEST(TryDelegate) {
  TestSignatures sigs;
  WasmRunner<uint32_t, uint32_t> r(execution_tier);
  uint8_t except = r.builder().AddException(sigs.v_v());
  constexpr uint32_t kResult0 = 23;
  constexpr uint32_t kResult1 = 42;

  // Build the main test function.
  r.Build({WASM_TRY_CATCH_T(
      kWasmI32,
      WASM_TRY_DELEGATE_T(kWasmI32,
                          WASM_STMTS(WASM_I32V(kResult1),
                                     WASM_IF(WASM_I32_EQZ(WASM_LOCAL_GET(0)),
                                             WASM_THROW(except))),
                          0),
      WASM_I32V(kResult0), except)});

  // Need to call through JS to allow for creation of stack traces.
  r.CheckCallViaJS(kResult0, 0);
  r.CheckCallViaJS(kResult1, 1);
}

WASM_EXEC_TEST(TestCatchlessTry) {
  TestSignatures sigs;
  WasmRunner<uint32_t> r(execution_tier);
  uint8_t except = r.builder().AddException(sigs.v_i());
  r.Build({WASM_TRY_CATCH_T(
      kWasmI32,
      WASM_TRY_T(kWasmI32, WASM_STMTS(WASM_I32V(0), WASM_THROW(except))),
      WASM_NOP, except)});
  r.CheckCallViaJS(0);
}

WASM_EXEC_TEST(TryCatchRethrow) {
  TestSignatures sigs;
  WasmRunner<uint32_t, uint32_t> r(execution_tier);
  uint8_t except1 = r.builder().AddException(sigs.v_v());
  uint8_t except2 = r.builder().AddException(sigs.v_v());
  constexpr uint32_t kResult0 = 23;
  constexpr uint32_t kResult1 = 42;
  constexpr uint32_t kUnreachable = 51;

  // Build the main test function.
  r.Build({WASM_TRY_CATCH_CATCH_T(
      kWasmI32,
      WASM_TRY_CATCH_T(
          kWasmI32, WASM_THROW(except2),
          WASM_TRY_CATCH_T(
              kWasmI32, WASM_THROW(except1),
              WASM_STMTS(WASM_I32V(kUnreachable),
                         WASM_IF_ELSE(WASM_I32_EQZ(WASM_LOCAL_GET(0)),
                                      WASM_RETHROW(1), WASM_RETHROW(2))),
              except1),
          except2),
      except1, WASM_I32V(kResult0), except2, WASM_I32V(kResult1))});

  // Need to call through JS to allow for creation of stack traces.
  r.CheckCallViaJS(kResult0, 0);
  r.CheckCallViaJS(kResult1, 1);
}

WASM_EXEC_TEST(TryDelegateToCaller) {
  TestSignatures sigs;
  WasmRunner<uint32_t, uint32_t> r(execution_tier);
  uint8_t except = r.builder().AddException(sigs.v_v());
  constexpr uint32_t kResult0 = 23;
  constexpr uint32_t kResult1 = 42;

  // Build the main test function.
  r.Build({WASM_TRY_CATCH_T(
      kWasmI32,
      WASM_TRY_DELEGATE_T(kWasmI32,
                          WASM_STMTS(WASM_I32V(kResult1),
                                     WASM_IF(WASM_I32_EQZ(WASM_LOCAL_GET(0)),
                                             WASM_THROW(except))),
                          1),
      WASM_I32V(kResult0), except)});

  // Need to call through JS to allow for creation of stack traces.
  constexpr int64_t trap = 0xDEADBEEF;
  r.CheckCallViaJS(trap, 0);
  r.CheckCallViaJS(kResult1, 1);
}

WASM_EXEC_TEST(TryCatchCallDirect) {
  TestSignatures sigs;
  WasmRunner<uint32_t, uint32_t> r(execution_tier);
  uint8_t except = r.builder().AddException(sigs.v_v());
  constexpr uint32_t kResult0 = 23;
  constexpr uint32_t kResult1 = 42;

  // Build a throwing helper function.
  WasmFunctionCompiler& throw_func = r.NewFunction(sigs.i_ii());
  throw_func.Build({WASM_THROW(except)});

  // Build the main test function.
  r.Build({WASM_TRY_CATCH_T(
      kWasmI32,
      WASM_STMTS(
          WASM_I32V(kResult1),
          WASM_IF(WASM_I32_EQZ(WASM_LOCAL_GET(0)),
                  WASM_STMTS(WASM_CALL_FUNCTION(throw_func.function_index(),
                                                WASM_I32V(7), WASM_I32V(9)),
                             WASM_DROP))),
      WASM_STMTS(WASM_I32V(kResult0)), except)});

  // Need to call through JS to allow for creation of stack traces.
  r.CheckCallViaJS(kResult0, 0);
  r.CheckCallViaJS(kResult1, 1);
}

WASM_EXEC_TEST(TryCatchAllCallDirect) {
  TestSignatures sigs;
  WasmRunner<uint32_t, uint32_t> r(execution_tier);
  uint8_t except = r.builder().AddException(sigs.v_v());
  constexpr uint32_t kResult0 = 23;
  constexpr uint32_t kResult1 = 42;

  // Build a throwing helper function.
  WasmFunctionCompiler& throw_func = r.NewFunction(sigs.i_ii());
  throw_func.Build({WASM_THROW(except)});

  // Build the main test function.
  r.Build({WASM_TRY_CATCH_ALL_T(
      kWasmI32,
      WASM_STMTS(
          WASM_I32V(kResult1),
          WASM_IF(WASM_I32_EQZ(WASM_LOCAL_GET(0)),
                  WASM_STMTS(WASM_CALL_FUNCTION(throw_func.function_index(),
                                                WASM_I32V(7), WASM_I32V(9)),
                             WASM_DROP))),
      WASM_STMTS(WASM_I32V(kResult0)))});

  // Need to call through JS to allow for creation of stack traces.
  r.CheckCallViaJS(kResult0, 0);
  r.CheckCallViaJS(kResult1, 1);
}

WASM_EXEC_TEST(TryCatchCallIndirect) {
  TestSignatures sigs;
  WasmRunner<uint32_t, uint32_t> r(execution_tier);
  uint8_t except = r.builder().AddException(sigs.v_v());
  constexpr uint32_t kResult0 = 23;
  constexpr uint32_t kResult1 = 42;

  // Build a throwing helper function.
  WasmFunctionCompiler& throw_func = r.NewFunction(sigs.i_ii());
  throw_func.Build({WASM_THROW(except)});

  // Add an indirect function table.
  uint16_t indirect_function_table[] = {
      static_cast<uint16_t>(throw_func.function_index())};
  r.builder().AddIndirectFunctionTable(indirect_function_table,
                                       arraysize(indirect_function_table));

  // Build the main test function.
  r.Build({WASM_TRY_CATCH_T(
      kWasmI32,
      WASM_STMTS(WASM_I32V(kResult1),
                 WASM_IF(WASM_I32_EQZ(WASM_LOCAL_GET(0)),
                         WASM_STMTS(WASM_CALL_INDIRECT(
                                        throw_func.sig_index(), WASM_I32V(7),
                                        WASM_I32V(9), WASM_LOCAL_GET(0)),
                                    WASM_DROP))),
      WASM_I32V(kResult0), except)});

  // Need to call through JS to allow for creation of stack traces.
  r.CheckCallViaJS(kResult0, 0);
  r.CheckCallViaJS(kResult1, 1);
}

WASM_EXEC_TEST(TryCatchAllCallIndirect) {
  TestSignatures sigs;
  WasmRunner<uint32_t, uint32_t> r(execution_tier);
  uint8_t except = r.builder().AddException(sigs.v_v());
  constexpr uint32_t kResult0 = 23;
  constexpr uint32_t kResult1 = 42;

  // Build a throwing helper function.
  WasmFunctionCompiler& throw_func = r.NewFunction(sigs.i_ii());
  throw_func.Build({WASM_THROW(except)});

  // Add an indirect function table.
  uint16_t indirect_function_table[] = {
      static_cast<uint16_t>(throw_func.function_index())};
  r.builder().AddIndirectFunctionTable(indirect_function_table,
                                       arraysize(indirect_function_table));

  // Build the main test function.
  r.Build({WASM_TRY_CATCH_ALL_T(
      kWasmI32,
      WASM_STMTS(WASM_I32V(kResult1),
                 WASM_IF(WASM_I32_EQZ(WASM_LOCAL_GET(0)),
                         WASM_STMTS(WASM_CALL_INDIRECT(
                                        throw_func.sig_index(), WASM_I32V(7),
                                        WASM_I32V(9), WASM_LOCAL_GET(0)),
                                    WASM_DROP))),
      WASM_I32V(kResult0))});

  // Need to call through JS to allow for creation of stack traces.
  r.CheckCallViaJS(kResult0, 0);
  r.CheckCallViaJS(kResult1, 1);
}

WASM_COMPILED_EXEC_TEST(TryCatchCallExternal) {
  TestSignatures sigs;
  HandleScope scope(CcTest::InitIsolateOnce());
  const char* source = "(function() { throw 'ball'; })";
  Handle<JSFunction> js_function = Cast<JSFunction>(v8::Utils::OpenHandle(
      *v8::Local<v8::Function>::Cast(CompileRun(source))));
  ManuallyImportedJSFunction import = {sigs.i_ii(), js_function};
  WasmRunner<uint32_t, uint32_t> r(execution_tier, kWasmOrigin, &import);
  constexpr uint32_t kResult0 = 23;
  constexpr uint32_t kResult1 = 42;
  constexpr uint32_t kJSFunc = 0;

  // Build the main test function.
  r.Build({WASM_TRY_CATCH_ALL_T(
      kWasmI32,
      WASM_STMTS(WASM_I32V(kResult1),
                 WASM_IF(WASM_I32_EQZ(WASM_LOCAL_GET(0)),
                         WASM_STMTS(WASM_CALL_FUNCTION(kJSFunc, WASM_I32V(7),
                                                       WASM_I32V(9)),
                                    WASM_DROP))),
      WASM_I32V(kResult0))});

  // Need to call through JS to allow for creation of stack traces.
  r.CheckCallViaJS(kResult0, 0);
  r.CheckCallViaJS(kResult1, 1);
}

WASM_COMPILED_EXEC_TEST(TryCatchAllCallExternal) {
  TestSignatures sigs;
  HandleScope scope(CcTest::InitIsolateOnce());
  const char* source = "(function() { throw 'ball'; })";
  Handle<JSFunction> js_function = Cast<JSFunction>(v8::Utils::OpenHandle(
      *v8::Local<v8::Function>::Cast(CompileRun(source))));
  ManuallyImportedJSFunction import = {sigs.i_ii(), js_function};
  WasmRunner<uint32_t, uint32_t> r(execution_tier, kWasmOrigin, &import);
  constexpr uint32_t kResult0 = 23;
  constexpr uint32_t kResult1 = 42;
  constexpr uint32_t kJSFunc = 0;

  // Build the main test function.
  r.Build({WASM_TRY_CATCH_ALL_T(
      kWasmI32,
      WASM_STMTS(WASM_I32V(kResult1),
                 WASM_IF(WASM_I32_EQZ(WASM_LOCAL_GET(0)),
                         WASM_STMTS(WASM_CALL_FUNCTION(kJSFunc, WASM_I32V(7),
                                                       WASM_I32V(9)),
                                    WASM_DROP))),
      WASM_I32V(kResult0))});

  // Need to call through JS to allow for creation of stack traces.
  r.CheckCallViaJS(kResult0, 0);
  r.CheckCallViaJS(kResult1, 1);
}

namespace {

void TestTrapNotCaught(uint8_t* code, size_t code_size,
                       TestExecutionTier execution_tier) {
  TestSignatures sigs;
  WasmRunner<uint32_t> r(execution_tier, kWasmOrigin, nullptr, "main");
  r.builder().AddMemory(kWasmPageSize);
  constexpr uint32_t kResultSuccess = 23;
  constexpr uint32_t kResultCaught = 47;

  // Add an indirect function table.
  const int kTableSize = 2;
  r.builder().AddIndirectFunctionTable(nullptr, kTableSize);

  // Build a trapping helper function.
  WasmFunctionCompiler& trap_func = r.NewFunction(sigs.i_ii());
  trap_func.Build(base::VectorOf(code, code_size));

  // Build the main test function.
  r.Build({WASM_TRY_CATCH_ALL_T(
      kWasmI32,
      WASM_STMTS(WASM_I32V(kResultSuccess),
                 WASM_CALL_FUNCTION(trap_func.function_index(), WASM_I32V(7),
                                    WASM_I32V(9)),
                 WASM_DROP),
      WASM_STMTS(WASM_I32V(kResultCaught)))});

  // Need to call through JS to allow for creation of stack traces.
  r.CheckCallViaJSTraps();
}

}  // namespace

WASM_EXEC_TEST(TryCatchTrapUnreachable) {
  uint8_t code[] = {WASM_UNREACHABLE};
  TestTrapNotCaught(code, arraysize(code), execution_tier);
}

WASM_EXEC_TEST(TryCatchTrapMemOutOfBounds) {
  uint8_t code[] = {WASM_LOAD_MEM(MachineType::Int32(), WASM_I32V_1(-1))};
  TestTrapNotCaught(code, arraysize(code), execution_tier);
}

WASM_EXEC_TEST(TryCatchTrapDivByZero) {
  uint8_t code[] = {WASM_I32_DIVS(WASM_LOCAL_GET(0), WASM_I32V_1(0))};
  TestTrapNotCaught(code, arraysize(code), execution_tier);
}

WASM_EXEC_TEST(TryCatchTrapRemByZero) {
  uint8_t code[] = {WASM_I32_REMS(WASM_LOCAL_GET(0), WASM_I32V_1(0))};
  TestTrapNotCaught(code, arraysize(code), execution_tier);
}

WASM_EXEC_TEST(TryCatchTrapTableFill) {
  int table_index = 0;
  int length = 10;  // OOB.
  int start = 10;   // OOB.
  uint8_t code[] = {
      WASM_TABLE_FILL(table_index, WASM_I32V(length),
                      WASM_REF_NULL(kFuncRefCode), WASM_I32V(start)),
      WASM_I32V_1(42)};
  TestTrapNotCaught(code, arraysize(code), execution_tier);
}

namespace {
// TODO(cleanup): Define in cctest.h and re-use where appropriate.
class IsolateScope {
 public:
  IsolateScope() {
    v8::Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = CcTest::array_buffer_allocator();
    isolate_ = v8::Isolate::New(create_params);
    isolate_->Enter();
  }

  ~IsolateScope() {
    isolate_->Exit();
    isolate_->Dispose();
  }

  v8::Isolate* isolate() { return isolate_; }
  Isolate* i_isolate() { return reinterpret_cast<Isolate*>(isolate_); }

 private:
  v8::Isolate* isolate_;
};
}  // namespace

UNINITIALIZED_WASM_EXEC_TEST(TestStackOverflowNotCaught) {
  TestSignatures sigs;
  // v8_flags.stack_size must be set before isolate initialization.
  FlagScope<int32_t> stack_size(&v8_flags.stack_size, 8);

  IsolateScope isolate_scope;
  LocalContext context(isolate_scope.isolate());

  WasmRunner<uint32_t> r(execution_tier, kWasmOrigin, nullptr, "main",
                         isolate_scope.i_isolate());

  // Build a function that calls itself until stack overflow.
  WasmFunctionCompiler& stack_overflow = r.NewFunction(sigs.v_v());
  stack_overflow.Build({kExprCallFunction,
                        static_cast<uint8_t>(stack_overflow.function_index())});

  // Build the main test function.
  r.Build({WASM_TRY_CATCH_ALL_T(
      kWasmI32,
      WASM_STMTS(WASM_I32V(1), kExprCallFunction,
                 static_cast<uint8_t>(stack_overflow.function_index())),
      WASM_STMTS(WASM_I32V(1)))});

  // Need to call through JS to allow for creation of stack traces.
  r.CheckCallViaJSTraps();
}

}  // namespace v8::internal::wasm
