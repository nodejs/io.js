// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/vector.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/copying-phase.h"
#include "src/compiler/turboshaft/dead-code-elimination-reducer.h"
#include "src/compiler/turboshaft/machine-optimization-reducer.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/compiler/turboshaft/required-optimization-reducer.h"
#include "test/common/flag-utils.h"
#include "test/unittests/compiler/turboshaft/reducer-test.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

class WasmSimdTest : public ReducerTest {
  // Some of the optimizations only apply with the new instruction selection and
  // are not supported by the Turbofan ISel / the RecreateSchedulePhase.
  FlagScope<bool> force_new_isel_{
      &v8_flags.turboshaft_wasm_instruction_selection_staged, true};
};

TEST_F(WasmSimdTest, UpperToLowerF32x4AddReduce) {
  auto test = CreateFromGraph(1, [](auto& Asm) {
    auto SplatKind = Simd128SplatOp::Kind::kF32x4;
    auto AddKind = Simd128BinopOp::Kind::kF32x4Add;
    auto ExtractKind = Simd128ExtractLaneOp::Kind::kF32x4;

    constexpr uint8_t upper_to_lower_1[kSimd128Size] = {
        8, 9, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0, 0, 0, 0, 0};
    constexpr uint8_t upper_to_lower_2[kSimd128Size] = {4, 5, 6, 7, 0, 0, 0, 0,
                                                        0, 0, 0, 0, 0, 0, 0, 0};

    V<Simd128> input = __ Simd128Splat(__ Float32Constant(1.0), SplatKind);
    V<Simd128> first_shuffle =
        __ Simd128Shuffle(input, input, upper_to_lower_1);
    V<Simd128> first_add = __ Simd128Binop(input, first_shuffle, AddKind);
    V<Simd128> second_shuffle =
        __ Simd128Shuffle(first_add, first_add, upper_to_lower_2);
    V<Simd128> second_add = __ Simd128Binop(first_add, second_shuffle, AddKind);
    __ Return(__ Simd128ExtractLane(second_add, ExtractKind, 0));
  });

  test.Run<MachineOptimizationReducer>();
  test.Run<DeadCodeEliminationReducer>();
  // We can only match pairwise fp operations.
  ASSERT_EQ(test.CountOp(Opcode::kSimd128Reduce), 0u);
}

TEST_F(WasmSimdTest, AlmostUpperToLowerI16x8AddReduce) {
  auto test = CreateFromGraph(1, [](auto& Asm) {
    auto SplatKind = Simd128SplatOp::Kind::kI16x8;
    auto AddKind = Simd128BinopOp::Kind::kI16x8Add;
    auto ExtractKind = Simd128ExtractLaneOp::Kind::kI16x8U;

    constexpr uint8_t almost_upper_to_lower_1[kSimd128Size] = {
        0, 0, 8, 9, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0, 0, 0,
    };
    constexpr uint8_t upper_to_lower_2[kSimd128Size] = {4, 5, 6, 7, 0, 0, 0, 0,
                                                        0, 0, 0, 0, 0, 0, 0, 0};
    constexpr uint8_t upper_to_lower_3[kSimd128Size] = {2, 3, 0, 0, 0, 0, 0, 0,
                                                        0, 0, 0, 0, 0, 0, 0, 0};

    V<Simd128> input = __ Simd128Splat(Asm.GetParameter(0), SplatKind);
    V<Simd128> first_shuffle =
        __ Simd128Shuffle(input, input, almost_upper_to_lower_1);
    V<Simd128> first_add = __ Simd128Binop(input, first_shuffle, AddKind);
    V<Simd128> second_shuffle =
        __ Simd128Shuffle(first_add, first_add, upper_to_lower_2);
    V<Simd128> second_add = __ Simd128Binop(first_add, second_shuffle, AddKind);
    V<Simd128> third_shuffle =
        __ Simd128Shuffle(second_add, second_add, upper_to_lower_3);
    V<Simd128> third_add = __ Simd128Binop(second_add, third_shuffle, AddKind);
    __ Return(__ Simd128ExtractLane(third_add, ExtractKind, 0));
  });

  test.Run<MachineOptimizationReducer>();
  test.Run<DeadCodeEliminationReducer>();

  // The first shuffle is not the one we're looking for.
  ASSERT_EQ(test.CountOp(Opcode::kSimd128Reduce), 0u);
}

TEST_F(WasmSimdTest, UpperToLowerI32x4AddReduce) {
  auto test = CreateFromGraph(1, [](auto& Asm) {
    auto SplatKind = Simd128SplatOp::Kind::kI32x4;
    auto AddKind = Simd128BinopOp::Kind::kI32x4Add;
    auto ExtractKind = Simd128ExtractLaneOp::Kind::kI32x4;

    constexpr uint8_t upper_to_lower_1[kSimd128Size] = {
        8, 9, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0, 0, 0, 0, 0};
    constexpr uint8_t upper_to_lower_2[kSimd128Size] = {4, 5, 6, 7, 0, 0, 0, 0,
                                                        0, 0, 0, 0, 0, 0, 0, 0};

    V<Simd128> input = __ Simd128Splat(Asm.GetParameter(0), SplatKind);
    V<Simd128> first_shuffle =
        __ Simd128Shuffle(input, input, upper_to_lower_1);
    V<Simd128> first_add = __ Simd128Binop(input, first_shuffle, AddKind);
    V<Simd128> second_shuffle =
        __ Simd128Shuffle(first_add, first_add, upper_to_lower_2);
    V<Simd128> second_add = __ Simd128Binop(first_add, second_shuffle, AddKind);
    __ Return(__ Simd128ExtractLane(second_add, ExtractKind, 0));
  });

  test.Run<MachineOptimizationReducer>();
  test.Run<DeadCodeEliminationReducer>();

#ifdef V8_TARGET_ARCH_ARM64
  ASSERT_EQ(test.CountOp(Opcode::kSimd128Shuffle), 0u);
  ASSERT_EQ(test.CountOp(Opcode::kSimd128Reduce), 1u);
  ASSERT_EQ(test.CountOp(Opcode::kSimd128ExtractLane), 1u);
#else
  ASSERT_EQ(test.CountOp(Opcode::kSimd128Reduce), 0u);
#endif
}

TEST_F(WasmSimdTest, PairwiseF32x4AddReduce) {
  auto test = CreateFromGraph(1, [](auto& Asm) {
    auto SplatKind = Simd128SplatOp::Kind::kF32x4;
    auto AddKind = Simd128BinopOp::Kind::kF32x4Add;
    auto ExtractKind = Simd128ExtractLaneOp::Kind::kF32x4;

    constexpr uint8_t upper_to_lower_1[kSimd128Size] = {
        4, 5, 6, 7, 0, 0, 0, 0, 12, 13, 14, 15, 0, 0, 0, 0};
    constexpr uint8_t upper_to_lower_2[kSimd128Size] = {
        8, 9, 10, 11, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    V<Simd128> input = __ Simd128Splat(__ Float32Constant(1.0), SplatKind);
    V<Simd128> first_shuffle =
        __ Simd128Shuffle(input, input, upper_to_lower_1);
    V<Simd128> first_add = __ Simd128Binop(input, first_shuffle, AddKind);
    V<Simd128> second_shuffle =
        __ Simd128Shuffle(first_add, first_add, upper_to_lower_2);
    V<Simd128> second_add = __ Simd128Binop(first_add, second_shuffle, AddKind);
    __ Return(__ Simd128ExtractLane(second_add, ExtractKind, 0));
  });

  test.Run<MachineOptimizationReducer>();
  test.Run<DeadCodeEliminationReducer>();

#ifdef V8_TARGET_ARCH_ARM64
  ASSERT_EQ(test.CountOp(Opcode::kSimd128Shuffle), 0u);
  ASSERT_EQ(test.CountOp(Opcode::kSimd128Reduce), 1u);
  ASSERT_EQ(test.CountOp(Opcode::kSimd128ExtractLane), 1u);
#else
  ASSERT_EQ(test.CountOp(Opcode::kSimd128Reduce), 0u);
#endif
}

TEST_F(WasmSimdTest, AlmostPairwiseF32x4AddReduce) {
  auto test = CreateFromGraph(1, [](auto& Asm) {
    auto SplatKind = Simd128SplatOp::Kind::kF32x4;
    auto AddKind = Simd128BinopOp::Kind::kF32x4Add;
    auto ExtractKind = Simd128ExtractLaneOp::Kind::kF32x4;

    constexpr uint8_t upper_to_lower_1[kSimd128Size] = {
        4, 5, 6, 7, 0, 0, 0, 0, 12, 13, 14, 15, 0, 0, 0, 0};
    constexpr uint8_t upper_to_lower_2[kSimd128Size] = {
        8, 9, 10, 11, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    V<Simd128> input = __ Simd128Splat(__ Float32Constant(1.0), SplatKind);
    V<Simd128> first_shuffle =
        __ Simd128Shuffle(input, input, upper_to_lower_1);
    V<Simd128> first_add = __ Simd128Binop(input, first_shuffle, AddKind);
    V<Simd128> second_shuffle =
        __ Simd128Shuffle(first_add, first_add, upper_to_lower_2);
    V<Simd128> tricksy_add = __ Simd128Binop(first_add, first_add, AddKind);
    V<Simd128> second_add =
        __ Simd128Binop(tricksy_add, second_shuffle, AddKind);
    __ Return(__ Simd128ExtractLane(second_add, ExtractKind, 0));
  });

  test.Run<MachineOptimizationReducer>();
  test.Run<DeadCodeEliminationReducer>();

  // There's an additional addition.
  ASSERT_EQ(test.CountOp(Opcode::kSimd128Reduce), 0u);
}

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft
