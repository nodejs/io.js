// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/api/api-inl.h"
#include "src/init/v8.h"

#include "src/wasm/streaming-decoder.h"
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-module-builder.h"

#include "test/cctest/cctest.h"

#include "test/common/wasm/test-signatures.h"
#include "test/common/wasm/wasm-macro-gen.h"

namespace v8 {
namespace internal {
namespace wasm {

namespace {

class TestResolver : public CompilationResultResolver {
 public:
  explicit TestResolver(std::atomic<int>* pending)
      : native_module_(nullptr), pending_(pending) {}

  void OnCompilationSucceeded(i::Handle<i::WasmModuleObject> module) override {
    if (!module.is_null()) {
      native_module_ = module->shared_native_module();
      pending_->fetch_sub(1);
    }
  }

  void OnCompilationFailed(i::Handle<i::Object> error_reason) override {
    CHECK(false);
  }

  std::shared_ptr<NativeModule> native_module() { return native_module_; }

 private:
  std::shared_ptr<NativeModule> native_module_;
  std::atomic<int>* pending_;
};

class StreamTester {
 public:
  explicit StreamTester(std::shared_ptr<TestResolver> test_resolver)
      : internal_scope_(CcTest::i_isolate()), test_resolver_(test_resolver) {
    i::Isolate* i_isolate = CcTest::i_isolate();

    Handle<Context> context = i_isolate->native_context();

    stream_ = GetWasmEngine()->StartStreamingCompilation(
        i_isolate, WasmFeatures::All(), CompileTimeImports{}, context,
        "WebAssembly.compileStreaming()", test_resolver_);
  }

  void OnBytesReceived(const uint8_t* start, size_t length) {
    stream_->OnBytesReceived(base::Vector<const uint8_t>(start, length));
  }

  void FinishStream() { stream_->Finish(); }

 private:
  i::HandleScope internal_scope_;
  std::shared_ptr<StreamingDecoder> stream_;
  std::shared_ptr<TestResolver> test_resolver_;
};

// Create a valid module such that the bytes depend on {n}.
ZoneBuffer GetValidModuleBytes(Zone* zone, uint8_t n) {
  ZoneBuffer buffer(zone);
  TestSignatures sigs;
  WasmModuleBuilder builder(zone);
  {
    WasmFunctionBuilder* f = builder.AddFunction(sigs.v_v());
    uint8_t code[] = {kExprI32Const, n, kExprDrop, kExprEnd};
    f->EmitCode(code, arraysize(code));
  }
  builder.WriteTo(&buffer);
  return buffer;
}

std::shared_ptr<NativeModule> SyncCompile(base::Vector<const uint8_t> bytes) {
  ErrorThrower thrower(CcTest::i_isolate(), "Test");
  auto enabled_features = WasmFeatures::FromIsolate(CcTest::i_isolate());
  auto wire_bytes = ModuleWireBytes(bytes.begin(), bytes.end());
  Handle<WasmModuleObject> module =
      GetWasmEngine()
          ->SyncCompile(CcTest::i_isolate(), enabled_features,
                        CompileTimeImports{}, &thrower, wire_bytes)
          .ToHandleChecked();
  return module->shared_native_module();
}

// Shared prefix.
constexpr uint8_t kPrefix[] = {
    WASM_MODULE_HEADER,                // module header
    kTypeSectionCode,                  // section code
    U32V_1(1 + SIZEOF_SIG_ENTRY_v_v),  // section size
    U32V_1(1),                         // type count
    SIG_ENTRY_v_v,                     // signature entry
    kFunctionSectionCode,              // section code
    U32V_1(2),                         // section size
    U32V_1(1),                         // functions count
    0,                                 // signature index
    kCodeSectionCode,                  // section code
    U32V_1(7),                         // section size
    U32V_1(1),                         // functions count
    5,                                 // body size
};

constexpr uint8_t kFunctionA[] = {
    U32V_1(0), kExprI32Const, U32V_1(0), kExprDrop, kExprEnd,
};
constexpr uint8_t kFunctionB[] = {
    U32V_1(0), kExprI32Const, U32V_1(1), kExprDrop, kExprEnd,
};

constexpr size_t kPrefixSize = arraysize(kPrefix);
constexpr size_t kFunctionSize = arraysize(kFunctionA);

}  // namespace

TEST(TestAsyncCache) {
  CcTest::InitializeVM();
  i::HandleScope internal_scope(CcTest::i_isolate());
  AccountingAllocator allocator;
  Zone zone(&allocator, "CompilationCacheTester");

  auto bufferA = GetValidModuleBytes(&zone, 0);
  auto bufferB = GetValidModuleBytes(&zone, 1);

  std::atomic<int> pending(3);
  auto resolverA1 = std::make_shared<TestResolver>(&pending);
  auto resolverA2 = std::make_shared<TestResolver>(&pending);
  auto resolverB = std::make_shared<TestResolver>(&pending);

  GetWasmEngine()->AsyncCompile(CcTest::i_isolate(), WasmFeatures::All(),
                                CompileTimeImports{}, resolverA1,
                                ModuleWireBytes(bufferA.begin(), bufferA.end()),
                                true, "WebAssembly.compile");
  GetWasmEngine()->AsyncCompile(CcTest::i_isolate(), WasmFeatures::All(),
                                CompileTimeImports{}, resolverA2,
                                ModuleWireBytes(bufferA.begin(), bufferA.end()),
                                true, "WebAssembly.compile");
  GetWasmEngine()->AsyncCompile(CcTest::i_isolate(), WasmFeatures::All(),
                                CompileTimeImports{}, resolverB,
                                ModuleWireBytes(bufferB.begin(), bufferB.end()),
                                true, "WebAssembly.compile");

  while (pending > 0) {
    v8::platform::PumpMessageLoop(i::V8::GetCurrentPlatform(),
                                  CcTest::isolate());
  }

  CHECK_EQ(resolverA1->native_module(), resolverA2->native_module());
  CHECK_NE(resolverA1->native_module(), resolverB->native_module());
}

TEST(TestStreamingCache) {
  CcTest::InitializeVM();

  std::atomic<int> pending(3);
  auto resolverA1 = std::make_shared<TestResolver>(&pending);
  auto resolverA2 = std::make_shared<TestResolver>(&pending);
  auto resolverB = std::make_shared<TestResolver>(&pending);

  StreamTester testerA1(resolverA1);
  StreamTester testerA2(resolverA2);
  StreamTester testerB(resolverB);

  // Start receiving kPrefix bytes.
  testerA1.OnBytesReceived(kPrefix, kPrefixSize);
  testerA2.OnBytesReceived(kPrefix, kPrefixSize);
  testerB.OnBytesReceived(kPrefix, kPrefixSize);

  // Receive function bytes and start streaming compilation.
  testerA1.OnBytesReceived(kFunctionA, kFunctionSize);
  testerA1.FinishStream();
  testerA2.OnBytesReceived(kFunctionA, kFunctionSize);
  testerA2.FinishStream();
  testerB.OnBytesReceived(kFunctionB, kFunctionSize);
  testerB.FinishStream();

  while (pending > 0) {
    v8::platform::PumpMessageLoop(i::V8::GetCurrentPlatform(),
                                  CcTest::isolate());
  }

  std::shared_ptr<NativeModule> native_module_A1 = resolverA1->native_module();
  std::shared_ptr<NativeModule> native_module_A2 = resolverA2->native_module();
  std::shared_ptr<NativeModule> native_module_B = resolverB->native_module();
  CHECK_EQ(native_module_A1, native_module_A2);
  CHECK_NE(native_module_A1, native_module_B);
}

TEST(TestStreamingAndSyncCache) {
  CcTest::InitializeVM();

  std::atomic<int> pending(1);
  auto resolver = std::make_shared<TestResolver>(&pending);
  StreamTester tester(resolver);

  tester.OnBytesReceived(kPrefix, kPrefixSize);

  // Compile the same module synchronously to make sure we don't deadlock
  // waiting for streaming compilation to finish.
  auto full_bytes =
      base::OwnedVector<uint8_t>::New(kPrefixSize + kFunctionSize);
  memcpy(full_bytes.begin(), kPrefix, kPrefixSize);
  memcpy(full_bytes.begin() + kPrefixSize, kFunctionA, kFunctionSize);
  auto native_module_sync = SyncCompile(full_bytes.as_vector());

  // Streaming compilation should just discard its native module now and use the
  // one inserted in the cache by sync compilation.
  tester.OnBytesReceived(kFunctionA, kFunctionSize);
  tester.FinishStream();

  while (pending > 0) {
    v8::platform::PumpMessageLoop(i::V8::GetCurrentPlatform(),
                                  CcTest::isolate());
  }

  std::shared_ptr<NativeModule> native_module_streaming =
      resolver->native_module();
  CHECK_EQ(native_module_streaming, native_module_sync);
}

void TestModuleSharingBetweenIsolates() {
  class ShareModuleThread : public base::Thread {
   public:
    ShareModuleThread(
        const char* name,
        std::function<void(std::shared_ptr<NativeModule>)> register_module)
        : base::Thread(base::Thread::Options{name}),
          register_module_(std::move(register_module)) {}

    void Run() override {
      v8::Isolate::CreateParams isolate_create_params;
      auto* ab_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
      isolate_create_params.array_buffer_allocator = ab_allocator;
      v8::Isolate* isolate = v8::Isolate::New(isolate_create_params);
      Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);
      isolate->Enter();

      {
        i::HandleScope handle_scope(i_isolate);
        v8::Context::New(isolate)->Enter();
        auto full_bytes =
            base::OwnedVector<uint8_t>::New(kPrefixSize + kFunctionSize);
        memcpy(full_bytes.begin(), kPrefix, kPrefixSize);
        memcpy(full_bytes.begin() + kPrefixSize, kFunctionA, kFunctionSize);
        ErrorThrower thrower(i_isolate, "Test");
        std::shared_ptr<NativeModule> native_module =
            GetWasmEngine()
                ->SyncCompile(i_isolate, WasmFeatures::All(),
                              CompileTimeImports{}, &thrower,
                              ModuleWireBytes{full_bytes.as_vector()})
                .ToHandleChecked()
                ->shared_native_module();
        register_module_(native_module);
        // Check that we can access the code (see https://crbug.com/1280451).
        WasmCodeRefScope code_ref_scope;
        uint8_t* code_start = native_module->GetCode(0)->instructions().begin();
        // Use the loaded value in a CHECK to prevent the compiler from just
        // optimizing it away. Even {volatile} would require that.
        CHECK_NE(0, *code_start);
      }

      isolate->Exit();
      isolate->Dispose();
      delete ab_allocator;
    }

   private:
    const std::function<void(std::shared_ptr<NativeModule>)> register_module_;
  };

  std::vector<std::shared_ptr<NativeModule>> modules;
  base::Mutex mutex;
  auto register_module = [&](std::shared_ptr<NativeModule> module) {
    base::MutexGuard guard(&mutex);
    modules.emplace_back(std::move(module));
  };

  ShareModuleThread thread1("ShareModuleThread1", register_module);
  CHECK(thread1.Start());
  thread1.Join();

  // Start a second thread which should get the cached module.
  ShareModuleThread thread2("ShareModuleThread2", register_module);
  CHECK(thread2.Start());
  thread2.Join();

  CHECK_EQ(2, modules.size());
  CHECK_EQ(modules[0].get(), modules[1].get());
}

UNINITIALIZED_TEST(TwoIsolatesShareNativeModule) {
  v8_flags.wasm_lazy_compilation = false;
  TestModuleSharingBetweenIsolates();
}

UNINITIALIZED_TEST(TwoIsolatesShareNativeModuleWithPku) {
  v8_flags.wasm_lazy_compilation = false;
  v8_flags.memory_protection_keys = true;
  TestModuleSharingBetweenIsolates();
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
