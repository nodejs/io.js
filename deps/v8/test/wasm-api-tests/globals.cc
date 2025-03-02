// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/wasm-api-tests/wasm-api-test.h"

namespace v8 {
namespace internal {
namespace wasm {

using ::wasm::GlobalType;

TEST_F(WasmCapiTest, Globals) {
  const bool kMutable = true;
  const bool kImmutable = false;

  // Define imported and exported globals in the module.
  const uint32_t cfi_index = builder()->AddGlobalImport(
      base::CStrVector("const f32"), kWasmF32, kImmutable);
  const uint32_t cii_index = builder()->AddGlobalImport(
      base::CStrVector("const i64"), kWasmI64, kImmutable);
  const uint32_t vfi_index = builder()->AddGlobalImport(
      base::CStrVector("var f32"), kWasmF32, kMutable);
  const uint32_t vii_index = builder()->AddGlobalImport(
      base::CStrVector("var i64"), kWasmI64, kMutable);
  const int kNumImported = 4;

  const uint32_t cfe_index =
      kNumImported +
      builder()->AddExportedGlobal(kWasmF32, kImmutable, WasmInitExpr(5.f),
                                   base::CStrVector("const f32"));
  const uint32_t cie_index =
      kNumImported + builder()->AddExportedGlobal(
                         kWasmI64, kImmutable, WasmInitExpr(int64_t{6}),
                         base::CStrVector("const i64"));
  const uint32_t vfe_index =
      kNumImported + builder()->AddExportedGlobal(kWasmF32, kMutable,
                                                  WasmInitExpr(7.f),
                                                  base::CStrVector("var f32"));
  const uint32_t vie_index =
      kNumImported + builder()->AddExportedGlobal(kWasmI64, kMutable,
                                                  WasmInitExpr(int64_t{8}),
                                                  base::CStrVector("var i64"));

  // Define functions for inspecting globals.
  ValueType f32_type[] = {kWasmF32};
  ValueType i64_type[] = {kWasmI64};
  FunctionSig return_f32(1, 0, f32_type);
  FunctionSig return_i64(1, 0, i64_type);
  uint8_t gcfi[] = {WASM_GLOBAL_GET(cfi_index)};
  AddExportedFunction(base::CStrVector("get const f32 import"), gcfi,
                      sizeof(gcfi), &return_f32);
  uint8_t gcii[] = {WASM_GLOBAL_GET(cii_index)};
  AddExportedFunction(base::CStrVector("get const i64 import"), gcii,
                      sizeof(gcii), &return_i64);
  uint8_t gvfi[] = {WASM_GLOBAL_GET(vfi_index)};
  AddExportedFunction(base::CStrVector("get var f32 import"), gvfi,
                      sizeof(gvfi), &return_f32);
  uint8_t gvii[] = {WASM_GLOBAL_GET(vii_index)};
  AddExportedFunction(base::CStrVector("get var i64 import"), gvii,
                      sizeof(gvii), &return_i64);

  uint8_t gcfe[] = {WASM_GLOBAL_GET(cfe_index)};
  AddExportedFunction(base::CStrVector("get const f32 export"), gcfe,
                      sizeof(gcfe), &return_f32);
  uint8_t gcie[] = {WASM_GLOBAL_GET(cie_index)};
  AddExportedFunction(base::CStrVector("get const i64 export"), gcie,
                      sizeof(gcie), &return_i64);
  uint8_t gvfe[] = {WASM_GLOBAL_GET(vfe_index)};
  AddExportedFunction(base::CStrVector("get var f32 export"), gvfe,
                      sizeof(gvfe), &return_f32);
  uint8_t gvie[] = {WASM_GLOBAL_GET(vie_index)};
  AddExportedFunction(base::CStrVector("get var i64 export"), gvie,
                      sizeof(gvie), &return_i64);

  // Define functions for manipulating globals.
  FunctionSig param_f32(0, 1, f32_type);
  FunctionSig param_i64(0, 1, i64_type);
  uint8_t svfi[] = {WASM_GLOBAL_SET(vfi_index, WASM_LOCAL_GET(0))};
  AddExportedFunction(base::CStrVector("set var f32 import"), svfi,
                      sizeof(svfi), &param_f32);
  uint8_t svii[] = {WASM_GLOBAL_SET(vii_index, WASM_LOCAL_GET(0))};
  AddExportedFunction(base::CStrVector("set var i64 import"), svii,
                      sizeof(svii), &param_i64);
  uint8_t svfe[] = {WASM_GLOBAL_SET(vfe_index, WASM_LOCAL_GET(0))};
  AddExportedFunction(base::CStrVector("set var f32 export"), svfe,
                      sizeof(svfe), &param_f32);
  uint8_t svie[] = {WASM_GLOBAL_SET(vie_index, WASM_LOCAL_GET(0))};
  AddExportedFunction(base::CStrVector("set var i64 export"), svie,
                      sizeof(svie), &param_i64);

  // Create imported globals.
  own<GlobalType> const_f32_type = GlobalType::make(
      ValType::make(::wasm::ValKind::F32), ::wasm::Mutability::CONST);
  own<GlobalType> const_i64_type = GlobalType::make(
      ValType::make(::wasm::ValKind::I64), ::wasm::Mutability::CONST);
  own<GlobalType> var_f32_type = GlobalType::make(
      ValType::make(::wasm::ValKind::F32), ::wasm::Mutability::VAR);
  own<GlobalType> var_i64_type = GlobalType::make(
      ValType::make(::wasm::ValKind::I64), ::wasm::Mutability::VAR);
  own<Global> const_f32_import =
      Global::make(store(), const_f32_type.get(), Val::f32(1));
  own<Global> const_i64_import =
      Global::make(store(), const_i64_type.get(), Val::i64(2));
  own<Global> var_f32_import =
      Global::make(store(), var_f32_type.get(), Val::f32(3));
  own<Global> var_i64_import =
      Global::make(store(), var_i64_type.get(), Val::i64(4));
  vec<Extern*> imports =
      vec<Extern*>::make(const_f32_import.get(), const_i64_import.get(),
                         var_f32_import.get(), var_i64_import.get());

  Instantiate(imports);

  // Extract exports.
  size_t i = 0;
  Global* const_f32_export = GetExportedGlobal(i++);
  Global* const_i64_export = GetExportedGlobal(i++);
  Global* var_f32_export = GetExportedGlobal(i++);
  Global* var_i64_export = GetExportedGlobal(i++);
  Func* get_const_f32_import = GetExportedFunction(i++);
  Func* get_const_i64_import = GetExportedFunction(i++);
  Func* get_var_f32_import = GetExportedFunction(i++);
  Func* get_var_i64_import = GetExportedFunction(i++);
  Func* get_const_f32_export = GetExportedFunction(i++);
  Func* get_const_i64_export = GetExportedFunction(i++);
  Func* get_var_f32_export = GetExportedFunction(i++);
  Func* get_var_i64_export = GetExportedFunction(i++);
  Func* set_var_f32_import = GetExportedFunction(i++);
  Func* set_var_i64_import = GetExportedFunction(i++);
  Func* set_var_f32_export = GetExportedFunction(i++);
  Func* set_var_i64_export = GetExportedFunction(i++);

  // Try cloning.
  EXPECT_TRUE(var_f32_import->copy()->same(var_f32_import.get()));

  // Check initial values.
  EXPECT_EQ(1.f, const_f32_import->get().f32());
  EXPECT_EQ(2, const_i64_import->get().i64());
  EXPECT_EQ(3.f, var_f32_import->get().f32());
  EXPECT_EQ(4, var_i64_import->get().i64());
  EXPECT_EQ(5.f, const_f32_export->get().f32());
  EXPECT_EQ(6, const_i64_export->get().i64());
  EXPECT_EQ(7.f, var_f32_export->get().f32());
  EXPECT_EQ(8, var_i64_export->get().i64());
  vec<Val> result = vec<Val>::make_uninitialized(1);
  vec<Val> empty_args = vec<Val>::make_uninitialized();

  get_const_f32_import->call(empty_args, result);
  EXPECT_EQ(1.f, result[0].f32());
  get_const_i64_import->call(empty_args, result);
  EXPECT_EQ(2, result[0].i64());
  get_var_f32_import->call(empty_args, result);
  EXPECT_EQ(3.f, result[0].f32());
  get_var_i64_import->call(empty_args, result);
  EXPECT_EQ(4, result[0].i64());
  get_const_f32_export->call(empty_args, result);
  EXPECT_EQ(5.f, result[0].f32());
  get_const_i64_export->call(empty_args, result);
  EXPECT_EQ(6, result[0].i64());
  get_var_f32_export->call(empty_args, result);
  EXPECT_EQ(7.f, result[0].f32());
  get_var_i64_export->call(empty_args, result);
  EXPECT_EQ(8, result[0].i64());

  // Modify variables through the API and check again.
  var_f32_import->set(Val::f32(33));
  var_i64_import->set(Val::i64(34));
  var_f32_export->set(Val::f32(35));
  var_i64_export->set(Val::i64(36));

  EXPECT_EQ(33.f, var_f32_import->get().f32());
  EXPECT_EQ(34, var_i64_import->get().i64());
  EXPECT_EQ(35.f, var_f32_export->get().f32());
  EXPECT_EQ(36, var_i64_export->get().i64());

  get_var_f32_import->call(empty_args, result);
  EXPECT_EQ(33.f, result[0].f32());
  get_var_i64_import->call(empty_args, result);
  EXPECT_EQ(34, result[0].i64());
  get_var_f32_export->call(empty_args, result);
  EXPECT_EQ(35.f, result[0].f32());
  get_var_i64_export->call(empty_args, result);
  EXPECT_EQ(36, result[0].i64());

  // Modify variables through calls and check again.
  vec<Val> empty_rets = vec<Val>::make_uninitialized();
  vec<Val> args = vec<Val>::make_uninitialized(1);
  args[0] = Val::f32(73);
  set_var_f32_import->call(args, empty_rets);
  args[0] = Val::i64(74);
  set_var_i64_import->call(args, empty_rets);
  args[0] = Val::f32(75);
  set_var_f32_export->call(args, empty_rets);
  args[0] = Val::i64(76);
  set_var_i64_export->call(args, empty_rets);

  EXPECT_EQ(73.f, var_f32_import->get().f32());
  EXPECT_EQ(74, var_i64_import->get().i64());
  EXPECT_EQ(75.f, var_f32_export->get().f32());
  EXPECT_EQ(76, var_i64_export->get().i64());

  get_var_f32_import->call(empty_args, result);
  EXPECT_EQ(73.f, result[0].f32());
  get_var_i64_import->call(empty_args, result);
  EXPECT_EQ(74, result[0].i64());
  get_var_f32_export->call(empty_args, result);
  EXPECT_EQ(75.f, result[0].f32());
  get_var_i64_export->call(empty_args, result);
  EXPECT_EQ(76, result[0].i64());
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
