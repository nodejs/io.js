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

namespace v8::internal::compiler::turboshaft {

template <typename Op>
struct MachInst {
  Op op;
  const char* constructor_name;
  ArchOpcode arch_opcode;
  MachineType machine_type;
};

using MachInst1 = MachInst<TSUnop>;
using MachInst2 = MachInst<TSBinop>;

template <typename T>
std::ostream& operator<<(std::ostream& os, const MachInst<T>& mi) {
  return os << mi.constructor_name;
}

struct Shift {
  MachInst2 mi;
  AddressingMode mode;
};

std::ostream& operator<<(std::ostream& os, const Shift& shift) {
  return os << shift.mi;
}

// Helper to build Int32Constant or Int64Constant depending on the given
// machine type.
OpIndex BuildConstant(TurboshaftInstructionSelectorTest::StreamBuilder* m,
                      MachineType type, int64_t value) {
  switch (type.representation()) {
    case MachineRepresentation::kWord32:
      return m->Int32Constant(static_cast<int32_t>(value));

    case MachineRepresentation::kWord64:
      return m->Int64Constant(value);

    default:
      UNIMPLEMENTED();
  }
}

// ARM64 logical instructions.
const MachInst2 kLogicalInstructions[] = {
    {TSBinop::kWord32BitwiseAnd, "Word32BitwiseAnd", kArm64And32,
     MachineType::Int32()},
    {TSBinop::kWord64BitwiseAnd, "Word64BitwiseAnd", kArm64And,
     MachineType::Int64()},
    {TSBinop::kWord32BitwiseOr, "Word32BitwiseOr", kArm64Or32,
     MachineType::Int32()},
    {TSBinop::kWord64BitwiseOr, "Word64BitwiseOr", kArm64Or,
     MachineType::Int64()},
    {TSBinop::kWord32BitwiseXor, "Word32BitwiseXor", kArm64Eor32,
     MachineType::Int32()},
    {TSBinop::kWord64BitwiseXor, "Word64BitwiseXor", kArm64Eor,
     MachineType::Int64()}};

// ARM64 logical immediates: contiguous set bits, rotated about a power of two
// sized block. The block is then duplicated across the word. Below is a random
// subset of the 32-bit immediates.
const uint32_t kLogical32Immediates[] = {
    0x00000002, 0x00000003, 0x00000070, 0x00000080, 0x00000100, 0x000001C0,
    0x00000300, 0x000007E0, 0x00003FFC, 0x00007FC0, 0x0003C000, 0x0003F000,
    0x0003FFC0, 0x0003FFF8, 0x0007FF00, 0x0007FFE0, 0x000E0000, 0x001E0000,
    0x001FFFFC, 0x003F0000, 0x003F8000, 0x00780000, 0x007FC000, 0x00FF0000,
    0x01800000, 0x01800180, 0x01F801F8, 0x03FE0000, 0x03FFFFC0, 0x03FFFFFC,
    0x06000000, 0x07FC0000, 0x07FFC000, 0x07FFFFC0, 0x07FFFFE0, 0x0FFE0FFE,
    0x0FFFF800, 0x0FFFFFF0, 0x0FFFFFFF, 0x18001800, 0x1F001F00, 0x1F801F80,
    0x30303030, 0x3FF03FF0, 0x3FF83FF8, 0x3FFF0000, 0x3FFF8000, 0x3FFFFFC0,
    0x70007000, 0x7F7F7F7F, 0x7FC00000, 0x7FFFFFC0, 0x8000001F, 0x800001FF,
    0x81818181, 0x9FFF9FFF, 0xC00007FF, 0xC0FFFFFF, 0xDDDDDDDD, 0xE00001FF,
    0xE00003FF, 0xE007FFFF, 0xEFFFEFFF, 0xF000003F, 0xF001F001, 0xF3FFF3FF,
    0xF800001F, 0xF80FFFFF, 0xF87FF87F, 0xFBFBFBFB, 0xFC00001F, 0xFC0000FF,
    0xFC0001FF, 0xFC03FC03, 0xFE0001FF, 0xFF000001, 0xFF03FF03, 0xFF800000,
    0xFF800FFF, 0xFF801FFF, 0xFF87FFFF, 0xFFC0003F, 0xFFC007FF, 0xFFCFFFCF,
    0xFFE00003, 0xFFE1FFFF, 0xFFF0001F, 0xFFF07FFF, 0xFFF80007, 0xFFF87FFF,
    0xFFFC00FF, 0xFFFE07FF, 0xFFFF00FF, 0xFFFFC001, 0xFFFFF007, 0xFFFFF3FF,
    0xFFFFF807, 0xFFFFF9FF, 0xFFFFFC0F, 0xFFFFFEFF};

// Random subset of 64-bit logical immediates.
const uint64_t kLogical64Immediates[] = {
    0x0000000000000001, 0x0000000000000002, 0x0000000000000003,
    0x0000000000000070, 0x0000000000000080, 0x0000000000000100,
    0x00000000000001C0, 0x0000000000000300, 0x0000000000000600,
    0x00000000000007E0, 0x0000000000003FFC, 0x0000000000007FC0,
    0x0000000600000000, 0x0000003FFFFFFFFC, 0x000000F000000000,
    0x000001F800000000, 0x0003FC0000000000, 0x0003FC000003FC00,
    0x0003FFFFFFC00000, 0x0003FFFFFFFFFFC0, 0x0006000000060000,
    0x003FFFFFFFFC0000, 0x0180018001800180, 0x01F801F801F801F8,
    0x0600000000000000, 0x1000000010000000, 0x1000100010001000,
    0x1010101010101010, 0x1111111111111111, 0x1F001F001F001F00,
    0x1F1F1F1F1F1F1F1F, 0x1FFFFFFFFFFFFFFE, 0x3FFC3FFC3FFC3FFC,
    0x5555555555555555, 0x7F7F7F7F7F7F7F7F, 0x8000000000000000,
    0x8000001F8000001F, 0x8181818181818181, 0x9999999999999999,
    0x9FFF9FFF9FFF9FFF, 0xAAAAAAAAAAAAAAAA, 0xDDDDDDDDDDDDDDDD,
    0xE0000000000001FF, 0xF800000000000000, 0xF8000000000001FF,
    0xF807F807F807F807, 0xFEFEFEFEFEFEFEFE, 0xFFFEFFFEFFFEFFFE,
    0xFFFFF807FFFFF807, 0xFFFFF9FFFFFFF9FF, 0xFFFFFC0FFFFFFC0F,
    0xFFFFFC0FFFFFFFFF, 0xFFFFFEFFFFFFFEFF, 0xFFFFFEFFFFFFFFFF,
    0xFFFFFF8000000000, 0xFFFFFFFEFFFFFFFE, 0xFFFFFFFFEFFFFFFF,
    0xFFFFFFFFF9FFFFFF, 0xFFFFFFFFFF800000, 0xFFFFFFFFFFFFC0FF,
    0xFFFFFFFFFFFFFFFE};

// ARM64 arithmetic instructions.
struct AddSub {
  MachInst2 mi;
  ArchOpcode negate_arch_opcode;
};

std::ostream& operator<<(std::ostream& os, const AddSub& op) {
  return os << op.mi;
}

const AddSub kAddSubInstructions[] = {
    {{TSBinop::kWord32Add, "Word32Add", kArm64Add32, MachineType::Int32()},
     kArm64Sub32},
    {{TSBinop::kWord64Add, "Word64Add", kArm64Add, MachineType::Int64()},
     kArm64Sub},
    {{TSBinop::kWord32Sub, "Int32Sub", kArm64Sub32, MachineType::Int32()},
     kArm64Add32},
    {{TSBinop::kWord64Sub, "Word64Sub", kArm64Sub, MachineType::Int64()},
     kArm64Add}};

// ARM64 Add/Sub immediates: 12-bit immediate optionally shifted by 12.
// Below is a combination of a random subset and some edge values.
const int32_t kAddSubImmediates[] = {
    0,        1,        69,       493,      599,      701,      719,
    768,      818,      842,      945,      1246,     1286,     1429,
    1669,     2171,     2179,     2182,     2254,     2334,     2338,
    2343,     2396,     2449,     2610,     2732,     2855,     2876,
    2944,     3377,     3458,     3475,     3476,     3540,     3574,
    3601,     3813,     3871,     3917,     4095,     4096,     16384,
    364544,   462848,   970752,   1523712,  1863680,  2363392,  3219456,
    3280896,  4247552,  4526080,  4575232,  4960256,  5505024,  5894144,
    6004736,  6193152,  6385664,  6795264,  7114752,  7233536,  7348224,
    7499776,  7573504,  7729152,  8634368,  8937472,  9465856,  10354688,
    10682368, 11059200, 11460608, 13168640, 13176832, 14336000, 15028224,
    15597568, 15892480, 16773120};

// ARM64 flag setting data processing instructions.
const MachInst2 kDPFlagSetInstructions[] = {
    {TSBinop::kWord32BitwiseAnd, "Word32BitwiseAnd", kArm64Tst32,
     MachineType::Int32()},
    {TSBinop::kWord32Add, "Word32Add", kArm64Cmn32, MachineType::Int32()},
    {TSBinop::kWord32Sub, "Int32Sub", kArm64Cmp32, MachineType::Int32()},
    {TSBinop::kWord64BitwiseAnd, "Word64BitwiseAnd", kArm64Tst,
     MachineType::Int64()}};

// ARM64 arithmetic with overflow instructions.
const MachInst2 kOvfAddSubInstructions[] = {
    {TSBinop::kInt32AddCheckOverflow, "Int32AddWithOverflow", kArm64Add32,
     MachineType::Int32()},
    {TSBinop::kInt32SubCheckOverflow, "Int32SubWithOverflow", kArm64Sub32,
     MachineType::Int32()},
    {TSBinop::kInt64AddCheckOverflow, "Int64AddWithOverflow", kArm64Add,
     MachineType::Int64()},
    {TSBinop::kInt64SubCheckOverflow, "Int64SubWithOverflow", kArm64Sub,
     MachineType::Int64()}};

// ARM64 shift instructions.
const Shift kShiftInstructions[] = {
    {{TSBinop::kWord32ShiftLeft, "Word32ShiftLeft", kArm64Lsl32,
      MachineType::Int32()},
     kMode_Operand2_R_LSL_I},
    {{TSBinop::kWord64ShiftLeft, "Word64ShiftLeft", kArm64Lsl,
      MachineType::Int64()},
     kMode_Operand2_R_LSL_I},
    {{TSBinop::kWord32ShiftRightLogical, "Word32ShiftRightLogical", kArm64Lsr32,
      MachineType::Int32()},
     kMode_Operand2_R_LSR_I},
    {{TSBinop::kWord64ShiftRightLogical, "Word64ShiftRightLogical", kArm64Lsr,
      MachineType::Int64()},
     kMode_Operand2_R_LSR_I},
    {{TSBinop::kWord32ShiftRightArithmetic, "Word32ShiftRightArithmetic",
      kArm64Asr32, MachineType::Int32()},
     kMode_Operand2_R_ASR_I},
    {{TSBinop::kWord64ShiftRightArithmetic, "Word64ShiftRightArithmetic",
      kArm64Asr, MachineType::Int64()},
     kMode_Operand2_R_ASR_I},
    {{TSBinop::kWord32RotateRight, "Word32Ror", kArm64Ror32,
      MachineType::Int32()},
     kMode_Operand2_R_ROR_I},
    {{TSBinop::kWord64RotateRight, "Word64Ror", kArm64Ror,
      MachineType::Int64()},
     kMode_Operand2_R_ROR_I}};

// ARM64 Mul/Div instructions.
const MachInst2 kMulDivInstructions[] = {
    {TSBinop::kWord32Mul, "Word32Mul", kArm64Mul32, MachineType::Int32()},
    {TSBinop::kWord64Mul, "Word64Mul", kArm64Mul, MachineType::Int64()},
    {TSBinop::kInt32Div, "Int32Div", kArm64Idiv32, MachineType::Int32()},
    {TSBinop::kInt64Div, "Int64Div", kArm64Idiv, MachineType::Int64()},
    {TSBinop::kUint32Div, "Uint32Div", kArm64Udiv32, MachineType::Int32()},
    {TSBinop::kUint64Div, "Uint64Div", kArm64Udiv, MachineType::Int64()}};

// ARM64 FP arithmetic instructions.
const MachInst2 kFPArithInstructions[] = {
    {TSBinop::kFloat64Add, "Float64Add", kArm64Float64Add,
     MachineType::Float64()},
    {TSBinop::kFloat64Sub, "Float64Sub", kArm64Float64Sub,
     MachineType::Float64()},
    {TSBinop::kFloat64Mul, "Float64Mul", kArm64Float64Mul,
     MachineType::Float64()},
    {TSBinop::kFloat64Div, "Float64Div", kArm64Float64Div,
     MachineType::Float64()}};

struct FPCmp {
  MachInst2 mi;
  FlagsCondition cond;
  FlagsCondition commuted_cond;
};

std::ostream& operator<<(std::ostream& os, const FPCmp& cmp) {
  return os << cmp.mi;
}

// ARM64 FP comparison instructions.
const FPCmp kFPCmpInstructions[] = {
    {{TSBinop::kFloat64Equal, "Float64Equal", kArm64Float64Cmp,
      MachineType::Float64()},
     kEqual,
     kEqual},
    {{TSBinop::kFloat64LessThan, "Float64LessThan", kArm64Float64Cmp,
      MachineType::Float64()},
     kFloatLessThan,
     kFloatGreaterThan},
    {{TSBinop::kFloat64LessThanOrEqual, "Float64LessThanOrEqual",
      kArm64Float64Cmp, MachineType::Float64()},
     kFloatLessThanOrEqual,
     kFloatGreaterThanOrEqual},
    {{TSBinop::kFloat32Equal, "Float32Equal", kArm64Float32Cmp,
      MachineType::Float32()},
     kEqual,
     kEqual},
    {{TSBinop::kFloat32LessThan, "Float32LessThan", kArm64Float32Cmp,
      MachineType::Float32()},
     kFloatLessThan,
     kFloatGreaterThan},
    {{TSBinop::kFloat32LessThanOrEqual, "Float32LessThanOrEqual",
      kArm64Float32Cmp, MachineType::Float32()},
     kFloatLessThanOrEqual,
     kFloatGreaterThanOrEqual}};

struct Conversion {
  // The machine_type field in MachInst1 represents the destination type.
  MachInst1 mi;
  MachineType src_machine_type;
};

std::ostream& operator<<(std::ostream& os, const Conversion& conv) {
  return os << conv.mi;
}

// ARM64 type conversion instructions.
const Conversion kConversionInstructions[] = {
    {{TSUnop::kChangeFloat32ToFloat64, "ChangeFloat32ToFloat64",
      kArm64Float32ToFloat64, MachineType::Float64()},
     MachineType::Float32()},
    {{TSUnop::kTruncateFloat64ToFloat32, "TruncateFloat64ToFloat32",
      kArm64Float64ToFloat32, MachineType::Float32()},
     MachineType::Float64()},
    {{TSUnop::kChangeInt32ToInt64, "ChangeInt32ToInt64", kArm64Sxtw,
      MachineType::Int64()},
     MachineType::Int32()},
    {{TSUnop::kChangeUint32ToUint64, "ChangeUint32ToUint64", kArm64Mov32,
      MachineType::Uint64()},
     MachineType::Uint32()},
    {{TSUnop::kTruncateWord64ToWord32, "TruncateWord64ToWord32", kArchNop,
      MachineType::Int32()},
     MachineType::Int64()},
    {{TSUnop::kChangeInt32ToFloat64, "ChangeInt32ToFloat64",
      kArm64Int32ToFloat64, MachineType::Float64()},
     MachineType::Int32()},
    {{TSUnop::kChangeUint32ToFloat64, "ChangeUint32ToFloat64",
      kArm64Uint32ToFloat64, MachineType::Float64()},
     MachineType::Uint32()},
    {{TSUnop::kReversibleFloat64ToInt32, "ReversibleFloat64ToInt32",
      kArm64Float64ToInt32, MachineType::Int32()},
     MachineType::Float64()},
    {{TSUnop::kReversibleFloat64ToUint32, "ReversibleFloat64ToUint32",
      kArm64Float64ToUint32, MachineType::Uint32()},
     MachineType::Float64()}};

// ARM64 instructions that clear the top 32 bits of the destination.
const MachInst2 kCanElideChangeUint32ToUint64[] = {
    {TSBinop::kWord32BitwiseAnd, "Word32BitwisAnd", kArm64And32,
     MachineType::Uint32()},
    {TSBinop::kWord32BitwiseOr, "Word32BitwisOr", kArm64Or32,
     MachineType::Uint32()},
    {TSBinop::kWord32BitwiseXor, "Word32BitwisXor", kArm64Eor32,
     MachineType::Uint32()},
    {TSBinop::kWord32ShiftLeft, "Word32ShiftLeft", kArm64Lsl32,
     MachineType::Uint32()},
    {TSBinop::kWord32ShiftRightLogical, "Word32ShiftRightLogical", kArm64Lsr32,
     MachineType::Uint32()},
    {TSBinop::kWord32ShiftRightArithmetic, "Word32ShiftRightArithmetic",
     kArm64Asr32, MachineType::Uint32()},
    {TSBinop::kWord32RotateRight, "Word32RotateRight", kArm64Ror32,
     MachineType::Uint32()},
    {TSBinop::kWord32Equal, "Word32Equal", kArm64Cmp32, MachineType::Uint32()},
    {TSBinop::kWord32Add, "Word32Add", kArm64Add32, MachineType::Int32()},
    {TSBinop::kWord32Sub, "Word32Sub", kArm64Sub32, MachineType::Int32()},
    {TSBinop::kWord32Mul, "Word32Mul", kArm64Mul32, MachineType::Int32()},
    {TSBinop::kInt32Div, "Int32Div", kArm64Idiv32, MachineType::Int32()},
    {TSBinop::kInt32Mod, "Int32Mod", kArm64Imod32, MachineType::Int32()},
    {TSBinop::kInt32LessThan, "Int32LessThan", kArm64Cmp32,
     MachineType::Int32()},
    {TSBinop::kInt32LessThanOrEqual, "Int32LessThanOrEqual", kArm64Cmp32,
     MachineType::Int32()},
    {TSBinop::kUint32Div, "Uint32Div", kArm64Udiv32, MachineType::Uint32()},
    {TSBinop::kUint32LessThan, "Uint32LessThan", kArm64Cmp32,
     MachineType::Uint32()},
    {TSBinop::kUint32LessThanOrEqual, "Uint32LessThanOrEqual", kArm64Cmp32,
     MachineType::Uint32()},
    {TSBinop::kUint32Mod, "Uint32Mod", kArm64Umod32, MachineType::Uint32()},
};
const MachInst2 kCanElideChangeUint32ToUint64MultiOutput[] = {
    {TSBinop::kInt32AddCheckOverflow, "Int32AddCheckOverflow", kArm64Add32,
     MachineType::Int32()},
    {TSBinop::kInt32SubCheckOverflow, "Int32SubCheckOverflow", kArm64Sub32,
     MachineType::Int32()},
};

// -----------------------------------------------------------------------------
// Logical instructions.

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

TEST_P(TurboshaftInstructionSelectorLogicalTest, Immediate) {
  const MachInst2 dpi = GetParam();
  const MachineType type = dpi.machine_type;
  if (type == MachineType::Int32()) {
    // Immediate on the right.
    TRACED_FOREACH(int32_t, imm, kLogical32Immediates) {
      StreamBuilder m(this, type, type);
      m.Return(m.Emit(dpi.op, m.Parameter(0), m.Int32Constant(imm)));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
      ASSERT_EQ(2U, s[0]->InputCount());
      EXPECT_TRUE(s[0]->InputAt(1)->IsImmediate());
      EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
      EXPECT_EQ(1U, s[0]->OutputCount());
    }

    // Immediate on the left; all logical ops should commute.
    TRACED_FOREACH(int32_t, imm, kLogical32Immediates) {
      StreamBuilder m(this, type, type);
      m.Return(m.Emit(dpi.op, m.Int32Constant(imm), m.Parameter(0)));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
      ASSERT_EQ(2U, s[0]->InputCount());
      EXPECT_TRUE(s[0]->InputAt(1)->IsImmediate());
      EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
      EXPECT_EQ(1U, s[0]->OutputCount());
    }
  } else if (type == MachineType::Int64()) {
    // Immediate on the right.
    TRACED_FOREACH(int64_t, imm, kLogical64Immediates) {
      StreamBuilder m(this, type, type);
      m.Return(m.Emit(dpi.op, m.Parameter(0), m.Int64Constant(imm)));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
      ASSERT_EQ(2U, s[0]->InputCount());
      EXPECT_TRUE(s[0]->InputAt(1)->IsImmediate());
      EXPECT_EQ(imm, s.ToInt64(s[0]->InputAt(1)));
      EXPECT_EQ(1U, s[0]->OutputCount());
    }

    // Immediate on the left; all logical ops should commute.
    TRACED_FOREACH(int64_t, imm, kLogical64Immediates) {
      StreamBuilder m(this, type, type);
      m.Return(m.Emit(dpi.op, m.Int64Constant(imm), m.Parameter(0)));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
      ASSERT_EQ(2U, s[0]->InputCount());
      EXPECT_TRUE(s[0]->InputAt(1)->IsImmediate());
      EXPECT_EQ(imm, s.ToInt64(s[0]->InputAt(1)));
      EXPECT_EQ(1U, s[0]->OutputCount());
    }
  }
}

TEST_P(TurboshaftInstructionSelectorLogicalTest, ShiftByImmediate) {
  const MachInst2 dpi = GetParam();
  const MachineType type = dpi.machine_type;
  TRACED_FOREACH(Shift, shift, kShiftInstructions) {
    // Only test 64-bit shifted operands with 64-bit instructions.
    if (shift.mi.machine_type != type) continue;

    TRACED_FORRANGE(int, imm, 0, ((type == MachineType::Int32()) ? 31 : 63)) {
      StreamBuilder m(this, type, type, type);
      m.Return(
          m.Emit(dpi.op, m.Parameter(0),
                 m.Emit(shift.mi.op, m.Parameter(1), m.Int32Constant(imm))));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
      EXPECT_EQ(shift.mode, s[0]->addressing_mode());
      EXPECT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(imm, s.ToInt64(s[0]->InputAt(2)));
      EXPECT_EQ(1U, s[0]->OutputCount());
    }

    TRACED_FORRANGE(int, imm, 0, ((type == MachineType::Int32()) ? 31 : 63)) {
      StreamBuilder m(this, type, type, type);
      m.Return(m.Emit(dpi.op,
                      m.Emit(shift.mi.op, m.Parameter(1), m.Int32Constant(imm)),
                      m.Parameter(0)));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
      EXPECT_EQ(shift.mode, s[0]->addressing_mode());
      EXPECT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(imm, s.ToInt64(s[0]->InputAt(2)));
      EXPECT_EQ(1U, s[0]->OutputCount());
    }
  }
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorLogicalTest,
                         ::testing::ValuesIn(kLogicalInstructions));

// -----------------------------------------------------------------------------
// Add and Sub instructions.

using TurboshaftInstructionSelectorAddSubTest =
    TurboshaftInstructionSelectorTestWithParam<AddSub>;

TEST_P(TurboshaftInstructionSelectorAddSubTest, Parameter) {
  const AddSub dpi = GetParam();
  const MachineType type = dpi.mi.machine_type;
  StreamBuilder m(this, type, type, type);
  m.Return(m.Emit(dpi.mi.op, m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(dpi.mi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}

TEST_P(TurboshaftInstructionSelectorAddSubTest, ImmediateOnRight) {
  const AddSub dpi = GetParam();
  const MachineType type = dpi.mi.machine_type;
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    StreamBuilder m(this, type, type);
    m.Return(m.Emit(dpi.mi.op, m.Parameter(0), BuildConstant(&m, type, imm)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(dpi.mi.arch_opcode, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_TRUE(s[0]->InputAt(1)->IsImmediate());
    EXPECT_EQ(imm, s.ToInt64(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_P(TurboshaftInstructionSelectorAddSubTest, NegImmediateOnRight) {
  const AddSub dpi = GetParam();
  const MachineType type = dpi.mi.machine_type;
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    if (imm == 0) continue;
    StreamBuilder m(this, type, type);
    m.Return(m.Emit(dpi.mi.op, m.Parameter(0), BuildConstant(&m, type, -imm)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(dpi.negate_arch_opcode, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    ASSERT_TRUE(s[0]->InputAt(1)->IsImmediate());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_P(TurboshaftInstructionSelectorAddSubTest, ShiftByImmediateOnRight) {
  const AddSub dpi = GetParam();
  const MachineType type = dpi.mi.machine_type;
  TRACED_FOREACH(Shift, shift, kShiftInstructions) {
    // Only test 64-bit shifted operands with 64-bit instructions.
    if (shift.mi.machine_type != type) continue;

    if ((shift.mi.arch_opcode == kArm64Ror32) ||
        (shift.mi.arch_opcode == kArm64Ror)) {
      // Not supported by add/sub instructions.
      continue;
    }

    TRACED_FORRANGE(int, imm, 0, ((type == MachineType::Int32()) ? 31 : 63)) {
      StreamBuilder m(this, type, type, type);
      m.Return(
          m.Emit(dpi.mi.op, m.Parameter(0),
                 m.Emit(shift.mi.op, m.Parameter(1), m.Word32Constant(imm))));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(dpi.mi.arch_opcode, s[0]->arch_opcode());
      EXPECT_EQ(shift.mode, s[0]->addressing_mode());
      EXPECT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(imm, s.ToInt64(s[0]->InputAt(2)));
      EXPECT_EQ(1U, s[0]->OutputCount());
    }
  }
}

OpIndex SignExtendAndEmit(TurboshaftInstructionSelectorTest::StreamBuilder& m,
                          MachineType type, TSBinop op, OpIndex left,
                          OpIndex right) {
  RegisterRepresentation rep = RegisterRepresentation::FromMachineType(type);
  if (rep == RegisterRepresentation::Word32()) {
    return m.Emit(op, left, right);
  }
  auto left_rep = m.output_graph().Get(left).outputs_rep();
  auto right_rep = m.output_graph().Get(right).outputs_rep();
  DCHECK_EQ(1U, left_rep.size());
  DCHECK_EQ(1U, right_rep.size());
  if (left_rep[0] == RegisterRepresentation::Word32()) {
    left = m.ChangeInt32ToInt64(left);
  }
  if (right_rep[0] == RegisterRepresentation::Word32()) {
    right = m.ChangeInt32ToInt64(right);
  }
  return m.Emit(op, left, right);
}

TEST_P(TurboshaftInstructionSelectorAddSubTest, UnsignedExtendByte) {
  const AddSub dpi = GetParam();
  const MachineType type = dpi.mi.machine_type;
  StreamBuilder m(this, type, type, type);
  m.Return(SignExtendAndEmit(
      m, type, dpi.mi.op, m.Parameter(0),
      m.Word32BitwiseAnd(m.Parameter(1, RegisterRepresentation::Word32()),
                         m.Int32Constant(0xFF))));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(dpi.mi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(kMode_Operand2_R_UXTB, s[0]->addressing_mode());
  ASSERT_EQ(2U, s[0]->InputCount());
  ASSERT_EQ(1U, s[0]->OutputCount());
}

TEST_P(TurboshaftInstructionSelectorAddSubTest, UnsignedExtendHalfword) {
  const AddSub dpi = GetParam();
  const MachineType type = dpi.mi.machine_type;
  StreamBuilder m(this, type, type, type);
  m.Return(SignExtendAndEmit(
      m, type, dpi.mi.op, m.Parameter(0),
      m.Word32BitwiseAnd(m.Parameter(1, RegisterRepresentation::Word32()),
                         m.Int32Constant(0xFFFF))));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(dpi.mi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(kMode_Operand2_R_UXTH, s[0]->addressing_mode());
  ASSERT_EQ(2U, s[0]->InputCount());
  ASSERT_EQ(1U, s[0]->OutputCount());
}

TEST_P(TurboshaftInstructionSelectorAddSubTest, SignedExtendByte) {
  const AddSub dpi = GetParam();
  const MachineType type = dpi.mi.machine_type;
  StreamBuilder m(this, type, type, type);
  m.Return(SignExtendAndEmit(
      m, type, dpi.mi.op, m.Parameter(0),
      m.Word32ShiftRightArithmetic(
          m.Word32ShiftLeft(m.Parameter(1, RegisterRepresentation::Word32()),
                            m.Int32Constant(24)),
          m.Int32Constant(24))));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(dpi.mi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(kMode_Operand2_R_SXTB, s[0]->addressing_mode());
  ASSERT_EQ(2U, s[0]->InputCount());
  ASSERT_EQ(1U, s[0]->OutputCount());
}

TEST_P(TurboshaftInstructionSelectorAddSubTest, SignedExtendHalfword) {
  const AddSub dpi = GetParam();
  const MachineType type = dpi.mi.machine_type;
  StreamBuilder m(this, type, type, type);
  m.Return(SignExtendAndEmit(
      m, type, dpi.mi.op, m.Parameter(0),
      m.Word32ShiftRightArithmetic(
          m.Word32ShiftLeft(m.Parameter(1, RegisterRepresentation::Word32()),
                            m.Int32Constant(16)),
          m.Int32Constant(16))));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(dpi.mi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(kMode_Operand2_R_SXTH, s[0]->addressing_mode());
  ASSERT_EQ(2U, s[0]->InputCount());
  ASSERT_EQ(1U, s[0]->OutputCount());
}

TEST_P(TurboshaftInstructionSelectorAddSubTest, SignedExtendWord) {
  const AddSub dpi = GetParam();
  const MachineType type = dpi.mi.machine_type;
  if (type != MachineType::Int64()) return;
  StreamBuilder m(this, type, type, MachineType::Int32());
  m.Return(
      m.Emit(dpi.mi.op, m.Parameter(0), m.ChangeInt32ToInt64(m.Parameter(1))));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(dpi.mi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(kMode_Operand2_R_SXTW, s[0]->addressing_mode());
  ASSERT_EQ(2U, s[0]->InputCount());
  ASSERT_EQ(1U, s[0]->OutputCount());
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorAddSubTest,
                         ::testing::ValuesIn(kAddSubInstructions));

TEST_F(TurboshaftInstructionSelectorTest, AddImmediateOnLeft) {
  // 32-bit add.
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(m.Word32Add(m.Int32Constant(imm), m.Parameter(0)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Add32, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_TRUE(s[0]->InputAt(1)->IsImmediate());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }

  // 64-bit add.
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int64());
    m.Return(m.Word64Add(m.Int64Constant(imm), m.Parameter(0)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Add, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_TRUE(s[0]->InputAt(1)->IsImmediate());
    EXPECT_EQ(imm, s.ToInt64(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, SubZeroOnLeft) {
  {
    // 32-bit subtract.
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    m.Return(m.Word32Sub(m.Int32Constant(0), m.Parameter(0)));
    Stream s = m.Build();

    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Sub32, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_TRUE(s[0]->InputAt(0)->IsImmediate());
    EXPECT_EQ(0, s.ToInt32(s[0]->InputAt(0)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  {
    // 64-bit subtract.
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int64(),
                    MachineType::Int64());
    m.Return(m.Word64Sub(m.Int64Constant(0), m.Parameter(0)));
    Stream s = m.Build();

    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Sub, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_TRUE(s[0]->InputAt(0)->IsImmediate());
    EXPECT_EQ(0, s.ToInt64(s[0]->InputAt(0)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, SubZeroOnLeftWithShift) {
  TRACED_FOREACH(Shift, shift, kShiftInstructions) {
    {
      // Test 32-bit operations. Ignore ROR shifts, as subtract does not
      // support them.
      if ((shift.mi.machine_type != MachineType::Int32()) ||
          (shift.mi.arch_opcode == kArm64Ror32) ||
          (shift.mi.arch_opcode == kArm64Ror))
        continue;

      TRACED_FORRANGE(int, imm, -32, 63) {
        StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                        MachineType::Int32());
        m.Return(m.Word32Sub(
            m.Int32Constant(0),
            m.Emit(shift.mi.op, m.Parameter(1), m.Int32Constant(imm))));
        Stream s = m.Build();

        ASSERT_EQ(1U, s.size());
        EXPECT_EQ(kArm64Sub32, s[0]->arch_opcode());
        ASSERT_EQ(3U, s[0]->InputCount());
        EXPECT_TRUE(s[0]->InputAt(0)->IsImmediate());
        EXPECT_EQ(0, s.ToInt32(s[0]->InputAt(0)));
        EXPECT_EQ(shift.mode, s[0]->addressing_mode());
        EXPECT_EQ(0x3F & imm, 0x3F & s.ToInt32(s[0]->InputAt(2)));
        EXPECT_EQ(1U, s[0]->OutputCount());
      }
    }
    {
      // Test 64-bit operations. Ignore ROR shifts, as subtract does not
      // support them.
      if ((shift.mi.machine_type != MachineType::Int64()) ||
          (shift.mi.arch_opcode == kArm64Ror32) ||
          (shift.mi.arch_opcode == kArm64Ror))
        continue;

      TRACED_FORRANGE(int, imm, -32, 127) {
        StreamBuilder m(this, MachineType::Int64(), MachineType::Int64(),
                        MachineType::Int64());
        m.Return(m.Word64Sub(
            m.Int64Constant(0),
            m.Emit(shift.mi.op, m.Parameter(1), m.Int64Constant(imm))));
        Stream s = m.Build();

        ASSERT_EQ(1U, s.size());
        EXPECT_EQ(kArm64Sub, s[0]->arch_opcode());
        ASSERT_EQ(3U, s[0]->InputCount());
        EXPECT_TRUE(s[0]->InputAt(0)->IsImmediate());
        EXPECT_EQ(0, s.ToInt32(s[0]->InputAt(0)));
        EXPECT_EQ(shift.mode, s[0]->addressing_mode());
        EXPECT_EQ(0x3F & imm, 0x3F & s.ToInt32(s[0]->InputAt(2)));
        EXPECT_EQ(1U, s[0]->OutputCount());
      }
    }
  }
}

TEST_F(TurboshaftInstructionSelectorTest, AddNegImmediateOnLeft) {
  // 32-bit add.
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    if (imm == 0) continue;
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(m.Word32Add(m.Int32Constant(-imm), m.Parameter(0)));
    Stream s = m.Build();

    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Sub32, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    ASSERT_TRUE(s[0]->InputAt(1)->IsImmediate());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }

  // 64-bit add.
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    if (imm == 0) continue;
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int64());
    m.Return(m.Word64Add(m.Int64Constant(-imm), m.Parameter(0)));
    Stream s = m.Build();

    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Sub, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    ASSERT_TRUE(s[0]->InputAt(1)->IsImmediate());
    EXPECT_EQ(imm, s.ToInt64(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, AddShiftByImmediateOnLeft) {
  // 32-bit add.
  TRACED_FOREACH(Shift, shift, kShiftInstructions) {
    // Only test relevant shifted operands.
    if (shift.mi.machine_type != MachineType::Int32()) continue;
    if (shift.mi.arch_opcode == kArm64Ror32) continue;

    // The available shift operand range is `0 <= imm < 32`, but we also test
    // that immediates outside this range are handled properly (modulo-32).
    TRACED_FORRANGE(int, imm, -32, 63) {
      StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                      MachineType::Int32());
      m.Return(
          m.Word32Add(m.Emit(shift.mi.op, m.Parameter(1), m.Int32Constant(imm)),
                      m.Parameter(0)));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(kArm64Add32, s[0]->arch_opcode());
      EXPECT_EQ(shift.mode, s[0]->addressing_mode());
      EXPECT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(0x3F & imm, 0x3F & s.ToInt64(s[0]->InputAt(2)));
      EXPECT_EQ(1U, s[0]->OutputCount());
    }
  }

  // 64-bit add.
  TRACED_FOREACH(Shift, shift, kShiftInstructions) {
    // Only test relevant shifted operands.
    if (shift.mi.machine_type != MachineType::Int64()) continue;
    if (shift.mi.arch_opcode == kArm64Ror) continue;

    // The available shift operand range is `0 <= imm < 64`, but we also test
    // that immediates outside this range are handled properly (modulo-64).
    TRACED_FORRANGE(int, imm, -64, 127) {
      StreamBuilder m(this, MachineType::Int64(), MachineType::Int64(),
                      MachineType::Int64());
      m.Return(
          m.Word64Add(m.Emit(shift.mi.op, m.Parameter(1), m.Int32Constant(imm)),
                      m.Parameter(0)));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(kArm64Add, s[0]->arch_opcode());
      EXPECT_EQ(shift.mode, s[0]->addressing_mode());
      EXPECT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(0x3F & imm, 0x3F & s.ToInt64(s[0]->InputAt(2)));
      EXPECT_EQ(1U, s[0]->OutputCount());
    }
  }
}

TEST_F(TurboshaftInstructionSelectorTest, AddUnsignedExtendByteOnLeft) {
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    m.Return(
        m.Word32Add(m.Word32BitwiseAnd(m.Parameter(0), m.Int32Constant(0xFF)),
                    m.Parameter(1)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Add32, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_UXTB, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    ASSERT_EQ(1U, s[0]->OutputCount());
  }
  {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int32(),
                    MachineType::Int64());
    m.Return(m.Word64Add(m.ChangeInt32ToInt64(m.Word32BitwiseAnd(
                             m.Parameter(0), m.Int32Constant(0xFF))),
                         m.Parameter(1)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Add, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_UXTB, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    ASSERT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, AddUnsignedExtendHalfwordOnLeft) {
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    m.Return(
        m.Word32Add(m.Word32BitwiseAnd(m.Parameter(0), m.Int32Constant(0xFFFF)),
                    m.Parameter(1)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Add32, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_UXTH, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    ASSERT_EQ(1U, s[0]->OutputCount());
  }
  {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int32(),
                    MachineType::Int64());
    m.Return(m.Word64Add(m.ChangeInt32ToInt64(m.Word32BitwiseAnd(
                             m.Parameter(0), m.Int32Constant(0xFFFF))),
                         m.Parameter(1)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Add, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_UXTH, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    ASSERT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, AddSignedExtendByteOnLeft) {
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    m.Return(
        m.Word32Add(m.Word32ShiftRightArithmetic(
                        m.Word32ShiftLeft(m.Parameter(0), m.Int32Constant(24)),
                        m.Int32Constant(24)),
                    m.Parameter(1)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Add32, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_SXTB, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    ASSERT_EQ(1U, s[0]->OutputCount());
  }
  {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int32(),
                    MachineType::Int64());
    m.Return(
        m.Word64Add(m.ChangeInt32ToInt64(m.Word32ShiftRightArithmetic(
                        m.Word32ShiftLeft(m.Parameter(0), m.Int32Constant(24)),
                        m.Int32Constant(24))),
                    m.Parameter(1)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Add, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_SXTB, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    ASSERT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, AddSignedExtendHalfwordOnLeft) {
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    m.Return(
        m.Word32Add(m.Word32ShiftRightArithmetic(
                        m.Word32ShiftLeft(m.Parameter(0), m.Int32Constant(16)),
                        m.Int32Constant(16)),
                    m.Parameter(1)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Add32, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_SXTH, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    ASSERT_EQ(1U, s[0]->OutputCount());
  }
  {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int32(),
                    MachineType::Int64());
    m.Return(
        m.Word64Add(m.ChangeInt32ToInt64(m.Word32ShiftRightArithmetic(
                        m.Word32ShiftLeft(m.Parameter(0), m.Int32Constant(16)),
                        m.Int32Constant(16))),
                    m.Parameter(1)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Add, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_SXTH, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    ASSERT_EQ(1U, s[0]->OutputCount());
  }
}

#if V8_ENABLE_WEBASSEMBLY
enum PairwiseAddSide { LEFT, RIGHT };

std::ostream& operator<<(std::ostream& os, const PairwiseAddSide& side) {
  switch (side) {
    case LEFT:
      return os << "LEFT";
    case RIGHT:
      return os << "RIGHT";
  }
}

struct AddWithPairwiseAddSideAndWidth {
  PairwiseAddSide side;
  int32_t width;
  bool isSigned;
};

std::ostream& operator<<(std::ostream& os,
                         const AddWithPairwiseAddSideAndWidth& sw) {
  return os << "{ side: " << sw.side << ", width: " << sw.width
            << ", isSigned: " << sw.isSigned << " }";
}

using TurboshaftInstructionSelectorAddWithPairwiseAddTest =
    TurboshaftInstructionSelectorTestWithParam<AddWithPairwiseAddSideAndWidth>;

TEST_P(TurboshaftInstructionSelectorAddWithPairwiseAddTest,
       AddWithPairwiseAdd) {
  AddWithPairwiseAddSideAndWidth params = GetParam();
  const MachineType type = MachineType::Simd128();
  StreamBuilder m(this, type, type, type, type);

  OpIndex x = m.Parameter(0);
  OpIndex y = m.Parameter(1);
  OpIndex pairwiseAdd;
  if (params.width == 32 && params.isSigned) {
    pairwiseAdd = m.I32x4ExtAddPairwiseI16x8S(x);
  } else if (params.width == 16 && params.isSigned) {
    pairwiseAdd = m.I16x8ExtAddPairwiseI8x16S(x);
  } else if (params.width == 32 && !params.isSigned) {
    pairwiseAdd = m.I32x4ExtAddPairwiseI16x8U(x);
  } else {
    pairwiseAdd = m.I16x8ExtAddPairwiseI8x16U(x);
  }

  OpIndex add;
  if (params.width == 32) {
    add = params.side == LEFT ? m.I32x4Add(pairwiseAdd, y)
                              : m.I32x4Add(y, pairwiseAdd);
  } else {
    add = params.side == LEFT ? m.I16x8Add(pairwiseAdd, y)
                              : m.I16x8Add(y, pairwiseAdd);
  }

  m.Return(add);
  Stream s = m.Build();

  // Should be fused to Sadalp/Uadalp
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(params.isSigned ? kArm64Sadalp : kArm64Uadalp, s[0]->arch_opcode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}

const AddWithPairwiseAddSideAndWidth kAddWithPairAddTestCases[] = {
    {LEFT, 16, true},  {RIGHT, 16, true}, {LEFT, 32, true},
    {RIGHT, 32, true}, {LEFT, 16, false}, {RIGHT, 16, false},
    {LEFT, 32, false}, {RIGHT, 32, false}};

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorAddWithPairwiseAddTest,
                         ::testing::ValuesIn(kAddWithPairAddTestCases));
#endif  // V8_ENABLE_WEBASSEMBLY

// -----------------------------------------------------------------------------
// Data processing controlled branches.

using TurboshaftInstructionSelectorDPFlagSetTest =
    TurboshaftInstructionSelectorTestWithParam<MachInst2>;

TEST_P(TurboshaftInstructionSelectorDPFlagSetTest, BranchWithParameters) {
  const MachInst2 dpi = GetParam();
  const MachineType type = dpi.machine_type;
  StreamBuilder m(this, type, type, type);
  Block *a = m.NewBlock(), *b = m.NewBlock();
  OpIndex cond = m.Emit(dpi.op, m.Parameter(0), m.Parameter(1));
  if (type == MachineType::Int64()) cond = m.TruncateWord64ToWord32(cond);
  m.Branch(V<Word32>::Cast(cond), a, b);
  m.Bind(a);
  m.Return(m.Int32Constant(1));
  m.Bind(b);
  m.Return(m.Int32Constant(0));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
  EXPECT_EQ(kNotEqual, s[0]->flags_condition());
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorDPFlagSetTest,
                         ::testing::ValuesIn(kDPFlagSetInstructions));

TEST_F(TurboshaftInstructionSelectorTest, Word32AndBranchWithImmediateOnRight) {
  TRACED_FOREACH(int32_t, imm, kLogical32Immediates) {
    // Skip the cases where the instruction selector would use tbz/tbnz.
    if (base::bits::CountPopulation(static_cast<uint32_t>(imm)) == 1) continue;

    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    m.Branch(m.Word32BitwiseAnd(m.Parameter(0), m.Int32Constant(imm)), a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(1));
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Tst32, s[0]->arch_opcode());
    EXPECT_EQ(4U, s[0]->InputCount());
    EXPECT_EQ(InstructionOperand::IMMEDIATE, s[0]->InputAt(1)->kind());
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(kNotEqual, s[0]->flags_condition());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, Word64AndBranchWithImmediateOnRight) {
  TRACED_FOREACH(int64_t, imm, kLogical64Immediates) {
    // Skip the cases where the instruction selector would use tbz/tbnz.
    if (base::bits::CountPopulation(static_cast<uint64_t>(imm)) == 1) continue;

    StreamBuilder m(this, MachineType::Int64(), MachineType::Int64());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    m.Branch(m.TruncateWord64ToWord32(
                 m.Word64BitwiseAnd(m.Parameter(0), m.Int64Constant(imm))),
             a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(1));
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Tst, s[0]->arch_opcode());
    EXPECT_EQ(4U, s[0]->InputCount());
    EXPECT_EQ(InstructionOperand::IMMEDIATE, s[0]->InputAt(1)->kind());
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(kNotEqual, s[0]->flags_condition());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, AddBranchWithImmediateOnRight) {
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    m.Branch(m.Word32Add(m.Parameter(0), m.Int32Constant(imm)), a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(1));
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Cmn32, s[0]->arch_opcode());
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(kNotEqual, s[0]->flags_condition());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, SubBranchWithImmediateOnRight) {
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    m.Branch(m.Word32Sub(m.Parameter(0), m.Int32Constant(imm)), a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(1));
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ((imm == 0) ? kArm64CompareAndBranch32 : kArm64Cmp32,
              s[0]->arch_opcode());
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(kNotEqual, s[0]->flags_condition());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, Word32AndBranchWithImmediateOnLeft) {
  TRACED_FOREACH(int32_t, imm, kLogical32Immediates) {
    // Skip the cases where the instruction selector would use tbz/tbnz.
    if (base::bits::CountPopulation(static_cast<uint32_t>(imm)) == 1) continue;

    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    m.Branch(m.Word32BitwiseAnd(m.Int32Constant(imm), m.Parameter(0)), a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(1));
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Tst32, s[0]->arch_opcode());
    EXPECT_EQ(4U, s[0]->InputCount());
    EXPECT_EQ(InstructionOperand::IMMEDIATE, s[0]->InputAt(1)->kind());
    ASSERT_LE(1U, s[0]->InputCount());
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(kNotEqual, s[0]->flags_condition());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, Word64AndBranchWithImmediateOnLeft) {
  TRACED_FOREACH(int64_t, imm, kLogical64Immediates) {
    // Skip the cases where the instruction selector would use tbz/tbnz.
    if (base::bits::CountPopulation(static_cast<uint64_t>(imm)) == 1) continue;

    StreamBuilder m(this, MachineType::Int64(), MachineType::Int64());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    m.Branch(m.TruncateWord64ToWord32(
                 m.Word64BitwiseAnd(m.Int64Constant(imm), m.Parameter(0))),
             a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(1));
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Tst, s[0]->arch_opcode());
    EXPECT_EQ(4U, s[0]->InputCount());
    EXPECT_EQ(InstructionOperand::IMMEDIATE, s[0]->InputAt(1)->kind());
    ASSERT_LE(1U, s[0]->InputCount());
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(kNotEqual, s[0]->flags_condition());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, AddBranchWithImmediateOnLeft) {
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    m.Branch(m.Word32Add(m.Int32Constant(imm), m.Parameter(0)), a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(1));
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Cmn32, s[0]->arch_opcode());
    ASSERT_LE(1U, s[0]->InputCount());
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(kNotEqual, s[0]->flags_condition());
  }
}

struct TestAndBranch {
  MachInst<
      std::function<V<Word32>(TurboshaftInstructionSelectorTest::StreamBuilder&,
                              OpIndex, uint64_t mask)>>
      mi;
  FlagsCondition cond;
};

std::ostream& operator<<(std::ostream& os, const TestAndBranch& tb) {
  return os << tb.mi;
}

const TestAndBranch kTestAndBranchMatchers32[] = {
    // Branch on the result of Word32BitwiseAnd directly.
    {{[](TurboshaftInstructionSelectorTest::StreamBuilder& m, OpIndex x,
         uint32_t mask) -> V<Word32> {
        return m.Word32BitwiseAnd(x, m.Int32Constant(mask));
      },
      "if (x and mask)", kArm64TestAndBranch32, MachineType::Int32()},
     kNotEqual},
    {{[](TurboshaftInstructionSelectorTest::StreamBuilder& m, OpIndex x,
         uint32_t mask) -> V<Word32> {
        return m.Word32BinaryNot(m.Word32BitwiseAnd(x, m.Int32Constant(mask)));
      },
      "if not (x and mask)", kArm64TestAndBranch32, MachineType::Int32()},
     kEqual},
    // Branch on the result of '(x and mask) == mask'. This tests that a bit is
    // set rather than cleared which is why conditions are inverted.
    {{[](TurboshaftInstructionSelectorTest::StreamBuilder& m, OpIndex x,
         uint32_t mask) -> V<Word32> {
        return m.Word32Equal(m.Word32BitwiseAnd(x, m.Int32Constant(mask)),
                             m.Int32Constant(mask));
      },
      "if ((x and mask) == mask)", kArm64TestAndBranch32, MachineType::Int32()},
     kNotEqual},
    {{[](TurboshaftInstructionSelectorTest::StreamBuilder& m, OpIndex x,
         uint32_t mask) -> V<Word32> {
        return m.Word32BinaryNot(
            m.Word32Equal(m.Word32BitwiseAnd(x, m.Int32Constant(mask)),
                          m.Int32Constant(mask)));
      },
      "if ((x and mask) != mask)", kArm64TestAndBranch32, MachineType::Int32()},
     kEqual},
    {{[](TurboshaftInstructionSelectorTest::StreamBuilder& m, OpIndex x,
         uint32_t mask) -> V<Word32> {
        return m.Word32Equal(m.Int32Constant(mask),
                             m.Word32BitwiseAnd(x, m.Int32Constant(mask)));
      },
      "if (mask == (x and mask))", kArm64TestAndBranch32, MachineType::Int32()},
     kNotEqual},
    {{[](TurboshaftInstructionSelectorTest::StreamBuilder& m, OpIndex x,
         uint32_t mask) -> V<Word32> {
        return m.Word32BinaryNot(
            m.Word32Equal(m.Int32Constant(mask),
                          m.Word32BitwiseAnd(x, m.Int32Constant(mask))));
      },
      "if (mask != (x and mask))", kArm64TestAndBranch32, MachineType::Int32()},
     kEqual}};

using TurboshaftInstructionSelectorTestAndBranchTest =
    TurboshaftInstructionSelectorTestWithParam<TestAndBranch>;

TEST_P(TurboshaftInstructionSelectorTestAndBranchTest, TestAndBranch32) {
  const TestAndBranch inst = GetParam();
  TRACED_FORRANGE(int, bit, 0, 31) {
    uint32_t mask = 1 << bit;
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    m.Branch(inst.mi.op(m, m.Parameter(0), mask), a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(1));
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(inst.mi.arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(inst.cond, s[0]->flags_condition());
    EXPECT_EQ(4U, s[0]->InputCount());
    EXPECT_EQ(InstructionOperand::IMMEDIATE, s[0]->InputAt(1)->kind());
    EXPECT_EQ(bit, s.ToInt32(s[0]->InputAt(1)));
  }
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorTestAndBranchTest,
                         ::testing::ValuesIn(kTestAndBranchMatchers32));

// TODO(arm64): Add the missing Word32BinaryNot test cases from the 32-bit
// version.
const TestAndBranch kTestAndBranchMatchers64[] = {
    // Branch on the result of Word64BitwiseAnd directly.
    {{[](TurboshaftInstructionSelectorTest::StreamBuilder& m, OpIndex x,
         uint64_t mask) -> V<Word32> {
        return m.TruncateWord64ToWord32(
            m.Word64BitwiseAnd(x, m.Int64Constant(mask)));
      },
      "if (x and mask)", kArm64TestAndBranch, MachineType::Int64()},
     kNotEqual},
    {{[](TurboshaftInstructionSelectorTest::StreamBuilder& m, OpIndex x,
         uint64_t mask) -> V<Word32> {
        return m.Word64Equal(m.Word64BitwiseAnd(x, m.Int64Constant(mask)),
                             m.Int64Constant(0));
      },
      "if not (x and mask)", kArm64TestAndBranch, MachineType::Int64()},
     kEqual},
    // Branch on the result of '(x and mask) == mask'. This tests that a bit is
    // set rather than cleared which is why conditions are inverted.
    {{[](TurboshaftInstructionSelectorTest::StreamBuilder& m, OpIndex x,
         uint64_t mask) -> V<Word32> {
        return m.Word64Equal(m.Word64BitwiseAnd(x, m.Int64Constant(mask)),
                             m.Int64Constant(mask));
      },
      "if ((x and mask) == mask)", kArm64TestAndBranch, MachineType::Int64()},
     kNotEqual},
    {{[](TurboshaftInstructionSelectorTest::StreamBuilder& m, OpIndex x,
         uint64_t mask) -> V<Word32> {
        return m.Word64Equal(m.Int64Constant(mask),
                             m.Word64BitwiseAnd(x, m.Int64Constant(mask)));
      },
      "if (mask == (x and mask))", kArm64TestAndBranch, MachineType::Int64()},
     kNotEqual}};

using TurboshaftInstructionSelectorTestAndBranchTest64 =
    TurboshaftInstructionSelectorTestWithParam<TestAndBranch>;

TEST_P(TurboshaftInstructionSelectorTestAndBranchTest64, TestAndBranch64) {
  const TestAndBranch inst = GetParam();
  // TRACED_FORRANGE(int, bit, 0, 63) {
  int bit = 0;
  uint64_t mask = uint64_t{1} << bit;
  StreamBuilder m(this, MachineType::Int64(), MachineType::Int64());
  Block *a = m.NewBlock(), *b = m.NewBlock();
  m.Branch(inst.mi.op(m, m.Parameter(0), mask), a, b);
  m.Bind(a);
  m.Return(m.Int64Constant(1));
  m.Bind(b);
  m.Return(m.Int64Constant(0));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(inst.mi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(inst.cond, s[0]->flags_condition());
  EXPECT_EQ(4U, s[0]->InputCount());
  EXPECT_EQ(InstructionOperand::IMMEDIATE, s[0]->InputAt(1)->kind());
  EXPECT_EQ(bit, s.ToInt64(s[0]->InputAt(1)));
  // }
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorTestAndBranchTest64,
                         ::testing::ValuesIn(kTestAndBranchMatchers64));

TEST_F(TurboshaftInstructionSelectorTest,
       Word64AndBranchWithOneBitMaskOnRight) {
  TRACED_FORRANGE(int, bit, 0, 63) {
    uint64_t mask = uint64_t{1} << bit;
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int64());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    m.Branch(m.TruncateWord64ToWord32(
                 m.Word64BitwiseAnd(m.Parameter(0), m.Int64Constant(mask))),
             a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(1));
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64TestAndBranch, s[0]->arch_opcode());
    EXPECT_EQ(kNotEqual, s[0]->flags_condition());
    EXPECT_EQ(4U, s[0]->InputCount());
    EXPECT_EQ(InstructionOperand::IMMEDIATE, s[0]->InputAt(1)->kind());
    EXPECT_EQ(bit, s.ToInt64(s[0]->InputAt(1)));
  }
}

TEST_F(TurboshaftInstructionSelectorTest,
       TestAndBranch64EqualWhenCanCoverFalse) {
  TRACED_FORRANGE(int, bit, 0, 63) {
    uint64_t mask = uint64_t{1} << bit;
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int64());
    Block *a = m.NewBlock(), *b = m.NewBlock(), *c = m.NewBlock();
    OpIndex n = m.Word64BitwiseAnd(m.Parameter(0), m.Int64Constant(mask));
    m.Branch(m.Word64Equal(n, m.Int64Constant(0)), a, b);
    m.Bind(a);
    m.Branch(m.Word64Equal(n, m.Int64Constant(3)), b, c);
    m.Bind(c);
    m.Return(m.Int64Constant(1));
    m.Bind(b);
    m.Return(m.Int64Constant(0));

    Stream s = m.Build();
    ASSERT_EQ(3U, s.size());
    EXPECT_EQ(kArm64And, s[0]->arch_opcode());
    EXPECT_EQ(kEqual, s[0]->flags_condition());
    EXPECT_EQ(kArm64TestAndBranch, s[1]->arch_opcode());
    EXPECT_EQ(kEqual, s[1]->flags_condition());
    EXPECT_EQ(kArm64Cmp, s[2]->arch_opcode());
    EXPECT_EQ(kEqual, s[2]->flags_condition());
    EXPECT_EQ(2U, s[0]->InputCount());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, TestAndBranch64AndWhenCanCoverFalse) {
  TRACED_FORRANGE(int, bit, 0, 63) {
    uint64_t mask = uint64_t{1} << bit;
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int64());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    m.Branch(m.TruncateWord64ToWord32(
                 m.Word64BitwiseAnd(m.Parameter(0), m.Int64Constant(mask))),
             a, b);
    m.Bind(a);
    m.Return(m.Int64Constant(1));
    m.Bind(b);
    m.Return(m.Int64Constant(0));

    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64TestAndBranch, s[0]->arch_opcode());
    EXPECT_EQ(InstructionOperand::IMMEDIATE, s[0]->InputAt(1)->kind());
    EXPECT_EQ(4U, s[0]->InputCount());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, TestAndBranch32AndWhenCanCoverFalse) {
  TRACED_FORRANGE(int, bit, 0, 31) {
    uint32_t mask = uint32_t{1} << bit;
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    m.Branch(m.Word32BitwiseAnd(m.Parameter(0), m.Int32Constant(mask)), a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(1));
    m.Bind(b);
    m.Return(m.Int32Constant(0));

    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64TestAndBranch32, s[0]->arch_opcode());
    EXPECT_EQ(InstructionOperand::IMMEDIATE, s[0]->InputAt(1)->kind());
    EXPECT_EQ(4U, s[0]->InputCount());
  }
}

TEST_F(TurboshaftInstructionSelectorTest,
       Word32EqualZeroAndBranchWithOneBitMask) {
  TRACED_FORRANGE(int, bit, 0, 31) {
    uint32_t mask = 1 << bit;
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    m.Branch(
        m.Word32Equal(m.Word32BitwiseAnd(m.Parameter(0), m.Int32Constant(mask)),
                      m.Int32Constant(0)),
        a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(1));
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64TestAndBranch32, s[0]->arch_opcode());
    EXPECT_EQ(kEqual, s[0]->flags_condition());
    EXPECT_EQ(4U, s[0]->InputCount());
    EXPECT_EQ(InstructionOperand::IMMEDIATE, s[0]->InputAt(1)->kind());
    EXPECT_EQ(bit, s.ToInt32(s[0]->InputAt(1)));
  }

  TRACED_FORRANGE(int, bit, 0, 31) {
    uint32_t mask = 1 << bit;
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    m.Branch(m.Word32NotEqual(
                 m.Word32BitwiseAnd(m.Parameter(0), m.Int32Constant(mask)),
                 m.Int32Constant(0)),
             a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(1));
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64TestAndBranch32, s[0]->arch_opcode());
    EXPECT_EQ(kNotEqual, s[0]->flags_condition());
    EXPECT_EQ(4U, s[0]->InputCount());
    EXPECT_EQ(InstructionOperand::IMMEDIATE, s[0]->InputAt(1)->kind());
    EXPECT_EQ(bit, s.ToInt32(s[0]->InputAt(1)));
  }
}

TEST_F(TurboshaftInstructionSelectorTest,
       Word64EqualZeroAndBranchWithOneBitMask) {
  TRACED_FORRANGE(int, bit, 0, 63) {
    uint64_t mask = uint64_t{1} << bit;
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int64());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    m.Branch(
        m.Word64Equal(m.Word64BitwiseAnd(m.Parameter(0), m.Int64Constant(mask)),
                      m.Int64Constant(0)),
        a, b);
    m.Bind(a);
    m.Return(m.Int64Constant(1));
    m.Bind(b);
    m.Return(m.Int64Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64TestAndBranch, s[0]->arch_opcode());
    EXPECT_EQ(kEqual, s[0]->flags_condition());
    EXPECT_EQ(4U, s[0]->InputCount());
    EXPECT_EQ(InstructionOperand::IMMEDIATE, s[0]->InputAt(1)->kind());
    EXPECT_EQ(bit, s.ToInt64(s[0]->InputAt(1)));
  }

  TRACED_FORRANGE(int, bit, 0, 63) {
    uint64_t mask = uint64_t{1} << bit;
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int64());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    m.Branch(m.Word64NotEqual(
                 m.Word64BitwiseAnd(m.Parameter(0), m.Int64Constant(mask)),
                 m.Int64Constant(0)),
             a, b);
    m.Bind(a);
    m.Return(m.Int64Constant(1));
    m.Bind(b);
    m.Return(m.Int64Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64TestAndBranch, s[0]->arch_opcode());
    EXPECT_EQ(kNotEqual, s[0]->flags_condition());
    EXPECT_EQ(4U, s[0]->InputCount());
    EXPECT_EQ(InstructionOperand::IMMEDIATE, s[0]->InputAt(1)->kind());
    EXPECT_EQ(bit, s.ToInt64(s[0]->InputAt(1)));
  }
}

TEST_F(TurboshaftInstructionSelectorTest, CompareAgainstZeroAndBranch) {
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    V<Word32> p0 = m.Parameter(0);
    m.Branch(p0, a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(1));
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64CompareAndBranch32, s[0]->arch_opcode());
    EXPECT_EQ(kNotEqual, s[0]->flags_condition());
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  }

  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    OpIndex p0 = m.Parameter(0);
    m.Branch(m.Word32BinaryNot(p0), a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(1));
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64CompareAndBranch32, s[0]->arch_opcode());
    EXPECT_EQ(kEqual, s[0]->flags_condition());
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  }
}

TEST_F(TurboshaftInstructionSelectorTest, EqualZeroAndBranch) {
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    OpIndex p0 = m.Parameter(0);
    m.Branch(m.Word32Equal(p0, m.Int32Constant(0)), a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(1));
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64CompareAndBranch32, s[0]->arch_opcode());
    EXPECT_EQ(kEqual, s[0]->flags_condition());
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  }

  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    OpIndex p0 = m.Parameter(0);
    m.Branch(m.Word32NotEqual(p0, m.Int32Constant(0)), a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(1));
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64CompareAndBranch32, s[0]->arch_opcode());
    EXPECT_EQ(kNotEqual, s[0]->flags_condition());
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  }

  {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int64());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    OpIndex p0 = m.Parameter(0);
    m.Branch(m.Word64Equal(p0, m.Int64Constant(0)), a, b);
    m.Bind(a);
    m.Return(m.Int64Constant(1));
    m.Bind(b);
    m.Return(m.Int64Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64CompareAndBranch, s[0]->arch_opcode());
    EXPECT_EQ(kEqual, s[0]->flags_condition());
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  }

  {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int64());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    OpIndex p0 = m.Parameter(0);
    m.Branch(m.Word64NotEqual(p0, m.Int64Constant(0)), a, b);
    m.Bind(a);
    m.Return(m.Int64Constant(1));
    m.Bind(b);
    m.Return(m.Int64Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64CompareAndBranch, s[0]->arch_opcode());
    EXPECT_EQ(kNotEqual, s[0]->flags_condition());
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  }
}

TEST_F(TurboshaftInstructionSelectorTest, ConditionalCompares) {
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32(), MachineType::Int32());
    OpIndex a = m.Int32LessThan(m.Parameter(0), m.Parameter(1));
    OpIndex b = m.Int32LessThan(m.Parameter(0), m.Parameter(2));
    m.Return(m.Word32BitwiseAnd(a, b));
    Stream s = m.Build();
    EXPECT_EQ(kArm64Cmp32, s[0]->arch_opcode());
    EXPECT_EQ(kFlags_conditional_set, s[0]->flags_mode());
    EXPECT_EQ(9U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int64(),
                    MachineType::Int64(), MachineType::Int64());
    OpIndex a = m.Word64Equal(m.Parameter(0), m.Parameter(1));
    OpIndex b = m.Word64Equal(m.Parameter(0), m.Parameter(2));
    OpIndex c = m.Word64NotEqual(m.Parameter(0), m.Int64Constant(42));
    m.Return(m.Word32BitwiseOr(m.Word32BitwiseOr(a, b), c));
    Stream s = m.Build();
    EXPECT_EQ(kArm64Cmp, s[0]->arch_opcode());
    EXPECT_EQ(kFlags_conditional_set, s[0]->flags_mode());
    EXPECT_EQ(14U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int64(),
                    MachineType::Int64(), MachineType::Int64());
    OpIndex a = m.Word64Equal(m.Parameter(0), m.Int64Constant(30));
    OpIndex b = m.Word64Equal(m.Parameter(0), m.Int64Constant(50));
    OpIndex c = m.Uint64LessThanOrEqual(m.Parameter(0), m.Parameter(1));
    OpIndex d = m.Int64LessThan(m.Parameter(0), m.Parameter(2));
    m.Return(
        m.Word32BitwiseAnd(m.Word32BitwiseAnd(m.Word32BitwiseOr(a, b), c), d));
    Stream s = m.Build();
    EXPECT_EQ(kArm64Cmp, s[0]->arch_opcode());
    EXPECT_EQ(50, s.ToInt64(s[0]->InputAt(1)));
    EXPECT_EQ(kFlags_conditional_set, s[0]->flags_mode());
    EXPECT_EQ(19U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int64(),
                    MachineType::Int64(), MachineType::Int64());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    OpIndex cond_a = m.Int64LessThan(m.Parameter(0), m.Parameter(1));
    OpIndex cond_b = m.Int64LessThan(m.Parameter(0), m.Parameter(2));
    m.Branch(m.Word32BitwiseAnd(cond_a, cond_b), a, b);
    m.Bind(a);
    m.Return(m.Int64Constant(1));
    m.Bind(b);
    m.Return(m.Int64Constant(0));
    Stream s = m.Build();
    EXPECT_EQ(kArm64Cmp, s[0]->arch_opcode());
    EXPECT_EQ(kFlags_conditional_branch, s[0]->flags_mode());
  }
  {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int32(),
                    MachineType::Int32(), MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    OpIndex cond_a = m.Int32LessThan(m.Parameter(0), m.Parameter(1));
    OpIndex cond_b = m.Int32LessThan(m.Parameter(0), m.Parameter(2));
    m.Branch(m.Word32BitwiseOr(cond_a, cond_b), a, b);
    m.Bind(a);
    m.Return(m.Int64Constant(1));
    m.Bind(b);
    m.Return(m.Int64Constant(0));
    Stream s = m.Build();
    EXPECT_EQ(kArm64Cmp32, s[0]->arch_opcode());
    EXPECT_EQ(kFlags_conditional_branch, s[0]->flags_mode());
  }
}

// -----------------------------------------------------------------------------
// Add and subtract instructions with overflow.

using TurboshaftInstructionSelectorOvfAddSubTest =
    TurboshaftInstructionSelectorTestWithParam<MachInst2>;

TEST_P(TurboshaftInstructionSelectorOvfAddSubTest, OvfParameter) {
  const MachInst2 dpi = GetParam();
  const MachineType type = dpi.machine_type;
  StreamBuilder m(this, type, type, type);
  m.Return(m.Projection(m.Emit(dpi.op, m.Parameter(0), m.Parameter(1)), 1));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_LE(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_set, s[0]->flags_mode());
  EXPECT_EQ(kOverflow, s[0]->flags_condition());
}

TEST_P(TurboshaftInstructionSelectorOvfAddSubTest, OvfImmediateOnRight) {
  const MachInst2 dpi = GetParam();
  const MachineType type = dpi.machine_type;
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    StreamBuilder m(this, type, type);
    OpIndex cst = dpi.machine_type == MachineType::Int32()
                      ? OpIndex{m.Int32Constant(imm)}
                      : OpIndex{m.Int64Constant(imm)};
    m.Return(m.Projection(m.Emit(dpi.op, m.Parameter(0), cst), 1));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_LE(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kOverflow, s[0]->flags_condition());
  }
}

TEST_P(TurboshaftInstructionSelectorOvfAddSubTest, ValParameter) {
  const MachInst2 dpi = GetParam();
  const MachineType type = dpi.machine_type;
  StreamBuilder m(this, type, type, type);
  m.Return(m.Projection(m.Emit(dpi.op, m.Parameter(0), m.Parameter(1)), 0));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_LE(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_none, s[0]->flags_mode());
}

TEST_P(TurboshaftInstructionSelectorOvfAddSubTest, ValImmediateOnRight) {
  const MachInst2 dpi = GetParam();
  const MachineType type = dpi.machine_type;
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    StreamBuilder m(this, type, type);
    OpIndex cst = dpi.machine_type == MachineType::Int32()
                      ? OpIndex{m.Int32Constant(imm)}
                      : OpIndex{m.Int64Constant(imm)};
    m.Return(m.Projection(m.Emit(dpi.op, m.Parameter(0), cst), 0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_LE(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_none, s[0]->flags_mode());
  }
}

TEST_P(TurboshaftInstructionSelectorOvfAddSubTest, BothParameter) {
  const MachInst2 dpi = GetParam();
  const MachineType type = dpi.machine_type;
  StreamBuilder m(this, type, type, type);
  OpIndex n = m.Emit(dpi.op, m.Parameter(0), m.Parameter(1));
  OpIndex proj0 = type == MachineType::Int64()
                      ? m.TruncateWord64ToWord32(m.Projection(n, 0))
                      : m.Projection(n, 0);
  m.Return(m.Word32Equal(proj0, m.Projection(n, 1)));
  Stream s = m.Build();
  ASSERT_LE(1U, s.size());
  EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(2U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_set, s[0]->flags_mode());
  EXPECT_EQ(kOverflow, s[0]->flags_condition());
}

TEST_P(TurboshaftInstructionSelectorOvfAddSubTest, BothImmediateOnRight) {
  const MachInst2 dpi = GetParam();
  const MachineType type = dpi.machine_type;
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    StreamBuilder m(this, type, type);
    OpIndex cst = dpi.machine_type == MachineType::Int32()
                      ? OpIndex{m.Int32Constant(imm)}
                      : OpIndex{m.Int64Constant(imm)};
    OpIndex n = m.Emit(dpi.op, m.Parameter(0), cst);
    OpIndex proj0 = type == MachineType::Int64()
                        ? m.TruncateWord64ToWord32(m.Projection(n, 0))
                        : m.Projection(n, 0);
    m.Return(m.Word32Equal(proj0, m.Projection(n, 1)));
    Stream s = m.Build();
    ASSERT_LE(1U, s.size());
    EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(2U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kOverflow, s[0]->flags_condition());
  }
}

TEST_P(TurboshaftInstructionSelectorOvfAddSubTest, BranchWithParameters) {
  const MachInst2 dpi = GetParam();
  const MachineType type = dpi.machine_type;
  StreamBuilder m(this, type, type, type);
  Block *a = m.NewBlock(), *b = m.NewBlock();
  OpIndex n = m.Emit(dpi.op, m.Parameter(0), m.Parameter(1));
  m.Branch(V<Word32>::Cast(m.Projection(n, 1)), a, b);
  m.Bind(a);
  m.Return(m.Int32Constant(0));
  m.Bind(b);
  m.Return(m.Projection(n, 0));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(4U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
  EXPECT_EQ(kOverflow, s[0]->flags_condition());
}

TEST_P(TurboshaftInstructionSelectorOvfAddSubTest, BranchWithImmediateOnRight) {
  const MachInst2 dpi = GetParam();
  const MachineType type = dpi.machine_type;
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    StreamBuilder m(this, type, type);
    Block *a = m.NewBlock(), *b = m.NewBlock();
    OpIndex cst = dpi.machine_type == MachineType::Int32()
                      ? OpIndex{m.Int32Constant(imm)}
                      : OpIndex{m.Int64Constant(imm)};
    OpIndex n = m.Emit(dpi.op, m.Parameter(0), cst);
    m.Branch(V<Word32>::Cast(m.Projection(n, 1)), a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(0));
    m.Bind(b);
    m.Return(m.Projection(n, 0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
    ASSERT_EQ(4U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(kOverflow, s[0]->flags_condition());
  }
}

TEST_P(TurboshaftInstructionSelectorOvfAddSubTest, RORShift) {
  // ADD and SUB do not support ROR shifts, make sure we do not try
  // to merge them into the ADD/SUB instruction.
  const MachInst2 dpi = GetParam();
  const MachineType type = dpi.machine_type;
  auto rotate = TSBinop::kWord64RotateRight;
  ArchOpcode rotate_opcode = kArm64Ror;
  if (type == MachineType::Int32()) {
    rotate = TSBinop::kWord32RotateRight;
    rotate_opcode = kArm64Ror32;
  }
  TRACED_FORRANGE(int32_t, imm, -32, 63) {
    StreamBuilder m(this, type, type, type);
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    OpIndex r = m.Emit(rotate, p1, m.Int32Constant(imm));
    m.Return(m.Projection(m.Emit(dpi.op, p0, r), 0));
    Stream s = m.Build();
    ASSERT_EQ(2U, s.size());
    EXPECT_EQ(rotate_opcode, s[0]->arch_opcode());
    EXPECT_EQ(dpi.arch_opcode, s[1]->arch_opcode());
  }
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorOvfAddSubTest,
                         ::testing::ValuesIn(kOvfAddSubInstructions));

TEST_F(TurboshaftInstructionSelectorTest, OvfFlagAddImmediateOnRight) {
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(m.Projection(
        m.Int32AddCheckOverflow(m.Parameter(0), m.Int32Constant(imm)), 1));
    Stream s = m.Build();

    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Add32, s[0]->arch_opcode());
    EXPECT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_LE(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kOverflow, s[0]->flags_condition());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, OvfValAddImmediateOnRight) {
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(m.Projection(
        m.Int32AddCheckOverflow(m.Parameter(0), m.Int32Constant(imm)), 0));
    Stream s = m.Build();

    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Add32, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_LE(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_none, s[0]->flags_mode());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, OvfBothAddImmediateOnRight) {
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    OpIndex n = m.Int32AddCheckOverflow(m.Parameter(0), m.Int32Constant(imm));
    m.Return(m.Word32Equal(m.Projection(n, 0), m.Projection(n, 1)));
    Stream s = m.Build();

    ASSERT_LE(1U, s.size());
    EXPECT_EQ(kArm64Add32, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(2U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kOverflow, s[0]->flags_condition());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, OvfBranchWithImmediateOnRight) {
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    OpIndex n = m.Int32AddCheckOverflow(m.Parameter(0), m.Int32Constant(imm));
    m.Branch(V<Word32>::Cast(m.Projection(n, 1)), a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(0));
    m.Bind(b);
    m.Return(m.Projection(n, 0));
    Stream s = m.Build();

    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Add32, s[0]->arch_opcode());
    ASSERT_EQ(4U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(kOverflow, s[0]->flags_condition());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, OvfValMulImmediateOnRight) {
  TRACED_FORRANGE(int32_t, shift, 0, 30) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(m.Projection(
        m.Int32MulCheckOverflow(m.Parameter(0), m.Int32Constant(1 << shift)),
        0));
    Stream s = m.Build();

    ASSERT_EQ(2U, s.size());
    EXPECT_EQ(kArm64Sbfiz, s[0]->arch_opcode());
    EXPECT_EQ(kArm64Cmp, s[1]->arch_opcode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(shift, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_LE(1U, s[0]->OutputCount());
    EXPECT_EQ(32, s.ToInt32(s[0]->InputAt(2)));
    EXPECT_EQ(kFlags_none, s[0]->flags_mode());
  }
}

// -----------------------------------------------------------------------------
// Shift instructions.

using TurboshaftInstructionSelectorShiftTest =
    TurboshaftInstructionSelectorTestWithParam<Shift>;

TEST_P(TurboshaftInstructionSelectorShiftTest, Parameter) {
  const Shift shift = GetParam();
  const MachineType type = shift.mi.machine_type;
  StreamBuilder m(this, type, type, MachineType::Int32());
  m.Return(m.Emit(shift.mi.op, m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(shift.mi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}

TEST_P(TurboshaftInstructionSelectorShiftTest, Immediate) {
  const Shift shift = GetParam();
  const MachineType type = shift.mi.machine_type;
  TRACED_FORRANGE(int32_t, imm, 0,
                  ((1 << ElementSizeLog2Of(type.representation())) * 8) - 1) {
    StreamBuilder m(this, type, type);
    m.Return(m.Emit(shift.mi.op, m.Parameter(0), m.Int32Constant(imm)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(shift.mi.arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(2U, s[0]->InputCount());
    EXPECT_TRUE(s[0]->InputAt(1)->IsImmediate());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorShiftTest,
                         ::testing::ValuesIn(kShiftInstructions));

TEST_F(TurboshaftInstructionSelectorTest, Word64ShlWithChangeInt32ToInt64) {
  TRACED_FORRANGE(int32_t, x, 32, 63) {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const n =
        m.Word64ShiftLeft(m.ChangeInt32ToInt64(p0), m.Int32Constant(x));
    m.Return(n);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Lsl, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(x, s.ToInt64(s[0]->InputAt(1)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
  }
}

TEST_F(TurboshaftInstructionSelectorTest, Word64ShlWithChangeUint32ToUint64) {
  TRACED_FORRANGE(int32_t, x, 32, 63) {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Uint32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const n =
        m.Word64ShiftLeft(m.ChangeUint32ToUint64(p0), m.Int32Constant(x));
    m.Return(n);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Lsl, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(x, s.ToInt64(s[0]->InputAt(1)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
  }
}

TEST_F(TurboshaftInstructionSelectorTest, TruncateWord64ToWord32WithWord64Sar) {
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int64());
  OpIndex const p = m.Parameter(0);
  OpIndex const t = m.TruncateWord64ToWord32(
      m.Word64ShiftRightArithmetic(p, m.Int32Constant(32)));
  m.Return(t);
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArm64Asr, s[0]->arch_opcode());
  ASSERT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(s.ToVreg(p), s.ToVreg(s[0]->InputAt(0)));
  EXPECT_EQ(32, s.ToInt64(s[0]->InputAt(1)));
  ASSERT_EQ(1U, s[0]->OutputCount());
}

TEST_F(TurboshaftInstructionSelectorTest,
       TruncateWord64ToWord32WithWord64ShiftRightLogical) {
  TRACED_FORRANGE(int32_t, x, 32, 63) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int64());
    OpIndex const p = m.Parameter(0);
    OpIndex const t = m.TruncateWord64ToWord32(
        m.Word64ShiftRightLogical(p, m.Int32Constant(x)));
    m.Return(t);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Lsr, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(x, s.ToInt64(s[0]->InputAt(1)));
    ASSERT_EQ(1U, s[0]->OutputCount());
  }
}

// -----------------------------------------------------------------------------
// Mul and Div instructions.

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

namespace {

struct MulDPInst {
  const char* mul_constructor_name;
  TSBinop mul_op;
  TSBinop add_op;
  TSBinop sub_op;
  ArchOpcode multiply_add_arch_opcode;
  ArchOpcode multiply_sub_arch_opcode;
  ArchOpcode multiply_neg_arch_opcode;
  MachineType machine_type;
};

std::ostream& operator<<(std::ostream& os, const MulDPInst& inst) {
  return os << inst.mul_constructor_name;
}

}  // namespace

static const MulDPInst kMulDPInstructions[] = {
    {"Word32Mul", TSBinop::kWord32Mul, TSBinop::kWord32Add, TSBinop::kWord32Sub,
     kArm64Madd32, kArm64Msub32, kArm64Mneg32, MachineType::Int32()},
    {"Word64Mul", TSBinop::kWord64Mul, TSBinop::kWord64Add, TSBinop::kWord64Sub,
     kArm64Madd, kArm64Msub, kArm64Mneg, MachineType::Int64()}};

using TurboshaftInstructionSelectorIntDPWithIntMulTest =
    TurboshaftInstructionSelectorTestWithParam<MulDPInst>;

TEST_P(TurboshaftInstructionSelectorIntDPWithIntMulTest, AddWithMul) {
  const MulDPInst mdpi = GetParam();
  const MachineType type = mdpi.machine_type;
  {
    StreamBuilder m(this, type, type, type, type);
    OpIndex n = m.Emit(mdpi.mul_op, m.Parameter(1), m.Parameter(2));
    m.Return(m.Emit(mdpi.add_op, m.Parameter(0), n));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(mdpi.multiply_add_arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  {
    StreamBuilder m(this, type, type, type, type);
    OpIndex n = m.Emit(mdpi.mul_op, m.Parameter(0), m.Parameter(1));
    m.Return(m.Emit(mdpi.add_op, n, m.Parameter(2)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(mdpi.multiply_add_arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_P(TurboshaftInstructionSelectorIntDPWithIntMulTest, SubWithMul) {
  const MulDPInst mdpi = GetParam();
  const MachineType type = mdpi.machine_type;
  {
    StreamBuilder m(this, type, type, type, type);
    OpIndex n = m.Emit(mdpi.mul_op, m.Parameter(1), m.Parameter(2));
    m.Return(m.Emit(mdpi.sub_op, m.Parameter(0), n));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(mdpi.multiply_sub_arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_P(TurboshaftInstructionSelectorIntDPWithIntMulTest, NegativeMul) {
  const MulDPInst mdpi = GetParam();
  const MachineType type = mdpi.machine_type;
  {
    StreamBuilder m(this, type, type, type);
    OpIndex n = m.Emit(mdpi.sub_op, BuildConstant(&m, type, 0), m.Parameter(0));
    m.Return(m.Emit(mdpi.mul_op, n, m.Parameter(1)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(mdpi.multiply_neg_arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  {
    StreamBuilder m(this, type, type, type);
    OpIndex n = m.Emit(mdpi.sub_op, BuildConstant(&m, type, 0), m.Parameter(1));
    m.Return(m.Emit(mdpi.mul_op, m.Parameter(0), n));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(mdpi.multiply_neg_arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorIntDPWithIntMulTest,
                         ::testing::ValuesIn(kMulDPInstructions));

#if V8_ENABLE_WEBASSEMBLY

TEST_F(TurboshaftInstructionSelectorTest, AddReduce) {
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Simd128());
    V<Simd128> reduce = m.I8x16AddReduce(m.Parameter(0));
    m.Return(reduce);
    Stream s = m.Build();
    EXPECT_EQ(kArm64I8x16Addv, s[0]->arch_opcode());
    EXPECT_EQ(1U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Simd128());
    V<Simd128> reduce = m.I16x8AddReduce(m.Parameter(0));
    m.Return(reduce);
    Stream s = m.Build();
    EXPECT_EQ(kArm64I16x8Addv, s[0]->arch_opcode());
    EXPECT_EQ(1U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Simd128());
    V<Simd128> reduce = m.I32x4AddReduce(m.Parameter(0));
    m.Return(reduce);
    Stream s = m.Build();
    EXPECT_EQ(kArm64I32x4Addv, s[0]->arch_opcode());
    EXPECT_EQ(1U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Simd128());
    V<Simd128> reduce = m.I64x2AddReduce(m.Parameter(0));
    m.Return(reduce);
    Stream s = m.Build();
    EXPECT_EQ(kArm64I64x2AddPair, s[0]->arch_opcode());
    EXPECT_EQ(1U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  {
    StreamBuilder m(this, MachineType::Float32(), MachineType::Simd128());
    V<Simd128> reduce = m.F32x4AddReduce(m.Parameter(0));
    m.Return(reduce);
    Stream s = m.Build();
    EXPECT_EQ(kArm64F32x4AddReducePairwise, s[0]->arch_opcode());
    EXPECT_EQ(1U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  {
    StreamBuilder m(this, MachineType::Float64(), MachineType::Simd128());
    V<Simd128> reduce = m.F64x2AddReduce(m.Parameter(0));
    m.Return(reduce);
    Stream s = m.Build();
    EXPECT_EQ(kArm64F64x2AddPair, s[0]->arch_opcode());
    EXPECT_EQ(1U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

namespace {

struct SIMDMulDPInst {
  const char* mul_constructor_name;
  TSBinop mul_operator;
  TSBinop add_operator;
  TSBinop sub_operator;
  ArchOpcode multiply_add_arch_opcode;
  ArchOpcode multiply_sub_arch_opcode;
  MachineType machine_type;
  const int lane_size;
};

std::ostream& operator<<(std::ostream& os, const SIMDMulDPInst& inst) {
  return os << inst.mul_constructor_name;
}

}  // namespace

static const SIMDMulDPInst kSIMDMulDPInstructions[] = {
    {"I32x4Mul", TSBinop::kI32x4Mul, TSBinop::kI32x4Add, TSBinop::kI32x4Sub,
     kArm64Mla, kArm64Mls, MachineType::Simd128(), 32},
    {"I16x8Mul", TSBinop::kI16x8Mul, TSBinop::kI16x8Add, TSBinop::kI16x8Sub,
     kArm64Mla, kArm64Mls, MachineType::Simd128(), 16}};

using TurboshaftInstructionSelectorSIMDDPWithSIMDMulTest =
    TurboshaftInstructionSelectorTestWithParam<SIMDMulDPInst>;

TEST_P(TurboshaftInstructionSelectorSIMDDPWithSIMDMulTest, AddWithMul) {
  const SIMDMulDPInst mdpi = GetParam();
  const MachineType type = mdpi.machine_type;
  {
    StreamBuilder m(this, type, type, type, type);
    OpIndex n = m.Emit(mdpi.mul_operator, m.Parameter(1), m.Parameter(2));
    m.Return(m.Emit(mdpi.add_operator, m.Parameter(0), n));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(mdpi.multiply_add_arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(mdpi.lane_size, LaneSizeField::decode(s[0]->opcode()));
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  {
    StreamBuilder m(this, type, type, type, type);
    OpIndex n = m.Emit(mdpi.mul_operator, m.Parameter(0), m.Parameter(1));
    m.Return(m.Emit(mdpi.add_operator, n, m.Parameter(2)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(mdpi.multiply_add_arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(mdpi.lane_size, LaneSizeField::decode(s[0]->opcode()));
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_P(TurboshaftInstructionSelectorSIMDDPWithSIMDMulTest, SubWithMul) {
  const SIMDMulDPInst mdpi = GetParam();
  const MachineType type = mdpi.machine_type;
  {
    StreamBuilder m(this, type, type, type, type);
    OpIndex n = m.Emit(mdpi.mul_operator, m.Parameter(1), m.Parameter(2));
    m.Return(m.Emit(mdpi.sub_operator, m.Parameter(0), n));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(mdpi.multiply_sub_arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(mdpi.lane_size, LaneSizeField::decode(s[0]->opcode()));
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorSIMDDPWithSIMDMulTest,
                         ::testing::ValuesIn(kSIMDMulDPInstructions));

namespace {

struct SIMDShrAddInst {
  const char* shradd_constructor_name;
  TSBinop shr_s_operator;
  TSBinop shr_u_operator;
  TSBinop add_operator;
  const int laneSize;
};

std::ostream& operator<<(std::ostream& os, const SIMDShrAddInst& inst) {
  return os << inst.shradd_constructor_name;
}

}  // namespace

static const SIMDShrAddInst kSIMDShrAddInstructions[] = {
    {"I64x2ShrAdd", TSBinop::kI64x2ShrS, TSBinop::kI64x2ShrU,
     TSBinop::kI64x2Add, 64},
    {"I32x4ShrAdd", TSBinop::kI32x4ShrS, TSBinop::kI32x4ShrU,
     TSBinop::kI32x4Add, 32},
    {"I16x8ShrAdd", TSBinop::kI16x8ShrS, TSBinop::kI16x8ShrU,
     TSBinop::kI16x8Add, 16},
    {"I8x16ShrAdd", TSBinop::kI8x16ShrS, TSBinop::kI8x16ShrU,
     TSBinop::kI8x16Add, 8}};

using TurboshaftInstructionSelectorSIMDShrAddTest =
    TurboshaftInstructionSelectorTestWithParam<SIMDShrAddInst>;

TEST_P(TurboshaftInstructionSelectorSIMDShrAddTest, ShrAddS) {
  const SIMDShrAddInst param = GetParam();
  const MachineType type = MachineType::Simd128();
  {
    StreamBuilder m(this, type, type, type);
    OpIndex n =
        m.Emit(param.shr_s_operator, m.Parameter(1), m.Int32Constant(1));
    m.Return(m.Emit(param.add_operator, m.Parameter(0), n));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Ssra, s[0]->arch_opcode());
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(param.laneSize, LaneSizeField::decode(s[0]->opcode()));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  {
    StreamBuilder m(this, type, type, type);
    OpIndex n =
        m.Emit(param.shr_s_operator, m.Parameter(0), m.Int32Constant(1));
    m.Return(m.Emit(param.add_operator, n, m.Parameter(1)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Ssra, s[0]->arch_opcode());
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(param.laneSize, LaneSizeField::decode(s[0]->opcode()));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_P(TurboshaftInstructionSelectorSIMDShrAddTest, ShrAddU) {
  const SIMDShrAddInst param = GetParam();
  const MachineType type = MachineType::Simd128();
  {
    StreamBuilder m(this, type, type, type);
    OpIndex n =
        m.Emit(param.shr_u_operator, m.Parameter(1), m.Int32Constant(1));
    m.Return(m.Emit(param.add_operator, m.Parameter(0), n));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Usra, s[0]->arch_opcode());
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(param.laneSize, LaneSizeField::decode(s[0]->opcode()));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  {
    StreamBuilder m(this, type, type, type);
    OpIndex n =
        m.Emit(param.shr_u_operator, m.Parameter(0), m.Int32Constant(1));
    m.Return(m.Emit(param.add_operator, n, m.Parameter(1)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Usra, s[0]->arch_opcode());
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(param.laneSize, LaneSizeField::decode(s[0]->opcode()));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorSIMDShrAddTest,
                         ::testing::ValuesIn(kSIMDShrAddInstructions));

namespace {
struct SIMDAddExtMulInst {
  const char* mul_constructor_name;
  TSBinop mul_operator;
  TSBinop add_operator;
  ArchOpcode multiply_add_arch_opcode;
  MachineType machine_type;
  int lane_size;
};
}  // namespace

static const SIMDAddExtMulInst kSimdAddExtMulInstructions[] = {
    {"I16x8ExtMulLowI8x16S", TSBinop::kI16x8ExtMulLowI8x16S, TSBinop::kI16x8Add,
     kArm64Smlal, MachineType::Simd128(), 16},
    {"I16x8ExtMulHighI8x16S", TSBinop::kI16x8ExtMulHighI8x16S,
     TSBinop::kI16x8Add, kArm64Smlal2, MachineType::Simd128(), 16},
    {"I16x8ExtMulLowI8x16U", TSBinop::kI16x8ExtMulLowI8x16U, TSBinop::kI16x8Add,
     kArm64Umlal, MachineType::Simd128(), 16},
    {"I16x8ExtMulHighI8x16U", TSBinop::kI16x8ExtMulHighI8x16U,
     TSBinop::kI16x8Add, kArm64Umlal2, MachineType::Simd128(), 16},
    {"I32x4ExtMulLowI16x8S", TSBinop::kI32x4ExtMulLowI16x8S, TSBinop::kI32x4Add,
     kArm64Smlal, MachineType::Simd128(), 32},
    {"I32x4ExtMulHighI16x8S", TSBinop::kI32x4ExtMulHighI16x8S,
     TSBinop::kI32x4Add, kArm64Smlal2, MachineType::Simd128(), 32},
    {"I32x4ExtMulLowI16x8U", TSBinop::kI32x4ExtMulLowI16x8U, TSBinop::kI32x4Add,
     kArm64Umlal, MachineType::Simd128(), 32},
    {"I32x4ExtMulHighI16x8U", TSBinop::kI32x4ExtMulHighI16x8U,
     TSBinop::kI32x4Add, kArm64Umlal2, MachineType::Simd128(), 32}};

using TurboshaftInstructionSelectorSIMDAddExtMulTest =
    TurboshaftInstructionSelectorTestWithParam<SIMDAddExtMulInst>;

// TODO(zhin): This can be merged with InstructionSelectorSIMDDPWithSIMDMulTest
// once sub+extmul matching is implemented.
TEST_P(TurboshaftInstructionSelectorSIMDAddExtMulTest, AddExtMul) {
  const SIMDAddExtMulInst mdpi = GetParam();
  const MachineType type = mdpi.machine_type;
  {
    // Test Add(x, ExtMul(y, z)).
    StreamBuilder m(this, type, type, type, type);
    OpIndex n = m.Emit(mdpi.mul_operator, m.Parameter(1), m.Parameter(2));
    m.Return(m.Emit(mdpi.add_operator, m.Parameter(0), n));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(mdpi.multiply_add_arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(mdpi.lane_size, LaneSizeField::decode(s[0]->opcode()));
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  {
    // Test Add(ExtMul(y, z), x), making sure it's commutative.
    StreamBuilder m(this, type, type, type, type);
    OpIndex n = m.Emit(mdpi.mul_operator, m.Parameter(0), m.Parameter(1));
    m.Return(m.Emit(mdpi.add_operator, n, m.Parameter(2)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(mdpi.multiply_add_arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(mdpi.lane_size, LaneSizeField::decode(s[0]->opcode()));
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorSIMDAddExtMulTest,
                         ::testing::ValuesIn(kSimdAddExtMulInstructions));

struct SIMDMulDupInst {
  const uint8_t shuffle[16];
  int32_t lane;
  int shuffle_input_index;
};

const SIMDMulDupInst kSIMDF32x4MulDuplInstructions[] = {
    {
        {0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3},
        0,
        0,
    },
    {
        {4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7},
        1,
        0,
    },
    {
        {8, 9, 10, 11, 8, 9, 10, 11, 8, 9, 10, 11, 8, 9, 10, 11},
        2,
        0,
    },
    {
        {12, 13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15},
        3,
        0,
    },
    {
        {16, 17, 18, 19, 16, 17, 18, 19, 16, 17, 18, 19, 16, 17, 18, 19},
        0,
        1,
    },
    {
        {20, 21, 22, 23, 20, 21, 22, 23, 20, 21, 22, 23, 20, 21, 22, 23},
        1,
        1,
    },
    {
        {24, 25, 26, 27, 24, 25, 26, 27, 24, 25, 26, 27, 24, 25, 26, 27},
        2,
        1,
    },
    {
        {28, 29, 30, 31, 28, 29, 30, 31, 28, 29, 30, 31, 28, 29, 30, 31},
        3,
        1,
    },
};

using TurboshaftInstructionSelectorSimdF32x4MulWithDupTest =
    TurboshaftInstructionSelectorTestWithParam<SIMDMulDupInst>;

TEST_P(TurboshaftInstructionSelectorSimdF32x4MulWithDupTest, MulWithDup) {
  const SIMDMulDupInst param = GetParam();
  const MachineType type = MachineType::Simd128();
  {
    StreamBuilder m(this, type, type, type, type);
    OpIndex shuffle =
        m.Simd128Shuffle(m.Parameter(0), m.Parameter(1), param.shuffle);
    m.Return(m.F32x4Mul(m.Parameter(2), shuffle));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64FMulElement, s[0]->arch_opcode());
    EXPECT_EQ(32, LaneSizeField::decode(s[0]->opcode()));
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(param.lane, s.ToInt32(s[0]->InputAt(2)));
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(m.Parameter(param.shuffle_input_index)),
              s.ToVreg(s[0]->InputAt(1)));
  }

  // Multiplication operator should be commutative, so test shuffle op as lhs.
  {
    StreamBuilder m(this, type, type, type, type);
    OpIndex shuffle =
        m.Simd128Shuffle(m.Parameter(0), m.Parameter(1), param.shuffle);
    m.Return(m.F32x4Mul(shuffle, m.Parameter(2)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64FMulElement, s[0]->arch_opcode());
    EXPECT_EQ(32, LaneSizeField::decode(s[0]->opcode()));
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(param.lane, s.ToInt32(s[0]->InputAt(2)));
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(m.Parameter(param.shuffle_input_index)),
              s.ToVreg(s[0]->InputAt(1)));
  }
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorSimdF32x4MulWithDupTest,
                         ::testing::ValuesIn(kSIMDF32x4MulDuplInstructions));

TEST_F(TurboshaftInstructionSelectorTest, SimdF32x4MulWithDupNegativeTest) {
  const MachineType type = MachineType::Simd128();
  // Check that optimization does not match when the shuffle is not a f32x4.dup.
  const uint8_t mask[kSimd128Size] = {0};
  {
    StreamBuilder m(this, type, type, type, type);
    OpIndex shuffle = m.Simd128Shuffle(m.Parameter(0), m.Parameter(1), mask);
    m.Return(m.F32x4Mul(m.Parameter(2), shuffle));
    Stream s = m.Build();
    ASSERT_EQ(2U, s.size());
    // The shuffle is a i8x16.dup of lane 0.
    EXPECT_EQ(kArm64S128Dup, s[0]->arch_opcode());
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(kArm64FMul, s[1]->arch_opcode());
    EXPECT_EQ(32, LaneSizeField::decode(s[1]->opcode()));
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(2U, s[1]->InputCount());
    EXPECT_EQ(1U, s[1]->OutputCount());
  }
}

const SIMDMulDupInst kSIMDF64x2MulDuplInstructions[] = {
    {
        {0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7},
        0,
        0,
    },
    {
        {8, 9, 10, 11, 12, 13, 14, 15, 8, 9, 10, 11, 12, 13, 14, 15},
        1,
        0,
    },
    {
        {16, 17, 18, 19, 20, 21, 22, 23, 16, 17, 18, 19, 20, 21, 22, 23},
        0,
        1,
    },
    {
        {24, 25, 26, 27, 28, 29, 30, 31, 24, 25, 26, 27, 28, 29, 30, 31},
        1,
        1,
    },
};

using TurboshaftInstructionSelectorSimdF64x2MulWithDupTest =
    TurboshaftInstructionSelectorTestWithParam<SIMDMulDupInst>;

TEST_P(TurboshaftInstructionSelectorSimdF64x2MulWithDupTest, MulWithDup) {
  const SIMDMulDupInst param = GetParam();
  const MachineType type = MachineType::Simd128();
  {
    StreamBuilder m(this, type, type, type, type);
    OpIndex shuffle =
        m.Simd128Shuffle(m.Parameter(0), m.Parameter(1), param.shuffle);
    m.Return(m.F64x2Mul(m.Parameter(2), shuffle));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64FMulElement, s[0]->arch_opcode());
    EXPECT_EQ(64, LaneSizeField::decode(s[0]->opcode()));
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(param.lane, s.ToInt32(s[0]->InputAt(2)));
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(m.Parameter(param.shuffle_input_index)),
              s.ToVreg(s[0]->InputAt(1)));
  }

  // Multiplication operator should be commutative, so test shuffle op as lhs.
  {
    StreamBuilder m(this, type, type, type, type);
    OpIndex shuffle =
        m.Simd128Shuffle(m.Parameter(0), m.Parameter(1), param.shuffle);
    m.Return(m.F64x2Mul(shuffle, m.Parameter(2)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64FMulElement, s[0]->arch_opcode());
    EXPECT_EQ(64, LaneSizeField::decode(s[0]->opcode()));
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(param.lane, s.ToInt32(s[0]->InputAt(2)));
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(m.Parameter(param.shuffle_input_index)),
              s.ToVreg(s[0]->InputAt(1)));
  }
}

TEST_F(TurboshaftInstructionSelectorTest, ReverseShuffle32x4Test) {
  const MachineType type = MachineType::Simd128();
  {
    const uint8_t shuffle[] = {
      12, 13, 14, 15,
      8, 9, 10, 11,
      4, 5, 6, 7,
      0, 1, 2, 3
    };
    StreamBuilder m(this, type, type, type, type);
    m.Return(m.Simd128Shuffle(m.Parameter(0), m.Parameter(1), shuffle));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64S32x4Reverse, s[0]->arch_opcode());
    EXPECT_EQ(s.ToVreg(m.Parameter(0)), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  {
    const uint8_t shuffle[] = {
      28, 29, 30, 31,
      24, 25, 26, 27,
      20, 21, 22, 23,
      16, 17, 18, 19
    };
    StreamBuilder m(this, type, type, type, type);
    m.Return(m.Simd128Shuffle(m.Parameter(0), m.Parameter(1), shuffle));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64S32x4Reverse, s[0]->arch_opcode());
    EXPECT_EQ(s.ToVreg(m.Parameter(1)), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorSimdF64x2MulWithDupTest,
                         ::testing::ValuesIn(kSIMDF64x2MulDuplInstructions));

TEST_F(TurboshaftInstructionSelectorTest, SimdF64x2MulWithDupNegativeTest) {
  const MachineType type = MachineType::Simd128();
  // Check that optimization does not match when the shuffle is not a f64x2.dup.
  const uint8_t mask[kSimd128Size] = {0};
  {
    StreamBuilder m(this, type, type, type, type);
    OpIndex shuffle = m.Simd128Shuffle(m.Parameter(0), m.Parameter(1), mask);
    m.Return(m.F64x2Mul(m.Parameter(2), shuffle));
    Stream s = m.Build();
    ASSERT_EQ(2U, s.size());
    // The shuffle is a i8x16.dup of lane 0.
    EXPECT_EQ(kArm64S128Dup, s[0]->arch_opcode());
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(kArm64FMul, s[1]->arch_opcode());
    EXPECT_EQ(64, LaneSizeField::decode(s[1]->opcode()));
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(2U, s[1]->InputCount());
    EXPECT_EQ(1U, s[1]->OutputCount());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, OneLaneSwizzle32x4Test) {
  const MachineType type = MachineType::Simd128();
  {
    const uint8_t shuffle[] = {
      0, 1, 2, 3,
      4, 5, 6, 7,
      4, 5, 6, 7,
      12, 13, 14, 15
    };
    StreamBuilder m(this, type, type, type, type);
    m.Return(m.Simd128Shuffle(m.Parameter(0), m.Parameter(1), shuffle));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64S32x4OneLaneSwizzle, s[0]->arch_opcode());
    EXPECT_EQ(s.ToVreg(m.Parameter(0)), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  {
    const uint8_t shuffle[] = {
      16, 17, 18, 19,
      20, 21, 22, 23,
      24, 25, 26, 27,
      16, 17, 18, 19
    };
    StreamBuilder m(this, type, type, type, type);
    m.Return(m.Simd128Shuffle(m.Parameter(0), m.Parameter(1), shuffle));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64S32x4OneLaneSwizzle, s[0]->arch_opcode());
    EXPECT_EQ(s.ToVreg(m.Parameter(1)), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

#endif  // V8_ENABLE_WEBASSEMBLY

TEST_F(TurboshaftInstructionSelectorTest, Word32MulWithImmediate) {
  // x * (2^k + 1) -> x + (x << k)
  TRACED_FORRANGE(int32_t, k, 1, 30) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(m.Word32Mul(m.Parameter(0), m.Int32Constant((1 << k) + 1)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Add32, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(s[0]->InputAt(0)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(k, s.ToInt32(s[0]->InputAt(2)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  // (2^k + 1) * x -> x + (x << k)
  TRACED_FORRANGE(int32_t, k, 1, 30) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(m.Word32Mul(m.Int32Constant((1 << k) + 1), m.Parameter(0)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Add32, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(s[0]->InputAt(0)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(k, s.ToInt32(s[0]->InputAt(2)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  // x * (2^k + 1) + c -> x + (x << k) + c
  TRACED_FORRANGE(int32_t, k, 1, 30) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    m.Return(
        m.Word32Add(m.Word32Mul(m.Parameter(0), m.Int32Constant((1 << k) + 1)),
                    m.Parameter(1)));
    Stream s = m.Build();
    ASSERT_EQ(2U, s.size());
    EXPECT_EQ(kArm64Add32, s[0]->arch_opcode());
    EXPECT_EQ(kArm64Add32, s[1]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(s[0]->InputAt(0)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(k, s.ToInt32(s[0]->InputAt(2)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  // (2^k + 1) * x + c -> x + (x << k) + c
  TRACED_FORRANGE(int32_t, k, 1, 30) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    m.Return(
        m.Word32Add(m.Word32Mul(m.Int32Constant((1 << k) + 1), m.Parameter(0)),
                    m.Parameter(1)));
    Stream s = m.Build();
    ASSERT_EQ(2U, s.size());
    EXPECT_EQ(kArm64Add32, s[0]->arch_opcode());
    EXPECT_EQ(kArm64Add32, s[1]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(s[0]->InputAt(0)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(k, s.ToInt32(s[0]->InputAt(2)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  // c + x * (2^k + 1) -> c + x + (x << k)
  TRACED_FORRANGE(int32_t, k, 1, 30) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    m.Return(m.Word32Add(
        m.Parameter(0),
        m.Word32Mul(m.Parameter(1), m.Int32Constant((1 << k) + 1))));
    Stream s = m.Build();
    ASSERT_EQ(2U, s.size());
    EXPECT_EQ(kArm64Add32, s[0]->arch_opcode());
    EXPECT_EQ(kArm64Add32, s[1]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(s[0]->InputAt(1)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(k, s.ToInt32(s[0]->InputAt(2)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  // c + (2^k + 1) * x -> c + x + (x << k)
  TRACED_FORRANGE(int32_t, k, 1, 30) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    m.Return(m.Word32Add(
        m.Parameter(0),
        m.Word32Mul(m.Int32Constant((1 << k) + 1), m.Parameter(1))));
    Stream s = m.Build();
    ASSERT_EQ(2U, s.size());
    EXPECT_EQ(kArm64Add32, s[0]->arch_opcode());
    EXPECT_EQ(kArm64Add32, s[1]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(s[0]->InputAt(1)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(k, s.ToInt32(s[0]->InputAt(2)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  // c - x * (2^k + 1) -> c - x + (x << k)
  TRACED_FORRANGE(int32_t, k, 1, 30) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    m.Return(m.Word32Sub(
        m.Parameter(0),
        m.Word32Mul(m.Parameter(1), m.Int32Constant((1 << k) + 1))));
    Stream s = m.Build();
    ASSERT_EQ(2U, s.size());
    EXPECT_EQ(kArm64Add32, s[0]->arch_opcode());
    EXPECT_EQ(kArm64Sub32, s[1]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(s[0]->InputAt(1)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(k, s.ToInt32(s[0]->InputAt(2)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  // c - (2^k + 1) * x -> c - x + (x << k)
  TRACED_FORRANGE(int32_t, k, 1, 30) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    m.Return(m.Word32Sub(
        m.Parameter(0),
        m.Word32Mul(m.Int32Constant((1 << k) + 1), m.Parameter(1))));
    Stream s = m.Build();
    ASSERT_EQ(2U, s.size());
    EXPECT_EQ(kArm64Add32, s[0]->arch_opcode());
    EXPECT_EQ(kArm64Sub32, s[1]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(s[0]->InputAt(1)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(k, s.ToInt32(s[0]->InputAt(2)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, Word64MulWithImmediate) {
  // x * (2^k + 1) -> x + (x << k)
  TRACED_FORRANGE(int64_t, k, 1, 62) {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int64());
    m.Return(
        m.Word64Mul(m.Parameter(0), m.Int64Constant((int64_t{1} << k) + 1)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Add, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(s[0]->InputAt(0)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(k, s.ToInt64(s[0]->InputAt(2)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  // (2^k + 1) * x -> x + (x << k)
  TRACED_FORRANGE(int64_t, k, 1, 62) {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int64());
    m.Return(
        m.Word64Mul(m.Int64Constant((int64_t{1} << k) + 1), m.Parameter(0)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Add, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(s[0]->InputAt(0)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(k, s.ToInt64(s[0]->InputAt(2)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  // x * (2^k + 1) + c -> x + (x << k) + c
  TRACED_FORRANGE(int64_t, k, 1, 62) {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int64(),
                    MachineType::Int64());
    m.Return(m.Word64Add(
        m.Word64Mul(m.Parameter(0), m.Int64Constant((int64_t{1} << k) + 1)),
        m.Parameter(1)));
    Stream s = m.Build();
    ASSERT_EQ(2U, s.size());
    EXPECT_EQ(kArm64Add, s[0]->arch_opcode());
    EXPECT_EQ(kArm64Add, s[1]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(s[0]->InputAt(0)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(k, s.ToInt64(s[0]->InputAt(2)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  // (2^k + 1) * x + c -> x + (x << k) + c
  TRACED_FORRANGE(int64_t, k, 1, 62) {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int64(),
                    MachineType::Int64());
    m.Return(m.Word64Add(
        m.Word64Mul(m.Int64Constant((int64_t{1} << k) + 1), m.Parameter(0)),
        m.Parameter(1)));
    Stream s = m.Build();
    ASSERT_EQ(2U, s.size());
    EXPECT_EQ(kArm64Add, s[0]->arch_opcode());
    EXPECT_EQ(kArm64Add, s[1]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(s[0]->InputAt(0)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(k, s.ToInt64(s[0]->InputAt(2)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  // c + x * (2^k + 1) -> c + x + (x << k)
  TRACED_FORRANGE(int64_t, k, 1, 62) {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int64(),
                    MachineType::Int64());
    m.Return(m.Word64Add(
        m.Parameter(0),
        m.Word64Mul(m.Parameter(1), m.Int64Constant((int64_t{1} << k) + 1))));
    Stream s = m.Build();
    ASSERT_EQ(2U, s.size());
    EXPECT_EQ(kArm64Add, s[0]->arch_opcode());
    EXPECT_EQ(kArm64Add, s[1]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(s[0]->InputAt(0)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(k, s.ToInt64(s[0]->InputAt(2)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  // c + (2^k + 1) * x -> c + x + (x << k)
  TRACED_FORRANGE(int64_t, k, 1, 62) {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int64(),
                    MachineType::Int64());
    m.Return(m.Word64Add(
        m.Parameter(0),
        m.Word64Mul(m.Int64Constant((int64_t{1} << k) + 1), m.Parameter(1))));
    Stream s = m.Build();
    ASSERT_EQ(2U, s.size());
    EXPECT_EQ(kArm64Add, s[0]->arch_opcode());
    EXPECT_EQ(kArm64Add, s[1]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(s[0]->InputAt(0)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(k, s.ToInt64(s[0]->InputAt(2)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  // c - x * (2^k + 1) -> c - x + (x << k)
  TRACED_FORRANGE(int64_t, k, 1, 62) {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int64(),
                    MachineType::Int64());
    m.Return(m.Word64Sub(
        m.Parameter(0),
        m.Word64Mul(m.Parameter(1), m.Int64Constant((int64_t{1} << k) + 1))));
    Stream s = m.Build();
    ASSERT_EQ(2U, s.size());
    EXPECT_EQ(kArm64Add, s[0]->arch_opcode());
    EXPECT_EQ(kArm64Sub, s[1]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(s[0]->InputAt(0)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(k, s.ToInt64(s[0]->InputAt(2)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  // c - (2^k + 1) * x -> c - x + (x << k)
  TRACED_FORRANGE(int64_t, k, 1, 62) {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int64(),
                    MachineType::Int64());
    m.Return(m.Word64Sub(
        m.Parameter(0),
        m.Word64Mul(m.Int64Constant((int64_t{1} << k) + 1), m.Parameter(1))));
    Stream s = m.Build();
    ASSERT_EQ(2U, s.size());
    EXPECT_EQ(kArm64Add, s[0]->arch_opcode());
    EXPECT_EQ(kArm64Sub, s[1]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(s[0]->InputAt(0)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(k, s.ToInt64(s[0]->InputAt(2)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

// -----------------------------------------------------------------------------
// Floating point instructions.

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

TEST_P(TurboshaftInstructionSelectorFPCmpTest, WithImmediateZeroOnRight) {
  const FPCmp cmp = GetParam();
  StreamBuilder m(this, MachineType::Int32(), cmp.mi.machine_type);
  if (cmp.mi.machine_type == MachineType::Float64()) {
    m.Return(m.Emit(cmp.mi.op, m.Parameter(0), m.Float64Constant(0.0)));
  } else {
    m.Return(m.Emit(cmp.mi.op, m.Parameter(0), m.Float32Constant(0.0f)));
  }
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(cmp.mi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_TRUE(s[0]->InputAt(1)->IsImmediate());
  EXPECT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_set, s[0]->flags_mode());
  EXPECT_EQ(cmp.cond, s[0]->flags_condition());
}

TEST_P(TurboshaftInstructionSelectorFPCmpTest, WithImmediateZeroOnLeft) {
  const FPCmp cmp = GetParam();
  StreamBuilder m(this, MachineType::Int32(), cmp.mi.machine_type);
  if (cmp.mi.machine_type == MachineType::Float64()) {
    m.Return(m.Emit(cmp.mi.op, m.Float64Constant(0.0), m.Parameter(0)));
  } else {
    m.Return(m.Emit(cmp.mi.op, m.Float32Constant(0.0f), m.Parameter(0)));
  }
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(cmp.mi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_TRUE(s[0]->InputAt(1)->IsImmediate());
  EXPECT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_set, s[0]->flags_mode());
  EXPECT_EQ(cmp.commuted_cond, s[0]->flags_condition());
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorFPCmpTest,
                         ::testing::ValuesIn(kFPCmpInstructions));

TEST_F(TurboshaftInstructionSelectorTest, Float32SelectWithRegisters) {
  StreamBuilder m(this, MachineType::Float32(), MachineType::Float32(),
                  MachineType::Float32());
  OpIndex cond = m.Int32Constant(1);
  m.Return(m.Float32Select(cond, m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArm64Tst32, s[0]->arch_opcode());
  EXPECT_EQ(4U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_select, s[0]->flags_mode());
  EXPECT_EQ(kNotEqual, s[0]->flags_condition());
}

TEST_F(TurboshaftInstructionSelectorTest, Float32SelectWithZero) {
  StreamBuilder m(this, MachineType::Float32(), MachineType::Float32());
  OpIndex cond = m.Int32Constant(1);
  m.Return(m.Float32Select(cond, m.Parameter(0), m.Float32Constant(0.0f)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArm64Tst32, s[0]->arch_opcode());
  EXPECT_EQ(4U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
  EXPECT_TRUE(s[0]->InputAt(3)->IsImmediate());
  EXPECT_EQ(kFlags_select, s[0]->flags_mode());
  EXPECT_EQ(kNotEqual, s[0]->flags_condition());
}

TEST_F(TurboshaftInstructionSelectorTest, Float64SelectWithRegisters) {
  StreamBuilder m(this, MachineType::Float64(), MachineType::Float64(),
                  MachineType::Float64());
  OpIndex cond = m.Int32Constant(1);
  m.Return(m.Float64Select(cond, m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArm64Tst32, s[0]->arch_opcode());
  EXPECT_EQ(4U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_select, s[0]->flags_mode());
  EXPECT_EQ(kNotEqual, s[0]->flags_condition());
}

TEST_F(TurboshaftInstructionSelectorTest, Float64SelectWithZero) {
  StreamBuilder m(this, MachineType::Float64(), MachineType::Float64());
  OpIndex cond = m.Int32Constant(1);
  m.Return(m.Float64Select(cond, m.Parameter(0), m.Float64Constant(0.0f)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArm64Tst32, s[0]->arch_opcode());
  EXPECT_EQ(4U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
  EXPECT_TRUE(s[0]->InputAt(3)->IsImmediate());
  EXPECT_EQ(kFlags_select, s[0]->flags_mode());
  EXPECT_EQ(kNotEqual, s[0]->flags_condition());
}

TEST_F(TurboshaftInstructionSelectorTest, Word32SelectWithRegisters) {
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  OpIndex cond = m.Int32Constant(1);
  m.Return(m.Word32Select(cond, m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArm64Tst32, s[0]->arch_opcode());
  EXPECT_EQ(4U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_select, s[0]->flags_mode());
  EXPECT_EQ(kNotEqual, s[0]->flags_condition());
}

TEST_F(TurboshaftInstructionSelectorTest, Word32SelectWithZero) {
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
  OpIndex cond = m.Int32Constant(1);
  m.Return(m.Word32Select(cond, m.Parameter(0), m.Int32Constant(0)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArm64Tst32, s[0]->arch_opcode());
  EXPECT_EQ(4U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
  EXPECT_TRUE(s[0]->InputAt(3)->IsImmediate());
  EXPECT_EQ(kFlags_select, s[0]->flags_mode());
  EXPECT_EQ(kNotEqual, s[0]->flags_condition());
}

TEST_F(TurboshaftInstructionSelectorTest, Word64SelectWithRegisters) {
  StreamBuilder m(this, MachineType::Int64(), MachineType::Int64(),
                  MachineType::Int64());
  OpIndex cond = m.Int32Constant(1);
  m.Return(m.Word64Select(cond, m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArm64Tst32, s[0]->arch_opcode());
  EXPECT_EQ(4U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_select, s[0]->flags_mode());
  EXPECT_EQ(kNotEqual, s[0]->flags_condition());
}

TEST_F(TurboshaftInstructionSelectorTest, Word64SelectWithZero) {
  StreamBuilder m(this, MachineType::Int64(), MachineType::Int64());
  OpIndex cond = m.Int32Constant(1);
  m.Return(m.Word64Select(cond, m.Parameter(0), m.Int64Constant(0)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArm64Tst32, s[0]->arch_opcode());
  EXPECT_EQ(4U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
  EXPECT_TRUE(s[0]->InputAt(3)->IsImmediate());
  EXPECT_EQ(kFlags_select, s[0]->flags_mode());
  EXPECT_EQ(kNotEqual, s[0]->flags_condition());
}

// -----------------------------------------------------------------------------
// Conversions.

using TurboshaftInstructionSelectorConversionTest =
    TurboshaftInstructionSelectorTestWithParam<Conversion>;

TEST_P(TurboshaftInstructionSelectorConversionTest, Parameter) {
  const Conversion conv = GetParam();
  StreamBuilder m(this, conv.mi.machine_type, conv.src_machine_type);
  m.Return(m.Emit(conv.mi.op, m.Parameter(0)));
  Stream s = m.Build();
  if (conv.mi.arch_opcode == kArchNop) {
    ASSERT_EQ(0U, s.size());
    return;
  }
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(conv.mi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(1U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorConversionTest,
                         ::testing::ValuesIn(kConversionInstructions));

using TurboshaftInstructionSelectorElidedChangeUint32ToUint64Test =
    TurboshaftInstructionSelectorTestWithParam<MachInst2>;

TEST_P(TurboshaftInstructionSelectorElidedChangeUint32ToUint64Test, Parameter) {
  const MachInst2 binop = GetParam();
  StreamBuilder m(this, MachineType::Uint64(), binop.machine_type,
                  binop.machine_type);
  m.Return(
      m.ChangeUint32ToUint64(m.Emit(binop.op, m.Parameter(0), m.Parameter(1))));
  Stream s = m.Build();
  // Make sure the `ChangeUint32ToUint64` node turned into a no-op.
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(binop.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}

INSTANTIATE_TEST_SUITE_P(
    TurboshaftInstructionSelectorTest,
    TurboshaftInstructionSelectorElidedChangeUint32ToUint64Test,
    ::testing::ValuesIn(kCanElideChangeUint32ToUint64));

using TurboshaftInstructionSelectorElidedChangeUint32ToUint64MultiOutputTest =
    TurboshaftInstructionSelectorTestWithParam<MachInst2>;

TEST_P(TurboshaftInstructionSelectorElidedChangeUint32ToUint64MultiOutputTest,
       Parameter) {
  const MachInst2 binop = GetParam();
  StreamBuilder m(this, MachineType::Uint64(), binop.machine_type,
                  binop.machine_type);
  m.Return(m.ChangeUint32ToUint64(
      m.Projection(m.Emit(binop.op, m.Parameter(0), m.Parameter(1)), 0)));
  Stream s = m.Build();
  // Make sure the `ChangeUint32ToUint64` node turned into a no-op.
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(binop.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}

INSTANTIATE_TEST_SUITE_P(
    TurboshaftInstructionSelectorTest,
    TurboshaftInstructionSelectorElidedChangeUint32ToUint64MultiOutputTest,
    ::testing::ValuesIn(kCanElideChangeUint32ToUint64MultiOutput));

TEST_F(TurboshaftInstructionSelectorTest, ChangeUint32ToUint64AfterLoad) {
  // For each case, make sure the `ChangeUint32ToUint64` node turned into a
  // no-op.

  // Ldrb
  {
    StreamBuilder m(this, MachineType::Uint64(), MachineType::Pointer(),
                    MachineType::Pointer());
    m.Return(m.ChangeUint32ToUint64(
        m.Load(MachineType::Uint8(), m.Parameter(0), m.Parameter(1))));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Ldrb, s[0]->arch_opcode());
    EXPECT_EQ(kMode_MRR, s[0]->addressing_mode());
    EXPECT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  // Ldrh
  {
    StreamBuilder m(this, MachineType::Uint64(), MachineType::Pointer(),
                    MachineType::Pointer());
    m.Return(m.ChangeUint32ToUint64(
        m.Load(MachineType::Uint16(), m.Parameter(0), m.Parameter(1))));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Ldrh, s[0]->arch_opcode());
    EXPECT_EQ(kMode_MRR, s[0]->addressing_mode());
    EXPECT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  // LdrW
  {
    StreamBuilder m(this, MachineType::Uint64(), MachineType::Pointer(),
                    MachineType::Pointer());
    m.Return(m.ChangeUint32ToUint64(
        m.Load(MachineType::Uint32(), m.Parameter(0), m.Parameter(1))));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64LdrW, s[0]->arch_opcode());
    EXPECT_EQ(kMode_MRR, s[0]->addressing_mode());
    EXPECT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, ChangeInt32ToInt64AfterLoad) {
  // For each case, test that the conversion is merged into the load
  // operation.
  // ChangeInt32ToInt64(Load_Uint8) -> Ldrb
  {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Pointer(),
                    MachineType::Pointer());
    m.Return(m.ChangeInt32ToInt64(
        m.Load(MachineType::Uint8(), m.Parameter(0), m.Parameter(1))));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Ldrb, s[0]->arch_opcode());
    EXPECT_EQ(kMode_MRR, s[0]->addressing_mode());
    EXPECT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  // ChangeInt32ToInt64(Load_Int8) -> Ldrsb
  {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Pointer(),
                    MachineType::Pointer());
    m.Return(m.ChangeInt32ToInt64(
        m.Load(MachineType::Int8(), m.Parameter(0), m.Parameter(1))));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Ldrsb, s[0]->arch_opcode());
    EXPECT_EQ(kMode_MRR, s[0]->addressing_mode());
    EXPECT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  // ChangeInt32ToInt64(Load_Uint16) -> Ldrh
  {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Pointer(),
                    MachineType::Pointer());
    m.Return(m.ChangeInt32ToInt64(
        m.Load(MachineType::Uint16(), m.Parameter(0), m.Parameter(1))));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Ldrh, s[0]->arch_opcode());
    EXPECT_EQ(kMode_MRR, s[0]->addressing_mode());
    EXPECT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  // ChangeInt32ToInt64(Load_Int16) -> Ldrsh
  {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Pointer(),
                    MachineType::Pointer());
    m.Return(m.ChangeInt32ToInt64(
        m.Load(MachineType::Int16(), m.Parameter(0), m.Parameter(1))));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Ldrsh, s[0]->arch_opcode());
    EXPECT_EQ(kMode_MRR, s[0]->addressing_mode());
    EXPECT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  // ChangeInt32ToInt64(Load_Uint32) -> Ldrsw
  {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Pointer(),
                    MachineType::Pointer());
    m.Return(m.ChangeInt32ToInt64(
        m.Load(MachineType::Uint32(), m.Parameter(0), m.Parameter(1))));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Ldrsw, s[0]->arch_opcode());
    EXPECT_EQ(kMode_MRR, s[0]->addressing_mode());
    EXPECT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  // ChangeInt32ToInt64(Load_Int32) -> Ldrsw
  {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Pointer(),
                    MachineType::Pointer());
    m.Return(m.ChangeInt32ToInt64(
        m.Load(MachineType::Int32(), m.Parameter(0), m.Parameter(1))));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Ldrsw, s[0]->arch_opcode());
    EXPECT_EQ(kMode_MRR, s[0]->addressing_mode());
    EXPECT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, ChangeInt32ToInt64WithWord32Sar) {
  // Test the mod 32 behaviour of Word32ShiftRightArithmetic by iterating up
  // to 33.
  TRACED_FORRANGE(int32_t, imm, 0, 33) {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int32());
    m.Return(m.ChangeInt32ToInt64(
        m.Word32ShiftRightArithmetic(m.Parameter(0), m.Int32Constant(imm))));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Sbfx, s[0]->arch_opcode());
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(imm & 0x1f, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(32 - (imm & 0x1f), s.ToInt32(s[0]->InputAt(2)));
  }
}

TEST_F(TurboshaftInstructionSelectorTest, Word64SarWithChangeInt32ToInt64) {
  TRACED_FORRANGE(int32_t, imm, -31, 63) {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int32());
    m.Return(m.Word64ShiftRightArithmetic(m.ChangeInt32ToInt64(m.Parameter(0)),
                                          m.Int32Constant(imm)));
    Stream s = m.Build();
    // Optimization should only be applied when 0 <= imm < 32
    if (0 <= imm && imm < 32) {
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(kArm64Sbfx, s[0]->arch_opcode());
      EXPECT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(1U, s[0]->OutputCount());
      EXPECT_EQ(imm, s.ToInt64(s[0]->InputAt(1)));
      EXPECT_EQ(32 - imm, s.ToInt64(s[0]->InputAt(2)));
    } else {
      ASSERT_EQ(2U, s.size());
      EXPECT_EQ(kArm64Sxtw, s[0]->arch_opcode());
      EXPECT_EQ(1U, s[0]->InputCount());
      EXPECT_EQ(1U, s[0]->OutputCount());
      EXPECT_EQ(kArm64Asr, s[1]->arch_opcode());
      EXPECT_EQ(2U, s[1]->InputCount());
      EXPECT_EQ(1U, s[1]->OutputCount());
      EXPECT_EQ(imm, s.ToInt64(s[1]->InputAt(1)));
    }
  }
}

// -----------------------------------------------------------------------------
// Memory access instructions.

namespace {

struct MemoryAccess {
  MachineType type;
  ArchOpcode ldr_opcode;
  ArchOpcode str_opcode;
  const int32_t immediates[20];
};

std::ostream& operator<<(std::ostream& os, const MemoryAccess& memacc) {
  return os << memacc.type;
}

}  // namespace

static const MemoryAccess kMemoryAccesses[] = {
    {MachineType::Int8(),
     kArm64LdrsbW,
     kArm64Strb,
     {-256, -255, -3,  -2,   -1,   0,    1,    2,    3,    255,
      256,  257,  258, 1000, 1001, 2121, 2442, 4093, 4094, 4095}},
    {MachineType::Uint8(),
     kArm64Ldrb,
     kArm64Strb,
     {-256, -255, -3,  -2,   -1,   0,    1,    2,    3,    255,
      256,  257,  258, 1000, 1001, 2121, 2442, 4093, 4094, 4095}},
    {MachineType::Int16(),
     kArm64LdrshW,
     kArm64Strh,
     {-256, -255, -3,  -2,   -1,   0,    1,    2,    3,    255,
      256,  258,  260, 4096, 4098, 4100, 4242, 6786, 8188, 8190}},
    {MachineType::Uint16(),
     kArm64Ldrh,
     kArm64Strh,
     {-256, -255, -3,  -2,   -1,   0,    1,    2,    3,    255,
      256,  258,  260, 4096, 4098, 4100, 4242, 6786, 8188, 8190}},
    {MachineType::Int32(),
     kArm64LdrW,
     kArm64StrW,
     {-256, -255, -3,   -2,   -1,   0,    1,    2,    3,     255,
      256,  260,  4096, 4100, 8192, 8196, 3276, 3280, 16376, 16380}},
    {MachineType::Uint32(),
     kArm64LdrW,
     kArm64StrW,
     {-256, -255, -3,   -2,   -1,   0,    1,    2,    3,     255,
      256,  260,  4096, 4100, 8192, 8196, 3276, 3280, 16376, 16380}},
    {MachineType::Int64(),
     kArm64Ldr,
     kArm64Str,
     {-256, -255, -3,   -2,   -1,   0,    1,     2,     3,     255,
      256,  264,  4096, 4104, 8192, 8200, 16384, 16392, 32752, 32760}},
    {MachineType::Uint64(),
     kArm64Ldr,
     kArm64Str,
     {-256, -255, -3,   -2,   -1,   0,    1,     2,     3,     255,
      256,  264,  4096, 4104, 8192, 8200, 16384, 16392, 32752, 32760}},
    {MachineType::Float32(),
     kArm64LdrS,
     kArm64StrS,
     {-256, -255, -3,   -2,   -1,   0,    1,    2,    3,     255,
      256,  260,  4096, 4100, 8192, 8196, 3276, 3280, 16376, 16380}},
    {MachineType::Float64(),
     kArm64LdrD,
     kArm64StrD,
     {-256, -255, -3,   -2,   -1,   0,    1,     2,     3,     255,
      256,  264,  4096, 4104, 8192, 8200, 16384, 16392, 32752, 32760}}};

using TurboshaftInstructionSelectorMemoryAccessTest =
    TurboshaftInstructionSelectorTestWithParam<MemoryAccess>;

TEST_P(TurboshaftInstructionSelectorMemoryAccessTest, LoadWithParameters) {
  const MemoryAccess memacc = GetParam();
  StreamBuilder m(this, memacc.type, MachineType::Pointer(),
                  MachineType::Int64());
  m.Return(m.Load(memacc.type, m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(memacc.ldr_opcode, s[0]->arch_opcode());
  EXPECT_EQ(kMode_MRR, s[0]->addressing_mode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}

TEST_P(TurboshaftInstructionSelectorMemoryAccessTest, LoadWithImmediateIndex) {
  const MemoryAccess memacc = GetParam();
  TRACED_FOREACH(int32_t, index, memacc.immediates) {
    StreamBuilder m(this, memacc.type, MachineType::Pointer());
    m.Return(m.Load(memacc.type, m.Parameter(0), m.Int64Constant(index)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(memacc.ldr_opcode, s[0]->arch_opcode());
    EXPECT_EQ(kMode_MRI, s[0]->addressing_mode());
    EXPECT_EQ(2U, s[0]->InputCount());
    ASSERT_EQ(InstructionOperand::IMMEDIATE, s[0]->InputAt(1)->kind());
    EXPECT_EQ(index, s.ToInt32(s[0]->InputAt(1)));
    ASSERT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_P(TurboshaftInstructionSelectorMemoryAccessTest, StoreWithParameters) {
  const MemoryAccess memacc = GetParam();
  StreamBuilder m(this, MachineType::Int32(), MachineType::Pointer(),
                  MachineType::Int64(), memacc.type);
  m.Store(memacc.type.representation(), m.Parameter(0), m.Parameter(1),
          m.Parameter(2), kNoWriteBarrier);
  m.Return(m.Int32Constant(0));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(memacc.str_opcode, s[0]->arch_opcode());
  EXPECT_EQ(kMode_MRR, s[0]->addressing_mode());
  EXPECT_EQ(3U, s[0]->InputCount());
  EXPECT_EQ(0U, s[0]->OutputCount());
}

TEST_P(TurboshaftInstructionSelectorMemoryAccessTest, StoreWithImmediateIndex) {
  const MemoryAccess memacc = GetParam();
  TRACED_FOREACH(int32_t, index, memacc.immediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Pointer(),
                    memacc.type);
    m.Store(memacc.type.representation(), m.Parameter(0),
            m.Int64Constant(index), m.Parameter(1), kNoWriteBarrier);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(memacc.str_opcode, s[0]->arch_opcode());
    EXPECT_EQ(kMode_MRI, s[0]->addressing_mode());
    ASSERT_EQ(3U, s[0]->InputCount());
    ASSERT_EQ(InstructionOperand::IMMEDIATE, s[0]->InputAt(2)->kind());
    EXPECT_EQ(index, s.ToInt32(s[0]->InputAt(2)));
    EXPECT_EQ(0U, s[0]->OutputCount());
  }
}

TEST_P(TurboshaftInstructionSelectorMemoryAccessTest, StoreZero) {
  const MemoryAccess memacc = GetParam();
  TRACED_FOREACH(int32_t, index, memacc.immediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Pointer());
    MachineRepresentation rep = memacc.type.representation();
    OpIndex zero;
    switch (rep) {
      case MachineRepresentation::kWord8:
      case MachineRepresentation::kWord16:
      case MachineRepresentation::kWord32:
        zero = m.Word32Constant(0);
        break;
      case MachineRepresentation::kWord64:
        zero = m.Int64Constant(0);
        break;
      case MachineRepresentation::kFloat32:
        zero = m.Float32Constant(0);
        break;
      case MachineRepresentation::kFloat64:
        zero = m.Float64Constant(0);
        break;
      default:
        UNREACHABLE();
    }
    m.Store(rep, m.Parameter(0), m.Int64Constant(index), zero, kNoWriteBarrier);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(memacc.str_opcode, s[0]->arch_opcode());
    EXPECT_EQ(kMode_MRI, s[0]->addressing_mode());
    ASSERT_EQ(3U, s[0]->InputCount());
    ASSERT_EQ(InstructionOperand::IMMEDIATE, s[0]->InputAt(2)->kind());
    EXPECT_EQ(index, s.ToInt32(s[0]->InputAt(2)));
    ASSERT_EQ(InstructionOperand::IMMEDIATE, s[0]->InputAt(0)->kind());
    switch (rep) {
      case MachineRepresentation::kWord8:
      case MachineRepresentation::kWord16:
      case MachineRepresentation::kWord32:
      case MachineRepresentation::kWord64:
        EXPECT_EQ(0, s.ToInt64(s[0]->InputAt(0)));
        break;
      case MachineRepresentation::kFloat32:
        EXPECT_EQ(0, s.ToFloat32(s[0]->InputAt(0)));
        break;
      case MachineRepresentation::kFloat64:
        EXPECT_EQ(0, s.ToFloat64(s[0]->InputAt(0)));
        break;
      default:
        UNREACHABLE();
    }
    EXPECT_EQ(0U, s[0]->OutputCount());
  }
}

TEST_P(TurboshaftInstructionSelectorMemoryAccessTest, LoadWithShiftedIndex) {
  const MemoryAccess memacc = GetParam();
  TRACED_FORRANGE(int, immediate_shift, 0, 4) {
    // 32 bit shift
    {
      StreamBuilder m(this, memacc.type, MachineType::Pointer(),
                      MachineType::Int32());
      OpIndex const index =
          m.Word32ShiftLeft(m.Parameter(1), m.Int32Constant(immediate_shift));
      m.Return(
          m.Load(memacc.type, m.Parameter(0), m.ChangeUint32ToUint64(index)));
      Stream s = m.Build();
      if (immediate_shift == ElementSizeLog2Of(memacc.type.representation())) {
        ASSERT_EQ(1U, s.size());
        EXPECT_EQ(memacc.ldr_opcode, s[0]->arch_opcode());
        EXPECT_EQ(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
        EXPECT_EQ(3U, s[0]->InputCount());
        EXPECT_EQ(1U, s[0]->OutputCount());
      } else {
        // Make sure we haven't merged the shift into the load instruction.
        ASSERT_NE(1U, s.size());
        EXPECT_NE(memacc.ldr_opcode, s[0]->arch_opcode());
        EXPECT_NE(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
      }
    }
    // 64 bit shift
    {
      StreamBuilder m(this, memacc.type, MachineType::Pointer(),
                      MachineType::Int64());
      OpIndex const index =
          m.Word64ShiftLeft(m.Parameter(1), m.Int32Constant(immediate_shift));
      m.Return(m.Load(memacc.type, m.Parameter(0), index));
      Stream s = m.Build();
      if (immediate_shift == ElementSizeLog2Of(memacc.type.representation())) {
        ASSERT_EQ(1U, s.size());
        EXPECT_EQ(memacc.ldr_opcode, s[0]->arch_opcode());
        EXPECT_EQ(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
        EXPECT_EQ(3U, s[0]->InputCount());
        EXPECT_EQ(1U, s[0]->OutputCount());
      } else {
        // Make sure we haven't merged the shift into the load instruction.
        ASSERT_NE(1U, s.size());
        EXPECT_NE(memacc.ldr_opcode, s[0]->arch_opcode());
        EXPECT_NE(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
      }
    }
  }
}

TEST_P(TurboshaftInstructionSelectorMemoryAccessTest, StoreWithShiftedIndex) {
  const MemoryAccess memacc = GetParam();
  TRACED_FORRANGE(int, immediate_shift, 0, 4) {
    // 32 bit shift
    {
      StreamBuilder m(this, MachineType::Int32(), MachineType::Pointer(),
                      MachineType::Int32(), memacc.type);
      OpIndex const index =
          m.Word32ShiftLeft(m.Parameter(1), m.Int32Constant(immediate_shift));
      m.Store(memacc.type.representation(), m.Parameter(0),
              m.ChangeUint32ToUint64(index), m.Parameter(2), kNoWriteBarrier);
      m.Return(m.Int32Constant(0));
      Stream s = m.Build();
      if (immediate_shift == ElementSizeLog2Of(memacc.type.representation())) {
        ASSERT_EQ(1U, s.size());
        EXPECT_EQ(memacc.str_opcode, s[0]->arch_opcode());
        EXPECT_EQ(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
        EXPECT_EQ(4U, s[0]->InputCount());
        EXPECT_EQ(0U, s[0]->OutputCount());
      } else {
        // Make sure we haven't merged the shift into the store instruction.
        ASSERT_NE(1U, s.size());
        EXPECT_NE(memacc.str_opcode, s[0]->arch_opcode());
        EXPECT_NE(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
      }
    }
    // 64 bit shift
    {
      StreamBuilder m(this, MachineType::Int64(), MachineType::Pointer(),
                      MachineType::Int64(), memacc.type);
      OpIndex const index =
          m.Word64ShiftLeft(m.Parameter(1), m.Int32Constant(immediate_shift));
      m.Store(memacc.type.representation(), m.Parameter(0), index,
              m.Parameter(2), kNoWriteBarrier);
      m.Return(m.Int64Constant(0));
      Stream s = m.Build();
      if (immediate_shift == ElementSizeLog2Of(memacc.type.representation())) {
        ASSERT_EQ(1U, s.size());
        EXPECT_EQ(memacc.str_opcode, s[0]->arch_opcode());
        EXPECT_EQ(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
        EXPECT_EQ(4U, s[0]->InputCount());
        EXPECT_EQ(0U, s[0]->OutputCount());
      } else {
        // Make sure we haven't merged the shift into the store instruction.
        ASSERT_NE(1U, s.size());
        EXPECT_NE(memacc.str_opcode, s[0]->arch_opcode());
        EXPECT_NE(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
      }
    }
  }
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorMemoryAccessTest,
                         ::testing::ValuesIn(kMemoryAccesses));

// This list doesn't contain kIndirectPointerWriteBarrier because only indirect
// pointer fields can be stored to with that barrier kind.
static const WriteBarrierKind kWriteBarrierKinds[] = {
    kMapWriteBarrier, kPointerWriteBarrier, kEphemeronKeyWriteBarrier,
    kFullWriteBarrier};

const int32_t kStoreWithBarrierImmediates[] = {
    -256, -255, -3,   -2,   -1,   0,    1,     2,     3,     255,
    256,  264,  4096, 4104, 8192, 8200, 16384, 16392, 32752, 32760};

using TurboshaftInstructionSelectorStoreWithBarrierTest =
    TurboshaftInstructionSelectorTestWithParam<WriteBarrierKind>;

TEST_P(TurboshaftInstructionSelectorStoreWithBarrierTest,
       StoreWithWriteBarrierParameters) {
  const WriteBarrierKind barrier_kind = GetParam();
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int64(),
                  MachineType::Int64(), MachineType::AnyTagged());
  m.Store(MachineRepresentation::kTagged, m.Parameter(0), m.Parameter(1),
          m.Parameter(2), barrier_kind);
  m.Return(m.Int32Constant(0));
  Stream s = m.Build(kAllExceptNopInstructions);
  // We have two instructions that are not nops: Store and Return.
  ASSERT_EQ(2U, s.size());
  EXPECT_EQ(kArchStoreWithWriteBarrier, s[0]->arch_opcode());
  EXPECT_EQ(kMode_MRR, s[0]->addressing_mode());
  EXPECT_EQ(3U, s[0]->InputCount());
  EXPECT_EQ(0U, s[0]->OutputCount());
}

TEST_P(TurboshaftInstructionSelectorStoreWithBarrierTest,
       StoreWithWriteBarrierImmediate) {
  const WriteBarrierKind barrier_kind = GetParam();
  TRACED_FOREACH(int32_t, index, kStoreWithBarrierImmediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int64(),
                    MachineType::AnyTagged());
    m.Store(MachineRepresentation::kTagged, m.Parameter(0),
            m.Int64Constant(index), m.Parameter(1), barrier_kind);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build(kAllExceptNopInstructions);
    // We have two instructions that are not nops: Store and Return.
    ASSERT_EQ(2U, s.size());
    EXPECT_EQ(kArchStoreWithWriteBarrier, s[0]->arch_opcode());
    // With compressed pointers, a store with barrier is a 32-bit str which has
    // a smaller immediate range.
    if (COMPRESS_POINTERS_BOOL && (index > 16380)) {
      EXPECT_EQ(kMode_MRR, s[0]->addressing_mode());
    } else {
      EXPECT_EQ(kMode_MRI, s[0]->addressing_mode());
    }
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(0U, s[0]->OutputCount());
  }
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorStoreWithBarrierTest,
                         ::testing::ValuesIn(kWriteBarrierKinds));

// -----------------------------------------------------------------------------
// Comparison instructions.

static const MachInst2 kComparisonInstructions[] = {
    {TSBinop::kWord32Equal, "Word32Equal", kArm64Cmp32, MachineType::Int32()},
    {TSBinop::kWord64Equal, "Word64Equal", kArm64Cmp, MachineType::Int64()},
};

using TurboshaftInstructionSelectorComparisonTest =
    TurboshaftInstructionSelectorTestWithParam<MachInst2>;

TEST_P(TurboshaftInstructionSelectorComparisonTest, WithParameters) {
  const MachInst2 cmp = GetParam();
  const MachineType type = cmp.machine_type;
  StreamBuilder m(this, type, type, type);
  m.Return(m.Emit(cmp.op, m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(cmp.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_set, s[0]->flags_mode());
  EXPECT_EQ(kEqual, s[0]->flags_condition());
}

TEST_P(TurboshaftInstructionSelectorComparisonTest, WithImmediate) {
  const MachInst2 cmp = GetParam();
  const MachineType type = cmp.machine_type;
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    // Compare with 0 are turned into tst instruction.
    if (imm == 0) continue;
    StreamBuilder m(this, type, type);
    m.Return(m.Emit(cmp.op, m.Parameter(0), BuildConstant(&m, type, imm)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(cmp.arch_opcode, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    ASSERT_EQ(InstructionOperand::IMMEDIATE, s[0]->InputAt(1)->kind());
    EXPECT_EQ(imm, s.ToInt64(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kEqual, s[0]->flags_condition());
  }
  TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
    // Compare with 0 are turned into tst instruction.
    if (imm == 0) continue;
    StreamBuilder m(this, type, type);
    m.Return(m.Emit(cmp.op, BuildConstant(&m, type, imm), m.Parameter(0)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(cmp.arch_opcode, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    ASSERT_EQ(InstructionOperand::IMMEDIATE, s[0]->InputAt(1)->kind());
    EXPECT_EQ(imm, s.ToInt64(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kEqual, s[0]->flags_condition());
  }
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorComparisonTest,
                         ::testing::ValuesIn(kComparisonInstructions));

TEST_F(TurboshaftInstructionSelectorTest, Word32EqualWithZero) {
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(m.Word32Equal(m.Parameter(0), m.Int32Constant(0)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Tst32, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(s[0]->InputAt(0)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kEqual, s[0]->flags_condition());
  }
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(m.Word32Equal(m.Int32Constant(0), m.Parameter(0)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Tst32, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(s[0]->InputAt(0)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kEqual, s[0]->flags_condition());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, Word64EqualWithZero) {
  {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int64());
    m.Return(m.Word64Equal(m.Parameter(0), m.Int64Constant(0)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Tst, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(s[0]->InputAt(0)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kEqual, s[0]->flags_condition());
  }
  {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int64());
    m.Return(m.Word64Equal(m.Int64Constant(0), m.Parameter(0)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Tst, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(s[0]->InputAt(0)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kEqual, s[0]->flags_condition());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, Word32EqualWithWord32Shift) {
  TRACED_FOREACH(Shift, shift, kShiftInstructions) {
    // Skip non 32-bit shifts or ror operations.
    if (shift.mi.machine_type != MachineType::Int32() ||
        shift.mi.arch_opcode == kArm64Ror32) {
      continue;
    }

    TRACED_FORRANGE(int32_t, imm, -32, 63) {
      StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                      MachineType::Int32());
      OpIndex const p0 = m.Parameter(0);
      OpIndex const p1 = m.Parameter(1);
      OpIndex r = m.Emit(shift.mi.op, p1, m.Int32Constant(imm));
      m.Return(m.Word32Equal(p0, r));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(kArm64Cmp32, s[0]->arch_opcode());
      EXPECT_EQ(shift.mode, s[0]->addressing_mode());
      ASSERT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
      EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
      EXPECT_EQ(0x3F & imm, 0x3F & s.ToInt32(s[0]->InputAt(2)));
      ASSERT_EQ(1U, s[0]->OutputCount());
    }
    TRACED_FORRANGE(int32_t, imm, -32, 63) {
      StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                      MachineType::Int32());
      OpIndex const p0 = m.Parameter(0);
      OpIndex const p1 = m.Parameter(1);
      OpIndex r = m.Emit(shift.mi.op, p1, m.Int32Constant(imm));
      m.Return(m.Word32Equal(r, p0));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(kArm64Cmp32, s[0]->arch_opcode());
      EXPECT_EQ(shift.mode, s[0]->addressing_mode());
      ASSERT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
      EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
      EXPECT_EQ(0x3F & imm, 0x3F & s.ToInt32(s[0]->InputAt(2)));
      ASSERT_EQ(1U, s[0]->OutputCount());
    }
  }
}

TEST_F(TurboshaftInstructionSelectorTest, Word32EqualWithUnsignedExtendByte) {
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    OpIndex r = m.Word32BitwiseAnd(p1, m.Int32Constant(0xFF));
    m.Return(m.Word32Equal(p0, r));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Cmp32, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_UXTB, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
    ASSERT_EQ(1U, s[0]->OutputCount());
  }
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    OpIndex r = m.Word32BitwiseAnd(p1, m.Int32Constant(0xFF));
    m.Return(m.Word32Equal(r, p0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Cmp32, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_UXTB, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
    ASSERT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_F(TurboshaftInstructionSelectorTest,
       Word32EqualWithUnsignedExtendHalfword) {
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    OpIndex r = m.Word32BitwiseAnd(p1, m.Int32Constant(0xFFFF));
    m.Return(m.Word32Equal(p0, r));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Cmp32, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_UXTH, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
    ASSERT_EQ(1U, s[0]->OutputCount());
  }
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    OpIndex r = m.Word32BitwiseAnd(p1, m.Int32Constant(0xFFFF));
    m.Return(m.Word32Equal(r, p0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Cmp32, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_UXTH, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
    ASSERT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, Word32EqualWithSignedExtendByte) {
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    OpIndex r = m.Word32ShiftRightArithmetic(
        m.Word32ShiftLeft(p1, m.Int32Constant(24)), m.Int32Constant(24));
    m.Return(m.Word32Equal(p0, r));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Cmp32, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_SXTB, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
    ASSERT_EQ(1U, s[0]->OutputCount());
  }
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    OpIndex r = m.Word32ShiftRightArithmetic(
        m.Word32ShiftLeft(p1, m.Int32Constant(24)), m.Int32Constant(24));
    m.Return(m.Word32Equal(r, p0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Cmp32, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_SXTB, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
    ASSERT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, Word32EqualWithSignedExtendHalfword) {
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    OpIndex r = m.Word32ShiftRightArithmetic(
        m.Word32ShiftLeft(p1, m.Int32Constant(16)), m.Int32Constant(16));
    m.Return(m.Word32Equal(p0, r));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Cmp32, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_SXTH, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
    ASSERT_EQ(1U, s[0]->OutputCount());
  }
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    OpIndex r = m.Word32ShiftRightArithmetic(
        m.Word32ShiftLeft(p1, m.Int32Constant(16)), m.Int32Constant(16));
    m.Return(m.Word32Equal(r, p0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Cmp32, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_SXTH, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
    ASSERT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, Word32EqualZeroWithWord32Equal) {
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    m.Return(m.Word32Equal(m.Word32Equal(p0, p1), m.Int32Constant(0)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Cmp32, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kNotEqual, s[0]->flags_condition());
  }
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    m.Return(m.Word32Equal(m.Int32Constant(0), m.Word32Equal(p0, p1)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Cmp32, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kNotEqual, s[0]->flags_condition());
  }
}

namespace {

struct IntegerCmp {
  MachInst2 mi;
  FlagsCondition cond;
  FlagsCondition commuted_cond;
};

std::ostream& operator<<(std::ostream& os, const IntegerCmp& cmp) {
  return os << cmp.mi;
}

// ARM64 32-bit integer comparison instructions.
const IntegerCmp kIntegerCmpInstructions[] = {
    {{TSBinop::kWord32Equal, "Word32Equal", kArm64Cmp32, MachineType::Int32()},
     kEqual,
     kEqual},
    {{TSBinop::kInt32LessThan, "Int32LessThan", kArm64Cmp32,
      MachineType::Int32()},
     kSignedLessThan,
     kSignedGreaterThan},
    {{TSBinop::kInt32LessThanOrEqual, "Int32LessThanOrEqual", kArm64Cmp32,
      MachineType::Int32()},
     kSignedLessThanOrEqual,
     kSignedGreaterThanOrEqual},
    {{TSBinop::kUint32LessThan, "Uint32LessThan", kArm64Cmp32,
      MachineType::Uint32()},
     kUnsignedLessThan,
     kUnsignedGreaterThan},
    {{TSBinop::kUint32LessThanOrEqual, "Uint32LessThanOrEqual", kArm64Cmp32,
      MachineType::Uint32()},
     kUnsignedLessThanOrEqual,
     kUnsignedGreaterThanOrEqual}};

const IntegerCmp kIntegerCmpEqualityInstructions[] = {
    {{TSBinop::kWord32Equal, "Word32Equal", kArm64Cmp32, MachineType::Int32()},
     kEqual,
     kEqual},
    {{TSBinop::kWord32NotEqual, "Word32NotEqual", kArm64Cmp32,
      MachineType::Int32()},
     kNotEqual,
     kNotEqual}};
}  // namespace

TEST_F(TurboshaftInstructionSelectorTest, Word32CompareNegateWithWord32Shift) {
  TRACED_FOREACH(IntegerCmp, cmp, kIntegerCmpEqualityInstructions) {
    TRACED_FOREACH(Shift, shift, kShiftInstructions) {
      // Test 32-bit operations. Ignore ROR shifts, as compare-negate does not
      // support them.
      if (shift.mi.machine_type != MachineType::Int32() ||
          shift.mi.arch_opcode == kArm64Ror32) {
        continue;
      }

      TRACED_FORRANGE(int32_t, imm, -32, 63) {
        StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                        MachineType::Int32());
        OpIndex const p0 = m.Parameter(0);
        OpIndex const p1 = m.Parameter(1);
        OpIndex r = m.Emit(shift.mi.op, p1, m.Int32Constant(imm));
        m.Return(m.Emit(cmp.mi.op, p0, m.Word32Sub(m.Int32Constant(0), r)));
        Stream s = m.Build();
        ASSERT_EQ(1U, s.size());
        EXPECT_EQ(kArm64Cmn32, s[0]->arch_opcode());
        EXPECT_EQ(3U, s[0]->InputCount());
        EXPECT_EQ(shift.mode, s[0]->addressing_mode());
        EXPECT_EQ(0x3F & imm, 0x3F & s.ToInt32(s[0]->InputAt(2)));
        EXPECT_EQ(1U, s[0]->OutputCount());
        EXPECT_EQ(kFlags_set, s[0]->flags_mode());
        EXPECT_EQ(cmp.cond, s[0]->flags_condition());
      }
    }
  }
}

TEST_F(TurboshaftInstructionSelectorTest, CmpWithImmediateOnLeft) {
  TRACED_FOREACH(IntegerCmp, cmp, kIntegerCmpInstructions) {
    TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
      // kEqual and kNotEqual trigger the cbz/cbnz optimization, which
      // is tested elsewhere.
      if (cmp.cond == kEqual || cmp.cond == kNotEqual) continue;
      // For signed less than or equal to zero, we generate TBNZ.
      if (cmp.cond == kSignedLessThanOrEqual && imm == 0) continue;
      StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
      OpIndex const p0 = m.Parameter(0);
      m.Return(m.Emit(cmp.mi.op, m.Int32Constant(imm), p0));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(kArm64Cmp32, s[0]->arch_opcode());
      ASSERT_LE(2U, s[0]->InputCount());
      EXPECT_EQ(kFlags_set, s[0]->flags_mode());
      EXPECT_EQ(cmp.commuted_cond, s[0]->flags_condition());
      EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    }
  }
}

TEST_F(TurboshaftInstructionSelectorTest, CmnWithImmediateOnLeft) {
  TRACED_FOREACH(IntegerCmp, cmp, kIntegerCmpEqualityInstructions) {
    TRACED_FOREACH(int32_t, imm, kAddSubImmediates) {
      // kEqual and kNotEqual trigger the cbz/cbnz optimization, which
      // is tested elsewhere.
      if (cmp.cond == kEqual || cmp.cond == kNotEqual) continue;
      StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
      OpIndex sub = m.Word32Sub(m.Int32Constant(0), m.Parameter(0));
      m.Return(m.Emit(cmp.mi.op, m.Int32Constant(imm), sub));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(kArm64Cmn32, s[0]->arch_opcode());
      ASSERT_LE(2U, s[0]->InputCount());
      EXPECT_EQ(kFlags_set, s[0]->flags_mode());
      EXPECT_EQ(cmp.cond, s[0]->flags_condition());
      EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    }
  }
}

TEST_F(TurboshaftInstructionSelectorTest, CmpSignedExtendByteOnLeft) {
  TRACED_FOREACH(IntegerCmp, cmp, kIntegerCmpInstructions) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    OpIndex extend = m.Word32ShiftRightArithmetic(
        m.Word32ShiftLeft(m.Parameter(0), m.Int32Constant(24)),
        m.Int32Constant(24));
    m.Return(m.Emit(cmp.mi.op, extend, m.Parameter(1)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Cmp32, s[0]->arch_opcode());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(cmp.commuted_cond, s[0]->flags_condition());
    EXPECT_EQ(kMode_Operand2_R_SXTB, s[0]->addressing_mode());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, CmnSignedExtendByteOnLeft) {
  TRACED_FOREACH(IntegerCmp, cmp, kIntegerCmpEqualityInstructions) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    OpIndex sub = m.Word32Sub(m.Int32Constant(0), m.Parameter(0));
    OpIndex extend = m.Word32ShiftRightArithmetic(
        m.Word32ShiftLeft(m.Parameter(0), m.Int32Constant(24)),
        m.Int32Constant(24));
    m.Return(m.Emit(cmp.mi.op, extend, sub));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Cmn32, s[0]->arch_opcode());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(cmp.cond, s[0]->flags_condition());
    EXPECT_EQ(kMode_Operand2_R_SXTB, s[0]->addressing_mode());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, CmpShiftByImmediateOnLeft) {
  TRACED_FOREACH(IntegerCmp, cmp, kIntegerCmpInstructions) {
    TRACED_FOREACH(Shift, shift, kShiftInstructions) {
      // Only test relevant shifted operands.
      if (shift.mi.machine_type != MachineType::Int32()) continue;

      // The available shift operand range is `0 <= imm < 32`, but we also test
      // that immediates outside this range are handled properly (modulo-32).
      TRACED_FORRANGE(int, imm, -32, 63) {
        StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                        MachineType::Int32());
        m.Return(
            m.Emit(cmp.mi.op,
                   m.Emit(shift.mi.op, m.Parameter(1), m.Int32Constant(imm)),
                   m.Parameter(0)));
        Stream s = m.Build();
        // Cmp does not support ROR shifts.
        if (shift.mi.arch_opcode == kArm64Ror32) {
          ASSERT_EQ(2U, s.size());
          continue;
        }
        ASSERT_EQ(1U, s.size());
        EXPECT_EQ(kArm64Cmp32, s[0]->arch_opcode());
        EXPECT_EQ(shift.mode, s[0]->addressing_mode());
        EXPECT_EQ(3U, s[0]->InputCount());
        EXPECT_EQ(0x3F & imm, 0x3F & s.ToInt64(s[0]->InputAt(2)));
        EXPECT_EQ(1U, s[0]->OutputCount());
        EXPECT_EQ(kFlags_set, s[0]->flags_mode());
        EXPECT_EQ(cmp.commuted_cond, s[0]->flags_condition());
      }
    }
  }
}

TEST_F(TurboshaftInstructionSelectorTest, CmnShiftByImmediateOnLeft) {
  TRACED_FOREACH(IntegerCmp, cmp, kIntegerCmpEqualityInstructions) {
    TRACED_FOREACH(Shift, shift, kShiftInstructions) {
      // Only test relevant shifted operands.
      if (shift.mi.machine_type != MachineType::Int32()) continue;

      // The available shift operand range is `0 <= imm < 32`, but we also test
      // that immediates outside this range are handled properly (modulo-32).
      TRACED_FORRANGE(int, imm, -32, 63) {
        StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                        MachineType::Int32());
        OpIndex sub = m.Word32Sub(m.Int32Constant(0), m.Parameter(0));
        m.Return(m.Emit(
            cmp.mi.op,
            m.Emit(shift.mi.op, m.Parameter(1), m.Int32Constant(imm)), sub));
        Stream s = m.Build();
        // Cmn does not support ROR shifts.
        if (shift.mi.arch_opcode == kArm64Ror32) {
          ASSERT_EQ(2U, s.size());
          continue;
        }
        ASSERT_EQ(1U, s.size());
        EXPECT_EQ(kArm64Cmn32, s[0]->arch_opcode());
        EXPECT_EQ(shift.mode, s[0]->addressing_mode());
        EXPECT_EQ(3U, s[0]->InputCount());
        EXPECT_EQ(0x3F & imm, 0x3F & s.ToInt64(s[0]->InputAt(2)));
        EXPECT_EQ(1U, s[0]->OutputCount());
        EXPECT_EQ(kFlags_set, s[0]->flags_mode());
        EXPECT_EQ(cmp.cond, s[0]->flags_condition());
      }
    }
  }
}

// -----------------------------------------------------------------------------
// Flag-setting add and and instructions.

const IntegerCmp kBinopCmpZeroRightInstructions[] = {
    {{TSBinop::kWord32Equal, "Word32Equal", kArm64Cmp32, MachineType::Int32()},
     kEqual,
     kEqual},
    {{TSBinop::kWord32NotEqual, "Word32NotEqual", kArm64Cmp32,
      MachineType::Int32()},
     kNotEqual,
     kNotEqual},
    {{TSBinop::kInt32LessThan, "Int32LessThan", kArm64Cmp32,
      MachineType::Int32()},
     kNegative,
     kNegative},
    {{TSBinop::kInt32GreaterThanOrEqual, "Int32GreaterThanOrEqual", kArm64Cmp32,
      MachineType::Int32()},
     kPositiveOrZero,
     kPositiveOrZero},
    {{TSBinop::kUint32LessThanOrEqual, "Uint32LessThanOrEqual", kArm64Cmp32,
      MachineType::Int32()},
     kEqual,
     kEqual},
    {{TSBinop::kUint32GreaterThan, "Uint32GreaterThan", kArm64Cmp32,
      MachineType::Int32()},
     kNotEqual,
     kNotEqual}};

const IntegerCmp kBinop64CmpZeroRightInstructions[] = {
    {{TSBinop::kWord64Equal, "Word64Equal", kArm64Cmp, MachineType::Int64()},
     kEqual,
     kEqual},
    {{TSBinop::kWord64NotEqual, "Word64NotEqual", kArm64Cmp,
      MachineType::Int64()},
     kNotEqual,
     kNotEqual},
    {{TSBinop::kInt64LessThan, "Int64LessThan", kArm64Cmp,
      MachineType::Int64()},
     kNegative,
     kNegative},
    {{TSBinop::kInt64GreaterThanOrEqual, "Int64GreaterThanOrEqual", kArm64Cmp,
      MachineType::Int64()},
     kPositiveOrZero,
     kPositiveOrZero},
    {{TSBinop::kUint64LessThanOrEqual, "Uint64LessThanOrEqual", kArm64Cmp,
      MachineType::Int64()},
     kEqual,
     kEqual},
    {{TSBinop::kUint64GreaterThan, "Uint64GreaterThan", kArm64Cmp,
      MachineType::Int64()},
     kNotEqual,
     kNotEqual},
};

const IntegerCmp kBinopCmpZeroLeftInstructions[] = {
    {{TSBinop::kWord32Equal, "Word32Equal", kArm64Cmp32, MachineType::Int32()},
     kEqual,
     kEqual},
    {{TSBinop::kWord32NotEqual, "Word32NotEqual", kArm64Cmp32,
      MachineType::Int32()},
     kNotEqual,
     kNotEqual},
    {{TSBinop::kInt32GreaterThan, "Int32GreaterThan", kArm64Cmp32,
      MachineType::Int32()},
     kNegative,
     kNegative},
    {{TSBinop::kInt32LessThanOrEqual, "Int32LessThanOrEqual", kArm64Cmp32,
      MachineType::Int32()},
     kPositiveOrZero,
     kPositiveOrZero},
    {{TSBinop::kUint32GreaterThanOrEqual, "Uint32GreaterThanOrEqual",
      kArm64Cmp32, MachineType::Int32()},
     kEqual,
     kEqual},
    {{TSBinop::kUint32LessThan, "Uint32LessThan", kArm64Cmp32,
      MachineType::Int32()},
     kNotEqual,
     kNotEqual}};

struct FlagSettingInst {
  MachInst2 mi;
  ArchOpcode no_output_opcode;
};

std::ostream& operator<<(std::ostream& os, const FlagSettingInst& inst) {
  return os << inst.mi.constructor_name;
}

const FlagSettingInst kFlagSettingInstructions[] = {
    {{TSBinop::kWord32Add, "Int32Add", kArm64Add32, MachineType::Int32()},
     kArm64Cmn32},
    {{TSBinop::kWord32BitwiseAnd, "Word32BitwiseAnd", kArm64And32,
      MachineType::Int32()},
     kArm64Tst32}};

using TurboshaftInstructionSelectorFlagSettingTest =
    TurboshaftInstructionSelectorTestWithParam<FlagSettingInst>;

TEST_P(TurboshaftInstructionSelectorFlagSettingTest, CmpZeroRight) {
  const FlagSettingInst inst = GetParam();
  // Add with single user : a cmp instruction.
  TRACED_FOREACH(IntegerCmp, cmp, kBinopCmpZeroRightInstructions) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    OpIndex binop = m.Emit(inst.mi.op, m.Parameter(0), m.Parameter(1));
    m.Return(m.Emit(cmp.mi.op, binop, m.Int32Constant(0)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(inst.no_output_opcode, s[0]->arch_opcode());
    EXPECT_EQ(s.ToVreg(m.Parameter(0)), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(m.Parameter(1)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(cmp.cond, s[0]->flags_condition());
  }
}

TEST_P(TurboshaftInstructionSelectorFlagSettingTest, CmpZeroLeft) {
  const FlagSettingInst inst = GetParam();
  // Test a cmp with zero on the left-hand side.
  TRACED_FOREACH(IntegerCmp, cmp, kBinopCmpZeroLeftInstructions) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    OpIndex binop = m.Emit(inst.mi.op, m.Parameter(0), m.Parameter(1));
    m.Return(m.Emit(cmp.mi.op, m.Int32Constant(0), binop));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(inst.no_output_opcode, s[0]->arch_opcode());
    EXPECT_EQ(s.ToVreg(m.Parameter(0)), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(m.Parameter(1)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(cmp.cond, s[0]->flags_condition());
  }
}

TEST_P(TurboshaftInstructionSelectorFlagSettingTest,
       CmpZeroOnlyUserInBasicBlock) {
  const FlagSettingInst inst = GetParam();
  // Binop with additional users, but in a different basic block.
  TRACED_FOREACH(IntegerCmp, cmp, kBinopCmpZeroRightInstructions) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    OpIndex binop = m.Emit(inst.mi.op, m.Parameter(0), m.Parameter(1));
    OpIndex comp = m.Emit(cmp.mi.op, binop, m.Int32Constant(0));
    m.Branch(m.Parameter<Word32>(0), a, b);
    m.Bind(a);
    m.Return(binop);
    m.Bind(b);
    m.Return(comp);
    Stream s = m.Build();
    ASSERT_EQ(2U, s.size());  // Flag-setting instruction and branch.
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(inst.mi.arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(s.ToVreg(m.Parameter(0)), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(m.Parameter(1)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(cmp.cond, s[0]->flags_condition());
  }
}

TEST_P(TurboshaftInstructionSelectorFlagSettingTest, ShiftedOperand) {
  const FlagSettingInst inst = GetParam();
  // Like the test above, but with a shifted input to the binary operator.
  TRACED_FOREACH(IntegerCmp, cmp, kBinopCmpZeroRightInstructions) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    OpIndex imm = m.Int32Constant(5);
    OpIndex shift = m.Word32ShiftLeft(m.Parameter(1), imm);
    OpIndex binop = m.Emit(inst.mi.op, m.Parameter(0), shift);
    OpIndex comp = m.Emit(cmp.mi.op, binop, m.Int32Constant(0));
    m.Branch(m.Parameter<Word32>(0), a, b);
    m.Bind(a);
    m.Return(binop);
    m.Bind(b);
    m.Return(comp);
    Stream s = m.Build();
    ASSERT_EQ(2U, s.size());  // Flag-setting instruction and branch.
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(inst.mi.arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(s.ToVreg(m.Parameter(0)), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(m.Parameter(1)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(5, s.ToInt32(s[0]->InputAt(2)));
    EXPECT_EQ(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(cmp.cond, s[0]->flags_condition());
  }
}

TEST_P(TurboshaftInstructionSelectorFlagSettingTest, UsersInSameBasicBlock) {
  const FlagSettingInst inst = GetParam();
  // Binop with additional users, in the same basic block. We need to make sure
  // we don't try to optimise this case.
  TRACED_FOREACH(IntegerCmp, cmp, kIntegerCmpInstructions) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    OpIndex binop = m.Emit(inst.mi.op, m.Parameter(0), m.Parameter(1));
    OpIndex mul = m.Word32Mul(m.Parameter(0), binop);
    OpIndex comp = m.Emit(cmp.mi.op, binop, m.Int32Constant(0));
    m.Branch(m.Parameter<Word32>(0), a, b);
    m.Bind(a);
    m.Return(mul);
    m.Bind(b);
    m.Return(comp);
    Stream s = m.Build();
    ASSERT_EQ(4U, s.size());  // Includes the compare and branch instruction.
    EXPECT_EQ(inst.mi.arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(kFlags_none, s[0]->flags_mode());
    EXPECT_EQ(kArm64Mul32, s[1]->arch_opcode());
    EXPECT_EQ(kArm64Cmp32, s[2]->arch_opcode());
    EXPECT_EQ(kFlags_set, s[2]->flags_mode());
    EXPECT_EQ(cmp.cond, s[2]->flags_condition());
  }
}

TEST_P(TurboshaftInstructionSelectorFlagSettingTest, CommuteImmediate) {
  const FlagSettingInst inst = GetParam();
  // Immediate on left hand side of the binary operator.
  TRACED_FOREACH(IntegerCmp, cmp, kBinopCmpZeroRightInstructions) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    // 3 can be an immediate on both arithmetic and logical instructions.
    OpIndex imm = m.Int32Constant(3);
    OpIndex binop = m.Emit(inst.mi.op, imm, m.Parameter(0));
    OpIndex comp = m.Emit(cmp.mi.op, binop, m.Int32Constant(0));
    m.Return(comp);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(inst.no_output_opcode, s[0]->arch_opcode());
    EXPECT_EQ(s.ToVreg(m.Parameter(0)), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(3, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(cmp.cond, s[0]->flags_condition());
  }
}

TEST_P(TurboshaftInstructionSelectorFlagSettingTest, CommuteShift) {
  const FlagSettingInst inst = GetParam();
  // Left-hand side operand shifted by immediate.
  TRACED_FOREACH(IntegerCmp, cmp, kBinopCmpZeroRightInstructions) {
    TRACED_FOREACH(Shift, shift, kShiftInstructions) {
      // Only test relevant shifted operands.
      if (shift.mi.machine_type != MachineType::Int32()) continue;

      StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                      MachineType::Int32());
      OpIndex imm = m.Int32Constant(5);
      OpIndex shifted_operand = m.Emit(shift.mi.op, m.Parameter(0), imm);
      OpIndex binop = m.Emit(inst.mi.op, shifted_operand, m.Parameter(1));
      OpIndex comp = m.Emit(cmp.mi.op, binop, m.Int32Constant(0));
      m.Return(comp);
      Stream s = m.Build();
      // Cmn does not support ROR shifts.
      if (inst.no_output_opcode == kArm64Cmn32 &&
          shift.mi.arch_opcode == kArm64Ror32) {
        ASSERT_EQ(2U, s.size());
        continue;
      }
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(inst.no_output_opcode, s[0]->arch_opcode());
      EXPECT_EQ(shift.mode, s[0]->addressing_mode());
      EXPECT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(5, s.ToInt64(s[0]->InputAt(2)));
      EXPECT_EQ(1U, s[0]->OutputCount());
      EXPECT_EQ(kFlags_set, s[0]->flags_mode());
      EXPECT_EQ(cmp.cond, s[0]->flags_condition());
    }
  }
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorFlagSettingTest,
                         ::testing::ValuesIn(kFlagSettingInstructions));

TEST_F(TurboshaftInstructionSelectorTest, TstInvalidImmediate) {
  // Make sure we do not generate an invalid immediate for TST.
  TRACED_FOREACH(IntegerCmp, cmp, kBinopCmpZeroRightInstructions) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    // 5 is not a valid constant for TST.
    OpIndex imm = m.Int32Constant(5);
    OpIndex binop = m.Word32BitwiseAnd(imm, m.Parameter(0));
    OpIndex comp = m.Emit(cmp.mi.op, binop, m.Int32Constant(0));
    m.Return(comp);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(kArm64Tst32, s[0]->arch_opcode());
    EXPECT_NE(InstructionOperand::IMMEDIATE, s[0]->InputAt(0)->kind());
    EXPECT_NE(InstructionOperand::IMMEDIATE, s[0]->InputAt(1)->kind());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(cmp.cond, s[0]->flags_condition());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, CommuteAddsExtend) {
  // Extended left-hand side operand.
  TRACED_FOREACH(IntegerCmp, cmp, kBinopCmpZeroRightInstructions) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    OpIndex extend = m.Word32ShiftRightArithmetic(
        m.Word32ShiftLeft(m.Parameter(0), m.Int32Constant(24)),
        m.Int32Constant(24));
    OpIndex binop = m.Word32Add(extend, m.Parameter(1));
    m.Return(m.Emit(cmp.mi.op, binop, m.Int32Constant(0)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Cmn32, s[0]->arch_opcode());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(cmp.cond, s[0]->flags_condition());
    EXPECT_EQ(kMode_Operand2_R_SXTB, s[0]->addressing_mode());
  }
}

// -----------------------------------------------------------------------------
// Miscellaneous

static const MachInst2 kLogicalWithNotRHSs[] = {
    {TSBinop::kWord32BitwiseAnd, "Word32BitwiseAnd", kArm64Bic32,
     MachineType::Int32()},
    {TSBinop::kWord64BitwiseAnd, "Word64BitwiseAnd", kArm64Bic,
     MachineType::Int64()},
    {TSBinop::kWord32BitwiseOr, "Word32BitwiseOr", kArm64Orn32,
     MachineType::Int32()},
    {TSBinop::kWord64BitwiseOr, "Word64BitwiseOr", kArm64Orn,
     MachineType::Int64()},
    {TSBinop::kWord32BitwiseXor, "Word32BitwiseXor", kArm64Eon32,
     MachineType::Int32()},
    {TSBinop::kWord64BitwiseXor, "Word64BitwiseXor", kArm64Eon,
     MachineType::Int64()}};

using TurboshaftInstructionSelectorLogicalWithNotRHSTest =
    TurboshaftInstructionSelectorTestWithParam<MachInst2>;

TEST_P(TurboshaftInstructionSelectorLogicalWithNotRHSTest, Parameter) {
  const MachInst2 inst = GetParam();
  const MachineType type = inst.machine_type;
  // Test cases where RHS is Xor(x, -1).
  {
    StreamBuilder m(this, type, type, type);
    if (type == MachineType::Int32()) {
      m.Return(m.Emit(inst.op, m.Parameter(0),
                      m.Word32BitwiseXor(m.Parameter(1), m.Int32Constant(-1))));
    } else {
      ASSERT_EQ(MachineType::Int64(), type);
      m.Return(m.Emit(inst.op, m.Parameter(0),
                      m.Word64BitwiseXor(m.Parameter(1), m.Int64Constant(-1))));
    }
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(inst.arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  {
    StreamBuilder m(this, type, type, type);
    if (type == MachineType::Int32()) {
      m.Return(m.Emit(inst.op,
                      m.Word32BitwiseXor(m.Parameter(0), m.Int32Constant(-1)),
                      m.Parameter(1)));
    } else {
      ASSERT_EQ(MachineType::Int64(), type);
      m.Return(m.Emit(inst.op,
                      m.Word64BitwiseXor(m.Parameter(0), m.Int64Constant(-1)),
                      m.Parameter(1)));
    }
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(inst.arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  // Test cases where RHS is Not(x).
  {
    StreamBuilder m(this, type, type, type);
    if (type == MachineType::Int32()) {
      m.Return(
          m.Emit(inst.op, m.Parameter(0), m.Word32BitwiseNot(m.Parameter(1))));
    } else {
      ASSERT_EQ(MachineType::Int64(), type);
      m.Return(
          m.Emit(inst.op, m.Parameter(0), m.Word64BitwiseNot(m.Parameter(1))));
    }
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(inst.arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  {
    StreamBuilder m(this, type, type, type);
    if (type == MachineType::Int32()) {
      m.Return(
          m.Emit(inst.op, m.Word32BitwiseNot(m.Parameter(0)), m.Parameter(1)));
    } else {
      ASSERT_EQ(MachineType::Int64(), type);
      m.Return(
          m.Emit(inst.op, m.Word64BitwiseNot(m.Parameter(0)), m.Parameter(1)));
    }
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(inst.arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorLogicalWithNotRHSTest,
                         ::testing::ValuesIn(kLogicalWithNotRHSs));

TEST_F(TurboshaftInstructionSelectorTest, Word32BitwiseNotWithParameter) {
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
  m.Return(m.Word32BitwiseNot(m.Parameter(0)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArm64Not32, s[0]->arch_opcode());
  EXPECT_EQ(1U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}

TEST_F(TurboshaftInstructionSelectorTest, Word64NotWithParameter) {
  StreamBuilder m(this, MachineType::Int64(), MachineType::Int64());
  m.Return(m.Word64BitwiseNot(m.Parameter(0)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArm64Not, s[0]->arch_opcode());
  EXPECT_EQ(1U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}

TEST_F(TurboshaftInstructionSelectorTest,
       Word32BitwiseXorMinusOneWithParameter) {
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(m.Word32BitwiseXor(m.Parameter(0), m.Int32Constant(-1)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Not32, s[0]->arch_opcode());
    EXPECT_EQ(1U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(m.Word32BitwiseXor(m.Int32Constant(-1), m.Parameter(0)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Not32, s[0]->arch_opcode());
    EXPECT_EQ(1U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, Word64XorMinusOneWithParameter) {
  {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int64());
    m.Return(m.Word64BitwiseXor(m.Parameter(0), m.Int64Constant(-1)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Not, s[0]->arch_opcode());
    EXPECT_EQ(1U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int64());
    m.Return(m.Word64BitwiseXor(m.Int64Constant(-1), m.Parameter(0)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Not, s[0]->arch_opcode());
    EXPECT_EQ(1U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_F(TurboshaftInstructionSelectorTest,
       Word32ShiftRightLogicalWithWord32AndWithImmediate) {
  // The available shift operand range is `0 <= imm < 32`, but we also test
  // that immediates outside this range are handled properly (modulo-32).
  TRACED_FORRANGE(int32_t, shift, -32, 63) {
    int32_t lsb = shift & 0x1F;
    TRACED_FORRANGE(int32_t, width, 1, 32 - lsb) {
      uint32_t jnk = rng()->NextInt();
      jnk = (lsb > 0) ? (jnk >> (32 - lsb)) : 0;
      uint32_t msk = ((0xFFFFFFFFu >> (32 - width)) << lsb) | jnk;
      StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
      m.Return(m.Word32ShiftRightLogical(
          m.Word32BitwiseAnd(m.Parameter(0), m.Int32Constant(msk)),
          m.Int32Constant(shift)));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(kArm64Ubfx32, s[0]->arch_opcode());
      ASSERT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(lsb, s.ToInt32(s[0]->InputAt(1)));
      EXPECT_EQ(width, s.ToInt32(s[0]->InputAt(2)));
    }
  }
  TRACED_FORRANGE(int32_t, shift, -32, 63) {
    int32_t lsb = shift & 0x1F;
    TRACED_FORRANGE(int32_t, width, 1, 32 - lsb) {
      uint32_t jnk = rng()->NextInt();
      jnk = (lsb > 0) ? (jnk >> (32 - lsb)) : 0;
      uint32_t msk = ((0xFFFFFFFFu >> (32 - width)) << lsb) | jnk;
      StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
      m.Return(m.Word32ShiftRightLogical(
          m.Word32BitwiseAnd(m.Int32Constant(msk), m.Parameter(0)),
          m.Int32Constant(shift)));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(kArm64Ubfx32, s[0]->arch_opcode());
      ASSERT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(lsb, s.ToInt32(s[0]->InputAt(1)));
      EXPECT_EQ(width, s.ToInt32(s[0]->InputAt(2)));
    }
  }
}

TEST_F(TurboshaftInstructionSelectorTest,
       Word64ShiftRightLogicalWithWord64AndWithImmediate) {
  // The available shift operand range is `0 <= imm < 64`, but we also test
  // that immediates outside this range are handled properly (modulo-64).
  TRACED_FORRANGE(int32_t, shift, -64, 127) {
    int32_t lsb = shift & 0x3F;
    TRACED_FORRANGE(int32_t, width, 1, 64 - lsb) {
      uint64_t jnk = rng()->NextInt64();
      jnk = (lsb > 0) ? (jnk >> (64 - lsb)) : 0;
      uint64_t msk =
          ((uint64_t{0xFFFFFFFFFFFFFFFF} >> (64 - width)) << lsb) | jnk;
      StreamBuilder m(this, MachineType::Int64(), MachineType::Int64());
      m.Return(m.Word64ShiftRightLogical(
          m.Word64BitwiseAnd(m.Parameter(0), m.Int64Constant(msk)),
          m.Int32Constant(shift)));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(kArm64Ubfx, s[0]->arch_opcode());
      ASSERT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(lsb, s.ToInt64(s[0]->InputAt(1)));
      EXPECT_EQ(width, s.ToInt64(s[0]->InputAt(2)));
    }
  }
  TRACED_FORRANGE(int32_t, shift, -64, 127) {
    int32_t lsb = shift & 0x3F;
    TRACED_FORRANGE(int32_t, width, 1, 64 - lsb) {
      uint64_t jnk = rng()->NextInt64();
      jnk = (lsb > 0) ? (jnk >> (64 - lsb)) : 0;
      uint64_t msk =
          ((uint64_t{0xFFFFFFFFFFFFFFFF} >> (64 - width)) << lsb) | jnk;
      StreamBuilder m(this, MachineType::Int64(), MachineType::Int64());
      m.Return(m.Word64ShiftRightLogical(
          m.Word64BitwiseAnd(m.Int64Constant(msk), m.Parameter(0)),
          m.Int32Constant(shift)));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(kArm64Ubfx, s[0]->arch_opcode());
      ASSERT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(lsb, s.ToInt64(s[0]->InputAt(1)));
      EXPECT_EQ(width, s.ToInt64(s[0]->InputAt(2)));
    }
  }
}

TEST_F(TurboshaftInstructionSelectorTest,
       Word32AndWithImmediateWithWord32ShiftRightLogical) {
  // The available shift operand range is `0 <= imm < 32`, but we also test
  // that immediates outside this range are handled properly (modulo-32).
  TRACED_FORRANGE(int32_t, shift, -32, 63) {
    int32_t lsb = shift & 0x1F;
    TRACED_FORRANGE(int32_t, width, 1, 31) {
      uint32_t msk = (1u << width) - 1;
      StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
      m.Return(m.Word32BitwiseAnd(
          m.Word32ShiftRightLogical(m.Parameter(0), m.Int32Constant(shift)),
          m.Int32Constant(msk)));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(kArm64Ubfx32, s[0]->arch_opcode());
      ASSERT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(lsb, s.ToInt32(s[0]->InputAt(1)));
      int32_t actual_width = (lsb + width > 32) ? (32 - lsb) : width;
      EXPECT_EQ(actual_width, s.ToInt32(s[0]->InputAt(2)));
    }
  }
  TRACED_FORRANGE(int32_t, shift, -32, 63) {
    int32_t lsb = shift & 0x1F;
    TRACED_FORRANGE(int32_t, width, 1, 31) {
      uint32_t msk = (1u << width) - 1;
      StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
      m.Return(m.Word32BitwiseAnd(
          m.Int32Constant(msk),
          m.Word32ShiftRightLogical(m.Parameter(0), m.Int32Constant(shift))));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(kArm64Ubfx32, s[0]->arch_opcode());
      ASSERT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(lsb, s.ToInt32(s[0]->InputAt(1)));
      int32_t actual_width = (lsb + width > 32) ? (32 - lsb) : width;
      EXPECT_EQ(actual_width, s.ToInt32(s[0]->InputAt(2)));
    }
  }
}

TEST_F(TurboshaftInstructionSelectorTest,
       Word64AndWithImmediateWithWord64ShiftRightLogical) {
  // The available shift operand range is `0 <= imm < 64`, but we also test
  // that immediates outside this range are handled properly (modulo-64).
  TRACED_FORRANGE(int32_t, shift, -64, 127) {
    int64_t lsb = shift & 0x3F;
    TRACED_FORRANGE(int64_t, width, 1, 63) {
      uint64_t msk = (uint64_t{1} << width) - 1;
      StreamBuilder m(this, MachineType::Int64(), MachineType::Int64());
      m.Return(m.Word64BitwiseAnd(
          m.Word64ShiftRightLogical(m.Parameter(0), m.Int32Constant(shift)),
          m.Int64Constant(msk)));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(kArm64Ubfx, s[0]->arch_opcode());
      ASSERT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(lsb, s.ToInt64(s[0]->InputAt(1)));
      int64_t actual_width = (lsb + width > 64) ? (64 - lsb) : width;
      EXPECT_EQ(actual_width, s.ToInt64(s[0]->InputAt(2)));
    }
  }
  TRACED_FORRANGE(int32_t, shift, -64, 127) {
    int64_t lsb = shift & 0x3F;
    TRACED_FORRANGE(int64_t, width, 1, 63) {
      uint64_t msk = (uint64_t{1} << width) - 1;
      StreamBuilder m(this, MachineType::Int64(), MachineType::Int64());
      m.Return(m.Word64BitwiseAnd(
          m.Int64Constant(msk),
          m.Word64ShiftRightLogical(m.Parameter(0), m.Int32Constant(shift))));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(kArm64Ubfx, s[0]->arch_opcode());
      ASSERT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(lsb, s.ToInt64(s[0]->InputAt(1)));
      int64_t actual_width = (lsb + width > 64) ? (64 - lsb) : width;
      EXPECT_EQ(actual_width, s.ToInt64(s[0]->InputAt(2)));
    }
  }
}

TEST_F(TurboshaftInstructionSelectorTest, Word32MulHighWithParameters) {
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  OpIndex const p0 = m.Parameter(0);
  OpIndex const p1 = m.Parameter(1);
  OpIndex const n = m.Int32MulOverflownBits(p0, p1);
  m.Return(n);
  Stream s = m.Build();
  ASSERT_EQ(2U, s.size());
  EXPECT_EQ(kArm64Smull, s[0]->arch_opcode());
  ASSERT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kArm64Asr, s[1]->arch_opcode());
  ASSERT_EQ(2U, s[1]->InputCount());
  EXPECT_EQ(s.ToVreg(s[0]->Output()), s.ToVreg(s[1]->InputAt(0)));
  EXPECT_EQ(32, s.ToInt64(s[1]->InputAt(1)));
  ASSERT_EQ(1U, s[1]->OutputCount());
  EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[1]->Output()));
}

TEST_F(TurboshaftInstructionSelectorTest, Word32MulHighWithSar) {
  TRACED_FORRANGE(int32_t, shift, -32, 63) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    OpIndex const n = m.Word32ShiftRightArithmetic(
        m.Int32MulOverflownBits(p0, p1), m.Int32Constant(shift));
    m.Return(n);
    Stream s = m.Build();
    ASSERT_EQ(2U, s.size());
    EXPECT_EQ(kArm64Smull, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(kArm64Asr, s[1]->arch_opcode());
    ASSERT_EQ(2U, s[1]->InputCount());
    EXPECT_EQ(s.ToVreg(s[0]->Output()), s.ToVreg(s[1]->InputAt(0)));
    EXPECT_EQ((shift & 0x1F) + 32, s.ToInt64(s[1]->InputAt(1)));
    ASSERT_EQ(1U, s[1]->OutputCount());
    EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[1]->Output()));
  }
}

TEST_F(TurboshaftInstructionSelectorTest, Word32MulHighWithAdd) {
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  OpIndex const p0 = m.Parameter(0);
  OpIndex const p1 = m.Parameter(1);
  OpIndex const a = m.Word32Add(m.Int32MulOverflownBits(p0, p1), p0);
  // Test only one shift constant here, as we're only interested in it being a
  // 32-bit operation; the shift amount is irrelevant.
  OpIndex const n = m.Word32ShiftRightArithmetic(a, m.Int32Constant(1));
  m.Return(n);
  Stream s = m.Build();
  ASSERT_EQ(3U, s.size());
  EXPECT_EQ(kArm64Smull, s[0]->arch_opcode());
  ASSERT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kArm64Add, s[1]->arch_opcode());
  EXPECT_EQ(kMode_Operand2_R_ASR_I, s[1]->addressing_mode());
  ASSERT_EQ(3U, s[1]->InputCount());
  EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[1]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(s[0]->Output()), s.ToVreg(s[1]->InputAt(1)));
  EXPECT_EQ(32, s.ToInt64(s[1]->InputAt(2)));
  ASSERT_EQ(1U, s[1]->OutputCount());
  EXPECT_EQ(kArm64Asr32, s[2]->arch_opcode());
  ASSERT_EQ(2U, s[2]->InputCount());
  EXPECT_EQ(s.ToVreg(s[1]->Output()), s.ToVreg(s[2]->InputAt(0)));
  EXPECT_EQ(1, s.ToInt64(s[2]->InputAt(1)));
  ASSERT_EQ(1U, s[2]->OutputCount());
  EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[2]->Output()));
}

TEST_F(TurboshaftInstructionSelectorTest, Uint32MulHighWithShr) {
  TRACED_FORRANGE(int32_t, shift, -32, 63) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    OpIndex const n = m.Word32ShiftRightLogical(
        m.Uint32MulOverflownBits(p0, p1), m.Int32Constant(shift));
    m.Return(n);
    Stream s = m.Build();
    ASSERT_EQ(2U, s.size());
    EXPECT_EQ(kArm64Umull, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(kArm64Lsr, s[1]->arch_opcode());
    ASSERT_EQ(2U, s[1]->InputCount());
    EXPECT_EQ(s.ToVreg(s[0]->Output()), s.ToVreg(s[1]->InputAt(0)));
    EXPECT_EQ((shift & 0x1F) + 32, s.ToInt64(s[1]->InputAt(1)));
    ASSERT_EQ(1U, s[1]->OutputCount());
    EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[1]->Output()));
  }
}

TEST_F(TurboshaftInstructionSelectorTest, Word32SarWithWord32Shl) {
  TRACED_FORRANGE(int32_t, shift, 1, 31) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const r = m.Word32ShiftRightArithmetic(
        m.Word32ShiftLeft(p0, m.Int32Constant(shift)), m.Int32Constant(shift));
    m.Return(r);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Sbfx32, s[0]->arch_opcode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(r), s.ToVreg(s[0]->Output()));
  }
  TRACED_FORRANGE(int32_t, shift, 1, 31) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const r = m.Word32ShiftRightArithmetic(
        m.Word32ShiftLeft(p0, m.Int32Constant(shift + 32)),
        m.Int32Constant(shift + 64));
    m.Return(r);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Sbfx32, s[0]->arch_opcode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(r), s.ToVreg(s[0]->Output()));
  }
}

TEST_F(TurboshaftInstructionSelectorTest,
       Word32ShiftRightLogicalWithWord32Shl) {
  TRACED_FORRANGE(int32_t, shift, 1, 31) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const r = m.Word32ShiftRightLogical(
        m.Word32ShiftLeft(p0, m.Int32Constant(shift)), m.Int32Constant(shift));
    m.Return(r);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Ubfx32, s[0]->arch_opcode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(r), s.ToVreg(s[0]->Output()));
  }
  TRACED_FORRANGE(int32_t, shift, 1, 31) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const r = m.Word32ShiftRightLogical(
        m.Word32ShiftLeft(p0, m.Int32Constant(shift + 32)),
        m.Int32Constant(shift + 64));
    m.Return(r);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Ubfx32, s[0]->arch_opcode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(r), s.ToVreg(s[0]->Output()));
  }
}

TEST_F(TurboshaftInstructionSelectorTest, Word32ShlWithWord32And) {
  TRACED_FORRANGE(int32_t, shift, 1, 30) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const r = m.Word32ShiftLeft(
        m.Word32BitwiseAnd(p0, m.Int32Constant((1 << (31 - shift)) - 1)),
        m.Int32Constant(shift));
    m.Return(r);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Ubfiz32, s[0]->arch_opcode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(r), s.ToVreg(s[0]->Output()));
  }
  TRACED_FORRANGE(int32_t, shift, 0, 30) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const r = m.Word32ShiftLeft(
        m.Word32BitwiseAnd(p0, m.Int32Constant((1u << (31 - shift)) - 1)),
        m.Int32Constant(shift + 1));
    m.Return(r);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Lsl32, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(r), s.ToVreg(s[0]->Output()));
  }
}

TEST_F(TurboshaftInstructionSelectorTest, Word32Clz) {
  StreamBuilder m(this, MachineType::Uint32(), MachineType::Uint32());
  OpIndex const p0 = m.Parameter(0);
  OpIndex const n = m.Word32CountLeadingZeros(p0);
  m.Return(n);
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArm64Clz32, s[0]->arch_opcode());
  ASSERT_EQ(1U, s[0]->InputCount());
  EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
}

TEST_F(TurboshaftInstructionSelectorTest, Float32Abs) {
  StreamBuilder m(this, MachineType::Float32(), MachineType::Float32());
  OpIndex const p0 = m.Parameter(0);
  OpIndex const n = m.Float32Abs(p0);
  m.Return(n);
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArm64Float32Abs, s[0]->arch_opcode());
  ASSERT_EQ(1U, s[0]->InputCount());
  EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
}

TEST_F(TurboshaftInstructionSelectorTest, Float64Abs) {
  StreamBuilder m(this, MachineType::Float64(), MachineType::Float64());
  OpIndex const p0 = m.Parameter(0);
  OpIndex const n = m.Float64Abs(p0);
  m.Return(n);
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArm64Float64Abs, s[0]->arch_opcode());
  ASSERT_EQ(1U, s[0]->InputCount());
  EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
}

TEST_F(TurboshaftInstructionSelectorTest, Float32Abd) {
  StreamBuilder m(this, MachineType::Float32(), MachineType::Float32(),
                  MachineType::Float32());
  OpIndex const p0 = m.Parameter(0);
  OpIndex const p1 = m.Parameter(1);
  OpIndex const fsub = m.Float32Sub(p0, p1);
  OpIndex const fabs = m.Float32Abs(fsub);
  m.Return(fabs);
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArm64Float32Abd, s[0]->arch_opcode());
  ASSERT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(s.ToVreg(fabs), s.ToVreg(s[0]->Output()));
}

TEST_F(TurboshaftInstructionSelectorTest, Float64Abd) {
  StreamBuilder m(this, MachineType::Float64(), MachineType::Float64(),
                  MachineType::Float64());
  OpIndex const p0 = m.Parameter(0);
  OpIndex const p1 = m.Parameter(1);
  OpIndex const fsub = m.Float64Sub(p0, p1);
  OpIndex const fabs = m.Float64Abs(fsub);
  m.Return(fabs);
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArm64Float64Abd, s[0]->arch_opcode());
  ASSERT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(s.ToVreg(fabs), s.ToVreg(s[0]->Output()));
}

TEST_F(TurboshaftInstructionSelectorTest, Float64Max) {
  StreamBuilder m(this, MachineType::Float64(), MachineType::Float64(),
                  MachineType::Float64());
  OpIndex const p0 = m.Parameter(0);
  OpIndex const p1 = m.Parameter(1);
  OpIndex const n = m.Float64Max(p0, p1);
  m.Return(n);
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArm64Float64Max, s[0]->arch_opcode());
  ASSERT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
}

TEST_F(TurboshaftInstructionSelectorTest, Float64Min) {
  StreamBuilder m(this, MachineType::Float64(), MachineType::Float64(),
                  MachineType::Float64());
  OpIndex const p0 = m.Parameter(0);
  OpIndex const p1 = m.Parameter(1);
  OpIndex const n = m.Float64Min(p0, p1);
  m.Return(n);
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArm64Float64Min, s[0]->arch_opcode());
  ASSERT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
}

TEST_F(TurboshaftInstructionSelectorTest, Float32Neg) {
  StreamBuilder m(this, MachineType::Float32(), MachineType::Float32());
  OpIndex const p0 = m.Parameter(0);
  OpIndex const n = m.Float32Negate(m.Parameter(0));
  m.Return(n);
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArm64Float32Neg, s[0]->arch_opcode());
  ASSERT_EQ(1U, s[0]->InputCount());
  EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
}

TEST_F(TurboshaftInstructionSelectorTest, Float64Neg) {
  StreamBuilder m(this, MachineType::Float64(), MachineType::Float64());
  OpIndex const p0 = m.Parameter(0);
  OpIndex const n = m.Float64Negate(m.Parameter(0));
  m.Return(n);
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArm64Float64Neg, s[0]->arch_opcode());
  ASSERT_EQ(1U, s[0]->InputCount());
  EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
}

TEST_F(TurboshaftInstructionSelectorTest, Float32NegWithMul) {
  StreamBuilder m(this, MachineType::Float32(), MachineType::Float32(),
                  MachineType::Float32());
  OpIndex const p0 = m.Parameter(0);
  OpIndex const p1 = m.Parameter(1);
  OpIndex const n1 = m.Float32Mul(p0, p1);
  OpIndex const n2 = m.Float32Negate(n1);
  m.Return(n2);
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArm64Float32Fnmul, s[0]->arch_opcode());
  ASSERT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(s.ToVreg(n2), s.ToVreg(s[0]->Output()));
}

TEST_F(TurboshaftInstructionSelectorTest, Float64NegWithMul) {
  StreamBuilder m(this, MachineType::Float64(), MachineType::Float64(),
                  MachineType::Float64());
  OpIndex const p0 = m.Parameter(0);
  OpIndex const p1 = m.Parameter(1);
  OpIndex const n1 = m.Float64Mul(p0, p1);
  OpIndex const n2 = m.Float64Negate(n1);
  m.Return(n2);
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArm64Float64Fnmul, s[0]->arch_opcode());
  ASSERT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(s.ToVreg(n2), s.ToVreg(s[0]->Output()));
}

TEST_F(TurboshaftInstructionSelectorTest, Float32MulWithNeg) {
  StreamBuilder m(this, MachineType::Float32(), MachineType::Float32(),
                  MachineType::Float32());
  OpIndex const p0 = m.Parameter(0);
  OpIndex const p1 = m.Parameter(1);
  OpIndex const n1 = m.Float32Negate(p0);
  OpIndex const n2 = m.Float32Mul(n1, p1);
  m.Return(n2);
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArm64Float32Fnmul, s[0]->arch_opcode());
  ASSERT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(s.ToVreg(n2), s.ToVreg(s[0]->Output()));
}

TEST_F(TurboshaftInstructionSelectorTest, Float64MulWithNeg) {
  StreamBuilder m(this, MachineType::Float64(), MachineType::Float64(),
                  MachineType::Float64());
  OpIndex const p0 = m.Parameter(0);
  OpIndex const p1 = m.Parameter(1);
  OpIndex const n1 = m.Float64Negate(p0);
  OpIndex const n2 = m.Float64Mul(n1, p1);
  m.Return(n2);
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArm64Float64Fnmul, s[0]->arch_opcode());
  ASSERT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(s.ToVreg(n2), s.ToVreg(s[0]->Output()));
}

TEST_F(TurboshaftInstructionSelectorTest, LoadAndShiftRight) {
  {
    int32_t immediates[] = {-256, -255, -3,   -2,   -1,    0,    1,
                            2,    3,    255,  256,  260,   4096, 4100,
                            8192, 8196, 3276, 3280, 16376, 16380};
    TRACED_FOREACH(int32_t, index, immediates) {
      StreamBuilder m(this, MachineType::Uint64(), MachineType::Pointer());
      OpIndex const load = m.Load(MachineType::Uint64(), m.Parameter(0),
                                  m.Int64Constant(index - 4));
      OpIndex const sar =
          m.Word64ShiftRightArithmetic(load, m.Int32Constant(32));
      // Make sure we don't fold the shift into the following add:
      m.Return(m.Word64Add(sar, m.Parameter(0)));
      Stream s = m.Build();
      ASSERT_EQ(2U, s.size());
      EXPECT_EQ(kArm64Ldrsw, s[0]->arch_opcode());
      EXPECT_EQ(kMode_MRI, s[0]->addressing_mode());
      EXPECT_EQ(2U, s[0]->InputCount());
      EXPECT_EQ(s.ToVreg(m.Parameter(0)), s.ToVreg(s[0]->InputAt(0)));
      ASSERT_EQ(InstructionOperand::IMMEDIATE, s[0]->InputAt(1)->kind());
      EXPECT_EQ(index, s.ToInt32(s[0]->InputAt(1)));
      ASSERT_EQ(1U, s[0]->OutputCount());
    }
  }
}

TEST_F(TurboshaftInstructionSelectorTest, CompareAgainstZero32) {
  TRACED_FOREACH(IntegerCmp, cmp, kBinopCmpZeroRightInstructions) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    OpIndex const param = m.Parameter(0);
    Block *a = m.NewBlock(), *b = m.NewBlock();
    m.Branch(m.Emit<Word32>(cmp.mi.op, param, m.Int32Constant(0)), a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(1));
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(s.ToVreg(param), s.ToVreg(s[0]->InputAt(0)));
    if (cmp.cond == kNegative || cmp.cond == kPositiveOrZero) {
      EXPECT_EQ(kArm64TestAndBranch32, s[0]->arch_opcode());
      EXPECT_EQ(4U, s[0]->InputCount());  // The labels are also inputs.
      EXPECT_EQ((cmp.cond == kNegative) ? kNotEqual : kEqual,
                s[0]->flags_condition());
      EXPECT_EQ(InstructionOperand::IMMEDIATE, s[0]->InputAt(1)->kind());
      EXPECT_EQ(31, s.ToInt32(s[0]->InputAt(1)));
    } else {
      EXPECT_EQ(kArm64CompareAndBranch32, s[0]->arch_opcode());
      EXPECT_EQ(3U, s[0]->InputCount());  // The labels are also inputs.
      EXPECT_EQ(cmp.cond, s[0]->flags_condition());
    }
  }
}

TEST_F(TurboshaftInstructionSelectorTest, CompareAgainstZero64) {
  TRACED_FOREACH(IntegerCmp, cmp, kBinop64CmpZeroRightInstructions) {
    StreamBuilder m(this, MachineType::Int64(), MachineType::Int64());
    OpIndex const param = m.Parameter(0);
    Block *a = m.NewBlock(), *b = m.NewBlock();
    m.Branch(m.Emit<Word32>(cmp.mi.op, param, m.Int64Constant(0)), a, b);
    m.Bind(a);
    m.Return(m.Int64Constant(1));
    m.Bind(b);
    m.Return(m.Int64Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(s.ToVreg(param), s.ToVreg(s[0]->InputAt(0)));
    if (cmp.cond == kNegative || cmp.cond == kPositiveOrZero) {
      EXPECT_EQ(kArm64TestAndBranch, s[0]->arch_opcode());
      EXPECT_EQ(4U, s[0]->InputCount());  // The labels are also inputs.
      EXPECT_EQ((cmp.cond == kNegative) ? kNotEqual : kEqual,
                s[0]->flags_condition());
      EXPECT_EQ(InstructionOperand::IMMEDIATE, s[0]->InputAt(1)->kind());
      EXPECT_EQ(63, s.ToInt32(s[0]->InputAt(1)));
    } else {
      EXPECT_EQ(kArm64CompareAndBranch, s[0]->arch_opcode());
      EXPECT_EQ(3U, s[0]->InputCount());  // The labels are also inputs.
      EXPECT_EQ(cmp.cond, s[0]->flags_condition());
    }
  }
}

TEST_F(TurboshaftInstructionSelectorTest, CompareFloat64HighLessThanZero64) {
  StreamBuilder m(this, MachineType::Int32(), MachineType::Float64());
  OpIndex const param = m.Parameter(0);
  OpIndex const high = m.Float64ExtractHighWord32(param);
  Block *a = m.NewBlock(), *b = m.NewBlock();
  m.Branch(m.Int32LessThan(high, m.Int32Constant(0)), a, b);
  m.Bind(a);
  m.Return(m.Int32Constant(1));
  m.Bind(b);
  m.Return(m.Int32Constant(0));
  Stream s = m.Build();
  ASSERT_EQ(2U, s.size());
  EXPECT_EQ(kArm64U64MoveFloat64, s[0]->arch_opcode());
  EXPECT_EQ(kArm64TestAndBranch, s[1]->arch_opcode());
  EXPECT_EQ(kNotEqual, s[1]->flags_condition());
  EXPECT_EQ(4U, s[1]->InputCount());
  EXPECT_EQ(InstructionOperand::IMMEDIATE, s[1]->InputAt(1)->kind());
  EXPECT_EQ(63, s.ToInt32(s[1]->InputAt(1)));
}

TEST_F(TurboshaftInstructionSelectorTest,
       CompareFloat64HighGreaterThanOrEqualZero64) {
  StreamBuilder m(this, MachineType::Int32(), MachineType::Float64());
  OpIndex const param = m.Parameter(0);
  OpIndex const high = m.Float64ExtractHighWord32(param);
  Block *a = m.NewBlock(), *b = m.NewBlock();
  m.Branch(m.Int32GreaterThanOrEqual(high, m.Int32Constant(0)), a, b);
  m.Bind(a);
  m.Return(m.Int32Constant(1));
  m.Bind(b);
  m.Return(m.Int32Constant(0));
  Stream s = m.Build();
  ASSERT_EQ(2U, s.size());
  EXPECT_EQ(kArm64U64MoveFloat64, s[0]->arch_opcode());
  EXPECT_EQ(kArm64TestAndBranch, s[1]->arch_opcode());
  EXPECT_EQ(kEqual, s[1]->flags_condition());
  EXPECT_EQ(4U, s[1]->InputCount());
  EXPECT_EQ(InstructionOperand::IMMEDIATE, s[1]->InputAt(1)->kind());
  EXPECT_EQ(63, s.ToInt32(s[1]->InputAt(1)));
}

TEST_F(TurboshaftInstructionSelectorTest, ExternalReferenceLoad1) {
  // Test offsets we can use kMode_Root for.
  const int64_t kOffsets[] = {0, 1, 4, INT32_MIN, INT32_MAX};
  TRACED_FOREACH(int64_t, offset, kOffsets) {
    StreamBuilder m(this, MachineType::Int64());
    ExternalReference reference =
        base::bit_cast<ExternalReference>(isolate()->isolate_root() + offset);
    OpIndex const value =
        m.Load(MachineType::Int64(), m.ExternalConstant(reference));
    m.Return(value);

    Stream s = m.Build();

    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArm64Ldr, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Root, s[0]->addressing_mode());
    EXPECT_EQ(1U, s[0]->InputCount());
    EXPECT_EQ(s.ToInt64(s[0]->InputAt(0)), offset);
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, ExternalReferenceLoad2) {
  // Offset too large, we cannot use kMode_Root.
  StreamBuilder m(this, MachineType::Int64());
  int64_t offset = 0x100000000;
  ExternalReference reference =
      base::bit_cast<ExternalReference>(isolate()->isolate_root() + offset);
  OpIndex const value =
      m.Load(MachineType::Int64(), m.ExternalConstant(reference));
  m.Return(value);

  Stream s = m.Build();

  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArm64Ldr, s[0]->arch_opcode());
  EXPECT_NE(kMode_Root, s[0]->addressing_mode());
}

namespace {
// Builds a call with the specified signature and nodes as arguments.
// Then checks that the correct number of kArm64Poke and kArm64PokePair were
// generated.
void TestPokePair(TurboshaftInstructionSelectorTest::StreamBuilder* m,
                  Zone* zone, MachineSignature::Builder* builder,
                  base::Vector<const OpIndex> args, int expected_poke_pair,
                  int expected_poke) {
  auto call_descriptor = TurboshaftInstructionSelectorTest::StreamBuilder::
      MakeSimpleTSCallDescriptor(zone, builder->Build());

  OpIndex callee = m->Int64Constant(0);
  m->Call(callee, OpIndex::Invalid(), args, call_descriptor);
  m->Return(m->UndefinedConstant());

  auto s = m->Build();
  int num_poke_pair = 0;
  int num_poke = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i]->arch_opcode() == kArm64PokePair) {
      num_poke_pair++;
    }

    if (s[i]->arch_opcode() == kArm64Poke) {
      num_poke++;
    }
  }

  EXPECT_EQ(expected_poke_pair, num_poke_pair);
  EXPECT_EQ(expected_poke, num_poke);
}
}  // namespace

TEST_F(TurboshaftInstructionSelectorTest, PokePairPrepareArgumentsInt32) {
  {
    MachineSignature::Builder builder(zone(), 0, 3);
    builder.AddParam(MachineType::Int32());
    builder.AddParam(MachineType::Int32());
    builder.AddParam(MachineType::Int32());

    StreamBuilder m(this, MachineType::AnyTagged());
    OpIndex nodes[] = {
        m.Int32Constant(0),
        m.Int32Constant(0),
        m.Int32Constant(0),
    };

    const int expected_poke_pair = 1;
    // Note: The `+ 1` here comes from the padding Poke in
    // EmitPrepareArguments.
    const int expected_poke = 1 + 1;

    TestPokePair(&m, zone(), &builder, base::VectorOf(nodes, arraysize(nodes)),
                 expected_poke_pair, expected_poke);
  }

  {
    MachineSignature::Builder builder(zone(), 0, 4);
    builder.AddParam(MachineType::Int32());
    builder.AddParam(MachineType::Int32());
    builder.AddParam(MachineType::Int32());
    builder.AddParam(MachineType::Int32());

    StreamBuilder m(this, MachineType::AnyTagged());
    OpIndex nodes[] = {
        m.Int32Constant(0),
        m.Int32Constant(0),
        m.Int32Constant(0),
        m.Int32Constant(0),
    };

    const int expected_poke_pair = 2;
    const int expected_poke = 0;

    TestPokePair(&m, zone(), &builder, base::VectorOf(nodes, arraysize(nodes)),
                 expected_poke_pair, expected_poke);
  }
}

TEST_F(TurboshaftInstructionSelectorTest, PokePairPrepareArgumentsInt64) {
  MachineSignature::Builder builder(zone(), 0, 4);
  builder.AddParam(MachineType::Int64());
  builder.AddParam(MachineType::Int64());
  builder.AddParam(MachineType::Int64());
  builder.AddParam(MachineType::Int64());

  StreamBuilder m(this, MachineType::AnyTagged());
  OpIndex nodes[] = {
      m.Int64Constant(0),
      m.Int64Constant(0),
      m.Int64Constant(0),
      m.Int64Constant(0),
  };

  const int expected_poke_pair = 2;
  const int expected_poke = 0;

  TestPokePair(&m, zone(), &builder, base::VectorOf(nodes, arraysize(nodes)),
               expected_poke_pair, expected_poke);
}

TEST_F(TurboshaftInstructionSelectorTest, PokePairPrepareArgumentsFloat32) {
  MachineSignature::Builder builder(zone(), 0, 4);
  builder.AddParam(MachineType::Float32());
  builder.AddParam(MachineType::Float32());
  builder.AddParam(MachineType::Float32());
  builder.AddParam(MachineType::Float32());

  StreamBuilder m(this, MachineType::AnyTagged());
  OpIndex nodes[] = {
      m.Float32Constant(0.0f),
      m.Float32Constant(0.0f),
      m.Float32Constant(0.0f),
      m.Float32Constant(0.0f),
  };

  const int expected_poke_pair = 2;
  const int expected_poke = 0;

  TestPokePair(&m, zone(), &builder, base::VectorOf(nodes, arraysize(nodes)),
               expected_poke_pair, expected_poke);
}

TEST_F(TurboshaftInstructionSelectorTest, PokePairPrepareArgumentsFloat64) {
  MachineSignature::Builder builder(zone(), 0, 4);
  builder.AddParam(MachineType::Float64());
  builder.AddParam(MachineType::Float64());
  builder.AddParam(MachineType::Float64());
  builder.AddParam(MachineType::Float64());

  StreamBuilder m(this, MachineType::AnyTagged());
  OpIndex nodes[] = {
      m.Float64Constant(0.0f),
      m.Float64Constant(0.0f),
      m.Float64Constant(0.0f),
      m.Float64Constant(0.0f),
  };

  const int expected_poke_pair = 2;
  const int expected_poke = 0;

  TestPokePair(&m, zone(), &builder, base::VectorOf(nodes, arraysize(nodes)),
               expected_poke_pair, expected_poke);
}

TEST_F(TurboshaftInstructionSelectorTest,
       PokePairPrepareArgumentsIntFloatMixed) {
  {
    MachineSignature::Builder builder(zone(), 0, 4);
    builder.AddParam(MachineType::Int32());
    builder.AddParam(MachineType::Float32());
    builder.AddParam(MachineType::Int32());
    builder.AddParam(MachineType::Float32());

    StreamBuilder m(this, MachineType::AnyTagged());
    OpIndex nodes[] = {
        m.Int32Constant(0),
        m.Float32Constant(0.0f),
        m.Int32Constant(0),
        m.Float32Constant(0.0f),
    };

    const int expected_poke_pair = 0;
    const int expected_poke = 4;

    TestPokePair(&m, zone(), &builder, base::VectorOf(nodes, arraysize(nodes)),
                 expected_poke_pair, expected_poke);
  }

  {
    MachineSignature::Builder builder(zone(), 0, 7);
    builder.AddParam(MachineType::Float32());
    builder.AddParam(MachineType::Int32());
    builder.AddParam(MachineType::Int32());
    builder.AddParam(MachineType::Float64());
    builder.AddParam(MachineType::Int64());
    builder.AddParam(MachineType::Float64());
    builder.AddParam(MachineType::Float64());

    StreamBuilder m(this, MachineType::AnyTagged());
    OpIndex nodes[] = {m.Float32Constant(0.0f), m.Int32Constant(0),
                       m.Int32Constant(0),      m.Float64Constant(0.0f),
                       m.Int64Constant(0),      m.Float64Constant(0.0f),
                       m.Float64Constant(0.0f)};

    const int expected_poke_pair = 2;

    // Note: The `+ 1` here comes from the padding Poke in
    // EmitPrepareArguments.
    const int expected_poke = 3 + 1;

    TestPokePair(&m, zone(), &builder, base::VectorOf(nodes, arraysize(nodes)),
                 expected_poke_pair, expected_poke);
  }
}

#if V8_ENABLE_WEBASSEMBLY
TEST_F(TurboshaftInstructionSelectorTest, PokePairPrepareArgumentsSimd128) {
  MachineSignature::Builder builder(zone(), 0, 2);
  builder.AddParam(MachineType::Simd128());
  builder.AddParam(MachineType::Simd128());

  StreamBuilder m(this, MachineType::AnyTagged());
  OpIndex nodes[] = {
      m.Simd128Splat(m.Int32Constant(0), Simd128SplatOp::Kind::kI32x4),
      m.Simd128Splat(m.Int32Constant(0), Simd128SplatOp::Kind::kI32x4)};

  const int expected_poke_pair = 0;
  const int expected_poke = 2;

  // Using kArm64PokePair is not currently supported for Simd128.
  TestPokePair(&m, zone(), &builder, base::VectorOf(nodes, arraysize(nodes)),
               expected_poke_pair, expected_poke);
}

struct SIMDConstZeroCmTest {
  const bool is_zero;
  const uint8_t lane_size;
  TSBinop cm_operator;
  const ArchOpcode expected_op_left;
  const ArchOpcode expected_op_right;
  const size_t size;
};

static const SIMDConstZeroCmTest SIMDConstZeroCmTests[] = {
    {true, 8, TSBinop::kI8x16Eq, kArm64IEq, kArm64IEq, 1},
    {true, 8, TSBinop::kI8x16Ne, kArm64INe, kArm64INe, 1},
    {true, 8, TSBinop::kI8x16GeS, kArm64ILeS, kArm64IGeS, 1},
    {true, 8, TSBinop::kI8x16GtS, kArm64ILtS, kArm64IGtS, 1},
    {false, 8, TSBinop::kI8x16Eq, kArm64IEq, kArm64IEq, 2},
    {false, 8, TSBinop::kI8x16Ne, kArm64INe, kArm64INe, 2},
    {false, 8, TSBinop::kI8x16GeS, kArm64IGeS, kArm64IGeS, 2},
    {false, 8, TSBinop::kI8x16GtS, kArm64IGtS, kArm64IGtS, 2},
    {true, 16, TSBinop::kI16x8Eq, kArm64IEq, kArm64IEq, 1},
    {true, 16, TSBinop::kI16x8Ne, kArm64INe, kArm64INe, 1},
    {true, 16, TSBinop::kI16x8GeS, kArm64ILeS, kArm64IGeS, 1},
    {true, 16, TSBinop::kI16x8GtS, kArm64ILtS, kArm64IGtS, 1},
    {false, 16, TSBinop::kI16x8Eq, kArm64IEq, kArm64IEq, 2},
    {false, 16, TSBinop::kI16x8Ne, kArm64INe, kArm64INe, 2},
    {false, 16, TSBinop::kI16x8GeS, kArm64IGeS, kArm64IGeS, 2},
    {false, 16, TSBinop::kI16x8GtS, kArm64IGtS, kArm64IGtS, 2},
    {true, 32, TSBinop::kI32x4Eq, kArm64IEq, kArm64IEq, 1},
    {true, 32, TSBinop::kI32x4Ne, kArm64INe, kArm64INe, 1},
    {true, 32, TSBinop::kI32x4GeS, kArm64ILeS, kArm64IGeS, 1},
    {true, 32, TSBinop::kI32x4GtS, kArm64ILtS, kArm64IGtS, 1},
    {false, 32, TSBinop::kI32x4Eq, kArm64IEq, kArm64IEq, 2},
    {false, 32, TSBinop::kI32x4Ne, kArm64INe, kArm64INe, 2},
    {false, 32, TSBinop::kI32x4GeS, kArm64IGeS, kArm64IGeS, 2},
    {false, 32, TSBinop::kI32x4GtS, kArm64IGtS, kArm64IGtS, 2},
    {true, 64, TSBinop::kI64x2Eq, kArm64IEq, kArm64IEq, 1},
    {true, 64, TSBinop::kI64x2Ne, kArm64INe, kArm64INe, 1},
    {true, 64, TSBinop::kI64x2GeS, kArm64ILeS, kArm64IGeS, 1},
    {true, 64, TSBinop::kI64x2GtS, kArm64ILtS, kArm64IGtS, 1},
    {false, 64, TSBinop::kI64x2Eq, kArm64IEq, kArm64IEq, 2},
    {false, 64, TSBinop::kI64x2Ne, kArm64INe, kArm64INe, 2},
    {false, 64, TSBinop::kI64x2GeS, kArm64IGeS, kArm64IGeS, 2},
    {false, 64, TSBinop::kI64x2GtS, kArm64IGtS, kArm64IGtS, 2},
    {true, 64, TSBinop::kF64x2Eq, kArm64FEq, kArm64FEq, 1},
    {true, 64, TSBinop::kF64x2Ne, kArm64FNe, kArm64FNe, 1},
    {true, 64, TSBinop::kF64x2Lt, kArm64FGt, kArm64FLt, 1},
    {true, 64, TSBinop::kF64x2Le, kArm64FGe, kArm64FLe, 1},
    {false, 64, TSBinop::kF64x2Eq, kArm64FEq, kArm64FEq, 2},
    {false, 64, TSBinop::kF64x2Ne, kArm64FNe, kArm64FNe, 2},
    {false, 64, TSBinop::kF64x2Lt, kArm64FLt, kArm64FLt, 2},
    {false, 64, TSBinop::kF64x2Le, kArm64FLe, kArm64FLe, 2},
    {true, 32, TSBinop::kF32x4Eq, kArm64FEq, kArm64FEq, 1},
    {true, 32, TSBinop::kF32x4Ne, kArm64FNe, kArm64FNe, 1},
    {true, 32, TSBinop::kF32x4Lt, kArm64FGt, kArm64FLt, 1},
    {true, 32, TSBinop::kF32x4Le, kArm64FGe, kArm64FLe, 1},
    {false, 32, TSBinop::kF32x4Eq, kArm64FEq, kArm64FEq, 2},
    {false, 32, TSBinop::kF32x4Ne, kArm64FNe, kArm64FNe, 2},
    {false, 32, TSBinop::kF32x4Lt, kArm64FLt, kArm64FLt, 2},
    {false, 32, TSBinop::kF32x4Le, kArm64FLe, kArm64FLe, 2},
};

using TurboshaftInstructionSelectorSIMDConstZeroCmTest =
    TurboshaftInstructionSelectorTestWithParam<SIMDConstZeroCmTest>;

TEST_P(TurboshaftInstructionSelectorSIMDConstZeroCmTest, ConstZero) {
  const SIMDConstZeroCmTest param = GetParam();
  uint8_t data[16] = {};
  if (!param.is_zero) data[0] = 0xff;
  // Const node on the left
  {
    StreamBuilder m(this, MachineType::Simd128(), MachineType::Simd128());
    OpIndex cnst = m.Simd128Constant(data);
    OpIndex fcm = m.Emit(param.cm_operator, cnst, m.Parameter(0));
    m.Return(fcm);
    Stream s = m.Build();
    ASSERT_EQ(param.size, s.size());
    if (param.size == 1) {
      EXPECT_EQ(param.expected_op_left, s[0]->arch_opcode());
      EXPECT_EQ(1U, s[0]->InputCount());
      EXPECT_EQ(1U, s[0]->OutputCount());
      EXPECT_EQ(param.lane_size, LaneSizeField::decode(s[0]->opcode()));
    } else {
      EXPECT_EQ(kArm64S128Const, s[0]->arch_opcode());
      EXPECT_EQ(param.expected_op_left, s[1]->arch_opcode());
      EXPECT_EQ(2U, s[1]->InputCount());
      EXPECT_EQ(1U, s[1]->OutputCount());
      EXPECT_EQ(param.lane_size, LaneSizeField::decode(s[1]->opcode()));
    }
  }
  //  Const node on the right
  {
    StreamBuilder m(this, MachineType::Simd128(), MachineType::Simd128());
    OpIndex cnst = m.Simd128Constant(data);
    OpIndex fcm = m.Emit(param.cm_operator, m.Parameter(0), cnst);
    m.Return(fcm);
    Stream s = m.Build();
    ASSERT_EQ(param.size, s.size());
    if (param.size == 1) {
      EXPECT_EQ(param.expected_op_right, s[0]->arch_opcode());
      EXPECT_EQ(1U, s[0]->InputCount());
      EXPECT_EQ(1U, s[0]->OutputCount());
      EXPECT_EQ(param.lane_size, LaneSizeField::decode(s[0]->opcode()));
    } else {
      EXPECT_EQ(kArm64S128Const, s[0]->arch_opcode());
      EXPECT_EQ(param.expected_op_right, s[1]->arch_opcode());
      EXPECT_EQ(2U, s[1]->InputCount());
      EXPECT_EQ(1U, s[1]->OutputCount());
      EXPECT_EQ(param.lane_size, LaneSizeField::decode(s[1]->opcode()));
    }
  }
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorSIMDConstZeroCmTest,
                         ::testing::ValuesIn(SIMDConstZeroCmTests));

struct SIMDConstAndTest {
  const uint8_t data[16];
  TSBinop simd_op;
  const ArchOpcode expected_op;
  const bool symmetrical;
  const uint8_t lane_size;
  const uint8_t shift_amount;
  const int32_t expected_imm;
  const size_t size;
};

static const SIMDConstAndTest SIMDConstAndTests[] = {
    {{0xFF, 0xFE, 0xFF, 0xFE, 0xFF, 0xFE, 0xFF, 0xFE, 0xFF, 0xFE, 0xFF, 0xFE,
      0xFF, 0xFE, 0xFF, 0xFE},
     TSBinop::kS128And,
     kArm64S128AndNot,
     true,
     16,
     8,
     0x01,
     1},
    {{0xFE, 0xFF, 0xFE, 0xFF, 0xFE, 0xFF, 0xFE, 0xFF, 0xFE, 0xFF, 0xFE, 0xFF,
      0xFE, 0xFF, 0xFE, 0xFF},
     TSBinop::kS128And,
     kArm64S128AndNot,
     true,
     16,
     0,
     0x01,
     1},

    {{0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFE,
      0xFF, 0xFF, 0xFF, 0xFE},
     TSBinop::kS128And,
     kArm64S128AndNot,
     true,
     32,
     24,
     0x01,
     1},
    {{0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF,
      0xFF, 0xFF, 0xFE, 0xFF},
     TSBinop::kS128And,
     kArm64S128AndNot,
     true,
     32,
     16,
     0x01,
     1},
    {{0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF,
      0xFF, 0xFE, 0xFF, 0xFF},
     TSBinop::kS128And,
     kArm64S128AndNot,
     true,
     32,
     8,
     0x01,
     1},
    {{0xFE, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF,
      0xFE, 0xFF, 0xFF, 0xFF},
     TSBinop::kS128And,
     kArm64S128AndNot,
     true,
     32,
     0,
     0x01,
     1},

    {{0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
      0xEE, 0xEE, 0xEE, 0xEE},
     TSBinop::kS128And,
     kArm64S128And,
     true,
     0,
     0,
     0x00,
     2},

    {{0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01,
      0x00, 0x01, 0x00, 0x01},
     TSBinop::kS128AndNot,
     kArm64S128AndNot,
     false,
     16,
     8,
     0x01,
     1},
    {{0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00,
      0x01, 0x00, 0x01, 0x00},
     TSBinop::kS128AndNot,
     kArm64S128AndNot,
     false,
     16,
     0,
     0x01,
     1},

    {{0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
      0x00, 0x00, 0x00, 0x01},
     TSBinop::kS128AndNot,
     kArm64S128AndNot,
     false,
     32,
     24,
     0x01,
     1},
    {{0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
      0x00, 0x00, 0x01, 0x00},
     TSBinop::kS128AndNot,
     kArm64S128AndNot,
     false,
     32,
     16,
     0x01,
     1},
    {{0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
      0x00, 0x01, 0x00, 0x00},
     TSBinop::kS128AndNot,
     kArm64S128AndNot,
     false,
     32,
     8,
     0x01,
     1},
    {{0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
      0x01, 0x00, 0x00, 0x00},
     TSBinop::kS128AndNot,
     kArm64S128AndNot,
     false,
     32,
     0,
     0x01,
     1},

    {{0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
      0xEE, 0xEE, 0xEE, 0xEE},
     TSBinop::kS128AndNot,
     kArm64S128AndNot,
     false,
     0,
     0,
     0x00,
     2},
};

using TurboshaftInstructionSelectorSIMDConstAndTest =
    TurboshaftInstructionSelectorTestWithParam<SIMDConstAndTest>;

TEST_P(TurboshaftInstructionSelectorSIMDConstAndTest, ConstAnd) {
  const SIMDConstAndTest param = GetParam();
  // Const node on the left
  {
    StreamBuilder m(this, MachineType::Simd128(), MachineType::Simd128());
    OpIndex cnst = m.Simd128Constant(param.data);
    OpIndex op = m.Emit(param.simd_op, cnst, m.Parameter(0));
    m.Return(op);
    Stream s = m.Build();

    // Bic cannot always be applied when the immediate is on the left
    size_t expected_size = param.symmetrical ? param.size : 2;
    ASSERT_EQ(expected_size, s.size());
    if (expected_size == 1) {
      EXPECT_EQ(param.expected_op, s[0]->arch_opcode());
      EXPECT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(1U, s[0]->OutputCount());
      EXPECT_EQ(param.lane_size, LaneSizeField::decode(s[0]->opcode()));
      EXPECT_EQ(param.shift_amount, s.ToInt32(s[0]->InputAt(2)));
      EXPECT_EQ(param.expected_imm, s.ToInt32(s[0]->InputAt(1)));
    } else {
      EXPECT_EQ(kArm64S128Const, s[0]->arch_opcode());
      EXPECT_EQ(param.expected_op, s[1]->arch_opcode());
      EXPECT_EQ(2U, s[1]->InputCount());
      EXPECT_EQ(1U, s[1]->OutputCount());
    }
  }
  //  Const node on the right
  {
    StreamBuilder m(this, MachineType::Simd128(), MachineType::Simd128());
    OpIndex cnst = m.Simd128Constant(param.data);
    OpIndex op = m.Emit(param.simd_op, m.Parameter(0), cnst);
    m.Return(op);
    Stream s = m.Build();
    ASSERT_EQ(param.size, s.size());
    if (param.size == 1) {
      EXPECT_EQ(param.expected_op, s[0]->arch_opcode());
      EXPECT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(1U, s[0]->OutputCount());
      EXPECT_EQ(param.lane_size, LaneSizeField::decode(s[0]->opcode()));
      EXPECT_EQ(param.shift_amount, s.ToInt32(s[0]->InputAt(2)));
      EXPECT_EQ(param.expected_imm, s.ToInt32(s[0]->InputAt(1)));
    } else {
      EXPECT_EQ(kArm64S128Const, s[0]->arch_opcode());
      EXPECT_EQ(param.expected_op, s[1]->arch_opcode());
      EXPECT_EQ(2U, s[1]->InputCount());
      EXPECT_EQ(1U, s[1]->OutputCount());
    }
  }
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorSIMDConstAndTest,
                         ::testing::ValuesIn(SIMDConstAndTests));
#endif  // V8_ENABLE_WEBASSEMBLY

}  // namespace v8::internal::compiler::turboshaft
