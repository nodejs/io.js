// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>

#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/objects/objects-inl.h"
#include "test/unittests/compiler/backend/turboshaft-instruction-selector-unittest.h"

namespace v8::internal::compiler::turboshaft {

namespace {

using Constructor = OpIndex (RawMachineAssembler::*)(OpIndex, OpIndex);

// Data processing instructions.
struct DPI {
  TSBinop op;
  const char* constructor_name;
  ArchOpcode arch_opcode;
  ArchOpcode reverse_arch_opcode;
  ArchOpcode test_arch_opcode;
};

std::ostream& operator<<(std::ostream& os, const DPI& dpi) {
  return os << dpi.constructor_name;
}

const DPI kDPIs[] = {
    {TSBinop::kWord32BitwiseAnd, "Word32BitwiseAnd", kArmAnd, kArmAnd, kArmTst},
    {TSBinop::kWord32BitwiseOr, "Word32Or", kArmOrr, kArmOrr, kArmOrr},
    {TSBinop::kWord32BitwiseXor, "Word32Xor", kArmEor, kArmEor, kArmTeq},
    {TSBinop::kWord32Add, "Word32Add", kArmAdd, kArmAdd, kArmCmn},
    {TSBinop::kWord32Sub, "Word32Sub", kArmSub, kArmRsb, kArmCmp}};

// Floating point arithmetic instructions.
struct FAI {
  TSBinop op;
  const char* constructor_name;
  MachineType machine_type;
  ArchOpcode arch_opcode;
};

std::ostream& operator<<(std::ostream& os, const FAI& fai) {
  return os << fai.constructor_name;
}

const FAI kFAIs[] = {
    {TSBinop::kFloat32Add, "Float32Add", MachineType::Float32(), kArmVaddF32},
    {TSBinop::kFloat64Add, "Float64Add", MachineType::Float64(), kArmVaddF64},
    {TSBinop::kFloat32Sub, "Float32Sub", MachineType::Float32(), kArmVsubF32},
    {TSBinop::kFloat64Sub, "Float64Sub", MachineType::Float64(), kArmVsubF64},
    {TSBinop::kFloat32Mul, "Float32Mul", MachineType::Float32(), kArmVmulF32},
    {TSBinop::kFloat64Mul, "Float64Mul", MachineType::Float64(), kArmVmulF64},
    {TSBinop::kFloat32Div, "Float32Div", MachineType::Float32(), kArmVdivF32},
    {TSBinop::kFloat64Div, "Float64Div", MachineType::Float64(), kArmVdivF64}};

// Data processing instructions with overflow.
struct ODPI {
  TSBinop op;
  const char* constructor_name;
  ArchOpcode arch_opcode;
  ArchOpcode reverse_arch_opcode;
};

std::ostream& operator<<(std::ostream& os, const ODPI& odpi) {
  return os << odpi.constructor_name;
}

const ODPI kODPIs[] = {{TSBinop::kInt32AddCheckOverflow,
                        "Word32AddWithOverflow", kArmAdd, kArmAdd},
                       {TSBinop::kInt32SubCheckOverflow,
                        "Word32SubWithOverflow", kArmSub, kArmRsb}};

// Shifts.
struct Shift {
  TSBinop op;
  const char* constructor_name;
  int32_t i_low;          // lowest possible immediate
  int32_t i_high;         // highest possible immediate
  AddressingMode i_mode;  // Operand2_R_<shift>_I
  AddressingMode r_mode;  // Operand2_R_<shift>_R
};

std::ostream& operator<<(std::ostream& os, const Shift& shift) {
  return os << shift.constructor_name;
}

const Shift kShifts[] = {
    {TSBinop::kWord32ShiftRightArithmetic, "Word32ShiftRightArithmetic", 1, 32,
     kMode_Operand2_R_ASR_I, kMode_Operand2_R_ASR_R},
    {TSBinop::kWord32ShiftLeft, "Word32ShiftLeft", 0, 31,
     kMode_Operand2_R_LSL_I, kMode_Operand2_R_LSL_R},
    {TSBinop::kWord32ShiftRightLogical, "Word32ShiftRightLogical", 1, 32,
     kMode_Operand2_R_LSR_I, kMode_Operand2_R_LSR_R},
    {TSBinop::kWord32RotateRight, "Word32Ror", 1, 31, kMode_Operand2_R_ROR_I,
     kMode_Operand2_R_ROR_R}};

// clang-format off
// Immediates (random subset).
const int32_t kImmediates[] = {
    std::numeric_limits<int32_t>::min(), -2147483617, -2147483606, -2113929216,
    -2080374784, -1996488704, -1879048192, -1459617792, -1358954496,
    -1342177265, -1275068414, -1073741818, -1073741777, -855638016, -805306368,
    -402653184, -268435444, -16777216, 0, 35, 61, 105, 116, 171, 245, 255, 692,
    1216, 1248, 1520, 1600, 1888, 3744, 4080, 5888, 8384, 9344, 9472, 9792,
    13312, 15040, 15360, 20736, 22272, 23296, 32000, 33536, 37120, 45824, 47872,
    56320, 59392, 65280, 72704, 101376, 147456, 161792, 164864, 167936, 173056,
    195584, 209920, 212992, 356352, 655360, 704512, 716800, 851968, 901120,
    1044480, 1523712, 2572288, 3211264, 3588096, 3833856, 3866624, 4325376,
    5177344, 6488064, 7012352, 7471104, 14090240, 16711680, 19398656, 22282240,
    28573696, 30408704, 30670848, 43253760, 54525952, 55312384, 56623104,
    68157440, 115343360, 131072000, 187695104, 188743680, 195035136, 197132288,
    203423744, 218103808, 267386880, 268435470, 285212672, 402653185, 415236096,
    595591168, 603979776, 603979778, 629145600, 1073741835, 1073741855,
    1073741861, 1073741884, 1157627904, 1476395008, 1476395010, 1610612741,
    2030043136, 2080374785, 2097152000};
// clang-format on

}  // namespace

// -----------------------------------------------------------------------------
// Data processing instructions.

using TurboshaftInstructionSelectorDPITest =
    TurboshaftInstructionSelectorTestWithParam<DPI>;

TEST_P(TurboshaftInstructionSelectorDPITest, Parameters) {
  const DPI dpi = GetParam();
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  m.Return(m.Emit(dpi.op, m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(kMode_Operand2_R, s[0]->addressing_mode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}

TEST_P(TurboshaftInstructionSelectorDPITest, Immediate) {
  const DPI dpi = GetParam();
  TRACED_FOREACH(int32_t, imm, kImmediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(m.Emit(dpi.op, m.Parameter(0), m.Int32Constant(imm)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_I, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  TRACED_FOREACH(int32_t, imm, kImmediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(m.Emit(dpi.op, m.Int32Constant(imm), m.Parameter(0)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(dpi.reverse_arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_I, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_P(TurboshaftInstructionSelectorDPITest, ShiftByParameter) {
  const DPI dpi = GetParam();
  TRACED_FOREACH(Shift, shift, kShifts) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32(), MachineType::Int32());
    m.Return(m.Emit(dpi.op, m.Parameter(0),
                    m.Emit(shift.op, m.Parameter(1), m.Parameter(2))));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(shift.r_mode, s[0]->addressing_mode());
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  TRACED_FOREACH(Shift, shift, kShifts) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32(), MachineType::Int32());
    m.Return(m.Emit(dpi.op, m.Emit(shift.op, m.Parameter(0), m.Parameter(1)),
                    m.Parameter(2)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(dpi.reverse_arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(shift.r_mode, s[0]->addressing_mode());
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_P(TurboshaftInstructionSelectorDPITest, ShiftByImmediate) {
  const DPI dpi = GetParam();
  TRACED_FOREACH(Shift, shift, kShifts) {
    TRACED_FORRANGE(int32_t, imm, shift.i_low, shift.i_high) {
      StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                      MachineType::Int32());
      m.Return(m.Emit(dpi.op, m.Parameter(0),
                      m.Emit(shift.op, m.Parameter(1), m.Int32Constant(imm))));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(dpi.arch_opcode, s[0]->arch_opcode());
      EXPECT_EQ(shift.i_mode, s[0]->addressing_mode());
      ASSERT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(2)));
      EXPECT_EQ(1U, s[0]->OutputCount());
    }
  }
  TRACED_FOREACH(Shift, shift, kShifts) {
    TRACED_FORRANGE(int32_t, imm, shift.i_low, shift.i_high) {
      StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                      MachineType::Int32());
      m.Return(m.Emit(dpi.op,
                      m.Emit(shift.op, m.Parameter(0), m.Int32Constant(imm)),
                      m.Parameter(1)));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(dpi.reverse_arch_opcode, s[0]->arch_opcode());
      EXPECT_EQ(shift.i_mode, s[0]->addressing_mode());
      ASSERT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(2)));
      EXPECT_EQ(1U, s[0]->OutputCount());
    }
  }
}

TEST_P(TurboshaftInstructionSelectorDPITest, BranchWithParameters) {
  const DPI dpi = GetParam();
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  Block *a = m.NewBlock(), *b = m.NewBlock();
  m.Branch(m.Emit<Word32>(dpi.op, m.Parameter(0), m.Parameter(1)), a, b);
  m.Bind(a);
  m.Return(m.Int32Constant(1));
  m.Bind(b);
  m.Return(m.Int32Constant(0));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(dpi.test_arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(kMode_Operand2_R, s[0]->addressing_mode());
  EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
  EXPECT_EQ(kNotEqual, s[0]->flags_condition());
}

TEST_P(TurboshaftInstructionSelectorDPITest, BranchWithImmediate) {
  const DPI dpi = GetParam();
  TRACED_FOREACH(int32_t, imm, kImmediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    m.Branch(m.Emit<Word32>(dpi.op, m.Parameter(0), m.Int32Constant(imm)), a,
             b);
    m.Bind(a);
    m.Return(m.Int32Constant(1));
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(dpi.test_arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_I, s[0]->addressing_mode());
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(kNotEqual, s[0]->flags_condition());
  }
  TRACED_FOREACH(int32_t, imm, kImmediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    m.Branch(m.Emit<Word32>(dpi.op, m.Int32Constant(imm), m.Parameter(0)), a,
             b);
    m.Bind(a);
    m.Return(m.Int32Constant(1));
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(dpi.test_arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_I, s[0]->addressing_mode());
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(kNotEqual, s[0]->flags_condition());
  }
}

TEST_P(TurboshaftInstructionSelectorDPITest, BranchWithShiftByParameter) {
  const DPI dpi = GetParam();
  TRACED_FOREACH(Shift, shift, kShifts) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32(), MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    m.Branch(m.Emit<Word32>(dpi.op, m.Parameter(0),
                            m.Emit(shift.op, m.Parameter(1), m.Parameter(2))),
             a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(1));
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(dpi.test_arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(shift.r_mode, s[0]->addressing_mode());
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(kNotEqual, s[0]->flags_condition());
  }
  TRACED_FOREACH(Shift, shift, kShifts) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32(), MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    m.Branch(
        m.Emit<Word32>(dpi.op, m.Emit(shift.op, m.Parameter(0), m.Parameter(1)),
                       m.Parameter(2)),
        a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(1));
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(dpi.test_arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(shift.r_mode, s[0]->addressing_mode());
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(kNotEqual, s[0]->flags_condition());
  }
}

TEST_P(TurboshaftInstructionSelectorDPITest, BranchWithShiftByImmediate) {
  const DPI dpi = GetParam();
  TRACED_FOREACH(Shift, shift, kShifts) {
    TRACED_FORRANGE(int32_t, imm, shift.i_low, shift.i_high) {
      StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                      MachineType::Int32());
      Block *a = m.NewBlock(), *b = m.NewBlock();
      m.Branch(m.Emit<Word32>(
                   dpi.op, m.Parameter(0),
                   m.Emit(shift.op, m.Parameter(1), m.Int32Constant(imm))),
               a, b);
      m.Bind(a);
      m.Return(m.Int32Constant(1));
      m.Bind(b);
      m.Return(m.Int32Constant(0));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(dpi.test_arch_opcode, s[0]->arch_opcode());
      EXPECT_EQ(shift.i_mode, s[0]->addressing_mode());
      ASSERT_EQ(5U, s[0]->InputCount());
      EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(2)));
      EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
      EXPECT_EQ(kNotEqual, s[0]->flags_condition());
    }
  }
  TRACED_FOREACH(Shift, shift, kShifts) {
    TRACED_FORRANGE(int32_t, imm, shift.i_low, shift.i_high) {
      StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                      MachineType::Int32());
      Block *a = m.NewBlock(), *b = m.NewBlock();
      m.Branch(
          m.Emit<Word32>(dpi.op,
                         m.Emit(shift.op, m.Parameter(0), m.Int32Constant(imm)),
                         m.Parameter(1)),
          a, b);
      m.Bind(a);
      m.Return(m.Int32Constant(1));
      m.Bind(b);
      m.Return(m.Int32Constant(0));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(dpi.test_arch_opcode, s[0]->arch_opcode());
      EXPECT_EQ(shift.i_mode, s[0]->addressing_mode());
      ASSERT_EQ(5U, s[0]->InputCount());
      EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(2)));
      EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
      EXPECT_EQ(kNotEqual, s[0]->flags_condition());
    }
  }
}

TEST_P(TurboshaftInstructionSelectorDPITest, BranchIfZeroWithParameters) {
  const DPI dpi = GetParam();
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  Block *a = m.NewBlock(), *b = m.NewBlock();
  m.Branch(m.Word32Equal(m.Emit(dpi.op, m.Parameter(0), m.Parameter(1)),
                         m.Int32Constant(0)),
           a, b);
  m.Bind(a);
  m.Return(m.Int32Constant(1));
  m.Bind(b);
  m.Return(m.Int32Constant(0));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(dpi.test_arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(kMode_Operand2_R, s[0]->addressing_mode());
  EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
  EXPECT_EQ(kEqual, s[0]->flags_condition());
}

TEST_P(TurboshaftInstructionSelectorDPITest, BranchIfNotZeroWithParameters) {
  const DPI dpi = GetParam();
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  Block *a = m.NewBlock(), *b = m.NewBlock();
  m.Branch(m.Word32NotEqual(m.Emit(dpi.op, m.Parameter(0), m.Parameter(1)),
                            m.Int32Constant(0)),
           a, b);
  m.Bind(a);
  m.Return(m.Int32Constant(1));
  m.Bind(b);
  m.Return(m.Int32Constant(0));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(dpi.test_arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(kMode_Operand2_R, s[0]->addressing_mode());
  EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
  EXPECT_EQ(kNotEqual, s[0]->flags_condition());
}

TEST_P(TurboshaftInstructionSelectorDPITest, BranchIfZeroWithImmediate) {
  const DPI dpi = GetParam();
  TRACED_FOREACH(int32_t, imm, kImmediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    m.Branch(m.Word32Equal(m.Emit(dpi.op, m.Parameter(0), m.Int32Constant(imm)),
                           m.Int32Constant(0)),
             a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(1));
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(dpi.test_arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_I, s[0]->addressing_mode());
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(kEqual, s[0]->flags_condition());
  }
  TRACED_FOREACH(int32_t, imm, kImmediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    m.Branch(m.Word32Equal(m.Emit(dpi.op, m.Int32Constant(imm), m.Parameter(0)),
                           m.Int32Constant(0)),
             a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(1));
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(dpi.test_arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_I, s[0]->addressing_mode());
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(kEqual, s[0]->flags_condition());
  }
}

TEST_P(TurboshaftInstructionSelectorDPITest, BranchIfNotZeroWithImmediate) {
  const DPI dpi = GetParam();
  TRACED_FOREACH(int32_t, imm, kImmediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    m.Branch(
        m.Word32NotEqual(m.Emit(dpi.op, m.Parameter(0), m.Int32Constant(imm)),
                         m.Int32Constant(0)),
        a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(1));
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(dpi.test_arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_I, s[0]->addressing_mode());
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(kNotEqual, s[0]->flags_condition());
  }
  TRACED_FOREACH(int32_t, imm, kImmediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    m.Branch(
        m.Word32NotEqual(m.Emit(dpi.op, m.Int32Constant(imm), m.Parameter(0)),
                         m.Int32Constant(0)),
        a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(1));
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(dpi.test_arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_I, s[0]->addressing_mode());
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(kNotEqual, s[0]->flags_condition());
  }
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorDPITest,
                         ::testing::ValuesIn(kDPIs));

// -----------------------------------------------------------------------------
// Data processing instructions with overflow.

using TurboshaftInstructionSelectorODPITest =
    TurboshaftInstructionSelectorTestWithParam<ODPI>;

TEST_P(TurboshaftInstructionSelectorODPITest, OvfWithParameters) {
  const ODPI odpi = GetParam();
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  m.Return(m.Projection(m.Emit(odpi.op, m.Parameter(0), m.Parameter(1)), 1));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(odpi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(kMode_Operand2_R, s[0]->addressing_mode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_LE(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_set, s[0]->flags_mode());
  EXPECT_EQ(kOverflow, s[0]->flags_condition());
}

TEST_P(TurboshaftInstructionSelectorODPITest, OvfWithImmediate) {
  const ODPI odpi = GetParam();
  TRACED_FOREACH(int32_t, imm, kImmediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(
        m.Projection(m.Emit(odpi.op, m.Parameter(0), m.Int32Constant(imm)), 1));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(odpi.arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_I, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_LE(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kOverflow, s[0]->flags_condition());
  }
  TRACED_FOREACH(int32_t, imm, kImmediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(
        m.Projection(m.Emit(odpi.op, m.Int32Constant(imm), m.Parameter(0)), 1));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(odpi.reverse_arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_I, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_LE(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kOverflow, s[0]->flags_condition());
  }
}

TEST_P(TurboshaftInstructionSelectorODPITest, OvfWithShiftByParameter) {
  const ODPI odpi = GetParam();
  TRACED_FOREACH(Shift, shift, kShifts) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32(), MachineType::Int32());
    m.Return(
        m.Projection(m.Emit(odpi.op, m.Parameter(0),
                            m.Emit(shift.op, m.Parameter(1), m.Parameter(2))),
                     1));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(odpi.arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(shift.r_mode, s[0]->addressing_mode());
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_LE(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kOverflow, s[0]->flags_condition());
  }
  TRACED_FOREACH(Shift, shift, kShifts) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32(), MachineType::Int32());
    m.Return(m.Projection(
        m.Emit(odpi.op, m.Emit(shift.op, m.Parameter(0), m.Parameter(1)),
               m.Parameter(0)),
        1));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(odpi.reverse_arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(shift.r_mode, s[0]->addressing_mode());
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_LE(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kOverflow, s[0]->flags_condition());
  }
}

TEST_P(TurboshaftInstructionSelectorODPITest, OvfWithShiftByImmediate) {
  const ODPI odpi = GetParam();
  TRACED_FOREACH(Shift, shift, kShifts) {
    TRACED_FORRANGE(int32_t, imm, shift.i_low, shift.i_high) {
      StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                      MachineType::Int32());
      m.Return(m.Projection(
          m.Emit(odpi.op, m.Parameter(0),
                 m.Emit(shift.op, m.Parameter(1), m.Int32Constant(imm))),
          1));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(odpi.arch_opcode, s[0]->arch_opcode());
      EXPECT_EQ(shift.i_mode, s[0]->addressing_mode());
      ASSERT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(2)));
      EXPECT_LE(1U, s[0]->OutputCount());
      EXPECT_EQ(kFlags_set, s[0]->flags_mode());
      EXPECT_EQ(kOverflow, s[0]->flags_condition());
    }
  }
  TRACED_FOREACH(Shift, shift, kShifts) {
    TRACED_FORRANGE(int32_t, imm, shift.i_low, shift.i_high) {
      StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                      MachineType::Int32());
      m.Return(m.Projection(
          m.Emit(odpi.op,
                 m.Emit(shift.op, m.Parameter(1), m.Int32Constant(imm)),
                 m.Parameter(0)),
          1));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(odpi.reverse_arch_opcode, s[0]->arch_opcode());
      EXPECT_EQ(shift.i_mode, s[0]->addressing_mode());
      ASSERT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(2)));
      EXPECT_LE(1U, s[0]->OutputCount());
      EXPECT_EQ(kFlags_set, s[0]->flags_mode());
      EXPECT_EQ(kOverflow, s[0]->flags_condition());
    }
  }
}

TEST_P(TurboshaftInstructionSelectorODPITest, ValWithParameters) {
  const ODPI odpi = GetParam();
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  m.Return(m.Projection(m.Emit(odpi.op, m.Parameter(0), m.Parameter(1)), 0));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(odpi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(kMode_Operand2_R, s[0]->addressing_mode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_LE(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_none, s[0]->flags_mode());
}

TEST_P(TurboshaftInstructionSelectorODPITest, ValWithImmediate) {
  const ODPI odpi = GetParam();
  TRACED_FOREACH(int32_t, imm, kImmediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(
        m.Projection(m.Emit(odpi.op, m.Parameter(0), m.Int32Constant(imm)), 0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(odpi.arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_I, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_LE(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_none, s[0]->flags_mode());
  }
  TRACED_FOREACH(int32_t, imm, kImmediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(
        m.Projection(m.Emit(odpi.op, m.Int32Constant(imm), m.Parameter(0)), 0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(odpi.reverse_arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_I, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_LE(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_none, s[0]->flags_mode());
  }
}

TEST_P(TurboshaftInstructionSelectorODPITest, ValWithShiftByParameter) {
  const ODPI odpi = GetParam();
  TRACED_FOREACH(Shift, shift, kShifts) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32(), MachineType::Int32());
    m.Return(
        m.Projection(m.Emit(odpi.op, m.Parameter(0),
                            m.Emit(shift.op, m.Parameter(1), m.Parameter(2))),
                     0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(odpi.arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(shift.r_mode, s[0]->addressing_mode());
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_LE(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_none, s[0]->flags_mode());
  }
  TRACED_FOREACH(Shift, shift, kShifts) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32(), MachineType::Int32());
    m.Return(m.Projection(
        m.Emit(odpi.op, m.Emit(shift.op, m.Parameter(0), m.Parameter(1)),
               m.Parameter(0)),
        0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(odpi.reverse_arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(shift.r_mode, s[0]->addressing_mode());
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_LE(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_none, s[0]->flags_mode());
  }
}

TEST_P(TurboshaftInstructionSelectorODPITest, ValWithShiftByImmediate) {
  const ODPI odpi = GetParam();
  TRACED_FOREACH(Shift, shift, kShifts) {
    TRACED_FORRANGE(int32_t, imm, shift.i_low, shift.i_high) {
      StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                      MachineType::Int32());
      m.Return(m.Projection(
          m.Emit(odpi.op, m.Parameter(0),
                 m.Emit(shift.op, m.Parameter(1), m.Int32Constant(imm))),
          0));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(odpi.arch_opcode, s[0]->arch_opcode());
      EXPECT_EQ(shift.i_mode, s[0]->addressing_mode());
      ASSERT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(2)));
      EXPECT_LE(1U, s[0]->OutputCount());
      EXPECT_EQ(kFlags_none, s[0]->flags_mode());
    }
  }
  TRACED_FOREACH(Shift, shift, kShifts) {
    TRACED_FORRANGE(int32_t, imm, shift.i_low, shift.i_high) {
      StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                      MachineType::Int32());
      m.Return(m.Projection(
          m.Emit(odpi.op,
                 m.Emit(shift.op, m.Parameter(1), m.Int32Constant(imm)),
                 m.Parameter(0)),
          0));
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(odpi.reverse_arch_opcode, s[0]->arch_opcode());
      EXPECT_EQ(shift.i_mode, s[0]->addressing_mode());
      ASSERT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(2)));
      EXPECT_LE(1U, s[0]->OutputCount());
      EXPECT_EQ(kFlags_none, s[0]->flags_mode());
    }
  }
}

TEST_P(TurboshaftInstructionSelectorODPITest, BothWithParameters) {
  const ODPI odpi = GetParam();
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  OpIndex n = m.Emit(odpi.op, m.Parameter(0), m.Parameter(1));
  m.Return(m.Word32Equal(m.Projection(n, 0), m.Projection(n, 1)));
  Stream s = m.Build();
  ASSERT_LE(1U, s.size());
  EXPECT_EQ(odpi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(kMode_Operand2_R, s[0]->addressing_mode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(2U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_set, s[0]->flags_mode());
  EXPECT_EQ(kOverflow, s[0]->flags_condition());
}

TEST_P(TurboshaftInstructionSelectorODPITest, BothWithImmediate) {
  const ODPI odpi = GetParam();
  TRACED_FOREACH(int32_t, imm, kImmediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    OpIndex n = m.Emit(odpi.op, m.Parameter(0), m.Int32Constant(imm));
    m.Return(m.Word32Equal(m.Projection(n, 0), m.Projection(n, 1)));
    Stream s = m.Build();
    ASSERT_LE(1U, s.size());
    EXPECT_EQ(odpi.arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_I, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(2U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kOverflow, s[0]->flags_condition());
  }
  TRACED_FOREACH(int32_t, imm, kImmediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    OpIndex n = m.Emit(odpi.op, m.Int32Constant(imm), m.Parameter(0));
    m.Return(m.Word32Equal(m.Projection(n, 0), m.Projection(n, 1)));
    Stream s = m.Build();
    ASSERT_LE(1U, s.size());
    EXPECT_EQ(odpi.reverse_arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_I, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(2U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kOverflow, s[0]->flags_condition());
  }
}

TEST_P(TurboshaftInstructionSelectorODPITest, BothWithShiftByParameter) {
  const ODPI odpi = GetParam();
  TRACED_FOREACH(Shift, shift, kShifts) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32(), MachineType::Int32());
    OpIndex n = m.Emit(odpi.op, m.Parameter(0),
                       m.Emit(shift.op, m.Parameter(1), m.Parameter(2)));
    m.Return(m.Word32Equal(m.Projection(n, 0), m.Projection(n, 1)));
    Stream s = m.Build();
    ASSERT_LE(1U, s.size());
    EXPECT_EQ(odpi.arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(shift.r_mode, s[0]->addressing_mode());
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(2U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kOverflow, s[0]->flags_condition());
  }
  TRACED_FOREACH(Shift, shift, kShifts) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32(), MachineType::Int32());
    OpIndex n =
        m.Emit(odpi.op, m.Emit(shift.op, m.Parameter(0), m.Parameter(1)),
               m.Parameter(2));
    m.Return(m.Word32Equal(m.Projection(n, 0), m.Projection(n, 1)));
    Stream s = m.Build();
    ASSERT_LE(1U, s.size());
    EXPECT_EQ(odpi.reverse_arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(shift.r_mode, s[0]->addressing_mode());
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(2U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kOverflow, s[0]->flags_condition());
  }
}

TEST_P(TurboshaftInstructionSelectorODPITest, BothWithShiftByImmediate) {
  const ODPI odpi = GetParam();
  TRACED_FOREACH(Shift, shift, kShifts) {
    TRACED_FORRANGE(int32_t, imm, shift.i_low, shift.i_high) {
      StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                      MachineType::Int32());
      OpIndex n =
          m.Emit(odpi.op, m.Parameter(0),
                 m.Emit(shift.op, m.Parameter(1), m.Int32Constant(imm)));
      m.Return(m.Word32Equal(m.Projection(n, 0), m.Projection(n, 1)));
      Stream s = m.Build();
      ASSERT_LE(1U, s.size());
      EXPECT_EQ(odpi.arch_opcode, s[0]->arch_opcode());
      EXPECT_EQ(shift.i_mode, s[0]->addressing_mode());
      ASSERT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(2)));
      EXPECT_EQ(2U, s[0]->OutputCount());
      EXPECT_EQ(kFlags_set, s[0]->flags_mode());
      EXPECT_EQ(kOverflow, s[0]->flags_condition());
    }
  }
  TRACED_FOREACH(Shift, shift, kShifts) {
    TRACED_FORRANGE(int32_t, imm, shift.i_low, shift.i_high) {
      StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                      MachineType::Int32());
      OpIndex n = m.Emit(odpi.op,
                         m.Emit(shift.op, m.Parameter(0), m.Int32Constant(imm)),
                         m.Parameter(1));
      m.Return(m.Word32Equal(m.Projection(n, 0), m.Projection(n, 1)));
      Stream s = m.Build();
      ASSERT_LE(1U, s.size());
      EXPECT_EQ(odpi.reverse_arch_opcode, s[0]->arch_opcode());
      EXPECT_EQ(shift.i_mode, s[0]->addressing_mode());
      ASSERT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(2)));
      EXPECT_EQ(2U, s[0]->OutputCount());
      EXPECT_EQ(kFlags_set, s[0]->flags_mode());
      EXPECT_EQ(kOverflow, s[0]->flags_condition());
    }
  }
}

TEST_P(TurboshaftInstructionSelectorODPITest, BranchWithParameters) {
  const ODPI odpi = GetParam();
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  Block *a = m.NewBlock(), *b = m.NewBlock();
  OpIndex n = m.Emit(odpi.op, m.Parameter(0), m.Parameter(1));
  m.Branch(V<Word32>::Cast(m.Projection(n, 1)), a, b);
  m.Bind(a);
  m.Return(m.Int32Constant(0));
  m.Bind(b);
  m.Return(m.Projection(n, 0));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(odpi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(kMode_Operand2_R, s[0]->addressing_mode());
  EXPECT_EQ(4U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
  EXPECT_EQ(kOverflow, s[0]->flags_condition());
}

TEST_P(TurboshaftInstructionSelectorODPITest, BranchWithImmediate) {
  const ODPI odpi = GetParam();
  TRACED_FOREACH(int32_t, imm, kImmediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    OpIndex n = m.Emit(odpi.op, m.Parameter(0), m.Int32Constant(imm));
    m.Branch(V<Word32>::Cast(m.Projection(n, 1)), a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(0));
    m.Bind(b);
    m.Return(m.Projection(n, 0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(odpi.arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_I, s[0]->addressing_mode());
    ASSERT_EQ(4U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(kOverflow, s[0]->flags_condition());
  }
  TRACED_FOREACH(int32_t, imm, kImmediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    OpIndex n = m.Emit(odpi.op, m.Int32Constant(imm), m.Parameter(0));
    m.Branch(V<Word32>::Cast(m.Projection(n, 1)), a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(0));
    m.Bind(b);
    m.Return(m.Projection(n, 0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(odpi.reverse_arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_I, s[0]->addressing_mode());
    ASSERT_EQ(4U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(kOverflow, s[0]->flags_condition());
  }
}

TEST_P(TurboshaftInstructionSelectorODPITest, BranchIfZeroWithParameters) {
  const ODPI odpi = GetParam();
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  Block *a = m.NewBlock(), *b = m.NewBlock();
  OpIndex n = m.Emit(odpi.op, m.Parameter(0), m.Parameter(1));
  m.Branch(m.Word32Equal(m.Projection(n, 1), m.Int32Constant(0)), a, b);
  m.Bind(a);
  m.Return(m.Projection(n, 0));
  m.Bind(b);
  m.Return(m.Int32Constant(0));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(odpi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(kMode_Operand2_R, s[0]->addressing_mode());
  EXPECT_EQ(4U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
  EXPECT_EQ(kNotOverflow, s[0]->flags_condition());
}

TEST_P(TurboshaftInstructionSelectorODPITest, BranchIfNotZeroWithParameters) {
  const ODPI odpi = GetParam();
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  Block *a = m.NewBlock(), *b = m.NewBlock();
  OpIndex n = m.Emit(odpi.op, m.Parameter(0), m.Parameter(1));
  m.Branch(m.Word32NotEqual(m.Projection(n, 1), m.Int32Constant(0)), a, b);
  m.Bind(a);
  m.Return(m.Projection(n, 0));
  m.Bind(b);
  m.Return(m.Int32Constant(0));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(odpi.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(kMode_Operand2_R, s[0]->addressing_mode());
  EXPECT_EQ(4U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
  EXPECT_EQ(kOverflow, s[0]->flags_condition());
}

TEST_P(TurboshaftInstructionSelectorODPITest, BranchIfOverflowWithLoop) {
  const ODPI ovf_binop = GetParam();
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());

  WordRepresentation phi_repr = WordRepresentation::Word32();

  Block* loop_header = m.NewLoopHeader();
  Block *b1 = m.NewBlock(), *b2 = m.NewBlock();

  OpIndex v1 = m.Parameter(0);
  OpIndex v2 = m.Parameter(0);

  m.Goto(loop_header);
  m.Bind(loop_header);
  OpIndex phi = m.PendingLoopPhi(v1, phi_repr);
  OpIndex binop = m.Emit(ovf_binop.op, v1, v2);
  m.Branch(m.Word32Equal(m.Projection(binop, 1), m.Word32Constant(0)), b1, b2);
  m.Bind(b2);
  m.Goto(loop_header);
  m.Bind(b1);
  m.Return(v1);

  m.output_graph().Replace<PhiOp>(
      phi, base::VectorOf<OpIndex>({v1, m.Projection(binop, 0)}), phi_repr);

  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(ovf_binop.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(4U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
  EXPECT_EQ(kNotOverflow, s[0]->flags_condition());
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorODPITest,
                         ::testing::ValuesIn(kODPIs));

// -----------------------------------------------------------------------------
// Shifts.

using TurboshaftInstructionSelectorShiftTest =
    TurboshaftInstructionSelectorTestWithParam<Shift>;

TEST_P(TurboshaftInstructionSelectorShiftTest, Parameters) {
  const Shift shift = GetParam();
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  m.Return(m.Emit(shift.op, m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmMov, s[0]->arch_opcode());
  EXPECT_EQ(shift.r_mode, s[0]->addressing_mode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}

TEST_P(TurboshaftInstructionSelectorShiftTest, Immediate) {
  const Shift shift = GetParam();
  TRACED_FORRANGE(int32_t, imm, shift.i_low, shift.i_high) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(m.Emit(shift.op, m.Parameter(0), m.Int32Constant(imm)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmMov, s[0]->arch_opcode());
    EXPECT_EQ(shift.i_mode, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_P(TurboshaftInstructionSelectorShiftTest, Word32EqualWithParameter) {
  const Shift shift = GetParam();
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32(), MachineType::Int32());
    m.Return(m.Word32Equal(m.Parameter(0),
                           m.Emit(shift.op, m.Parameter(1), m.Parameter(2))));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmCmp, s[0]->arch_opcode());
    EXPECT_EQ(shift.r_mode, s[0]->addressing_mode());
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kEqual, s[0]->flags_condition());
  }
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32(), MachineType::Int32());
    m.Return(m.Word32Equal(m.Emit(shift.op, m.Parameter(1), m.Parameter(2)),
                           m.Parameter(0)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmCmp, s[0]->arch_opcode());
    EXPECT_EQ(shift.r_mode, s[0]->addressing_mode());
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kEqual, s[0]->flags_condition());
  }
}

TEST_P(TurboshaftInstructionSelectorShiftTest,
       Word32EqualWithParameterAndImmediate) {
  const Shift shift = GetParam();
  TRACED_FORRANGE(int32_t, imm, shift.i_low, shift.i_high) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    m.Return(
        m.Word32Equal(m.Emit(shift.op, m.Parameter(1), m.Int32Constant(imm)),
                      m.Parameter(0)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmCmp, s[0]->arch_opcode());
    EXPECT_EQ(shift.i_mode, s[0]->addressing_mode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(2)));
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kEqual, s[0]->flags_condition());
  }
  TRACED_FORRANGE(int32_t, imm, shift.i_low, shift.i_high) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    m.Return(m.Word32Equal(m.Parameter(0), m.Emit(shift.op, m.Parameter(1),
                                                  m.Int32Constant(imm))));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmCmp, s[0]->arch_opcode());
    EXPECT_EQ(shift.i_mode, s[0]->addressing_mode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(2)));
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kEqual, s[0]->flags_condition());
  }
}

TEST_P(TurboshaftInstructionSelectorShiftTest,
       Word32EqualToZeroWithParameters) {
  const Shift shift = GetParam();
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  m.Return(m.Word32Equal(m.Int32Constant(0),
                         m.Emit(shift.op, m.Parameter(0), m.Parameter(1))));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmMov, s[0]->arch_opcode());
  EXPECT_EQ(shift.r_mode, s[0]->addressing_mode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(2U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_set, s[0]->flags_mode());
  EXPECT_EQ(kEqual, s[0]->flags_condition());
}

TEST_P(TurboshaftInstructionSelectorShiftTest, Word32EqualToZeroWithImmediate) {
  const Shift shift = GetParam();
  TRACED_FORRANGE(int32_t, imm, shift.i_low, shift.i_high) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    m.Return(m.Word32Equal(m.Int32Constant(0), m.Emit(shift.op, m.Parameter(0),
                                                      m.Int32Constant(imm))));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmMov, s[0]->arch_opcode());
    EXPECT_EQ(shift.i_mode, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(2U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kEqual, s[0]->flags_condition());
  }
}

TEST_P(TurboshaftInstructionSelectorShiftTest, Word32BitwiseNotWithParameters) {
  const Shift shift = GetParam();
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  m.Return(
      m.Word32BitwiseNot(m.Emit(shift.op, m.Parameter(0), m.Parameter(1))));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmMvn, s[0]->arch_opcode());
  EXPECT_EQ(shift.r_mode, s[0]->addressing_mode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}

TEST_P(TurboshaftInstructionSelectorShiftTest, Word32BitwiseNotWithImmediate) {
  const Shift shift = GetParam();
  TRACED_FORRANGE(int32_t, imm, shift.i_low, shift.i_high) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(m.Word32BitwiseNot(
        m.Emit(shift.op, m.Parameter(0), m.Int32Constant(imm))));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmMvn, s[0]->arch_opcode());
    EXPECT_EQ(shift.i_mode, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_P(TurboshaftInstructionSelectorShiftTest,
       Word32BitwiseAndWithWord32BitwiseNotWithParameters) {
  const Shift shift = GetParam();
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32(), MachineType::Int32());
  m.Return(m.Word32BitwiseAnd(
      m.Parameter(0),
      m.Word32BitwiseNot(m.Emit(shift.op, m.Parameter(1), m.Parameter(2)))));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmBic, s[0]->arch_opcode());
  EXPECT_EQ(shift.r_mode, s[0]->addressing_mode());
  EXPECT_EQ(3U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}

TEST_P(TurboshaftInstructionSelectorShiftTest,
       Word32BitwiseAndWithWord32BitwiseNotWithImmediate) {
  const Shift shift = GetParam();
  TRACED_FORRANGE(int32_t, imm, shift.i_low, shift.i_high) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    m.Return(m.Word32BitwiseAnd(
        m.Parameter(0), m.Word32BitwiseNot(m.Emit(shift.op, m.Parameter(1),
                                                  m.Int32Constant(imm)))));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmBic, s[0]->arch_opcode());
    EXPECT_EQ(shift.i_mode, s[0]->addressing_mode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(2)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorShiftTest,
                         ::testing::ValuesIn(kShifts));

// -----------------------------------------------------------------------------
// Memory access instructions.

namespace {

struct MemoryAccess {
  MachineType type;
  ArchOpcode ldr_opcode;
  ArchOpcode str_opcode;
  bool (TurboshaftInstructionSelectorTest::Stream::*val_predicate)(
      const InstructionOperand*) const;
  const int32_t immediates[40];
};

std::ostream& operator<<(std::ostream& os, const MemoryAccess& memacc) {
  return os << memacc.type;
}

const MemoryAccess kMemoryAccesses[] = {
    {MachineType::Int8(),
     kArmLdrsb,
     kArmStrb,
     &TurboshaftInstructionSelectorTest::Stream::IsInteger,
     {-4095, -3340, -3231, -3224, -3088, -1758, -1203, -123, -117, -91,
      -89,   -87,   -86,   -82,   -44,   -23,   -3,    0,    7,    10,
      39,    52,    69,    71,    91,    92,    107,   109,  115,  124,
      286,   655,   1362,  1569,  2587,  3067,  3096,  3462, 3510, 4095}},
    {MachineType::Uint8(),
     kArmLdrb,
     kArmStrb,
     &TurboshaftInstructionSelectorTest::Stream::IsInteger,
     {-4095, -3914, -3536, -3234, -3185, -3169, -1073, -990, -859, -720,
      -434,  -127,  -124,  -122,  -105,  -91,   -86,   -64,  -55,  -53,
      -30,   -10,   -3,    0,     20,    28,    39,    58,   64,   73,
      75,    100,   108,   121,   686,   963,   1363,  2759, 3449, 4095}},
    {MachineType::Int16(),
     kArmLdrsh,
     kArmStrh,
     &TurboshaftInstructionSelectorTest::Stream::IsInteger,
     {-255, -251, -232, -220, -144, -138, -130, -126, -116, -115,
      -102, -101, -98,  -69,  -59,  -56,  -39,  -35,  -23,  -19,
      -7,   0,    22,   26,   37,   68,   83,   87,   98,   102,
      108,  111,  117,  171,  195,  203,  204,  245,  246,  255}},
    {MachineType::Uint16(),
     kArmLdrh,
     kArmStrh,
     &TurboshaftInstructionSelectorTest::Stream::IsInteger,
     {-255, -230, -201, -172, -125, -119, -118, -105, -98, -79,
      -54,  -42,  -41,  -32,  -12,  -11,  -5,   -4,   0,   5,
      9,    25,   28,   51,   58,   60,   89,   104,  108, 109,
      114,  116,  120,  138,  150,  161,  166,  172,  228, 255}},
    {MachineType::Int32(),
     kArmLdr,
     kArmStr,
     &TurboshaftInstructionSelectorTest::Stream::IsInteger,
     {-4095, -1898, -1685, -1562, -1408, -1313, -344, -128, -116, -100,
      -92,   -80,   -72,   -71,   -56,   -25,   -21,  -11,  -9,   0,
      3,     5,     27,    28,    42,    52,    63,   88,   93,   97,
      125,   846,   1037,  2102,  2403,  2597,  2632, 2997, 3935, 4095}},
    {MachineType::Float32(),
     kArmVldrF32,
     kArmVstrF32,
     &TurboshaftInstructionSelectorTest::Stream::IsDouble,
     {-1020, -928, -896, -772, -728, -680, -660, -488, -372, -112,
      -100,  -92,  -84,  -80,  -72,  -64,  -60,  -56,  -52,  -48,
      -36,   -32,  -20,  -8,   -4,   0,    8,    20,   24,   40,
      64,    112,  204,  388,  516,  852,  856,  976,  988,  1020}},
    {MachineType::Float64(),
     kArmVldrF64,
     kArmVstrF64,
     &TurboshaftInstructionSelectorTest::Stream::IsDouble,
     {-1020, -948, -796, -696, -612, -364, -320, -308, -128, -112,
      -108,  -104, -96,  -84,  -80,  -56,  -48,  -40,  -20,  0,
      24,    28,   36,   48,   64,   84,   96,   100,  108,  116,
      120,   140,  156,  408,  432,  444,  772,  832,  940,  1020}}};

}  // namespace

using TurboshaftInstructionSelectorMemoryAccessTest =
    TurboshaftInstructionSelectorTestWithParam<MemoryAccess>;

TEST_P(TurboshaftInstructionSelectorMemoryAccessTest, LoadWithParameters) {
  const MemoryAccess memacc = GetParam();
  StreamBuilder m(this, memacc.type, MachineType::Pointer(),
                  MachineType::Int32());
  m.Return(m.Load(memacc.type, m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(memacc.ldr_opcode, s[0]->arch_opcode());
  EXPECT_EQ(kMode_Offset_RR, s[0]->addressing_mode());
  EXPECT_EQ(2U, s[0]->InputCount());
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_TRUE((s.*memacc.val_predicate)(s[0]->Output()));
}

TEST_P(TurboshaftInstructionSelectorMemoryAccessTest, LoadWithImmediateIndex) {
  const MemoryAccess memacc = GetParam();
  TRACED_FOREACH(int32_t, index, memacc.immediates) {
    StreamBuilder m(this, memacc.type, MachineType::Pointer());
    m.Return(m.Load(memacc.type, m.Parameter(0), m.Int32Constant(index)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(memacc.ldr_opcode, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Offset_RI, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    ASSERT_EQ(InstructionOperand::IMMEDIATE, s[0]->InputAt(1)->kind());
    EXPECT_EQ(index, s.ToInt32(s[0]->InputAt(1)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_TRUE((s.*memacc.val_predicate)(s[0]->Output()));
  }
}

TEST_P(TurboshaftInstructionSelectorMemoryAccessTest, StoreWithParameters) {
  const MemoryAccess memacc = GetParam();
  StreamBuilder m(this, MachineType::Int32(), MachineType::Pointer(),
                  MachineType::Int32(), memacc.type);
  m.Store(memacc.type.representation(), m.Parameter(0), m.Parameter(1),
          m.Parameter(2), kNoWriteBarrier);
  m.Return(m.Int32Constant(0));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(memacc.str_opcode, s[0]->arch_opcode());
  EXPECT_EQ(kMode_Offset_RR, s[0]->addressing_mode());
  EXPECT_EQ(3U, s[0]->InputCount());
  EXPECT_EQ(0U, s[0]->OutputCount());
}

TEST_P(TurboshaftInstructionSelectorMemoryAccessTest, StoreWithImmediateIndex) {
  const MemoryAccess memacc = GetParam();
  TRACED_FOREACH(int32_t, index, memacc.immediates) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Pointer(),
                    memacc.type);
    m.Store(memacc.type.representation(), m.Parameter(0),
            m.Int32Constant(index), m.Parameter(1), kNoWriteBarrier);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(memacc.str_opcode, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Offset_RI, s[0]->addressing_mode());
    ASSERT_EQ(3U, s[0]->InputCount());
    ASSERT_EQ(InstructionOperand::IMMEDIATE, s[0]->InputAt(2)->kind());
    EXPECT_EQ(index, s.ToInt32(s[0]->InputAt(2)));
    EXPECT_EQ(0U, s[0]->OutputCount());
  }
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorMemoryAccessTest,
                         ::testing::ValuesIn(kMemoryAccesses));

TEST_F(TurboshaftInstructionSelectorMemoryAccessTest, LoadWithShiftedIndex) {
  TRACED_FORRANGE(int, immediate_shift, 1, 31) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Pointer(),
                    MachineType::Int32());
    OpIndex const index =
        m.Word32ShiftLeft(m.Parameter(1), m.Int32Constant(immediate_shift));
    m.Return(m.Load(MachineType::Int32(), m.Parameter(0), index));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmLdr, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
    EXPECT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_F(TurboshaftInstructionSelectorMemoryAccessTest, StoreWithShiftedIndex) {
  TRACED_FORRANGE(int, immediate_shift, 1, 31) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Pointer(),
                    MachineType::Int32(), MachineType::Int32());
    OpIndex const index =
        m.Word32ShiftLeft(m.Parameter(1), m.Int32Constant(immediate_shift));
    m.Store(MachineRepresentation::kWord32, m.Parameter(0), index,
            m.Parameter(2), kNoWriteBarrier);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmStr, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
    EXPECT_EQ(4U, s[0]->InputCount());
    EXPECT_EQ(0U, s[0]->OutputCount());
  }
}

// -----------------------------------------------------------------------------
// Conversions.

TEST_F(TurboshaftInstructionSelectorTest, ChangeFloat32ToFloat64WithParameter) {
  StreamBuilder m(this, MachineType::Float64(), MachineType::Float32());
  m.Return(m.ChangeFloat32ToFloat64(m.Parameter(0)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmVcvtF64F32, s[0]->arch_opcode());
  EXPECT_EQ(1U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}

TEST_F(TurboshaftInstructionSelectorTest,
       TruncateFloat64ToFloat32WithParameter) {
  StreamBuilder m(this, MachineType::Float32(), MachineType::Float64());
  m.Return(m.TruncateFloat64ToFloat32(m.Parameter(0)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmVcvtF32F64, s[0]->arch_opcode());
  EXPECT_EQ(1U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}

// -----------------------------------------------------------------------------
// Comparisons.

namespace {

struct Comparison {
  TSBinop op;
  const char* constructor_name;
  FlagsCondition flags_condition;
  FlagsCondition negated_flags_condition;
  FlagsCondition commuted_flags_condition;
};

std::ostream& operator<<(std::ostream& os, const Comparison& cmp) {
  return os << cmp.constructor_name;
}

const Comparison kComparisons[] = {
    {TSBinop::kWord32Equal, "Word32Equal", kEqual, kNotEqual, kEqual},
    {TSBinop::kInt32LessThan, "Int32LessThan", kSignedLessThan,
     kSignedGreaterThanOrEqual, kSignedGreaterThan},
    {TSBinop::kInt32LessThanOrEqual, "Int32LessThanOrEqual",
     kSignedLessThanOrEqual, kSignedGreaterThan, kSignedGreaterThanOrEqual},
    {TSBinop::kUint32LessThan, "Uint32LessThan", kUnsignedLessThan,
     kUnsignedGreaterThanOrEqual, kUnsignedGreaterThan},
    {TSBinop::kUint32LessThanOrEqual, "Uint32LessThanOrEqual",
     kUnsignedLessThanOrEqual, kUnsignedGreaterThan,
     kUnsignedGreaterThanOrEqual}};

}  // namespace

using TurboshaftInstructionSelectorComparisonTest =
    TurboshaftInstructionSelectorTestWithParam<Comparison>;

TEST_P(TurboshaftInstructionSelectorComparisonTest, Parameters) {
  const Comparison& cmp = GetParam();
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  OpIndex const p0 = m.Parameter(0);
  OpIndex const p1 = m.Parameter(1);
  OpIndex const r = m.Emit(cmp.op, p0, p1);
  m.Return(r);
  Stream const s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmCmp, s[0]->arch_opcode());
  EXPECT_EQ(kMode_Operand2_R, s[0]->addressing_mode());
  ASSERT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(s.ToVreg(r), s.ToVreg(s[0]->OutputAt(0)));
  EXPECT_EQ(kFlags_set, s[0]->flags_mode());
  EXPECT_EQ(cmp.flags_condition, s[0]->flags_condition());
}

TEST_P(TurboshaftInstructionSelectorComparisonTest, Word32EqualWithZero) {
  {
    const Comparison& cmp = GetParam();
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    OpIndex const r = m.Word32Equal(m.Emit(cmp.op, p0, p1), m.Int32Constant(0));
    m.Return(r);
    Stream const s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmCmp, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(r), s.ToVreg(s[0]->OutputAt(0)));
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(cmp.negated_flags_condition, s[0]->flags_condition());
  }
  {
    const Comparison& cmp = GetParam();
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    OpIndex const r = m.Word32Equal(m.Int32Constant(0), m.Emit(cmp.op, p0, p1));
    m.Return(r);
    Stream const s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmCmp, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(r), s.ToVreg(s[0]->OutputAt(0)));
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(cmp.negated_flags_condition, s[0]->flags_condition());
  }
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorComparisonTest,
                         ::testing::ValuesIn(kComparisons));

// -----------------------------------------------------------------------------
// Floating point comparisons.

namespace {

const Comparison kF32Comparisons[] = {
    {TSBinop::kFloat32Equal, "Float32Equal", kEqual, kNotEqual, kEqual},
    {TSBinop::kFloat32LessThan, "Float32LessThan", kFloatLessThan,
     kFloatGreaterThanOrEqualOrUnordered, kFloatGreaterThan},
    {TSBinop::kFloat32LessThanOrEqual, "Float32LessThanOrEqual",
     kFloatLessThanOrEqual, kFloatGreaterThanOrUnordered,
     kFloatGreaterThanOrEqual}};

}  // namespace

using TurboshaftInstructionSelectorF32ComparisonTest =
    TurboshaftInstructionSelectorTestWithParam<Comparison>;

TEST_P(TurboshaftInstructionSelectorF32ComparisonTest, WithParameters) {
  const Comparison& cmp = GetParam();
  StreamBuilder m(this, MachineType::Int32(), MachineType::Float32(),
                  MachineType::Float32());
  m.Return(m.Emit(cmp.op, m.Parameter(0), m.Parameter(1)));
  Stream const s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmVcmpF32, s[0]->arch_opcode());
  ASSERT_EQ(2U, s[0]->InputCount());
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_set, s[0]->flags_mode());
  EXPECT_EQ(cmp.flags_condition, s[0]->flags_condition());
}

TEST_P(TurboshaftInstructionSelectorF32ComparisonTest, NegatedWithParameters) {
  const Comparison& cmp = GetParam();
  StreamBuilder m(this, MachineType::Int32(), MachineType::Float32(),
                  MachineType::Float32());
  m.Return(m.Word32BinaryNot(m.Emit(cmp.op, m.Parameter(0), m.Parameter(1))));
  Stream const s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmVcmpF32, s[0]->arch_opcode());
  ASSERT_EQ(2U, s[0]->InputCount());
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_set, s[0]->flags_mode());
  EXPECT_EQ(cmp.negated_flags_condition, s[0]->flags_condition());
}

TEST_P(TurboshaftInstructionSelectorF32ComparisonTest,
       WithImmediateZeroOnRight) {
  const Comparison& cmp = GetParam();
  StreamBuilder m(this, MachineType::Int32(), MachineType::Float32());
  m.Return(m.Emit(cmp.op, m.Parameter(0), m.Float32Constant(0.0)));
  Stream const s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmVcmpF32, s[0]->arch_opcode());
  ASSERT_EQ(2U, s[0]->InputCount());
  EXPECT_TRUE(s[0]->InputAt(1)->IsImmediate());
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_set, s[0]->flags_mode());
  EXPECT_EQ(cmp.flags_condition, s[0]->flags_condition());
}

TEST_P(TurboshaftInstructionSelectorF32ComparisonTest,
       WithImmediateZeroOnLeft) {
  const Comparison& cmp = GetParam();
  StreamBuilder m(this, MachineType::Int32(), MachineType::Float32());
  m.Return(m.Emit(cmp.op, m.Float32Constant(0.0f), m.Parameter(0)));
  Stream const s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmVcmpF32, s[0]->arch_opcode());
  ASSERT_EQ(2U, s[0]->InputCount());
  EXPECT_TRUE(s[0]->InputAt(1)->IsImmediate());
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_set, s[0]->flags_mode());
  EXPECT_EQ(cmp.commuted_flags_condition, s[0]->flags_condition());
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorF32ComparisonTest,
                         ::testing::ValuesIn(kF32Comparisons));

namespace {

const Comparison kF64Comparisons[] = {
    {TSBinop::kFloat64Equal, "Float64Equal", kEqual, kNotEqual, kEqual},
    {TSBinop::kFloat64LessThan, "Float64LessThan", kFloatLessThan,
     kFloatGreaterThanOrEqualOrUnordered, kFloatGreaterThan},
    {TSBinop::kFloat64LessThanOrEqual, "Float64LessThanOrEqual",
     kFloatLessThanOrEqual, kFloatGreaterThanOrUnordered,
     kFloatGreaterThanOrEqual}};

}  // namespace

using TurboshaftInstructionSelectorF64ComparisonTest =
    TurboshaftInstructionSelectorTestWithParam<Comparison>;

TEST_P(TurboshaftInstructionSelectorF64ComparisonTest, WithParameters) {
  const Comparison& cmp = GetParam();
  StreamBuilder m(this, MachineType::Int32(), MachineType::Float64(),
                  MachineType::Float64());
  m.Return(m.Emit(cmp.op, m.Parameter(0), m.Parameter(1)));
  Stream const s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmVcmpF64, s[0]->arch_opcode());
  ASSERT_EQ(2U, s[0]->InputCount());
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_set, s[0]->flags_mode());
  EXPECT_EQ(cmp.flags_condition, s[0]->flags_condition());
}

TEST_P(TurboshaftInstructionSelectorF64ComparisonTest, NegatedWithParameters) {
  const Comparison& cmp = GetParam();
  StreamBuilder m(this, MachineType::Int32(), MachineType::Float64(),
                  MachineType::Float64());
  m.Return(m.Word32BinaryNot(m.Emit(cmp.op, m.Parameter(0), m.Parameter(1))));
  Stream const s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmVcmpF64, s[0]->arch_opcode());
  ASSERT_EQ(2U, s[0]->InputCount());
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_set, s[0]->flags_mode());
  EXPECT_EQ(cmp.negated_flags_condition, s[0]->flags_condition());
}

TEST_P(TurboshaftInstructionSelectorF64ComparisonTest,
       WithImmediateZeroOnRight) {
  const Comparison& cmp = GetParam();
  StreamBuilder m(this, MachineType::Int32(), MachineType::Float64());
  m.Return(m.Emit(cmp.op, m.Parameter(0), m.Float64Constant(0.0)));
  Stream const s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmVcmpF64, s[0]->arch_opcode());
  ASSERT_EQ(2U, s[0]->InputCount());
  EXPECT_TRUE(s[0]->InputAt(1)->IsImmediate());
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_set, s[0]->flags_mode());
  EXPECT_EQ(cmp.flags_condition, s[0]->flags_condition());
}

TEST_P(TurboshaftInstructionSelectorF64ComparisonTest,
       WithImmediateZeroOnLeft) {
  const Comparison& cmp = GetParam();
  StreamBuilder m(this, MachineType::Int32(), MachineType::Float64());
  m.Return(m.Emit(cmp.op, m.Float64Constant(0.0), m.Parameter(0)));
  Stream const s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmVcmpF64, s[0]->arch_opcode());
  ASSERT_EQ(2U, s[0]->InputCount());
  EXPECT_TRUE(s[0]->InputAt(1)->IsImmediate());
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_set, s[0]->flags_mode());
  EXPECT_EQ(cmp.commuted_flags_condition, s[0]->flags_condition());
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorF64ComparisonTest,
                         ::testing::ValuesIn(kF64Comparisons));

// -----------------------------------------------------------------------------
// Floating point arithmetic.

using TurboshaftInstructionSelectorFAITest =
    TurboshaftInstructionSelectorTestWithParam<FAI>;

TEST_P(TurboshaftInstructionSelectorFAITest, Parameters) {
  const FAI& fai = GetParam();
  StreamBuilder m(this, fai.machine_type, fai.machine_type, fai.machine_type);
  OpIndex const p0 = m.Parameter(0);
  OpIndex const p1 = m.Parameter(1);
  OpIndex const r = m.Emit(fai.op, p0, p1);
  m.Return(r);
  Stream const s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(fai.arch_opcode, s[0]->arch_opcode());
  EXPECT_EQ(kMode_None, s[0]->addressing_mode());
  ASSERT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(s.ToVreg(r), s.ToVreg(s[0]->OutputAt(0)));
  EXPECT_EQ(kFlags_none, s[0]->flags_mode());
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorFAITest,
                         ::testing::ValuesIn(kFAIs));

TEST_F(TurboshaftInstructionSelectorTest, Float32Abs) {
  StreamBuilder m(this, MachineType::Float32(), MachineType::Float32());
  OpIndex const p0 = m.Parameter(0);
  OpIndex const n = m.Float32Abs(p0);
  m.Return(n);
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmVabsF32, s[0]->arch_opcode());
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
  EXPECT_EQ(kArmVabsF64, s[0]->arch_opcode());
  ASSERT_EQ(1U, s[0]->InputCount());
  EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
}

TEST_F(TurboshaftInstructionSelectorTest, Float32AddWithFloat32Mul) {
  {
    StreamBuilder m(this, MachineType::Float32(), MachineType::Float32(),
                    MachineType::Float32(), MachineType::Float32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    OpIndex const p2 = m.Parameter(2);
    OpIndex const n = m.Float32Add(m.Float32Mul(p0, p1), p2);
    m.Return(n);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmVmlaF32, s[0]->arch_opcode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p2), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(2)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_TRUE(
        UnallocatedOperand::cast(s[0]->Output())->HasSameAsInputPolicy());
    EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
    EXPECT_EQ(kFlags_none, s[0]->flags_mode());
  }
  {
    StreamBuilder m(this, MachineType::Float32(), MachineType::Float32(),
                    MachineType::Float32(), MachineType::Float32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    OpIndex const p2 = m.Parameter(2);
    OpIndex const n = m.Float32Add(p0, m.Float32Mul(p1, p2));
    m.Return(n);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmVmlaF32, s[0]->arch_opcode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(s.ToVreg(p2), s.ToVreg(s[0]->InputAt(2)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_TRUE(
        UnallocatedOperand::cast(s[0]->Output())->HasSameAsInputPolicy());
    EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
    EXPECT_EQ(kFlags_none, s[0]->flags_mode());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, Float64AddWithFloat64Mul) {
  {
    StreamBuilder m(this, MachineType::Float64(), MachineType::Float64(),
                    MachineType::Float64(), MachineType::Float64());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    OpIndex const p2 = m.Parameter(2);
    OpIndex const n = m.Float64Add(m.Float64Mul(p0, p1), p2);
    m.Return(n);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmVmlaF64, s[0]->arch_opcode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p2), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(2)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_TRUE(
        UnallocatedOperand::cast(s[0]->Output())->HasSameAsInputPolicy());
    EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
    EXPECT_EQ(kFlags_none, s[0]->flags_mode());
  }
  {
    StreamBuilder m(this, MachineType::Float64(), MachineType::Float64(),
                    MachineType::Float64(), MachineType::Float64());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    OpIndex const p2 = m.Parameter(2);
    OpIndex const n = m.Float64Add(p0, m.Float64Mul(p1, p2));
    m.Return(n);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmVmlaF64, s[0]->arch_opcode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(s.ToVreg(p2), s.ToVreg(s[0]->InputAt(2)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_TRUE(
        UnallocatedOperand::cast(s[0]->Output())->HasSameAsInputPolicy());
    EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
    EXPECT_EQ(kFlags_none, s[0]->flags_mode());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, Float32SubWithFloat32Mul) {
  StreamBuilder m(this, MachineType::Float32(), MachineType::Float32(),
                  MachineType::Float32(), MachineType::Float32());
  OpIndex const p0 = m.Parameter(0);
  OpIndex const p1 = m.Parameter(1);
  OpIndex const p2 = m.Parameter(2);
  OpIndex const n = m.Float32Sub(p0, m.Float32Mul(p1, p2));
  m.Return(n);
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmVmlsF32, s[0]->arch_opcode());
  ASSERT_EQ(3U, s[0]->InputCount());
  EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
  EXPECT_EQ(s.ToVreg(p2), s.ToVreg(s[0]->InputAt(2)));
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_TRUE(UnallocatedOperand::cast(s[0]->Output())->HasSameAsInputPolicy());
  EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
  EXPECT_EQ(kFlags_none, s[0]->flags_mode());
}

TEST_F(TurboshaftInstructionSelectorTest, Float64SubWithFloat64Mul) {
  StreamBuilder m(this, MachineType::Float64(), MachineType::Float64(),
                  MachineType::Float64(), MachineType::Float64());
  OpIndex const p0 = m.Parameter(0);
  OpIndex const p1 = m.Parameter(1);
  OpIndex const p2 = m.Parameter(2);
  OpIndex const n = m.Float64Sub(p0, m.Float64Mul(p1, p2));
  m.Return(n);
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmVmlsF64, s[0]->arch_opcode());
  ASSERT_EQ(3U, s[0]->InputCount());
  EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
  EXPECT_EQ(s.ToVreg(p2), s.ToVreg(s[0]->InputAt(2)));
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_TRUE(UnallocatedOperand::cast(s[0]->Output())->HasSameAsInputPolicy());
  EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
  EXPECT_EQ(kFlags_none, s[0]->flags_mode());
}

TEST_F(TurboshaftInstructionSelectorTest, Float32Sqrt) {
  StreamBuilder m(this, MachineType::Float32(), MachineType::Float32());
  OpIndex const p0 = m.Parameter(0);
  OpIndex const n = m.Float32Sqrt(p0);
  m.Return(n);
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmVsqrtF32, s[0]->arch_opcode());
  ASSERT_EQ(1U, s[0]->InputCount());
  EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
  EXPECT_EQ(kFlags_none, s[0]->flags_mode());
}

TEST_F(TurboshaftInstructionSelectorTest, Float64Sqrt) {
  StreamBuilder m(this, MachineType::Float64(), MachineType::Float64());
  OpIndex const p0 = m.Parameter(0);
  OpIndex const n = m.Float64Sqrt(p0);
  m.Return(n);
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmVsqrtF64, s[0]->arch_opcode());
  ASSERT_EQ(1U, s[0]->InputCount());
  EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
  EXPECT_EQ(kFlags_none, s[0]->flags_mode());
}

// -----------------------------------------------------------------------------
// Flag-setting instructions.

const Comparison kBinopCmpZeroRightInstructions[] = {
    {TSBinop::kWord32Equal, "Word32Equal", kEqual, kNotEqual, kEqual},
    {TSBinop::kWord32NotEqual, "Word32NotEqual", kNotEqual, kEqual, kNotEqual},
    {TSBinop::kInt32LessThan, "Int32LessThan", kNegative, kPositiveOrZero,
     kNegative},
    {TSBinop::kInt32GreaterThanOrEqual, "Int32GreaterThanOrEqual",
     kPositiveOrZero, kNegative, kPositiveOrZero},
    {TSBinop::kUint32LessThanOrEqual, "Uint32LessThanOrEqual", kEqual,
     kNotEqual, kEqual},
    {TSBinop::kUint32GreaterThan, "Uint32GreaterThan", kNotEqual, kEqual,
     kNotEqual}};

const Comparison kBinopCmpZeroLeftInstructions[] = {
    {TSBinop::kWord32Equal, "Word32Equal", kEqual, kNotEqual, kEqual},
    {TSBinop::kWord32NotEqual, "Word32NotEqual", kNotEqual, kEqual, kNotEqual},
    {TSBinop::kInt32GreaterThan, "Int32GreaterThan", kNegative, kPositiveOrZero,
     kNegative},
    {TSBinop::kInt32LessThanOrEqual, "Int32LessThanOrEqual", kPositiveOrZero,
     kNegative, kPositiveOrZero},
    {TSBinop::kUint32GreaterThanOrEqual, "Uint32GreaterThanOrEqual", kEqual,
     kNotEqual, kEqual},
    {TSBinop::kUint32LessThan, "Uint32LessThan", kNotEqual, kEqual, kNotEqual}};

struct FlagSettingInst {
  TSBinop op;
  const char* constructor_name;
  ArchOpcode arch_opcode;
  ArchOpcode no_output_opcode;
};

std::ostream& operator<<(std::ostream& os, const FlagSettingInst& inst) {
  return os << inst.constructor_name;
}

const FlagSettingInst kFlagSettingInstructions[] = {
    {TSBinop::kWord32Add, "Word32Add", kArmAdd, kArmCmn},
    {TSBinop::kWord32BitwiseAnd, "Word32BitwiseAnd", kArmAnd, kArmTst},
    {TSBinop::kWord32BitwiseOr, "Word32Or", kArmOrr, kArmOrr},
    {TSBinop::kWord32BitwiseXor, "Word32Xor", kArmEor, kArmTeq}};

using TurboshaftInstructionSelectorFlagSettingTest =
    TurboshaftInstructionSelectorTestWithParam<FlagSettingInst>;

TEST_P(TurboshaftInstructionSelectorFlagSettingTest, CmpZeroRight) {
  const FlagSettingInst inst = GetParam();
  // Binop with single user : a cmp instruction.
  TRACED_FOREACH(Comparison, cmp, kBinopCmpZeroRightInstructions) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    OpIndex binop = m.Emit(inst.op, m.Parameter(0), m.Parameter(1));
    V<Word32> comp = m.Emit<Word32>(cmp.op, binop, m.Int32Constant(0));
    m.Branch(comp, a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(1));
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    ASSERT_EQ(4U, s[0]->InputCount());  // The labels are also inputs.
    EXPECT_EQ(inst.no_output_opcode, s[0]->arch_opcode());
    EXPECT_EQ(s.ToVreg(m.Parameter(0)), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(m.Parameter(1)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(cmp.flags_condition, s[0]->flags_condition());
  }
}

TEST_P(TurboshaftInstructionSelectorFlagSettingTest, CmpZeroLeft) {
  const FlagSettingInst inst = GetParam();
  // Test a cmp with zero on the left-hand side.
  TRACED_FOREACH(Comparison, cmp, kBinopCmpZeroLeftInstructions) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    OpIndex binop = m.Emit(inst.op, m.Parameter(0), m.Parameter(1));
    V<Word32> comp = m.Emit<Word32>(cmp.op, m.Int32Constant(0), binop);
    m.Branch(comp, a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(1));
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    ASSERT_EQ(4U, s[0]->InputCount());  // The labels are also inputs.
    EXPECT_EQ(inst.no_output_opcode, s[0]->arch_opcode());
    EXPECT_EQ(s.ToVreg(m.Parameter(0)), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(m.Parameter(1)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(cmp.flags_condition, s[0]->flags_condition());
  }
}

TEST_P(TurboshaftInstructionSelectorFlagSettingTest,
       CmpZeroOnlyUserInBasicBlock) {
  const FlagSettingInst inst = GetParam();
  // Binop with additional users, but in a different basic block.
  TRACED_FOREACH(Comparison, cmp, kBinopCmpZeroRightInstructions) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    OpIndex binop = m.Emit(inst.op, m.Parameter(0), m.Parameter(1));
    V<Word32> comp = m.Emit<Word32>(cmp.op, binop, m.Int32Constant(0));
    m.Branch(comp, a, b);
    m.Bind(a);
    m.Return(binop);
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    ASSERT_EQ(4U, s[0]->InputCount());  // The labels are also inputs.
    EXPECT_EQ(inst.arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(s.ToVreg(m.Parameter(0)), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(m.Parameter(1)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(cmp.flags_condition, s[0]->flags_condition());
  }
}

TEST_P(TurboshaftInstructionSelectorFlagSettingTest, ShiftedOperand) {
  const FlagSettingInst inst = GetParam();
  // Like the test above, but with a shifted input to the binary operator.
  TRACED_FOREACH(Comparison, cmp, kBinopCmpZeroRightInstructions) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    OpIndex imm = m.Int32Constant(5);
    OpIndex shift = m.Word32ShiftLeft(m.Parameter(1), imm);
    OpIndex binop = m.Emit(inst.op, m.Parameter(0), shift);
    V<Word32> comp = m.Emit<Word32>(cmp.op, binop, m.Int32Constant(0));
    m.Branch(comp, a, b);
    m.Bind(a);
    m.Return(binop);
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    ASSERT_EQ(5U, s[0]->InputCount());  // The labels are also inputs.
    EXPECT_EQ(inst.arch_opcode, s[0]->arch_opcode());
    EXPECT_EQ(s.ToVreg(m.Parameter(0)), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(m.Parameter(1)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(5, s.ToInt32(s[0]->InputAt(2)));
    EXPECT_EQ(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(cmp.flags_condition, s[0]->flags_condition());
  }
}

TEST_P(TurboshaftInstructionSelectorFlagSettingTest, UsersInSameBasicBlock) {
  const FlagSettingInst inst = GetParam();
  // Binop with additional users, in the same basic block. We need to make sure
  // we don't try to optimise this case.
  TRACED_FOREACH(Comparison, cmp, kComparisons) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    OpIndex binop = m.Emit(inst.op, m.Parameter(0), m.Parameter(1));
    OpIndex mul = m.Word32Mul(m.Parameter(0), binop);
    V<Word32> comp = m.Emit(cmp.op, binop, m.Int32Constant(0));
    m.Branch(comp, a, b);
    m.Bind(a);
    m.Return(mul);
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(3U, s.size());
    EXPECT_EQ(inst.arch_opcode, s[0]->arch_opcode());
    EXPECT_NE(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(kArmMul, s[1]->arch_opcode());
    EXPECT_EQ(kArmCmp, s[2]->arch_opcode());
    EXPECT_EQ(kFlags_branch, s[2]->flags_mode());
    EXPECT_EQ(cmp.flags_condition, s[2]->flags_condition());
  }
}

TEST_P(TurboshaftInstructionSelectorFlagSettingTest, CommuteImmediate) {
  const FlagSettingInst inst = GetParam();
  // Immediate on left hand side of the binary operator.
  TRACED_FOREACH(Comparison, cmp, kBinopCmpZeroRightInstructions) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    Block *a = m.NewBlock(), *b = m.NewBlock();
    OpIndex imm = m.Int32Constant(3);
    OpIndex binop = m.Emit(inst.op, imm, m.Parameter(0));
    V<Word32> comp = m.Emit(cmp.op, binop, m.Int32Constant(0));
    m.Branch(comp, a, b);
    m.Bind(a);
    m.Return(m.Int32Constant(1));
    m.Bind(b);
    m.Return(m.Int32Constant(0));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    ASSERT_EQ(4U, s[0]->InputCount());  // The labels are also inputs.
    EXPECT_EQ(inst.no_output_opcode, s[0]->arch_opcode());
    EXPECT_EQ(s.ToVreg(m.Parameter(0)), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(3, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(kFlags_branch, s[0]->flags_mode());
    EXPECT_EQ(cmp.flags_condition, s[0]->flags_condition());
  }
}

TEST_P(TurboshaftInstructionSelectorFlagSettingTest, CommuteShift) {
  const FlagSettingInst inst = GetParam();
  // Left-hand side operand shifted by immediate.
  TRACED_FOREACH(Comparison, cmp, kBinopCmpZeroRightInstructions) {
    TRACED_FOREACH(Shift, shift, kShifts) {
      StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                      MachineType::Int32());
      OpIndex imm = m.Int32Constant(5);
      OpIndex shifted_operand = m.Emit(shift.op, m.Parameter(0), imm);
      OpIndex binop = m.Emit(inst.op, shifted_operand, m.Parameter(1));
      OpIndex comp = m.Emit(cmp.op, binop, m.Int32Constant(0));
      m.Return(comp);
      Stream s = m.Build();
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(inst.no_output_opcode, s[0]->arch_opcode());
      EXPECT_EQ(shift.i_mode, s[0]->addressing_mode());
      EXPECT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(5, s.ToInt64(s[0]->InputAt(2)));
      EXPECT_EQ(inst.arch_opcode == kArmOrr ? 2U : 1U, s[0]->OutputCount());
      EXPECT_EQ(kFlags_set, s[0]->flags_mode());
      EXPECT_EQ(cmp.flags_condition, s[0]->flags_condition());
    }
  }
}

INSTANTIATE_TEST_SUITE_P(TurboshaftInstructionSelectorTest,
                         TurboshaftInstructionSelectorFlagSettingTest,
                         ::testing::ValuesIn(kFlagSettingInstructions));

// -----------------------------------------------------------------------------
// Miscellaneous.

TEST_F(TurboshaftInstructionSelectorTest, Word32AddWithWord32Mul) {
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32(), MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    OpIndex const p2 = m.Parameter(2);
    OpIndex const n = m.Word32Add(p0, m.Word32Mul(p1, p2));
    m.Return(n);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmMla, s[0]->arch_opcode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p2), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(2)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
  }
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32(), MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    OpIndex const p2 = m.Parameter(2);
    OpIndex const n = m.Word32Add(m.Word32Mul(p1, p2), p0);
    m.Return(n);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmMla, s[0]->arch_opcode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p2), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(2)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
  }
}

TEST_F(TurboshaftInstructionSelectorTest, Word32AddWithInt32MulOverflownBits) {
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32(), MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    OpIndex const p2 = m.Parameter(2);
    OpIndex const n = m.Word32Add(p0, m.Int32MulOverflownBits(p1, p2));
    m.Return(n);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmSmmla, s[0]->arch_opcode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p2), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(2)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
  }
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32(), MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    OpIndex const p2 = m.Parameter(2);
    OpIndex const n = m.Word32Add(m.Int32MulOverflownBits(p1, p2), p0);
    m.Return(n);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmSmmla, s[0]->arch_opcode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p2), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(2)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
  }
}

TEST_F(TurboshaftInstructionSelectorTest, Word32AddWithWord32BitwiseAnd) {
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    OpIndex const r =
        m.Word32Add(m.Word32BitwiseAnd(p0, m.Int32Constant(0xFF)), p1);
    m.Return(r);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmUxtab, s[0]->arch_opcode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(0, s.ToInt32(s[0]->InputAt(2)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(r), s.ToVreg(s[0]->Output()));
  }
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    OpIndex const r =
        m.Word32Add(p1, m.Word32BitwiseAnd(p0, m.Int32Constant(0xFF)));
    m.Return(r);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmUxtab, s[0]->arch_opcode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(0, s.ToInt32(s[0]->InputAt(2)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(r), s.ToVreg(s[0]->Output()));
  }
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    OpIndex const r =
        m.Word32Add(m.Word32BitwiseAnd(p0, m.Int32Constant(0xFFFF)), p1);
    m.Return(r);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmUxtah, s[0]->arch_opcode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(0, s.ToInt32(s[0]->InputAt(2)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(r), s.ToVreg(s[0]->Output()));
  }
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    OpIndex const r =
        m.Word32Add(p1, m.Word32BitwiseAnd(p0, m.Int32Constant(0xFFFF)));
    m.Return(r);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmUxtah, s[0]->arch_opcode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(0, s.ToInt32(s[0]->InputAt(2)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(r), s.ToVreg(s[0]->Output()));
  }
}

TEST_F(TurboshaftInstructionSelectorTest,
       Word32AddWithWord32ShiftRightArithmeticWithWord32ShiftLeft) {
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    OpIndex const r = m.Word32Add(
        m.Word32ShiftRightArithmetic(m.Word32ShiftLeft(p0, m.Int32Constant(24)),
                                     m.Int32Constant(24)),
        p1);
    m.Return(r);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmSxtab, s[0]->arch_opcode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(0, s.ToInt32(s[0]->InputAt(2)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(r), s.ToVreg(s[0]->Output()));
  }
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    OpIndex const r = m.Word32Add(
        p1,
        m.Word32ShiftRightArithmetic(m.Word32ShiftLeft(p0, m.Int32Constant(24)),
                                     m.Int32Constant(24)));
    m.Return(r);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmSxtab, s[0]->arch_opcode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(0, s.ToInt32(s[0]->InputAt(2)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(r), s.ToVreg(s[0]->Output()));
  }
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    OpIndex const r = m.Word32Add(
        m.Word32ShiftRightArithmetic(m.Word32ShiftLeft(p0, m.Int32Constant(16)),
                                     m.Int32Constant(16)),
        p1);
    m.Return(r);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmSxtah, s[0]->arch_opcode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(0, s.ToInt32(s[0]->InputAt(2)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(r), s.ToVreg(s[0]->Output()));
  }
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const p1 = m.Parameter(1);
    OpIndex const r = m.Word32Add(
        p1,
        m.Word32ShiftRightArithmetic(m.Word32ShiftLeft(p0, m.Int32Constant(16)),
                                     m.Int32Constant(16)));
    m.Return(r);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmSxtah, s[0]->arch_opcode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(0, s.ToInt32(s[0]->InputAt(2)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(r), s.ToVreg(s[0]->Output()));
  }
}

TEST_F(TurboshaftInstructionSelectorTest, Word32SubWithWord32Mul) {
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32(), MachineType::Int32());
  m.Return(
      m.Word32Sub(m.Parameter(0), m.Word32Mul(m.Parameter(1), m.Parameter(2))));
  Stream s = m.Build();
  ASSERT_EQ(2U, s.size());
  EXPECT_EQ(kArmMul, s[0]->arch_opcode());
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kArmSub, s[1]->arch_opcode());
  ASSERT_EQ(2U, s[1]->InputCount());
  EXPECT_EQ(s.ToVreg(s[0]->Output()), s.ToVreg(s[1]->InputAt(1)));
}

TEST_F(TurboshaftInstructionSelectorTest, Word32SubWithWord32MulForMLS) {
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32(), MachineType::Int32());
  m.Return(
      m.Word32Sub(m.Parameter(0), m.Word32Mul(m.Parameter(1), m.Parameter(2))));
  Stream s = m.Build(ARMv7);
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmMls, s[0]->arch_opcode());
  EXPECT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(3U, s[0]->InputCount());
}

TEST_F(TurboshaftInstructionSelectorTest, Int32DivWithParameters) {
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  m.Return(m.Int32Div(m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(4U, s.size());
  EXPECT_EQ(kArmVcvtF64S32, s[0]->arch_opcode());
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kArmVcvtF64S32, s[1]->arch_opcode());
  ASSERT_EQ(1U, s[1]->OutputCount());
  EXPECT_EQ(kArmVdivF64, s[2]->arch_opcode());
  ASSERT_EQ(2U, s[2]->InputCount());
  ASSERT_EQ(1U, s[2]->OutputCount());
  EXPECT_EQ(s.ToVreg(s[0]->Output()), s.ToVreg(s[2]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(s[1]->Output()), s.ToVreg(s[2]->InputAt(1)));
  EXPECT_EQ(kArmVcvtS32F64, s[3]->arch_opcode());
  ASSERT_EQ(1U, s[3]->InputCount());
  EXPECT_EQ(s.ToVreg(s[2]->Output()), s.ToVreg(s[3]->InputAt(0)));
}

TEST_F(TurboshaftInstructionSelectorTest, Int32DivWithParametersForSUDIV) {
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  m.Return(m.Int32Div(m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build(SUDIV);
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmSdiv, s[0]->arch_opcode());
}

TEST_F(TurboshaftInstructionSelectorTest, Int32ModWithParameters) {
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  m.Return(m.Int32Mod(m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(6U, s.size());
  EXPECT_EQ(kArmVcvtF64S32, s[0]->arch_opcode());
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kArmVcvtF64S32, s[1]->arch_opcode());
  ASSERT_EQ(1U, s[1]->OutputCount());
  EXPECT_EQ(kArmVdivF64, s[2]->arch_opcode());
  ASSERT_EQ(2U, s[2]->InputCount());
  ASSERT_EQ(1U, s[2]->OutputCount());
  EXPECT_EQ(s.ToVreg(s[0]->Output()), s.ToVreg(s[2]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(s[1]->Output()), s.ToVreg(s[2]->InputAt(1)));
  EXPECT_EQ(kArmVcvtS32F64, s[3]->arch_opcode());
  ASSERT_EQ(1U, s[3]->InputCount());
  EXPECT_EQ(s.ToVreg(s[2]->Output()), s.ToVreg(s[3]->InputAt(0)));
  EXPECT_EQ(kArmMul, s[4]->arch_opcode());
  ASSERT_EQ(1U, s[4]->OutputCount());
  ASSERT_EQ(2U, s[4]->InputCount());
  EXPECT_EQ(s.ToVreg(s[3]->Output()), s.ToVreg(s[4]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(s[1]->InputAt(0)), s.ToVreg(s[4]->InputAt(1)));
  EXPECT_EQ(kArmSub, s[5]->arch_opcode());
  ASSERT_EQ(1U, s[5]->OutputCount());
  ASSERT_EQ(2U, s[5]->InputCount());
  EXPECT_EQ(s.ToVreg(s[0]->InputAt(0)), s.ToVreg(s[5]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(s[4]->Output()), s.ToVreg(s[5]->InputAt(1)));
}

TEST_F(TurboshaftInstructionSelectorTest, Int32ModWithParametersForSUDIV) {
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  m.Return(m.Int32Mod(m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build(SUDIV);
  ASSERT_EQ(3U, s.size());
  EXPECT_EQ(kArmSdiv, s[0]->arch_opcode());
  ASSERT_EQ(1U, s[0]->OutputCount());
  ASSERT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(kArmMul, s[1]->arch_opcode());
  ASSERT_EQ(1U, s[1]->OutputCount());
  ASSERT_EQ(2U, s[1]->InputCount());
  EXPECT_EQ(s.ToVreg(s[0]->Output()), s.ToVreg(s[1]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(s[0]->InputAt(1)), s.ToVreg(s[1]->InputAt(1)));
  EXPECT_EQ(kArmSub, s[2]->arch_opcode());
  ASSERT_EQ(1U, s[2]->OutputCount());
  ASSERT_EQ(2U, s[2]->InputCount());
  EXPECT_EQ(s.ToVreg(s[0]->InputAt(0)), s.ToVreg(s[2]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(s[1]->Output()), s.ToVreg(s[2]->InputAt(1)));
}

TEST_F(TurboshaftInstructionSelectorTest,
       Int32ModWithParametersForSUDIVAndMLS) {
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  m.Return(m.Int32Mod(m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build(ARMv7, SUDIV);
  ASSERT_EQ(2U, s.size());
  EXPECT_EQ(kArmSdiv, s[0]->arch_opcode());
  ASSERT_EQ(1U, s[0]->OutputCount());
  ASSERT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(kArmMls, s[1]->arch_opcode());
  ASSERT_EQ(1U, s[1]->OutputCount());
  ASSERT_EQ(3U, s[1]->InputCount());
  EXPECT_EQ(s.ToVreg(s[0]->Output()), s.ToVreg(s[1]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(s[0]->InputAt(1)), s.ToVreg(s[1]->InputAt(1)));
  EXPECT_EQ(s.ToVreg(s[0]->InputAt(0)), s.ToVreg(s[1]->InputAt(2)));
}

TEST_F(TurboshaftInstructionSelectorTest, Word32MulWithParameters) {
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  m.Return(m.Word32Mul(m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmMul, s[0]->arch_opcode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}

TEST_F(TurboshaftInstructionSelectorTest, Word32MulWithImmediate) {
  // x * (2^k + 1) -> x + (x >> k)
  TRACED_FORRANGE(int32_t, k, 1, 30) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(m.Word32Mul(m.Parameter(0), m.Int32Constant((1 << k) + 1)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmAdd, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(s[0]->InputAt(0)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(k, s.ToInt32(s[0]->InputAt(2)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  // x * (2^k - 1) -> -x + (x >> k)
  TRACED_FORRANGE(int32_t, k, 3, 30) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(m.Word32Mul(m.Parameter(0), m.Int32Constant((1 << k) - 1)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmRsb, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(s[0]->InputAt(0)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(k, s.ToInt32(s[0]->InputAt(2)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  // (2^k + 1) * x -> x + (x >> k)
  TRACED_FORRANGE(int32_t, k, 1, 30) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(m.Word32Mul(m.Int32Constant((1 << k) + 1), m.Parameter(0)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmAdd, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(s[0]->InputAt(0)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(k, s.ToInt32(s[0]->InputAt(2)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  // x * (2^k - 1) -> -x + (x >> k)
  TRACED_FORRANGE(int32_t, k, 3, 30) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(m.Word32Mul(m.Int32Constant((1 << k) - 1), m.Parameter(0)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmRsb, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R_LSL_I, s[0]->addressing_mode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(s[0]->InputAt(0)), s.ToVreg(s[0]->InputAt(1)));
    EXPECT_EQ(k, s.ToInt32(s[0]->InputAt(2)));
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, Int32MulOverflownBitsWithParameters) {
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  OpIndex const p0 = m.Parameter(0);
  OpIndex const p1 = m.Parameter(1);
  OpIndex const n = m.Int32MulOverflownBits(p0, p1);
  m.Return(n);
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmSmmul, s[0]->arch_opcode());
  ASSERT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
}

TEST_F(TurboshaftInstructionSelectorTest,
       UInt32MulOverflownBitsWithParameters) {
  StreamBuilder m(this, MachineType::Uint32(), MachineType::Uint32(),
                  MachineType::Uint32());
  OpIndex const p0 = m.Parameter(0);
  OpIndex const p1 = m.Parameter(1);
  OpIndex const n = m.Uint32MulOverflownBits(p0, p1);
  m.Return(n);
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmUmull, s[0]->arch_opcode());
  ASSERT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
  ASSERT_EQ(2U, s[0]->OutputCount());
  EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->OutputAt(1)));
}

TEST_F(TurboshaftInstructionSelectorTest, Uint32DivWithParameters) {
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  m.Return(m.Uint32Div(m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(4U, s.size());
  EXPECT_EQ(kArmVcvtF64U32, s[0]->arch_opcode());
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kArmVcvtF64U32, s[1]->arch_opcode());
  ASSERT_EQ(1U, s[1]->OutputCount());
  EXPECT_EQ(kArmVdivF64, s[2]->arch_opcode());
  ASSERT_EQ(2U, s[2]->InputCount());
  ASSERT_EQ(1U, s[2]->OutputCount());
  EXPECT_EQ(s.ToVreg(s[0]->Output()), s.ToVreg(s[2]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(s[1]->Output()), s.ToVreg(s[2]->InputAt(1)));
  EXPECT_EQ(kArmVcvtU32F64, s[3]->arch_opcode());
  ASSERT_EQ(1U, s[3]->InputCount());
  EXPECT_EQ(s.ToVreg(s[2]->Output()), s.ToVreg(s[3]->InputAt(0)));
}

TEST_F(TurboshaftInstructionSelectorTest, Uint32DivWithParametersForSUDIV) {
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  m.Return(m.Uint32Div(m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build(SUDIV);
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmUdiv, s[0]->arch_opcode());
}

TEST_F(TurboshaftInstructionSelectorTest, Uint32ModWithParameters) {
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  m.Return(m.Uint32Mod(m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(6U, s.size());
  EXPECT_EQ(kArmVcvtF64U32, s[0]->arch_opcode());
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kArmVcvtF64U32, s[1]->arch_opcode());
  ASSERT_EQ(1U, s[1]->OutputCount());
  EXPECT_EQ(kArmVdivF64, s[2]->arch_opcode());
  ASSERT_EQ(2U, s[2]->InputCount());
  ASSERT_EQ(1U, s[2]->OutputCount());
  EXPECT_EQ(s.ToVreg(s[0]->Output()), s.ToVreg(s[2]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(s[1]->Output()), s.ToVreg(s[2]->InputAt(1)));
  EXPECT_EQ(kArmVcvtU32F64, s[3]->arch_opcode());
  ASSERT_EQ(1U, s[3]->InputCount());
  EXPECT_EQ(s.ToVreg(s[2]->Output()), s.ToVreg(s[3]->InputAt(0)));
  EXPECT_EQ(kArmMul, s[4]->arch_opcode());
  ASSERT_EQ(1U, s[4]->OutputCount());
  ASSERT_EQ(2U, s[4]->InputCount());
  EXPECT_EQ(s.ToVreg(s[3]->Output()), s.ToVreg(s[4]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(s[1]->InputAt(0)), s.ToVreg(s[4]->InputAt(1)));
  EXPECT_EQ(kArmSub, s[5]->arch_opcode());
  ASSERT_EQ(1U, s[5]->OutputCount());
  ASSERT_EQ(2U, s[5]->InputCount());
  EXPECT_EQ(s.ToVreg(s[0]->InputAt(0)), s.ToVreg(s[5]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(s[4]->Output()), s.ToVreg(s[5]->InputAt(1)));
}

TEST_F(TurboshaftInstructionSelectorTest, Uint32ModWithParametersForSUDIV) {
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  m.Return(m.Uint32Mod(m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build(SUDIV);
  ASSERT_EQ(3U, s.size());
  EXPECT_EQ(kArmUdiv, s[0]->arch_opcode());
  ASSERT_EQ(1U, s[0]->OutputCount());
  ASSERT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(kArmMul, s[1]->arch_opcode());
  ASSERT_EQ(1U, s[1]->OutputCount());
  ASSERT_EQ(2U, s[1]->InputCount());
  EXPECT_EQ(s.ToVreg(s[0]->Output()), s.ToVreg(s[1]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(s[0]->InputAt(1)), s.ToVreg(s[1]->InputAt(1)));
  EXPECT_EQ(kArmSub, s[2]->arch_opcode());
  ASSERT_EQ(1U, s[2]->OutputCount());
  ASSERT_EQ(2U, s[2]->InputCount());
  EXPECT_EQ(s.ToVreg(s[0]->InputAt(0)), s.ToVreg(s[2]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(s[1]->Output()), s.ToVreg(s[2]->InputAt(1)));
}

TEST_F(TurboshaftInstructionSelectorTest,
       Uint32ModWithParametersForSUDIVAndMLS) {
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  m.Return(m.Uint32Mod(m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build(ARMv7, SUDIV);
  ASSERT_EQ(2U, s.size());
  EXPECT_EQ(kArmUdiv, s[0]->arch_opcode());
  ASSERT_EQ(1U, s[0]->OutputCount());
  ASSERT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(kArmMls, s[1]->arch_opcode());
  ASSERT_EQ(1U, s[1]->OutputCount());
  ASSERT_EQ(3U, s[1]->InputCount());
  EXPECT_EQ(s.ToVreg(s[0]->Output()), s.ToVreg(s[1]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(s[0]->InputAt(1)), s.ToVreg(s[1]->InputAt(1)));
  EXPECT_EQ(s.ToVreg(s[0]->InputAt(0)), s.ToVreg(s[1]->InputAt(2)));
}

TEST_F(TurboshaftInstructionSelectorTest,
       Word32ShiftLeftWord32ShiftRightArithmeticForSbfx) {
  TRACED_FORRANGE(int32_t, shl, 1, 31) {
    TRACED_FORRANGE(int32_t, sar, shl, 31) {
      if ((shl == sar) && (sar == 16)) continue;  // Sxth.
      if ((shl == sar) && (sar == 24)) continue;  // Sxtb.
      StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
      m.Return(m.Word32ShiftRightArithmetic(
          m.Word32ShiftLeft(m.Parameter(0), m.Int32Constant(shl)),
          m.Int32Constant(sar)));
      Stream s = m.Build(ARMv7);
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(kArmSbfx, s[0]->arch_opcode());
      ASSERT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(sar - shl, s.ToInt32(s[0]->InputAt(1)));
      EXPECT_EQ(32 - sar, s.ToInt32(s[0]->InputAt(2)));
    }
  }
}

TEST_F(TurboshaftInstructionSelectorTest,
       Word32BitwiseAndWithUbfxImmediateForARMv7) {
  TRACED_FORRANGE(int32_t, width, 9, 23) {
    if (width == 16) continue;  // Uxth.
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(m.Word32BitwiseAnd(m.Parameter(0),
                                m.Int32Constant(0xFFFFFFFFu >> (32 - width))));
    Stream s = m.Build(ARMv7);
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmUbfx, s[0]->arch_opcode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(0, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(width, s.ToInt32(s[0]->InputAt(2)));
  }
  TRACED_FORRANGE(int32_t, width, 9, 23) {
    if (width == 16) continue;  // Uxth.
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(m.Word32BitwiseAnd(m.Int32Constant(0xFFFFFFFFu >> (32 - width)),
                                m.Parameter(0)));
    Stream s = m.Build(ARMv7);
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmUbfx, s[0]->arch_opcode());
    ASSERT_EQ(3U, s[0]->InputCount());
    EXPECT_EQ(0, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(width, s.ToInt32(s[0]->InputAt(2)));
  }
}

TEST_F(TurboshaftInstructionSelectorTest,
       Word32BitwiseAndWithBfcImmediateForARMv7) {
  TRACED_FORRANGE(int32_t, lsb, 0, 31) {
    TRACED_FORRANGE(int32_t, width, 9, (24 - lsb) - 1) {
      StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
      m.Return(m.Word32BitwiseAnd(
          m.Parameter(0),
          m.Int32Constant(~((0xFFFFFFFFu >> (32 - width)) << lsb))));
      Stream s = m.Build(ARMv7);
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(kArmBfc, s[0]->arch_opcode());
      ASSERT_EQ(1U, s[0]->OutputCount());
      EXPECT_TRUE(
          UnallocatedOperand::cast(s[0]->Output())->HasSameAsInputPolicy());
      ASSERT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(lsb, s.ToInt32(s[0]->InputAt(1)));
      EXPECT_EQ(width, s.ToInt32(s[0]->InputAt(2)));
    }
  }
  TRACED_FORRANGE(int32_t, lsb, 0, 31) {
    TRACED_FORRANGE(int32_t, width, 9, (24 - lsb) - 1) {
      StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
      m.Return(m.Word32BitwiseAnd(
          m.Int32Constant(~((0xFFFFFFFFu >> (32 - width)) << lsb)),
          m.Parameter(0)));
      Stream s = m.Build(ARMv7);
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(kArmBfc, s[0]->arch_opcode());
      ASSERT_EQ(1U, s[0]->OutputCount());
      EXPECT_TRUE(
          UnallocatedOperand::cast(s[0]->Output())->HasSameAsInputPolicy());
      ASSERT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(lsb, s.ToInt32(s[0]->InputAt(1)));
      EXPECT_EQ(width, s.ToInt32(s[0]->InputAt(2)));
    }
  }
}

TEST_F(TurboshaftInstructionSelectorTest, Word32BitwiseAndWith0xFFFF) {
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const r = m.Word32BitwiseAnd(p0, m.Int32Constant(0xFFFF));
    m.Return(r);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmUxth, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(0, s.ToInt32(s[0]->InputAt(1)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(r), s.ToVreg(s[0]->Output()));
  }
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const r = m.Word32BitwiseAnd(m.Int32Constant(0xFFFF), p0);
    m.Return(r);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmUxth, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(0, s.ToInt32(s[0]->InputAt(1)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(r), s.ToVreg(s[0]->Output()));
  }
}

TEST_F(TurboshaftInstructionSelectorTest,
       Word32ShiftRightArithmeticWithWord32ShiftLeft) {
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const r = m.Word32ShiftRightArithmetic(
        m.Word32ShiftLeft(p0, m.Int32Constant(24)), m.Int32Constant(24));
    m.Return(r);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmSxtb, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(0, s.ToInt32(s[0]->InputAt(1)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(r), s.ToVreg(s[0]->Output()));
  }
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const r = m.Word32ShiftRightArithmetic(
        m.Word32ShiftLeft(p0, m.Int32Constant(16)), m.Int32Constant(16));
    m.Return(r);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmSxth, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(0, s.ToInt32(s[0]->InputAt(1)));
    ASSERT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(s.ToVreg(r), s.ToVreg(s[0]->Output()));
  }
}

TEST_F(TurboshaftInstructionSelectorTest,
       Word32ShiftRightLogicalWithWord32BitwiseAndWithImmediateForARMv7) {
  TRACED_FORRANGE(int32_t, lsb, 0, 31) {
    TRACED_FORRANGE(int32_t, width, 1, 32 - lsb) {
      uint32_t max = 1 << lsb;
      if (max > static_cast<uint32_t>(kMaxInt)) max -= 1;
      uint32_t jnk = rng()->NextInt(max);
      uint32_t msk = ((0xFFFFFFFFu >> (32 - width)) << lsb) | jnk;
      StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
      m.Return(m.Word32ShiftRightLogical(
          m.Word32BitwiseAnd(m.Parameter(0), m.Int32Constant(msk)),
          m.Int32Constant(lsb)));
      Stream s = m.Build(ARMv7);
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(kArmUbfx, s[0]->arch_opcode());
      ASSERT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(lsb, s.ToInt32(s[0]->InputAt(1)));
      EXPECT_EQ(width, s.ToInt32(s[0]->InputAt(2)));
    }
  }
  TRACED_FORRANGE(int32_t, lsb, 0, 31) {
    TRACED_FORRANGE(int32_t, width, 1, 32 - lsb) {
      uint32_t max = 1 << lsb;
      if (max > static_cast<uint32_t>(kMaxInt)) max -= 1;
      uint32_t jnk = rng()->NextInt(max);
      uint32_t msk = ((0xFFFFFFFFu >> (32 - width)) << lsb) | jnk;
      StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
      m.Return(m.Word32ShiftRightLogical(
          m.Word32BitwiseAnd(m.Int32Constant(msk), m.Parameter(0)),
          m.Int32Constant(lsb)));
      Stream s = m.Build(ARMv7);
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(kArmUbfx, s[0]->arch_opcode());
      ASSERT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(lsb, s.ToInt32(s[0]->InputAt(1)));
      EXPECT_EQ(width, s.ToInt32(s[0]->InputAt(2)));
    }
  }
}

TEST_F(TurboshaftInstructionSelectorTest,
       Word32BitwiseAndWithWord32BitwiseNot) {
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    m.Return(
        m.Word32BitwiseAnd(m.Parameter(0), m.Word32BitwiseNot(m.Parameter(1))));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmBic, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R, s[0]->addressing_mode());
    EXPECT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                    MachineType::Int32());
    m.Return(
        m.Word32BitwiseAnd(m.Word32BitwiseNot(m.Parameter(0)), m.Parameter(1)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmBic, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_R, s[0]->addressing_mode());
    EXPECT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(1U, s[0]->OutputCount());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, Word32EqualWithParameters) {
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32(),
                  MachineType::Int32());
  m.Return(m.Word32Equal(m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmCmp, s[0]->arch_opcode());
  EXPECT_EQ(kMode_Operand2_R, s[0]->addressing_mode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(kFlags_set, s[0]->flags_mode());
  EXPECT_EQ(kEqual, s[0]->flags_condition());
}

TEST_F(TurboshaftInstructionSelectorTest, Word32EqualWithImmediate) {
  TRACED_FOREACH(int32_t, imm, kImmediates) {
    if (imm == 0) continue;
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(m.Word32Equal(m.Parameter(0), m.Int32Constant(imm)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmCmp, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_I, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kEqual, s[0]->flags_condition());
  }
  TRACED_FOREACH(int32_t, imm, kImmediates) {
    if (imm == 0) continue;
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(m.Word32Equal(m.Int32Constant(imm), m.Parameter(0)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmCmp, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_I, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(imm, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kEqual, s[0]->flags_condition());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, Word32EqualWithZero) {
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(m.Word32Equal(m.Parameter(0), m.Int32Constant(0)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmCmp, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_I, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(m.Parameter(0)), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(0, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kEqual, s[0]->flags_condition());
  }
  {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    m.Return(m.Word32Equal(m.Int32Constant(0), m.Parameter(0)));
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmCmp, s[0]->arch_opcode());
    EXPECT_EQ(kMode_Operand2_I, s[0]->addressing_mode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(s.ToVreg(m.Parameter(0)), s.ToVreg(s[0]->InputAt(0)));
    EXPECT_EQ(0, s.ToInt32(s[0]->InputAt(1)));
    EXPECT_EQ(1U, s[0]->OutputCount());
    EXPECT_EQ(kFlags_set, s[0]->flags_mode());
    EXPECT_EQ(kEqual, s[0]->flags_condition());
  }
}

TEST_F(TurboshaftInstructionSelectorTest, Word32BitwiseNotWithParameter) {
  StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
  m.Return(m.Word32BitwiseNot(m.Parameter(0)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmMvn, s[0]->arch_opcode());
  EXPECT_EQ(kMode_Operand2_R, s[0]->addressing_mode());
  EXPECT_EQ(1U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}

TEST_F(TurboshaftInstructionSelectorTest,
       Word32BitwiseAndWithWord32ShiftRightLogicalWithImmediateForARMv7) {
  TRACED_FORRANGE(int32_t, lsb, 1, 31) {
    TRACED_FORRANGE(int32_t, width, 1, 32 - lsb) {
      if (((width == 8) || (width == 16)) &&
          ((lsb == 8) || (lsb == 16) || (lsb == 24)))
        continue;  // Uxtb/h ror.
      StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
      m.Return(m.Word32BitwiseAnd(
          m.Word32ShiftRightLogical(m.Parameter(0), m.Int32Constant(lsb)),
          m.Int32Constant(0xFFFFFFFFu >> (32 - width))));
      Stream s = m.Build(ARMv7);
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(kArmUbfx, s[0]->arch_opcode());
      ASSERT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(lsb, s.ToInt32(s[0]->InputAt(1)));
      EXPECT_EQ(width, s.ToInt32(s[0]->InputAt(2)));
    }
  }
  TRACED_FORRANGE(int32_t, lsb, 1, 31) {
    TRACED_FORRANGE(int32_t, width, 1, 32 - lsb) {
      if (((width == 8) || (width == 16)) &&
          ((lsb == 8) || (lsb == 16) || (lsb == 24)))
        continue;  // Uxtb/h ror.
      StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
      m.Return(m.Word32BitwiseAnd(
          m.Int32Constant(0xFFFFFFFFu >> (32 - width)),
          m.Word32ShiftRightLogical(m.Parameter(0), m.Int32Constant(lsb))));
      Stream s = m.Build(ARMv7);
      ASSERT_EQ(1U, s.size());
      EXPECT_EQ(kArmUbfx, s[0]->arch_opcode());
      ASSERT_EQ(3U, s[0]->InputCount());
      EXPECT_EQ(lsb, s.ToInt32(s[0]->InputAt(1)));
      EXPECT_EQ(width, s.ToInt32(s[0]->InputAt(2)));
    }
  }
}

TEST_F(TurboshaftInstructionSelectorTest,
       Word32BitwiseAndWithWord32ShiftRightLogicalAnd0xFF) {
  TRACED_FORRANGE(int32_t, shr, 1, 3) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const r = m.Word32BitwiseAnd(
        m.Word32ShiftRightLogical(p0, m.Int32Constant(shr * 8)),
        m.Int32Constant(0xFF));
    m.Return(r);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmUxtb, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(shr * 8, s.ToInt32(s[0]->InputAt(1)));
  }
  TRACED_FORRANGE(int32_t, shr, 1, 3) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const r = m.Word32BitwiseAnd(
        m.Int32Constant(0xFF),
        m.Word32ShiftRightLogical(p0, m.Int32Constant(shr * 8)));
    m.Return(r);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmUxtb, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(shr * 8, s.ToInt32(s[0]->InputAt(1)));
  }
}

TEST_F(TurboshaftInstructionSelectorTest,
       Word32BitwiseAndWithWord32ShiftRightLogicalAnd0xFFFF) {
  TRACED_FORRANGE(int32_t, shr, 1, 2) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const r = m.Word32BitwiseAnd(
        m.Word32ShiftRightLogical(p0, m.Int32Constant(shr * 8)),
        m.Int32Constant(0xFFFF));
    m.Return(r);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmUxth, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(shr * 8, s.ToInt32(s[0]->InputAt(1)));
  }
  TRACED_FORRANGE(int32_t, shr, 1, 2) {
    StreamBuilder m(this, MachineType::Int32(), MachineType::Int32());
    OpIndex const p0 = m.Parameter(0);
    OpIndex const r = m.Word32BitwiseAnd(
        m.Int32Constant(0xFFFF),
        m.Word32ShiftRightLogical(p0, m.Int32Constant(shr * 8)));
    m.Return(r);
    Stream s = m.Build();
    ASSERT_EQ(1U, s.size());
    EXPECT_EQ(kArmUxth, s[0]->arch_opcode());
    ASSERT_EQ(2U, s[0]->InputCount());
    EXPECT_EQ(shr * 8, s.ToInt32(s[0]->InputAt(1)));
  }
}

TEST_F(TurboshaftInstructionSelectorTest, Word32Clz) {
  StreamBuilder m(this, MachineType::Uint32(), MachineType::Uint32());
  OpIndex const p0 = m.Parameter(0);
  OpIndex const n = m.Word32CountLeadingZeros(p0);
  m.Return(n);
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmClz, s[0]->arch_opcode());
  ASSERT_EQ(1U, s[0]->InputCount());
  EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
}

TEST_F(TurboshaftInstructionSelectorTest, Float64Max) {
  StreamBuilder m(this, MachineType::Float64(), MachineType::Float64(),
                  MachineType::Float64());
  OpIndex const p0 = m.Parameter(0);
  OpIndex const p1 = m.Parameter(1);
  OpIndex const n = m.Float64Max(p0, p1);
  m.Return(n);
  Stream s = m.Build(ARMv8);
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmFloat64Max, s[0]->arch_opcode());
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
  Stream s = m.Build(ARMv8);
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmFloat64Min, s[0]->arch_opcode());
  ASSERT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  EXPECT_EQ(s.ToVreg(p1), s.ToVreg(s[0]->InputAt(1)));
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
}

TEST_F(TurboshaftInstructionSelectorTest, Float32Neg) {
  StreamBuilder m(this, MachineType::Float32(), MachineType::Float32());
  OpIndex const p0 = m.Parameter(0);
  // Don't use m.Float32Neg() as that generates an explicit sub.
  OpIndex const n = m.Float32Negate(m.Parameter(0));
  m.Return(n);
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmVnegF32, s[0]->arch_opcode());
  ASSERT_EQ(1U, s[0]->InputCount());
  EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
}

TEST_F(TurboshaftInstructionSelectorTest, Float64Neg) {
  StreamBuilder m(this, MachineType::Float64(), MachineType::Float64());
  OpIndex const p0 = m.Parameter(0);
  // Don't use m.Float64Neg() as that generates an explicit sub.
  OpIndex const n = m.Float64Negate(m.Parameter(0));
  m.Return(n);
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmVnegF64, s[0]->arch_opcode());
  ASSERT_EQ(1U, s[0]->InputCount());
  EXPECT_EQ(s.ToVreg(p0), s.ToVreg(s[0]->InputAt(0)));
  ASSERT_EQ(1U, s[0]->OutputCount());
  EXPECT_EQ(s.ToVreg(n), s.ToVreg(s[0]->Output()));
}
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

  OpIndex left = pairwiseAdd, right = y;
  if (params.side == LEFT) std::swap(left, right);

  OpIndex add =
      params.width == 32 ? m.I32x4Add(left, right) : m.I16x8Add(left, right);

  m.Return(add);
  Stream s = m.Build();

  // Should be fused to Vpadal
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kArmVpadal, s[0]->arch_opcode());
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

}  // namespace v8::internal::compiler::turboshaft
