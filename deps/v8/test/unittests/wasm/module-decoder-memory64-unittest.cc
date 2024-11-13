// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/objects-inl.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-features.h"
#include "src/wasm/wasm-limits.h"
#include "test/common/wasm/wasm-macro-gen.h"
#include "test/unittests/test-utils.h"

namespace v8::internal::wasm {

class Memory64DecodingTest : public TestWithIsolateAndZone {
 public:
  std::shared_ptr<const WasmModule> DecodeModule(
      std::initializer_list<uint8_t> module_body_bytes) {
    // Add the wasm magic and version number automatically.
    std::vector<uint8_t> module_bytes{WASM_MODULE_HEADER};
    module_bytes.insert(module_bytes.end(), module_body_bytes);
    static constexpr WasmEnabledFeatures kEnabledFeatures{
        WasmEnabledFeature::memory64};
    bool kValidateFunctions = true;
    ModuleResult result =
        DecodeWasmModule(kEnabledFeatures, base::VectorOf(module_bytes),
                         kValidateFunctions, kWasmOrigin);
    EXPECT_TRUE(result.ok()) << result.error().message();
    return result.ok() ? std::move(result).value() : nullptr;
  }
};

TEST_F(Memory64DecodingTest, MemoryLimitLEB64) {
  // 2 bytes LEB (32-bit range), no maximum.
  auto module = DecodeModule(
      {SECTION(Memory, ENTRY_COUNT(1), kMemory64NoMaximum, U32V_2(5))});
  ASSERT_NE(nullptr, module);
  ASSERT_EQ(1u, module->memories.size());
  const WasmMemory* memory = &module->memories[0];
  EXPECT_EQ(5u, memory->initial_pages);
  EXPECT_FALSE(memory->has_maximum_pages);
  EXPECT_TRUE(memory->is_memory64);

  // 2 bytes LEB (32-bit range), with maximum.
  module = DecodeModule({SECTION(Memory, ENTRY_COUNT(1), kMemory64WithMaximum,
                                 U32V_2(7), U32V_2(47))});
  ASSERT_NE(nullptr, module);
  ASSERT_EQ(1u, module->memories.size());
  memory = &module->memories[0];
  EXPECT_EQ(7u, memory->initial_pages);
  EXPECT_TRUE(memory->has_maximum_pages);
  EXPECT_EQ(47u, memory->maximum_pages);
  EXPECT_TRUE(memory->is_memory64);

  // 10 bytes LEB, 32-bit range, no maximum.
  module = DecodeModule(
      {SECTION(Memory, ENTRY_COUNT(1), kMemory64NoMaximum, U64V_10(2))});
  ASSERT_NE(nullptr, module);
  ASSERT_EQ(1u, module->memories.size());
  memory = &module->memories[0];
  EXPECT_EQ(2u, memory->initial_pages);
  EXPECT_FALSE(memory->has_maximum_pages);
  EXPECT_TRUE(memory->is_memory64);

  // 10 bytes LEB, 32-bit range, with maximum.
  module = DecodeModule({SECTION(Memory, ENTRY_COUNT(1), kMemory64WithMaximum,
                                 U64V_10(2), U64V_10(6))});
  ASSERT_NE(nullptr, module);
  ASSERT_EQ(1u, module->memories.size());
  memory = &module->memories[0];
  EXPECT_EQ(2u, memory->initial_pages);
  EXPECT_TRUE(memory->has_maximum_pages);
  EXPECT_EQ(6u, memory->maximum_pages);
  EXPECT_TRUE(memory->is_memory64);

  // TODO(clemensb): Test numbers outside the 32-bit range once that's
  // supported.
}

}  // namespace v8::internal::wasm
