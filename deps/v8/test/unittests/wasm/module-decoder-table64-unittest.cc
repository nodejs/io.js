// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/module-decoder.h"
#include "test/common/wasm/flag-utils.h"
#include "test/common/wasm/wasm-macro-gen.h"
#include "test/unittests/test-utils.h"
#include "testing/gmock-support.h"

using testing::HasSubstr;

namespace v8::internal::wasm {
namespace module_decoder_unittest {

#define EXPECT_NOT_OK(result, msg)                           \
  do {                                                       \
    EXPECT_FALSE(result.ok());                               \
    if (!result.ok()) {                                      \
      EXPECT_THAT(result.error().message(), HasSubstr(msg)); \
    }                                                        \
  } while (false)

#define WASM_INIT_EXPR_I32V_1(val) WASM_I32V_1(val), kExprEnd
#define WASM_INIT_EXPR_I64V_5(val) WASM_I64V_5(val), kExprEnd
#define WASM_INIT_EXPR_FUNC_REF_NULL WASM_REF_NULL(kFuncRefCode), kExprEnd

class Table64DecodingTest : public TestWithIsolateAndZone {
 public:
  WasmEnabledFeatures enabled_features_ = WasmEnabledFeatures::None();

  ModuleResult DecodeModule(std::initializer_list<uint8_t> module_body_bytes) {
    // Add the wasm magic and version number automatically.
    std::vector<uint8_t> module_bytes{WASM_MODULE_HEADER};
    module_bytes.insert(module_bytes.end(), module_body_bytes);
    // Table64 is part of the Memory64 proposal.
    bool kValidateFunctions = true;
    ModuleResult result =
        DecodeWasmModule(enabled_features_, base::VectorOf(module_bytes),
                         kValidateFunctions, kWasmOrigin);
    return result;
  }
};

TEST_F(Table64DecodingTest, TableLimitLEB64) {
  WASM_FEATURE_SCOPE(memory64);

  // 2 bytes LEB (32-bit range), no maximum.
  ModuleResult module = DecodeModule({SECTION(
      Table, ENTRY_COUNT(1), kFuncRefCode, kMemory64NoMaximum, U32V_2(5))});
  EXPECT_TRUE(module.ok()) << module.error().message();
  ASSERT_EQ(1u, module.value()->tables.size());
  const WasmTable* table = &module.value()->tables[0];
  EXPECT_EQ(5u, table->initial_size);
  EXPECT_FALSE(table->has_maximum_size);
  EXPECT_TRUE(table->is_table64);

  // 3 bytes LEB (32-bit range), with maximum.
  module =
      DecodeModule({SECTION(Table, ENTRY_COUNT(1), kExternRefCode,
                            kMemory64WithMaximum, U32V_3(12), U32V_3(123))});
  EXPECT_TRUE(module.ok()) << module.error().message();
  ASSERT_EQ(1u, module.value()->tables.size());
  table = &module.value()->tables[0];
  EXPECT_EQ(12u, table->initial_size);
  EXPECT_TRUE(table->has_maximum_size);
  EXPECT_EQ(123u, table->maximum_size);
  EXPECT_TRUE(table->is_table64);

  // 5 bytes LEB (32-bit range), no maximum.
  module = DecodeModule({SECTION(Table, ENTRY_COUNT(1), kExternRefCode,
                                 kMemory64NoMaximum, U64V_5(7))});
  EXPECT_TRUE(module.ok()) << module.error().message();
  ASSERT_EQ(1u, module.value()->tables.size());
  table = &module.value()->tables[0];
  EXPECT_EQ(7u, table->initial_size);
  EXPECT_FALSE(table->has_maximum_size);
  EXPECT_TRUE(table->is_table64);

  // 10 bytes LEB (32-bit range), with maximum.
  module =
      DecodeModule({SECTION(Table, ENTRY_COUNT(1), kFuncRefCode,
                            kMemory64WithMaximum, U64V_10(4), U64V_10(1234))});
  EXPECT_TRUE(module.ok()) << module.error().message();
  ASSERT_EQ(1u, module.value()->tables.size());
  table = &module.value()->tables[0];
  EXPECT_EQ(4u, table->initial_size);
  EXPECT_TRUE(table->has_maximum_size);
  EXPECT_EQ(1234u, table->maximum_size);
  EXPECT_TRUE(table->is_table64);
}

TEST_F(Table64DecodingTest, InvalidTableLimits) {
  WASM_FEATURE_SCOPE(memory64);

  const uint8_t kInvalidLimits = 0x15;
  ModuleResult module = DecodeModule({SECTION(
      Table, ENTRY_COUNT(1), kFuncRefCode, kInvalidLimits, U32V_2(5))});
  EXPECT_NOT_OK(module, "invalid table limits flags");
}

TEST_F(Table64DecodingTest, DisabledFlag) {
  ModuleResult module = DecodeModule({SECTION(
      Table, ENTRY_COUNT(1), kFuncRefCode, kMemory64NoMaximum, U32V_2(5))});
  EXPECT_NOT_OK(module,
                "invalid table limits flags 0x4 (enable with "
                "--experimental-wasm-memory64)");
}

TEST_F(Table64DecodingTest, ImportedTable64) {
  WASM_FEATURE_SCOPE(memory64);

  // 10 bytes LEB (32-bit range), no maximum.
  ModuleResult module = DecodeModule(
      {SECTION(Import, ENTRY_COUNT(1), ADD_COUNT('m'), ADD_COUNT('t'),
               kExternalTable, kFuncRefCode, kMemory64NoMaximum, U64V_10(5))});
  EXPECT_TRUE(module.ok()) << module.error().message();
  ASSERT_EQ(1u, module.value()->tables.size());
  const WasmTable* table = &module.value()->tables[0];
  EXPECT_EQ(5u, table->initial_size);
  EXPECT_FALSE(table->has_maximum_size);
  EXPECT_TRUE(table->is_table64);

  // 5 bytes LEB (32-bit range), with maximum.
  module = DecodeModule({SECTION(
      Import, ENTRY_COUNT(1), ADD_COUNT('m'), ADD_COUNT('t'), kExternalTable,
      kFuncRefCode, kMemory64WithMaximum, U64V_5(123), U64V_5(225))});
  EXPECT_TRUE(module.ok()) << module.error().message();
  ASSERT_EQ(1u, module.value()->tables.size());
  table = &module.value()->tables[0];
  EXPECT_EQ(123u, table->initial_size);
  EXPECT_TRUE(table->has_maximum_size);
  EXPECT_TRUE(table->is_table64);
  EXPECT_EQ(225u, table->maximum_size);
}

}  // namespace module_decoder_unittest
}  // namespace v8::internal::wasm
