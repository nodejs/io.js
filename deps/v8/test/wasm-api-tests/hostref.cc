// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "test/wasm-api-tests/wasm-api-test.h"

namespace v8 {
namespace internal {
namespace wasm {

using ::wasm::Frame;
using ::wasm::Message;

namespace {

own<Trap> IdentityCallback(const vec<Val>& args, vec<Val>& results) {
  results[0] = args[0].copy();
  return nullptr;
}

}  // namespace

TEST_F(WasmCapiTest, HostRef) {
  ValueType rr_reps[] = {kWasmExternRef, kWasmExternRef};
  ValueType ri_reps[] = {kWasmExternRef, kWasmI32};
  ValueType ir_reps[] = {kWasmI32, kWasmExternRef};
  // Naming convention: result_params_sig.
  FunctionSig r_r_sig(1, 1, rr_reps);
  FunctionSig v_r_sig(0, 1, rr_reps);
  FunctionSig r_v_sig(1, 0, rr_reps);
  FunctionSig v_ir_sig(0, 2, ir_reps);
  FunctionSig r_i_sig(1, 1, ri_reps);
  uint32_t func_index = builder()->AddImport(base::CStrVector("f"), &r_r_sig);
  const bool kMutable = true;
  uint32_t global_index = builder()->AddExportedGlobal(
      kWasmExternRef, kMutable, WasmInitExpr::RefNullConst(HeapType::kExtern),
      base::CStrVector("global"));
  uint32_t table_index = builder()->AddTable(kWasmExternRef, 10);
  builder()->AddExport(base::CStrVector("table"), kExternalTable, table_index);
  uint8_t global_set_code[] = {
      WASM_GLOBAL_SET(global_index, WASM_LOCAL_GET(0))};
  AddExportedFunction(base::CStrVector("global.set"), global_set_code,
                      sizeof(global_set_code), &v_r_sig);
  uint8_t global_get_code[] = {WASM_GLOBAL_GET(global_index)};
  AddExportedFunction(base::CStrVector("global.get"), global_get_code,
                      sizeof(global_get_code), &r_v_sig);
  uint8_t table_set_code[] = {
      WASM_TABLE_SET(table_index, WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))};
  AddExportedFunction(base::CStrVector("table.set"), table_set_code,
                      sizeof(table_set_code), &v_ir_sig);
  uint8_t table_get_code[] = {WASM_TABLE_GET(table_index, WASM_LOCAL_GET(0))};
  AddExportedFunction(base::CStrVector("table.get"), table_get_code,
                      sizeof(table_get_code), &r_i_sig);
  uint8_t func_call_code[] = {
      WASM_CALL_FUNCTION(func_index, WASM_LOCAL_GET(0))};
  AddExportedFunction(base::CStrVector("func.call"), func_call_code,
                      sizeof(func_call_code), &r_r_sig);

  own<FuncType> func_type = FuncType::make(
      ownvec<ValType>::make(ValType::make(::wasm::ValKind::EXTERNREF)),
      ownvec<ValType>::make(ValType::make(::wasm::ValKind::EXTERNREF)));
  own<Func> callback = Func::make(store(), func_type.get(), IdentityCallback);
  vec<Extern*> imports = vec<Extern*>::make(callback.get());
  Instantiate(imports);

  Global* global = GetExportedGlobal(0);
  Table* table = GetExportedTable(1);
  const Func* global_set = GetExportedFunction(2);
  const Func* global_get = GetExportedFunction(3);
  const Func* table_set = GetExportedFunction(4);
  const Func* table_get = GetExportedFunction(5);
  const Func* func_call = GetExportedFunction(6);

  own<Foreign> host1 = Foreign::make(store());
  own<Foreign> host2 = Foreign::make(store());
  host1->set_host_info(reinterpret_cast<void*>(1));
  host2->set_host_info(reinterpret_cast<void*>(2));

  // Basic checks.
  EXPECT_TRUE(host1->copy()->same(host1.get()));
  EXPECT_TRUE(host2->copy()->same(host2.get()));
  Val val = Val::ref(host1->copy());
  EXPECT_TRUE(val.ref()->copy()->same(host1.get()));
  own<Ref> ref = val.release_ref();
  EXPECT_EQ(nullptr, val.ref());
  EXPECT_TRUE(ref->copy()->same(host1.get()));

  // Interact with the Global.
  vec<Val> args = vec<Val>::make_uninitialized(2);
  vec<Val> results = vec<Val>::make_uninitialized(1);

  vec<Val> empty_args = vec<Val>::make_uninitialized();
  vec<Val> empty_rets = vec<Val>::make_uninitialized();

  own<Trap> trap = global_get->call(empty_args, results);
  EXPECT_EQ(nullptr, trap);
  EXPECT_EQ(nullptr, results[0].release_ref());
  args[0] = Val::ref(host1.get()->copy());
  trap = global_set->call(args, empty_rets);
  EXPECT_EQ(nullptr, trap);
  trap = global_get->call(empty_args, results);
  EXPECT_EQ(nullptr, trap);
  EXPECT_TRUE(results[0].release_ref()->same(host1.get()));
  args[0] = Val::ref(host2.get()->copy());
  trap = global_set->call(args, empty_rets);
  EXPECT_EQ(nullptr, trap);
  trap = global_get->call(empty_args, results);
  EXPECT_EQ(nullptr, trap);
  EXPECT_TRUE(results[0].release_ref()->same(host2.get()));
  args[0] = Val::ref(own<Ref>());
  trap = global_set->call(args, empty_rets);
  EXPECT_EQ(nullptr, trap);
  trap = global_get->call(empty_args, results);
  EXPECT_EQ(nullptr, trap);
  EXPECT_EQ(nullptr, results[0].release_ref());

  EXPECT_EQ(nullptr, global->get().release_ref());
  global->set(Val(host2->copy()));
  trap = global_get->call(empty_args, results);
  EXPECT_EQ(nullptr, trap);
  EXPECT_TRUE(results[0].release_ref()->same(host2.get()));
  EXPECT_TRUE(global->get().release_ref()->same(host2.get()));

  // Interact with the Table.
  args[0] = Val::i32(0);
  trap = table_get->call(args, results);
  EXPECT_EQ(nullptr, trap);
  EXPECT_EQ(nullptr, results[0].release_ref());
  args[0] = Val::i32(1);
  trap = table_get->call(args, results);
  EXPECT_EQ(nullptr, trap);
  EXPECT_EQ(nullptr, results[0].release_ref());
  args[0] = Val::i32(0);
  args[1] = Val::ref(host1.get()->copy());
  trap = table_set->call(args, empty_rets);
  EXPECT_EQ(nullptr, trap);
  args[0] = Val::i32(1);
  args[1] = Val::ref(host2.get()->copy());
  trap = table_set->call(args, empty_rets);
  EXPECT_EQ(nullptr, trap);
  args[0] = Val::i32(0);
  trap = table_get->call(args, results);
  EXPECT_EQ(nullptr, trap);
  EXPECT_TRUE(results[0].release_ref()->same(host1.get()));
  args[0] = Val::i32(1);
  trap = table_get->call(args, results);
  EXPECT_EQ(nullptr, trap);
  EXPECT_TRUE(results[0].release_ref()->same(host2.get()));
  args[0] = Val::i32(0);
  args[1] = Val::ref(own<Ref>());
  trap = table_set->call(args, empty_rets);
  EXPECT_EQ(nullptr, trap);
  trap = table_get->call(args, results);
  EXPECT_EQ(nullptr, trap);
  EXPECT_EQ(nullptr, results[0].release_ref());

  EXPECT_EQ(nullptr, table->get(2));
  table->set(2, host1.get());
  args[0] = Val::i32(2);
  trap = table_get->call(args, results);
  EXPECT_EQ(nullptr, trap);
  EXPECT_TRUE(results[0].release_ref()->same(host1.get()));
  EXPECT_TRUE(table->get(2)->same(host1.get()));

  // Interact with the Function.
  args[0] = Val::ref(own<Ref>());
  trap = func_call->call(args, results);
  EXPECT_EQ(nullptr, trap);
  EXPECT_EQ(nullptr, results[0].release_ref());
  args[0] = Val::ref(host1.get()->copy());
  trap = func_call->call(args, results);
  EXPECT_EQ(nullptr, trap);
  EXPECT_TRUE(results[0].release_ref()->same(host1.get()));
  args[0] = Val::ref(host2.get()->copy());
  trap = func_call->call(args, results);
  EXPECT_EQ(nullptr, trap);
  EXPECT_TRUE(results[0].release_ref()->same(host2.get()));
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
