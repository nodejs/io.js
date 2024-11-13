// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_GRAPH_BUILDER_H_
#define V8_MAGLEV_MAGLEV_GRAPH_BUILDER_H_

#include <cmath>
#include <iomanip>
#include <map>
#include <optional>
#include <type_traits>
#include <utility>

#include "src/base/functional.h"
#include "src/base/logging.h"
#include "src/base/vector.h"
#include "src/codegen/external-reference.h"
#include "src/codegen/source-position-table.h"
#include "src/common/globals.h"
#include "src/compiler-dispatcher/optimizing-compile-dispatcher.h"
#include "src/compiler/bytecode-analysis.h"
#include "src/compiler/bytecode-liveness-map.h"
#include "src/compiler/feedback-source.h"
#include "src/compiler/heap-refs.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/processed-feedback.h"
#include "src/deoptimizer/deoptimize-reason.h"
#include "src/flags/flags.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/interpreter/bytecode-decoder.h"
#include "src/interpreter/bytecode-register.h"
#include "src/interpreter/bytecodes.h"
#include "src/interpreter/interpreter-intrinsics.h"
#include "src/maglev/maglev-graph-labeller.h"
#include "src/maglev/maglev-graph-printer.h"
#include "src/maglev/maglev-graph.h"
#include "src/maglev/maglev-interpreter-frame-state.h"
#include "src/maglev/maglev-ir.h"
#include "src/objects/arguments.h"
#include "src/objects/bytecode-array.h"
#include "src/objects/elements-kind.h"
#include "src/objects/string.h"
#include "src/utils/memcopy.h"

namespace v8 {
namespace internal {
namespace maglev {

class CallArguments;

class V8_NODISCARD ReduceResult {
 public:
  enum Kind {
    kDoneWithValue = 0,  // No need to mask while returning the pointer.
    kDoneWithAbort,
    kDoneWithoutValue,
    kFail,
    kNone,
  };

  ReduceResult() : payload_(kNone) {}

  // NOLINTNEXTLINE
  ReduceResult(ValueNode* value) : payload_(value) { DCHECK_NOT_NULL(value); }

  ValueNode* value() const {
    DCHECK(HasValue());
    return payload_.GetPointerWithKnownPayload(kDoneWithValue);
  }
  bool HasValue() const { return kind() == kDoneWithValue; }

  static ReduceResult Done(ValueNode* value) { return ReduceResult(value); }
  static ReduceResult Done() { return ReduceResult(kDoneWithoutValue); }
  static ReduceResult DoneWithAbort() { return ReduceResult(kDoneWithAbort); }
  static ReduceResult Fail() { return ReduceResult(kFail); }

  ReduceResult(const ReduceResult&) V8_NOEXCEPT = default;
  ReduceResult& operator=(const ReduceResult&) V8_NOEXCEPT = default;

  // No/undefined result, created by default constructor.
  bool IsNone() const { return kind() == kNone; }

  // Either DoneWithValue, DoneWithoutValue or DoneWithAbort.
  bool IsDone() const { return !IsFail() && !IsNone(); }

  // ReduceResult failed.
  bool IsFail() const { return kind() == kFail; }

  // Done with a ValueNode.
  bool IsDoneWithValue() const { return HasValue(); }

  // Done without producing a ValueNode.
  bool IsDoneWithoutValue() const { return kind() == kDoneWithoutValue; }

  // Done with an abort (unconditional deopt, infinite loop in an inlined
  // function, etc)
  bool IsDoneWithAbort() const { return kind() == kDoneWithAbort; }

  Kind kind() const { return payload_.GetPayload(); }

 private:
  explicit ReduceResult(Kind kind) : payload_(kind) {}
  base::PointerWithPayload<ValueNode, Kind, 3> payload_;
};

#define RETURN_IF_DONE(result)   \
  do {                           \
    ReduceResult res = (result); \
    if (res.IsDone()) {          \
      return res;                \
    }                            \
  } while (false)

#define RETURN_VOID_IF_DONE(result) \
  do {                              \
    ReduceResult res = (result);    \
    if (res.IsDone()) {             \
      if (res.IsDoneWithAbort()) {  \
        MarkBytecodeDead();         \
        return;                     \
      }                             \
      return;                       \
    }                               \
  } while (false)

#define PROCESS_AND_RETURN_IF_DONE(result, value_processor) \
  do {                                                      \
    ReduceResult res = (result);                            \
    if (res.IsDone()) {                                     \
      if (res.IsDoneWithAbort()) {                          \
        MarkBytecodeDead();                                 \
      } else if (res.IsDoneWithValue()) {                   \
        value_processor(res.value());                       \
      }                                                     \
      return;                                               \
    }                                                       \
  } while (false)

#define RETURN_IF_ABORT(result)             \
  do {                                      \
    if ((result).IsDoneWithAbort()) {       \
      return ReduceResult::DoneWithAbort(); \
    }                                       \
  } while (false)

#define GET_VALUE_OR_ABORT(variable, result) \
  do {                                       \
    ReduceResult res = (result);             \
    if (res.IsDoneWithAbort()) {             \
      return ReduceResult::DoneWithAbort();  \
    }                                        \
    DCHECK(res.IsDoneWithValue());           \
    variable = res.value();                  \
  } while (false)

#define RETURN_VOID_IF_ABORT(result)  \
  do {                                \
    if ((result).IsDoneWithAbort()) { \
      MarkBytecodeDead();             \
      return;                         \
    }                                 \
  } while (false)

#define RETURN_VOID_ON_ABORT(result) \
  do {                               \
    ReduceResult res = (result);     \
    USE(res);                        \
    DCHECK(res.IsDoneWithAbort());   \
    MarkBytecodeDead();              \
    return;                          \
  } while (false)

enum class ToNumberHint {
  kDisallowToNumber,
  kAssumeSmi,
  kAssumeNumber,
  kAssumeNumberOrBoolean,
  kAssumeNumberOrOddball
};

enum class UseReprHintRecording { kRecord, kDoNotRecord };

NodeType StaticTypeForNode(compiler::JSHeapBroker* broker,
                           LocalIsolate* isolate, ValueNode* node);

class MaglevGraphBuilder {
 public:
  class DeoptFrameScope;

  explicit MaglevGraphBuilder(
      LocalIsolate* local_isolate, MaglevCompilationUnit* compilation_unit,
      Graph* graph, float call_frequency = 1.0f,
      BytecodeOffset caller_bytecode_offset = BytecodeOffset::None(),
      bool caller_is_inside_loop = false,
      int inlining_id = SourcePosition::kNotInlined,
      MaglevGraphBuilder* parent = nullptr);

  void Build() {
    DCHECK(!is_inline());

    StartPrologue();
    for (int i = 0; i < parameter_count(); i++) {
      // TODO(v8:7700): Consider creating InitialValue nodes lazily.
      InitialValue* v = AddNewNode<InitialValue>(
          {}, interpreter::Register::FromParameterIndex(i));
      DCHECK_EQ(graph()->parameters().size(), static_cast<size_t>(i));
      graph()->parameters().push_back(v);
      SetArgument(i, v);
    }

    BuildRegisterFrameInitialization();

    // Don't use the AddNewNode helper for the function entry stack check, so
    // that we can set a custom deopt frame on it.
    FunctionEntryStackCheck* function_entry_stack_check =
        NodeBase::New<FunctionEntryStackCheck>(zone(), 0);
    new (function_entry_stack_check->lazy_deopt_info()) LazyDeoptInfo(
        zone(), GetDeoptFrameForEntryStackCheck(),
        interpreter::Register::invalid_value(), 0, compiler::FeedbackSource());
    AddInitializedNodeToGraph(function_entry_stack_check);

    BuildMergeStates();
    EndPrologue();
    in_prologue_ = false;

    compiler::ScopeInfoRef scope_info =
        compilation_unit_->shared_function_info().scope_info(broker());
    if (scope_info.HasOuterScopeInfo()) {
      scope_info = scope_info.OuterScopeInfo(broker());
      while (!scope_info.HasContext() && scope_info.HasOuterScopeInfo()) {
        scope_info = scope_info.OuterScopeInfo(broker());
      }
      if (scope_info.HasContext()) {
        graph()->record_scope_info(GetContext(), scope_info);
      }
    }
    if (compilation_unit_->is_osr()) {
      OsrAnalyzePrequel();
    }

    BuildBody();
  }

  ReduceResult BuildInlined(ValueNode* context, ValueNode* function,
                            ValueNode* new_target, const CallArguments& args);

  void StartPrologue();
  void SetArgument(int i, ValueNode* value);
  void InitializeRegister(interpreter::Register reg, ValueNode* value);
  ValueNode* GetArgument(int i);
  ValueNode* GetInlinedArgument(int i);
  void BuildRegisterFrameInitialization(ValueNode* context = nullptr,
                                        ValueNode* closure = nullptr,
                                        ValueNode* new_target = nullptr);
  void BuildMergeStates();
  BasicBlock* EndPrologue();
  void PeelLoop();
  void BuildLoopForPeeling();

  void OsrAnalyzePrequel();

  void BuildBody() {
    while (!source_position_iterator_.done() &&
           source_position_iterator_.code_offset() < entrypoint_) {
      source_position_iterator_.Advance();
      UpdateSourceAndBytecodePosition(source_position_iterator_.code_offset());
    }
    for (iterator_.SetOffset(entrypoint_); !iterator_.done();
         iterator_.Advance()) {
      local_isolate_->heap()->Safepoint();
      if (V8_UNLIKELY(
              loop_headers_to_peel_.Contains(iterator_.current_offset()))) {
        PeelLoop();
        DCHECK_EQ(iterator_.current_bytecode(),
                  interpreter::Bytecode::kJumpLoop);
        continue;
      }
      VisitSingleBytecode();
    }
  }

  VirtualObject* CreateVirtualObjectForMerge(compiler::MapRef map,
                                             uint32_t slot_count);

  SmiConstant* GetSmiConstant(int constant) const {
    DCHECK(Smi::IsValid(constant));
    auto it = graph_->smi().find(constant);
    if (it == graph_->smi().end()) {
      SmiConstant* node =
          CreateNewConstantNode<SmiConstant>(0, Smi::FromInt(constant));
      graph_->smi().emplace(constant, node);
      return node;
    }
    return it->second;
  }

  TaggedIndexConstant* GetTaggedIndexConstant(int constant) {
    DCHECK(TaggedIndex::IsValid(constant));
    auto it = graph_->tagged_index().find(constant);
    if (it == graph_->tagged_index().end()) {
      TaggedIndexConstant* node = CreateNewConstantNode<TaggedIndexConstant>(
          0, TaggedIndex::FromIntptr(constant));
      graph_->tagged_index().emplace(constant, node);
      return node;
    }
    return it->second;
  }

  Int32Constant* GetInt32Constant(int32_t constant) {
    auto it = graph_->int32().find(constant);
    if (it == graph_->int32().end()) {
      Int32Constant* node = CreateNewConstantNode<Int32Constant>(0, constant);
      graph_->int32().emplace(constant, node);
      return node;
    }
    return it->second;
  }

  Uint32Constant* GetUint32Constant(int constant) {
    auto it = graph_->uint32().find(constant);
    if (it == graph_->uint32().end()) {
      Uint32Constant* node = CreateNewConstantNode<Uint32Constant>(0, constant);
      graph_->uint32().emplace(constant, node);
      return node;
    }
    return it->second;
  }

  Float64Constant* GetFloat64Constant(double constant) {
    return GetFloat64Constant(
        Float64::FromBits(base::double_to_uint64(constant)));
  }

  Float64Constant* GetFloat64Constant(Float64 constant) {
    auto it = graph_->float64().find(constant.get_bits());
    if (it == graph_->float64().end()) {
      Float64Constant* node =
          CreateNewConstantNode<Float64Constant>(0, constant);
      graph_->float64().emplace(constant.get_bits(), node);
      return node;
    }
    return it->second;
  }

  ValueNode* GetNumberConstant(double constant);

  static compiler::OptionalHeapObjectRef TryGetConstant(
      compiler::JSHeapBroker* broker, LocalIsolate* isolate, ValueNode* node);

  Graph* graph() const { return graph_; }
  Zone* zone() const { return compilation_unit_->zone(); }
  MaglevCompilationUnit* compilation_unit() const { return compilation_unit_; }
  const InterpreterFrameState& current_interpreter_frame() const {
    return current_interpreter_frame_;
  }
  MaglevGraphBuilder* parent() const { return parent_; }
  const DeoptFrameScope* current_deopt_scope() const {
    return current_deopt_scope_;
  }
  compiler::JSHeapBroker* broker() const { return broker_; }
  LocalIsolate* local_isolate() const { return local_isolate_; }

  bool has_graph_labeller() const {
    return compilation_unit_->has_graph_labeller();
  }
  MaglevGraphLabeller* graph_labeller() const {
    return compilation_unit_->graph_labeller();
  }

  DeoptFrame GetLatestCheckpointedFrame();

  bool need_checkpointed_loop_entry() {
    return v8_flags.maglev_speculative_hoist_phi_untagging ||
           v8_flags.maglev_licm;
  }

  void RecordUseReprHint(Phi* phi, UseRepresentationSet reprs) {
    phi->RecordUseReprHint(reprs, iterator_.current_offset());
  }
  void RecordUseReprHint(Phi* phi, UseRepresentation repr) {
    RecordUseReprHint(phi, UseRepresentationSet{repr});
  }
  void RecordUseReprHintIfPhi(ValueNode* node, UseRepresentation repr) {
    if (Phi* phi = node->TryCast<Phi>()) {
      RecordUseReprHint(phi, repr);
    }
  }

 private:
  // Helper class for building a subgraph with its own control flow, that is not
  // attached to any bytecode.
  //
  // It does this by creating a fake dummy compilation unit and frame state, and
  // wrapping up all the places where it pretends to be interpreted but isn't.
  class MaglevSubGraphBuilder {
   public:
    class Variable;
    class Label;
    class LoopLabel;

    MaglevSubGraphBuilder(MaglevGraphBuilder* builder, int variable_count);
    LoopLabel BeginLoop(std::initializer_list<Variable*> loop_vars);
    // TODO(victorgomes): Change GotoIFXXX to get a lambda returning a
    // BranchResult.
    template <typename ControlNodeT, typename... Args>
    void GotoIfTrue(Label* true_target,
                    std::initializer_list<ValueNode*> control_inputs,
                    Args&&... args);
    template <typename ControlNodeT, typename... Args>
    void GotoIfFalse(Label* false_target,
                     std::initializer_list<ValueNode*> control_inputs,
                     Args&&... args);
    void GotoOrTrim(Label* label);
    void Goto(Label* label);
    void ReducePredecessorCount(Label* label, unsigned num = 1);
    void EndLoop(LoopLabel* loop_label);
    void Bind(Label* label);
    V8_NODISCARD ReduceResult TrimPredecessorsAndBind(Label* label);
    void set(Variable& var, ValueNode* value);
    ValueNode* get(const Variable& var) const;

    void MergeIntoLabel(Label* label, BasicBlock* predecessor);

   private:
    class BorrowParentKnownNodeAspectsAndVOs;
    void TakeKnownNodeAspectsAndVOsFromParent();
    void MoveKnownNodeAspectsAndVOsToParent();

    MaglevGraphBuilder* builder_;
    MaglevCompilationUnit* compilation_unit_;
    InterpreterFrameState pseudo_frame_;
  };

  // TODO(olivf): Currently identifying dead code relies on the fact that loops
  // must be entered through the loop header by at least one of the
  // predecessors. We might want to re-evaluate this in case we want to be able
  // to OSR into nested loops while compiling the full continuation.
  static constexpr bool kLoopsMustBeEnteredThroughHeader = true;

  class CallSpeculationScope;
  class SaveCallSpeculationScope;

  bool CheckStaticType(ValueNode* node, NodeType type, NodeType* old = nullptr);
  bool CheckType(ValueNode* node, NodeType type, NodeType* old = nullptr);
  NodeType CheckTypes(ValueNode* node, std::initializer_list<NodeType> types);
  bool EnsureType(ValueNode* node, NodeType type, NodeType* old = nullptr);
  NodeType GetType(ValueNode* node);
  NodeInfo* GetOrCreateInfoFor(ValueNode* node) {
    return known_node_aspects().GetOrCreateInfoFor(node, broker(),
                                                   local_isolate());
  }

  // Returns true if we statically know that {lhs} and {rhs} have different
  // types.
  bool HaveDifferentTypes(ValueNode* lhs, ValueNode* rhs);
  bool HasDifferentType(ValueNode* lhs, NodeType rhs_type);

  template <typename Function>
  bool EnsureType(ValueNode* node, NodeType type, Function ensure_new_type);
  bool MayBeNullOrUndefined(ValueNode* node);

  void SetKnownValue(ValueNode* node, compiler::ObjectRef constant,
                     NodeType new_node_type);
  bool ShouldEmitInterruptBudgetChecks() {
    if (is_inline()) return false;
    if (compilation_unit()->info()->for_turboshaft_frontend()) {
      // As the top-tier compiler, Turboshaft doesn't need interrupt budget
      // checks.
      return false;
    }
    return v8_flags.force_emit_interrupt_budget_checks || v8_flags.turbofan;
  }
  bool ShouldEmitOsrInterruptBudgetChecks() {
    if (!v8_flags.turbofan || !v8_flags.use_osr || !v8_flags.osr_from_maglev)
      return false;
    if (!graph_->is_osr() && !v8_flags.always_osr_from_maglev) return false;
    // TODO(olivf) OSR from maglev requires lazy recompilation (see
    // CompileOptimizedOSRFromMaglev for details). Without this we end up in
    // deopt loops, e.g., in chromium content_unittests.
    if (!OptimizingCompileDispatcher::Enabled()) {
      return false;
    }
    // TODO(olivf) OSR'ing from inlined loops is something we might want, but
    // can't with our current osr-from-maglev implementation. The reason is that
    // we OSR up by first going down to the interpreter. For inlined loops this
    // means we would deoptimize to the caller and then probably end up in the
    // same maglev osr code again, before reaching the turbofan OSR code in the
    // callee. The solution is to support osr from maglev without
    // deoptimization.
    return !(graph_->is_osr() && is_inline());
  }
  bool MaglevIsTopTier() const { return !v8_flags.turbofan && v8_flags.maglev; }
  BasicBlock* CreateEdgeSplitBlock(BasicBlockRef& jump_targets,
                                   BasicBlock* predecessor) {
    if (v8_flags.trace_maglev_graph_building) {
      std::cout << "== New empty block ==" << std::endl;
      PrintVirtualObjects();
    }
    DCHECK_NULL(current_block_);
    current_block_ = zone()->New<BasicBlock>(nullptr, zone());
    BasicBlock* result = FinishBlock<Jump>({}, &jump_targets);
    result->set_edge_split_block(predecessor);
#ifdef DEBUG
    new_nodes_.clear();
#endif
    return result;
  }

  void ProcessMergePointAtExceptionHandlerStart(int offset) {
    MergePointInterpreterFrameState& merge_state = *merge_states_[offset];
    DCHECK_EQ(merge_state.predecessor_count(), 0);

    // Copy state.
    current_interpreter_frame_.CopyFrom(*compilation_unit_, merge_state);
    // Expressions would have to be explicitly preserved across exceptions.
    // However, at this point we do not know which ones might be used.
    current_interpreter_frame_.known_node_aspects()
        ->ClearAvailableExpressions();

    // Merges aren't simple fallthroughs, so we should reset the checkpoint
    // validity.
    ResetBuilderCachedState();

    // Register exception phis.
    if (has_graph_labeller()) {
      for (Phi* phi : *merge_states_[offset]->phis()) {
        graph_labeller()->RegisterNode(phi, compilation_unit_,
                                       BytecodeOffset(offset),
                                       current_source_position_);
        if (v8_flags.trace_maglev_graph_building) {
          std::cout << "  " << phi << "  "
                    << PrintNodeLabel(graph_labeller(), phi) << ": "
                    << PrintNode(graph_labeller(), phi) << std::endl;
        }
      }
    }
  }

  void ProcessMergePoint(int offset, bool preserve_known_node_aspects) {
    // First copy the merge state to be the current state.
    MergePointInterpreterFrameState& merge_state = *merge_states_[offset];
    current_interpreter_frame_.CopyFrom(*compilation_unit_, merge_state,
                                        preserve_known_node_aspects, zone());

    ProcessMergePointPredecessors(merge_state, jump_targets_[offset]);
  }

  // Splits incoming critical edges and labels predecessors.
  void ProcessMergePointPredecessors(
      MergePointInterpreterFrameState& merge_state,
      BasicBlockRef& jump_targets) {
    // Merges aren't simple fallthroughs, so we should reset state which is
    // cached directly on the builder instead of on the merge states.
    ResetBuilderCachedState();

    // TODO(victorgomes:349923027): Remove these checks after bug is fixed.
    if (merge_state.is_loop()) {
      CHECK_EQ(merge_state.predecessors_so_far(),
               merge_state.predecessor_count() - 1);
    } else {
      CHECK_EQ(merge_state.predecessors_so_far(),
               merge_state.predecessor_count());
    }

    if (merge_state.predecessor_count() == 1) return;

    // Set up edge-split.
    int predecessor_index = merge_state.predecessor_count() - 1;
    if (merge_state.is_loop()) {
      // For loops, the JumpLoop block hasn't been generated yet, and so isn't
      // in the list of jump targets. IT's the last predecessor, so drop the
      // index by one.
      DCHECK(merge_state.is_unmerged_loop());
      predecessor_index--;
    }
    BasicBlockRef* old_jump_targets = jump_targets.Reset();
    while (old_jump_targets != nullptr) {
      BasicBlock* predecessor = merge_state.predecessor_at(predecessor_index);
      CHECK(predecessor);
      ControlNode* control = predecessor->control_node();
      if (control->Is<ConditionalControlNode>()) {
        // CreateEmptyBlock automatically registers itself with the offset.
        predecessor = CreateEdgeSplitBlock(jump_targets, predecessor);
        // Set the old predecessor's (the conditional block) reference to
        // point to the new empty predecessor block.
        old_jump_targets =
            old_jump_targets->SetToBlockAndReturnNext(predecessor);
        merge_state.set_predecessor_at(predecessor_index, predecessor);
      } else {
        // Re-register the block in the offset's ref list.
        old_jump_targets = old_jump_targets->MoveToRefList(&jump_targets);
      }
      // We only set the predecessor id after splitting critical edges, to make
      // sure the edge split blocks pick up the correct predecessor index.
      predecessor->set_predecessor_id(predecessor_index--);
    }
    DCHECK_EQ(predecessor_index, -1);
    RegisterPhisWithGraphLabeller(merge_state);
  }

  void RegisterPhisWithGraphLabeller(
      MergePointInterpreterFrameState& merge_state) {
    if (!has_graph_labeller()) return;

    for (Phi* phi : *merge_state.phis()) {
      graph_labeller()->RegisterNode(phi);
      if (v8_flags.trace_maglev_graph_building) {
        std::cout << "  " << phi << "  "
                  << PrintNodeLabel(graph_labeller(), phi) << ": "
                  << PrintNode(graph_labeller(), phi) << std::endl;
      }
    }
  }

  // Return true if the given offset is a merge point, i.e. there are jumps
  // targetting it.
  bool IsOffsetAMergePoint(int offset) {
    return merge_states_[offset] != nullptr;
  }

  // Called when a block is killed by an unconditional eager deopt.
  ReduceResult EmitUnconditionalDeopt(DeoptimizeReason reason) {
    current_block_->set_deferred(true);
    // Create a block rather than calling finish, since we don't yet know the
    // next block's offset before the loop skipping the rest of the bytecodes.
    FinishBlock<Deopt>({}, reason);
    return ReduceResult::DoneWithAbort();
  }

  void KillPeeledLoopTargets(int peelings) {
    DCHECK_EQ(iterator_.current_bytecode(), interpreter::Bytecode::kJumpLoop);
    int target = iterator_.GetJumpTargetOffset();
    // Since we ended up not peeling we must kill all the doubly accounted
    // jumps out of the loop.
    interpreter::BytecodeArrayIterator iterator(bytecode().object());
    for (iterator.SetOffset(target);
         iterator.current_offset() < iterator_.current_offset();
         iterator.Advance()) {
      interpreter::Bytecode bc = iterator.current_bytecode();
      DCHECK_NE(bc, interpreter::Bytecode::kJumpLoop);
      int kill = -1;
      if (interpreter::Bytecodes::IsJump(bc) &&
          iterator.GetJumpTargetOffset() > iterator_.current_offset()) {
        kill = iterator.GetJumpTargetOffset();
      } else if (is_inline() && interpreter::Bytecodes::Returns(bc)) {
        kill = inline_exit_offset();
      }
      if (kill != -1) {
        if (!merge_states_[kill]) {
          predecessors_[kill] -= peelings;
        } else {
          for (int i = 0; i < peelings; ++i) {
            merge_states_[kill]->MergeDead(*compilation_unit_);
          }
        }
      }
    }
  }

  void MarkBytecodeDead() {
    DCHECK_NULL(current_block_);
    if (v8_flags.trace_maglev_graph_building) {
      std::cout << "== Dead ==\n"
                << std::setw(4) << iterator_.current_offset() << " : ";
      interpreter::BytecodeDecoder::Decode(std::cout,
                                           iterator_.current_address());
      std::cout << std::endl;
    }

    // If the current bytecode is a jump to elsewhere, then this jump is
    // also dead and we should make sure to merge it as a dead predecessor.
    interpreter::Bytecode bytecode = iterator_.current_bytecode();
    if (interpreter::Bytecodes::IsForwardJump(bytecode)) {
      // Jumps merge into their target, and conditional jumps also merge into
      // the fallthrough.
      MergeDeadIntoFrameState(iterator_.GetJumpTargetOffset());
      if (interpreter::Bytecodes::IsConditionalJump(bytecode)) {
        MergeDeadIntoFrameState(iterator_.next_offset());
      }
    } else if (bytecode == interpreter::Bytecode::kJumpLoop) {
      // JumpLoop merges into its loop header, which has to be treated
      // specially by the merge.
      if (!in_peeled_iteration() || in_optimistic_peeling_iteration()) {
        MergeDeadLoopIntoFrameState(iterator_.GetJumpTargetOffset());
      }
    } else if (interpreter::Bytecodes::IsSwitch(bytecode)) {
      // Switches merge into their targets, and into the fallthrough.
      for (auto offset : iterator_.GetJumpTableTargetOffsets()) {
        MergeDeadIntoFrameState(offset.target_offset);
      }
      MergeDeadIntoFrameState(iterator_.next_offset());
    } else if (!interpreter::Bytecodes::Returns(bytecode) &&
               !interpreter::Bytecodes::UnconditionallyThrows(bytecode)) {
      // Any other bytecode that doesn't return or throw will merge into the
      // fallthrough.
      MergeDeadIntoFrameState(iterator_.next_offset());
    } else if (interpreter::Bytecodes::Returns(bytecode) && is_inline()) {
      MergeDeadIntoFrameState(inline_exit_offset());
    }

    // TODO(leszeks): We could now continue iterating the bytecode
  }

  void UpdateSourceAndBytecodePosition(int offset) {
    if (source_position_iterator_.done()) return;
    if (source_position_iterator_.code_offset() == offset) {
      current_source_position_ = SourcePosition(
          source_position_iterator_.source_position().ScriptOffset(),
          inlining_id_);
      source_position_iterator_.Advance();
    } else {
      DCHECK_GT(source_position_iterator_.code_offset(), offset);
    }
  }

  void PrintVirtualObjects() {
    if (!v8_flags.trace_maglev_graph_building) return;
    current_interpreter_frame_.virtual_objects().Print(
        std::cout, "* VOs (Interpreter Frame State): ",
        compilation_unit()->graph_labeller());
  }

  void VisitSingleBytecode() {
    if (v8_flags.trace_maglev_graph_building) {
      std::cout << std::setw(4) << iterator_.current_offset() << " : ";
      interpreter::BytecodeDecoder::Decode(std::cout,
                                           iterator_.current_address());
      std::cout << std::endl;
    }

    int offset = iterator_.current_offset();
    UpdateSourceAndBytecodePosition(offset);

    MergePointInterpreterFrameState* merge_state = merge_states_[offset];
    if (V8_UNLIKELY(merge_state != nullptr)) {
      bool preserve_known_node_aspects = in_optimistic_peeling_iteration() &&
                                         loop_headers_to_peel_.Contains(offset);
      if (merge_state->is_resumable_loop()) {
        current_for_in_state.enum_cache_indices = nullptr;
      }
      if (current_block_ != nullptr) {
        DCHECK(!preserve_known_node_aspects);
        // TODO(leszeks): Re-evaluate this DCHECK, we might hit it if the only
        // bytecodes in this basic block were only register juggling.
        // DCHECK(!current_block_->nodes().is_empty());
        BasicBlock* predecessor;
        if (merge_state->is_loop() && !merge_state->is_resumable_loop() &&
            need_checkpointed_loop_entry()) {
          predecessor =
              FinishBlock<CheckpointedJump>({}, &jump_targets_[offset]);
        } else {
          predecessor = FinishBlock<Jump>({}, &jump_targets_[offset]);
        }
        merge_state->Merge(this, *compilation_unit_, current_interpreter_frame_,
                           predecessor);
      }
      if (v8_flags.trace_maglev_graph_building) {
        auto detail = merge_state->is_exception_handler() ? "exception handler"
                      : merge_state->is_loop()            ? "loop header"
                                                          : "merge";
        std::cout << "== New block (" << detail << " @" << merge_state
                  << ") at "
                  << compilation_unit()->shared_function_info().object()
                  << "==" << std::endl;
        PrintVirtualObjects();
      }

      if (V8_UNLIKELY(merge_state->is_exception_handler())) {
        DCHECK_EQ(predecessors_[offset], 0);
        // If we have no reference to this block, then the exception handler is
        // dead.
        if (!jump_targets_[offset].has_ref() ||
            !merge_state->exception_handler_was_used()) {
          MarkBytecodeDead();
          return;
        }
        ProcessMergePointAtExceptionHandlerStart(offset);
      } else if (merge_state->is_loop() && !merge_state->is_resumable_loop() &&
                 merge_state->is_unreachable_loop()) {
        // We encoutered a loop header that is only reachable by the JumpLoop
        // back-edge, but the bytecode_analysis didn't notice upfront. This can
        // e.g. be a loop that is entered on a dead fall-through.
        static_assert(kLoopsMustBeEnteredThroughHeader);
        MarkBytecodeDead();
        return;
      } else {
        ProcessMergePoint(offset, preserve_known_node_aspects);
      }

      if (is_loop_effect_tracking_enabled() && merge_state->is_loop()) {
        BeginLoopEffects(offset);
      }
      // We pass nullptr for the `predecessor` argument of StartNewBlock because
      // this block is guaranteed to have a merge_state_, and hence to not have
      // a `predecessor_` field.
      StartNewBlock(offset, /*predecessor*/ nullptr);
    } else if (V8_UNLIKELY(current_block_ == nullptr)) {
      // If we don't have a current block, the bytecode must be dead (because of
      // some earlier deopt). Mark this bytecode dead too and return.
      // TODO(leszeks): Merge these two conditions by marking dead states with
      // a sentinel value.
#ifdef DEBUG
      if (predecessors_[offset] == 1) {
        DCHECK(bytecode_analysis().IsLoopHeader(offset));
        DCHECK_NULL(merge_state);
      } else {
        DCHECK_EQ(predecessors_[offset], 0);
      }
#endif
      MarkBytecodeDead();
      return;
    }

    // Handle exceptions if we have a table.
    if (bytecode().handler_table_size() > 0) {
      // Pop all entries where offset >= end.
      while (IsInsideTryBlock()) {
        HandlerTableEntry& entry = catch_block_stack_.top();
        if (offset < entry.end) break;
        catch_block_stack_.pop();
      }
      // Push new entries from interpreter handler table where offset >= start
      // && offset < end.
      HandlerTable table(*bytecode().object());
      while (next_handler_table_index_ < table.NumberOfRangeEntries()) {
        int start = table.GetRangeStart(next_handler_table_index_);
        if (offset < start) break;
        int end = table.GetRangeEnd(next_handler_table_index_);
        if (offset >= end) {
          next_handler_table_index_++;
          continue;
        }
        int handler = table.GetRangeHandler(next_handler_table_index_);
        catch_block_stack_.push({end, handler});
        DCHECK_NOT_NULL(merge_states_[handler]);
        next_handler_table_index_++;
      }
    }

    DCHECK_NOT_NULL(current_block_);
#ifdef DEBUG
    // Clear new nodes for the next VisitFoo
    new_nodes_.clear();
#endif

    if (iterator_.current_bytecode() == interpreter::Bytecode::kJumpLoop &&
        iterator_.GetJumpTargetOffset() < entrypoint_) {
      static_assert(kLoopsMustBeEnteredThroughHeader);
      RETURN_VOID_ON_ABORT(
          EmitUnconditionalDeopt(DeoptimizeReason::kOSREarlyExit));
    }

    switch (iterator_.current_bytecode()) {
#define BYTECODE_CASE(name, ...)       \
  case interpreter::Bytecode::k##name: \
    Visit##name();                     \
    break;
      BYTECODE_LIST(BYTECODE_CASE)
#undef BYTECODE_CASE
    }
  }

#define BYTECODE_VISITOR(name, ...) void Visit##name();
  BYTECODE_LIST(BYTECODE_VISITOR)
#undef BYTECODE_VISITOR

#define DECLARE_VISITOR(name, ...) \
  void VisitIntrinsic##name(interpreter::RegisterList args);
  INTRINSICS_LIST(DECLARE_VISITOR)
#undef DECLARE_VISITOR

  void AddInitializedNodeToGraph(Node* node) {
    // VirtualObjects should never be add to the Maglev graph.
    DCHECK(!node->Is<VirtualObject>());
    current_block_->nodes().Add(node);
    node->set_owner(current_block_);
    if (has_graph_labeller())
      graph_labeller()->RegisterNode(node, compilation_unit_,
                                     BytecodeOffset(iterator_.current_offset()),
                                     current_source_position_);
    if (v8_flags.trace_maglev_graph_building) {
      std::cout << "  " << node << "  "
                << PrintNodeLabel(graph_labeller(), node) << ": "
                << PrintNode(graph_labeller(), node) << std::endl;
    }
#ifdef DEBUG
    new_nodes_.insert(node);
#endif
  }

  // Add a new node with a dynamic set of inputs which are initialized by the
  // `post_create_input_initializer` function before the node is added to the
  // graph.
  template <typename NodeT, typename Function, typename... Args>
  NodeT* AddNewNode(size_t input_count,
                    Function&& post_create_input_initializer, Args&&... args) {
    NodeT* node =
        NodeBase::New<NodeT>(zone(), input_count, std::forward<Args>(args)...);
    post_create_input_initializer(node);
    return AttachExtraInfoAndAddToGraph(node);
  }

  template <typename NodeT, typename... Args>
  NodeT* AddNewNodeOrGetEquivalent(std::initializer_list<ValueNode*> raw_inputs,
                                   Args&&... args) {
    DCHECK(v8_flags.maglev_cse);
    static constexpr Opcode op = Node::opcode_of<NodeT>;
    static_assert(Node::participate_in_cse(op));
    using options_result =
        typename std::invoke_result<decltype(&NodeT::options),
                                    const NodeT>::type;
    static_assert(
        std::is_assignable<options_result, std::tuple<Args...>>::value,
        "Instruction participating in CSE needs options() returning "
        "a tuple matching the constructor arguments");
    static_assert(IsFixedInputNode<NodeT>());
    static_assert(NodeT::kInputCount <= 3);

    std::array<ValueNode*, NodeT::kInputCount> inputs;
    // Nodes with zero input count don't have kInputTypes defined.
    if constexpr (NodeT::kInputCount > 0) {
      int i = 0;
      constexpr UseReprHintRecording hint = ShouldRecordUseReprHint<NodeT>();
      for (ValueNode* raw_input : raw_inputs) {
        inputs[i] = ConvertInputTo<hint>(raw_input, NodeT::kInputTypes[i]);
        i++;
      }
      if constexpr (IsCommutativeNode(Node::opcode_of<NodeT>)) {
        static_assert(NodeT::kInputCount == 2);
        if (inputs[0] > inputs[1]) {
          std::swap(inputs[0], inputs[1]);
        }
      }
    }

    uint32_t value_number;
    {
      size_t tmp_value_number = base::hash_value(op);
      (
          [&] {
            tmp_value_number =
                fast_hash_combine(tmp_value_number, gvn_hash_value(args));
          }(),
          ...);
      for (const auto& inp : inputs) {
        tmp_value_number =
            fast_hash_combine(tmp_value_number, base::hash_value(inp));
      }
      value_number = static_cast<uint32_t>(tmp_value_number);
    }

    auto exists = known_node_aspects().available_expressions.find(value_number);
    if (exists != known_node_aspects().available_expressions.end()) {
      auto candidate = exists->second.node;
      const bool sanity_check =
          candidate->Is<NodeT>() &&
          static_cast<size_t>(candidate->input_count()) == inputs.size();
      DCHECK_IMPLIES(sanity_check,
                     (StaticPropertiesForOpcode(op) &
                      candidate->properties()) == candidate->properties());
      const bool epoch_check =
          !Node::needs_epoch_check(op) ||
          known_node_aspects().effect_epoch() <= exists->second.effect_epoch;
      if (sanity_check && epoch_check) {
        if (static_cast<NodeT*>(candidate)->options() ==
            std::tuple{std::forward<Args>(args)...}) {
          int i = 0;
          for (const auto& inp : inputs) {
            if (inp != candidate->input(i).node()) {
              break;
            }
            i++;
          }
          if (static_cast<size_t>(i) == inputs.size()) {
            return static_cast<NodeT*>(candidate);
          }
        }
      }
      if (!epoch_check) {
        known_node_aspects().available_expressions.erase(exists);
      }
    }
    NodeT* node = NodeBase::New<NodeT>(zone(), inputs.size(),
                                       std::forward<Args>(args)...);
    int i = 0;
    for (ValueNode* input : inputs) {
      DCHECK_NOT_NULL(input);
      node->set_input(i++, input);
    }
    DCHECK_EQ(node->options(), std::tuple{std::forward<Args>(args)...});
    uint32_t epoch = Node::needs_epoch_check(op)
                         ? known_node_aspects().effect_epoch()
                         : KnownNodeAspects::kEffectEpochForPureInstructions;
    if (epoch != KnownNodeAspects::kEffectEpochOverflow) {
      known_node_aspects().available_expressions[value_number] = {node, epoch};
    }
    return AttachExtraInfoAndAddToGraph(node);
  }

  // Add a new node with a static set of inputs.
  template <typename NodeT, typename... Args>
  NodeT* AddNewNode(std::initializer_list<ValueNode*> inputs, Args&&... args) {
    static_assert(IsFixedInputNode<NodeT>());
    if constexpr (Node::participate_in_cse(Node::opcode_of<NodeT>)) {
      if (v8_flags.maglev_cse) {
        return AddNewNodeOrGetEquivalent<NodeT>(inputs,
                                                std::forward<Args>(args)...);
      }
    }
    NodeT* node = NodeBase::New<NodeT>(zone(), inputs.size(),
                                       std::forward<Args>(args)...);
    SetNodeInputs(node, inputs);
    return AttachExtraInfoAndAddToGraph(node);
  }

  template <typename NodeT, typename... Args>
  NodeT* CreateNewConstantNode(Args&&... args) const {
    static_assert(IsConstantNode(Node::opcode_of<NodeT>));
    NodeT* node = NodeBase::New<NodeT>(zone(), std::forward<Args>(args)...);
    static_assert(!NodeT::kProperties.can_eager_deopt());
    static_assert(!NodeT::kProperties.can_lazy_deopt());
    static_assert(!NodeT::kProperties.can_throw());
    static_assert(!NodeT::kProperties.can_write());
    if (has_graph_labeller()) graph_labeller()->RegisterNode(node);
    if (v8_flags.trace_maglev_graph_building) {
      std::cout << "  " << node << "  "
                << PrintNodeLabel(graph_labeller(), node) << ": "
                << PrintNode(graph_labeller(), node) << std::endl;
    }
    return node;
  }

  template <typename NodeT>
  NodeT* AttachExtraInfoAndAddToGraph(NodeT* node) {
    static_assert(NodeT::kProperties.is_deopt_checkpoint() +
                      NodeT::kProperties.can_eager_deopt() +
                      NodeT::kProperties.can_lazy_deopt() <=
                  1);
    if constexpr (NodeT::kProperties.can_eager_deopt() ||
                  NodeT::kProperties.can_lazy_deopt()) {
      ClearCurrentAllocationBlock();
    }
    AttachDeoptCheckpoint(node);
    AttachEagerDeoptInfo(node);
    AttachLazyDeoptInfo(node);
    AttachExceptionHandlerInfo(node);
    AddInitializedNodeToGraph(node);
    MarkPossibleSideEffect(node);
    return node;
  }

  template <typename NodeT>
  void AttachDeoptCheckpoint(NodeT* node) {
    if constexpr (NodeT::kProperties.is_deopt_checkpoint()) {
      node->SetEagerDeoptInfo(zone(), GetLatestCheckpointedFrame());
    }
  }

  template <typename NodeT>
  void AttachEagerDeoptInfo(NodeT* node) {
    if constexpr (NodeT::kProperties.can_eager_deopt()) {
      node->SetEagerDeoptInfo(zone(), GetLatestCheckpointedFrame(),
                              current_speculation_feedback_);
    }
  }

  template <typename NodeT>
  void AttachLazyDeoptInfo(NodeT* node) {
    if constexpr (NodeT::kProperties.can_lazy_deopt()) {
      auto [register_result, register_count] = GetResultLocationAndSize();
      new (node->lazy_deopt_info()) LazyDeoptInfo(
          zone(), GetDeoptFrameForLazyDeopt(register_result, register_count),
          register_result, register_count, current_speculation_feedback_);
    }
  }

  template <typename NodeT>
  void AttachExceptionHandlerInfo(NodeT* node) {
    if constexpr (NodeT::kProperties.can_throw()) {
      CatchBlockDetails catch_block = GetCurrentTryCatchBlock();
      if (catch_block.ref) {
        if (!catch_block.state->exception_handler_was_used()) {
          // Attach an empty live exception handler to mark that there's a
          // matching catch but we'll lazy deopt if we ever throw.
          new (node->exception_handler_info()) ExceptionHandlerInfo(
              catch_block.ref, ExceptionHandlerInfo::kLazyDeopt);
          DCHECK(node->exception_handler_info()->HasExceptionHandler());
          DCHECK(node->exception_handler_info()->ShouldLazyDeopt());
          return;
        }

        new (node->exception_handler_info()) ExceptionHandlerInfo(
            catch_block.ref, CatchBlockDeoptFrameDistance());
        DCHECK(node->exception_handler_info()->HasExceptionHandler());
        DCHECK(!node->exception_handler_info()->ShouldLazyDeopt());

        // Merge the current state into the handler state.
        DCHECK_NOT_NULL(catch_block.state);
        catch_block.state->MergeThrow(
            GetCurrentCatchBlockGraphBuilder(), catch_block.unit,
            *current_interpreter_frame_.known_node_aspects(),
            current_interpreter_frame_.virtual_objects());
      } else {
        // Patch no exception handler marker.
        // TODO(victorgomes): Avoid allocating exception handler data in this
        // case.
        new (node->exception_handler_info()) ExceptionHandlerInfo();
        DCHECK(!node->exception_handler_info()->HasExceptionHandler());
      }
    }
  }

  // Bytecode iterator of the current graph builder is inside a try-block
  // region.
  bool IsInsideTryBlock() const { return catch_block_stack_.size() > 0; }

  int CatchBlockDeoptFrameDistance() const {
    if (IsInsideTryBlock()) return 0;
    DCHECK_IMPLIES(parent_catch_deopt_frame_distance_ > 0, is_inline());
    return parent_catch_deopt_frame_distance_;
  }

  struct CatchBlockDetails {
    BasicBlockRef* ref = nullptr;
    MergePointInterpreterFrameState* state = nullptr;
    const MaglevCompilationUnit* unit = nullptr;
  };

  CatchBlockDetails GetCurrentTryCatchBlock() {
    if (IsInsideTryBlock()) {
      // Inside a try-block.
      int offset = catch_block_stack_.top().handler;
      return {&jump_targets_[offset], merge_states_[offset], compilation_unit_};
    }
    DCHECK_IMPLIES(parent_catch_.ref != nullptr, is_inline());
    return parent_catch_;
  }

  MaglevGraphBuilder* GetCurrentCatchBlockGraphBuilder() {
    if (IsInsideTryBlock()) return this;
    MaglevGraphBuilder* builder = this;
    for (int depth = 0; depth < parent_catch_deopt_frame_distance_; depth++) {
      builder = builder->parent();
    }
    return builder;
  }

  bool ContextMayAlias(ValueNode* context,
                       compiler::OptionalScopeInfoRef scope_info);
  enum ContextSlotMutability { kImmutable, kMutable };
  bool TrySpecializeLoadContextSlotToFunctionContext(
      ValueNode** context, size_t* depth, int slot_index,
      ContextSlotMutability slot_mutability);
  ValueNode* LoadAndCacheContextSlot(ValueNode* context, int offset,
                                     ContextSlotMutability slot_mutability);
  void StoreAndCacheContextSlot(ValueNode* context, int offset,
                                ValueNode* value);
  ValueNode* TryGetParentContext(ValueNode* node);
  void MinimizeContextChainDepth(ValueNode** context, size_t* depth);
  void EscapeContext();
  void BuildLoadContextSlot(ValueNode* context, size_t depth, int slot_index,
                            ContextSlotMutability slot_mutability);
  void BuildStoreContextSlotHelper(ValueNode* context, size_t depth,
                                   int slot_index, ValueNode* value,
                                   bool update_side_data);
  void BuildStoreContextSlot(ValueNode* context, size_t depth, int slot_index,
                             ValueNode* value);
  void BuildStoreScriptContextSlot(ValueNode* context, size_t depth,
                                   int slot_index, ValueNode* value);

  void BuildStoreMap(ValueNode* object, compiler::MapRef map,
                     StoreMap::Kind kind);

  ValueNode* BuildExtendPropertiesBackingStore(compiler::MapRef map,
                                               ValueNode* receiver,
                                               ValueNode* property_array);

  template <Builtin kBuiltin>
  CallBuiltin* BuildCallBuiltin(std::initializer_list<ValueNode*> inputs) {
    using Descriptor = typename CallInterfaceDescriptorFor<kBuiltin>::type;
    if constexpr (Descriptor::HasContextParameter()) {
      return AddNewNode<CallBuiltin>(
          inputs.size() + 1,
          [&](CallBuiltin* call_builtin) {
            int arg_index = 0;
            for (auto* input : inputs) {
              call_builtin->set_arg(arg_index++, input);
            }
          },
          kBuiltin, GetContext());
    } else {
      return AddNewNode<CallBuiltin>(
          inputs.size(),
          [&](CallBuiltin* call_builtin) {
            int arg_index = 0;
            for (auto* input : inputs) {
              call_builtin->set_arg(arg_index++, input);
            }
          },
          kBuiltin);
    }
  }

  template <Builtin kBuiltin>
  CallBuiltin* BuildCallBuiltin(
      std::initializer_list<ValueNode*> inputs,
      compiler::FeedbackSource const& feedback,
      CallBuiltin::FeedbackSlotType slot_type = CallBuiltin::kTaggedIndex) {
    CallBuiltin* call_builtin = BuildCallBuiltin<kBuiltin>(inputs);
    call_builtin->set_feedback(feedback, slot_type);
#ifdef DEBUG
    // Check that the last parameters are kSlot and kVector.
    using Descriptor = typename CallInterfaceDescriptorFor<kBuiltin>::type;
    int slot_index = call_builtin->InputCountWithoutContext();
    int vector_index = slot_index + 1;
    DCHECK_EQ(slot_index, Descriptor::kSlot);
    // TODO(victorgomes): Rename all kFeedbackVector parameters in the builtins
    // to kVector.
    DCHECK_EQ(vector_index, Descriptor::kVector);
#endif  // DEBUG
    return call_builtin;
  }

  CallCPPBuiltin* BuildCallCPPBuiltin(
      Builtin builtin, ValueNode* target, ValueNode* new_target,
      std::initializer_list<ValueNode*> inputs) {
    DCHECK(Builtins::IsCpp(builtin));
    const size_t input_count = inputs.size() + CallCPPBuiltin::kFixedInputCount;
    return AddNewNode<CallCPPBuiltin>(
        input_count,
        [&](CallCPPBuiltin* call_builtin) {
          int arg_index = 0;
          for (auto* input : inputs) {
            call_builtin->set_arg(arg_index++, input);
          }
        },
        builtin, GetTaggedValue(target), GetTaggedValue(new_target),
        GetTaggedValue(GetContext()));
  }

  void BuildLoadGlobal(compiler::NameRef name,
                       compiler::FeedbackSource& feedback_source,
                       TypeofMode typeof_mode);

  ValueNode* BuildToString(ValueNode* value, ToString::ConversionMode mode);

  constexpr bool RuntimeFunctionCanThrow(Runtime::FunctionId function_id) {
#define BAILOUT(name, ...) \
  if (function_id == Runtime::k##name) return true;
    FOR_EACH_THROWING_INTRINSIC(BAILOUT)
#undef BAILOUT
    return false;
  }

  ReduceResult BuildCallRuntime(Runtime::FunctionId function_id,
                                std::initializer_list<ValueNode*> inputs) {
    CallRuntime* result = AddNewNode<CallRuntime>(
        inputs.size() + CallRuntime::kFixedInputCount,
        [&](CallRuntime* call_runtime) {
          int arg_index = 0;
          for (auto* input : inputs) {
            call_runtime->set_arg(arg_index++, GetTaggedValue(input));
          }
        },
        function_id, GetContext());

    if (RuntimeFunctionCanThrow(function_id)) {
      return BuildAbort(AbortReason::kUnexpectedReturnFromThrow);
    }
    return result;
  }

  ReduceResult BuildAbort(AbortReason reason) {
    // Create a block rather than calling finish, since we don't yet know the
    // next block's offset before the loop skipping the rest of the bytecodes.
    FinishBlock<Abort>({}, reason);
    return ReduceResult::DoneWithAbort();
  }

  void Print(const char* str) {
    Handle<String> string_handle =
        local_isolate()->factory()->NewStringFromAsciiChecked(
            str, AllocationType::kOld);
    ValueNode* string_node = GetConstant(MakeRefAssumeMemoryFence(
        broker(), broker()->CanonicalPersistentHandle(string_handle)));
    CHECK(BuildCallRuntime(Runtime::kGlobalPrint, {string_node}).IsDone());
  }

  void Print(ValueNode* value) {
    CHECK(BuildCallRuntime(Runtime::kDebugPrint, {value}).IsDone());
  }

  void Print(const char* str, ValueNode* value) {
    Print(str);
    Print(value);
  }

  ValueNode* GetClosure() const {
    return current_interpreter_frame_.get(
        interpreter::Register::function_closure());
  }

  ValueNode* GetContext() const {
    return current_interpreter_frame_.get(
        interpreter::Register::current_context());
  }

  void SetContext(ValueNode* context) {
    current_interpreter_frame_.set(interpreter::Register::current_context(),
                                   context);
  }

  FeedbackSlot GetSlotOperand(int operand_index) const {
    return iterator_.GetSlotOperand(operand_index);
  }

  uint32_t GetFlag8Operand(int operand_index) const {
    return iterator_.GetFlag8Operand(operand_index);
  }

  uint32_t GetFlag16Operand(int operand_index) const {
    return iterator_.GetFlag16Operand(operand_index);
  }

  template <class T, typename = std::enable_if_t<is_taggable_v<T>>>
  typename compiler::ref_traits<T>::ref_type GetRefOperand(int operand_index) {
    // The BytecodeArray itself was fetched by using a barrier so all reads
    // from the constant pool are safe.
    return MakeRefAssumeMemoryFence(
        broker(), broker()->CanonicalPersistentHandle(
                      Cast<T>(iterator_.GetConstantForIndexOperand(
                          operand_index, local_isolate()))));
  }

  ExternalConstant* GetExternalConstant(ExternalReference reference) {
    auto it = graph_->external_references().find(reference.address());
    if (it == graph_->external_references().end()) {
      ExternalConstant* node =
          CreateNewConstantNode<ExternalConstant>(0, reference);
      graph_->external_references().emplace(reference.address(), node);
      return node;
    }
    return it->second;
  }

  RootConstant* GetRootConstant(RootIndex index) {
    auto it = graph_->root().find(index);
    if (it == graph_->root().end()) {
      RootConstant* node = CreateNewConstantNode<RootConstant>(0, index);
      graph_->root().emplace(index, node);
      return node;
    }
    return it->second;
  }

  RootConstant* GetBooleanConstant(bool value) {
    return GetRootConstant(value ? RootIndex::kTrueValue
                                 : RootIndex::kFalseValue);
  }

  ValueNode* GetConstant(compiler::ObjectRef ref);

  ValueNode* GetTrustedConstant(compiler::HeapObjectRef ref,
                                IndirectPointerTag tag);

  ValueNode* GetRegisterInput(Register reg) {
    DCHECK(!graph_->register_inputs().has(reg));
    graph_->register_inputs().set(reg);
    return AddNewNode<RegisterInput>({}, reg);
  }

#define DEFINE_IS_ROOT_OBJECT(type, name, CamelName)               \
  bool Is##CamelName(ValueNode* value) const {                     \
    if (RootConstant* constant = value->TryCast<RootConstant>()) { \
      return constant->index() == RootIndex::k##CamelName;         \
    }                                                              \
    return false;                                                  \
  }
  ROOT_LIST(DEFINE_IS_ROOT_OBJECT)
#undef DEFINE_IS_ROOT_OBJECT

  // Move an existing ValueNode between two registers. You can pass
  // virtual_accumulator as the src or dst to move in or out of the accumulator.
  void MoveNodeBetweenRegisters(interpreter::Register src,
                                interpreter::Register dst) {
    // We shouldn't be moving newly created nodes between registers.
    DCHECK(!IsNodeCreatedForThisBytecode(current_interpreter_frame_.get(src)));
    DCHECK_NOT_NULL(current_interpreter_frame_.get(src));

    current_interpreter_frame_.set(dst, current_interpreter_frame_.get(src));
  }

  ValueNode* GetTaggedValue(ValueNode* value,
                            UseReprHintRecording record_use_repr_hint =
                                UseReprHintRecording::kRecord);
  ReduceResult GetSmiValue(ValueNode* value,
                           UseReprHintRecording record_use_repr_hint =
                               UseReprHintRecording::kRecord);

  ReduceResult GetSmiValue(interpreter::Register reg,
                           UseReprHintRecording record_use_repr_hint =
                               UseReprHintRecording::kRecord) {
    ValueNode* value = current_interpreter_frame_.get(reg);
    return GetSmiValue(value, record_use_repr_hint);
  }

  ValueNode* GetTaggedValue(interpreter::Register reg,
                            UseReprHintRecording record_use_repr_hint =
                                UseReprHintRecording::kRecord) {
    ValueNode* value = current_interpreter_frame_.get(reg);
    return GetTaggedValue(value, record_use_repr_hint);
  }

  ValueNode* GetInternalizedString(interpreter::Register reg);

  // Get an Int32 representation node whose value is equivalent to the ToInt32
  // truncation of the given node (including a ToNumber call). Only trivial
  // ToNumber is allowed -- values that are already numeric, and optionally
  // oddballs.
  //
  // Deopts if the ToNumber is non-trivial.
  ValueNode* GetTruncatedInt32ForToNumber(ValueNode* value, ToNumberHint hint);

  ValueNode* GetTruncatedInt32ForToNumber(interpreter::Register reg,
                                          ToNumberHint hint) {
    return GetTruncatedInt32ForToNumber(current_interpreter_frame_.get(reg),
                                        hint);
  }

  // Get an Int32 representation node whose value is equivalent to the ToUint8
  // truncation of the given node (including a ToNumber call). Only trivial
  // ToNumber is allowed -- values that are already numeric, and optionally
  // oddballs.
  //
  // Deopts if the ToNumber is non-trivial.
  ValueNode* GetUint8ClampedForToNumber(ValueNode* value, ToNumberHint hint);

  ValueNode* GetUint8ClampedForToNumber(interpreter::Register reg,
                                        ToNumberHint hint) {
    return GetUint8ClampedForToNumber(current_interpreter_frame_.get(reg),
                                      hint);
  }

  std::optional<int32_t> TryGetInt32Constant(ValueNode* value);
  std::optional<uint32_t> TryGetUint32Constant(ValueNode* value);

  // Get an Int32 representation node whose value is equivalent to the given
  // node.
  //
  // Deopts if the value is not exactly representable as an Int32.
  ValueNode* GetInt32(ValueNode* value);

  void EnsureInt32(ValueNode* value) {
    // Either the value is Int32 already, or we force a conversion to Int32 and
    // cache the value in its alternative representation node.
    GetInt32(value);
  }

  void EnsureInt32(interpreter::Register reg) {
    EnsureInt32(current_interpreter_frame_.get(reg));
  }

  std::optional<double> TryGetFloat64Constant(ValueNode* value,
                                              ToNumberHint hint);

  // Get a Float64 representation node whose value is equivalent to the given
  // node.
  //
  // Deopts if the value is not exactly representable as a Float64.
  ValueNode* GetFloat64(ValueNode* value);

  ValueNode* GetFloat64(interpreter::Register reg) {
    return GetFloat64(current_interpreter_frame_.get(reg));
  }

  // Get a Float64 representation node whose value is the result of ToNumber on
  // the given node. Only trivial ToNumber is allowed -- values that are already
  // numeric, and optionally oddballs.
  //
  // Deopts if the ToNumber value is not exactly representable as a Float64, or
  // the ToNumber is non-trivial.
  ValueNode* GetFloat64ForToNumber(ValueNode* value, ToNumberHint hint);

  ValueNode* GetFloat64ForToNumber(interpreter::Register reg,
                                   ToNumberHint hint) {
    return GetFloat64ForToNumber(current_interpreter_frame_.get(reg), hint);
  }

  ValueNode* GetHoleyFloat64ForToNumber(ValueNode* value, ToNumberHint hint);

  ValueNode* GetHoleyFloat64ForToNumber(interpreter::Register reg,
                                        ToNumberHint hint) {
    return GetHoleyFloat64ForToNumber(current_interpreter_frame_.get(reg),
                                      hint);
  }

  ValueNode* GetAccumulator() {
    return current_interpreter_frame_.get(
        interpreter::Register::virtual_accumulator());
  }

  ReduceResult GetAccumulatorSmi(UseReprHintRecording record_use_repr_hint =
                                     UseReprHintRecording::kRecord) {
    return GetSmiValue(interpreter::Register::virtual_accumulator(),
                       record_use_repr_hint);
  }

  ValueNode* GetAccumulatorTruncatedInt32ForToNumber(ToNumberHint hint) {
    return GetTruncatedInt32ForToNumber(
        interpreter::Register::virtual_accumulator(), hint);
  }

  ValueNode* GetAccumulatorUint8ClampedForToNumber(ToNumberHint hint) {
    return GetUint8ClampedForToNumber(
        interpreter::Register::virtual_accumulator(), hint);
  }

  ValueNode* GetAccumulatorHoleyFloat64ForToNumber(ToNumberHint hint) {
    return GetHoleyFloat64ForToNumber(
        interpreter::Register::virtual_accumulator(), hint);
  }

  ValueNode* GetSilencedNaN(ValueNode* value) {
    DCHECK_EQ(value->properties().value_representation(),
              ValueRepresentation::kFloat64);

    // We only need to check for silenced NaN in non-conversion nodes or
    // conversion from tagged, since they can't be signalling NaNs.
    if (value->properties().is_conversion()) {
      // A conversion node should have at least one input.
      DCHECK_GE(value->input_count(), 1);
      // If the conversion node is tagged, we could be reading a fabricated sNaN
      // value (built using a BufferArray for example).
      if (!value->input(0).node()->properties().is_tagged()) {
        return value;
      }
    }

    // Special case constants, since we know what they are.
    Float64Constant* constant = value->TryCast<Float64Constant>();
    if (constant) {
      constexpr double quiet_NaN = std::numeric_limits<double>::quiet_NaN();
      if (!constant->value().is_nan()) return constant;
      return GetFloat64Constant(quiet_NaN);
    }

    // Silence all other values.
    return AddNewNode<HoleyFloat64ToMaybeNanFloat64>({value});
  }

  bool IsRegisterEqualToAccumulator(int operand_index) {
    interpreter::Register source = iterator_.GetRegisterOperand(operand_index);
    return current_interpreter_frame_.get(source) ==
           current_interpreter_frame_.accumulator();
  }

  ValueNode* LoadRegister(int operand_index) {
    return current_interpreter_frame_.get(
        iterator_.GetRegisterOperand(operand_index));
  }

  ValueNode* LoadRegisterHoleyFloat64ForToNumber(int operand_index,
                                                 ToNumberHint hint) {
    return GetHoleyFloat64ForToNumber(
        iterator_.GetRegisterOperand(operand_index), hint);
  }

  template <typename NodeT>
  void SetAccumulator(NodeT* node) {
    // Accumulator stores are equivalent to stores to the virtual accumulator
    // register.
    StoreRegister(interpreter::Register::virtual_accumulator(), node);
  }

  void ClobberAccumulator() {
    DCHECK(interpreter::Bytecodes::ClobbersAccumulator(
        iterator_.current_bytecode()));
    current_interpreter_frame_.set_accumulator(
        GetRootConstant(RootIndex::kOptimizedOut));
  }

  ValueNode* GetSecondValue(ValueNode* result) {
    // GetSecondReturnedValue must be added just after a node that calls a
    // builtin that expects 2 returned values. It simply binds kReturnRegister1
    // to a value node. Since the previous node must have been a builtin
    // call, the register is available in the register allocator. No gap moves
    // would be emitted between these two nodes.
    if (result->opcode() == Opcode::kCallRuntime) {
      DCHECK_EQ(result->Cast<CallRuntime>()->ReturnCount(), 2);
    } else if (result->opcode() == Opcode::kCallBuiltin) {
      DCHECK_EQ(result->Cast<CallBuiltin>()->ReturnCount(), 2);
    } else {
      DCHECK_EQ(result->opcode(), Opcode::kForInPrepare);
    }
    // {result} must be the last node in the current block.
    DCHECK(current_block_->nodes().Contains(result));
    DCHECK_EQ(result->NextNode(), nullptr);
    return AddNewNode<GetSecondReturnedValue>({});
  }

  template <typename NodeT>
  void StoreRegister(interpreter::Register target, NodeT* value) {
    static_assert(std::is_base_of_v<ValueNode, NodeT>);
    DCHECK(HasOutputRegister(target));
    current_interpreter_frame_.set(target, value);

    // Make sure the lazy deopt info of this value, if any, is registered as
    // mutating this register.
    DCHECK_IMPLIES(value->properties().can_lazy_deopt() &&
                       IsNodeCreatedForThisBytecode(value),
                   value->lazy_deopt_info()->IsResultRegister(target));
  }

  void SetAccumulatorInBranch(ValueNode* value) {
    DCHECK_IMPLIES(value->properties().can_lazy_deopt(),
                   !IsNodeCreatedForThisBytecode(value));
    current_interpreter_frame_.set(interpreter::Register::virtual_accumulator(),
                                   value);
  }

  template <typename NodeT>
  void StoreRegisterPair(
      std::pair<interpreter::Register, interpreter::Register> target,
      NodeT* value) {
    const interpreter::Register target0 = target.first;
    const interpreter::Register target1 = target.second;

    DCHECK_EQ(interpreter::Register(target0.index() + 1), target1);
    DCHECK_EQ(value->ReturnCount(), 2);

    if (!v8_flags.maglev_cse) {
      // TODO(olivf): CSE might deduplicate this value and the one below.
      DCHECK_NE(0, new_nodes_.count(value));
    }
    DCHECK(HasOutputRegister(target0));
    current_interpreter_frame_.set(target0, value);

    ValueNode* second_value = GetSecondValue(value);
    if (!v8_flags.maglev_cse) {
      DCHECK_NE(0, new_nodes_.count(second_value));
    }
    DCHECK(HasOutputRegister(target1));
    current_interpreter_frame_.set(target1, second_value);

    // Make sure the lazy deopt info of this value, if any, is registered as
    // mutating these registers.
    DCHECK_IMPLIES(value->properties().can_lazy_deopt() &&
                       IsNodeCreatedForThisBytecode(value),
                   value->lazy_deopt_info()->IsResultRegister(target0));
    DCHECK_IMPLIES(value->properties().can_lazy_deopt() &&
                       IsNodeCreatedForThisBytecode(value),
                   value->lazy_deopt_info()->IsResultRegister(target1));
  }

  std::pair<interpreter::Register, int> GetResultLocationAndSize() const;
#ifdef DEBUG
  bool HasOutputRegister(interpreter::Register reg) const;
#endif

  DeoptFrame* GetParentDeoptFrame();
  DeoptFrame GetDeoptFrameForLazyDeopt(interpreter::Register result_location,
                                       int result_size);
  DeoptFrame GetDeoptFrameForLazyDeoptHelper(
      interpreter::Register result_location, int result_size,
      DeoptFrameScope* scope, bool mark_accumulator_dead);
  InterpretedDeoptFrame GetDeoptFrameForEntryStackCheck();

  template <typename NodeT>
  void MarkPossibleSideEffect(NodeT* node) {
    if constexpr (NodeT::kProperties.can_read() ||
                  NodeT::kProperties.can_deopt() ||
                  NodeT::kProperties.can_throw()) {
      unobserved_context_slot_stores_.clear();
    }

    // Don't do anything for nodes without side effects.
    if constexpr (!NodeT::kProperties.can_write()) return;

    if (v8_flags.maglev_cse) {
      known_node_aspects().increment_effect_epoch();
    }

    // We only need to clear unstable node aspects on the current builder, not
    // the parent, since we'll anyway copy the known_node_aspects to the parent
    // once we finish the inlined function.

    if constexpr (IsElementsArrayWrite(Node::opcode_of<NodeT>)) {
      node->ClearElementsProperties(known_node_aspects());
      if (is_loop_effect_tracking()) {
        loop_effects_->keys_cleared.insert(
            KnownNodeAspects::LoadedPropertyMapKey::Elements());
      }
    } else if constexpr (!IsSimpleFieldStore(Node::opcode_of<NodeT>) &&
                         !IsTypedArrayStore(Node::opcode_of<NodeT>)) {
      // Don't change known node aspects for simple field stores. The only
      // relevant side effect on these is writes to objects which invalidate
      // loaded properties and context slots, and we invalidate these already as
      // part of emitting the store.
      node->ClearUnstableNodeAspects(known_node_aspects());
      if (is_loop_effect_tracking()) {
        loop_effects_->unstable_aspects_cleared = true;
      }
    }

    // Simple field stores can't possibly change or migrate the map.
    static constexpr bool is_possible_map_change =
        !IsSimpleFieldStore(Node::opcode_of<NodeT>);

    // All user-observable side effects need to clear state that is cached on
    // the builder. This reset has to be propagated up through the parents.
    // TODO(leszeks): What side effects aren't observable? Maybe migrations?
    for (MaglevGraphBuilder* builder = this; builder != nullptr;
         builder = builder->parent_) {
      builder->ResetBuilderCachedState<is_possible_map_change>();
    }
  }

  template <bool is_possible_map_change = true>
  void ResetBuilderCachedState() {
    latest_checkpointed_frame_.reset();

    // If a map might have changed, then we need to re-check it for for-in.
    // TODO(leszeks): Track this on merge states / known node aspects, rather
    // than on the graph, so that it can survive control flow.
    if constexpr (is_possible_map_change) {
      current_for_in_state.receiver_needs_map_check = true;
    }
  }

  int next_offset() const {
    return iterator_.current_offset() + iterator_.current_bytecode_size();
  }
  const compiler::BytecodeLivenessState* GetInLiveness() const {
    return GetInLivenessFor(iterator_.current_offset());
  }
  const compiler::BytecodeLivenessState* GetInLivenessFor(int offset) const {
    return bytecode_analysis().GetInLivenessFor(offset);
  }
  const compiler::BytecodeLivenessState* GetOutLiveness() const {
    return GetOutLivenessFor(iterator_.current_offset());
  }
  const compiler::BytecodeLivenessState* GetOutLivenessFor(int offset) const {
    return bytecode_analysis().GetOutLivenessFor(offset);
  }

  void StartNewBlock(int offset, BasicBlock* predecessor) {
    StartNewBlock(predecessor, merge_states_[offset], jump_targets_[offset]);
  }

  void StartNewBlock(BasicBlock* predecessor,
                     MergePointInterpreterFrameState* merge_state,
                     BasicBlockRef& refs_to_block) {
    DCHECK_NULL(current_block_);
    current_block_ = zone()->New<BasicBlock>(merge_state, zone());
    if (merge_state == nullptr) {
      DCHECK_NOT_NULL(predecessor);
      current_block_->set_predecessor(predecessor);
    } else {
      merge_state->InitializeWithBasicBlock(current_block_);
    }
    refs_to_block.Bind(current_block_);
  }

  template <UseReprHintRecording hint = UseReprHintRecording::kRecord>
  ValueNode* ConvertInputTo(ValueNode* input, ValueRepresentation expected) {
    ValueRepresentation repr = input->properties().value_representation();
    if (repr == expected) return input;
    switch (expected) {
      case ValueRepresentation::kTagged:
        return GetTaggedValue(input, hint);
      case ValueRepresentation::kInt32:
        return GetInt32(input);
      case ValueRepresentation::kFloat64:
      case ValueRepresentation::kHoleyFloat64:
        return GetFloat64(input);
      case ValueRepresentation::kUint32:
      case ValueRepresentation::kIntPtr:
        // These conversion should be explicitly done beforehand.
        UNREACHABLE();
    }
  }

  template <typename NodeT>
  static constexpr UseReprHintRecording ShouldRecordUseReprHint() {
    // We do not record a Tagged use on Return, since they are never on the hot
    // path, and will lead to a maximum of one additional Tagging operation in
    // the worst case. This allows loop accumulator to be untagged even if they
    // are later returned.
    if constexpr (std::is_same_v<NodeT, Return>) {
      return UseReprHintRecording::kDoNotRecord;
    } else {
      return UseReprHintRecording::kRecord;
    }
  }

  template <typename NodeT>
  void SetNodeInputs(NodeT* node, std::initializer_list<ValueNode*> inputs) {
    // Nodes with zero input count don't have kInputTypes defined.
    if constexpr (NodeT::kInputCount > 0) {
      constexpr UseReprHintRecording hint = ShouldRecordUseReprHint<NodeT>();
      int i = 0;
      for (ValueNode* input : inputs) {
        DCHECK_NOT_NULL(input);
        node->set_input(i, ConvertInputTo<hint>(input, NodeT::kInputTypes[i]));
        i++;
      }
    }
  }

  template <typename ControlNodeT, typename... Args>
  BasicBlock* FinishBlock(std::initializer_list<ValueNode*> control_inputs,
                          Args&&... args) {
    ControlNodeT* control_node = NodeBase::New<ControlNodeT>(
        zone(), control_inputs.size(), std::forward<Args>(args)...);
    SetNodeInputs(control_node, control_inputs);
    AttachEagerDeoptInfo(control_node);
    AttachDeoptCheckpoint(control_node);
    static_assert(!ControlNodeT::kProperties.can_lazy_deopt());
    static_assert(!ControlNodeT::kProperties.can_throw());
    static_assert(!ControlNodeT::kProperties.can_write());
    control_node->set_owner(current_block_);
    current_block_->set_control_node(control_node);
    // Clear unobserved context slot stores when there is any controlflow.
    // TODO(olivf): More precision could be achieved by tracking dominating
    // stores within known_node_aspects. For this we could use a stack of
    // stores, which we push on split and pop on merge.
    unobserved_context_slot_stores_.clear();

    BasicBlock* block = current_block_;
    current_block_ = nullptr;

    graph()->Add(block);
    if (has_graph_labeller()) {
      graph_labeller()->RegisterNode(control_node, compilation_unit_,
                                     BytecodeOffset(iterator_.current_offset()),
                                     current_source_position_);
      graph_labeller()->RegisterBasicBlock(block);
      if (v8_flags.trace_maglev_graph_building) {
        bool kSkipTargets = true;
        std::cout << "  " << control_node << "  "
                  << PrintNodeLabel(graph_labeller(), control_node) << ": "
                  << PrintNode(graph_labeller(), control_node, kSkipTargets)
                  << std::endl;
      }
    }
    return block;
  }

  void StartFallthroughBlock(int next_block_offset, BasicBlock* predecessor) {
    // Start a new block for the fallthrough path, unless it's a merge point, in
    // which case we merge our state into it. That merge-point could also be a
    // loop header, in which case the merge state might not exist yet (if the
    // only predecessors are this path and the JumpLoop).
    DCHECK_NULL(current_block_);

    if (NumPredecessors(next_block_offset) == 1) {
      if (v8_flags.trace_maglev_graph_building) {
        std::cout << "== New block (single fallthrough) at "
                  << *compilation_unit_->shared_function_info().object()
                  << "==" << std::endl;
        PrintVirtualObjects();
      }
      StartNewBlock(next_block_offset, predecessor);
    } else {
      MergeIntoFrameState(predecessor, next_block_offset);
    }
  }

  ValueNode* GetValueOrUndefined(ValueNode* maybe_value) {
    if (maybe_value == nullptr) {
      return GetRootConstant(RootIndex::kUndefinedValue);
    }
    return maybe_value;
  }

  ValueNode* GetConvertReceiver(compiler::SharedFunctionInfoRef shared,
                                const CallArguments& args);

  compiler::OptionalHeapObjectRef TryGetConstant(
      ValueNode* node, ValueNode** constant_node = nullptr);

  template <typename LoadNode>
  ReduceResult TryBuildLoadDataView(const CallArguments& args,
                                    ExternalArrayType type);
  template <typename StoreNode, typename Function>
  ReduceResult TryBuildStoreDataView(const CallArguments& args,
                                     ExternalArrayType type,
                                     Function&& getValue);

#ifdef V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
#define CONTINUATION_PRESERVED_EMBEDDER_DATA_LIST(V) \
  V(GetContinuationPreservedEmbedderData)            \
  V(SetContinuationPreservedEmbedderData)
#else
#define CONTINUATION_PRESERVED_EMBEDDER_DATA_LIST(V)
#endif  // V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA

#define MAGLEV_REDUCED_BUILTIN(V)              \
  V(ArrayConstructor)                          \
  V(ArrayForEach)                              \
  V(ArrayIsArray)                              \
  V(ArrayIteratorPrototypeNext)                \
  V(ArrayPrototypeEntries)                     \
  V(ArrayPrototypeKeys)                        \
  V(ArrayPrototypeValues)                      \
  V(DataViewPrototypeGetInt8)                  \
  V(DataViewPrototypeSetInt8)                  \
  V(DataViewPrototypeGetInt16)                 \
  V(DataViewPrototypeSetInt16)                 \
  V(DataViewPrototypeGetInt32)                 \
  V(DataViewPrototypeSetInt32)                 \
  V(DataViewPrototypeGetFloat64)               \
  V(DataViewPrototypeSetFloat64)               \
  V(FunctionPrototypeApply)                    \
  V(FunctionPrototypeCall)                     \
  V(FunctionPrototypeHasInstance)              \
  V(ObjectPrototypeGetProto)                   \
  V(ObjectGetPrototypeOf)                      \
  V(ReflectGetPrototypeOf)                     \
  V(ObjectPrototypeHasOwnProperty)             \
  V(NumberParseInt)                            \
  V(MathCeil)                                  \
  V(MathFloor)                                 \
  V(MathPow)                                   \
  V(ArrayPrototypePush)                        \
  V(ArrayPrototypePop)                         \
  V(MathAbs)                                   \
  V(MathRound)                                 \
  V(StringConstructor)                         \
  V(StringFromCharCode)                        \
  V(StringPrototypeCharCodeAt)                 \
  V(StringPrototypeCodePointAt)                \
  V(StringPrototypeIterator)                   \
  V(StringPrototypeLocaleCompare)              \
  CONTINUATION_PRESERVED_EMBEDDER_DATA_LIST(V) \
  IEEE_754_UNARY_LIST(V)

#define DEFINE_BUILTIN_REDUCER(Name, ...)                      \
  ReduceResult TryReduce##Name(compiler::JSFunctionRef target, \
                               CallArguments& args);
  MAGLEV_REDUCED_BUILTIN(DEFINE_BUILTIN_REDUCER)
#undef DEFINE_BUILTIN_REDUCER

  ReduceResult TryReduceGetProto(ValueNode* node);

  template <typename MapKindsT, typename IndexToElementsKindFunc,
            typename BuildKindSpecificFunc>
  ReduceResult BuildJSArrayBuiltinMapSwitchOnElementsKind(
      ValueNode* receiver, const MapKindsT& map_kinds,
      MaglevSubGraphBuilder& sub_graph,
      std::optional<MaglevSubGraphBuilder::Label>& do_return,
      int unique_kind_count, IndexToElementsKindFunc&& index_to_elements_kind,
      BuildKindSpecificFunc&& build_kind_specific);

  ReduceResult DoTryReduceMathRound(CallArguments& args,
                                    Float64Round::Kind kind);

  template <typename CallNode, typename... Args>
  CallNode* AddNewCallNode(const CallArguments& args, Args&&... extra_args);

  ReduceResult TryReduceGetIterator(ValueNode* receiver, int load_slot,
                                    int call_slot);

  ValueNode* BuildCallSelf(ValueNode* context, ValueNode* function,
                           ValueNode* new_target,
                           compiler::SharedFunctionInfoRef shared,
                           CallArguments& args);
  ReduceResult TryReduceBuiltin(
      compiler::JSFunctionRef target, compiler::SharedFunctionInfoRef shared,
      CallArguments& args, const compiler::FeedbackSource& feedback_source);
  bool TargetIsCurrentCompilingUnit(compiler::JSFunctionRef target);
  ReduceResult TryBuildCallKnownJSFunction(
      compiler::JSFunctionRef function, ValueNode* new_target,
      CallArguments& args, const compiler::FeedbackSource& feedback_source);
  ReduceResult TryBuildCallKnownJSFunction(
      ValueNode* context, ValueNode* function, ValueNode* new_target,
      compiler::SharedFunctionInfoRef shared,
      compiler::OptionalFeedbackVectorRef feedback_vector, CallArguments& args,
      const compiler::FeedbackSource& feedback_source);
  bool ShouldInlineCall(compiler::SharedFunctionInfoRef shared,
                        compiler::OptionalFeedbackVectorRef feedback_vector,
                        float call_frequency);
  ReduceResult TryBuildInlinedCall(
      ValueNode* context, ValueNode* function, ValueNode* new_target,
      compiler::SharedFunctionInfoRef shared,
      compiler::OptionalFeedbackVectorRef feedback_vector, CallArguments& args,
      const compiler::FeedbackSource& feedback_source);
  ValueNode* BuildGenericCall(ValueNode* target, Call::TargetType target_type,
                              const CallArguments& args);
  ReduceResult ReduceCallForConstant(
      compiler::JSFunctionRef target, CallArguments& args,
      const compiler::FeedbackSource& feedback_source =
          compiler::FeedbackSource());
  ReduceResult ReduceCallForTarget(
      ValueNode* target_node, compiler::JSFunctionRef target,
      CallArguments& args, const compiler::FeedbackSource& feedback_source);
  ReduceResult ReduceCallForNewClosure(
      ValueNode* target_node, ValueNode* target_context,
      compiler::SharedFunctionInfoRef shared,
      compiler::OptionalFeedbackVectorRef feedback_vector, CallArguments& args,
      const compiler::FeedbackSource& feedback_source);
  ReduceResult TryBuildCallKnownApiFunction(
      compiler::JSFunctionRef function, compiler::SharedFunctionInfoRef shared,
      CallArguments& args);
  compiler::HolderLookupResult TryInferApiHolderValue(
      compiler::FunctionTemplateInfoRef function_template_info,
      ValueNode* receiver);
  ReduceResult ReduceCallForApiFunction(
      compiler::FunctionTemplateInfoRef api_callback,
      compiler::OptionalSharedFunctionInfoRef maybe_shared,
      compiler::OptionalJSObjectRef api_holder, CallArguments& args);
  ReduceResult ReduceFunctionPrototypeApplyCallWithReceiver(
      compiler::OptionalHeapObjectRef maybe_receiver, CallArguments& args,
      const compiler::FeedbackSource& feedback_source);
  ReduceResult ReduceCallWithArrayLikeForArgumentsObject(
      ValueNode* target_node, CallArguments& args,
      VirtualObject* arguments_object,
      const compiler::FeedbackSource& feedback_source);
  ReduceResult ReduceCallWithArrayLike(
      ValueNode* target_node, CallArguments& args,
      const compiler::FeedbackSource& feedback_source);
  ReduceResult ReduceCall(ValueNode* target_node, CallArguments& args,
                          const compiler::FeedbackSource& feedback_source =
                              compiler::FeedbackSource());
  void BuildCallWithFeedback(ValueNode* target_node, CallArguments& args,
                             const compiler::FeedbackSource& feedback_source);
  void BuildCallFromRegisterList(ConvertReceiverMode receiver_mode);
  void BuildCallFromRegisters(int argc_count,
                              ConvertReceiverMode receiver_mode);

  ValueNode* BuildElementsArray(int length);
  ValueNode* BuildAndAllocateKeyValueArray(ValueNode* key, ValueNode* value);
  ValueNode* BuildAndAllocateJSArray(
      compiler::MapRef map, ValueNode* length, ValueNode* elements,
      const compiler::SlackTrackingPrediction& slack_tracking_prediction,
      AllocationType allocation_type);
  ValueNode* BuildAndAllocateJSArrayIterator(ValueNode* array,
                                             IterationKind iteration_kind);

  ReduceResult TryBuildAndAllocateJSGeneratorObject(ValueNode* closure,
                                                    ValueNode* receiver);

  ValueNode* BuildGenericConstruct(
      ValueNode* target, ValueNode* new_target, ValueNode* context,
      const CallArguments& args,
      const compiler::FeedbackSource& feedback_source =
          compiler::FeedbackSource());

  ReduceResult TryReduceConstructArrayConstructor(
      compiler::JSFunctionRef array_function, CallArguments& args,
      compiler::OptionalAllocationSiteRef maybe_allocation_site = {});
  ReduceResult TryReduceConstructBuiltin(
      compiler::JSFunctionRef builtin,
      compiler::SharedFunctionInfoRef shared_function_info, ValueNode* target,
      CallArguments& args);
  ReduceResult TryReduceConstructGeneric(
      compiler::JSFunctionRef function,
      compiler::SharedFunctionInfoRef shared_function_info, ValueNode* target,
      ValueNode* new_target, CallArguments& args,
      compiler::FeedbackSource& feedback_source);
  ReduceResult TryReduceConstruct(compiler::HeapObjectRef feedback_target,
                                  ValueNode* target, ValueNode* new_target,
                                  CallArguments& args,
                                  compiler::FeedbackSource& feedback_source);
  void BuildConstruct(ValueNode* target, ValueNode* new_target,
                      CallArguments& args,
                      compiler::FeedbackSource& feedback_source);

  ReduceResult TryBuildScriptContextStore(
      const compiler::GlobalAccessFeedback& global_access_feedback);
  ReduceResult TryBuildPropertyCellStore(
      const compiler::GlobalAccessFeedback& global_access_feedback);
  ReduceResult TryBuildGlobalStore(
      const compiler::GlobalAccessFeedback& global_access_feedback);

  ReduceResult TryBuildScriptContextConstantLoad(
      const compiler::GlobalAccessFeedback& global_access_feedback);
  ReduceResult TryBuildScriptContextLoad(
      const compiler::GlobalAccessFeedback& global_access_feedback);
  ReduceResult TryBuildPropertyCellLoad(
      const compiler::GlobalAccessFeedback& global_access_feedback);
  ReduceResult TryBuildGlobalLoad(
      const compiler::GlobalAccessFeedback& global_access_feedback);

  bool TryBuildFindNonDefaultConstructorOrConstruct(
      ValueNode* this_function, ValueNode* new_target,
      std::pair<interpreter::Register, interpreter::Register> result);

  ValueNode* BuildSmiUntag(ValueNode* node);
  ValueNode* BuildNumberOrOddballToFloat64(
      ValueNode* node, TaggedToFloat64ConversionType conversion_type);

  ReduceResult BuildCheckSmi(ValueNode* object, bool elidable = true);
  void BuildCheckNumber(ValueNode* object);
  void BuildCheckHeapObject(ValueNode* object);
  void BuildCheckJSReceiver(ValueNode* object);
  void BuildCheckString(ValueNode* object);
  void BuildCheckSymbol(ValueNode* object);
  ReduceResult BuildCheckMaps(ValueNode* object,
                              base::Vector<const compiler::MapRef> maps);
  ReduceResult BuildTransitionElementsKindOrCheckMap(
      ValueNode* object, const ZoneVector<compiler::MapRef>& transition_sources,
      compiler::MapRef transition_target);
  ReduceResult BuildCompareMaps(
      ValueNode* heap_object, std::optional<ValueNode*> object_map,
      base::Vector<const compiler::MapRef> maps,
      MaglevSubGraphBuilder* sub_graph,
      std::optional<MaglevSubGraphBuilder::Label>& if_not_matched);
  ReduceResult BuildTransitionElementsKindAndCompareMaps(
      ValueNode* heap_object,
      const ZoneVector<compiler::MapRef>& transition_sources,
      compiler::MapRef transition_target, MaglevSubGraphBuilder* sub_graph,
      std::optional<MaglevSubGraphBuilder::Label>& if_not_matched);
  // Emits an unconditional deopt and returns false if the node is a constant
  // that doesn't match the ref.
  ReduceResult BuildCheckValue(ValueNode* node, compiler::ObjectRef ref);
  ReduceResult BuildCheckValue(ValueNode* node, compiler::HeapObjectRef ref);

  ValueNode* BuildConvertHoleToUndefined(ValueNode* node);
  ReduceResult BuildCheckNotHole(ValueNode* node);

  // Checks whether we're invalidating the constness of a const tracking let
  // variable, and if yes, deopts.
  void BuildCheckConstTrackingLetCell(ValueNode* context, ValueNode* value,
                                      int index);

  template <bool flip = false>
  ValueNode* BuildToBoolean(ValueNode* node);
  ValueNode* BuildLogicalNot(ValueNode* value);
  ValueNode* BuildTestUndetectable(ValueNode* value);
  void BuildToNumberOrToNumeric(Object::Conversion mode);

  enum class TrackObjectMode { kLoad, kStore };
  bool CanTrackObjectChanges(ValueNode* object, TrackObjectMode mode);
  bool CanElideWriteBarrier(ValueNode* object, ValueNode* value);

  void BuildInitializeStore(InlinedAllocation* alloc, ValueNode* value,
                            int offset);
  void TryBuildStoreTaggedFieldToAllocation(ValueNode* object, ValueNode* value,
                                            int offset);
  template <typename Instruction = LoadTaggedField, typename... Args>
  ValueNode* BuildLoadTaggedField(ValueNode* object, Args&&... args) {
    auto offset = std::get<0>(std::make_tuple(args...));
    if (offset != HeapObject::kMapOffset &&
        CanTrackObjectChanges(object, TrackObjectMode::kLoad)) {
      VirtualObject* vobject =
          GetObjectFromAllocation(object->Cast<InlinedAllocation>());
      ValueNode* value;
      CHECK_NE(vobject->type(), VirtualObject::kHeapNumber);
      if (vobject->type() == VirtualObject::kDefault) {
        value = vobject->get(offset);
      } else {
        DCHECK_EQ(vobject->type(), VirtualObject::kFixedDoubleArray);
        // The only offset we're allowed to read from the a FixedDoubleArray as
        // tagged field is the length.
        CHECK_EQ(offset, FixedDoubleArray::kLengthOffset);
        value = GetInt32Constant(vobject->double_elements_length());
      }
      if (v8_flags.trace_maglev_object_tracking) {
        std::cout << "  * Reusing value in virtual object "
                  << PrintNodeLabel(graph_labeller(), vobject) << "[" << offset
                  << "]: " << PrintNode(graph_labeller(), value) << std::endl;
      }
      return value;
    }
    return AddNewNode<Instruction>({object}, std::forward<Args>(args)...);
  }

  Node* BuildStoreTaggedField(ValueNode* object, ValueNode* value, int offset,
                              StoreTaggedMode store_mode);
  void BuildStoreTaggedFieldNoWriteBarrier(ValueNode* object, ValueNode* value,
                                           int offset,
                                           StoreTaggedMode store_mode);
  void BuildStoreTrustedPointerField(ValueNode* object, ValueNode* value,
                                     int offset, IndirectPointerTag tag,
                                     StoreTaggedMode store_mode);

  ValueNode* BuildLoadFixedArrayElement(ValueNode* elements, int index);
  ValueNode* BuildLoadFixedArrayElement(ValueNode* elements, ValueNode* index);
  void BuildStoreFixedArrayElement(ValueNode* elements, ValueNode* index,
                                   ValueNode* value);

  ValueNode* BuildLoadFixedDoubleArrayElement(ValueNode* elements, int index);
  ValueNode* BuildLoadFixedDoubleArrayElement(ValueNode* elements,
                                              ValueNode* index);
  void BuildStoreFixedDoubleArrayElement(ValueNode* elements, ValueNode* index,
                                         ValueNode* value);

  ValueNode* BuildLoadHoleyFixedDoubleArrayElement(ValueNode* elements,
                                                   ValueNode* index,
                                                   bool convert_hole);

  ValueNode* GetInt32ElementIndex(interpreter::Register reg) {
    ValueNode* index_object = current_interpreter_frame_.get(reg);
    return GetInt32ElementIndex(index_object);
  }
  ValueNode* GetInt32ElementIndex(ValueNode* index_object);

  ReduceResult GetUint32ElementIndex(interpreter::Register reg) {
    ValueNode* index_object = current_interpreter_frame_.get(reg);
    return GetUint32ElementIndex(index_object);
  }
  ReduceResult GetUint32ElementIndex(ValueNode* index_object);

  bool CanTreatHoleAsUndefined(
      base::Vector<const compiler::MapRef> const& receiver_maps);

  compiler::OptionalObjectRef TryFoldLoadDictPrototypeConstant(
      compiler::PropertyAccessInfo const& access_info);
  compiler::OptionalJSObjectRef TryGetConstantDataFieldHolder(
      compiler::PropertyAccessInfo const& access_info,
      ValueNode* lookup_start_object);
  compiler::OptionalObjectRef TryFoldLoadConstantDataField(
      compiler::JSObjectRef holder,
      compiler::PropertyAccessInfo const& access_info);
  std::optional<Float64> TryFoldLoadConstantDoubleField(
      compiler::JSObjectRef holder,
      compiler::PropertyAccessInfo const& access_info);

  // Returns the loaded value node but doesn't update the accumulator yet.
  ValueNode* BuildLoadField(compiler::PropertyAccessInfo const& access_info,
                            ValueNode* lookup_start_object,
                            compiler::NameRef name);
  ReduceResult TryBuildStoreField(
      compiler::PropertyAccessInfo const& access_info, ValueNode* receiver,
      compiler::AccessMode access_mode);
  ReduceResult TryBuildPropertyGetterCall(
      compiler::PropertyAccessInfo const& access_info, ValueNode* receiver,
      ValueNode* lookup_start_object);
  ReduceResult TryBuildPropertySetterCall(
      compiler::PropertyAccessInfo const& access_info, ValueNode* receiver,
      ValueNode* lookup_start_object, ValueNode* value);
  bool TryBuildGetKeyedPropertyWithEnumeratedKey(
      ValueNode* object, const compiler::FeedbackSource& feedback_source,
      const compiler::ProcessedFeedback& processed_feedback);
  void BuildGetKeyedProperty(
      ValueNode* object, const compiler::FeedbackSource& feedback_source,
      const compiler::ProcessedFeedback& processed_feedback);

  ValueNode* BuildLoadFixedArrayLength(ValueNode* fixed_array);
  ValueNode* BuildLoadJSArrayLength(ValueNode* js_array,
                                    NodeType length_type = NodeType::kSmi);
  ValueNode* BuildLoadElements(ValueNode* object);

  ReduceResult TryBuildCheckInt32Condition(ValueNode* lhs, ValueNode* rhs,
                                           AssertCondition condition,
                                           DeoptimizeReason reason);

  ReduceResult TryBuildPropertyLoad(
      ValueNode* receiver, ValueNode* lookup_start_object,
      compiler::NameRef name, compiler::PropertyAccessInfo const& access_info);
  ReduceResult TryBuildPropertyStore(
      ValueNode* receiver, ValueNode* lookup_start_object,
      compiler::NameRef name, compiler::PropertyAccessInfo const& access_info,
      compiler::AccessMode access_mode);
  ReduceResult TryBuildPropertyAccess(
      ValueNode* receiver, ValueNode* lookup_start_object,
      compiler::NameRef name, compiler::PropertyAccessInfo const& access_info,
      compiler::AccessMode access_mode);
  template <typename GenericAccessFunc>
  ReduceResult TryBuildNamedAccess(
      ValueNode* receiver, ValueNode* lookup_start_object,
      compiler::NamedAccessFeedback const& feedback,
      compiler::FeedbackSource const& feedback_source,
      compiler::AccessMode access_mode,
      GenericAccessFunc&& build_generic_access);

  ReduceResult TryBuildLoadNamedProperty(
      ValueNode* receiver, ValueNode* lookup_start_object,
      compiler::NameRef name, compiler::FeedbackSource& feedback_source);
  ReduceResult TryBuildLoadNamedProperty(
      ValueNode* receiver, compiler::NameRef name,
      compiler::FeedbackSource& feedback_source) {
    return TryBuildLoadNamedProperty(receiver, receiver, name, feedback_source);
  }

  ReduceResult BuildLoadTypedArrayLength(ValueNode* object,
                                         ElementsKind elements_kind);
  ValueNode* BuildLoadTypedArrayElement(ValueNode* object, ValueNode* index,
                                        ElementsKind elements_kind);
  void BuildStoreTypedArrayElement(ValueNode* object, ValueNode* index,
                                   ElementsKind elements_kind);

  ReduceResult TryBuildElementAccessOnString(
      ValueNode* object, ValueNode* index,
      compiler::KeyedAccessMode const& keyed_mode);
  ReduceResult TryBuildElementAccessOnTypedArray(
      ValueNode* object, ValueNode* index,
      const compiler::ElementAccessInfo& access_info,
      compiler::KeyedAccessMode const& keyed_mode);
  ReduceResult TryBuildElementLoadOnJSArrayOrJSObject(
      ValueNode* object, ValueNode* index,
      base::Vector<const compiler::MapRef> maps, ElementsKind kind,
      KeyedAccessLoadMode load_mode);
  ReduceResult TryBuildElementStoreOnJSArrayOrJSObject(
      ValueNode* object, ValueNode* index_object, ValueNode* value,
      base::Vector<const compiler::MapRef> maps, ElementsKind kind,
      const compiler::KeyedAccessMode& keyed_mode);
  ReduceResult TryBuildElementAccessOnJSArrayOrJSObject(
      ValueNode* object, ValueNode* index,
      const compiler::ElementAccessInfo& access_info,
      compiler::KeyedAccessMode const& keyed_mode);
  template <typename GenericAccessFunc>
  ReduceResult TryBuildElementAccess(
      ValueNode* object, ValueNode* index,
      compiler::ElementAccessFeedback const& feedback,
      compiler::FeedbackSource const& feedback_source,
      GenericAccessFunc&& build_generic_access);
  template <typename GenericAccessFunc>
  ReduceResult TryBuildPolymorphicElementAccess(
      ValueNode* object, ValueNode* index,
      const compiler::KeyedAccessMode& keyed_mode,
      const ZoneVector<compiler::ElementAccessInfo>& access_infos,
      GenericAccessFunc&& build_generic_access);
  template <typename GenericAccessFunc>
  ReduceResult TryBuildPolymorphicPropertyAccess(
      ValueNode* receiver, ValueNode* lookup_start_object,
      compiler::NamedAccessFeedback const& feedback,
      compiler::AccessMode access_mode,
      const ZoneVector<compiler::PropertyAccessInfo>& access_infos,
      GenericAccessFunc&& build_generic_access);

  // Load elimination -- when loading or storing a simple property without
  // side effects, record its value, and allow that value to be re-used on
  // subsequent loads.
  void RecordKnownProperty(ValueNode* lookup_start_object,
                           KnownNodeAspects::LoadedPropertyMapKey key,
                           ValueNode* value, bool is_const,
                           compiler::AccessMode access_mode);
  ReduceResult TryReuseKnownPropertyLoad(ValueNode* lookup_start_object,
                                         compiler::NameRef name);
  ValueNode* BuildLoadStringLength(ValueNode* string);

  // Converts the input node to a representation that's valid to store into an
  // array with elements kind |kind|.
  ReduceResult ConvertForStoring(ValueNode* node, ElementsKind kind);

  enum InferHasInPrototypeChainResult {
    kMayBeInPrototypeChain,
    kIsInPrototypeChain,
    kIsNotInPrototypeChain
  };
  InferHasInPrototypeChainResult InferHasInPrototypeChain(
      ValueNode* receiver, compiler::HeapObjectRef prototype);
  ReduceResult TryBuildFastHasInPrototypeChain(
      ValueNode* object, compiler::HeapObjectRef prototype);
  ReduceResult BuildHasInPrototypeChain(ValueNode* object,
                                        compiler::HeapObjectRef prototype);
  ReduceResult TryBuildFastOrdinaryHasInstance(ValueNode* object,
                                               compiler::JSObjectRef callable,
                                               ValueNode* callable_node);
  ReduceResult BuildOrdinaryHasInstance(ValueNode* object,
                                        compiler::JSObjectRef callable,
                                        ValueNode* callable_node);
  ReduceResult TryBuildFastInstanceOf(ValueNode* object,
                                      compiler::JSObjectRef callable_ref,
                                      ValueNode* callable_node);
  ReduceResult TryBuildFastInstanceOfWithFeedback(
      ValueNode* object, ValueNode* callable,
      compiler::FeedbackSource feedback_source);

  VirtualObject* GetObjectFromAllocation(InlinedAllocation* allocation);
  VirtualObject* GetModifiableObjectFromAllocation(
      InlinedAllocation* allocation);

  VirtualObject* DeepCopyVirtualObject(VirtualObject* vobj);
  VirtualObject* CreateVirtualObject(compiler::MapRef map,
                                     uint32_t slot_count_including_map);
  VirtualObject* CreateHeapNumber(Float64 value);
  VirtualObject* CreateDoubleFixedArray(uint32_t elements_length,
                                        compiler::FixedDoubleArrayRef elements);
  VirtualObject* CreateJSObject(compiler::MapRef map);
  VirtualObject* CreateJSArray(compiler::MapRef map, int instance_size,
                               ValueNode* length);
  VirtualObject* CreateJSArrayIterator(compiler::MapRef map,
                                       ValueNode* iterated_object,
                                       IterationKind kind);
  VirtualObject* CreateJSConstructor(compiler::JSFunctionRef constructor);
  VirtualObject* CreateFixedArray(compiler::MapRef map, int length);
  VirtualObject* CreateContext(compiler::MapRef map, int length,
                               compiler::ScopeInfoRef scope_info,
                               ValueNode* previous_context,
                               std::optional<ValueNode*> extension = {});
  VirtualObject* CreateArgumentsObject(compiler::MapRef map, ValueNode* length,
                                       ValueNode* elements,
                                       std::optional<ValueNode*> callee = {});
  VirtualObject* CreateMappedArgumentsElements(compiler::MapRef map,
                                               int mapped_count,
                                               ValueNode* context,
                                               ValueNode* unmapped_elements);
  VirtualObject* CreateRegExpLiteralObject(
      compiler::MapRef map, compiler::RegExpBoilerplateDescriptionRef literal);
  VirtualObject* CreateJSGeneratorObject(compiler::MapRef map,
                                         int instance_size, ValueNode* context,
                                         ValueNode* closure,
                                         ValueNode* receiver,
                                         ValueNode* register_file);
  VirtualObject* CreateJSIteratorResult(compiler::MapRef map, ValueNode* value,
                                        ValueNode* done);
  VirtualObject* CreateJSStringIterator(compiler::MapRef map,
                                        ValueNode* string);

  InlinedAllocation* ExtendOrReallocateCurrentAllocationBlock(
      AllocationType allocation_type, VirtualObject* value);

  void ClearCurrentAllocationBlock();

  inline void AddDeoptUse(ValueNode* node) {
    if (node == nullptr) return;
    DCHECK(!node->Is<VirtualObject>());
    if (InlinedAllocation* alloc = node->TryCast<InlinedAllocation>()) {
      VirtualObject* vobject =
          current_interpreter_frame_.virtual_objects().FindAllocatedWith(alloc);
      CHECK_NOT_NULL(vobject);
      AddDeoptUse(vobject);
      // Add an escaping use for the allocation.
      AddNonEscapingUses(alloc, 1);
      alloc->add_use();
    } else {
      node->add_use();
    }
  }
  void AddDeoptUse(VirtualObject* alloc);
  void AddNonEscapingUses(InlinedAllocation* allocation, int use_count);

  std::optional<VirtualObject*> TryGetNonEscapingArgumentsObject(
      ValueNode* value);

  ReduceResult TryBuildFastCreateObjectOrArrayLiteral(
      const compiler::LiteralFeedback& feedback);
  std::optional<VirtualObject*> TryReadBoilerplateForFastLiteral(
      compiler::JSObjectRef boilerplate, AllocationType allocation,
      int max_depth, int* max_properties);

  ValueNode* BuildInlinedAllocationForHeapNumber(VirtualObject* object,
                                                 AllocationType allocation);
  ValueNode* BuildInlinedAllocationForDoubleFixedArray(
      VirtualObject* object, AllocationType allocation);
  ValueNode* BuildInlinedAllocation(VirtualObject* object,
                                    AllocationType allocation);
  ValueNode* BuildInlinedArgumentsElements(int start_index, int length);
  ValueNode* BuildInlinedUnmappedArgumentsElements(int mapped_count);

  template <CreateArgumentsType type>
  VirtualObject* BuildVirtualArgumentsObject();
  template <CreateArgumentsType type>
  ValueNode* BuildAndAllocateArgumentsObject();

  bool CanAllocateSloppyArgumentElements();
  bool CanAllocateInlinedArgumentElements();

  ReduceResult TryBuildInlinedAllocatedContext(compiler::MapRef map,
                                               compiler::ScopeInfoRef scope,
                                               int context_length);

  template <Operation kOperation>
  void BuildGenericUnaryOperationNode();
  template <Operation kOperation>
  void BuildGenericBinaryOperationNode();
  template <Operation kOperation>
  void BuildGenericBinarySmiOperationNode();

  template <Operation kOperation>
  bool TryReduceCompareEqualAgainstConstant();

  template <Operation kOperation>
  ReduceResult TryFoldInt32UnaryOperation(ValueNode* value);
  template <Operation kOperation>
  ReduceResult TryFoldInt32BinaryOperation(ValueNode* left, ValueNode* right);
  template <Operation kOperation>
  ReduceResult TryFoldInt32BinaryOperation(ValueNode* left, int32_t cst_right);

  template <Operation kOperation>
  void BuildInt32UnaryOperationNode();
  void BuildTruncatingInt32BitwiseNotForToNumber(ToNumberHint hint);
  template <Operation kOperation>
  void BuildInt32BinaryOperationNode();
  template <Operation kOperation>
  void BuildInt32BinarySmiOperationNode();
  template <Operation kOperation>
  void BuildTruncatingInt32BinaryOperationNodeForToNumber(ToNumberHint hint);
  template <Operation kOperation>
  void BuildTruncatingInt32BinarySmiOperationNodeForToNumber(ToNumberHint hint);

  template <Operation kOperation>
  ReduceResult TryFoldFloat64UnaryOperationForToNumber(ToNumberHint hint,
                                                       ValueNode* value);
  template <Operation kOperation>
  ReduceResult TryFoldFloat64BinaryOperationForToNumber(ToNumberHint hint,
                                                        ValueNode* left,
                                                        ValueNode* right);
  template <Operation kOperation>
  ReduceResult TryFoldFloat64BinaryOperationForToNumber(ToNumberHint hint,
                                                        ValueNode* left,
                                                        double cst_right);

  template <Operation kOperation>
  void BuildFloat64UnaryOperationNodeForToNumber(ToNumberHint hint);
  template <Operation kOperation>
  void BuildFloat64BinaryOperationNodeForToNumber(ToNumberHint hint);
  template <Operation kOperation>
  void BuildFloat64BinarySmiOperationNodeForToNumber(ToNumberHint hint);

  template <Operation kOperation>
  void VisitUnaryOperation();
  template <Operation kOperation>
  void VisitBinaryOperation();
  template <Operation kOperation>
  void VisitBinarySmiOperation();

  template <Operation kOperation>
  void VisitCompareOperation();

  using TypeOfLiteralFlag = interpreter::TestTypeOfFlags::LiteralFlag;
  template <typename Function>
  ReduceResult TryReduceTypeOf(ValueNode* value, const Function& GetResult);
  ReduceResult TryReduceTypeOf(ValueNode* value);

  void BeginLoopEffects(int loop_header);
  void EndLoopEffects(int loop_header);
  void MergeIntoFrameState(BasicBlock* block, int target);
  void MergeDeadIntoFrameState(int target);
  void MergeDeadLoopIntoFrameState(int target);
  void MergeIntoInlinedReturnFrameState(BasicBlock* block);

  bool HasValidInitialMap(compiler::JSFunctionRef new_target,
                          compiler::JSFunctionRef constructor);

  ValueNode* BuildTaggedEqual(ValueNode* lhs, ValueNode* rhs);
  ValueNode* BuildTaggedEqual(ValueNode* lhs, RootIndex rhs_index);

  class BranchBuilder;

  enum class BranchType { kBranchIfTrue, kBranchIfFalse };
  enum class BranchSpecializationMode { kDefault, kAlwaysBoolean };
  enum class BranchResult {
    kDefault,
    kAlwaysTrue,
    kAlwaysFalse,
  };

  static inline BranchType NegateBranchType(BranchType jump_type) {
    switch (jump_type) {
      case BranchType::kBranchIfTrue:
        return BranchType::kBranchIfFalse;
      case BranchType::kBranchIfFalse:
        return BranchType::kBranchIfTrue;
    }
  }

  // This class encapsulates the logic of branch nodes (using the graph builder
  // or the sub graph builder).
  class BranchBuilder {
   public:
    enum Mode {
      kBytecodeJumpTarget,
      kLabelJumpTarget,
    };

    class PatchAccumulatorInBranchScope {
     public:
      PatchAccumulatorInBranchScope(BranchBuilder& builder, ValueNode* node,
                                    RootIndex root_index)
          : builder_(builder),
            node_(node),
            root_index_(root_index),
            jump_type_(builder.GetCurrentBranchType()) {
        if (builder.mode() == kBytecodeJumpTarget) {
          builder_.data_.bytecode_target.patch_accumulator_scope = this;
        }
      }

      ~PatchAccumulatorInBranchScope() {
        builder_.data_.bytecode_target.patch_accumulator_scope = nullptr;
      }

     private:
      BranchBuilder& builder_;
      ValueNode* node_;
      RootIndex root_index_;
      BranchType jump_type_;

      friend class BranchBuilder;
    };

    struct BytecodeJumpTarget {
      BytecodeJumpTarget(int jump_target_offset, int fallthrough_offset)
          : jump_target_offset(jump_target_offset),
            fallthrough_offset(fallthrough_offset),
            patch_accumulator_scope(nullptr) {}
      int jump_target_offset;
      int fallthrough_offset;
      PatchAccumulatorInBranchScope* patch_accumulator_scope;
    };

    struct LabelJumpTarget {
      explicit LabelJumpTarget(MaglevSubGraphBuilder::Label* jump_label)
          : jump_label(jump_label), fallthrough() {}
      MaglevSubGraphBuilder::Label* jump_label;
      BasicBlockRef fallthrough;
    };

    union Data {
      Data(int jump_target_offset, int fallthrough_offset)
          : bytecode_target(jump_target_offset, fallthrough_offset) {}
      explicit Data(MaglevSubGraphBuilder::Label* jump_label)
          : label_target(jump_label) {}
      BytecodeJumpTarget bytecode_target;
      LabelJumpTarget label_target;
    };

    // Creates a branch builder for bytecode offsets.
    BranchBuilder(MaglevGraphBuilder* builder, BranchType jump_type)
        : builder_(builder),
          sub_builder_(nullptr),
          jump_type_(jump_type),
          data_(builder->iterator_.GetJumpTargetOffset(),
                builder->iterator_.next_offset()) {}

    // Creates a branch builder for subgraph label.
    BranchBuilder(MaglevGraphBuilder* builder,
                  MaglevSubGraphBuilder* sub_builder, BranchType jump_type,
                  MaglevSubGraphBuilder::Label* jump_label)
        : builder_(builder),
          sub_builder_(sub_builder),
          jump_type_(jump_type),
          data_(jump_label) {}

    Mode mode() const {
      return sub_builder_ == nullptr ? kBytecodeJumpTarget : kLabelJumpTarget;
    }

    BranchType GetCurrentBranchType() const { return jump_type_; }

    void SetBranchSpecializationMode(BranchSpecializationMode mode) {
      branch_specialization_mode_ = mode;
    }
    void SwapTargets() { jump_type_ = NegateBranchType(jump_type_); }

    BasicBlockRef* jump_target();
    BasicBlockRef* fallthrough();
    BasicBlockRef* true_target();
    BasicBlockRef* false_target();

    BranchResult FromBool(bool value) const;
    BranchResult AlwaysTrue() const { return FromBool(true); }
    BranchResult AlwaysFalse() const { return FromBool(false); }

    template <typename NodeT, typename... Args>
    BranchResult Build(std::initializer_list<ValueNode*> inputs,
                       Args&&... args);

   private:
    MaglevGraphBuilder* builder_;
    MaglevGraphBuilder::MaglevSubGraphBuilder* sub_builder_;
    BranchType jump_type_;
    BranchSpecializationMode branch_specialization_mode_ =
        BranchSpecializationMode::kDefault;
    Data data_;

    void StartFallthroughBlock(BasicBlock* predecessor);
    void SetAccumulatorInBranch(BranchType jump_type) const;
  };

  BranchBuilder CreateBranchBuilder(
      BranchType jump_type = BranchType::kBranchIfTrue) {
    return BranchBuilder(this, jump_type);
  }
  BranchBuilder CreateBranchBuilder(
      MaglevSubGraphBuilder* subgraph, MaglevSubGraphBuilder::Label* jump_label,
      BranchType jump_type = BranchType::kBranchIfTrue) {
    return BranchBuilder(this, subgraph, jump_type, jump_label);
  }

  BranchResult BuildBranchIfRootConstant(BranchBuilder& builder,
                                         ValueNode* node, RootIndex root_index);
  BranchResult BuildBranchIfToBooleanTrue(BranchBuilder& builder,
                                          ValueNode* node);
  BranchResult BuildBranchIfInt32ToBooleanTrue(BranchBuilder& builder,
                                               ValueNode* node);
  BranchResult BuildBranchIfFloat64ToBooleanTrue(BranchBuilder& builder,
                                                 ValueNode* node);
  BranchResult BuildBranchIfFloat64IsHole(BranchBuilder& builder,
                                          ValueNode* node);
  BranchResult BuildBranchIfReferenceEqual(BranchBuilder& builder,
                                           ValueNode* lhs, ValueNode* rhs);
  BranchResult BuildBranchIfInt32Compare(BranchBuilder& builder, Operation op,
                                         ValueNode* lhs, ValueNode* rhs);
  BranchResult BuildBranchIfUint32Compare(BranchBuilder& builder, Operation op,
                                          ValueNode* lhs, ValueNode* rhs);
  BranchResult BuildBranchIfUndefinedOrNull(BranchBuilder& builder,
                                            ValueNode* node);
  BranchResult BuildBranchIfUndetectable(BranchBuilder& builder,
                                         ValueNode* value);
  BranchResult BuildBranchIfJSReceiver(BranchBuilder& builder,
                                       ValueNode* value);

  BranchResult BuildBranchIfTrue(BranchBuilder& builder, ValueNode* node);
  BranchResult BuildBranchIfNull(BranchBuilder& builder, ValueNode* node);
  BranchResult BuildBranchIfUndefined(BranchBuilder& builder, ValueNode* node);
  BasicBlock* BuildBranchIfReferenceEqual(ValueNode* lhs, ValueNode* rhs,
                                          BasicBlockRef* true_target,
                                          BasicBlockRef* false_target);

  template <typename FCond, typename FTrue, typename FFalse>
  ValueNode* Select(FCond cond, FTrue if_true, FFalse if_false);

  template <typename FCond, typename FTrue, typename FFalse>
  ReduceResult SelectReduction(FCond cond, FTrue if_true, FFalse if_false);

  void MarkBranchDeadAndJumpIfNeeded(bool is_jump_taken);

  void CalculatePredecessorCounts() {
    // Add 1 after the end of the bytecode so we can always write to the offset
    // after the last bytecode.
    size_t array_length = bytecode().length() + 1;
    predecessors_ = zone()->AllocateArray<uint32_t>(array_length);
    MemsetUint32(predecessors_, 0, entrypoint_);
    MemsetUint32(predecessors_ + entrypoint_, 1, array_length - entrypoint_);

    const int max_peelings = v8_flags.maglev_optimistic_peeled_loops ? 2 : 1;
    // We count jumps from peeled loops to outside of the loop twice.
    bool is_loop_peeling_iteration = false;
    std::optional<int> peeled_loop_end;
    interpreter::BytecodeArrayIterator iterator(bytecode().object());
    for (iterator.SetOffset(entrypoint_); !iterator.done();
         iterator.Advance()) {
      interpreter::Bytecode bytecode = iterator.current_bytecode();
      if (allow_loop_peeling_ &&
          bytecode_analysis().IsLoopHeader(iterator.current_offset())) {
        const compiler::LoopInfo& loop_info =
            bytecode_analysis().GetLoopInfoFor(iterator.current_offset());
        // Generators use irreducible control flow, which makes loop peeling too
        // complicated.
        int size = loop_info.loop_end() - loop_info.loop_start();
        if (loop_info.innermost() && !loop_info.resumable() &&
            iterator.next_offset() < loop_info.loop_end() &&
            size < v8_flags.maglev_loop_peeling_max_size) {
          DCHECK(!is_loop_peeling_iteration);
          is_loop_peeling_iteration = true;
          loop_headers_to_peel_.Add(iterator.current_offset());
          peeled_loop_end = bytecode_analysis().GetLoopEndOffsetForInnermost(
              iterator.current_offset());
        }
      }
      if (interpreter::Bytecodes::IsJump(bytecode)) {
        if (is_loop_peeling_iteration &&
            bytecode == interpreter::Bytecode::kJumpLoop) {
          DCHECK_EQ(iterator.next_offset(), peeled_loop_end);
          is_loop_peeling_iteration = false;
          peeled_loop_end = {};
        }
        if (iterator.GetJumpTargetOffset() < entrypoint_) {
          static_assert(kLoopsMustBeEnteredThroughHeader);
          if (predecessors_[iterator.GetJumpTargetOffset()] == 1) {
            // We encoutered a JumpLoop whose loop header is not reachable
            // otherwise. This loop is either dead or the JumpLoop will bail
            // with DeoptimizeReason::kOSREarlyExit.
            predecessors_[iterator.GetJumpTargetOffset()] = 0;
          }
        } else {
          predecessors_[iterator.GetJumpTargetOffset()]++;
        }
        if (is_loop_peeling_iteration &&
            iterator.GetJumpTargetOffset() >= *peeled_loop_end) {
          // Jumps from within the peeled loop to outside need to be counted
          // twice, once for the peeled and once for the regular loop body.
          predecessors_[iterator.GetJumpTargetOffset()] += max_peelings;
        }
        if (!interpreter::Bytecodes::IsConditionalJump(bytecode)) {
          predecessors_[iterator.next_offset()]--;
        }
      } else if (interpreter::Bytecodes::IsSwitch(bytecode)) {
        for (auto offset : iterator.GetJumpTableTargetOffsets()) {
          predecessors_[offset.target_offset]++;
        }
      } else if (interpreter::Bytecodes::Returns(bytecode) ||
                 interpreter::Bytecodes::UnconditionallyThrows(bytecode)) {
        predecessors_[iterator.next_offset()]--;
        // Collect inline return jumps in the slot after the last bytecode.
        if (is_inline() && interpreter::Bytecodes::Returns(bytecode)) {
          predecessors_[array_length - 1]++;
          if (is_loop_peeling_iteration) {
            predecessors_[array_length - 1] += max_peelings;
          }
        }
      }
      // TODO(leszeks): Also consider handler entries (the bytecode analysis)
      // will do this automatically I guess if we merge this into that.
    }
    if (!is_inline()) {
      DCHECK_EQ(0, predecessors_[bytecode().length()]);
    }
  }

  int NumPredecessors(int offset) { return predecessors_[offset]; }

  compiler::FeedbackVectorRef feedback() const {
    return compilation_unit_->feedback();
  }
  const FeedbackNexus FeedbackNexusForOperand(int slot_operand_index) const {
    return FeedbackNexus(feedback().object(),
                         GetSlotOperand(slot_operand_index),
                         broker()->feedback_nexus_config());
  }
  const FeedbackNexus FeedbackNexusForSlot(FeedbackSlot slot) const {
    return FeedbackNexus(feedback().object(), slot,
                         broker()->feedback_nexus_config());
  }
  compiler::BytecodeArrayRef bytecode() const {
    return compilation_unit_->bytecode();
  }
  const compiler::BytecodeAnalysis& bytecode_analysis() const {
    return bytecode_analysis_;
  }
  int parameter_count() const { return compilation_unit_->parameter_count(); }
  int parameter_count_without_receiver() const { return parameter_count() - 1; }
  int register_count() const { return compilation_unit_->register_count(); }
  KnownNodeAspects& known_node_aspects() {
    return *current_interpreter_frame_.known_node_aspects();
  }

  // True when this graph builder is building the subgraph of an inlined
  // function.
  bool is_inline() const { return parent_ != nullptr; }
  int inlining_depth() const { return compilation_unit_->inlining_depth(); }

  int argument_count() const {
    DCHECK(is_inline());
    return static_cast<int>(inlined_arguments_.size());
  }
  int argument_count_without_receiver() const { return argument_count() - 1; }

  bool HasMismatchedArgumentAndParameterCount() {
    return is_inline() && (argument_count() != parameter_count());
  }

  bool IsInsideLoop() const {
    if (caller_is_inside_loop_) return true;
    int loop_header_offset =
        bytecode_analysis().GetLoopOffsetFor(iterator_.current_offset());
    if (loop_header_offset != -1) {
      const compiler::LoopInfo& loop_info =
          bytecode_analysis().GetLoopInfoFor(loop_header_offset);
      if (loop_info.parent_offset() == -1) {
        // This is the outmost loop, if we're actually inside the peel, we are
        // not really in a loop.
        return !in_peeled_iteration() || in_optimistic_peeling_iteration();
      }
      return true;
    }
    return false;
  }

  // The fake offset used as a target for all exits of an inlined function.
  int inline_exit_offset() const {
    DCHECK(is_inline());
    return bytecode().length();
  }

  uint32_t NewObjectId() { return graph_->NewObjectId(); }

  LocalIsolate* const local_isolate_;
  MaglevCompilationUnit* const compilation_unit_;
  MaglevGraphBuilder* const parent_;
  DeoptFrame* parent_deopt_frame_ = nullptr;
  CatchBlockDetails parent_catch_;
  int parent_catch_deopt_frame_distance_ = 0;
  // Cache the heap broker since we access it a bunch.
  compiler::JSHeapBroker* broker_ = compilation_unit_->broker();

  Graph* const graph_;
  compiler::BytecodeAnalysis bytecode_analysis_;
  interpreter::BytecodeArrayIterator iterator_;
  SourcePositionTableIterator source_position_iterator_;
  uint32_t* predecessors_;

  int peeled_iteration_count_ = 0;
  bool any_peeled_loop_ = false;
  bool allow_loop_peeling_;

  bool in_peeled_iteration() const {
    DCHECK_GE(peeled_iteration_count_, 0);
    return peeled_iteration_count_ > 0;
  }

  // When loop SPeeling is enabled then the second-last peeling iteration
  // is the optimistic iteration. At the end we try to compile the JumpLoop and
  // only proceed with the fallback iteration 0, if the loop state is
  // incompatible with the loop end state.
  bool in_optimistic_peeling_iteration() const {
    return v8_flags.maglev_optimistic_peeled_loops &&
           peeled_iteration_count_ == 1;
  }
  bool is_loop_effect_tracking_enabled() {
    return v8_flags.maglev_escape_analysis || v8_flags.maglev_licm;
  }
  bool is_loop_effect_tracking() { return loop_effects_; }
  LoopEffects* loop_effects_ = nullptr;
  ZoneDeque<LoopEffects*> loop_effects_stack_;

  // When processing the peeled iteration of a loop, we need to reset the
  // decremented predecessor counts inside of the loop before processing the
  // body again. For this, we record offsets where we decremented the
  // predecessor count.
  ZoneVector<int> decremented_predecessor_offsets_;
  // The set of loop headers for which we decided to do loop peeling.
  BitVector loop_headers_to_peel_;

  // Current block information.
  bool in_prologue_ = true;
  BasicBlock* current_block_ = nullptr;
  std::optional<InterpretedDeoptFrame> entry_stack_check_frame_;
  std::optional<DeoptFrame> latest_checkpointed_frame_;
  SourcePosition current_source_position_;
  struct ForInState {
    ValueNode* receiver = nullptr;
    ValueNode* cache_type = nullptr;
    ValueNode* enum_cache_indices = nullptr;
    ValueNode* key = nullptr;
    ValueNode* index = nullptr;
    bool receiver_needs_map_check = false;
  };
  // TODO(leszeks): Allow having a stack of these.
  ForInState current_for_in_state = ForInState();

  AllocationBlock* current_allocation_block_ = nullptr;

  float call_frequency_;

  BasicBlockRef* jump_targets_;
  MergePointInterpreterFrameState** merge_states_;

  InterpreterFrameState current_interpreter_frame_;
  compiler::FeedbackSource current_speculation_feedback_;

  base::Vector<ValueNode*> inlined_arguments_;
  BytecodeOffset caller_bytecode_offset_;
  bool caller_is_inside_loop_;
  ValueNode* inlined_new_target_ = nullptr;

  // Bytecode offset at which compilation should start.
  int entrypoint_;
  int bailout_for_entrypoint() {
    if (!graph_->is_osr()) return kFunctionEntryBytecodeOffset;
    return bytecode_analysis_.osr_bailout_id().ToInt();
  }

  int inlining_id_;

  DeoptFrameScope* current_deopt_scope_ = nullptr;

  struct HandlerTableEntry {
    int end;
    int handler;
  };
  ZoneStack<HandlerTableEntry> catch_block_stack_;
  int next_handler_table_index_ = 0;

#ifdef DEBUG
  bool IsNodeCreatedForThisBytecode(ValueNode* node) const {
    return new_nodes_.find(node) != new_nodes_.end();
  }
  std::unordered_set<Node*> new_nodes_;
#endif

  // Some helpers for CSE

  static size_t fast_hash_combine(size_t seed, size_t h) {
    // Implementation from boost. Good enough for GVN.
    return h + 0x9e3779b9 + (seed << 6) + (seed >> 2);
  }

  template <typename T>
  static size_t gvn_hash_value(const T& in) {
    return base::hash_value(in);
  }

  static size_t gvn_hash_value(const compiler::MapRef& map) {
    return map.hash_value();
  }

  static size_t gvn_hash_value(const interpreter::Register& reg) {
    return base::hash_value(reg.index());
  }

  static size_t gvn_hash_value(const Representation& rep) {
    return base::hash_value(rep.kind());
  }

  static size_t gvn_hash_value(const ExternalReference& ref) {
    return base::hash_value(ref.address());
  }

  static size_t gvn_hash_value(const PolymorphicAccessInfo& access_info) {
    return access_info.hash_value();
  }

  template <typename T>
  static size_t gvn_hash_value(const v8::internal::ZoneCompactSet<T>& vector) {
    size_t hash = base::hash_value(vector.size());
    for (auto e : vector) {
      hash = fast_hash_combine(hash, gvn_hash_value(e));
    }
    return hash;
  }

  template <typename T>
  static size_t gvn_hash_value(const v8::internal::ZoneVector<T>& vector) {
    size_t hash = base::hash_value(vector.size());
    for (auto e : vector) {
      hash = fast_hash_combine(hash, gvn_hash_value(e));
    }
    return hash;
  }

  bool CanSpeculateCall() const {
    return current_speculation_feedback_.IsValid();
  }

  inline void MarkNodeDead(Node* node) {
    for (int i = 0; i < node->input_count(); ++i) {
      node->input(i).clear();
    }
    node->OverwriteWith(Opcode::kDead);
  }

  ZoneUnorderedMap<KnownNodeAspects::LoadedContextSlotsKey, Node*>
      unobserved_context_slot_stores_;
};

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_GRAPH_BUILDER_H_
