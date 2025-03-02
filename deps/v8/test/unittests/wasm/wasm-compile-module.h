// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TEST_UNITTESTS_WASM_WASM_COMPILE_MODULE_H_
#define TEST_UNITTESTS_WASM_WASM_COMPILE_MODULE_H_

#include "include/libplatform/libplatform.h"
#include "src/base/vector.h"
#include "src/execution/isolate.h"
#include "src/handles/handles.h"
#include "src/wasm/streaming-decoder.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-objects.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest-support.h"

namespace v8::internal::wasm {

class WasmCompileHelper : public AllStatic {
 public:
  static void SyncCompile(Isolate* isolate,
                          base::OwnedVector<const uint8_t> bytes) {
    ErrorThrower thrower(isolate, "WasmCompileHelper::SyncCompile");
    GetWasmEngine()->SyncCompile(isolate, WasmEnabledFeatures::All(),
                                 CompileTimeImports{}, &thrower,
                                 std::move(bytes));
    ASSERT_FALSE(thrower.error()) << thrower.error_msg();
  }

  static void AsyncCompile(Isolate* isolate,
                           base::OwnedVector<const uint8_t> bytes) {
    std::shared_ptr<TestResolver> resolver = std::make_shared<TestResolver>();

    GetWasmEngine()->AsyncCompile(
        isolate, WasmEnabledFeatures::All(), CompileTimeImports{}, resolver,
        std::move(bytes), "WasmCompileHelper::AsyncCompile");
    while (resolver->pending()) {
      v8::platform::PumpMessageLoop(i::V8::GetCurrentPlatform(),
                                    reinterpret_cast<v8::Isolate*>(isolate));
    }
  }

  static void StreamingCompile(Isolate* isolate,
                               base::Vector<const uint8_t> bytes) {
    std::shared_ptr<TestResolver> resolver = std::make_shared<TestResolver>();
    std::shared_ptr<StreamingDecoder> streaming_decoder =
        GetWasmEngine()->StartStreamingCompilation(
            isolate, WasmEnabledFeatures::All(), CompileTimeImports{},
            handle(isolate->context()->native_context(), isolate),
            "StreamingCompile", resolver);
    base::RandomNumberGenerator* rng = isolate->random_number_generator();
    for (auto remaining_bytes = bytes; !remaining_bytes.empty();) {
      // Split randomly; with 10% probability do not split.
      ASSERT_GE(size_t{kMaxInt / 2}, remaining_bytes.size());
      size_t split_point =
          remaining_bytes.size() == 1 || rng->NextInt(10) == 0
              ? remaining_bytes.size()
              : 1 + rng->NextInt(static_cast<int>(remaining_bytes.size() - 1));
      streaming_decoder->OnBytesReceived(
          remaining_bytes.SubVector(0, split_point));
      remaining_bytes += split_point;
    }
    streaming_decoder->Finish(true);

    while (resolver->pending()) {
      v8::platform::PumpMessageLoop(i::V8::GetCurrentPlatform(),
                                    reinterpret_cast<v8::Isolate*>(isolate));
    }
  }

 private:
  struct TestResolver : public CompilationResultResolver {
   public:
    void OnCompilationSucceeded(
        i::Handle<i::WasmModuleObject> module) override {
      ASSERT_FALSE(module.is_null());
      ASSERT_EQ(true, pending_.exchange(false, std::memory_order_relaxed));
    }

    void OnCompilationFailed(i::Handle<i::Object> error_reason) override {
      Print(*error_reason);
      FAIL();
    }

    bool pending() const { return pending_.load(std::memory_order_relaxed); }

   private:
    std::atomic<bool> pending_{true};
  };
};

}  // namespace v8::internal::wasm

#endif  // TEST_UNITTESTS_WASM_WASM_COMPILE_MODULE_H_
