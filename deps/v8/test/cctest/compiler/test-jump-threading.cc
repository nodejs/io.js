// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/source-position.h"
#include "src/compiler/backend/instruction-codes.h"
#include "src/compiler/backend/instruction.h"
#include "src/compiler/backend/jump-threading.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {
namespace compiler {

class TestCode : public HandleAndZoneScope {
 public:
  explicit TestCode(size_t block_count)
      : HandleAndZoneScope(),
        blocks_(main_zone()),
        sequence_(main_isolate(), main_zone(), &blocks_),
        rpo_number_(RpoNumber::FromInt(0)),
        current_(nullptr) {
    sequence_.IncreaseRpoForTesting(block_count);
  }

  ZoneVector<InstructionBlock*> blocks_;
  InstructionSequence sequence_;
  RpoNumber rpo_number_;
  InstructionBlock* current_;

  int Jump(int target) {
    Start();
    InstructionOperand ops[] = {UseRpo(target)};
    sequence_.AddInstruction(Instruction::New(main_zone(), kArchJmp, 0, nullptr,
                                              1, ops, 0, nullptr));
    int pos = static_cast<int>(sequence_.instructions().size() - 1);
    End();
    return pos;
  }
  int Branch(int ttarget, int ftarget) {
    Start();
    InstructionOperand ops[] = {UseRpo(ttarget), UseRpo(ftarget)};
    InstructionCode code = 119 | FlagsModeField::encode(kFlags_branch) |
                           FlagsConditionField::encode(kEqual);
    sequence_.AddInstruction(
        Instruction::New(main_zone(), code, 0, nullptr, 2, ops, 0, nullptr));
    int pos = static_cast<int>(sequence_.instructions().size() - 1);
    End();
    return pos;
  }
  int Return(int size, bool defer = false, bool deconstruct_frame = false) {
    Start(defer, deconstruct_frame);
    InstructionOperand ops[] = {Immediate(size)};
    sequence_.AddInstruction(Instruction::New(main_zone(), kArchRet, 0, nullptr,
                                              1, ops, 0, nullptr));
    int pos = static_cast<int>(sequence_.instructions().size() - 1);
    End();
    return pos;
  }
  void Nop() {
    Start();
    sequence_.AddInstruction(Instruction::New(main_zone(), kArchNop));
  }
  void RedundantMoves() {
    Start();
    sequence_.AddInstruction(Instruction::New(main_zone(), kArchNop));
    int index = static_cast<int>(sequence_.instructions().size()) - 1;
    AddGapMove(index, AllocatedOperand(LocationOperand::REGISTER,
                                       MachineRepresentation::kWord32, 13),
               AllocatedOperand(LocationOperand::REGISTER,
                                MachineRepresentation::kWord32, 13));
  }
  void NonRedundantMoves() {
    Start();
    sequence_.AddInstruction(Instruction::New(main_zone(), kArchNop));
    int index = static_cast<int>(sequence_.instructions().size()) - 1;
    AddGapMove(index, ConstantOperand(11),
               AllocatedOperand(LocationOperand::REGISTER,
                                MachineRepresentation::kWord32, 11));
  }
  int JumpWithGapMove(int target, int id = 10) {
    Start();
    InstructionOperand ops[] = {UseRpo(target)};
    sequence_.AddInstruction(Instruction::New(main_zone(), kArchJmp, 0, nullptr,
                                              1, ops, 0, nullptr));
    int index = static_cast<int>(sequence_.instructions().size()) - 1;
    InstructionOperand from = AllocatedOperand(
        LocationOperand::REGISTER, MachineRepresentation::kWord32, id);
    InstructionOperand to = AllocatedOperand(
        LocationOperand::REGISTER, MachineRepresentation::kWord32, id + 1);
    AddGapMove(index, from, to);
    End();
    return index;
  }

  void Other() {
    Start();
    sequence_.AddInstruction(Instruction::New(main_zone(), 155));
  }
  void End() {
    Start();
    int end = static_cast<int>(sequence_.instructions().size());
    if (current_->code_start() == end) {  // Empty block.  Insert a nop.
      sequence_.AddInstruction(Instruction::New(main_zone(), kArchNop));
    }
    sequence_.EndBlock(current_->rpo_number());
    current_ = nullptr;
    rpo_number_ = RpoNumber::FromInt(rpo_number_.ToInt() + 1);
  }
  InstructionOperand UseRpo(int num) {
    return sequence_.AddImmediate(Constant(RpoNumber::FromInt(num)));
  }
  InstructionOperand Immediate(int num) {
    return sequence_.AddImmediate(Constant(num));
  }
  void Start(bool deferred = false, bool deconstruct_frame = false) {
    if (current_ == nullptr) {
      current_ = main_zone()->New<InstructionBlock>(
          main_zone(), rpo_number_, RpoNumber::Invalid(), RpoNumber::Invalid(),
          RpoNumber::Invalid(), deferred, false);
      if (deconstruct_frame) {
        current_->mark_must_deconstruct_frame();
      }
      blocks_.push_back(current_);
      sequence_.StartBlock(rpo_number_);
    }
  }
  void Defer() {
    CHECK_NULL(current_);
    Start(true);
  }
  void AddGapMove(int index, const InstructionOperand& from,
                  const InstructionOperand& to) {
    sequence_.InstructionAt(index)
        ->GetOrCreateParallelMove(Instruction::START, main_zone())
        ->AddMove(from, to);
  }
};

void VerifyForwarding(TestCode* code, int count, int* expected) {
  v8::internal::AccountingAllocator allocator;
  Zone local_zone(&allocator, ZONE_NAME);
  ZoneVector<RpoNumber> result(&local_zone);
  JumpThreading::ComputeForwarding(&local_zone, &result, &code->sequence_,
                                   true);

  CHECK(count == static_cast<int>(result.size()));
  for (int i = 0; i < count; i++) {
    CHECK_EQ(expected[i], result[i].ToInt());
  }
}

TEST(FwEmpty1) {
  constexpr size_t kBlockCount = 3;
  TestCode code(kBlockCount);

  // B0
  code.Jump(1);
  // B1
  code.Jump(2);
  // B2
  code.End();

  static int expected[] = {2, 2, 2};
  VerifyForwarding(&code, kBlockCount, expected);
}


TEST(FwEmptyN) {
  constexpr size_t kBlockCount = 3;
  for (int i = 0; i < 9; i++) {
    TestCode code(kBlockCount);

    // B0
    code.Jump(1);
    // B1
    for (int j = 0; j < i; j++) code.Nop();
    code.Jump(2);
    // B2
    code.End();

    static int expected[] = {2, 2, 2};
    VerifyForwarding(&code, kBlockCount, expected);
  }
}


TEST(FwNone1) {
  constexpr size_t kBlockCount = 1;
  TestCode code(kBlockCount);

  // B0
  code.End();

  static int expected[] = {0};
  VerifyForwarding(&code, kBlockCount, expected);
}


TEST(FwMoves1) {
  constexpr size_t kBlockCount = 1;
  TestCode code(kBlockCount);

  // B0
  code.RedundantMoves();
  code.End();

  static int expected[] = {0};
  VerifyForwarding(&code, kBlockCount, expected);
}


TEST(FwMoves2) {
  constexpr size_t kBlockCount = 2;
  TestCode code(kBlockCount);

  // B0
  code.RedundantMoves();
  code.Jump(1);
  // B1
  code.End();

  static int expected[] = {1, 1};
  VerifyForwarding(&code, kBlockCount, expected);
}


TEST(FwMoves2b) {
  constexpr size_t kBlockCount = 2;
  TestCode code(kBlockCount);

  // B0
  code.NonRedundantMoves();
  code.Jump(1);
  // B1
  code.End();

  static int expected[] = {0, 1};
  VerifyForwarding(&code, kBlockCount, expected);
}

TEST(FwMoves3a) {
  constexpr size_t kBlockCount = 4;
  TestCode code(kBlockCount);

  // B0
  code.JumpWithGapMove(3, 10);
  // B1 (merge B1 into B0, because they have the same gap moves.)
  code.JumpWithGapMove(3, 10);
  // B2 (can not merge B2 into B0, because they have different gap moves.)
  code.JumpWithGapMove(3, 11);
  // B3
  code.End();

  static int expected[] = {0, 0, 2, 3};
  VerifyForwarding(&code, kBlockCount, expected);
}

TEST(FwMoves3b) {
  constexpr size_t kBlockCount = 7;
  TestCode code(kBlockCount);

  // B0
  code.JumpWithGapMove(6);
  // B1
  code.Jump(2);
  // B2
  code.Jump(3);
  // B3
  code.JumpWithGapMove(6);
  // B4
  code.Jump(3);
  // B5
  code.Jump(2);
  // B6
  code.End();

  static int expected[] = {0, 0, 0, 0, 0, 0, 6};
  VerifyForwarding(&code, kBlockCount, expected);
}

TEST(FwOther2) {
  constexpr size_t kBlockCount = 2;
  TestCode code(kBlockCount);

  // B0
  code.Other();
  code.Jump(1);
  // B1
  code.End();

  static int expected[] = {0, 1};
  VerifyForwarding(&code, kBlockCount, expected);
}


TEST(FwNone2a) {
  constexpr size_t kBlockCount = 2;
  TestCode code(kBlockCount);

  // B0
  code.Jump(1);
  // B1
  code.End();

  static int expected[] = {1, 1};
  VerifyForwarding(&code, kBlockCount, expected);
}


TEST(FwNone2b) {
  constexpr size_t kBlockCount = 2;
  TestCode code(kBlockCount);

  // B0
  code.Jump(1);
  // B1
  code.End();

  static int expected[] = {1, 1};
  VerifyForwarding(&code, kBlockCount, expected);
}


TEST(FwLoop1) {
  constexpr size_t kBlockCount = 1;
  TestCode code(kBlockCount);

  // B0
  code.Jump(0);

  static int expected[] = {0};
  VerifyForwarding(&code, kBlockCount, expected);
}


TEST(FwLoop2) {
  constexpr size_t kBlockCount = 2;
  TestCode code(kBlockCount);

  // B0
  code.Jump(1);
  // B1
  code.Jump(0);

  static int expected[] = {0, 0};
  VerifyForwarding(&code, kBlockCount, expected);
}


TEST(FwLoop3) {
  constexpr size_t kBlockCount = 3;
  TestCode code(kBlockCount);

  // B0
  code.Jump(1);
  // B1
  code.Jump(2);
  // B2
  code.Jump(0);

  static int expected[] = {0, 0, 0};
  VerifyForwarding(&code, kBlockCount, expected);
}


TEST(FwLoop1b) {
  constexpr size_t kBlockCount = 2;
  TestCode code(kBlockCount);

  // B0
  code.Jump(1);
  // B1
  code.Jump(1);

  static int expected[] = {1, 1};
  VerifyForwarding(&code, 2, expected);
}


TEST(FwLoop2b) {
  constexpr size_t kBlockCount = 3;
  TestCode code(kBlockCount);

  // B0
  code.Jump(1);
  // B1
  code.Jump(2);
  // B2
  code.Jump(1);

  static int expected[] = {1, 1, 1};
  VerifyForwarding(&code, kBlockCount, expected);
}


TEST(FwLoop3b) {
  constexpr size_t kBlockCount = 4;
  TestCode code(kBlockCount);

  // B0
  code.Jump(1);
  // B1
  code.Jump(2);
  // B2
  code.Jump(3);
  // B3
  code.Jump(1);

  static int expected[] = {1, 1, 1, 1};
  VerifyForwarding(&code, kBlockCount, expected);
}


TEST(FwLoop2_1a) {
  constexpr size_t kBlockCount = 5;
  TestCode code(kBlockCount);

  // B0
  code.Jump(1);
  // B1
  code.Jump(2);
  // B2
  code.Jump(3);
  // B3
  code.Jump(1);
  // B4
  code.Jump(2);

  static int expected[] = {1, 1, 1, 1, 1};
  VerifyForwarding(&code, kBlockCount, expected);
}


TEST(FwLoop2_1b) {
  constexpr size_t kBlockCount = 5;
  TestCode code(kBlockCount);

  // B0
  code.Jump(1);
  // B1
  code.Jump(2);
  // B2
  code.Jump(4);
  // B3
  code.Jump(1);
  // B4
  code.Jump(2);

  static int expected[] = {2, 2, 2, 2, 2};
  VerifyForwarding(&code, kBlockCount, expected);
}


TEST(FwLoop2_1c) {
  constexpr size_t kBlockCount = 5;
  TestCode code(kBlockCount);

  // B0
  code.Jump(1);
  // B1
  code.Jump(2);
  // B2
  code.Jump(4);
  // B3
  code.Jump(2);
  // B4
  code.Jump(1);

  static int expected[] = {1, 1, 1, 1, 1};
  VerifyForwarding(&code, kBlockCount, expected);
}


TEST(FwLoop2_1d) {
  constexpr size_t kBlockCount = 5;
  TestCode code(kBlockCount);

  // B0
  code.Jump(1);
  // B1
  code.Jump(2);
  // B2
  code.Jump(1);
  // B3
  code.Jump(1);
  // B4
  code.Jump(1);

  static int expected[] = {1, 1, 1, 1, 1};
  VerifyForwarding(&code, kBlockCount, expected);
}


TEST(FwLoop3_1a) {
  constexpr size_t kBlockCount = 6;
  TestCode code(kBlockCount);

  // B0
  code.Jump(1);
  // B1
  code.Jump(2);
  // B2
  code.Jump(3);
  // B3
  code.Jump(2);
  // B4
  code.Jump(1);
  // B5
  code.Jump(0);

  static int expected[] = {2, 2, 2, 2, 2, 2};
  VerifyForwarding(&code, kBlockCount, expected);
}

TEST(FwLoop4a) {
  constexpr size_t kBlockCount = 2;
  TestCode code(kBlockCount);

  // B0
  code.JumpWithGapMove(1);
  // B1
  code.JumpWithGapMove(0);

  static int expected[] = {0, 1};
  VerifyForwarding(&code, kBlockCount, expected);
}

TEST(FwLoop4b) {
  constexpr size_t kBlockCount = 4;
  TestCode code(kBlockCount);

  // B0
  code.Jump(3);
  // B1
  code.JumpWithGapMove(2);
  // B2
  code.Jump(0);
  // B3
  code.JumpWithGapMove(2);

  static int expected[] = {3, 3, 3, 3};
  VerifyForwarding(&code, kBlockCount, expected);
}

TEST(FwDiamonds) {
  constexpr size_t kBlockCount = 4;
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      TestCode code(kBlockCount);

      // B0
      code.Branch(1, 2);
      // B1
      if (i) code.Other();
      code.Jump(3);
      // B2
      if (j) code.Other();
      code.Jump(3);
      // B3
      code.End();

      int expected[] = {0, i ? 1 : 3, j ? 2 : 3, 3};
      VerifyForwarding(&code, kBlockCount, expected);
    }
  }
}


TEST(FwDiamonds2) {
  constexpr size_t kBlockCount = 5;
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      for (int k = 0; k < 2; k++) {
        TestCode code(kBlockCount);
        // B0
        code.Branch(1, 2);
        // B1
        if (i) code.Other();
        code.Jump(3);
        // B2
        if (j) code.Other();
        code.Jump(3);
        // B3
        if (k) code.NonRedundantMoves();
        code.Jump(4);
        // B4
        code.End();

        int merge = k ? 3 : 4;
        int expected[] = {0, i ? 1 : merge, j ? 2 : merge, merge, 4};
        VerifyForwarding(&code, kBlockCount, expected);
      }
    }
  }
}


TEST(FwDoubleDiamonds) {
  constexpr size_t kBlockCount = 7;
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      for (int x = 0; x < 2; x++) {
        for (int y = 0; y < 2; y++) {
          TestCode code(kBlockCount);
          // B0
          code.Branch(1, 2);
          // B1
          if (i) code.Other();
          code.Jump(3);
          // B2
          if (j) code.Other();
          code.Jump(3);
          // B3
          code.Branch(4, 5);
          // B4
          if (x) code.Other();
          code.Jump(6);
          // B5
          if (y) code.Other();
          code.Jump(6);
          // B6
          code.End();

          int expected[] = {0,         i ? 1 : 3, j ? 2 : 3, 3,
                            x ? 4 : 6, y ? 5 : 6, 6};
          VerifyForwarding(&code, kBlockCount, expected);
        }
      }
    }
  }
}

template <int kSize>
void RunPermutationsRecursive(int outer[kSize], int start,
                              void (*run)(int*, int)) {
  int permutation[kSize];

  for (int i = 0; i < kSize; i++) permutation[i] = outer[i];

  int count = kSize - start;
  if (count == 0) return run(permutation, kSize);
  for (int i = start; i < kSize; i++) {
    permutation[start] = outer[i];
    permutation[i] = outer[start];
    RunPermutationsRecursive<kSize>(permutation, start + 1, run);
    permutation[i] = outer[i];
    permutation[start] = outer[start];
  }
}


template <int kSize>
void RunAllPermutations(void (*run)(int*, int)) {
  int permutation[kSize];
  for (int i = 0; i < kSize; i++) permutation[i] = i;
  RunPermutationsRecursive<kSize>(permutation, 0, run);
}


void PrintPermutation(int* permutation, int size) {
  printf("{ ");
  for (int i = 0; i < size; i++) {
    if (i > 0) printf(", ");
    printf("%d", permutation[i]);
  }
  printf(" }\n");
}


int find(int x, int* permutation, int size) {
  for (int i = 0; i < size; i++) {
    if (permutation[i] == x) return i;
  }
  return size;
}


void RunPermutedChain(int* permutation, int size) {
  const int kBlockCount = size + 2;
  TestCode code(kBlockCount);
  int cur = -1;
  for (int i = 0; i < size; i++) {
    code.Jump(find(cur + 1, permutation, size) + 1);
    cur = permutation[i];
  }
  code.Jump(find(cur + 1, permutation, size) + 1);
  code.End();

  int expected[] = {size + 1, size + 1, size + 1, size + 1,
                    size + 1, size + 1, size + 1};
  VerifyForwarding(&code, kBlockCount, expected);
}


TEST(FwPermuted_chain) {
  RunAllPermutations<3>(RunPermutedChain);
  RunAllPermutations<4>(RunPermutedChain);
  RunAllPermutations<5>(RunPermutedChain);
}


void RunPermutedDiamond(int* permutation, int size) {
  constexpr size_t kBlockCount = 6;
  TestCode code(kBlockCount);
  int br = 1 + find(0, permutation, size);
  code.Jump(br);
  for (int i = 0; i < size; i++) {
    switch (permutation[i]) {
      case 0:
        code.Branch(1 + find(1, permutation, size),
                    1 + find(2, permutation, size));
        break;
      case 1:
        code.Jump(1 + find(3, permutation, size));
        break;
      case 2:
        code.Jump(1 + find(3, permutation, size));
        break;
      case 3:
        code.Jump(5);
        break;
    }
  }
  code.End();

  int expected[] = {br, 5, 5, 5, 5, 5};
  expected[br] = br;
  VerifyForwarding(&code, kBlockCount, expected);
}


TEST(FwPermuted_diamond) { RunAllPermutations<4>(RunPermutedDiamond); }

void ApplyForwarding(TestCode* code, int size, int* forward) {
  code->sequence_.RecomputeAssemblyOrderForTesting();
  ZoneVector<RpoNumber> vector(code->main_zone());
  for (int i = 0; i < size; i++) {
    vector.push_back(RpoNumber::FromInt(forward[i]));
  }
  JumpThreading::ApplyForwarding(code->main_zone(), vector, &code->sequence_);
}

void CheckJump(TestCode* code, int pos, int target) {
  Instruction* instr = code->sequence_.InstructionAt(pos);
  CHECK_EQ(kArchJmp, instr->arch_opcode());
  CHECK_EQ(1, static_cast<int>(instr->InputCount()));
  CHECK_EQ(0, static_cast<int>(instr->OutputCount()));
  CHECK_EQ(0, static_cast<int>(instr->TempCount()));
  CHECK_EQ(target, code->sequence_.InputRpo(instr, 0).ToInt());
}

void CheckRet(TestCode* code, int pos) {
  Instruction* instr = code->sequence_.InstructionAt(pos);
  CHECK_EQ(kArchRet, instr->arch_opcode());
  CHECK_EQ(1, static_cast<int>(instr->InputCount()));
  CHECK_EQ(0, static_cast<int>(instr->OutputCount()));
  CHECK_EQ(0, static_cast<int>(instr->TempCount()));
}

void CheckNop(TestCode* code, int pos) {
  Instruction* instr = code->sequence_.InstructionAt(pos);
  CHECK_EQ(kArchNop, instr->arch_opcode());
  CHECK_EQ(0, static_cast<int>(instr->InputCount()));
  CHECK_EQ(0, static_cast<int>(instr->OutputCount()));
  CHECK_EQ(0, static_cast<int>(instr->TempCount()));
}

void CheckBranch(TestCode* code, int pos, int t1, int t2) {
  Instruction* instr = code->sequence_.InstructionAt(pos);
  CHECK_EQ(2, static_cast<int>(instr->InputCount()));
  CHECK_EQ(0, static_cast<int>(instr->OutputCount()));
  CHECK_EQ(0, static_cast<int>(instr->TempCount()));
  CHECK_EQ(t1, code->sequence_.InputRpo(instr, 0).ToInt());
  CHECK_EQ(t2, code->sequence_.InputRpo(instr, 1).ToInt());
}

void CheckAssemblyOrder(TestCode* code, int size, int* expected) {
  int i = 0;
  for (auto const block : code->sequence_.instruction_blocks()) {
    CHECK_EQ(expected[i++], block->ao_number().ToInt());
  }
}

TEST(Rewire1) {
  constexpr size_t kBlockCount = 3;
  TestCode code(kBlockCount);

  // B0
  int j1 = code.Jump(1);
  // B1
  int j2 = code.Jump(2);
  // B2
  code.End();

  static int forward[] = {2, 2, 2};
  ApplyForwarding(&code, kBlockCount, forward);
  CheckJump(&code, j1, 2);
  CheckNop(&code, j2);

  static int assembly[] = {0, 1, 1};
  CheckAssemblyOrder(&code, kBlockCount, assembly);
}


TEST(Rewire1_deferred) {
  constexpr size_t kBlockCount = 4;
  TestCode code(kBlockCount);

  // B0
  int j1 = code.Jump(1);
  // B1
  int j2 = code.Jump(2);
  // B2
  code.Defer();
  int j3 = code.Jump(3);
  // B3
  code.Return(0);

  static int forward[] = {3, 3, 3, 3};
  ApplyForwarding(&code, kBlockCount, forward);
  CheckJump(&code, j1, 3);
  CheckNop(&code, j2);
  CheckNop(&code, j3);

  static int assembly[] = {0, 1, 2, 1};
  CheckAssemblyOrder(&code, kBlockCount, assembly);
}


TEST(Rewire2_deferred) {
  constexpr size_t kBlockCount = 4;
  TestCode code(kBlockCount);

  // B0
  code.Other();
  int j1 = code.Jump(1);
  // B1
  code.Defer();
  code.Jump(2);
  // B2
  code.Defer();
  int j2 = code.Jump(3);
  // B3
  code.End();

  static int forward[] = {0, 1, 2, 3};
  ApplyForwarding(&code, kBlockCount, forward);
  CheckJump(&code, j1, 1);
  CheckJump(&code, j2, 3);

  static int assembly[] = {0, 2, 3, 1};
  CheckAssemblyOrder(&code, kBlockCount, assembly);
}

TEST(Rewire_deferred_diamond) {
  constexpr size_t kBlockCount = 4;
  TestCode code(kBlockCount);

  // B0
  int b1 = code.Branch(1, 2);
  // B1
  code.Jump(3);
  // B2
  code.Defer();
  int j1 = code.Jump(3);
  // B3
  code.Return(0);

  static int forward[] = {0, 3, 3, 3};
  VerifyForwarding(&code, kBlockCount, forward);
  ApplyForwarding(&code, kBlockCount, forward);
  CheckBranch(&code, b1, 3, 3);
  CheckNop(&code, j1);

  static int assembly[] = {0, 1, 2, 1};
  CheckAssemblyOrder(&code, kBlockCount, assembly);
}

TEST(Rewire_diamond) {
  constexpr size_t kBlockCount = 5;
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      TestCode code(kBlockCount);
      // B0
      int j1 = code.Jump(1);
      // B1
      int b1 = code.Branch(2, 3);
      // B2
      int j2 = code.Jump(4);
      // B3
      int j3 = code.Jump(4);
      // B5
      code.End();

      int forward[] = {0, 1, i ? 4 : 2, j ? 4 : 3, 4};
      ApplyForwarding(&code, kBlockCount, forward);
      CheckJump(&code, j1, 1);
      CheckBranch(&code, b1, i ? 4 : 2, j ? 4 : 3);
      if (i) {
        CheckNop(&code, j2);
      } else {
        CheckJump(&code, j2, 4);
      }
      if (j) {
        CheckNop(&code, j3);
      } else {
        CheckJump(&code, j3, 4);
      }

      int assembly[] = {0, 1, 2, 3, 4};
      if (i) {
        for (int k = 3; k < 5; k++) assembly[k]--;
      }
      if (j) {
        for (int k = 4; k < 5; k++) assembly[k]--;
      }
      CheckAssemblyOrder(&code, kBlockCount, assembly);
    }
  }
}

TEST(RewireRet) {
  constexpr size_t kBlockCount = 4;
  TestCode code(kBlockCount);

  // B0
  code.Branch(1, 2);
  // B1
  int j1 = code.Return(0);
  // B2
  int j2 = code.Return(0);
  // B3
  code.End();

  int forward[] = {0, 1, 1, 3};
  VerifyForwarding(&code, 4, forward);
  ApplyForwarding(&code, 4, forward);

  CheckRet(&code, j1);
  CheckNop(&code, j2);
}

TEST(RewireRet1) {
  constexpr size_t kBlockCount = 4;
  TestCode code(kBlockCount);

  // B0
  code.Branch(1, 2);
  // B1
  int j1 = code.Return(0);
  // B2
  int j2 = code.Return(0, true, true);
  // B3
  code.End();

  int forward[] = {0, 1, 2, 3};
  VerifyForwarding(&code, kBlockCount, forward);
  ApplyForwarding(&code, kBlockCount, forward);

  CheckRet(&code, j1);
  CheckRet(&code, j2);
}

TEST(RewireRet2) {
  constexpr size_t kBlockCount = 4;
  TestCode code(kBlockCount);

  // B0
  code.Branch(1, 2);
  // B1
  int j1 = code.Return(0, true, true);
  // B2
  int j2 = code.Return(0, true, true);
  // B3
  code.End();

  int forward[] = {0, 1, 1, 3};
  VerifyForwarding(&code, kBlockCount, forward);
  ApplyForwarding(&code, kBlockCount, forward);

  CheckRet(&code, j1);
  CheckNop(&code, j2);
}

TEST(DifferentSizeRet) {
  constexpr size_t kBlockCount = 4;
  TestCode code(kBlockCount);

  // B0
  code.Branch(1, 2);
  // B1
  int j1 = code.Return(0);
  // B2
  int j2 = code.Return(1);
  // B3
  code.End();

  int forward[] = {0, 1, 2, 3};
  VerifyForwarding(&code, kBlockCount, forward);
  ApplyForwarding(&code, kBlockCount, forward);

  CheckRet(&code, j1);
  CheckRet(&code, j2);
}

TEST(RewireGapJump1) {
  constexpr size_t kBlockCount = 4;
  TestCode code(kBlockCount);

  // B0
  int j1 = code.JumpWithGapMove(3);
  // B1
  int j2 = code.JumpWithGapMove(3);
  // B2
  int j3 = code.JumpWithGapMove(3);
  // B3
  code.End();

  int forward[] = {0, 0, 0, 3};
  VerifyForwarding(&code, kBlockCount, forward);
  ApplyForwarding(&code, kBlockCount, forward);
  CheckJump(&code, j1, 3);
  CheckNop(&code, j2);
  CheckNop(&code, j3);

  static int assembly[] = {0, 1, 1, 1};
  CheckAssemblyOrder(&code, kBlockCount, assembly);
}

TEST(RewireGapJump2) {
  constexpr size_t kBlockCount = 6;
  TestCode code(kBlockCount);

  // B0
  int j1 = code.JumpWithGapMove(4);
  // B1
  int j2 = code.JumpWithGapMove(4);
  // B2
  code.Other();
  int j3 = code.Jump(3);
  // B3
  int j4 = code.Jump(1);
  // B4
  int j5 = code.Jump(5);
  // B5
  code.End();

  int forward[] = {0, 0, 2, 0, 5, 5};
  VerifyForwarding(&code, kBlockCount, forward);
  ApplyForwarding(&code, kBlockCount, forward);
  CheckJump(&code, j1, 5);
  CheckNop(&code, j2);
  CheckJump(&code, j3, 0);
  CheckNop(&code, j4);
  CheckNop(&code, j5);

  static int assembly[] = {0, 1, 1, 2, 2, 2};
  CheckAssemblyOrder(&code, kBlockCount, assembly);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
