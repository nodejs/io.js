// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/wasm-api-tests/wasm-api-test.h"

#include "src/execution/isolate.h"
#include "src/heap/heap.h"
#include "src/wasm/c-api.h"

namespace v8 {
namespace internal {
namespace wasm {

using ::wasm::ExportType;
using ::wasm::GlobalType;
using ::wasm::MemoryType;
using ::wasm::TableType;

namespace {

const char* kFuncName = "func1";
const char* kGlobalName = "global2";
const char* kTableName = "table3";
const char* kMemoryName = "memory4";

void ExpectName(const char* expected, const ::wasm::Name& name) {
  size_t len = strlen(expected);
  EXPECT_EQ(len, name.size());
  EXPECT_EQ(0, strncmp(expected, name.get(), len));
}

}  // namespace

TEST_F(WasmCapiTest, Reflect) {
  // Create a module exporting a function, a global, a table, and a memory.
  uint8_t code[] = {WASM_UNREACHABLE};
  ValueType types[] = {kWasmI32, kWasmExternRef, kWasmI32,
                       kWasmI64, kWasmF32,       kWasmF64};
  FunctionSig sig(2, 4, types);
  AddExportedFunction(base::CStrVector(kFuncName), code, sizeof(code), &sig);

  builder()->AddExportedGlobal(kWasmF64, false, WasmInitExpr(0.0),
                               base::CStrVector(kGlobalName));

  builder()->AddTable(kWasmFuncRef, 12, 12);
  builder()->AddExport(base::CStrVector(kTableName), kExternalTable, 0);

  builder()->AddMemory(1);
  builder()->AddExport(base::CStrVector(kMemoryName), kExternalMemory, 0);

  Instantiate(nullptr);

  ownvec<ExportType> export_types = module()->exports();
  const ownvec<Extern>& exports = this->exports();
  EXPECT_EQ(exports.size(), export_types.size());
  EXPECT_EQ(4u, exports.size());
  for (size_t i = 0; i < exports.size(); i++) {
    ::wasm::ExternKind kind = exports[i]->kind();
    const ::wasm::ExternType* extern_type = export_types[i]->type();
    EXPECT_EQ(kind, extern_type->kind());
    if (kind == ::wasm::EXTERN_FUNC) {
      ExpectName(kFuncName, export_types[i]->name());
      const FuncType* type = extern_type->func();
      const ownvec<ValType>& params = type->params();
      EXPECT_EQ(4u, params.size());
      EXPECT_EQ(::wasm::I32, params[0]->kind());
      EXPECT_EQ(::wasm::I64, params[1]->kind());
      EXPECT_EQ(::wasm::F32, params[2]->kind());
      EXPECT_EQ(::wasm::F64, params[3]->kind());
      const ownvec<ValType>& results = type->results();
      EXPECT_EQ(2u, results.size());
      EXPECT_EQ(::wasm::I32, results[0]->kind());
      EXPECT_EQ(::wasm::ANYREF, results[1]->kind());

      const Func* func = exports[i]->func();
      EXPECT_EQ(4u, func->param_arity());
      EXPECT_EQ(2u, func->result_arity());

    } else if (kind == ::wasm::EXTERN_GLOBAL) {
      ExpectName(kGlobalName, export_types[i]->name());
      const GlobalType* type = extern_type->global();
      EXPECT_EQ(::wasm::F64, type->content()->kind());
      EXPECT_EQ(::wasm::CONST, type->mutability());

    } else if (kind == ::wasm::EXTERN_TABLE) {
      ExpectName(kTableName, export_types[i]->name());
      const TableType* type = extern_type->table();
      EXPECT_EQ(::wasm::FUNCREF, type->element()->kind());
      ::wasm::Limits limits = type->limits();
      EXPECT_EQ(12u, limits.min);
      EXPECT_EQ(12u, limits.max);

    } else if (kind == ::wasm::EXTERN_MEMORY) {
      ExpectName(kMemoryName, export_types[i]->name());
      const MemoryType* type = extern_type->memory();
      ::wasm::Limits limits = type->limits();
      EXPECT_EQ(1u, limits.min);
      EXPECT_EQ(std::numeric_limits<uint32_t>::max(), limits.max);

    } else {
      UNREACHABLE();
    }
  }
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
