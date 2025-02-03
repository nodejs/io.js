// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "src/execution/microtask-queue.h"
#include "src/objects/objects-inl.h"
#include "src/wasm/function-compiler.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-module-builder.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects-inl.h"

#include "test/cctest/cctest.h"
#include "test/common/wasm/test-signatures.h"
#include "test/common/wasm/wasm-macro-gen.h"
#include "test/common/wasm/wasm-module-runner.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace test_wasm_shared_engine {

// Helper type definition representing a WebAssembly module shared between
// multiple Isolates with implicit reference counting.
using SharedModule = std::shared_ptr<NativeModule>;

// Helper class representing an Isolate that uses the process-wide (shared) wasm
// engine.
class SharedEngineIsolate {
 public:
  SharedEngineIsolate() : isolate_(v8::Isolate::Allocate()) {
    v8::Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = CcTest::array_buffer_allocator();
    v8::Isolate::Initialize(isolate_, create_params);
    v8_isolate()->Enter();
    v8::HandleScope handle_scope(v8_isolate());
    v8::Context::New(v8_isolate())->Enter();
    testing::SetupIsolateForWasmModule(isolate());
    zone_.reset(new Zone(isolate()->allocator(), ZONE_NAME));
  }
  ~SharedEngineIsolate() {
    v8_isolate()->Exit();
    zone_.reset();
    isolate_->Dispose();
  }

  Zone* zone() const { return zone_.get(); }
  v8::Isolate* v8_isolate() { return isolate_; }
  Isolate* isolate() { return reinterpret_cast<Isolate*>(isolate_); }

  Handle<WasmInstanceObject> CompileAndInstantiate(ZoneBuffer* buffer) {
    ErrorThrower thrower(isolate(), "CompileAndInstantiate");
    MaybeHandle<WasmInstanceObject> instance =
        testing::CompileAndInstantiateForTesting(
            isolate(), &thrower,
            ModuleWireBytes(buffer->begin(), buffer->end()));
    return instance.ToHandleChecked();
  }

  Handle<WasmInstanceObject> ImportInstance(SharedModule shared_module) {
    Handle<WasmModuleObject> module_object =
        GetWasmEngine()->ImportNativeModule(isolate(), shared_module, {});
    ErrorThrower thrower(isolate(), "ImportInstance");
    MaybeHandle<WasmInstanceObject> instance = GetWasmEngine()->SyncInstantiate(
        isolate(), &thrower, module_object, {}, {});
    return instance.ToHandleChecked();
  }

  SharedModule ExportInstance(DirectHandle<WasmInstanceObject> instance) {
    return instance->module_object()->shared_native_module();
  }

  int32_t Run(Handle<WasmInstanceObject> instance) {
    return testing::CallWasmFunctionForTesting(isolate(), instance, "main", {});
  }

 private:
  v8::Isolate* isolate_;
  std::unique_ptr<Zone> zone_;
};

// Helper class representing a Thread running its own instance of an Isolate
// with a shared WebAssembly engine available at construction time.
class SharedEngineThread : public v8::base::Thread {
 public:
  explicit SharedEngineThread(
      std::function<void(SharedEngineIsolate*)> callback)
      : Thread(Options("SharedEngineThread")), callback_(callback) {}

  void Run() override {
    SharedEngineIsolate isolate;
    callback_(&isolate);
  }

 private:
  std::function<void(SharedEngineIsolate*)> callback_;
};

namespace {

ZoneBuffer* BuildReturnConstantModule(Zone* zone, int constant) {
  TestSignatures sigs;
  ZoneBuffer* buffer = zone->New<ZoneBuffer>(zone);
  WasmModuleBuilder* builder = zone->New<WasmModuleBuilder>(zone);
  WasmFunctionBuilder* f = builder->AddFunction(sigs.i_v());
  f->builder()->AddExport(base::CStrVector("main"), f);
  uint8_t code[] = {WASM_I32V_2(constant)};
  f->EmitCode(code, sizeof(code));
  f->Emit(kExprEnd);
  builder->WriteTo(buffer);
  return buffer;
}

class MockInstantiationResolver : public InstantiationResultResolver {
 public:
  explicit MockInstantiationResolver(Handle<Object>* out_instance)
      : out_instance_(out_instance) {}
  void OnInstantiationSucceeded(Handle<WasmInstanceObject> result) override {
    *out_instance_->location() = result->ptr();
  }
  void OnInstantiationFailed(Handle<Object> error_reason) override {
    UNREACHABLE();
  }

 private:
  Handle<Object>* out_instance_;
};

class MockCompilationResolver : public CompilationResultResolver {
 public:
  MockCompilationResolver(SharedEngineIsolate* isolate,
                          Handle<Object>* out_instance)
      : isolate_(isolate), out_instance_(out_instance) {}
  void OnCompilationSucceeded(Handle<WasmModuleObject> result) override {
    GetWasmEngine()->AsyncInstantiate(
        isolate_->isolate(),
        std::make_unique<MockInstantiationResolver>(out_instance_), result, {});
  }
  void OnCompilationFailed(Handle<Object> error_reason) override {
    UNREACHABLE();
  }

 private:
  SharedEngineIsolate* isolate_;
  Handle<Object>* out_instance_;
};

void PumpMessageLoop(SharedEngineIsolate* isolate) {
  v8::platform::PumpMessageLoop(i::V8::GetCurrentPlatform(),
                                isolate->v8_isolate(),
                                platform::MessageLoopBehavior::kWaitForWork);
  isolate->isolate()->default_microtask_queue()->RunMicrotasks(
      isolate->isolate());
}

Handle<WasmInstanceObject> CompileAndInstantiateAsync(
    SharedEngineIsolate* isolate, ZoneBuffer* buffer) {
  Handle<Object> maybe_instance = handle(Smi::zero(), isolate->isolate());
  auto enabled_features = WasmEnabledFeatures::FromIsolate(isolate->isolate());
  constexpr const char* kAPIMethodName = "Test.CompileAndInstantiateAsync";
  GetWasmEngine()->AsyncCompile(
      isolate->isolate(), enabled_features, CompileTimeImports{},
      std::make_unique<MockCompilationResolver>(isolate, &maybe_instance),
      ModuleWireBytes(buffer->begin(), buffer->end()), true, kAPIMethodName);
  while (!IsWasmInstanceObject(*maybe_instance)) PumpMessageLoop(isolate);
  Handle<WasmInstanceObject> instance =
      Cast<WasmInstanceObject>(maybe_instance);
  return instance;
}

}  // namespace

TEST(SharedEngineRunSeparated) {
  {
    SharedEngineIsolate isolate;
    HandleScope scope(isolate.isolate());
    ZoneBuffer* buffer = BuildReturnConstantModule(isolate.zone(), 23);
    Handle<WasmInstanceObject> instance = isolate.CompileAndInstantiate(buffer);
    CHECK_EQ(23, isolate.Run(instance));
  }
  {
    SharedEngineIsolate isolate;
    HandleScope scope(isolate.isolate());
    ZoneBuffer* buffer = BuildReturnConstantModule(isolate.zone(), 42);
    Handle<WasmInstanceObject> instance = isolate.CompileAndInstantiate(buffer);
    CHECK_EQ(42, isolate.Run(instance));
  }
}

TEST(SharedEngineRunImported) {
  SharedModule module;
  {
    SharedEngineIsolate isolate;
    HandleScope scope(isolate.isolate());
    ZoneBuffer* buffer = BuildReturnConstantModule(isolate.zone(), 23);
    Handle<WasmInstanceObject> instance = isolate.CompileAndInstantiate(buffer);
    module = isolate.ExportInstance(instance);
    CHECK_EQ(23, isolate.Run(instance));
  }
  {
    SharedEngineIsolate isolate;
    HandleScope scope(isolate.isolate());
    Handle<WasmInstanceObject> instance = isolate.ImportInstance(module);
    CHECK_EQ(23, isolate.Run(instance));
  }
}

TEST(SharedEngineRunThreadedBuildingSync) {
  SharedEngineThread thread1([](SharedEngineIsolate* isolate) {
    HandleScope scope(isolate->isolate());
    ZoneBuffer* buffer = BuildReturnConstantModule(isolate->zone(), 23);
    Handle<WasmInstanceObject> instance =
        isolate->CompileAndInstantiate(buffer);
    CHECK_EQ(23, isolate->Run(instance));
  });
  SharedEngineThread thread2([](SharedEngineIsolate* isolate) {
    HandleScope scope(isolate->isolate());
    ZoneBuffer* buffer = BuildReturnConstantModule(isolate->zone(), 42);
    Handle<WasmInstanceObject> instance =
        isolate->CompileAndInstantiate(buffer);
    CHECK_EQ(42, isolate->Run(instance));
  });
  CHECK(thread1.Start());
  CHECK(thread2.Start());
  thread1.Join();
  thread2.Join();
}

TEST(SharedEngineRunThreadedBuildingAsync) {
  SharedEngineThread thread1([](SharedEngineIsolate* isolate) {
    HandleScope scope(isolate->isolate());
    ZoneBuffer* buffer = BuildReturnConstantModule(isolate->zone(), 23);
    Handle<WasmInstanceObject> instance =
        CompileAndInstantiateAsync(isolate, buffer);
    CHECK_EQ(23, isolate->Run(instance));
  });
  SharedEngineThread thread2([](SharedEngineIsolate* isolate) {
    HandleScope scope(isolate->isolate());
    ZoneBuffer* buffer = BuildReturnConstantModule(isolate->zone(), 42);
    Handle<WasmInstanceObject> instance =
        CompileAndInstantiateAsync(isolate, buffer);
    CHECK_EQ(42, isolate->Run(instance));
  });
  CHECK(thread1.Start());
  CHECK(thread2.Start());
  thread1.Join();
  thread2.Join();
}

TEST(SharedEngineRunThreadedExecution) {
  SharedModule module;
  {
    SharedEngineIsolate isolate;
    HandleScope scope(isolate.isolate());
    ZoneBuffer* buffer = BuildReturnConstantModule(isolate.zone(), 23);
    DirectHandle<WasmInstanceObject> instance =
        isolate.CompileAndInstantiate(buffer);
    module = isolate.ExportInstance(instance);
  }
  SharedEngineThread thread1([module](SharedEngineIsolate* isolate) {
    HandleScope scope(isolate->isolate());
    Handle<WasmInstanceObject> instance = isolate->ImportInstance(module);
    CHECK_EQ(23, isolate->Run(instance));
  });
  SharedEngineThread thread2([module](SharedEngineIsolate* isolate) {
    HandleScope scope(isolate->isolate());
    Handle<WasmInstanceObject> instance = isolate->ImportInstance(module);
    CHECK_EQ(23, isolate->Run(instance));
  });
  CHECK(thread1.Start());
  CHECK(thread2.Start());
  thread1.Join();
  thread2.Join();
}

TEST(SharedEngineRunThreadedTierUp) {
  SharedModule module;
  {
    SharedEngineIsolate isolate;
    HandleScope scope(isolate.isolate());
    ZoneBuffer* buffer = BuildReturnConstantModule(isolate.zone(), 23);
    DirectHandle<WasmInstanceObject> instance =
        isolate.CompileAndInstantiate(buffer);
    module = isolate.ExportInstance(instance);
  }
  constexpr int kNumberOfThreads = 5;
  std::list<SharedEngineThread> threads;
  for (int i = 0; i < kNumberOfThreads; ++i) {
    threads.emplace_back([module](SharedEngineIsolate* isolate) {
      constexpr int kNumberOfIterations = 100;
      HandleScope scope(isolate->isolate());
      Handle<WasmInstanceObject> instance = isolate->ImportInstance(module);
      for (int j = 0; j < kNumberOfIterations; ++j) {
        CHECK_EQ(23, isolate->Run(instance));
      }
    });
  }
  threads.emplace_back([module](SharedEngineIsolate* isolate) {
    HandleScope scope(isolate->isolate());
    Handle<WasmInstanceObject> instance = isolate->ImportInstance(module);
    WasmDetectedFeatures detected;
    WasmCompilationUnit::CompileWasmFunction(
        isolate->isolate()->counters(), module.get(), &detected,
        &module->module()->functions[0], ExecutionTier::kTurbofan);
    CHECK_EQ(23, isolate->Run(instance));
  });
  for (auto& thread : threads) CHECK(thread.Start());
  for (auto& thread : threads) thread.Join();
}

}  // namespace test_wasm_shared_engine
}  // namespace wasm
}  // namespace internal
}  // namespace v8
