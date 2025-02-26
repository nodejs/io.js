// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/wasm-api-tests/wasm-api-test.h"

namespace v8 {
namespace internal {
namespace wasm {

using ::wasm::Extern;
using ::wasm::Limits;
using ::wasm::TableType;
using ::wasm::ValKind::FUNCREF;

namespace {

own<Trap> Negate(const vec<Val>& args, vec<Val>& results) {
  results[0] = Val(-args[0].i32());
  return nullptr;
}

void ExpectTrap(const Func* func, int arg1, int arg2) {
  vec<Val> args = vec<Val>::make(Val::i32(arg1), Val::i32(arg2));
  vec<Val> results = vec<Val>::make_uninitialized(1);
  own<Trap> trap = func->call(args, results);
  EXPECT_NE(nullptr, trap);
}

void ExpectResult(int expected, const Func* func, int arg1, int arg2) {
  vec<Val> args = vec<Val>::make(Val::i32(arg1), Val::i32(arg2));
  vec<Val> results = vec<Val>::make_uninitialized(1);
  own<Trap> trap = func->call(args, results);
  EXPECT_EQ(nullptr, trap);
  EXPECT_EQ(expected, results[0].i32());
}

}  // namespace

TEST_F(WasmCapiTest, Table) {
  const uint32_t table_index = builder()->AddTable(kWasmFuncRef, 2, 10);
  builder()->AddExport(base::CStrVector("table"), kExternalTable, table_index);
  const ModuleTypeIndex sig_i_i_index =
      builder()->AddSignature(wasm_i_i_sig(), true);
  ValueType reps[] = {kWasmI32, kWasmI32, kWasmI32};
  FunctionSig call_sig(1, 2, reps);
  uint8_t call_code[] = {
      WASM_CALL_INDIRECT(sig_i_i_index, WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))};
  AddExportedFunction(base::CStrVector("call_indirect"), call_code,
                      sizeof(call_code), &call_sig);
  uint8_t f_code[] = {WASM_LOCAL_GET(0)};
  AddExportedFunction(base::CStrVector("f"), f_code, sizeof(f_code),
                      wasm_i_i_sig());
  uint8_t g_code[] = {WASM_I32V_1(42)};
  AddExportedFunction(base::CStrVector("g"), g_code, sizeof(g_code),
                      wasm_i_i_sig());
  // Set table[1] to {f}, which has function index 1.
  builder()->SetIndirectFunction(
      table_index, 1, 1,
      WasmModuleBuilder::WasmElemSegment::kRelativeToImports);

  vec<Extern*> imports = vec<Extern*>::make_uninitialized();
  Instantiate(imports);

  Table* table = GetExportedTable(0);
  Func* call_indirect = GetExportedFunction(1);
  Func* f = GetExportedFunction(2);
  Func* g = GetExportedFunction(3);
  own<Func> h = Func::make(store(), cpp_i_i_sig(), Negate);

  // Try cloning.
  EXPECT_TRUE(table->copy()->same(table));

  // Check initial table state.
  EXPECT_EQ(2u, table->size());
  EXPECT_EQ(nullptr, table->get(0));
  EXPECT_NE(nullptr, table->get(1));
  ExpectTrap(call_indirect, 0, 0);
  ExpectResult(7, call_indirect, 7, 1);
  ExpectTrap(call_indirect, 0, 2);

  // Mutate table.
  EXPECT_TRUE(table->set(0, g));
  EXPECT_TRUE(table->set(1, nullptr));
  EXPECT_FALSE(table->set(2, f));
  EXPECT_NE(nullptr, table->get(0));
  EXPECT_EQ(nullptr, table->get(1));
  ExpectResult(42, call_indirect, 7, 0);
  ExpectTrap(call_indirect, 0, 1);
  ExpectTrap(call_indirect, 0, 2);

  // Grow table.
  EXPECT_TRUE(table->grow(3));
  EXPECT_EQ(5u, table->size());
  EXPECT_TRUE(table->set(2, f));
  EXPECT_TRUE(table->set(3, h.get()));
  EXPECT_FALSE(table->set(5, nullptr));
  EXPECT_NE(nullptr, table->get(2));
  EXPECT_NE(nullptr, table->get(3));
  EXPECT_EQ(nullptr, table->get(4));
  ExpectResult(5, call_indirect, 5, 2);
  ExpectResult(-6, call_indirect, 6, 3);
  ExpectTrap(call_indirect, 0, 4);
  ExpectTrap(call_indirect, 0, 5);
  EXPECT_TRUE(table->grow(2, f));
  EXPECT_EQ(7u, table->size());
  EXPECT_NE(nullptr, table->get(5));
  EXPECT_NE(nullptr, table->get(6));
  EXPECT_FALSE(table->grow(5));
  EXPECT_TRUE(table->grow(3));
  EXPECT_TRUE(table->grow(0));

  // Create standalone table.
  // TODO(wasm+): Once Wasm allows multiple tables, turn this into import.
  own<TableType> tabletype =
      TableType::make(ValType::make(FUNCREF), Limits(5, 5));
  own<Table> table2 = Table::make(store(), tabletype.get());
  EXPECT_EQ(5u, table2->size());
  EXPECT_FALSE(table2->grow(1));
  EXPECT_TRUE(table2->grow(0));
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
