// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/backend/code-generator.h"
#include "src/compiler/backend/instruction.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/linkage.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node.h"
#include "src/compiler/operator.h"
#include "src/compiler/schedule.h"
#include "src/compiler/scheduler.h"
#include "src/compiler/turbofan-graph.h"
#include "src/objects/objects-inl.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {
namespace compiler {

using TestInstr = v8::internal::compiler::Instruction;
using TestInstrSeq = v8::internal::compiler::InstructionSequence;

// A testing helper for the register code abstraction.
class InstructionTester : public HandleAndZoneScope {
 public:  // We're all friends here.
  InstructionTester()
      : HandleAndZoneScope(kCompressGraphZone),
        graph(zone()),
        schedule(zone()),
        common(zone()),
        machine(zone()),
        code(nullptr) {}

  Graph graph;
  Schedule schedule;
  CommonOperatorBuilder common;
  MachineOperatorBuilder machine;
  TestInstrSeq* code;

  Zone* zone() { return main_zone(); }

  void allocCode() {
    if (schedule.rpo_order()->size() == 0) {
      // Compute the RPO order.
      Scheduler::ComputeSpecialRPO(main_zone(), &schedule);
      CHECK_NE(0u, schedule.rpo_order()->size());
    }
    InstructionBlocks* instruction_blocks =
        TestInstrSeq::InstructionBlocksFor(main_zone(), &schedule);
    code = main_zone()->New<TestInstrSeq>(main_isolate(), main_zone(),
                                          instruction_blocks);
  }

  Node* Int32Constant(int32_t val) {
    Node* node = graph.NewNode(common.Int32Constant(val));
    schedule.AddNode(schedule.start(), node);
    return node;
  }

  Node* Float64Constant(double val) {
    Node* node = graph.NewNode(common.Float64Constant(val));
    schedule.AddNode(schedule.start(), node);
    return node;
  }

  Node* Parameter(int32_t which) {
    Node* node = graph.NewNode(common.Parameter(which));
    schedule.AddNode(schedule.start(), node);
    return node;
  }

  Node* NewNode(BasicBlock* block) {
    Node* node = graph.NewNode(common.Int32Constant(111));
    schedule.AddNode(block, node);
    return node;
  }

  int NewInstr() {
    InstructionCode opcode = static_cast<InstructionCode>(110);
    TestInstr* instr = TestInstr::New(zone(), opcode);
    return code->AddInstruction(instr);
  }

  int NewNop() {
    TestInstr* instr = TestInstr::New(zone(), kArchNop);
    return code->AddInstruction(instr);
  }

  UnallocatedOperand Unallocated(int vreg) {
    return UnallocatedOperand(UnallocatedOperand::REGISTER_OR_SLOT, vreg);
  }

  RpoNumber RpoFor(BasicBlock* block) {
    return RpoNumber::FromInt(block->rpo_number());
  }

  InstructionBlock* BlockAt(BasicBlock* block) {
    return code->InstructionBlockAt(RpoFor(block));
  }
  BasicBlock* GetBasicBlock(int instruction_index) {
    const InstructionBlock* block =
        code->GetInstructionBlock(instruction_index);
    return schedule.rpo_order()->at(block->rpo_number().ToSize());
  }
  int first_instruction_index(BasicBlock* block) {
    return BlockAt(block)->first_instruction_index();
  }
  int last_instruction_index(BasicBlock* block) {
    return BlockAt(block)->last_instruction_index();
  }
};


TEST(InstructionBasic) {
  InstructionTester R;

  for (int i = 0; i < 10; i++) {
    R.Int32Constant(i);  // Add some nodes to the graph.
  }

  BasicBlock* last = R.schedule.start();
  for (int i = 0; i < 5; i++) {
    BasicBlock* block = R.schedule.NewBasicBlock();
    R.schedule.AddGoto(last, block);
    last = block;
  }

  R.allocCode();

  BasicBlockVector* blocks = R.schedule.rpo_order();
  CHECK_EQ(static_cast<int>(blocks->size()), R.code->InstructionBlockCount());

  for (auto block : *blocks) {
    CHECK_EQ(block->rpo_number(), R.BlockAt(block)->rpo_number().ToInt());
    CHECK(!block->loop_end());
  }
}


TEST(InstructionGetBasicBlock) {
  InstructionTester R;

  BasicBlock* b0 = R.schedule.start();
  BasicBlock* b1 = R.schedule.NewBasicBlock();
  BasicBlock* b2 = R.schedule.NewBasicBlock();
  BasicBlock* b3 = R.schedule.end();

  R.schedule.AddGoto(b0, b1);
  R.schedule.AddGoto(b1, b2);
  R.schedule.AddGoto(b2, b3);

  R.allocCode();

  R.code->StartBlock(R.RpoFor(b0));
  int i0 = R.NewInstr();
  int i1 = R.NewInstr();
  R.code->EndBlock(R.RpoFor(b0));
  R.code->StartBlock(R.RpoFor(b1));
  int i2 = R.NewInstr();
  int i3 = R.NewInstr();
  int i4 = R.NewInstr();
  int i5 = R.NewInstr();
  R.code->EndBlock(R.RpoFor(b1));
  R.code->StartBlock(R.RpoFor(b2));
  int i6 = R.NewInstr();
  int i7 = R.NewInstr();
  int i8 = R.NewInstr();
  R.code->EndBlock(R.RpoFor(b2));
  R.code->StartBlock(R.RpoFor(b3));
  R.NewNop();
  R.code->EndBlock(R.RpoFor(b3));

  CHECK_EQ(b0, R.GetBasicBlock(i0));
  CHECK_EQ(b0, R.GetBasicBlock(i1));

  CHECK_EQ(b1, R.GetBasicBlock(i2));
  CHECK_EQ(b1, R.GetBasicBlock(i3));
  CHECK_EQ(b1, R.GetBasicBlock(i4));
  CHECK_EQ(b1, R.GetBasicBlock(i5));

  CHECK_EQ(b2, R.GetBasicBlock(i6));
  CHECK_EQ(b2, R.GetBasicBlock(i7));
  CHECK_EQ(b2, R.GetBasicBlock(i8));

  CHECK_EQ(b0, R.GetBasicBlock(R.first_instruction_index(b0)));
  CHECK_EQ(b0, R.GetBasicBlock(R.last_instruction_index(b0)));

  CHECK_EQ(b1, R.GetBasicBlock(R.first_instruction_index(b1)));
  CHECK_EQ(b1, R.GetBasicBlock(R.last_instruction_index(b1)));

  CHECK_EQ(b2, R.GetBasicBlock(R.first_instruction_index(b2)));
  CHECK_EQ(b2, R.GetBasicBlock(R.last_instruction_index(b2)));

  CHECK_EQ(b3, R.GetBasicBlock(R.first_instruction_index(b3)));
  CHECK_EQ(b3, R.GetBasicBlock(R.last_instruction_index(b3)));
}


TEST(InstructionIsGapAt) {
  InstructionTester R;

  BasicBlock* b0 = R.schedule.start();
  R.schedule.AddReturn(b0, R.Int32Constant(1));

  R.allocCode();
  TestInstr* i0 = TestInstr::New(R.zone(), 100);
  TestInstr* g = TestInstr::New(R.zone(), 103);
  R.code->StartBlock(R.RpoFor(b0));
  R.code->AddInstruction(i0);
  R.code->AddInstruction(g);
  R.code->EndBlock(R.RpoFor(b0));

  CHECK_EQ(2, R.code->instructions().size());
}


TEST(InstructionIsGapAt2) {
  InstructionTester R;

  BasicBlock* b0 = R.schedule.start();
  BasicBlock* b1 = R.schedule.end();
  R.schedule.AddGoto(b0, b1);
  R.schedule.AddReturn(b1, R.Int32Constant(1));

  R.allocCode();
  TestInstr* i0 = TestInstr::New(R.zone(), 100);
  TestInstr* g = TestInstr::New(R.zone(), 103);
  R.code->StartBlock(R.RpoFor(b0));
  R.code->AddInstruction(i0);
  R.code->AddInstruction(g);
  R.code->EndBlock(R.RpoFor(b0));

  TestInstr* i1 = TestInstr::New(R.zone(), 102);
  TestInstr* g1 = TestInstr::New(R.zone(), 104);
  R.code->StartBlock(R.RpoFor(b1));
  R.code->AddInstruction(i1);
  R.code->AddInstruction(g1);
  R.code->EndBlock(R.RpoFor(b1));

  CHECK_EQ(4, R.code->instructions().size());
}


TEST(InstructionAddGapMove) {
  InstructionTester R;

  BasicBlock* b0 = R.schedule.start();
  R.schedule.AddReturn(b0, R.Int32Constant(1));

  R.allocCode();
  TestInstr* i0 = TestInstr::New(R.zone(), 100);
  TestInstr* g = TestInstr::New(R.zone(), 103);
  R.code->StartBlock(R.RpoFor(b0));
  R.code->AddInstruction(i0);
  R.code->AddInstruction(g);
  R.code->EndBlock(R.RpoFor(b0));

  CHECK_EQ(2, R.code->instructions().size());

  int index = 0;
  for (auto instr : R.code->instructions()) {
    UnallocatedOperand op1 = R.Unallocated(index++);
    UnallocatedOperand op2 = R.Unallocated(index++);
    instr->GetOrCreateParallelMove(TestInstr::START, R.zone())
        ->AddMove(op1, op2);
    ParallelMove* move = instr->GetParallelMove(TestInstr::START);
    CHECK(move);
    CHECK_EQ(1u, move->size());
    MoveOperands* cur = move->at(0);
    CHECK(op1.Equals(cur->source()));
    CHECK(op2.Equals(cur->destination()));
  }
}


TEST(InstructionOperands) {
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);

  {
    TestInstr* i = TestInstr::New(&zone, 101);
    CHECK_EQ(0, static_cast<int>(i->OutputCount()));
    CHECK_EQ(0, static_cast<int>(i->InputCount()));
    CHECK_EQ(0, static_cast<int>(i->TempCount()));
  }

  int vreg = 15;
  InstructionOperand outputs[] = {
      UnallocatedOperand(UnallocatedOperand::MUST_HAVE_REGISTER, vreg),
      UnallocatedOperand(UnallocatedOperand::MUST_HAVE_REGISTER, vreg),
      UnallocatedOperand(UnallocatedOperand::MUST_HAVE_REGISTER, vreg),
      UnallocatedOperand(UnallocatedOperand::MUST_HAVE_REGISTER, vreg)};

  InstructionOperand inputs[] = {
      UnallocatedOperand(UnallocatedOperand::MUST_HAVE_REGISTER, vreg),
      UnallocatedOperand(UnallocatedOperand::MUST_HAVE_REGISTER, vreg),
      UnallocatedOperand(UnallocatedOperand::MUST_HAVE_REGISTER, vreg),
      UnallocatedOperand(UnallocatedOperand::MUST_HAVE_REGISTER, vreg)};

  InstructionOperand temps[] = {
      UnallocatedOperand(UnallocatedOperand::MUST_HAVE_REGISTER, vreg),
      UnallocatedOperand(UnallocatedOperand::MUST_HAVE_REGISTER, vreg),
      UnallocatedOperand(UnallocatedOperand::MUST_HAVE_REGISTER, vreg),
      UnallocatedOperand(UnallocatedOperand::MUST_HAVE_REGISTER, vreg)};

  for (size_t i = 0; i < arraysize(outputs); i++) {
    for (size_t j = 0; j < arraysize(inputs); j++) {
      for (size_t k = 0; k < arraysize(temps); k++) {
        TestInstr* m =
            TestInstr::New(&zone, 101, i, outputs, j, inputs, k, temps);
        CHECK(i == m->OutputCount());
        CHECK(j == m->InputCount());
        CHECK(k == m->TempCount());

        for (size_t z = 0; z < i; z++) {
          CHECK(outputs[z].Equals(*m->OutputAt(z)));
        }

        for (size_t z = 0; z < j; z++) {
          CHECK(inputs[z].Equals(*m->InputAt(z)));
        }

        for (size_t z = 0; z < k; z++) {
          CHECK(temps[z].Equals(*m->TempAt(z)));
        }
      }
    }
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
