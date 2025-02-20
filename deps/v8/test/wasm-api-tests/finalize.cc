// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/wasm-api-tests/wasm-api-test.h"

namespace v8 {
namespace internal {
namespace wasm {

namespace {

int g_instances_finalized = 0;
int g_functions_finalized = 0;
int g_foreigns_finalized = 0;
int g_modules_finalized = 0;

const int kModuleMagic = 42;

void FinalizeInstance(void* data) {
  int iteration = static_cast<int>(reinterpret_cast<intptr_t>(data));
  g_instances_finalized += iteration;
}

void FinalizeFunction(void* data) {
  int iteration = static_cast<int>(reinterpret_cast<intptr_t>(data));
  g_functions_finalized += iteration;
}

void FinalizeForeign(void* data) {
  int iteration = static_cast<int>(reinterpret_cast<intptr_t>(data));
  g_foreigns_finalized += iteration;
}

void FinalizeModule(void* data) {
  g_modules_finalized += static_cast<int>(reinterpret_cast<intptr_t>(data));
}

void RunInStore(Store* store, base::Vector<const uint8_t> wire_bytes,
                int iterations) {
  vec<byte_t> binary = vec<byte_t>::make(
      wire_bytes.size(),
      reinterpret_cast<byte_t*>(const_cast<uint8_t*>(wire_bytes.begin())));
  own<Module> module = Module::make(store, binary);
  module->set_host_info(reinterpret_cast<void*>(kModuleMagic), &FinalizeModule);
  for (int iteration = 0; iteration < iterations; iteration++) {
    void* finalizer_data = reinterpret_cast<void*>(iteration);
    vec<Extern*> imports = vec<Extern*>::make_uninitialized();
    own<Instance> instance =
        Instance::make(store, module.get(), imports, nullptr);
    EXPECT_NE(nullptr, instance.get());
    instance->set_host_info(finalizer_data, &FinalizeInstance);

    own<Func> func = instance->exports()[0]->func()->copy();
    ASSERT_NE(func, nullptr);
    func->set_host_info(finalizer_data, &FinalizeFunction);
    vec<Val> args = vec<Val>::make(Val::i32(iteration));
    vec<Val> results = vec<Val>::make_uninitialized(1);
    func->call(args, results);
    EXPECT_EQ(iteration, results[0].i32());

    own<Foreign> foreign = Foreign::make(store);
    foreign->set_host_info(finalizer_data, &FinalizeForeign);
  }
}

}  // namespace

TEST_F(WasmCapiTest, InstanceFinalization) {
  // Add a dummy function: f(x) { return x; }
  uint8_t code[] = {WASM_RETURN(WASM_LOCAL_GET(0))};
  AddExportedFunction(base::CStrVector("f"), code, sizeof(code),
                      wasm_i_i_sig());
  Compile();
  g_instances_finalized = 0;
  g_functions_finalized = 0;
  g_foreigns_finalized = 0;
  g_modules_finalized = 0;
  module()->set_host_info(reinterpret_cast<void*>(kModuleMagic),
                          &FinalizeModule);
  static const int kIterations = 10;
  RunInStore(store(), wire_bytes(), kIterations);
  {
    own<Store> store2 = Store::make(engine());
    RunInStore(store2.get(), wire_bytes(), kIterations);
  }
  RunInStore(store(), wire_bytes(), kIterations);
  Shutdown();
  // Verify that (1) all finalizers were called, and (2) they passed the
  // correct host data: the loop above sets {i} as data, and the finalizer
  // callbacks add them all up, so the expected value after three rounds is
  // 3 * sum([0, 1, ..., kIterations - 1]), which per Gauss's formula is:
  static const int kExpected = 3 * ((kIterations * (kIterations - 1)) / 2);
  EXPECT_EQ(g_instances_finalized, kExpected);
  // There are two functions per iteration.
  EXPECT_EQ(g_functions_finalized, kExpected);
  EXPECT_EQ(g_foreigns_finalized, kExpected);
  EXPECT_EQ(g_modules_finalized, 4 * kModuleMagic);
}

namespace {

own<Trap> CapiFunction(void* env, const vec<Val>& args, vec<Val>& results) {
  int offset = static_cast<int>(reinterpret_cast<intptr_t>(env));
  results[0] = Val::i32(offset + args[0].i32());
  return nullptr;
}

int g_host_data_finalized = 0;
int g_capi_function_finalized = 0;

void FinalizeCapiFunction(void* data) {
  int value = static_cast<int>(reinterpret_cast<intptr_t>(data));
  g_capi_function_finalized += value;
}

void FinalizeHostData(void* data) {
  g_host_data_finalized += static_cast<int>(reinterpret_cast<intptr_t>(data));
}

}  // namespace

TEST_F(WasmCapiTest, CapiFunctionLifetimes) {
  uint32_t func_index =
      builder()->AddImport(base::CStrVector("f"), wasm_i_i_sig());
  builder()->ExportImportedFunction(base::CStrVector("f"), func_index);
  Compile();
  own<Instance> instance;
  void* kHostData = reinterpret_cast<void*>(1234);
  int base_summand = 1000;
  {
    // Test that the own<> pointers for Func and FuncType can go out of scope
    // without affecting the ability of the Func to be called later.
    own<FuncType> capi_func_type = FuncType::make(
        ownvec<ValType>::make(ValType::make(::wasm::ValKind::I32)),
        ownvec<ValType>::make(ValType::make(::wasm::ValKind::I32)));
    own<Func> capi_func =
        Func::make(store(), capi_func_type.get(), &CapiFunction,
                   reinterpret_cast<void*>(base_summand));
    vec<Extern*> imports = vec<Extern*>::make(capi_func.get());
    instance = Instance::make(store(), module(), imports);
    // TODO(jkummerow): It may or may not be desirable to be able to set
    // host data even here and have it survive the import/export dance.
    // We are awaiting resolution of the discussion at:
    // https://github.com/WebAssembly/wasm-c-api/issues/100
  }
  {
    ownvec<Extern> exports = instance->exports();
    Func* exported_func = exports[0]->func();
    constexpr int kArg = 123;
    vec<Val> args = vec<Val>::make(Val::i32(kArg));
    vec<Val> results = vec<Val>::make_uninitialized(1);
    exported_func->call(args, results);
    EXPECT_EQ(base_summand + kArg, results[0].i32());
    // Host data should survive destruction of the own<> pointer.
    exported_func->set_host_info(kHostData);
  }
  {
    ownvec<Extern> exports = instance->exports();
    Func* exported_func = exports[0]->func();
    EXPECT_EQ(kHostData, exported_func->get_host_info());
  }

  // Test that a Func can have its own internal metadata, an {env}, and
  // separate {host info}, without any of that interfering with each other.
  g_host_data_finalized = 0;
  g_capi_function_finalized = 0;
  base_summand = 23;
  constexpr int kFinalizerData = 345;
  {
    own<FuncType> capi_func_type = FuncType::make(
        ownvec<ValType>::make(ValType::make(::wasm::ValKind::I32)),
        ownvec<ValType>::make(ValType::make(::wasm::ValKind::I32)));
    own<Func> capi_func = Func::make(
        store(), capi_func_type.get(), &CapiFunction,
        reinterpret_cast<void*>(base_summand), &FinalizeCapiFunction);
    capi_func->set_host_info(reinterpret_cast<void*>(kFinalizerData),
                             &FinalizeHostData);
    constexpr int kArg = 19;
    vec<Val> args = vec<Val>::make(Val::i32(kArg));
    vec<Val> results = vec<Val>::make_uninitialized(1);
    capi_func->call(args, results);
    EXPECT_EQ(base_summand + kArg, results[0].i32());
  }
  instance.reset();
  Shutdown();
  EXPECT_EQ(base_summand, g_capi_function_finalized);
  EXPECT_EQ(kFinalizerData, g_host_data_finalized);
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
