// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/wasm-api-tests/wasm-api-test.h"

namespace v8 {
namespace internal {
namespace wasm {

namespace {

own<Trap> DummyCallback(const Val args[], Val results[]) { return nullptr; }

}  // namespace

TEST_F(WasmCapiTest, StartupErrors) {
  FunctionSig sig(0, 0, nullptr);
  uint8_t code[] = {WASM_UNREACHABLE};
  WasmFunctionBuilder* start_func = builder()->AddFunction(&sig);
  start_func->EmitCode(code, static_cast<uint32_t>(sizeof(code)));
  start_func->Emit(kExprEnd);
  builder()->MarkStartFunction(start_func);
  builder()->AddImport(base::CStrVector("dummy"), &sig);
  Compile();
  own<Trap> trap;

  // Try to make an Instance with non-matching imports.
  own<Func> bad_func = Func::make(store(), cpp_i_i_sig(), DummyCallback);
  Extern* bad_imports[] = {bad_func.get()};
  own<Instance> instance =
      Instance::make(store(), module(), bad_imports, &trap);
  EXPECT_EQ(nullptr, instance);
  EXPECT_NE(nullptr, trap);
  EXPECT_STREQ(
      "Uncaught LinkError: instantiation: Import #0 \"\" \"dummy\": "
      "imported function does not match the expected type",
      trap->message().get());
  EXPECT_EQ(nullptr, trap->origin());
  // Don't crash if there is no {trap}.
  instance = Instance::make(store(), module(), bad_imports, nullptr);
  EXPECT_EQ(nullptr, instance);

  // Try to make an instance with a {start} function that traps.
  own<FuncType> good_sig =
      FuncType::make(ownvec<ValType>::make(), ownvec<ValType>::make());
  own<Func> good_func = Func::make(store(), good_sig.get(), DummyCallback);
  Extern* good_imports[] = {good_func.get()};
  instance = Instance::make(store(), module(), good_imports, &trap);
  EXPECT_EQ(nullptr, instance);
  EXPECT_NE(nullptr, trap);
  EXPECT_STREQ("Uncaught RuntimeError: unreachable", trap->message().get());
  EXPECT_NE(nullptr, trap->origin());
  // Don't crash if there is no {trap}.
  instance = Instance::make(store(), module(), good_imports, nullptr);
  EXPECT_EQ(nullptr, instance);
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
