// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-module-builder.h"
#include "test/cctest/cctest.h"
#include "test/cctest/wasm/wasm-run-utils.h"
#include "test/common/wasm/test-signatures.h"
#include "test/common/wasm/wasm-macro-gen.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace test_run_wasm_bulk_memory {

namespace {
void CheckMemoryEquals(TestingModuleBuilder* builder, size_t index,
                       const std::vector<uint8_t>& expected) {
  const uint8_t* mem_start = builder->raw_mem_start<uint8_t>();
  const uint8_t* mem_end = builder->raw_mem_end<uint8_t>();
  size_t mem_size = mem_end - mem_start;
  CHECK_LE(index, mem_size);
  CHECK_LE(index + expected.size(), mem_size);
  for (size_t i = 0; i < expected.size(); ++i) {
    CHECK_EQ(expected[i], mem_start[index + i]);
  }
}

void CheckMemoryEqualsZero(TestingModuleBuilder* builder, size_t index,
                           size_t length) {
  const uint8_t* mem_start = builder->raw_mem_start<uint8_t>();
  const uint8_t* mem_end = builder->raw_mem_end<uint8_t>();
  size_t mem_size = mem_end - mem_start;
  CHECK_LE(index, mem_size);
  CHECK_LE(index + length, mem_size);
  for (size_t i = 0; i < length; ++i) {
    CHECK_EQ(0, mem_start[index + i]);
  }
}

void CheckMemoryEqualsFollowedByZeroes(TestingModuleBuilder* builder,
                                       const std::vector<uint8_t>& expected) {
  CheckMemoryEquals(builder, 0, expected);
  CheckMemoryEqualsZero(builder, expected.size(),
                        builder->mem_size() - expected.size());
}
}  // namespace

WASM_EXEC_TEST(MemoryInit) {
  WasmRunner<uint32_t, uint32_t, uint32_t, uint32_t> r(execution_tier);
  r.builder().AddMemory(kWasmPageSize);
  const uint8_t data[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  r.builder().AddPassiveDataSegment(base::ArrayVector(data));
  r.Build({WASM_MEMORY_INIT(0, WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
                            WASM_LOCAL_GET(2)),
           kExprI32Const, 0});

  // All zeroes.
  CheckMemoryEqualsZero(&r.builder(), 0, kWasmPageSize);

  // Copy all bytes from data segment 0, to memory at [10, 20).
  CHECK_EQ(0, r.Call(10, 0, 10));
  CheckMemoryEqualsFollowedByZeroes(
      &r.builder(),
      {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9});

  // Copy bytes in range [5, 10) from data segment 0, to memory at [0, 5).
  CHECK_EQ(0, r.Call(0, 5, 5));
  CheckMemoryEqualsFollowedByZeroes(
      &r.builder(),
      {5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9});

  // Copy 0 bytes does nothing.
  CHECK_EQ(0, r.Call(10, 1, 0));
  CheckMemoryEqualsFollowedByZeroes(
      &r.builder(),
      {5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9});

  // Copy 0 at end of memory region or data segment is OK.
  CHECK_EQ(0, r.Call(kWasmPageSize, 0, 0));
  CHECK_EQ(0, r.Call(0, sizeof(data), 0));
}

WASM_EXEC_TEST(MemoryInitOutOfBoundsData) {
  WasmRunner<uint32_t, uint32_t, uint32_t, uint32_t> r(execution_tier);
  r.builder().AddMemory(kWasmPageSize);
  const uint8_t data[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  r.builder().AddPassiveDataSegment(base::ArrayVector(data));
  r.Build({WASM_MEMORY_INIT(0, WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
                            WASM_LOCAL_GET(2)),
           kExprI32Const, 0});

  const uint32_t last_5_bytes = kWasmPageSize - 5;

  // Failing memory.init should not have any effect.
  CHECK_EQ(0xDEADBEEF, r.Call(kWasmPageSize - 5, 0, 6));
  CheckMemoryEquals(&r.builder(), last_5_bytes, {0, 0, 0, 0, 0});

  r.builder().BlankMemory();
  CHECK_EQ(0xDEADBEEF, r.Call(0, 5, 6));
  CheckMemoryEquals(&r.builder(), last_5_bytes, {0, 0, 0, 0, 0});
}

WASM_EXEC_TEST(MemoryInitOutOfBounds) {
  WasmRunner<uint32_t, uint32_t, uint32_t, uint32_t> r(execution_tier);
  r.builder().AddMemory(kWasmPageSize);
  const uint8_t data[kWasmPageSize] = {};
  r.builder().AddPassiveDataSegment(base::ArrayVector(data));
  r.Build({WASM_MEMORY_INIT(0, WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
                            WASM_LOCAL_GET(2)),
           kExprI32Const, 0});

  // OK, copy the full data segment to memory.
  r.Call(0, 0, kWasmPageSize);

  // Source range must not be out of bounds.
  CHECK_EQ(0xDEADBEEF, r.Call(0, 1, kWasmPageSize));
  CHECK_EQ(0xDEADBEEF, r.Call(0, 1000, kWasmPageSize));
  CHECK_EQ(0xDEADBEEF, r.Call(0, kWasmPageSize, 1));

  // Destination range must not be out of bounds.
  CHECK_EQ(0xDEADBEEF, r.Call(1, 0, kWasmPageSize));
  CHECK_EQ(0xDEADBEEF, r.Call(1000, 0, kWasmPageSize));
  CHECK_EQ(0xDEADBEEF, r.Call(kWasmPageSize, 0, 1));

  // Copy 0 out-of-bounds fails if target is invalid.
  CHECK_EQ(0xDEADBEEF, r.Call(kWasmPageSize + 1, 0, 0));
  CHECK_EQ(0xDEADBEEF, r.Call(0, kWasmPageSize + 1, 0));

  // Make sure bounds aren't checked with 32-bit wrapping.
  CHECK_EQ(0xDEADBEEF, r.Call(1, 1, 0xFFFFFFFF));
}

WASM_EXEC_TEST(MemoryCopy) {
  WasmRunner<uint32_t, uint32_t, uint32_t, uint32_t> r(execution_tier);
  uint8_t* mem = r.builder().AddMemory(kWasmPageSize);
  r.Build({WASM_MEMORY0_COPY(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
                             WASM_LOCAL_GET(2)),
           kExprI32Const, 0});

  const uint8_t initial[] = {0, 11, 22, 33, 44, 55, 66, 77};
  memcpy(mem, initial, sizeof(initial));

  // Copy from [1, 8] to [10, 16].
  CHECK_EQ(0, r.Call(10, 1, 8));
  CheckMemoryEqualsFollowedByZeroes(
      &r.builder(),
      {0, 11, 22, 33, 44, 55, 66, 77, 0, 0, 11, 22, 33, 44, 55, 66, 77});

  // Copy 0 bytes does nothing.
  CHECK_EQ(0, r.Call(10, 2, 0));
  CheckMemoryEqualsFollowedByZeroes(
      &r.builder(),
      {0, 11, 22, 33, 44, 55, 66, 77, 0, 0, 11, 22, 33, 44, 55, 66, 77});

  // Copy 0 at end of memory region is OK.
  CHECK_EQ(0, r.Call(kWasmPageSize, 0, 0));
  CHECK_EQ(0, r.Call(0, kWasmPageSize, 0));
}

WASM_EXEC_TEST(MemoryCopyOverlapping) {
  WasmRunner<uint32_t, uint32_t, uint32_t, uint32_t> r(execution_tier);
  uint8_t* mem = r.builder().AddMemory(kWasmPageSize);
  r.Build({WASM_MEMORY0_COPY(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
                             WASM_LOCAL_GET(2)),
           kExprI32Const, 0});

  const uint8_t initial[] = {10, 20, 30};
  memcpy(mem, initial, sizeof(initial));

  // Copy from [0, 3] -> [2, 5]. The copy must not overwrite 30 before copying
  // it (i.e. cannot copy forward in this case).
  CHECK_EQ(0, r.Call(2, 0, 3));
  CheckMemoryEqualsFollowedByZeroes(&r.builder(), {10, 20, 10, 20, 30});

  // Copy from [2, 5] -> [0, 3]. The copy must not write the first 10 (i.e.
  // cannot copy backward in this case).
  CHECK_EQ(0, r.Call(0, 2, 3));
  CheckMemoryEqualsFollowedByZeroes(&r.builder(), {10, 20, 30, 20, 30});
}

WASM_EXEC_TEST(MemoryCopyOutOfBoundsData) {
  WasmRunner<uint32_t, uint32_t, uint32_t, uint32_t> r(execution_tier);
  uint8_t* mem = r.builder().AddMemory(kWasmPageSize);
  r.Build({WASM_MEMORY0_COPY(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
                             WASM_LOCAL_GET(2)),
           kExprI32Const, 0});

  const uint8_t data[] = {11, 22, 33, 44, 55, 66, 77, 88};
  memcpy(mem, data, sizeof(data));

  const uint32_t last_5_bytes = kWasmPageSize - 5;

  CheckMemoryEquals(&r.builder(), last_5_bytes, {0, 0, 0, 0, 0});
  CHECK_EQ(0xDEADBEEF, r.Call(last_5_bytes, 0, 6));
  CheckMemoryEquals(&r.builder(), last_5_bytes, {0, 0, 0, 0, 0});

  r.builder().BlankMemory();
  memcpy(mem + last_5_bytes, data, 5);
  CHECK_EQ(0xDEADBEEF, r.Call(0, last_5_bytes, kWasmPageSize));
  CheckMemoryEquals(&r.builder(), last_5_bytes, {11, 22, 33, 44, 55});

  r.builder().BlankMemory();
  memcpy(mem + last_5_bytes, data, 5);
  CHECK_EQ(0xDEADBEEF, r.Call(last_5_bytes, 0, kWasmPageSize));
  CheckMemoryEquals(&r.builder(), last_5_bytes, {11, 22, 33, 44, 55});
}

WASM_EXEC_TEST(MemoryCopyOutOfBounds) {
  WasmRunner<uint32_t, uint32_t, uint32_t, uint32_t> r(execution_tier);
  r.builder().AddMemory(kWasmPageSize);
  r.Build({WASM_MEMORY0_COPY(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
                             WASM_LOCAL_GET(2)),
           kExprI32Const, 0});

  // Copy full range is OK.
  CHECK_EQ(0, r.Call(0, 0, kWasmPageSize));

  // Source range must not be out of bounds.
  CHECK_EQ(0xDEADBEEF, r.Call(0, 1, kWasmPageSize));
  CHECK_EQ(0xDEADBEEF, r.Call(0, 1000, kWasmPageSize));
  CHECK_EQ(0xDEADBEEF, r.Call(0, kWasmPageSize, 1));

  // Destination range must not be out of bounds.
  CHECK_EQ(0xDEADBEEF, r.Call(1, 0, kWasmPageSize));
  CHECK_EQ(0xDEADBEEF, r.Call(1000, 0, kWasmPageSize));
  CHECK_EQ(0xDEADBEEF, r.Call(kWasmPageSize, 0, 1));

  // Copy 0 out-of-bounds fails if target is invalid.
  CHECK_EQ(0xDEADBEEF, r.Call(kWasmPageSize + 1, 0, 0));
  CHECK_EQ(0xDEADBEEF, r.Call(0, kWasmPageSize + 1, 0));

  // Make sure bounds aren't checked with 32-bit wrapping.
  CHECK_EQ(0xDEADBEEF, r.Call(1, 1, 0xFFFFFFFF));
}

WASM_EXEC_TEST(MemoryFill) {
  WasmRunner<uint32_t, uint32_t, uint32_t, uint32_t> r(execution_tier);
  r.builder().AddMemory(kWasmPageSize);
  r.Build({WASM_MEMORY_FILL(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
                            WASM_LOCAL_GET(2)),
           kExprI32Const, 0});
  CHECK_EQ(0, r.Call(1, 33, 5));
  CheckMemoryEqualsFollowedByZeroes(&r.builder(), {0, 33, 33, 33, 33, 33});

  CHECK_EQ(0, r.Call(4, 66, 4));
  CheckMemoryEqualsFollowedByZeroes(&r.builder(),
                                    {0, 33, 33, 33, 66, 66, 66, 66});

  // Fill 0 bytes does nothing.
  CHECK_EQ(0, r.Call(4, 66, 0));
  CheckMemoryEqualsFollowedByZeroes(&r.builder(),
                                    {0, 33, 33, 33, 66, 66, 66, 66});

  // Fill 0 at end of memory region is OK.
  CHECK_EQ(0, r.Call(kWasmPageSize, 66, 0));
}

WASM_EXEC_TEST(MemoryFillValueWrapsToByte) {
  WasmRunner<uint32_t, uint32_t, uint32_t, uint32_t> r(execution_tier);
  r.builder().AddMemory(kWasmPageSize);
  r.Build({WASM_MEMORY_FILL(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
                            WASM_LOCAL_GET(2)),
           kExprI32Const, 0});
  CHECK_EQ(0, r.Call(0, 1000, 3));
  const uint8_t expected = 1000 & 255;
  CheckMemoryEqualsFollowedByZeroes(&r.builder(),
                                    {expected, expected, expected});
}

WASM_EXEC_TEST(MemoryFillOutOfBoundsData) {
  WasmRunner<uint32_t, uint32_t, uint32_t, uint32_t> r(execution_tier);
  r.builder().AddMemory(kWasmPageSize);
  r.Build({WASM_MEMORY_FILL(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
                            WASM_LOCAL_GET(2)),
           kExprI32Const, 0});
  const uint8_t v = 123;
  CHECK_EQ(0xDEADBEEF, r.Call(kWasmPageSize - 5, v, 999));
  CheckMemoryEquals(&r.builder(), kWasmPageSize - 6, {0, 0, 0, 0, 0, 0});
}

WASM_EXEC_TEST(MemoryFillOutOfBounds) {
  WasmRunner<uint32_t, uint32_t, uint32_t, uint32_t> r(execution_tier);
  r.builder().AddMemory(kWasmPageSize);
  r.Build({WASM_MEMORY_FILL(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
                            WASM_LOCAL_GET(2)),
           kExprI32Const, 0});

  const uint8_t v = 123;

  // Destination range must not be out of bounds.
  CHECK_EQ(0xDEADBEEF, r.Call(1, v, kWasmPageSize));
  CHECK_EQ(0xDEADBEEF, r.Call(1000, v, kWasmPageSize));
  CHECK_EQ(0xDEADBEEF, r.Call(kWasmPageSize, v, 1));

  // Fill 0 out-of-bounds still fails.
  CHECK_EQ(0xDEADBEEF, r.Call(kWasmPageSize + 1, v, 0));

  // Make sure bounds aren't checked with 32-bit wrapping.
  CHECK_EQ(0xDEADBEEF, r.Call(1, v, 0xFFFFFFFF));
}

WASM_EXEC_TEST(DataDropTwice) {
  WasmRunner<uint32_t> r(execution_tier);
  r.builder().AddMemory(kWasmPageSize);
  const uint8_t data[] = {0};
  r.builder().AddPassiveDataSegment(base::ArrayVector(data));
  r.Build({WASM_DATA_DROP(0), kExprI32Const, 0});

  CHECK_EQ(0, r.Call());
  CHECK_EQ(0, r.Call());
}

WASM_EXEC_TEST(DataDropThenMemoryInit) {
  WasmRunner<uint32_t> r(execution_tier);
  r.builder().AddMemory(kWasmPageSize);
  const uint8_t data[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  r.builder().AddPassiveDataSegment(base::ArrayVector(data));
  r.Build({WASM_DATA_DROP(0),
           WASM_MEMORY_INIT(0, WASM_I32V_1(0), WASM_I32V_1(1), WASM_I32V_1(2)),
           kExprI32Const, 0});

  CHECK_EQ(0xDEADBEEF, r.Call());
}

void TestTableCopyInbounds(TestExecutionTier execution_tier, int table_dst,
                           int table_src) {
  WasmRunner<uint32_t, uint32_t, uint32_t, uint32_t> r(execution_tier);
  const uint32_t kTableSize = 5;
  // Add 10 function tables, even though we only test one table.
  for (int i = 0; i < 10; ++i) {
    r.builder().AddIndirectFunctionTable(nullptr, kTableSize);
  }
  r.Build({WASM_TABLE_COPY(table_dst, table_src, WASM_LOCAL_GET(0),
                           WASM_LOCAL_GET(1), WASM_LOCAL_GET(2)),
           kExprI32Const, 0});

  for (uint32_t i = 0; i <= kTableSize; ++i) {
    r.CheckCallViaJS(0, 0, 0, i);  // nop
    r.CheckCallViaJS(0, 0, i, kTableSize - i);
    r.CheckCallViaJS(0, i, 0, kTableSize - i);
  }
}

WASM_COMPILED_EXEC_TEST(TableCopyInboundsFrom0To0) {
  TestTableCopyInbounds(execution_tier, 0, 0);
}

WASM_COMPILED_EXEC_TEST(TableCopyInboundsFrom3To0) {
  TestTableCopyInbounds(execution_tier, 3, 0);
}

WASM_COMPILED_EXEC_TEST(TableCopyInboundsFrom5To9) {
  TestTableCopyInbounds(execution_tier, 5, 9);
}

WASM_COMPILED_EXEC_TEST(TableCopyInboundsFrom6To6) {
  TestTableCopyInbounds(execution_tier, 6, 6);
}

namespace {
template <typename... Args>
void CheckTable(Isolate* isolate, DirectHandle<WasmTableObject> table,
                Args... args) {
  uint32_t args_length = static_cast<uint32_t>(sizeof...(args));
  CHECK_EQ(table->current_length(), args_length);
  Handle<Object> handles[] = {args...};
  for (uint32_t i = 0; i < args_length; ++i) {
    CHECK(WasmTableObject::Get(isolate, table, i).is_identical_to(handles[i]));
  }
}

template <typename WasmRunner, typename... Args>
void CheckTableCall(Isolate* isolate, DirectHandle<WasmTableObject> table,
                    WasmRunner* r, uint32_t function_index, Args... args) {
  uint32_t args_length = static_cast<uint32_t>(sizeof...(args));
  CHECK_EQ(table->current_length(), args_length);
  double expected[] = {args...};
  for (uint32_t i = 0; i < args_length; ++i) {
    Handle<Object> buffer[] = {isolate->factory()->NewNumber(i)};
    r->CheckCallApplyViaJS(expected[i], function_index, buffer, 1);
  }
}
}  // namespace

void TestTableCopyElems(TestExecutionTier execution_tier, int table_dst,
                        int table_src) {
  Isolate* isolate = CcTest::InitIsolateOnce();
  HandleScope scope(isolate);
  TestSignatures sigs;
  WasmRunner<uint32_t, uint32_t, uint32_t, uint32_t> r(execution_tier);
  const uint32_t kTableSize = 5;
  uint16_t function_indexes[kTableSize];
  const uint32_t sig_index = r.builder().AddSignature(sigs.i_v());

  for (uint32_t i = 0; i < kTableSize; ++i) {
    WasmFunctionCompiler& fn = r.NewFunction(sigs.i_v(), "f");
    fn.Build({WASM_I32V_1(i)});
    fn.SetSigIndex(sig_index);
    function_indexes[i] = fn.function_index();
  }

  for (int i = 0; i < 10; ++i) {
    r.builder().AddIndirectFunctionTable(function_indexes, kTableSize);
  }

  r.Build({WASM_TABLE_COPY(table_dst, table_src, WASM_LOCAL_GET(0),
                           WASM_LOCAL_GET(1), WASM_LOCAL_GET(2)),
           kExprI32Const, 0});

  r.builder().InitializeWrapperCache();

  auto table =
      handle(Cast<WasmTableObject>(
                 r.builder().trusted_instance_data()->tables()->get(table_dst)),
             isolate);
  r.CheckCallViaJS(0, 0, 0, kTableSize);
  auto f0 = WasmTableObject::Get(isolate, table, 0);
  auto f1 = WasmTableObject::Get(isolate, table, 1);
  auto f2 = WasmTableObject::Get(isolate, table, 2);
  auto f3 = WasmTableObject::Get(isolate, table, 3);
  auto f4 = WasmTableObject::Get(isolate, table, 4);

  if (table_dst == table_src) {
    CheckTable(isolate, table, f0, f1, f2, f3, f4);
    r.CheckCallViaJS(0, 0, 1, 1);
    CheckTable(isolate, table, f1, f1, f2, f3, f4);
    r.CheckCallViaJS(0, 0, 1, 2);
    CheckTable(isolate, table, f1, f2, f2, f3, f4);
    r.CheckCallViaJS(0, 3, 0, 2);
    CheckTable(isolate, table, f1, f2, f2, f1, f2);
    r.CheckCallViaJS(0, 1, 0, 2);
    CheckTable(isolate, table, f1, f1, f2, f1, f2);
  } else {
    CheckTable(isolate, table, f0, f1, f2, f3, f4);
    r.CheckCallViaJS(0, 0, 1, 1);
    CheckTable(isolate, table, f1, f1, f2, f3, f4);
    r.CheckCallViaJS(0, 0, 1, 2);
    CheckTable(isolate, table, f1, f2, f2, f3, f4);
    r.CheckCallViaJS(0, 3, 0, 2);
    CheckTable(isolate, table, f1, f2, f2, f0, f1);
    r.CheckCallViaJS(0, 1, 0, 2);
    CheckTable(isolate, table, f1, f0, f1, f0, f1);
  }
}

WASM_COMPILED_EXEC_TEST(TableCopyElemsFrom0To0) {
  TestTableCopyElems(execution_tier, 0, 0);
}

WASM_COMPILED_EXEC_TEST(TableCopyElemsFrom3To0) {
  TestTableCopyElems(execution_tier, 3, 0);
}

WASM_COMPILED_EXEC_TEST(TableCopyElemsFrom5To9) {
  TestTableCopyElems(execution_tier, 5, 9);
}

WASM_COMPILED_EXEC_TEST(TableCopyElemsFrom6To6) {
  TestTableCopyElems(execution_tier, 6, 6);
}

void TestTableCopyCalls(TestExecutionTier execution_tier, int table_dst,
                        int table_src) {
  Isolate* isolate = CcTest::InitIsolateOnce();
  HandleScope scope(isolate);
  TestSignatures sigs;
  WasmRunner<uint32_t, uint32_t, uint32_t, uint32_t> r(execution_tier);
  const uint32_t kTableSize = 5;
  uint16_t function_indexes[kTableSize];
  const uint32_t sig_index = r.builder().AddSignature(sigs.i_v());

  for (uint32_t i = 0; i < kTableSize; ++i) {
    WasmFunctionCompiler& fn = r.NewFunction(sigs.i_v(), "f");
    fn.Build({WASM_I32V_1(i)});
    fn.SetSigIndex(sig_index);
    function_indexes[i] = fn.function_index();
  }

  for (int i = 0; i < 10; ++i) {
    r.builder().AddIndirectFunctionTable(function_indexes, kTableSize);
  }

  WasmFunctionCompiler& call = r.NewFunction(sigs.i_i(), "call");
  call.Build(
      {WASM_CALL_INDIRECT_TABLE(table_dst, sig_index, WASM_LOCAL_GET(0))});
  const uint32_t call_index = call.function_index();

  r.Build({WASM_TABLE_COPY(table_dst, table_src, WASM_LOCAL_GET(0),
                           WASM_LOCAL_GET(1), WASM_LOCAL_GET(2)),
           kExprI32Const, 0});

  auto table =
      handle(Cast<WasmTableObject>(
                 r.builder().trusted_instance_data()->tables()->get(table_dst)),
             isolate);

  if (table_dst == table_src) {
    CheckTableCall(isolate, table, &r, call_index, 0.0, 1.0, 2.0, 3.0, 4.0);
    r.CheckCallViaJS(0, 0, 1, 1);
    CheckTableCall(isolate, table, &r, call_index, 1.0, 1.0, 2.0, 3.0, 4.0);
    r.CheckCallViaJS(0, 0, 1, 2);
    CheckTableCall(isolate, table, &r, call_index, 1.0, 2.0, 2.0, 3.0, 4.0);
    r.CheckCallViaJS(0, 3, 0, 2);
    CheckTableCall(isolate, table, &r, call_index, 1.0, 2.0, 2.0, 1.0, 2.0);
  } else {
    CheckTableCall(isolate, table, &r, call_index, 0.0, 1.0, 2.0, 3.0, 4.0);
    r.CheckCallViaJS(0, 0, 1, 1);
    CheckTableCall(isolate, table, &r, call_index, 1.0, 1.0, 2.0, 3.0, 4.0);
    r.CheckCallViaJS(0, 0, 1, 2);
    CheckTableCall(isolate, table, &r, call_index, 1.0, 2.0, 2.0, 3.0, 4.0);
    r.CheckCallViaJS(0, 3, 0, 2);
    CheckTableCall(isolate, table, &r, call_index, 1.0, 2.0, 2.0, 0.0, 1.0);
  }
}

WASM_COMPILED_EXEC_TEST(TableCopyCallsTo0From0) {
  TestTableCopyCalls(execution_tier, 0, 0);
}

WASM_COMPILED_EXEC_TEST(TableCopyCallsTo3From0) {
  TestTableCopyCalls(execution_tier, 3, 0);
}

WASM_COMPILED_EXEC_TEST(TableCopyCallsTo5From9) {
  TestTableCopyCalls(execution_tier, 5, 9);
}

WASM_COMPILED_EXEC_TEST(TableCopyCallsTo6From6) {
  TestTableCopyCalls(execution_tier, 6, 6);
}

void TestTableCopyOobWrites(TestExecutionTier execution_tier, int table_dst,
                            int table_src) {
  Isolate* isolate = CcTest::InitIsolateOnce();
  HandleScope scope(isolate);
  TestSignatures sigs;
  WasmRunner<uint32_t, uint32_t, uint32_t, uint32_t> r(execution_tier);
  const uint32_t kTableSize = 5;
  uint16_t function_indexes[kTableSize];
  const uint32_t sig_index = r.builder().AddSignature(sigs.i_v());

  for (uint32_t i = 0; i < kTableSize; ++i) {
    WasmFunctionCompiler& fn = r.NewFunction(sigs.i_v(), "f");
    fn.Build({WASM_I32V_1(i)});
    fn.SetSigIndex(sig_index);
    function_indexes[i] = fn.function_index();
  }

  for (int i = 0; i < 10; ++i) {
    r.builder().AddIndirectFunctionTable(function_indexes, kTableSize);
  }

  r.Build({WASM_TABLE_COPY(table_dst, table_src, WASM_LOCAL_GET(0),
                           WASM_LOCAL_GET(1), WASM_LOCAL_GET(2)),
           kExprI32Const, 0});

  r.builder().InitializeWrapperCache();

  auto table =
      handle(Cast<WasmTableObject>(
                 r.builder().trusted_instance_data()->tables()->get(table_dst)),
             isolate);
  // Fill the dst table with values from the src table, to make checks easier.
  r.CheckCallViaJS(0, 0, 0, kTableSize);
  auto f0 = WasmTableObject::Get(isolate, table, 0);
  auto f1 = WasmTableObject::Get(isolate, table, 1);
  auto f2 = WasmTableObject::Get(isolate, table, 2);
  auto f3 = WasmTableObject::Get(isolate, table, 3);
  auto f4 = WasmTableObject::Get(isolate, table, 4);

  CheckTable(isolate, table, f0, f1, f2, f3, f4);

  // Failing table.copy should not have any effect.
  r.CheckCallViaJS(0xDEADBEEF, 3, 0, 3);
  CheckTable(isolate, table, f0, f1, f2, f3, f4);

  r.CheckCallViaJS(0xDEADBEEF, 0, 4, 2);
  CheckTable(isolate, table, f0, f1, f2, f3, f4);

  r.CheckCallViaJS(0xDEADBEEF, 3, 0, 99);
  CheckTable(isolate, table, f0, f1, f2, f3, f4);

  r.CheckCallViaJS(0xDEADBEEF, 0, 1, 99);
  CheckTable(isolate, table, f0, f1, f2, f3, f4);
}

WASM_COMPILED_EXEC_TEST(TableCopyOobWritesFrom0To0) {
  TestTableCopyOobWrites(execution_tier, 0, 0);
}

WASM_COMPILED_EXEC_TEST(TableCopyOobWritesFrom3To0) {
  TestTableCopyOobWrites(execution_tier, 3, 0);
}

WASM_COMPILED_EXEC_TEST(TableCopyOobWritesFrom5To9) {
  TestTableCopyOobWrites(execution_tier, 5, 9);
}

WASM_COMPILED_EXEC_TEST(TableCopyOobWritesFrom6To6) {
  TestTableCopyOobWrites(execution_tier, 6, 6);
}

void TestTableCopyOob1(TestExecutionTier execution_tier, int table_dst,
                       int table_src) {
  WasmRunner<uint32_t, uint32_t, uint32_t, uint32_t> r(execution_tier);
  const uint32_t kTableSize = 5;

  for (int i = 0; i < 10; ++i) {
    r.builder().AddIndirectFunctionTable(nullptr, kTableSize);
  }

  r.Build({WASM_TABLE_COPY(table_dst, table_src, WASM_LOCAL_GET(0),
                           WASM_LOCAL_GET(1), WASM_LOCAL_GET(2)),
           kExprI32Const, 0});

  r.CheckCallViaJS(0, 0, 0, 1);           // nop
  r.CheckCallViaJS(0, 0, 0, kTableSize);  // nop
  r.CheckCallViaJS(0xDEADBEEF, 0, 0, kTableSize + 1);
  r.CheckCallViaJS(0xDEADBEEF, 1, 0, kTableSize);
  r.CheckCallViaJS(0xDEADBEEF, 0, 1, kTableSize);

  {
    const uint32_t big = 1000000;
    r.CheckCallViaJS(0xDEADBEEF, big, 0, 0);
    r.CheckCallViaJS(0xDEADBEEF, 0, big, 0);
  }

  for (uint32_t big = 4294967295; big > 1000; big >>= 1) {
    r.CheckCallViaJS(0xDEADBEEF, big, 0, 1);
    r.CheckCallViaJS(0xDEADBEEF, 0, big, 1);
    r.CheckCallViaJS(0xDEADBEEF, 0, 0, big);
  }

  for (uint32_t big = -1000; big != 0; big <<= 1) {
    r.CheckCallViaJS(0xDEADBEEF, big, 0, 1);
    r.CheckCallViaJS(0xDEADBEEF, 0, big, 1);
    r.CheckCallViaJS(0xDEADBEEF, 0, 0, big);
  }
}

WASM_COMPILED_EXEC_TEST(TableCopyOob1From0To0) {
  TestTableCopyOob1(execution_tier, 0, 0);
}

WASM_COMPILED_EXEC_TEST(TableCopyOob1From3To0) {
  TestTableCopyOob1(execution_tier, 3, 0);
}

WASM_COMPILED_EXEC_TEST(TableCopyOob1From5To9) {
  TestTableCopyOob1(execution_tier, 5, 9);
}

WASM_COMPILED_EXEC_TEST(TableCopyOob1From6To6) {
  TestTableCopyOob1(execution_tier, 6, 6);
}

}  // namespace test_run_wasm_bulk_memory
}  // namespace wasm
}  // namespace internal
}  // namespace v8
