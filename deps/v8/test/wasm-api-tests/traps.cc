// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/wasm-api-tests/wasm-api-test.h"

#include "src/execution/isolate.h"
#include "src/wasm/c-api.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/wasm-engine.h"

#include <iostream>

namespace v8 {
namespace internal {
namespace wasm {

using ::wasm::Frame;
using ::wasm::Message;

namespace {

own<Trap> FailCallback(void* env, const Val args[], Val results[]) {
  Store* store = reinterpret_cast<Store*>(env);
  Message message = Message::make(std::string("callback abort"));
  return Trap::make(store, message);
}

void ExpectMessage(const char* expected, const Message& message) {
  size_t len = strlen(expected);
  EXPECT_EQ(len, message.size());
  EXPECT_EQ(0, strncmp(expected, message.get(), len));
}

}  // namespace

TEST_F(WasmCapiTest, Traps) {
  ValueType i32_type[] = {kWasmI32};
  FunctionSig sig(1, 0, i32_type);
  uint32_t callback_index =
      builder()->AddImport(base::CStrVector("callback"), &sig);
  uint8_t code[] = {WASM_CALL_FUNCTION0(callback_index)};
  AddExportedFunction(base::CStrVector("callback"), code, sizeof(code), &sig);

  uint8_t code2[] = {WASM_CALL_FUNCTION0(3)};
  AddExportedFunction(base::CStrVector("unreachable"), code2, sizeof(code2),
                      &sig);
  // The first constant is a 4-byte dummy so that the {unreachable} trap
  // has a more interesting offset. This is called by code2.
  uint8_t code3[] = {WASM_I32V_3(0), WASM_UNREACHABLE, WASM_I32V_1(1)};
  AddFunction(code3, sizeof(code3), &sig);

  // Check that traps returned from a C callback are uncatchable in Wasm.
  uint8_t code4[] = {WASM_TRY_CATCH_ALL_T(
      kWasmI32, WASM_CALL_FUNCTION0(callback_index), WASM_I32V(42))};
  AddExportedFunction(base::CStrVector("uncatchable"), code4, sizeof(code4),
                      &sig);

  own<FuncType> func_type =
      FuncType::make(ownvec<ValType>::make(),
                     ownvec<ValType>::make(ValType::make(::wasm::I32)));
  own<Func> cpp_callback = Func::make(store(), func_type.get(), FailCallback,
                                      reinterpret_cast<void*>(store()));
  Extern* imports[] = {cpp_callback.get()};
  Instantiate(imports);

  // Use internal machinery to parse the module to find the function offsets.
  // This makes the test more robust than hardcoding them.
  ModuleResult result =
      DecodeWasmModule(WasmEnabledFeatures::All(), wire_bytes(), false,
                       ModuleOrigin::kWasmOrigin);
  ASSERT_TRUE(result.ok());
  const WasmFunction* func1 = &result.value()->functions[1];
  const WasmFunction* func2 = &result.value()->functions[2];
  const WasmFunction* func3 = &result.value()->functions[3];
  const uint32_t func1_offset = func1->code.offset();
  const uint32_t func2_offset = func2->code.offset();
  const uint32_t func3_offset = func3->code.offset();

  Func* cpp_trapping_func = GetExportedFunction(0);
  own<Trap> cpp_trap = cpp_trapping_func->call();
  EXPECT_NE(nullptr, cpp_trap.get());
  ExpectMessage("Uncaught Error: callback abort", cpp_trap->message());
  own<Frame> frame = cpp_trap->origin();
  EXPECT_TRUE(frame->instance()->same(instance()));
  EXPECT_EQ(1u, frame->func_index());
  EXPECT_EQ(1u, frame->func_offset());
  EXPECT_EQ(func1_offset + frame->func_offset(), frame->module_offset());
  ownvec<Frame> trace = cpp_trap->trace();
  EXPECT_EQ(1u, trace.size());
  frame.reset(trace[0].release());
  EXPECT_TRUE(frame->instance()->same(instance()));
  EXPECT_EQ(1u, frame->func_index());
  EXPECT_EQ(1u, frame->func_offset());
  EXPECT_EQ(func1_offset + frame->func_offset(), frame->module_offset());

  Func* wasm_trapping_func = GetExportedFunction(1);
  own<Trap> wasm_trap = wasm_trapping_func->call();
  EXPECT_NE(nullptr, wasm_trap.get());
  ExpectMessage("Uncaught RuntimeError: unreachable", wasm_trap->message());
  frame = wasm_trap->origin();
  EXPECT_TRUE(frame->instance()->same(instance()));
  EXPECT_EQ(3u, frame->func_index());
  EXPECT_EQ(5u, frame->func_offset());
  EXPECT_EQ(func3_offset + frame->func_offset(), frame->module_offset());
  trace = wasm_trap->trace();
  EXPECT_EQ(2u, trace.size());

  frame.reset(trace[0].release());
  EXPECT_TRUE(frame->instance()->same(instance()));
  EXPECT_EQ(3u, frame->func_index());
  EXPECT_EQ(5u, frame->func_offset());
  EXPECT_EQ(func3_offset + frame->func_offset(), frame->module_offset());

  frame.reset(trace[1].release());
  EXPECT_TRUE(frame->instance()->same(instance()));
  EXPECT_EQ(2u, frame->func_index());
  EXPECT_EQ(1u, frame->func_offset());
  EXPECT_EQ(func2_offset + frame->func_offset(), frame->module_offset());

  Func* wasm_uncatchable_func = GetExportedFunction(2);
  Val* args = nullptr;
  Val results[1] = {Val(3.14)};  // Sentinel value.
  own<Trap> uncatchable_trap = wasm_uncatchable_func->call(args, results);
  EXPECT_NE(nullptr, uncatchable_trap.get());
  EXPECT_EQ(::wasm::F64, results[0].kind());
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
