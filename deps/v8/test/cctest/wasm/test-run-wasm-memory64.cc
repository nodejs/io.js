// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-opcodes-inl.h"
#include "test/cctest/cctest.h"
#include "test/cctest/wasm/wasm-run-utils.h"
#include "test/common/wasm/test-signatures.h"
#include "test/common/wasm/wasm-macro-gen.h"
#include "test/common/wasm/wasm-module-runner.h"

namespace v8::internal::wasm {

template <typename ReturnType, typename... ParamTypes>
class Memory64Runner : public WasmRunner<ReturnType, ParamTypes...> {
 public:
  explicit Memory64Runner(TestExecutionTier execution_tier)
      : WasmRunner<ReturnType, ParamTypes...>(execution_tier, kWasmOrigin,
                                              nullptr, "main") {
    this->builder().EnableFeature(WasmEnabledFeature::memory64);
  }

  template <typename T>
  T* AddMemoryElems(uint32_t count) {
    return this->builder().template AddMemoryElems<T>(count, kMemory64);
  }

  uint8_t* AddMemory(uint32_t size, size_t max_size,
                     SharedFlag shared = SharedFlag::kNotShared) {
    return this->builder().AddMemory(size, shared, kMemory64, max_size);
  }
};

WASM_EXEC_TEST(Load) {
  Memory64Runner<uint32_t, uint64_t> r(execution_tier);
  uint32_t* memory =
      r.AddMemoryElems<uint32_t>(kWasmPageSize / sizeof(int32_t));

  r.Build({WASM_LOAD_MEM(MachineType::Int32(), WASM_LOCAL_GET(0))});

  CHECK_EQ(0, r.Call(0));

#if V8_TARGET_BIG_ENDIAN
  memory[0] = 0x78563412;
#else
  memory[0] = 0x12345678;
#endif
  CHECK_EQ(0x12345678, r.Call(0));
  CHECK_EQ(0x123456, r.Call(1));
  CHECK_EQ(0x1234, r.Call(2));
  CHECK_EQ(0x12, r.Call(3));
  CHECK_EQ(0x0, r.Call(4));

  CHECK_TRAP(r.Call(-1));
  CHECK_TRAP(r.Call(kWasmPageSize));
  CHECK_TRAP(r.Call(kWasmPageSize - 3));
  CHECK_EQ(0x0, r.Call(kWasmPageSize - 4));
  CHECK_TRAP(r.Call(uint64_t{1} << 32));
}

// TODO(clemensb): Test atomic instructions.

WASM_EXEC_TEST(InitExpression) {
  EXPERIMENTAL_FLAG_SCOPE(memory64);
  Isolate* isolate = CcTest::InitIsolateOnce();
  HandleScope scope(isolate);

  ErrorThrower thrower(isolate, "TestMemory64InitExpression");

  const uint8_t data[] = {
      WASM_MODULE_HEADER,                     //
      SECTION(Memory,                         //
              ENTRY_COUNT(1),                 //
              kMemory64WithMaximum,           // type
              1,                              // initial size
              2),                             // maximum size
      SECTION(Data,                           //
              ENTRY_COUNT(1),                 //
              0,                              // linear memory index
              WASM_I64V_3(0xFFFF), kExprEnd,  // destination offset
              U32V_1(1),                      // source size
              'c')                            // data bytes
  };

  testing::CompileAndInstantiateForTesting(
      isolate, &thrower, ModuleWireBytes(data, data + arraysize(data)));
  if (thrower.error()) {
    Print(*thrower.Reify());
    FATAL("compile or instantiate error");
  }
}

WASM_EXEC_TEST(MemorySize) {
  Memory64Runner<uint64_t> r(execution_tier);
  constexpr int kNumPages = 13;
  r.AddMemoryElems<uint8_t>(kNumPages * kWasmPageSize);

  r.Build({WASM_MEMORY_SIZE});

  CHECK_EQ(kNumPages, r.Call());
}

WASM_EXEC_TEST(MemoryGrow) {
  Memory64Runner<int64_t, int64_t> r(execution_tier);
  r.AddMemory(kWasmPageSize, 13 * kWasmPageSize);

  r.Build({WASM_MEMORY_GROW(WASM_LOCAL_GET(0))});
  CHECK_EQ(1, r.Call(6));
  CHECK_EQ(7, r.Call(1));
  CHECK_EQ(-1, r.Call(-1));
  CHECK_EQ(-1, r.Call(int64_t{1} << 31));
  CHECK_EQ(-1, r.Call(int64_t{1} << 32));
  CHECK_EQ(-1, r.Call(int64_t{1} << 33));
  CHECK_EQ(-1, r.Call(int64_t{1} << 63));
  CHECK_EQ(-1, r.Call(6));  // Above the maximum of 13.
  CHECK_EQ(8, r.Call(5));   // Just at the maximum of 13.
}

}  // namespace v8::internal::wasm
