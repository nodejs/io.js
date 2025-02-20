// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>

#include "include/v8-isolate.h"
#include "src/wasm/wasm-module-builder.h"
#include "test/common/wasm/test-signatures.h"
#include "test/unittests/test-utils.h"
#include "test/unittests/wasm/wasm-compile-module.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace v8::internal::wasm {

// Execute each test with sync, async, and streaming compilation.
enum CompileType { kSync, kAsync, kStreaming };

class WasmUseCounterTest
    : public WithZoneMixin<WithInternalIsolateMixin<WithContextMixin<
          WithIsolateScopeMixin<WithIsolateMixin<WithDefaultPlatformMixin<
              ::testing::TestWithParam<CompileType>>>>>>> {
 public:
  using UC = v8::Isolate::UseCounterFeature;
  using UCMap = std::map<UC, int>;

  WasmUseCounterTest() {
    isolate()->SetUseCounterCallback([](v8::Isolate* isolate, UC feature) {
      GetUseCounterMap()[feature] += 1;
    });
  }

  void AddFunction(std::initializer_list<const uint8_t> body) {
    WasmFunctionBuilder* f = builder_.AddFunction(sigs_.v_i());
    f->EmitCode(body);
    builder_.WriteTo(&buffer_);
  }

  void Compile() {
    base::OwnedVector<const uint8_t> bytes = base::OwnedCopyOf(buffer_);
    switch (GetParam()) {
      case kSync:
        return WasmCompileHelper::SyncCompile(isolate(), std::move(bytes));
      case kAsync:
        return WasmCompileHelper::AsyncCompile(isolate(), std::move(bytes));
      case kStreaming:
        return WasmCompileHelper::StreamingCompile(isolate(),
                                                   bytes.as_vector());
    }
  }

  void CheckUseCounters(
      std::initializer_list<std::pair<const UC, int>> use_counters) {
    EXPECT_THAT(GetUseCounterMap(),
                testing::UnorderedElementsAreArray(use_counters));
  }

  WasmModuleBuilder& builder() { return builder_; }

 private:
  static UCMap& GetUseCounterMap() {
    static UCMap global_use_counter_map;
    return global_use_counter_map;
  }

  ZoneBuffer buffer_{zone()};
  HandleScope scope_{isolate()};

  WasmModuleBuilder builder_{zone()};
  TestSignatures sigs_;
};

std::string PrintCompileType(
    ::testing::TestParamInfo<CompileType> compile_type) {
  switch (compile_type.param) {
    case kSync:
      return "Sync";
    case kAsync:
      return "Async";
    case kStreaming:
      return "Streaming";
  }
}

INSTANTIATE_TEST_SUITE_P(CompileTypes, WasmUseCounterTest,
                         ::testing::Values(CompileType::kSync,
                                           CompileType::kAsync,
                                           CompileType::kStreaming),
                         PrintCompileType);

TEST_P(WasmUseCounterTest, SimpleModule) {
  AddFunction({kExprEnd});
  Compile();
  CheckUseCounters({{UC::kWasmModuleCompilation, 1}});
}

TEST_P(WasmUseCounterTest, Memory64) {
  builder().AddMemory64(1, 1);
  AddFunction({kExprEnd});
  Compile();
  CheckUseCounters({{UC::kWasmModuleCompilation, 1}, {UC::kWasmMemory64, 1}});
}

TEST_P(WasmUseCounterTest, Memory64_Twice) {
  builder().AddMemory64(1, 1);
  AddFunction({kExprEnd});
  Compile();
  Compile();
  CheckUseCounters({{UC::kWasmModuleCompilation, 2}, {UC::kWasmMemory64, 2}});
}

TEST_P(WasmUseCounterTest, Memory64AndRefTypes) {
  builder().AddMemory64(1, 1);
  AddFunction({kExprRefNull, kFuncRefCode, kExprDrop, kExprEnd});
  Compile();
  CheckUseCounters({{UC::kWasmModuleCompilation, 1},
                    {UC::kWasmMemory64, 1},
                    {UC::kWasmRefTypes, 1}});
}

}  // namespace v8::internal::wasm
