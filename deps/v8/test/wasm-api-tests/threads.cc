// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/wasm-api-tests/wasm-api-test.h"

#include <mutex>
#include <thread>

namespace v8 {
namespace internal {
namespace wasm {

using ::wasm::Shared;

namespace {

const int kNumThreads = 10;
const int kIterationsPerThread = 3;
int g_traces;

own<Trap> Callback(void* env, const vec<Val>& args, vec<Val>& results) {
  std::lock_guard<std::mutex> lock(*reinterpret_cast<std::mutex*>(env));
  g_traces += args[0].i32();
  return nullptr;
}

void Main(Engine* engine, Shared<Module>* shared, std::mutex* mutex, int id) {
  own<Store> store = Store::make(engine);
  own<Module> module = Module::obtain(store.get(), shared);
  EXPECT_NE(nullptr, module.get());
  for (int i = 0; i < kIterationsPerThread; i++) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));

    // Create imports.
    own<FuncType> func_type = FuncType::make(
        ownvec<ValType>::make(ValType::make(::wasm::ValKind::I32)),
        ownvec<ValType>::make());
    own<Func> func = Func::make(store.get(), func_type.get(), Callback, mutex);
    own<::wasm::GlobalType> global_type = ::wasm::GlobalType::make(
        ValType::make(::wasm::ValKind::I32), ::wasm::Mutability::CONST);
    own<Global> global =
        Global::make(store.get(), global_type.get(), Val::i32(id));

    // Instantiate and run.
    // With the current implementation of the WasmModuleBuilder, global
    // imports always come before function imports, regardless of the
    // order of builder()->Add*Import() calls below.
    vec<Extern*> imports = vec<Extern*>::make(global.get(), func.get());
    own<Instance> instance = Instance::make(store.get(), module.get(), imports);
    ownvec<Extern> exports = instance->exports();
    Func* run_func = exports[0]->func();
    vec<Val> rets = vec<Val>::make_uninitialized();
    vec<Val> args = vec<Val>::make_uninitialized();
    run_func->call(args, rets);
  }
}

}  // namespace

TEST_F(WasmCapiTest, Threads) {
  // Create module.
  ValueType i32_type[] = {kWasmI32};
  FunctionSig param_i32(0, 1, i32_type);
  uint32_t callback_index =
      builder()->AddImport(base::CStrVector("callback"), &param_i32);
  uint32_t global_index =
      builder()->AddGlobalImport(base::CStrVector("id"), kWasmI32, false);

  uint8_t code[] = {
      WASM_CALL_FUNCTION(callback_index, WASM_GLOBAL_GET(global_index))};
  FunctionSig empty_sig(0, 0, nullptr);
  AddExportedFunction(base::CStrVector("run"), code, sizeof(code), &empty_sig);
  Compile();
  own<Shared<Module>> shared = module()->share();

  // Spawn threads.
  g_traces = 0;
  std::mutex mutex;
  std::thread threads[kNumThreads];
  for (int i = 0; i < kNumThreads; i++) {
    threads[i] = std::thread(Main, engine(), shared.get(), &mutex, i);
  }
  for (int i = 0; i < kNumThreads; i++) {
    threads[i].join();
  }
  // Each thread in each iteration adds its ID to {traces}, so in the end
  // we expect kIterationsPerThread * sum([0, ..., kNumThreads-1]).
  // Per Gauss:
  const int kExpected =
      kIterationsPerThread * (kNumThreads - 1) * kNumThreads / 2;
  EXPECT_EQ(kExpected, g_traces);
}

TEST_F(WasmCapiTest, MultiStoresOneThread) {
  // These Stores intentionally have overlapping, but non-nested lifetimes.
  own<Store> store1 = Store::make(engine());
  own<Store> store2 = Store::make(engine());
  own<Store> store3 = Store::make(engine());
  store1.reset();
  store2.reset();
  store3.reset();
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
