// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BACKEND_INSTRUCTION_SELECTOR_H_
#define V8_COMPILER_BACKEND_INSTRUCTION_SELECTOR_H_

#include <map>
#include <optional>

#include "src/codegen/cpu-features.h"
#include "src/codegen/machine-type.h"
#include "src/compiler/backend/instruction-scheduler.h"
#include "src/compiler/backend/instruction-selector-adapter.h"
#include "src/compiler/backend/instruction.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/feedback-source.h"
#include "src/compiler/linkage.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/compiler/turboshaft/utils.h"
#include "src/utils/bit-vector.h"
#include "src/zone/zone-containers.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/simd-shuffle.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

class TickCounter;

namespace compiler {

// Forward declarations.
class BasicBlock;
template <typename Adapter>
struct CallBufferT;  // TODO(bmeurer): Remove this.
template <typename Adapter>
class InstructionSelectorT;
class Linkage;
template <typename Adapter>
class OperandGeneratorT;
template <typename Adapter>
class SwitchInfoT;
template <typename Adapter>
struct CaseInfoT;
class TurbofanStateObjectDeduplicator;
class TurboshaftStateObjectDeduplicator;

class V8_EXPORT_PRIVATE InstructionSelector final {
 public:
  enum SourcePositionMode { kCallSourcePositions, kAllSourcePositions };
  enum EnableScheduling { kDisableScheduling, kEnableScheduling };
  enum EnableRootsRelativeAddressing {
    kDisableRootsRelativeAddressing,
    kEnableRootsRelativeAddressing
  };
  enum EnableSwitchJumpTable {
    kDisableSwitchJumpTable,
    kEnableSwitchJumpTable
  };
  enum EnableTraceTurboJson { kDisableTraceTurboJson, kEnableTraceTurboJson };

  class Features final {
   public:
    Features() : bits_(0) {}
    explicit Features(unsigned bits) : bits_(bits) {}
    explicit Features(CpuFeature f) : bits_(1u << f) {}
    Features(CpuFeature f1, CpuFeature f2) : bits_((1u << f1) | (1u << f2)) {}

    bool Contains(CpuFeature f) const { return (bits_ & (1u << f)); }

   private:
    unsigned bits_;
  };

  static InstructionSelector ForTurbofan(
      Zone* zone, size_t node_count, Linkage* linkage,
      InstructionSequence* sequence, Schedule* schedule,
      SourcePositionTable* source_positions, Frame* frame,
      EnableSwitchJumpTable enable_switch_jump_table, TickCounter* tick_counter,
      JSHeapBroker* broker, size_t* max_unoptimized_frame_height,
      size_t* max_pushed_argument_count,
      SourcePositionMode source_position_mode = kCallSourcePositions,
      Features features = SupportedFeatures(),
      EnableScheduling enable_scheduling = v8_flags.turbo_instruction_scheduling
                                               ? kEnableScheduling
                                               : kDisableScheduling,
      EnableRootsRelativeAddressing enable_roots_relative_addressing =
          kDisableRootsRelativeAddressing,
      EnableTraceTurboJson trace_turbo = kDisableTraceTurboJson);

  static InstructionSelector ForTurboshaft(
      Zone* zone, size_t node_count, Linkage* linkage,
      InstructionSequence* sequence, turboshaft::Graph* schedule, Frame* frame,
      EnableSwitchJumpTable enable_switch_jump_table, TickCounter* tick_counter,
      JSHeapBroker* broker, size_t* max_unoptimized_frame_height,
      size_t* max_pushed_argument_count,
      SourcePositionMode source_position_mode = kCallSourcePositions,
      Features features = SupportedFeatures(),
      EnableScheduling enable_scheduling = v8_flags.turbo_instruction_scheduling
                                               ? kEnableScheduling
                                               : kDisableScheduling,
      EnableRootsRelativeAddressing enable_roots_relative_addressing =
          kDisableRootsRelativeAddressing,
      EnableTraceTurboJson trace_turbo = kDisableTraceTurboJson);

  ~InstructionSelector();

  std::optional<BailoutReason> SelectInstructions();

  bool IsSupported(CpuFeature feature) const;

  // Returns the features supported on the target platform.
  static Features SupportedFeatures() {
    return Features(CpuFeatures::SupportedFeatures());
  }

  const ZoneVector<std::pair<int, int>>& instr_origins() const;
  const std::map<NodeId, int> GetVirtualRegistersForTesting() const;

  static MachineOperatorBuilder::Flags SupportedMachineOperatorFlags();
  static MachineOperatorBuilder::AlignmentRequirements AlignmentRequirements();

 private:
  InstructionSelector(InstructionSelectorT<TurbofanAdapter>* turbofan_impl,
                      InstructionSelectorT<TurboshaftAdapter>* turboshaft_impl);
  InstructionSelector(const InstructionSelector&) = delete;
  InstructionSelector& operator=(const InstructionSelector&) = delete;

  InstructionSelectorT<TurbofanAdapter>* turbofan_impl_;
  InstructionSelectorT<TurboshaftAdapter>* turboshaft_impl_;
};

// The flags continuation is a way to combine a branch or a materialization
// of a boolean value with an instruction that sets the flags register.
// The whole instruction is treated as a unit by the register allocator, and
// thus no spills or moves can be introduced between the flags-setting
// instruction and the branch or set it should be combined with.
template <typename Adapter>
class FlagsContinuationT final {
 public:
  using block_t = typename Adapter::block_t;
  using node_t = typename Adapter::node_t;
  using id_t = typename Adapter::id_t;

  struct ConditionalCompare {
    InstructionCode code;
    FlagsCondition compare_condition;
    FlagsCondition default_flags;
    node_t lhs;
    node_t rhs;
  };
  // This limit covered almost all the opportunities when compiling the debug
  // builtins.
  static constexpr size_t kMaxCompareChainSize = 4;
  using compare_chain_t = std::array<ConditionalCompare, kMaxCompareChainSize>;

  FlagsContinuationT() : mode_(kFlags_none) {}

  // Creates a new flags continuation from the given condition and true/false
  // blocks.
  static FlagsContinuationT ForBranch(FlagsCondition condition,
                                      block_t true_block, block_t false_block) {
    return FlagsContinuationT(kFlags_branch, condition, true_block,
                              false_block);
  }

  // Creates a new flags continuation from the given conditional compare chain
  // and true/false blocks.
  static FlagsContinuationT ForConditionalBranch(
      compare_chain_t& compares, uint32_t num_conditional_compares,
      FlagsCondition branch_condition, block_t true_block,
      block_t false_block) {
    return FlagsContinuationT(compares, num_conditional_compares,
                              branch_condition, true_block, false_block);
  }

  // Creates a new flags continuation for an eager deoptimization exit.
  static FlagsContinuationT ForDeoptimize(FlagsCondition condition,
                                          DeoptimizeReason reason, id_t node_id,
                                          FeedbackSource const& feedback,
                                          node_t frame_state) {
    return FlagsContinuationT(kFlags_deoptimize, condition, reason, node_id,
                              feedback, frame_state);
  }
  static FlagsContinuationT ForDeoptimizeForTesting(
      FlagsCondition condition, DeoptimizeReason reason, id_t node_id,
      FeedbackSource const& feedback, node_t frame_state) {
    // test-instruction-scheduler.cc passes a dummy Node* as frame_state.
    // Contents don't matter as long as it's not nullptr.
    return FlagsContinuationT(kFlags_deoptimize, condition, reason, node_id,
                              feedback, frame_state);
  }

  // Creates a new flags continuation for a boolean value.
  static FlagsContinuationT ForSet(FlagsCondition condition, node_t result) {
    return FlagsContinuationT(condition, result);
  }

  // Creates a new flags continuation for a conditional boolean value.
  static FlagsContinuationT ForConditionalSet(compare_chain_t& compares,
                                              uint32_t num_conditional_compares,
                                              FlagsCondition set_condition,
                                              node_t result) {
    return FlagsContinuationT(compares, num_conditional_compares, set_condition,
                              result);
  }

  // Creates a new flags continuation for a wasm trap.
  static FlagsContinuationT ForTrap(FlagsCondition condition, TrapId trap_id) {
    return FlagsContinuationT(condition, trap_id);
  }

  static FlagsContinuationT ForSelect(FlagsCondition condition, node_t result,
                                      node_t true_value, node_t false_value) {
    return FlagsContinuationT(condition, result, true_value, false_value);
  }

  bool IsNone() const { return mode_ == kFlags_none; }
  bool IsBranch() const { return mode_ == kFlags_branch; }
  bool IsConditionalBranch() const {
    return mode_ == kFlags_conditional_branch;
  }
  bool IsDeoptimize() const { return mode_ == kFlags_deoptimize; }
  bool IsSet() const { return mode_ == kFlags_set; }
  bool IsConditionalSet() const { return mode_ == kFlags_conditional_set; }
  bool IsTrap() const { return mode_ == kFlags_trap; }
  bool IsSelect() const { return mode_ == kFlags_select; }
  FlagsCondition condition() const {
    DCHECK(!IsNone());
    return condition_;
  }
  FlagsCondition final_condition() const {
    DCHECK(IsConditionalSet() || IsConditionalBranch());
    return final_condition_;
  }
  DeoptimizeReason reason() const {
    DCHECK(IsDeoptimize());
    return reason_;
  }
  id_t node_id() const {
    DCHECK(IsDeoptimize());
    return node_id_;
  }
  FeedbackSource const& feedback() const {
    DCHECK(IsDeoptimize());
    return feedback_;
  }
  node_t frame_state() const {
    DCHECK(IsDeoptimize());
    return frame_state_or_result_;
  }
  node_t result() const {
    DCHECK(IsSet() || IsConditionalSet() || IsSelect());
    return frame_state_or_result_;
  }
  TrapId trap_id() const {
    DCHECK(IsTrap());
    return trap_id_;
  }
  block_t true_block() const {
    DCHECK(IsBranch() || IsConditionalBranch());
    return true_block_;
  }
  block_t false_block() const {
    DCHECK(IsBranch() || IsConditionalBranch());
    return false_block_;
  }
  node_t true_value() const {
    DCHECK(IsSelect());
    return true_value_;
  }
  node_t false_value() const {
    DCHECK(IsSelect());
    return false_value_;
  }
  const compare_chain_t& compares() const {
    DCHECK(IsConditionalSet() || IsConditionalBranch());
    return compares_;
  }
  uint32_t num_conditional_compares() const {
    DCHECK(IsConditionalSet() || IsConditionalBranch());
    return num_conditional_compares_;
  }

  void Negate() {
    DCHECK(!IsNone());
    DCHECK(!IsConditionalSet() && !IsConditionalBranch());
    condition_ = NegateFlagsCondition(condition_);
  }

  void Commute() {
    DCHECK(!IsNone());
    DCHECK(!IsConditionalSet() && !IsConditionalBranch());
    condition_ = CommuteFlagsCondition(condition_);
  }

  void Overwrite(FlagsCondition condition) {
    DCHECK(!IsConditionalSet() && !IsConditionalBranch());
    condition_ = condition;
  }

  void OverwriteAndNegateIfEqual(FlagsCondition condition) {
    DCHECK(condition_ == kEqual || condition_ == kNotEqual);
    DCHECK(!IsConditionalSet() && !IsConditionalBranch());
    bool negate = condition_ == kEqual;
    condition_ = condition;
    if (negate) Negate();
  }

  void OverwriteUnsignedIfSigned() {
    DCHECK(!IsConditionalSet() && !IsConditionalBranch());
    switch (condition_) {
      case kSignedLessThan:
        condition_ = kUnsignedLessThan;
        break;
      case kSignedLessThanOrEqual:
        condition_ = kUnsignedLessThanOrEqual;
        break;
      case kSignedGreaterThan:
        condition_ = kUnsignedGreaterThan;
        break;
      case kSignedGreaterThanOrEqual:
        condition_ = kUnsignedGreaterThanOrEqual;
        break;
      default:
        break;
    }
  }

  // Encodes this flags continuation into the given opcode.
  InstructionCode Encode(InstructionCode opcode) {
    opcode |= FlagsModeField::encode(mode_);
    if (mode_ != kFlags_none) {
      opcode |= FlagsConditionField::encode(condition_);
    }
    return opcode;
  }

 private:
  FlagsContinuationT(FlagsMode mode, FlagsCondition condition,
                     block_t true_block, block_t false_block)
      : mode_(mode),
        condition_(condition),
        true_block_(true_block),
        false_block_(false_block) {
    DCHECK(mode == kFlags_branch);
    DCHECK_NOT_NULL(true_block);
    DCHECK_NOT_NULL(false_block);
  }

  FlagsContinuationT(compare_chain_t& compares,
                     uint32_t num_conditional_compares,
                     FlagsCondition branch_condition, block_t true_block,
                     block_t false_block)
      : mode_(kFlags_conditional_branch),
        condition_(compares.front().compare_condition),
        final_condition_(branch_condition),
        num_conditional_compares_(num_conditional_compares),
        compares_(compares),
        true_block_(true_block),
        false_block_(false_block) {
    DCHECK_NOT_NULL(true_block);
    DCHECK_NOT_NULL(false_block);
  }

  FlagsContinuationT(FlagsMode mode, FlagsCondition condition,
                     DeoptimizeReason reason, id_t node_id,
                     FeedbackSource const& feedback, node_t frame_state)
      : mode_(mode),
        condition_(condition),
        reason_(reason),
        node_id_(node_id),
        feedback_(feedback),
        frame_state_or_result_(frame_state) {
    DCHECK(mode == kFlags_deoptimize);
    DCHECK(Adapter::valid(frame_state));
  }

  FlagsContinuationT(FlagsCondition condition, node_t result)
      : mode_(kFlags_set),
        condition_(condition),
        frame_state_or_result_(result) {
    DCHECK(Adapter::valid(result));
  }

  FlagsContinuationT(compare_chain_t& compares,
                     uint32_t num_conditional_compares,
                     FlagsCondition set_condition, node_t result)
      : mode_(kFlags_conditional_set),
        condition_(compares.front().compare_condition),
        final_condition_(set_condition),
        num_conditional_compares_(num_conditional_compares),
        compares_(compares),
        frame_state_or_result_(result) {
    DCHECK(Adapter::valid(result));
  }

  FlagsContinuationT(FlagsCondition condition, TrapId trap_id)
      : mode_(kFlags_trap), condition_(condition), trap_id_(trap_id) {}

  FlagsContinuationT(FlagsCondition condition, node_t result, node_t true_value,
                     node_t false_value)
      : mode_(kFlags_select),
        condition_(condition),
        frame_state_or_result_(result),
        true_value_(true_value),
        false_value_(false_value) {
    DCHECK(Adapter::valid(result));
    DCHECK(Adapter::valid(true_value));
    DCHECK(Adapter::valid(false_value));
  }

  FlagsMode const mode_;
  FlagsCondition condition_;
  FlagsCondition final_condition_;     // Only valid if mode_ ==
                                       // kFlags_conditional_set.
  uint32_t num_conditional_compares_;  // Only valid if mode_ ==
                                       // kFlags_conditional_set.
  compare_chain_t compares_;  // Only valid if mode_ == kFlags_conditional_set.
  DeoptimizeReason reason_;         // Only valid if mode_ == kFlags_deoptimize*
  id_t node_id_;                    // Only valid if mode_ == kFlags_deoptimize*
  FeedbackSource feedback_;         // Only valid if mode_ == kFlags_deoptimize*
  node_t frame_state_or_result_;    // Only valid if mode_ == kFlags_deoptimize*
                                    // or mode_ == kFlags_set.
  block_t true_block_;              // Only valid if mode_ == kFlags_branch*.
  block_t false_block_;             // Only valid if mode_ == kFlags_branch*.
  TrapId trap_id_;                  // Only valid if mode_ == kFlags_trap.
  node_t true_value_;               // Only valid if mode_ == kFlags_select.
  node_t false_value_;              // Only valid if mode_ == kFlags_select.
};

// This struct connects nodes of parameters which are going to be pushed on the
// call stack with their parameter index in the call descriptor of the callee.
template <typename Adapter>
struct PushParameterT {
  using node_t = typename Adapter::node_t;
  PushParameterT(node_t n = {},
                 LinkageLocation l = LinkageLocation::ForAnyRegister())
      : node(n), location(l) {}

  node_t node;
  LinkageLocation location;
};

enum class FrameStateInputKind { kAny, kStackSlot };

// Instruction selection generates an InstructionSequence for a given Schedule.
template <typename Adapter>
class InstructionSelectorT final : public Adapter {
 public:
  using OperandGenerator = OperandGeneratorT<Adapter>;
  using PushParameter = PushParameterT<Adapter>;
  using CallBuffer = CallBufferT<Adapter>;
  using FlagsContinuation = FlagsContinuationT<Adapter>;
  using SwitchInfo = SwitchInfoT<Adapter>;
  using CaseInfo = CaseInfoT<Adapter>;

  using schedule_t = typename Adapter::schedule_t;
  using block_t = typename Adapter::block_t;
  using block_range_t = typename Adapter::block_range_t;
  using node_t = typename Adapter::node_t;
  using optional_node_t = typename Adapter::optional_node_t;
  using id_t = typename Adapter::id_t;
  using source_position_table_t = typename Adapter::source_position_table_t;

  using Features = InstructionSelector::Features;

  InstructionSelectorT(
      Zone* zone, size_t node_count, Linkage* linkage,
      InstructionSequence* sequence, schedule_t schedule,
      source_position_table_t* source_positions, Frame* frame,
      InstructionSelector::EnableSwitchJumpTable enable_switch_jump_table,
      TickCounter* tick_counter, JSHeapBroker* broker,
      size_t* max_unoptimized_frame_height, size_t* max_pushed_argument_count,
      InstructionSelector::SourcePositionMode source_position_mode =
          InstructionSelector::kCallSourcePositions,
      Features features = SupportedFeatures(),
      InstructionSelector::EnableScheduling enable_scheduling =
          v8_flags.turbo_instruction_scheduling
              ? InstructionSelector::kEnableScheduling
              : InstructionSelector::kDisableScheduling,
      InstructionSelector::EnableRootsRelativeAddressing
          enable_roots_relative_addressing =
              InstructionSelector::kDisableRootsRelativeAddressing,
      InstructionSelector::EnableTraceTurboJson trace_turbo =
          InstructionSelector::kDisableTraceTurboJson);

  // Visit code for the entire graph with the included schedule.
  std::optional<BailoutReason> SelectInstructions();

  void StartBlock(RpoNumber rpo);
  void EndBlock(RpoNumber rpo);
  void AddInstruction(Instruction* instr);
  void AddTerminator(Instruction* instr);

  // ===========================================================================
  // ============= Architecture-independent code emission methods. =============
  // ===========================================================================

  Instruction* Emit(InstructionCode opcode, InstructionOperand output,
                    size_t temp_count = 0, InstructionOperand* temps = nullptr);
  Instruction* Emit(InstructionCode opcode, InstructionOperand output,
                    InstructionOperand a, size_t temp_count = 0,
                    InstructionOperand* temps = nullptr);
  Instruction* Emit(InstructionCode opcode, InstructionOperand output,
                    InstructionOperand a, InstructionOperand b,
                    size_t temp_count = 0, InstructionOperand* temps = nullptr);
  Instruction* Emit(InstructionCode opcode, InstructionOperand output,
                    InstructionOperand a, InstructionOperand b,
                    InstructionOperand c, size_t temp_count = 0,
                    InstructionOperand* temps = nullptr);
  Instruction* Emit(InstructionCode opcode, InstructionOperand output,
                    InstructionOperand a, InstructionOperand b,
                    InstructionOperand c, InstructionOperand d,
                    size_t temp_count = 0, InstructionOperand* temps = nullptr);
  Instruction* Emit(InstructionCode opcode, InstructionOperand output,
                    InstructionOperand a, InstructionOperand b,
                    InstructionOperand c, InstructionOperand d,
                    InstructionOperand e, size_t temp_count = 0,
                    InstructionOperand* temps = nullptr);
  Instruction* Emit(InstructionCode opcode, InstructionOperand output,
                    InstructionOperand a, InstructionOperand b,
                    InstructionOperand c, InstructionOperand d,
                    InstructionOperand e, InstructionOperand f,
                    size_t temp_count = 0, InstructionOperand* temps = nullptr);
  Instruction* Emit(InstructionCode opcode, InstructionOperand output,
                    InstructionOperand a, InstructionOperand b,
                    InstructionOperand c, InstructionOperand d,
                    InstructionOperand e, InstructionOperand f,
                    InstructionOperand g, InstructionOperand h,
                    size_t temp_count = 0, InstructionOperand* temps = nullptr);
  Instruction* Emit(InstructionCode opcode, size_t output_count,
                    InstructionOperand* outputs, size_t input_count,
                    InstructionOperand* inputs, size_t temp_count = 0,
                    InstructionOperand* temps = nullptr);
  Instruction* Emit(Instruction* instr);

  // [0-3] operand instructions with no output, uses labels for true and false
  // blocks of the continuation.
  Instruction* EmitWithContinuation(InstructionCode opcode,
                                    FlagsContinuation* cont);
  Instruction* EmitWithContinuation(InstructionCode opcode,
                                    InstructionOperand a,
                                    FlagsContinuation* cont);
  Instruction* EmitWithContinuation(InstructionCode opcode,
                                    InstructionOperand a, InstructionOperand b,
                                    FlagsContinuation* cont);
  Instruction* EmitWithContinuation(InstructionCode opcode,
                                    InstructionOperand a, InstructionOperand b,
                                    InstructionOperand c,
                                    FlagsContinuation* cont);
  Instruction* EmitWithContinuation(InstructionCode opcode, size_t output_count,
                                    InstructionOperand* outputs,
                                    size_t input_count,
                                    InstructionOperand* inputs,
                                    FlagsContinuation* cont);
  Instruction* EmitWithContinuation(
      InstructionCode opcode, size_t output_count, InstructionOperand* outputs,
      size_t input_count, InstructionOperand* inputs, size_t temp_count,
      InstructionOperand* temps, FlagsContinuation* cont);

  void EmitIdentity(node_t node);

  // ===========================================================================
  // ============== Architecture-independent CPU feature methods. ==============
  // ===========================================================================

  bool IsSupported(CpuFeature feature) const {
    return features_.Contains(feature);
  }

  // Returns the features supported on the target platform.
  static Features SupportedFeatures() {
    return Features(CpuFeatures::SupportedFeatures());
  }

  // ===========================================================================
  // ============ Architecture-independent graph covering methods. =============
  // ===========================================================================

  // Used in pattern matching during code generation.
  // Check if {node} can be covered while generating code for the current
  // instruction. A node can be covered if the {user} of the node has the only
  // edge, the two are in the same basic block, and there are no side-effects
  // in-between. The last check is crucial for soundness.
  // For pure nodes, CanCover(a,b) is checked to avoid duplicated execution:
  // If this is not the case, code for b must still be generated for other
  // users, and fusing is unlikely to improve performance.
  bool CanCover(node_t user, node_t node) const;

  // Used in pattern matching during code generation.
  // This function checks that {node} and {user} are in the same basic block,
  // and that {user} is the only user of {node} in this basic block.  This
  // check guarantees that there are no users of {node} scheduled between
  // {node} and {user}, and thus we can select a single instruction for both
  // nodes, if such an instruction exists. This check can be used for example
  // when selecting instructions for:
  //   n = Int32Add(a, b)
  //   c = Word32Compare(n, 0, cond)
  //   Branch(c, true_label, false_label)
  // Here we can generate a flag-setting add instruction, even if the add has
  // uses in other basic blocks, since the flag-setting add instruction will
  // still generate the result of the addition and not just set the flags.
  // However, if we had uses of the add in the same basic block, we could have:
  //   n = Int32Add(a, b)
  //   o = OtherOp(n, ...)
  //   c = Word32Compare(n, 0, cond)
  //   Branch(c, true_label, false_label)
  // where we cannot select the add and the compare together.  If we were to
  // select a flag-setting add instruction for Word32Compare and Int32Add while
  // visiting Word32Compare, we would then have to select an instruction for
  // OtherOp *afterwards*, which means we would attempt to use the result of
  // the add before we have defined it.
  bool IsOnlyUserOfNodeInSameBlock(node_t user, node_t node) const;

  // Checks if {node} was already defined, and therefore code was already
  // generated for it.
  bool IsDefined(node_t node) const;

  // Checks if {node} has any uses, and therefore code has to be generated for
  // it. Always returns {true} if the node has effect IsRequiredWhenUnused.
  bool IsUsed(node_t node) const;
  // Checks if {node} has any uses, and therefore code has to be generated for
  // it. Ignores the IsRequiredWhenUnused effect.
  bool IsReallyUsed(node_t node) const;

  // Checks if {node} is currently live.
  bool IsLive(node_t node) const { return !IsDefined(node) && IsUsed(node); }
  // Checks if {node} is currently live, ignoring the IsRequiredWhenUnused
  // effect.
  bool IsReallyLive(node_t node) const {
    return !IsDefined(node) && IsReallyUsed(node);
  }

  // Gets the effect level of {node}.
  int GetEffectLevel(node_t node) const;

  // Gets the effect level of {node}, appropriately adjusted based on
  // continuation flags if the node is a branch.
  int GetEffectLevel(node_t node, FlagsContinuation* cont) const;

  int GetVirtualRegister(node_t node);
  const std::map<id_t, int> GetVirtualRegistersForTesting() const;

  // Check if we can generate loads and stores of ExternalConstants relative
  // to the roots register.
  bool CanAddressRelativeToRootsRegister(
      const ExternalReference& reference) const;
  // Check if we can use the roots register to access GC roots.
  bool CanUseRootsRegister() const;

  Isolate* isolate() const { return sequence()->isolate(); }

  const ZoneVector<std::pair<int, int>>& instr_origins() const {
    return instr_origins_;
  }

  node_t FindProjection(node_t node, size_t projection_index);

  // Records that this ProtectedLoad node can be deleted if not used, even
  // though it has a required_when_unused effect.
  void SetProtectedLoadToRemove(node_t node) {
    if constexpr (Adapter::IsTurboshaft) {
      DCHECK(this->IsProtectedLoad(node));
      protected_loads_to_remove_->Add(this->id(node));
    } else {
      UNREACHABLE();
    }
  }

  // Records that this node embeds a ProtectedLoad as operand, and so it is
  // itself a "protected" instruction, for which we'll need to record the source
  // position.
  void MarkAsProtected(node_t node) {
    if constexpr (Adapter::IsTurboshaft) {
      additional_protected_instructions_->Add(this->id(node));
    } else {
      UNREACHABLE();
    }
  }

 private:
  friend class OperandGeneratorT<Adapter>;

  bool UseInstructionScheduling() const {
    return (enable_scheduling_ == InstructionSelector::kEnableScheduling) &&
           InstructionScheduler::SchedulerSupported();
  }

  void AppendDeoptimizeArguments(InstructionOperandVector* args,
                                 DeoptimizeReason reason, id_t node_id,
                                 FeedbackSource const& feedback,
                                 node_t frame_state,
                                 DeoptimizeKind kind = DeoptimizeKind::kEager);

  void EmitTableSwitch(const SwitchInfo& sw,
                       InstructionOperand const& index_operand);
  void EmitBinarySearchSwitch(const SwitchInfo& sw,
                              InstructionOperand const& value_operand);

  void TryRename(InstructionOperand* op);
  int GetRename(int virtual_register);
  void SetRename(node_t node, node_t rename);
  void UpdateRenames(Instruction* instruction);
  void UpdateRenamesInPhi(PhiInstruction* phi);

  // Inform the instruction selection that {node} was just defined.
  void MarkAsDefined(node_t node);

  // Inform the instruction selection that {node} has at least one use and we
  // will need to generate code for it.
  void MarkAsUsed(node_t node);

  // Sets the effect level of {node}.
  void SetEffectLevel(node_t node, int effect_level);

  // Inform the register allocation of the representation of the value produced
  // by {node}.
  void MarkAsRepresentation(MachineRepresentation rep, node_t node);
  void MarkAsRepresentation(turboshaft::RegisterRepresentation rep,
                            node_t node) {
    MarkAsRepresentation(rep.machine_representation(), node);
  }
  void MarkAsWord32(node_t node) {
    MarkAsRepresentation(MachineRepresentation::kWord32, node);
  }
  void MarkAsWord64(node_t node) {
    MarkAsRepresentation(MachineRepresentation::kWord64, node);
  }
  void MarkAsFloat32(node_t node) {
    MarkAsRepresentation(MachineRepresentation::kFloat32, node);
  }
  void MarkAsFloat64(node_t node) {
    MarkAsRepresentation(MachineRepresentation::kFloat64, node);
  }
  void MarkAsSimd128(node_t node) {
    MarkAsRepresentation(MachineRepresentation::kSimd128, node);
  }
  void MarkAsSimd256(node_t node) {
    MarkAsRepresentation(MachineRepresentation::kSimd256, node);
  }
  void MarkAsTagged(node_t node) {
    MarkAsRepresentation(MachineRepresentation::kTagged, node);
  }
  void MarkAsCompressed(node_t node) {
    MarkAsRepresentation(MachineRepresentation::kCompressed, node);
  }

  // Inform the register allocation of the representation of the unallocated
  // operand {op}.
  void MarkAsRepresentation(MachineRepresentation rep,
                            const InstructionOperand& op);

  enum CallBufferFlag {
    kCallCodeImmediate = 1u << 0,
    kCallAddressImmediate = 1u << 1,
    kCallTail = 1u << 2,
    kCallFixedTargetRegister = 1u << 3
  };
  using CallBufferFlags = base::Flags<CallBufferFlag>;

  // Initialize the call buffer with the InstructionOperands, nodes, etc,
  // corresponding
  // to the inputs and outputs of the call.
  // {call_code_immediate} to generate immediate operands to calls of code.
  // {call_address_immediate} to generate immediate operands to address calls.
  void InitializeCallBuffer(node_t call, CallBuffer* buffer,
                            CallBufferFlags flags, int stack_slot_delta = 0);
  bool IsTailCallAddressImmediate();

  void UpdateMaxPushedArgumentCount(size_t count);

  using StateObjectDeduplicator =
      std::conditional_t<std::is_same_v<Adapter, TurboshaftAdapter>,
                         TurboshaftStateObjectDeduplicator,
                         TurbofanStateObjectDeduplicator>;
  FrameStateDescriptor* GetFrameStateDescriptor(node_t node);
  size_t AddInputsToFrameStateDescriptor(FrameStateDescriptor* descriptor,
                                         node_t state, OperandGenerator* g,
                                         StateObjectDeduplicator* deduplicator,
                                         InstructionOperandVector* inputs,
                                         FrameStateInputKind kind, Zone* zone);
  size_t AddInputsToFrameStateDescriptor(StateValueList* values,
                                         InstructionOperandVector* inputs,
                                         OperandGenerator* g,
                                         StateObjectDeduplicator* deduplicator,
                                         node_t node, FrameStateInputKind kind,
                                         Zone* zone);
  size_t AddOperandToStateValueDescriptor(StateValueList* values,
                                          InstructionOperandVector* inputs,
                                          OperandGenerator* g,
                                          StateObjectDeduplicator* deduplicator,
                                          node_t input, MachineType type,
                                          FrameStateInputKind kind, Zone* zone);

  // ===========================================================================
  // ============= Architecture-specific graph covering methods. ===============
  // ===========================================================================

  // Visit nodes in the given block and generate code.
  void VisitBlock(block_t block);

  // Visit the node for the control flow at the end of the block, generating
  // code if necessary.
  void VisitControl(block_t block);

  // Visit the node and generate code, if any.
  void VisitNode(node_t node);

  // Visit the node and generate code for IEEE 754 functions.
  void VisitFloat64Ieee754Binop(node_t, InstructionCode code);
  void VisitFloat64Ieee754Unop(node_t, InstructionCode code);

#define DECLARE_GENERATOR_T(x) void Visit##x(node_t node);
  DECLARE_GENERATOR_T(Word32And)
  DECLARE_GENERATOR_T(Word32Xor)
  DECLARE_GENERATOR_T(Int32Add)
  DECLARE_GENERATOR_T(Int32Sub)
  DECLARE_GENERATOR_T(Int32Mul)
  DECLARE_GENERATOR_T(Int32MulHigh)
  DECLARE_GENERATOR_T(Int32Div)
  DECLARE_GENERATOR_T(Int32Mod)
  DECLARE_GENERATOR_T(Uint32Div)
  DECLARE_GENERATOR_T(Uint32Mod)
  DECLARE_GENERATOR_T(Uint32MulHigh)
  DECLARE_GENERATOR_T(Word32Or)
  DECLARE_GENERATOR_T(Word32Sar)
  DECLARE_GENERATOR_T(Word32Shl)
  DECLARE_GENERATOR_T(Word32Shr)
  DECLARE_GENERATOR_T(Word32Rol)
  DECLARE_GENERATOR_T(Word32Ror)
  DECLARE_GENERATOR_T(Word64Shl)
  DECLARE_GENERATOR_T(Word64Sar)
  DECLARE_GENERATOR_T(Word64Shr)
  DECLARE_GENERATOR_T(Word64Rol)
  DECLARE_GENERATOR_T(Word64Ror)
  DECLARE_GENERATOR_T(Int32AddWithOverflow)
  DECLARE_GENERATOR_T(Int32MulWithOverflow)
  DECLARE_GENERATOR_T(Int32SubWithOverflow)
  DECLARE_GENERATOR_T(Int64AddWithOverflow)
  DECLARE_GENERATOR_T(Int64SubWithOverflow)
  DECLARE_GENERATOR_T(Int64MulWithOverflow)
  DECLARE_GENERATOR_T(Int64Add)
  DECLARE_GENERATOR_T(Word64And)
  DECLARE_GENERATOR_T(Word64Or)
  DECLARE_GENERATOR_T(Word64Xor)
  DECLARE_GENERATOR_T(Int64Sub)
  DECLARE_GENERATOR_T(Int64Mul)
  DECLARE_GENERATOR_T(Int64MulHigh)
  DECLARE_GENERATOR_T(Int64Div)
  DECLARE_GENERATOR_T(Int64Mod)
  DECLARE_GENERATOR_T(Uint64Div)
  DECLARE_GENERATOR_T(Uint64Mod)
  DECLARE_GENERATOR_T(Uint64MulHigh)
  DECLARE_GENERATOR_T(Word32AtomicStore)
  DECLARE_GENERATOR_T(Word64AtomicStore)
  DECLARE_GENERATOR_T(Word32Equal)
  DECLARE_GENERATOR_T(Word64Equal)
  DECLARE_GENERATOR_T(Int32LessThan)
  DECLARE_GENERATOR_T(Int32LessThanOrEqual)
  DECLARE_GENERATOR_T(Int64LessThan)
  DECLARE_GENERATOR_T(Int64LessThanOrEqual)
  DECLARE_GENERATOR_T(Uint32LessThan)
  DECLARE_GENERATOR_T(Uint32LessThanOrEqual)
  DECLARE_GENERATOR_T(Uint64LessThan)
  DECLARE_GENERATOR_T(Uint64LessThanOrEqual)
  DECLARE_GENERATOR_T(Float64Sub)
  DECLARE_GENERATOR_T(Float64Div)
  DECLARE_GENERATOR_T(Float32Equal)
  DECLARE_GENERATOR_T(Float32LessThan)
  DECLARE_GENERATOR_T(Float32LessThanOrEqual)
  DECLARE_GENERATOR_T(Float64Equal)
  DECLARE_GENERATOR_T(Float64LessThan)
  DECLARE_GENERATOR_T(Float64LessThanOrEqual)
  DECLARE_GENERATOR_T(Load)
  DECLARE_GENERATOR_T(StackPointerGreaterThan)
  DECLARE_GENERATOR_T(Store)
  DECLARE_GENERATOR_T(ProtectedStore)
  DECLARE_GENERATOR_T(BitcastTaggedToWord)
  DECLARE_GENERATOR_T(BitcastWordToTagged)
  DECLARE_GENERATOR_T(BitcastSmiToWord)
  DECLARE_GENERATOR_T(ChangeInt32ToInt64)
  DECLARE_GENERATOR_T(ChangeInt32ToFloat64)
  DECLARE_GENERATOR_T(ChangeFloat32ToFloat64)
  DECLARE_GENERATOR_T(RoundFloat64ToInt32)
  DECLARE_GENERATOR_T(TruncateFloat64ToWord32)
  DECLARE_GENERATOR_T(TruncateFloat64ToFloat32)
  DECLARE_GENERATOR_T(TruncateFloat32ToInt32)
  DECLARE_GENERATOR_T(TruncateFloat32ToUint32)
  DECLARE_GENERATOR_T(ChangeFloat64ToInt32)
  DECLARE_GENERATOR_T(ChangeFloat64ToUint32)
  DECLARE_GENERATOR_T(ChangeFloat64ToInt64)
  DECLARE_GENERATOR_T(ChangeFloat64ToUint64)
  DECLARE_GENERATOR_T(TruncateFloat64ToInt64)
  DECLARE_GENERATOR_T(RoundInt32ToFloat32)
  DECLARE_GENERATOR_T(RoundInt64ToFloat32)
  DECLARE_GENERATOR_T(RoundInt64ToFloat64)
  DECLARE_GENERATOR_T(RoundUint32ToFloat32)
  DECLARE_GENERATOR_T(RoundUint64ToFloat32)
  DECLARE_GENERATOR_T(RoundUint64ToFloat64)
  DECLARE_GENERATOR_T(ChangeInt64ToFloat64)
  DECLARE_GENERATOR_T(ChangeUint32ToFloat64)
  DECLARE_GENERATOR_T(ChangeUint32ToUint64)
  DECLARE_GENERATOR_T(Float64ExtractLowWord32)
  DECLARE_GENERATOR_T(Float64ExtractHighWord32)
  DECLARE_GENERATOR_T(Float32Add)
  DECLARE_GENERATOR_T(Float32Sub)
  DECLARE_GENERATOR_T(Float32Mul)
  DECLARE_GENERATOR_T(Float32Div)
  DECLARE_GENERATOR_T(Float32Max)
  DECLARE_GENERATOR_T(Float32Min)
  DECLARE_GENERATOR_T(Float64Atan2)
  DECLARE_GENERATOR_T(Float64Max)
  DECLARE_GENERATOR_T(Float64Min)
  DECLARE_GENERATOR_T(Float64Add)
  DECLARE_GENERATOR_T(Float64Mul)
  DECLARE_GENERATOR_T(Float64Mod)
  DECLARE_GENERATOR_T(Float64Pow)
  DECLARE_GENERATOR_T(BitcastWord32ToWord64)
  DECLARE_GENERATOR_T(BitcastFloat32ToInt32)
  DECLARE_GENERATOR_T(BitcastFloat64ToInt64)
  DECLARE_GENERATOR_T(BitcastInt32ToFloat32)
  DECLARE_GENERATOR_T(BitcastInt64ToFloat64)
  DECLARE_GENERATOR_T(Float32Abs)
  DECLARE_GENERATOR_T(Float32Neg)
  DECLARE_GENERATOR_T(Float32RoundDown)
  DECLARE_GENERATOR_T(Float32RoundTiesEven)
  DECLARE_GENERATOR_T(Float32RoundTruncate)
  DECLARE_GENERATOR_T(Float32RoundUp)
  DECLARE_GENERATOR_T(Float32Sqrt)
  DECLARE_GENERATOR_T(Float64Abs)
  DECLARE_GENERATOR_T(Float64Acos)
  DECLARE_GENERATOR_T(Float64Acosh)
  DECLARE_GENERATOR_T(Float64Asin)
  DECLARE_GENERATOR_T(Float64Asinh)
  DECLARE_GENERATOR_T(Float64Atan)
  DECLARE_GENERATOR_T(Float64Atanh)
  DECLARE_GENERATOR_T(Float64Cbrt)
  DECLARE_GENERATOR_T(Float64Cos)
  DECLARE_GENERATOR_T(Float64Cosh)
  DECLARE_GENERATOR_T(Float64Exp)
  DECLARE_GENERATOR_T(Float64Expm1)
  DECLARE_GENERATOR_T(Float64Log)
  DECLARE_GENERATOR_T(Float64Log1p)
  DECLARE_GENERATOR_T(Float64Log10)
  DECLARE_GENERATOR_T(Float64Log2)
  DECLARE_GENERATOR_T(Float64Neg)
  DECLARE_GENERATOR_T(Float64RoundDown)
  DECLARE_GENERATOR_T(Float64RoundTiesAway)
  DECLARE_GENERATOR_T(Float64RoundTiesEven)
  DECLARE_GENERATOR_T(Float64RoundTruncate)
  DECLARE_GENERATOR_T(Float64RoundUp)
  DECLARE_GENERATOR_T(Float64Sin)
  DECLARE_GENERATOR_T(Float64Sinh)
  DECLARE_GENERATOR_T(Float64Sqrt)
  DECLARE_GENERATOR_T(Float64Tan)
  DECLARE_GENERATOR_T(Float64Tanh)
  DECLARE_GENERATOR_T(Float64SilenceNaN)
  DECLARE_GENERATOR_T(Word32Clz)
  DECLARE_GENERATOR_T(Word32Ctz)
  DECLARE_GENERATOR_T(Word32ReverseBytes)
  DECLARE_GENERATOR_T(Word32Popcnt)
  DECLARE_GENERATOR_T(Word64Popcnt)
  DECLARE_GENERATOR_T(Word64Clz)
  DECLARE_GENERATOR_T(Word64Ctz)
  DECLARE_GENERATOR_T(Word64ReverseBytes)
  DECLARE_GENERATOR_T(SignExtendWord8ToInt32)
  DECLARE_GENERATOR_T(SignExtendWord16ToInt32)
  DECLARE_GENERATOR_T(SignExtendWord8ToInt64)
  DECLARE_GENERATOR_T(SignExtendWord16ToInt64)
  DECLARE_GENERATOR_T(TruncateInt64ToInt32)
  DECLARE_GENERATOR_T(StackSlot)
  DECLARE_GENERATOR_T(LoadRootRegister)
  DECLARE_GENERATOR_T(DebugBreak)
  DECLARE_GENERATOR_T(TryTruncateFloat32ToInt64)
  DECLARE_GENERATOR_T(TryTruncateFloat64ToInt64)
  DECLARE_GENERATOR_T(TryTruncateFloat32ToUint64)
  DECLARE_GENERATOR_T(TryTruncateFloat64ToUint64)
  DECLARE_GENERATOR_T(TryTruncateFloat64ToInt32)
  DECLARE_GENERATOR_T(TryTruncateFloat64ToUint32)
  DECLARE_GENERATOR_T(Int32PairAdd)
  DECLARE_GENERATOR_T(Int32PairSub)
  DECLARE_GENERATOR_T(Int32PairMul)
  DECLARE_GENERATOR_T(Word32PairShl)
  DECLARE_GENERATOR_T(Word32PairShr)
  DECLARE_GENERATOR_T(Word32PairSar)
  DECLARE_GENERATOR_T(Float64InsertLowWord32)
  DECLARE_GENERATOR_T(Float64InsertHighWord32)
  DECLARE_GENERATOR_T(Comment)
  DECLARE_GENERATOR_T(Word32ReverseBits)
  DECLARE_GENERATOR_T(Word64ReverseBits)
  DECLARE_GENERATOR_T(AbortCSADcheck)
  DECLARE_GENERATOR_T(StorePair)
  DECLARE_GENERATOR_T(UnalignedLoad)
  DECLARE_GENERATOR_T(UnalignedStore)
  DECLARE_GENERATOR_T(Int32AbsWithOverflow)
  DECLARE_GENERATOR_T(Int64AbsWithOverflow)
  DECLARE_GENERATOR_T(TruncateFloat64ToUint32)
  DECLARE_GENERATOR_T(SignExtendWord32ToInt64)
  DECLARE_GENERATOR_T(TraceInstruction)
  DECLARE_GENERATOR_T(MemoryBarrier)
  DECLARE_GENERATOR_T(LoadStackCheckOffset)
  DECLARE_GENERATOR_T(LoadFramePointer)
  DECLARE_GENERATOR_T(LoadParentFramePointer)
  DECLARE_GENERATOR_T(ProtectedLoad)
  DECLARE_GENERATOR_T(Word32AtomicAdd)
  DECLARE_GENERATOR_T(Word32AtomicSub)
  DECLARE_GENERATOR_T(Word32AtomicAnd)
  DECLARE_GENERATOR_T(Word32AtomicOr)
  DECLARE_GENERATOR_T(Word32AtomicXor)
  DECLARE_GENERATOR_T(Word32AtomicExchange)
  DECLARE_GENERATOR_T(Word32AtomicCompareExchange)
  DECLARE_GENERATOR_T(Word64AtomicAdd)
  DECLARE_GENERATOR_T(Word64AtomicSub)
  DECLARE_GENERATOR_T(Word64AtomicAnd)
  DECLARE_GENERATOR_T(Word64AtomicOr)
  DECLARE_GENERATOR_T(Word64AtomicXor)
  DECLARE_GENERATOR_T(Word64AtomicExchange)
  DECLARE_GENERATOR_T(Word64AtomicCompareExchange)
  DECLARE_GENERATOR_T(Word32AtomicLoad)
  DECLARE_GENERATOR_T(Word64AtomicLoad)
  DECLARE_GENERATOR_T(Word32AtomicPairLoad)
  DECLARE_GENERATOR_T(Word32AtomicPairStore)
  DECLARE_GENERATOR_T(Word32AtomicPairAdd)
  DECLARE_GENERATOR_T(Word32AtomicPairSub)
  DECLARE_GENERATOR_T(Word32AtomicPairAnd)
  DECLARE_GENERATOR_T(Word32AtomicPairOr)
  DECLARE_GENERATOR_T(Word32AtomicPairXor)
  DECLARE_GENERATOR_T(Word32AtomicPairExchange)
  DECLARE_GENERATOR_T(Word32AtomicPairCompareExchange)
  DECLARE_GENERATOR_T(Simd128ReverseBytes)
  MACHINE_SIMD128_OP_LIST(DECLARE_GENERATOR_T)
  MACHINE_SIMD256_OP_LIST(DECLARE_GENERATOR_T)
  IF_WASM(DECLARE_GENERATOR_T, LoadStackPointer)
  IF_WASM(DECLARE_GENERATOR_T, SetStackPointer)
#undef DECLARE_GENERATOR_T

  // Visit the load node with a value and opcode to replace with.
  void VisitLoad(node_t node, node_t value, InstructionCode opcode);
  void VisitLoadTransform(Node* node, Node* value, InstructionCode opcode);
  void VisitFinishRegion(Node* node);
  void VisitParameter(node_t node);
  void VisitIfException(node_t node);
  void VisitOsrValue(node_t node);
  void VisitPhi(node_t node);
  void VisitProjection(node_t node);
  void VisitConstant(node_t node);
  void VisitCall(node_t call, block_t handler = {});
  void VisitDeoptimizeIf(node_t node);
  void VisitDeoptimizeUnless(node_t node);
  void VisitDynamicCheckMapsWithDeoptUnless(Node* node);
  void VisitTrapIf(node_t node, TrapId trap_id);
  void VisitTrapUnless(node_t node, TrapId trap_id);
  void VisitTailCall(node_t call);
  void VisitGoto(block_t target);
  void VisitBranch(node_t input, block_t tbranch, block_t fbranch);
  void VisitSwitch(node_t node, const SwitchInfo& sw);
  void VisitDeoptimize(DeoptimizeReason reason, id_t node_id,
                       FeedbackSource const& feedback, node_t frame_state);
  void VisitSelect(node_t node);
  void VisitReturn(node_t node);
  void VisitThrow(Node* node);
  void VisitRetain(node_t node);
  void VisitUnreachable(node_t node);
  void VisitStaticAssert(node_t node);
  void VisitDeadValue(Node* node);
  void VisitBitcastWord32PairToFloat64(node_t node);

  void TryPrepareScheduleFirstProjection(node_t maybe_projection);

  void VisitStackPointerGreaterThan(node_t node, FlagsContinuation* cont);

  void VisitWordCompareZero(node_t user, node_t value, FlagsContinuation* cont);

  void EmitPrepareArguments(ZoneVector<PushParameter>* arguments,
                            const CallDescriptor* call_descriptor, node_t node);
  void EmitPrepareResults(ZoneVector<PushParameter>* results,
                          const CallDescriptor* call_descriptor, node_t node);

  // In LOONG64, calling convention uses free GP param register to pass
  // floating-point arguments when no FP param register is available. But
  // gap does not support moving from FPR to GPR, so we add EmitMoveFPRToParam
  // to complete movement.
  void EmitMoveFPRToParam(InstructionOperand* op, LinkageLocation location);
  // Moving floating-point param from GP param register to FPR to participate in
  // subsequent operations, whether CallCFunction or normal floating-point
  // operations.
  void EmitMoveParamToFPR(node_t node, int index);

  bool CanProduceSignalingNaN(Node* node);

  void AddOutputToSelectContinuation(OperandGenerator* g, int first_input_index,
                                     node_t node);

  void ConsumeEqualZero(turboshaft::OpIndex* user, turboshaft::OpIndex* value,
                        FlagsContinuation* cont);

  // ===========================================================================
  // ============= Vector instruction (SIMD) helper fns. =======================
  // ===========================================================================
  void VisitI8x16RelaxedSwizzle(node_t node);

#if V8_ENABLE_WEBASSEMBLY
  // Canonicalize shuffles to make pattern matching simpler. Returns the shuffle
  // indices, and a boolean indicating if the shuffle is a swizzle (one input).
  template <const int simd_size = kSimd128Size,
            typename = std::enable_if_t<simd_size == kSimd128Size ||
                                        simd_size == kSimd256Size>>
  void CanonicalizeShuffle(typename Adapter::SimdShuffleView& view,
                           uint8_t* shuffle, bool* is_swizzle) {
    // Get raw shuffle indices.
    if constexpr (simd_size == kSimd128Size) {
      DCHECK(view.isSimd128());
      memcpy(shuffle, view.data(), kSimd128Size);
    } else if constexpr (simd_size == kSimd256Size) {
      DCHECK(!view.isSimd128());
      memcpy(shuffle, view.data(), kSimd256Size);
    } else {
      UNREACHABLE();
    }
    bool needs_swap;
    bool inputs_equal =
        GetVirtualRegister(view.input(0)) == GetVirtualRegister(view.input(1));
    wasm::SimdShuffle::CanonicalizeShuffle<simd_size>(inputs_equal, shuffle,
                                                      &needs_swap, is_swizzle);
    if (needs_swap) {
      SwapShuffleInputs(view);
    }
    // Duplicate the first input; for some shuffles on some architectures, it's
    // easiest to implement a swizzle as a shuffle so it might be used.
    if (*is_swizzle) {
      view.DuplicateFirstInput();
    }
  }

  // Swaps the two first input operands of the node, to help match shuffles
  // to specific architectural instructions.
  void SwapShuffleInputs(typename Adapter::SimdShuffleView& node);

#if V8_ENABLE_WASM_SIMD256_REVEC
  void VisitSimd256LoadTransform(node_t node);

#ifdef V8_TARGET_ARCH_X64
  void VisitSimd256Shufd(node_t node);
  void VisitSimd256Shufps(node_t node);
  void VisitSimd256Unpack(node_t node);
  void VisitSimdPack128To256(node_t node);
#endif  // V8_TARGET_ARCH_X64
#endif  // V8_ENABLE_WASM_SIMD256_REVEC

#ifdef V8_TARGET_ARCH_X64
  bool CanOptimizeF64x2PromoteLowF32x4(node_t node);
#endif

#endif  // V8_ENABLE_WEBASSEMBLY

  // ===========================================================================

  schedule_t schedule() const { return schedule_; }
  Linkage* linkage() const { return linkage_; }
  InstructionSequence* sequence() const { return sequence_; }
  base::Vector<const turboshaft::OpIndex> turboshaft_uses(
      turboshaft::OpIndex node) const {
    DCHECK(turboshaft_use_map_.has_value());
    return turboshaft_use_map_->uses(node);
  }
  Zone* instruction_zone() const { return sequence()->zone(); }
  Zone* zone() const { return zone_; }

  void set_instruction_selection_failed() {
    instruction_selection_failed_ = true;
  }
  bool instruction_selection_failed() { return instruction_selection_failed_; }

  FlagsCondition GetComparisonFlagCondition(
      const turboshaft::ComparisonOp& op) const;

  void MarkPairProjectionsAsWord32(node_t node);
  bool IsSourcePositionUsed(node_t node);
  void VisitWord32AtomicBinaryOperation(node_t node, ArchOpcode int8_op,
                                        ArchOpcode uint8_op,
                                        ArchOpcode int16_op,
                                        ArchOpcode uint16_op,
                                        ArchOpcode word32_op);
  void VisitWord64AtomicBinaryOperation(node_t node, ArchOpcode uint8_op,
                                        ArchOpcode uint16_op,
                                        ArchOpcode uint32_op,
                                        ArchOpcode uint64_op);
  void VisitWord64AtomicNarrowBinop(Node* node, ArchOpcode uint8_op,
                                    ArchOpcode uint16_op, ArchOpcode uint32_op);

#if V8_TARGET_ARCH_64_BIT
  bool ZeroExtendsWord32ToWord64(node_t node, int recursion_depth = 0);
  bool ZeroExtendsWord32ToWord64NoPhis(node_t node);

  enum Upper32BitsState : uint8_t {
    kNotYetChecked,
    kUpperBitsGuaranteedZero,
    kNoGuarantee,
  };
#endif  // V8_TARGET_ARCH_64_BIT

  struct FrameStateInput {
    FrameStateInput(node_t node_, FrameStateInputKind kind_)
        : node(node_), kind(kind_) {}

    node_t node;
    FrameStateInputKind kind;

    struct Hash {
      size_t operator()(FrameStateInput const& source) const {
        return base::hash_combine(source.node,
                                  static_cast<size_t>(source.kind));
      }
    };

    struct Equal {
      bool operator()(FrameStateInput const& lhs,
                      FrameStateInput const& rhs) const {
        return lhs.node == rhs.node && lhs.kind == rhs.kind;
      }
    };
  };

  struct CachedStateValues;
  class CachedStateValuesBuilder;

  // ===========================================================================

  Zone* const zone_;
  Linkage* const linkage_;
  InstructionSequence* const sequence_;
  source_position_table_t* const source_positions_;
  InstructionSelector::SourcePositionMode const source_position_mode_;
  Features features_;
  schedule_t const schedule_;
  block_t current_block_;
  ZoneVector<Instruction*> instructions_;
  InstructionOperandVector continuation_inputs_;
  InstructionOperandVector continuation_outputs_;
  InstructionOperandVector continuation_temps_;
  BitVector defined_;
  BitVector used_;
  IntVector effect_level_;
  int current_effect_level_;
  IntVector virtual_registers_;
  IntVector virtual_register_rename_;
  InstructionScheduler* scheduler_;
  InstructionSelector::EnableScheduling enable_scheduling_;
  InstructionSelector::EnableRootsRelativeAddressing
      enable_roots_relative_addressing_;
  InstructionSelector::EnableSwitchJumpTable enable_switch_jump_table_;
  ZoneUnorderedMap<FrameStateInput, CachedStateValues*,
                   typename FrameStateInput::Hash,
                   typename FrameStateInput::Equal>
      state_values_cache_;

  Frame* frame_;
  bool instruction_selection_failed_;
  ZoneVector<std::pair<int, int>> instr_origins_;
  InstructionSelector::EnableTraceTurboJson trace_turbo_;
  TickCounter* const tick_counter_;
  // The broker is only used for unparking the LocalHeap for diagnostic printing
  // for failed StaticAsserts.
  JSHeapBroker* const broker_;

  // Store the maximal unoptimized frame height and an maximal number of pushed
  // arguments (for calls). Later used to apply an offset to stack checks.
  size_t* max_unoptimized_frame_height_;
  size_t* max_pushed_argument_count_;

  // Turboshaft-adapter only.
  std::optional<turboshaft::UseMap> turboshaft_use_map_;
  std::optional<BitVector> protected_loads_to_remove_;
  std::optional<BitVector> additional_protected_instructions_;

#if V8_TARGET_ARCH_64_BIT
  // Holds lazily-computed results for whether phi nodes guarantee their upper
  // 32 bits to be zero. Indexed by node ID; nobody reads or writes the values
  // for non-phi nodes.
  ZoneVector<Upper32BitsState> phi_states_;
#endif
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_BACKEND_INSTRUCTION_SELECTOR_H_
