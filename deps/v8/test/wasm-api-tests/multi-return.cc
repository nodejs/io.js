// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/wasm-api-tests/wasm-api-test.h"

namespace v8 {
namespace internal {
namespace wasm {

using ::wasm::ValKind::I32;
using ::wasm::ValKind::I64;

namespace {

own<Trap> Callback(const vec<Val>& args, vec<Val>& results) {
  results[0] = args[3].copy();
  results[1] = args[1].copy();
  results[2] = args[2].copy();
  results[3] = args[0].copy();
  return nullptr;
}

}  // namespace

TEST_F(WasmCapiTest, MultiReturn) {
  ValueType reps[] = {kWasmI32, kWasmI64, kWasmI64, kWasmI32,
                      kWasmI32, kWasmI64, kWasmI64, kWasmI32};
  FunctionSig sig(4, 4, reps);
  uint32_t func_index = builder()->AddImport(base::CStrVector("f"), &sig);
  uint8_t code[] = {WASM_CALL_FUNCTION(func_index, WASM_LOCAL_GET(0),
                                       WASM_LOCAL_GET(2), WASM_LOCAL_GET(1),
                                       WASM_LOCAL_GET(3))};
  AddExportedFunction(base::CStrVector("g"), code, sizeof(code), &sig);

  ownvec<ValType> types =
      ownvec<ValType>::make(ValType::make(I32), ValType::make(I64),
                            ValType::make(I64), ValType::make(I32));
  own<FuncType> func_type =
      FuncType::make(types.deep_copy(), types.deep_copy());
  own<Func> callback = Func::make(store(), func_type.get(), Callback);
  vec<Extern*> imports = vec<Extern*>::make(callback.get());
  Instantiate(imports);

  Func* run_func = GetExportedFunction(0);
  vec<Val> args =
      vec<Val>::make(Val::i32(1), Val::i64(2), Val::i64(3), Val::i32(4));
  vec<Val> results = vec<Val>::make_uninitialized(4);
  own<Trap> trap = run_func->call(args, results);
  EXPECT_EQ(nullptr, trap);
  EXPECT_EQ(4, results[0].i32());
  EXPECT_EQ(3, results[1].i64());
  EXPECT_EQ(2, results[2].i64());
  EXPECT_EQ(1, results[3].i32());
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
