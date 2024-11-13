// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/cctest/wasm/wasm-simd-utils.h"

#include <cmath>
#include <type_traits>

#include "src/base/logging.h"
#include "src/base/memory.h"
#include "src/common/globals.h"
#include "src/wasm/compilation-environment.h"
#include "src/wasm/value-type.h"
#include "src/wasm/wasm-opcodes-inl.h"
#include "src/wasm/wasm-opcodes.h"
#include "test/cctest/wasm/wasm-run-utils.h"
#include "test/common/c-signature.h"
#include "test/common/value-helper.h"
#include "test/common/wasm/wasm-macro-gen.h"
#include "third_party/fp16/src/include/fp16.h"

namespace v8 {
namespace internal {
namespace wasm {
void RunI8x16UnOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                      Int8UnOp expected_op) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // Global to hold output.
  int8_t* g = r.builder().AddGlobal<int8_t>(kWasmS128);
  // Build fn to splat test value, perform unop, and write the result.
  uint8_t value = 0;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_I8x16_SPLAT(WASM_LOCAL_GET(value))),
           WASM_GLOBAL_SET(0, WASM_SIMD_UNOP(opcode, WASM_LOCAL_GET(temp1))),
           WASM_ONE});

  FOR_INT8_INPUTS(x) {
    r.Call(x);
    int8_t expected = expected_op(x);
    for (int i = 0; i < 16; i++) {
      CHECK_EQ(expected, LANE(g, i));
    }
  }
}

template <typename T, typename OpType>
void RunI8x16BinOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                       OpType expected_op) {
  WasmRunner<int32_t, T, T> r(execution_tier);
  // Global to hold output.
  T* g = r.builder().template AddGlobal<T>(kWasmS128);
  // Build fn to splat test values, perform binop, and write the result.
  uint8_t value1 = 0, value2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_I8x16_SPLAT(WASM_LOCAL_GET(value1))),
           WASM_LOCAL_SET(temp2, WASM_SIMD_I8x16_SPLAT(WASM_LOCAL_GET(value2))),
           WASM_GLOBAL_SET(0, WASM_SIMD_BINOP(opcode, WASM_LOCAL_GET(temp1),
                                              WASM_LOCAL_GET(temp2))),
           WASM_ONE});

  for (T x : compiler::ValueHelper::GetVector<T>()) {
    for (T y : compiler::ValueHelper::GetVector<T>()) {
      r.Call(x, y);
      T expected = expected_op(x, y);
      for (int i = 0; i < 16; i++) {
        CHECK_EQ(expected, LANE(g, i));
      }
    }
  }
}

// Explicit instantiations of uses.
template void RunI8x16BinOpTest<int8_t>(TestExecutionTier, WasmOpcode,
                                        Int8BinOp);

template void RunI8x16BinOpTest<uint8_t>(TestExecutionTier, WasmOpcode,
                                         Uint8BinOp);

void RunI8x16ShiftOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                         Int8ShiftOp expected_op) {
  // Intentionally shift by 8, should be no-op.
  for (int shift = 1; shift <= 8; shift++) {
    WasmRunner<int32_t, int32_t> r(execution_tier);
    int32_t* memory = r.builder().AddMemoryElems<int32_t>(1);
    int8_t* g_imm = r.builder().AddGlobal<int8_t>(kWasmS128);
    int8_t* g_mem = r.builder().AddGlobal<int8_t>(kWasmS128);
    uint8_t value = 0;
    uint8_t simd = r.AllocateLocal(kWasmS128);
    // Shift using an immediate, and shift using a value loaded from memory.
    r.Build({WASM_LOCAL_SET(simd, WASM_SIMD_I8x16_SPLAT(WASM_LOCAL_GET(value))),
             WASM_GLOBAL_SET(0, WASM_SIMD_SHIFT_OP(opcode, WASM_LOCAL_GET(simd),
                                                   WASM_I32V(shift))),
             WASM_GLOBAL_SET(
                 1, WASM_SIMD_SHIFT_OP(
                        opcode, WASM_LOCAL_GET(simd),
                        WASM_LOAD_MEM(MachineType::Int32(), WASM_ZERO))),
             WASM_ONE});

    r.builder().WriteMemory(&memory[0], shift);
    FOR_INT8_INPUTS(x) {
      r.Call(x);
      int8_t expected = expected_op(x, shift);
      for (int i = 0; i < 16; i++) {
        CHECK_EQ(expected, LANE(g_imm, i));
        CHECK_EQ(expected, LANE(g_mem, i));
      }
    }
  }
}

void RunI8x16MixedRelationalOpTest(TestExecutionTier execution_tier,
                                   WasmOpcode opcode, Int8BinOp expected_op) {
  WasmRunner<int32_t, int32_t, int32_t> r(execution_tier);
  uint8_t value1 = 0, value2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_I8x16_SPLAT(WASM_LOCAL_GET(value1))),
           WASM_LOCAL_SET(temp2, WASM_SIMD_I16x8_SPLAT(WASM_LOCAL_GET(value2))),
           WASM_LOCAL_SET(temp3, WASM_SIMD_BINOP(opcode, WASM_LOCAL_GET(temp1),
                                                 WASM_LOCAL_GET(temp2))),
           WASM_SIMD_I8x16_EXTRACT_LANE(0, WASM_LOCAL_GET(temp3))});

  CHECK_EQ(expected_op(0xff, static_cast<uint8_t>(0x7fff)),
           r.Call(0xff, 0x7fff));
  CHECK_EQ(expected_op(0xfe, static_cast<uint8_t>(0x7fff)),
           r.Call(0xfe, 0x7fff));
  CHECK_EQ(expected_op(0xff, static_cast<uint8_t>(0x7ffe)),
           r.Call(0xff, 0x7ffe));
}

void RunI16x8UnOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                      Int16UnOp expected_op) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // Global to hold output.
  int16_t* g = r.builder().AddGlobal<int16_t>(kWasmS128);
  // Build fn to splat test value, perform unop, and write the result.
  uint8_t value = 0;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_I16x8_SPLAT(WASM_LOCAL_GET(value))),
           WASM_GLOBAL_SET(0, WASM_SIMD_UNOP(opcode, WASM_LOCAL_GET(temp1))),
           WASM_ONE});

  FOR_INT16_INPUTS(x) {
    r.Call(x);
    int16_t expected = expected_op(x);
    for (int i = 0; i < 8; i++) {
      CHECK_EQ(expected, LANE(g, i));
    }
  }
}

template <typename T, typename OpType>
void RunI16x8BinOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                       OpType expected_op) {
  WasmRunner<int32_t, T, T> r(execution_tier);
  // Global to hold output.
  T* g = r.builder().template AddGlobal<T>(kWasmS128);
  // Build fn to splat test values, perform binop, and write the result.
  uint8_t value1 = 0, value2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_I16x8_SPLAT(WASM_LOCAL_GET(value1))),
           WASM_LOCAL_SET(temp2, WASM_SIMD_I16x8_SPLAT(WASM_LOCAL_GET(value2))),
           WASM_GLOBAL_SET(0, WASM_SIMD_BINOP(opcode, WASM_LOCAL_GET(temp1),
                                              WASM_LOCAL_GET(temp2))),
           WASM_ONE});

  for (T x : compiler::ValueHelper::GetVector<T>()) {
    for (T y : compiler::ValueHelper::GetVector<T>()) {
      r.Call(x, y);
      T expected = expected_op(x, y);
      for (int i = 0; i < 8; i++) {
        CHECK_EQ(expected, LANE(g, i));
      }
    }
  }
}

// Explicit instantiations of uses.
template void RunI16x8BinOpTest<int16_t>(TestExecutionTier, WasmOpcode,
                                         Int16BinOp);
template void RunI16x8BinOpTest<uint16_t>(TestExecutionTier, WasmOpcode,
                                          Uint16BinOp);

void RunI16x8ShiftOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                         Int16ShiftOp expected_op) {
  // Intentionally shift by 16, should be no-op.
  for (int shift = 1; shift <= 16; shift++) {
    WasmRunner<int32_t, int32_t> r(execution_tier);
    int32_t* memory = r.builder().AddMemoryElems<int32_t>(1);
    int16_t* g_imm = r.builder().AddGlobal<int16_t>(kWasmS128);
    int16_t* g_mem = r.builder().AddGlobal<int16_t>(kWasmS128);
    uint8_t value = 0;
    uint8_t simd = r.AllocateLocal(kWasmS128);
    // Shift using an immediate, and shift using a value loaded from memory.
    r.Build({WASM_LOCAL_SET(simd, WASM_SIMD_I16x8_SPLAT(WASM_LOCAL_GET(value))),
             WASM_GLOBAL_SET(0, WASM_SIMD_SHIFT_OP(opcode, WASM_LOCAL_GET(simd),
                                                   WASM_I32V(shift))),
             WASM_GLOBAL_SET(
                 1, WASM_SIMD_SHIFT_OP(
                        opcode, WASM_LOCAL_GET(simd),
                        WASM_LOAD_MEM(MachineType::Int32(), WASM_ZERO))),
             WASM_ONE});

    r.builder().WriteMemory(&memory[0], shift);
    FOR_INT16_INPUTS(x) {
      r.Call(x);
      int16_t expected = expected_op(x, shift);
      for (int i = 0; i < 8; i++) {
        CHECK_EQ(expected, LANE(g_imm, i));
        CHECK_EQ(expected, LANE(g_mem, i));
      }
    }
  }
}

void RunI16x8MixedRelationalOpTest(TestExecutionTier execution_tier,
                                   WasmOpcode opcode, Int16BinOp expected_op) {
  WasmRunner<int32_t, int32_t, int32_t> r(execution_tier);
  uint8_t value1 = 0, value2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_I16x8_SPLAT(WASM_LOCAL_GET(value1))),
           WASM_LOCAL_SET(temp2, WASM_SIMD_I32x4_SPLAT(WASM_LOCAL_GET(value2))),
           WASM_LOCAL_SET(temp3, WASM_SIMD_BINOP(opcode, WASM_LOCAL_GET(temp1),
                                                 WASM_LOCAL_GET(temp2))),
           WASM_SIMD_I16x8_EXTRACT_LANE(0, WASM_LOCAL_GET(temp3))});

  CHECK_EQ(expected_op(0xffff, static_cast<uint16_t>(0x7fffffff)),
           r.Call(0xffff, 0x7fffffff));
  CHECK_EQ(expected_op(0xfeff, static_cast<uint16_t>(0x7fffffff)),
           r.Call(0xfeff, 0x7fffffff));
  CHECK_EQ(expected_op(0xffff, static_cast<uint16_t>(0x7ffffeff)),
           r.Call(0xffff, 0x7ffffeff));
}

void RunI32x4UnOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                      Int32UnOp expected_op) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // Global to hold output.
  int32_t* g = r.builder().AddGlobal<int32_t>(kWasmS128);
  // Build fn to splat test value, perform unop, and write the result.
  uint8_t value = 0;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_I32x4_SPLAT(WASM_LOCAL_GET(value))),
           WASM_GLOBAL_SET(0, WASM_SIMD_UNOP(opcode, WASM_LOCAL_GET(temp1))),
           WASM_ONE});

  FOR_INT32_INPUTS(x) {
    r.Call(x);
    int32_t expected = expected_op(x);
    for (int i = 0; i < 4; i++) {
      CHECK_EQ(expected, LANE(g, i));
    }
  }
}

void RunI32x4BinOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                       Int32BinOp expected_op) {
  WasmRunner<int32_t, int32_t, int32_t> r(execution_tier);
  // Global to hold output.
  int32_t* g = r.builder().AddGlobal<int32_t>(kWasmS128);
  // Build fn to splat test values, perform binop, and write the result.
  uint8_t value1 = 0, value2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_I32x4_SPLAT(WASM_LOCAL_GET(value1))),
           WASM_LOCAL_SET(temp2, WASM_SIMD_I32x4_SPLAT(WASM_LOCAL_GET(value2))),
           WASM_GLOBAL_SET(0, WASM_SIMD_BINOP(opcode, WASM_LOCAL_GET(temp1),
                                              WASM_LOCAL_GET(temp2))),
           WASM_ONE});

  FOR_INT32_INPUTS(x) {
    FOR_INT32_INPUTS(y) {
      r.Call(x, y);
      int32_t expected = expected_op(x, y);
      for (int i = 0; i < 4; i++) {
        CHECK_EQ(expected, LANE(g, i));
      }
    }
  }
}

void RunI32x4ShiftOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                         Int32ShiftOp expected_op) {
  // Intentionally shift by 32, should be no-op.
  for (int shift = 1; shift <= 32; shift++) {
    WasmRunner<int32_t, int32_t> r(execution_tier);
    int32_t* memory = r.builder().AddMemoryElems<int32_t>(1);
    int32_t* g_imm = r.builder().AddGlobal<int32_t>(kWasmS128);
    int32_t* g_mem = r.builder().AddGlobal<int32_t>(kWasmS128);
    uint8_t value = 0;
    uint8_t simd = r.AllocateLocal(kWasmS128);
    // Shift using an immediate, and shift using a value loaded from memory.
    r.Build({WASM_LOCAL_SET(simd, WASM_SIMD_I32x4_SPLAT(WASM_LOCAL_GET(value))),
             WASM_GLOBAL_SET(0, WASM_SIMD_SHIFT_OP(opcode, WASM_LOCAL_GET(simd),
                                                   WASM_I32V(shift))),
             WASM_GLOBAL_SET(
                 1, WASM_SIMD_SHIFT_OP(
                        opcode, WASM_LOCAL_GET(simd),
                        WASM_LOAD_MEM(MachineType::Int32(), WASM_ZERO))),
             WASM_ONE});

    r.builder().WriteMemory(&memory[0], shift);
    FOR_INT32_INPUTS(x) {
      r.Call(x);
      int32_t expected = expected_op(x, shift);
      for (int i = 0; i < 4; i++) {
        CHECK_EQ(expected, LANE(g_imm, i));
        CHECK_EQ(expected, LANE(g_mem, i));
      }
    }
  }
}

void RunI64x2UnOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                      Int64UnOp expected_op) {
  WasmRunner<int32_t, int64_t> r(execution_tier);
  // Global to hold output.
  int64_t* g = r.builder().AddGlobal<int64_t>(kWasmS128);
  // Build fn to splat test value, perform unop, and write the result.
  uint8_t value = 0;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_I64x2_SPLAT(WASM_LOCAL_GET(value))),
           WASM_GLOBAL_SET(0, WASM_SIMD_UNOP(opcode, WASM_LOCAL_GET(temp1))),
           WASM_ONE});

  FOR_INT64_INPUTS(x) {
    r.Call(x);
    int64_t expected = expected_op(x);
    for (int i = 0; i < 2; i++) {
      CHECK_EQ(expected, LANE(g, i));
    }
  }
}

void RunI64x2BinOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                       Int64BinOp expected_op) {
  WasmRunner<int32_t, int64_t, int64_t> r(execution_tier);
  // Global to hold output.
  int64_t* g = r.builder().AddGlobal<int64_t>(kWasmS128);
  // Build fn to splat test values, perform binop, and write the result.
  uint8_t value1 = 0, value2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_I64x2_SPLAT(WASM_LOCAL_GET(value1))),
           WASM_LOCAL_SET(temp2, WASM_SIMD_I64x2_SPLAT(WASM_LOCAL_GET(value2))),
           WASM_GLOBAL_SET(0, WASM_SIMD_BINOP(opcode, WASM_LOCAL_GET(temp1),
                                              WASM_LOCAL_GET(temp2))),
           WASM_ONE});

  FOR_INT64_INPUTS(x) {
    FOR_INT64_INPUTS(y) {
      r.Call(x, y);
      int64_t expected = expected_op(x, y);
      for (int i = 0; i < 2; i++) {
        CHECK_EQ(expected, LANE(g, i));
      }
    }
  }
}

void RunI64x2ShiftOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                         Int64ShiftOp expected_op) {
  // Intentionally shift by 64, should be no-op.
  for (int shift = 1; shift <= 64; shift++) {
    WasmRunner<int32_t, int64_t> r(execution_tier);
    int32_t* memory = r.builder().AddMemoryElems<int32_t>(1);
    int64_t* g_imm = r.builder().AddGlobal<int64_t>(kWasmS128);
    int64_t* g_mem = r.builder().AddGlobal<int64_t>(kWasmS128);
    uint8_t value = 0;
    uint8_t simd = r.AllocateLocal(kWasmS128);
    // Shift using an immediate, and shift using a value loaded from memory.
    r.Build({WASM_LOCAL_SET(simd, WASM_SIMD_I64x2_SPLAT(WASM_LOCAL_GET(value))),
             WASM_GLOBAL_SET(0, WASM_SIMD_SHIFT_OP(opcode, WASM_LOCAL_GET(simd),
                                                   WASM_I32V(shift))),
             WASM_GLOBAL_SET(
                 1, WASM_SIMD_SHIFT_OP(
                        opcode, WASM_LOCAL_GET(simd),
                        WASM_LOAD_MEM(MachineType::Int32(), WASM_ZERO))),
             WASM_ONE});

    r.builder().WriteMemory(&memory[0], shift);
    FOR_INT64_INPUTS(x) {
      r.Call(x);
      int64_t expected = expected_op(x, shift);
      for (int i = 0; i < 2; i++) {
        CHECK_EQ(expected, LANE(g_imm, i));
        CHECK_EQ(expected, LANE(g_mem, i));
      }
    }
  }
}

bool IsCanonical(uint16_t actual) {
  // Canonical NaN has quiet bit and no payload.
  return isnan(actual) && (actual & 0xFE00) == actual;
}

bool isnan(uint16_t f) { return (f & 0x7C00) == 0x7C00 && (f & 0x03FF); }

void CheckFloat16LaneResult(float x, float y, uint16_t expected,
                            uint16_t actual, bool exact) {
  CheckFloat16LaneResult(x, y, y, expected, actual, exact);
}

void CheckFloat16LaneResult(float x, float y, float z, uint16_t expected,
                            uint16_t actual, bool exact) {
  if (isnan(expected)) {
    CHECK(isnan(actual));
    if (std::isnan(x) && IsSameNan(fp16_ieee_from_fp32_value(x), actual)) {
      return;
    }
    if (std::isnan(y) && IsSameNan(fp16_ieee_from_fp32_value(y), actual)) {
      return;
    }
    if (std::isnan(z) && IsSameNan(fp16_ieee_from_fp32_value(z), actual)) {
      return;
    }
    if (IsSameNan(expected, actual)) return;
    if (IsCanonical(actual)) return;
    // This is expected to assert; it's useful for debugging.
    CHECK_EQ(expected, actual);
  } else {
    if (exact) {
      CHECK_EQ(expected, actual);
      return;
    }
    // Otherwise, perform an approximate equality test. First check for
    // equality to handle +/-Infinity where approximate equality doesn't work.
    if (expected == actual) return;

    // 1% error allows all platforms to pass easily.
    constexpr float kApproximationError = 0.01f;
    float f32_expected = fp16_ieee_to_fp32_value(expected);
    float f32_actual = fp16_ieee_to_fp32_value(actual);
    float abs_error = std::abs(f32_expected) * kApproximationError;
    float min = f32_expected - abs_error;
    float max = f32_expected + abs_error;
    CHECK_LE(min, f32_actual);
    CHECK_GE(max, f32_actual);
  }
}

void RunF16x8UnOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                      HalfUnOp expected_op, bool exact) {
  WasmRunner<int32_t, float> r(execution_tier);
  // Global to hold output.
  uint16_t* g = r.builder().AddGlobal<uint16_t>(kWasmS128);
  // Build fn to splat test value, perform unop, and write the result.
  uint8_t value = 0;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_F16x8_SPLAT(WASM_LOCAL_GET(value))),
           WASM_GLOBAL_SET(0, WASM_SIMD_UNOP(opcode, WASM_LOCAL_GET(temp1))),
           WASM_ONE});

  FOR_FLOAT32_INPUTS(x) {
    if (!PlatformCanRepresent(x)) continue;
    // Extreme values have larger errors so skip them for approximation tests.
    if (!exact && IsExtreme(x)) continue;
    uint16_t expected = expected_op(fp16_ieee_from_fp32_value(x));
    if (!PlatformCanRepresent(expected)) continue;
    r.Call(x);
    for (int i = 0; i < 8; i++) {
      uint16_t actual = LANE(g, i);
      CheckFloat16LaneResult(x, x, expected, actual, exact);
    }
  }
}

void RunF16x8BinOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                       HalfBinOp expected_op) {
  WasmRunner<int32_t, float, float> r(execution_tier);
  // Global to hold output.
  uint16_t* g = r.builder().AddGlobal<uint16_t>(kWasmS128);
  // Build fn to splat test values, perform binop, and write the result.
  uint8_t value1 = 0, value2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_F16x8_SPLAT(WASM_LOCAL_GET(value1))),
           WASM_LOCAL_SET(temp2, WASM_SIMD_F16x8_SPLAT(WASM_LOCAL_GET(value2))),
           WASM_GLOBAL_SET(0, WASM_SIMD_BINOP(opcode, WASM_LOCAL_GET(temp1),
                                              WASM_LOCAL_GET(temp2))),
           WASM_ONE});

  FOR_FLOAT32_INPUTS(x) {
    if (!PlatformCanRepresent(x)) continue;
    FOR_FLOAT32_INPUTS(y) {
      if (!PlatformCanRepresent(y)) continue;
      uint16_t expected = expected_op(fp16_ieee_from_fp32_value(x),
                                      fp16_ieee_from_fp32_value(y));
      if (!PlatformCanRepresent(expected)) continue;
      r.Call(x, y);
      for (int i = 0; i < 8; i++) {
        uint16_t actual = LANE(g, i);
        CheckFloat16LaneResult(x, y, expected, actual, true /* exact */);
      }
    }
  }
}

void RunF16x8CompareOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                           HalfCompareOp expected_op) {
  WasmRunner<int32_t, float, float> r(execution_tier);
  // Set up global to hold mask output.
  int16_t* g = r.builder().AddGlobal<int16_t>(kWasmS128);
  // Build fn to splat test values, perform compare op, and write the result.
  uint8_t value1 = 0, value2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_F16x8_SPLAT(WASM_LOCAL_GET(value1))),
           WASM_LOCAL_SET(temp2, WASM_SIMD_F16x8_SPLAT(WASM_LOCAL_GET(value2))),
           WASM_GLOBAL_SET(0, WASM_SIMD_BINOP(opcode, WASM_LOCAL_GET(temp1),
                                              WASM_LOCAL_GET(temp2))),
           WASM_ONE});

  FOR_FLOAT32_INPUTS(x) {
    if (!PlatformCanRepresent(x)) continue;
    FOR_FLOAT32_INPUTS(y) {
      if (!PlatformCanRepresent(y)) continue;
      float diff = x - y;  // Model comparison as subtraction.
      if (!PlatformCanRepresent(diff)) continue;
      r.Call(x, y);
      int16_t expected = expected_op(fp16_ieee_from_fp32_value(x),
                                     fp16_ieee_from_fp32_value(y));
      for (int i = 0; i < 8; i++) {
        CHECK_EQ(expected, LANE(g, i));
      }
    }
  }
}

bool IsExtreme(float x) {
  float abs_x = std::fabs(x);
  const float kSmallFloatThreshold = 1.0e-32f;
  const float kLargeFloatThreshold = 1.0e32f;
  return abs_x != 0.0f &&  // 0 or -0 are fine.
         (abs_x < kSmallFloatThreshold || abs_x > kLargeFloatThreshold);
}

bool IsCanonical(float actual) {
  uint32_t actual_bits = base::bit_cast<uint32_t>(actual);
  // Canonical NaN has quiet bit and no payload.
  return (actual_bits & 0xFFC00000) == actual_bits;
}

void CheckFloatResult(float x, float y, float expected, float actual,
                      bool exact) {
  if (std::isnan(expected)) {
    CHECK(std::isnan(actual));
    if (std::isnan(x) && IsSameNan(x, actual)) return;
    if (std::isnan(y) && IsSameNan(y, actual)) return;
    if (IsSameNan(expected, actual)) return;
    if (IsCanonical(actual)) return;
    // This is expected to assert; it's useful for debugging.
    CHECK_EQ(base::bit_cast<uint32_t>(expected),
             base::bit_cast<uint32_t>(actual));
  } else {
    if (exact) {
      CHECK_EQ(expected, actual);
      // The sign of 0's must match.
      CHECK_EQ(std::signbit(expected), std::signbit(actual));
      return;
    }
    // Otherwise, perform an approximate equality test. First check for
    // equality to handle +/-Infinity where approximate equality doesn't work.
    if (expected == actual) return;

    // 1% error allows all platforms to pass easily.
    constexpr float kApproximationError = 0.01f;
    float abs_error = std::abs(expected) * kApproximationError;
    float min = expected - abs_error;
    float max = expected + abs_error;
    CHECK_LE(min, actual);
    CHECK_GE(max, actual);
  }
}

void RunF32x4UnOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                      FloatUnOp expected_op, bool exact) {
  WasmRunner<int32_t, float> r(execution_tier);
  // Global to hold output.
  float* g = r.builder().AddGlobal<float>(kWasmS128);
  // Build fn to splat test value, perform unop, and write the result.
  uint8_t value = 0;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_F32x4_SPLAT(WASM_LOCAL_GET(value))),
           WASM_GLOBAL_SET(0, WASM_SIMD_UNOP(opcode, WASM_LOCAL_GET(temp1))),
           WASM_ONE});

  FOR_FLOAT32_INPUTS(x) {
    if (!PlatformCanRepresent(x)) continue;
    // Extreme values have larger errors so skip them for approximation tests.
    if (!exact && IsExtreme(x)) continue;
    float expected = expected_op(x);
#if V8_OS_AIX
    if (!MightReverseSign<FloatUnOp>(expected_op))
      expected = FpOpWorkaround<float>(x, expected);
#endif
    if (!PlatformCanRepresent(expected)) continue;
    r.Call(x);
    for (int i = 0; i < 4; i++) {
      float actual = LANE(g, i);
      CheckFloatResult(x, x, expected, actual, exact);
    }
  }

  FOR_FLOAT32_NAN_INPUTS(f) {
    float x = base::bit_cast<float>(nan_test_array[f]);
    if (!PlatformCanRepresent(x)) continue;
    // Extreme values have larger errors so skip them for approximation tests.
    if (!exact && IsExtreme(x)) continue;
    float expected = expected_op(x);
    if (!PlatformCanRepresent(expected)) continue;
    r.Call(x);
    for (int i = 0; i < 4; i++) {
      float actual = LANE(g, i);
      CheckFloatResult(x, x, expected, actual, exact);
    }
  }
}

namespace {
// Relaxed-simd operations are deterministic only for some range of values.
// Exclude those from being tested. Currently this is only used for f32x4, f64x2
// relaxed min and max.
template <typename T>
typename std::enable_if<std::is_floating_point<T>::value, bool>::type
ShouldSkipTestingConstants(WasmOpcode opcode, T lhs, T rhs) {
  bool has_nan = std::isnan(lhs) || std::isnan(rhs);
  bool zeroes_of_opposite_signs =
      (lhs == 0 && rhs == 0 && (std::signbit(lhs) != std::signbit(rhs)));
  return WasmOpcodes::IsRelaxedSimdOpcode(opcode) &&
         (has_nan || zeroes_of_opposite_signs);
}
}  // namespace

void RunF32x4BinOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                       FloatBinOp expected_op) {
  WasmRunner<int32_t, float, float> r(execution_tier);
  // Global to hold output.
  float* g = r.builder().AddGlobal<float>(kWasmS128);
  // Build fn to splat test values, perform binop, and write the result.
  uint8_t value1 = 0, value2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_F32x4_SPLAT(WASM_LOCAL_GET(value1))),
           WASM_LOCAL_SET(temp2, WASM_SIMD_F32x4_SPLAT(WASM_LOCAL_GET(value2))),
           WASM_GLOBAL_SET(0, WASM_SIMD_BINOP(opcode, WASM_LOCAL_GET(temp1),
                                              WASM_LOCAL_GET(temp2))),
           WASM_ONE});

  FOR_FLOAT32_INPUTS(x) {
    if (!PlatformCanRepresent(x)) continue;
    FOR_FLOAT32_INPUTS(y) {
      if (!PlatformCanRepresent(y)) continue;
      if (ShouldSkipTestingConstants(opcode, x, y)) continue;
      float expected = expected_op(x, y);
      if (!PlatformCanRepresent(expected)) continue;
      r.Call(x, y);
      for (int i = 0; i < 4; i++) {
        float actual = g[i];
        CheckFloatResult(x, y, expected, actual, true /* exact */);
      }
    }
  }

  FOR_FLOAT32_NAN_INPUTS(f) {
    float x = base::bit_cast<float>(nan_test_array[f]);
    if (!PlatformCanRepresent(x)) continue;
    FOR_FLOAT32_NAN_INPUTS(j) {
      float y = base::bit_cast<float>(nan_test_array[j]);
      if (!PlatformCanRepresent(y)) continue;
      if (ShouldSkipTestingConstants(opcode, x, y)) continue;
      float expected = expected_op(x, y);
      if (!PlatformCanRepresent(expected)) continue;
      r.Call(x, y);
      for (int i = 0; i < 4; i++) {
        float actual = LANE(g, i);
        CheckFloatResult(x, y, expected, actual, true /* exact */);
      }
    }
  }
}

void RunF32x4CompareOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                           FloatCompareOp expected_op) {
  WasmRunner<int32_t, float, float> r(execution_tier);
  // Set up global to hold mask output.
  int32_t* g = r.builder().AddGlobal<int32_t>(kWasmS128);
  // Build fn to splat test values, perform compare op, and write the result.
  uint8_t value1 = 0, value2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_F32x4_SPLAT(WASM_LOCAL_GET(value1))),
           WASM_LOCAL_SET(temp2, WASM_SIMD_F32x4_SPLAT(WASM_LOCAL_GET(value2))),
           WASM_GLOBAL_SET(0, WASM_SIMD_BINOP(opcode, WASM_LOCAL_GET(temp1),
                                              WASM_LOCAL_GET(temp2))),
           WASM_ONE});

  FOR_FLOAT32_INPUTS(x) {
    if (!PlatformCanRepresent(x)) continue;
    FOR_FLOAT32_INPUTS(y) {
      if (!PlatformCanRepresent(y)) continue;
      float diff = x - y;  // Model comparison as subtraction.
      if (!PlatformCanRepresent(diff)) continue;
      r.Call(x, y);
      int32_t expected = expected_op(x, y);
      for (int i = 0; i < 4; i++) {
        CHECK_EQ(expected, LANE(g, i));
      }
    }
  }
}

bool IsExtreme(double x) {
  double abs_x = std::fabs(x);
  const double kSmallFloatThreshold = 1.0e-298;
  const double kLargeFloatThreshold = 1.0e298;
  return abs_x != 0.0f &&  // 0 or -0 are fine.
         (abs_x < kSmallFloatThreshold || abs_x > kLargeFloatThreshold);
}

bool IsCanonical(double actual) {
  uint64_t actual_bits = base::bit_cast<uint64_t>(actual);
  // Canonical NaN has quiet bit and no payload.
  return (actual_bits & 0xFFF8000000000000) == actual_bits;
}

void CheckDoubleResult(double x, double y, double expected, double actual,
                       bool exact) {
  if (std::isnan(expected)) {
    CHECK(std::isnan(actual));
    if (std::isnan(x) && IsSameNan(x, actual)) return;
    if (std::isnan(y) && IsSameNan(y, actual)) return;
    if (IsSameNan(expected, actual)) return;
    if (IsCanonical(actual)) return;
    // This is expected to assert; it's useful for debugging.
    CHECK_EQ(base::bit_cast<uint64_t>(expected),
             base::bit_cast<uint64_t>(actual));
  } else {
    if (exact) {
      CHECK_EQ(expected, actual);
      // The sign of 0's must match.
      CHECK_EQ(std::signbit(expected), std::signbit(actual));
      return;
    }
    // Otherwise, perform an approximate equality test. First check for
    // equality to handle +/-Infinity where approximate equality doesn't work.
    if (expected == actual) return;

    // 1% error allows all platforms to pass easily.
    constexpr double kApproximationError = 0.01f;
    double abs_error = std::abs(expected) * kApproximationError,
           min = expected - abs_error, max = expected + abs_error;
    CHECK_LE(min, actual);
    CHECK_GE(max, actual);
  }
}

void RunF64x2UnOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                      DoubleUnOp expected_op, bool exact) {
  WasmRunner<int32_t, double> r(execution_tier);
  // Global to hold output.
  double* g = r.builder().AddGlobal<double>(kWasmS128);
  // Build fn to splat test value, perform unop, and write the result.
  uint8_t value = 0;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_F64x2_SPLAT(WASM_LOCAL_GET(value))),
           WASM_GLOBAL_SET(0, WASM_SIMD_UNOP(opcode, WASM_LOCAL_GET(temp1))),
           WASM_ONE});

  FOR_FLOAT64_INPUTS(x) {
    if (!PlatformCanRepresent(x)) continue;
    // Extreme values have larger errors so skip them for approximation tests.
    if (!exact && IsExtreme(x)) continue;
    double expected = expected_op(x);
#if V8_OS_AIX
    if (!MightReverseSign<DoubleUnOp>(expected_op))
      expected = FpOpWorkaround<double>(x, expected);
#endif
    if (!PlatformCanRepresent(expected)) continue;
    r.Call(x);
    for (int i = 0; i < 2; i++) {
      double actual = LANE(g, i);
      CheckDoubleResult(x, x, expected, actual, exact);
    }
  }

  FOR_FLOAT64_NAN_INPUTS(d) {
    double x = base::bit_cast<double>(double_nan_test_array[d]);
    if (!PlatformCanRepresent(x)) continue;
    // Extreme values have larger errors so skip them for approximation tests.
    if (!exact && IsExtreme(x)) continue;
    double expected = expected_op(x);
    if (!PlatformCanRepresent(expected)) continue;
    r.Call(x);
    for (int i = 0; i < 2; i++) {
      double actual = LANE(g, i);
      CheckDoubleResult(x, x, expected, actual, exact);
    }
  }
}

void RunF64x2BinOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                       DoubleBinOp expected_op) {
  WasmRunner<int32_t, double, double> r(execution_tier);
  // Global to hold output.
  double* g = r.builder().AddGlobal<double>(kWasmS128);
  // Build fn to splat test value, perform binop, and write the result.
  uint8_t value1 = 0, value2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_F64x2_SPLAT(WASM_LOCAL_GET(value1))),
           WASM_LOCAL_SET(temp2, WASM_SIMD_F64x2_SPLAT(WASM_LOCAL_GET(value2))),
           WASM_GLOBAL_SET(0, WASM_SIMD_BINOP(opcode, WASM_LOCAL_GET(temp1),
                                              WASM_LOCAL_GET(temp2))),
           WASM_ONE});

  FOR_FLOAT64_INPUTS(x) {
    if (!PlatformCanRepresent(x)) continue;
    FOR_FLOAT64_INPUTS(y) {
      if (!PlatformCanRepresent(x)) continue;
      if (ShouldSkipTestingConstants(opcode, x, y)) continue;
      double expected = expected_op(x, y);
      if (!PlatformCanRepresent(expected)) continue;
      r.Call(x, y);
      for (int i = 0; i < 2; i++) {
        double actual = LANE(g, i);
        CheckDoubleResult(x, y, expected, actual, true /* exact */);
      }
    }
  }

  FOR_FLOAT64_NAN_INPUTS(d) {
    double x = base::bit_cast<double>(double_nan_test_array[d]);
    if (!PlatformCanRepresent(x)) continue;
    FOR_FLOAT64_NAN_INPUTS(j) {
      double y = base::bit_cast<double>(double_nan_test_array[j]);
      double expected = expected_op(x, y);
      if (!PlatformCanRepresent(expected)) continue;
      if (ShouldSkipTestingConstants(opcode, x, y)) continue;
      r.Call(x, y);
      for (int i = 0; i < 2; i++) {
        double actual = LANE(g, i);
        CheckDoubleResult(x, y, expected, actual, true /* exact */);
      }
    }
  }
}

void RunF64x2CompareOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                           DoubleCompareOp expected_op) {
  WasmRunner<int32_t, double, double> r(execution_tier);
  // Set up global to hold mask output.
  int64_t* g = r.builder().AddGlobal<int64_t>(kWasmS128);
  // Build fn to splat test values, perform compare op, and write the result.
  uint8_t value1 = 0, value2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  // Make the lanes of each temp compare differently:
  // temp1 = y, x and temp2 = y, y.
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_F64x2_SPLAT(WASM_LOCAL_GET(value1))),
           WASM_LOCAL_SET(temp1,
                          WASM_SIMD_F64x2_REPLACE_LANE(1, WASM_LOCAL_GET(temp1),
                                                       WASM_LOCAL_GET(value2))),
           WASM_LOCAL_SET(temp2, WASM_SIMD_F64x2_SPLAT(WASM_LOCAL_GET(value2))),
           WASM_GLOBAL_SET(0, WASM_SIMD_BINOP(opcode, WASM_LOCAL_GET(temp1),
                                              WASM_LOCAL_GET(temp2))),
           WASM_ONE});

  FOR_FLOAT64_INPUTS(x) {
    if (!PlatformCanRepresent(x)) continue;
    FOR_FLOAT64_INPUTS(y) {
      if (!PlatformCanRepresent(y)) continue;
      double diff = x - y;  // Model comparison as subtraction.
      if (!PlatformCanRepresent(diff)) continue;
      r.Call(x, y);
      int64_t expected0 = expected_op(x, y);
      int64_t expected1 = expected_op(y, y);
      CHECK_EQ(expected0, LANE(g, 0));
      CHECK_EQ(expected1, LANE(g, 1));
    }
  }
}

#ifdef V8_ENABLE_WASM_SIMD256_REVEC
template <typename T, typename OpType>
void RunI8x32BinOpRevecTest(WasmOpcode opcode, OpType expected_op,
                            compiler::IrOpcode::Value revec_opcode) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t, int32_t, int32_t> r(
      TestExecutionTier::kTurbofan);
  T* memory = r.builder().AddMemoryElems<T>(96);
  // Build fn perform binary operation on two 256 bit vectors a and b,
  // store the result in c:
  //   simd128 *a,*b,*c;
  //   *c = *a bin_op *b;
  //   *(c+1) = *(a+1) bin_op *(b+1);
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t param3 = 2;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimd256Binop>);
    BUILD_AND_CHECK_REVEC_NODE(
        r, revec_opcode,
        WASM_LOCAL_SET(
            temp1,
            WASM_SIMD_BINOP(opcode, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1)),
                            WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param2)))),
        WASM_LOCAL_SET(
            temp2,
            WASM_SIMD_BINOP(
                opcode,
                WASM_SIMD_LOAD_MEM_OFFSET(offset, WASM_LOCAL_GET(param1)),
                WASM_SIMD_LOAD_MEM_OFFSET(offset, WASM_LOCAL_GET(param2)))),
        WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param3), WASM_LOCAL_GET(temp1)),
        WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param3),
                                   WASM_LOCAL_GET(temp2)),
        WASM_ONE);
  }
  for (T x : compiler::ValueHelper::GetVector<T>()) {
    for (T y : compiler::ValueHelper::GetVector<T>()) {
      for (int i = 0; i < 16; i++) {
        r.builder().WriteMemory(&memory[i], x);
        r.builder().WriteMemory(&memory[i + 16], x);
        r.builder().WriteMemory(&memory[i + 32], y);
        r.builder().WriteMemory(&memory[i + 48], y);
      }
      r.Call(0, 32, 64);
      T expected = expected_op(x, y);
      for (int i = 0; i < 16; i++) {
        CHECK_EQ(expected, memory[i + 64]);
        CHECK_EQ(expected, memory[i + 80]);
      }
    }
  }
}

// Explicit instantiations of uses.
template void RunI8x32BinOpRevecTest<int8_t>(
    WasmOpcode, Int8BinOp, compiler::IrOpcode::Value revec_opcode);

template void RunI8x32BinOpRevecTest<uint8_t>(
    WasmOpcode, Uint8BinOp, compiler::IrOpcode::Value revec_opcode);

void RunI16x16UnOpRevecTest(WasmOpcode opcode, Int16UnOp expected_op,
                            compiler::IrOpcode::Value revec_opcode) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  int16_t* memory = r.builder().AddMemoryElems<int16_t>(32);
  // Build fn to load a I16x16 vector with test value, perform unop, and write
  // the result to another array.
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;

  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimd256Unary>);
    BUILD_AND_CHECK_REVEC_NODE(
        r, revec_opcode,
        WASM_LOCAL_SET(
            temp1,
            WASM_SIMD_UNOP(opcode, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1)))),
        WASM_LOCAL_SET(
            temp2, WASM_SIMD_UNOP(opcode, WASM_SIMD_LOAD_MEM_OFFSET(
                                              offset, WASM_LOCAL_GET(param1)))),
        WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(temp1)),
        WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                   WASM_LOCAL_GET(temp2)),
        WASM_ONE);
  }
  FOR_INT16_INPUTS(x) {
    r.builder().WriteMemory(&memory[1], x);
    r.builder().WriteMemory(&memory[10], x);
    r.Call(0, 32);
    int16_t expected = expected_op(x);
    CHECK_EQ(expected, memory[17]);
    CHECK_EQ(expected, memory[26]);
  }
}

template <typename T, typename OpType>
void RunI16x16BinOpRevecTest(WasmOpcode opcode, OpType expected_op,
                             compiler::IrOpcode::Value revec_opcode) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t, int32_t, int32_t> r(
      TestExecutionTier::kTurbofan);
  T* memory = r.builder().AddMemoryElems<T>(48);
  // Build fn perform binary operation on two 256 bit vectors a and b,
  // store the result in c:
  //   simd128 *a,*b,*c;
  //   *c = *a bin_op *b;
  //   *(c+1) = *(a+1) bin_op *(b+1);
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t param3 = 2;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;

  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimd256Binop>);
    BUILD_AND_CHECK_REVEC_NODE(
        r, revec_opcode,
        WASM_LOCAL_SET(
            temp1,
            WASM_SIMD_BINOP(opcode, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1)),
                            WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param2)))),
        WASM_LOCAL_SET(
            temp2,
            WASM_SIMD_BINOP(
                opcode,
                WASM_SIMD_LOAD_MEM_OFFSET(offset, WASM_LOCAL_GET(param1)),
                WASM_SIMD_LOAD_MEM_OFFSET(offset, WASM_LOCAL_GET(param2)))),
        WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param3), WASM_LOCAL_GET(temp1)),
        WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param3),
                                   WASM_LOCAL_GET(temp2)),
        WASM_ONE);
  }
  for (T x : compiler::ValueHelper::GetVector<T>()) {
    for (T y : compiler::ValueHelper::GetVector<T>()) {
      for (int i = 0; i < 8; i++) {
        r.builder().WriteMemory(&memory[i], x);
        r.builder().WriteMemory(&memory[i + 8], x);
        r.builder().WriteMemory(&memory[i + 16], y);
        r.builder().WriteMemory(&memory[i + 24], y);
      }
      r.Call(0, 32, 64);
      T expected = expected_op(x, y);
      for (int i = 0; i < 8; i++) {
        CHECK_EQ(expected, memory[i + 32]);
        CHECK_EQ(expected, memory[i + 40]);
      }
    }
  }
}

// Explicit instantiations of uses.
template void RunI16x16BinOpRevecTest<int16_t>(
    WasmOpcode, Int16BinOp, compiler::IrOpcode::Value revec_opcode);

template void RunI16x16BinOpRevecTest<uint16_t>(
    WasmOpcode, Uint16BinOp, compiler::IrOpcode::Value revec_opcode);

void RunI16x16ShiftOpRevecTest(WasmOpcode opcode, Int16ShiftOp expected_op,
                               compiler::IrOpcode::Value revec_opcode) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  for (int shift = 1; shift <= 8; shift++) {
    WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
    int16_t* memory = r.builder().AddMemoryElems<int16_t>(34);
    // Build fn to load an I16x16 vector with test value, shift using an
    // immediate and a value loaded from memory. Write the result to another
    // array.
    uint8_t param1 = 0;
    uint8_t param2 = 1;
    uint8_t temp1 = r.AllocateLocal(kWasmI32);
    uint8_t temp2 = r.AllocateLocal(kWasmS128);
    uint8_t temp3 = r.AllocateLocal(kWasmS128);
    constexpr uint8_t offset = 16;

    {
      TSSimd256VerifyScope ts_scope(
          r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                        compiler::turboshaft::Opcode::kSimd256Shift>);
      BUILD_AND_CHECK_REVEC_NODE(
          r, revec_opcode,
          WASM_LOCAL_SET(temp2,
                         WASM_SIMD_SHIFT_OP(
                             opcode, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1)),
                             WASM_I32V(shift))),
          WASM_LOCAL_SET(temp3,
                         WASM_SIMD_SHIFT_OP(opcode,
                                            WASM_SIMD_LOAD_MEM_OFFSET(
                                                offset, WASM_LOCAL_GET(param1)),
                                            WASM_I32V(shift))),
          WASM_LOCAL_SET(temp1,
                         WASM_LOAD_MEM(MachineType::Int32(), WASM_I32V(64))),
          WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2),
                              WASM_SIMD_SHIFT_OP(opcode, WASM_LOCAL_GET(temp2),
                                                 WASM_LOCAL_GET(temp1))),
          WASM_SIMD_STORE_MEM_OFFSET(
              offset, WASM_LOCAL_GET(param2),
              WASM_SIMD_SHIFT_OP(opcode, WASM_LOCAL_GET(temp3),
                                 WASM_LOCAL_GET(temp1))),
          WASM_ONE);
    }
    r.builder().WriteMemory(reinterpret_cast<int32_t*>(&memory[32]), shift);
    FOR_INT16_INPUTS(x) {
      r.builder().WriteMemory(&memory[1], x);
      r.builder().WriteMemory(&memory[10], x);
      r.Call(0, 32);
      // Shift twice
      int16_t expected = expected_op(expected_op(x, shift), shift);
      CHECK_EQ(expected, memory[17]);
      CHECK_EQ(expected, memory[26]);
    }
  }
}

void RunI32x8UnOpRevecTest(WasmOpcode opcode, Int32UnOp expected_op,
                           compiler::IrOpcode::Value revec_opcode) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  int32_t* memory = r.builder().AddMemoryElems<int32_t>(16);
  // Build fn to load a I32x8 vector with test value, perform unop, and write
  // the result to another array.
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimd256Unary>);
    BUILD_AND_CHECK_REVEC_NODE(
        r, revec_opcode,
        WASM_LOCAL_SET(
            temp1,
            WASM_SIMD_UNOP(opcode, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1)))),
        WASM_LOCAL_SET(
            temp2, WASM_SIMD_UNOP(opcode, WASM_SIMD_LOAD_MEM_OFFSET(
                                              offset, WASM_LOCAL_GET(param1)))),
        WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(temp1)),
        WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                   WASM_LOCAL_GET(temp2)),
        WASM_ONE);
  }
  FOR_INT32_INPUTS(x) {
    r.builder().WriteMemory(&memory[1], x);
    r.builder().WriteMemory(&memory[6], x);
    r.Call(0, 32);
    int32_t expected = expected_op(x);
    CHECK_EQ(expected, memory[9]);
    CHECK_EQ(expected, memory[14]);
  }
}

template <typename T, typename OpType>
void RunI32x8BinOpRevecTest(WasmOpcode opcode, OpType expected_op,
                            compiler::IrOpcode::Value revec_opcode) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t, int32_t, int32_t> r(
      TestExecutionTier::kTurbofan);
  T* memory = r.builder().AddMemoryElems<T>(24);
  // Build fn perform binary operation on two 256 bit vectors a and b,
  // store the result in c:
  //   simd128 *a,*b,*c;
  //   *c = *a bin_op *b;
  //   *(c+1) = *(a+1) bin_op *(b+1);
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t param3 = 2;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimd256Binop>);
    BUILD_AND_CHECK_REVEC_NODE(
        r, revec_opcode,
        WASM_LOCAL_SET(
            temp1,
            WASM_SIMD_BINOP(opcode, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1)),
                            WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param2)))),
        WASM_LOCAL_SET(
            temp2,
            WASM_SIMD_BINOP(
                opcode,
                WASM_SIMD_LOAD_MEM_OFFSET(offset, WASM_LOCAL_GET(param1)),
                WASM_SIMD_LOAD_MEM_OFFSET(offset, WASM_LOCAL_GET(param2)))),
        WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param3), WASM_LOCAL_GET(temp1)),
        WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param3),
                                   WASM_LOCAL_GET(temp2)),
        WASM_ONE);
  }
  for (T x : compiler::ValueHelper::GetVector<T>()) {
    for (T y : compiler::ValueHelper::GetVector<T>()) {
      for (int i = 0; i < 4; i++) {
        r.builder().WriteMemory(&memory[i], x);
        r.builder().WriteMemory(&memory[i + 4], x);
        r.builder().WriteMemory(&memory[i + 8], y);
        r.builder().WriteMemory(&memory[i + 12], y);
      }
      r.Call(0, 32, 64);
      T expected = expected_op(x, y);
      for (int i = 0; i < 4; i++) {
        CHECK_EQ(expected, memory[i + 16]);
        CHECK_EQ(expected, memory[i + 20]);
      }
    }
  }
}

// Explicit instantiations of uses.
template void RunI32x8BinOpRevecTest<int32_t>(WasmOpcode, Int32BinOp,
                                              compiler::IrOpcode::Value);

template void RunI32x8BinOpRevecTest<uint32_t>(WasmOpcode, Uint32BinOp,
                                               compiler::IrOpcode::Value);

void RunI32x8ShiftOpRevecTest(WasmOpcode opcode, Int32ShiftOp expected_op,
                              compiler::IrOpcode::Value revec_opcode) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  for (int shift = 1; shift <= 16; shift++) {
    WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
    int32_t* memory = r.builder().AddMemoryElems<int32_t>(17);
    // Build fn to load an I32x8 vector with test value, shift using an
    // immediate and a value loaded from memory. Write the result to another
    // array.
    uint8_t param1 = 0;
    uint8_t param2 = 1;
    uint8_t temp1 = r.AllocateLocal(kWasmI32);
    uint8_t temp2 = r.AllocateLocal(kWasmS128);
    uint8_t temp3 = r.AllocateLocal(kWasmS128);
    constexpr uint8_t offset = 16;

    {
      TSSimd256VerifyScope ts_scope(
          r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                        compiler::turboshaft::Opcode::kSimd256Shift>);
      BUILD_AND_CHECK_REVEC_NODE(
          r, revec_opcode,
          WASM_LOCAL_SET(temp2,
                         WASM_SIMD_SHIFT_OP(
                             opcode, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1)),
                             WASM_I32V(shift))),
          WASM_LOCAL_SET(temp3,
                         WASM_SIMD_SHIFT_OP(opcode,
                                            WASM_SIMD_LOAD_MEM_OFFSET(
                                                offset, WASM_LOCAL_GET(param1)),
                                            WASM_I32V(shift))),
          WASM_LOCAL_SET(temp1,
                         WASM_LOAD_MEM(MachineType::Int32(), WASM_I32V(64))),
          WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2),
                              WASM_SIMD_SHIFT_OP(opcode, WASM_LOCAL_GET(temp2),
                                                 WASM_LOCAL_GET(temp1))),
          WASM_SIMD_STORE_MEM_OFFSET(
              offset, WASM_LOCAL_GET(param2),
              WASM_SIMD_SHIFT_OP(opcode, WASM_LOCAL_GET(temp3),
                                 WASM_LOCAL_GET(temp1))),
          WASM_ONE);
    }
    r.builder().WriteMemory(&memory[16], shift);
    FOR_INT32_INPUTS(x) {
      r.builder().WriteMemory(&memory[1], x);
      r.builder().WriteMemory(&memory[6], x);
      r.Call(0, 32);
      // Shift twice
      int32_t expected = expected_op(expected_op(x, shift), shift);
      CHECK_EQ(expected, memory[9]);
      CHECK_EQ(expected, memory[14]);
    }
  }
}

void RunI64x4BinOpRevecTest(WasmOpcode opcode, Int64BinOp expected_op,
                            compiler::IrOpcode::Value revec_opcode) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t, int32_t, int32_t> r(
      TestExecutionTier::kTurbofan);
  int64_t* memory = r.builder().AddMemoryElems<int64_t>(12);
  // Build fn perform binary operation on two 256 bit vectors a and b,
  // store the result in c:
  //   simd128 *a,*b,*c;
  //   *c = *a bin_op *b;
  //   *(c+1) = *(a+1) bin_op *(b+1);
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t param3 = 2;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;

  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimd256Binop>);
    BUILD_AND_CHECK_REVEC_NODE(
        r, revec_opcode,
        WASM_LOCAL_SET(
            temp1,
            WASM_SIMD_BINOP(opcode, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1)),
                            WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param2)))),
        WASM_LOCAL_SET(
            temp2,
            WASM_SIMD_BINOP(
                opcode,
                WASM_SIMD_LOAD_MEM_OFFSET(offset, WASM_LOCAL_GET(param1)),
                WASM_SIMD_LOAD_MEM_OFFSET(offset, WASM_LOCAL_GET(param2)))),
        WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param3), WASM_LOCAL_GET(temp1)),
        WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param3),
                                   WASM_LOCAL_GET(temp2)),
        WASM_ONE);
  }
  FOR_INT64_INPUTS(x) {
    FOR_INT64_INPUTS(y) {
      for (int i = 0; i < 2; i++) {
        r.builder().WriteMemory(&memory[i], x);
        r.builder().WriteMemory(&memory[i + 2], x);
        r.builder().WriteMemory(&memory[i + 4], y);
        r.builder().WriteMemory(&memory[i + 6], y);
      }
      r.Call(0, 32, 64);
      int64_t expected = expected_op(x, y);
      for (int i = 0; i < 2; i++) {
        CHECK_EQ(expected, memory[i + 8]);
        CHECK_EQ(expected, memory[i + 10]);
      }
    }
  }
}

void RunI64x4ShiftOpRevecTest(WasmOpcode opcode, Int64ShiftOp expected_op,
                              compiler::IrOpcode::Value revec_opcode) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  for (int shift = 1; shift <= 32; shift++) {
    WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
    int64_t* memory = r.builder().AddMemoryElems<int64_t>(9);
    // Build fn to load an I64x4 vector with test value, shift using an
    // immediate and a value loaded from memory. Write the result to another
    // array.
    uint8_t param1 = 0;
    uint8_t param2 = 1;
    uint8_t temp1 = r.AllocateLocal(kWasmI32);
    uint8_t temp2 = r.AllocateLocal(kWasmS128);
    uint8_t temp3 = r.AllocateLocal(kWasmS128);
    constexpr uint8_t offset = 16;

    {
      TSSimd256VerifyScope ts_scope(
          r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                        compiler::turboshaft::Opcode::kSimd256Shift>);
      BUILD_AND_CHECK_REVEC_NODE(
          r, revec_opcode,
          WASM_LOCAL_SET(temp2,
                         WASM_SIMD_SHIFT_OP(
                             opcode, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1)),
                             WASM_I32V(shift))),
          WASM_LOCAL_SET(temp3,
                         WASM_SIMD_SHIFT_OP(opcode,
                                            WASM_SIMD_LOAD_MEM_OFFSET(
                                                offset, WASM_LOCAL_GET(param1)),
                                            WASM_I32V(shift))),
          WASM_LOCAL_SET(temp1,
                         WASM_LOAD_MEM(MachineType::Int32(), WASM_I32V(64))),
          WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2),
                              WASM_SIMD_SHIFT_OP(opcode, WASM_LOCAL_GET(temp2),
                                                 WASM_LOCAL_GET(temp1))),
          WASM_SIMD_STORE_MEM_OFFSET(
              offset, WASM_LOCAL_GET(param2),
              WASM_SIMD_SHIFT_OP(opcode, WASM_LOCAL_GET(temp3),
                                 WASM_LOCAL_GET(temp1))),
          WASM_ONE);
    }
    r.builder().WriteMemory(reinterpret_cast<int32_t*>(&memory[8]), shift);
    FOR_INT64_INPUTS(x) {
      r.builder().WriteMemory(&memory[0], x);
      r.builder().WriteMemory(&memory[3], x);
      r.Call(0, 32);
      // Shift twice
      int64_t expected = expected_op(expected_op(x, shift), shift);
      CHECK_EQ(expected, memory[4]);
      CHECK_EQ(expected, memory[7]);
    }
  }
}

void RunF32x8UnOpRevecTest(WasmOpcode opcode, FloatUnOp expected_op,
                           compiler::IrOpcode::Value revec_opcode) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  float* memory = r.builder().AddMemoryElems<float>(16);
  // Build fn to load a F32x8 vector with test value, perform unop, and write
  // the result to another array.
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;

  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimd256Unary>);
    BUILD_AND_CHECK_REVEC_NODE(
        r, revec_opcode,
        WASM_LOCAL_SET(
            temp1,
            WASM_SIMD_UNOP(opcode, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1)))),
        WASM_LOCAL_SET(
            temp2, WASM_SIMD_UNOP(opcode, WASM_SIMD_LOAD_MEM_OFFSET(
                                              offset, WASM_LOCAL_GET(param1)))),
        WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(temp1)),
        WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                   WASM_LOCAL_GET(temp2)),
        WASM_ONE);
  }
  FOR_FLOAT32_INPUTS(x) {
    if (!PlatformCanRepresent(x)) continue;
    float expected = expected_op(x);
#if V8_OS_AIX
    if (!MightReverseSign<FloatUnOp>(expected_op))
      expected = FpOpWorkaround<float>(x, expected);
#endif
    if (!PlatformCanRepresent(expected)) continue;
    r.builder().WriteMemory(&memory[1], x);
    r.builder().WriteMemory(&memory[6], x);
    r.Call(0, 32);
    CheckFloatResult(x, x, expected, memory[9]);
    CheckFloatResult(x, x, expected, memory[14]);
  }

  FOR_FLOAT32_NAN_INPUTS(f) {
    float x = base::bit_cast<float>(nan_test_array[f]);
    if (!PlatformCanRepresent(x)) continue;
    float expected = expected_op(x);
    if (!PlatformCanRepresent(expected)) continue;
    r.builder().WriteMemory(&memory[1], x);
    r.builder().WriteMemory(&memory[6], x);
    r.Call(0, 32);
    CheckFloatResult(x, x, expected, memory[9]);
    CheckFloatResult(x, x, expected, memory[14]);
  }
}

void RunF32x8BinOpRevecTest(WasmOpcode opcode, FloatBinOp expected_op,
                            compiler::IrOpcode::Value revec_opcode) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t, int32_t, int32_t> r(
      TestExecutionTier::kTurbofan);
  float* memory = r.builder().AddMemoryElems<float>(24);
  // Build fn perform binary operation on two 256 bit vectors a and b,
  // store the result in c:
  //   simd128 *a,*b,*c;
  //   *c = *a bin_op *b;
  //   *(c+1) = *(a+1) bin_op *(b+1);
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t param3 = 2;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;

  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimd256Binop>);
    BUILD_AND_CHECK_REVEC_NODE(
        r, revec_opcode,
        WASM_LOCAL_SET(
            temp1,
            WASM_SIMD_BINOP(opcode, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1)),
                            WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param2)))),
        WASM_LOCAL_SET(
            temp2,
            WASM_SIMD_BINOP(
                opcode,
                WASM_SIMD_LOAD_MEM_OFFSET(offset, WASM_LOCAL_GET(param1)),
                WASM_SIMD_LOAD_MEM_OFFSET(offset, WASM_LOCAL_GET(param2)))),
        WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param3), WASM_LOCAL_GET(temp1)),
        WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param3),
                                   WASM_LOCAL_GET(temp2)),
        WASM_ONE);
  }
  FOR_FLOAT32_INPUTS(x) {
    if (!PlatformCanRepresent(x)) continue;
    FOR_FLOAT32_INPUTS(y) {
      if (!PlatformCanRepresent(y)) continue;
      if (ShouldSkipTestingConstants(opcode, x, y)) continue;
      float expected = expected_op(x, y);
      if (!PlatformCanRepresent(expected)) continue;
      for (int i = 0; i < 4; i++) {
        r.builder().WriteMemory(&memory[i], x);
        r.builder().WriteMemory(&memory[i + 4], x);
        r.builder().WriteMemory(&memory[i + 8], y);
        r.builder().WriteMemory(&memory[i + 12], y);
      }
      r.Call(0, 32, 64);
      for (int i = 0; i < 4; i++) {
        CheckFloatResult(x, y, expected, memory[i + 16], true /* exact */);
        CheckFloatResult(x, y, expected, memory[i + 20], true /* exact */);
      }
    }
  }

  FOR_FLOAT32_NAN_INPUTS(f) {
    float x = base::bit_cast<float>(nan_test_array[f]);
    if (!PlatformCanRepresent(x)) continue;
    FOR_FLOAT32_NAN_INPUTS(j) {
      float y = base::bit_cast<float>(nan_test_array[j]);
      if (!PlatformCanRepresent(y)) continue;
      if (ShouldSkipTestingConstants(opcode, x, y)) continue;
      float expected = expected_op(x, y);
      if (!PlatformCanRepresent(expected)) continue;
      for (int i = 0; i < 4; i++) {
        r.builder().WriteMemory(&memory[i], x);
        r.builder().WriteMemory(&memory[i + 4], x);
        r.builder().WriteMemory(&memory[i + 8], y);
        r.builder().WriteMemory(&memory[i + 12], y);
      }
      r.Call(0, 32, 64);
      for (int i = 0; i < 4; i++) {
        CheckFloatResult(x, y, expected, memory[i + 16], true /* exact */);
        CheckFloatResult(x, y, expected, memory[i + 20], true /* exact */);
      }
    }
  }
}

void RunF64x4UnOpRevecTest(WasmOpcode opcode, DoubleUnOp expected_op,
                           compiler::IrOpcode::Value revec_opcode) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  double* memory = r.builder().AddMemoryElems<double>(8);
  // Build fn to load a F64x4 vector with test value, perform unop, and write
  // the result to another array.
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;

  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimd256Unary>);
    BUILD_AND_CHECK_REVEC_NODE(
        r, revec_opcode,
        WASM_LOCAL_SET(
            temp1,
            WASM_SIMD_UNOP(opcode, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1)))),
        WASM_LOCAL_SET(
            temp2, WASM_SIMD_UNOP(opcode, WASM_SIMD_LOAD_MEM_OFFSET(
                                              offset, WASM_LOCAL_GET(param1)))),
        WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(temp1)),
        WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                   WASM_LOCAL_GET(temp2)),
        WASM_ONE);
  }
  FOR_FLOAT64_INPUTS(x) {
    if (!PlatformCanRepresent(x)) continue;
    double expected = expected_op(x);
#if V8_OS_AIX
    if (!MightReverseSign<DoubleUnOp>(expected_op))
      expected = FpOpWorkaround<double>(x, expected);
#endif
    if (!PlatformCanRepresent(expected)) continue;
    r.builder().WriteMemory(&memory[0], x);
    r.builder().WriteMemory(&memory[3], x);
    r.Call(0, 32);
    CheckDoubleResult(x, x, expected, memory[4]);
    CheckDoubleResult(x, x, expected, memory[7]);
  }

  FOR_FLOAT64_NAN_INPUTS(d) {
    double x = base::bit_cast<double>(double_nan_test_array[d]);
    if (!PlatformCanRepresent(x)) continue;
    double expected = expected_op(x);
    if (!PlatformCanRepresent(expected)) continue;
    r.builder().WriteMemory(&memory[0], x);
    r.builder().WriteMemory(&memory[3], x);
    r.Call(0, 32);
    CheckDoubleResult(x, x, expected, memory[4]);
    CheckDoubleResult(x, x, expected, memory[7]);
  }
}

void RunF64x4BinOpRevecTest(WasmOpcode opcode, DoubleBinOp expected_op,
                            compiler::IrOpcode::Value revec_opcode) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t, int32_t, int32_t> r(
      TestExecutionTier::kTurbofan);
  double* memory = r.builder().AddMemoryElems<double>(12);
  // Build fn perform binary operation on two 256 bit vectors a and b,
  // store the result in c:
  //   simd128 *a,*b,*c;
  //   *c = *a bin_op *b;
  //   *(c+1) = *(a+1) bin_op *(b+1);
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t param3 = 2;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;

  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimd256Binop>);
    BUILD_AND_CHECK_REVEC_NODE(
        r, revec_opcode,
        WASM_LOCAL_SET(
            temp1,
            WASM_SIMD_BINOP(opcode, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1)),
                            WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param2)))),
        WASM_LOCAL_SET(
            temp2,
            WASM_SIMD_BINOP(
                opcode,
                WASM_SIMD_LOAD_MEM_OFFSET(offset, WASM_LOCAL_GET(param1)),
                WASM_SIMD_LOAD_MEM_OFFSET(offset, WASM_LOCAL_GET(param2)))),
        WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param3), WASM_LOCAL_GET(temp1)),
        WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param3),
                                   WASM_LOCAL_GET(temp2)),
        WASM_ONE);
  }
  FOR_FLOAT64_INPUTS(x) {
    if (!PlatformCanRepresent(x)) continue;
    FOR_FLOAT64_INPUTS(y) {
      if (!PlatformCanRepresent(y)) continue;
      if (ShouldSkipTestingConstants(opcode, x, y)) continue;
      double expected = expected_op(x, y);
      if (!PlatformCanRepresent(expected)) continue;
      for (int i = 0; i < 2; i++) {
        r.builder().WriteMemory(&memory[i], x);
        r.builder().WriteMemory(&memory[i + 2], x);
        r.builder().WriteMemory(&memory[i + 4], y);
        r.builder().WriteMemory(&memory[i + 6], y);
      }
      r.Call(0, 32, 64);
      for (int i = 0; i < 2; i++) {
        CheckDoubleResult(x, y, expected, memory[i + 8], true /* exact */);
        CheckDoubleResult(x, y, expected, memory[i + 10], true /* exact */);
      }
    }
  }

  FOR_FLOAT64_NAN_INPUTS(f) {
    double x = base::bit_cast<double>(double_nan_test_array[f]);
    if (!PlatformCanRepresent(x)) continue;
    FOR_FLOAT64_NAN_INPUTS(j) {
      double y = base::bit_cast<double>(double_nan_test_array[j]);
      if (!PlatformCanRepresent(y)) continue;
      if (ShouldSkipTestingConstants(opcode, x, y)) continue;
      double expected = expected_op(x, y);
      if (!PlatformCanRepresent(expected)) continue;
      for (int i = 0; i < 2; i++) {
        r.builder().WriteMemory(&memory[i], x);
        r.builder().WriteMemory(&memory[i + 2], x);
        r.builder().WriteMemory(&memory[i + 4], y);
        r.builder().WriteMemory(&memory[i + 6], y);
      }
      r.Call(0, 32, 64);
      for (int i = 0; i < 2; i++) {
        CheckDoubleResult(x, y, expected, memory[i + 8], true /* exact */);
        CheckDoubleResult(x, y, expected, memory[i + 10], true /* exact */);
      }
    }
  }
}

void RunI8x32UnOpRevecTest(WasmOpcode opcode, Int8UnOp expected_op,
                           compiler::IrOpcode::Value revec_opcode) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  int8_t* memory = r.builder().AddMemoryElems<int8_t>(64);
  // Build fn to load a I8x32 vector with test value, perform unop, and write
  // the result to another array.
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimd256Unary>);

    BUILD_AND_CHECK_REVEC_NODE(
        r, revec_opcode,
        WASM_LOCAL_SET(
            temp1,
            WASM_SIMD_UNOP(opcode, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1)))),
        WASM_LOCAL_SET(
            temp2, WASM_SIMD_UNOP(opcode, WASM_SIMD_LOAD_MEM_OFFSET(
                                              offset, WASM_LOCAL_GET(param1)))),
        WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(temp1)),
        WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                   WASM_LOCAL_GET(temp2)),
        WASM_ONE);
  }
  FOR_INT8_INPUTS(x) {
    r.builder().WriteMemory(&memory[1], x);
    r.builder().WriteMemory(&memory[18], x);
    r.Call(0, 32);
    int8_t expected = expected_op(x);
    CHECK_EQ(expected, memory[33]);
    CHECK_EQ(expected, memory[50]);
  }
}

template <typename T>
void RunI32x8ConvertF32x8RevecTest(WasmOpcode opcode,
                                   ConvertToIntOp expected_op,
                                   compiler::IrOpcode::Value revec_opcode) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  float* memory = r.builder().AddMemoryElems<float>(16);
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimd256Unary>);
    BUILD_AND_CHECK_REVEC_NODE(
        r, revec_opcode,
        WASM_LOCAL_SET(
            temp1,
            WASM_SIMD_UNOP(opcode, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1)))),
        WASM_LOCAL_SET(
            temp2, WASM_SIMD_UNOP(opcode, WASM_SIMD_LOAD_MEM_OFFSET(
                                              offset, WASM_LOCAL_GET(param1)))),
        WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(temp1)),
        WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                   WASM_LOCAL_GET(temp2)),
        WASM_ONE);
  }
  bool is_unsigned = std::is_same_v<T, uint32_t>;
  FOR_FLOAT32_INPUTS(x) {
    if (!PlatformCanRepresent(x)) continue;
    for (int i = 0; i < 8; i++) {
      r.builder().WriteMemory(&memory[i], x);
    }
    r.Call(0, 32);
    int32_t expected_value = expected_op(x, is_unsigned);
    for (int i = 0; i < 8; i++) {
      CHECK_EQ(memcmp((const void*)&expected_value, &memory[8 + i], 4), 0);
    }
  }
}

// Explicit instantiations of uses.
template void RunI32x8ConvertF32x8RevecTest<int32_t>(WasmOpcode, ConvertToIntOp,
                                                     compiler::IrOpcode::Value);
template void RunI32x8ConvertF32x8RevecTest<uint32_t>(
    WasmOpcode, ConvertToIntOp, compiler::IrOpcode::Value);
#endif  // V8_ENABLE_WASM_SIMD256_REVEC

}  // namespace wasm
}  // namespace internal
}  // namespace v8
