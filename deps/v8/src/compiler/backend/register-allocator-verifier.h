// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BACKEND_REGISTER_ALLOCATOR_VERIFIER_H_
#define V8_COMPILER_BACKEND_REGISTER_ALLOCATOR_VERIFIER_H_

#include "src/compiler/backend/instruction.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {

class InstructionBlock;
class InstructionSequence;

// The register allocator validator traverses instructions in the instruction
// sequence, and verifies the correctness of machine operand substitutions of
// virtual registers. It collects the virtual register instruction signatures
// before register allocation. Then, after the register allocation pipeline
// completes, it compares the operand substitutions against the pre-allocation
// data.
// At a high level, validation works as follows: we iterate through each block,
// and, in a block, through each instruction; then:
// - when an operand is the output of an instruction, we associate it to the
// virtual register that the instruction sequence declares as its output. We
// use the concept of "FinalAssessment" to model this.
// - when an operand is used in an instruction, we check that the assessment
// matches the expectation of the instruction
// - moves simply copy the assessment over to the new operand
// - blocks with more than one predecessor associate to each operand a "Pending"
// assessment. The pending assessment remembers the operand and block where it
// was created. Then, when the value is used (which may be as a different
// operand, because of moves), we check that the virtual register at the use
// site matches the definition of this pending operand: either the phi inputs
// match, or, if it's not a phi, all the predecessors at the point the pending
// assessment was defined have that operand assigned to the given virtual
// register. If all checks out, we record in the assessment that the virtual
// register is aliased by the specific operand.
// If a block is a loop header - so one or more of its predecessors are it or
// below - we still treat uses of operands as above, but we record which operand
// assessments haven't been made yet, and what virtual register they must
// correspond to, and verify that when we are done with the respective
// predecessor blocks.
// This way, the algorithm always makes a final decision about the operands
// in an instruction, ensuring convergence.
// Operand assessments are recorded per block, as the result at the exit from
// the block. When moving to a new block, we copy assessments from its single
// predecessor, or, if the block has multiple predecessors, the mechanism was
// described already.

enum AssessmentKind { Final, Pending };

class Assessment : public ZoneObject {
 public:
  Assessment(const Assessment&) = delete;
  Assessment& operator=(const Assessment&) = delete;

  AssessmentKind kind() const { return kind_; }

 protected:
  explicit Assessment(AssessmentKind kind) : kind_(kind) {}
  AssessmentKind kind_;
};

// PendingAssessments are associated to operands coming from the multiple
// predecessors of a block. We only record the operand and the block, and
// will determine if the way the operand is defined (from the predecessors)
// matches a particular use. We allow more than one vreg association with
// an operand - this handles scenarios where multiple phis are
// defined with identical operands, and the move optimizer moved down the moves
// separating the 2 phis in the block defining them.
class PendingAssessment final : public Assessment {
 public:
  explicit PendingAssessment(Zone* zone, const InstructionBlock* origin,
                             InstructionOperand operand)
      : Assessment(Pending),
        origin_(origin),
        operand_(operand),
        aliases_(zone) {}

  PendingAssessment(const PendingAssessment&) = delete;
  PendingAssessment& operator=(const PendingAssessment&) = delete;

  static const PendingAssessment* cast(const Assessment* assessment) {
    CHECK(assessment->kind() == Pending);
    return static_cast<const PendingAssessment*>(assessment);
  }

  static PendingAssessment* cast(Assessment* assessment) {
    CHECK(assessment->kind() == Pending);
    return static_cast<PendingAssessment*>(assessment);
  }

  const InstructionBlock* origin() const { return origin_; }
  InstructionOperand operand() const { return operand_; }
  bool IsAliasOf(int vreg) const { return aliases_.count(vreg) > 0; }
  void AddAlias(int vreg) { aliases_.insert(vreg); }

 private:
  const InstructionBlock* const origin_;
  InstructionOperand operand_;
  ZoneSet<int> aliases_;
};

// FinalAssessments are associated to operands that we know to be a certain
// virtual register.
class FinalAssessment final : public Assessment {
 public:
  explicit FinalAssessment(int virtual_register)
      : Assessment(Final), virtual_register_(virtual_register) {}
  FinalAssessment(const FinalAssessment&) = delete;
  FinalAssessment& operator=(const FinalAssessment&) = delete;

  int virtual_register() const { return virtual_register_; }
  static const FinalAssessment* cast(const Assessment* assessment) {
    CHECK(assessment->kind() == Final);
    return static_cast<const FinalAssessment*>(assessment);
  }

 private:
  int virtual_register_;
};

struct OperandAsKeyLess {
  bool operator()(const InstructionOperand& a,
                  const InstructionOperand& b) const {
    return a.CompareCanonicalized(b);
  }
};

// Assessments associated with a basic block.
class BlockAssessments : public ZoneObject {
 public:
  using OperandMap = ZoneMap<InstructionOperand, Assessment*, OperandAsKeyLess>;
  using OperandSet = ZoneSet<InstructionOperand, OperandAsKeyLess>;
  explicit BlockAssessments(Zone* zone, int spill_slot_delta,
                            const InstructionSequence* sequence)
      : map_(zone),
        map_for_moves_(zone),
        stale_ref_stack_slots_(zone),
        spill_slot_delta_(spill_slot_delta),
        zone_(zone),
        sequence_(sequence) {}
  BlockAssessments(const BlockAssessments&) = delete;
  BlockAssessments& operator=(const BlockAssessments&) = delete;

  void Drop(InstructionOperand operand) {
    map_.erase(operand);
    stale_ref_stack_slots_.erase(operand);
  }
  void DropRegisters();
  void AddDefinition(InstructionOperand operand, int virtual_register) {
    auto existent = map_.find(operand);
    if (existent != map_.end()) {
      // Drop the assignment
      map_.erase(existent);
      // Destination operand is no longer a stale reference.
      stale_ref_stack_slots_.erase(operand);
    }
    map_.insert(
        std::make_pair(operand, zone_->New<FinalAssessment>(virtual_register)));
  }

  void PerformMoves(const Instruction* instruction);
  void PerformParallelMoves(const ParallelMove* moves);
  void CopyFrom(const BlockAssessments* other) {
    CHECK(map_.empty());
    CHECK(stale_ref_stack_slots_.empty());
    CHECK_NOT_NULL(other);
    map_.insert(other->map_.begin(), other->map_.end());
    stale_ref_stack_slots_.insert(other->stale_ref_stack_slots_.begin(),
                                  other->stale_ref_stack_slots_.end());
  }
  void CheckReferenceMap(const ReferenceMap* reference_map);
  bool IsStaleReferenceStackSlot(InstructionOperand op,
                                 base::Optional<int> vreg = base::nullopt);

  OperandMap& map() { return map_; }
  const OperandMap& map() const { return map_; }

  OperandSet& stale_ref_stack_slots() { return stale_ref_stack_slots_; }
  const OperandSet& stale_ref_stack_slots() const {
    return stale_ref_stack_slots_;
  }

  int spill_slot_delta() const { return spill_slot_delta_; }

  void Print() const;

 private:
  OperandMap map_;
  OperandMap map_for_moves_;
  // TODOC(dmercadier): how do stack slots become stale exactly? What are the
  // implications of a stack slot being stale?
  OperandSet stale_ref_stack_slots_;
  int spill_slot_delta_;
  Zone* zone_;
  const InstructionSequence* sequence_;
};

class RegisterAllocatorVerifier final : public ZoneObject {
 public:
  RegisterAllocatorVerifier(Zone* zone, const RegisterConfiguration* config,
                            const InstructionSequence* sequence,
                            const Frame* frame);
  RegisterAllocatorVerifier(const RegisterAllocatorVerifier&) = delete;
  RegisterAllocatorVerifier& operator=(const RegisterAllocatorVerifier&) =
      delete;

  void VerifyAssignment(const char* caller_info);
  void VerifyGapMoves();

 private:
  enum ConstraintType {
    kConstant,
    kImmediate,
    kRegister,
    kFixedRegister,
    kFPRegister,
    kFixedFPRegister,
    kSlot,
    kFixedSlot,
    kRegisterOrSlot,
    kRegisterOrSlotFP,
    kRegisterOrSlotOrConstant,
    kSameAsInput,
    kRegisterAndSlot
  };

  struct OperandConstraint {
    ConstraintType type_;
    // Constant or immediate value, register code, slot index, or slot size
    // when relevant.
    int value_;
    int spilled_slot_;
    int virtual_register_;
  };

  struct InstructionConstraint {
    const Instruction* instruction_;
    size_t operand_constaints_size_;
    OperandConstraint* operand_constraints_;
  };

  using Constraints = ZoneVector<InstructionConstraint>;

  class DelayedAssessments : public ZoneObject {
   public:
    explicit DelayedAssessments(Zone* zone) : map_(zone) {}

    const ZoneMap<InstructionOperand, int, OperandAsKeyLess>& map() const {
      return map_;
    }

    void AddDelayedAssessment(InstructionOperand op, int vreg) {
      auto it = map_.find(op);
      if (it == map_.end()) {
        map_.insert(std::make_pair(op, vreg));
      } else {
        CHECK_EQ(it->second, vreg);
      }
    }

   private:
    ZoneMap<InstructionOperand, int, OperandAsKeyLess> map_;
  };

  Zone* zone() const { return zone_; }
  const RegisterConfiguration* config() { return config_; }
  const InstructionSequence* sequence() const { return sequence_; }
  Constraints* constraints() { return &constraints_; }
  int spill_slot_delta() const { return spill_slot_delta_; }

  static void VerifyInput(const OperandConstraint& constraint);
  static void VerifyTemp(const OperandConstraint& constraint);
  static void VerifyOutput(const OperandConstraint& constraint);

  void BuildConstraint(const InstructionOperand* op,
                       OperandConstraint* constraint);
  void CheckConstraint(const InstructionOperand* op,
                       const OperandConstraint* constraint);
  BlockAssessments* CreateForBlock(const InstructionBlock* block);

  // Prove that this operand is an alias of this virtual register in the given
  // block. Update the assessment if that's the case.
  void ValidatePendingAssessment(RpoNumber block_id, InstructionOperand op,
                                 const BlockAssessments* current_assessments,
                                 PendingAssessment* const assessment,
                                 int virtual_register);
  void ValidateUse(RpoNumber block_id, BlockAssessments* current_assessments,
                   InstructionOperand op, int virtual_register);

  Zone* const zone_;
  const RegisterConfiguration* config_;
  const InstructionSequence* const sequence_;
  Constraints constraints_;
  ZoneMap<RpoNumber, BlockAssessments*> assessments_;
  ZoneMap<RpoNumber, DelayedAssessments*> outstanding_assessments_;
  int spill_slot_delta_;
  // TODO(chromium:725559): remove after we understand this bug's root cause.
  const char* caller_info_ = nullptr;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_BACKEND_REGISTER_ALLOCATOR_VERIFIER_H_
