// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/machine-type.h"
#include "src/common/globals.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/objects/objects-inl.h"
#include "test/unittests/compiler/backend/turboshaft-instruction-selector-unittest.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace turboshaft {
namespace {
template <typename Op>
struct MachInst {
  Op op;
  const char* constructor_name;
  ArchOpcode arch_opcode;
  MachineType machine_type;
};

template <typename T>
std::ostream& operator<<(std::ostream& os, const MachInst<T>& mi) {
  return os << mi.constructor_name;
}

using MachInst1 = MachInst<TSUnop>;
using MachInst2 = MachInst<TSBinop>;

// To avoid duplicated code IntCmp helper structure
// is created. It contains MachInst2 with two nodes and expected_size
// because different cmp instructions have different size.
struct IntCmp {
  MachInst2 mi;
  uint32_t expected_size;
};

struct FPCmp {
  MachInst2 mi;
  FlagsCondition cond;
};

const FPCmp kFPCmpInstructions[] = {
    {{TSBinop::kFloat64Equal, "Float64Equal", kRiscvCmpD,
      MachineType::Float64()},
     kEqual},
    {{TSBinop::kFloat64LessThan, "Float64LessThan", kRiscvCmpD,
      MachineType::Float64()},
     kUnsignedLessThan},
    {{TSBinop::kFloat64LessThanOrEqual, "Float64LessThanOrEqual", kRiscvCmpD,
      MachineType::Float64()},
     kUnsignedLessThanOrEqual},
    //TODO(riscv): ADD kFloat64GreaterThan/kFloat64GreaterThanOrEqual
    // in turboshaft-unittestes.
    // {{TSBinop::kFloat64GreaterThan, "Float64GreaterThan",
    //   kRiscvCmpD, MachineType::Float64()},
    //  kUnsignedLessThan},
    // {{TSBinop::kFloat64GreaterThanOrEqual,
    //   "Float64GreaterThanOrEqual", kRiscvCmpD, MachineType::Float64()},
    //  kUnsignedLessThanOrEqual}
};

struct Conversion {
  // The machine_type field in MachInst1 represents the destination type.
  MachInst1 mi;
  MachineType src_machine_type;
};

// ----------------------------------------------------------------------------
// Logical instructions.
// ----------------------------------------------------------------------------

const MachInst2 kLogicalInstructions[] = {
    {TSBinop::kWord32BitwiseAnd, "Word32And", kRiscvAnd, MachineType::Int32()},
    {TSBinop::kWord32BitwiseOr, "Word32Or", kRiscvOr, MachineType::Int32()},
    {TSBinop::kWord32BitwiseXor, "Word32Xor", kRiscvXor, MachineType::Int32()}};

// ----------------------------------------------------------------------------
// Shift instructions.
// ----------------------------------------------------------------------------
const MachInst2 kShiftInstructions[] = {
    {TSBinop::kWord32ShiftLeft, "Word32Shl", kRiscvShl32, MachineType::Int32()},
    {TSBinop::kWord32ShiftRightLogical, "Word32Shr", kRiscvShr32,
     MachineType::Int32()},
    {TSBinop::kWord32ShiftRightArithmetic, "Word32Sar", kRiscvSar32,
     MachineType::Int32()},
    {TSBinop::kWord32RotateRight, "Word32Ror", kRiscvRor32,
     MachineType::Int32()}};

// ----------------------------------------------------------------------------
// MUL/DIV instructions.
// ----------------------------------------------------------------------------

const MachInst2 kMulDivInstructions[] = {
    {TSBinop::kWord32Mul, "Int32Mul", kRiscvMul32, MachineType::Int32()},
    {TSBinop::kInt32Div, "Int32Div", kRiscvDiv32, MachineType::Int32()},
    {TSBinop::kUint32Div, "Uint32Div", kRiscvDivU32, MachineType::Uint32()},
    {TSBinop::kFloat64Mul, "Float64Mul", kRiscvMulD, MachineType::Float64()},
    {TSBinop::kFloat64Div, "Float64Div", kRiscvDivD, MachineType::Float64()}};

// ----------------------------------------------------------------------------
// MOD instructions.
// ----------------------------------------------------------------------------

const MachInst2 kModInstructions[] = {
    {TSBinop::kInt32Mod, "Int32Mod", kRiscvMod32, MachineType::Int32()},
    {TSBinop::kUint32Mod, "Uint32Mod", kRiscvModU32, MachineType::Int32()},
    // {TSBinop::kFloat64Mod, "Float64Mod", kRiscvModD,
    //  MachineType::Float64()}
};

// ----------------------------------------------------------------------------
// Arithmetic FPU instructions.
// ----------------------------------------------------------------------------

const MachInst2 kFPArithInstructions[] = {
    {TSBinop::kFloat64Add, "Float64Add", kRiscvAddD, MachineType::Float64()},
    {TSBinop::kFloat64Sub, "Float64Sub", kRiscvSubD, MachineType::Float64()}};

// ----------------------------------------------------------------------------
// IntArithTest instructions, two nodes.
// ----------------------------------------------------------------------------

const MachInst2 kAddSubInstructions[] = {
    {TSBinop::kWord32Add, "Int32Add", kRiscvAdd32, MachineType::Int32()},
    {TSBinop::kWord32Sub, "Int32Sub", kRiscvSub32, MachineType::Int32()}};

// ----------------------------------------------------------------------------
// IntArithTest instructions, one node.
// ----------------------------------------------------------------------------

const MachInst1 kAddSubOneInstructions[] = {
    // {TSBinop::kInt32Neg, "Int32Neg", kRiscvSub32,
    //  MachineType::Int32()},
};

// ----------------------------------------------------------------------------
// Arithmetic compare instructions.
// ----------------------------------------------------------------------------

const IntCmp kCmpInstructions[] = {
    // {{TSBinop::kWordEqual, "WordEqual", kRiscvCmp,
    //   MachineType::Int64()},
    //  1U},
    // {{TSBinop::kWordNotEqual, "WordNotEqual", kRiscvCmp,
    //   MachineType::Int64()},
    //  1U},
    // {{TSBinop::kWord32Equal, "Word32Equal", kRiscvCmp,
    //   MachineType::Int32()},
    //  1U},
    // {{TSBinop::kWord32NotEqual, "Word32NotEqual", kRiscvCmp,
    //   MachineType::Int32()},
    //  1U},
    {{TSBinop::kInt32LessThan, "Int32LessThan", kRiscvCmp,
      MachineType::Int32()},
     1U},
    {{TSBinop::kInt32LessThanOrEqual, "Int32LessThanOrEqual", kRiscvCmp,
      MachineType::Int32()},
     1U},
    {{TSBinop::kInt32GreaterThan, "Int32GreaterThan", kRiscvCmp,
      MachineType::Int32()},
     1U},
    {{TSBinop::kInt32GreaterThanOrEqual, "Int32GreaterThanOrEqual", kRiscvCmp,
      MachineType::Int32()},
     1U},
    {{TSBinop::kUint32LessThan, "Uint32LessThan", kRiscvCmp,
      MachineType::Uint32()},
     1U},
    {{TSBinop::kUint32LessThanOrEqual, "Uint32LessThanOrEqual", kRiscvCmp,
      MachineType::Uint32()},
     1U}};

// ----------------------------------------------------------------------------
// Conversion instructions.
// ----------------------------------------------------------------------------

const Conversion kConversionInstructions[] = {
    // Conversion instructions are related to machine_operator.h:
    // FPU conversions:
    // Convert representation of integers between float64 and int32/uint32.
    // The precise rounding mode and handling of out of range inputs are *not*
    // defined for these operators, since they are intended only for use with
    // integers.
    {{TSUnop::kChangeInt32ToFloat64, "ChangeInt32ToFloat64", kRiscvCvtDW,
      MachineType::Float64()},
     MachineType::Int32()},
    {{TSUnop::kChangeUint32ToFloat64, "ChangeUint32ToFloat64", kRiscvCvtDUw,
      MachineType::Float64()},
     MachineType::Int32()},

    // {{TSUnop::kChangeFloat64ToInt32, "ChangeFloat64ToInt32",
    //   kRiscvTruncWD, MachineType::Float64()},
    //  MachineType::Int32()},

    // {{TSUnop::kChangeFloat64ToUint32, "ChangeFloat64ToUint32",
    //   kRiscvTruncUwD, MachineType::Float64()},
    //  MachineType::Int32()}
};

const Conversion kFloat32RoundInstructions[] = {
    // {{TSUnop::kFloat32RoundUp, "Float32RoundUp",
    //   kRiscvFloat32RoundUp, MachineType::Int32()},
    //  MachineType::Float32()},
    // {{TSUnop::kFloat32RoundDown, "Float32RoundDown",
    //   kRiscvFloat32RoundDown, MachineType::Int32()},
    //  MachineType::Float32()},
    // {{TSUnop::Float32RoundTiesEven, "Float32RoundTiesEven",
    //   kRiscvFloat32RoundTiesEven, MachineType::Int32()},
    //  MachineType::Float32()},
    // {{TSUnop::Float32RoundTruncate, "Float32RoundTruncate",
    //   kRiscvFloat32RoundTruncate, MachineType::Int32()},
    //  MachineType::Float32()}
};

}  // namespace

using TurboshaftInstructionSelectorFPCmpTest =
    TurboshaftInstructionSelectorTestWithParam<FPCmp>;

TEST_P(TurboshaftInstructionSelectorFPCmpTest, Parameter) {
  const FPCmp cmp = GetParam();
  StreamBuilder m(this, MachineType::Int32(), cmp.mi.machine_type,
                  cmp.mi.machine_type);
  m.Return(m.Emit(cmp.mi.op, m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(cmp.mi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_set, s[0]->flags_mode());
  EXPECT_EQ(cmp.cond, s[0]->flags_condition());
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorFPCmpTest,
                         ::testing::ValuesIn(kFPCmpInstructions));

// ----------------------------------------------------------------------------
// Arithmetic compare instructions integers
// ----------------------------------------------------------------------------
using TurboshaftInstructionSelectorCmpTest =
    TurboshaftInstructionSelectorTestWithParam<IntCmp>;

TEST_P(TurboshaftInstructionSelectorCmpTest, Parameter) {
  const IntCmp cmp = GetParam();
  const MachineType type = cmp.mi.machine_type;
  StreamBuilder m(this, type, type, type);
  m.Return(m.Emit(cmp.mi.op, m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();

  if (v8_flags.debug_code &&
      type.representation() == MachineRepresentation::kWord32) {
    ASSERT_EQ(1U, s.size());

    EXPECT_EQ(cmp.mi.arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  } else {
    ASSERT_EQ(cmp.expected_size, s.size());
    EXPECT_EQ(cmp.mi.arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorCmpTest,
                         ::testing::ValuesIn(kCmpInstructions));

// ----------------------------------------------------------------------------
// Shift instructions.
// ----------------------------------------------------------------------------
using TurboshaftInstructionSelectorShiftTest =
    TurboshaftInstructionSelectorTestWithParam<MachInst2>;

TEST_P(TurboshaftInstructionSelectorShiftTest, Immediate) {
  const MachInst2 dpi = GetParam();
  const MachineType type = dpi.machine_type;
  TRACED_FORRANGE(int32_t, imm, 0,
                  ((1 << ElementSizeLog2Of(type.representation())) * 8) - 1) {
    StreamBuilder m(this, type, type);
    m.Return(m.Emit(dpi.op, m.Parameter(0), m.Int32Constant(imm)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(2U, s[0]->InputCount());
    EXPECT_TRUE(s[0]->InputAt(1)->IsImmediate());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorShiftTest,
                         ::testing::ValuesIn(kShiftInstructions));

// ----------------------------------------------------------------------------
// Logical instructions.
// ----------------------------------------------------------------------------
using TurboshaftInstructionSelectorLogicalTest =
    TurboshaftInstructionSelectorTestWithParam<MachInst2>;

TEST_P(TurboshaftInstructionSelectorLogicalTest, Parameter) {
  const MachInst2 dpi = GetParam();
  const MachineType type = dpi.machine_type;
  StreamBuilder m(this, type, type, type);
  m.Return(m.Emit(dpi.op, m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorLogicalTest,
                         ::testing::ValuesIn(kLogicalInstructions));

// TEST_F(TurboshaftInstructionSelectorTest, Word32XorMinusOneWithParameter) {
//   {
//     StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
//     m.Return(m.Word32Xor(m.Parameter(0), m.Int32Constant(-1)));
//     Stream s = m.Build();
//     ASSERT_EQ(1U, s.size());
//     EXPECT_EQ(kRiscvNor, s[0]->arch_opcode());
//     EXPECT_EQ(2U, s[0]->InputCount());
//     EXPECT_EQ(1U, s[0]->OutputCount());
//   }
//   {
//     StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
//     m.Return(m.Word32Xor(m.Int32Constant(-1), m.Parameter(0)));
//     Stream s = m.Build();
//     ASSERT_EQ(1U, s.size());
//     EXPECT_EQ(kRiscvNor, s[0]->arch_opcode());
//     EXPECT_EQ(2U, s[0]->InputCount());
//     EXPECT_EQ(1U, s[0]->OutputCount());
//   }
// }

// TEST_F(TurboshaftInstructionSelectorTest, Word32XorMinusOneWithWord32Or) {
//   {
//     StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
//     m.Return(m.Word32Xor(m.Word32Or(m.Parameter(0), m.Parameter(0)),
//                          m.Int32Constant(-1)));
//     Stream s = m.Build();
//     ASSERT_EQ(1U, s.size());
//     EXPECT_EQ(kRiscvNor, s[0]->arch_opcode());
//     EXPECT_EQ(2U, s[0]->InputCount());
//     EXPECT_EQ(1U, s[0]->OutputCount());
//   }
//   {
//     StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
//     m.Return(m.Word32Xor(m.Int32Constant(-1),
//                          m.Word32Or(m.Parameter(0), m.Parameter(0))));
//     Stream s = m.Build();
//     ASSERT_EQ(1U, s.size());
//     EXPECT_EQ(kRiscvNor, s[0]->arch_opcode());
//     EXPECT_EQ(2U, s[0]->InputCount());
//     EXPECT_EQ(1U, s[0]->OutputCount());
//   }
// }

// TEST_F(TurboshaftInstructionSelectorTest, Word32ShlWithWord32And) {
//   TRACED_FORRANGE(int32_t, shift, 0, 30) {
//     StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
//     Node* const p0 = m.Parameter(0);
//     Node* const r =
//         m.Word32Shl(m.Word32And(p0, m.Int32Constant((1 << (31 - shift)) -
//         1)),
//                     m.Int32Constant(shift + 1));
//     m.Return(r);
//     Stream s = m.Build();
//     ASSERT_EQ(1U, s.size());
//     EXPECT_EQ(kRiscvShl32, s[0]->arch_opcode());
//     ASSERT_EQ(2U, s[0]->InputCount());
//     EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
//     ASSERT_EQ(1U, s[0]->OutputCount());
//     EXPECT_EQ(s.ToVreg(r), s.ToVreg(s[0]->Output()));
//   }
// }

// // TEST_F(TurboshaftInstructionSelectorTest, Word64ShlWithWord64And) {
// //   TRACED_FORRANGE(int32_t, shift, 0, 62) {
// //     StreamBuilder m(this, MachineType::Int64(), MachineType::Int64());
// //     Node* const p0 = m.Parameter(0);
// //     Node* const r =
// //         m.Word64Shl(m.Word64And(p0, m.Int64Constant((1L << (63 - shift)) -
// //         1)),
// //                     m.Int64Constant(shift + 1));
// //     m.Return(r);
// //     Stream s = m.Build();
// //     ASSERT_EQ(1U, s.size());
// //     EXPECT_EQ(kRiscvShl64, s[0]->arch_opcode());
// //     ASSERT_EQ(2U, s[0]->InputCount());
// //     EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
// //     ASSERT_EQ(1U, s[0]->OutputCount());
// //     EXPECT_EQ(s.ToVreg(r), s.ToVreg(s[0]->Output()));
// //   }
// // }

// TEST_F(TurboshaftInstructionSelectorTest, Word32SarWithWord32Shl) {
//   {
//     StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
//     Node* const p0 = m.Parameter(0);
//     Node* const r =
//         m.Word32Sar(m.Word32Shl(p0, m.Int32Constant(24)),
//         m.Int32Constant(24));
//     m.Return(r);
//     Stream s = m.Build();
//     ASSERT_EQ(1U, s.size());
//     EXPECT_EQ(kRiscvSignExtendByte, s[0]->arch_opcode());
//     ASSERT_EQ(1U, s[0]->InputCount());
//     EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
//     ASSERT_EQ(1U, s[0]->OutputCount());
//     EXPECT_EQ(s.ToVreg(r), s.ToVreg(s[0]->Output()));
//   }
//   {
//     StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
//     Node* const p0 = m.Parameter(0);
//     Node* const r =
//         m.Word32Sar(m.Word32Shl(p0, m.Int32Constant(16)),
//         m.Int32Constant(16));
//     m.Return(r);
//     Stream s = m.Build();
//     ASSERT_EQ(1U, s.size());
//     EXPECT_EQ(kRiscvSignExtendShort, s[0]->arch_opcode());
//     ASSERT_EQ(1U, s[0]->InputCount());
//     EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
//     ASSERT_EQ(1U, s[0]->OutputCount());
//     EXPECT_EQ(s.ToVreg(r), s.ToVreg(s[0]->Output()));
//   }
//   {
//     StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
//     Node* const p0 = m.Parameter(0);
//     Node* const r =
//         m.Word32Sar(m.Word32Shl(p0, m.Int32Constant(32)),
//         m.Int32Constant(32));
//     m.Return(r);
//     Stream s = m.Build();
//     ASSERT_EQ(1U, s.size());
//     EXPECT_EQ(kRiscvShl32, s[0]->arch_opcode());
//     ASSERT_EQ(2U, s[0]->InputCount());
//     EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
//     EXPECT_EQ(0, s.ToInt32(s[0]->InputAt(1)));
//     ASSERT_EQ(1U, s[0]->OutputCount());
//     EXPECT_EQ(s.ToVreg(r), s.ToVreg(s[0]->Output()));
//   }
// }

// ----------------------------------------------------------------------------
// MUL/DIV instructions.
// ----------------------------------------------------------------------------
using TurboshaftInstructionSelectorMulDivTest =
    TurboshaftInstructionSelectorTestWithParam<MachInst2>;

TEST_P(TurboshaftInstructionSelectorMulDivTest, Parameter) {
  const MachInst2 dpi = GetParam();
  const MachineType type = dpi.machine_type;
  StreamBuilder m(this, type, type, type);
  m.Return(m.Emit(dpi.op, m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorMulDivTest,
                         ::testing::ValuesIn(kMulDivInstructions));

// ----------------------------------------------------------------------------
// MOD instructions.
// ----------------------------------------------------------------------------
using TurboshaftInstructionSelectorModTest =
    TurboshaftInstructionSelectorTestWithParam<MachInst2>;

TEST_P(TurboshaftInstructionSelectorModTest, Parameter) {
  const MachInst2 dpi = GetParam();
  const MachineType type = dpi.machine_type;
  StreamBuilder m(this, type, type, type);
  m.Return(m.Emit(dpi.op, m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorModTest,
                         ::testing::ValuesIn(kModInstructions));

// ----------------------------------------------------------------------------
// Floating point instructions.
// ----------------------------------------------------------------------------
using TurboshaftInstructionSelectorFPArithTest =
    TurboshaftInstructionSelectorTestWithParam<MachInst2>;

TEST_P(TurboshaftInstructionSelectorFPArithTest, Parameter) {
  const MachInst2 fpa = GetParam();
  StreamBuilder m(this, fpa.machine_type, fpa.machine_type, fpa.machine_type);
  m.Return(m.Emit(fpa.op, m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(fpa.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorFPArithTest,
                         ::testing::ValuesIn(kFPArithInstructions));
// ----------------------------------------------------------------------------
// Integer arithmetic
// ----------------------------------------------------------------------------
using TurboshaftInstructionSelectorIntArithTwoTest =
    TurboshaftInstructionSelectorTestWithParam<MachInst2>;

TEST_P(TurboshaftInstructionSelectorIntArithTwoTest, Parameter) {
  const MachInst2 intpa = GetParam();
  StreamBuilder m(this, intpa.machine_type, intpa.machine_type,
                  intpa.machine_type);
  m.Return(m.Emit(intpa.op, m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(intpa.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorIntArithTwoTest,
                         ::testing::ValuesIn(kAddSubInstructions));

// ----------------------------------------------------------------------------
// One node.
// ----------------------------------------------------------------------------

// using InstructionSelectorIntArithOneTest =
//     InstructionSelectorTestWithParam<MachInst1>;

// TEST_P(TurboshaftInstructionSelectorIntArithOneTest, Parameter) {
//   const MachInst1 intpa = GetParam();
//   StreamBuilder m(this, intpa.machine_type, intpa.machine_type,
//                   intpa.machine_type);
//   m.Return((m.*intpa.constructor)(m.Parameter(0)));
//   Stream s = m.Build();
//   ASSERT_EQ(1U, s.size());
//   EXPECT_EQ(intpa.arch_opcode, s[0]->arch_opcode());
//   EXPECT_EQ(2U, s[0]->InputCount());
//   EXPECT_EQ(1U, s[0]->OutputCount());
// }

// INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
//                          InstructionSelectorIntArithOneTest,
//                          ::testing::ValuesIn(kAddSubOneInstructions));
// ----------------------------------------------------------------------------
// Conversions.
// ----------------------------------------------------------------------------
using TurboshaftInstructionSelectorConversionTest =
    TurboshaftInstructionSelectorTestWithParam<Conversion>;

TEST_P(TurboshaftInstructionSelectorConversionTest, Parameter) {
  const Conversion conv = GetParam();
  StreamBuilder m(this, conv.mi.machine_type, conv.src_machine_type);
  m.Return(m.Emit(conv.mi.op, (m.Parameter(0))));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(conv.mi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(1U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorConversionTest,
                         ::testing::ValuesIn(kConversionInstructions));

using TurboshaftCombineChangeFloat32ToInt32WithRoundFloat32 =
    TurboshaftInstructionSelectorTestWithParam<Conversion>;

// TEST_P(TurboshaftCombineChangeFloat32ToInt32WithRoundFloat32, Parameter) {
//   {
//     const Conversion conv = GetParam();
//     StreamBuilder m(this, conv.mi.machine_type, conv.src_machine_type);
//     m.Return(m.ChangeFloat64ToInt32(
//         m.ChangeFloat32ToFloat64((m.*conv.mi.constructor)(m.Parameter(0)))));
//     Stream s = m.Build();
//     ASSERT_EQ(2U, s.size());
//     EXPECT_EQ(conv.mi.arch_opcode, s[0]->arch_opcode());
//     EXPECT_EQ(kRiscvTruncWS, s[1]->arch_opcode());
//     EXPECT_EQ(kMode_None, s[0]->addressing_mode());
//     ASSERT_EQ(1U, s[0]->InputCount());
//     EXPECT_EQ(1U, s[0]->OutputCount());
//   }
// }

// INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
//                          CombineChangeFloat32ToInt32WithRoundFloat32,
//                          ::testing::ValuesIn(kFloat32RoundInstructions));

TEST_F(TurboshaftInstructionSelectorTest,
       ChangeFloat64ToInt32OfChangeFloat32ToFloat64) {
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Float32());
    m.Return(m.Emit(TSUnop::kReversibleFloat64ToInt32,
                    m.Emit(TSUnop::kChangeFloat32ToFloat64, m.Parameter(0))));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kRiscvTruncWS, s[0]->arch_opcode());
    EXPECT_EQ(kMode_None, s[0]->addressing_mode());
    ASSERT_EQ(1U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_F(TurboshaftInstructionSelectorTest,
       TruncateFloat64ToFloat32OfChangeInt32ToFloat64) {
  {
    StreamBuilder m(this, MachineType::Float32(), MachineType::Int32());
    m.Return(m.Emit(TSUnop::kTruncateFloat64ToFloat32,
                    m.Emit(TSUnop::kChangeInt32ToFloat64, m.Parameter(0))));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kRiscvCvtSW, s[0]->arch_opcode());
    EXPECT_EQ(kMode_None, s[0]->addressing_mode());
    ASSERT_EQ(1U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

// ----------------------------------------------------------------------------
// Loads and stores.
// ----------------------------------------------------------------------------

namespace {

struct MemoryAccess {
  MachineType type;
  ArchOpcode load_opcode;
  ArchOpcode store_opcode;
};

static const MemoryAccess kMemoryAccesses[] = {
    {MachineType::Int8(), kRiscvLb, kRiscvSb},
    {MachineType::Uint8(), kRiscvLbu, kRiscvSb},
    {MachineType::Int16(), kRiscvLh, kRiscvSh},
    {MachineType::Uint16(), kRiscvLhu, kRiscvSh},
    {MachineType::Int32(), kRiscvLw, kRiscvSw},
    {MachineType::Float32(), kRiscvLoadFloat, kRiscvStoreFloat},
    {MachineType::Float64(), kRiscvLoadDouble, kRiscvStoreDouble}};

struct MemoryAccessImm {
  MachineType type;
  ArchOpcode load_opcode;
  ArchOpcode store_opcode;
  bool (TurboshaftInstructionSelectorTest::Stream::*val_predicate)(
      const InstructionOperand*) const;
  const int32_t immediates[40];
};

std::ostream& operator<<(std::ostream& os, const MemoryAccessImm& acc) {
  return os << acc.type;
}

struct MemoryAccessImm1 {
  MachineType type;
  ArchOpcode load_opcode;
  ArchOpcode store_opcode;
  bool (TurboshaftInstructionSelectorTest::Stream::*val_predicate)(
      const InstructionOperand*) const;
  const int32_t immediates[5];
};

std::ostream& operator<<(std::ostream& os, const MemoryAccessImm1& acc) {
  return os << acc.type;
}

// ----------------------------------------------------------------------------
// Loads and stores immediate values
// ----------------------------------------------------------------------------

const MemoryAccessImm kMemoryAccessesImm[] = {
    {MachineType::Int8(),
     kRiscvLb,
     kRiscvSb,
     &TurboshaftInstructionSelectorTest::Stream::IsInteger,
     {-4095, -3340, -3231, -3224, -3088, -1758, -1203, -123, -117, -91,
      -89,   -87,   -86,   -82,   -44,   -23,   -3,    0,    7,    10,
      39,    52,    69,    71,    91,    92,    107,   109,  115,  124,
      286,   655,   1362,  1569,  2587,  3067,  3096,  3462, 3510, 4095}},
    {MachineType::Uint8(),
     kRiscvLbu,
     kRiscvSb,
     &TurboshaftInstructionSelectorTest::Stream::IsInteger,
     {-4095, -3340, -3231, -3224, -3088, -1758, -1203, -123, -117, -91,
      -89,   -87,   -86,   -82,   -44,   -23,   -3,    0,    7,    10,
      39,    52,    69,    71,    91,    92,    107,   109,  115,  124,
      286,   655,   1362,  1569,  2587,  3067,  3096,  3462, 3510, 4095}},
    {MachineType::Int16(),
     kRiscvLh,
     kRiscvSh,
     &TurboshaftInstructionSelectorTest::Stream::IsInteger,
     {-4095, -3340, -3231, -3224, -3088, -1758, -1203, -123, -117, -91,
      -89,   -87,   -86,   -82,   -44,   -23,   -3,    0,    7,    10,
      39,    52,    69,    71,    91,    92,    107,   109,  115,  124,
      286,   655,   1362,  1569,  2587,  3067,  3096,  3462, 3510, 4095}},
    {MachineType::Uint16(),
     kRiscvLhu,
     kRiscvSh,
     &TurboshaftInstructionSelectorTest::Stream::IsInteger,
     {-4095, -3340, -3231, -3224, -3088, -1758, -1203, -123, -117, -91,
      -89,   -87,   -86,   -82,   -44,   -23,   -3,    0,    7,    10,
      39,    52,    69,    71,    91,    92,    107,   109,  115,  124,
      286,   655,   1362,  1569,  2587,  3067,  3096,  3462, 3510, 4095}},
    {MachineType::Int32(),
     kRiscvLw,
     kRiscvSw,
     &TurboshaftInstructionSelectorTest::Stream::IsInteger,
     {-4095, -3340, -3231, -3224, -3088, -1758, -1203, -123, -117, -91,
      -89,   -87,   -86,   -82,   -44,   -23,   -3,    0,    7,    10,
      39,    52,    69,    71,    91,    92,    107,   109,  115,  124,
      286,   655,   1362,  1569,  2587,  3067,  3096,  3462, 3510, 4095}},
    {MachineType::Float32(),
     kRiscvLoadFloat,
     kRiscvStoreFloat,
     &TurboshaftInstructionSelectorTest::Stream::IsDouble,
     {-4095, -3340, -3231, -3224, -3088, -1758, -1203, -123, -117, -91,
      -89,   -87,   -86,   -82,   -44,   -23,   -3,    0,    7,    10,
      39,    52,    69,    71,    91,    92,    107,   109,  115,  124,
      286,   655,   1362,  1569,  2587,  3067,  3096,  3462, 3510, 4095}},
    {MachineType::Float64(),
     kRiscvLoadDouble,
     kRiscvStoreDouble,
     &TurboshaftInstructionSelectorTest::Stream::IsDouble,
     {-4095, -3340, -3231, -3224, -3088, -1758, -1203, -123, -117, -91,
      -89,   -87,   -86,   -82,   -44,   -23,   -3,    0,    7,    10,
      39,    52,    69,    71,    91,    92,    107,   109,  115,  124,
      286,   655,   1362,  1569,  2587,  3067,  3096,  3462, 3510, 4095}}};

const MemoryAccessImm1 kMemoryAccessImmMoreThan16bit[] = {
    {MachineType::Int8(),
     kRiscvLb,
     kRiscvSb,
     &TurboshaftInstructionSelectorTest::Stream::IsInteger,
     {-65000, -55000, 32777, 55000, 65000}},
    {MachineType::Uint8(),
     kRiscvLbu,
     kRiscvSb,
     &TurboshaftInstructionSelectorTest::Stream::IsInteger,
     {-65000, -55000, 32777, 55000, 65000}},
    {MachineType::Int16(),
     kRiscvLh,
     kRiscvSh,
     &TurboshaftInstructionSelectorTest::Stream::IsInteger,
     {-65000, -55000, 32777, 55000, 65000}},
    {MachineType::Uint16(),
     kRiscvLhu,
     kRiscvSh,
     &TurboshaftInstructionSelectorTest::Stream::IsInteger,
     {-65000, -55000, 32777, 55000, 65000}},
    {MachineType::Int32(),
     kRiscvLw,
     kRiscvSw,
     &TurboshaftInstructionSelectorTest::Stream::IsInteger,
     {-65000, -55000, 32777, 55000, 65000}},
    {MachineType::Float32(),
     kRiscvLoadFloat,
     kRiscvStoreFloat,
     &TurboshaftInstructionSelectorTest::Stream::IsDouble,
     {-65000, -55000, 32777, 55000, 65000}},
    {MachineType::Float64(),
     kRiscvLoadDouble,
     kRiscvStoreDouble,
     &TurboshaftInstructionSelectorTest::Stream::IsDouble,
     {-65000, -55000, 32777, 55000, 65000}}};

#ifdef RISCV_HAS_NO_UNALIGNED
struct MemoryAccessImm2 {
  MachineType type;
  ArchOpcode store_opcode;
  ArchOpcode store_opcode_unaligned;
  bool (TurboshaftInstructionSelectorTest::Stream::*val_predicate)(
      const InstructionOperand*) const;
  const int32_t immediates[40];
};

std::ostream& operator<<(std::ostream& os, const MemoryAccessImm2& acc) {
  return os << acc.type;
}

const MemoryAccessImm2 kMemoryAccessesImmUnaligned[] = {
    {MachineType::Int16(),
     kRiscvUsh,
     kRiscvSh,
     &TurboshaftInstructionSelectorTest::Stream::IsInteger,
     {-4095, -3340, -3231, -3224, -3088, -1758, -1203, -123, -117, -91,
      -89,   -87,   -86,   -82,   -44,   -23,   -3,    0,    7,    10,
      39,    52,    69,    71,    91,    92,    107,   109,  115,  124,
      286,   655,   1362,  1569,  2587,  3067,  3096,  3462, 3510, 4095}},
    {MachineType::Int32(),
     kRiscvUsw,
     kRiscvSw,
     &TurboshaftInstructionSelectorTest::Stream::IsInteger,
     {-4095, -3340, -3231, -3224, -3088, -1758, -1203, -123, -117, -91,
      -89,   -87,   -86,   -82,   -44,   -23,   -3,    0,    7,    10,
      39,    52,    69,    71,    91,    92,    107,   109,  115,  124,
      286,   655,   1362,  1569,  2587,  3067,  3096,  3462, 3510, 4095}},
    {MachineType::Int64(),
     kRiscvUsd,
     kRiscvSd,
     &TurboshaftInstructionSelectorTest::Stream::IsInteger,
     {-4095, -3340, -3231, -3224, -3088, -1758, -1203, -123, -117, -91,
      -89,   -87,   -86,   -82,   -44,   -23,   -3,    0,    7,    10,
      39,    52,    69,    71,    91,    92,    107,   109,  115,  124,
      286,   655,   1362,  1569,  2587,  3067,  3096,  3462, 3510, 4095}},
    {MachineType::Float32(),
     kRiscvUStoreFloat,
     kRiscvStoreFloat,
     &TurboshaftInstructionSelectorTest::Stream::IsDouble,
     {-4095, -3340, -3231, -3224, -3088, -1758, -1203, -123, -117, -91,
      -89,   -87,   -86,   -82,   -44,   -23,   -3,    0,    7,    10,
      39,    52,    69,    71,    91,    92,    107,   109,  115,  124,
      286,   655,   1362,  1569,  2587,  3067,  3096,  3462, 3510, 4095}},
    {MachineType::Float64(),
     kRiscvUStoreDouble,
     kRiscvStoreDouble,
     &TurboshaftInstructionSelectorTest::Stream::IsDouble,
     {-4095, -3340, -3231, -3224, -3088, -1758, -1203, -123, -117, -91,
      -89,   -87,   -86,   -82,   -44,   -23,   -3,    0,    7,    10,
      39,    52,    69,    71,    91,    92,    107,   109,  115,  124,
      286,   655,   1362,  1569,  2587,  3067,  3096,  3462, 3510, 4095}}};
#endif
}  // namespace

using TurboshaftInstructionSelectorMemoryAccessTest =
    TurboshaftInstructionSelectorTestWithParam<MemoryAccess>;

TEST_P(TurboshaftInstructionSelectorMemoryAccessTest, LoadWithParameters) {
  const MemoryAccess memacc = GetParam();
  StreamBuilder m(this, memacc.type, MachineType::Pointer(),
                  MachineType::Int32());
  m.Return(m.Load(memacc.type, m.Parameter(0)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(memacc.load_opcode, s[0]->arch_opcode());
  EXPECT_EQ(kMode_MRI, s[0]->addressing_mode());
}

TEST_P(TurboshaftInstructionSelectorMemoryAccessTest, StoreWithParameters) {
  const MemoryAccess memacc = GetParam();
  StreamBuilder m(this, MachineType::Int32(), MachineType::Pointer(),
                  memacc.type, memacc.type);
  m.Store(memacc.type.representation(), m.Parameter(0), m.Int32Constant(0),
          m.Parameter(1), kNoWriteBarrier);
  m.Return(m.Int32Constant(0));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(memacc.store_opcode, s[0]->arch_opcode());
  EXPECT_EQ(kMode_MRI, s[0]->addressing_mode());
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorMemoryAccessTest,
                         ::testing::ValuesIn(kMemoryAccesses));

// ----------------------------------------------------------------------------
// Load immediate.
// ----------------------------------------------------------------------------

using TurboshaftInstructionSelectorMemoryAccessImmTest =
    TurboshaftInstructionSelectorTestWithParam<MemoryAccessImm>;

TEST_P(TurboshaftInstructionSelectorMemoryAccessImmTest,
       LoadWithImmediateIndex) {
  const MemoryAccessImm memacc = GetParam();
  TRACED_FOREACH(int32_t, index, memacc.immediates) {
    StreamBuilder m(this, memacc.type, MachineType::Pointer());
    m.Return(m.Load(memacc.type, m.Parameter(0), m.Int32Constant(index)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(memacc.load_opcode, s[0]->arch_opcode());
    EXPECT_EQ(kMode_MRI, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    ASSERT_EQ(InstructionOperand::IMMEDIATE, s[0]->InputAt(1)->kind());
    EXPECT_EQ(index, s.ToInt32(s[0]->InputAt(1)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_TRUE((s.*memacc.val_predicate)(s[0]->Output()));
  }
}

// ----------------------------------------------------------------------------
// Store immediate.
// ----------------------------------------------------------------------------

TEST_P(TurboshaftInstructionSelectorMemoryAccessImmTest,
       StoreWithImmediateIndex) {
  const MemoryAccessImm memacc = GetParam();
  TRACED_FOREACH(int32_t, index, memacc.immediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Pointer(),
                    memacc.type);
    m.Store(memacc.type.representation(), m.Parameter(0),
            m.Int32Constant(index), m.Parameter(1), kNoWriteBarrier);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(memacc.store_opcode, s[0]->arch_opcode());
    EXPECT_EQ(kMode_MRI, s[0]->addressing_mode());
    ASSERT_EQ(3U, s[0]->InputCount());
    ASSERT_EQ(InstructionOperand::IMMEDIATE, s[0]->InputAt(2)->kind());
    EXPECT_EQ(index, s.ToInt32(s[0]->InputAt(2)));
    EXPECT_EQ(0U, s[0]->OutputCount());
  }
}

TEST_P(TurboshaftInstructionSelectorMemoryAccessImmTest, StoreZero) {
  const MemoryAccessImm memacc = GetParam();
  TRACED_FOREACH(int32_t, index, memacc.immediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Pointer());
    OpIndex zero;
    if (memacc.type.representation() >= MachineRepresentation::kWord8 &&
        memacc.type.representation() <= MachineRepresentation::kWord64) {
      zero = m.WordConstant(
          0, memacc.type.representation() <= MachineRepresentation::kWord32
                 ? WordRepresentation::Word32()
                 : WordRepresentation::Word64());
    } else {
      zero = m.FloatConstant(
          0, memacc.type.representation() == MachineRepresentation::kFloat32
                 ? FloatRepresentation::Float32()
                 : FloatRepresentation::Float64());
    }
    m.Store(memacc.type.representation(), m.Parameter(0),
            m.Int32Constant(index), zero, kNoWriteBarrier);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(memacc.store_opcode, s[0]->arch_opcode());
    EXPECT_EQ(kMode_MRI, s[0]->addressing_mode());
    ASSERT_EQ(3U, s[0]->InputCount());
    ASSERT_EQ(InstructionOperand::IMMEDIATE, s[0]->InputAt(2)->kind());
    EXPECT_EQ(index, s.ToInt32(s[0]->InputAt(2)));
    ASSERT_EQ(InstructionOperand::IMMEDIATE, s[0]->InputAt(0)->kind());
    EXPECT_EQ(0,
              memacc.type.representation() < MachineRepresentation::kFloat32
                  ? s.ToInt64(s[0]->InputAt(0))
              : memacc.type.representation() == MachineRepresentation::kFloat32
                  ? s.ToFloat32(s[0]->InputAt(0))
                  : s.ToFloat64(s[0]->InputAt(0)));
    EXPECT_EQ(0U, s[0]->OutputCount());
  }
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorMemoryAccessImmTest,
                         ::testing::ValuesIn(kMemoryAccessesImm));

#ifdef RISCV_HAS_NO_UNALIGNED
using TurboshaftInstructionSelectorMemoryAccessUnalignedImmTest =
    TurboshaftInstructionSelectorTestWithParam<MemoryAccessImm2>;

TEST_P(TurboshaftInstructionSelectorMemoryAccessUnalignedImmTest, StoreZero) {
  const MemoryAccessImm2 memacc = GetParam();
  TRACED_FOREACH(int32_t, index, memacc.immediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Pointer());
    bool unaligned_store_supported =
        m.machine()->UnalignedStoreSupported(memacc.type.representation());
    m.UnalignedStore(memacc.type.representation(), m.Parameter(0),
                     m.Int32Constant(index), m.Int32Constant(0));
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    uint32_t i = is_int12(index) ? 0 : 1;
    ASSERT_EQ(i + 1, s.size());
    EXPECT_EQ(unaligned_store_supported ? memacc.store_opcode_unaligned
                                        : memacc.store_opcode,
              s[i]->arch_opcode());
    EXPECT_EQ(kMode_MRI, s[i]->addressing_mode());
    ASSERT_EQ(3U, s[i]->InputCount());
    ASSERT_EQ(InstructionOperand::IMMEDIATE, s[i]->InputAt(1)->kind());
    EXPECT_EQ(i == 0 ? index : 0, s.ToInt32(s[i]->InputAt(1)));
    ASSERT_EQ(InstructionOperand::IMMEDIATE, s[i]->InputAt(2)->kind());
    EXPECT_EQ(0, s.ToInt64(s[i]->InputAt(2)));
    EXPECT_EQ(0U, s[i]->OutputCount());
  }
}

INSTANTIATE_TEST_SUITE_P(
    TurboshaftInstructionSelectorTest,
    TurboshaftInstructionSelectorMemoryAccessUnalignedImmTest,
    ::testing::ValuesIn(kMemoryAccessesImmUnaligned));
#endif
// ----------------------------------------------------------------------------
// Load/store offsets more than 16 bits.
// ----------------------------------------------------------------------------

using TurboshaftInstructionSelectorMemoryAccessImmMoreThan16bitTest =
    TurboshaftInstructionSelectorTestWithParam<MemoryAccessImm1>;

TEST_P(TurboshaftInstructionSelectorMemoryAccessImmMoreThan16bitTest,
       LoadWithImmediateIndex) {
  const MemoryAccessImm1 memacc = GetParam();
  TRACED_FOREACH(int32_t, index, memacc.immediates) {
    StreamBuilder m(this, memacc.type, MachineType::Pointer());
    m.Return(m.Load(memacc.type, m.Parameter(0), m.Int32Constant(index)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(memacc.load_opcode, s[0]->arch_opcode());
    EXPECT_EQ(kMode_MRI, s[0]->addressing_mode());
    EXPECT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_P(TurboshaftInstructionSelectorMemoryAccessImmMoreThan16bitTest,
       StoreWithImmediateIndex) {
  const MemoryAccessImm1 memacc = GetParam();
  TRACED_FOREACH(int32_t, index, memacc.immediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Pointer(),
                    memacc.type);
    m.Store(memacc.type.representation(), m.Parameter(0),
            m.Int32Constant(index), m.Parameter(1), kNoWriteBarrier);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(memacc.store_opcode, s[0]->arch_opcode());
    EXPECT_EQ(kMode_MRI, s[0]->addressing_mode());
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(0U, s[0]->OutputCount());
  }
}

INSTANTIATE_TEST_SUITE_P(
    TurboshaftInstructionSelectorTest,
    TurboshaftInstructionSelectorMemoryAccessImmMoreThan16bitTest,
    ::testing::ValuesIn(kMemoryAccessImmMoreThan16bit));

// ----------------------------------------------------------------------------
// kRiscvCmp with zero testing.
// ----------------------------------------------------------------------------

TEST_F(TurboshaftInstructionSelectorTest, Word32EqualWithZero) {
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(m.Emit(TSBinop::kWord32Equal, m.Parameter(0), m.Int32Constant(0)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kRiscvCmpZero, s[0]->arch_opcode());
    EXPECT_EQ(kMode_None, s[0]->addressing_mode());
    ASSERT_EQ(1U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kEqual, s[0]->flags_condition());
  }
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(m.Emit(TSBinop::kWord32Equal, m.Int32Constant(0), m.Parameter(0)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kRiscvCmpZero, s[0]->arch_opcode());
    EXPECT_EQ(kMode_None, s[0]->addressing_mode());
    ASSERT_EQ(1U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kEqual, s[0]->flags_condition());
  }
}

// TEST_F(TurboshaftInstructionSelectorTest, Word32Clz) {
//   StreamBuilder m(this, MachineType::Uint32(), MachineType::Uint32());
//   Node* const p0 = m.Parameter(0);
//   Node* const n = m.Word32Clz(p0);
//   m.Return(n);
//   Stream s = m.Build();
//   ASSERT_EQ(1U, s.size());
//   EXPECT_EQ(kRiscvClz32, s[0]->arch_opcode());
//   ASSERT_EQ(1U, s[0]->InputCount());
//   EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
//   ASSERT_EQ(1U, s[0]->OutputCount());
//   EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
// }

// TEST_F(TurboshaftInstructionSelectorTest, Float32Abs) {
//   StreamBuilder m(this, MachineType::Float32(), MachineType::Float32());
//   Node* const p0 = m.Parameter(0);
//   Node* const n = m.Float32Abs(p0);
//   m.Return(n);
//   Stream s = m.Build();
//   ASSERT_EQ(1U, s.size());
//   EXPECT_EQ(kRiscvAbsS, s[0]->arch_opcode());
//   ASSERT_EQ(1U, s[0]->InputCount());
//   EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
//   ASSERT_EQ(1U, s[0]->OutputCount());
//   EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
// }

// TEST_F(TurboshaftInstructionSelectorTest, Float64Abs) {
//   StreamBuilder m(this, MachineType::Float64(), MachineType::Float64());
//   Node* const p0 = m.Parameter(0);
//   Node* const n = m.Float64Abs(p0);
//   m.Return(n);
//   Stream s = m.Build();
//   ASSERT_EQ(1U, s.size());
//   EXPECT_EQ(kRiscvAbsD, s[0]->arch_opcode());
//   ASSERT_EQ(1U, s[0]->InputCount());
//   EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
//   ASSERT_EQ(1U, s[0]->OutputCount());
//   EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
// }

// TEST_F(TurboshaftInstructionSelectorTest, Float64Max) {
//   StreamBuilder m(this, MachineType::Float64(), MachineType::Float64(),
//                   MachineType::Float64());
//   Node* const p0 = m.Parameter(0);
//   Node* const p1 = m.Parameter(1);
//   Node* const n = m.Float64Max(p0, p1);
//   m.Return(n);
//   Stream s = m.Build();
//   ASSERT_EQ(1U, s.size());
//   EXPECT_EQ(kRiscvFloat64Max, s[0]->arch_opcode());
//   ASSERT_EQ(2U, s[0]->InputCount());
//   ASSERT_EQ(1U, s[0]->OutputCount());
//   EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
// }

// TEST_F(TurboshaftInstructionSelectorTest, Float64Min) {
//   StreamBuilder m(this, MachineType::Float64(), MachineType::Float64(),
//                   MachineType::Float64());
//   Node* const p0 = m.Parameter(0);
//   Node* const p1 = m.Parameter(1);
//   Node* const n = m.Float64Min(p0, p1);
//   m.Return(n);
//   Stream s = m.Build();
//   ASSERT_EQ(1U, s.size());
//   EXPECT_EQ(kRiscvFloat64Min, s[0]->arch_opcode());
//   ASSERT_EQ(2U, s[0]->InputCount());
//   ASSERT_EQ(1U, s[0]->OutputCount());
//   EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
// }

TEST_F(TurboshaftInstructionSelectorTest, Word32ReverseBytes) {
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(m.Word32ReverseBytes(m.Parameter(0)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    // EXPECT_EQ(kRiscvByteSwap32, s[0]->arch_opcode());
    EXPECT_EQ(1U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, ExternalReferenceLoad1) {
  // Test offsets we can use kMode_Root for.
  const int32_t kOffsets[] = {0, 1, 4, INT32_MIN, INT32_MAX};
  TRACED_FOREACH(int64_t, offset, kOffsets) {
    StreamBuilder m(this, MachineType::Int32());
    ExternalReference reference = base::bit_cast<ExternalReference>(
        (int32_t)(isolate()->isolate_root() + offset));
    auto value = m.Load(MachineType::Int32(), m.ExternalConstant(reference));
    m.Return(value);

    Stream s = m.Build();

    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kRiscvLw, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Root, s[0]->addressing_mode());
    EXPECT_EQ(1U, s[0]->InputCount());
    EXPECT_EQ(s.ToInt64(s[0]->InputAt(0)), offset);
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

}  // namespace turboshaft
}  // namespace compiler
}  // namespace internal
}  // namespace v8
