// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BACKEND_INSTRUCTION_SCHEDULER_H_
#define V8_COMPILER_BACKEND_INSTRUCTION_SCHEDULER_H_

#include <optional>

#include "src/base/utils/random-number-generator.h"
#include "src/compiler/backend/instruction.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {

// A set of flags describing properties of the instructions so that the
// scheduler is aware of dependencies between instructions.
enum ArchOpcodeFlags {
  kNoOpcodeFlags = 0,
  kHasSideEffect = 1,    // The instruction has some side effects (memory
                         // store, function call...)
  kIsLoadOperation = 2,  // The instruction is a memory load.
  kMayNeedDeoptOrTrapCheck = 4,  // The instruction may be associated with a
                                 // deopt or trap check which must be run before
                                 // instruction e.g. div on Intel platform which
                                 // will raise an exception when the divisor is
                                 // zero.
  kIsBarrier = 8,  // The instruction can cause GC or it reads/writes registers
                   // that are not explicitly given. Nothing can be reordered
                   // across such an instruction.
};

class InstructionScheduler final : public ZoneObject {
 public:
  V8_EXPORT_PRIVATE InstructionScheduler(Zone* zone,
                                         InstructionSequence* sequence);

  V8_EXPORT_PRIVATE void StartBlock(RpoNumber rpo);
  V8_EXPORT_PRIVATE void EndBlock(RpoNumber rpo);

  V8_EXPORT_PRIVATE void AddInstruction(Instruction* instr);
  V8_EXPORT_PRIVATE void AddTerminator(Instruction* instr);

  static bool SchedulerSupported();

 private:
  // A scheduling graph node.
  // Represent an instruction and their dependencies.
  class ScheduleGraphNode : public ZoneObject {
   public:
    ScheduleGraphNode(Zone* zone, Instruction* instr);

    // Mark the instruction represented by 'node' as a dependency of this one.
    // The current instruction will be registered as an unscheduled predecessor
    // of 'node' (i.e. it must be scheduled before 'node').
    void AddSuccessor(ScheduleGraphNode* node);

    // Check if all the predecessors of this instruction have been scheduled.
    bool HasUnscheduledPredecessor() {
      return unscheduled_predecessors_count_ != 0;
    }

    // Record that we have scheduled one of the predecessors of this node.
    void DropUnscheduledPredecessor() {
      DCHECK_LT(0, unscheduled_predecessors_count_);
      unscheduled_predecessors_count_--;
    }

    Instruction* instruction() { return instr_; }
    ZoneDeque<ScheduleGraphNode*>& successors() { return successors_; }
    int latency() const { return latency_; }

    int total_latency() const { return total_latency_; }
    void set_total_latency(int latency) { total_latency_ = latency; }

    int start_cycle() const { return start_cycle_; }
    void set_start_cycle(int start_cycle) { start_cycle_ = start_cycle; }

   private:
    Instruction* instr_;
    ZoneDeque<ScheduleGraphNode*> successors_;

    // Number of unscheduled predecessors for this node.
    int unscheduled_predecessors_count_;

    // Estimate of the instruction latency (the number of cycles it takes for
    // instruction to complete).
    int latency_;

    // The sum of all the latencies on the path from this node to the end of
    // the graph (i.e. a node with no successor).
    int total_latency_;

    // The scheduler keeps a nominal cycle count to keep track of when the
    // result of an instruction is available. This field is updated by the
    // scheduler to indicate when the value of all the operands of this
    // instruction will be available.
    int start_cycle_;
  };

  // Keep track of all nodes ready to be scheduled (i.e. all their dependencies
  // have been scheduled. Note that this class is inteded to be extended by
  // concrete implementation of the scheduling queue which define the policy
  // to pop node from the queue.
  class SchedulingQueueBase {
   public:
    explicit SchedulingQueueBase(InstructionScheduler* scheduler)
        : scheduler_(scheduler), nodes_(scheduler->zone()) {}

    void AddNode(ScheduleGraphNode* node);

    bool IsEmpty() const { return nodes_.empty(); }

   protected:
    InstructionScheduler* scheduler_;
    ZoneLinkedList<ScheduleGraphNode*> nodes_;
  };

  // A scheduling queue which prioritize nodes on the critical path (we look
  // for the instruction with the highest latency on the path to reach the end
  // of the graph).
  class CriticalPathFirstQueue : public SchedulingQueueBase {
   public:
    explicit CriticalPathFirstQueue(InstructionScheduler* scheduler)
        : SchedulingQueueBase(scheduler) {}

    // Look for the best candidate to schedule, remove it from the queue and
    // return it.
    ScheduleGraphNode* PopBestCandidate(int cycle);
  };

  // A queue which pop a random node from the queue to perform stress tests on
  // the scheduler.
  class StressSchedulerQueue : public SchedulingQueueBase {
   public:
    explicit StressSchedulerQueue(InstructionScheduler* scheduler)
        : SchedulingQueueBase(scheduler) {}

    ScheduleGraphNode* PopBestCandidate(int cycle);

   private:
    base::RandomNumberGenerator* random_number_generator() {
      return scheduler_->random_number_generator();
    }
  };

  // Perform scheduling for the current block specifying the queue type to
  // use to determine the next best candidate.
  template <typename QueueType>
  void Schedule();

  // Return the scheduling properties of the given instruction.
  V8_EXPORT_PRIVATE int GetInstructionFlags(const Instruction* instr) const;
  int GetTargetInstructionFlags(const Instruction* instr) const;

  bool IsBarrier(const Instruction* instr) const {
    return (GetInstructionFlags(instr) & kIsBarrier) != 0;
  }

  // Check whether the given instruction has side effects (e.g. function call,
  // memory store).
  bool HasSideEffect(const Instruction* instr) const {
    return (GetInstructionFlags(instr) & kHasSideEffect) != 0;
  }

  // Return true if the instruction is a memory load.
  bool IsLoadOperation(const Instruction* instr) const {
    return (GetInstructionFlags(instr) & kIsLoadOperation) != 0;
  }

  bool CanTrap(const Instruction* instr) const {
    return instr->IsTrap() ||
           (instr->HasMemoryAccessMode() &&
            instr->memory_access_mode() != kMemoryAccessDirect);
  }

  // The scheduler will not move the following instructions before the last
  // deopt/trap check:
  //  * loads (this is conservative)
  //  * instructions with side effect
  //  * other deopts/traps
  // Any other instruction can be moved, apart from those that raise exceptions
  // on specific inputs - these are filtered out by the deopt/trap check.
  bool MayNeedDeoptOrTrapCheck(const Instruction* instr) const {
    return (GetInstructionFlags(instr) & kMayNeedDeoptOrTrapCheck) != 0;
  }

  // Return true if the instruction cannot be moved before the last deopt or
  // trap point we encountered.
  bool DependsOnDeoptOrTrap(const Instruction* instr) const {
    return MayNeedDeoptOrTrapCheck(instr) || instr->IsDeoptimizeCall() ||
           CanTrap(instr) || HasSideEffect(instr) || IsLoadOperation(instr);
  }

  // Identify nops used as a definition point for live-in registers at
  // function entry.
  bool IsFixedRegisterParameter(const Instruction* instr) const {
    return (instr->arch_opcode() == kArchNop) && (instr->OutputCount() == 1) &&
           (instr->OutputAt(0)->IsUnallocated()) &&
           (UnallocatedOperand::cast(instr->OutputAt(0))
                ->HasFixedRegisterPolicy() ||
            UnallocatedOperand::cast(instr->OutputAt(0))
                ->HasFixedFPRegisterPolicy());
  }

  void ComputeTotalLatencies();

  static int GetInstructionLatency(const Instruction* instr);

  Zone* zone() { return zone_; }
  InstructionSequence* sequence() { return sequence_; }
  base::RandomNumberGenerator* random_number_generator() {
    return &random_number_generator_.value();
  }

  Zone* zone_;
  InstructionSequence* sequence_;
  ZoneVector<ScheduleGraphNode*> graph_;

  friend class InstructionSchedulerTester;

  // Last side effect instruction encountered while building the graph.
  ScheduleGraphNode* last_side_effect_instr_;

  // Set of load instructions encountered since the last side effect instruction
  // which will be added as predecessors of the next instruction with side
  // effects.
  ZoneVector<ScheduleGraphNode*> pending_loads_;

  // Live-in register markers are nop instructions which are emitted at the
  // beginning of a basic block so that the register allocator will find a
  // defining instruction for live-in values. They must not be moved.
  // All these nops are chained together and added as a predecessor of every
  // other instructions in the basic block.
  ScheduleGraphNode* last_live_in_reg_marker_;

  // Last deoptimization or trap instruction encountered while building the
  // graph.
  ScheduleGraphNode* last_deopt_or_trap_;

  // Keep track of definition points for virtual registers. This is used to
  // record operand dependencies in the scheduling graph.
  ZoneMap<int32_t, ScheduleGraphNode*> operands_map_;

  std::optional<base::RandomNumberGenerator> random_number_generator_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_BACKEND_INSTRUCTION_SCHEDULER_H_
