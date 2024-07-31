// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/assembler-inl.h"
#include "src/compiler/pipeline.h"
#include "test/unittests/compiler/backend/instruction-sequence-unittest.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

// We can't just use the size of the moves collection, because of
// redundant moves which need to be discounted.
int GetMoveCount(const ParallelMove& moves) {
  int move_count = 0;
  for (auto move : moves) {
    if (move->IsEliminated() || move->IsRedundant()) continue;
    ++move_count;
  }
  return move_count;
}

bool AreOperandsOfSameType(
    const AllocatedOperand& op,
    const InstructionSequenceTest::TestOperand& test_op) {
  bool test_op_is_reg =
      (test_op.type_ ==
           InstructionSequenceTest::TestOperandType::kFixedRegister ||
       test_op.type_ == InstructionSequenceTest::TestOperandType::kRegister);

  return (op.IsRegister() && test_op_is_reg) ||
         (op.IsStackSlot() && !test_op_is_reg);
}

bool AllocatedOperandMatches(
    const AllocatedOperand& op,
    const InstructionSequenceTest::TestOperand& test_op) {
  return AreOperandsOfSameType(op, test_op) &&
         ((op.IsRegister() ? op.GetRegister().code() : op.index()) ==
              test_op.value_ ||
          test_op.value_ == InstructionSequenceTest::kNoValue);
}

int GetParallelMoveCount(int instr_index, Instruction::GapPosition gap_pos,
                         const InstructionSequence* sequence) {
  const ParallelMove* moves =
      sequence->InstructionAt(instr_index)->GetParallelMove(gap_pos);
  if (moves == nullptr) return 0;
  return GetMoveCount(*moves);
}

bool IsParallelMovePresent(int instr_index, Instruction::GapPosition gap_pos,
                           const InstructionSequence* sequence,
                           const InstructionSequenceTest::TestOperand& src,
                           const InstructionSequenceTest::TestOperand& dest) {
  const ParallelMove* moves =
      sequence->InstructionAt(instr_index)->GetParallelMove(gap_pos);
  EXPECT_NE(nullptr, moves);

  bool found_match = false;
  for (auto move : *moves) {
    if (move->IsEliminated() || move->IsRedundant()) continue;
    if (AllocatedOperandMatches(AllocatedOperand::cast(move->source()), src) &&
        AllocatedOperandMatches(AllocatedOperand::cast(move->destination()),
                                dest)) {
      found_match = true;
      break;
    }
  }
  return found_match;
}


class RegisterAllocatorTest : public InstructionSequenceTest {
 public:
  void Allocate() {
    WireBlocks();
    Pipeline::AllocateRegistersForTesting(config(), sequence(), true);
  }
};

TEST_F(RegisterAllocatorTest, CanAllocateThreeRegisters) {
  // return p0 + p1;
  StartBlock();
  auto a_reg = Parameter();
  auto b_reg = Parameter();
  auto c_reg = EmitOI(Reg(1), Reg(a_reg, 1), Reg(b_reg, 0));
  Return(c_reg);
  EndBlock(Last());

  Allocate();
}

TEST_F(RegisterAllocatorTest, CanAllocateFPRegisters) {
  StartBlock();
  TestOperand inputs[] = {
      Reg(FPParameter(kFloat64)), Reg(FPParameter(kFloat64)),
      Reg(FPParameter(kFloat32)), Reg(FPParameter(kFloat32)),
      Reg(FPParameter(kSimd128)), Reg(FPParameter(kSimd128))};
  VReg out1 = EmitOI(FPReg(1, kFloat64), arraysize(inputs), inputs);
  Return(out1);
  EndBlock(Last());

  Allocate();
}

TEST_F(RegisterAllocatorTest, SimpleLoop) {
  // i = K;
  // while(true) { i++ }
  StartBlock();
  auto i_reg = DefineConstant();
  // Add a branch around the loop to ensure the end-block
  // is connected.
  EndBlock(Branch(Reg(DefineConstant()), 3, 1));

  StartBlock();
  EndBlock();

  {
    StartLoop(1);

    StartBlock();
    auto phi = Phi(i_reg, 2);
    auto ipp = EmitOI(Same(), Reg(phi), Use(DefineConstant()));
    SetInput(phi, 1, ipp);
    EndBlock(Jump(0));

    EndLoop();
  }

  StartBlock();
  EndBlock();

  Allocate();
}

TEST_F(RegisterAllocatorTest, SimpleBranch) {
  // return i ? K1 : K2
  StartBlock();
  auto i = DefineConstant();
  EndBlock(Branch(Reg(i), 1, 2));

  StartBlock();
  Return(DefineConstant());
  EndBlock(Last());

  StartBlock();
  Return(DefineConstant());
  EndBlock(Last());

  Allocate();
}

TEST_F(RegisterAllocatorTest, SimpleDiamond) {
  // return p0 ? p0 : p0
  StartBlock();
  auto param = Parameter();
  EndBlock(Branch(Reg(param), 1, 2));

  StartBlock();
  EndBlock(Jump(2));

  StartBlock();
  EndBlock(Jump(1));

  StartBlock();
  Return(param);
  EndBlock();

  Allocate();
}

TEST_F(RegisterAllocatorTest, SimpleDiamondPhi) {
  // return i ? K1 : K2
  StartBlock();
  EndBlock(Branch(Reg(DefineConstant()), 1, 2));

  StartBlock();
  auto t_val = DefineConstant();
  EndBlock(Jump(2));

  StartBlock();
  auto f_val = DefineConstant();
  EndBlock(Jump(1));

  StartBlock();
  Return(Reg(Phi(t_val, f_val)));
  EndBlock();

  Allocate();
}

TEST_F(RegisterAllocatorTest, DiamondManyPhis) {
  constexpr int kPhis = Register::kNumRegisters * 2;

  StartBlock();
  EndBlock(Branch(Reg(DefineConstant()), 1, 2));

  StartBlock();
  VReg t_vals[kPhis];
  for (int i = 0; i < kPhis; ++i) {
    t_vals[i] = DefineConstant();
  }
  EndBlock(Jump(2));

  StartBlock();
  VReg f_vals[kPhis];
  for (int i = 0; i < kPhis; ++i) {
    f_vals[i] = DefineConstant();
  }
  EndBlock(Jump(1));

  StartBlock();
  TestOperand merged[kPhis];
  for (int i = 0; i < kPhis; ++i) {
    merged[i] = Use(Phi(t_vals[i], f_vals[i]));
  }
  Return(EmitCall(Slot(-1), kPhis, merged));
  EndBlock();

  Allocate();
}

TEST_F(RegisterAllocatorTest, DoubleDiamondManyRedundantPhis) {
  constexpr int kPhis = Register::kNumRegisters * 2;

  // First diamond.
  StartBlock();
  VReg vals[kPhis];
  for (int i = 0; i < kPhis; ++i) {
    vals[i] = Parameter(Slot(-1 - i));
  }
  EndBlock(Branch(Reg(DefineConstant()), 1, 2));

  StartBlock();
  EndBlock(Jump(2));

  StartBlock();
  EndBlock(Jump(1));

  // Second diamond.
  StartBlock();
  EndBlock(Branch(Reg(DefineConstant()), 1, 2));

  StartBlock();
  EndBlock(Jump(2));

  StartBlock();
  EndBlock(Jump(1));

  StartBlock();
  TestOperand merged[kPhis];
  for (int i = 0; i < kPhis; ++i) {
    merged[i] = Use(Phi(vals[i], vals[i]));
  }
  Return(EmitCall(Reg(0), kPhis, merged));
  EndBlock();

  Allocate();
}

TEST_F(RegisterAllocatorTest, RegressionPhisNeedTooManyRegisters) {
  const size_t kNumRegs = 3;
  const size_t kParams = kNumRegs + 1;
  // Override number of registers.
  SetNumRegs(kNumRegs, kNumRegs);

  StartBlock();
  auto constant = DefineConstant();
  VReg parameters[kParams];
  for (size_t i = 0; i < arraysize(parameters); ++i) {
    parameters[i] = DefineConstant();
  }
  EndBlock();

  PhiInstruction* phis[kParams];
  {
    StartLoop(2);

    // Loop header.
    StartBlock();

    for (size_t i = 0; i < arraysize(parameters); ++i) {
      phis[i] = Phi(parameters[i], 2);
    }

    // Perform some computations.
    // something like phi[i] += const
    for (size_t i = 0; i < arraysize(parameters); ++i) {
      auto result = EmitOI(Same(), Reg(phis[i]), Use(constant));
      SetInput(phis[i], 1, result);
    }

    EndBlock(Branch(Reg(DefineConstant()), 1, 2));

    // Jump back to loop header.
    StartBlock();
    EndBlock(Jump(-1));

    EndLoop();
  }

  StartBlock();
  Return(DefineConstant());
  EndBlock();

  Allocate();
}

TEST_F(RegisterAllocatorTest, SpillPhi) {
  StartBlock();
  EndBlock(Branch(Imm(), 1, 2));

  StartBlock();
  auto left = Define(Reg(0));
  EndBlock(Jump(2));

  StartBlock();
  auto right = Define(Reg(0));
  EndBlock();

  StartBlock();
  auto phi = Phi(left, right);
  EmitCall(Slot(-1));
  Return(Reg(phi));
  EndBlock();

  Allocate();
}

TEST_F(RegisterAllocatorTest, MoveLotsOfConstants) {
  StartBlock();
  VReg constants[Register::kNumRegisters];
  for (size_t i = 0; i < arraysize(constants); ++i) {
    constants[i] = DefineConstant();
  }
  TestOperand call_ops[Register::kNumRegisters * 2];
  for (int i = 0; i < Register::kNumRegisters; ++i) {
    call_ops[i] = Reg(constants[i], i);
  }
  for (int i = 0; i < Register::kNumRegisters; ++i) {
    call_ops[i + Register::kNumRegisters] = Slot(constants[i], i);
  }
  EmitCall(Slot(-1), arraysize(call_ops), call_ops);
  EndBlock(Last());

  Allocate();
}

TEST_F(RegisterAllocatorTest, SplitBeforeInstruction) {
  const int kNumRegs = 6;
  SetNumRegs(kNumRegs, kNumRegs);

  StartBlock();

  // Stack parameters/spilled values.
  auto p_0 = Define(Slot(-1));
  auto p_1 = Define(Slot(-2));

  // Fill registers.
  VReg values[kNumRegs];
  for (size_t i = 0; i < arraysize(values); ++i) {
    values[i] = Define(Reg(static_cast<int>(i)));
  }

  // values[0] will be split in the second half of this instruction.
  // Models Intel mod instructions.
  EmitOI(Reg(0), Reg(p_0, 1), UniqueReg(p_1));
  EmitI(Reg(values[0], 0));
  EndBlock(Last());

  Allocate();
}

TEST_F(RegisterAllocatorTest, SplitBeforeInstruction2) {
  const int kNumRegs = 6;
  SetNumRegs(kNumRegs, kNumRegs);

  StartBlock();

  // Stack parameters/spilled values.
  auto p_0 = Define(Slot(-1));
  auto p_1 = Define(Slot(-2));

  // Fill registers.
  VReg values[kNumRegs];
  for (size_t i = 0; i < arraysize(values); ++i) {
    values[i] = Define(Reg(static_cast<int>(i)));
  }

  // values[0] and [1] will be split in the second half of this instruction.
  EmitOOI(Reg(0), Reg(1), Reg(p_0, 0), Reg(p_1, 1));
  EmitI(Reg(values[0]), Reg(values[1]));
  EndBlock(Last());

  Allocate();
}

TEST_F(RegisterAllocatorTest, NestedDiamondPhiMerge) {
  // Outer diamond.
  StartBlock();
  EndBlock(Branch(Imm(), 1, 5));

  // Diamond 1
  StartBlock();
  EndBlock(Branch(Imm(), 1, 2));

  StartBlock();
  auto ll = Define(Reg());
  EndBlock(Jump(2));

  StartBlock();
  auto lr = Define(Reg());
  EndBlock();

  StartBlock();
  auto l_phi = Phi(ll, lr);
  EndBlock(Jump(5));

  // Diamond 2
  StartBlock();
  EndBlock(Branch(Imm(), 1, 2));

  StartBlock();
  auto rl = Define(Reg());
  EndBlock(Jump(2));

  StartBlock();
  auto rr = Define(Reg());
  EndBlock();

  StartBlock();
  auto r_phi = Phi(rl, rr);
  EndBlock();

  // Outer diamond merge.
  StartBlock();
  auto phi = Phi(l_phi, r_phi);
  Return(Reg(phi));
  EndBlock();

  Allocate();
}

TEST_F(RegisterAllocatorTest, NestedDiamondPhiMergeDifferent) {
  // Outer diamond.
  StartBlock();
  EndBlock(Branch(Imm(), 1, 5));

  // Diamond 1
  StartBlock();
  EndBlock(Branch(Imm(), 1, 2));

  StartBlock();
  auto ll = Define(Reg(0));
  EndBlock(Jump(2));

  StartBlock();
  auto lr = Define(Reg(1));
  EndBlock();

  StartBlock();
  auto l_phi = Phi(ll, lr);
  EndBlock(Jump(5));

  // Diamond 2
  StartBlock();
  EndBlock(Branch(Imm(), 1, 2));

  StartBlock();
  auto rl = Define(Reg(2));
  EndBlock(Jump(2));

  StartBlock();
  auto rr = Define(Reg(3));
  EndBlock();

  StartBlock();
  auto r_phi = Phi(rl, rr);
  EndBlock();

  // Outer diamond merge.
  StartBlock();
  auto phi = Phi(l_phi, r_phi);
  Return(Reg(phi));
  EndBlock();

  Allocate();
}

TEST_F(RegisterAllocatorTest, RegressionSplitBeforeAndMove) {
  StartBlock();

  // Fill registers.
  VReg values[Register::kNumRegisters];
  for (size_t i = 0; i < arraysize(values); ++i) {
    if (i == 0 || i == 1) continue;  // Leave a hole for c_1 to take.
    values[i] = Define(Reg(static_cast<int>(i)));
  }

  auto c_0 = DefineConstant();
  auto c_1 = DefineConstant();

  EmitOI(Reg(1), Reg(c_0, 0), UniqueReg(c_1));

  // Use previous values to force c_1 to split before the previous instruction.
  for (size_t i = 0; i < arraysize(values); ++i) {
    if (i == 0 || i == 1) continue;
    EmitI(Reg(values[i], static_cast<int>(i)));
  }

  EndBlock(Last());

  Allocate();
}

TEST_F(RegisterAllocatorTest, RegressionSpillTwice) {
  StartBlock();
  auto p_0 = Parameter(Reg(1));
  EmitCall(Slot(-2), Unique(p_0), Reg(p_0, 1));
  EndBlock(Last());

  Allocate();
}

TEST_F(RegisterAllocatorTest, RegressionLoadConstantBeforeSpill) {
  StartBlock();
  // Fill registers.
  VReg values[Register::kNumRegisters];
  for (size_t i = arraysize(values); i > 0; --i) {
    values[i - 1] = Define(Reg(static_cast<int>(i - 1)));
  }
  auto c = DefineConstant();
  auto to_spill = Define(Reg());
  EndBlock(Jump(1));

  {
    StartLoop(1);

    StartBlock();
    // Create a use for c in second half of prev block's last gap
    Phi(c);
    for (size_t i = arraysize(values); i > 0; --i) {
      Phi(values[i - 1]);
    }
    EndBlock(Jump(1));

    EndLoop();
  }

  StartBlock();
  // Force c to split within to_spill's definition.
  EmitI(Reg(c));
  EmitI(Reg(to_spill));
  EndBlock(Last());

  Allocate();
}

TEST_F(RegisterAllocatorTest, DiamondWithCallFirstBlock) {
  StartBlock();
  auto x = EmitOI(Reg(0));
  EndBlock(Branch(Reg(x), 1, 2));

  StartBlock();
  EmitCall(Slot(-1));
  auto occupy = EmitOI(Reg(0));
  EndBlock(Jump(2));

  StartBlock();
  EndBlock(FallThrough());

  StartBlock();
  Use(occupy);
  Return(Reg(x));
  EndBlock();
  Allocate();
}

TEST_F(RegisterAllocatorTest, DiamondWithCallSecondBlock) {
  StartBlock();
  auto x = EmitOI(Reg(0));
  EndBlock(Branch(Reg(x), 1, 2));

  StartBlock();
  EndBlock(Jump(2));

  StartBlock();
  EmitCall(Slot(-1));
  auto occupy = EmitOI(Reg(0));
  EndBlock(FallThrough());

  StartBlock();
  Use(occupy);
  Return(Reg(x));
  EndBlock();
  Allocate();
}

TEST_F(RegisterAllocatorTest, SingleDeferredBlockSpill) {
  StartBlock();  // B0
  auto var = EmitOI(Reg(0));
  EndBlock(Branch(Reg(var), 1, 2));

  StartBlock();  // B1
  EndBlock(Jump(2));

  StartBlock(true);  // B2
  EmitCall(Slot(-1), Slot(var));
  EndBlock();

  StartBlock();  // B3
  EmitNop();
  EndBlock();

  StartBlock();  // B4
  Return(Reg(var, 0));
  EndBlock();

  Allocate();

  const int var_def_index = 1;
  const int call_index = 3;

  // We should have no parallel moves at the "var_def_index" position.
  EXPECT_EQ(
      0, GetParallelMoveCount(var_def_index, Instruction::START, sequence()));

  // The spill should be performed at the position "call_index".
  EXPECT_TRUE(IsParallelMovePresent(call_index, Instruction::START, sequence(),
                                    Reg(0), Slot(0)));
}

TEST_F(RegisterAllocatorTest, ValidMultipleDeferredBlockSpills) {
  StartBlock();  // B0
  auto var1 = EmitOI(Reg(0));
  auto var2 = EmitOI(Reg(1));
  auto var3 = EmitOI(Reg(2));
  EndBlock(Branch(Reg(var1, 0), 1, 2));

  StartBlock(true);  // B1
  EmitCall(Slot(-2), Slot(var1));
  EndBlock(Jump(5));

  StartBlock();  // B2
  EmitNop();
  EndBlock();

  StartBlock();  // B3
  EmitNop();
  EndBlock(Branch(Reg(var2, 0), 1, 2));

  StartBlock(true);  // B4
  EmitCall(Slot(-1), Slot(var2));
  EndBlock(Jump(2));

  StartBlock();  // B5
  EmitNop();
  EndBlock();

  StartBlock();  // B6
  Return(Reg(var3, 2));
  EndBlock();

  const int def_of_v2 = 2;
  const int call_in_b1 = 4;
  const int call_in_b4 = 10;
  const int end_of_b1 = 5;
  const int end_of_b4 = 11;
  const int start_of_b6 = 14;

  Allocate();

  const int var3_reg = 2;
  const int var3_slot = 2;

  EXPECT_FALSE(IsParallelMovePresent(def_of_v2, Instruction::START, sequence(),
                                     Reg(var3_reg), Slot()));
  EXPECT_TRUE(IsParallelMovePresent(call_in_b1, Instruction::START, sequence(),
                                    Reg(var3_reg), Slot(var3_slot)));
  EXPECT_TRUE(IsParallelMovePresent(end_of_b1, Instruction::START, sequence(),
                                    Slot(var3_slot), Reg()));

  EXPECT_TRUE(IsParallelMovePresent(call_in_b4, Instruction::START, sequence(),
                                    Reg(var3_reg), Slot(var3_slot)));
  EXPECT_TRUE(IsParallelMovePresent(end_of_b4, Instruction::START, sequence(),
                                    Slot(var3_slot), Reg()));

  EXPECT_EQ(0,
            GetParallelMoveCount(start_of_b6, Instruction::START, sequence()));
}

namespace {

enum class ParameterType { kFixedSlot, kSlot, kRegister, kFixedRegister };

const ParameterType kParameterTypes[] = {
    ParameterType::kFixedSlot, ParameterType::kSlot, ParameterType::kRegister,
    ParameterType::kFixedRegister};

class SlotConstraintTest : public RegisterAllocatorTest,
                           public ::testing::WithParamInterface<
                               ::testing::tuple<ParameterType, int>> {
 public:
  static const int kMaxVariant = 5;

 protected:
  ParameterType parameter_type() const {
    return ::testing::get<0>(B::GetParam());
  }
  int variant() const { return ::testing::get<1>(B::GetParam()); }

 private:
  using B = ::testing::WithParamInterface<::testing::tuple<ParameterType, int>>;
};

}  // namespace

TEST_P(SlotConstraintTest, SlotConstraint) {
  StartBlock();
  VReg p_0;
  switch (parameter_type()) {
    case ParameterType::kFixedSlot:
      p_0 = Parameter(Slot(-1));
      break;
    case ParameterType::kSlot:
      p_0 = Parameter(Slot(-1));
      break;
    case ParameterType::kRegister:
      p_0 = Parameter(Reg());
      break;
    case ParameterType::kFixedRegister:
      p_0 = Parameter(Reg(1));
      break;
  }
  switch (variant()) {
    case 0:
      EmitI(Slot(p_0), Reg(p_0));
      break;
    case 1:
      EmitI(Slot(p_0));
      break;
    case 2:
      EmitI(Reg(p_0));
      EmitI(Slot(p_0));
      break;
    case 3:
      EmitI(Slot(p_0));
      EmitI(Reg(p_0));
      break;
    case 4:
      EmitI(Slot(p_0, -1), Slot(p_0), Reg(p_0), Reg(p_0, 1));
      break;
    default:
      UNREACHABLE();
  }
  EndBlock(Last());

  Allocate();
}

INSTANTIATE_TEST_SUITE_P(
    RegisterAllocatorTest, SlotConstraintTest,
    ::testing::Combine(::testing::ValuesIn(kParameterTypes),
                       ::testing::Range(0, SlotConstraintTest::kMaxVariant)));

}  // namespace
}  // namespace compiler
}  // namespace internal
}  // namespace v8
