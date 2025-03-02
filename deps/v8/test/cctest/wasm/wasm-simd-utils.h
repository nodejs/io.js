// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include "src/base/macros.h"
#include "src/compiler/node-observer.h"
#include "src/compiler/opcodes.h"
#include "src/wasm/compilation-environment.h"
#include "src/wasm/wasm-opcodes.h"
#include "test/cctest/wasm/wasm-run-utils.h"
#include "test/common/wasm/wasm-macro-gen.h"
#ifdef V8_ENABLE_WASM_SIMD256_REVEC
#include "src/compiler/turboshaft/wasm-revec-phase.h"
#endif  // V8_ENABLE_WASM_SIMD256_REVEC

namespace v8 {
namespace internal {

#ifdef V8_ENABLE_WASM_SIMD256_REVEC

enum class ExpectedResult {
  kFail,
  kPass,
};

class TSSimd256VerifyScope {
 public:
  static bool VerifyHaveAnySimd256Op(const compiler::turboshaft::Graph& graph) {
    for (const compiler::turboshaft::Operation& op : graph.AllOperations()) {
      switch (op.opcode) {
#define CASE_SIMD256(name)                      \
  case compiler::turboshaft::Opcode::k##name: { \
    return true;                                \
  }
        TURBOSHAFT_SIMD256_OPERATION_LIST(CASE_SIMD256)
        default:
          break;
      }
#undef CASE_SIMD256
    }
    return false;
  }

  template <compiler::turboshaft::Opcode opcode>
  static bool VerifyHaveOpcode(const compiler::turboshaft::Graph& graph) {
    for (const compiler::turboshaft::Operation& op : graph.AllOperations()) {
      if (op.opcode == opcode) {
        return true;
      }
    }
    return false;
  }

  template <typename TOp, TOp::Kind op_kind>
  static bool VerifyHaveOpWithKind(const compiler::turboshaft::Graph& graph) {
    for (const compiler::turboshaft::Operation& op : graph.AllOperations()) {
      if (const TOp* t_op = op.TryCast<TOp>()) {
        if (t_op->kind == op_kind) {
          return true;
        }
      }
    }
    return false;
  }

  explicit TSSimd256VerifyScope(
      Zone* zone,
      std::function<bool(const compiler::turboshaft::Graph&)> raw_handler =
          TSSimd256VerifyScope::VerifyHaveAnySimd256Op,
      ExpectedResult expected = ExpectedResult::kPass)
      : expected_(expected) {

    std::function<void(const compiler::turboshaft::Graph&)> handler =
        [raw_handler, this](const compiler::turboshaft::Graph& graph) {
          check_pass_ = raw_handler(graph);
        };

    verifier_ =
        std::make_unique<compiler::turboshaft::WasmRevecVerifier>(handler);
    isolate_ = CcTest::InitIsolateOnce();
    DCHECK_EQ(isolate_->wasm_revec_verifier_for_test(), nullptr);
    isolate_->set_wasm_revec_verifier_for_test(verifier_.get());
  }

  ~TSSimd256VerifyScope() {
    isolate_->set_wasm_revec_verifier_for_test(nullptr);
    CHECK_EQ(expected_ == ExpectedResult::kPass, check_pass_);
  }

  bool check_pass_ = false;
  ExpectedResult expected_ = ExpectedResult::kPass;
  Isolate* isolate_ = nullptr;
  std::unique_ptr<compiler::turboshaft::WasmRevecVerifier> verifier_;
};

class SIMD256NodeObserver : public compiler::NodeObserver {
 public:
  explicit SIMD256NodeObserver(
      std::function<void(const compiler::Node*)> handler)
      : handler_(handler) {
    DCHECK(handler_);
  }

  Observation OnNodeCreated(const compiler::Node* node) override {
    handler_(node);
    return Observation::kContinue;
  }

 private:
  std::function<void(const compiler::Node*)> handler_;
};

class ObserveSIMD256Scope {
 public:
  explicit ObserveSIMD256Scope(Isolate* isolate,
                               compiler::NodeObserver* node_observer)
      : isolate_(isolate), node_observer_(node_observer) {
    DCHECK_NOT_NULL(isolate_);
    DCHECK_NULL(isolate_->node_observer());
    isolate_->set_node_observer(node_observer_);
  }

  ~ObserveSIMD256Scope() {
    DCHECK_NOT_NULL(isolate_->node_observer());
    isolate_->set_node_observer(nullptr);
  }

  Isolate* isolate_;
  compiler::NodeObserver* node_observer_;
};

// Build input wasm expressions and check if the revectorization success
// (create the expected simd256 node).
// TODO(42202660): Reimplement checks for Turboshaft (Turbofan checks were
// removed in https://crrev.com/c/6074953).
#define BUILD_AND_CHECK_REVEC_NODE(wasm_runner, expected_simd256_op, ...) \
  r.Build({__VA_ARGS__});

#endif  // V8_ENABLE_WASM_SIMD256_REVEC

namespace wasm {

using Int8UnOp = int8_t (*)(int8_t);
using Int8BinOp = int8_t (*)(int8_t, int8_t);
using Uint8BinOp = uint8_t (*)(uint8_t, uint8_t);
using Int8CompareOp = int (*)(int8_t, int8_t);
using Int8ShiftOp = int8_t (*)(int8_t, int);

using Int16UnOp = int16_t (*)(int16_t);
using Int16BinOp = int16_t (*)(int16_t, int16_t);
using Uint16BinOp = uint16_t (*)(uint16_t, uint16_t);
using Int16ShiftOp = int16_t (*)(int16_t, int);
using Int32UnOp = int32_t (*)(int32_t);
using Int32BinOp = int32_t (*)(int32_t, int32_t);
using Uint32BinOp = uint32_t (*)(uint32_t, uint32_t);
using Int32ShiftOp = int32_t (*)(int32_t, int);
using Int64UnOp = int64_t (*)(int64_t);
using Int64BinOp = int64_t (*)(int64_t, int64_t);
using Int64ShiftOp = int64_t (*)(int64_t, int);
using HalfUnOp = uint16_t (*)(uint16_t);
using HalfBinOp = uint16_t (*)(uint16_t, uint16_t);
using HalfCompareOp = int16_t (*)(uint16_t, uint16_t);
using FloatUnOp = float (*)(float);
using FloatBinOp = float (*)(float, float);
using FloatCompareOp = int32_t (*)(float, float);
using DoubleUnOp = double (*)(double);
using DoubleBinOp = double (*)(double, double);
using DoubleCompareOp = int64_t (*)(double, double);
using ConvertToIntOp = int32_t (*)(double, bool);

void RunI8x16UnOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                      Int8UnOp expected_op);

template <typename T = int8_t, typename OpType = T (*)(T, T)>
void RunI8x16BinOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                       OpType expected_op);

void RunI8x16ShiftOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                         Int8ShiftOp expected_op);
void RunI8x16MixedRelationalOpTest(TestExecutionTier execution_tier,
                                   WasmOpcode opcode, Int8BinOp expected_op);

void RunI16x8UnOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                      Int16UnOp expected_op);
template <typename T = int16_t, typename OpType = T (*)(T, T)>
void RunI16x8BinOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                       OpType expected_op);
void RunI16x8ShiftOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                         Int16ShiftOp expected_op);
void RunI16x8MixedRelationalOpTest(TestExecutionTier execution_tier,
                                   WasmOpcode opcode, Int16BinOp expected_op);

void RunI32x4UnOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                      Int32UnOp expected_op);
void RunI32x4BinOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                       Int32BinOp expected_op);
void RunI32x4ShiftOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                         Int32ShiftOp expected_op);

void RunI64x2UnOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                      Int64UnOp expected_op);
void RunI64x2BinOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                       Int64BinOp expected_op);
void RunI64x2ShiftOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                         Int64ShiftOp expected_op);

// Generic expected value functions.
template <typename T, typename = typename std::enable_if<
                          std::is_floating_point<T>::value>::type>
T Negate(T a) {
  return -a;
}

template <typename T>
T Minimum(T a, T b) {
  return std::min(a, b);
}

template <typename T>
T Maximum(T a, T b) {
  return std::max(a, b);
}

#if V8_OS_AIX
template <typename T>
bool MightReverseSign(T float_op) {
  return float_op == static_cast<T>(Negate) ||
         float_op == static_cast<T>(std::abs);
}
#endif

// Test some values not included in the float inputs from value_helper. These
// tests are useful for opcodes that are synthesized during code gen, like Min
// and Max on ia32 and x64.
static constexpr uint32_t nan_test_array[] = {
    // Bit patterns of quiet NaNs and signaling NaNs, with or without
    // additional payload.
    0x7FC00000, 0xFFC00000, 0x7FFFFFFF, 0xFFFFFFFF, 0x7F876543, 0xFF876543,
    // NaN with top payload bit unset.
    0x7FA00000,
    // Both Infinities.
    0x7F800000, 0xFF800000,
    // Some "normal" numbers, 1 and -1.
    0x3F800000, 0xBF800000};

#define FOR_FLOAT32_NAN_INPUTS(i) \
  for (size_t i = 0; i < arraysize(nan_test_array); ++i)

// Test some values not included in the double inputs from value_helper. These
// tests are useful for opcodes that are synthesized during code gen, like Min
// and Max on ia32 and x64.
static constexpr uint64_t double_nan_test_array[] = {
    // quiet NaNs, + and -
    0x7FF8000000000001, 0xFFF8000000000001,
    // with payload
    0x7FF8000000000011, 0xFFF8000000000011,
    // signaling NaNs, + and -
    0x7FF0000000000001, 0xFFF0000000000001,
    // with payload
    0x7FF0000000000011, 0xFFF0000000000011,
    // Both Infinities.
    0x7FF0000000000000, 0xFFF0000000000000,
    // Some "normal" numbers, 1 and -1.
    0x3FF0000000000000, 0xBFF0000000000000};

#define FOR_FLOAT64_NAN_INPUTS(i) \
  for (size_t i = 0; i < arraysize(double_nan_test_array); ++i)

// Returns true if the platform can represent the result.
template <typename T>
bool PlatformCanRepresent(T x) {
#if V8_TARGET_ARCH_ARM
  return std::fpclassify(x) != FP_SUBNORMAL;
#else
  return true;
#endif
}

bool isnan(uint16_t f);
bool IsCanonical(uint16_t actual);
// Returns true for very small and very large numbers. We skip these test
// values for the approximation instructions, which don't work at the extremes.
bool IsExtreme(float x);
bool IsCanonical(float actual);
void CheckFloatResult(float x, float y, float expected, float actual,
                      bool exact = true);
void CheckFloat16LaneResult(float x, float y, float z, uint16_t expected,
                            uint16_t actual, bool exact = true);
void CheckFloat16LaneResult(float x, float y, uint16_t expected,
                            uint16_t actual, bool exact = true);

bool IsExtreme(double x);
bool IsCanonical(double actual);
void CheckDoubleResult(double x, double y, double expected, double actual,
                       bool exact = true);

void RunF16x8UnOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                      HalfUnOp expected_op, bool exact = true);
void RunF16x8BinOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                       HalfBinOp expected_op);
void RunF16x8CompareOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                           HalfCompareOp expected_op);

void RunF32x4UnOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                      FloatUnOp expected_op, bool exact = true);

void RunF32x4BinOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                       FloatBinOp expected_op);

void RunF32x4CompareOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                           FloatCompareOp expected_op);

void RunF64x2UnOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                      DoubleUnOp expected_op, bool exact = true);
void RunF64x2BinOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                       DoubleBinOp expected_op);
void RunF64x2CompareOpTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                           DoubleCompareOp expected_op);

#ifdef V8_ENABLE_WASM_SIMD256_REVEC
void RunI8x32UnOpRevecTest(WasmOpcode opcode, Int8UnOp expected_op,
                           compiler::IrOpcode::Value revec_opcode);
void RunI16x16UnOpRevecTest(WasmOpcode opcode, Int16UnOp expected_op,
                            compiler::IrOpcode::Value revec_opcode);
void RunI32x8UnOpRevecTest(WasmOpcode opcode, Int32UnOp expected_op,
                           compiler::IrOpcode::Value revec_opcode);
void RunF32x8UnOpRevecTest(WasmOpcode opcode, FloatUnOp expected_op,
                           compiler::IrOpcode::Value revec_opcode);
void RunF64x4UnOpRevecTest(WasmOpcode opcode, DoubleUnOp expected_op,
                           compiler::IrOpcode::Value revec_opcode);

template <typename T = int8_t, typename OpType = T (*)(T, T)>
void RunI8x32BinOpRevecTest(WasmOpcode opcode, OpType expected_op,
                            compiler::IrOpcode::Value revec_opcode);

template <typename T = int16_t, typename OpType = T (*)(T, T)>
void RunI16x16BinOpRevecTest(WasmOpcode opcode, OpType expected_op,
                             compiler::IrOpcode::Value revec_opcode);

template <typename T = int32_t, typename OpType = T (*)(T, T)>
void RunI32x8BinOpRevecTest(WasmOpcode opcode, OpType expected_op,
                            compiler::IrOpcode::Value revec_opcode);

void RunI64x4BinOpRevecTest(WasmOpcode opcode, Int64BinOp expected_op,
                            compiler::IrOpcode::Value revec_opcode);
void RunF64x4BinOpRevecTest(WasmOpcode opcode, DoubleBinOp expected_op,
                            compiler::IrOpcode::Value revec_opcode);
void RunF32x8BinOpRevecTest(WasmOpcode opcode, FloatBinOp expected_op,
                            compiler::IrOpcode::Value revec_opcode);

void RunI16x16ShiftOpRevecTest(WasmOpcode opcode, Int16ShiftOp expected_op,
                               compiler::IrOpcode::Value revec_opcode);
void RunI32x8ShiftOpRevecTest(WasmOpcode opcode, Int32ShiftOp expected_op,
                              compiler::IrOpcode::Value revec_opcode);
void RunI64x4ShiftOpRevecTest(WasmOpcode opcode, Int64ShiftOp expected_op,
                              compiler::IrOpcode::Value revec_opcode);

template <typename IntType>
void RunI32x8ConvertF32x8RevecTest(WasmOpcode opcode,
                                   ConvertToIntOp expected_op,
                                   compiler::IrOpcode::Value revec_opcode);
template <typename IntType>
void RunF32x8ConvertI32x8RevecTest(WasmOpcode opcode,
                                   compiler::IrOpcode::Value revec_opcode);
template <typename NarrowIntType, typename WideIntType>
void RunIntSignExtensionRevecTest(WasmOpcode opcode_low, WasmOpcode opcode_high,
                                  WasmOpcode splat_op,
                                  compiler::IrOpcode::Value revec_opcode);
template <typename S, typename T>
void RunIntToIntNarrowingRevecTest(WasmOpcode opcode,
                                   compiler::IrOpcode::Value revec_opcode);
#endif  // V8_ENABLE_WASM_SIMD256_REVEC

}  // namespace wasm
}  // namespace internal
}  // namespace v8
