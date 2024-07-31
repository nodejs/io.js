// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <type_traits>

#include "src/base/overflowing-math.h"
#include "src/base/safe_conversions.h"
#include "src/common/globals.h"
#include "src/wasm/compilation-environment.h"
#include "test/cctest/cctest.h"
#include "test/cctest/wasm/wasm-run-utils.h"
#include "test/cctest/wasm/wasm-simd-utils.h"
#include "test/common/wasm/flag-utils.h"
#include "test/common/wasm/wasm-macro-gen.h"

namespace v8::internal::wasm {

// Only used for qfma and qfms tests below.

// FMOperation holds the params (a, b, c) for a Multiply-Add or
// Multiply-Subtract operation, and the expected result if the operation was
// fused, rounded only once for the entire operation, or unfused, rounded after
// multiply and again after add/subtract.
template <typename T>
struct FMOperation {
  const T a;
  const T b;
  const T c;
  const T fused_result;
  const T unfused_result;
};

// large_n is large number that overflows T when multiplied by itself, this is a
// useful constant to test fused/unfused behavior.
template <typename T>
constexpr T large_n = T(0);

template <>
constexpr double large_n<double> = 1e200;

template <>
constexpr float large_n<float> = 1e20;

// Fused Multiply-Add performs a * b + c.
template <typename T>
static constexpr FMOperation<T> qfma_array[] = {
    {2.0f, 3.0f, 1.0f, 7.0f, 7.0f},
    // fused: a * b + c = (positive overflow) + -inf = -inf
    // unfused: a * b + c = inf + -inf = NaN
    {large_n<T>, large_n<T>, -std::numeric_limits<T>::infinity(),
     -std::numeric_limits<T>::infinity(), std::numeric_limits<T>::quiet_NaN()},
    // fused: a * b + c = (negative overflow) + inf = inf
    // unfused: a * b + c = -inf + inf = NaN
    {-large_n<T>, large_n<T>, std::numeric_limits<T>::infinity(),
     std::numeric_limits<T>::infinity(), std::numeric_limits<T>::quiet_NaN()},
    // NaN
    {2.0f, 3.0f, std::numeric_limits<T>::quiet_NaN(),
     std::numeric_limits<T>::quiet_NaN(), std::numeric_limits<T>::quiet_NaN()},
    // -NaN
    {2.0f, 3.0f, -std::numeric_limits<T>::quiet_NaN(),
     std::numeric_limits<T>::quiet_NaN(), std::numeric_limits<T>::quiet_NaN()}};

template <typename T>
static constexpr base::Vector<const FMOperation<T>> qfma_vector() {
  return base::ArrayVector(qfma_array<T>);
}

// Fused Multiply-Subtract performs -(a * b) + c.
template <typename T>
static constexpr FMOperation<T> qfms_array[]{
    {2.0f, 3.0f, 1.0f, -5.0f, -5.0f},
    // fused: -(a * b) + c = - (positive overflow) + inf = inf
    // unfused: -(a * b) + c = - inf + inf = NaN
    {large_n<T>, large_n<T>, std::numeric_limits<T>::infinity(),
     std::numeric_limits<T>::infinity(), std::numeric_limits<T>::quiet_NaN()},
    // fused: -(a * b) + c = (negative overflow) + -inf = -inf
    // unfused: -(a * b) + c = -inf - -inf = NaN
    {-large_n<T>, large_n<T>, -std::numeric_limits<T>::infinity(),
     -std::numeric_limits<T>::infinity(), std::numeric_limits<T>::quiet_NaN()},
    // NaN
    {2.0f, 3.0f, std::numeric_limits<T>::quiet_NaN(),
     std::numeric_limits<T>::quiet_NaN(), std::numeric_limits<T>::quiet_NaN()},
    // -NaN
    {2.0f, 3.0f, -std::numeric_limits<T>::quiet_NaN(),
     std::numeric_limits<T>::quiet_NaN(), std::numeric_limits<T>::quiet_NaN()}};

template <typename T>
static constexpr base::Vector<const FMOperation<T>> qfms_vector() {
  return base::ArrayVector(qfms_array<T>);
}

bool ExpectFused(TestExecutionTier tier) {
#if V8_TARGET_ARCH_X64 || V8_TARGET_ARCH_IA32
  // Fused results only when fma3 feature is enabled, and running on TurboFan or
  // Liftoff (which can fall back to TurboFan if FMA is not implemented).
  return CpuFeatures::IsSupported(FMA3) &&
         (tier == TestExecutionTier::kTurbofan ||
          tier == TestExecutionTier::kLiftoff);
#elif V8_TARGET_ARCH_ARM
  // Consistent feature detection for Neonv2 is required before emitting
  // fused instructions on Arm32. Not all Neon enabled Arm32 devices have
  // FMA instructions.
  return false;
#else
  // All ARM64 Neon enabled devices have support for FMA instructions, only the
  // Liftoff/Turbofan tiers emit codegen for fused results.
  return (tier == TestExecutionTier::kTurbofan ||
          tier == TestExecutionTier::kLiftoff);
#endif  // V8_TARGET_ARCH_X64 || V8_TARGET_ARCH_IA32
}

WASM_EXEC_TEST(F32x4Qfma) {
  WasmRunner<int32_t, float, float, float> r(execution_tier);
  // Set up global to hold mask output.
  float* g = r.builder().AddGlobal<float>(kWasmS128);
  // Build fn to splat test values, perform compare op, and write the result.
  uint8_t value1 = 0, value2 = 1, value3 = 2;
  r.Build(
      {WASM_GLOBAL_SET(0, WASM_SIMD_F32x4_QFMA(
                              WASM_SIMD_F32x4_SPLAT(WASM_LOCAL_GET(value1)),
                              WASM_SIMD_F32x4_SPLAT(WASM_LOCAL_GET(value2)),
                              WASM_SIMD_F32x4_SPLAT(WASM_LOCAL_GET(value3)))),
       WASM_ONE});

  for (FMOperation<float> x : qfma_vector<float>()) {
    r.Call(x.a, x.b, x.c);
    float expected =
        ExpectFused(execution_tier) ? x.fused_result : x.unfused_result;
    for (int i = 0; i < 4; i++) {
      float actual = LANE(g, i);
      CheckFloatResult(x.a, x.b, expected, actual, true /* exact */);
    }
  }
}

WASM_EXEC_TEST(F32x4Qfms) {
  WasmRunner<int32_t, float, float, float> r(execution_tier);
  // Set up global to hold mask output.
  float* g = r.builder().AddGlobal<float>(kWasmS128);
  // Build fn to splat test values, perform compare op, and write the result.
  uint8_t value1 = 0, value2 = 1, value3 = 2;
  r.Build(
      {WASM_GLOBAL_SET(0, WASM_SIMD_F32x4_QFMS(
                              WASM_SIMD_F32x4_SPLAT(WASM_LOCAL_GET(value1)),
                              WASM_SIMD_F32x4_SPLAT(WASM_LOCAL_GET(value2)),
                              WASM_SIMD_F32x4_SPLAT(WASM_LOCAL_GET(value3)))),
       WASM_ONE});

  for (FMOperation<float> x : qfms_vector<float>()) {
    r.Call(x.a, x.b, x.c);
    float expected =
        ExpectFused(execution_tier) ? x.fused_result : x.unfused_result;
    for (int i = 0; i < 4; i++) {
      float actual = LANE(g, i);
      CheckFloatResult(x.a, x.b, expected, actual, true /* exact */);
    }
  }
}

WASM_EXEC_TEST(F64x2Qfma) {
  WasmRunner<int32_t, double, double, double> r(execution_tier);
  // Set up global to hold mask output.
  double* g = r.builder().AddGlobal<double>(kWasmS128);
  // Build fn to splat test values, perform compare op, and write the result.
  uint8_t value1 = 0, value2 = 1, value3 = 2;
  r.Build(
      {WASM_GLOBAL_SET(0, WASM_SIMD_F64x2_QFMA(
                              WASM_SIMD_F64x2_SPLAT(WASM_LOCAL_GET(value1)),
                              WASM_SIMD_F64x2_SPLAT(WASM_LOCAL_GET(value2)),
                              WASM_SIMD_F64x2_SPLAT(WASM_LOCAL_GET(value3)))),
       WASM_ONE});

  for (FMOperation<double> x : qfma_vector<double>()) {
    r.Call(x.a, x.b, x.c);
    double expected =
        ExpectFused(execution_tier) ? x.fused_result : x.unfused_result;
    for (int i = 0; i < 2; i++) {
      double actual = LANE(g, i);
      CheckDoubleResult(x.a, x.b, expected, actual, true /* exact */);
    }
  }
}

WASM_EXEC_TEST(F64x2Qfms) {
  WasmRunner<int32_t, double, double, double> r(execution_tier);
  // Set up global to hold mask output.
  double* g = r.builder().AddGlobal<double>(kWasmS128);
  // Build fn to splat test values, perform compare op, and write the result.
  uint8_t value1 = 0, value2 = 1, value3 = 2;
  r.Build(
      {WASM_GLOBAL_SET(0, WASM_SIMD_F64x2_QFMS(
                              WASM_SIMD_F64x2_SPLAT(WASM_LOCAL_GET(value1)),
                              WASM_SIMD_F64x2_SPLAT(WASM_LOCAL_GET(value2)),
                              WASM_SIMD_F64x2_SPLAT(WASM_LOCAL_GET(value3)))),
       WASM_ONE});

  for (FMOperation<double> x : qfms_vector<double>()) {
    r.Call(x.a, x.b, x.c);
    double expected =
        ExpectFused(execution_tier) ? x.fused_result : x.unfused_result;
    for (int i = 0; i < 2; i++) {
      double actual = LANE(g, i);
      CheckDoubleResult(x.a, x.b, expected, actual, true /* exact */);
    }
  }
}

TEST(RunWasm_RegressFmaReg_liftoff) {
  FLAG_SCOPE(liftoff_only);
  TestExecutionTier execution_tier = TestExecutionTier::kLiftoff;
  WasmRunner<int32_t, float, float, float> r(execution_tier);
  uint8_t local = r.AllocateLocal(kWasmS128);
  float* g = r.builder().AddGlobal<float>(kWasmS128);
  uint8_t value1 = 0, value2 = 1, value3 = 2;
  r.Build(
      {// Get the first arg from a local so that the register is blocked even
       // after the arguments have been popped off the stack. This ensures that
       // the first source register is not also the destination.
       WASM_LOCAL_SET(local, WASM_SIMD_F32x4_SPLAT(WASM_LOCAL_GET(value1))),
       WASM_GLOBAL_SET(0, WASM_SIMD_F32x4_QFMA(
                              WASM_LOCAL_GET(local),
                              WASM_SIMD_F32x4_SPLAT(WASM_LOCAL_GET(value2)),
                              WASM_SIMD_F32x4_SPLAT(WASM_LOCAL_GET(value3)))),
       WASM_ONE});

  for (FMOperation<float> x : qfma_vector<float>()) {
    r.Call(x.a, x.b, x.c);
    float expected =
        ExpectFused(execution_tier) ? x.fused_result : x.unfused_result;
    for (int i = 0; i < 4; i++) {
      float actual = LANE(g, i);
      CheckFloatResult(x.a, x.b, expected, actual, true /* exact */);
    }
  }
}

namespace {
// Helper to convert an array of T into an array of uint8_t to be used a v128
// constants.
template <typename T, size_t N = kSimd128Size / sizeof(T)>
std::array<uint8_t, kSimd128Size> as_uint8(const T* src) {
  std::array<uint8_t, kSimd128Size> arr;
  for (size_t i = 0; i < N; i++) {
    WriteLittleEndianValue<T>(reinterpret_cast<T*>(&arr[0]) + i, src[i]);
  }
  return arr;
}

template <typename T, int kElems>
void RelaxedLaneSelectTest(TestExecutionTier execution_tier, const T v1[kElems],
                           const T v2[kElems], const T s[kElems],
                           const T expected[kElems], WasmOpcode laneselect) {
  auto lhs = as_uint8<T>(v1);
  auto rhs = as_uint8<T>(v2);
  auto mask = as_uint8<T>(s);
  WasmRunner<int32_t> r(execution_tier);
  T* dst = r.builder().AddGlobal<T>(kWasmS128);
  r.Build({WASM_GLOBAL_SET(0, WASM_SIMD_OPN(laneselect, WASM_SIMD_CONSTANT(lhs),
                                            WASM_SIMD_CONSTANT(rhs),
                                            WASM_SIMD_CONSTANT(mask))),
           WASM_ONE});

  CHECK_EQ(1, r.Call());
  for (int i = 0; i < kElems; i++) {
    CHECK_EQ(expected[i], LANE(dst, i));
  }
}

}  // namespace

WASM_EXEC_TEST(I8x16RelaxedLaneSelect) {
  constexpr int kElems = 16;
  constexpr uint8_t v1[kElems] = {0, 1, 2,  3,  4,  5,  6,  7,
                                  8, 9, 10, 11, 12, 13, 14, 15};
  constexpr uint8_t v2[kElems] = {16, 17, 18, 19, 20, 21, 22, 23,
                                  24, 25, 26, 27, 28, 29, 30, 31};
  constexpr uint8_t s[kElems] = {0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF,
                                 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF};
  constexpr uint8_t expected[kElems] = {16, 1, 18, 3,  20, 5,  22, 7,
                                        24, 9, 26, 11, 28, 13, 30, 15};
  RelaxedLaneSelectTest<uint8_t, kElems>(execution_tier, v1, v2, s, expected,
                                         kExprI8x16RelaxedLaneSelect);
}

WASM_EXEC_TEST(I16x8RelaxedLaneSelect) {
  constexpr int kElems = 8;
  uint16_t v1[kElems] = {0, 1, 2, 3, 4, 5, 6, 7};
  uint16_t v2[kElems] = {8, 9, 10, 11, 12, 13, 14, 15};
  uint16_t s[kElems] = {0, 0xFFFF, 0, 0xFFFF, 0, 0xFFFF, 0, 0xFFFF};
  constexpr uint16_t expected[kElems] = {8, 1, 10, 3, 12, 5, 14, 7};
  RelaxedLaneSelectTest<uint16_t, kElems>(execution_tier, v1, v2, s, expected,
                                          kExprI16x8RelaxedLaneSelect);
}

WASM_EXEC_TEST(I32x4RelaxedLaneSelect) {
  constexpr int kElems = 4;
  uint32_t v1[kElems] = {0, 1, 2, 3};
  uint32_t v2[kElems] = {4, 5, 6, 7};
  uint32_t s[kElems] = {0, 0xFFFF'FFFF, 0, 0xFFFF'FFFF};
  constexpr uint32_t expected[kElems] = {4, 1, 6, 3};
  RelaxedLaneSelectTest<uint32_t, kElems>(execution_tier, v1, v2, s, expected,
                                          kExprI32x4RelaxedLaneSelect);
}

WASM_EXEC_TEST(I64x2RelaxedLaneSelect) {
  constexpr int kElems = 2;
  uint64_t v1[kElems] = {0, 1};
  uint64_t v2[kElems] = {2, 3};
  uint64_t s[kElems] = {0, 0xFFFF'FFFF'FFFF'FFFF};
  constexpr uint64_t expected[kElems] = {2, 1};
  RelaxedLaneSelectTest<uint64_t, kElems>(execution_tier, v1, v2, s, expected,
                                          kExprI64x2RelaxedLaneSelect);
}

WASM_EXEC_TEST(F32x4RelaxedMin) {
  RunF32x4BinOpTest(execution_tier, kExprF32x4RelaxedMin, Minimum);
}

WASM_EXEC_TEST(F32x4RelaxedMax) {
  RunF32x4BinOpTest(execution_tier, kExprF32x4RelaxedMax, Maximum);
}

WASM_EXEC_TEST(F64x2RelaxedMin) {
  RunF64x2BinOpTest(execution_tier, kExprF64x2RelaxedMin, Minimum);
}

WASM_EXEC_TEST(F64x2RelaxedMax) {
  RunF64x2BinOpTest(execution_tier, kExprF64x2RelaxedMax, Maximum);
}

namespace {
// For relaxed trunc instructions, don't test out of range values.
// FloatType comes later so caller can rely on template argument deduction and
// just pass IntType.
template <typename IntType, typename FloatType>
typename std::enable_if<std::is_floating_point<FloatType>::value, bool>::type
ShouldSkipTestingConstant(FloatType x) {
  return std::isnan(x) || !base::IsValueInRangeForNumericType<IntType>(x) ||
         !PlatformCanRepresent(x);
}

template <typename IntType, typename FloatType>
void IntRelaxedTruncFloatTest(TestExecutionTier execution_tier,
                              WasmOpcode trunc_op, WasmOpcode splat_op) {
  WasmRunner<int, FloatType> r(execution_tier);
  IntType* g0 = r.builder().template AddGlobal<IntType>(kWasmS128);
  constexpr int lanes = kSimd128Size / sizeof(FloatType);

  // global[0] = trunc(splat(local[0])).
  r.Build({WASM_GLOBAL_SET(
               0, WASM_SIMD_UNOP(trunc_op,
                                 WASM_SIMD_UNOP(splat_op, WASM_LOCAL_GET(0)))),
           WASM_ONE});

  for (FloatType x : compiler::ValueHelper::GetVector<FloatType>()) {
    if (ShouldSkipTestingConstant<IntType>(x)) continue;
    CHECK_EQ(1, r.Call(x));
    IntType expected = base::checked_cast<IntType>(x);
    for (int i = 0; i < lanes; i++) {
      CHECK_EQ(expected, LANE(g0, i));
    }
  }
}
}  // namespace

WASM_EXEC_TEST(I32x4RelaxedTruncF64x2SZero) {
  IntRelaxedTruncFloatTest<int32_t, double>(
      execution_tier, kExprI32x4RelaxedTruncF64x2SZero, kExprF64x2Splat);
}

WASM_EXEC_TEST(I32x4RelaxedTruncF64x2UZero) {
  IntRelaxedTruncFloatTest<uint32_t, double>(
      execution_tier, kExprI32x4RelaxedTruncF64x2UZero, kExprF64x2Splat);
}

WASM_EXEC_TEST(I32x4RelaxedTruncF32x4S) {
  IntRelaxedTruncFloatTest<int32_t, float>(
      execution_tier, kExprI32x4RelaxedTruncF32x4S, kExprF32x4Splat);
}

WASM_EXEC_TEST(I32x4RelaxedTruncF32x4U) {
  IntRelaxedTruncFloatTest<uint32_t, float>(
      execution_tier, kExprI32x4RelaxedTruncF32x4U, kExprF32x4Splat);
}

WASM_EXEC_TEST(I8x16RelaxedSwizzle) {
  // Output is only defined for indices in the range [0,15].
  WasmRunner<int32_t> r(execution_tier);
  static const int kElems = kSimd128Size / sizeof(uint8_t);
  uint8_t* dst = r.builder().AddGlobal<uint8_t>(kWasmS128);
  uint8_t* src = r.builder().AddGlobal<uint8_t>(kWasmS128);
  uint8_t* indices = r.builder().AddGlobal<uint8_t>(kWasmS128);
  r.Build({WASM_GLOBAL_SET(
               0, WASM_SIMD_BINOP(kExprI8x16RelaxedSwizzle, WASM_GLOBAL_GET(1),
                                  WASM_GLOBAL_GET(2))),
           WASM_ONE});
  for (int i = 0; i < kElems; i++) {
    LANE(src, i) = kElems - i - 1;
    LANE(indices, i) = kElems - i - 1;
  }
  CHECK_EQ(1, r.Call());
  for (int i = 0; i < kElems; i++) {
    CHECK_EQ(LANE(dst, i), i);
  }
}

WASM_EXEC_TEST(I16x8RelaxedQ15MulRS) {
  WasmRunner<int32_t, int16_t, int16_t> r(execution_tier);
  // Global to hold output.
  int16_t* g = r.builder().template AddGlobal<int16_t>(kWasmS128);
  // Build fn to splat test values, perform binop, and write the result.
  uint8_t value1 = 0, value2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_I16x8_SPLAT(WASM_LOCAL_GET(value1))),
           WASM_LOCAL_SET(temp2, WASM_SIMD_I16x8_SPLAT(WASM_LOCAL_GET(value2))),
           WASM_GLOBAL_SET(0, WASM_SIMD_BINOP(kExprI16x8RelaxedQ15MulRS,
                                              WASM_LOCAL_GET(temp1),
                                              WASM_LOCAL_GET(temp2))),
           WASM_ONE});

  for (int16_t x : compiler::ValueHelper::GetVector<int16_t>()) {
    for (int16_t y : compiler::ValueHelper::GetVector<int16_t>()) {
      // Results are dependent on the underlying hardware when both inputs are
      // INT16_MIN, we could do something specific to test for x64/ARM behavior
      // but predictably other supported V8 platforms will have to test specific
      // behavior in that case, given that the lowering is fairly
      // straighforward, and occurence of this in higher level programs is rare,
      // this is okay to skip.
      if (x == INT16_MIN && y == INT16_MIN) break;
      r.Call(x, y);
      int16_t expected = SaturateRoundingQMul(x, y);
      for (int i = 0; i < 8; i++) {
        CHECK_EQ(expected, LANE(g, i));
      }
    }
  }
}

WASM_EXEC_TEST(I16x8DotI8x16I7x16S) {
  WasmRunner<int32_t, int8_t, int8_t> r(execution_tier);
  int16_t* g = r.builder().template AddGlobal<int16_t>(kWasmS128);
  uint8_t value1 = 0, value2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_I8x16_SPLAT(WASM_LOCAL_GET(value1))),
           WASM_LOCAL_SET(temp2, WASM_SIMD_I8x16_SPLAT(WASM_LOCAL_GET(value2))),
           WASM_GLOBAL_SET(0, WASM_SIMD_BINOP(kExprI16x8DotI8x16I7x16S,
                                              WASM_LOCAL_GET(temp1),
                                              WASM_LOCAL_GET(temp2))),
           WASM_ONE});

  for (int8_t x : compiler::ValueHelper::GetVector<int8_t>()) {
    for (int8_t y : compiler::ValueHelper::GetVector<int8_t>()) {
      r.Call(x, y & 0x7F);
      // * 2 because we of (x*y) + (x*y) = 2*x*y
      int16_t expected = base::MulWithWraparound(x * (y & 0x7F), 2);
      for (int i = 0; i < 8; i++) {
        CHECK_EQ(expected, LANE(g, i));
      }
    }
  }
}

WASM_EXEC_TEST(I32x4DotI8x16I7x16AddS) {
  WasmRunner<int32_t, int8_t, int8_t, int32_t> r(execution_tier);
  int32_t* g = r.builder().template AddGlobal<int32_t>(kWasmS128);
  uint8_t value1 = 0, value2 = 1, value3 = 2;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_I8x16_SPLAT(WASM_LOCAL_GET(value1))),
           WASM_LOCAL_SET(temp2, WASM_SIMD_I8x16_SPLAT(WASM_LOCAL_GET(value2))),
           WASM_LOCAL_SET(temp3, WASM_SIMD_I32x4_SPLAT(WASM_LOCAL_GET(value3))),
           WASM_GLOBAL_SET(
               0, WASM_SIMD_TERNOP(kExprI32x4DotI8x16I7x16AddS,
                                   WASM_LOCAL_GET(temp1), WASM_LOCAL_GET(temp2),
                                   WASM_LOCAL_GET(temp3))),
           WASM_ONE});

  for (int8_t x : compiler::ValueHelper::GetVector<int8_t>()) {
    for (int8_t y : compiler::ValueHelper::GetVector<int8_t>()) {
      for (int32_t z : compiler::ValueHelper::GetVector<int32_t>()) {
        int32_t expected = base::AddWithWraparound(
            base::MulWithWraparound(x * (y & 0x7F), 4), z);
        r.Call(x, y & 0x7F, z);
        for (int i = 0; i < 4; i++) {
          CHECK_EQ(expected, LANE(g, i));
        }
      }
    }
  }
}

}  // namespace v8::internal::wasm
