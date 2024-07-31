// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/cctest/wasm/wasm-atomics-utils.h"
#include "test/common/wasm/wasm-macro-gen.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace test_run_wasm_atomics {

void RunU32BinOp(TestExecutionTier execution_tier, WasmOpcode wasm_op,
                 Uint32BinOp expected_op) {
  WasmRunner<uint32_t, uint32_t> r(execution_tier);
  uint32_t* memory =
      r.builder().AddMemoryElems<uint32_t>(kWasmPageSize / sizeof(uint32_t));
  r.builder().SetMemoryShared();

  r.Build({WASM_ATOMICS_BINOP(wasm_op, WASM_ZERO, WASM_LOCAL_GET(0),
                              MachineRepresentation::kWord32)});

  FOR_UINT32_INPUTS(i) {
    uint32_t initial = i;
    FOR_UINT32_INPUTS(j) {
      r.builder().WriteMemory(&memory[0], initial);
      CHECK_EQ(initial, r.Call(j));
      uint32_t expected = expected_op(i, j);
      CHECK_EQ(expected, r.builder().ReadMemory(&memory[0]));
    }
  }
}

void RunU32BinOp_Const(TestExecutionTier execution_tier, WasmOpcode wasm_op,
                       Uint32BinOp expected_op) {
  FOR_UINT32_INPUTS(i) {
    WasmRunner<uint32_t> r(execution_tier);
    uint32_t* memory =
        r.builder().AddMemoryElems<uint32_t>(kWasmPageSize / sizeof(uint32_t));

    r.Build({WASM_ATOMICS_BINOP(wasm_op, WASM_ZERO, WASM_I32V(i),
                                MachineRepresentation::kWord32)});

    FOR_UINT32_INPUTS(j) {
      uint32_t initial = j;
      r.builder().WriteMemory(&memory[0], initial);
      CHECK_EQ(initial, r.Call());
      uint32_t expected = expected_op(j, i);
      CHECK_EQ(expected, r.builder().ReadMemory(&memory[0]));
    }
  }
}

#define TEST_OPERATION(Name)                                       \
  WASM_EXEC_TEST(I32Atomic##Name) {                                \
    RunU32BinOp(execution_tier, kExprI32Atomic##Name, Name);       \
    RunU32BinOp_Const(execution_tier, kExprI32Atomic##Name, Name); \
  }
WASM_ATOMIC_OPERATION_LIST(TEST_OPERATION)
#undef TEST_OPERATION

void RunU16BinOp(TestExecutionTier tier, WasmOpcode wasm_op,
                 Uint16BinOp expected_op) {
  WasmRunner<uint32_t, uint32_t> r(tier);
  uint16_t* memory =
      r.builder().AddMemoryElems<uint16_t>(kWasmPageSize / sizeof(uint16_t));
  r.builder().SetMemoryShared();

  r.Build({WASM_ATOMICS_BINOP(wasm_op, WASM_ZERO, WASM_LOCAL_GET(0),
                              MachineRepresentation::kWord16)});

  FOR_UINT16_INPUTS(i) {
    uint16_t initial = i;
    FOR_UINT16_INPUTS(j) {
      r.builder().WriteMemory(&memory[0], initial);
      CHECK_EQ(initial, r.Call(j));
      uint16_t expected = expected_op(i, j);
      CHECK_EQ(expected, r.builder().ReadMemory(&memory[0]));
    }
  }
}

void RunU16BinOp_Const(TestExecutionTier tier, WasmOpcode wasm_op,
                       Uint16BinOp expected_op) {
  FOR_UINT16_INPUTS(i) {
    WasmRunner<uint32_t> r(tier);
    uint16_t* memory =
        r.builder().AddMemoryElems<uint16_t>(kWasmPageSize / sizeof(uint16_t));

    r.Build({WASM_ATOMICS_BINOP(wasm_op, WASM_ZERO, WASM_I32V(i),
                                MachineRepresentation::kWord16)});

    FOR_UINT16_INPUTS(j) {
      uint16_t initial = j;
      r.builder().WriteMemory(&memory[0], initial);
      CHECK_EQ(initial, r.Call());
      uint16_t expected = expected_op(j, i);
      CHECK_EQ(expected, r.builder().ReadMemory(&memory[0]));
    }
  }
}

#define TEST_OPERATION(Name)                                            \
  WASM_EXEC_TEST(I32Atomic##Name##16U) {                                \
    RunU16BinOp(execution_tier, kExprI32Atomic##Name##16U, Name);       \
    RunU16BinOp_Const(execution_tier, kExprI32Atomic##Name##16U, Name); \
  }
WASM_ATOMIC_OPERATION_LIST(TEST_OPERATION)
#undef TEST_OPERATION

void RunU8BinOp(TestExecutionTier execution_tier, WasmOpcode wasm_op,
                Uint8BinOp expected_op) {
  WasmRunner<uint32_t, uint32_t> r(execution_tier);
  uint8_t* memory = r.builder().AddMemoryElems<uint8_t>(kWasmPageSize);
  r.builder().SetMemoryShared();

  r.Build({WASM_ATOMICS_BINOP(wasm_op, WASM_ZERO, WASM_LOCAL_GET(0),
                              MachineRepresentation::kWord8)});

  FOR_UINT8_INPUTS(i) {
    uint8_t initial = i;
    FOR_UINT8_INPUTS(j) {
      r.builder().WriteMemory(&memory[0], initial);
      CHECK_EQ(initial, r.Call(j));
      uint8_t expected = expected_op(i, j);
      CHECK_EQ(expected, r.builder().ReadMemory(&memory[0]));
    }
  }
}

void RunU8BinOp_Const(TestExecutionTier execution_tier, WasmOpcode wasm_op,
                      Uint8BinOp expected_op) {
  FOR_UINT8_INPUTS(i) {
    WasmRunner<uint32_t> r(execution_tier);
    uint8_t* memory = r.builder().AddMemoryElems<uint8_t>(kWasmPageSize);

    r.Build({WASM_ATOMICS_BINOP(wasm_op, WASM_ZERO, WASM_I32V(i),
                                MachineRepresentation::kWord8)});

    FOR_UINT8_INPUTS(j) {
      uint8_t initial = j;
      r.builder().WriteMemory(&memory[0], initial);
      CHECK_EQ(initial, r.Call());
      uint8_t expected = expected_op(j, i);
      CHECK_EQ(expected, r.builder().ReadMemory(&memory[0]));
    }
  }
}

#define TEST_OPERATION(Name)                                          \
  WASM_EXEC_TEST(I32Atomic##Name##8U) {                               \
    RunU8BinOp(execution_tier, kExprI32Atomic##Name##8U, Name);       \
    RunU8BinOp_Const(execution_tier, kExprI32Atomic##Name##8U, Name); \
  }
WASM_ATOMIC_OPERATION_LIST(TEST_OPERATION)
#undef TEST_OPERATION

void RunU64BinOp(TestExecutionTier execution_tier, WasmOpcode wasm_op,
                 Uint64BinOp expected_op) {
  WasmRunner<uint64_t, uint64_t> r(execution_tier);
  uint64_t* memory =
      r.builder().AddMemoryElems<uint64_t>(kWasmPageSize / sizeof(uint64_t));

  r.Build({WASM_ATOMICS_BINOP(wasm_op, WASM_ZERO, WASM_LOCAL_GET(0),
                              MachineRepresentation::kWord64)});

  FOR_UINT64_INPUTS(i) {
    uint64_t initial = i;
    FOR_UINT64_INPUTS(j) {
      r.builder().WriteMemory(&memory[0], initial);
      CHECK_EQ(initial, r.Call(j));
      uint64_t expected = expected_op(i, j);
      CHECK_EQ(expected, r.builder().ReadMemory(&memory[0]));
    }
  }
}

void RunU64BinOp_Const(TestExecutionTier execution_tier, WasmOpcode wasm_op,
                       Uint64BinOp expected_op) {
  FOR_UINT64_INPUTS(i) {
    WasmRunner<uint64_t> r(execution_tier);
    uint64_t* memory =
        r.builder().AddMemoryElems<uint64_t>(kWasmPageSize / sizeof(uint64_t));

    r.Build({WASM_ATOMICS_BINOP(wasm_op, WASM_ZERO, WASM_I64V_10(i),
                                MachineRepresentation::kWord64)});

    FOR_UINT64_INPUTS(j) {
      uint64_t initial = j;
      r.builder().WriteMemory(&memory[0], initial);
      CHECK_EQ(initial, r.Call());
      uint64_t expected = expected_op(j, i);
      CHECK_EQ(expected, r.builder().ReadMemory(&memory[0]));
    }
  }
}

#define TEST_OPERATION(Name)                                       \
  WASM_EXEC_TEST(I64Atomic##Name) {                                \
    RunU64BinOp(execution_tier, kExprI64Atomic##Name, Name);       \
    RunU64BinOp_Const(execution_tier, kExprI64Atomic##Name, Name); \
  }
WASM_ATOMIC_OPERATION_LIST(TEST_OPERATION)
#undef TEST_OPERATION

WASM_EXEC_TEST(I32AtomicCompareExchange) {
  WasmRunner<uint32_t, uint32_t, uint32_t> r(execution_tier);
  uint32_t* memory =
      r.builder().AddMemoryElems<uint32_t>(kWasmPageSize / sizeof(uint32_t));
  r.builder().SetMemoryShared();
  r.Build({WASM_ATOMICS_TERNARY_OP(kExprI32AtomicCompareExchange, WASM_ZERO,
                                   WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
                                   MachineRepresentation::kWord32)});

  FOR_UINT32_INPUTS(i) {
    uint32_t initial = i;
    FOR_UINT32_INPUTS(j) {
      r.builder().WriteMemory(&memory[0], initial);
      CHECK_EQ(initial, r.Call(i, j));
      uint32_t expected = CompareExchange(initial, i, j);
      CHECK_EQ(expected, r.builder().ReadMemory(&memory[0]));
    }
  }
}

WASM_EXEC_TEST(I32AtomicCompareExchange16U) {
  WasmRunner<uint32_t, uint32_t, uint32_t> r(execution_tier);
  uint16_t* memory =
      r.builder().AddMemoryElems<uint16_t>(kWasmPageSize / sizeof(uint16_t));
  r.builder().SetMemoryShared();
  r.Build({WASM_ATOMICS_TERNARY_OP(kExprI32AtomicCompareExchange16U, WASM_ZERO,
                                   WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
                                   MachineRepresentation::kWord16)});

  FOR_UINT16_INPUTS(i) {
    uint16_t initial = i;
    FOR_UINT16_INPUTS(j) {
      r.builder().WriteMemory(&memory[0], initial);
      CHECK_EQ(initial, r.Call(i, j));
      uint16_t expected = CompareExchange(initial, i, j);
      CHECK_EQ(expected, r.builder().ReadMemory(&memory[0]));
    }
  }
}

WASM_EXEC_TEST(I32AtomicCompareExchange8U) {
  WasmRunner<uint32_t, uint32_t, uint32_t> r(execution_tier);
  uint8_t* memory = r.builder().AddMemoryElems<uint8_t>(kWasmPageSize);
  r.builder().SetMemoryShared();
  r.Build({WASM_ATOMICS_TERNARY_OP(kExprI32AtomicCompareExchange8U, WASM_ZERO,
                                   WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
                                   MachineRepresentation::kWord8)});

  FOR_UINT8_INPUTS(i) {
    uint8_t initial = i;
    FOR_UINT8_INPUTS(j) {
      r.builder().WriteMemory(&memory[0], initial);
      CHECK_EQ(initial, r.Call(i, j));
      uint8_t expected = CompareExchange(initial, i, j);
      CHECK_EQ(expected, r.builder().ReadMemory(&memory[0]));
    }
  }
}

WASM_EXEC_TEST(I32AtomicCompareExchange_fail) {
  WasmRunner<uint32_t, uint32_t, uint32_t> r(execution_tier);
  uint32_t* memory =
      r.builder().AddMemoryElems<uint32_t>(kWasmPageSize / sizeof(uint32_t));
  r.builder().SetMemoryShared();
  r.Build({WASM_ATOMICS_TERNARY_OP(kExprI32AtomicCompareExchange, WASM_ZERO,
                                   WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
                                   MachineRepresentation::kWord32)});

  // The original value at the memory location.
  uint32_t old_val = 4;
  // The value we use as the expected value for the compare-exchange so that it
  // fails.
  uint32_t expected = 6;
  // The new value for the compare-exchange.
  uint32_t new_val = 5;

  r.builder().WriteMemory(&memory[0], old_val);
  CHECK_EQ(old_val, r.Call(expected, new_val));
}

WASM_EXEC_TEST(I32AtomicLoad) {
  WasmRunner<uint32_t> r(execution_tier);
  uint32_t* memory =
      r.builder().AddMemoryElems<uint32_t>(kWasmPageSize / sizeof(uint32_t));
  r.builder().SetMemoryShared();
  r.Build({WASM_ATOMICS_LOAD_OP(kExprI32AtomicLoad, WASM_ZERO,
                                MachineRepresentation::kWord32)});

  FOR_UINT32_INPUTS(i) {
    uint32_t expected = i;
    r.builder().WriteMemory(&memory[0], expected);
    CHECK_EQ(expected, r.Call());
  }
}

WASM_EXEC_TEST(I32AtomicLoad16U) {
  WasmRunner<uint32_t> r(execution_tier);
  uint16_t* memory =
      r.builder().AddMemoryElems<uint16_t>(kWasmPageSize / sizeof(uint16_t));
  r.builder().SetMemoryShared();
  r.Build({WASM_ATOMICS_LOAD_OP(kExprI32AtomicLoad16U, WASM_ZERO,
                                MachineRepresentation::kWord16)});

  FOR_UINT16_INPUTS(i) {
    uint16_t expected = i;
    r.builder().WriteMemory(&memory[0], expected);
    CHECK_EQ(expected, r.Call());
  }
}

WASM_EXEC_TEST(I32AtomicLoad8U) {
  WasmRunner<uint32_t> r(execution_tier);
  uint8_t* memory = r.builder().AddMemoryElems<uint8_t>(kWasmPageSize);
  r.builder().SetMemoryShared();
  r.Build({WASM_ATOMICS_LOAD_OP(kExprI32AtomicLoad8U, WASM_ZERO,
                                MachineRepresentation::kWord8)});

  FOR_UINT8_INPUTS(i) {
    uint8_t expected = i;
    r.builder().WriteMemory(&memory[0], expected);
    CHECK_EQ(expected, r.Call());
  }
}

WASM_EXEC_TEST(I32AtomicStoreLoad) {
  WasmRunner<uint32_t, uint32_t> r(execution_tier);
  uint32_t* memory =
      r.builder().AddMemoryElems<uint32_t>(kWasmPageSize / sizeof(uint32_t));
  r.builder().SetMemoryShared();

  r.Build(
      {WASM_ATOMICS_STORE_OP(kExprI32AtomicStore, WASM_ZERO, WASM_LOCAL_GET(0),
                             MachineRepresentation::kWord32),
       WASM_ATOMICS_LOAD_OP(kExprI32AtomicLoad, WASM_ZERO,
                            MachineRepresentation::kWord32)});

  FOR_UINT32_INPUTS(i) {
    uint32_t expected = i;
    CHECK_EQ(expected, r.Call(i));
    CHECK_EQ(expected, r.builder().ReadMemory(&memory[0]));
  }
}

WASM_EXEC_TEST(I32AtomicStoreLoad16U) {
  WasmRunner<uint32_t, uint32_t> r(execution_tier);
  uint16_t* memory =
      r.builder().AddMemoryElems<uint16_t>(kWasmPageSize / sizeof(uint16_t));
  r.builder().SetMemoryShared();

  r.Build(
      {WASM_ATOMICS_STORE_OP(kExprI32AtomicStore16U, WASM_ZERO,
                             WASM_LOCAL_GET(0), MachineRepresentation::kWord16),
       WASM_ATOMICS_LOAD_OP(kExprI32AtomicLoad16U, WASM_ZERO,
                            MachineRepresentation::kWord16)});

  FOR_UINT16_INPUTS(i) {
    uint16_t expected = i;
    CHECK_EQ(expected, r.Call(i));
    CHECK_EQ(expected, r.builder().ReadMemory(&memory[0]));
  }
}

WASM_EXEC_TEST(I32AtomicStoreLoad8U) {
  WasmRunner<uint32_t, uint32_t> r(execution_tier);
  uint8_t* memory = r.builder().AddMemoryElems<uint8_t>(kWasmPageSize);
  r.builder().SetMemoryShared();

  r.Build(
      {WASM_ATOMICS_STORE_OP(kExprI32AtomicStore8U, WASM_ZERO,
                             WASM_LOCAL_GET(0), MachineRepresentation::kWord8),
       WASM_ATOMICS_LOAD_OP(kExprI32AtomicLoad8U, WASM_ZERO,
                            MachineRepresentation::kWord8)});

  FOR_UINT8_INPUTS(i) {
    uint8_t expected = i;
    CHECK_EQ(expected, r.Call(i));
    CHECK_EQ(i, r.builder().ReadMemory(&memory[0]));
  }
}

WASM_EXEC_TEST(I32AtomicStoreParameter) {
  WasmRunner<uint32_t, uint32_t> r(execution_tier);
  uint32_t* memory =
      r.builder().AddMemoryElems<uint32_t>(kWasmPageSize / sizeof(uint32_t));
  r.builder().SetMemoryShared();

  r.Build(
      {WASM_ATOMICS_STORE_OP(kExprI32AtomicStore, WASM_ZERO, WASM_LOCAL_GET(0),
                             MachineRepresentation::kWord32),
       WASM_ATOMICS_BINOP(kExprI32AtomicAdd, WASM_ZERO, WASM_LOCAL_GET(0),
                          MachineRepresentation::kWord32)});
  CHECK_EQ(10, r.Call(10));
  CHECK_EQ(20, r.builder().ReadMemory(&memory[0]));
}

WASM_EXEC_TEST(AtomicFence) {
  WasmRunner<uint32_t> r(execution_tier);
  // Note that this test specifically doesn't use a shared memory, as the fence
  // instruction does not target a particular linear memory. It may occur in
  // modules which declare no memory, or a non-shared memory, without causing a
  // validation error.

  r.Build({WASM_ATOMICS_FENCE, WASM_ZERO});
  CHECK_EQ(0, r.Call());
}

WASM_EXEC_TEST(AtomicStoreNoConsideredEffectful) {
  // Use {Load} instead of {ProtectedLoad}.
  FLAG_SCOPE(wasm_enforce_bounds_checks);
  WasmRunner<uint32_t> r(execution_tier);
  r.builder().AddMemoryElems<int32_t>(kWasmPageSize / sizeof(int32_t));
  r.builder().SetMemoryShared();
  r.Build(
      {WASM_LOAD_MEM(MachineType::Int64(), WASM_ZERO),
       WASM_ATOMICS_STORE_OP(kExprI32AtomicStore, WASM_ZERO, WASM_I32V_1(20),
                             MachineRepresentation::kWord32),
       kExprI64Eqz});
  CHECK_EQ(1, r.Call());
}

void RunNoEffectTest(TestExecutionTier execution_tier, WasmOpcode wasm_op) {
  // Use {Load} instead of {ProtectedLoad}.
  FLAG_SCOPE(wasm_enforce_bounds_checks);
  WasmRunner<uint32_t> r(execution_tier);
  r.builder().AddMemoryElems<int32_t>(kWasmPageSize / sizeof(int32_t));
  r.builder().SetMemoryShared();
  r.Build({WASM_LOAD_MEM(MachineType::Int64(), WASM_ZERO),
           WASM_ATOMICS_BINOP(wasm_op, WASM_ZERO, WASM_I32V_1(20),
                              MachineRepresentation::kWord32),
           WASM_DROP, kExprI64Eqz});
  CHECK_EQ(1, r.Call());
}

WASM_EXEC_TEST(AtomicAddNoConsideredEffectful) {
  RunNoEffectTest(execution_tier, kExprI32AtomicAdd);
}

WASM_EXEC_TEST(AtomicExchangeNoConsideredEffectful) {
  RunNoEffectTest(execution_tier, kExprI32AtomicExchange);
}

WASM_EXEC_TEST(AtomicCompareExchangeNoConsideredEffectful) {
  // Use {Load} instead of {ProtectedLoad}.
  FLAG_SCOPE(wasm_enforce_bounds_checks);
  WasmRunner<uint32_t> r(execution_tier);
  r.builder().AddMemoryElems<int32_t>(kWasmPageSize / sizeof(int32_t));
  r.builder().SetMemoryShared();
  r.Build({WASM_LOAD_MEM(MachineType::Int32(), WASM_ZERO),
           WASM_ATOMICS_TERNARY_OP(kExprI32AtomicCompareExchange, WASM_ZERO,
                                   WASM_ZERO, WASM_I32V_1(30),
                                   MachineRepresentation::kWord32),
           WASM_DROP, kExprI32Eqz});
  CHECK_EQ(1, r.Call());
}

WASM_EXEC_TEST(I32AtomicLoad_trap) {
  WasmRunner<uint32_t> r(execution_tier);
  r.builder().AddMemory(kWasmPageSize);
  r.builder().SetMemoryShared();
  r.Build({WASM_ATOMICS_LOAD_OP(kExprI32AtomicLoad, WASM_I32V_3(kWasmPageSize),
                                MachineRepresentation::kWord32)});
  CHECK_TRAP(r.Call());
}

WASM_EXEC_TEST(I64AtomicLoad_trap) {
  WasmRunner<uint64_t> r(execution_tier);
  r.builder().AddMemory(kWasmPageSize);
  r.builder().SetMemoryShared();
  r.Build({WASM_ATOMICS_LOAD_OP(kExprI64AtomicLoad, WASM_I32V_3(kWasmPageSize),
                                MachineRepresentation::kWord64)});
  CHECK_TRAP64(r.Call());
}

WASM_EXEC_TEST(I32AtomicStore_trap) {
  WasmRunner<uint32_t> r(execution_tier);
  r.builder().AddMemory(kWasmPageSize);
  r.builder().SetMemoryShared();
  r.Build(
      {WASM_ATOMICS_STORE_OP(kExprI32AtomicStore, WASM_I32V_3(kWasmPageSize),
                             WASM_ZERO, MachineRepresentation::kWord32),
       WASM_ZERO});
  CHECK_TRAP(r.Call());
}

WASM_EXEC_TEST(I64AtomicStore_trap) {
  WasmRunner<uint32_t> r(execution_tier);
  r.builder().AddMemory(kWasmPageSize);
  r.builder().SetMemoryShared();
  r.Build(
      {WASM_ATOMICS_STORE_OP(kExprI64AtomicStore, WASM_I32V_3(kWasmPageSize),
                             WASM_ZERO64, MachineRepresentation::kWord64),
       WASM_ZERO});
  CHECK_TRAP(r.Call());
}

WASM_EXEC_TEST(I32AtomicLoad_NotOptOut) {
  WasmRunner<uint32_t> r(execution_tier);
  r.builder().AddMemory(kWasmPageSize);
  r.builder().SetMemoryShared();
  r.Build({WASM_I32_AND(
      WASM_ATOMICS_LOAD_OP(kExprI32AtomicLoad, WASM_I32V_3(kWasmPageSize),
                           MachineRepresentation::kWord32),
      WASM_ZERO)});
  CHECK_TRAP(r.Call());
}

void RunU32BinOp_OOB(TestExecutionTier execution_tier, WasmOpcode wasm_op) {
  WasmRunner<uint32_t> r(execution_tier);
  r.builder().AddMemory(kWasmPageSize);
  r.builder().SetMemoryShared();

  r.Build({WASM_ATOMICS_BINOP(wasm_op, WASM_I32V_3(kWasmPageSize), WASM_ZERO,
                              MachineRepresentation::kWord32)});

  CHECK_TRAP(r.Call());
}

#define TEST_OPERATION(Name)                               \
  WASM_EXEC_TEST(OOB_I32Atomic##Name) {                    \
    RunU32BinOp_OOB(execution_tier, kExprI32Atomic##Name); \
  }
WASM_ATOMIC_OPERATION_LIST(TEST_OPERATION)
#undef TEST_OPERATION

void RunU64BinOp_OOB(TestExecutionTier execution_tier, WasmOpcode wasm_op) {
  WasmRunner<uint64_t> r(execution_tier);
  r.builder().AddMemory(kWasmPageSize);
  r.builder().SetMemoryShared();

  r.Build({WASM_ATOMICS_BINOP(wasm_op, WASM_I32V_3(kWasmPageSize), WASM_ZERO64,
                              MachineRepresentation::kWord64)});

  CHECK_TRAP64(r.Call());
}

#define TEST_OPERATION(Name)                               \
  WASM_EXEC_TEST(OOB_I64Atomic##Name) {                    \
    RunU64BinOp_OOB(execution_tier, kExprI64Atomic##Name); \
  }
WASM_ATOMIC_OPERATION_LIST(TEST_OPERATION)
#undef TEST_OPERATION

WASM_EXEC_TEST(I32AtomicCompareExchange_trap) {
  WasmRunner<uint32_t, uint32_t, uint32_t> r(execution_tier);
  uint32_t* memory =
      r.builder().AddMemoryElems<uint32_t>(kWasmPageSize / sizeof(uint32_t));
  r.builder().SetMemoryShared();
  r.Build({WASM_ATOMICS_TERNARY_OP(
      kExprI32AtomicCompareExchange, WASM_I32V_3(kWasmPageSize),
      WASM_LOCAL_GET(0), WASM_LOCAL_GET(1), MachineRepresentation::kWord32)});

  FOR_UINT32_INPUTS(i) {
    uint32_t initial = i;
    FOR_UINT32_INPUTS(j) {
      r.builder().WriteMemory(&memory[0], initial);
      CHECK_TRAP(r.Call(i, j));
    }
  }
}

WASM_EXEC_TEST(I64AtomicCompareExchange_trap) {
  WasmRunner<uint64_t> r(execution_tier);
  r.builder().AddMemory(kWasmPageSize);
  r.builder().SetMemoryShared();
  r.Build({WASM_ATOMICS_TERNARY_OP(
      kExprI64AtomicCompareExchange, WASM_I32V_3(kWasmPageSize), WASM_ZERO64,
      WASM_ZERO64, MachineRepresentation::kWord64)});

  CHECK_TRAP64(r.Call());
}

}  // namespace test_run_wasm_atomics
}  // namespace wasm
}  // namespace internal
}  // namespace v8
