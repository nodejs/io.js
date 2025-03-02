// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/isolate.h"
#include "src/heap/heap.h"
#include "src/wasm/c-api.h"
#include "test/wasm-api-tests/wasm-api-test.h"

namespace v8 {
namespace internal {
namespace wasm {

namespace {

own<Trap> Stage2(void* env, const vec<Val>& args, vec<Val>& results) {
  printf("Stage2...\n");
  WasmCapiTest* self = reinterpret_cast<WasmCapiTest*>(env);
  Func* stage3 = self->GetExportedFunction(1);
  own<Trap> trap = stage3->call(args, results);
  if (trap) {
    printf("Stage2: got exception: %s\n", trap->message().get());
  } else {
    printf("Stage2: call successful\n");
  }
  return trap;
}

own<Trap> Stage4_GC(void* env, const vec<Val>& args, vec<Val>& results) {
  printf("Stage4...\n");
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(env);
  isolate->heap()->PreciseCollectAllGarbage(GCFlag::kForced,
                                            GarbageCollectionReason::kTesting);
  results[0] = Val::i32(args[0].i32() + 1);
  return nullptr;
}

class WasmCapiCallbacksTest : public WasmCapiTest {
 public:
  WasmCapiCallbacksTest() : WasmCapiTest() {
    // Build the following function:
    // int32 stage1(int32 arg0) { return stage2(arg0); }
    uint32_t stage2_index =
        builder()->AddImport(base::CStrVector("stage2"), wasm_i_i_sig());
    uint8_t code[] = {WASM_CALL_FUNCTION(stage2_index, WASM_LOCAL_GET(0))};
    AddExportedFunction(base::CStrVector("stage1"), code, sizeof(code));

    stage2_ = Func::make(store(), cpp_i_i_sig(), Stage2, this);
  }

  Func* stage2() { return stage2_.get(); }
  void AddExportedFunction(base::Vector<const char> name, uint8_t code[],
                           size_t code_size) {
    WasmCapiTest::AddExportedFunction(name, code, code_size, wasm_i_i_sig());
  }

 private:
  own<Func> stage2_;
};

}  // namespace

TEST_F(WasmCapiCallbacksTest, Trap) {
  // Build the following function:
  // int32 stage3_trap(int32 arg0) { unreachable(); }
  uint8_t code[] = {WASM_UNREACHABLE};
  AddExportedFunction(base::CStrVector("stage3_trap"), code, sizeof(code));

  vec<Extern*> imports = vec<Extern*>::make(stage2());
  Instantiate(imports);
  vec<Val> args = vec<Val>::make(Val::i32(42));
  vec<Val> results = vec<Val>::make_uninitialized(1);
  own<Trap> trap = GetExportedFunction(0)->call(args, results);
  EXPECT_NE(trap, nullptr);
  printf("Stage0: Got trap as expected: %s\n", trap->message().get());
}

TEST_F(WasmCapiCallbacksTest, GC) {
  // Build the following function:
  // int32 stage3_to4(int32 arg0) { return stage4(arg0); }
  uint32_t stage4_index =
      builder()->AddImport(base::CStrVector("stage4"), wasm_i_i_sig());
  uint8_t code[] = {WASM_CALL_FUNCTION(stage4_index, WASM_LOCAL_GET(0))};
  AddExportedFunction(base::CStrVector("stage3_to4"), code, sizeof(code));

  i::Isolate* isolate =
      reinterpret_cast<::wasm::StoreImpl*>(store())->i_isolate();
  own<Func> stage4 = Func::make(store(), cpp_i_i_sig(), Stage4_GC, isolate);
  EXPECT_EQ(cpp_i_i_sig()->params().size(), stage4->type()->params().size());
  EXPECT_EQ(cpp_i_i_sig()->results().size(), stage4->type()->results().size());
  vec<Extern*> imports = vec<Extern*>::make(stage2(), stage4.get());
  Instantiate(imports);
  vec<Val> args = vec<Val>::make(Val::i32(42));
  vec<Val> results = vec<Val>::make_uninitialized(1);
  own<Trap> trap = GetExportedFunction(0)->call(args, results);
  EXPECT_EQ(trap, nullptr);
  EXPECT_EQ(43, results[0].i32());
}

namespace {

own<Trap> FibonacciC(void* env, const vec<Val>& args, vec<Val>& results) {
  int32_t x = args[0].i32();
  if (x == 0 || x == 1) {
    results[0] = Val::i32(x);
    return nullptr;
  }
  WasmCapiTest* self = reinterpret_cast<WasmCapiTest*>(env);
  Func* fibo_wasm = self->GetExportedFunction(0);
  // Aggressively reuse existing arrays. That's maybe not great coding
  // style, but this test intentionally ensures that it works if someone
  // insists on doing it.
  vec<Val> recursive_args = vec<Val>::make(Val::i32(x - 1));
  own<Trap> trap = fibo_wasm->call(recursive_args, results);
  DCHECK_NULL(trap);
  int32_t x1 = results[0].i32();
  recursive_args[0] = Val::i32(x - 2);
  trap = fibo_wasm->call(recursive_args, results);
  DCHECK_NULL(trap);
  int32_t x2 = results[0].i32();
  results[0] = Val::i32(x1 + x2);
  return nullptr;
}

}  // namespace

TEST_F(WasmCapiTest, Recursion) {
  // Build the following function:
  // int32 fibonacci_wasm(int32 arg0) {
  //   if (arg0 == 0) return 0;
  //   if (arg0 == 1) return 1;
  //   return fibonacci_c(arg0 - 1) + fibonacci_c(arg0 - 2);
  // }
  uint32_t fibo_c_index =
      builder()->AddImport(base::CStrVector("fibonacci_c"), wasm_i_i_sig());
  uint8_t code_fibo[] = {
      WASM_IF(WASM_I32_EQ(WASM_LOCAL_GET(0), WASM_ZERO),
              WASM_RETURN(WASM_ZERO)),
      WASM_IF(WASM_I32_EQ(WASM_LOCAL_GET(0), WASM_ONE), WASM_RETURN(WASM_ONE)),
      // Muck with the parameter to ensure callers don't depend on its value.
      WASM_LOCAL_SET(0, WASM_I32_SUB(WASM_LOCAL_GET(0), WASM_ONE)),
      WASM_RETURN(WASM_I32_ADD(
          WASM_CALL_FUNCTION(fibo_c_index, WASM_LOCAL_GET(0)),
          WASM_CALL_FUNCTION(fibo_c_index,
                             WASM_I32_SUB(WASM_LOCAL_GET(0), WASM_ONE))))};
  AddExportedFunction(base::CStrVector("fibonacci_wasm"), code_fibo,
                      sizeof(code_fibo), wasm_i_i_sig());

  own<Func> fibonacci = Func::make(store(), cpp_i_i_sig(), FibonacciC, this);
  vec<Extern*> imports = vec<Extern*>::make(fibonacci.get());
  Instantiate(imports);
  // Enough iterations to make it interesting, few enough to keep it fast.
  vec<Val> args = vec<Val>::make(Val::i32(15));
  vec<Val> results = vec<Val>::make_uninitialized(1);
  own<Trap> result = GetExportedFunction(0)->call(args, results);
  EXPECT_EQ(result, nullptr);
  EXPECT_EQ(610, results[0].i32());
}

namespace {

own<Trap> PlusOne(const vec<Val>& args, vec<Val>& results) {
  int32_t a0 = args[0].i32();
  results[0] = Val::i32(a0 + 1);
  int64_t a1 = args[1].i64();
  results[1] = Val::i64(a1 + 1);
  float a2 = args[2].f32();
  results[2] = Val::f32(a2 + 1);
  double a3 = args[3].f64();
  results[3] = Val::f64(a3 + 1);
  results[4] = Val::ref(args[4].ref()->copy());  // No +1 for Refs.
  return nullptr;
}

own<Trap> PlusOneWithManyArgs(const vec<Val>& args, vec<Val>& results) {
  int32_t a0 = args[0].i32();
  results[0] = Val::i32(a0 + 1);
  int64_t a1 = args[1].i64();
  results[1] = Val::i64(a1 + 1);
  float a2 = args[2].f32();
  results[2] = Val::f32(a2 + 1);
  double a3 = args[3].f64();
  results[3] = Val::f64(a3 + 1);
  results[4] = Val::ref(args[4].ref()->copy());  // No +1 for Refs.
  int32_t a5 = args[5].i32();
  results[5] = Val::i32(a5 + 1);
  int64_t a6 = args[6].i64();
  results[6] = Val::i64(a6 + 1);
  float a7 = args[7].f32();
  results[7] = Val::f32(a7 + 1);
  double a8 = args[8].f64();
  results[8] = Val::f64(a8 + 1);
  int32_t a9 = args[9].i32();
  results[9] = Val::i32(a9 + 1);
  int64_t a10 = args[10].i64();
  results[10] = Val::i64(a10 + 1);
  float a11 = args[11].f32();
  results[11] = Val::f32(a11 + 1);
  double a12 = args[12].f64();
  results[12] = Val::f64(a12 + 1);
  int32_t a13 = args[13].i32();
  results[13] = Val::i32(a13 + 1);
  return nullptr;
}
}  // namespace

TEST_F(WasmCapiTest, DirectCallCapiFunction) {
  own<FuncType> cpp_sig = FuncType::make(
      ownvec<ValType>::make(ValType::make(::wasm::ValKind::I32),
                            ValType::make(::wasm::ValKind::I64),
                            ValType::make(::wasm::ValKind::F32),
                            ValType::make(::wasm::ValKind::F64),
                            ValType::make(::wasm::ValKind::EXTERNREF)),
      ownvec<ValType>::make(ValType::make(::wasm::ValKind::I32),
                            ValType::make(::wasm::ValKind::I64),
                            ValType::make(::wasm::ValKind::F32),
                            ValType::make(::wasm::ValKind::F64),
                            ValType::make(::wasm::ValKind::EXTERNREF)));
  own<Func> func = Func::make(store(), cpp_sig.get(), PlusOne);
  vec<Extern*> imports = vec<Extern*>::make(func.get());
  ValueType wasm_types[] = {kWasmI32,       kWasmI64,      kWasmF32, kWasmF64,
                            kWasmExternRef, kWasmI32,      kWasmI64, kWasmF32,
                            kWasmF64,       kWasmExternRef};
  FunctionSig wasm_sig(5, 5, wasm_types);
  int func_index = builder()->AddImport(base::CStrVector("func"), &wasm_sig);
  builder()->ExportImportedFunction(base::CStrVector("func"), func_index);
  Instantiate(imports);
  int32_t a0 = 42;
  int64_t a1 = 0x1234c0ffee;
  float a2 = 1234.5;
  double a3 = 123.45;
  vec<Val> args = vec<Val>::make(Val::i32(a0), Val::i64(a1), Val::f32(a2),
                                 Val::f64(a3), Val::ref(func->copy()));
  vec<Val> results = vec<Val>::make_uninitialized(5);
  // Test that {func} can be called directly.
  own<Trap> trap = func->call(args, results);
  EXPECT_EQ(nullptr, trap);
  EXPECT_EQ(a0 + 1, results[0].i32());
  EXPECT_EQ(a1 + 1, results[1].i64());
  EXPECT_EQ(a2 + 1, results[2].f32());
  EXPECT_EQ(a3 + 1, results[3].f64());
  EXPECT_TRUE(func->same(results[4].ref()));

  // Test that {func} can be called after import/export round-tripping.
  trap = GetExportedFunction(0)->call(args, results);
  EXPECT_EQ(nullptr, trap);
  EXPECT_EQ(a0 + 1, results[0].i32());
  EXPECT_EQ(a1 + 1, results[1].i64());
  EXPECT_EQ(a2 + 1, results[2].f32());
  EXPECT_EQ(a3 + 1, results[3].f64());
  EXPECT_TRUE(func->same(results[4].ref()));
}

TEST_F(WasmCapiTest, DirectCallCapiFunctionWithManyArgs) {
  // Test with many arguments to make sure that CWasmArgumentsPacker won't use
  // its buffer-on-stack optimization.
  own<FuncType> cpp_sig = FuncType::make(
      ownvec<ValType>::make(ValType::make(::wasm::ValKind::I32),
                            ValType::make(::wasm::ValKind::I64),
                            ValType::make(::wasm::ValKind::F32),
                            ValType::make(::wasm::ValKind::F64),
                            ValType::make(::wasm::ValKind::EXTERNREF),
                            ValType::make(::wasm::ValKind::I32),
                            ValType::make(::wasm::ValKind::I64),
                            ValType::make(::wasm::ValKind::F32),
                            ValType::make(::wasm::ValKind::F64),
                            ValType::make(::wasm::ValKind::I32),
                            ValType::make(::wasm::ValKind::I64),
                            ValType::make(::wasm::ValKind::F32),
                            ValType::make(::wasm::ValKind::F64),
                            ValType::make(::wasm::ValKind::I32)),
      ownvec<ValType>::make(ValType::make(::wasm::ValKind::I32),
                            ValType::make(::wasm::ValKind::I64),
                            ValType::make(::wasm::ValKind::F32),
                            ValType::make(::wasm::ValKind::F64),
                            ValType::make(::wasm::ValKind::EXTERNREF),
                            ValType::make(::wasm::ValKind::I32),
                            ValType::make(::wasm::ValKind::I64),
                            ValType::make(::wasm::ValKind::F32),
                            ValType::make(::wasm::ValKind::F64),
                            ValType::make(::wasm::ValKind::I32),
                            ValType::make(::wasm::ValKind::I64),
                            ValType::make(::wasm::ValKind::F32),
                            ValType::make(::wasm::ValKind::F64),
                            ValType::make(::wasm::ValKind::I32)));
  own<Func> func = Func::make(store(), cpp_sig.get(), PlusOneWithManyArgs);
  vec<Extern*> imports = vec<Extern*>::make(func.get());
  ValueType wasm_types[] = {
      kWasmI32,       kWasmI64, kWasmF32, kWasmF64, kWasmExternRef, kWasmI32,
      kWasmI64,       kWasmF32, kWasmF64, kWasmI32, kWasmI64,       kWasmF32,
      kWasmF64,       kWasmI32, kWasmI32, kWasmI64, kWasmF32,       kWasmF64,
      kWasmExternRef, kWasmI32, kWasmI64, kWasmF32, kWasmF64,       kWasmI32,
      kWasmI64,       kWasmF32, kWasmF64, kWasmI32};
  FunctionSig wasm_sig(14, 14, wasm_types);
  int func_index = builder()->AddImport(base::CStrVector("func"), &wasm_sig);
  builder()->ExportImportedFunction(base::CStrVector("func"), func_index);
  Instantiate(imports);
  int32_t a0 = 42;
  int64_t a1 = 0x1234c0ffee;
  float a2 = 1234.5;
  double a3 = 123.45;
  vec<Val> args =
      vec<Val>::make(Val::i32(a0), Val::i64(a1), Val::f32(a2), Val::f64(a3),
                     Val::ref(func->copy()), Val::i32(a0), Val::i64(a1),
                     Val::f32(a2), Val::f64(a3), Val::i32(a0), Val::i64(a1),
                     Val::f32(a2), Val::f64(a3), Val::i32(a0));
  vec<Val> results = vec<Val>::make_uninitialized(14);
  // Test that {func} can be called directly.
  own<Trap> trap = func->call(args, results);
  EXPECT_EQ(nullptr, trap);
  EXPECT_EQ(a0 + 1, results[0].i32());
  EXPECT_EQ(a1 + 1, results[1].i64());
  EXPECT_EQ(a2 + 1, results[2].f32());
  EXPECT_EQ(a3 + 1, results[3].f64());
  EXPECT_TRUE(func->same(results[4].ref()));
  EXPECT_EQ(a0 + 1, results[5].i32());
  EXPECT_EQ(a1 + 1, results[6].i64());
  EXPECT_EQ(a2 + 1, results[7].f32());
  EXPECT_EQ(a3 + 1, results[8].f64());
  EXPECT_EQ(a0 + 1, results[9].i32());
  EXPECT_EQ(a1 + 1, results[10].i64());
  EXPECT_EQ(a2 + 1, results[11].f32());
  EXPECT_EQ(a3 + 1, results[12].f64());
  EXPECT_EQ(a0 + 1, results[13].i32());

  // Test that {func} can be called after import/export round-tripping.
  trap = GetExportedFunction(0)->call(args, results);
  EXPECT_EQ(nullptr, trap);
  EXPECT_EQ(a0 + 1, results[0].i32());
  EXPECT_EQ(a1 + 1, results[1].i64());
  EXPECT_EQ(a2 + 1, results[2].f32());
  EXPECT_EQ(a3 + 1, results[3].f64());
  EXPECT_TRUE(func->same(results[4].ref()));
  EXPECT_EQ(a0 + 1, results[5].i32());
  EXPECT_EQ(a1 + 1, results[6].i64());
  EXPECT_EQ(a2 + 1, results[7].f32());
  EXPECT_EQ(a3 + 1, results[8].f64());
  EXPECT_EQ(a0 + 1, results[9].i32());
  EXPECT_EQ(a1 + 1, results[10].i64());
  EXPECT_EQ(a2 + 1, results[11].f32());
  EXPECT_EQ(a3 + 1, results[12].f64());
  EXPECT_EQ(a0 + 1, results[13].i32());
}
}  // namespace wasm
}  // namespace internal
}  // namespace v8
