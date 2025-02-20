// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/vector.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/branch-elimination-reducer.h"
#include "src/compiler/turboshaft/copying-phase.h"
#include "src/compiler/turboshaft/dead-code-elimination-reducer.h"
#include "src/compiler/turboshaft/loop-peeling-reducer.h"
#include "src/compiler/turboshaft/machine-optimization-reducer.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/compiler/turboshaft/required-optimization-reducer.h"
#include "src/compiler/turboshaft/variable-reducer.h"
#include "test/unittests/compiler/turboshaft/reducer-test.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

class ControlFlowTest : public ReducerTest {};

// This test creates a chain of empty blocks linked by Gotos. CopyingPhase
// should automatically inline them, leading to the graph containing a single
// block after a single CopyingPhase.
TEST_F(ControlFlowTest, DefaultBlockInlining) {
  auto test = CreateFromGraph(1, [](auto& Asm) {
    OpIndex cond = Asm.GetParameter(0);
    for (int i = 0; i < 10000; i++) {
      Label<> l(&Asm);
      GOTO(l);
      BIND(l);
    }
    __ Return(cond);
  });

  test.Run<>();

  ASSERT_EQ(test.graph().block_count(), 1u);
}

// This test creates a fairly large graph, where a pattern similar to this is
// repeating:
//
//       B1        B2
//         \      /
//          \    /
//            Phi
//          Branch(Phi)
//          /     \
//         /       \
//        B3        B4
//
// BranchElimination should remove such branches by cloning the block with the
// branch. In the end, the graph should contain (almost) no branches anymore.
TEST_F(ControlFlowTest, BranchElimination) {
  static constexpr int kSize = 10000;

  auto test = CreateFromGraph(1, [](auto& Asm) {
    V<Word32> cond =
        __ TaggedEqual(Asm.GetParameter(0), __ SmiConstant(Smi::FromInt(0)));

    Block* end = __ NewBlock();
    V<Word32> cst1 = __ Word32Constant(42);
    std::vector<Block*> destinations;
    for (int i = 0; i < kSize; i++) destinations.push_back(__ NewBlock());
    ZoneVector<SwitchOp::Case>* cases =
        Asm.graph().graph_zone()->template New<ZoneVector<SwitchOp::Case>>(
            Asm.graph().graph_zone());
    for (int i = 0; i < kSize; i++) {
      cases->push_back({i, destinations[i], BranchHint::kNone});
    }
    __ Switch(cond, base::VectorOf(*cases), end);

    __ Bind(destinations[0]);
    Block* b = __ NewBlock();
    __ Branch(cond, b, end);
    __ Bind(b);

    for (int i = 1; i < kSize; i++) {
      V<Word32> cst2 = __ Word32Constant(1);
      __ Goto(destinations[i]);
      __ Bind(destinations[i]);
      V<Word32> phi = __ Phi({cst1, cst2}, RegisterRepresentation::Word32());
      Block* b1 = __ NewBlock();
      __ Branch(phi, b1, end);
      __ Bind(b1);
    }
    __ Goto(end);
    __ Bind(end);

    __ Return(cond);
  });

  // BranchElimination should remove all branches (except the first one), but
  // will not inline the destinations right away.
  test.Run<BranchEliminationReducer, MachineOptimizationReducer>();

  ASSERT_EQ(test.CountOp(Opcode::kBranch), 1u);

  // An empty phase will then inline the empty intermediate blocks.
  test.Run<>();

  // The graph should now contain 2 blocks per case (1 edge-split + 1 merge),
  // and a few blocks before and after (the switch and the return for
  // instance). To make this test a bit future proof, we just check that the
  // number of block is "number of cases * 2 + a few more blocks" rather than
  // computing the exact expected number of blocks.
  static constexpr int kMaxOtherBlocksCount = 10;
  ASSERT_LE(test.graph().block_count(),
            static_cast<size_t>(kSize * 2 + kMaxOtherBlocksCount));
}

// When the block following a loop header has a single predecessor and contains
// Phis with a single input, loop peeling should be careful not to think that
// these phis are loop phis.
TEST_F(ControlFlowTest, LoopPeelingSingleInputPhi) {
  auto test = CreateFromGraph(1, [](auto& Asm) {
    Block* loop = __ NewLoopHeader();
    Block *loop_body = __ NewBlock(), *outside = __ NewBlock();
    __ Goto(loop);
    __ Bind(loop);
    V<Word32> cst = __ Word32Constant(42);
    __ Goto(loop_body);
    __ Bind(loop_body);
    V<Word32> phi = __ Phi({cst}, RegisterRepresentation::Word32());
    __ GotoIf(phi, outside);
    __ Goto(loop);
    __ Bind(outside);
    __ Return(__ Word32Constant(17));
  });

  test.Run<LoopPeelingReducer>();
}

// This test checks that DeadCodeElimination (DCE) eliminates dead blocks
// regardless or whether they are reached through a Goto or a Branch.
TEST_F(ControlFlowTest, DCEGoto) {
  auto test = CreateFromGraph(1, [](auto& Asm) {
    // This whole graph only contains unused operations (except for the final
    // Return).
    Block *b1 = __ NewBlock(), *b2 = __ NewBlock(), *b3 = __ NewBlock(),
          *b4 = __ NewBlock();
    __ Bind(b1);
    __ Word32Constant(71);
    __ Goto(b4);
    __ Bind(b4);
    OpIndex cond = Asm.GetParameter(0);
    IF (cond) {
      __ Word32Constant(47);
      __ Goto(b2);
      __ Bind(b2);
      __ Word32Constant(53);
    } ELSE {
      __ Word32Constant(19);
    }
    __ Word32Constant(42);
    __ Goto(b3);
    __ Bind(b3);
    __ Return(__ Word32Constant(17));
  });

  test.Run<DeadCodeEliminationReducer>();

  // The final graph should contain at most 2 blocks (we currently don't
  // eliminate the initial empty block, so we end up with 2 blocks rather than
  // 1; a subsequent optimization phase would remove the empty 1st block).
  ASSERT_LE(test.graph().block_count(), static_cast<size_t>(2));
}

TEST_F(ControlFlowTest, LoopVar) {
  auto test = CreateFromGraph(1, [](auto& Asm) {
    OpIndex p = Asm.GetParameter(0);
    Variable v1 = __ NewVariable(RegisterRepresentation::Tagged());
    Variable v2 = __ NewVariable(RegisterRepresentation::Tagged());
    __ SetVariable(v1, p);
    __ SetVariable(v2, p);
    LoopLabel<Word32> loop(&Asm);
    Label<Word32> end(&Asm);
    GOTO(loop, 0);

    BIND_LOOP(loop, iter) {
      GOTO_IF(__ Word32Equal(iter, 42), end, 15);

      __ SetVariable(v1, __ SmiConstant(Smi::FromInt(17)));

      GOTO(loop, __ Word32Add(iter, 1));
    }

    BIND(end, ret);
    OpIndex t = __ Word32Mul(ret, __ GetVariable(v1));
    __ Return(__ Word32BitwiseAnd(t, __ GetVariable(v2)));
  });

  ASSERT_EQ(0u, test.CountOp(Opcode::kPendingLoopPhi));
}

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft
