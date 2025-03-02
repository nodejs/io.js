// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/maglev/maglev-graph-builder.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

#include "src/base/bounds.h"
#include "src/base/ieee754.h"
#include "src/base/logging.h"
#include "src/base/vector.h"
#include "src/builtins/builtins-constructor.h"
#include "src/builtins/builtins.h"
#include "src/codegen/cpu-features.h"
#include "src/codegen/interface-descriptors-inl.h"
#include "src/common/assert-scope.h"
#include "src/common/globals.h"
#include "src/common/message-template.h"
#include "src/compiler/access-info.h"
#include "src/compiler/bytecode-liveness-map.h"
#include "src/compiler/compilation-dependencies.h"
#include "src/compiler/feedback-source.h"
#include "src/compiler/heap-refs.h"
#include "src/compiler/js-heap-broker-inl.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/processed-feedback.h"
#include "src/deoptimizer/deoptimize-reason.h"
#include "src/execution/protectors.h"
#include "src/flags/flags.h"
#include "src/handles/maybe-handles-inl.h"
#include "src/ic/handler-configuration-inl.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/interpreter/bytecode-flags-and-tokens.h"
#include "src/interpreter/bytecode-register.h"
#include "src/interpreter/bytecodes.h"
#include "src/maglev/maglev-compilation-info.h"
#include "src/maglev/maglev-compilation-unit.h"
#include "src/maglev/maglev-graph-printer.h"
#include "src/maglev/maglev-interpreter-frame-state.h"
#include "src/maglev/maglev-ir-inl.h"
#include "src/maglev/maglev-ir.h"
#include "src/numbers/conversions.h"
#include "src/numbers/ieee754.h"
#include "src/objects/arguments.h"
#include "src/objects/elements-kind.h"
#include "src/objects/feedback-vector.h"
#include "src/objects/fixed-array.h"
#include "src/objects/heap-number-inl.h"
#include "src/objects/js-array.h"
#include "src/objects/js-function.h"
#include "src/objects/js-objects.h"
#include "src/objects/literal-objects-inl.h"
#include "src/objects/name-inl.h"
#include "src/objects/object-list-macros.h"
#include "src/objects/property-cell.h"
#include "src/objects/property-details.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/slots-inl.h"
#include "src/objects/type-hints.h"
#include "src/roots/roots.h"
#include "src/utils/utils.h"
#include "src/zone/zone.h"

#ifdef V8_INTL_SUPPORT
#include "src/objects/intl-objects.h"
#endif

#define TRACE(...)                            \
  if (v8_flags.trace_maglev_graph_building) { \
    std::cout << __VA_ARGS__ << std::endl;    \
  }

#define FAIL(...)                                                         \
  TRACE("Failed " << __func__ << ":" << __LINE__ << ": " << __VA_ARGS__); \
  return {};

namespace v8::internal::maglev {

namespace {

enum class CpuOperation {
  kFloat64Round,
};

// TODO(leszeks): Add a generic mechanism for marking nodes as optionally
// supported.
bool IsSupported(CpuOperation op) {
  switch (op) {
    case CpuOperation::kFloat64Round:
#if defined(V8_TARGET_ARCH_X64) || defined(V8_TARGET_ARCH_IA32)
      return CpuFeatures::IsSupported(SSE4_1) || CpuFeatures::IsSupported(AVX);
#elif defined(V8_TARGET_ARCH_ARM)
      return CpuFeatures::IsSupported(ARMv8);
#elif defined(V8_TARGET_ARCH_ARM64) || defined(V8_TARGET_ARCH_PPC64) ||   \
    defined(V8_TARGET_ARCH_S390X) || defined(V8_TARGET_ARCH_RISCV64) ||   \
    defined(V8_TARGET_ARCH_RISCV32) || defined(V8_TARGET_ARCH_LOONG64) || \
    defined(V8_TARGET_ARCH_MIPS64)
      return true;
#else
#error "V8 does not support this architecture."
#endif
  }
}

class FunctionContextSpecialization final : public AllStatic {
 public:
  static compiler::OptionalContextRef TryToRef(
      const MaglevCompilationUnit* unit, ValueNode* context, size_t* depth) {
    DCHECK(unit->info()->specialize_to_function_context());
    if (Constant* n = context->TryCast<Constant>()) {
      return n->ref().AsContext().previous(unit->broker(), depth);
    }
    return {};
  }
};

}  // namespace

ValueNode* MaglevGraphBuilder::TryGetParentContext(ValueNode* node) {
  if (CreateFunctionContext* n = node->TryCast<CreateFunctionContext>()) {
    return n->context().node();
  }

  if (InlinedAllocation* alloc = node->TryCast<InlinedAllocation>()) {
    return alloc->object()->get(
        Context::OffsetOfElementAt(Context::PREVIOUS_INDEX));
  }

  if (CallRuntime* n = node->TryCast<CallRuntime>()) {
    switch (n->function_id()) {
      case Runtime::kPushBlockContext:
      case Runtime::kPushCatchContext:
      case Runtime::kNewFunctionContext:
        return n->context().node();
      default:
        break;
    }
  }

  return nullptr;
}

// Attempts to walk up the context chain through the graph in order to reduce
// depth and thus the number of runtime loads.
void MaglevGraphBuilder::MinimizeContextChainDepth(ValueNode** context,
                                                   size_t* depth) {
  while (*depth > 0) {
    ValueNode* parent_context = TryGetParentContext(*context);
    if (parent_context == nullptr) return;
    *context = parent_context;
    (*depth)--;
  }
}

void MaglevGraphBuilder::EscapeContext() {
  ValueNode* context = GetContext();
  if (InlinedAllocation* alloc = context->TryCast<InlinedAllocation>()) {
    alloc->ForceEscaping();
  }
}

class CallArguments {
 public:
  enum Mode {
    kDefault,
    kWithSpread,
    kWithArrayLike,
  };

  CallArguments(ConvertReceiverMode receiver_mode,
                interpreter::RegisterList reglist,
                const InterpreterFrameState& frame, Mode mode = kDefault)
      : receiver_mode_(receiver_mode),
        args_(reglist.register_count()),
        mode_(mode) {
    for (int i = 0; i < reglist.register_count(); i++) {
      args_[i] = frame.get(reglist[i]);
    }
    DCHECK_IMPLIES(args_.size() == 0,
                   receiver_mode == ConvertReceiverMode::kNullOrUndefined);
    DCHECK_IMPLIES(mode != kDefault,
                   receiver_mode == ConvertReceiverMode::kAny);
    DCHECK_IMPLIES(mode == kWithArrayLike, args_.size() == 2);
  }

  explicit CallArguments(ConvertReceiverMode receiver_mode)
      : receiver_mode_(receiver_mode), args_(), mode_(kDefault) {
    DCHECK_EQ(receiver_mode, ConvertReceiverMode::kNullOrUndefined);
  }

  CallArguments(ConvertReceiverMode receiver_mode,
                std::initializer_list<ValueNode*> args, Mode mode = kDefault)
      : receiver_mode_(receiver_mode), args_(args), mode_(mode) {
    DCHECK_IMPLIES(mode != kDefault,
                   receiver_mode == ConvertReceiverMode::kAny);
    DCHECK_IMPLIES(mode == kWithArrayLike, args_.size() == 2);
    CheckArgumentsAreNotConversionNodes();
  }

  CallArguments(ConvertReceiverMode receiver_mode,
                base::SmallVector<ValueNode*, 8>&& args, Mode mode = kDefault)
      : receiver_mode_(receiver_mode), args_(std::move(args)), mode_(mode) {
    DCHECK_IMPLIES(mode != kDefault,
                   receiver_mode == ConvertReceiverMode::kAny);
    DCHECK_IMPLIES(mode == kWithArrayLike, args_.size() == 2);
    CheckArgumentsAreNotConversionNodes();
  }

  ValueNode* receiver() const {
    if (receiver_mode_ == ConvertReceiverMode::kNullOrUndefined) {
      return nullptr;
    }
    return args_[0];
  }

  void set_receiver(ValueNode* receiver) {
    if (receiver_mode_ == ConvertReceiverMode::kNullOrUndefined) {
      args_.insert(args_.data(), receiver);
      receiver_mode_ = ConvertReceiverMode::kAny;
    } else {
      DCHECK(!receiver->properties().is_conversion());
      args_[0] = receiver;
    }
  }

  ValueNode* array_like_argument() {
    DCHECK_EQ(mode_, kWithArrayLike);
    DCHECK_GT(count(), 0);
    return args_[args_.size() - 1];
  }

  size_t count() const {
    if (receiver_mode_ == ConvertReceiverMode::kNullOrUndefined) {
      return args_.size();
    }
    return args_.size() - 1;
  }

  size_t count_with_receiver() const { return count() + 1; }

  ValueNode* operator[](size_t i) const {
    if (receiver_mode_ != ConvertReceiverMode::kNullOrUndefined) {
      i++;
    }
    if (i >= args_.size()) return nullptr;
    return args_[i];
  }

  void set_arg(size_t i, ValueNode* node) {
    if (receiver_mode_ != ConvertReceiverMode::kNullOrUndefined) {
      i++;
    }
    DCHECK_LT(i, args_.size());
    DCHECK(!node->properties().is_conversion());
    args_[i] = node;
  }

  Mode mode() const { return mode_; }

  ConvertReceiverMode receiver_mode() const { return receiver_mode_; }

  void PopArrayLikeArgument() {
    DCHECK_EQ(mode_, kWithArrayLike);
    DCHECK_GT(count(), 0);
    args_.pop_back();
  }

  void PopReceiver(ConvertReceiverMode new_receiver_mode) {
    DCHECK_NE(receiver_mode_, ConvertReceiverMode::kNullOrUndefined);
    DCHECK_NE(new_receiver_mode, ConvertReceiverMode::kNullOrUndefined);
    DCHECK_GT(args_.size(), 0);  // We have at least a receiver to pop!
    // TODO(victorgomes): Do this better!
    for (size_t i = 0; i < args_.size() - 1; i++) {
      args_[i] = args_[i + 1];
    }
    args_.pop_back();

    // If there is no non-receiver argument to become the new receiver,
    // consider the new receiver to be known undefined.
    receiver_mode_ = args_.empty() ? ConvertReceiverMode::kNullOrUndefined
                                   : new_receiver_mode;
  }

 private:
  ConvertReceiverMode receiver_mode_;
  base::SmallVector<ValueNode*, 8> args_;
  Mode mode_;

  void CheckArgumentsAreNotConversionNodes() {
#ifdef DEBUG
    // Arguments can leak to the interpreter frame if the call is inlined,
    // conversions should be stored in known_node_aspects/NodeInfo.
    for (ValueNode* arg : args_) {
      DCHECK(!arg->properties().is_conversion());
    }
#endif  // DEBUG
  }
};

class V8_NODISCARD MaglevGraphBuilder::SaveCallSpeculationScope {
 public:
  explicit SaveCallSpeculationScope(
      MaglevGraphBuilder* builder,
      compiler::FeedbackSource feedback_source = compiler::FeedbackSource())
      : builder_(builder) {
    saved_ = builder_->current_speculation_feedback_;
    // Only set the current speculation feedback if speculation is allowed.
    if (IsSpeculationAllowed(builder_->broker(), feedback_source)) {
      builder->current_speculation_feedback_ = feedback_source;
    } else {
      builder->current_speculation_feedback_ = compiler::FeedbackSource();
    }
  }
  ~SaveCallSpeculationScope() {
    builder_->current_speculation_feedback_ = saved_;
  }

  const compiler::FeedbackSource& value() { return saved_; }

 private:
  MaglevGraphBuilder* builder_;
  compiler::FeedbackSource saved_;

  static bool IsSpeculationAllowed(compiler::JSHeapBroker* broker,
                                   compiler::FeedbackSource feedback_source) {
    if (!feedback_source.IsValid()) return false;
    compiler::ProcessedFeedback const& processed_feedback =
        broker->GetFeedbackForCall(feedback_source);
    if (processed_feedback.IsInsufficient()) return false;
    return processed_feedback.AsCall().speculation_mode() ==
           SpeculationMode::kAllowSpeculation;
  }
};

class V8_NODISCARD MaglevGraphBuilder::DeoptFrameScope {
 public:
  DeoptFrameScope(MaglevGraphBuilder* builder, Builtin continuation,
                  compiler::OptionalJSFunctionRef maybe_js_target = {})
      : builder_(builder),
        parent_(builder->current_deopt_scope_),
        data_(DeoptFrame::BuiltinContinuationFrameData{
            continuation, {}, builder->GetContext(), maybe_js_target}) {
    builder_->current_interpreter_frame().virtual_objects().Snapshot();
    builder_->current_deopt_scope_ = this;
    builder_->AddDeoptUse(
        data_.get<DeoptFrame::BuiltinContinuationFrameData>().context);
    DCHECK(data_.get<DeoptFrame::BuiltinContinuationFrameData>()
               .parameters.empty());
  }

  DeoptFrameScope(MaglevGraphBuilder* builder, Builtin continuation,
                  compiler::OptionalJSFunctionRef maybe_js_target,
                  base::Vector<ValueNode* const> parameters)
      : builder_(builder),
        parent_(builder->current_deopt_scope_),
        data_(DeoptFrame::BuiltinContinuationFrameData{
            continuation, builder->zone()->CloneVector(parameters),
            builder->GetContext(), maybe_js_target}) {
    builder_->current_interpreter_frame().virtual_objects().Snapshot();
    builder_->current_deopt_scope_ = this;
    builder_->AddDeoptUse(
        data_.get<DeoptFrame::BuiltinContinuationFrameData>().context);
    if (parameters.size() > 0) {
      if (InlinedAllocation* receiver =
              parameters[0]->TryCast<InlinedAllocation>()) {
        // We escape the first argument, since the builtin continuation call can
        // trigger a stack iteration, which expects the receiver to be a
        // meterialized object.
        receiver->ForceEscaping();
      }
    }
    for (ValueNode* node :
         data_.get<DeoptFrame::BuiltinContinuationFrameData>().parameters) {
      builder_->AddDeoptUse(node);
    }
  }

  DeoptFrameScope(MaglevGraphBuilder* builder, ValueNode* receiver)
      : builder_(builder),
        parent_(builder->current_deopt_scope_),
        data_(DeoptFrame::ConstructInvokeStubFrameData{
            *builder->compilation_unit(), builder->current_source_position_,
            receiver, builder->GetContext()}) {
    builder_->current_interpreter_frame().virtual_objects().Snapshot();
    builder_->current_deopt_scope_ = this;
    builder_->AddDeoptUse(
        data_.get<DeoptFrame::ConstructInvokeStubFrameData>().receiver);
    builder_->AddDeoptUse(
        data_.get<DeoptFrame::ConstructInvokeStubFrameData>().context);
  }

  ~DeoptFrameScope() {
    builder_->current_deopt_scope_ = parent_;
    // We might have cached a checkpointed frame which includes this scope;
    // reset it just in case.
    builder_->latest_checkpointed_frame_.reset();
  }

  DeoptFrameScope* parent() const { return parent_; }

  bool IsLazyDeoptContinuationFrame() const {
    if (data_.tag() != DeoptFrame::FrameType::kBuiltinContinuationFrame) {
      return false;
    }
    switch (data_.get<DeoptFrame::FrameType::kBuiltinContinuationFrame>()
                .builtin_id) {
      case Builtin::kGetIteratorWithFeedbackLazyDeoptContinuation:
      case Builtin::kCallIteratorWithFeedbackLazyDeoptContinuation:
      case Builtin::kArrayForEachLoopLazyDeoptContinuation:
      case Builtin::kGenericLazyDeoptContinuation:
      case Builtin::kToBooleanLazyDeoptContinuation:
        return true;
      default:
        return false;
    }
  }

  DeoptFrame::FrameData& data() { return data_; }
  const DeoptFrame::FrameData& data() const { return data_; }

 private:
  MaglevGraphBuilder* builder_;
  DeoptFrameScope* parent_;
  DeoptFrame::FrameData data_;
};

class MaglevGraphBuilder::MaglevSubGraphBuilder::Variable {
 public:
  explicit Variable(int index) : pseudo_register_(index) {}

 private:
  friend class MaglevSubGraphBuilder;

  // Variables pretend to be interpreter registers as far as the dummy
  // compilation unit and merge states are concerned.
  interpreter::Register pseudo_register_;
};

class MaglevGraphBuilder::MaglevSubGraphBuilder::Label {
 public:
  Label(MaglevSubGraphBuilder* sub_builder, int predecessor_count)
      : predecessor_count_(predecessor_count),
        liveness_(
            sub_builder->builder_->zone()->New<compiler::BytecodeLivenessState>(
                sub_builder->compilation_unit_->register_count(),
                sub_builder->builder_->zone())) {}
  Label(MaglevSubGraphBuilder* sub_builder, int predecessor_count,
        std::initializer_list<Variable*> vars)
      : Label(sub_builder, predecessor_count) {
    for (Variable* var : vars) {
      liveness_->MarkRegisterLive(var->pseudo_register_.index());
    }
  }

 private:
  explicit Label(MergePointInterpreterFrameState* merge_state,
                 BasicBlock* basic_block)
      : merge_state_(merge_state), ref_(basic_block) {}

  friend class MaglevSubGraphBuilder;
  friend class BranchBuilder;
  MergePointInterpreterFrameState* merge_state_ = nullptr;
  int predecessor_count_ = -1;
  compiler::BytecodeLivenessState* liveness_ = nullptr;
  BasicBlockRef ref_;
};

class MaglevGraphBuilder::MaglevSubGraphBuilder::LoopLabel {
 public:
 private:
  explicit LoopLabel(MergePointInterpreterFrameState* merge_state,
                     BasicBlock* loop_header)
      : merge_state_(merge_state), loop_header_(loop_header) {}

  friend class MaglevSubGraphBuilder;
  MergePointInterpreterFrameState* merge_state_ = nullptr;
  BasicBlock* loop_header_;
};

class MaglevGraphBuilder::MaglevSubGraphBuilder::
    BorrowParentKnownNodeAspectsAndVOs {
 public:
  explicit BorrowParentKnownNodeAspectsAndVOs(
      MaglevSubGraphBuilder* sub_builder)
      : sub_builder_(sub_builder) {
    sub_builder_->TakeKnownNodeAspectsAndVOsFromParent();
  }
  ~BorrowParentKnownNodeAspectsAndVOs() {
    sub_builder_->MoveKnownNodeAspectsAndVOsToParent();
  }

 private:
  MaglevSubGraphBuilder* sub_builder_;
};

void MaglevGraphBuilder::BranchBuilder::StartFallthroughBlock(
    BasicBlock* predecessor) {
  switch (mode()) {
    case kBytecodeJumpTarget: {
      auto& data = data_.bytecode_target;
      if (data.patch_accumulator_scope &&
          (data.patch_accumulator_scope->node_ == builder_->GetAccumulator())) {
        SetAccumulatorInBranch(BranchType::kBranchIfTrue);
        builder_->MergeIntoFrameState(predecessor, data.jump_target_offset);
        SetAccumulatorInBranch(BranchType::kBranchIfFalse);
        builder_->StartFallthroughBlock(data.fallthrough_offset, predecessor);
      } else {
        builder_->MergeIntoFrameState(predecessor, data.jump_target_offset);
        builder_->StartFallthroughBlock(data.fallthrough_offset, predecessor);
      }
      break;
    }
    case kLabelJumpTarget:
      auto& data = data_.label_target;
      sub_builder_->MergeIntoLabel(data.jump_label, predecessor);
      builder_->StartNewBlock(predecessor, nullptr, data.fallthrough);
      break;
  }
}

void MaglevGraphBuilder::BranchBuilder::SetAccumulatorInBranch(
    BranchType jump_type) const {
  DCHECK_EQ(mode(), kBytecodeJumpTarget);
  auto& data = data_.bytecode_target;
  if (branch_specialization_mode_ == BranchSpecializationMode::kAlwaysBoolean) {
    builder_->SetAccumulatorInBranch(builder_->GetBooleanConstant(
        data.patch_accumulator_scope->jump_type_ == jump_type));
  } else if (data.patch_accumulator_scope->jump_type_ == jump_type) {
    builder_->SetAccumulatorInBranch(
        builder_->GetRootConstant(data.patch_accumulator_scope->root_index_));
  } else {
    builder_->SetAccumulatorInBranch(data.patch_accumulator_scope->node_);
  }
}

BasicBlockRef* MaglevGraphBuilder::BranchBuilder::jump_target() {
  switch (mode()) {
    case kBytecodeJumpTarget:
      return &builder_->jump_targets_[data_.bytecode_target.jump_target_offset];
    case kLabelJumpTarget:
      return &data_.label_target.jump_label->ref_;
  }
}

BasicBlockRef* MaglevGraphBuilder::BranchBuilder::fallthrough() {
  switch (mode()) {
    case kBytecodeJumpTarget:
      return &builder_->jump_targets_[data_.bytecode_target.fallthrough_offset];
    case kLabelJumpTarget:
      return &data_.label_target.fallthrough;
  }
}

BasicBlockRef* MaglevGraphBuilder::BranchBuilder::true_target() {
  return jump_type_ == BranchType::kBranchIfTrue ? jump_target()
                                                 : fallthrough();
}

BasicBlockRef* MaglevGraphBuilder::BranchBuilder::false_target() {
  return jump_type_ == BranchType::kBranchIfFalse ? jump_target()
                                                  : fallthrough();
}

MaglevGraphBuilder::BranchResult MaglevGraphBuilder::BranchBuilder::FromBool(
    bool value) const {
  switch (mode()) {
    case kBytecodeJumpTarget: {
      BranchType type_if_need_to_jump =
          (value ? BranchType::kBranchIfTrue : BranchType::kBranchIfFalse);
      builder_->MarkBranchDeadAndJumpIfNeeded(jump_type_ ==
                                              type_if_need_to_jump);
      return BranchResult::kDefault;
    }
    case kLabelJumpTarget:
      return value ? BranchResult::kAlwaysTrue : BranchResult::kAlwaysFalse;
  }
}

template <typename ControlNodeT, typename... Args>
MaglevGraphBuilder::BranchResult MaglevGraphBuilder::BranchBuilder::Build(
    std::initializer_list<ValueNode*> control_inputs, Args&&... args) {
  static_assert(IsConditionalControlNode(Node::opcode_of<ControlNodeT>));
  BasicBlock* block = builder_->FinishBlock<ControlNodeT>(
      control_inputs, std::forward<Args>(args)..., true_target(),
      false_target());
  StartFallthroughBlock(block);
  return BranchResult::kDefault;
}

MaglevGraphBuilder::MaglevSubGraphBuilder::MaglevSubGraphBuilder(
    MaglevGraphBuilder* builder, int variable_count)
    : builder_(builder),
      compilation_unit_(MaglevCompilationUnit::NewDummy(
          builder->zone(), builder->compilation_unit(), variable_count, 0, 0)),
      pseudo_frame_(*compilation_unit_, nullptr, VirtualObject::List()) {
  // We need to set a context, since this is unconditional in the frame state,
  // so set it to the real context.
  pseudo_frame_.set(interpreter::Register::current_context(),
                    builder_->current_interpreter_frame().get(
                        interpreter::Register::current_context()));
  DCHECK_NULL(pseudo_frame_.known_node_aspects());
}

MaglevGraphBuilder::MaglevSubGraphBuilder::LoopLabel
MaglevGraphBuilder::MaglevSubGraphBuilder::BeginLoop(
    std::initializer_list<Variable*> loop_vars) {
  // Create fake liveness and loop info for the loop, with all given loop vars
  // set to be live and assigned inside the loop.
  compiler::BytecodeLivenessState* loop_header_liveness =
      builder_->zone()->New<compiler::BytecodeLivenessState>(
          compilation_unit_->register_count(), builder_->zone());
  compiler::LoopInfo* loop_info = builder_->zone()->New<compiler::LoopInfo>(
      -1, 0, kMaxInt, compilation_unit_->parameter_count(),
      compilation_unit_->register_count(), builder_->zone());
  for (Variable* var : loop_vars) {
    loop_header_liveness->MarkRegisterLive(var->pseudo_register_.index());
    loop_info->assignments().Add(var->pseudo_register_);
  }

  // Finish the current block, jumping (as a fallthrough) to the loop header.
  BasicBlockRef loop_header_ref;
  BasicBlock* loop_predecessor =
      builder_->FinishBlock<Jump>({}, &loop_header_ref);

  // Create a state for the loop header, with two predecessors (the above jump
  // and the back edge), and initialise with the current state.
  MergePointInterpreterFrameState* loop_state =
      MergePointInterpreterFrameState::NewForLoop(
          pseudo_frame_, *compilation_unit_, 0, 2, loop_header_liveness,
          loop_info);

  {
    BorrowParentKnownNodeAspectsAndVOs borrow(this);
    loop_state->Merge(builder_, *compilation_unit_, pseudo_frame_,
                      loop_predecessor);
  }

  // Start a new basic block for the loop.
  DCHECK_NULL(pseudo_frame_.known_node_aspects());
  pseudo_frame_.CopyFrom(*compilation_unit_, *loop_state);
  MoveKnownNodeAspectsAndVOsToParent();

  builder_->ProcessMergePointPredecessors(*loop_state, loop_header_ref);
  builder_->StartNewBlock(nullptr, loop_state, loop_header_ref);

  return LoopLabel{loop_state, loop_header_ref.block_ptr()};
}

template <typename ControlNodeT, typename... Args>
void MaglevGraphBuilder::MaglevSubGraphBuilder::GotoIfTrue(
    Label* true_target, std::initializer_list<ValueNode*> control_inputs,
    Args&&... args) {
  static_assert(IsConditionalControlNode(Node::opcode_of<ControlNodeT>));

  BasicBlockRef fallthrough_ref;

  // Pass through to FinishBlock, converting Labels to BasicBlockRefs and the
  // fallthrough label to the fallthrough ref.
  BasicBlock* block = builder_->FinishBlock<ControlNodeT>(
      control_inputs, std::forward<Args>(args)..., &true_target->ref_,
      &fallthrough_ref);

  MergeIntoLabel(true_target, block);

  builder_->StartNewBlock(block, nullptr, fallthrough_ref);
}

template <typename ControlNodeT, typename... Args>
void MaglevGraphBuilder::MaglevSubGraphBuilder::GotoIfFalse(
    Label* false_target, std::initializer_list<ValueNode*> control_inputs,
    Args&&... args) {
  static_assert(IsConditionalControlNode(Node::opcode_of<ControlNodeT>));

  BasicBlockRef fallthrough_ref;

  // Pass through to FinishBlock, converting Labels to BasicBlockRefs and the
  // fallthrough label to the fallthrough ref.
  BasicBlock* block = builder_->FinishBlock<ControlNodeT>(
      control_inputs, std::forward<Args>(args)..., &fallthrough_ref,
      &false_target->ref_);

  MergeIntoLabel(false_target, block);

  builder_->StartNewBlock(block, nullptr, fallthrough_ref);
}

void MaglevGraphBuilder::MaglevSubGraphBuilder::GotoOrTrim(Label* label) {
  if (builder_->current_block_ == nullptr) {
    ReducePredecessorCount(label);
    return;
  }
  Goto(label);
}

void MaglevGraphBuilder::MaglevSubGraphBuilder::Goto(Label* label) {
  CHECK_NOT_NULL(builder_->current_block_);
  BasicBlock* block = builder_->FinishBlock<Jump>({}, &label->ref_);
  MergeIntoLabel(label, block);
}

void MaglevGraphBuilder::MaglevSubGraphBuilder::ReducePredecessorCount(
    Label* label, unsigned num) {
  DCHECK_GE(label->predecessor_count_, num);
  if (num == 0) {
    return;
  }
  label->predecessor_count_ -= num;
  if (label->merge_state_ != nullptr) {
    label->merge_state_->MergeDead(*compilation_unit_, num);
  }
}

void MaglevGraphBuilder::MaglevSubGraphBuilder::EndLoop(LoopLabel* loop_label) {
  if (builder_->current_block_ == nullptr) {
    loop_label->merge_state_->MergeDeadLoop(*compilation_unit_);
    return;
  }

  BasicBlock* block =
      builder_->FinishBlock<JumpLoop>({}, loop_label->loop_header_);
  {
    BorrowParentKnownNodeAspectsAndVOs borrow(this);
    loop_label->merge_state_->MergeLoop(builder_, *compilation_unit_,
                                        pseudo_frame_, block);
  }
  block->set_predecessor_id(loop_label->merge_state_->predecessor_count() - 1);
}

ReduceResult MaglevGraphBuilder::MaglevSubGraphBuilder::TrimPredecessorsAndBind(
    Label* label) {
  int predecessors_so_far = label->merge_state_ == nullptr
                                ? 0
                                : label->merge_state_->predecessors_so_far();
  DCHECK_LE(predecessors_so_far, label->predecessor_count_);
  builder_->current_block_ = nullptr;
  ReducePredecessorCount(label,
                         label->predecessor_count_ - predecessors_so_far);
  if (predecessors_so_far == 0) return ReduceResult::DoneWithAbort();
  Bind(label);
  return ReduceResult::Done();
}

void MaglevGraphBuilder::MaglevSubGraphBuilder::Bind(Label* label) {
  DCHECK_NULL(builder_->current_block_);

  DCHECK_NULL(pseudo_frame_.known_node_aspects());
  pseudo_frame_.CopyFrom(*compilation_unit_, *label->merge_state_);
  MoveKnownNodeAspectsAndVOsToParent();

  CHECK_EQ(label->merge_state_->predecessors_so_far(),
           label->predecessor_count_);

  builder_->ProcessMergePointPredecessors(*label->merge_state_, label->ref_);
  builder_->StartNewBlock(nullptr, label->merge_state_, label->ref_);
}

void MaglevGraphBuilder::MaglevSubGraphBuilder::set(Variable& var,
                                                    ValueNode* value) {
  pseudo_frame_.set(var.pseudo_register_, value);
}
ValueNode* MaglevGraphBuilder::MaglevSubGraphBuilder::get(
    const Variable& var) const {
  return pseudo_frame_.get(var.pseudo_register_);
}

template <typename FCond, typename FTrue, typename FFalse>
ReduceResult MaglevGraphBuilder::MaglevSubGraphBuilder::Branch(
    std::initializer_list<MaglevSubGraphBuilder::Variable*> vars, FCond cond,
    FTrue if_true, FFalse if_false) {
  MaglevSubGraphBuilder::Label else_branch(this, 1);
  BranchBuilder builder(builder_, this, BranchType::kBranchIfFalse,
                        &else_branch);
  BranchResult branch_result = cond(builder);
  if (branch_result == BranchResult::kAlwaysTrue) {
    return if_true();
  }
  if (branch_result == BranchResult::kAlwaysFalse) {
    return if_false();
  }
  DCHECK(branch_result == BranchResult::kDefault);
  MaglevSubGraphBuilder::Label done(this, 2, vars);
  MaybeReduceResult result_if_true = if_true();
  CHECK(result_if_true.IsDone());
  GotoOrTrim(&done);
  Bind(&else_branch);
  MaybeReduceResult result_if_false = if_false();
  CHECK(result_if_false.IsDone());
  if (result_if_true.IsDoneWithAbort() && result_if_false.IsDoneWithAbort()) {
    return ReduceResult::DoneWithAbort();
  }
  GotoOrTrim(&done);
  Bind(&done);
  return ReduceResult::Done();
}

template <typename FCond, typename FTrue, typename FFalse>
ValueNode* MaglevGraphBuilder::Select(FCond cond, FTrue if_true,
                                      FFalse if_false) {
  MaglevSubGraphBuilder subgraph(this, 1);
  MaglevSubGraphBuilder::Label else_branch(&subgraph, 1);
  BranchBuilder builder(this, &subgraph, BranchType::kBranchIfFalse,
                        &else_branch);
  BranchResult branch_result = cond(builder);
  if (branch_result == BranchResult::kAlwaysTrue) {
    return if_true();
  }
  if (branch_result == BranchResult::kAlwaysFalse) {
    return if_false();
  }
  DCHECK(branch_result == BranchResult::kDefault);
  MaglevSubGraphBuilder::Variable ret_val(0);
  MaglevSubGraphBuilder::Label done(&subgraph, 2, {&ret_val});
  subgraph.set(ret_val, if_true());
  subgraph.Goto(&done);
  subgraph.Bind(&else_branch);
  subgraph.set(ret_val, if_false());
  subgraph.Goto(&done);
  subgraph.Bind(&done);
  return subgraph.get(ret_val);
}

template <typename FCond, typename FTrue, typename FFalse>
MaybeReduceResult MaglevGraphBuilder::SelectReduction(FCond cond, FTrue if_true,
                                                      FFalse if_false) {
  MaglevSubGraphBuilder subgraph(this, 1);
  MaglevSubGraphBuilder::Label else_branch(&subgraph, 1);
  BranchBuilder builder(this, &subgraph, BranchType::kBranchIfFalse,
                        &else_branch);
  BranchResult branch_result = cond(builder);
  if (branch_result == BranchResult::kAlwaysTrue) {
    return if_true();
  }
  if (branch_result == BranchResult::kAlwaysFalse) {
    return if_false();
  }
  DCHECK(branch_result == BranchResult::kDefault);
  MaglevSubGraphBuilder::Variable ret_val(0);
  MaglevSubGraphBuilder::Label done(&subgraph, 2, {&ret_val});
  MaybeReduceResult result_if_true = if_true();
  CHECK(result_if_true.IsDone());
  if (result_if_true.IsDoneWithValue()) {
    subgraph.set(ret_val, result_if_true.value());
  }
  subgraph.GotoOrTrim(&done);
  subgraph.Bind(&else_branch);
  MaybeReduceResult result_if_false = if_false();
  CHECK(result_if_false.IsDone());
  if (result_if_true.IsDoneWithAbort() && result_if_false.IsDoneWithAbort()) {
    return ReduceResult::DoneWithAbort();
  }
  if (result_if_false.IsDoneWithValue()) {
    subgraph.set(ret_val, result_if_false.value());
  }
  subgraph.GotoOrTrim(&done);
  subgraph.Bind(&done);
  return subgraph.get(ret_val);
}

// Known node aspects for the pseudo frame are null aside from when merging --
// before each merge, we should borrow the node aspects from the parent
// builder, and after each merge point, we should copy the node aspects back
// to the parent. This is so that the parent graph builder can update its own
// known node aspects without having to worry about this pseudo frame.
void MaglevGraphBuilder::MaglevSubGraphBuilder::
    TakeKnownNodeAspectsAndVOsFromParent() {
  DCHECK_NULL(pseudo_frame_.known_node_aspects());
  DCHECK(pseudo_frame_.virtual_objects().is_empty());
  pseudo_frame_.set_known_node_aspects(
      builder_->current_interpreter_frame_.known_node_aspects());
  pseudo_frame_.set_virtual_objects(
      builder_->current_interpreter_frame_.virtual_objects());
}

void MaglevGraphBuilder::MaglevSubGraphBuilder::
    MoveKnownNodeAspectsAndVOsToParent() {
  DCHECK_NOT_NULL(pseudo_frame_.known_node_aspects());
  builder_->current_interpreter_frame_.set_known_node_aspects(
      pseudo_frame_.known_node_aspects());
  pseudo_frame_.clear_known_node_aspects();
  builder_->current_interpreter_frame_.set_virtual_objects(
      pseudo_frame_.virtual_objects());
  pseudo_frame_.set_virtual_objects(VirtualObject::List());
}

void MaglevGraphBuilder::MaglevSubGraphBuilder::MergeIntoLabel(
    Label* label, BasicBlock* predecessor) {
  BorrowParentKnownNodeAspectsAndVOs borrow(this);

  if (label->merge_state_ == nullptr) {
    // If there's no merge state, allocate a new one.
    label->merge_state_ = MergePointInterpreterFrameState::New(
        *compilation_unit_, pseudo_frame_, 0, label->predecessor_count_,
        predecessor, label->liveness_);
  } else {
    // If there already is a frame state, merge.
    label->merge_state_->Merge(builder_, *compilation_unit_, pseudo_frame_,
                               predecessor);
  }
}

MaglevGraphBuilder::MaglevGraphBuilder(
    LocalIsolate* local_isolate, MaglevCompilationUnit* compilation_unit,
    Graph* graph, float call_frequency, BytecodeOffset caller_bytecode_offset,
    bool caller_is_inside_loop, int inlining_id, MaglevGraphBuilder* parent)
    : local_isolate_(local_isolate),
      compilation_unit_(compilation_unit),
      parent_(parent),
      graph_(graph),
      bytecode_analysis_(bytecode().object(), zone(),
                         compilation_unit->osr_offset(), true),
      iterator_(bytecode().object()),
      source_position_iterator_(bytecode().SourcePositionTable(broker())),
      allow_loop_peeling_(v8_flags.maglev_loop_peeling),
      loop_effects_stack_(zone()),
      decremented_predecessor_offsets_(zone()),
      loop_headers_to_peel_(bytecode().length(), zone()),
      current_source_position_(SourcePosition(
          compilation_unit_->shared_function_info().StartPosition(),
          inlining_id)),
      call_frequency_(call_frequency),
      // Add an extra jump_target slot for the inline exit if needed.
      jump_targets_(zone()->AllocateArray<BasicBlockRef>(
          bytecode().length() + (is_inline() ? 1 : 0))),
      // Overallocate merge_states_ by one to allow always looking up the
      // next offset. This overallocated slot can also be used for the inline
      // exit when needed.
      merge_states_(zone()->AllocateArray<MergePointInterpreterFrameState*>(
          bytecode().length() + 1)),
      current_interpreter_frame_(
          *compilation_unit_,
          is_inline() ? parent->current_interpreter_frame_.known_node_aspects()
                      : compilation_unit_->zone()->New<KnownNodeAspects>(
                            compilation_unit_->zone()),
          is_inline() ? parent->current_interpreter_frame_.virtual_objects()
                      : VirtualObject::List()),
      caller_bytecode_offset_(caller_bytecode_offset),
      caller_is_inside_loop_(caller_is_inside_loop),
      entrypoint_(compilation_unit->is_osr()
                      ? bytecode_analysis_.osr_entry_point()
                      : 0),
      inlining_id_(inlining_id),
      catch_block_stack_(zone()),
      unobserved_context_slot_stores_(zone()) {
  memset(merge_states_, 0,
         (bytecode().length() + 1) * sizeof(InterpreterFrameState*));
  // Default construct basic block refs.
  // TODO(leszeks): This could be a memset of nullptr to ..._jump_targets_.
  for (int i = 0; i < bytecode().length(); ++i) {
    new (&jump_targets_[i]) BasicBlockRef();
  }

  if (is_inline()) {
    DCHECK_NOT_NULL(parent_);
    DCHECK_GT(compilation_unit->inlining_depth(), 0);
    // The allocation/initialisation logic here relies on inline_exit_offset
    // being the offset one past the end of the bytecode.
    DCHECK_EQ(inline_exit_offset(), bytecode().length());
    merge_states_[inline_exit_offset()] = nullptr;
    new (&jump_targets_[inline_exit_offset()]) BasicBlockRef();
    if (parent_->loop_effects_) {
      loop_effects_ = parent->loop_effects_;
      loop_effects_stack_.push_back(loop_effects_);
    }
    unobserved_context_slot_stores_ = parent_->unobserved_context_slot_stores_;
  }

  CHECK_IMPLIES(compilation_unit_->is_osr(), graph_->is_osr());
  CHECK_EQ(compilation_unit_->info()->toplevel_osr_offset() !=
               BytecodeOffset::None(),
           graph_->is_osr());
  if (compilation_unit_->is_osr()) {
    CHECK(!is_inline());

    // Make sure that we're at a valid OSR entrypoint.
    //
    // This is also a defense-in-depth check to make sure that we're not
    // compiling invalid bytecode if the OSR offset is wrong (e.g. because it
    // belongs to different bytecode).
    //
    // OSR'ing into the middle of a loop is currently not supported. There
    // should not be any issue with OSR'ing outside of loops, just we currently
    // dont do it...
    interpreter::BytecodeArrayIterator it(bytecode().object());
    it.AdvanceTo(compilation_unit_->osr_offset().ToInt());
    CHECK(it.CurrentBytecodeIsValidOSREntry());
    CHECK_EQ(entrypoint_, it.GetJumpTargetOffset());

    iterator_.AdvanceTo(entrypoint_);

    if (v8_flags.trace_maglev_graph_building) {
      std::cout << "- Non-standard entrypoint @" << entrypoint_
                << " by OSR from @" << compilation_unit_->osr_offset().ToInt()
                << std::endl;
    }
  }
  CHECK_IMPLIES(!compilation_unit_->is_osr(), entrypoint_ == 0);

  CalculatePredecessorCounts();
}

void MaglevGraphBuilder::StartPrologue() {
  current_block_ = zone()->New<BasicBlock>(nullptr, zone());
}

BasicBlock* MaglevGraphBuilder::EndPrologue() {
  BasicBlock* first_block;
  if (!is_inline() &&
      (v8_flags.maglev_hoist_osr_value_phi_untagging && graph_->is_osr())) {
    first_block =
        FinishBlock<CheckpointedJump>({}, &jump_targets_[entrypoint_]);
  } else {
    first_block = FinishBlock<Jump>({}, &jump_targets_[entrypoint_]);
  }
  MergeIntoFrameState(first_block, entrypoint_);
  return first_block;
}

void MaglevGraphBuilder::SetArgument(int i, ValueNode* value) {
  interpreter::Register reg = interpreter::Register::FromParameterIndex(i);
  current_interpreter_frame_.set(reg, value);
}

ValueNode* MaglevGraphBuilder::GetArgument(int i) {
  DCHECK_LT(i, parameter_count());
  interpreter::Register reg = interpreter::Register::FromParameterIndex(i);
  return current_interpreter_frame_.get(reg);
}

ValueNode* MaglevGraphBuilder::GetInlinedArgument(int i) {
  DCHECK(is_inline());
  DCHECK_LT(i, argument_count());
  return inlined_arguments_[i];
}

void MaglevGraphBuilder::InitializeRegister(interpreter::Register reg,
                                            ValueNode* value) {
  current_interpreter_frame_.set(
      reg, value ? value : AddNewNode<InitialValue>({}, reg));
}

void MaglevGraphBuilder::BuildRegisterFrameInitialization(
    ValueNode* context, ValueNode* closure, ValueNode* new_target) {
  if (closure == nullptr &&
      compilation_unit_->info()->specialize_to_function_context()) {
    compiler::JSFunctionRef function = compiler::MakeRefAssumeMemoryFence(
        broker(), broker()->CanonicalPersistentHandle(
                      compilation_unit_->info()->toplevel_function()));
    closure = GetConstant(function);
    context = GetConstant(function.context(broker()));
  }
  InitializeRegister(interpreter::Register::current_context(), context);
  InitializeRegister(interpreter::Register::function_closure(), closure);

  interpreter::Register new_target_or_generator_register =
      bytecode().incoming_new_target_or_generator_register();

  int register_index = 0;

  if (compilation_unit_->is_osr()) {
    for (; register_index < register_count(); register_index++) {
      auto val =
          AddNewNode<InitialValue>({}, interpreter::Register(register_index));
      InitializeRegister(interpreter::Register(register_index), val);
      graph_->osr_values().push_back(val);
    }
    return;
  }

  // TODO(leszeks): Don't emit if not needed.
  ValueNode* undefined_value = GetRootConstant(RootIndex::kUndefinedValue);
  if (new_target_or_generator_register.is_valid()) {
    int new_target_index = new_target_or_generator_register.index();
    for (; register_index < new_target_index; register_index++) {
      current_interpreter_frame_.set(interpreter::Register(register_index),
                                     undefined_value);
    }
    current_interpreter_frame_.set(
        new_target_or_generator_register,
        new_target ? new_target
                   : GetRegisterInput(kJavaScriptCallNewTargetRegister));
    register_index++;
  }
  for (; register_index < register_count(); register_index++) {
    InitializeRegister(interpreter::Register(register_index), undefined_value);
  }
}

void MaglevGraphBuilder::BuildMergeStates() {
  auto offset_and_info = bytecode_analysis().GetLoopInfos().begin();
  auto end = bytecode_analysis().GetLoopInfos().end();
  while (offset_and_info != end && offset_and_info->first < entrypoint_) {
    ++offset_and_info;
  }
  for (; offset_and_info != end; ++offset_and_info) {
    int offset = offset_and_info->first;
    const compiler::LoopInfo& loop_info = offset_and_info->second;
    if (loop_headers_to_peel_.Contains(offset)) {
      // Peeled loops are treated like normal merges at first. We will construct
      // the proper loop header merge state when reaching the `JumpLoop` of the
      // peeled iteration.
      continue;
    }
    const compiler::BytecodeLivenessState* liveness = GetInLivenessFor(offset);
    DCHECK_NULL(merge_states_[offset]);
    if (v8_flags.trace_maglev_graph_building) {
      std::cout << "- Creating loop merge state at @" << offset << std::endl;
    }
    merge_states_[offset] = MergePointInterpreterFrameState::NewForLoop(
        current_interpreter_frame_, *compilation_unit_, offset,
        predecessor_count(offset), liveness, &loop_info);
  }

  if (bytecode().handler_table_size() > 0) {
    HandlerTable table(*bytecode().object());
    for (int i = 0; i < table.NumberOfRangeEntries(); i++) {
      const int offset = table.GetRangeHandler(i);
      const bool was_used = table.HandlerWasUsed(i);
      const interpreter::Register context_reg(table.GetRangeData(i));
      const compiler::BytecodeLivenessState* liveness =
          GetInLivenessFor(offset);
      DCHECK_EQ(predecessor_count(offset), 0);
      DCHECK_NULL(merge_states_[offset]);
      if (v8_flags.trace_maglev_graph_building) {
        std::cout << "- Creating exception merge state at @" << offset
                  << (was_used ? "" : " (never used)") << ", context register r"
                  << context_reg.index() << std::endl;
      }
      merge_states_[offset] = MergePointInterpreterFrameState::NewForCatchBlock(
          *compilation_unit_, liveness, offset, was_used, context_reg, graph_);
    }
  }
}

namespace {

template <int index, interpreter::OperandType... operands>
struct GetResultLocationAndSizeHelper;

// Terminal cases
template <int index>
struct GetResultLocationAndSizeHelper<index> {
  static std::pair<interpreter::Register, int> GetResultLocationAndSize(
      const interpreter::BytecodeArrayIterator& iterator) {
    // TODO(leszeks): This should probably actually be "UNREACHABLE" but we have
    // lazy deopt info for interrupt budget updates at returns, not for actual
    // lazy deopts, but just for stack iteration purposes.
    return {interpreter::Register::invalid_value(), 0};
  }
  static bool HasOutputRegisterOperand() { return false; }
};

template <int index, interpreter::OperandType... operands>
struct GetResultLocationAndSizeHelper<index, interpreter::OperandType::kRegOut,
                                      operands...> {
  static std::pair<interpreter::Register, int> GetResultLocationAndSize(
      const interpreter::BytecodeArrayIterator& iterator) {
    // We shouldn't have any other output operands than this one.
    return {iterator.GetRegisterOperand(index), 1};
  }
  static bool HasOutputRegisterOperand() { return true; }
};

template <int index, interpreter::OperandType... operands>
struct GetResultLocationAndSizeHelper<
    index, interpreter::OperandType::kRegOutPair, operands...> {
  static std::pair<interpreter::Register, int> GetResultLocationAndSize(
      const interpreter::BytecodeArrayIterator& iterator) {
    // We shouldn't have any other output operands than this one.
    return {iterator.GetRegisterOperand(index), 2};
  }
  static bool HasOutputRegisterOperand() { return true; }
};

template <int index, interpreter::OperandType... operands>
struct GetResultLocationAndSizeHelper<
    index, interpreter::OperandType::kRegOutTriple, operands...> {
  static std::pair<interpreter::Register, int> GetResultLocationAndSize(
      const interpreter::BytecodeArrayIterator& iterator) {
    // We shouldn't have any other output operands than this one.
    DCHECK(!(GetResultLocationAndSizeHelper<
             index + 1, operands...>::HasOutputRegisterOperand()));
    return {iterator.GetRegisterOperand(index), 3};
  }
  static bool HasOutputRegisterOperand() { return true; }
};

// We don't support RegOutList for lazy deopts.
template <int index, interpreter::OperandType... operands>
struct GetResultLocationAndSizeHelper<
    index, interpreter::OperandType::kRegOutList, operands...> {
  static std::pair<interpreter::Register, int> GetResultLocationAndSize(
      const interpreter::BytecodeArrayIterator& iterator) {
    interpreter::RegisterList list = iterator.GetRegisterListOperand(index);
    return {list.first_register(), list.register_count()};
  }
  static bool HasOutputRegisterOperand() { return true; }
};

// Induction case.
template <int index, interpreter::OperandType operand,
          interpreter::OperandType... operands>
struct GetResultLocationAndSizeHelper<index, operand, operands...> {
  static std::pair<interpreter::Register, int> GetResultLocationAndSize(
      const interpreter::BytecodeArrayIterator& iterator) {
    return GetResultLocationAndSizeHelper<
        index + 1, operands...>::GetResultLocationAndSize(iterator);
  }
  static bool HasOutputRegisterOperand() {
    return GetResultLocationAndSizeHelper<
        index + 1, operands...>::HasOutputRegisterOperand();
  }
};

template <interpreter::Bytecode bytecode,
          interpreter::ImplicitRegisterUse implicit_use,
          interpreter::OperandType... operands>
std::pair<interpreter::Register, int> GetResultLocationAndSizeForBytecode(
    const interpreter::BytecodeArrayIterator& iterator) {
  // We don't support output registers for implicit registers.
  DCHECK(!interpreter::BytecodeOperands::WritesImplicitRegister(implicit_use));
  if (interpreter::BytecodeOperands::WritesAccumulator(implicit_use)) {
    // If we write the accumulator, we shouldn't also write an output register.
    DCHECK(!(GetResultLocationAndSizeHelper<
             0, operands...>::HasOutputRegisterOperand()));
    return {interpreter::Register::virtual_accumulator(), 1};
  }

  // Use template magic to output a the appropriate GetRegisterOperand call and
  // size for this bytecode.
  return GetResultLocationAndSizeHelper<
      0, operands...>::GetResultLocationAndSize(iterator);
}

}  // namespace

std::pair<interpreter::Register, int>
MaglevGraphBuilder::GetResultLocationAndSize() const {
  using Bytecode = interpreter::Bytecode;
  using OperandType = interpreter::OperandType;
  using ImplicitRegisterUse = interpreter::ImplicitRegisterUse;
  Bytecode bytecode = iterator_.current_bytecode();
  // TODO(leszeks): Only emit these cases for bytecodes we know can lazy deopt.
  switch (bytecode) {
#define CASE(Name, ...)                                           \
  case Bytecode::k##Name:                                         \
    return GetResultLocationAndSizeForBytecode<Bytecode::k##Name, \
                                               __VA_ARGS__>(iterator_);
    BYTECODE_LIST(CASE, CASE)
#undef CASE
  }
  UNREACHABLE();
}

#ifdef DEBUG
bool MaglevGraphBuilder::HasOutputRegister(interpreter::Register reg) const {
  interpreter::Bytecode bytecode = iterator_.current_bytecode();
  if (reg == interpreter::Register::virtual_accumulator()) {
    return interpreter::Bytecodes::WritesAccumulator(bytecode);
  }
  for (int i = 0; i < interpreter::Bytecodes::NumberOfOperands(bytecode); ++i) {
    if (interpreter::Bytecodes::IsRegisterOutputOperandType(
            interpreter::Bytecodes::GetOperandType(bytecode, i))) {
      interpreter::Register operand_reg = iterator_.GetRegisterOperand(i);
      int operand_range = iterator_.GetRegisterOperandRange(i);
      if (base::IsInRange(reg.index(), operand_reg.index(),
                          operand_reg.index() + operand_range)) {
        return true;
      }
    }
  }
  return false;
}
#endif

DeoptFrame* MaglevGraphBuilder::GetParentDeoptFrame() {
  if (parent_ == nullptr) return nullptr;
  if (parent_deopt_frame_ == nullptr) {
    // The parent resumes after the call, which is roughly equivalent to a lazy
    // deopt. Use the helper function directly so that we can mark the
    // accumulator as dead (since it'll be overwritten by this function's
    // return value anyway).
    // TODO(leszeks): This is true for our current set of
    // inlinings/continuations, but there might be cases in the future where it
    // isn't. We may need to store the relevant overwritten register in
    // LazyDeoptFrameScope.
    DCHECK(interpreter::Bytecodes::WritesAccumulator(
        parent_->iterator_.current_bytecode()));

    parent_deopt_frame_ =
        zone()->New<DeoptFrame>(parent_->GetDeoptFrameForLazyDeoptHelper(
            interpreter::Register::invalid_value(), 0,
            parent_->current_deopt_scope_, true));
    // Only create InlinedArgumentsDeoptFrame if we have a mismatch between
    // formal parameter and arguments count.
    if (HasMismatchedArgumentAndParameterCount()) {
      parent_deopt_frame_ = zone()->New<InlinedArgumentsDeoptFrame>(
          *compilation_unit_, caller_bytecode_offset_, GetClosure(),
          inlined_arguments_, parent_deopt_frame_);
      AddDeoptUse(GetClosure());
      for (ValueNode* arg :
           parent_deopt_frame_->as_inlined_arguments().arguments()) {
        AddDeoptUse(arg);
      }
    }
  }
  return parent_deopt_frame_;
}

DeoptFrame MaglevGraphBuilder::GetLatestCheckpointedFrame() {
  if (in_prologue_) {
    return GetDeoptFrameForEntryStackCheck();
  }
  if (!latest_checkpointed_frame_) {
    current_interpreter_frame_.virtual_objects().Snapshot();
    latest_checkpointed_frame_.emplace(InterpretedDeoptFrame(
        *compilation_unit_,
        zone()->New<CompactInterpreterFrameState>(
            *compilation_unit_, GetInLiveness(), current_interpreter_frame_),
        GetClosure(), BytecodeOffset(iterator_.current_offset()),
        current_source_position_, GetParentDeoptFrame()));

    latest_checkpointed_frame_->as_interpreted().frame_state()->ForEachValue(
        *compilation_unit_,
        [&](ValueNode* node, interpreter::Register) { AddDeoptUse(node); });
    AddDeoptUse(latest_checkpointed_frame_->as_interpreted().closure());

    // Skip lazy deopt builtin continuations.
    const DeoptFrameScope* deopt_scope = current_deopt_scope_;
    while (deopt_scope != nullptr &&
           deopt_scope->IsLazyDeoptContinuationFrame()) {
      deopt_scope = deopt_scope->parent();
    }

    if (deopt_scope != nullptr) {
      // Support exactly one eager deopt builtin continuation. This can be
      // expanded in the future if necessary.
      DCHECK_NULL(deopt_scope->parent());
      DCHECK_EQ(deopt_scope->data().tag(),
                DeoptFrame::FrameType::kBuiltinContinuationFrame);
#ifdef DEBUG
      if (deopt_scope->data().tag() ==
          DeoptFrame::FrameType::kBuiltinContinuationFrame) {
        const DeoptFrame::BuiltinContinuationFrameData& frame =
            deopt_scope->data().get<DeoptFrame::BuiltinContinuationFrameData>();
        if (frame.maybe_js_target) {
          int stack_parameter_count =
              Builtins::GetStackParameterCount(frame.builtin_id);
          DCHECK_EQ(stack_parameter_count, frame.parameters.length());
        } else {
          CallInterfaceDescriptor descriptor =
              Builtins::CallInterfaceDescriptorFor(frame.builtin_id);
          DCHECK_EQ(descriptor.GetParameterCount(), frame.parameters.length());
        }
      }
#endif

      // Wrap the above frame in the scope frame.
      latest_checkpointed_frame_.emplace(
          deopt_scope->data(),
          zone()->New<DeoptFrame>(*latest_checkpointed_frame_));
    }
  }
  return *latest_checkpointed_frame_;
}

DeoptFrame MaglevGraphBuilder::GetDeoptFrameForLazyDeopt(
    interpreter::Register result_location, int result_size) {
  return GetDeoptFrameForLazyDeoptHelper(result_location, result_size,
                                         current_deopt_scope_, false);
}

DeoptFrame MaglevGraphBuilder::GetDeoptFrameForLazyDeoptHelper(
    interpreter::Register result_location, int result_size,
    DeoptFrameScope* scope, bool mark_accumulator_dead) {
  if (scope == nullptr) {
    compiler::BytecodeLivenessState* liveness =
        zone()->New<compiler::BytecodeLivenessState>(*GetOutLiveness(), zone());
    // Remove result locations from liveness.
    if (result_location == interpreter::Register::virtual_accumulator()) {
      DCHECK_EQ(result_size, 1);
      liveness->MarkAccumulatorDead();
      mark_accumulator_dead = false;
    } else {
      DCHECK(!result_location.is_parameter());
      for (int i = 0; i < result_size; i++) {
        liveness->MarkRegisterDead(result_location.index() + i);
      }
    }
    // Explicitly drop the accumulator if needed.
    if (mark_accumulator_dead && liveness->AccumulatorIsLive()) {
      liveness->MarkAccumulatorDead();
    }
    current_interpreter_frame_.virtual_objects().Snapshot();
    InterpretedDeoptFrame ret(
        *compilation_unit_,
        zone()->New<CompactInterpreterFrameState>(*compilation_unit_, liveness,
                                                  current_interpreter_frame_),
        GetClosure(), BytecodeOffset(iterator_.current_offset()),
        current_source_position_, GetParentDeoptFrame());
    ret.frame_state()->ForEachValue(
        *compilation_unit_, [this](ValueNode* node, interpreter::Register reg) {
          // Receiver and closure values have to be materialized, even if
          // they don't otherwise escape.
          if (reg == interpreter::Register::receiver() ||
              reg == interpreter::Register::function_closure()) {
            node->add_use();
          } else {
            AddDeoptUse(node);
          }
        });
    AddDeoptUse(ret.closure());
    return ret;
  }

  // Currently only support builtin continuations for bytecodes that write to
  // the accumulator
  DCHECK(interpreter::Bytecodes::WritesOrClobbersAccumulator(
      iterator_.current_bytecode()));

#ifdef DEBUG
  if (scope->data().tag() == DeoptFrame::FrameType::kBuiltinContinuationFrame) {
    const DeoptFrame::BuiltinContinuationFrameData& frame =
        current_deopt_scope_->data()
            .get<DeoptFrame::BuiltinContinuationFrameData>();
    if (frame.maybe_js_target) {
      int stack_parameter_count =
          Builtins::GetStackParameterCount(frame.builtin_id);
      // The deopt input value is passed by the deoptimizer, so shouldn't be a
      // parameter here.
      DCHECK_EQ(stack_parameter_count, frame.parameters.length() + 1);
    } else {
      CallInterfaceDescriptor descriptor =
          Builtins::CallInterfaceDescriptorFor(frame.builtin_id);
      // The deopt input value is passed by the deoptimizer, so shouldn't be a
      // parameter here.
      DCHECK_EQ(descriptor.GetParameterCount(), frame.parameters.length() + 1);
      // The deopt input value is passed on the stack.
      DCHECK_GT(descriptor.GetStackParameterCount(), 0);
    }
  }
#endif

  // Mark the accumulator dead in parent frames since we know that the
  // continuation will write it.
  return DeoptFrame(scope->data(),
                    zone()->New<DeoptFrame>(GetDeoptFrameForLazyDeoptHelper(
                        result_location, result_size, scope->parent(),
                        scope->data().tag() ==
                            DeoptFrame::FrameType::kBuiltinContinuationFrame)));
}

InterpretedDeoptFrame MaglevGraphBuilder::GetDeoptFrameForEntryStackCheck() {
  if (entry_stack_check_frame_) return *entry_stack_check_frame_;
  DCHECK_EQ(iterator_.current_offset(), entrypoint_);
  DCHECK_NULL(parent_);
  entry_stack_check_frame_.emplace(
      *compilation_unit_,
      zone()->New<CompactInterpreterFrameState>(
          *compilation_unit_,
          GetInLivenessFor(graph_->is_osr() ? bailout_for_entrypoint() : 0),
          current_interpreter_frame_),
      GetClosure(), BytecodeOffset(bailout_for_entrypoint()),
      current_source_position_, nullptr);

  (*entry_stack_check_frame_)
      .frame_state()
      ->ForEachValue(
          *compilation_unit_,
          [&](ValueNode* node, interpreter::Register) { AddDeoptUse(node); });
  AddDeoptUse((*entry_stack_check_frame_).closure());
  return *entry_stack_check_frame_;
}

ValueNode* MaglevGraphBuilder::GetTaggedValue(
    ValueNode* value, UseReprHintRecording record_use_repr_hint) {
  if (V8_LIKELY(record_use_repr_hint == UseReprHintRecording::kRecord)) {
    RecordUseReprHintIfPhi(value, UseRepresentation::kTagged);
  }

  ValueRepresentation representation =
      value->properties().value_representation();
  if (representation == ValueRepresentation::kTagged) return value;

  if (Int32Constant* as_int32_constant = value->TryCast<Int32Constant>();
      as_int32_constant && Smi::IsValid(as_int32_constant->value())) {
    return GetSmiConstant(as_int32_constant->value());
  }

  NodeInfo* node_info = GetOrCreateInfoFor(value);
  auto& alternative = node_info->alternative();

  if (ValueNode* alt = alternative.tagged()) {
    return alt;
  }

  switch (representation) {
    case ValueRepresentation::kInt32: {
      if (NodeTypeIsSmi(node_info->type())) {
        return alternative.set_tagged(AddNewNode<UnsafeSmiTagInt32>({value}));
      }
      return alternative.set_tagged(AddNewNode<Int32ToNumber>({value}));
    }
    case ValueRepresentation::kUint32: {
      if (NodeTypeIsSmi(node_info->type())) {
        return alternative.set_tagged(AddNewNode<UnsafeSmiTagUint32>({value}));
      }
      return alternative.set_tagged(AddNewNode<Uint32ToNumber>({value}));
    }
    case ValueRepresentation::kFloat64: {
      return alternative.set_tagged(AddNewNode<Float64ToTagged>(
          {value}, Float64ToTagged::ConversionMode::kCanonicalizeSmi));
    }
    case ValueRepresentation::kHoleyFloat64: {
      return alternative.set_tagged(AddNewNode<HoleyFloat64ToTagged>(
          {value}, HoleyFloat64ToTagged::ConversionMode::kForceHeapNumber));
    }

    case ValueRepresentation::kIntPtr:
      if (NodeTypeIsSmi(node_info->type())) {
        return alternative.set_tagged(AddNewNode<UnsafeSmiTagIntPtr>({value}));
      }
      return alternative.set_tagged(AddNewNode<IntPtrToNumber>({value}));

    case ValueRepresentation::kTagged:
      UNREACHABLE();
  }
  UNREACHABLE();
}

ReduceResult MaglevGraphBuilder::GetSmiValue(
    ValueNode* value, UseReprHintRecording record_use_repr_hint) {
  if (V8_LIKELY(record_use_repr_hint == UseReprHintRecording::kRecord)) {
    RecordUseReprHintIfPhi(value, UseRepresentation::kTagged);
  }

  NodeInfo* node_info = GetOrCreateInfoFor(value);

  ValueRepresentation representation =
      value->properties().value_representation();
  if (representation == ValueRepresentation::kTagged) {
    return BuildCheckSmi(value, !value->Is<Phi>());
  }

  auto& alternative = node_info->alternative();

  if (ValueNode* alt = alternative.tagged()) {
    // HoleyFloat64ToTagged does not canonicalize Smis by default, since it can
    // be expensive. If we are reading a Smi value, we should try to
    // canonicalize now.
    if (HoleyFloat64ToTagged* conversion_node =
            alt->TryCast<HoleyFloat64ToTagged>()) {
      conversion_node->SetMode(
          HoleyFloat64ToTagged::ConversionMode::kCanonicalizeSmi);
    }
    return BuildCheckSmi(alt, !value->Is<Phi>());
  }

  switch (representation) {
    case ValueRepresentation::kInt32: {
      if (NodeTypeIsSmi(node_info->type())) {
        return alternative.set_tagged(AddNewNode<UnsafeSmiTagInt32>({value}));
      }
      return alternative.set_tagged(AddNewNode<CheckedSmiTagInt32>({value}));
    }
    case ValueRepresentation::kUint32: {
      if (NodeTypeIsSmi(node_info->type())) {
        return alternative.set_tagged(AddNewNode<UnsafeSmiTagUint32>({value}));
      }
      return alternative.set_tagged(AddNewNode<CheckedSmiTagUint32>({value}));
    }
    case ValueRepresentation::kFloat64: {
      return alternative.set_tagged(AddNewNode<CheckedSmiTagFloat64>({value}));
    }
    case ValueRepresentation::kHoleyFloat64: {
      return alternative.set_tagged(AddNewNode<CheckedSmiTagFloat64>({value}));
    }
    case ValueRepresentation::kIntPtr:
      return alternative.set_tagged(AddNewNode<CheckedSmiTagIntPtr>({value}));
    case ValueRepresentation::kTagged:
      UNREACHABLE();
  }
  UNREACHABLE();
}

namespace {
CheckType GetCheckType(NodeType type) {
  return NodeTypeIs(type, NodeType::kAnyHeapObject)
             ? CheckType::kOmitHeapObjectCheck
             : CheckType::kCheckHeapObject;
}
}  // namespace

ValueNode* MaglevGraphBuilder::GetInternalizedString(
    interpreter::Register reg) {
  ValueNode* node = current_interpreter_frame_.get(reg);
  NodeType old_type;
  if (CheckType(node, NodeType::kInternalizedString, &old_type)) return node;
  NodeInfo* known_info = GetOrCreateInfoFor(node);
  if (known_info->alternative().checked_value()) {
    node = known_info->alternative().checked_value();
    if (CheckType(node, NodeType::kInternalizedString, &old_type)) return node;
  }

  if (!NodeTypeIs(old_type, NodeType::kString)) {
    known_info->CombineType(NodeType::kString);
  }

  // This node may unwrap ThinStrings.
  ValueNode* maybe_unwrapping_node =
      AddNewNode<CheckedInternalizedString>({node}, GetCheckType(old_type));
  known_info->alternative().set_checked_value(maybe_unwrapping_node);

  current_interpreter_frame_.set(reg, maybe_unwrapping_node);
  return maybe_unwrapping_node;
}

ValueNode* MaglevGraphBuilder::GetTruncatedInt32ForToNumber(
    ValueNode* value, NodeType allowed_input_type,
    TaggedToFloat64ConversionType conversion_type) {
  RecordUseReprHintIfPhi(value, UseRepresentation::kTruncatedInt32);

  ValueRepresentation representation =
      value->properties().value_representation();
  if (representation == ValueRepresentation::kInt32) return value;
  if (representation == ValueRepresentation::kUint32) {
    // This node is cheap (no code gen, just a bitcast), so don't cache it.
    return AddNewNode<TruncateUint32ToInt32>({value});
  }

  // Process constants first to avoid allocating NodeInfo for them.
  switch (value->opcode()) {
    case Opcode::kConstant: {
      compiler::ObjectRef object = value->Cast<Constant>()->object();
      if (!object.IsHeapNumber()) break;
      int32_t truncated_value = DoubleToInt32(object.AsHeapNumber().value());
      if (!Smi::IsValid(truncated_value)) break;
      return GetInt32Constant(truncated_value);
    }
    case Opcode::kSmiConstant:
      return GetInt32Constant(value->Cast<SmiConstant>()->value().value());
    case Opcode::kRootConstant: {
      Tagged<Object> root_object =
          local_isolate_->root(value->Cast<RootConstant>()->index());
      if (!IsOddball(root_object, local_isolate_)) break;
      int32_t truncated_value =
          DoubleToInt32(Cast<Oddball>(root_object)->to_number_raw());
      // All oddball ToNumber truncations are valid Smis.
      DCHECK(Smi::IsValid(truncated_value));
      return GetInt32Constant(truncated_value);
    }
    case Opcode::kFloat64Constant: {
      int32_t truncated_value =
          DoubleToInt32(value->Cast<Float64Constant>()->value().get_scalar());
      if (!Smi::IsValid(truncated_value)) break;
      return GetInt32Constant(truncated_value);
    }

    // We could emit unconditional eager deopts for other kinds of constant, but
    // it's not necessary, the appropriate checking conversion nodes will deopt.
    default:
      break;
  }

  NodeInfo* node_info = GetOrCreateInfoFor(value);
  auto& alternative = node_info->alternative();

  // If there is an int32_alternative, then that works as a truncated value
  // too.
  if (ValueNode* alt = alternative.int32()) {
    return alt;
  }
  if (ValueNode* alt = alternative.truncated_int32_to_number()) {
    return alt;
  }

  switch (representation) {
    case ValueRepresentation::kTagged: {
      NodeType old_type;
      EnsureType(value, allowed_input_type, &old_type);
      if (NodeTypeIsSmi(old_type)) {
        // Smi untagging can be cached as an int32 alternative, not just a
        // truncated alternative.
        return alternative.set_int32(BuildSmiUntag(value));
      }
      if (allowed_input_type == NodeType::kSmi) {
        return alternative.set_int32(AddNewNode<CheckedSmiUntag>({value}));
      }
      if (NodeTypeIs(old_type, allowed_input_type)) {
        return alternative.set_truncated_int32_to_number(
            AddNewNode<TruncateNumberOrOddballToInt32>({value},
                                                       conversion_type));
      }
      return alternative.set_truncated_int32_to_number(
          AddNewNode<CheckedTruncateNumberOrOddballToInt32>({value},
                                                            conversion_type));
    }
    case ValueRepresentation::kFloat64:
    // Ignore conversion_type for HoleyFloat64, and treat them like Float64.
    // ToNumber of undefined is anyway a NaN, so we'll simply truncate away
    // the NaN-ness of the hole, and don't need to do extra oddball checks so
    // we can ignore the hint (though we'll miss updating the feedback).
    case ValueRepresentation::kHoleyFloat64: {
      return alternative.set_truncated_int32_to_number(
          AddNewNode<TruncateFloat64ToInt32>({value}));
    }

    case ValueRepresentation::kIntPtr: {
      // This is not an efficient implementation, but this only happens in
      // corner cases.
      ValueNode* value_to_number = AddNewNode<IntPtrToNumber>({value});
      return alternative.set_truncated_int32_to_number(
          AddNewNode<TruncateNumberOrOddballToInt32>(
              {value_to_number}, TaggedToFloat64ConversionType::kOnlyNumber));
    }
    case ValueRepresentation::kInt32:
    case ValueRepresentation::kUint32:
      UNREACHABLE();
  }
  UNREACHABLE();
}

std::optional<int32_t> MaglevGraphBuilder::TryGetInt32Constant(
    ValueNode* value) {
  switch (value->opcode()) {
    case Opcode::kInt32Constant:
      return value->Cast<Int32Constant>()->value();
    case Opcode::kUint32Constant: {
      uint32_t uint32_value = value->Cast<Uint32Constant>()->value();
      if (uint32_value <= INT32_MAX) {
        return static_cast<int32_t>(uint32_value);
      }
      return {};
    }
    case Opcode::kSmiConstant:
      return value->Cast<SmiConstant>()->value().value();
    case Opcode::kFloat64Constant: {
      double double_value =
          value->Cast<Float64Constant>()->value().get_scalar();
      if (!IsInt32Double(double_value)) return {};
      return FastD2I(value->Cast<Float64Constant>()->value().get_scalar());
    }
    default:
      break;
  }
  if (auto c = TryGetConstantAlternative(value)) {
    return TryGetInt32Constant(*c);
  }
  return {};
}

std::optional<uint32_t> MaglevGraphBuilder::TryGetUint32Constant(
    ValueNode* value) {
  switch (value->opcode()) {
    case Opcode::kInt32Constant: {
      int32_t int32_value = value->Cast<Int32Constant>()->value();
      if (int32_value >= 0) {
        return static_cast<uint32_t>(int32_value);
      }
      return {};
    }
    case Opcode::kUint32Constant:
      return value->Cast<Uint32Constant>()->value();
    case Opcode::kSmiConstant: {
      int32_t smi_value = value->Cast<SmiConstant>()->value().value();
      if (smi_value >= 0) {
        return static_cast<uint32_t>(smi_value);
      }
      return {};
    }
    case Opcode::kFloat64Constant: {
      double double_value =
          value->Cast<Float64Constant>()->value().get_scalar();
      if (!IsUint32Double(double_value)) return {};
      return FastD2UI(value->Cast<Float64Constant>()->value().get_scalar());
    }
    default:
      break;
  }
  if (auto c = TryGetConstantAlternative(value)) {
    return TryGetUint32Constant(*c);
  }
  return {};
}

ValueNode* MaglevGraphBuilder::GetInt32(ValueNode* value,
                                        bool can_be_heap_number) {
  RecordUseReprHintIfPhi(value, UseRepresentation::kInt32);

  ValueRepresentation representation =
      value->properties().value_representation();
  if (representation == ValueRepresentation::kInt32) return value;

  // Process constants first to avoid allocating NodeInfo for them.
  if (auto cst = TryGetInt32Constant(value)) {
    return GetInt32Constant(cst.value());
  }
  // We could emit unconditional eager deopts for other kinds of constant, but
  // it's not necessary, the appropriate checking conversion nodes will deopt.

  NodeInfo* node_info = GetOrCreateInfoFor(value);
  auto& alternative = node_info->alternative();

  if (ValueNode* alt = alternative.int32()) {
    return alt;
  }

  switch (representation) {
    case ValueRepresentation::kTagged: {
      if (can_be_heap_number && !CheckType(value, NodeType::kSmi)) {
        return alternative.set_int32(AddNewNode<CheckedNumberToInt32>({value}));
      }
      return alternative.set_int32(BuildSmiUntag(value));
    }
    case ValueRepresentation::kUint32: {
      if (node_info->is_smi()) {
        return alternative.set_int32(
            AddNewNode<TruncateUint32ToInt32>({value}));
      }
      return alternative.set_int32(AddNewNode<CheckedUint32ToInt32>({value}));
    }
    case ValueRepresentation::kFloat64:
    // The check here will also work for the hole NaN, so we can treat
    // HoleyFloat64 as Float64.
    case ValueRepresentation::kHoleyFloat64: {
      return alternative.set_int32(
          AddNewNode<CheckedTruncateFloat64ToInt32>({value}));
    }

    case ValueRepresentation::kIntPtr:
      return alternative.set_int32(AddNewNode<CheckedIntPtrToInt32>({value}));

    case ValueRepresentation::kInt32:
      UNREACHABLE();
  }
  UNREACHABLE();
}

std::optional<double> MaglevGraphBuilder::TryGetFloat64Constant(
    ValueNode* value, TaggedToFloat64ConversionType conversion_type) {
  switch (value->opcode()) {
    case Opcode::kConstant: {
      compiler::ObjectRef object = value->Cast<Constant>()->object();
      if (object.IsHeapNumber()) {
        return object.AsHeapNumber().value();
      }
      // Oddballs should be RootConstants.
      DCHECK(!IsOddball(*object.object()));
      return {};
    }
    case Opcode::kInt32Constant:
      return value->Cast<Int32Constant>()->value();
    case Opcode::kSmiConstant:
      return value->Cast<SmiConstant>()->value().value();
    case Opcode::kFloat64Constant:
      return value->Cast<Float64Constant>()->value().get_scalar();
    case Opcode::kRootConstant: {
      Tagged<Object> root_object =
          local_isolate_->root(value->Cast<RootConstant>()->index());
      if (conversion_type == TaggedToFloat64ConversionType::kNumberOrBoolean &&
          IsBoolean(root_object)) {
        return Cast<Oddball>(root_object)->to_number_raw();
      }
      if (conversion_type == TaggedToFloat64ConversionType::kNumberOrOddball &&
          IsOddball(root_object)) {
        return Cast<Oddball>(root_object)->to_number_raw();
      }
      if (IsHeapNumber(root_object)) {
        return Cast<HeapNumber>(root_object)->value();
      }
      return {};
    }
    default:
      break;
  }
  if (auto c = TryGetConstantAlternative(value)) {
    return TryGetFloat64Constant(*c, conversion_type);
  }
  return {};
}

ValueNode* MaglevGraphBuilder::GetFloat64(ValueNode* value) {
  RecordUseReprHintIfPhi(value, UseRepresentation::kFloat64);
  return GetFloat64ForToNumber(value, NodeType::kNumber,
                               TaggedToFloat64ConversionType::kOnlyNumber);
}

ValueNode* MaglevGraphBuilder::GetFloat64ForToNumber(
    ValueNode* value, NodeType allowed_input_type,
    TaggedToFloat64ConversionType conversion_type) {
  ValueRepresentation representation =
      value->properties().value_representation();
  if (representation == ValueRepresentation::kFloat64) return value;

  // Process constants first to avoid allocating NodeInfo for them.
  if (auto cst = TryGetFloat64Constant(value, conversion_type)) {
    return GetFloat64Constant(cst.value());
  }
  // We could emit unconditional eager deopts for other kinds of constant, but
  // it's not necessary, the appropriate checking conversion nodes will deopt.

  NodeInfo* node_info = GetOrCreateInfoFor(value);
  auto& alternative = node_info->alternative();

  if (ValueNode* alt = alternative.float64()) {
    return alt;
  }

  switch (representation) {
    case ValueRepresentation::kTagged: {
      auto combined_type = CombineType(allowed_input_type, node_info->type());
      if (NodeTypeIs(combined_type, NodeType::kSmi)) {
        // Get the float64 value of a Smi value its int32 representation.
        return GetFloat64(GetInt32(value));
      }
      if (NodeTypeIs(combined_type, NodeType::kNumber)) {
        // Number->Float64 conversions are exact alternatives, so they can
        // also become the canonical float64_alternative.
        return alternative.set_float64(BuildNumberOrOddballToFloat64(
            value, NodeType::kNumber,
            TaggedToFloat64ConversionType::kOnlyNumber));
      }
      if (NodeTypeIs(combined_type, NodeType::kNumberOrOddball)) {
        // NumberOrOddball->Float64 conversions are not exact alternatives,
        // since they lose the information that this is an oddball, so they
        // can only become the canonical float64_alternative if they are a
        // known number (and therefore not oddball).
        return BuildNumberOrOddballToFloat64(value, combined_type,
                                             conversion_type);
      }
      // The type is impossible. We could generate an unconditional deopt here,
      // but it's too invasive. So we just generate a check which will always
      // deopt.
      return BuildNumberOrOddballToFloat64(value, allowed_input_type,
                                           conversion_type);
    }
    case ValueRepresentation::kInt32:
      return alternative.set_float64(AddNewNode<ChangeInt32ToFloat64>({value}));
    case ValueRepresentation::kUint32:
      return alternative.set_float64(
          AddNewNode<ChangeUint32ToFloat64>({value}));
    case ValueRepresentation::kHoleyFloat64: {
      switch (allowed_input_type) {
        case NodeType::kSmi:
        case NodeType::kNumber:
        case NodeType::kNumberOrBoolean:
          // Number->Float64 conversions are exact alternatives, so they can
          // also become the canonical float64_alternative. The HoleyFloat64
          // representation can represent undefined but no other oddballs, so
          // booleans cannot occur here and kNumberOrBoolean can be grouped with
          // kNumber.
          return alternative.set_float64(
              AddNewNode<CheckedHoleyFloat64ToFloat64>({value}));
        case NodeType::kNumberOrOddball:
          // NumberOrOddball->Float64 conversions are not exact alternatives,
          // since they lose the information that this is an oddball, so they
          // cannot become the canonical float64_alternative.
          return AddNewNode<HoleyFloat64ToMaybeNanFloat64>({value});
        default:
          UNREACHABLE();
      }
    }
    case ValueRepresentation::kIntPtr:
      return alternative.set_float64(
          AddNewNode<ChangeIntPtrToFloat64>({value}));
    case ValueRepresentation::kFloat64:
      UNREACHABLE();
  }
  UNREACHABLE();
}

ValueNode* MaglevGraphBuilder::GetHoleyFloat64ForToNumber(
    ValueNode* value, NodeType allowed_input_type,
    TaggedToFloat64ConversionType conversion_type) {
  RecordUseReprHintIfPhi(value, UseRepresentation::kHoleyFloat64);
  ValueRepresentation representation =
      value->properties().value_representation();
  // Ignore the hint for
  if (representation == ValueRepresentation::kHoleyFloat64) return value;
  return GetFloat64ForToNumber(value, allowed_input_type, conversion_type);
}

namespace {
int32_t ClampToUint8(int32_t value) {
  if (value < 0) return 0;
  if (value > 255) return 255;
  return value;
}
}  // namespace

ValueNode* MaglevGraphBuilder::GetUint8ClampedForToNumber(ValueNode* value) {
  switch (value->properties().value_representation()) {
    case ValueRepresentation::kIntPtr:
      // This is not an efficient implementation, but this only happens in
      // corner cases.
      return AddNewNode<CheckedNumberToUint8Clamped>(
          {AddNewNode<IntPtrToNumber>({value})});
    case ValueRepresentation::kTagged: {
      if (SmiConstant* constant = value->TryCast<SmiConstant>()) {
        return GetInt32Constant(ClampToUint8(constant->value().value()));
      }
      NodeInfo* info = known_node_aspects().TryGetInfoFor(value);
      if (info && info->alternative().int32()) {
        return AddNewNode<Int32ToUint8Clamped>({info->alternative().int32()});
      }
      return AddNewNode<CheckedNumberToUint8Clamped>({value});
    }
    // HoleyFloat64 is treated like Float64. ToNumber of undefined is anyway a
    // NaN, so we'll simply truncate away the NaN-ness of the hole, and don't
    // need to do extra oddball checks (though we'll miss updating the
    // feedback).
    case ValueRepresentation::kFloat64:
    case ValueRepresentation::kHoleyFloat64:
      // TODO(leszeks): Handle Float64Constant, which requires the correct
      // rounding for clamping.
      return AddNewNode<Float64ToUint8Clamped>({value});
    case ValueRepresentation::kInt32:
      if (Int32Constant* constant = value->TryCast<Int32Constant>()) {
        return GetInt32Constant(ClampToUint8(constant->value()));
      }
      return AddNewNode<Int32ToUint8Clamped>({value});
    case ValueRepresentation::kUint32:
      return AddNewNode<Uint32ToUint8Clamped>({value});
  }
  UNREACHABLE();
}

namespace {
template <Operation kOperation>
struct NodeForOperationHelper;

#define NODE_FOR_OPERATION_HELPER(Name)               \
  template <>                                         \
  struct NodeForOperationHelper<Operation::k##Name> { \
    using generic_type = Generic##Name;               \
  };
OPERATION_LIST(NODE_FOR_OPERATION_HELPER)
#undef NODE_FOR_OPERATION_HELPER

template <Operation kOperation>
using GenericNodeForOperation =
    typename NodeForOperationHelper<kOperation>::generic_type;

// Bitwise operations reinterprets the numeric input as Int32 bits for a
// bitwise operation, which means we want to do slightly different conversions.
template <Operation kOperation>
constexpr bool BinaryOperationIsBitwiseInt32() {
  switch (kOperation) {
    case Operation::kBitwiseNot:
    case Operation::kBitwiseAnd:
    case Operation::kBitwiseOr:
    case Operation::kBitwiseXor:
    case Operation::kShiftLeft:
    case Operation::kShiftRight:
    case Operation::kShiftRightLogical:
      return true;
    default:
      return false;
  }
}
}  // namespace

// MAP_OPERATION_TO_NODES are tuples with the following format:
// - Operation name,
// - Int32 operation node,
// - Identity of int32 operation (e.g, 0 for add/sub and 1 for mul/div), if it
//   exists, or otherwise {}.
#define MAP_BINARY_OPERATION_TO_INT32_NODE(V) \
  V(Add, Int32AddWithOverflow, 0)             \
  V(Subtract, Int32SubtractWithOverflow, 0)   \
  V(Multiply, Int32MultiplyWithOverflow, 1)   \
  V(Divide, Int32DivideWithOverflow, 1)       \
  V(Modulus, Int32ModulusWithOverflow, {})    \
  V(BitwiseAnd, Int32BitwiseAnd, ~0)          \
  V(BitwiseOr, Int32BitwiseOr, 0)             \
  V(BitwiseXor, Int32BitwiseXor, 0)           \
  V(ShiftLeft, Int32ShiftLeft, 0)             \
  V(ShiftRight, Int32ShiftRight, 0)           \
  V(ShiftRightLogical, Int32ShiftRightLogical, {})

#define MAP_UNARY_OPERATION_TO_INT32_NODE(V) \
  V(BitwiseNot, Int32BitwiseNot)             \
  V(Increment, Int32IncrementWithOverflow)   \
  V(Decrement, Int32DecrementWithOverflow)   \
  V(Negate, Int32NegateWithOverflow)

// MAP_OPERATION_TO_FLOAT64_NODE are tuples with the following format:
// (Operation name, Float64 operation node).
#define MAP_OPERATION_TO_FLOAT64_NODE(V) \
  V(Add, Float64Add)                     \
  V(Subtract, Float64Subtract)           \
  V(Multiply, Float64Multiply)           \
  V(Divide, Float64Divide)               \
  V(Modulus, Float64Modulus)             \
  V(Exponentiate, Float64Exponentiate)

template <Operation kOperation>
static constexpr std::optional<int> Int32Identity() {
  switch (kOperation) {
#define CASE(op, _, identity) \
  case Operation::k##op:      \
    return identity;
    MAP_BINARY_OPERATION_TO_INT32_NODE(CASE)
#undef CASE
    default:
      UNREACHABLE();
  }
}

namespace {
template <Operation kOperation>
struct Int32NodeForHelper;
#define SPECIALIZATION(op, OpNode, ...)         \
  template <>                                   \
  struct Int32NodeForHelper<Operation::k##op> { \
    using type = OpNode;                        \
  };
MAP_UNARY_OPERATION_TO_INT32_NODE(SPECIALIZATION)
MAP_BINARY_OPERATION_TO_INT32_NODE(SPECIALIZATION)
#undef SPECIALIZATION

template <Operation kOperation>
using Int32NodeFor = typename Int32NodeForHelper<kOperation>::type;

template <Operation kOperation>
struct Float64NodeForHelper;
#define SPECIALIZATION(op, OpNode)                \
  template <>                                     \
  struct Float64NodeForHelper<Operation::k##op> { \
    using type = OpNode;                          \
  };
MAP_OPERATION_TO_FLOAT64_NODE(SPECIALIZATION)
#undef SPECIALIZATION

template <Operation kOperation>
using Float64NodeFor = typename Float64NodeForHelper<kOperation>::type;
}  // namespace

template <Operation kOperation>
void MaglevGraphBuilder::BuildGenericUnaryOperationNode() {
  FeedbackSlot slot_index = GetSlotOperand(0);
  ValueNode* value = GetAccumulator();
  SetAccumulator(AddNewNode<GenericNodeForOperation<kOperation>>(
      {value}, compiler::FeedbackSource{feedback(), slot_index}));
}

template <Operation kOperation>
void MaglevGraphBuilder::BuildGenericBinaryOperationNode() {
  ValueNode* left = LoadRegister(0);
  ValueNode* right = GetAccumulator();
  FeedbackSlot slot_index = GetSlotOperand(1);
  SetAccumulator(AddNewNode<GenericNodeForOperation<kOperation>>(
      {left, right}, compiler::FeedbackSource{feedback(), slot_index}));
}

template <Operation kOperation>
void MaglevGraphBuilder::BuildGenericBinarySmiOperationNode() {
  ValueNode* left = GetAccumulator();
  int constant = iterator_.GetImmediateOperand(0);
  ValueNode* right = GetSmiConstant(constant);
  FeedbackSlot slot_index = GetSlotOperand(1);
  SetAccumulator(AddNewNode<GenericNodeForOperation<kOperation>>(
      {left, right}, compiler::FeedbackSource{feedback(), slot_index}));
}

template <Operation kOperation>
MaybeReduceResult MaglevGraphBuilder::TryFoldInt32UnaryOperation(
    ValueNode* node) {
  auto cst = TryGetInt32Constant(node);
  if (!cst.has_value()) return {};
  switch (kOperation) {
    case Operation::kBitwiseNot:
      return GetInt32Constant(~cst.value());
    case Operation::kIncrement:
      if (cst.value() < INT32_MAX) {
        return GetInt32Constant(cst.value() + 1);
      }
      return {};
    case Operation::kDecrement:
      if (cst.value() > INT32_MIN) {
        return GetInt32Constant(cst.value() - 1);
      }
      return {};
    case Operation::kNegate:
      if (cst.value() == 0) {
        return {};
      }
      if (cst.value() != INT32_MIN) {
        return GetInt32Constant(-cst.value());
      }
      return {};
    default:
      UNREACHABLE();
  }
}

template <Operation kOperation>
ReduceResult MaglevGraphBuilder::BuildInt32UnaryOperationNode() {
  // Use BuildTruncatingInt32BitwiseNotForToNumber with Smi input hint
  // for truncating operations.
  static_assert(!BinaryOperationIsBitwiseInt32<kOperation>());
  ValueNode* value = GetAccumulator();
  PROCESS_AND_RETURN_IF_DONE(TryFoldInt32UnaryOperation<kOperation>(value),
                             SetAccumulator);
  using OpNodeT = Int32NodeFor<kOperation>;
  SetAccumulator(AddNewNode<OpNodeT>({value}));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::BuildTruncatingInt32BitwiseNotForToNumber(
    NodeType allowed_input_type,
    TaggedToFloat64ConversionType conversion_type) {
  ValueNode* value =
      GetTruncatedInt32ForToNumber(current_interpreter_frame_.accumulator(),
                                   allowed_input_type, conversion_type);
  PROCESS_AND_RETURN_IF_DONE(
      TryFoldInt32UnaryOperation<Operation::kBitwiseNot>(value),
      SetAccumulator);
  SetAccumulator(AddNewNode<Int32BitwiseNot>({value}));
  return ReduceResult::Done();
}

template <Operation kOperation>
MaybeReduceResult MaglevGraphBuilder::TryFoldInt32BinaryOperation(
    ValueNode* left, ValueNode* right) {
  auto cst_right = TryGetInt32Constant(right);
  if (!cst_right.has_value()) return {};
  return TryFoldInt32BinaryOperation<kOperation>(left, cst_right.value());
}

template <Operation kOperation>
MaybeReduceResult MaglevGraphBuilder::TryFoldInt32BinaryOperation(
    ValueNode* left, int32_t cst_right) {
  auto cst_left = TryGetInt32Constant(left);
  if (!cst_left.has_value()) return {};
  switch (kOperation) {
    case Operation::kAdd: {
      int64_t result = static_cast<int64_t>(cst_left.value()) +
                       static_cast<int64_t>(cst_right);
      if (result >= INT32_MIN && result <= INT32_MAX) {
        return GetInt32Constant(static_cast<int32_t>(result));
      }
      return {};
    }
    case Operation::kSubtract: {
      int64_t result = static_cast<int64_t>(cst_left.value()) -
                       static_cast<int64_t>(cst_right);
      if (result >= INT32_MIN && result <= INT32_MAX) {
        return GetInt32Constant(static_cast<int32_t>(result));
      }
      return {};
    }
    case Operation::kMultiply: {
      int64_t result = static_cast<int64_t>(cst_left.value()) *
                       static_cast<int64_t>(cst_right);
      if (result >= INT32_MIN && result <= INT32_MAX) {
        return GetInt32Constant(static_cast<int32_t>(result));
      }
      return {};
    }
    case Operation::kModulus:
      // TODO(v8:7700): Constant fold mod.
      return {};
    case Operation::kDivide:
      // TODO(v8:7700): Constant fold division.
      return {};
    case Operation::kBitwiseAnd:
      return GetInt32Constant(cst_left.value() & cst_right);
    case Operation::kBitwiseOr:
      return GetInt32Constant(cst_left.value() | cst_right);
    case Operation::kBitwiseXor:
      return GetInt32Constant(cst_left.value() ^ cst_right);
    case Operation::kShiftLeft:
      return GetInt32Constant(cst_left.value()
                              << (static_cast<uint32_t>(cst_right) % 32));
    case Operation::kShiftRight:
      return GetInt32Constant(cst_left.value() >>
                              (static_cast<uint32_t>(cst_right) % 32));
    case Operation::kShiftRightLogical:
      return GetUint32Constant(static_cast<uint32_t>(cst_left.value()) >>
                               (static_cast<uint32_t>(cst_right) % 32));
    default:
      UNREACHABLE();
  }
}

template <Operation kOperation>
ReduceResult MaglevGraphBuilder::BuildInt32BinaryOperationNode() {
  // Use BuildTruncatingInt32BinaryOperationNodeForToNumber with Smi input hint
  // for truncating operations.
  static_assert(!BinaryOperationIsBitwiseInt32<kOperation>());
  ValueNode* left = LoadRegister(0);
  ValueNode* right = GetAccumulator();
  PROCESS_AND_RETURN_IF_DONE(
      TryFoldInt32BinaryOperation<kOperation>(left, right), SetAccumulator);
  using OpNodeT = Int32NodeFor<kOperation>;
  SetAccumulator(AddNewNode<OpNodeT>({left, right}));
  return ReduceResult::Done();
}

template <Operation kOperation>
ReduceResult
MaglevGraphBuilder::BuildTruncatingInt32BinaryOperationNodeForToNumber(
    NodeType allowed_input_type,
    TaggedToFloat64ConversionType conversion_type) {
  static_assert(BinaryOperationIsBitwiseInt32<kOperation>());
  ValueNode* left;
  ValueNode* right;
  if (IsRegisterEqualToAccumulator(0)) {
    left = right = GetTruncatedInt32ForToNumber(
        current_interpreter_frame_.get(iterator_.GetRegisterOperand(0)),
        allowed_input_type, conversion_type);
  } else {
    left = GetTruncatedInt32ForToNumber(
        current_interpreter_frame_.get(iterator_.GetRegisterOperand(0)),
        allowed_input_type, conversion_type);
    right =
        GetTruncatedInt32ForToNumber(current_interpreter_frame_.accumulator(),
                                     allowed_input_type, conversion_type);
  }
  PROCESS_AND_RETURN_IF_DONE(
      TryFoldInt32BinaryOperation<kOperation>(left, right), SetAccumulator);
  SetAccumulator(AddNewNode<Int32NodeFor<kOperation>>({left, right}));
  return ReduceResult::Done();
}

template <Operation kOperation>
ReduceResult MaglevGraphBuilder::BuildInt32BinarySmiOperationNode() {
  // Truncating Int32 nodes treat their input as a signed int32 regardless
  // of whether it's really signed or not, so we allow Uint32 by loading a
  // TruncatedInt32 value.
  static_assert(!BinaryOperationIsBitwiseInt32<kOperation>());
  ValueNode* left = GetAccumulator();
  int32_t constant = iterator_.GetImmediateOperand(0);
  if (std::optional<int>(constant) == Int32Identity<kOperation>()) {
    // Deopt if {left} is not an Int32.
    EnsureInt32(left);
    // If the constant is the unit of the operation, it already has the right
    // value, so just return.
    return ReduceResult::Done();
  }
  PROCESS_AND_RETURN_IF_DONE(
      TryFoldInt32BinaryOperation<kOperation>(left, constant), SetAccumulator);
  ValueNode* right = GetInt32Constant(constant);
  using OpNodeT = Int32NodeFor<kOperation>;
  SetAccumulator(AddNewNode<OpNodeT>({left, right}));
  return ReduceResult::Done();
}

template <Operation kOperation>
ReduceResult
MaglevGraphBuilder::BuildTruncatingInt32BinarySmiOperationNodeForToNumber(
    NodeType allowed_input_type,
    TaggedToFloat64ConversionType conversion_type) {
  static_assert(BinaryOperationIsBitwiseInt32<kOperation>());
  ValueNode* left =
      GetTruncatedInt32ForToNumber(current_interpreter_frame_.accumulator(),
                                   allowed_input_type, conversion_type);
  int32_t constant = iterator_.GetImmediateOperand(0);
  if (std::optional<int>(constant) == Int32Identity<kOperation>()) {
    // If the constant is the unit of the operation, it already has the right
    // value, so use the truncated value (if not just a conversion) and return.
    if (!left->properties().is_conversion()) {
      current_interpreter_frame_.set_accumulator(left);
    }
    return ReduceResult::Done();
  }
  PROCESS_AND_RETURN_IF_DONE(
      TryFoldInt32BinaryOperation<kOperation>(left, constant), SetAccumulator);
  ValueNode* right = GetInt32Constant(constant);
  SetAccumulator(AddNewNode<Int32NodeFor<kOperation>>({left, right}));
  return ReduceResult::Done();
}

ValueNode* MaglevGraphBuilder::GetNumberConstant(double constant) {
  if (IsSmiDouble(constant)) {
    return GetInt32Constant(FastD2I(constant));
  }
  return GetFloat64Constant(constant);
}

template <Operation kOperation>
MaybeReduceResult MaglevGraphBuilder::TryFoldFloat64UnaryOperationForToNumber(
    TaggedToFloat64ConversionType conversion_type, ValueNode* value) {
  auto cst = TryGetFloat64Constant(value, conversion_type);
  if (!cst.has_value()) return {};
  switch (kOperation) {
    case Operation::kNegate:
      return GetNumberConstant(-cst.value());
    case Operation::kIncrement:
      return GetNumberConstant(cst.value() + 1);
    case Operation::kDecrement:
      return GetNumberConstant(cst.value() - 1);
    default:
      UNREACHABLE();
  }
}

template <Operation kOperation>
MaybeReduceResult MaglevGraphBuilder::TryFoldFloat64BinaryOperationForToNumber(
    TaggedToFloat64ConversionType conversion_type, ValueNode* left,
    ValueNode* right) {
  auto cst_right = TryGetFloat64Constant(right, conversion_type);
  if (!cst_right.has_value()) return {};
  return TryFoldFloat64BinaryOperationForToNumber<kOperation>(
      conversion_type, left, cst_right.value());
}

template <Operation kOperation>
MaybeReduceResult MaglevGraphBuilder::TryFoldFloat64BinaryOperationForToNumber(
    TaggedToFloat64ConversionType conversion_type, ValueNode* left,
    double cst_right) {
  auto cst_left = TryGetFloat64Constant(left, conversion_type);
  if (!cst_left.has_value()) return {};
  switch (kOperation) {
    case Operation::kAdd:
      return GetNumberConstant(cst_left.value() + cst_right);
    case Operation::kSubtract:
      return GetNumberConstant(cst_left.value() - cst_right);
    case Operation::kMultiply:
      return GetNumberConstant(cst_left.value() * cst_right);
    case Operation::kDivide:
      return GetNumberConstant(cst_left.value() / cst_right);
    case Operation::kModulus:
      // TODO(v8:7700): Constant fold mod.
      return {};
    case Operation::kExponentiate:
      return GetNumberConstant(math::pow(cst_left.value(), cst_right));
    default:
      UNREACHABLE();
  }
}

template <Operation kOperation>
ReduceResult MaglevGraphBuilder::BuildFloat64BinarySmiOperationNodeForToNumber(
    NodeType allowed_input_type,
    TaggedToFloat64ConversionType conversion_type) {
  // TODO(v8:7700): Do constant identity folding. Make sure to normalize
  // HoleyFloat64 nodes if folded.
  ValueNode* left = GetAccumulatorHoleyFloat64ForToNumber(allowed_input_type,
                                                          conversion_type);
  double constant = static_cast<double>(iterator_.GetImmediateOperand(0));
  PROCESS_AND_RETURN_IF_DONE(
      TryFoldFloat64BinaryOperationForToNumber<kOperation>(conversion_type,
                                                           left, constant),
      SetAccumulator);
  ValueNode* right = GetFloat64Constant(constant);
  SetAccumulator(AddNewNode<Float64NodeFor<kOperation>>({left, right}));
  return ReduceResult::Done();
}

template <Operation kOperation>
ReduceResult MaglevGraphBuilder::BuildFloat64UnaryOperationNodeForToNumber(
    NodeType allowed_input_type,
    TaggedToFloat64ConversionType conversion_type) {
  // TODO(v8:7700): Do constant identity folding. Make sure to normalize
  // HoleyFloat64 nodes if folded.
  ValueNode* value = GetAccumulatorHoleyFloat64ForToNumber(allowed_input_type,
                                                           conversion_type);
  PROCESS_AND_RETURN_IF_DONE(
      TryFoldFloat64UnaryOperationForToNumber<kOperation>(conversion_type,
                                                          value),
      SetAccumulator);
  switch (kOperation) {
    case Operation::kNegate:
      SetAccumulator(AddNewNode<Float64Negate>({value}));
      break;
    case Operation::kIncrement:
      SetAccumulator(AddNewNode<Float64Add>({value, GetFloat64Constant(1)}));
      break;
    case Operation::kDecrement:
      SetAccumulator(
          AddNewNode<Float64Subtract>({value, GetFloat64Constant(1)}));
      break;
    default:
      UNREACHABLE();
  }
  return ReduceResult::Done();
}

template <Operation kOperation>
ReduceResult MaglevGraphBuilder::BuildFloat64BinaryOperationNodeForToNumber(
    NodeType allowed_input_type,
    TaggedToFloat64ConversionType conversion_type) {
  // TODO(v8:7700): Do constant identity folding. Make sure to normalize
  // HoleyFloat64 nodes if folded.
  ValueNode* left = LoadRegisterHoleyFloat64ForToNumber(0, allowed_input_type,
                                                        conversion_type);
  ValueNode* right = GetAccumulatorHoleyFloat64ForToNumber(allowed_input_type,
                                                           conversion_type);
  PROCESS_AND_RETURN_IF_DONE(
      TryFoldFloat64BinaryOperationForToNumber<kOperation>(conversion_type,
                                                           left, right),
      SetAccumulator);
  SetAccumulator(AddNewNode<Float64NodeFor<kOperation>>({left, right}));
  return ReduceResult::Done();
}

namespace {
std::tuple<NodeType, TaggedToFloat64ConversionType>
BinopHintToNodeTypeAndConversionType(BinaryOperationHint hint) {
  switch (hint) {
    case BinaryOperationHint::kSignedSmall:
      return std::make_tuple(NodeType::kSmi,
                             TaggedToFloat64ConversionType::kOnlyNumber);
    case BinaryOperationHint::kSignedSmallInputs:
    case BinaryOperationHint::kNumber:
      return std::make_tuple(NodeType::kNumber,
                             TaggedToFloat64ConversionType::kOnlyNumber);
    case BinaryOperationHint::kNumberOrOddball:
      return std::make_tuple(NodeType::kNumberOrOddball,
                             TaggedToFloat64ConversionType::kNumberOrOddball);
    case BinaryOperationHint::kNone:
    case BinaryOperationHint::kString:
    case BinaryOperationHint::kStringOrStringWrapper:
    case BinaryOperationHint::kBigInt:
    case BinaryOperationHint::kBigInt64:
    case BinaryOperationHint::kAny:
      UNREACHABLE();
  }
}
}  // namespace

template <Operation kOperation>
ReduceResult MaglevGraphBuilder::VisitUnaryOperation() {
  FeedbackNexus nexus = FeedbackNexusForOperand(0);
  BinaryOperationHint feedback_hint = nexus.GetBinaryOperationFeedback();
  switch (feedback_hint) {
    case BinaryOperationHint::kNone:
      return EmitUnconditionalDeopt(
          DeoptimizeReason::kInsufficientTypeFeedbackForBinaryOperation);
    case BinaryOperationHint::kSignedSmall:
    case BinaryOperationHint::kSignedSmallInputs:
    case BinaryOperationHint::kNumber:
    case BinaryOperationHint::kNumberOrOddball: {
      auto [allowed_input_type, conversion_type] =
          BinopHintToNodeTypeAndConversionType(feedback_hint);
      if constexpr (BinaryOperationIsBitwiseInt32<kOperation>()) {
        static_assert(kOperation == Operation::kBitwiseNot);
        return BuildTruncatingInt32BitwiseNotForToNumber(allowed_input_type,
                                                         conversion_type);
      } else if (feedback_hint == BinaryOperationHint::kSignedSmall) {
        return BuildInt32UnaryOperationNode<kOperation>();
      }
      return BuildFloat64UnaryOperationNodeForToNumber<kOperation>(
          allowed_input_type, conversion_type);
      break;
    }
    case BinaryOperationHint::kString:
    case BinaryOperationHint::kStringOrStringWrapper:
    case BinaryOperationHint::kBigInt:
    case BinaryOperationHint::kBigInt64:
    case BinaryOperationHint::kAny:
      // Fallback to generic node.
      break;
  }
  BuildGenericUnaryOperationNode<kOperation>();
  return ReduceResult::Done();
}

template <Operation kOperation>
ReduceResult MaglevGraphBuilder::VisitBinaryOperation() {
  FeedbackNexus nexus = FeedbackNexusForOperand(1);
  BinaryOperationHint feedback_hint = nexus.GetBinaryOperationFeedback();
  switch (feedback_hint) {
    case BinaryOperationHint::kNone:
      return EmitUnconditionalDeopt(
          DeoptimizeReason::kInsufficientTypeFeedbackForBinaryOperation);
    case BinaryOperationHint::kSignedSmall:
    case BinaryOperationHint::kSignedSmallInputs:
    case BinaryOperationHint::kNumber:
    case BinaryOperationHint::kNumberOrOddball: {
      auto [allowed_input_type, conversion_type] =
          BinopHintToNodeTypeAndConversionType(feedback_hint);
      if constexpr (BinaryOperationIsBitwiseInt32<kOperation>()) {
        return BuildTruncatingInt32BinaryOperationNodeForToNumber<kOperation>(
            allowed_input_type, conversion_type);
      } else if (feedback_hint == BinaryOperationHint::kSignedSmall) {
        if constexpr (kOperation == Operation::kExponentiate) {
          // Exponentiate never updates the feedback to be a Smi.
          UNREACHABLE();
        } else {
          return BuildInt32BinaryOperationNode<kOperation>();
        }
      } else {
        return BuildFloat64BinaryOperationNodeForToNumber<kOperation>(
            allowed_input_type, conversion_type);
      }
      break;
    }
    case BinaryOperationHint::kString:
      if constexpr (kOperation == Operation::kAdd) {
        ValueNode* left = LoadRegister(0);
        ValueNode* right = GetAccumulator();
        if (RootConstant* root_constant = left->TryCast<RootConstant>()) {
          if (root_constant->index() == RootIndex::kempty_string) {
            BuildCheckString(right);
            // The right side is already in the accumulator register.
            return ReduceResult::Done();
          }
        }
        if (RootConstant* root_constant = right->TryCast<RootConstant>()) {
          if (root_constant->index() == RootIndex::kempty_string) {
            BuildCheckString(left);
            MoveNodeBetweenRegisters(
                iterator_.GetRegisterOperand(0),
                interpreter::Register::virtual_accumulator());
            return ReduceResult::Done();
          }
        }
        BuildCheckString(left);
        BuildCheckString(right);
        SetAccumulator(AddNewNode<StringConcat>({left, right}));
        return ReduceResult::Done();
      }
      break;
    case BinaryOperationHint::kStringOrStringWrapper:
      if constexpr (kOperation == Operation::kAdd) {
        if (broker()
                ->dependencies()
                ->DependOnStringWrapperToPrimitiveProtector()) {
          ValueNode* left = LoadRegister(0);
          ValueNode* right = GetAccumulator();
          BuildCheckStringOrStringWrapper(left);
          BuildCheckStringOrStringWrapper(right);
          SetAccumulator(AddNewNode<StringWrapperConcat>({left, right}));
          return ReduceResult::Done();
        }
      }
      [[fallthrough]];
    case BinaryOperationHint::kBigInt:
    case BinaryOperationHint::kBigInt64:
    case BinaryOperationHint::kAny:
      // Fallback to generic node.
      break;
  }
  BuildGenericBinaryOperationNode<kOperation>();
  return ReduceResult::Done();
}

template <Operation kOperation>
ReduceResult MaglevGraphBuilder::VisitBinarySmiOperation() {
  FeedbackNexus nexus = FeedbackNexusForOperand(1);
  BinaryOperationHint feedback_hint = nexus.GetBinaryOperationFeedback();
  switch (feedback_hint) {
    case BinaryOperationHint::kNone:
      return EmitUnconditionalDeopt(
          DeoptimizeReason::kInsufficientTypeFeedbackForBinaryOperation);
    case BinaryOperationHint::kSignedSmall:
    case BinaryOperationHint::kSignedSmallInputs:
    case BinaryOperationHint::kNumber:
    case BinaryOperationHint::kNumberOrOddball: {
      const auto [allowed_input_type, conversion_type] =
          BinopHintToNodeTypeAndConversionType(feedback_hint);
      if constexpr (BinaryOperationIsBitwiseInt32<kOperation>()) {
        return BuildTruncatingInt32BinarySmiOperationNodeForToNumber<
            kOperation>(allowed_input_type, conversion_type);
      } else if (feedback_hint == BinaryOperationHint::kSignedSmall) {
        if constexpr (kOperation == Operation::kExponentiate) {
          // Exponentiate never updates the feedback to be a Smi.
          UNREACHABLE();
        } else {
          return BuildInt32BinarySmiOperationNode<kOperation>();
        }
      } else {
        return BuildFloat64BinarySmiOperationNodeForToNumber<kOperation>(
            allowed_input_type, conversion_type);
      }
      break;
    }
    case BinaryOperationHint::kString:
    case BinaryOperationHint::kStringOrStringWrapper:
    case BinaryOperationHint::kBigInt:
    case BinaryOperationHint::kBigInt64:
    case BinaryOperationHint::kAny:
      // Fallback to generic node.
      break;
  }
  BuildGenericBinarySmiOperationNode<kOperation>();
  return ReduceResult::Done();
}

template <Operation kOperation, typename type>
bool OperationValue(type left, type right) {
  switch (kOperation) {
    case Operation::kEqual:
    case Operation::kStrictEqual:
      return left == right;
    case Operation::kLessThan:
      return left < right;
    case Operation::kLessThanOrEqual:
      return left <= right;
    case Operation::kGreaterThan:
      return left > right;
    case Operation::kGreaterThanOrEqual:
      return left >= right;
  }
}

// static
compiler::OptionalHeapObjectRef MaglevGraphBuilder::TryGetConstant(
    compiler::JSHeapBroker* broker, LocalIsolate* isolate, ValueNode* node) {
  if (Constant* c = node->TryCast<Constant>()) {
    return c->object();
  }
  if (RootConstant* c = node->TryCast<RootConstant>()) {
    return MakeRef(broker, isolate->root_handle(c->index())).AsHeapObject();
  }
  return {};
}

compiler::OptionalHeapObjectRef MaglevGraphBuilder::TryGetConstant(
    ValueNode* node, ValueNode** constant_node) {
  if (auto result = TryGetConstant(broker(), local_isolate(), node)) {
    if (constant_node) *constant_node = node;
    return result;
  }
  if (auto c = TryGetConstantAlternative(node)) {
    return TryGetConstant(*c, constant_node);
  }
  return {};
}

std::optional<ValueNode*> MaglevGraphBuilder::TryGetConstantAlternative(
    ValueNode* node) {
  const NodeInfo* info = known_node_aspects().TryGetInfoFor(node);
  if (info) {
    if (auto c = info->alternative().checked_value()) {
      if (IsConstantNode(c->opcode())) {
        return c;
      }
    }
  }
  return {};
}

template <Operation kOperation>
bool MaglevGraphBuilder::TryReduceCompareEqualAgainstConstant() {
  if (kOperation != Operation::kStrictEqual && kOperation != Operation::kEqual)
    return false;

  ValueNode* left = LoadRegister(0);
  ValueNode* right = GetAccumulator();

  ValueNode* other = right;
  compiler::OptionalHeapObjectRef maybe_constant = TryGetConstant(left);
  if (!maybe_constant) {
    maybe_constant = TryGetConstant(right);
    other = left;
  }
  if (!maybe_constant) return false;

  if (CheckType(other, NodeType::kBoolean)) {
    auto CompareOtherWith = [&](bool constant) {
      compiler::OptionalHeapObjectRef const_other = TryGetConstant(other);
      if (const_other) {
        auto bool_other = const_other->TryGetBooleanValue(broker());
        if (bool_other.has_value()) {
          SetAccumulator(GetBooleanConstant(constant == *bool_other));
          return;
        }
      }
      if (constant) {
        SetAccumulator(other);
      } else {
        SetAccumulator(AddNewNode<LogicalNot>({other}));
      }
    };

    if (maybe_constant.equals(broker_->true_value())) {
      CompareOtherWith(true);
      return true;
    } else if (maybe_constant.equals(broker_->false_value())) {
      CompareOtherWith(false);
      return true;
    } else if (kOperation == Operation::kEqual) {
      // For `bool == num` we can convert the actual comparison `ToNumber(bool)
      // == num` into `(num == 1) ? bool : ((num == 0) ? !bool : false)`,
      std::optional<double> val = {};
      if (maybe_constant.value().IsSmi()) {
        val = maybe_constant.value().AsSmi();
      } else if (maybe_constant.value().IsHeapNumber()) {
        val = maybe_constant.value().AsHeapNumber().value();
      }
      if (val) {
        if (*val == 0) {
          CompareOtherWith(false);
        } else if (*val == 1) {
          CompareOtherWith(true);
        } else {
          // The constant number is neither equal to `ToNumber(true)` nor
          // `ToNumber(false)`.
          SetAccumulator(GetBooleanConstant(false));
        }
        return true;
      }
    }
  }

  if (kOperation != Operation::kStrictEqual) return false;

  InstanceType type = maybe_constant.value().map(broker()).instance_type();
  if (!InstanceTypeChecker::IsReferenceComparable(type)) return false;

  // If the constant is the undefined value, we can compare it
  // against holey floats.
  if (maybe_constant->IsUndefined()) {
    ValueNode* holey_float = nullptr;
    if (left->properties().value_representation() ==
        ValueRepresentation::kHoleyFloat64) {
      holey_float = left;
    } else if (right->properties().value_representation() ==
               ValueRepresentation::kHoleyFloat64) {
      holey_float = right;
    }
    if (holey_float) {
      SetAccumulator(AddNewNode<HoleyFloat64IsHole>({holey_float}));
      return true;
    }
  }

  if (left->properties().value_representation() !=
          ValueRepresentation::kTagged ||
      right->properties().value_representation() !=
          ValueRepresentation::kTagged) {
    SetAccumulator(GetBooleanConstant(false));
  } else {
    SetAccumulator(BuildTaggedEqual(left, right));
  }
  return true;
}

template <Operation kOperation>
ReduceResult MaglevGraphBuilder::VisitCompareOperation() {
  if (TryReduceCompareEqualAgainstConstant<kOperation>())
    return ReduceResult::Done();

  // Compare opcodes are not always commutative. We sort the ones which are for
  // better CSE coverage.
  auto SortCommute = [](ValueNode*& left, ValueNode*& right) {
    if (!v8_flags.maglev_cse) return;
    if (kOperation != Operation::kEqual &&
        kOperation != Operation::kStrictEqual) {
      return;
    }
    if (left > right) {
      std::swap(left, right);
    }
  };

  auto TryConstantFoldInt32 = [&](ValueNode* left, ValueNode* right) {
    if (left->Is<Int32Constant>() && right->Is<Int32Constant>()) {
      int left_value = left->Cast<Int32Constant>()->value();
      int right_value = right->Cast<Int32Constant>()->value();
      SetAccumulator(GetBooleanConstant(
          OperationValue<kOperation>(left_value, right_value)));
      return true;
    }
    return false;
  };

  auto TryConstantFoldEqual = [&](ValueNode* left, ValueNode* right) {
    if (left == right) {
      SetAccumulator(
          GetBooleanConstant(kOperation == Operation::kEqual ||
                             kOperation == Operation::kStrictEqual ||
                             kOperation == Operation::kLessThanOrEqual ||
                             kOperation == Operation::kGreaterThanOrEqual));
      return true;
    }
    return false;
  };

  auto MaybeOddballs = [&]() {
    auto MaybeOddball = [&](ValueNode* value) {
      ValueRepresentation rep = value->value_representation();
      switch (rep) {
        case ValueRepresentation::kInt32:
        case ValueRepresentation::kUint32:
        case ValueRepresentation::kFloat64:
          return false;
        default:
          break;
      }
      return !CheckType(value, NodeType::kNumber);
    };
    return MaybeOddball(LoadRegister(0)) || MaybeOddball(GetAccumulator());
  };

  FeedbackNexus nexus = FeedbackNexusForOperand(1);
  switch (nexus.GetCompareOperationFeedback()) {
    case CompareOperationHint::kNone:
      return EmitUnconditionalDeopt(
          DeoptimizeReason::kInsufficientTypeFeedbackForCompareOperation);

    case CompareOperationHint::kSignedSmall: {
      // TODO(victorgomes): Add a smart equality operator, that compares for
      // constants in different representations.
      ValueNode* left = GetInt32(LoadRegister(0));
      ValueNode* right = GetInt32(GetAccumulator());
      if (TryConstantFoldEqual(left, right)) return ReduceResult::Done();
      if (TryConstantFoldInt32(left, right)) return ReduceResult::Done();
      SortCommute(left, right);
      SetAccumulator(AddNewNode<Int32Compare>({left, right}, kOperation));
      return ReduceResult::Done();
    }
    case CompareOperationHint::kNumberOrOddball:
      // TODO(leszeks): we could support all kNumberOrOddball with
      // BranchIfFloat64Compare, but we'd need to special case comparing
      // oddballs with NaN value (e.g. undefined) against themselves.
      if (MaybeOddballs()) {
        break;
      }
      [[fallthrough]];
    case CompareOperationHint::kNumberOrBoolean:
      if (kOperation == Operation::kStrictEqual && MaybeOddballs()) {
        break;
      }
      [[fallthrough]];
    case CompareOperationHint::kNumber: {
      ValueNode* left = LoadRegister(0);
      ValueNode* right = GetAccumulator();
      if (left->value_representation() == ValueRepresentation::kInt32 &&
          right->value_representation() == ValueRepresentation::kInt32) {
        if (TryConstantFoldEqual(left, right)) return ReduceResult::Done();
        if (TryConstantFoldInt32(left, right)) return ReduceResult::Done();
        SortCommute(left, right);
        SetAccumulator(AddNewNode<Int32Compare>({left, right}, kOperation));
        return ReduceResult::Done();
      }
      // In compare operations, booleans should be converted to Float64 but
      // non-boolean oddballs shouldn't. Even if the feedback type was
      // kNumberOrOddball, we'd still pass
      // TaggedToFloat64ConversionType::kNumberOrBoolean.
      NodeType allowed_input_type;
      TaggedToFloat64ConversionType conversion_type;
      if (nexus.GetCompareOperationFeedback() ==
          CompareOperationHint::kNumberOrBoolean) {
        allowed_input_type = NodeType::kNumberOrBoolean;
        conversion_type = TaggedToFloat64ConversionType::kNumberOrBoolean;
      } else {
        allowed_input_type = NodeType::kNumber;
        conversion_type = TaggedToFloat64ConversionType::kOnlyNumber;
      }
      left = GetFloat64ForToNumber(left, allowed_input_type, conversion_type);
      right = GetFloat64ForToNumber(right, allowed_input_type, conversion_type);
      if (left->Is<Float64Constant>() && right->Is<Float64Constant>()) {
        double left_value = left->Cast<Float64Constant>()->value().get_scalar();
        double right_value =
            right->Cast<Float64Constant>()->value().get_scalar();
        SetAccumulator(GetBooleanConstant(
            OperationValue<kOperation>(left_value, right_value)));
        return ReduceResult::Done();
      }
      SortCommute(left, right);
      SetAccumulator(AddNewNode<Float64Compare>({left, right}, kOperation));
      return ReduceResult::Done();
    }
    case CompareOperationHint::kInternalizedString: {
      DCHECK(kOperation == Operation::kEqual ||
             kOperation == Operation::kStrictEqual);
      ValueNode *left, *right;
      if (IsRegisterEqualToAccumulator(0)) {
        left = right = GetInternalizedString(iterator_.GetRegisterOperand(0));
        SetAccumulator(GetRootConstant(RootIndex::kTrueValue));
        return ReduceResult::Done();
      }
      left = GetInternalizedString(iterator_.GetRegisterOperand(0));
      right =
          GetInternalizedString(interpreter::Register::virtual_accumulator());
      if (TryConstantFoldEqual(left, right)) return ReduceResult::Done();
      SetAccumulator(BuildTaggedEqual(left, right));
      return ReduceResult::Done();
    }
    case CompareOperationHint::kSymbol: {
      DCHECK(kOperation == Operation::kEqual ||
             kOperation == Operation::kStrictEqual);

      ValueNode* left = LoadRegister(0);
      ValueNode* right = GetAccumulator();
      BuildCheckSymbol(left);
      BuildCheckSymbol(right);
      if (TryConstantFoldEqual(left, right)) return ReduceResult::Done();
      SetAccumulator(BuildTaggedEqual(left, right));
      return ReduceResult::Done();
    }
    case CompareOperationHint::kString: {
      ValueNode* left = LoadRegister(0);
      ValueNode* right = GetAccumulator();
      BuildCheckString(left);
      BuildCheckString(right);

      ValueNode* result;
      if (TryConstantFoldEqual(left, right)) return ReduceResult::Done();
      ValueNode* tagged_left = GetTaggedValue(left);
      ValueNode* tagged_right = GetTaggedValue(right);
      switch (kOperation) {
        case Operation::kEqual:
        case Operation::kStrictEqual:
          result = AddNewNode<StringEqual>({tagged_left, tagged_right});
          break;
        case Operation::kLessThan:
          result = BuildCallBuiltin<Builtin::kStringLessThan>(
              {tagged_left, tagged_right});
          break;
        case Operation::kLessThanOrEqual:
          result = BuildCallBuiltin<Builtin::kStringLessThanOrEqual>(
              {tagged_left, tagged_right});
          break;
        case Operation::kGreaterThan:
          result = BuildCallBuiltin<Builtin::kStringGreaterThan>(
              {tagged_left, tagged_right});
          break;
        case Operation::kGreaterThanOrEqual:
          result = BuildCallBuiltin<Builtin::kStringGreaterThanOrEqual>(
              {tagged_left, tagged_right});
          break;
      }

      SetAccumulator(result);
      return ReduceResult::Done();
    }
    case CompareOperationHint::kAny:
    case CompareOperationHint::kBigInt64:
    case CompareOperationHint::kBigInt:
    case CompareOperationHint::kReceiverOrNullOrUndefined:
      break;
    case CompareOperationHint::kReceiver: {
      DCHECK(kOperation == Operation::kEqual ||
             kOperation == Operation::kStrictEqual);

      ValueNode* left = LoadRegister(0);
      ValueNode* right = GetAccumulator();
      BuildCheckJSReceiver(left);
      BuildCheckJSReceiver(right);
      SetAccumulator(BuildTaggedEqual(left, right));
      return ReduceResult::Done();
    }
  }

  BuildGenericBinaryOperationNode<kOperation>();
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitLdar() {
  MoveNodeBetweenRegisters(iterator_.GetRegisterOperand(0),
                           interpreter::Register::virtual_accumulator());
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitLdaZero() {
  SetAccumulator(GetSmiConstant(0));
  return ReduceResult::Done();
}
ReduceResult MaglevGraphBuilder::VisitLdaSmi() {
  int constant = iterator_.GetImmediateOperand(0);
  SetAccumulator(GetSmiConstant(constant));
  return ReduceResult::Done();
}
ReduceResult MaglevGraphBuilder::VisitLdaUndefined() {
  SetAccumulator(GetRootConstant(RootIndex::kUndefinedValue));
  return ReduceResult::Done();
}
ReduceResult MaglevGraphBuilder::VisitLdaNull() {
  SetAccumulator(GetRootConstant(RootIndex::kNullValue));
  return ReduceResult::Done();
}
ReduceResult MaglevGraphBuilder::VisitLdaTheHole() {
  SetAccumulator(GetRootConstant(RootIndex::kTheHoleValue));
  return ReduceResult::Done();
}
ReduceResult MaglevGraphBuilder::VisitLdaTrue() {
  SetAccumulator(GetRootConstant(RootIndex::kTrueValue));
  return ReduceResult::Done();
}
ReduceResult MaglevGraphBuilder::VisitLdaFalse() {
  SetAccumulator(GetRootConstant(RootIndex::kFalseValue));
  return ReduceResult::Done();
}
ReduceResult MaglevGraphBuilder::VisitLdaConstant() {
  SetAccumulator(GetConstant(GetRefOperand<HeapObject>(0)));
  return ReduceResult::Done();
}

bool MaglevGraphBuilder::TrySpecializeLoadContextSlotToFunctionContext(
    ValueNode* context, int slot_index, ContextSlotMutability slot_mutability) {
  DCHECK(compilation_unit_->info()->specialize_to_function_context());

  if (slot_mutability == kMutable) return false;

  auto constant = TryGetConstant(context);
  if (!constant) return false;

  compiler::ContextRef context_ref = constant.value().AsContext();

  compiler::OptionalObjectRef maybe_slot_value =
      context_ref.get(broker(), slot_index);
  if (!maybe_slot_value.has_value()) return false;

  compiler::ObjectRef slot_value = maybe_slot_value.value();
  if (slot_value.IsHeapObject()) {
    // Even though the context slot is immutable, the context might have escaped
    // before the function to which it belongs has initialized the slot.  We
    // must be conservative and check if the value in the slot is currently the
    // hole or undefined. Only if it is neither of these, can we be sure that it
    // won't change anymore.
    //
    // See also: JSContextSpecialization::ReduceJSLoadContext.
    compiler::OddballType oddball_type =
        slot_value.AsHeapObject().map(broker()).oddball_type(broker());
    if (oddball_type == compiler::OddballType::kUndefined ||
        slot_value.IsTheHole()) {
      return false;
    }
  }

  // Fold the load of the immutable slot.

  SetAccumulator(GetConstant(slot_value));
  return true;
}

ValueNode* MaglevGraphBuilder::TrySpecializeLoadScriptContextSlot(
    ValueNode* context_node, int index) {
  if (!context_node->Is<Constant>()) return {};
  compiler::ContextRef context =
      context_node->Cast<Constant>()->ref().AsContext();
  DCHECK(context.object()->IsScriptContext());
  auto maybe_property = context.object()->GetScriptContextSideProperty(index);
  if (!maybe_property) return {};
  auto property = maybe_property.value();
  int offset = Context::OffsetOfElementAt(index);
  switch (property) {
    case ContextSidePropertyCell::kConst: {
      compiler::OptionalObjectRef constant = context.get(broker(), index);
      if (!constant.has_value()) {
        return BuildLoadTaggedField<LoadTaggedFieldForContextSlot>(context_node,
                                                                   offset);
      }
      broker()->dependencies()->DependOnScriptContextSlotProperty(
          context, index, property, broker());
      return GetConstant(*constant);
    }
    case ContextSidePropertyCell::kSmi: {
      broker()->dependencies()->DependOnScriptContextSlotProperty(
          context, index, property, broker());
      ValueNode* value = BuildLoadTaggedField<LoadTaggedFieldForContextSlot>(
          context_node, offset);
      EnsureType(value, NodeType::kSmi);
      return value;
    }
    case ContextSidePropertyCell::kMutableInt32:
      if (auto mutable_heap_number = context.get(broker(), index)) {
        if (!mutable_heap_number->IsHeapNumber()) {
          // TODO(victorgomes): In case the tag is out of date by now we could
          // retry this reduction.
          break;
        }
        broker()->dependencies()->DependOnScriptContextSlotProperty(
            context, index, property, broker());
        return AddNewNode<LoadInt32>(
            {GetConstant(*mutable_heap_number)},
            static_cast<int>(offsetof(HeapNumber, value_)));
      }
      broker()->dependencies()->DependOnScriptContextSlotProperty(
          context, index, property, broker());
      return AddNewNode<LoadHeapInt32>({context_node}, offset);
    case ContextSidePropertyCell::kMutableHeapNumber:
      if (auto mutable_heap_number = context.get(broker(), index)) {
        if (!mutable_heap_number->IsHeapNumber()) {
          // TODO(victorgomes): In case the tag is out of date by now we could
          // retry this reduction.
          break;
        }
        broker()->dependencies()->DependOnScriptContextSlotProperty(
            context, index, property, broker());
        return AddNewNode<LoadFloat64>(
            {GetConstant(*mutable_heap_number)},
            static_cast<int>(offsetof(HeapNumber, value_)));
      }
      broker()->dependencies()->DependOnScriptContextSlotProperty(
          context, index, property, broker());
      return AddNewNode<LoadDoubleField>({context_node}, offset);
    case ContextSidePropertyCell::kOther:
      break;
    default:
      UNREACHABLE();
  }
  return BuildLoadTaggedField<LoadTaggedFieldForContextSlot>(context_node,
                                                             offset);
}

ValueNode* MaglevGraphBuilder::LoadAndCacheContextSlot(
    ValueNode* context, int index, ContextSlotMutability slot_mutability,
    ContextKind context_kind) {
  int offset = Context::OffsetOfElementAt(index);
  ValueNode*& cached_value =
      slot_mutability == kMutable
          ? known_node_aspects().loaded_context_slots[{context, offset}]
          : known_node_aspects().loaded_context_constants[{context, offset}];
  if (cached_value) {
    if (v8_flags.trace_maglev_graph_building) {
      std::cout << "  * Reusing cached context slot "
                << PrintNodeLabel(graph_labeller(), context) << "[" << offset
                << "]: " << PrintNode(graph_labeller(), cached_value)
                << std::endl;
    }
    return cached_value;
  }
  known_node_aspects().UpdateMayHaveAliasingContexts(context);
  if (context_kind == ContextKind::kScriptContext &&
      (v8_flags.script_context_mutable_heap_number ||
       v8_flags.const_tracking_let) &&
      slot_mutability == kMutable) {
    // We collect feedback only for mutable context slots.
    cached_value = TrySpecializeLoadScriptContextSlot(context, index);
    if (cached_value) return cached_value;
    return cached_value =
               BuildLoadTaggedField<LoadTaggedFieldForScriptContextSlot>(
                   context, index);
  }
  return cached_value = BuildLoadTaggedField<LoadTaggedFieldForContextSlot>(
             context, offset);
}

bool MaglevGraphBuilder::ContextMayAlias(
    ValueNode* context, compiler::OptionalScopeInfoRef scope_info) {
  // Distinguishing contexts by their scope info only works if scope infos are
  // guaranteed to be unique.
  if (!v8_flags.reuse_scope_infos) return true;
  if (!scope_info.has_value()) {
    return true;
  }
  auto other = graph()->TryGetScopeInfo(context, broker());
  if (!other.has_value()) {
    return true;
  }
  return scope_info->equals(*other);
}

MaybeReduceResult MaglevGraphBuilder::TrySpecializeStoreScriptContextSlot(
    ValueNode* context, int index, ValueNode* value, Node** store) {
  DCHECK_NOT_NULL(store);
  DCHECK(v8_flags.script_context_mutable_heap_number ||
         v8_flags.const_tracking_let);
  if (!context->Is<Constant>()) {
    *store = AddNewNode<StoreScriptContextSlotWithWriteBarrier>(
        {context, value}, index);
    return ReduceResult::Done();
  }

  compiler::ContextRef context_ref =
      context->Cast<Constant>()->ref().AsContext();
  DCHECK(context_ref.object()->IsScriptContext());
  auto maybe_property =
      context_ref.object()->GetScriptContextSideProperty(index);
  if (!maybe_property) {
    *store = AddNewNode<StoreScriptContextSlotWithWriteBarrier>(
        {context, value}, index);
    return ReduceResult::Done();
  }
  auto property = maybe_property.value();
  int offset = Context::OffsetOfElementAt(index);
  if (property == ContextSidePropertyCell::kConst) {
    compiler::OptionalObjectRef constant = context_ref.get(broker(), index);
    if (!constant.has_value() ||
        (constant->IsString() && !constant->IsInternalizedString())) {
      *store = AddNewNode<StoreScriptContextSlotWithWriteBarrier>(
          {context, value}, index);
      return ReduceResult::Done();
    }
    broker()->dependencies()->DependOnScriptContextSlotProperty(
        context_ref, index, property, broker());
    return BuildCheckValue(value, *constant);
  }

  if (!v8_flags.script_context_mutable_heap_number) {
    *store = BuildStoreTaggedField(context, value, offset,
                                   StoreTaggedMode::kDefault);
    return ReduceResult::Done();
  }

  switch (property) {
    case ContextSidePropertyCell::kConst:
      UNREACHABLE();
    case ContextSidePropertyCell::kSmi:
      RETURN_IF_ABORT(BuildCheckSmi(value));
      broker()->dependencies()->DependOnScriptContextSlotProperty(
          context_ref, index, property, broker());
      *store = BuildStoreTaggedField(context, value, offset,
                                     StoreTaggedMode::kDefault);
      break;
    case ContextSidePropertyCell::kMutableInt32:
      EnsureInt32(value, true);
      if (auto mutable_heap_number = context_ref.get(broker(), index)) {
        if (!mutable_heap_number->IsHeapNumber()) {
          // TODO(victorgomes): In case the tag is out of date by now we could
          // retry this reduction.
          return {};
        }
        *store = AddNewNode<StoreInt32>(
            {GetConstant(*mutable_heap_number), value},
            static_cast<int>(offsetof(HeapNumber, value_)));
      } else {
        *store = AddNewNode<StoreHeapInt32>({context, value}, offset);
      }
      broker()->dependencies()->DependOnScriptContextSlotProperty(
          context_ref, index, property, broker());
      break;
    case ContextSidePropertyCell::kMutableHeapNumber:
      BuildCheckNumber(value);
      if (auto mutable_heap_number = context_ref.get(broker(), index)) {
        if (!mutable_heap_number->IsHeapNumber()) {
          // TODO(victorgomes): In case the tag is out of date by now we could
          // retry this reduction.
          return {};
        }
        *store = AddNewNode<StoreFloat64>(
            {GetConstant(*mutable_heap_number), value},
            static_cast<int>(offsetof(HeapNumber, value_)));
      } else {
        *store = AddNewNode<StoreDoubleField>({context, value}, offset);
      }
      broker()->dependencies()->DependOnScriptContextSlotProperty(
          context_ref, index, property, broker());
      break;
    case ContextSidePropertyCell::kOther:
      *store = BuildStoreTaggedField(context, value, offset,
                                     StoreTaggedMode::kDefault);
      break;
    default:
      UNREACHABLE();
  }
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::StoreAndCacheContextSlot(
    ValueNode* context, int index, ValueNode* value, ContextKind context_kind) {
  int offset = Context::OffsetOfElementAt(index);
  DCHECK_EQ(
      known_node_aspects().loaded_context_constants.count({context, offset}),
      0);

  Node* store = nullptr;
  if ((v8_flags.script_context_mutable_heap_number ||
       v8_flags.const_tracking_let) &&
      context_kind == ContextKind::kScriptContext) {
    MaybeReduceResult result =
        TrySpecializeStoreScriptContextSlot(context, index, value, &store);
    RETURN_IF_ABORT(result);
    if (!store && result.IsDone()) {
      // If we didn't need to emit any store, there is nothing to cache.
      return result.Checked();
    }
  }

  if (!store) {
    store = BuildStoreTaggedField(context, value, offset,
                                  StoreTaggedMode::kDefault);
  }

  if (v8_flags.trace_maglev_graph_building) {
    std::cout << "  * Recording context slot store "
              << PrintNodeLabel(graph_labeller(), context) << "[" << offset
              << "]: " << PrintNode(graph_labeller(), value) << std::endl;
  }
  known_node_aspects().UpdateMayHaveAliasingContexts(context);
  KnownNodeAspects::LoadedContextSlots& loaded_context_slots =
      known_node_aspects().loaded_context_slots;
  if (known_node_aspects().may_have_aliasing_contexts() ==
      KnownNodeAspects::ContextSlotLoadsAlias::Yes) {
    compiler::OptionalScopeInfoRef scope_info =
        graph()->TryGetScopeInfo(context, broker());
    for (auto& cache : loaded_context_slots) {
      if (std::get<int>(cache.first) == offset &&
          std::get<ValueNode*>(cache.first) != context) {
        if (ContextMayAlias(std::get<ValueNode*>(cache.first), scope_info) &&
            cache.second != value) {
          if (v8_flags.trace_maglev_graph_building) {
            std::cout << "  * Clearing probably aliasing value "
                      << PrintNodeLabel(graph_labeller(),
                                        std::get<ValueNode*>(cache.first))
                      << "[" << offset
                      << "]: " << PrintNode(graph_labeller(), value)
                      << std::endl;
          }
          cache.second = nullptr;
          if (is_loop_effect_tracking()) {
            loop_effects_->context_slot_written.insert(cache.first);
            loop_effects_->may_have_aliasing_contexts = true;
          }
        }
      }
    }
  }
  KnownNodeAspects::LoadedContextSlotsKey key{context, offset};
  auto updated = loaded_context_slots.emplace(key, value);
  if (updated.second) {
    if (is_loop_effect_tracking()) {
      loop_effects_->context_slot_written.insert(key);
    }
    unobserved_context_slot_stores_[key] = store;
  } else {
    if (updated.first->second != value) {
      updated.first->second = value;
      if (is_loop_effect_tracking()) {
        loop_effects_->context_slot_written.insert(key);
      }
    }
    if (known_node_aspects().may_have_aliasing_contexts() !=
        KnownNodeAspects::ContextSlotLoadsAlias::Yes) {
      auto last_store = unobserved_context_slot_stores_.find(key);
      if (last_store != unobserved_context_slot_stores_.end()) {
        MarkNodeDead(last_store->second);
        last_store->second = store;
      } else {
        unobserved_context_slot_stores_[key] = store;
      }
    }
  }
  return ReduceResult::Done();
}

void MaglevGraphBuilder::BuildLoadContextSlot(
    ValueNode* context, size_t depth, int slot_index,
    ContextSlotMutability slot_mutability, ContextKind context_kind) {
  context = GetContextAtDepth(context, depth);
  if (compilation_unit_->info()->specialize_to_function_context() &&
      TrySpecializeLoadContextSlotToFunctionContext(context, slot_index,
                                                    slot_mutability)) {
    return;  // Our work here is done.
  }

  // Always load the slot here as if it were mutable. Immutable slots have a
  // narrow range of mutability if the context escapes before the slot is
  // initialized, so we can't safely assume that the load can be cached in case
  // it's a load before initialization (e.g. var a = a + 42).
  current_interpreter_frame_.set_accumulator(
      LoadAndCacheContextSlot(context, slot_index, kMutable, context_kind));
}

ReduceResult MaglevGraphBuilder::BuildStoreContextSlot(
    ValueNode* context, size_t depth, int slot_index, ValueNode* value,
    ContextKind context_kind) {
  context = GetContextAtDepth(context, depth);
  return StoreAndCacheContextSlot(context, slot_index, value, context_kind);
}

ReduceResult MaglevGraphBuilder::VisitLdaContextSlot() {
  ValueNode* context = LoadRegister(0);
  int slot_index = iterator_.GetIndexOperand(1);
  size_t depth = iterator_.GetUnsignedImmediateOperand(2);
  BuildLoadContextSlot(context, depth, slot_index, kMutable,
                       ContextKind::kDefault);
  return ReduceResult::Done();
}
ReduceResult MaglevGraphBuilder::VisitLdaScriptContextSlot() {
  ValueNode* context = LoadRegister(0);
  int slot_index = iterator_.GetIndexOperand(1);
  size_t depth = iterator_.GetUnsignedImmediateOperand(2);
  BuildLoadContextSlot(context, depth, slot_index, kMutable,
                       ContextKind::kScriptContext);
  return ReduceResult::Done();
}
ReduceResult MaglevGraphBuilder::VisitLdaImmutableContextSlot() {
  ValueNode* context = LoadRegister(0);
  int slot_index = iterator_.GetIndexOperand(1);
  size_t depth = iterator_.GetUnsignedImmediateOperand(2);
  BuildLoadContextSlot(context, depth, slot_index, kImmutable,
                       ContextKind::kDefault);
  return ReduceResult::Done();
}
ReduceResult MaglevGraphBuilder::VisitLdaCurrentContextSlot() {
  ValueNode* context = GetContext();
  int slot_index = iterator_.GetIndexOperand(0);
  BuildLoadContextSlot(context, 0, slot_index, kMutable, ContextKind::kDefault);
  return ReduceResult::Done();
}
ReduceResult MaglevGraphBuilder::VisitLdaCurrentScriptContextSlot() {
  ValueNode* context = GetContext();
  int slot_index = iterator_.GetIndexOperand(0);
  BuildLoadContextSlot(context, 0, slot_index, kMutable,
                       ContextKind::kScriptContext);
  return ReduceResult::Done();
}
ReduceResult MaglevGraphBuilder::VisitLdaImmutableCurrentContextSlot() {
  ValueNode* context = GetContext();
  int slot_index = iterator_.GetIndexOperand(0);
  BuildLoadContextSlot(context, 0, slot_index, kImmutable,
                       ContextKind::kDefault);
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitStaContextSlot() {
  ValueNode* context = LoadRegister(0);
  int slot_index = iterator_.GetIndexOperand(1);
  size_t depth = iterator_.GetUnsignedImmediateOperand(2);
  return BuildStoreContextSlot(context, depth, slot_index, GetAccumulator(),
                               ContextKind::kDefault);
}
ReduceResult MaglevGraphBuilder::VisitStaCurrentContextSlot() {
  ValueNode* context = GetContext();
  int slot_index = iterator_.GetIndexOperand(0);
  return BuildStoreContextSlot(context, 0, slot_index, GetAccumulator(),
                               ContextKind::kDefault);
}

ReduceResult MaglevGraphBuilder::VisitStaScriptContextSlot() {
  ValueNode* context = LoadRegister(0);
  int slot_index = iterator_.GetIndexOperand(1);
  size_t depth = iterator_.GetUnsignedImmediateOperand(2);
  return BuildStoreContextSlot(context, depth, slot_index, GetAccumulator(),
                               ContextKind::kScriptContext);
}

ReduceResult MaglevGraphBuilder::VisitStaCurrentScriptContextSlot() {
  ValueNode* context = GetContext();
  int slot_index = iterator_.GetIndexOperand(0);
  return BuildStoreContextSlot(context, 0, slot_index, GetAccumulator(),
                               ContextKind::kScriptContext);
}

ReduceResult MaglevGraphBuilder::VisitStar() {
  MoveNodeBetweenRegisters(interpreter::Register::virtual_accumulator(),
                           iterator_.GetRegisterOperand(0));
  return ReduceResult::Done();
}
#define SHORT_STAR_VISITOR(Name, ...)                                          \
  ReduceResult MaglevGraphBuilder::Visit##Name() {                             \
    MoveNodeBetweenRegisters(                                                  \
        interpreter::Register::virtual_accumulator(),                          \
        interpreter::Register::FromShortStar(interpreter::Bytecode::k##Name)); \
    return ReduceResult::Done();                                               \
  }
SHORT_STAR_BYTECODE_LIST(SHORT_STAR_VISITOR)
#undef SHORT_STAR_VISITOR

ReduceResult MaglevGraphBuilder::VisitMov() {
  MoveNodeBetweenRegisters(iterator_.GetRegisterOperand(0),
                           iterator_.GetRegisterOperand(1));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitPushContext() {
  MoveNodeBetweenRegisters(interpreter::Register::current_context(),
                           iterator_.GetRegisterOperand(0));
  SetContext(GetAccumulator());
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitPopContext() {
  SetContext(LoadRegister(0));
  return ReduceResult::Done();
}

ValueNode* MaglevGraphBuilder::BuildTaggedEqual(ValueNode* lhs,
                                                ValueNode* rhs) {
  ValueNode* tagged_lhs = GetTaggedValue(lhs);
  ValueNode* tagged_rhs = GetTaggedValue(rhs);
  if (tagged_lhs == tagged_rhs) {
    return GetBooleanConstant(true);
  }
  if (HaveDisjointTypes(tagged_lhs, tagged_rhs)) {
    return GetBooleanConstant(false);
  }
  // TODO(victorgomes): We could retrieve the HeapObjectRef in Constant and
  // compare them.
  if (IsConstantNode(tagged_lhs->opcode()) && !tagged_lhs->Is<Constant>() &&
      tagged_lhs->opcode() == tagged_rhs->opcode()) {
    // Constants nodes are canonicalized, except for the node holding
    // HeapObjectRef, so equal constants should have been handled above.
    return GetBooleanConstant(false);
  }
  return AddNewNode<TaggedEqual>({tagged_lhs, tagged_rhs});
}

ValueNode* MaglevGraphBuilder::BuildTaggedEqual(ValueNode* lhs,
                                                RootIndex rhs_index) {
  return BuildTaggedEqual(lhs, GetRootConstant(rhs_index));
}

ReduceResult MaglevGraphBuilder::VisitTestReferenceEqual() {
  ValueNode* lhs = LoadRegister(0);
  ValueNode* rhs = GetAccumulator();
  SetAccumulator(BuildTaggedEqual(lhs, rhs));
  return ReduceResult::Done();
}

ValueNode* MaglevGraphBuilder::BuildTestUndetectable(ValueNode* value) {
  if (value->properties().value_representation() ==
      ValueRepresentation::kHoleyFloat64) {
    return AddNewNode<HoleyFloat64IsHole>({value});
  } else if (value->properties().value_representation() !=
             ValueRepresentation::kTagged) {
    return GetBooleanConstant(false);
  }

  if (auto maybe_constant = TryGetConstant(value)) {
    auto map = maybe_constant.value().map(broker());
    return GetBooleanConstant(map.is_undetectable());
  }

  NodeType node_type;
  if (CheckType(value, NodeType::kSmi, &node_type)) {
    return GetBooleanConstant(false);
  }

  auto it = known_node_aspects().FindInfo(value);
  if (known_node_aspects().IsValid(it)) {
    NodeInfo& info = it->second;
    if (info.possible_maps_are_known()) {
      // We check if all the possible maps have the same undetectable bit value.
      DCHECK_GT(info.possible_maps().size(), 0);
      bool first_is_undetectable = info.possible_maps()[0].is_undetectable();
      bool all_the_same_value =
          std::all_of(info.possible_maps().begin(), info.possible_maps().end(),
                      [first_is_undetectable](compiler::MapRef map) {
                        bool is_undetectable = map.is_undetectable();
                        return (first_is_undetectable && is_undetectable) ||
                               (!first_is_undetectable && !is_undetectable);
                      });
      if (all_the_same_value) {
        return GetBooleanConstant(first_is_undetectable);
      }
    }
  }

  enum CheckType type = GetCheckType(node_type);
  return AddNewNode<TestUndetectable>({value}, type);
}

MaglevGraphBuilder::BranchResult MaglevGraphBuilder::BuildBranchIfUndetectable(
    BranchBuilder& builder, ValueNode* value) {
  ValueNode* result = BuildTestUndetectable(value);
  switch (result->opcode()) {
    case Opcode::kRootConstant:
      switch (result->Cast<RootConstant>()->index()) {
        case RootIndex::kTrueValue:
        case RootIndex::kUndefinedValue:
        case RootIndex::kNullValue:
          return builder.AlwaysTrue();
        default:
          return builder.AlwaysFalse();
      }
    case Opcode::kHoleyFloat64IsHole:
      return BuildBranchIfFloat64IsHole(
          builder, result->Cast<HoleyFloat64IsHole>()->input().node());
    case Opcode::kTestUndetectable:
      return builder.Build<BranchIfUndetectable>(
          {result->Cast<TestUndetectable>()->value().node()},
          result->Cast<TestUndetectable>()->check_type());
    default:
      UNREACHABLE();
  }
}

ReduceResult MaglevGraphBuilder::VisitTestUndetectable() {
  SetAccumulator(BuildTestUndetectable(GetAccumulator()));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitTestNull() {
  ValueNode* value = GetAccumulator();
  SetAccumulator(BuildTaggedEqual(value, RootIndex::kNullValue));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitTestUndefined() {
  ValueNode* value = GetAccumulator();
  SetAccumulator(BuildTaggedEqual(value, RootIndex::kUndefinedValue));
  return ReduceResult::Done();
}

template <typename Function>
MaybeReduceResult MaglevGraphBuilder::TryReduceTypeOf(
    ValueNode* value, const Function& GetResult) {
  // Similar to TF, we assume that all undetectable receiver objects are also
  // callables. In practice, there is only one: document.all.
  switch (CheckTypes(
      value, {NodeType::kBoolean, NodeType::kNumber, NodeType::kString,
              NodeType::kSymbol, NodeType::kCallable, NodeType::kJSArray})) {
    case NodeType::kBoolean:
      return GetResult(TypeOfLiteralFlag::kBoolean, RootIndex::kboolean_string);
    case NodeType::kNumber:
      return GetResult(TypeOfLiteralFlag::kNumber, RootIndex::knumber_string);
    case NodeType::kString:
      return GetResult(TypeOfLiteralFlag::kString, RootIndex::kstring_string);
    case NodeType::kSymbol:
      return GetResult(TypeOfLiteralFlag::kSymbol, RootIndex::ksymbol_string);
    case NodeType::kCallable:
      return Select(
          [&](auto& builder) {
            return BuildBranchIfUndetectable(builder, value);
          },
          [&] {
            return GetResult(TypeOfLiteralFlag::kUndefined,
                             RootIndex::kundefined_string);
          },
          [&] {
            return GetResult(TypeOfLiteralFlag::kFunction,
                             RootIndex::kfunction_string);
          });
    case NodeType::kJSArray:
      // TODO(victorgomes): Track JSReceiver, non-callable types in Maglev.
      return GetResult(TypeOfLiteralFlag::kObject, RootIndex::kobject_string);
    default:
      break;
  }

  if (IsNullValue(value)) {
    return GetResult(TypeOfLiteralFlag::kObject, RootIndex::kobject_string);
  }
  if (IsUndefinedValue(value)) {
    return GetResult(TypeOfLiteralFlag::kUndefined,
                     RootIndex::kundefined_string);
  }

  return {};
}

MaybeReduceResult MaglevGraphBuilder::TryReduceTypeOf(ValueNode* value) {
  return TryReduceTypeOf(value,
                         [&](TypeOfLiteralFlag _, RootIndex idx) -> ValueNode* {
                           return GetRootConstant(idx);
                         });
}

ReduceResult MaglevGraphBuilder::VisitTestTypeOf() {
  // TODO(v8:7700): Add a branch version of TestTypeOf that does not need to
  // materialise the boolean value.
  TypeOfLiteralFlag literal =
      interpreter::TestTypeOfFlags::Decode(GetFlag8Operand(0));
  if (literal == TypeOfLiteralFlag::kOther) {
    SetAccumulator(GetRootConstant(RootIndex::kFalseValue));
    return ReduceResult::Done();
  }
  ValueNode* value = GetAccumulator();
  auto GetResult = [&](TypeOfLiteralFlag expected, RootIndex _) {
    return GetRootConstant(literal == expected ? RootIndex::kTrueValue
                                               : RootIndex::kFalseValue);
  };
  PROCESS_AND_RETURN_IF_DONE(TryReduceTypeOf(value, GetResult), SetAccumulator);

  SetAccumulator(AddNewNode<TestTypeOf>({value}, literal));
  return ReduceResult::Done();
}

MaybeReduceResult MaglevGraphBuilder::TryBuildScriptContextStore(
    const compiler::GlobalAccessFeedback& global_access_feedback) {
  DCHECK(global_access_feedback.IsScriptContextSlot());
  if (global_access_feedback.immutable()) return {};
  auto script_context = GetConstant(global_access_feedback.script_context());
  return StoreAndCacheContextSlot(
      script_context, global_access_feedback.slot_index(), GetAccumulator(),
      ContextKind::kScriptContext);
}

MaybeReduceResult MaglevGraphBuilder::TryBuildPropertyCellStore(
    const compiler::GlobalAccessFeedback& global_access_feedback) {
  DCHECK(global_access_feedback.IsPropertyCell());

  compiler::PropertyCellRef property_cell =
      global_access_feedback.property_cell();
  if (!property_cell.Cache(broker())) return {};

  compiler::ObjectRef property_cell_value = property_cell.value(broker());
  if (property_cell_value.IsPropertyCellHole()) {
    // The property cell is no longer valid.
    return EmitUnconditionalDeopt(
        DeoptimizeReason::kInsufficientTypeFeedbackForGenericNamedAccess);
  }

  PropertyDetails property_details = property_cell.property_details();
  DCHECK_EQ(PropertyKind::kData, property_details.kind());

  if (property_details.IsReadOnly()) {
    // Don't even bother trying to lower stores to read-only data
    // properties.
    // TODO(neis): We could generate code that checks if the new value
    // equals the old one and then does nothing or deopts, respectively.
    return {};
  }

  switch (property_details.cell_type()) {
    case PropertyCellType::kUndefined:
      return {};
    case PropertyCellType::kConstant: {
      // TODO(victorgomes): Support non-internalized string.
      if (property_cell_value.IsString() &&
          !property_cell_value.IsInternalizedString()) {
        return {};
      }
      // Record a code dependency on the cell, and just deoptimize if the new
      // value doesn't match the previous value stored inside the cell.
      broker()->dependencies()->DependOnGlobalProperty(property_cell);
      ValueNode* value = GetAccumulator();
      return BuildCheckValue(value, property_cell_value);
    }
    case PropertyCellType::kConstantType: {
      // We rely on stability further below.
      if (property_cell_value.IsHeapObject() &&
          !property_cell_value.AsHeapObject().map(broker()).is_stable()) {
        return {};
      }
      // Record a code dependency on the cell, and just deoptimize if the new
      // value's type doesn't match the type of the previous value in the cell.
      broker()->dependencies()->DependOnGlobalProperty(property_cell);
      ValueNode* value = GetAccumulator();
      if (property_cell_value.IsHeapObject()) {
        compiler::MapRef property_cell_value_map =
            property_cell_value.AsHeapObject().map(broker());
        broker()->dependencies()->DependOnStableMap(property_cell_value_map);
        BuildCheckHeapObject(value);
        RETURN_IF_ABORT(
            BuildCheckMaps(value, base::VectorOf({property_cell_value_map})));
      } else {
        RETURN_IF_ABORT(GetSmiValue(value));
      }
      ValueNode* property_cell_node = GetConstant(property_cell.AsHeapObject());
      BuildStoreTaggedField(property_cell_node, value,
                            PropertyCell::kValueOffset,
                            StoreTaggedMode::kDefault);
      break;
    }
    case PropertyCellType::kMutable: {
      // Record a code dependency on the cell, and just deoptimize if the
      // property ever becomes read-only.
      broker()->dependencies()->DependOnGlobalProperty(property_cell);
      ValueNode* property_cell_node = GetConstant(property_cell.AsHeapObject());
      BuildStoreTaggedField(property_cell_node, GetAccumulator(),
                            PropertyCell::kValueOffset,
                            StoreTaggedMode::kDefault);
      break;
    }
    case PropertyCellType::kInTransition:
      UNREACHABLE();
  }
  return ReduceResult::Done();
}

MaybeReduceResult MaglevGraphBuilder::TryBuildScriptContextConstantLoad(
    const compiler::GlobalAccessFeedback& global_access_feedback) {
  DCHECK(global_access_feedback.IsScriptContextSlot());
  if (!global_access_feedback.immutable()) return {};
  compiler::OptionalObjectRef maybe_slot_value =
      global_access_feedback.script_context().get(
          broker(), global_access_feedback.slot_index());
  if (!maybe_slot_value) return {};
  return GetConstant(maybe_slot_value.value());
}

MaybeReduceResult MaglevGraphBuilder::TryBuildScriptContextLoad(
    const compiler::GlobalAccessFeedback& global_access_feedback) {
  DCHECK(global_access_feedback.IsScriptContextSlot());
  RETURN_IF_DONE(TryBuildScriptContextConstantLoad(global_access_feedback));
  auto script_context = GetConstant(global_access_feedback.script_context());
  ContextSlotMutability mutability =
      global_access_feedback.immutable() ? kImmutable : kMutable;
  return LoadAndCacheContextSlot(script_context,
                                 global_access_feedback.slot_index(),
                                 mutability, ContextKind::kScriptContext);
}

MaybeReduceResult MaglevGraphBuilder::TryBuildPropertyCellLoad(
    const compiler::GlobalAccessFeedback& global_access_feedback) {
  // TODO(leszeks): A bunch of this is copied from
  // js-native-context-specialization.cc -- I wonder if we can unify it
  // somehow.
  DCHECK(global_access_feedback.IsPropertyCell());

  compiler::PropertyCellRef property_cell =
      global_access_feedback.property_cell();
  if (!property_cell.Cache(broker())) return {};

  compiler::ObjectRef property_cell_value = property_cell.value(broker());
  if (property_cell_value.IsPropertyCellHole()) {
    // The property cell is no longer valid.
    return EmitUnconditionalDeopt(
        DeoptimizeReason::kInsufficientTypeFeedbackForGenericNamedAccess);
  }

  PropertyDetails property_details = property_cell.property_details();
  PropertyCellType property_cell_type = property_details.cell_type();
  DCHECK_EQ(PropertyKind::kData, property_details.kind());

  if (!property_details.IsConfigurable() && property_details.IsReadOnly()) {
    return GetConstant(property_cell_value);
  }

  // Record a code dependency on the cell if we can benefit from the
  // additional feedback, or the global property is configurable (i.e.
  // can be deleted or reconfigured to an accessor property).
  if (property_cell_type != PropertyCellType::kMutable ||
      property_details.IsConfigurable()) {
    broker()->dependencies()->DependOnGlobalProperty(property_cell);
  }

  // Load from constant/undefined global property can be constant-folded.
  if (property_cell_type == PropertyCellType::kConstant ||
      property_cell_type == PropertyCellType::kUndefined) {
    return GetConstant(property_cell_value);
  }

  ValueNode* property_cell_node = GetConstant(property_cell.AsHeapObject());
  return BuildLoadTaggedField(property_cell_node, PropertyCell::kValueOffset);
}

MaybeReduceResult MaglevGraphBuilder::TryBuildGlobalStore(
    const compiler::GlobalAccessFeedback& global_access_feedback) {
  if (global_access_feedback.IsScriptContextSlot()) {
    return TryBuildScriptContextStore(global_access_feedback);
  } else if (global_access_feedback.IsPropertyCell()) {
    return TryBuildPropertyCellStore(global_access_feedback);
  } else {
    DCHECK(global_access_feedback.IsMegamorphic());
    return {};
  }
}

MaybeReduceResult MaglevGraphBuilder::TryBuildGlobalLoad(
    const compiler::GlobalAccessFeedback& global_access_feedback) {
  if (global_access_feedback.IsScriptContextSlot()) {
    return TryBuildScriptContextLoad(global_access_feedback);
  } else if (global_access_feedback.IsPropertyCell()) {
    return TryBuildPropertyCellLoad(global_access_feedback);
  } else {
    DCHECK(global_access_feedback.IsMegamorphic());
    return {};
  }
}

ReduceResult MaglevGraphBuilder::VisitLdaGlobal() {
  // LdaGlobal <name_index> <slot>

  static const int kNameOperandIndex = 0;
  static const int kSlotOperandIndex = 1;

  compiler::NameRef name = GetRefOperand<Name>(kNameOperandIndex);
  FeedbackSlot slot = GetSlotOperand(kSlotOperandIndex);
  compiler::FeedbackSource feedback_source{feedback(), slot};
  return BuildLoadGlobal(name, feedback_source, TypeofMode::kNotInside);
}

ReduceResult MaglevGraphBuilder::VisitLdaGlobalInsideTypeof() {
  // LdaGlobalInsideTypeof <name_index> <slot>

  static const int kNameOperandIndex = 0;
  static const int kSlotOperandIndex = 1;

  compiler::NameRef name = GetRefOperand<Name>(kNameOperandIndex);
  FeedbackSlot slot = GetSlotOperand(kSlotOperandIndex);
  compiler::FeedbackSource feedback_source{feedback(), slot};
  return BuildLoadGlobal(name, feedback_source, TypeofMode::kInside);
}

ReduceResult MaglevGraphBuilder::VisitStaGlobal() {
  // StaGlobal <name_index> <slot>
  FeedbackSlot slot = GetSlotOperand(1);
  compiler::FeedbackSource feedback_source{feedback(), slot};

  const compiler::ProcessedFeedback& access_feedback =
      broker()->GetFeedbackForGlobalAccess(feedback_source);

  if (access_feedback.IsInsufficient()) {
    return EmitUnconditionalDeopt(
        DeoptimizeReason::kInsufficientTypeFeedbackForGenericGlobalAccess);
  }

  const compiler::GlobalAccessFeedback& global_access_feedback =
      access_feedback.AsGlobalAccess();
  RETURN_IF_DONE(TryBuildGlobalStore(global_access_feedback));

  ValueNode* value = GetAccumulator();
  compiler::NameRef name = GetRefOperand<Name>(0);
  ValueNode* context = GetContext();
  AddNewNode<StoreGlobal>({context, value}, name, feedback_source);
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitLdaLookupSlot() {
  // LdaLookupSlot <name_index>
  ValueNode* name = GetConstant(GetRefOperand<Name>(0));
  SetAccumulator(BuildCallRuntime(Runtime::kLoadLookupSlot, {name}).value());
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitLdaLookupContextSlot() {
  // LdaLookupContextSlot <name_index> <feedback_slot> <depth>
  ValueNode* name = GetConstant(GetRefOperand<Name>(0));
  ValueNode* slot = GetTaggedIndexConstant(iterator_.GetIndexOperand(1));
  ValueNode* depth =
      GetTaggedIndexConstant(iterator_.GetUnsignedImmediateOperand(2));
  SetAccumulator(
      BuildCallBuiltin<Builtin::kLookupContextTrampoline>({name, depth, slot}));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitLdaLookupScriptContextSlot() {
  // LdaLookupContextSlot <name_index> <feedback_slot> <depth>
  ValueNode* name = GetConstant(GetRefOperand<Name>(0));
  ValueNode* slot = GetTaggedIndexConstant(iterator_.GetIndexOperand(1));
  ValueNode* depth =
      GetTaggedIndexConstant(iterator_.GetUnsignedImmediateOperand(2));
  SetAccumulator(BuildCallBuiltin<Builtin::kLookupScriptContextTrampoline>(
      {name, depth, slot}));
  return ReduceResult::Done();
}

bool MaglevGraphBuilder::CheckContextExtensions(size_t depth) {
  compiler::OptionalScopeInfoRef maybe_scope_info =
      graph()->TryGetScopeInfo(GetContext(), broker());
  if (!maybe_scope_info.has_value()) return false;
  compiler::ScopeInfoRef scope_info = maybe_scope_info.value();
  for (uint32_t d = 0; d < depth; d++) {
    CHECK_NE(scope_info.scope_type(), ScopeType::SCRIPT_SCOPE);
    CHECK_NE(scope_info.scope_type(), ScopeType::REPL_MODE_SCOPE);
    if (scope_info.HasContextExtensionSlot() &&
        !broker()->dependencies()->DependOnEmptyContextExtension(scope_info)) {
      // Using EmptyContextExtension dependency is not possible for this
      // scope_info, so generate dynamic checks.
      ValueNode* context = GetContextAtDepth(GetContext(), d);
      // Only support known contexts so that we can check that there's no
      // extension at compile time. Otherwise we could end up in a deopt loop
      // once we do get an extension.
      compiler::OptionalHeapObjectRef maybe_ref = TryGetConstant(context);
      if (!maybe_ref) return false;
      compiler::ContextRef context_ref = maybe_ref.value().AsContext();
      compiler::OptionalObjectRef extension_ref =
          context_ref.get(broker(), Context::EXTENSION_INDEX);
      // The extension may be concurrently installed while we're checking the
      // context, in which case it may still be uninitialized. This still
      // means an extension is about to appear, so we should block this
      // optimization.
      if (!extension_ref) return false;
      if (!extension_ref->IsUndefined()) return false;
      ValueNode* extension = LoadAndCacheContextSlot(
          context, Context::EXTENSION_INDEX, kMutable, ContextKind::kDefault);
      AddNewNode<CheckValue>({extension}, broker()->undefined_value());
    }
    CHECK_IMPLIES(!scope_info.HasOuterScopeInfo(), d + 1 == depth);
    if (scope_info.HasOuterScopeInfo()) {
      scope_info = scope_info.OuterScopeInfo(broker());
    }
  }
  return true;
}

ReduceResult MaglevGraphBuilder::VisitLdaLookupGlobalSlot() {
  // LdaLookupGlobalSlot <name_index> <feedback_slot> <depth>
  compiler::NameRef name = GetRefOperand<Name>(0);
  if (CheckContextExtensions(iterator_.GetUnsignedImmediateOperand(2))) {
    FeedbackSlot slot = GetSlotOperand(1);
    compiler::FeedbackSource feedback_source{feedback(), slot};
    return BuildLoadGlobal(name, feedback_source, TypeofMode::kNotInside);
  } else {
    ValueNode* name_node = GetConstant(name);
    ValueNode* slot = GetTaggedIndexConstant(iterator_.GetIndexOperand(1));
    ValueNode* depth =
        GetTaggedIndexConstant(iterator_.GetUnsignedImmediateOperand(2));
    ValueNode* result;
    if (parent_) {
      ValueNode* vector = GetConstant(feedback());
      result = BuildCallBuiltin<Builtin::kLookupGlobalIC>(
          {name_node, depth, slot, vector});
    } else {
      result = BuildCallBuiltin<Builtin::kLookupGlobalICTrampoline>(
          {name_node, depth, slot});
    }
    SetAccumulator(result);
    return ReduceResult::Done();
  }
}

ReduceResult MaglevGraphBuilder::VisitLdaLookupSlotInsideTypeof() {
  // LdaLookupSlotInsideTypeof <name_index>
  ValueNode* name = GetConstant(GetRefOperand<Name>(0));
  SetAccumulator(
      BuildCallRuntime(Runtime::kLoadLookupSlotInsideTypeof, {name}).value());
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitLdaLookupContextSlotInsideTypeof() {
  // LdaLookupContextSlotInsideTypeof <name_index> <context_slot> <depth>
  ValueNode* name = GetConstant(GetRefOperand<Name>(0));
  ValueNode* slot = GetTaggedIndexConstant(iterator_.GetIndexOperand(1));
  ValueNode* depth =
      GetTaggedIndexConstant(iterator_.GetUnsignedImmediateOperand(2));
  SetAccumulator(
      BuildCallBuiltin<Builtin::kLookupContextInsideTypeofTrampoline>(
          {name, depth, slot}));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitLdaLookupScriptContextSlotInsideTypeof() {
  // LdaLookupContextSlotInsideTypeof <name_index> <context_slot> <depth>
  ValueNode* name = GetConstant(GetRefOperand<Name>(0));
  ValueNode* slot = GetTaggedIndexConstant(iterator_.GetIndexOperand(1));
  ValueNode* depth =
      GetTaggedIndexConstant(iterator_.GetUnsignedImmediateOperand(2));
  SetAccumulator(
      BuildCallBuiltin<Builtin::kLookupScriptContextInsideTypeofTrampoline>(
          {name, depth, slot}));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitLdaLookupGlobalSlotInsideTypeof() {
  // LdaLookupGlobalSlotInsideTypeof <name_index> <context_slot> <depth>
  ValueNode* name = GetConstant(GetRefOperand<Name>(0));
  ValueNode* slot = GetTaggedIndexConstant(iterator_.GetIndexOperand(1));
  ValueNode* depth =
      GetTaggedIndexConstant(iterator_.GetUnsignedImmediateOperand(2));
  ValueNode* result;
  if (parent_) {
    ValueNode* vector = GetConstant(feedback());
    result = BuildCallBuiltin<Builtin::kLookupGlobalICInsideTypeof>(
        {name, depth, slot, vector});
  } else {
    result = BuildCallBuiltin<Builtin::kLookupGlobalICInsideTypeofTrampoline>(
        {name, depth, slot});
  }
  SetAccumulator(result);
  return ReduceResult::Done();
}

namespace {
Runtime::FunctionId StaLookupSlotFunction(uint8_t sta_lookup_slot_flags) {
  using Flags = interpreter::StoreLookupSlotFlags;
  switch (Flags::GetLanguageMode(sta_lookup_slot_flags)) {
    case LanguageMode::kStrict:
      return Runtime::kStoreLookupSlot_Strict;
    case LanguageMode::kSloppy:
      if (Flags::IsLookupHoistingMode(sta_lookup_slot_flags)) {
        return Runtime::kStoreLookupSlot_SloppyHoisting;
      } else {
        return Runtime::kStoreLookupSlot_Sloppy;
      }
  }
}
}  // namespace

ReduceResult MaglevGraphBuilder::VisitStaLookupSlot() {
  // StaLookupSlot <name_index> <flags>
  ValueNode* value = GetAccumulator();
  ValueNode* name = GetConstant(GetRefOperand<Name>(0));
  uint32_t flags = GetFlag8Operand(1);
  EscapeContext();
  SetAccumulator(
      BuildCallRuntime(StaLookupSlotFunction(flags), {name, value}).value());
  return ReduceResult::Done();
}

NodeType StaticTypeForNode(compiler::JSHeapBroker* broker,
                           LocalIsolate* isolate, ValueNode* node) {
  switch (node->properties().value_representation()) {
    case ValueRepresentation::kInt32:
    case ValueRepresentation::kUint32:
    case ValueRepresentation::kFloat64:
    case ValueRepresentation::kIntPtr:
      return NodeType::kNumber;
    case ValueRepresentation::kHoleyFloat64:
      return NodeType::kNumberOrOddball;
    case ValueRepresentation::kTagged:
      break;
  }
  switch (node->opcode()) {
    case Opcode::kPhi:
      return node->Cast<Phi>()->type();
    case Opcode::kCheckedSmiTagInt32:
    case Opcode::kCheckedSmiTagUint32:
    case Opcode::kCheckedSmiTagIntPtr:
    case Opcode::kCheckedSmiTagFloat64:
    case Opcode::kUnsafeSmiTagInt32:
    case Opcode::kUnsafeSmiTagUint32:
    case Opcode::kUnsafeSmiTagIntPtr:
    case Opcode::kSmiConstant:
      return NodeType::kSmi;
    case Opcode::kInt32ToNumber:
    case Opcode::kUint32ToNumber:
    case Opcode::kIntPtrToNumber:
    case Opcode::kFloat64ToTagged:
      return NodeType::kNumber;
    case Opcode::kHoleyFloat64ToTagged:
      return NodeType::kNumberOrOddball;
    case Opcode::kAllocationBlock:
    case Opcode::kInlinedAllocation:
      return StaticTypeForMap(node->Cast<InlinedAllocation>()->object()->map(),
                              broker);
    case Opcode::kRootConstant: {
      RootConstant* constant = node->Cast<RootConstant>();
      switch (constant->index()) {
        case RootIndex::kTrueValue:
        case RootIndex::kFalseValue:
          return NodeType::kBoolean;
        case RootIndex::kUndefinedValue:
        case RootIndex::kNullValue:
          return NodeType::kOddball;
        default:
          break;
      }
      [[fallthrough]];
    }
    case Opcode::kConstant: {
      compiler::HeapObjectRef ref =
          MaglevGraphBuilder::TryGetConstant(broker, isolate, node).value();
      return StaticTypeForConstant(broker, ref);
    }
    case Opcode::kToNumberOrNumeric:
      if (node->Cast<ToNumberOrNumeric>()->mode() ==
          Object::Conversion::kToNumber) {
        return NodeType::kNumber;
      }
      // TODO(verwaest): Check what we need here.
      return NodeType::kUnknown;
    case Opcode::kToString:
    case Opcode::kNumberToString:
    case Opcode::kStringConcat:
    case Opcode::kStringWrapperConcat:
      return NodeType::kString;
    case Opcode::kCheckedInternalizedString:
      return NodeType::kInternalizedString;
    case Opcode::kToObject:
    case Opcode::kCreateObjectLiteral:
    case Opcode::kCreateShallowObjectLiteral:
      return NodeType::kJSReceiver;
    case Opcode::kCreateArrayLiteral:
    case Opcode::kCreateShallowArrayLiteral:
      return NodeType::kJSArray;
    case Opcode::kToName:
      return NodeType::kName;
    case Opcode::kFastCreateClosure:
    case Opcode::kCreateClosure:
      return NodeType::kCallable;
    case Opcode::kInt32Compare:
    case Opcode::kFloat64Compare:
    case Opcode::kGenericEqual:
    case Opcode::kGenericStrictEqual:
    case Opcode::kGenericLessThan:
    case Opcode::kGenericLessThanOrEqual:
    case Opcode::kGenericGreaterThan:
    case Opcode::kGenericGreaterThanOrEqual:
    case Opcode::kLogicalNot:
    case Opcode::kStringEqual:
    case Opcode::kTaggedEqual:
    case Opcode::kTaggedNotEqual:
    case Opcode::kTestInstanceOf:
    case Opcode::kTestTypeOf:
    case Opcode::kTestUndetectable:
    case Opcode::kToBoolean:
    case Opcode::kToBooleanLogicalNot:
    case Opcode::kIntPtrToBoolean:
      return NodeType::kBoolean;
      // Not value nodes:
#define GENERATE_CASE(Name) case Opcode::k##Name:
      CONTROL_NODE_LIST(GENERATE_CASE)
      NON_VALUE_NODE_LIST(GENERATE_CASE)
#undef GENERATE_CASE
      UNREACHABLE();
    case Opcode::kTransitionElementsKind:
    // Unsorted value nodes. TODO(maglev): See which of these should return
    // something else than kUnknown.
    case Opcode::kIdentity:
    case Opcode::kArgumentsElements:
    case Opcode::kArgumentsLength:
    case Opcode::kRestLength:
    case Opcode::kCall:
    case Opcode::kCallBuiltin:
    case Opcode::kCallCPPBuiltin:
    case Opcode::kCallForwardVarargs:
    case Opcode::kCallRuntime:
    case Opcode::kCallWithArrayLike:
    case Opcode::kCallWithSpread:
    case Opcode::kCallKnownApiFunction:
    case Opcode::kCallKnownJSFunction:
    case Opcode::kCallSelf:
    case Opcode::kConstruct:
    case Opcode::kCheckConstructResult:
    case Opcode::kCheckDerivedConstructResult:
    case Opcode::kConstructWithSpread:
    case Opcode::kConvertReceiver:
    case Opcode::kConvertHoleToUndefined:
    case Opcode::kCreateFunctionContext:
    case Opcode::kCreateRegExpLiteral:
    case Opcode::kDeleteProperty:
    case Opcode::kEnsureWritableFastElements:
    case Opcode::kExtendPropertiesBackingStore:
    case Opcode::kForInPrepare:
    case Opcode::kForInNext:
    case Opcode::kGeneratorRestoreRegister:
    case Opcode::kGetIterator:
    case Opcode::kGetSecondReturnedValue:
    case Opcode::kGetTemplateObject:
    case Opcode::kHasInPrototypeChain:
    case Opcode::kInitialValue:
    case Opcode::kLoadTaggedField:
    case Opcode::kLoadTaggedFieldForProperty:
    case Opcode::kLoadTaggedFieldForContextSlot:
    case Opcode::kLoadTaggedFieldForScriptContextSlot:
    case Opcode::kLoadDoubleField:
    case Opcode::kLoadFloat64:
    case Opcode::kLoadInt32:
    case Opcode::kLoadHeapInt32:
    case Opcode::kLoadTaggedFieldByFieldIndex:
    case Opcode::kLoadFixedArrayElement:
    case Opcode::kLoadFixedDoubleArrayElement:
    case Opcode::kLoadHoleyFixedDoubleArrayElement:
    case Opcode::kLoadHoleyFixedDoubleArrayElementCheckedNotHole:
    case Opcode::kLoadSignedIntDataViewElement:
    case Opcode::kLoadDoubleDataViewElement:
    case Opcode::kLoadTypedArrayLength:
    case Opcode::kLoadSignedIntTypedArrayElement:
    case Opcode::kLoadUnsignedIntTypedArrayElement:
    case Opcode::kLoadDoubleTypedArrayElement:
    case Opcode::kLoadEnumCacheLength:
    case Opcode::kLoadGlobal:
    case Opcode::kLoadNamedGeneric:
    case Opcode::kLoadNamedFromSuperGeneric:
    case Opcode::kMaybeGrowFastElements:
    case Opcode::kMigrateMapIfNeeded:
    case Opcode::kSetNamedGeneric:
    case Opcode::kDefineNamedOwnGeneric:
    case Opcode::kStoreInArrayLiteralGeneric:
    case Opcode::kStoreGlobal:
    case Opcode::kGetKeyedGeneric:
    case Opcode::kSetKeyedGeneric:
    case Opcode::kDefineKeyedOwnGeneric:
    case Opcode::kRegisterInput:
    case Opcode::kCheckedSmiSizedInt32:
    case Opcode::kCheckedSmiUntag:
    case Opcode::kUnsafeSmiUntag:
    case Opcode::kCheckedObjectToIndex:
    case Opcode::kCheckedTruncateNumberOrOddballToInt32:
    case Opcode::kCheckedInt32ToUint32:
    case Opcode::kCheckedIntPtrToUint32:
    case Opcode::kUnsafeInt32ToUint32:
    case Opcode::kCheckedUint32ToInt32:
    case Opcode::kCheckedIntPtrToInt32:
    case Opcode::kChangeInt32ToFloat64:
    case Opcode::kChangeUint32ToFloat64:
    case Opcode::kChangeIntPtrToFloat64:
    case Opcode::kCheckedTruncateFloat64ToInt32:
    case Opcode::kCheckedTruncateFloat64ToUint32:
    case Opcode::kTruncateNumberOrOddballToInt32:
    case Opcode::kCheckedNumberToInt32:
    case Opcode::kTruncateUint32ToInt32:
    case Opcode::kTruncateFloat64ToInt32:
    case Opcode::kUnsafeTruncateUint32ToInt32:
    case Opcode::kUnsafeTruncateFloat64ToInt32:
    case Opcode::kInt32ToUint8Clamped:
    case Opcode::kUint32ToUint8Clamped:
    case Opcode::kFloat64ToUint8Clamped:
    case Opcode::kCheckedNumberToUint8Clamped:
    case Opcode::kFloat64ToHeapNumberForField:
    case Opcode::kCheckedNumberOrOddballToFloat64:
    case Opcode::kUncheckedNumberOrOddballToFloat64:
    case Opcode::kCheckedNumberOrOddballToHoleyFloat64:
    case Opcode::kCheckedHoleyFloat64ToFloat64:
    case Opcode::kHoleyFloat64ToMaybeNanFloat64:
    case Opcode::kHoleyFloat64IsHole:
    case Opcode::kSetPendingMessage:
    case Opcode::kStringAt:
    case Opcode::kStringLength:
    case Opcode::kAllocateElementsArray:
    case Opcode::kUpdateJSArrayLength:
    case Opcode::kVirtualObject:
    case Opcode::kGetContinuationPreservedEmbedderData:
    case Opcode::kExternalConstant:
    case Opcode::kFloat64Constant:
    case Opcode::kInt32Constant:
    case Opcode::kUint32Constant:
    case Opcode::kTaggedIndexConstant:
    case Opcode::kTrustedConstant:
    case Opcode::kInt32AbsWithOverflow:
    case Opcode::kInt32AddWithOverflow:
    case Opcode::kInt32SubtractWithOverflow:
    case Opcode::kInt32MultiplyWithOverflow:
    case Opcode::kInt32DivideWithOverflow:
    case Opcode::kInt32ModulusWithOverflow:
    case Opcode::kInt32BitwiseAnd:
    case Opcode::kInt32BitwiseOr:
    case Opcode::kInt32BitwiseXor:
    case Opcode::kInt32ShiftLeft:
    case Opcode::kInt32ShiftRight:
    case Opcode::kInt32ShiftRightLogical:
    case Opcode::kInt32BitwiseNot:
    case Opcode::kInt32NegateWithOverflow:
    case Opcode::kInt32IncrementWithOverflow:
    case Opcode::kInt32DecrementWithOverflow:
    case Opcode::kInt32ToBoolean:
    case Opcode::kFloat64Abs:
    case Opcode::kFloat64Add:
    case Opcode::kFloat64Subtract:
    case Opcode::kFloat64Multiply:
    case Opcode::kFloat64Divide:
    case Opcode::kFloat64Exponentiate:
    case Opcode::kFloat64Modulus:
    case Opcode::kFloat64Negate:
    case Opcode::kFloat64Round:
    case Opcode::kFloat64ToBoolean:
    case Opcode::kFloat64Ieee754Unary:
    case Opcode::kCheckedSmiIncrement:
    case Opcode::kCheckedSmiDecrement:
    case Opcode::kGenericAdd:
    case Opcode::kGenericSubtract:
    case Opcode::kGenericMultiply:
    case Opcode::kGenericDivide:
    case Opcode::kGenericModulus:
    case Opcode::kGenericExponentiate:
    case Opcode::kGenericBitwiseAnd:
    case Opcode::kGenericBitwiseOr:
    case Opcode::kGenericBitwiseXor:
    case Opcode::kGenericShiftLeft:
    case Opcode::kGenericShiftRight:
    case Opcode::kGenericShiftRightLogical:
    case Opcode::kGenericBitwiseNot:
    case Opcode::kGenericNegate:
    case Opcode::kGenericIncrement:
    case Opcode::kGenericDecrement:
    case Opcode::kBuiltinStringFromCharCode:
    case Opcode::kBuiltinStringPrototypeCharCodeOrCodePointAt:
      return NodeType::kUnknown;
  }
}

bool MaglevGraphBuilder::CheckStaticType(ValueNode* node, NodeType type,
                                         NodeType* current_type) {
  NodeType static_type = StaticTypeForNode(broker(), local_isolate(), node);
  if (current_type) *current_type = static_type;
  return NodeTypeIs(static_type, type);
}

bool MaglevGraphBuilder::EnsureType(ValueNode* node, NodeType type,
                                    NodeType* old_type) {
  if (CheckStaticType(node, type, old_type)) return true;
  NodeInfo* known_info = GetOrCreateInfoFor(node);
  if (old_type) *old_type = known_info->type();
  if (NodeTypeIs(known_info->type(), type)) return true;
  known_info->CombineType(type);
  if (auto phi = node->TryCast<Phi>()) {
    known_info->CombineType(phi->type());
  }
  return false;
}

template <typename Function>
bool MaglevGraphBuilder::EnsureType(ValueNode* node, NodeType type,
                                    Function ensure_new_type) {
  if (CheckStaticType(node, type)) return true;
  NodeInfo* known_info = GetOrCreateInfoFor(node);
  if (NodeTypeIs(known_info->type(), type)) return true;
  ensure_new_type(known_info->type());
  known_info->CombineType(type);
  return false;
}

void MaglevGraphBuilder::SetKnownValue(ValueNode* node, compiler::ObjectRef ref,
                                       NodeType new_node_type) {
  DCHECK(!node->Is<Constant>());
  DCHECK(!node->Is<RootConstant>());
  NodeInfo* known_info = GetOrCreateInfoFor(node);
  // ref type should be compatible with type.
  DCHECK(NodeTypeIs(StaticTypeForConstant(broker(), ref), new_node_type));
  if (ref.IsHeapObject()) {
    DCHECK(IsInstanceOfNodeType(ref.AsHeapObject().map(broker()),
                                known_info->type(), broker()));
  } else {
    DCHECK(!NodeTypeIs(known_info->type(), NodeType::kAnyHeapObject));
  }
  known_info->CombineType(new_node_type);
  known_info->alternative().set_checked_value(GetConstant(ref));
}

NodeType MaglevGraphBuilder::CheckTypes(ValueNode* node,
                                        std::initializer_list<NodeType> types) {
  auto it = known_node_aspects().FindInfo(node);
  bool has_kna = known_node_aspects().IsValid(it);
  for (NodeType type : types) {
    if (CheckStaticType(node, type)) return type;
    if (has_kna) {
      if (NodeTypeIs(it->second.type(), type)) return type;
    }
  }
  return NodeType::kUnknown;
}

bool MaglevGraphBuilder::CheckType(ValueNode* node, NodeType type,
                                   NodeType* current_type) {
  if (CheckStaticType(node, type, current_type)) return true;
  auto it = known_node_aspects().FindInfo(node);
  if (!known_node_aspects().IsValid(it)) return false;
  if (current_type) *current_type = it->second.type();
  return NodeTypeIs(it->second.type(), type);
}

NodeType MaglevGraphBuilder::GetType(ValueNode* node) {
  auto it = known_node_aspects().FindInfo(node);
  if (!known_node_aspects().IsValid(it)) {
    return StaticTypeForNode(broker(), local_isolate(), node);
  }
  NodeType actual_type = it->second.type();
  if (auto phi = node->TryCast<Phi>()) {
    actual_type = CombineType(actual_type, phi->type());
  }
#ifdef DEBUG
  NodeType static_type = StaticTypeForNode(broker(), local_isolate(), node);
  if (!NodeTypeIs(actual_type, static_type)) {
    // In case we needed a numerical alternative of a smi value, the type
    // must generalize. In all other cases the node info type should reflect the
    // actual type.
    DCHECK(static_type == NodeType::kSmi && actual_type == NodeType::kNumber &&
           !known_node_aspects().TryGetInfoFor(node)->alternative().has_none());
  }
#endif  // DEBUG
  return actual_type;
}

bool MaglevGraphBuilder::HaveDisjointTypes(ValueNode* lhs, ValueNode* rhs) {
  return HasDisjointType(lhs, GetType(rhs));
}

// Note: this is conservative, ie, returns true if {lhs} cannot be {rhs}.
// It might return false even if {lhs} is not {rhs}.
bool MaglevGraphBuilder::HasDisjointType(ValueNode* lhs, NodeType rhs_type) {
  NodeType lhs_type = GetType(lhs);
  NodeType meet = CombineType(lhs_type, rhs_type);
  return NodeTypeCannotHaveInstances(meet);
}

bool MaglevGraphBuilder::MayBeNullOrUndefined(ValueNode* node) {
  NodeType static_type = StaticTypeForNode(broker(), local_isolate(), node);
  if (!NodeTypeMayBeNullOrUndefined(static_type)) return false;
  auto it = known_node_aspects().FindInfo(node);
  if (!known_node_aspects().IsValid(it)) return true;
  return NodeTypeMayBeNullOrUndefined(it->second.type());
}

ValueNode* MaglevGraphBuilder::BuildSmiUntag(ValueNode* node) {
  if (EnsureType(node, NodeType::kSmi)) {
    if (SmiValuesAre31Bits()) {
      if (auto phi = node->TryCast<Phi>()) {
        phi->SetUseRequires31BitValue();
      }
    }
    return AddNewNode<UnsafeSmiUntag>({node});
  } else {
    return AddNewNode<CheckedSmiUntag>({node});
  }
}

ValueNode* MaglevGraphBuilder::BuildNumberOrOddballToFloat64(
    ValueNode* node, NodeType allowed_input_type,
    TaggedToFloat64ConversionType conversion_type) {
  NodeType old_type;
  if (EnsureType(node, allowed_input_type, &old_type)) {
    if (old_type == NodeType::kSmi) {
      ValueNode* untagged_smi = BuildSmiUntag(node);
      return AddNewNode<ChangeInt32ToFloat64>({untagged_smi});
    }
    return AddNewNode<UncheckedNumberOrOddballToFloat64>({node},
                                                         conversion_type);
  } else {
    return AddNewNode<CheckedNumberOrOddballToFloat64>({node}, conversion_type);
  }
}

ReduceResult MaglevGraphBuilder::BuildCheckSmi(ValueNode* object,
                                               bool elidable) {
  if (CheckStaticType(object, NodeType::kSmi)) return object;
  if (CheckType(object, NodeType::kAnyHeapObject)) {
    return EmitUnconditionalDeopt(DeoptimizeReason::kNotASmi);
  }
  if (EnsureType(object, NodeType::kSmi) && elidable) return object;
  switch (object->value_representation()) {
    case ValueRepresentation::kInt32:
      if (!SmiValuesAre32Bits()) {
        AddNewNode<CheckInt32IsSmi>({object});
      }
      break;
    case ValueRepresentation::kUint32:
      AddNewNode<CheckUint32IsSmi>({object});
      break;
    case ValueRepresentation::kFloat64:
    case ValueRepresentation::kHoleyFloat64:
      AddNewNode<CheckHoleyFloat64IsSmi>({object});
      break;
    case ValueRepresentation::kTagged:
      AddNewNode<CheckSmi>({object});
      break;
    case ValueRepresentation::kIntPtr:
      AddNewNode<CheckIntPtrIsSmi>({object});
      break;
  }
  return object;
}

void MaglevGraphBuilder::BuildCheckHeapObject(ValueNode* object) {
  if (EnsureType(object, NodeType::kAnyHeapObject)) return;
  AddNewNode<CheckHeapObject>({object});
}

void MaglevGraphBuilder::BuildCheckString(ValueNode* object) {
  NodeType known_type;
  if (EnsureType(object, NodeType::kString, &known_type)) return;
  AddNewNode<CheckString>({object}, GetCheckType(known_type));
}

void MaglevGraphBuilder::BuildCheckStringOrStringWrapper(ValueNode* object) {
  NodeType known_type;
  if (EnsureType(object, NodeType::kStringOrStringWrapper, &known_type)) return;
  AddNewNode<CheckStringOrStringWrapper>({object}, GetCheckType(known_type));
}

void MaglevGraphBuilder::BuildCheckNumber(ValueNode* object) {
  if (EnsureType(object, NodeType::kNumber)) return;
  AddNewNode<CheckNumber>({object}, Object::Conversion::kToNumber);
}

void MaglevGraphBuilder::BuildCheckSymbol(ValueNode* object) {
  NodeType known_type;
  if (EnsureType(object, NodeType::kSymbol, &known_type)) return;
  AddNewNode<CheckSymbol>({object}, GetCheckType(known_type));
}

void MaglevGraphBuilder::BuildCheckJSReceiver(ValueNode* object) {
  NodeType known_type;
  if (EnsureType(object, NodeType::kJSReceiver, &known_type)) return;
  AddNewNode<CheckInstanceType>({object}, GetCheckType(known_type),
                                FIRST_JS_RECEIVER_TYPE, LAST_JS_RECEIVER_TYPE);
}

namespace {

class KnownMapsMerger {
 public:
  explicit KnownMapsMerger(compiler::JSHeapBroker* broker, Zone* zone,
                           base::Vector<const compiler::MapRef> requested_maps)
      : broker_(broker), zone_(zone), requested_maps_(requested_maps) {}

  void IntersectWithKnownNodeAspects(
      ValueNode* object, const KnownNodeAspects& known_node_aspects) {
    auto node_info_it = known_node_aspects.FindInfo(object);
    bool has_node_info = known_node_aspects.IsValid(node_info_it);
    NodeType type =
        has_node_info ? node_info_it->second.type() : NodeType::kUnknown;
    if (has_node_info && node_info_it->second.possible_maps_are_known()) {
      // TODO(v8:7700): Make intersection non-quadratic.
      for (compiler::MapRef possible_map :
           node_info_it->second.possible_maps()) {
        if (std::find(requested_maps_.begin(), requested_maps_.end(),
                      possible_map) != requested_maps_.end()) {
          // No need to add dependencies, we already have them for all known
          // possible maps.
          // Filter maps which are impossible given this objects type. Since we
          // want to prove that an object with map `map` is not an instance of
          // `type`, we cannot use `StaticTypeForMap`, as it only provides an
          // approximation. This filtering is done to avoid creating
          // non-sensical types later (e.g. if we think only a non-string map
          // is possible, after a string check).
          if (IsInstanceOfNodeType(possible_map, type, broker_)) {
            InsertMap(possible_map);
          }
        } else {
          known_maps_are_subset_of_requested_maps_ = false;
        }
      }
      if (intersect_set_.is_empty()) {
        node_type_ = NodeType::kUnknown;
      }
    } else {
      // A missing entry here means the universal set, i.e., we don't know
      // anything about the possible maps of the object. Intersect with the
      // universal set, which means just insert all requested maps.
      known_maps_are_subset_of_requested_maps_ = false;
      existing_known_maps_found_ = false;
      for (compiler::MapRef map : requested_maps_) {
        InsertMap(map);
      }
    }
  }

  void UpdateKnownNodeAspects(ValueNode* object,
                              KnownNodeAspects& known_node_aspects) {
    // Update known maps.
    auto node_info = known_node_aspects.GetOrCreateInfoFor(
        object, broker_, broker_->local_isolate());
    node_info->SetPossibleMaps(intersect_set_, any_map_is_unstable_, node_type_,
                               broker_);
    // Make sure known_node_aspects.any_map_for_any_node_is_unstable is updated
    // in case any_map_is_unstable changed to true for this object -- this can
    // happen if this was an intersection with the universal set which added new
    // possible unstable maps.
    if (any_map_is_unstable_) {
      known_node_aspects.any_map_for_any_node_is_unstable = true;
    }
    // At this point, known_node_aspects.any_map_for_any_node_is_unstable may be
    // true despite there no longer being any unstable maps for any nodes (if
    // this was the only node with unstable maps and this intersection removed
    // those). This is ok, because that's at worst just an overestimate -- we
    // could track whether this node's any_map_is_unstable flipped from true to
    // false, but this is likely overkill.
    // Insert stable map dependencies which weren't inserted yet. This is only
    // needed if our set of known maps was empty and we created it anew based on
    // maps we checked.
    if (!existing_known_maps_found_) {
      for (compiler::MapRef map : intersect_set_) {
        if (map.is_stable()) {
          broker_->dependencies()->DependOnStableMap(map);
        }
      }
    } else {
      // TODO(victorgomes): Add a DCHECK_SLOW that checks if the maps already
      // exist in the CompilationDependencySet.
    }
  }

  bool known_maps_are_subset_of_requested_maps() const {
    return known_maps_are_subset_of_requested_maps_;
  }
  bool emit_check_with_migration() const { return emit_check_with_migration_; }

  const compiler::ZoneRefSet<Map>& intersect_set() const {
    return intersect_set_;
  }

  NodeType node_type() const { return node_type_; }

 private:
  compiler::JSHeapBroker* broker_;
  Zone* zone_;
  base::Vector<const compiler::MapRef> requested_maps_;
  compiler::ZoneRefSet<Map> intersect_set_;
  bool known_maps_are_subset_of_requested_maps_ = true;
  bool existing_known_maps_found_ = true;
  bool emit_check_with_migration_ = false;
  bool any_map_is_unstable_ = false;
  NodeType node_type_ = static_cast<NodeType>(-1);

  Zone* zone() const { return zone_; }

  void InsertMap(compiler::MapRef map) {
    if (map.is_migration_target()) {
      emit_check_with_migration_ = true;
    }
    NodeType new_type = StaticTypeForMap(map, broker_);
    if (new_type == NodeType::kHeapNumber) {
      new_type = IntersectType(new_type, NodeType::kSmi);
    }
    node_type_ = IntersectType(node_type_, new_type);
    if (!map.is_stable()) {
      any_map_is_unstable_ = true;
    }
    intersect_set_.insert(map, zone());
  }
};

}  // namespace

ReduceResult MaglevGraphBuilder::BuildCheckMaps(
    ValueNode* object, base::Vector<const compiler::MapRef> maps,
    std::optional<ValueNode*> map,
    bool has_deprecated_map_without_migration_target) {
  // TODO(verwaest): Support other objects with possible known stable maps as
  // well.
  if (compiler::OptionalHeapObjectRef constant = TryGetConstant(object)) {
    // For constants with stable maps that match one of the desired maps, we
    // don't need to emit a map check, and can use the dependency -- we
    // can't do this for unstable maps because the constant could migrate
    // during compilation.
    compiler::MapRef constant_map = constant.value().map(broker());
    if (std::find(maps.begin(), maps.end(), constant_map) != maps.end()) {
      if (constant_map.is_stable()) {
        broker()->dependencies()->DependOnStableMap(constant_map);
        return ReduceResult::Done();
      }
      // TODO(verwaest): Reduce maps to the constant map.
    } else {
      // TODO(leszeks): Insert an unconditional deopt if the constant map
      // doesn't match the required map.
    }
  }

  NodeInfo* known_info = GetOrCreateInfoFor(object);

  // Calculates if known maps are a subset of maps, their map intersection and
  // whether we should emit check with migration.
  KnownMapsMerger merger(broker(), zone(), maps);
  merger.IntersectWithKnownNodeAspects(object, known_node_aspects());

  // If the known maps are the subset of the maps to check, we are done.
  if (merger.known_maps_are_subset_of_requested_maps()) {
    // The node type of known_info can get out of sync with the possible maps.
    // For instance after merging with an effectively dead branch (i.e., check
    // contradicting all possible maps).
    // TODO(olivf) Try to combine node_info and possible maps and ensure that
    // narrowing the type also clears impossible possible_maps.
    if (!NodeTypeIs(known_info->type(), merger.node_type())) {
      known_info->IntersectType(merger.node_type());
    }
#ifdef DEBUG
    // Double check that, for every possible map, it's one of the maps we'd
    // want to check.
    for (compiler::MapRef possible_map :
         known_node_aspects().TryGetInfoFor(object)->possible_maps()) {
      DCHECK_NE(std::find(maps.begin(), maps.end(), possible_map), maps.end());
    }
#endif
    return ReduceResult::Done();
  }

  if (merger.intersect_set().is_empty()) {
    return EmitUnconditionalDeopt(DeoptimizeReason::kWrongMap);
  }

  // TODO(v8:7700): Check if the {maps} - {known_maps} size is smaller than
  // {maps} \intersect {known_maps}, we can emit CheckNotMaps instead.

  // Emit checks.
  if (merger.emit_check_with_migration()) {
    AddNewNode<CheckMapsWithMigration>({object}, merger.intersect_set(),
                                       GetCheckType(known_info->type()));
  } else if (has_deprecated_map_without_migration_target) {
    AddNewNode<CheckMapsWithMigrationAndDeopt>(
        {object}, merger.intersect_set(), GetCheckType(known_info->type()));
  } else if (map) {
    AddNewNode<CheckMapsWithAlreadyLoadedMap>({object, *map},
                                              merger.intersect_set());
  } else {
    AddNewNode<CheckMaps>({object}, merger.intersect_set(),
                          GetCheckType(known_info->type()));
  }

  merger.UpdateKnownNodeAspects(object, known_node_aspects());
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::BuildTransitionElementsKindOrCheckMap(
    ValueNode* heap_object, ValueNode* object_map,
    const ZoneVector<compiler::MapRef>& transition_sources,
    compiler::MapRef transition_target) {
  // TODO(marja): Optimizations based on what we know about the intersection of
  // known maps and transition sources or transition target.

  // TransitionElementsKind doesn't happen in cases where we'd need to do
  // CheckMapsWithMigration instead of CheckMaps.
  CHECK(!transition_target.is_migration_target());
  for (const compiler::MapRef transition_source : transition_sources) {
    CHECK(!transition_source.is_migration_target());
  }

  NodeInfo* known_info = GetOrCreateInfoFor(heap_object);

  AddNewNode<TransitionElementsKindOrCheckMap>(
      {heap_object, object_map}, transition_sources, transition_target);
  // After this operation, heap_object's map is transition_target (or we
  // deopted).
  known_info->SetPossibleMaps(
      PossibleMaps{transition_target}, !transition_target.is_stable(),
      StaticTypeForMap(transition_target, broker()), broker());
  DCHECK(transition_target.IsJSReceiverMap());
  if (!transition_target.is_stable()) {
    known_node_aspects().any_map_for_any_node_is_unstable = true;
  } else {
    broker()->dependencies()->DependOnStableMap(transition_target);
  }
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::BuildCompareMaps(
    ValueNode* heap_object, ValueNode* object_map,
    base::Vector<const compiler::MapRef> maps, MaglevSubGraphBuilder* sub_graph,
    std::optional<MaglevSubGraphBuilder::Label>& if_not_matched) {
  GetOrCreateInfoFor(heap_object);
  KnownMapsMerger merger(broker(), zone(), maps);
  merger.IntersectWithKnownNodeAspects(heap_object, known_node_aspects());

  if (merger.intersect_set().is_empty()) {
    return ReduceResult::DoneWithAbort();
  }

  // TODO(pthier): Support map packing.
  DCHECK(!V8_MAP_PACKING_BOOL);

  // TODO(pthier): Handle map migrations.
  std::optional<MaglevSubGraphBuilder::Label> map_matched;
  const compiler::ZoneRefSet<Map>& relevant_maps = merger.intersect_set();
  if (relevant_maps.size() > 1) {
    map_matched.emplace(sub_graph, static_cast<int>(relevant_maps.size()));
    for (size_t map_index = 1; map_index < relevant_maps.size(); map_index++) {
      sub_graph->GotoIfTrue<BranchIfReferenceEqual>(
          &*map_matched,
          {object_map, GetConstant(relevant_maps.at(map_index))});
    }
  }
  if_not_matched.emplace(sub_graph, 1);
  sub_graph->GotoIfFalse<BranchIfReferenceEqual>(
      &*if_not_matched, {object_map, GetConstant(relevant_maps.at(0))});
  if (map_matched.has_value()) {
    sub_graph->Goto(&*map_matched);
    sub_graph->Bind(&*map_matched);
  }
  merger.UpdateKnownNodeAspects(heap_object, known_node_aspects());
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::BuildTransitionElementsKindAndCompareMaps(
    ValueNode* heap_object, ValueNode* object_map,
    const ZoneVector<compiler::MapRef>& transition_sources,
    compiler::MapRef transition_target, MaglevSubGraphBuilder* sub_graph,
    std::optional<MaglevSubGraphBuilder::Label>& if_not_matched) {
  DCHECK(!transition_target.is_migration_target());

  NodeInfo* known_info = GetOrCreateInfoFor(heap_object);

  // TODO(pthier): Calculate and use the intersection of known maps with
  // (transition_sources union transition_target).

  ValueNode* new_map = AddNewNode<TransitionElementsKind>(
      {heap_object, object_map}, transition_sources, transition_target);

  // TODO(pthier): Support map packing.
  DCHECK(!V8_MAP_PACKING_BOOL);
  if_not_matched.emplace(sub_graph, 1);
  sub_graph->GotoIfFalse<BranchIfReferenceEqual>(
      &*if_not_matched, {new_map, GetConstant(transition_target)});
  // After the branch, object's map is transition_target.
  DCHECK(transition_target.IsJSReceiverMap());
  known_info->SetPossibleMaps(
      PossibleMaps{transition_target}, !transition_target.is_stable(),
      StaticTypeForMap(transition_target, broker()), broker());
  if (!transition_target.is_stable()) {
    known_node_aspects().any_map_for_any_node_is_unstable = true;
  } else {
    broker()->dependencies()->DependOnStableMap(transition_target);
  }
  return ReduceResult::Done();
}

namespace {
AllocationBlock* GetAllocation(ValueNode* object) {
  if (object->Is<InlinedAllocation>()) {
    object = object->Cast<InlinedAllocation>()->input(0).node();
  }
  if (object->Is<AllocationBlock>()) {
    return object->Cast<AllocationBlock>();
  }
  return nullptr;
}
}  // namespace

bool MaglevGraphBuilder::CanElideWriteBarrier(ValueNode* object,
                                              ValueNode* value) {
  if (value->Is<RootConstant>()) return true;
  if (CheckType(value, NodeType::kSmi)) {
    RecordUseReprHintIfPhi(value, UseRepresentation::kTagged);
    return true;
  }

  // No need for a write barrier if both object and value are part of the same
  // folded young allocation.
  AllocationBlock* allocation = GetAllocation(object);
  if (allocation != nullptr &&
      allocation->allocation_type() == AllocationType::kYoung &&
      allocation == GetAllocation(value)) {
    return true;
  }

  // If tagged and not Smi, we cannot elide write barrier.
  if (value->is_tagged()) return false;

  // If its alternative conversion node is Smi, {value} will be converted to
  // a Smi when tagged.
  NodeInfo* node_info = GetOrCreateInfoFor(value);
  if (ValueNode* tagged_alt = node_info->alternative().tagged()) {
    DCHECK(tagged_alt->properties().is_conversion());
    return CheckType(tagged_alt, NodeType::kSmi);
  }
  return false;
}

void MaglevGraphBuilder::BuildInitializeStore(InlinedAllocation* object,
                                              ValueNode* value, int offset) {
  const bool value_is_trusted = value->Is<TrustedConstant>();
  DCHECK(value->is_tagged());
  if (InlinedAllocation* inlined_value = value->TryCast<InlinedAllocation>()) {
    // Add to the escape set.
    auto escape_deps = graph()->allocations_escape_map().find(object);
    CHECK(escape_deps != graph()->allocations_escape_map().end());
    escape_deps->second.push_back(inlined_value);
    // Add to the elided set.
    auto& elided_map = graph()->allocations_elide_map();
    auto elided_deps = elided_map.try_emplace(inlined_value, zone()).first;
    elided_deps->second.push_back(object);
    inlined_value->AddNonEscapingUses();
  }
  if (value_is_trusted) {
    BuildStoreTrustedPointerField(object, value, offset,
                                  value->Cast<TrustedConstant>()->tag(),
                                  StoreTaggedMode::kInitializing);
  } else {
    BuildStoreTaggedField(object, value, offset,
                          StoreTaggedMode::kInitializing);
  }
}

namespace {
bool IsEscaping(Graph* graph, InlinedAllocation* alloc) {
  if (alloc->IsEscaping()) return true;
  auto it = graph->allocations_elide_map().find(alloc);
  if (it == graph->allocations_elide_map().end()) return false;
  for (InlinedAllocation* inner_alloc : it->second) {
    if (IsEscaping(graph, inner_alloc)) {
      return true;
    }
  }
  return false;
}

bool VerifyIsNotEscaping(VirtualObject::List vos, InlinedAllocation* alloc) {
  for (VirtualObject* vo : vos) {
    if (vo->type() != VirtualObject::kDefault) continue;
    if (vo->allocation() == alloc) continue;
    for (uint32_t i = 0; i < vo->slot_count(); i++) {
      ValueNode* nested_value = vo->get_by_index(i);
      if (!nested_value->Is<InlinedAllocation>()) continue;
      ValueNode* nested_alloc = nested_value->Cast<InlinedAllocation>();
      if (nested_alloc == alloc) {
        if (vo->allocation()->IsEscaping()) return false;
        if (!VerifyIsNotEscaping(vos, vo->allocation())) return false;
      }
    }
  }
  return true;
}
}  // namespace

bool MaglevGraphBuilder::CanTrackObjectChanges(ValueNode* receiver,
                                               TrackObjectMode mode) {
  DCHECK(!receiver->Is<VirtualObject>());
  if (!v8_flags.maglev_object_tracking) return false;
  if (!receiver->Is<InlinedAllocation>()) return false;
  InlinedAllocation* alloc = receiver->Cast<InlinedAllocation>();
  if (mode == TrackObjectMode::kStore) {
    // If we have two objects A and B, such that A points to B (it contains B in
    // one of its field), we cannot change B without also changing A, even if
    // both can be elided. For now, we escape both objects instead.
    if (graph_->allocations_elide_map().find(alloc) !=
        graph_->allocations_elide_map().end()) {
      return false;
    }
    if (alloc->IsEscaping()) return false;
    // Ensure object is escaped if we are within a try-catch block. This is
    // crucial because a deoptimization point inside the catch handler could
    // re-materialize objects differently, depending on whether the throw
    // occurred before or after this store. We could potentially relax this
    // requirement by verifying that no throwable nodes have been emitted since
    // the try-block started,  but for now, err on the side of caution and
    // always escape.
    if (IsInsideTryBlock()) return false;
  } else {
    DCHECK_EQ(mode, TrackObjectMode::kLoad);
    if (IsEscaping(graph_, alloc)) return false;
  }
  // We don't support loop phis inside VirtualObjects, so any access inside a
  // loop should escape the object, except for objects that were created since
  // the last loop header.
  if (IsInsideLoop()) {
    if (!is_loop_effect_tracking() ||
        !loop_effects_->allocations.contains(alloc)) {
      return false;
    }
  }
  // Iterate all live objects to be sure that the allocation is not escaping.
  SLOW_DCHECK(
      VerifyIsNotEscaping(current_interpreter_frame_.virtual_objects(), alloc));
  return true;
}

VirtualObject* MaglevGraphBuilder::GetObjectFromAllocation(
    InlinedAllocation* allocation) {
  VirtualObject* vobject = allocation->object();
  // If it hasn't be snapshotted yet, it is the latest created version of this
  // object, we don't need to search for it.
  if (vobject->IsSnapshot()) {
    vobject = current_interpreter_frame_.virtual_objects().FindAllocatedWith(
        allocation);
  }
  return vobject;
}

VirtualObject* MaglevGraphBuilder::GetModifiableObjectFromAllocation(
    InlinedAllocation* allocation) {
  VirtualObject* vobject = allocation->object();
  // If it hasn't be snapshotted yet, it is the latest created version of this
  // object and we can still modify it, we don't need to copy it.
  if (vobject->IsSnapshot()) {
    return DeepCopyVirtualObject(
        current_interpreter_frame_.virtual_objects().FindAllocatedWith(
            allocation));
  }
  return vobject;
}

void MaglevGraphBuilder::TryBuildStoreTaggedFieldToAllocation(ValueNode* object,
                                                              ValueNode* value,
                                                              int offset) {
  if (offset == HeapObject::kMapOffset) return;
  if (!CanTrackObjectChanges(object, TrackObjectMode::kStore)) return;
  // This avoids loop in the object graph.
  if (value->Is<InlinedAllocation>()) return;
  InlinedAllocation* allocation = object->Cast<InlinedAllocation>();
  VirtualObject* vobject = GetModifiableObjectFromAllocation(allocation);
  CHECK_EQ(vobject->type(), VirtualObject::kDefault);
  CHECK_NOT_NULL(vobject);
  vobject->set(offset, value);
  AddNonEscapingUses(allocation, 1);
  if (v8_flags.trace_maglev_object_tracking) {
    std::cout << "  * Setting value in virtual object "
              << PrintNodeLabel(graph_labeller(), vobject) << "[" << offset
              << "]: " << PrintNode(graph_labeller(), value) << std::endl;
  }
}

Node* MaglevGraphBuilder::BuildStoreTaggedField(ValueNode* object,
                                                ValueNode* value, int offset,
                                                StoreTaggedMode store_mode) {
  // The value may be used to initialize a VO, which can leak to IFS.
  // It should NOT be a conversion node, UNLESS it's an initializing value.
  // Initializing values are tagged before allocation, since conversion nodes
  // may allocate, and are not used to set a VO.
  DCHECK_IMPLIES(store_mode != StoreTaggedMode::kInitializing,
                 !value->properties().is_conversion());
  if (store_mode != StoreTaggedMode::kInitializing) {
    TryBuildStoreTaggedFieldToAllocation(object, value, offset);
  }
  if (CanElideWriteBarrier(object, value)) {
    return AddNewNode<StoreTaggedFieldNoWriteBarrier>({object, value}, offset,
                                                      store_mode);
  } else {
    return AddNewNode<StoreTaggedFieldWithWriteBarrier>({object, value}, offset,
                                                        store_mode);
  }
}

void MaglevGraphBuilder::BuildStoreTaggedFieldNoWriteBarrier(
    ValueNode* object, ValueNode* value, int offset,
    StoreTaggedMode store_mode) {
  // The value may be used to initialize a VO, which can leak to IFS.
  // It should NOT be a conversion node, UNLESS it's an initializing value.
  // Initializing values are tagged before allocation, since conversion nodes
  // may allocate, and are not used to set a VO.
  DCHECK_IMPLIES(store_mode != StoreTaggedMode::kInitializing,
                 !value->properties().is_conversion());
  DCHECK(CanElideWriteBarrier(object, value));
  if (store_mode != StoreTaggedMode::kInitializing) {
    TryBuildStoreTaggedFieldToAllocation(object, value, offset);
  }
  AddNewNode<StoreTaggedFieldNoWriteBarrier>({object, value}, offset,
                                             store_mode);
}

void MaglevGraphBuilder::BuildStoreTrustedPointerField(
    ValueNode* object, ValueNode* value, int offset, IndirectPointerTag tag,
    StoreTaggedMode store_mode) {
#ifdef V8_ENABLE_SANDBOX
  AddNewNode<StoreTrustedPointerFieldWithWriteBarrier>({object, value}, offset,
                                                       tag, store_mode);
#else
  BuildStoreTaggedField(object, value, offset, store_mode);
#endif  // V8_ENABLE_SANDBOX
}

ValueNode* MaglevGraphBuilder::BuildLoadFixedArrayElement(ValueNode* elements,
                                                          int index) {
  compiler::OptionalHeapObjectRef maybe_constant;
  if ((maybe_constant = TryGetConstant(elements)) &&
      maybe_constant.value().IsFixedArray()) {
    compiler::FixedArrayRef fixed_array_ref =
        maybe_constant.value().AsFixedArray();
    if (index >= 0 && static_cast<uint32_t>(index) < fixed_array_ref.length()) {
      compiler::OptionalObjectRef maybe_value =
          fixed_array_ref.TryGet(broker(), index);
      if (maybe_value) return GetConstant(*maybe_value);
    } else {
      return GetRootConstant(RootIndex::kTheHoleValue);
    }
  }
  if (CanTrackObjectChanges(elements, TrackObjectMode::kLoad)) {
    VirtualObject* vobject =
        GetObjectFromAllocation(elements->Cast<InlinedAllocation>());
    CHECK_EQ(vobject->type(), VirtualObject::kDefault);
    DCHECK(vobject->map().IsFixedArrayMap());
    ValueNode* length_node = vobject->get(offsetof(FixedArray, length_));
    if (auto length = TryGetInt32Constant(length_node)) {
      if (index >= 0 && index < length.value()) {
        return vobject->get(FixedArray::OffsetOfElementAt(index));
      } else {
        return GetRootConstant(RootIndex::kTheHoleValue);
      }
    }
  }
  if (index < 0 || index >= FixedArray::kMaxLength) {
    return GetRootConstant(RootIndex::kTheHoleValue);
  }
  return AddNewNode<LoadTaggedField>({elements},
                                     FixedArray::OffsetOfElementAt(index));
}

ValueNode* MaglevGraphBuilder::BuildLoadFixedArrayElement(ValueNode* elements,
                                                          ValueNode* index) {
  if (auto constant = TryGetInt32Constant(index)) {
    return BuildLoadFixedArrayElement(elements, constant.value());
  }
  return AddNewNode<LoadFixedArrayElement>({elements, index});
}

void MaglevGraphBuilder::BuildStoreFixedArrayElement(ValueNode* elements,
                                                     ValueNode* index,
                                                     ValueNode* value) {
  // TODO(victorgomes): Support storing element to a virtual object. If we
  // modify the elements array, we need to modify the original object to point
  // to the new elements array.
  if (CanElideWriteBarrier(elements, value)) {
    AddNewNode<StoreFixedArrayElementNoWriteBarrier>({elements, index, value});
  } else {
    AddNewNode<StoreFixedArrayElementWithWriteBarrier>(
        {elements, index, value});
  }
}

ValueNode* MaglevGraphBuilder::BuildLoadFixedDoubleArrayElement(
    ValueNode* elements, int index) {
  if (CanTrackObjectChanges(elements, TrackObjectMode::kLoad)) {
    VirtualObject* vobject =
        GetObjectFromAllocation(elements->Cast<InlinedAllocation>());
    compiler::FixedDoubleArrayRef elements_array = vobject->double_elements();
    if (index >= 0 && static_cast<uint32_t>(index) < elements_array.length()) {
      Float64 value = elements_array.GetFromImmutableFixedDoubleArray(index);
      return GetFloat64Constant(value.get_scalar());
    } else {
      return GetRootConstant(RootIndex::kTheHoleValue);
    }
  }
  if (index < 0 || index >= FixedArray::kMaxLength) {
    return GetRootConstant(RootIndex::kTheHoleValue);
  }
  return AddNewNode<LoadFixedDoubleArrayElement>(
      {elements, GetInt32Constant(index)});
}

ValueNode* MaglevGraphBuilder::BuildLoadFixedDoubleArrayElement(
    ValueNode* elements, ValueNode* index) {
  if (auto constant = TryGetInt32Constant(index)) {
    return BuildLoadFixedDoubleArrayElement(elements, constant.value());
  }
  return AddNewNode<LoadFixedDoubleArrayElement>({elements, index});
}

void MaglevGraphBuilder::BuildStoreFixedDoubleArrayElement(ValueNode* elements,
                                                           ValueNode* index,
                                                           ValueNode* value) {
  // TODO(victorgomes): Support storing double element to a virtual object.
  AddNewNode<StoreFixedDoubleArrayElement>({elements, index, value});
}

ValueNode* MaglevGraphBuilder::BuildLoadHoleyFixedDoubleArrayElement(
    ValueNode* elements, ValueNode* index, bool convert_hole) {
  if (convert_hole) {
    return AddNewNode<LoadHoleyFixedDoubleArrayElement>({elements, index});
  } else {
    return AddNewNode<LoadHoleyFixedDoubleArrayElementCheckedNotHole>(
        {elements, index});
  }
}

bool MaglevGraphBuilder::CanTreatHoleAsUndefined(
    base::Vector<const compiler::MapRef> const& receiver_maps) {
  // Check if all {receiver_maps} have one of the initial Array.prototype
  // or Object.prototype objects as their prototype (in any of the current
  // native contexts, as the global Array protector works isolate-wide).
  for (compiler::MapRef receiver_map : receiver_maps) {
    compiler::ObjectRef receiver_prototype = receiver_map.prototype(broker());
    if (!receiver_prototype.IsJSObject() ||
        !broker()->IsArrayOrObjectPrototype(receiver_prototype.AsJSObject())) {
      return false;
    }
  }

  // Check if the array prototype chain is intact.
  return broker()->dependencies()->DependOnNoElementsProtector();
}

compiler::OptionalObjectRef
MaglevGraphBuilder::TryFoldLoadDictPrototypeConstant(
    compiler::PropertyAccessInfo const& access_info) {
  DCHECK(V8_DICT_PROPERTY_CONST_TRACKING_BOOL);
  DCHECK(access_info.IsDictionaryProtoDataConstant());
  DCHECK(access_info.holder().has_value());

  compiler::OptionalObjectRef constant =
      access_info.holder()->GetOwnDictionaryProperty(
          broker(), access_info.dictionary_index(), broker()->dependencies());
  if (!constant.has_value()) return {};

  for (compiler::MapRef map : access_info.lookup_start_object_maps()) {
    DirectHandle<Map> map_handle = map.object();
    // Non-JSReceivers that passed AccessInfoFactory::ComputePropertyAccessInfo
    // must have different lookup start map.
    if (!IsJSReceiverMap(*map_handle)) {
      // Perform the implicit ToObject for primitives here.
      // Implemented according to ES6 section 7.3.2 GetV (V, P).
      Tagged<JSFunction> constructor =
          Map::GetConstructorFunction(
              *map_handle, *broker()->target_native_context().object())
              .value();
      // {constructor.initial_map()} is loaded/stored with acquire-release
      // semantics for constructors.
      map = MakeRefAssumeMemoryFence(broker(), constructor->initial_map());
      DCHECK(IsJSObjectMap(*map.object()));
    }
    broker()->dependencies()->DependOnConstantInDictionaryPrototypeChain(
        map, access_info.name(), constant.value(), PropertyKind::kData);
  }

  return constant;
}

compiler::OptionalJSObjectRef MaglevGraphBuilder::TryGetConstantDataFieldHolder(
    compiler::PropertyAccessInfo const& access_info,
    ValueNode* lookup_start_object) {
  if (!access_info.IsFastDataConstant()) return {};
  if (access_info.holder().has_value()) {
    return access_info.holder();
  }
  if (compiler::OptionalHeapObjectRef c = TryGetConstant(lookup_start_object)) {
    if (c.value().IsJSObject()) {
      return c.value().AsJSObject();
    }
  }
  return {};
}

compiler::OptionalObjectRef MaglevGraphBuilder::TryFoldLoadConstantDataField(
    compiler::JSObjectRef holder,
    compiler::PropertyAccessInfo const& access_info) {
  DCHECK(!access_info.field_representation().IsDouble());
  return holder.GetOwnFastConstantDataProperty(
      broker(), access_info.field_representation(), access_info.field_index(),
      broker()->dependencies());
}

std::optional<Float64> MaglevGraphBuilder::TryFoldLoadConstantDoubleField(
    compiler::JSObjectRef holder,
    compiler::PropertyAccessInfo const& access_info) {
  DCHECK(access_info.field_representation().IsDouble());
  return holder.GetOwnFastConstantDoubleProperty(
      broker(), access_info.field_index(), broker()->dependencies());
}

MaybeReduceResult MaglevGraphBuilder::TryBuildPropertyGetterCall(
    compiler::PropertyAccessInfo const& access_info, ValueNode* receiver,
    ValueNode* lookup_start_object) {
  compiler::ObjectRef constant = access_info.constant().value();

  if (access_info.IsDictionaryProtoAccessorConstant()) {
    // For fast mode holders we recorded dependencies in BuildPropertyLoad.
    for (const compiler::MapRef map : access_info.lookup_start_object_maps()) {
      broker()->dependencies()->DependOnConstantInDictionaryPrototypeChain(
          map, access_info.name(), constant, PropertyKind::kAccessor);
    }
  }

  // Introduce the call to the getter function.
  if (constant.IsJSFunction()) {
    ConvertReceiverMode receiver_mode =
        receiver == lookup_start_object
            ? ConvertReceiverMode::kNotNullOrUndefined
            : ConvertReceiverMode::kAny;
    CallArguments args(receiver_mode, {receiver});
    return TryReduceCallForConstant(constant.AsJSFunction(), args);
  } else {
    // Disable optimizations for super ICs using API getters, so that we get
    // the correct receiver checks.
    if (receiver != lookup_start_object) return {};
    compiler::FunctionTemplateInfoRef templ = constant.AsFunctionTemplateInfo();
    CallArguments args(ConvertReceiverMode::kNotNullOrUndefined, {receiver});

    return TryReduceCallForApiFunction(templ, {}, access_info.api_holder(),
                                       args);
  }
}

MaybeReduceResult MaglevGraphBuilder::TryBuildPropertySetterCall(
    compiler::PropertyAccessInfo const& access_info, ValueNode* receiver,
    ValueNode* lookup_start_object, ValueNode* value) {
  // Setting super properties shouldn't end up here.
  DCHECK_EQ(receiver, lookup_start_object);
  compiler::ObjectRef constant = access_info.constant().value();
  if (constant.IsJSFunction()) {
    CallArguments args(ConvertReceiverMode::kNotNullOrUndefined,
                       {receiver, value});
    RETURN_IF_ABORT(TryReduceCallForConstant(constant.AsJSFunction(), args));
  } else {
    compiler::FunctionTemplateInfoRef templ = constant.AsFunctionTemplateInfo();
    CallArguments args(ConvertReceiverMode::kNotNullOrUndefined,
                       {receiver, value});
    RETURN_IF_ABORT(
        TryReduceCallForApiFunction(templ, {}, access_info.api_holder(), args));
  }
  // Ignore the return value of the setter call.
  return ReduceResult::Done();
}

ValueNode* MaglevGraphBuilder::BuildLoadField(
    compiler::PropertyAccessInfo const& access_info,
    ValueNode* lookup_start_object, compiler::NameRef name) {
  compiler::OptionalJSObjectRef constant_holder =
      TryGetConstantDataFieldHolder(access_info, lookup_start_object);
  if (constant_holder) {
    if (access_info.field_representation().IsDouble()) {
      std::optional<Float64> constant =
          TryFoldLoadConstantDoubleField(constant_holder.value(), access_info);
      if (constant.has_value()) {
        return GetFloat64Constant(constant.value());
      }
    } else {
      compiler::OptionalObjectRef constant =
          TryFoldLoadConstantDataField(constant_holder.value(), access_info);
      if (constant.has_value()) {
        return GetConstant(constant.value());
      }
    }
  }
  // Resolve property holder.
  ValueNode* load_source;
  if (access_info.holder().has_value()) {
    load_source = GetConstant(access_info.holder().value());
  } else {
    load_source = lookup_start_object;
  }

  FieldIndex field_index = access_info.field_index();
  if (!field_index.is_inobject()) {
    // The field is in the property array, first load it from there.
    load_source =
        BuildLoadTaggedField(load_source, JSReceiver::kPropertiesOrHashOffset);
  }

  // Do the load.
  if (field_index.is_double()) {
    return AddNewNode<LoadDoubleField>({load_source}, field_index.offset());
  }
  ValueNode* value = BuildLoadTaggedField<LoadTaggedFieldForProperty>(
      load_source, field_index.offset(), name);
  // Insert stable field information if present.
  if (access_info.field_representation().IsSmi()) {
    NodeInfo* known_info = GetOrCreateInfoFor(value);
    known_info->CombineType(NodeType::kSmi);
  } else if (access_info.field_representation().IsHeapObject()) {
    NodeInfo* known_info = GetOrCreateInfoFor(value);
    if (access_info.field_map().has_value() &&
        access_info.field_map().value().is_stable()) {
      DCHECK(access_info.field_map().value().IsJSReceiverMap());
      auto map = access_info.field_map().value();
      known_info->SetPossibleMaps(PossibleMaps{map}, false,
                                  StaticTypeForMap(map, broker()), broker());
      broker()->dependencies()->DependOnStableMap(map);
    } else {
      known_info->CombineType(NodeType::kAnyHeapObject);
    }
  }
  return value;
}

ValueNode* MaglevGraphBuilder::BuildLoadFixedArrayLength(
    ValueNode* fixed_array) {
  ValueNode* length =
      BuildLoadTaggedField(fixed_array, offsetof(FixedArray, length_));
  EnsureType(length, NodeType::kSmi);
  return length;
}

ValueNode* MaglevGraphBuilder::BuildLoadJSArrayLength(ValueNode* js_array,
                                                      NodeType length_type) {
  // TODO(leszeks): JSArray.length is known to be non-constant, don't bother
  // searching the constant values.
  MaybeReduceResult known_length =
      TryReuseKnownPropertyLoad(js_array, broker()->length_string());
  if (known_length.IsDone()) {
    DCHECK(known_length.IsDoneWithValue());
    return known_length.value();
  }

  ValueNode* length = BuildLoadTaggedField<LoadTaggedFieldForProperty>(
      js_array, JSArray::kLengthOffset, broker()->length_string());
  GetOrCreateInfoFor(length)->CombineType(length_type);
  RecordKnownProperty(js_array, broker()->length_string(), length, false,
                      compiler::AccessMode::kLoad);
  return length;
}

void MaglevGraphBuilder::BuildStoreMap(ValueNode* object, compiler::MapRef map,
                                       StoreMap::Kind kind) {
  AddNewNode<StoreMap>({object}, map, kind);
  NodeType object_type = StaticTypeForMap(map, broker());
  NodeInfo* node_info = GetOrCreateInfoFor(object);
  if (map.is_stable()) {
    node_info->SetPossibleMaps(PossibleMaps{map}, false, object_type, broker());
    broker()->dependencies()->DependOnStableMap(map);
  } else {
    node_info->SetPossibleMaps(PossibleMaps{map}, true, object_type, broker());
    known_node_aspects().any_map_for_any_node_is_unstable = true;
  }
}

ValueNode* MaglevGraphBuilder::BuildExtendPropertiesBackingStore(
    compiler::MapRef map, ValueNode* receiver, ValueNode* property_array) {
  int length = map.NextFreePropertyIndex() - map.GetInObjectProperties();
  // Under normal circumstances, NextFreePropertyIndex() will always be larger
  // than GetInObjectProperties(). However, an attacker able to corrupt heap
  // memory can break this invariant, in which case we'll get confused here,
  // potentially causing a sandbox violation. This CHECK defends against that.
  SBXCHECK_GE(length, 0);
  return AddNewNode<ExtendPropertiesBackingStore>({property_array, receiver},
                                                  length);
}

MaybeReduceResult MaglevGraphBuilder::TryBuildStoreField(
    compiler::PropertyAccessInfo const& access_info, ValueNode* receiver,
    compiler::AccessMode access_mode) {
  FieldIndex field_index = access_info.field_index();
  Representation field_representation = access_info.field_representation();

  compiler::OptionalMapRef original_map;
  if (access_info.HasTransitionMap()) {
    compiler::MapRef transition = access_info.transition_map().value();
    original_map = transition.GetBackPointer(broker()).AsMap();

    if (original_map->UnusedPropertyFields() == 0) {
      DCHECK(!field_index.is_inobject());
    }
    if (!field_index.is_inobject()) {
      // If slack tracking ends after this compilation started but before it's
      // finished, then {original_map} could be out-of-sync with {transition}.
      // In particular, its UnusedPropertyFields could be non-zero, which would
      // lead us to not extend the property backing store, while the underlying
      // Map has actually zero UnusedPropertyFields. Thus, we install a
      // dependency on {orininal_map} now, so that if such a situation happens,
      // we'll throw away the code.
      broker()->dependencies()->DependOnNoSlackTrackingChange(*original_map);
    }
  } else if (access_info.IsFastDataConstant() &&
             access_mode == compiler::AccessMode::kStore) {
    return EmitUnconditionalDeopt(DeoptimizeReason::kStoreToConstant);
  }

  ValueNode* store_target;
  if (field_index.is_inobject()) {
    store_target = receiver;
  } else {
    // The field is in the property array, first load it from there.
    store_target =
        BuildLoadTaggedField(receiver, JSReceiver::kPropertiesOrHashOffset);
    if (original_map && original_map->UnusedPropertyFields() == 0) {
      store_target = BuildExtendPropertiesBackingStore(*original_map, receiver,
                                                       store_target);
    }
  }

  if (field_representation.IsDouble()) {
    ValueNode* float64_value = GetAccumulator();
    if (access_info.HasTransitionMap()) {
      // Allocate the mutable double box owned by the field.
      ValueNode* value =
          AddNewNode<Float64ToHeapNumberForField>({float64_value});
      BuildStoreTaggedField(store_target, value, field_index.offset(),
                            StoreTaggedMode::kTransitioning);
      BuildStoreMap(receiver, access_info.transition_map().value(),
                    StoreMap::Kind::kTransitioning);
    } else {
      AddNewNode<StoreDoubleField>({store_target, float64_value},
                                   field_index.offset());
    }
    return ReduceResult::Done();
  }

  ValueNode* value = GetAccumulator();
  if (field_representation.IsSmi()) {
    RETURN_IF_ABORT(GetAccumulatorSmi());
  } else {
    if (field_representation.IsHeapObject()) {
      // Emit a map check for the field type, if needed, otherwise just a
      // HeapObject check.
      if (access_info.field_map().has_value()) {
        RETURN_IF_ABORT(BuildCheckMaps(
            value, base::VectorOf({access_info.field_map().value()})));
      } else {
        BuildCheckHeapObject(value);
      }
    }
  }

  StoreTaggedMode store_mode = access_info.HasTransitionMap()
                                   ? StoreTaggedMode::kTransitioning
                                   : StoreTaggedMode::kDefault;
  if (field_representation.IsSmi()) {
    BuildStoreTaggedFieldNoWriteBarrier(store_target, value,
                                        field_index.offset(), store_mode);
  } else {
    DCHECK(field_representation.IsHeapObject() ||
           field_representation.IsTagged());
    BuildStoreTaggedField(store_target, value, field_index.offset(),
                          store_mode);
  }
  if (access_info.HasTransitionMap()) {
    BuildStoreMap(receiver, access_info.transition_map().value(),
                  StoreMap::Kind::kTransitioning);
  }

  return ReduceResult::Done();
}

namespace {
bool AccessInfoGuaranteedConst(
    compiler::PropertyAccessInfo const& access_info) {
  if (!access_info.IsFastDataConstant() && !access_info.IsStringLength()) {
    return false;
  }

  // Even if we have a constant load, if the map is not stable, we cannot
  // guarantee that the load is preserved across side-effecting calls.
  // TODO(v8:7700): It might be possible to track it as const if we know
  // that we're still on the main transition tree; and if we add a
  // dependency on the stable end-maps of the entire tree.
  for (auto& map : access_info.lookup_start_object_maps()) {
    if (!map.is_stable()) {
      return false;
    }
  }
  return true;
}
}  // namespace

MaybeReduceResult MaglevGraphBuilder::TryBuildPropertyLoad(
    ValueNode* receiver, ValueNode* lookup_start_object, compiler::NameRef name,
    compiler::PropertyAccessInfo const& access_info) {
  if (access_info.holder().has_value() && !access_info.HasDictionaryHolder()) {
    broker()->dependencies()->DependOnStablePrototypeChains(
        access_info.lookup_start_object_maps(), kStartAtPrototype,
        access_info.holder().value());
  }

  switch (access_info.kind()) {
    case compiler::PropertyAccessInfo::kInvalid:
      UNREACHABLE();
    case compiler::PropertyAccessInfo::kNotFound:
      return GetRootConstant(RootIndex::kUndefinedValue);
    case compiler::PropertyAccessInfo::kDataField:
    case compiler::PropertyAccessInfo::kFastDataConstant: {
      ValueNode* result =
          BuildLoadField(access_info, lookup_start_object, name);
      RecordKnownProperty(lookup_start_object, name, result,
                          AccessInfoGuaranteedConst(access_info),
                          compiler::AccessMode::kLoad);
      return result;
    }
    case compiler::PropertyAccessInfo::kDictionaryProtoDataConstant: {
      compiler::OptionalObjectRef constant =
          TryFoldLoadDictPrototypeConstant(access_info);
      if (!constant.has_value()) return {};
      return GetConstant(constant.value());
    }
    case compiler::PropertyAccessInfo::kFastAccessorConstant:
    case compiler::PropertyAccessInfo::kDictionaryProtoAccessorConstant:
      return TryBuildPropertyGetterCall(access_info, receiver,
                                        lookup_start_object);
    case compiler::PropertyAccessInfo::kModuleExport: {
      ValueNode* cell = GetConstant(access_info.constant().value().AsCell());
      return BuildLoadTaggedField<LoadTaggedFieldForProperty>(
          cell, Cell::kValueOffset, name);
    }
    case compiler::PropertyAccessInfo::kStringLength: {
      DCHECK_EQ(receiver, lookup_start_object);
      ValueNode* result = BuildLoadStringLength(receiver);
      RecordKnownProperty(lookup_start_object, name, result,
                          AccessInfoGuaranteedConst(access_info),
                          compiler::AccessMode::kLoad);
      return result;
    }
    case compiler::PropertyAccessInfo::kStringWrapperLength: {
      // TODO(dmercadier): update KnownNodeInfo.
      ValueNode* string = BuildLoadTaggedField(
          lookup_start_object, JSPrimitiveWrapper::kValueOffset);
      return AddNewNode<StringLength>({string});
    }
    case compiler::PropertyAccessInfo::kTypedArrayLength: {
      DCHECK_EQ(receiver, lookup_start_object);
      CHECK(!IsRabGsabTypedArrayElementsKind(access_info.elements_kind()));
      return BuildLoadTypedArrayLength(receiver, access_info.elements_kind());
    }
  }
}

MaybeReduceResult MaglevGraphBuilder::TryBuildPropertyStore(
    ValueNode* receiver, ValueNode* lookup_start_object, compiler::NameRef name,
    compiler::PropertyAccessInfo const& access_info,
    compiler::AccessMode access_mode) {
  if (access_info.holder().has_value()) {
    broker()->dependencies()->DependOnStablePrototypeChains(
        access_info.lookup_start_object_maps(), kStartAtPrototype,
        access_info.holder().value());
  }

  switch (access_info.kind()) {
    case compiler::PropertyAccessInfo::kFastAccessorConstant: {
      return TryBuildPropertySetterCall(access_info, receiver,
                                        lookup_start_object, GetAccumulator());
    }
    case compiler::PropertyAccessInfo::kDataField:
    case compiler::PropertyAccessInfo::kFastDataConstant: {
      MaybeReduceResult res =
          TryBuildStoreField(access_info, receiver, access_mode);
      if (res.IsDone()) {
        RecordKnownProperty(
            receiver, name, current_interpreter_frame_.accumulator(),
            AccessInfoGuaranteedConst(access_info), access_mode);
        return res;
      }
      return {};
    }
    case compiler::PropertyAccessInfo::kInvalid:
    case compiler::PropertyAccessInfo::kNotFound:
    case compiler::PropertyAccessInfo::kDictionaryProtoDataConstant:
    case compiler::PropertyAccessInfo::kDictionaryProtoAccessorConstant:
    case compiler::PropertyAccessInfo::kModuleExport:
    case compiler::PropertyAccessInfo::kStringLength:
    case compiler::PropertyAccessInfo::kStringWrapperLength:
    case compiler::PropertyAccessInfo::kTypedArrayLength:
      UNREACHABLE();
  }
}

MaybeReduceResult MaglevGraphBuilder::TryBuildPropertyAccess(
    ValueNode* receiver, ValueNode* lookup_start_object, compiler::NameRef name,
    compiler::PropertyAccessInfo const& access_info,
    compiler::AccessMode access_mode) {
  switch (access_mode) {
    case compiler::AccessMode::kLoad:
      return TryBuildPropertyLoad(receiver, lookup_start_object, name,
                                  access_info);
    case compiler::AccessMode::kStore:
    case compiler::AccessMode::kStoreInLiteral:
    case compiler::AccessMode::kDefine:
      DCHECK_EQ(receiver, lookup_start_object);
      return TryBuildPropertyStore(receiver, lookup_start_object, name,
                                   access_info, access_mode);
    case compiler::AccessMode::kHas:
      // TODO(victorgomes): BuildPropertyTest.
      return {};
  }
}

template <typename GenericAccessFunc>
MaybeReduceResult MaglevGraphBuilder::TryBuildNamedAccess(
    ValueNode* receiver, ValueNode* lookup_start_object,
    compiler::NamedAccessFeedback const& feedback,
    compiler::FeedbackSource const& feedback_source,
    compiler::AccessMode access_mode,
    GenericAccessFunc&& build_generic_access) {
  compiler::ZoneRefSet<Map> inferred_maps;

  bool has_deprecated_map_without_migration_target = false;
  if (compiler::OptionalHeapObjectRef c = TryGetConstant(lookup_start_object)) {
    compiler::MapRef constant_map = c.value().map(broker());
    if (c.value().IsJSFunction() &&
        feedback.name().equals(broker()->prototype_string())) {
      compiler::JSFunctionRef function = c.value().AsJSFunction();
      if (!constant_map.has_prototype_slot() ||
          !function.has_instance_prototype(broker()) ||
          function.PrototypeRequiresRuntimeLookup(broker()) ||
          access_mode != compiler::AccessMode::kLoad) {
        return {};
      }
      compiler::HeapObjectRef prototype =
          broker()->dependencies()->DependOnPrototypeProperty(function);
      return GetConstant(prototype);
    }
    inferred_maps = compiler::ZoneRefSet<Map>(constant_map);
  } else if (feedback.maps().empty()) {
    // The IC is megamorphic.

    // We can't do megamorphic loads for lookups where the lookup start isn't
    // the receiver (e.g. load from super).
    if (receiver != lookup_start_object) return {};

    // Use known possible maps if we have any.
    NodeInfo* object_info =
        known_node_aspects().TryGetInfoFor(lookup_start_object);
    if (object_info && object_info->possible_maps_are_known()) {
      inferred_maps = object_info->possible_maps();
    } else {
      // If we have no known maps, make the access megamorphic.
      switch (access_mode) {
        case compiler::AccessMode::kLoad:
          return BuildCallBuiltin<Builtin::kLoadIC_Megamorphic>(
              {GetTaggedValue(receiver), GetConstant(feedback.name())},
              feedback_source);
        case compiler::AccessMode::kStore:
          return BuildCallBuiltin<Builtin::kStoreIC_Megamorphic>(
              {GetTaggedValue(receiver), GetConstant(feedback.name()),
               GetTaggedValue(GetAccumulator())},
              feedback_source);
        case compiler::AccessMode::kDefine:
          return {};
        case compiler::AccessMode::kHas:
        case compiler::AccessMode::kStoreInLiteral:
          UNREACHABLE();
      }
    }
  } else {
    // TODO(leszeks): This is doing duplicate work with BuildCheckMaps,
    // consider passing the merger into there.
    KnownMapsMerger merger(broker(), zone(), base::VectorOf(feedback.maps()));
    merger.IntersectWithKnownNodeAspects(lookup_start_object,
                                         known_node_aspects());
    inferred_maps = merger.intersect_set();
    has_deprecated_map_without_migration_target =
        feedback.has_deprecated_map_without_migration_target();
  }

  if (inferred_maps.is_empty()) {
    return EmitUnconditionalDeopt(DeoptimizeReason::kWrongMap);
  }

  ZoneVector<compiler::PropertyAccessInfo> access_infos(zone());
  ZoneVector<compiler::PropertyAccessInfo> access_infos_for_feedback(zone());

  for (compiler::MapRef map : inferred_maps) {
    if (map.is_deprecated()) continue;

    // TODO(v8:12547): Support writing to objects in shared space, which
    // need a write barrier that calls Object::Share to ensure the RHS is
    // shared.
    if (InstanceTypeChecker::IsAlwaysSharedSpaceJSObject(map.instance_type()) &&
        access_mode == compiler::AccessMode::kStore) {
      return {};
    }

    compiler::PropertyAccessInfo access_info =
        broker()->GetPropertyAccessInfo(map, feedback.name(), access_mode);
    access_infos_for_feedback.push_back(access_info);
  }

  compiler::AccessInfoFactory access_info_factory(broker(), zone());
  if (!access_info_factory.FinalizePropertyAccessInfos(
          access_infos_for_feedback, access_mode, &access_infos)) {
    return {};
  }

  // Check for monomorphic case.
  if (access_infos.size() == 1) {
    compiler::PropertyAccessInfo const& access_info = access_infos.front();
    base::Vector<const compiler::MapRef> maps =
        base::VectorOf(access_info.lookup_start_object_maps());
    if (HasOnlyStringMaps(maps)) {
      // Check for string maps before checking if we need to do an access
      // check. Primitive strings always get the prototype from the native
      // context they're operated on, so they don't need the access check.
      BuildCheckString(lookup_start_object);
    } else if (HasOnlyNumberMaps(maps)) {
      BuildCheckNumber(lookup_start_object);
    } else {
      RETURN_IF_ABORT(
          BuildCheckMaps(lookup_start_object, maps, {},
                         has_deprecated_map_without_migration_target));
    }

    // Generate the actual property
    return TryBuildPropertyAccess(receiver, lookup_start_object,
                                  feedback.name(), access_info, access_mode);
  } else {
    // TODO(victorgomes): Unify control flow logic with
    // TryBuildPolymorphicElementAccess.
    return TryBuildPolymorphicPropertyAccess(
        receiver, lookup_start_object, feedback, access_mode, access_infos,
        build_generic_access);
  }
}

ValueNode* MaglevGraphBuilder::GetInt32ElementIndex(ValueNode* object) {
  RecordUseReprHintIfPhi(object, UseRepresentation::kInt32);

  switch (object->properties().value_representation()) {
    case ValueRepresentation::kIntPtr:
      return AddNewNode<CheckedIntPtrToInt32>({object});
    case ValueRepresentation::kTagged:
      NodeType old_type;
      if (SmiConstant* constant = object->TryCast<SmiConstant>()) {
        return GetInt32Constant(constant->value().value());
      } else if (CheckType(object, NodeType::kSmi, &old_type)) {
        auto& alternative = GetOrCreateInfoFor(object)->alternative();
        return alternative.get_or_set_int32(
            [&]() { return BuildSmiUntag(object); });
      } else {
        // TODO(leszeks): Cache this knowledge/converted value somehow on
        // the node info.
        return AddNewNode<CheckedObjectToIndex>({object},
                                                GetCheckType(old_type));
      }
    case ValueRepresentation::kInt32:
      // Already good.
      return object;
    case ValueRepresentation::kUint32:
    case ValueRepresentation::kFloat64:
    case ValueRepresentation::kHoleyFloat64:
      return GetInt32(object);
  }
}

// TODO(victorgomes): Consider caching the values and adding an
// uint32_alternative in node_info.
ReduceResult MaglevGraphBuilder::GetUint32ElementIndex(ValueNode* object) {
  // Don't record a Uint32 Phi use here, since the tagged path goes via
  // GetInt32ElementIndex, making this an Int32 Phi use.

  switch (object->properties().value_representation()) {
    case ValueRepresentation::kIntPtr:
      return AddNewNode<CheckedIntPtrToUint32>({object});
    case ValueRepresentation::kTagged:
      // TODO(victorgomes): Consider creating a CheckedObjectToUnsignedIndex.
      if (SmiConstant* constant = object->TryCast<SmiConstant>()) {
        int32_t value = constant->value().value();
        if (value < 0) {
          return EmitUnconditionalDeopt(DeoptimizeReason::kNotUint32);
        }
        return GetUint32Constant(value);
      }
      return AddNewNode<CheckedInt32ToUint32>({GetInt32ElementIndex(object)});
    case ValueRepresentation::kInt32:
      if (Int32Constant* constant = object->TryCast<Int32Constant>()) {
        int32_t value = constant->value();
        if (value < 0) {
          return EmitUnconditionalDeopt(DeoptimizeReason::kNotUint32);
        }
        return GetUint32Constant(value);
      }
      return AddNewNode<CheckedInt32ToUint32>({object});
    case ValueRepresentation::kUint32:
      return object;
    case ValueRepresentation::kFloat64:
      if (Float64Constant* constant = object->TryCast<Float64Constant>()) {
        double value = constant->value().get_scalar();
        uint32_t uint32_value;
        if (!DoubleToUint32IfEqualToSelf(value, &uint32_value)) {
          return EmitUnconditionalDeopt(DeoptimizeReason::kNotUint32);
        }
        if (Smi::IsValid(uint32_value)) {
          return GetUint32Constant(uint32_value);
        }
      }
      [[fallthrough]];
    case ValueRepresentation::kHoleyFloat64: {
      // CheckedTruncateFloat64ToUint32 will gracefully deopt on holes.
      return AddNewNode<CheckedTruncateFloat64ToUint32>({object});
    }
  }
}

MaybeReduceResult MaglevGraphBuilder::TryBuildElementAccessOnString(
    ValueNode* object, ValueNode* index_object,
    compiler::KeyedAccessMode const& keyed_mode) {
  // Strings are immutable and `in` cannot be used on strings
  if (keyed_mode.access_mode() != compiler::AccessMode::kLoad) {
    return {};
  }

  // Ensure that {object} is actually a String.
  BuildCheckString(object);

  ValueNode* length = BuildLoadStringLength(object);
  ValueNode* index = GetInt32ElementIndex(index_object);
  auto emit_load = [&] { return AddNewNode<StringAt>({object, index}); };

  if (LoadModeHandlesOOB(keyed_mode.load_mode()) &&
      broker()->dependencies()->DependOnNoElementsProtector()) {
    ValueNode* positive_index;
    GET_VALUE_OR_ABORT(positive_index, GetUint32ElementIndex(index));
    ValueNode* uint32_length = AddNewNode<UnsafeInt32ToUint32>({length});
    return Select(
        [&](auto& builder) {
          return BuildBranchIfUint32Compare(builder, Operation::kLessThan,
                                            positive_index, uint32_length);
        },
        emit_load, [&] { return GetRootConstant(RootIndex::kUndefinedValue); });
  } else {
    RETURN_IF_ABORT(TryBuildCheckInt32Condition(
        index, length, AssertCondition::kUnsignedLessThan,
        DeoptimizeReason::kOutOfBounds));
    return emit_load();
  }
}

namespace {
MaybeReduceResult TryFindLoadedProperty(
    const KnownNodeAspects::LoadedPropertyMap& loaded_properties,
    ValueNode* lookup_start_object,
    KnownNodeAspects::LoadedPropertyMapKey name) {
  auto props_for_name = loaded_properties.find(name);
  if (props_for_name == loaded_properties.end()) return {};

  auto it = props_for_name->second.find(lookup_start_object);
  if (it == props_for_name->second.end()) return {};

  return it->second;
}

bool CheckConditionIn32(int32_t lhs, int32_t rhs, AssertCondition condition) {
  switch (condition) {
    case AssertCondition::kEqual:
      return lhs == rhs;
    case AssertCondition::kNotEqual:
      return lhs != rhs;
    case AssertCondition::kLessThan:
      return lhs < rhs;
    case AssertCondition::kLessThanEqual:
      return lhs <= rhs;
    case AssertCondition::kGreaterThan:
      return lhs > rhs;
    case AssertCondition::kGreaterThanEqual:
      return lhs >= rhs;
    case AssertCondition::kUnsignedLessThan:
      return static_cast<uint32_t>(lhs) < static_cast<uint32_t>(rhs);
    case AssertCondition::kUnsignedLessThanEqual:
      return static_cast<uint32_t>(lhs) <= static_cast<uint32_t>(rhs);
    case AssertCondition::kUnsignedGreaterThan:
      return static_cast<uint32_t>(lhs) > static_cast<uint32_t>(rhs);
    case AssertCondition::kUnsignedGreaterThanEqual:
      return static_cast<uint32_t>(lhs) >= static_cast<uint32_t>(rhs);
  }
}

bool CompareInt32(int32_t lhs, int32_t rhs, Operation operation) {
  switch (operation) {
    case Operation::kEqual:
    case Operation::kStrictEqual:
      return lhs == rhs;
    case Operation::kLessThan:
      return lhs < rhs;
    case Operation::kLessThanOrEqual:
      return lhs <= rhs;
    case Operation::kGreaterThan:
      return lhs > rhs;
    case Operation::kGreaterThanOrEqual:
      return lhs >= rhs;
    default:
      UNREACHABLE();
  }
}

bool CompareUint32(uint32_t lhs, uint32_t rhs, Operation operation) {
  switch (operation) {
    case Operation::kEqual:
    case Operation::kStrictEqual:
      return lhs == rhs;
    case Operation::kLessThan:
      return lhs < rhs;
    case Operation::kLessThanOrEqual:
      return lhs <= rhs;
    case Operation::kGreaterThan:
      return lhs > rhs;
    case Operation::kGreaterThanOrEqual:
      return lhs >= rhs;
    default:
      UNREACHABLE();
  }
}

}  // namespace

MaybeReduceResult MaglevGraphBuilder::TryBuildCheckInt32Condition(
    ValueNode* lhs, ValueNode* rhs, AssertCondition condition,
    DeoptimizeReason reason) {
  auto lhs_const = TryGetInt32Constant(lhs);
  if (lhs_const) {
    auto rhs_const = TryGetInt32Constant(rhs);
    if (rhs_const) {
      if (CheckConditionIn32(lhs_const.value(), rhs_const.value(), condition)) {
        return ReduceResult::Done();
      }
      return EmitUnconditionalDeopt(reason);
    }
  }
  AddNewNode<CheckInt32Condition>({lhs, rhs}, condition, reason);
  return ReduceResult::Done();
}

ValueNode* MaglevGraphBuilder::BuildLoadElements(ValueNode* object) {
  MaybeReduceResult known_elements =
      TryFindLoadedProperty(known_node_aspects().loaded_properties, object,
                            KnownNodeAspects::LoadedPropertyMapKey::Elements());
  if (known_elements.IsDone()) {
    DCHECK(known_elements.IsDoneWithValue());
    if (v8_flags.trace_maglev_graph_building) {
      std::cout << "  * Reusing non-constant [Elements] "
                << PrintNodeLabel(graph_labeller(), known_elements.value())
                << ": " << PrintNode(graph_labeller(), known_elements.value())
                << std::endl;
    }
    return known_elements.value();
  }

  DCHECK_EQ(JSObject::kElementsOffset, JSArray::kElementsOffset);
  ValueNode* elements = BuildLoadTaggedField(object, JSObject::kElementsOffset);
  RecordKnownProperty(object,
                      KnownNodeAspects::LoadedPropertyMapKey::Elements(),
                      elements, false, compiler::AccessMode::kLoad);
  return elements;
}

ReduceResult MaglevGraphBuilder::BuildLoadTypedArrayLength(
    ValueNode* object, ElementsKind elements_kind) {
  DCHECK(IsTypedArrayOrRabGsabTypedArrayElementsKind(elements_kind));
  bool is_variable_length = IsRabGsabTypedArrayElementsKind(elements_kind);

  if (!is_variable_length) {
    // Note: We can't use broker()->length_string() here, because it could
    // conflict with redefinitions of the TypedArray length property.
    RETURN_IF_DONE(TryFindLoadedProperty(
        known_node_aspects().loaded_constant_properties, object,
        KnownNodeAspects::LoadedPropertyMapKey::TypedArrayLength()));
  }

  ValueNode* result = AddNewNode<LoadTypedArrayLength>({object}, elements_kind);
  if (!is_variable_length) {
    RecordKnownProperty(
        object, KnownNodeAspects::LoadedPropertyMapKey::TypedArrayLength(),
        result, true, compiler::AccessMode::kLoad);
  }
  return result;
}

ValueNode* MaglevGraphBuilder::BuildLoadTypedArrayElement(
    ValueNode* object, ValueNode* index, ElementsKind elements_kind) {
#define BUILD_AND_RETURN_LOAD_TYPED_ARRAY(Type)                     \
  return AddNewNode<Load##Type##TypedArrayElement>({object, index}, \
                                                   elements_kind);

  switch (elements_kind) {
    case INT8_ELEMENTS:
    case INT16_ELEMENTS:
    case INT32_ELEMENTS:
      BUILD_AND_RETURN_LOAD_TYPED_ARRAY(SignedInt);
    case UINT8_CLAMPED_ELEMENTS:
    case UINT8_ELEMENTS:
    case UINT16_ELEMENTS:
    case UINT32_ELEMENTS:
      BUILD_AND_RETURN_LOAD_TYPED_ARRAY(UnsignedInt);
    case FLOAT32_ELEMENTS:
    case FLOAT64_ELEMENTS:
      BUILD_AND_RETURN_LOAD_TYPED_ARRAY(Double);
    default:
      UNREACHABLE();
  }
#undef BUILD_AND_RETURN_LOAD_TYPED_ARRAY
}

void MaglevGraphBuilder::BuildStoreTypedArrayElement(
    ValueNode* object, ValueNode* index, ElementsKind elements_kind) {
#define BUILD_STORE_TYPED_ARRAY(Type, value)                           \
  AddNewNode<Store##Type##TypedArrayElement>({object, index, (value)}, \
                                             elements_kind);

  // TODO(leszeks): These operations have a deopt loop when the ToNumber
  // conversion sees a type other than number or oddball. Turbofan has the same
  // deopt loop, but ideally we'd avoid it.
  switch (elements_kind) {
    case UINT8_CLAMPED_ELEMENTS: {
      BUILD_STORE_TYPED_ARRAY(Int, GetAccumulatorUint8ClampedForToNumber())
      break;
    }
    case INT8_ELEMENTS:
    case INT16_ELEMENTS:
    case INT32_ELEMENTS:
    case UINT8_ELEMENTS:
    case UINT16_ELEMENTS:
    case UINT32_ELEMENTS:
      BUILD_STORE_TYPED_ARRAY(
          Int, GetAccumulatorTruncatedInt32ForToNumber(
                   NodeType::kNumberOrOddball,
                   TaggedToFloat64ConversionType::kNumberOrOddball))
      break;
    case FLOAT32_ELEMENTS:
    case FLOAT64_ELEMENTS:
      BUILD_STORE_TYPED_ARRAY(
          Double, GetAccumulatorHoleyFloat64ForToNumber(
                      NodeType::kNumberOrOddball,
                      TaggedToFloat64ConversionType::kNumberOrOddball))
      break;
    default:
      UNREACHABLE();
  }
#undef BUILD_STORE_TYPED_ARRAY
}

MaybeReduceResult MaglevGraphBuilder::TryBuildElementAccessOnTypedArray(
    ValueNode* object, ValueNode* index_object,
    const compiler::ElementAccessInfo& access_info,
    compiler::KeyedAccessMode const& keyed_mode) {
  DCHECK(HasOnlyJSTypedArrayMaps(
      base::VectorOf(access_info.lookup_start_object_maps())));
  ElementsKind elements_kind = access_info.elements_kind();
  if (elements_kind == FLOAT16_ELEMENTS ||
      elements_kind == BIGUINT64_ELEMENTS ||
      elements_kind == BIGINT64_ELEMENTS) {
    return {};
  }
  if (keyed_mode.access_mode() == compiler::AccessMode::kLoad &&
      LoadModeHandlesOOB(keyed_mode.load_mode())) {
    // TODO(victorgomes): Handle OOB mode.
    return {};
  }
  if (keyed_mode.access_mode() == compiler::AccessMode::kStore &&
      StoreModeIgnoresTypeArrayOOB(keyed_mode.store_mode())) {
    // TODO(victorgomes): Handle OOB mode.
    return {};
  }
  if (keyed_mode.access_mode() == compiler::AccessMode::kStore &&
      elements_kind == UINT8_CLAMPED_ELEMENTS &&
      !IsSupported(CpuOperation::kFloat64Round)) {
    // TODO(victorgomes): Technically we still support if value (in the
    // accumulator) is of type int32. It would be nice to have a roll back
    // mechanism instead, so that we do not need to check this early.
    return {};
  }
  if (!broker()->dependencies()->DependOnArrayBufferDetachingProtector()) {
    // TODO(leszeks): Eliminate this check.
    AddNewNode<CheckTypedArrayNotDetached>({object});
  }
  ValueNode* index;
  ValueNode* length;
  GET_VALUE_OR_ABORT(index, GetUint32ElementIndex(index_object));
  GET_VALUE_OR_ABORT(length, BuildLoadTypedArrayLength(object, elements_kind));
  AddNewNode<CheckTypedArrayBounds>({index, length});
  switch (keyed_mode.access_mode()) {
    case compiler::AccessMode::kLoad:
      DCHECK(!LoadModeHandlesOOB(keyed_mode.load_mode()));
      return BuildLoadTypedArrayElement(object, index, elements_kind);
    case compiler::AccessMode::kStore:
      DCHECK(StoreModeIsInBounds(keyed_mode.store_mode()));
      BuildStoreTypedArrayElement(object, index, elements_kind);
      return ReduceResult::Done();
    case compiler::AccessMode::kHas:
      // TODO(victorgomes): Implement has element access.
      return {};
    case compiler::AccessMode::kStoreInLiteral:
    case compiler::AccessMode::kDefine:
      UNREACHABLE();
  }
}

MaybeReduceResult MaglevGraphBuilder::TryBuildElementLoadOnJSArrayOrJSObject(
    ValueNode* object, ValueNode* index_object,
    base::Vector<const compiler::MapRef> maps, ElementsKind elements_kind,
    KeyedAccessLoadMode load_mode) {
  DCHECK(IsFastElementsKind(elements_kind));
  bool is_jsarray = HasOnlyJSArrayMaps(maps);
  DCHECK(is_jsarray || HasOnlyJSObjectMaps(maps));

  ValueNode* elements_array = BuildLoadElements(object);
  ValueNode* index = GetInt32ElementIndex(index_object);
  ValueNode* length = is_jsarray ? GetInt32(BuildLoadJSArrayLength(object))
                                 : BuildLoadFixedArrayLength(elements_array);

  auto emit_load = [&]() -> MaybeReduceResult {
    ValueNode* result;
    if (elements_kind == HOLEY_DOUBLE_ELEMENTS) {
      result = BuildLoadHoleyFixedDoubleArrayElement(
          elements_array, index,
          CanTreatHoleAsUndefined(maps) && LoadModeHandlesHoles(load_mode));
    } else if (elements_kind == PACKED_DOUBLE_ELEMENTS) {
      result = BuildLoadFixedDoubleArrayElement(elements_array, index);
    } else {
      DCHECK(!IsDoubleElementsKind(elements_kind));
      result = BuildLoadFixedArrayElement(elements_array, index);
      if (IsHoleyElementsKind(elements_kind)) {
        if (CanTreatHoleAsUndefined(maps) && LoadModeHandlesHoles(load_mode)) {
          result = BuildConvertHoleToUndefined(result);
        } else {
          RETURN_IF_ABORT(BuildCheckNotHole(result));
          if (IsSmiElementsKind(elements_kind)) {
            EnsureType(result, NodeType::kSmi);
          }
        }
      } else if (IsSmiElementsKind(elements_kind)) {
        EnsureType(result, NodeType::kSmi);
      }
    }
    return result;
  };

  if (CanTreatHoleAsUndefined(maps) && LoadModeHandlesOOB(load_mode)) {
    ValueNode* positive_index;
    GET_VALUE_OR_ABORT(positive_index, GetUint32ElementIndex(index));
    ValueNode* uint32_length = AddNewNode<UnsafeInt32ToUint32>({length});
    return SelectReduction(
        [&](auto& builder) {
          return BuildBranchIfUint32Compare(builder, Operation::kLessThan,
                                            positive_index, uint32_length);
        },
        emit_load, [&] { return GetRootConstant(RootIndex::kUndefinedValue); });
  } else {
    RETURN_IF_ABORT(TryBuildCheckInt32Condition(
        index, length, AssertCondition::kUnsignedLessThan,
        DeoptimizeReason::kOutOfBounds));
    return emit_load();
  }
}

ReduceResult MaglevGraphBuilder::ConvertForStoring(ValueNode* value,
                                                   ElementsKind kind) {
  if (IsDoubleElementsKind(kind)) {
    // Make sure we do not store signalling NaNs into double arrays.
    // TODO(leszeks): Consider making this a bit on StoreFixedDoubleArrayElement
    // rather than a separate node.
    return GetSilencedNaN(GetFloat64(value));
  }
  if (IsSmiElementsKind(kind)) return GetSmiValue(value);
  return value;
}

MaybeReduceResult MaglevGraphBuilder::TryBuildElementStoreOnJSArrayOrJSObject(
    ValueNode* object, ValueNode* index_object, ValueNode* value,
    base::Vector<const compiler::MapRef> maps, ElementsKind elements_kind,
    const compiler::KeyedAccessMode& keyed_mode) {
  DCHECK(IsFastElementsKind(elements_kind));

  const bool is_jsarray = HasOnlyJSArrayMaps(maps);
  DCHECK(is_jsarray || HasOnlyJSObjectMaps(maps));

  // Get the elements array.
  ValueNode* elements_array = BuildLoadElements(object);
  GET_VALUE_OR_ABORT(value, ConvertForStoring(value, elements_kind));
  ValueNode* index;

  // TODO(verwaest): Loop peeling will turn the first iteration index of spread
  // literals into smi constants as well, breaking the assumption that we'll
  // have preallocated the space if we see known indices. Turn off this
  // optimization if loop peeling is on.
  if (keyed_mode.access_mode() == compiler::AccessMode::kStoreInLiteral &&
      index_object->Is<SmiConstant>() && is_jsarray && !any_peeled_loop_) {
    index = GetInt32ElementIndex(index_object);
  } else {
    // Check boundaries.
    ValueNode* elements_array_length = nullptr;
    ValueNode* length;
    if (is_jsarray) {
      length = GetInt32(BuildLoadJSArrayLength(object));
    } else {
      length = elements_array_length =
          BuildLoadFixedArrayLength(elements_array);
    }
    index = GetInt32ElementIndex(index_object);
    if (keyed_mode.store_mode() == KeyedAccessStoreMode::kGrowAndHandleCOW) {
      if (elements_array_length == nullptr) {
        elements_array_length = BuildLoadFixedArrayLength(elements_array);
      }

      // Validate the {index} depending on holeyness:
      //
      // For HOLEY_*_ELEMENTS the {index} must not exceed the {elements}
      // backing store capacity plus the maximum allowed gap, as otherwise
      // the (potential) backing store growth would normalize and thus
      // the elements kind of the {receiver} would change to slow mode.
      //
      // For JSArray PACKED_*_ELEMENTS the {index} must be within the range
      // [0,length+1[ to be valid. In case {index} equals {length},
      // the {receiver} will be extended, but kept packed.
      //
      // Non-JSArray PACKED_*_ELEMENTS always grow by adding holes because they
      // lack the magical length property, which requires a map transition.
      // So we can assume that this did not happen if we did not see this map.
      ValueNode* limit =
          IsHoleyElementsKind(elements_kind)
              ? AddNewNode<Int32AddWithOverflow>(
                    {elements_array_length,
                     GetInt32Constant(JSObject::kMaxGap)})
          : is_jsarray
              ? AddNewNode<Int32AddWithOverflow>({length, GetInt32Constant(1)})
              : elements_array_length;
      RETURN_IF_ABORT(TryBuildCheckInt32Condition(
          index, limit, AssertCondition::kUnsignedLessThan,
          DeoptimizeReason::kOutOfBounds));

      // Grow backing store if necessary and handle COW.
      elements_array = AddNewNode<MaybeGrowFastElements>(
          {elements_array, object, index, elements_array_length},
          elements_kind);

      // If we didn't grow {elements}, it might still be COW, in which case we
      // copy it now.
      if (IsSmiOrObjectElementsKind(elements_kind)) {
        DCHECK_EQ(keyed_mode.store_mode(),
                  KeyedAccessStoreMode::kGrowAndHandleCOW);
        elements_array =
            AddNewNode<EnsureWritableFastElements>({elements_array, object});
      }

      // Update length if necessary.
      if (is_jsarray) {
        ValueNode* new_length =
            AddNewNode<UpdateJSArrayLength>({length, object, index});
        RecordKnownProperty(object, broker()->length_string(), new_length,
                            false, compiler::AccessMode::kStore);
      }
    } else {
      RETURN_IF_ABORT(TryBuildCheckInt32Condition(
          index, length, AssertCondition::kUnsignedLessThan,
          DeoptimizeReason::kOutOfBounds));

      // Handle COW if needed.
      if (IsSmiOrObjectElementsKind(elements_kind)) {
        if (keyed_mode.store_mode() == KeyedAccessStoreMode::kHandleCOW) {
          elements_array =
              AddNewNode<EnsureWritableFastElements>({elements_array, object});
        } else {
          // Ensure that this is not a COW FixedArray.
          RETURN_IF_ABORT(BuildCheckMaps(
              elements_array, base::VectorOf({broker()->fixed_array_map()})));
        }
      }
    }
  }

  // Do the store.
  if (IsDoubleElementsKind(elements_kind)) {
    BuildStoreFixedDoubleArrayElement(elements_array, index, value);
  } else {
    BuildStoreFixedArrayElement(elements_array, index, value);
  }

  return ReduceResult::Done();
}

MaybeReduceResult MaglevGraphBuilder::TryBuildElementAccessOnJSArrayOrJSObject(
    ValueNode* object, ValueNode* index_object,
    const compiler::ElementAccessInfo& access_info,
    compiler::KeyedAccessMode const& keyed_mode) {
  if (!IsFastElementsKind(access_info.elements_kind())) {
    return {};
  }
  switch (keyed_mode.access_mode()) {
    case compiler::AccessMode::kLoad:
      return TryBuildElementLoadOnJSArrayOrJSObject(
          object, index_object,
          base::VectorOf(access_info.lookup_start_object_maps()),
          access_info.elements_kind(), keyed_mode.load_mode());
    case compiler::AccessMode::kStoreInLiteral:
    case compiler::AccessMode::kStore: {
      base::Vector<const compiler::MapRef> maps =
          base::VectorOf(access_info.lookup_start_object_maps());
      ElementsKind elements_kind = access_info.elements_kind();
      return TryBuildElementStoreOnJSArrayOrJSObject(object, index_object,
                                                     GetAccumulator(), maps,
                                                     elements_kind, keyed_mode);
    }
    default:
      // TODO(victorgomes): Implement more access types.
      return {};
  }
}

template <typename GenericAccessFunc>
MaybeReduceResult MaglevGraphBuilder::TryBuildElementAccess(
    ValueNode* object, ValueNode* index_object,
    compiler::ElementAccessFeedback const& feedback,
    compiler::FeedbackSource const& feedback_source,
    GenericAccessFunc&& build_generic_access) {
  const compiler::KeyedAccessMode& keyed_mode = feedback.keyed_mode();
  // Check for the megamorphic case.
  if (feedback.transition_groups().empty()) {
    if (keyed_mode.access_mode() == compiler::AccessMode::kLoad) {
      return BuildCallBuiltin<Builtin::kKeyedLoadIC_Megamorphic>(
          {GetTaggedValue(object), GetTaggedValue(index_object)},
          feedback_source);
    } else if (keyed_mode.access_mode() == compiler::AccessMode::kStore) {
      return BuildCallBuiltin<Builtin::kKeyedStoreIC_Megamorphic>(
          {GetTaggedValue(object), GetTaggedValue(index_object),
           GetTaggedValue(GetAccumulator())},
          feedback_source);
    }
    return {};
  }

  NodeInfo* object_info = known_node_aspects().TryGetInfoFor(object);
  compiler::ElementAccessFeedback refined_feedback =
      object_info && object_info->possible_maps_are_known()
          ? feedback.Refine(broker(), object_info->possible_maps())
          : feedback;

  if (refined_feedback.HasOnlyStringMaps(broker())) {
    return TryBuildElementAccessOnString(object, index_object, keyed_mode);
  }

  compiler::AccessInfoFactory access_info_factory(broker(), zone());
  ZoneVector<compiler::ElementAccessInfo> access_infos(zone());
  if (!access_info_factory.ComputeElementAccessInfos(refined_feedback,
                                                     &access_infos) ||
      access_infos.empty()) {
    return {};
  }

  // TODO(leszeks): This is copied without changes from TurboFan's native
  // context specialization. We should figure out a way to share this code.
  //
  // For holey stores or growing stores, we need to check that the prototype
  // chain contains no setters for elements, and we need to guard those checks
  // via code dependencies on the relevant prototype maps.
  if (keyed_mode.access_mode() == compiler::AccessMode::kStore) {
    // TODO(v8:7700): We could have a fast path here, that checks for the
    // common case of Array or Object prototype only and therefore avoids
    // the zone allocation of this vector.
    ZoneVector<compiler::MapRef> prototype_maps(zone());
    for (compiler::ElementAccessInfo const& access_info : access_infos) {
      for (compiler::MapRef receiver_map :
           access_info.lookup_start_object_maps()) {
        // If the {receiver_map} has a prototype and its elements backing
        // store is either holey, or we have a potentially growing store,
        // then we need to check that all prototypes have stable maps with
        // with no element accessors and no throwing behavior for elements (and
        // we need to guard against changes to that below).
        if ((IsHoleyOrDictionaryElementsKind(receiver_map.elements_kind()) ||
             StoreModeCanGrow(refined_feedback.keyed_mode().store_mode())) &&
            !receiver_map.PrototypesElementsDoNotHaveAccessorsOrThrow(
                broker(), &prototype_maps)) {
          return {};
        }

        // TODO(v8:12547): Support writing to objects in shared space, which
        // need a write barrier that calls Object::Share to ensure the RHS is
        // shared.
        if (InstanceTypeChecker::IsAlwaysSharedSpaceJSObject(
                receiver_map.instance_type())) {
          return {};
        }
      }
    }
    for (compiler::MapRef prototype_map : prototype_maps) {
      broker()->dependencies()->DependOnStableMap(prototype_map);
    }
  }

  // Check for monomorphic case.
  if (access_infos.size() == 1) {
    compiler::ElementAccessInfo const& access_info = access_infos.front();
    // TODO(victorgomes): Support RAB/GSAB backed typed arrays.
    if (IsRabGsabTypedArrayElementsKind(access_info.elements_kind())) {
      return {};
    }

    if (!access_info.transition_sources().empty()) {
      compiler::MapRef transition_target =
          access_info.lookup_start_object_maps().front();
      const ZoneVector<compiler::MapRef>& transition_sources =
          access_info.transition_sources();

      // There are no transitions in heap number maps. If `object` is a SMI, we
      // would anyway fail the transition and deopt later.
      DCHECK_NE(transition_target.instance_type(),
                InstanceType::HEAP_NUMBER_TYPE);
#ifdef DEBUG
      for (auto& transition_source : transition_sources) {
        DCHECK_NE(transition_source.instance_type(),
                  InstanceType::HEAP_NUMBER_TYPE);
      }
#endif  // DEBUG

      BuildCheckHeapObject(object);
      ValueNode* object_map =
          BuildLoadTaggedField(object, HeapObject::kMapOffset);

      RETURN_IF_ABORT(BuildTransitionElementsKindOrCheckMap(
          object, object_map, transition_sources, transition_target));
    } else {
      RETURN_IF_ABORT(BuildCheckMaps(
          object, base::VectorOf(access_info.lookup_start_object_maps())));
    }
    if (IsTypedArrayElementsKind(access_info.elements_kind())) {
      return TryBuildElementAccessOnTypedArray(object, index_object,
                                               access_info, keyed_mode);
    }
    return TryBuildElementAccessOnJSArrayOrJSObject(object, index_object,
                                                    access_info, keyed_mode);
  } else {
    return TryBuildPolymorphicElementAccess(object, index_object, keyed_mode,
                                            access_infos, build_generic_access);
  }
}

template <typename GenericAccessFunc>
MaybeReduceResult MaglevGraphBuilder::TryBuildPolymorphicElementAccess(
    ValueNode* object, ValueNode* index_object,
    const compiler::KeyedAccessMode& keyed_mode,
    const ZoneVector<compiler::ElementAccessInfo>& access_infos,
    GenericAccessFunc&& build_generic_access) {
  if (keyed_mode.access_mode() == compiler::AccessMode::kLoad &&
      LoadModeHandlesOOB(keyed_mode.load_mode())) {
    // TODO(victorgomes): Handle OOB mode.
    return {};
  }

  const bool is_any_store = compiler::IsAnyStore(keyed_mode.access_mode());
  const int access_info_count = static_cast<int>(access_infos.size());
  // Stores don't return a value, so we don't need a variable for the result.
  MaglevSubGraphBuilder sub_graph(this, is_any_store ? 0 : 1);
  std::optional<MaglevSubGraphBuilder::Variable> ret_val;
  std::optional<MaglevSubGraphBuilder::Label> done;
  std::optional<MaglevSubGraphBuilder::Label> generic_access;

  BuildCheckHeapObject(object);
  ValueNode* object_map = BuildLoadTaggedField(object, HeapObject::kMapOffset);

  // TODO(pthier): We could do better here than just emitting code for each map,
  // as many different maps can produce the exact samce code (e.g. TypedArray
  // access for Uint16/Uint32/Int16/Int32/...).
  for (int i = 0; i < access_info_count; i++) {
    compiler::ElementAccessInfo const& access_info = access_infos[i];
    std::optional<MaglevSubGraphBuilder::Label> check_next_map;
    const bool handle_transitions = !access_info.transition_sources().empty();
    MaybeReduceResult map_check_result;
    if (i == access_info_count - 1) {
      if (handle_transitions) {
        compiler::MapRef transition_target =
            access_info.lookup_start_object_maps().front();
        map_check_result = BuildTransitionElementsKindOrCheckMap(
            object, object_map, access_info.transition_sources(),
            transition_target);
      } else {
        map_check_result = BuildCheckMaps(
            object, base::VectorOf(access_info.lookup_start_object_maps()),
            object_map);
      }
    } else {
      if (handle_transitions) {
        compiler::MapRef transition_target =
            access_info.lookup_start_object_maps().front();
        map_check_result = BuildTransitionElementsKindAndCompareMaps(
            object, object_map, access_info.transition_sources(),
            transition_target, &sub_graph, check_next_map);
      } else {
        map_check_result = BuildCompareMaps(
            object, object_map,
            base::VectorOf(access_info.lookup_start_object_maps()), &sub_graph,
            check_next_map);
      }
    }
    if (map_check_result.IsDoneWithAbort()) {
      // We know from known possible maps that this branch is not reachable,
      // so don't emit any code for it.
      continue;
    }
    MaybeReduceResult result;
    // TODO(victorgomes): Support RAB/GSAB backed typed arrays.
    if (IsRabGsabTypedArrayElementsKind(access_info.elements_kind())) {
      result = MaybeReduceResult::Fail();
    } else if (IsTypedArrayElementsKind(access_info.elements_kind())) {
      result = TryBuildElementAccessOnTypedArray(object, index_object,
                                                 access_info, keyed_mode);
    } else {
      result = TryBuildElementAccessOnJSArrayOrJSObject(
          object, index_object, access_info, keyed_mode);
    }

    switch (result.kind()) {
      case MaybeReduceResult::kDoneWithValue:
      case MaybeReduceResult::kDoneWithoutValue:
        DCHECK_EQ(result.HasValue(), !is_any_store);
        if (!done.has_value()) {
          // We initialize the label {done} lazily on the first possible path.
          // If no possible path exists, it is guaranteed that BuildCheckMaps
          // emitted an unconditional deopt and we return DoneWithAbort at the
          // end. We need one extra predecessor to jump from the generic case.
          const int possible_predecessors = access_info_count - i + 1;
          if (is_any_store) {
            done.emplace(&sub_graph, possible_predecessors);
          } else {
            ret_val.emplace(0);
            done.emplace(
                &sub_graph, possible_predecessors,
                std::initializer_list<MaglevSubGraphBuilder::Variable*>{
                    &*ret_val});
          }
        }
        if (!is_any_store) {
          sub_graph.set(*ret_val, result.value());
        }
        sub_graph.Goto(&*done);
        break;
      case MaybeReduceResult::kFail:
        if (!generic_access.has_value()) {
          // Conservatively assume that all remaining branches can go into the
          // generic path, as we have to initialize the predecessors upfront.
          // TODO(pthier): Find a better way to do that.
          generic_access.emplace(&sub_graph, access_info_count - i);
        }
        sub_graph.Goto(&*generic_access);
        break;
      case MaybeReduceResult::kDoneWithAbort:
        break;
    }
    if (check_next_map.has_value()) {
      sub_graph.Bind(&*check_next_map);
    }
  }
  if (generic_access.has_value() &&
      !sub_graph.TrimPredecessorsAndBind(&*generic_access).IsDoneWithAbort()) {
    MaybeReduceResult generic_result = build_generic_access();
    DCHECK(generic_result.IsDone());
    DCHECK_EQ(generic_result.IsDoneWithValue(), !is_any_store);
    if (!done.has_value()) {
      return is_any_store ? ReduceResult::Done() : generic_result.value();
    }
    if (!is_any_store) {
      sub_graph.set(*ret_val, generic_result.value());
    }
    sub_graph.Goto(&*done);
  }
  if (done.has_value()) {
    RETURN_IF_ABORT(sub_graph.TrimPredecessorsAndBind(&*done));
    return is_any_store ? ReduceResult::Done() : sub_graph.get(*ret_val);
  } else {
    return ReduceResult::DoneWithAbort();
  }
}

template <typename GenericAccessFunc>
MaybeReduceResult MaglevGraphBuilder::TryBuildPolymorphicPropertyAccess(
    ValueNode* receiver, ValueNode* lookup_start_object,
    compiler::NamedAccessFeedback const& feedback,
    compiler::AccessMode access_mode,
    const ZoneVector<compiler::PropertyAccessInfo>& access_infos,
    GenericAccessFunc&& build_generic_access) {
  const bool is_any_store = compiler::IsAnyStore(access_mode);
  const int access_info_count = static_cast<int>(access_infos.size());
  int number_map_index = -1;

  bool needs_migration = false;
  bool has_deprecated_map_without_migration_target =
      feedback.has_deprecated_map_without_migration_target();
  for (int i = 0; i < access_info_count; i++) {
    compiler::PropertyAccessInfo const& access_info = access_infos[i];
    DCHECK(!access_info.IsInvalid());
    for (compiler::MapRef map : access_info.lookup_start_object_maps()) {
      if (map.is_migration_target()) {
        needs_migration = true;
      }
      if (map.IsHeapNumberMap()) {
        GetOrCreateInfoFor(lookup_start_object);
        base::SmallVector<compiler::MapRef, 1> known_maps = {map};
        KnownMapsMerger merger(broker(), zone(), base::VectorOf(known_maps));
        merger.IntersectWithKnownNodeAspects(lookup_start_object,
                                             known_node_aspects());
        if (!merger.intersect_set().is_empty()) {
          DCHECK_EQ(number_map_index, -1);
          number_map_index = i;
        }
      }
    }
  }

  // Stores don't return a value, so we don't need a variable for the result.
  MaglevSubGraphBuilder sub_graph(this, is_any_store ? 0 : 1);
  std::optional<MaglevSubGraphBuilder::Variable> ret_val;
  std::optional<MaglevSubGraphBuilder::Label> done;
  std::optional<MaglevSubGraphBuilder::Label> is_number;
  std::optional<MaglevSubGraphBuilder::Label> generic_access;

  if (number_map_index >= 0) {
    is_number.emplace(&sub_graph, 2);
    sub_graph.GotoIfTrue<BranchIfSmi>(&*is_number, {lookup_start_object});
  } else {
    BuildCheckHeapObject(lookup_start_object);
  }
  ValueNode* lookup_start_object_map =
      BuildLoadTaggedField(lookup_start_object, HeapObject::kMapOffset);

  if (needs_migration &&
      !v8_flags.maglev_skip_migration_check_for_polymorphic_access) {
    // TODO(marja, v8:7700): Try migrating only if all comparisons failed.
    // TODO(marja, v8:7700): Investigate making polymoprhic map comparison (with
    // migration) a control node (like switch).
    lookup_start_object_map = AddNewNode<MigrateMapIfNeeded>(
        {lookup_start_object_map, lookup_start_object});
  }

  for (int i = 0; i < access_info_count; i++) {
    compiler::PropertyAccessInfo const& access_info = access_infos[i];
    std::optional<MaglevSubGraphBuilder::Label> check_next_map;
    MaybeReduceResult map_check_result;
    const auto& maps = access_info.lookup_start_object_maps();
    if (i == access_info_count - 1) {
      map_check_result =
          BuildCheckMaps(lookup_start_object, base::VectorOf(maps), {},
                         has_deprecated_map_without_migration_target);
    } else {
      map_check_result =
          BuildCompareMaps(lookup_start_object, lookup_start_object_map,
                           base::VectorOf(maps), &sub_graph, check_next_map);
    }
    if (map_check_result.IsDoneWithAbort()) {
      // We know from known possible maps that this branch is not reachable,
      // so don't emit any code for it.
      continue;
    }
    if (i == number_map_index) {
      DCHECK(is_number.has_value());
      sub_graph.Goto(&*is_number);
      sub_graph.Bind(&*is_number);
    }

    MaybeReduceResult result;
    if (is_any_store) {
      result = TryBuildPropertyStore(receiver, lookup_start_object,
                                     feedback.name(), access_info, access_mode);
    } else {
      result = TryBuildPropertyLoad(receiver, lookup_start_object,
                                    feedback.name(), access_info);
    }

    switch (result.kind()) {
      case MaybeReduceResult::kDoneWithValue:
      case MaybeReduceResult::kDoneWithoutValue:
        DCHECK_EQ(result.HasValue(), !is_any_store);
        if (!done.has_value()) {
          // We initialize the label {done} lazily on the first possible path.
          // If no possible path exists, it is guaranteed that BuildCheckMaps
          // emitted an unconditional deopt and we return DoneWithAbort at the
          // end. We need one extra predecessor to jump from the generic case.
          const int possible_predecessors = access_info_count - i + 1;
          if (is_any_store) {
            done.emplace(&sub_graph, possible_predecessors);
          } else {
            ret_val.emplace(0);
            done.emplace(
                &sub_graph, possible_predecessors,
                std::initializer_list<MaglevSubGraphBuilder::Variable*>{
                    &*ret_val});
          }
        }

        if (!is_any_store) {
          sub_graph.set(*ret_val, result.value());
        }
        sub_graph.Goto(&*done);
        break;
      case MaybeReduceResult::kDoneWithAbort:
        break;
      case MaybeReduceResult::kFail:
        if (!generic_access.has_value()) {
          // Conservatively assume that all remaining branches can go into the
          // generic path, as we have to initialize the predecessors upfront.
          // TODO(pthier): Find a better way to do that.
          generic_access.emplace(&sub_graph, access_info_count - i);
        }
        sub_graph.Goto(&*generic_access);
        break;
      default:
        UNREACHABLE();
    }

    if (check_next_map.has_value()) {
      sub_graph.Bind(&*check_next_map);
    }
  }

  if (generic_access.has_value() &&
      !sub_graph.TrimPredecessorsAndBind(&*generic_access).IsDoneWithAbort()) {
    MaybeReduceResult generic_result = build_generic_access();
    DCHECK(generic_result.IsDone());
    DCHECK_EQ(generic_result.IsDoneWithValue(), !is_any_store);
    if (!done.has_value()) {
      return is_any_store ? ReduceResult::Done() : generic_result.value();
    }
    if (!is_any_store) {
      sub_graph.set(*ret_val, generic_result.value());
    }
    sub_graph.Goto(&*done);
  }

  if (done.has_value()) {
    RETURN_IF_ABORT(sub_graph.TrimPredecessorsAndBind(&*done));
    return is_any_store ? ReduceResult::Done() : sub_graph.get(*ret_val);
  } else {
    return ReduceResult::DoneWithAbort();
  }
}

void MaglevGraphBuilder::RecordKnownProperty(
    ValueNode* lookup_start_object, KnownNodeAspects::LoadedPropertyMapKey key,
    ValueNode* value, bool is_const, compiler::AccessMode access_mode) {
  DCHECK(!value->properties().is_conversion());
  KnownNodeAspects::LoadedPropertyMap& loaded_properties =
      is_const ? known_node_aspects().loaded_constant_properties
               : known_node_aspects().loaded_properties;
  // Try to get loaded_properties[key] if it already exists, otherwise
  // construct loaded_properties[key] = ZoneMap{zone()}.
  auto& props_for_key =
      loaded_properties.try_emplace(key, zone()).first->second;

  if (!is_const && IsAnyStore(access_mode)) {
    if (is_loop_effect_tracking()) {
      loop_effects_->keys_cleared.insert(key);
    }
    // We don't do any aliasing analysis, so stores clobber all other cached
    // loads of a property with that key. We only need to do this for
    // non-constant properties, since constant properties are known not to
    // change and therefore can't be clobbered.
    // TODO(leszeks): Do some light aliasing analysis here, e.g. checking
    // whether there's an intersection of known maps.
    if (v8_flags.trace_maglev_graph_building) {
      std::cout << "  * Removing all non-constant cached ";
      switch (key.type()) {
        case KnownNodeAspects::LoadedPropertyMapKey::kName:
          std::cout << "properties with name " << *key.name().object();
          break;
        case KnownNodeAspects::LoadedPropertyMapKey::kElements:
          std::cout << "Elements";
          break;
        case KnownNodeAspects::LoadedPropertyMapKey::kTypedArrayLength:
          std::cout << "TypedArray length";
          break;
        case KnownNodeAspects::LoadedPropertyMapKey::kStringLength:
          std::cout << "String length";
          break;
      }
      std::cout << std::endl;
    }
    props_for_key.clear();
  }

  if (v8_flags.trace_maglev_graph_building) {
    std::cout << "  * Recording " << (is_const ? "constant" : "non-constant")
              << " known property "
              << PrintNodeLabel(graph_labeller(), lookup_start_object) << ": "
              << PrintNode(graph_labeller(), lookup_start_object) << " [";
    switch (key.type()) {
      case KnownNodeAspects::LoadedPropertyMapKey::kName:
        std::cout << *key.name().object();
        break;
      case KnownNodeAspects::LoadedPropertyMapKey::kElements:
        std::cout << "Elements";
        break;
      case KnownNodeAspects::LoadedPropertyMapKey::kTypedArrayLength:
        std::cout << "TypedArray length";
        break;
      case KnownNodeAspects::LoadedPropertyMapKey::kStringLength:
        std::cout << "String length";
        break;
    }
    std::cout << "] = " << PrintNodeLabel(graph_labeller(), value) << ": "
              << PrintNode(graph_labeller(), value) << std::endl;
  }

  if (IsAnyStore(access_mode) && !is_const && is_loop_effect_tracking()) {
    auto updated = props_for_key.emplace(lookup_start_object, value);
    if (updated.second) {
      loop_effects_->objects_written.insert(lookup_start_object);
    } else if (updated.first->second != value) {
      updated.first->second = value;
      loop_effects_->objects_written.insert(lookup_start_object);
    }
  } else {
    props_for_key[lookup_start_object] = value;
  }
}

MaybeReduceResult MaglevGraphBuilder::TryReuseKnownPropertyLoad(
    ValueNode* lookup_start_object, compiler::NameRef name) {
  if (MaybeReduceResult result = TryFindLoadedProperty(
          known_node_aspects().loaded_properties, lookup_start_object, name);
      result.IsDone()) {
    if (v8_flags.trace_maglev_graph_building && result.IsDoneWithValue()) {
      std::cout << "  * Reusing non-constant loaded property "
                << PrintNodeLabel(graph_labeller(), result.value()) << ": "
                << PrintNode(graph_labeller(), result.value()) << std::endl;
    }
    return result;
  }
  if (MaybeReduceResult result =
          TryFindLoadedProperty(known_node_aspects().loaded_constant_properties,
                                lookup_start_object, name);
      result.IsDone()) {
    if (v8_flags.trace_maglev_graph_building && result.IsDoneWithValue()) {
      std::cout << "  * Reusing constant loaded property "
                << PrintNodeLabel(graph_labeller(), result.value()) << ": "
                << PrintNode(graph_labeller(), result.value()) << std::endl;
    }
    return result;
  }
  return {};
}

ValueNode* MaglevGraphBuilder::BuildLoadStringLength(ValueNode* string) {
  if (MaybeReduceResult result = TryFindLoadedProperty(
          known_node_aspects().loaded_constant_properties, string,
          KnownNodeAspects::LoadedPropertyMapKey::StringLength());
      result.IsDone()) {
    if (v8_flags.trace_maglev_graph_building && result.IsDoneWithValue()) {
      std::cout << "  * Reusing constant [String length]"
                << PrintNodeLabel(graph_labeller(), result.value()) << ": "
                << PrintNode(graph_labeller(), result.value()) << std::endl;
    }
    return result.value();
  }
  ValueNode* result = AddNewNode<StringLength>({string});
  RecordKnownProperty(string,
                      KnownNodeAspects::LoadedPropertyMapKey::StringLength(),
                      result, true, compiler::AccessMode::kLoad);
  return result;
}

template <typename GenericAccessFunc>
MaybeReduceResult MaglevGraphBuilder::TryBuildLoadNamedProperty(
    ValueNode* receiver, ValueNode* lookup_start_object, compiler::NameRef name,
    compiler::FeedbackSource& feedback_source,
    GenericAccessFunc&& build_generic_access) {
  const compiler::ProcessedFeedback& processed_feedback =
      broker()->GetFeedbackForPropertyAccess(feedback_source,
                                             compiler::AccessMode::kLoad, name);
  switch (processed_feedback.kind()) {
    case compiler::ProcessedFeedback::kInsufficient:
      return EmitUnconditionalDeopt(
          DeoptimizeReason::kInsufficientTypeFeedbackForGenericNamedAccess);
    case compiler::ProcessedFeedback::kNamedAccess: {
      RETURN_IF_DONE(TryReuseKnownPropertyLoad(lookup_start_object, name));
      return TryBuildNamedAccess(
          receiver, lookup_start_object, processed_feedback.AsNamedAccess(),
          feedback_source, compiler::AccessMode::kLoad, build_generic_access);
    }
    default:
      return {};
  }
}

MaybeReduceResult MaglevGraphBuilder::TryBuildLoadNamedProperty(
    ValueNode* receiver, compiler::NameRef name,
    compiler::FeedbackSource& feedback_source) {
  auto build_generic_access = [this, &receiver, &name, &feedback_source]() {
    ValueNode* context = GetContext();
    return AddNewNode<LoadNamedGeneric>({context, receiver}, name,
                                        feedback_source);
  };
  return TryBuildLoadNamedProperty(receiver, receiver, name, feedback_source,
                                   build_generic_access);
}

ReduceResult MaglevGraphBuilder::VisitGetNamedProperty() {
  // GetNamedProperty <object> <name_index> <slot>
  ValueNode* object = LoadRegister(0);
  compiler::NameRef name = GetRefOperand<Name>(1);
  FeedbackSlot slot = GetSlotOperand(2);
  compiler::FeedbackSource feedback_source{feedback(), slot};
  PROCESS_AND_RETURN_IF_DONE(
      TryBuildLoadNamedProperty(object, name, feedback_source), SetAccumulator);
  // Create a generic load in the fallthrough.
  ValueNode* context = GetContext();
  SetAccumulator(
      AddNewNode<LoadNamedGeneric>({context, object}, name, feedback_source));
  return ReduceResult::Done();
}

ValueNode* MaglevGraphBuilder::GetConstant(compiler::ObjectRef ref) {
  if (ref.IsSmi()) return GetSmiConstant(ref.AsSmi());
  compiler::HeapObjectRef constant = ref.AsHeapObject();

  if (IsThinString(*constant.object())) {
    constant = MakeRefAssumeMemoryFence(
        broker(), Cast<ThinString>(*constant.object())->actual());
  }

  auto root_index = broker()->FindRootIndex(constant);
  if (root_index.has_value()) {
    return GetRootConstant(*root_index);
  }

  auto it = graph_->constants().find(constant);
  if (it == graph_->constants().end()) {
    Constant* node = CreateNewConstantNode<Constant>(0, constant);
    graph_->constants().emplace(constant, node);
    return node;
  }
  return it->second;
}

ValueNode* MaglevGraphBuilder::GetTrustedConstant(compiler::HeapObjectRef ref,
                                                  IndirectPointerTag tag) {
#ifdef V8_ENABLE_SANDBOX
  auto it = graph_->trusted_constants().find(ref);
  if (it == graph_->trusted_constants().end()) {
    TrustedConstant* node = CreateNewConstantNode<TrustedConstant>(0, ref, tag);
    graph_->trusted_constants().emplace(ref, node);
    return node;
  }
  SBXCHECK_EQ(it->second->tag(), tag);
  return it->second;
#else
  return GetConstant(ref);
#endif
}

ReduceResult MaglevGraphBuilder::VisitGetNamedPropertyFromSuper() {
  // GetNamedPropertyFromSuper <receiver> <name_index> <slot>
  ValueNode* receiver = LoadRegister(0);
  ValueNode* home_object = GetAccumulator();
  compiler::NameRef name = GetRefOperand<Name>(1);
  FeedbackSlot slot = GetSlotOperand(2);
  compiler::FeedbackSource feedback_source{feedback(), slot};
  // {home_object} is guaranteed to be a HeapObject.
  ValueNode* home_object_map =
      BuildLoadTaggedField(home_object, HeapObject::kMapOffset);
  ValueNode* lookup_start_object =
      BuildLoadTaggedField(home_object_map, Map::kPrototypeOffset);

  auto build_generic_access = [this, &receiver, &lookup_start_object, &name,
                               &feedback_source]() {
    ValueNode* context = GetContext();
    return AddNewNode<LoadNamedFromSuperGeneric>(
        {context, receiver, lookup_start_object}, name, feedback_source);
  };

  PROCESS_AND_RETURN_IF_DONE(
      TryBuildLoadNamedProperty(receiver, lookup_start_object, name,
                                feedback_source, build_generic_access),
      SetAccumulator);
  // Create a generic load.
  SetAccumulator(build_generic_access());
  return ReduceResult::Done();
}

bool MaglevGraphBuilder::TryBuildGetKeyedPropertyWithEnumeratedKey(
    ValueNode* object, const compiler::FeedbackSource& feedback_source,
    const compiler::ProcessedFeedback& processed_feedback) {
  if (current_for_in_state.index != nullptr &&
      current_for_in_state.enum_cache_indices != nullptr &&
      current_for_in_state.key == current_interpreter_frame_.accumulator()) {
    bool speculating_receiver_map_matches = false;
    if (current_for_in_state.receiver != object) {
      // When the feedback is uninitialized, it is either a keyed load which
      // always hits the enum cache, or a keyed load that had never been
      // reached. In either case, we can check the map of the receiver and use
      // the enum cache if the map match the {cache_type}.
      if (processed_feedback.kind() !=
          compiler::ProcessedFeedback::kInsufficient) {
        return false;
      }
      BuildCheckHeapObject(object);
      speculating_receiver_map_matches = true;
    }

    if (current_for_in_state.receiver_needs_map_check ||
        speculating_receiver_map_matches) {
      auto* receiver_map = BuildLoadTaggedField(object, HeapObject::kMapOffset);
      AddNewNode<CheckDynamicValue>(
          {receiver_map, current_for_in_state.cache_type});
      if (current_for_in_state.receiver == object) {
        current_for_in_state.receiver_needs_map_check = false;
      }
    }
    // TODO(leszeks): Cache the field index per iteration.
    auto* field_index = BuildLoadFixedArrayElement(
        current_for_in_state.enum_cache_indices, current_for_in_state.index);
    SetAccumulator(
        AddNewNode<LoadTaggedFieldByFieldIndex>({object, field_index}));
    return true;
  }
  return false;
}

ReduceResult MaglevGraphBuilder::BuildGetKeyedProperty(
    ValueNode* object, const compiler::FeedbackSource& feedback_source,
    const compiler::ProcessedFeedback& processed_feedback) {
  if (TryBuildGetKeyedPropertyWithEnumeratedKey(object, feedback_source,
                                                processed_feedback)) {
    return ReduceResult::Done();
  }

  auto build_generic_access = [this, object, &feedback_source]() {
    ValueNode* context = GetContext();
    ValueNode* key = GetAccumulator();
    return AddNewNode<GetKeyedGeneric>({context, object, key}, feedback_source);
  };

  switch (processed_feedback.kind()) {
    case compiler::ProcessedFeedback::kInsufficient:
      return EmitUnconditionalDeopt(
          DeoptimizeReason::kInsufficientTypeFeedbackForGenericKeyedAccess);

    case compiler::ProcessedFeedback::kElementAccess: {
      // Get the accumulator without conversion. TryBuildElementAccess
      // will try to pick the best representation.
      ValueNode* index = current_interpreter_frame_.accumulator();
      MaybeReduceResult result = TryBuildElementAccess(
          object, index, processed_feedback.AsElementAccess(), feedback_source,
          build_generic_access);
      PROCESS_AND_RETURN_IF_DONE(result, SetAccumulator);
      break;
    }

    case compiler::ProcessedFeedback::kNamedAccess: {
      ValueNode* key = GetAccumulator();
      compiler::NameRef name = processed_feedback.AsNamedAccess().name();
      RETURN_IF_ABORT(BuildCheckValue(key, name));

      MaybeReduceResult result = TryReuseKnownPropertyLoad(object, name);
      PROCESS_AND_RETURN_IF_DONE(result, SetAccumulator);

      result = TryBuildNamedAccess(
          object, object, processed_feedback.AsNamedAccess(), feedback_source,
          compiler::AccessMode::kLoad, build_generic_access);
      PROCESS_AND_RETURN_IF_DONE(result, SetAccumulator);
      break;
    }

    default:
      break;
  }

  // Create a generic load in the fallthrough.
  SetAccumulator(build_generic_access());
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitGetKeyedProperty() {
  // GetKeyedProperty <object> <slot>
  ValueNode* object = LoadRegister(0);
  // TODO(leszeks): We don't need to tag the key if it's an Int32 and a simple
  // monomorphic element load.
  FeedbackSlot slot = GetSlotOperand(1);
  compiler::FeedbackSource feedback_source{feedback(), slot};

  const compiler::ProcessedFeedback* processed_feedback =
      &broker()->GetFeedbackForPropertyAccess(
          feedback_source, compiler::AccessMode::kLoad, std::nullopt);
  if (processed_feedback->kind() ==
          compiler::ProcessedFeedback::kElementAccess &&
      processed_feedback->AsElementAccess().transition_groups().empty()) {
    if (auto constant = TryGetConstant(GetAccumulator());
        constant.has_value() && constant->IsName()) {
      compiler::NameRef name = constant->AsName();
      if (name.IsUniqueName() && !name.object()->IsArrayIndex()) {
        processed_feedback =
            &processed_feedback->AsElementAccess().Refine(broker(), name);
      }
    }
  }

  return BuildGetKeyedProperty(object, feedback_source, *processed_feedback);
}

ReduceResult MaglevGraphBuilder::VisitGetEnumeratedKeyedProperty() {
  // GetEnumeratedKeyedProperty <object> <enum_index> <cache_type> <slot>
  ValueNode* object = LoadRegister(0);
  FeedbackSlot slot = GetSlotOperand(3);
  compiler::FeedbackSource feedback_source{feedback(), slot};

  const compiler::ProcessedFeedback& processed_feedback =
      broker()->GetFeedbackForPropertyAccess(
          feedback_source, compiler::AccessMode::kLoad, std::nullopt);

  return BuildGetKeyedProperty(object, feedback_source, processed_feedback);
}

ReduceResult MaglevGraphBuilder::VisitLdaModuleVariable() {
  // LdaModuleVariable <cell_index> <depth>
  int cell_index = iterator_.GetImmediateOperand(0);
  size_t depth = iterator_.GetUnsignedImmediateOperand(1);
  ValueNode* context = GetContextAtDepth(GetContext(), depth);

  ValueNode* module = LoadAndCacheContextSlot(
      context, Context::EXTENSION_INDEX, kImmutable, ContextKind::kDefault);
  ValueNode* exports_or_imports;
  if (cell_index > 0) {
    exports_or_imports =
        BuildLoadTaggedField(module, SourceTextModule::kRegularExportsOffset);
    // The actual array index is (cell_index - 1).
    cell_index -= 1;
  } else {
    exports_or_imports =
        BuildLoadTaggedField(module, SourceTextModule::kRegularImportsOffset);
    // The actual array index is (-cell_index - 1).
    cell_index = -cell_index - 1;
  }
  ValueNode* cell = BuildLoadFixedArrayElement(exports_or_imports, cell_index);
  SetAccumulator(BuildLoadTaggedField(cell, Cell::kValueOffset));
  return ReduceResult::Done();
}

ValueNode* MaglevGraphBuilder::GetContextAtDepth(ValueNode* context,
                                                 size_t depth) {
  MinimizeContextChainDepth(&context, &depth);

  if (compilation_unit_->info()->specialize_to_function_context()) {
    compiler::OptionalContextRef maybe_ref =
        FunctionContextSpecialization::TryToRef(compilation_unit_, context,
                                                &depth);
    if (maybe_ref.has_value()) {
      context = GetConstant(maybe_ref.value());
    }
  }

  for (size_t i = 0; i < depth; i++) {
    context = LoadAndCacheContextSlot(context, Context::PREVIOUS_INDEX,
                                      kImmutable, ContextKind::kDefault);
  }
  return context;
}

ReduceResult MaglevGraphBuilder::VisitStaModuleVariable() {
  // StaModuleVariable <cell_index> <depth>
  int cell_index = iterator_.GetImmediateOperand(0);
  if (V8_UNLIKELY(cell_index < 0)) {
    // TODO(verwaest): Make this fail as well.
    return BuildCallRuntime(Runtime::kAbort,
                            {GetSmiConstant(static_cast<int>(
                                AbortReason::kUnsupportedModuleOperation))});
  }

  size_t depth = iterator_.GetUnsignedImmediateOperand(1);
  ValueNode* context = GetContextAtDepth(GetContext(), depth);

  ValueNode* module = LoadAndCacheContextSlot(
      context, Context::EXTENSION_INDEX, kImmutable, ContextKind::kDefault);
  ValueNode* exports =
      BuildLoadTaggedField(module, SourceTextModule::kRegularExportsOffset);
  // The actual array index is (cell_index - 1).
  cell_index -= 1;
  ValueNode* cell = BuildLoadFixedArrayElement(exports, cell_index);
  BuildStoreTaggedField(cell, GetAccumulator(), Cell::kValueOffset,
                        StoreTaggedMode::kDefault);
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::BuildLoadGlobal(
    compiler::NameRef name, compiler::FeedbackSource& feedback_source,
    TypeofMode typeof_mode) {
  const compiler::ProcessedFeedback& access_feedback =
      broker()->GetFeedbackForGlobalAccess(feedback_source);

  if (access_feedback.IsInsufficient()) {
    return EmitUnconditionalDeopt(
        DeoptimizeReason::kInsufficientTypeFeedbackForGenericGlobalAccess);
  }

  const compiler::GlobalAccessFeedback& global_access_feedback =
      access_feedback.AsGlobalAccess();
  PROCESS_AND_RETURN_IF_DONE(TryBuildGlobalLoad(global_access_feedback),
                             SetAccumulator);

  ValueNode* context = GetContext();
  SetAccumulator(
      AddNewNode<LoadGlobal>({context}, name, feedback_source, typeof_mode));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitSetNamedProperty() {
  // SetNamedProperty <object> <name_index> <slot>
  ValueNode* object = LoadRegister(0);
  compiler::NameRef name = GetRefOperand<Name>(1);
  FeedbackSlot slot = GetSlotOperand(2);
  compiler::FeedbackSource feedback_source{feedback(), slot};

  const compiler::ProcessedFeedback& processed_feedback =
      broker()->GetFeedbackForPropertyAccess(
          feedback_source, compiler::AccessMode::kStore, name);

  auto build_generic_access = [this, object, &name, &feedback_source]() {
    ValueNode* context = GetContext();
    ValueNode* value = GetAccumulator();
    AddNewNode<SetNamedGeneric>({context, object, value}, name,
                                feedback_source);
    return ReduceResult::Done();
  };

  switch (processed_feedback.kind()) {
    case compiler::ProcessedFeedback::kInsufficient:
      return EmitUnconditionalDeopt(
          DeoptimizeReason::kInsufficientTypeFeedbackForGenericNamedAccess);

    case compiler::ProcessedFeedback::kNamedAccess:
      RETURN_IF_DONE(TryBuildNamedAccess(
          object, object, processed_feedback.AsNamedAccess(), feedback_source,
          compiler::AccessMode::kStore, build_generic_access));
      break;
    default:
      break;
  }

  // Create a generic store in the fallthrough.
  return build_generic_access();
}

ReduceResult MaglevGraphBuilder::VisitDefineNamedOwnProperty() {
  // DefineNamedOwnProperty <object> <name_index> <slot>
  ValueNode* object = LoadRegister(0);
  compiler::NameRef name = GetRefOperand<Name>(1);
  FeedbackSlot slot = GetSlotOperand(2);
  compiler::FeedbackSource feedback_source{feedback(), slot};

  const compiler::ProcessedFeedback& processed_feedback =
      broker()->GetFeedbackForPropertyAccess(
          feedback_source, compiler::AccessMode::kStore, name);

  auto build_generic_access = [this, object, &name, &feedback_source]() {
    ValueNode* context = GetContext();
    ValueNode* value = GetAccumulator();
    AddNewNode<DefineNamedOwnGeneric>({context, object, value}, name,
                                      feedback_source);
    return ReduceResult::Done();
  };
  switch (processed_feedback.kind()) {
    case compiler::ProcessedFeedback::kInsufficient:
      return EmitUnconditionalDeopt(
          DeoptimizeReason::kInsufficientTypeFeedbackForGenericNamedAccess);

    case compiler::ProcessedFeedback::kNamedAccess:
      RETURN_IF_DONE(TryBuildNamedAccess(
          object, object, processed_feedback.AsNamedAccess(), feedback_source,
          compiler::AccessMode::kDefine, build_generic_access));
      break;

    default:
      break;
  }

  // Create a generic store in the fallthrough.
  return build_generic_access();
}

ReduceResult MaglevGraphBuilder::VisitSetKeyedProperty() {
  // SetKeyedProperty <object> <key> <slot>
  ValueNode* object = LoadRegister(0);
  FeedbackSlot slot = GetSlotOperand(2);
  compiler::FeedbackSource feedback_source{feedback(), slot};

  const compiler::ProcessedFeedback& processed_feedback =
      broker()->GetFeedbackForPropertyAccess(
          feedback_source, compiler::AccessMode::kStore, std::nullopt);

  auto build_generic_access = [this, object, &feedback_source]() {
    ValueNode* key = LoadRegister(1);
    ValueNode* context = GetContext();
    ValueNode* value = GetAccumulator();
    AddNewNode<SetKeyedGeneric>({context, object, key, value}, feedback_source);
    return ReduceResult::Done();
  };

  switch (processed_feedback.kind()) {
    case compiler::ProcessedFeedback::kInsufficient:
      return EmitUnconditionalDeopt(
          DeoptimizeReason::kInsufficientTypeFeedbackForGenericKeyedAccess);

    case compiler::ProcessedFeedback::kElementAccess: {
      // Get the key without conversion. TryBuildElementAccess will try to pick
      // the best representation.
      ValueNode* index =
          current_interpreter_frame_.get(iterator_.GetRegisterOperand(1));
      RETURN_IF_DONE(TryBuildElementAccess(
          object, index, processed_feedback.AsElementAccess(), feedback_source,
          build_generic_access));
    } break;

    default:
      break;
  }

  // Create a generic store in the fallthrough.
  return build_generic_access();
}

ReduceResult MaglevGraphBuilder::VisitDefineKeyedOwnProperty() {
  // DefineKeyedOwnProperty <object> <key> <flags> <slot>
  ValueNode* object = LoadRegister(0);
  ValueNode* key = LoadRegister(1);
  ValueNode* flags = GetSmiConstant(GetFlag8Operand(2));
  FeedbackSlot slot = GetSlotOperand(3);
  compiler::FeedbackSource feedback_source{feedback(), slot};

  // TODO(victorgomes): Add monomorphic fast path.

  // Create a generic store in the fallthrough.
  ValueNode* context = GetContext();
  ValueNode* value = GetAccumulator();
  AddNewNode<DefineKeyedOwnGeneric>({context, object, key, value, flags},
                                    feedback_source);
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitStaInArrayLiteral() {
  // StaInArrayLiteral <object> <index> <slot>
  ValueNode* object = LoadRegister(0);
  ValueNode* index = LoadRegister(1);
  FeedbackSlot slot = GetSlotOperand(2);
  compiler::FeedbackSource feedback_source{feedback(), slot};

  const compiler::ProcessedFeedback& processed_feedback =
      broker()->GetFeedbackForPropertyAccess(
          feedback_source, compiler::AccessMode::kStoreInLiteral, std::nullopt);

  auto build_generic_access = [this, object, index, &feedback_source]() {
    ValueNode* context = GetContext();
    ValueNode* value = GetAccumulator();
    AddNewNode<StoreInArrayLiteralGeneric>({context, object, index, value},
                                           feedback_source);
    return ReduceResult::Done();
  };

  switch (processed_feedback.kind()) {
    case compiler::ProcessedFeedback::kInsufficient:
      return EmitUnconditionalDeopt(
          DeoptimizeReason::kInsufficientTypeFeedbackForGenericKeyedAccess);

    case compiler::ProcessedFeedback::kElementAccess: {
      RETURN_IF_DONE(TryBuildElementAccess(
          object, index, processed_feedback.AsElementAccess(), feedback_source,
          build_generic_access));
      break;
    }

    default:
      break;
  }

  // Create a generic store in the fallthrough.
  return build_generic_access();
}

ReduceResult MaglevGraphBuilder::VisitDefineKeyedOwnPropertyInLiteral() {
  ValueNode* object = LoadRegister(0);
  ValueNode* name = LoadRegister(1);
  ValueNode* value = GetAccumulator();
  ValueNode* flags = GetSmiConstant(GetFlag8Operand(2));
  ValueNode* slot = GetTaggedIndexConstant(GetSlotOperand(3).ToInt());
  ValueNode* feedback_vector = GetConstant(feedback());
  return BuildCallRuntime(Runtime::kDefineKeyedOwnPropertyInLiteral,
                          {object, name, value, flags, feedback_vector, slot});
}

ReduceResult MaglevGraphBuilder::VisitAdd() {
  return VisitBinaryOperation<Operation::kAdd>();
}
ReduceResult MaglevGraphBuilder::VisitSub() {
  return VisitBinaryOperation<Operation::kSubtract>();
}
ReduceResult MaglevGraphBuilder::VisitMul() {
  return VisitBinaryOperation<Operation::kMultiply>();
}
ReduceResult MaglevGraphBuilder::VisitDiv() {
  return VisitBinaryOperation<Operation::kDivide>();
}
ReduceResult MaglevGraphBuilder::VisitMod() {
  return VisitBinaryOperation<Operation::kModulus>();
}
ReduceResult MaglevGraphBuilder::VisitExp() {
  return VisitBinaryOperation<Operation::kExponentiate>();
}
ReduceResult MaglevGraphBuilder::VisitBitwiseOr() {
  return VisitBinaryOperation<Operation::kBitwiseOr>();
}
ReduceResult MaglevGraphBuilder::VisitBitwiseXor() {
  return VisitBinaryOperation<Operation::kBitwiseXor>();
}
ReduceResult MaglevGraphBuilder::VisitBitwiseAnd() {
  return VisitBinaryOperation<Operation::kBitwiseAnd>();
}
ReduceResult MaglevGraphBuilder::VisitShiftLeft() {
  return VisitBinaryOperation<Operation::kShiftLeft>();
}
ReduceResult MaglevGraphBuilder::VisitShiftRight() {
  return VisitBinaryOperation<Operation::kShiftRight>();
}
ReduceResult MaglevGraphBuilder::VisitShiftRightLogical() {
  return VisitBinaryOperation<Operation::kShiftRightLogical>();
}

ReduceResult MaglevGraphBuilder::VisitAddSmi() {
  return VisitBinarySmiOperation<Operation::kAdd>();
}
ReduceResult MaglevGraphBuilder::VisitSubSmi() {
  return VisitBinarySmiOperation<Operation::kSubtract>();
}
ReduceResult MaglevGraphBuilder::VisitMulSmi() {
  return VisitBinarySmiOperation<Operation::kMultiply>();
}
ReduceResult MaglevGraphBuilder::VisitDivSmi() {
  return VisitBinarySmiOperation<Operation::kDivide>();
}
ReduceResult MaglevGraphBuilder::VisitModSmi() {
  return VisitBinarySmiOperation<Operation::kModulus>();
}
ReduceResult MaglevGraphBuilder::VisitExpSmi() {
  return VisitBinarySmiOperation<Operation::kExponentiate>();
}
ReduceResult MaglevGraphBuilder::VisitBitwiseOrSmi() {
  return VisitBinarySmiOperation<Operation::kBitwiseOr>();
}
ReduceResult MaglevGraphBuilder::VisitBitwiseXorSmi() {
  return VisitBinarySmiOperation<Operation::kBitwiseXor>();
}
ReduceResult MaglevGraphBuilder::VisitBitwiseAndSmi() {
  return VisitBinarySmiOperation<Operation::kBitwiseAnd>();
}
ReduceResult MaglevGraphBuilder::VisitShiftLeftSmi() {
  return VisitBinarySmiOperation<Operation::kShiftLeft>();
}
ReduceResult MaglevGraphBuilder::VisitShiftRightSmi() {
  return VisitBinarySmiOperation<Operation::kShiftRight>();
}
ReduceResult MaglevGraphBuilder::VisitShiftRightLogicalSmi() {
  return VisitBinarySmiOperation<Operation::kShiftRightLogical>();
}

ReduceResult MaglevGraphBuilder::VisitInc() {
  return VisitUnaryOperation<Operation::kIncrement>();
}
ReduceResult MaglevGraphBuilder::VisitDec() {
  return VisitUnaryOperation<Operation::kDecrement>();
}
ReduceResult MaglevGraphBuilder::VisitNegate() {
  return VisitUnaryOperation<Operation::kNegate>();
}
ReduceResult MaglevGraphBuilder::VisitBitwiseNot() {
  return VisitUnaryOperation<Operation::kBitwiseNot>();
}

ReduceResult MaglevGraphBuilder::VisitToBooleanLogicalNot() {
  SetAccumulator(BuildToBoolean</* flip */ true>(GetAccumulator()));
  return ReduceResult::Done();
}

ValueNode* MaglevGraphBuilder::BuildLogicalNot(ValueNode* value) {
  // TODO(victorgomes): Use NodeInfo to add more type optimizations here.
  switch (value->opcode()) {
#define CASE(Name)                                         \
  case Opcode::k##Name: {                                  \
    return GetBooleanConstant(                             \
        !value->Cast<Name>()->ToBoolean(local_isolate())); \
  }
    CONSTANT_VALUE_NODE_LIST(CASE)
#undef CASE
    default:
      return AddNewNode<LogicalNot>({value});
  }
}

ReduceResult MaglevGraphBuilder::VisitLogicalNot() {
  // Invariant: accumulator must already be a boolean value.
  SetAccumulator(BuildLogicalNot(GetAccumulator()));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitTypeOf() {
  ValueNode* value = GetAccumulator();
  PROCESS_AND_RETURN_IF_DONE(TryReduceTypeOf(value), SetAccumulator);

  FeedbackNexus nexus = FeedbackNexusForOperand(0);
  TypeOfFeedback::Result feedback = nexus.GetTypeOfFeedback();
  switch (feedback) {
    case TypeOfFeedback::kNone:
      return EmitUnconditionalDeopt(
          DeoptimizeReason::kInsufficientTypeFeedbackForTypeOf);
    case TypeOfFeedback::kNumber:
      BuildCheckNumber(value);
      SetAccumulator(GetRootConstant(RootIndex::knumber_string));
      return ReduceResult::Done();
    case TypeOfFeedback::kString:
      BuildCheckString(value);
      SetAccumulator(GetRootConstant(RootIndex::kstring_string));
      return ReduceResult::Done();
    case TypeOfFeedback::kFunction:
      AddNewNode<CheckDetectableCallable>({value},
                                          GetCheckType(GetType(value)));
      EnsureType(value, NodeType::kCallable);
      SetAccumulator(GetRootConstant(RootIndex::kfunction_string));
      return ReduceResult::Done();
    default:
      break;
  }

  SetAccumulator(BuildCallBuiltin<Builtin::kTypeof>({GetTaggedValue(value)}));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitDeletePropertyStrict() {
  ValueNode* object = LoadRegister(0);
  ValueNode* key = GetAccumulator();
  ValueNode* context = GetContext();
  SetAccumulator(AddNewNode<DeleteProperty>({context, object, key},
                                            LanguageMode::kStrict));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitDeletePropertySloppy() {
  ValueNode* object = LoadRegister(0);
  ValueNode* key = GetAccumulator();
  ValueNode* context = GetContext();
  SetAccumulator(AddNewNode<DeleteProperty>({context, object, key},
                                            LanguageMode::kSloppy));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitGetSuperConstructor() {
  ValueNode* active_function = GetAccumulator();
  // TODO(victorgomes): Maybe BuildLoadTaggedField should support constants
  // instead.
  if (compiler::OptionalHeapObjectRef constant =
          TryGetConstant(active_function)) {
    compiler::MapRef map = constant->map(broker());
    if (map.is_stable()) {
      broker()->dependencies()->DependOnStableMap(map);
      ValueNode* map_proto = GetConstant(map.prototype(broker()));
      StoreRegister(iterator_.GetRegisterOperand(0), map_proto);
      return ReduceResult::Done();
    }
  }
  ValueNode* map =
      BuildLoadTaggedField(active_function, HeapObject::kMapOffset);
  ValueNode* map_proto = BuildLoadTaggedField(map, Map::kPrototypeOffset);
  StoreRegister(iterator_.GetRegisterOperand(0), map_proto);
  return ReduceResult::Done();
}

bool MaglevGraphBuilder::HasValidInitialMap(
    compiler::JSFunctionRef new_target, compiler::JSFunctionRef constructor) {
  if (!new_target.map(broker()).has_prototype_slot()) return false;
  if (!new_target.has_initial_map(broker())) return false;
  compiler::MapRef initial_map = new_target.initial_map(broker());
  return initial_map.GetConstructor(broker()).equals(constructor);
}

bool MaglevGraphBuilder::TryBuildFindNonDefaultConstructorOrConstruct(
    ValueNode* this_function, ValueNode* new_target,
    std::pair<interpreter::Register, interpreter::Register> result) {
  // See also:
  // JSNativeContextSpecialization::ReduceJSFindNonDefaultConstructorOrConstruct

  compiler::OptionalHeapObjectRef maybe_constant =
      TryGetConstant(this_function);
  if (!maybe_constant) return false;

  compiler::MapRef function_map = maybe_constant->map(broker());
  compiler::HeapObjectRef current = function_map.prototype(broker());

  // TODO(v8:13091): Don't produce incomplete stack traces when debug is active.
  // We already deopt when a breakpoint is set. But it would be even nicer to
  // avoid producting incomplete stack traces when when debug is active, even if
  // there are no breakpoints - then a user inspecting stack traces via Dev
  // Tools would always see the full stack trace.

  while (true) {
    if (!current.IsJSFunction()) return false;
    compiler::JSFunctionRef current_function = current.AsJSFunction();

    // If there are class fields, bail out. TODO(v8:13091): Handle them here.
    if (current_function.shared(broker())
            .requires_instance_members_initializer()) {
      return false;
    }

    // If there are private methods, bail out. TODO(v8:13091): Handle them here.
    if (current_function.context(broker())
            .scope_info(broker())
            .ClassScopeHasPrivateBrand()) {
      return false;
    }

    FunctionKind kind = current_function.shared(broker()).kind();
    if (kind != FunctionKind::kDefaultDerivedConstructor) {
      // The hierarchy walk will end here; this is the last change to bail out
      // before creating new nodes.
      if (!broker()->dependencies()->DependOnArrayIteratorProtector()) {
        return false;
      }

      compiler::OptionalHeapObjectRef new_target_function =
          TryGetConstant(new_target);
      if (kind == FunctionKind::kDefaultBaseConstructor) {
        // Store the result register first, so that a lazy deopt in
        // `FastNewObject` writes `true` to this register.
        StoreRegister(result.first, GetBooleanConstant(true));

        ValueNode* object;
        if (new_target_function && new_target_function->IsJSFunction() &&
            HasValidInitialMap(new_target_function->AsJSFunction(),
                               current_function)) {
          object = BuildInlinedAllocation(
              CreateJSConstructor(new_target_function->AsJSFunction()),
              AllocationType::kYoung);
          ClearCurrentAllocationBlock();
        } else {
          object = BuildCallBuiltin<Builtin::kFastNewObject>(
              {GetConstant(current_function), GetTaggedValue(new_target)});
          // We've already stored "true" into result.first, so a deopt here just
          // has to store result.second. Also mark result.first as being used,
          // since the lazy deopt frame won't have marked it since it used to be
          // a result register.
          AddDeoptUse(current_interpreter_frame_.get(result.first));
          object->lazy_deopt_info()->UpdateResultLocation(result.second, 1);
        }
        StoreRegister(result.second, object);
      } else {
        StoreRegister(result.first, GetBooleanConstant(false));
        StoreRegister(result.second, GetConstant(current));
      }

      broker()->dependencies()->DependOnStablePrototypeChain(
          function_map, WhereToStart::kStartAtReceiver, current_function);
      return true;
    }

    // Keep walking up the class tree.
    current = current_function.map(broker()).prototype(broker());
  }
}

ReduceResult MaglevGraphBuilder::VisitFindNonDefaultConstructorOrConstruct() {
  ValueNode* this_function = LoadRegister(0);
  ValueNode* new_target = LoadRegister(1);

  auto register_pair = iterator_.GetRegisterPairOperand(2);

  if (TryBuildFindNonDefaultConstructorOrConstruct(this_function, new_target,
                                                   register_pair)) {
    return ReduceResult::Done();
  }

  CallBuiltin* result =
      BuildCallBuiltin<Builtin::kFindNonDefaultConstructorOrConstruct>(
          {GetTaggedValue(this_function), GetTaggedValue(new_target)});
  StoreRegisterPair(register_pair, result);
  return ReduceResult::Done();
}

namespace {
void ForceEscapeIfAllocation(ValueNode* value) {
  if (InlinedAllocation* alloc = value->TryCast<InlinedAllocation>()) {
    alloc->ForceEscaping();
  }
}
}  // namespace

ReduceResult MaglevGraphBuilder::BuildInlined(ValueNode* context,
                                              ValueNode* function,
                                              ValueNode* new_target,
                                              const CallArguments& args) {
  DCHECK(is_inline());

  // Manually create the prologue of the inner function graph, so that we
  // can manually set up the arguments.
  DCHECK_NOT_NULL(current_block_);

  // Set receiver.
  ValueNode* receiver =
      GetConvertReceiver(compilation_unit_->shared_function_info(), args);
  SetArgument(0, receiver);

  // The inlined function could call a builtin that iterates the frame, the
  // receiver needs to have been materialized.
  // TODO(victorgomes): Can we relax this requirement? Maybe we can allocate the
  // object lazily? This is also only required if the inlined function is not a
  // leaf (ie. it calls other functions).
  ForceEscapeIfAllocation(receiver);

  // Set remaining arguments.
  RootConstant* undefined_constant =
      GetRootConstant(RootIndex::kUndefinedValue);
  int arg_count = static_cast<int>(args.count());
  int formal_parameter_count = compilation_unit_->parameter_count() - 1;
  for (int i = 0; i < formal_parameter_count; i++) {
    ValueNode* arg_value = args[i];
    if (arg_value == nullptr) arg_value = undefined_constant;
    SetArgument(i + 1, arg_value);
  }

  // Save all arguments if we have a mismatch between arguments count and
  // parameter count.
  inlined_arguments_ = zone()->AllocateVector<ValueNode*>(arg_count + 1);
  inlined_arguments_[0] = receiver;
  for (int i = 0; i < arg_count; i++) {
    inlined_arguments_[i + 1] = args[i];
  }

  inlined_new_target_ = new_target;

  BuildRegisterFrameInitialization(context, function, new_target);
  BuildMergeStates();
  EndPrologue();
  in_prologue_ = false;

  // Build the inlined function body.
  BuildBody();

  // All returns in the inlined body jump to a merge point one past the bytecode
  // length (i.e. at offset bytecode.length()). If there isn't one already,
  // create a block at this fake offset and have it jump out of the inlined
  // function, into a new block that we create which resumes execution of the
  // outer function.
  if (!current_block_) {
    // If we don't have a merge state at the inline_exit_offset, then there is
    // no control flow that reaches the end of the inlined function, either
    // because of infinite loops or deopts
    if (merge_states_[inline_exit_offset()] == nullptr) {
      return ReduceResult::DoneWithAbort();
    }

    ProcessMergePoint(inline_exit_offset(), /*preserve_kna*/ false);
    StartNewBlock(inline_exit_offset(), /*predecessor*/ nullptr);
  }

  // Pull the returned accumulator value out of the inlined function's final
  // merged return state.
  return current_interpreter_frame_.accumulator();
}

#define TRACE_INLINING(...)                       \
  do {                                            \
    if (v8_flags.trace_maglev_inlining)           \
      StdoutStream{} << __VA_ARGS__ << std::endl; \
  } while (false)

#define TRACE_CANNOT_INLINE(...) \
  TRACE_INLINING("  cannot inline " << shared << ": " << __VA_ARGS__)

bool MaglevGraphBuilder::ShouldInlineCall(
    compiler::SharedFunctionInfoRef shared,
    compiler::OptionalFeedbackVectorRef feedback_vector, float call_frequency) {
  if (graph()->total_inlined_bytecode_size() >
      v8_flags.max_maglev_inlined_bytecode_size_cumulative) {
    compilation_unit_->info()->set_could_not_inline_all_candidates();
    TRACE_CANNOT_INLINE("maximum inlined bytecode size");
    return false;
  }
  if (!feedback_vector) {
    // TODO(verwaest): Soft deopt instead?
    TRACE_CANNOT_INLINE("it has not been compiled/run with feedback yet");
    return false;
  }
  // TODO(olivf): This is a temporary stopgap to prevent infinite recursion when
  // inlining, because we currently excempt small functions from some of the
  // negative heuristics. We should refactor these heuristics and make sure they
  // make sense in the presence of (mutually) recursive inlining. Please do
  // *not* return true before this check.
  if (inlining_depth() > v8_flags.max_maglev_hard_inline_depth) {
    TRACE_CANNOT_INLINE("inlining depth ("
                        << inlining_depth() << ") >= hard-max-depth ("
                        << v8_flags.max_maglev_hard_inline_depth << ")");
    return false;
  }
  if (compilation_unit_->shared_function_info().equals(shared)) {
    TRACE_CANNOT_INLINE("direct recursion");
    return false;
  }
  SharedFunctionInfo::Inlineability inlineability =
      shared.GetInlineability(broker());
  if (inlineability != SharedFunctionInfo::Inlineability::kIsInlineable) {
    TRACE_CANNOT_INLINE(inlineability);
    return false;
  }
  // TODO(victorgomes): Support NewTarget/RegisterInput in inlined functions.
  compiler::BytecodeArrayRef bytecode = shared.GetBytecodeArray(broker());
  if (bytecode.incoming_new_target_or_generator_register().is_valid()) {
    TRACE_CANNOT_INLINE("use unsupported NewTargetOrGenerator register");
    return false;
  }
  if (call_frequency < v8_flags.min_maglev_inlining_frequency) {
    TRACE_CANNOT_INLINE("call frequency ("
                        << call_frequency << ") < minimum threshold ("
                        << v8_flags.min_maglev_inlining_frequency << ")");
    return false;
  }
  if (bytecode.length() < v8_flags.max_maglev_inlined_bytecode_size_small) {
    TRACE_INLINING("  inlining "
                   << shared
                   << ": small function, skipping max-size and max-depth");
    return true;
  }
  if (bytecode.length() > v8_flags.max_maglev_inlined_bytecode_size) {
    TRACE_CANNOT_INLINE("big function, size ("
                        << bytecode.length() << ") >= max-size ("
                        << v8_flags.max_maglev_inlined_bytecode_size << ")");
    return false;
  }
  if (inlining_depth() > v8_flags.max_maglev_inline_depth) {
    TRACE_CANNOT_INLINE("inlining depth ("
                        << inlining_depth() << ") >= max-depth ("
                        << v8_flags.max_maglev_inline_depth << ")");
    return false;
  }
  TRACE_INLINING("  inlining " << shared);
  if (v8_flags.trace_maglev_inlining_verbose) {
    BytecodeArray::Disassemble(bytecode.object(), std::cout);
    i::Print(*feedback_vector->object(), std::cout);
  }
  graph()->add_inlined_bytecode_size(bytecode.length());
  return true;
}

MaybeReduceResult MaglevGraphBuilder::TryBuildInlinedCall(
    ValueNode* context, ValueNode* function, ValueNode* new_target,
    compiler::SharedFunctionInfoRef shared,
    compiler::OptionalFeedbackVectorRef feedback_vector, CallArguments& args,
    const compiler::FeedbackSource& feedback_source) {
  DCHECK_EQ(args.mode(), CallArguments::kDefault);
  float feedback_frequency = 0.0f;
  if (feedback_source.IsValid()) {
    compiler::ProcessedFeedback const& feedback =
        broker()->GetFeedbackForCall(feedback_source);
    feedback_frequency =
        feedback.IsInsufficient() ? 0.0f : feedback.AsCall().frequency();
  }
  float call_frequency = feedback_frequency * call_frequency_;
  if (!ShouldInlineCall(shared, feedback_vector, call_frequency)) {
    return {};
  }

  compiler::BytecodeArrayRef bytecode = shared.GetBytecodeArray(broker());

  if (v8_flags.maglev_print_inlined &&
      TopLevelFunctionPassMaglevPrintFilter() &&
      (v8_flags.print_maglev_code || v8_flags.print_maglev_graph ||
       v8_flags.print_maglev_graphs)) {
    std::cout << "== Inlining " << Brief(*shared.object()) << std::endl;
    BytecodeArray::Disassemble(bytecode.object(), std::cout);
    if (v8_flags.maglev_print_feedback) {
      i::Print(*feedback_vector->object(), std::cout);
    }
  } else if (v8_flags.trace_maglev_graph_building) {
    std::cout << "== Inlining " << shared.object() << std::endl;
  }

  graph()->inlined_functions().push_back(
      OptimizedCompilationInfo::InlinedFunctionHolder(
          shared.object(), bytecode.object(), current_source_position_));
  if (feedback_vector->object()->invocation_count_before_stable(kRelaxedLoad) >
      v8_flags.invocation_count_for_early_optimization) {
    compilation_unit_->info()->set_could_not_inline_all_candidates();
  }
  int inlining_id = static_cast<int>(graph()->inlined_functions().size() - 1);

  // Create a new compilation unit and graph builder for the inlined
  // function.
  MaglevCompilationUnit* inner_unit = MaglevCompilationUnit::NewInner(
      zone(), compilation_unit_, shared, feedback_vector.value());
  MaglevGraphBuilder inner_graph_builder(
      local_isolate_, inner_unit, graph_, call_frequency,
      BytecodeOffset(iterator_.current_offset()), IsInsideLoop(), inlining_id,
      this);

  // Merge catch block state if needed.
  CatchBlockDetails catch_block = GetCurrentTryCatchBlock();
  if (catch_block.ref && catch_block.state->exception_handler_was_used()) {
    // Merge the current state into the handler state.
    catch_block.state->MergeThrow(
        GetCurrentCatchBlockGraphBuilder(), catch_block.unit,
        *current_interpreter_frame_.known_node_aspects(),
        current_interpreter_frame_.virtual_objects());
  }

  // Propagate catch block.
  inner_graph_builder.parent_catch_ = catch_block;
  inner_graph_builder.parent_catch_deopt_frame_distance_ =
      1 + (IsInsideTryBlock() ? 0 : parent_catch_deopt_frame_distance_);

  // Set the inner graph builder to build in the current block.
  inner_graph_builder.current_block_ = current_block_;

  MaybeReduceResult result =
      inner_graph_builder.BuildInlined(context, function, new_target, args);
  if (result.IsDoneWithAbort()) {
    DCHECK_NULL(inner_graph_builder.current_block_);
    current_block_ = nullptr;
    if (v8_flags.trace_maglev_graph_building) {
      std::cout << "== Finished inlining (abort) " << shared.object()
                << std::endl;
    }
    return ReduceResult::DoneWithAbort();
  }

  // Propagate KnownNodeAspects back to the caller.
  current_interpreter_frame_.set_known_node_aspects(
      inner_graph_builder.current_interpreter_frame_.known_node_aspects());
  unobserved_context_slot_stores_ =
      inner_graph_builder.unobserved_context_slot_stores_;

  // Propagate virtual object lists back to the caller.
  current_interpreter_frame_.set_virtual_objects(
      inner_graph_builder.current_interpreter_frame_.virtual_objects());

  DCHECK(result.IsDoneWithValue());
  // Resume execution using the final block of the inner builder.
  current_block_ = inner_graph_builder.current_block_;

  if (v8_flags.trace_maglev_graph_building) {
    std::cout << "== Finished inlining " << shared.object() << std::endl;
  }
  return result;
}

namespace {

bool CanInlineArrayIteratingBuiltin(compiler::JSHeapBroker* broker,
                                    const PossibleMaps& maps,
                                    ElementsKind* kind_return) {
  DCHECK_NE(0, maps.size());
  *kind_return = maps.at(0).elements_kind();
  for (compiler::MapRef map : maps) {
    if (!map.supports_fast_array_iteration(broker) ||
        !UnionElementsKindUptoSize(kind_return, map.elements_kind())) {
      return false;
    }
  }
  return true;
}

}  // namespace

MaybeReduceResult MaglevGraphBuilder::TryReduceArrayIsArray(
    compiler::JSFunctionRef target, CallArguments& args) {
  if (args.count() == 0) return GetBooleanConstant(false);

  ValueNode* node = args[0];

  if (CheckType(node, NodeType::kJSArray)) {
    return GetBooleanConstant(true);
  }

  auto node_info = known_node_aspects().TryGetInfoFor(node);
  if (node_info && node_info->possible_maps_are_known()) {
    bool has_array_map = false;
    bool has_proxy_map = false;
    bool has_other_map = false;
    for (compiler::MapRef map : node_info->possible_maps()) {
      InstanceType type = map.instance_type();
      if (InstanceTypeChecker::IsJSArray(type)) {
        has_array_map = true;
      } else if (InstanceTypeChecker::IsJSProxy(type)) {
        has_proxy_map = true;
      } else {
        has_other_map = true;
      }
    }
    if ((has_array_map ^ has_other_map) && !has_proxy_map) {
      if (has_array_map) node_info->CombineType(NodeType::kJSArray);
      return GetBooleanConstant(has_array_map);
    }
  }

  // TODO(verwaest): Add a node that checks the instance type.
  return {};
}

MaybeReduceResult MaglevGraphBuilder::TryReduceArrayForEach(
    compiler::JSFunctionRef target, CallArguments& args) {
  if (!CanSpeculateCall()) return {};

  ValueNode* receiver = args.receiver();
  if (!receiver) return {};

  if (args.count() < 1) {
    if (v8_flags.trace_maglev_graph_building) {
      std::cout << "  ! Failed to reduce Array.prototype.forEach - not enough "
                   "arguments"
                << std::endl;
    }
    return {};
  }

  auto node_info = known_node_aspects().TryGetInfoFor(receiver);
  if (!node_info || !node_info->possible_maps_are_known()) {
    if (v8_flags.trace_maglev_graph_building) {
      std::cout << "  ! Failed to reduce Array.prototype.forEach - receiver "
                   "map is unknown"
                << std::endl;
    }
    return {};
  }

  ElementsKind elements_kind;
  if (!CanInlineArrayIteratingBuiltin(broker(), node_info->possible_maps(),
                                      &elements_kind)) {
    if (v8_flags.trace_maglev_graph_building) {
      std::cout << "  ! Failed to reduce Array.prototype.forEach - doesn't "
                   "support fast array iteration or incompatible maps"
                << std::endl;
    }
    return {};
  }

  // TODO(leszeks): May only be needed for holey elements kinds.
  if (!broker()->dependencies()->DependOnNoElementsProtector()) {
    if (v8_flags.trace_maglev_graph_building) {
      std::cout << "  ! Failed to reduce Array.prototype.forEach - invalidated "
                   "no elements protector"
                << std::endl;
    }
    return {};
  }

  ValueNode* callback = args[0];
  if (!callback->is_tagged()) {
    if (v8_flags.trace_maglev_graph_building) {
      std::cout << "  ! Failed to reduce Array.prototype.forEach - callback is "
                   "untagged value"
                << std::endl;
    }
    return {};
  }

  ValueNode* this_arg =
      args.count() > 1 ? args[1] : GetRootConstant(RootIndex::kUndefinedValue);

  ValueNode* original_length = BuildLoadJSArrayLength(receiver);

  // Elide the callable check if the node is known callable.
  EnsureType(callback, NodeType::kCallable, [&](NodeType old_type) {
    // ThrowIfNotCallable is wrapped in a lazy_deopt_scope to make sure the
    // exception has the right call stack.
    DeoptFrameScope lazy_deopt_scope(
        this, Builtin::kArrayForEachLoopLazyDeoptContinuation, target,
        base::VectorOf<ValueNode*>({receiver, callback, this_arg,
                                    GetSmiConstant(0), original_length}));
    AddNewNode<ThrowIfNotCallable>({callback});
  });

  ValueNode* original_length_int32 = GetInt32(original_length);

  // Remember the receiver map set before entering the loop the call.
  bool receiver_maps_were_unstable = node_info->possible_maps_are_unstable();
  PossibleMaps receiver_maps_before_loop(node_info->possible_maps());

  // Create a sub graph builder with two variable (index and length)
  MaglevSubGraphBuilder sub_builder(this, 2);
  MaglevSubGraphBuilder::Variable var_index(0);
  MaglevSubGraphBuilder::Variable var_length(1);

  MaglevSubGraphBuilder::Label loop_end(&sub_builder, 1);

  // ```
  // index = 0
  // bind loop_header
  // ```
  sub_builder.set(var_index, GetSmiConstant(0));
  sub_builder.set(var_length, original_length);
  MaglevSubGraphBuilder::LoopLabel loop_header =
      sub_builder.BeginLoop({&var_index, &var_length});

  // Reset known state that is cleared by BeginLoop, but is known to be true on
  // the first iteration, and will be re-checked at the end of the loop.

  // Reset the known receiver maps if necessary.
  if (receiver_maps_were_unstable) {
    node_info->SetPossibleMaps(receiver_maps_before_loop,
                               receiver_maps_were_unstable,
                               // Node type is monotonic, no need to reset it.
                               NodeType::kUnknown, broker());
    known_node_aspects().any_map_for_any_node_is_unstable = true;
  } else {
    DCHECK_EQ(node_info->possible_maps().size(),
              receiver_maps_before_loop.size());
  }
  // Reset the cached loaded array length to the length var.
  RecordKnownProperty(receiver, broker()->length_string(),
                      sub_builder.get(var_length), false,
                      compiler::AccessMode::kLoad);

  // ```
  // if (index_int32 < length_int32)
  //   fallthrough
  // else
  //   goto end
  // ```
  Phi* index_tagged = sub_builder.get(var_index)->Cast<Phi>();
  EnsureType(index_tagged, NodeType::kSmi);
  ValueNode* index_int32 = GetInt32(index_tagged);

  sub_builder.GotoIfFalse<BranchIfInt32Compare>(
      &loop_end, {index_int32, original_length_int32}, Operation::kLessThan);

  // ```
  // next_index = index + 1
  // ```
  ValueNode* next_index_int32 = nullptr;
  {
    // Eager deopt scope for index increment overflow.
    // TODO(pthier): In practice this increment can never overflow, as the max
    // possible array length is less than int32 max value. Add a new
    // Int32Increment that asserts no overflow instead of deopting.
    DeoptFrameScope eager_deopt_scope(
        this, Builtin::kArrayForEachLoopEagerDeoptContinuation, target,
        base::VectorOf<ValueNode*>(
            {receiver, callback, this_arg, index_int32, original_length}));
    next_index_int32 = AddNewNode<Int32IncrementWithOverflow>({index_int32});
    EnsureType(next_index_int32, NodeType::kSmi);
  }
  // TODO(leszeks): Assert Smi.

  // ```
  // element = array.elements[index]
  // ```
  ValueNode* elements = BuildLoadElements(receiver);
  ValueNode* element;
  if (IsDoubleElementsKind(elements_kind)) {
    element = BuildLoadFixedDoubleArrayElement(elements, index_int32);
  } else {
    element = BuildLoadFixedArrayElement(elements, index_int32);
  }

  std::optional<MaglevSubGraphBuilder::Label> skip_call;
  if (IsHoleyElementsKind(elements_kind)) {
    // ```
    // if (element is hole) goto skip_call
    // ```
    skip_call.emplace(
        &sub_builder, 2,
        std::initializer_list<MaglevSubGraphBuilder::Variable*>{&var_length});
    if (elements_kind == HOLEY_DOUBLE_ELEMENTS) {
      sub_builder.GotoIfTrue<BranchIfFloat64IsHole>(&*skip_call, {element});
    } else {
      sub_builder.GotoIfTrue<BranchIfRootConstant>(&*skip_call, {element},
                                                   RootIndex::kTheHoleValue);
    }
  }

  // ```
  // callback(this_arg, element, array)
  // ```
  MaybeReduceResult result;
  {
    DeoptFrameScope lazy_deopt_scope(
        this, Builtin::kArrayForEachLoopLazyDeoptContinuation, target,
        base::VectorOf<ValueNode*>(
            {receiver, callback, this_arg, next_index_int32, original_length}));

    CallArguments call_args =
        args.count() < 2
            ? CallArguments(ConvertReceiverMode::kNullOrUndefined,
                            {element, index_tagged, receiver})
            : CallArguments(ConvertReceiverMode::kAny,
                            {this_arg, element, index_tagged, receiver});

    SaveCallSpeculationScope saved(this);
    result = ReduceCall(callback, call_args, saved.value());
  }

  // ```
  // index = next_index
  // jump loop_header
  // ```
  DCHECK_IMPLIES(result.IsDoneWithAbort(), current_block_ == nullptr);

  // No need to finish the loop if this code is unreachable.
  if (!result.IsDoneWithAbort()) {
    // If any of the receiver's maps were unstable maps, we have to re-check the
    // maps on each iteration, in case the callback changed them. That said, we
    // know that the maps are valid on the first iteration, so we can rotate the
    // check to _after_ the callback, and then elide it if the receiver maps are
    // still known to be valid (i.e. the known maps after the call are contained
    // inside the known maps before the call).
    bool recheck_maps_after_call = receiver_maps_were_unstable;
    if (recheck_maps_after_call) {
      // No need to recheck maps if there are known maps...
      if (auto receiver_info_after_call =
              known_node_aspects().TryGetInfoFor(receiver)) {
        // ... and those known maps are equal to, or a subset of, the maps
        // before the call.
        if (receiver_info_after_call &&
            receiver_info_after_call->possible_maps_are_known()) {
          recheck_maps_after_call = !receiver_maps_before_loop.contains(
              receiver_info_after_call->possible_maps());
        }
      }
    }

    // Make sure to finish the loop if we eager deopt in the map check or index
    // check.
    DeoptFrameScope eager_deopt_scope(
        this, Builtin::kArrayForEachLoopEagerDeoptContinuation, target,
        base::VectorOf<ValueNode*>(
            {receiver, callback, this_arg, next_index_int32, original_length}));

    if (recheck_maps_after_call) {
      // Build the CheckMap manually, since we're doing it with already known
      // maps rather than feedback, and we don't need to update known node
      // aspects or types since we're at the end of the loop anyway.
      bool emit_check_with_migration = std::any_of(
          receiver_maps_before_loop.begin(), receiver_maps_before_loop.end(),
          [](compiler::MapRef map) { return map.is_migration_target(); });
      if (emit_check_with_migration) {
        AddNewNode<CheckMapsWithMigration>({receiver},
                                           receiver_maps_before_loop,
                                           CheckType::kOmitHeapObjectCheck);
      } else {
        AddNewNode<CheckMaps>({receiver}, receiver_maps_before_loop,
                              CheckType::kOmitHeapObjectCheck);
      }
    }

    // Check if the index is still in bounds, in case the callback changed the
    // length.
    ValueNode* current_length = BuildLoadJSArrayLength(receiver);
    sub_builder.set(var_length, current_length);

    // Reference compare the loaded length against the original length. If this
    // is the same value node, then we didn't have any side effects and didn't
    // clear the cached length.
    if (current_length != original_length) {
      RETURN_IF_ABORT(
          TryBuildCheckInt32Condition(original_length_int32, current_length,
                                      AssertCondition::kUnsignedLessThanEqual,
                                      DeoptimizeReason::kArrayLengthChanged));
    }
  }

  if (skip_call.has_value()) {
    sub_builder.GotoOrTrim(&*skip_call);
    sub_builder.Bind(&*skip_call);
  }

  sub_builder.set(var_index, next_index_int32);
  sub_builder.EndLoop(&loop_header);

  // ```
  // bind end
  // ```
  sub_builder.Bind(&loop_end);

  return GetRootConstant(RootIndex::kUndefinedValue);
}

MaybeReduceResult MaglevGraphBuilder::TryReduceArrayIteratorPrototypeNext(
    compiler::JSFunctionRef target, CallArguments& args) {
  if (!CanSpeculateCall()) return {};

  ValueNode* receiver = args.receiver();
  if (!receiver) return {};

  if (!receiver->Is<InlinedAllocation>()) return {};
  VirtualObject* iterator = receiver->Cast<InlinedAllocation>()->object();
  if (!iterator->map().IsJSArrayIteratorMap()) {
    FAIL("iterator is not a JS array iterator object");
  }

  ValueNode* iterated_object =
      iterator->get(JSArrayIterator::kIteratedObjectOffset);
  ElementsKind elements_kind;
  base::SmallVector<compiler::MapRef, 4> maps;
  if (iterated_object->Is<InlinedAllocation>()) {
    VirtualObject* array = iterated_object->Cast<InlinedAllocation>()->object();
    // TODO(victorgomes): Remove this once we track changes in the inlined
    // allocated object.
    if (iterated_object->Cast<InlinedAllocation>()->IsEscaping()) {
      FAIL("allocation is escaping, map could have been changed");
    }
    // TODO(victorgomes): This effectively disable the optimization for `for-of`
    // loops. We need to figure it out a way to re-enable this.
    if (IsInsideLoop()) {
      FAIL("we're inside a loop, iterated object map could change");
    }
    auto map = array->map();
    if (!map.supports_fast_array_iteration(broker())) {
      FAIL("no fast array iteration support");
    }
    elements_kind = map.elements_kind();
    maps.push_back(map);
  } else {
    auto node_info = known_node_aspects().TryGetInfoFor(iterated_object);
    if (!node_info || !node_info->possible_maps_are_known()) {
      FAIL("iterated object is unknown");
    }
    if (!CanInlineArrayIteratingBuiltin(broker(), node_info->possible_maps(),
                                        &elements_kind)) {
      FAIL("no fast array iteration support or incompatible maps");
    }
    for (auto map : node_info->possible_maps()) {
      maps.push_back(map);
    }
  }

  // TODO(victorgomes): Support typed arrays.
  if (IsTypedArrayElementsKind(elements_kind)) {
    FAIL("no typed arrays support");
  }

  if (IsHoleyElementsKind(elements_kind) &&
      !broker()->dependencies()->DependOnNoElementsProtector()) {
    FAIL("no elements protector");
  }

  // Load the [[NextIndex]] from the {iterator}.
  // We can assume index and length fit in Uint32.
  ValueNode* index =
      BuildLoadTaggedField(receiver, JSArrayIterator::kNextIndexOffset);
  ValueNode* uint32_index;
  GET_VALUE_OR_ABORT(uint32_index, GetUint32ElementIndex(index));
  ValueNode* uint32_length;
  GET_VALUE_OR_ABORT(uint32_length,
                     GetUint32ElementIndex(BuildLoadJSArrayLength(
                         iterated_object, IsFastElementsKind(elements_kind)
                                              ? NodeType::kSmi
                                              : NodeType::kNumber)));

  // Check next index is below length
  MaglevSubGraphBuilder subgraph(this, 2);
  MaglevSubGraphBuilder::Variable is_done(0);
  MaglevSubGraphBuilder::Variable ret_value(1);
  RETURN_IF_ABORT(subgraph.Branch(
      {&is_done, &ret_value},
      [&](auto& builder) {
        return BuildBranchIfUint32Compare(builder, Operation::kLessThan,
                                          uint32_index, uint32_length);
      },
      [&] {
        ValueNode* int32_index = GetInt32(uint32_index);
        subgraph.set(is_done, GetBooleanConstant(false));
        DCHECK(
            iterator->get(JSArrayIterator::kKindOffset)->Is<Int32Constant>());
        IterationKind iteration_kind = static_cast<IterationKind>(
            iterator->get(JSArrayIterator::kKindOffset)
                ->Cast<Int32Constant>()
                ->value());
        if (iteration_kind == IterationKind::kKeys) {
          subgraph.set(ret_value, index);
        } else {
          ValueNode* value;
          GET_VALUE_OR_ABORT(
              value,
              TryBuildElementLoadOnJSArrayOrJSObject(
                  iterated_object, int32_index, base::VectorOf(maps),
                  elements_kind, KeyedAccessLoadMode::kHandleOOBAndHoles));
          if (iteration_kind == IterationKind::kEntries) {
            ValueNode* key_value_array;
            GET_VALUE_OR_ABORT(key_value_array,
                               BuildAndAllocateKeyValueArray(index, value));
            subgraph.set(ret_value, key_value_array);
          } else {
            subgraph.set(ret_value, value);
          }
        }
        // Add 1 to index
        ValueNode* next_index = AddNewNode<Int32AddWithOverflow>(
            {int32_index, GetInt32Constant(1)});
        EnsureType(next_index, NodeType::kSmi);
        // Update [[NextIndex]]
        BuildStoreTaggedFieldNoWriteBarrier(receiver, next_index,
                                            JSArrayIterator::kNextIndexOffset,
                                            StoreTaggedMode::kDefault);
        return ReduceResult::Done();
      },
      [&] {
        // Index is greater or equal than length.
        subgraph.set(is_done, GetBooleanConstant(true));
        subgraph.set(ret_value, GetRootConstant(RootIndex::kUndefinedValue));
        if (!IsTypedArrayElementsKind(elements_kind)) {
          // Mark the {iterator} as exhausted by setting the [[NextIndex]] to a
          // value that will never pass the length check again (aka the maximum
          // value possible for the specific iterated object). Note that this is
          // different from what the specification says, which is changing the
          // [[IteratedObject]] field to undefined, but that makes it difficult
          // to eliminate the map checks and "length" accesses in for..of loops.
          //
          // This is not necessary for JSTypedArray's, since the length of those
          // cannot change later and so if we were ever out of bounds for them
          // we will stay out-of-bounds forever.
          BuildStoreTaggedField(receiver, GetFloat64Constant(kMaxUInt32),
                                JSArrayIterator::kNextIndexOffset,
                                StoreTaggedMode::kDefault);
        }
        return ReduceResult::Done();
      }));

  // Allocate result object and return.
  compiler::MapRef map =
      broker()->target_native_context().iterator_result_map(broker());
  VirtualObject* iter_result = CreateJSIteratorResult(
      map, subgraph.get(ret_value), subgraph.get(is_done));
  ValueNode* allocation =
      BuildInlinedAllocation(iter_result, AllocationType::kYoung);
  // TODO(leszeks): Don't eagerly clear the raw allocation, have the
  // next side effect clear it.
  ClearCurrentAllocationBlock();
  return allocation;
}

MaybeReduceResult MaglevGraphBuilder::TryReduceArrayPrototypeEntries(
    compiler::JSFunctionRef target, CallArguments& args) {
  if (!CanSpeculateCall()) return {};
  ValueNode* receiver = GetValueOrUndefined(args.receiver());
  if (!CheckType(receiver, NodeType::kJSReceiver)) {
    return {};
  }
  return BuildAndAllocateJSArrayIterator(receiver, IterationKind::kEntries);
}

MaybeReduceResult MaglevGraphBuilder::TryReduceArrayPrototypeKeys(
    compiler::JSFunctionRef target, CallArguments& args) {
  if (!CanSpeculateCall()) return {};
  ValueNode* receiver = GetValueOrUndefined(args.receiver());
  if (!CheckType(receiver, NodeType::kJSReceiver)) {
    return {};
  }
  return BuildAndAllocateJSArrayIterator(receiver, IterationKind::kKeys);
}

MaybeReduceResult MaglevGraphBuilder::TryReduceArrayPrototypeValues(
    compiler::JSFunctionRef target, CallArguments& args) {
  if (!CanSpeculateCall()) return {};
  ValueNode* receiver = GetValueOrUndefined(args.receiver());
  if (!CheckType(receiver, NodeType::kJSReceiver)) {
    return {};
  }
  return BuildAndAllocateJSArrayIterator(receiver, IterationKind::kValues);
}

MaybeReduceResult MaglevGraphBuilder::TryReduceStringFromCharCode(
    compiler::JSFunctionRef target, CallArguments& args) {
  if (!CanSpeculateCall()) return {};
  if (args.count() != 1) return {};
  return AddNewNode<BuiltinStringFromCharCode>({GetTruncatedInt32ForToNumber(
      args[0], NodeType::kNumberOrOddball,
      TaggedToFloat64ConversionType::kNumberOrOddball)});
}

MaybeReduceResult MaglevGraphBuilder::TryReduceStringPrototypeCharCodeAt(
    compiler::JSFunctionRef target, CallArguments& args) {
  if (!CanSpeculateCall()) return {};
  ValueNode* receiver = GetValueOrUndefined(args.receiver());
  ValueNode* index;
  if (args.count() == 0) {
    // Index is the undefined object. ToIntegerOrInfinity(undefined) = 0.
    index = GetInt32Constant(0);
  } else {
    index = GetInt32ElementIndex(args[0]);
  }
  // Any other argument is ignored.

  // Try to constant-fold if receiver and index are constant
  if (auto cst = TryGetConstant(receiver)) {
    if (cst->IsString() && index->Is<Int32Constant>()) {
      compiler::StringRef str = cst->AsString();
      int idx = index->Cast<Int32Constant>()->value();
      if (idx >= 0 && static_cast<uint32_t>(idx) < str.length()) {
        if (std::optional<uint16_t> value = str.GetChar(broker(), idx)) {
          return GetSmiConstant(*value);
        }
      }
    }
  }

  // Ensure that {receiver} is actually a String.
  BuildCheckString(receiver);
  // And index is below length.
  ValueNode* length = BuildLoadStringLength(receiver);
  RETURN_IF_ABORT(TryBuildCheckInt32Condition(
      index, length, AssertCondition::kUnsignedLessThan,
      DeoptimizeReason::kOutOfBounds));
  return AddNewNode<BuiltinStringPrototypeCharCodeOrCodePointAt>(
      {receiver, index},
      BuiltinStringPrototypeCharCodeOrCodePointAt::kCharCodeAt);
}

MaybeReduceResult MaglevGraphBuilder::TryReduceStringPrototypeCodePointAt(
    compiler::JSFunctionRef target, CallArguments& args) {
  if (!CanSpeculateCall()) return {};
  ValueNode* receiver = GetValueOrUndefined(args.receiver());
  ValueNode* index;
  if (args.count() == 0) {
    // Index is the undefined object. ToIntegerOrInfinity(undefined) = 0.
    index = GetInt32Constant(0);
  } else {
    index = GetInt32ElementIndex(args[0]);
  }
  // Any other argument is ignored.
  // Ensure that {receiver} is actually a String.
  BuildCheckString(receiver);
  // And index is below length.
  ValueNode* length = BuildLoadStringLength(receiver);
  RETURN_IF_ABORT(TryBuildCheckInt32Condition(
      index, length, AssertCondition::kUnsignedLessThan,
      DeoptimizeReason::kOutOfBounds));
  return AddNewNode<BuiltinStringPrototypeCharCodeOrCodePointAt>(
      {receiver, index},
      BuiltinStringPrototypeCharCodeOrCodePointAt::kCodePointAt);
}

MaybeReduceResult MaglevGraphBuilder::TryReduceStringPrototypeIterator(
    compiler::JSFunctionRef target, CallArguments& args) {
  if (!CanSpeculateCall()) return {};
  ValueNode* receiver = GetValueOrUndefined(args.receiver());
  // Ensure that {receiver} is actually a String.
  BuildCheckString(receiver);
  compiler::MapRef map =
      broker()->target_native_context().initial_string_iterator_map(broker());
  VirtualObject* string_iterator = CreateJSStringIterator(map, receiver);
  ValueNode* allocation =
      BuildInlinedAllocation(string_iterator, AllocationType::kYoung);
  // TODO(leszeks): Don't eagerly clear the raw allocation, have the
  // next side effect clear it.
  ClearCurrentAllocationBlock();
  return allocation;
}

#ifdef V8_INTL_SUPPORT

MaybeReduceResult MaglevGraphBuilder::TryReduceStringPrototypeLocaleCompareIntl(
    compiler::JSFunctionRef target, CallArguments& args) {
  if (args.count() < 1 || args.count() > 3) return {};

  LocalFactory* factory = local_isolate()->factory();
  compiler::ObjectRef undefined_ref = broker()->undefined_value();

  DirectHandle<Object> locales_handle;
  ValueNode* locales_node = nullptr;
  if (args.count() > 1) {
    compiler::OptionalHeapObjectRef maybe_locales = TryGetConstant(args[1]);
    if (!maybe_locales) return {};
    compiler::HeapObjectRef locales = maybe_locales.value();
    if (locales.equals(undefined_ref)) {
      locales_handle = factory->undefined_value();
      locales_node = GetRootConstant(RootIndex::kUndefinedValue);
    } else {
      if (!locales.IsString()) return {};
      compiler::StringRef sref = locales.AsString();
      std::optional<Handle<String>> maybe_locales_handle =
          sref.ObjectIfContentAccessible(broker());
      if (!maybe_locales_handle) return {};
      locales_handle = *maybe_locales_handle;
      locales_node = args[1];
    }
  } else {
    locales_handle = factory->undefined_value();
    locales_node = GetRootConstant(RootIndex::kUndefinedValue);
  }

  if (args.count() > 2) {
    compiler::OptionalHeapObjectRef maybe_options = TryGetConstant(args[2]);
    if (!maybe_options) return {};
    if (!maybe_options.value().equals(undefined_ref)) return {};
  }

  DCHECK(!locales_handle.is_null());
  DCHECK_NOT_NULL(locales_node);

  if (Intl::CompareStringsOptionsFor(local_isolate(), locales_handle,
                                     factory->undefined_value()) !=
      Intl::CompareStringsOptions::kTryFastPath) {
    return {};
  }
  return BuildCallBuiltin<Builtin::kStringFastLocaleCompare>(
      {GetConstant(target),
       GetTaggedValue(GetValueOrUndefined(args.receiver())),
       GetTaggedValue(args[0]), GetTaggedValue(locales_node)});
}

#endif  // V8_INTL_SUPPORT

#ifdef V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
MaybeReduceResult
MaglevGraphBuilder::TryReduceGetContinuationPreservedEmbedderData(
    compiler::JSFunctionRef target, CallArguments& args) {
  return AddNewNode<GetContinuationPreservedEmbedderData>({});
}

MaybeReduceResult
MaglevGraphBuilder::TryReduceSetContinuationPreservedEmbedderData(
    compiler::JSFunctionRef target, CallArguments& args) {
  if (args.count() == 0) return {};

  AddNewNode<SetContinuationPreservedEmbedderData>({args[0]});
  return GetRootConstant(RootIndex::kUndefinedValue);
}
#endif  // V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA

template <typename LoadNode>
MaybeReduceResult MaglevGraphBuilder::TryBuildLoadDataView(
    const CallArguments& args, ExternalArrayType type) {
  if (!CanSpeculateCall()) return {};
  if (!broker()->dependencies()->DependOnArrayBufferDetachingProtector()) {
    // TODO(victorgomes): Add checks whether the array has been detached.
    return {};
  }
  // TODO(victorgomes): Add data view to known types.
  ValueNode* receiver = GetValueOrUndefined(args.receiver());
  AddNewNode<CheckInstanceType>({receiver}, CheckType::kCheckHeapObject,
                                JS_DATA_VIEW_TYPE, JS_DATA_VIEW_TYPE);
  // TODO(v8:11111): Optimize for JS_RAB_GSAB_DATA_VIEW_TYPE too.
  ValueNode* offset =
      args[0] ? GetInt32ElementIndex(args[0]) : GetInt32Constant(0);
  AddNewNode<CheckJSDataViewBounds>({receiver, offset}, type);
  ValueNode* is_little_endian = args[1] ? args[1] : GetBooleanConstant(false);
  return AddNewNode<LoadNode>({receiver, offset, is_little_endian}, type);
}

template <typename StoreNode, typename Function>
MaybeReduceResult MaglevGraphBuilder::TryBuildStoreDataView(
    const CallArguments& args, ExternalArrayType type, Function&& getValue) {
  if (!CanSpeculateCall()) return {};
  if (!broker()->dependencies()->DependOnArrayBufferDetachingProtector()) {
    // TODO(victorgomes): Add checks whether the array has been detached.
    return {};
  }
  // TODO(victorgomes): Add data view to known types.
  ValueNode* receiver = GetValueOrUndefined(args.receiver());
  AddNewNode<CheckInstanceType>({receiver}, CheckType::kCheckHeapObject,
                                JS_DATA_VIEW_TYPE, JS_DATA_VIEW_TYPE);
  // TODO(v8:11111): Optimize for JS_RAB_GSAB_DATA_VIEW_TYPE too.
  ValueNode* offset =
      args[0] ? GetInt32ElementIndex(args[0]) : GetInt32Constant(0);
  AddNewNode<CheckJSDataViewBounds>({receiver, offset},
                                    ExternalArrayType::kExternalFloat64Array);
  ValueNode* value = getValue(args[1]);
  ValueNode* is_little_endian = args[2] ? args[2] : GetBooleanConstant(false);
  AddNewNode<StoreNode>({receiver, offset, value, is_little_endian}, type);
  return GetRootConstant(RootIndex::kUndefinedValue);
}

MaybeReduceResult MaglevGraphBuilder::TryReduceDataViewPrototypeGetInt8(
    compiler::JSFunctionRef target, CallArguments& args) {
  return TryBuildLoadDataView<LoadSignedIntDataViewElement>(
      args, ExternalArrayType::kExternalInt8Array);
}
MaybeReduceResult MaglevGraphBuilder::TryReduceDataViewPrototypeSetInt8(
    compiler::JSFunctionRef target, CallArguments& args) {
  return TryBuildStoreDataView<StoreSignedIntDataViewElement>(
      args, ExternalArrayType::kExternalInt8Array,
      [&](ValueNode* value) { return value ? value : GetInt32Constant(0); });
}
MaybeReduceResult MaglevGraphBuilder::TryReduceDataViewPrototypeGetInt16(
    compiler::JSFunctionRef target, CallArguments& args) {
  return TryBuildLoadDataView<LoadSignedIntDataViewElement>(
      args, ExternalArrayType::kExternalInt16Array);
}
MaybeReduceResult MaglevGraphBuilder::TryReduceDataViewPrototypeSetInt16(
    compiler::JSFunctionRef target, CallArguments& args) {
  return TryBuildStoreDataView<StoreSignedIntDataViewElement>(
      args, ExternalArrayType::kExternalInt16Array,
      [&](ValueNode* value) { return value ? value : GetInt32Constant(0); });
}
MaybeReduceResult MaglevGraphBuilder::TryReduceDataViewPrototypeGetInt32(
    compiler::JSFunctionRef target, CallArguments& args) {
  return TryBuildLoadDataView<LoadSignedIntDataViewElement>(
      args, ExternalArrayType::kExternalInt32Array);
}
MaybeReduceResult MaglevGraphBuilder::TryReduceDataViewPrototypeSetInt32(
    compiler::JSFunctionRef target, CallArguments& args) {
  return TryBuildStoreDataView<StoreSignedIntDataViewElement>(
      args, ExternalArrayType::kExternalInt32Array,
      [&](ValueNode* value) { return value ? value : GetInt32Constant(0); });
}
MaybeReduceResult MaglevGraphBuilder::TryReduceDataViewPrototypeGetFloat64(
    compiler::JSFunctionRef target, CallArguments& args) {
  return TryBuildLoadDataView<LoadDoubleDataViewElement>(
      args, ExternalArrayType::kExternalFloat64Array);
}
MaybeReduceResult MaglevGraphBuilder::TryReduceDataViewPrototypeSetFloat64(
    compiler::JSFunctionRef target, CallArguments& args) {
  return TryBuildStoreDataView<StoreDoubleDataViewElement>(
      args, ExternalArrayType::kExternalFloat64Array, [&](ValueNode* value) {
        return value ? GetHoleyFloat64ForToNumber(
                           value, NodeType::kNumberOrOddball,
                           TaggedToFloat64ConversionType::kNumberOrOddball)
                     : GetFloat64Constant(
                           std::numeric_limits<double>::quiet_NaN());
      });
}

MaybeReduceResult MaglevGraphBuilder::TryReduceFunctionPrototypeCall(
    compiler::JSFunctionRef target, CallArguments& args) {
  // We can't reduce Function#call when there is no receiver function.
  if (args.receiver_mode() == ConvertReceiverMode::kNullOrUndefined) {
    return {};
  }
  ValueNode* receiver = GetValueOrUndefined(args.receiver());
  args.PopReceiver(ConvertReceiverMode::kAny);

  SaveCallSpeculationScope saved(this);
  return ReduceCall(receiver, args, saved.value());
}

MaybeReduceResult MaglevGraphBuilder::TryReduceFunctionPrototypeApply(
    compiler::JSFunctionRef target, CallArguments& args) {
  compiler::OptionalHeapObjectRef maybe_receiver;
  if (current_speculation_feedback_.IsValid()) {
    const compiler::ProcessedFeedback& processed_feedback =
        broker()->GetFeedbackForCall(current_speculation_feedback_);
    DCHECK_EQ(processed_feedback.kind(), compiler::ProcessedFeedback::kCall);
    const compiler::CallFeedback& call_feedback = processed_feedback.AsCall();
    if (call_feedback.call_feedback_content() ==
        CallFeedbackContent::kReceiver) {
      maybe_receiver = call_feedback.target();
    }
  }
  return TryReduceFunctionPrototypeApplyCallWithReceiver(
      maybe_receiver, args, current_speculation_feedback_);
}

namespace {

template <size_t MaxKindCount, typename KindsToIndexFunc>
bool CanInlineArrayResizingBuiltin(
    compiler::JSHeapBroker* broker, const PossibleMaps& possible_maps,
    std::array<SmallZoneVector<compiler::MapRef, 2>, MaxKindCount>& map_kinds,
    KindsToIndexFunc&& elements_kind_to_index, int* unique_kind_count,
    bool is_loading) {
  uint8_t kind_bitmap = 0;
  for (compiler::MapRef map : possible_maps) {
    if (!map.supports_fast_array_resize(broker)) {
      return false;
    }
    ElementsKind kind = map.elements_kind();
    if (is_loading && kind == HOLEY_DOUBLE_ELEMENTS) {
      return false;
    }
    // Group maps by elements kind, using the provided function to translate
    // elements kinds to indices.
    // kind_bitmap is used to get the unique kinds (predecessor count for the
    // next block).
    uint8_t kind_index = elements_kind_to_index(kind);
    kind_bitmap |= 1 << kind_index;
    map_kinds[kind_index].push_back(map);
  }

  *unique_kind_count = base::bits::CountPopulation(kind_bitmap);
  DCHECK_GE(*unique_kind_count, 1);
  return true;
}

}  // namespace

template <typename MapKindsT, typename IndexToElementsKindFunc,
          typename BuildKindSpecificFunc>
MaybeReduceResult
MaglevGraphBuilder::BuildJSArrayBuiltinMapSwitchOnElementsKind(
    ValueNode* receiver, const MapKindsT& map_kinds,
    MaglevSubGraphBuilder& sub_graph,
    std::optional<MaglevSubGraphBuilder::Label>& do_return,
    int unique_kind_count, IndexToElementsKindFunc&& index_to_elements_kind,
    BuildKindSpecificFunc&& build_kind_specific) {
  // TODO(pthier): Support map packing.
  DCHECK(!V8_MAP_PACKING_BOOL);
  ValueNode* receiver_map =
      BuildLoadTaggedField(receiver, HeapObject::kMapOffset);
  int emitted_kind_checks = 0;
  bool any_successful = false;
  for (size_t kind_index = 0; kind_index < map_kinds.size(); kind_index++) {
    const auto& maps = map_kinds[kind_index];
    // Skip kinds we haven't observed.
    if (maps.empty()) continue;
    ElementsKind kind = index_to_elements_kind(kind_index);
    // Create branches for all but the last elements kind. We don't need
    // to check the maps of the last kind, as all possible maps have already
    // been checked when the property (builtin name) was loaded.
    if (++emitted_kind_checks < unique_kind_count) {
      MaglevSubGraphBuilder::Label check_next_map(&sub_graph, 1);
      std::optional<MaglevSubGraphBuilder::Label> do_push;
      if (maps.size() > 1) {
        do_push.emplace(&sub_graph, static_cast<int>(maps.size()));
        for (size_t map_index = 1; map_index < maps.size(); map_index++) {
          sub_graph.GotoIfTrue<BranchIfReferenceEqual>(
              &*do_push, {receiver_map, GetConstant(maps[map_index])});
        }
      }
      sub_graph.GotoIfFalse<BranchIfReferenceEqual>(
          &check_next_map, {receiver_map, GetConstant(maps[0])});
      if (do_push.has_value()) {
        sub_graph.Goto(&*do_push);
        sub_graph.Bind(&*do_push);
      }
      if (!build_kind_specific(kind).IsDoneWithAbort()) {
        any_successful = true;
      }
      DCHECK(do_return.has_value());
      sub_graph.GotoOrTrim(&*do_return);
      sub_graph.Bind(&check_next_map);
    } else {
      if (!build_kind_specific(kind).IsDoneWithAbort()) {
        any_successful = true;
      }
      if (do_return.has_value()) {
        sub_graph.GotoOrTrim(&*do_return);
      }
    }
  }
  DCHECK_IMPLIES(!any_successful, !current_block_);
  return any_successful ? ReduceResult::Done() : ReduceResult::DoneWithAbort();
}

MaybeReduceResult MaglevGraphBuilder::TryReduceArrayPrototypePush(
    compiler::JSFunctionRef target, CallArguments& args) {
  if (!CanSpeculateCall()) return {};
  // We can't reduce Function#call when there is no receiver function.
  if (args.receiver_mode() == ConvertReceiverMode::kNullOrUndefined) {
    if (v8_flags.trace_maglev_graph_building) {
      std::cout << "  ! Failed to reduce Array.prototype.push - no receiver"
                << std::endl;
    }
    return {};
  }
  // TODO(pthier): Support multiple arguments.
  if (args.count() != 1) {
    if (v8_flags.trace_maglev_graph_building) {
      std::cout << "  ! Failed to reduce Array.prototype.push - invalid "
                   "argument count"
                << std::endl;
    }
    return {};
  }
  ValueNode* receiver = GetValueOrUndefined(args.receiver());

  auto node_info = known_node_aspects().TryGetInfoFor(receiver);
  // If the map set is not found, then we don't know anything about the map of
  // the receiver, so bail.
  if (!node_info || !node_info->possible_maps_are_known()) {
    if (v8_flags.trace_maglev_graph_building) {
      std::cout
          << "  ! Failed to reduce Array.prototype.push - unknown receiver map"
          << std::endl;
    }
    return {};
  }

  const PossibleMaps& possible_maps = node_info->possible_maps();
  // If the set of possible maps is empty, then there's no possible map for this
  // receiver, therefore this path is unreachable at runtime. We're unlikely to
  // ever hit this case, BuildCheckMaps should already unconditionally deopt,
  // but check it in case another checking operation fails to statically
  // unconditionally deopt.
  if (possible_maps.is_empty()) {
    // TODO(leszeks): Add an unreachable assert here.
    return ReduceResult::DoneWithAbort();
  }

  if (!broker()->dependencies()->DependOnNoElementsProtector()) {
    if (v8_flags.trace_maglev_graph_building) {
      std::cout << "  ! Failed to reduce Array.prototype.push - "
                   "NoElementsProtector invalidated"
                << std::endl;
    }
    return {};
  }

  // Check that inlining resizing array builtins is supported and group maps
  // by elements kind.
  std::array<SmallZoneVector<compiler::MapRef, 2>, 3> map_kinds = {
      SmallZoneVector<compiler::MapRef, 2>(zone()),
      SmallZoneVector<compiler::MapRef, 2>(zone()),
      SmallZoneVector<compiler::MapRef, 2>(zone())};
  // Function to group maps by elements kind, ignoring packedness. Packedness
  // doesn't matter for push().
  // Kinds we care about are all paired in the first 6 values of ElementsKind,
  // so we can use integer division to truncate holeyness.
  auto elements_kind_to_index = [&](ElementsKind kind) {
    static_assert(kFastElementsKindCount <= 6);
    static_assert(kFastElementsKindPackedToHoley == 1);
    return static_cast<uint8_t>(kind) / 2;
  };
  auto index_to_elements_kind = [&](uint8_t kind_index) {
    return static_cast<ElementsKind>(kind_index * 2);
  };
  int unique_kind_count;
  if (!CanInlineArrayResizingBuiltin(broker(), possible_maps, map_kinds,
                                     elements_kind_to_index, &unique_kind_count,
                                     false)) {
    if (v8_flags.trace_maglev_graph_building) {
      std::cout << "  ! Failed to reduce Array.prototype.push - Map doesn't "
                   "support fast resizing"
                << std::endl;
    }
    return {};
  }

  MaglevSubGraphBuilder sub_graph(this, 0);

  std::optional<MaglevSubGraphBuilder::Label> do_return;
  if (unique_kind_count > 1) {
    do_return.emplace(&sub_graph, unique_kind_count);
  }

  ValueNode* old_array_length_smi;
  GET_VALUE_OR_ABORT(old_array_length_smi,
                     GetSmiValue(BuildLoadJSArrayLength(receiver)));
  ValueNode* old_array_length =
      AddNewNode<UnsafeSmiUntag>({old_array_length_smi});
  ValueNode* new_array_length_smi =
      AddNewNode<CheckedSmiIncrement>({old_array_length_smi});

  ValueNode* elements_array = BuildLoadElements(receiver);
  ValueNode* elements_array_length = BuildLoadFixedArrayLength(elements_array);

  auto build_array_push = [&](ElementsKind kind) {
    ValueNode* value;
    GET_VALUE_OR_ABORT(value, ConvertForStoring(args[0], kind));

    ValueNode* writable_elements_array = AddNewNode<MaybeGrowFastElements>(
        {elements_array, receiver, old_array_length, elements_array_length},
        kind);

    AddNewNode<StoreTaggedFieldNoWriteBarrier>({receiver, new_array_length_smi},
                                               JSArray::kLengthOffset,
                                               StoreTaggedMode::kDefault);

    // Do the store
    if (IsDoubleElementsKind(kind)) {
      BuildStoreFixedDoubleArrayElement(writable_elements_array,
                                        old_array_length, value);
    } else {
      DCHECK(IsSmiElementsKind(kind) || IsObjectElementsKind(kind));
      BuildStoreFixedArrayElement(writable_elements_array, old_array_length,
                                  value);
    }
    return ReduceResult::Done();
  };

  RETURN_IF_ABORT(BuildJSArrayBuiltinMapSwitchOnElementsKind(
      receiver, map_kinds, sub_graph, do_return, unique_kind_count,
      index_to_elements_kind, build_array_push));

  if (do_return.has_value()) {
    sub_graph.Bind(&*do_return);
  }
  RecordKnownProperty(receiver, broker()->length_string(), new_array_length_smi,
                      false, compiler::AccessMode::kStore);
  return new_array_length_smi;
}

MaybeReduceResult MaglevGraphBuilder::TryReduceArrayPrototypePop(
    compiler::JSFunctionRef target, CallArguments& args) {
  if (!CanSpeculateCall()) return {};
  // We can't reduce Function#call when there is no receiver function.
  if (args.receiver_mode() == ConvertReceiverMode::kNullOrUndefined) {
    if (v8_flags.trace_maglev_graph_building) {
      std::cout << "  ! Failed to reduce Array.prototype.pop - no receiver"
                << std::endl;
    }
    return {};
  }

  ValueNode* receiver = GetValueOrUndefined(args.receiver());

  auto node_info = known_node_aspects().TryGetInfoFor(receiver);
  // If the map set is not found, then we don't know anything about the map of
  // the receiver, so bail.
  if (!node_info || !node_info->possible_maps_are_known()) {
    if (v8_flags.trace_maglev_graph_building) {
      std::cout
          << "  ! Failed to reduce Array.prototype.pop - unknown receiver map"
          << std::endl;
    }
    return {};
  }

  const PossibleMaps& possible_maps = node_info->possible_maps();

  // If the set of possible maps is empty, then there's no possible map for this
  // receiver, therefore this path is unreachable at runtime. We're unlikely to
  // ever hit this case, BuildCheckMaps should already unconditionally deopt,
  // but check it in case another checking operation fails to statically
  // unconditionally deopt.
  if (possible_maps.is_empty()) {
    // TODO(leszeks): Add an unreachable assert here.
    return ReduceResult::DoneWithAbort();
  }

  if (!broker()->dependencies()->DependOnNoElementsProtector()) {
    if (v8_flags.trace_maglev_graph_building) {
      std::cout << "  ! Failed to reduce Array.prototype.pop - "
                   "NoElementsProtector invalidated"
                << std::endl;
    }
    return {};
  }

  constexpr int max_kind_count = 4;
  std::array<SmallZoneVector<compiler::MapRef, 2>, max_kind_count> map_kinds = {
      SmallZoneVector<compiler::MapRef, 2>(zone()),
      SmallZoneVector<compiler::MapRef, 2>(zone()),
      SmallZoneVector<compiler::MapRef, 2>(zone()),
      SmallZoneVector<compiler::MapRef, 2>(zone())};
  // Smi and Object elements kinds are treated as identical for pop, so we can
  // group them together without differentiation.
  // ElementsKind is mapped to an index in the 4 element array using:
  //   - Bit 2 (Only set for double in the fast element range) is mapped to bit
  //   1)
  //   - Bit 0 (packedness)
  // The complete mapping:
  // +-------+----------------------------------------------+
  // | Index |    ElementsKinds                             |
  // +-------+----------------------------------------------+
  // |   0   |    PACKED_SMI_ELEMENTS and PACKED_ELEMENTS   |
  // |   1   |    HOLEY_SMI_ELEMENETS and HOLEY_ELEMENTS    |
  // |   2   |    PACKED_DOUBLE_ELEMENTS                    |
  // |   3   |    HOLEY_DOUBLE_ELEMENTS                     |
  // +-------+----------------------------------------------+
  auto elements_kind_to_index = [&](ElementsKind kind) {
    uint8_t kind_int = static_cast<uint8_t>(kind);
    uint8_t kind_index = ((kind_int & 0x4) >> 1) | (kind_int & 0x1);
    DCHECK_LT(kind_index, max_kind_count);
    return kind_index;
  };
  auto index_to_elements_kind = [&](uint8_t kind_index) {
    uint8_t kind_int;
    kind_int = ((kind_index & 0x2) << 1) | (kind_index & 0x1);
    return static_cast<ElementsKind>(kind_int);
  };

  int unique_kind_count;
  if (!CanInlineArrayResizingBuiltin(broker(), possible_maps, map_kinds,
                                     elements_kind_to_index, &unique_kind_count,
                                     true)) {
    if (v8_flags.trace_maglev_graph_building) {
      std::cout << "  ! Failed to reduce Array.prototype.pop - Map doesn't "
                   "support fast resizing"
                << std::endl;
    }
    return {};
  }

  MaglevSubGraphBuilder sub_graph(this, 2);
  MaglevSubGraphBuilder::Variable var_value(0);
  MaglevSubGraphBuilder::Variable var_new_array_length(1);

  std::optional<MaglevSubGraphBuilder::Label> do_return =
      std::make_optional<MaglevSubGraphBuilder::Label>(
          &sub_graph, unique_kind_count + 1,
          std::initializer_list<MaglevSubGraphBuilder::Variable*>{
              &var_value, &var_new_array_length});
  MaglevSubGraphBuilder::Label empty_array(&sub_graph, 1);

  ValueNode* old_array_length_smi;
  GET_VALUE_OR_ABORT(old_array_length_smi,
                     GetSmiValue(BuildLoadJSArrayLength(receiver)));

  // If the array is empty, skip the pop and return undefined.
  sub_graph.GotoIfTrue<BranchIfReferenceEqual>(
      &empty_array, {old_array_length_smi, GetSmiConstant(0)});

  ValueNode* elements_array = BuildLoadElements(receiver);
  ValueNode* new_array_length_smi =
      AddNewNode<CheckedSmiDecrement>({old_array_length_smi});
  ValueNode* new_array_length =
      AddNewNode<UnsafeSmiUntag>({new_array_length_smi});
  sub_graph.set(var_new_array_length, new_array_length_smi);

  auto build_array_pop = [&](ElementsKind kind) {
    // Handle COW if needed.
    ValueNode* writable_elements_array =
        IsSmiOrObjectElementsKind(kind)
            ? AddNewNode<EnsureWritableFastElements>({elements_array, receiver})
            : elements_array;

    // Store new length.
    AddNewNode<StoreTaggedFieldNoWriteBarrier>({receiver, new_array_length_smi},
                                               JSArray::kLengthOffset,
                                               StoreTaggedMode::kDefault);

    // Load the value and store the hole in it's place.
    ValueNode* value;
    if (IsDoubleElementsKind(kind)) {
      value = BuildLoadFixedDoubleArrayElement(writable_elements_array,
                                               new_array_length);
      BuildStoreFixedDoubleArrayElement(
          writable_elements_array, new_array_length,
          GetFloat64Constant(Float64::FromBits(kHoleNanInt64)));
    } else {
      DCHECK(IsSmiElementsKind(kind) || IsObjectElementsKind(kind));
      value =
          BuildLoadFixedArrayElement(writable_elements_array, new_array_length);
      BuildStoreFixedArrayElement(writable_elements_array, new_array_length,
                                  GetRootConstant(RootIndex::kTheHoleValue));
    }

    if (IsHoleyElementsKind(kind)) {
      value = AddNewNode<ConvertHoleToUndefined>({value});
    }
    sub_graph.set(var_value, value);
    return ReduceResult::Done();
  };

  RETURN_IF_ABORT(BuildJSArrayBuiltinMapSwitchOnElementsKind(
      receiver, map_kinds, sub_graph, do_return, unique_kind_count,
      index_to_elements_kind, build_array_pop));

  sub_graph.Bind(&empty_array);
  sub_graph.set(var_new_array_length, GetSmiConstant(0));
  sub_graph.set(var_value, GetRootConstant(RootIndex::kUndefinedValue));
  sub_graph.Goto(&*do_return);

  sub_graph.Bind(&*do_return);
  RecordKnownProperty(receiver, broker()->length_string(),
                      sub_graph.get(var_new_array_length), false,
                      compiler::AccessMode::kStore);
  return sub_graph.get(var_value);
}

MaybeReduceResult MaglevGraphBuilder::TryReduceFunctionPrototypeHasInstance(
    compiler::JSFunctionRef target, CallArguments& args) {
  // We can't reduce Function#hasInstance when there is no receiver function.
  if (args.receiver_mode() == ConvertReceiverMode::kNullOrUndefined) {
    return {};
  }
  if (args.count() != 1) {
    return {};
  }
  compiler::OptionalHeapObjectRef maybe_receiver_constant =
      TryGetConstant(args.receiver());
  if (!maybe_receiver_constant) {
    return {};
  }
  compiler::HeapObjectRef receiver_object = maybe_receiver_constant.value();
  if (!receiver_object.IsJSObject() ||
      !receiver_object.map(broker()).is_callable()) {
    return {};
  }
  return BuildOrdinaryHasInstance(args[0], receiver_object.AsJSObject(),
                                  nullptr);
}

MaybeReduceResult MaglevGraphBuilder::TryReduceObjectPrototypeHasOwnProperty(
    compiler::JSFunctionRef target, CallArguments& args) {
  if (!CanSpeculateCall()) return {};
  if (args.receiver_mode() == ConvertReceiverMode::kNullOrUndefined) {
    return {};
  }

  // We can constant-fold the {receiver.hasOwnProperty(name)} builtin call to
  // the {True} node in this case:

  //   for (name in receiver) {
  //     if (receiver.hasOwnProperty(name)) {
  //        ...
  //     }
  //   }

  if (args.count() != 1 || args[0] != current_for_in_state.key) {
    return {};
  }
  ValueNode* receiver = args.receiver();
  if (receiver == current_for_in_state.receiver) {
    if (current_for_in_state.receiver_needs_map_check) {
      auto* receiver_map =
          BuildLoadTaggedField(receiver, HeapObject::kMapOffset);
      AddNewNode<CheckDynamicValue>(
          {receiver_map, current_for_in_state.cache_type});
      current_for_in_state.receiver_needs_map_check = false;
    }
    return GetRootConstant(RootIndex::kTrueValue);
  }

  // We can also optimize for this case below:

  // receiver(is a heap constant with fast map)
  //  ^
  //  |    object(all keys are enumerable)
  //  |      ^
  //  |      |
  //  |   JSForInNext
  //  |      ^
  //  +----+ |
  //       | |
  //  JSCall[hasOwnProperty]

  // We can replace the {JSCall} with several internalized string
  // comparisons.

  compiler::OptionalMapRef maybe_receiver_map;
  compiler::OptionalHeapObjectRef receiver_ref = TryGetConstant(receiver);
  if (receiver_ref.has_value()) {
    compiler::HeapObjectRef receiver_object = receiver_ref.value();
    compiler::MapRef receiver_map = receiver_object.map(broker());
    maybe_receiver_map = receiver_map;
  } else {
    NodeInfo* known_info = GetOrCreateInfoFor(receiver);
    if (known_info->possible_maps_are_known()) {
      compiler::ZoneRefSet<Map> possible_maps = known_info->possible_maps();
      if (possible_maps.size() == 1) {
        compiler::MapRef receiver_map = *(possible_maps.begin());
        maybe_receiver_map = receiver_map;
      }
    }
  }
  if (!maybe_receiver_map.has_value()) return {};

  compiler::MapRef receiver_map = maybe_receiver_map.value();
  InstanceType instance_type = receiver_map.instance_type();
  int const nof = receiver_map.NumberOfOwnDescriptors();
  // We set a heuristic value to limit the compare instructions number.
  if (nof > 4 || IsSpecialReceiverInstanceType(instance_type) ||
      receiver_map.is_dictionary_map()) {
    return {};
  }
  RETURN_IF_ABORT(BuildCheckMaps(receiver, base::VectorOf({receiver_map})));
  //  Replace builtin call with several internalized string comparisons.
  MaglevSubGraphBuilder sub_graph(this, 1);
  MaglevSubGraphBuilder::Variable var_result(0);
  MaglevSubGraphBuilder::Label done(
      &sub_graph, nof + 1,
      std::initializer_list<MaglevSubGraphBuilder::Variable*>{&var_result});
  const compiler::DescriptorArrayRef descriptor_array =
      receiver_map.instance_descriptors(broker());
  for (InternalIndex key_index : InternalIndex::Range(nof)) {
    compiler::NameRef receiver_key =
        descriptor_array.GetPropertyKey(broker(), key_index);
    ValueNode* lhs = GetConstant(receiver_key);
    sub_graph.set(var_result, GetRootConstant(RootIndex::kTrueValue));
    sub_graph.GotoIfTrue<BranchIfReferenceEqual>(&done, {lhs, args[0]});
  }
  sub_graph.set(var_result, GetRootConstant(RootIndex::kFalseValue));
  sub_graph.Goto(&done);
  sub_graph.Bind(&done);
  return sub_graph.get(var_result);
}

MaybeReduceResult MaglevGraphBuilder::TryReduceGetProto(ValueNode* object) {
  NodeInfo* info = known_node_aspects().TryGetInfoFor(object);
  if (!info || !info->possible_maps_are_known()) {
    return {};
  }
  auto& possible_maps = info->possible_maps();
  if (possible_maps.is_empty()) {
    return ReduceResult::DoneWithAbort();
  }
  auto it = possible_maps.begin();
  compiler::MapRef map = *it;
  if (IsSpecialReceiverInstanceType(map.instance_type())) {
    return {};
  }
  DCHECK(!map.IsPrimitiveMap() && map.IsJSReceiverMap());
  compiler::HeapObjectRef proto = map.prototype(broker());
  ++it;
  for (; it != possible_maps.end(); ++it) {
    map = *it;
    if (IsSpecialReceiverInstanceType(map.instance_type()) ||
        !proto.equals(map.prototype(broker()))) {
      return {};
    }
    DCHECK(!map.IsPrimitiveMap() && map.IsJSReceiverMap());
  }
  return GetConstant(proto);
}

MaybeReduceResult MaglevGraphBuilder::TryReduceObjectPrototypeGetProto(
    compiler::JSFunctionRef target, CallArguments& args) {
  if (args.count() != 0) return {};
  return TryReduceGetProto(args.receiver());
}

MaybeReduceResult MaglevGraphBuilder::TryReduceObjectGetPrototypeOf(
    compiler::JSFunctionRef target, CallArguments& args) {
  if (args.count() != 1) return {};
  return TryReduceGetProto(args[0]);
}

MaybeReduceResult MaglevGraphBuilder::TryReduceReflectGetPrototypeOf(
    compiler::JSFunctionRef target, CallArguments& args) {
  return TryReduceObjectGetPrototypeOf(target, args);
}

MaybeReduceResult MaglevGraphBuilder::TryReduceMathRound(
    compiler::JSFunctionRef target, CallArguments& args) {
  return DoTryReduceMathRound(args, Float64Round::Kind::kNearest);
}

MaybeReduceResult MaglevGraphBuilder::TryReduceNumberParseInt(
    compiler::JSFunctionRef target, CallArguments& args) {
  if (args.count() == 0) {
    return GetRootConstant(RootIndex::kNanValue);
  }
  if (args.count() != 1) {
    if (RootConstant* root_cst = args[1]->TryCast<RootConstant>()) {
      if (root_cst->index() != RootIndex::kUndefinedValue) {
        return {};
      }
    } else if (SmiConstant* smi_cst = args[1]->TryCast<SmiConstant>()) {
      if (smi_cst->value().value() != 10 && smi_cst->value().value() != 0) {
        return {};
      }
    } else {
      return {};
    }
  }

  ValueNode* arg = args[0];

  switch (arg->value_representation()) {
    case ValueRepresentation::kUint32:
    case ValueRepresentation::kInt32:
    case ValueRepresentation::kIntPtr:
      return arg;
    case ValueRepresentation::kTagged:
      switch (CheckTypes(arg, {NodeType::kSmi})) {
        case NodeType::kSmi:
          return arg;
        default:
          // TODO(verwaest): Support actually parsing strings, converting
          // doubles to ints, ...
          return {};
      }
    case ValueRepresentation::kFloat64:
    case ValueRepresentation::kHoleyFloat64:
      return {};
  }
}

MaybeReduceResult MaglevGraphBuilder::TryReduceMathAbs(
    compiler::JSFunctionRef target, CallArguments& args) {
  if (args.count() == 0) {
    return GetRootConstant(RootIndex::kNanValue);
  }
  ValueNode* arg = args[0];

  switch (arg->value_representation()) {
    case ValueRepresentation::kUint32:
    case ValueRepresentation::kIntPtr:
      // TODO(388844115): Rename IntPtr to make it clear it's non-negative.
      return arg;
    case ValueRepresentation::kInt32:
      if (!CanSpeculateCall()) return {};
      return AddNewNode<Int32AbsWithOverflow>({arg});
    case ValueRepresentation::kTagged:
      switch (CheckTypes(arg, {NodeType::kSmi, NodeType::kNumberOrOddball})) {
        case NodeType::kSmi:
          if (!CanSpeculateCall()) return {};
          return AddNewNode<Int32AbsWithOverflow>({arg});
        case NodeType::kNumberOrOddball:
          return AddNewNode<Float64Abs>({GetHoleyFloat64ForToNumber(
              arg, NodeType::kNumberOrOddball,
              TaggedToFloat64ConversionType::kNumberOrOddball)});
        // TODO(verwaest): Add support for ToNumberOrNumeric and deopt.
        default:
          break;
      }
      break;
    case ValueRepresentation::kFloat64:
    case ValueRepresentation::kHoleyFloat64:
      return AddNewNode<Float64Abs>({arg});
  }
  return {};
}

MaybeReduceResult MaglevGraphBuilder::TryReduceMathFloor(
    compiler::JSFunctionRef target, CallArguments& args) {
  return DoTryReduceMathRound(args, Float64Round::Kind::kFloor);
}

MaybeReduceResult MaglevGraphBuilder::TryReduceMathCeil(
    compiler::JSFunctionRef target, CallArguments& args) {
  return DoTryReduceMathRound(args, Float64Round::Kind::kCeil);
}

MaybeReduceResult MaglevGraphBuilder::DoTryReduceMathRound(
    CallArguments& args, Float64Round::Kind kind) {
  if (args.count() == 0) {
    return GetRootConstant(RootIndex::kNanValue);
  }
  ValueNode* arg = args[0];
  auto arg_repr = arg->value_representation();
  if (arg_repr == ValueRepresentation::kInt32 ||
      arg_repr == ValueRepresentation::kUint32) {
    return arg;
  }
  if (CheckType(arg, NodeType::kSmi)) return arg;
  if (!IsSupported(CpuOperation::kFloat64Round)) {
    return {};
  }
  if (arg_repr == ValueRepresentation::kFloat64 ||
      arg_repr == ValueRepresentation::kHoleyFloat64) {
    return AddNewNode<Float64Round>({arg}, kind);
  }
  DCHECK_EQ(arg_repr, ValueRepresentation::kTagged);
  if (CheckType(arg, NodeType::kNumberOrOddball)) {
    return AddNewNode<Float64Round>(
        {GetHoleyFloat64ForToNumber(
            arg, NodeType::kNumberOrOddball,
            TaggedToFloat64ConversionType::kNumberOrOddball)},
        kind);
  }
  if (!CanSpeculateCall()) return {};
  DeoptFrameScope continuation_scope(this, Float64Round::continuation(kind));
  ToNumberOrNumeric* conversion =
      AddNewNode<ToNumberOrNumeric>({arg}, Object::Conversion::kToNumber);
  ValueNode* float64_value = AddNewNode<UncheckedNumberOrOddballToFloat64>(
      {conversion}, TaggedToFloat64ConversionType::kOnlyNumber);
  return AddNewNode<Float64Round>({float64_value}, kind);
}

MaybeReduceResult MaglevGraphBuilder::TryReduceArrayConstructor(
    compiler::JSFunctionRef target, CallArguments& args) {
  return TryReduceConstructArrayConstructor(target, args);
}

MaybeReduceResult MaglevGraphBuilder::TryReduceStringConstructor(
    compiler::JSFunctionRef target, CallArguments& args) {
  if (args.count() == 0) {
    return GetRootConstant(RootIndex::kempty_string);
  }

  return BuildToString(args[0], ToString::kConvertSymbol);
}

MaybeReduceResult MaglevGraphBuilder::TryReduceMathPow(
    compiler::JSFunctionRef target, CallArguments& args) {
  if (args.count() < 2) {
    // For < 2 args, we'll be calculating Math.Pow(arg[0], undefined), which is
    // ToNumber(arg[0]) ** NaN == NaN. So we can just return NaN.
    // However, if there is a single argument and it's tagged, we have to call
    // ToNumber on it before returning NaN, for side effects. This call could
    // lazy deopt, which would mean we'd need a continuation to actually set
    // the NaN return value... it's easier to just bail out, this should be
    // an uncommon case anyway.
    if (args.count() == 1 && args[0]->properties().is_tagged()) {
      return {};
    }
    return GetRootConstant(RootIndex::kNanValue);
  }
  if (!CanSpeculateCall()) return {};
  // If both arguments are tagged, it is cheaper to call Math.Pow builtin,
  // instead of Float64Exponentiate, since we are still making a call and we
  // don't need to unbox both inputs. See https://crbug.com/1393643.
  if (args[0]->properties().is_tagged() && args[1]->properties().is_tagged()) {
    // The Math.pow call will be created in CallKnownJSFunction reduction.
    return {};
  }
  ValueNode* left = GetHoleyFloat64ForToNumber(
      args[0], NodeType::kNumber, TaggedToFloat64ConversionType::kOnlyNumber);
  ValueNode* right = GetHoleyFloat64ForToNumber(
      args[1], NodeType::kNumber, TaggedToFloat64ConversionType::kOnlyNumber);
  return AddNewNode<Float64Exponentiate>({left, right});
}

#define MATH_UNARY_IEEE_BUILTIN_REDUCER(MathName, ExtName, EnumName)          \
  MaybeReduceResult MaglevGraphBuilder::TryReduce##MathName(                  \
      compiler::JSFunctionRef target, CallArguments& args) {                  \
    if (args.count() < 1) {                                                   \
      return GetRootConstant(RootIndex::kNanValue);                           \
    }                                                                         \
    if (!CanSpeculateCall()) {                                                \
      ValueRepresentation rep = args[0]->properties().value_representation(); \
      if (rep == ValueRepresentation::kTagged ||                              \
          rep == ValueRepresentation::kHoleyFloat64) {                        \
        return {};                                                            \
      }                                                                       \
    }                                                                         \
    ValueNode* value =                                                        \
        GetFloat64ForToNumber(args[0], NodeType::kNumber,                     \
                              TaggedToFloat64ConversionType::kOnlyNumber);    \
    return AddNewNode<Float64Ieee754Unary>(                                   \
        {value}, Float64Ieee754Unary::Ieee754Function::k##EnumName);          \
  }

IEEE_754_UNARY_LIST(MATH_UNARY_IEEE_BUILTIN_REDUCER)
#undef MATH_UNARY_IEEE_BUILTIN_REDUCER

MaybeReduceResult MaglevGraphBuilder::TryReduceBuiltin(
    compiler::JSFunctionRef target, compiler::SharedFunctionInfoRef shared,
    CallArguments& args, const compiler::FeedbackSource& feedback_source) {
  if (args.mode() != CallArguments::kDefault) {
    // TODO(victorgomes): Maybe inline the spread stub? Or call known function
    // directly if arguments list is an array.
    return {};
  }
  SaveCallSpeculationScope speculate(this, feedback_source);
  if (!shared.HasBuiltinId()) return {};
  if (v8_flags.trace_maglev_graph_building) {
    std::cout << "  ! Trying to reduce builtin "
              << Builtins::name(shared.builtin_id()) << std::endl;
  }
  switch (shared.builtin_id()) {
#define CASE(Name, ...)  \
  case Builtin::k##Name: \
    return TryReduce##Name(target, args);
    MAGLEV_REDUCED_BUILTIN(CASE)
#undef CASE
    default:
      // TODO(v8:7700): Inline more builtins.
      return {};
  }
}

ValueNode* MaglevGraphBuilder::GetConvertReceiver(
    compiler::SharedFunctionInfoRef shared, const CallArguments& args) {
  if (shared.native() || shared.language_mode() == LanguageMode::kStrict) {
    if (args.receiver_mode() == ConvertReceiverMode::kNullOrUndefined) {
      return GetRootConstant(RootIndex::kUndefinedValue);
    } else {
      return args.receiver();
    }
  }
  if (args.receiver_mode() == ConvertReceiverMode::kNullOrUndefined) {
    return GetConstant(
        broker()->target_native_context().global_proxy_object(broker()));
  }
  ValueNode* receiver = args.receiver();
  if (CheckType(receiver, NodeType::kJSReceiver)) return receiver;
  if (compiler::OptionalHeapObjectRef maybe_constant =
          TryGetConstant(receiver)) {
    compiler::HeapObjectRef constant = maybe_constant.value();
    if (constant.IsNullOrUndefined()) {
      return GetConstant(
          broker()->target_native_context().global_proxy_object(broker()));
    }
  }
  return AddNewNode<ConvertReceiver>(
      {receiver}, broker()->target_native_context(), args.receiver_mode());
}

template <typename CallNode, typename... Args>
CallNode* MaglevGraphBuilder::AddNewCallNode(const CallArguments& args,
                                             Args&&... extra_args) {
  size_t input_count = args.count_with_receiver() + CallNode::kFixedInputCount;
  return AddNewNode<CallNode>(
      input_count,
      [&](CallNode* call) {
        int arg_index = 0;
        call->set_arg(arg_index++,
                      GetTaggedValue(GetValueOrUndefined(args.receiver())));
        for (size_t i = 0; i < args.count(); ++i) {
          call->set_arg(arg_index++, GetTaggedValue(args[i]));
        }
      },
      std::forward<Args>(extra_args)...);
}

ValueNode* MaglevGraphBuilder::BuildGenericCall(ValueNode* target,
                                                Call::TargetType target_type,
                                                const CallArguments& args) {
  // TODO(victorgomes): We do not collect call feedback from optimized/inlined
  // calls. In order to be consistent, we don't pass the feedback_source to the
  // IR, so that we avoid collecting for generic calls as well. We might want to
  // revisit this in the future.
  switch (args.mode()) {
    case CallArguments::kDefault:
      return AddNewCallNode<Call>(args, args.receiver_mode(), target_type,
                                  GetTaggedValue(target),
                                  GetTaggedValue(GetContext()));
    case CallArguments::kWithSpread:
      DCHECK_EQ(args.receiver_mode(), ConvertReceiverMode::kAny);
      return AddNewCallNode<CallWithSpread>(args, GetTaggedValue(target),
                                            GetTaggedValue(GetContext()));
    case CallArguments::kWithArrayLike:
      DCHECK_EQ(args.receiver_mode(), ConvertReceiverMode::kAny);
      // We don't use AddNewCallNode here, because the number of required
      // arguments is known statically.
      return AddNewNode<CallWithArrayLike>(
          {target, GetValueOrUndefined(args.receiver()), args[0],
           GetContext()});
  }
}

ValueNode* MaglevGraphBuilder::BuildCallSelf(
    ValueNode* context, ValueNode* function, ValueNode* new_target,
    compiler::SharedFunctionInfoRef shared, CallArguments& args) {
  ValueNode* receiver = GetConvertReceiver(shared, args);
  size_t input_count = args.count() + CallSelf::kFixedInputCount;
  graph()->set_has_recursive_calls(true);
  DCHECK_EQ(
      compilation_unit_->info()->toplevel_compilation_unit()->parameter_count(),
      shared.internal_formal_parameter_count_with_receiver());
  return AddNewNode<CallSelf>(
      input_count,
      [&](CallSelf* call) {
        for (int i = 0; i < static_cast<int>(args.count()); i++) {
          call->set_arg(i, GetTaggedValue(args[i]));
        }
      },
      compilation_unit_->info()->toplevel_compilation_unit()->parameter_count(),
      GetTaggedValue(function), GetTaggedValue(context),
      GetTaggedValue(receiver), GetTaggedValue(new_target));
}

bool MaglevGraphBuilder::TargetIsCurrentCompilingUnit(
    compiler::JSFunctionRef target) {
  if (compilation_unit_->info()->specialize_to_function_context()) {
    return target.object().equals(
        compilation_unit_->info()->toplevel_function());
  }
  return target.object()->shared() ==
         compilation_unit_->info()->toplevel_function()->shared();
}

MaybeReduceResult MaglevGraphBuilder::TryReduceCallForApiFunction(
    compiler::FunctionTemplateInfoRef api_callback,
    compiler::OptionalSharedFunctionInfoRef maybe_shared,
    compiler::OptionalJSObjectRef api_holder, CallArguments& args) {
  if (args.mode() != CallArguments::kDefault) {
    // TODO(victorgomes): Maybe inline the spread stub? Or call known function
    // directly if arguments list is an array.
    return {};
  }
  // Check if the function has an associated C++ code to execute.
  compiler::OptionalObjectRef maybe_callback_data =
      api_callback.callback_data(broker());
  if (!maybe_callback_data.has_value()) {
    // TODO(ishell): consider generating "return undefined" for empty function
    // instead of failing.
    return {};
  }

  size_t input_count = args.count() + CallKnownApiFunction::kFixedInputCount;
  ValueNode* receiver;
  if (maybe_shared.has_value()) {
    receiver = GetConvertReceiver(maybe_shared.value(), args);
  } else {
    receiver = args.receiver();
    CHECK_NOT_NULL(receiver);
  }

  CallKnownApiFunction::Mode mode =
      broker()->dependencies()->DependOnNoProfilingProtector()
          ? (v8_flags.maglev_inline_api_calls
                 ? CallKnownApiFunction::kNoProfilingInlined
                 : CallKnownApiFunction::kNoProfiling)
          : CallKnownApiFunction::kGeneric;

  return AddNewNode<CallKnownApiFunction>(
      input_count,
      [&](CallKnownApiFunction* call) {
        for (int i = 0; i < static_cast<int>(args.count()); i++) {
          call->set_arg(i, GetTaggedValue(args[i]));
        }
      },
      mode, api_callback, api_holder, GetTaggedValue(GetContext()),
      GetTaggedValue(receiver));
}

MaybeReduceResult MaglevGraphBuilder::TryBuildCallKnownApiFunction(
    compiler::JSFunctionRef function, compiler::SharedFunctionInfoRef shared,
    CallArguments& args) {
  compiler::OptionalFunctionTemplateInfoRef maybe_function_template_info =
      shared.function_template_info(broker());
  if (!maybe_function_template_info.has_value()) {
    // Not an Api function.
    return {};
  }

  // See if we can optimize this API call.
  compiler::FunctionTemplateInfoRef function_template_info =
      maybe_function_template_info.value();

  compiler::HolderLookupResult api_holder;
  if (function_template_info.accept_any_receiver() &&
      function_template_info.is_signature_undefined(broker())) {
    // We might be able to optimize the API call depending on the
    // {function_template_info}.
    // If the API function accepts any kind of {receiver}, we only need to
    // ensure that the {receiver} is actually a JSReceiver at this point,
    // and also pass that as the {holder}. There are two independent bits
    // here:
    //
    //  a. When the "accept any receiver" bit is set, it means we don't
    //     need to perform access checks, even if the {receiver}'s map
    //     has the "needs access check" bit set.
    //  b. When the {function_template_info} has no signature, we don't
    //     need to do the compatible receiver check, since all receivers
    //     are considered compatible at that point, and the {receiver}
    //     will be pass as the {holder}.

    api_holder =
        compiler::HolderLookupResult{CallOptimization::kHolderIsReceiver};
  } else {
    // Try to infer API holder from the known aspects of the {receiver}.
    api_holder =
        TryInferApiHolderValue(function_template_info, args.receiver());
  }

  switch (api_holder.lookup) {
    case CallOptimization::kHolderIsReceiver:
    case CallOptimization::kHolderFound:
      return TryReduceCallForApiFunction(function_template_info, shared,
                                         api_holder.holder, args);

    case CallOptimization::kHolderNotFound:
      break;
  }

  // We don't have enough information to eliminate the access check
  // and/or the compatible receiver check, so use the generic builtin
  // that does those checks dynamically. This is still significantly
  // faster than the generic call sequence.
  Builtin builtin_name;
  // TODO(ishell): create no-profiling versions of kCallFunctionTemplate
  // builtins and use them here based on DependOnNoProfilingProtector()
  // dependency state.
  if (function_template_info.accept_any_receiver()) {
    DCHECK(!function_template_info.is_signature_undefined(broker()));
    builtin_name = Builtin::kCallFunctionTemplate_CheckCompatibleReceiver;
  } else if (function_template_info.is_signature_undefined(broker())) {
    builtin_name = Builtin::kCallFunctionTemplate_CheckAccess;
  } else {
    builtin_name =
        Builtin::kCallFunctionTemplate_CheckAccessAndCompatibleReceiver;
  }

  // The CallFunctionTemplate builtin requires the {receiver} to be
  // an actual JSReceiver, so make sure we do the proper conversion
  // first if necessary.
  ValueNode* receiver = GetConvertReceiver(shared, args);
  int kContext = 1;
  int kFunctionTemplateInfo = 1;
  int kArgc = 1;
  return AddNewNode<CallBuiltin>(
      kFunctionTemplateInfo + kArgc + kContext + args.count_with_receiver(),
      [&](CallBuiltin* call_builtin) {
        int arg_index = 0;
        call_builtin->set_arg(arg_index++, GetConstant(function_template_info));
        call_builtin->set_arg(
            arg_index++,
            GetInt32Constant(JSParameterCount(static_cast<int>(args.count()))));

        call_builtin->set_arg(arg_index++, GetTaggedValue(receiver));
        for (int i = 0; i < static_cast<int>(args.count()); i++) {
          call_builtin->set_arg(arg_index++, GetTaggedValue(args[i]));
        }
      },
      builtin_name, GetTaggedValue(GetContext()));
}

MaybeReduceResult MaglevGraphBuilder::TryBuildCallKnownJSFunction(
    compiler::JSFunctionRef function, ValueNode* new_target,
    CallArguments& args, const compiler::FeedbackSource& feedback_source) {
  // Don't inline CallFunction stub across native contexts.
  if (function.native_context(broker()) != broker()->target_native_context()) {
    return {};
  }
  compiler::SharedFunctionInfoRef shared = function.shared(broker());
  RETURN_IF_DONE(TryBuildCallKnownApiFunction(function, shared, args));

  ValueNode* closure = GetConstant(function);
  compiler::ContextRef context = function.context(broker());
  ValueNode* context_node = GetConstant(context);
  MaybeReduceResult res;
  if (MaglevIsTopTier() && TargetIsCurrentCompilingUnit(function) &&
      !graph_->is_osr()) {
    DCHECK(!shared.HasBuiltinId());
    res = BuildCallSelf(context_node, closure, new_target, shared, args);
  } else {
    res = TryBuildCallKnownJSFunction(
        context_node, closure, new_target,
#ifdef V8_ENABLE_LEAPTIERING
        function.dispatch_handle(),
#endif
        shared, function.feedback_vector(broker()), args, feedback_source);
  }
  return res;
}

MaybeReduceResult MaglevGraphBuilder::TryBuildCallKnownJSFunction(
    ValueNode* context, ValueNode* function, ValueNode* new_target,
#ifdef V8_ENABLE_LEAPTIERING
    JSDispatchHandle dispatch_handle,
#endif
    compiler::SharedFunctionInfoRef shared,
    compiler::OptionalFeedbackVectorRef feedback_vector, CallArguments& args,
    const compiler::FeedbackSource& feedback_source) {
  if (v8_flags.maglev_inlining) {
    RETURN_IF_DONE(TryBuildInlinedCall(context, function, new_target, shared,
                                       feedback_vector, args, feedback_source));
  }
  ValueNode* receiver = GetConvertReceiver(shared, args);
  size_t input_count = args.count() + CallKnownJSFunction::kFixedInputCount;
  return AddNewNode<CallKnownJSFunction>(
      input_count,
      [&](CallKnownJSFunction* call) {
        for (int i = 0; i < static_cast<int>(args.count()); i++) {
          call->set_arg(i, GetTaggedValue(args[i]));
        }
      },
#ifdef V8_ENABLE_LEAPTIERING
      dispatch_handle,
#endif
      shared, GetTaggedValue(function), GetTaggedValue(context),
      GetTaggedValue(receiver), GetTaggedValue(new_target));
}

ReduceResult MaglevGraphBuilder::BuildCheckValue(ValueNode* node,
                                                 compiler::HeapObjectRef ref) {
  DCHECK(!ref.IsSmi());
  DCHECK(!ref.IsHeapNumber());

  if (!IsInstanceOfNodeType(ref.map(broker()), GetType(node), broker())) {
    return EmitUnconditionalDeopt(DeoptimizeReason::kValueMismatch);
  }
  if (compiler::OptionalHeapObjectRef maybe_constant = TryGetConstant(node)) {
    if (maybe_constant.value().equals(ref)) {
      return ReduceResult::Done();
    }
    return EmitUnconditionalDeopt(DeoptimizeReason::kUnknown);
  }
  if (ref.IsString()) {
    DCHECK(ref.IsInternalizedString());
    AddNewNode<CheckValueEqualsString>({node}, ref.AsInternalizedString());
    SetKnownValue(node, ref, NodeType::kString);
  } else {
    AddNewNode<CheckValue>({node}, ref);
    SetKnownValue(node, ref, StaticTypeForConstant(broker(), ref));
  }

  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::BuildCheckValue(ValueNode* node,
                                                 compiler::ObjectRef ref) {
  if (ref.IsHeapObject() && !ref.IsHeapNumber()) {
    return BuildCheckValue(node, ref.AsHeapObject());
  }
  return BuildCheckNumericalValue(node, ref);
}

ReduceResult MaglevGraphBuilder::BuildCheckNumericalValue(
    ValueNode* node, compiler::ObjectRef ref) {
  DCHECK(ref.IsSmi() || ref.IsHeapNumber());
  if (ref.IsSmi()) {
    int ref_value = ref.AsSmi();
    if (IsConstantNode(node->opcode())) {
      if (node->Is<SmiConstant>() &&
          node->Cast<SmiConstant>()->value().value() == ref_value) {
        return ReduceResult::Done();
      }
      if (node->Is<Int32Constant>() &&
          node->Cast<Int32Constant>()->value() == ref_value) {
        return ReduceResult::Done();
      }
      return EmitUnconditionalDeopt(DeoptimizeReason::kUnknown);
    }
    if (NodeTypeIs(GetType(node), NodeType::kAnyHeapObject)) {
      return EmitUnconditionalDeopt(DeoptimizeReason::kValueMismatch);
    }
    AddNewNode<CheckValueEqualsInt32>({node}, ref_value);
  } else {
    DCHECK(ref.IsHeapNumber());
    Float64 ref_value = Float64::FromBits(ref.AsHeapNumber().value_as_bits());
    DCHECK(!ref_value.is_hole_nan());
    if (node->Is<Float64Constant>()) {
      Float64 f64 = node->Cast<Float64Constant>()->value();
      DCHECK(!f64.is_hole_nan());
      if (f64 == ref_value) {
        return ReduceResult::Done();
      }
      return EmitUnconditionalDeopt(DeoptimizeReason::kUnknown);
    } else if (compiler::OptionalHeapObjectRef constant =
                   TryGetConstant(node)) {
      if (constant.value().IsHeapNumber()) {
        Float64 f64 =
            Float64::FromBits(constant.value().AsHeapNumber().value_as_bits());
        DCHECK(!f64.is_hole_nan());
        if (f64 == ref_value) {
          return ReduceResult::Done();
        }
      }
      return EmitUnconditionalDeopt(DeoptimizeReason::kUnknown);
    }
    if (!NodeTypeIs(NodeType::kNumber, GetType(node))) {
      return EmitUnconditionalDeopt(DeoptimizeReason::kValueMismatch);
    }
    if (ref_value.is_nan()) {
      AddNewNode<CheckFloat64IsNan>({node});
    } else {
      AddNewNode<CheckValueEqualsFloat64>({node}, ref_value);
    }
  }

  SetKnownValue(node, ref, NodeType::kNumber);
  return ReduceResult::Done();
}

ValueNode* MaglevGraphBuilder::BuildConvertHoleToUndefined(ValueNode* node) {
  if (!node->is_tagged()) return node;
  compiler::OptionalHeapObjectRef maybe_constant = TryGetConstant(node);
  if (maybe_constant) {
    return maybe_constant.value().IsTheHole()
               ? GetRootConstant(RootIndex::kUndefinedValue)
               : node;
  }
  return AddNewNode<ConvertHoleToUndefined>({node});
}

ReduceResult MaglevGraphBuilder::BuildCheckNotHole(ValueNode* node) {
  if (!node->is_tagged()) return ReduceResult::Done();
  compiler::OptionalHeapObjectRef maybe_constant = TryGetConstant(node);
  if (maybe_constant) {
    if (maybe_constant.value().IsTheHole()) {
      return EmitUnconditionalDeopt(DeoptimizeReason::kHole);
    }
    return ReduceResult::Done();
  }
  AddNewNode<CheckNotHole>({node});
  return ReduceResult::Done();
}

MaybeReduceResult MaglevGraphBuilder::TryReduceCallForConstant(
    compiler::JSFunctionRef target, CallArguments& args,
    const compiler::FeedbackSource& feedback_source) {
  if (args.mode() != CallArguments::kDefault) {
    // TODO(victorgomes): Maybe inline the spread stub? Or call known function
    // directly if arguments list is an array.
    return {};
  }
  compiler::SharedFunctionInfoRef shared = target.shared(broker());
  ValueNode* target_node = GetConstant(target);
  // Do not reduce calls to functions with break points.
  if (!shared.HasBreakInfo(broker())) {
    if (IsClassConstructor(shared.kind())) {
      // If we have a class constructor, we should raise an exception.
      return BuildCallRuntime(Runtime::kThrowConstructorNonCallableError,
                              {target_node});
    }
    DCHECK(IsCallable(*target.object()));
    RETURN_IF_DONE(TryReduceBuiltin(target, shared, args, feedback_source));
    RETURN_IF_DONE(TryBuildCallKnownJSFunction(
        target, GetRootConstant(RootIndex::kUndefinedValue), args,
        feedback_source));
  }
  return BuildGenericCall(target_node, Call::TargetType::kJSFunction, args);
}

compiler::HolderLookupResult MaglevGraphBuilder::TryInferApiHolderValue(
    compiler::FunctionTemplateInfoRef function_template_info,
    ValueNode* receiver) {
  const compiler::HolderLookupResult not_found;

  auto receiver_info = known_node_aspects().TryGetInfoFor(receiver);
  if (!receiver_info || !receiver_info->possible_maps_are_known()) {
    // No info about receiver, can't infer API holder.
    return not_found;
  }
  DCHECK(!receiver_info->possible_maps().is_empty());
  compiler::MapRef first_receiver_map = receiver_info->possible_maps()[0];

  // See if we can constant-fold the compatible receiver checks.
  compiler::HolderLookupResult api_holder =
      function_template_info.LookupHolderOfExpectedType(broker(),
                                                        first_receiver_map);
  if (api_holder.lookup == CallOptimization::kHolderNotFound) {
    // Can't infer API holder.
    return not_found;
  }

  // Check that all {receiver_maps} are actually JSReceiver maps and
  // that the {function_template_info} accepts them without access
  // checks (even if "access check needed" is set for {receiver}).
  //
  // API holder might be a receivers's hidden prototype (i.e. the receiver is
  // a global proxy), so in this case the map check or stability dependency on
  // the receiver guard us from detaching a global object from global proxy.
  CHECK(first_receiver_map.IsJSReceiverMap());
  CHECK(!first_receiver_map.is_access_check_needed() ||
        function_template_info.accept_any_receiver());

  for (compiler::MapRef receiver_map : receiver_info->possible_maps()) {
    compiler::HolderLookupResult holder_i =
        function_template_info.LookupHolderOfExpectedType(broker(),
                                                          receiver_map);

    if (api_holder.lookup != holder_i.lookup) {
      // Different API holders, dynamic lookup is required.
      return not_found;
    }
    DCHECK(holder_i.lookup == CallOptimization::kHolderFound ||
           holder_i.lookup == CallOptimization::kHolderIsReceiver);
    if (holder_i.lookup == CallOptimization::kHolderFound) {
      DCHECK(api_holder.holder.has_value() && holder_i.holder.has_value());
      if (!api_holder.holder->equals(*holder_i.holder)) {
        // Different API holders, dynamic lookup is required.
        return not_found;
      }
    }

    CHECK(receiver_map.IsJSReceiverMap());
    CHECK(!receiver_map.is_access_check_needed() ||
          function_template_info.accept_any_receiver());
  }
  return api_holder;
}

MaybeReduceResult MaglevGraphBuilder::TryReduceCallForTarget(
    ValueNode* target_node, compiler::JSFunctionRef target, CallArguments& args,
    const compiler::FeedbackSource& feedback_source) {
  RETURN_IF_ABORT(BuildCheckValue(target_node, target));
  return TryReduceCallForConstant(target, args, feedback_source);
}

MaybeReduceResult MaglevGraphBuilder::TryReduceCallForNewClosure(
    ValueNode* target_node, ValueNode* target_context,
#ifdef V8_ENABLE_LEAPTIERING
    JSDispatchHandle dispatch_handle,
#endif
    compiler::SharedFunctionInfoRef shared,
    compiler::OptionalFeedbackVectorRef feedback_vector, CallArguments& args,
    const compiler::FeedbackSource& feedback_source) {
  // Do not reduce calls to functions with break points.
  if (args.mode() != CallArguments::kDefault) {
    // TODO(victorgomes): Maybe inline the spread stub? Or call known function
    // directly if arguments list is an array.
    return {};
  }
  if (!shared.HasBreakInfo(broker())) {
    if (IsClassConstructor(shared.kind())) {
      // If we have a class constructor, we should raise an exception.
      return BuildCallRuntime(Runtime::kThrowConstructorNonCallableError,
                              {target_node});
    }
    RETURN_IF_DONE(TryBuildCallKnownJSFunction(
        target_context, target_node,
        GetRootConstant(RootIndex::kUndefinedValue),
#ifdef V8_ENABLE_LEAPTIERING
        dispatch_handle,
#endif
        shared, feedback_vector, args, feedback_source));
  }
  return BuildGenericCall(target_node, Call::TargetType::kJSFunction, args);
}

MaybeReduceResult
MaglevGraphBuilder::TryReduceFunctionPrototypeApplyCallWithReceiver(
    compiler::OptionalHeapObjectRef maybe_receiver, CallArguments& args,
    const compiler::FeedbackSource& feedback_source) {
  if (args.mode() != CallArguments::kDefault) return {};

  ValueNode* function = GetValueOrUndefined(args.receiver());
  if (maybe_receiver.has_value()) {
    RETURN_IF_ABORT(BuildCheckValue(function, maybe_receiver.value()));
    function = GetConstant(maybe_receiver.value());
  }

  SaveCallSpeculationScope saved(this);
  if (args.count() == 0) {
    CallArguments empty_args(ConvertReceiverMode::kNullOrUndefined);
    return ReduceCall(function, empty_args, feedback_source);
  }
  auto build_call_only_with_new_receiver = [&] {
    CallArguments new_args(ConvertReceiverMode::kAny, {args[0]});
    return ReduceCall(function, new_args, feedback_source);
  };
  if (args.count() == 1 || IsNullValue(args[1]) || IsUndefinedValue(args[1])) {
    return build_call_only_with_new_receiver();
  }
  auto build_call_with_array_like = [&] {
    CallArguments new_args(ConvertReceiverMode::kAny, {args[0], args[1]},
                           CallArguments::kWithArrayLike);
    return ReduceCallWithArrayLike(function, new_args, feedback_source);
  };
  if (!MayBeNullOrUndefined(args[1])) {
    return build_call_with_array_like();
  }
  return SelectReduction(
      [&](auto& builder) {
        return BuildBranchIfUndefinedOrNull(builder, args[1]);
      },
      build_call_only_with_new_receiver, build_call_with_array_like);
}

ReduceResult MaglevGraphBuilder::BuildCallWithFeedback(
    ValueNode* target_node, CallArguments& args,
    const compiler::FeedbackSource& feedback_source) {
  const compiler::ProcessedFeedback& processed_feedback =
      broker()->GetFeedbackForCall(feedback_source);
  if (processed_feedback.IsInsufficient()) {
    return EmitUnconditionalDeopt(
        DeoptimizeReason::kInsufficientTypeFeedbackForCall);
  }

  DCHECK_EQ(processed_feedback.kind(), compiler::ProcessedFeedback::kCall);
  const compiler::CallFeedback& call_feedback = processed_feedback.AsCall();

  if (call_feedback.target().has_value() &&
      call_feedback.target()->IsJSFunction()) {
    CallFeedbackContent content = call_feedback.call_feedback_content();
    compiler::JSFunctionRef feedback_target =
        call_feedback.target()->AsJSFunction();
    if (content == CallFeedbackContent::kReceiver) {
      compiler::NativeContextRef native_context =
          broker()->target_native_context();
      compiler::JSFunctionRef apply_function =
          native_context.function_prototype_apply(broker());
      RETURN_IF_ABORT(BuildCheckValue(target_node, apply_function));
      PROCESS_AND_RETURN_IF_DONE(
          TryReduceFunctionPrototypeApplyCallWithReceiver(feedback_target, args,
                                                          feedback_source),
          SetAccumulator);
      feedback_target = apply_function;
    } else {
      DCHECK_EQ(CallFeedbackContent::kTarget, content);
    }
    RETURN_IF_ABORT(BuildCheckValue(target_node, feedback_target));
  }

  PROCESS_AND_RETURN_IF_DONE(ReduceCall(target_node, args, feedback_source),
                             SetAccumulator);
  UNREACHABLE();
}

ReduceResult MaglevGraphBuilder::ReduceCallWithArrayLikeForArgumentsObject(
    ValueNode* target_node, CallArguments& args,
    VirtualObject* arguments_object,
    const compiler::FeedbackSource& feedback_source) {
  DCHECK_EQ(args.mode(), CallArguments::kWithArrayLike);
  DCHECK(arguments_object->map().IsJSArgumentsObjectMap() ||
         arguments_object->map().IsJSArrayMap());
  args.PopArrayLikeArgument();
  ValueNode* elements_value =
      arguments_object->get(JSArgumentsObject::kElementsOffset);
  if (elements_value->Is<ArgumentsElements>()) {
    Call::TargetType target_type = Call::TargetType::kAny;
    // TODO(victorgomes): Add JSFunction node type in KNA and use the info here.
    if (compiler::OptionalHeapObjectRef maybe_constant =
            TryGetConstant(target_node)) {
      if (maybe_constant->IsJSFunction()) {
        compiler::SharedFunctionInfoRef shared =
            maybe_constant->AsJSFunction().shared(broker());
        if (!IsClassConstructor(shared.kind())) {
          target_type = Call::TargetType::kJSFunction;
        }
      }
    }
    int start_index = 0;
    if (elements_value->Cast<ArgumentsElements>()->type() ==
        CreateArgumentsType::kRestParameter) {
      start_index =
          elements_value->Cast<ArgumentsElements>()->formal_parameter_count();
    }
    return AddNewCallNode<CallForwardVarargs>(args, GetTaggedValue(target_node),
                                              GetTaggedValue(GetContext()),
                                              start_index, target_type);
  }

  if (elements_value->Is<RootConstant>()) {
    // It is a RootConstant, Elements can only be the empty fixed array.
    DCHECK_EQ(elements_value->Cast<RootConstant>()->index(),
              RootIndex::kEmptyFixedArray);
    CallArguments new_args(ConvertReceiverMode::kAny, {args.receiver()});
    return ReduceCall(target_node, new_args, feedback_source);
  }

  if (Constant* constant_value = elements_value->TryCast<Constant>()) {
    DCHECK(constant_value->object().IsFixedArray());
    compiler::FixedArrayRef elements = constant_value->object().AsFixedArray();
    base::SmallVector<ValueNode*, 8> arg_list;
    DCHECK_NOT_NULL(args.receiver());
    arg_list.push_back(args.receiver());
    for (int i = 0; i < static_cast<int>(args.count()); i++) {
      arg_list.push_back(args[i]);
    }
    for (uint32_t i = 0; i < elements.length(); i++) {
      arg_list.push_back(GetConstant(*elements.TryGet(broker(), i)));
    }
    CallArguments new_args(ConvertReceiverMode::kAny, std::move(arg_list));
    return ReduceCall(target_node, new_args, feedback_source);
  }

  DCHECK(elements_value->Is<InlinedAllocation>());
  InlinedAllocation* allocation = elements_value->Cast<InlinedAllocation>();
  VirtualObject* elements = allocation->object();

  base::SmallVector<ValueNode*, 8> arg_list;
  DCHECK_NOT_NULL(args.receiver());
  arg_list.push_back(args.receiver());
  for (int i = 0; i < static_cast<int>(args.count()); i++) {
    arg_list.push_back(args[i]);
  }
  DCHECK(elements->get(offsetof(FixedArray, length_))->Is<Int32Constant>());
  int length = elements->get(offsetof(FixedArray, length_))
                   ->Cast<Int32Constant>()
                   ->value();
  for (int i = 0; i < length; i++) {
    arg_list.push_back(elements->get(FixedArray::OffsetOfElementAt(i)));
  }
  CallArguments new_args(ConvertReceiverMode::kAny, std::move(arg_list));
  return ReduceCall(target_node, new_args, feedback_source);
}

namespace {
bool IsSloppyMappedArgumentsObject(compiler::JSHeapBroker* broker,
                                   compiler::MapRef map) {
  return broker->target_native_context()
      .fast_aliased_arguments_map(broker)
      .equals(map);
}
}  // namespace

std::optional<VirtualObject*>
MaglevGraphBuilder::TryGetNonEscapingArgumentsObject(ValueNode* value) {
  if (!value->Is<InlinedAllocation>()) return {};
  InlinedAllocation* alloc = value->Cast<InlinedAllocation>();
  // Although the arguments object has not been changed so far, since it is not
  // escaping, it could be modified after this bytecode if it is inside a loop.
  if (IsInsideLoop()) {
    if (!is_loop_effect_tracking() ||
        !loop_effects_->allocations.contains(alloc)) {
      return {};
    }
  }
  // TODO(victorgomes): We can probably loosen the IsNotEscaping requirement if
  // we keep track of the arguments object changes so far.
  if (alloc->IsEscaping()) return {};
  VirtualObject* object = alloc->object();
  // TODO(victorgomes): Support simple JSArray forwarding.
  compiler::MapRef map = object->map();
  // It is a rest parameter, if it is an array with ArgumentsElements node as
  // the elements array.
  if (map.IsJSArrayMap() && object->get(JSArgumentsObject::kElementsOffset)
                                ->Is<ArgumentsElements>()) {
    return object;
  }
  // TODO(victorgomes): We can loosen the IsSloppyMappedArgumentsObject
  // requirement if there is no stores to  the mapped arguments.
  if (map.IsJSArgumentsObjectMap() &&
      !IsSloppyMappedArgumentsObject(broker(), map)) {
    return object;
  }
  return {};
}

ReduceResult MaglevGraphBuilder::ReduceCallWithArrayLike(
    ValueNode* target_node, CallArguments& args,
    const compiler::FeedbackSource& feedback_source) {
  DCHECK_EQ(args.mode(), CallArguments::kWithArrayLike);

  // TODO(victorgomes): Add the case for JSArrays and Rest parameter.
  if (std::optional<VirtualObject*> arguments_object =
          TryGetNonEscapingArgumentsObject(args.array_like_argument())) {
    RETURN_IF_DONE(ReduceCallWithArrayLikeForArgumentsObject(
        target_node, args, *arguments_object, feedback_source));
  }

  // On fallthrough, create a generic call.
  return BuildGenericCall(target_node, Call::TargetType::kAny, args);
}

ReduceResult MaglevGraphBuilder::ReduceCall(
    ValueNode* target_node, CallArguments& args,
    const compiler::FeedbackSource& feedback_source) {
  if (compiler::OptionalHeapObjectRef maybe_constant =
          TryGetConstant(target_node)) {
    if (maybe_constant->IsJSFunction()) {
      MaybeReduceResult result = TryReduceCallForTarget(
          target_node, maybe_constant->AsJSFunction(), args, feedback_source);
      RETURN_IF_DONE(result);
    }
  }

  // If the implementation here becomes more complex, we could probably
  // deduplicate the code for FastCreateClosure and CreateClosure by using
  // templates or giving them a shared base class.
  if (FastCreateClosure* fast_create_closure =
          target_node->TryCast<FastCreateClosure>()) {
    MaybeReduceResult result = TryReduceCallForNewClosure(
        fast_create_closure, fast_create_closure->context().node(),
#ifdef V8_ENABLE_LEAPTIERING
        fast_create_closure->feedback_cell().dispatch_handle(),
#endif
        fast_create_closure->shared_function_info(),
        fast_create_closure->feedback_cell().feedback_vector(broker()), args,
        feedback_source);
    RETURN_IF_DONE(result);
  } else if (CreateClosure* create_closure =
                 target_node->TryCast<CreateClosure>()) {
    MaybeReduceResult result = TryReduceCallForNewClosure(
        create_closure, create_closure->context().node(),
#ifdef V8_ENABLE_LEAPTIERING
        create_closure->feedback_cell().dispatch_handle(),
#endif
        create_closure->shared_function_info(),
        create_closure->feedback_cell().feedback_vector(broker()), args,
        feedback_source);
    RETURN_IF_DONE(result);
  }

  // On fallthrough, create a generic call.
  return BuildGenericCall(target_node, Call::TargetType::kAny, args);
}

ReduceResult MaglevGraphBuilder::BuildCallFromRegisterList(
    ConvertReceiverMode receiver_mode) {
  ValueNode* target = LoadRegister(0);
  interpreter::RegisterList reg_list = iterator_.GetRegisterListOperand(1);
  FeedbackSlot slot = GetSlotOperand(3);
  compiler::FeedbackSource feedback_source(feedback(), slot);
  CallArguments args(receiver_mode, reg_list, current_interpreter_frame_);
  return BuildCallWithFeedback(target, args, feedback_source);
}

ReduceResult MaglevGraphBuilder::BuildCallFromRegisters(
    int arg_count, ConvertReceiverMode receiver_mode) {
  ValueNode* target = LoadRegister(0);
  const int receiver_count =
      (receiver_mode == ConvertReceiverMode::kNullOrUndefined) ? 0 : 1;
  const int reg_count = arg_count + receiver_count;
  FeedbackSlot slot = GetSlotOperand(reg_count + 1);
  compiler::FeedbackSource feedback_source(feedback(), slot);
  switch (reg_count) {
    case 0: {
      DCHECK_EQ(receiver_mode, ConvertReceiverMode::kNullOrUndefined);
      CallArguments args(receiver_mode);
      return BuildCallWithFeedback(target, args, feedback_source);
    }
    case 1: {
      CallArguments args(receiver_mode, {LoadRegister(1)});
      return BuildCallWithFeedback(target, args, feedback_source);
    }
    case 2: {
      CallArguments args(receiver_mode, {LoadRegister(1), LoadRegister(2)});
      return BuildCallWithFeedback(target, args, feedback_source);
    }
    case 3: {
      CallArguments args(receiver_mode,
                         {LoadRegister(1), LoadRegister(2), LoadRegister(3)});
      return BuildCallWithFeedback(target, args, feedback_source);
    }
    default:
      UNREACHABLE();
  }
}

ReduceResult MaglevGraphBuilder::VisitCallAnyReceiver() {
  return BuildCallFromRegisterList(ConvertReceiverMode::kAny);
}
ReduceResult MaglevGraphBuilder::VisitCallProperty() {
  return BuildCallFromRegisterList(ConvertReceiverMode::kNotNullOrUndefined);
}
ReduceResult MaglevGraphBuilder::VisitCallProperty0() {
  return BuildCallFromRegisters(0, ConvertReceiverMode::kNotNullOrUndefined);
}
ReduceResult MaglevGraphBuilder::VisitCallProperty1() {
  return BuildCallFromRegisters(1, ConvertReceiverMode::kNotNullOrUndefined);
}
ReduceResult MaglevGraphBuilder::VisitCallProperty2() {
  return BuildCallFromRegisters(2, ConvertReceiverMode::kNotNullOrUndefined);
}
ReduceResult MaglevGraphBuilder::VisitCallUndefinedReceiver() {
  return BuildCallFromRegisterList(ConvertReceiverMode::kNullOrUndefined);
}
ReduceResult MaglevGraphBuilder::VisitCallUndefinedReceiver0() {
  return BuildCallFromRegisters(0, ConvertReceiverMode::kNullOrUndefined);
}
ReduceResult MaglevGraphBuilder::VisitCallUndefinedReceiver1() {
  return BuildCallFromRegisters(1, ConvertReceiverMode::kNullOrUndefined);
}
ReduceResult MaglevGraphBuilder::VisitCallUndefinedReceiver2() {
  return BuildCallFromRegisters(2, ConvertReceiverMode::kNullOrUndefined);
}

ReduceResult MaglevGraphBuilder::VisitCallWithSpread() {
  ValueNode* function = LoadRegister(0);
  interpreter::RegisterList reglist = iterator_.GetRegisterListOperand(1);
  FeedbackSlot slot = GetSlotOperand(3);
  compiler::FeedbackSource feedback_source(feedback(), slot);
  CallArguments args(ConvertReceiverMode::kAny, reglist,
                     current_interpreter_frame_, CallArguments::kWithSpread);
  return BuildCallWithFeedback(function, args, feedback_source);
}

ReduceResult MaglevGraphBuilder::VisitCallRuntime() {
  Runtime::FunctionId function_id = iterator_.GetRuntimeIdOperand(0);
  interpreter::RegisterList args = iterator_.GetRegisterListOperand(1);
  ValueNode* context = GetContext();
  size_t input_count = args.register_count() + CallRuntime::kFixedInputCount;
  CallRuntime* call_runtime = AddNewNode<CallRuntime>(
      input_count,
      [&](CallRuntime* call_runtime) {
        for (int i = 0; i < args.register_count(); ++i) {
          call_runtime->set_arg(i, GetTaggedValue(args[i]));
        }
      },
      function_id, context);
  SetAccumulator(call_runtime);

  if (RuntimeFunctionCanThrow(function_id)) {
    return BuildAbort(AbortReason::kUnexpectedReturnFromThrow);
  }
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitCallJSRuntime() {
  // Get the function to call from the native context.
  compiler::NativeContextRef native_context = broker()->target_native_context();
  ValueNode* context = GetConstant(native_context);
  uint32_t slot = iterator_.GetNativeContextIndexOperand(0);
  ValueNode* callee =
      LoadAndCacheContextSlot(context, slot, kMutable, ContextKind::kDefault);
  // Call the function.
  interpreter::RegisterList reglist = iterator_.GetRegisterListOperand(1);
  CallArguments args(ConvertReceiverMode::kNullOrUndefined, reglist,
                     current_interpreter_frame_);
  SetAccumulator(BuildGenericCall(callee, Call::TargetType::kJSFunction, args));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitCallRuntimeForPair() {
  Runtime::FunctionId function_id = iterator_.GetRuntimeIdOperand(0);
  interpreter::RegisterList args = iterator_.GetRegisterListOperand(1);
  ValueNode* context = GetContext();

  size_t input_count = args.register_count() + CallRuntime::kFixedInputCount;
  CallRuntime* call_runtime = AddNewNode<CallRuntime>(
      input_count,
      [&](CallRuntime* call_runtime) {
        for (int i = 0; i < args.register_count(); ++i) {
          call_runtime->set_arg(i, GetTaggedValue(args[i]));
        }
      },
      function_id, context);
  auto result = iterator_.GetRegisterPairOperand(3);
  StoreRegisterPair(result, call_runtime);
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitInvokeIntrinsic() {
  // InvokeIntrinsic <function_id> <first_arg> <arg_count>
  Runtime::FunctionId intrinsic_id = iterator_.GetIntrinsicIdOperand(0);
  interpreter::RegisterList args = iterator_.GetRegisterListOperand(1);
  switch (intrinsic_id) {
#define CASE(Name, _, arg_count)                                         \
  case Runtime::kInline##Name:                                           \
    DCHECK_IMPLIES(arg_count != -1, arg_count == args.register_count()); \
    return VisitIntrinsic##Name(args);
    INTRINSICS_LIST(CASE)
#undef CASE
    default:
      UNREACHABLE();
  }
}

ReduceResult MaglevGraphBuilder::VisitIntrinsicCopyDataProperties(
    interpreter::RegisterList args) {
  DCHECK_EQ(args.register_count(), 2);
  SetAccumulator(BuildCallBuiltin<Builtin::kCopyDataProperties>(
      {GetTaggedValue(args[0]), GetTaggedValue(args[1])}));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::
    VisitIntrinsicCopyDataPropertiesWithExcludedPropertiesOnStack(
        interpreter::RegisterList args) {
  SmiConstant* excluded_property_count =
      GetSmiConstant(args.register_count() - 1);
  int kContext = 1;
  int kExcludedPropertyCount = 1;
  CallBuiltin* call_builtin = AddNewNode<CallBuiltin>(
      args.register_count() + kContext + kExcludedPropertyCount,
      [&](CallBuiltin* call_builtin) {
        int arg_index = 0;
        call_builtin->set_arg(arg_index++, GetTaggedValue(args[0]));
        call_builtin->set_arg(arg_index++, excluded_property_count);
        for (int i = 1; i < args.register_count(); i++) {
          call_builtin->set_arg(arg_index++, GetTaggedValue(args[i]));
        }
      },
      Builtin::kCopyDataPropertiesWithExcludedProperties,
      GetTaggedValue(GetContext()));
  SetAccumulator(call_builtin);
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitIntrinsicCreateIterResultObject(
    interpreter::RegisterList args) {
  DCHECK_EQ(args.register_count(), 2);
  ValueNode* value = current_interpreter_frame_.get(args[0]);
  ValueNode* done = current_interpreter_frame_.get(args[1]);
  compiler::MapRef map =
      broker()->target_native_context().iterator_result_map(broker());
  VirtualObject* iter_result = CreateJSIteratorResult(map, value, done);
  ValueNode* allocation =
      BuildInlinedAllocation(iter_result, AllocationType::kYoung);
  // TODO(leszeks): Don't eagerly clear the raw allocation, have the
  // next side effect clear it.
  ClearCurrentAllocationBlock();
  SetAccumulator(allocation);
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitIntrinsicCreateAsyncFromSyncIterator(
    interpreter::RegisterList args) {
  DCHECK_EQ(args.register_count(), 1);
  SetAccumulator(
      BuildCallBuiltin<Builtin::kCreateAsyncFromSyncIteratorBaseline>(
          {GetTaggedValue(args[0])}));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitIntrinsicCreateJSGeneratorObject(
    interpreter::RegisterList args) {
  DCHECK_EQ(args.register_count(), 2);
  ValueNode* closure = current_interpreter_frame_.get(args[0]);
  ValueNode* receiver = current_interpreter_frame_.get(args[1]);
  PROCESS_AND_RETURN_IF_DONE(
      TryBuildAndAllocateJSGeneratorObject(closure, receiver), SetAccumulator);
  SetAccumulator(BuildCallBuiltin<Builtin::kCreateGeneratorObject>(
      {GetTaggedValue(closure), GetTaggedValue(receiver)}));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitIntrinsicGeneratorGetResumeMode(
    interpreter::RegisterList args) {
  DCHECK_EQ(args.register_count(), 1);
  ValueNode* generator = current_interpreter_frame_.get(args[0]);
  SetAccumulator(
      BuildLoadTaggedField(generator, JSGeneratorObject::kResumeModeOffset));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitIntrinsicGeneratorClose(
    interpreter::RegisterList args) {
  DCHECK_EQ(args.register_count(), 1);
  ValueNode* generator = current_interpreter_frame_.get(args[0]);
  ValueNode* value = GetSmiConstant(JSGeneratorObject::kGeneratorClosed);
  BuildStoreTaggedFieldNoWriteBarrier(generator, value,
                                      JSGeneratorObject::kContinuationOffset,
                                      StoreTaggedMode::kDefault);
  SetAccumulator(GetRootConstant(RootIndex::kUndefinedValue));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitIntrinsicGetImportMetaObject(
    interpreter::RegisterList args) {
  DCHECK_EQ(args.register_count(), 0);
  SetAccumulator(BuildCallRuntime(Runtime::kGetImportMetaObject, {}).value());
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitIntrinsicAsyncFunctionAwait(
    interpreter::RegisterList args) {
  DCHECK_EQ(args.register_count(), 2);
  SetAccumulator(BuildCallBuiltin<Builtin::kAsyncFunctionAwait>(
      {GetTaggedValue(args[0]), GetTaggedValue(args[1])}));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitIntrinsicAsyncFunctionEnter(
    interpreter::RegisterList args) {
  DCHECK_EQ(args.register_count(), 2);
  SetAccumulator(BuildCallBuiltin<Builtin::kAsyncFunctionEnter>(
      {GetTaggedValue(args[0]), GetTaggedValue(args[1])}));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitIntrinsicAsyncFunctionReject(
    interpreter::RegisterList args) {
  DCHECK_EQ(args.register_count(), 2);
  SetAccumulator(BuildCallBuiltin<Builtin::kAsyncFunctionReject>(
      {GetTaggedValue(args[0]), GetTaggedValue(args[1])}));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitIntrinsicAsyncFunctionResolve(
    interpreter::RegisterList args) {
  DCHECK_EQ(args.register_count(), 2);
  SetAccumulator(BuildCallBuiltin<Builtin::kAsyncFunctionResolve>(
      {GetTaggedValue(args[0]), GetTaggedValue(args[1])}));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitIntrinsicAsyncGeneratorAwait(
    interpreter::RegisterList args) {
  DCHECK_EQ(args.register_count(), 2);
  SetAccumulator(BuildCallBuiltin<Builtin::kAsyncGeneratorAwait>(
      {GetTaggedValue(args[0]), GetTaggedValue(args[1])}));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitIntrinsicAsyncGeneratorReject(
    interpreter::RegisterList args) {
  DCHECK_EQ(args.register_count(), 2);
  SetAccumulator(BuildCallBuiltin<Builtin::kAsyncGeneratorReject>(
      {GetTaggedValue(args[0]), GetTaggedValue(args[1])}));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitIntrinsicAsyncGeneratorResolve(
    interpreter::RegisterList args) {
  DCHECK_EQ(args.register_count(), 3);
  SetAccumulator(BuildCallBuiltin<Builtin::kAsyncGeneratorResolve>(
      {GetTaggedValue(args[0]), GetTaggedValue(args[1]),
       GetTaggedValue(args[2])}));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitIntrinsicAsyncGeneratorYieldWithAwait(
    interpreter::RegisterList args) {
  DCHECK_EQ(args.register_count(), 2);
  SetAccumulator(BuildCallBuiltin<Builtin::kAsyncGeneratorYieldWithAwait>(
      {GetTaggedValue(args[0]), GetTaggedValue(args[1])}));
  return ReduceResult::Done();
}

ValueNode* MaglevGraphBuilder::BuildGenericConstruct(
    ValueNode* target, ValueNode* new_target, ValueNode* context,
    const CallArguments& args,
    const compiler::FeedbackSource& feedback_source) {
  size_t input_count = args.count_with_receiver() + Construct::kFixedInputCount;
  DCHECK_EQ(args.receiver_mode(), ConvertReceiverMode::kNullOrUndefined);
  return AddNewNode<Construct>(
      input_count,
      [&](Construct* construct) {
        int arg_index = 0;
        // Add undefined receiver.
        construct->set_arg(arg_index++,
                           GetRootConstant(RootIndex::kUndefinedValue));
        for (size_t i = 0; i < args.count(); i++) {
          construct->set_arg(arg_index++, GetTaggedValue(args[i]));
        }
      },
      feedback_source, GetTaggedValue(target), GetTaggedValue(new_target),
      GetTaggedValue(context));
}

ReduceResult MaglevGraphBuilder::BuildAndAllocateKeyValueArray(
    ValueNode* key, ValueNode* value) {
  VirtualObject* elements = CreateFixedArray(broker()->fixed_array_map(), 2);
  elements->set(FixedArray::OffsetOfElementAt(0), key);
  elements->set(FixedArray::OffsetOfElementAt(1), value);
  compiler::MapRef map =
      broker()->target_native_context().js_array_packed_elements_map(broker());
  VirtualObject* array;
  GET_VALUE_OR_ABORT(
      array, CreateJSArray(map, map.instance_size(), GetInt32Constant(2)));
  array->set(JSArray::kElementsOffset, elements);
  ValueNode* allocation = BuildInlinedAllocation(array, AllocationType::kYoung);
  // TODO(leszeks): Don't eagerly clear the raw allocation, have the
  // next side effect clear it.
  ClearCurrentAllocationBlock();
  return allocation;
}

ReduceResult MaglevGraphBuilder::BuildAndAllocateJSArray(
    compiler::MapRef map, ValueNode* length, ValueNode* elements,
    const compiler::SlackTrackingPrediction& slack_tracking_prediction,
    AllocationType allocation_type) {
  VirtualObject* array;
  GET_VALUE_OR_ABORT(
      array,
      CreateJSArray(map, slack_tracking_prediction.instance_size(), length));
  array->set(JSArray::kElementsOffset, elements);
  for (int i = 0; i < slack_tracking_prediction.inobject_property_count();
       i++) {
    array->set(map.GetInObjectPropertyOffset(i),
               GetRootConstant(RootIndex::kUndefinedValue));
  }
  ValueNode* allocation = BuildInlinedAllocation(array, allocation_type);
  // TODO(leszeks): Don't eagerly clear the raw allocation, have the
  // next side effect clear it.
  ClearCurrentAllocationBlock();
  return allocation;
}

ValueNode* MaglevGraphBuilder::BuildAndAllocateJSArrayIterator(
    ValueNode* array, IterationKind iteration_kind) {
  compiler::MapRef map =
      broker()->target_native_context().initial_array_iterator_map(broker());
  VirtualObject* iterator = CreateJSArrayIterator(map, array, iteration_kind);
  ValueNode* allocation =
      BuildInlinedAllocation(iterator, AllocationType::kYoung);
  // TODO(leszeks): Don't eagerly clear the raw allocation, have the
  // next side effect clear it.
  ClearCurrentAllocationBlock();
  return allocation;
}

MaybeReduceResult MaglevGraphBuilder::TryBuildAndAllocateJSGeneratorObject(
    ValueNode* closure, ValueNode* receiver) {
  compiler::OptionalHeapObjectRef maybe_constant = TryGetConstant(closure);
  if (!maybe_constant.has_value()) return {};
  if (!maybe_constant->IsJSFunction()) return {};
  compiler::JSFunctionRef function = maybe_constant->AsJSFunction();
  if (!function.has_initial_map(broker())) return {};

  // Create the register file.
  compiler::SharedFunctionInfoRef shared = function.shared(broker());
  DCHECK(shared.HasBytecodeArray());
  compiler::BytecodeArrayRef bytecode_array = shared.GetBytecodeArray(broker());
  int parameter_count_no_receiver = bytecode_array.parameter_count() - 1;
  int length = parameter_count_no_receiver + bytecode_array.register_count();
  if (FixedArray::SizeFor(length) > kMaxRegularHeapObjectSize) {
    return {};
  }
  auto undefined = GetRootConstant(RootIndex::kUndefinedValue);
  VirtualObject* register_file =
      CreateFixedArray(broker()->fixed_array_map(), length);
  for (int i = 0; i < length; i++) {
    register_file->set(FixedArray::OffsetOfElementAt(i), undefined);
  }

  // Create the JS[Async]GeneratorObject instance.
  compiler::SlackTrackingPrediction slack_tracking_prediction =
      broker()->dependencies()->DependOnInitialMapInstanceSizePrediction(
          function);
  compiler::MapRef initial_map = function.initial_map(broker());
  VirtualObject* generator = CreateJSGeneratorObject(
      initial_map, slack_tracking_prediction.instance_size(), GetContext(),
      closure, receiver, register_file);

  // Handle in-object properties.
  for (int i = 0; i < slack_tracking_prediction.inobject_property_count();
       i++) {
    generator->set(initial_map.GetInObjectPropertyOffset(i), undefined);
  }

  ValueNode* allocation =
      BuildInlinedAllocation(generator, AllocationType::kYoung);
  // TODO(leszeks): Don't eagerly clear the raw allocation, have the
  // next side effect clear it.
  ClearCurrentAllocationBlock();
  return allocation;
}

namespace {

compiler::OptionalMapRef GetArrayConstructorInitialMap(
    compiler::JSHeapBroker* broker, compiler::JSFunctionRef array_function,
    ElementsKind elements_kind, size_t argc, std::optional<int> maybe_length) {
  compiler::MapRef initial_map = array_function.initial_map(broker);
  if (argc == 1 && (!maybe_length.has_value() || *maybe_length > 0)) {
    // Constructing an Array via new Array(N) where N is an unsigned
    // integer, always creates a holey backing store.
    elements_kind = GetHoleyElementsKind(elements_kind);
  }
  return initial_map.AsElementsKind(broker, elements_kind);
}

}  // namespace

ValueNode* MaglevGraphBuilder::BuildElementsArray(int length) {
  if (length == 0) {
    return GetRootConstant(RootIndex::kEmptyFixedArray);
  }
  VirtualObject* elements =
      CreateFixedArray(broker()->fixed_array_map(), length);
  auto hole = GetRootConstant(RootIndex::kTheHoleValue);
  for (int i = 0; i < length; i++) {
    elements->set(FixedArray::OffsetOfElementAt(i), hole);
  }
  return elements;
}

MaybeReduceResult MaglevGraphBuilder::TryReduceConstructArrayConstructor(
    compiler::JSFunctionRef array_function, CallArguments& args,
    compiler::OptionalAllocationSiteRef maybe_allocation_site) {
  ElementsKind elements_kind =
      maybe_allocation_site.has_value()
          ? maybe_allocation_site->GetElementsKind()
          : array_function.initial_map(broker()).elements_kind();
  // TODO(victorgomes): Support double elements array.
  if (IsDoubleElementsKind(elements_kind)) return {};
  DCHECK(IsFastElementsKind(elements_kind));

  std::optional<int> maybe_length;
  if (args.count() == 1) {
    maybe_length = TryGetInt32Constant(args[0]);
  }
  compiler::OptionalMapRef maybe_initial_map = GetArrayConstructorInitialMap(
      broker(), array_function, elements_kind, args.count(), maybe_length);
  if (!maybe_initial_map.has_value()) return {};
  compiler::MapRef initial_map = maybe_initial_map.value();
  compiler::SlackTrackingPrediction slack_tracking_prediction =
      broker()->dependencies()->DependOnInitialMapInstanceSizePrediction(
          array_function);

  // Tells whether we are protected by either the {site} or a
  // protector cell to do certain speculative optimizations.
  bool can_inline_call = false;
  AllocationType allocation_type = AllocationType::kYoung;

  if (maybe_allocation_site) {
    can_inline_call = maybe_allocation_site->CanInlineCall();
    allocation_type =
        broker()->dependencies()->DependOnPretenureMode(*maybe_allocation_site);
    broker()->dependencies()->DependOnElementsKind(*maybe_allocation_site);
  } else {
    compiler::PropertyCellRef array_constructor_protector = MakeRef(
        broker(), local_isolate()->factory()->array_constructor_protector());
    array_constructor_protector.CacheAsProtector(broker());
    can_inline_call = array_constructor_protector.value(broker()).AsSmi() ==
                      Protectors::kProtectorValid;
  }

  if (args.count() == 0) {
    return BuildAndAllocateJSArray(
        initial_map, GetSmiConstant(0),
        BuildElementsArray(JSArray::kPreallocatedArrayElements),
        slack_tracking_prediction, allocation_type);
  }

  if (maybe_length.has_value() && *maybe_length >= 0 &&
      *maybe_length < JSArray::kInitialMaxFastElementArray) {
    return BuildAndAllocateJSArray(initial_map, GetSmiConstant(*maybe_length),
                                   BuildElementsArray(*maybe_length),
                                   slack_tracking_prediction, allocation_type);
  }

  // TODO(victorgomes): If we know the argument cannot be a number, we should
  // allocate an array with one element.
  // We don't know anything about the length, so we rely on the allocation
  // site to avoid deopt loops.
  if (args.count() == 1 && can_inline_call) {
    return SelectReduction(
        [&](auto& builder) {
          return BuildBranchIfInt32Compare(builder,
                                           Operation::kGreaterThanOrEqual,
                                           args[0], GetInt32Constant(0));
        },
        [&] {
          ValueNode* elements =
              AddNewNode<AllocateElementsArray>({args[0]}, allocation_type);
          return BuildAndAllocateJSArray(initial_map, args[0], elements,
                                         slack_tracking_prediction,
                                         allocation_type);
        },
        [&] {
          ValueNode* error = GetSmiConstant(
              static_cast<int>(MessageTemplate::kInvalidArrayLength));
          return BuildCallRuntime(Runtime::kThrowRangeError, {error});
        });
  }

  // TODO(victorgomes): Support the constructor with argument count larger
  // than 1.
  return {};
}

MaybeReduceResult MaglevGraphBuilder::TryReduceConstructBuiltin(
    compiler::JSFunctionRef builtin,
    compiler::SharedFunctionInfoRef shared_function_info, ValueNode* target,
    CallArguments& args) {
  // TODO(victorgomes): specialize more known constants builtin targets.
  switch (shared_function_info.builtin_id()) {
    case Builtin::kArrayConstructor: {
      RETURN_IF_DONE(TryReduceConstructArrayConstructor(builtin, args));
      break;
    }
    case Builtin::kObjectConstructor: {
      // If no value is passed, we can immediately lower to a simple
      // constructor.
      if (args.count() == 0) {
        RETURN_IF_ABORT(BuildCheckValue(target, builtin));
        ValueNode* result = BuildInlinedAllocation(CreateJSConstructor(builtin),
                                                   AllocationType::kYoung);
        // TODO(leszeks): Don't eagerly clear the raw allocation, have the
        // next side effect clear it.
        ClearCurrentAllocationBlock();
        return result;
      }
      break;
    }
    default:
      break;
  }
  return {};
}

MaybeReduceResult MaglevGraphBuilder::TryReduceConstructGeneric(
    compiler::JSFunctionRef function,
    compiler::SharedFunctionInfoRef shared_function_info, ValueNode* target,
    ValueNode* new_target, CallArguments& args,
    compiler::FeedbackSource& feedback_source) {
  RETURN_IF_ABORT(BuildCheckValue(target, function));

  int construct_arg_count = static_cast<int>(args.count());
  base::Vector<ValueNode*> construct_arguments_without_receiver =
      zone()->AllocateVector<ValueNode*>(construct_arg_count);
  for (int i = 0; i < construct_arg_count; i++) {
    construct_arguments_without_receiver[i] = args[i];
  }

  if (IsDerivedConstructor(shared_function_info.kind())) {
    ValueNode* implicit_receiver = GetRootConstant(RootIndex::kTheHoleValue);
    args.set_receiver(implicit_receiver);
    ValueNode* call_result;
    {
      DeoptFrameScope construct(this, implicit_receiver);
      MaybeReduceResult result = TryBuildCallKnownJSFunction(
          function, new_target, args, feedback_source);
      RETURN_IF_ABORT(result);
      call_result = result.value();
    }
    if (CheckType(call_result, NodeType::kJSReceiver)) return call_result;
    ValueNode* constant_node;
    if (compiler::OptionalHeapObjectRef maybe_constant =
            TryGetConstant(call_result, &constant_node)) {
      compiler::HeapObjectRef constant = maybe_constant.value();
      if (constant.IsJSReceiver()) return constant_node;
    }
    if (!call_result->properties().is_tagged()) {
      return BuildCallRuntime(Runtime::kThrowConstructorReturnedNonObject, {});
    }
    return AddNewNode<CheckDerivedConstructResult>({call_result});
  }

  // We do not create a construct stub lazy deopt frame, since
  // FastNewObject cannot fail if target is a JSFunction.
  ValueNode* implicit_receiver = nullptr;
  if (function.has_initial_map(broker())) {
    compiler::MapRef map = function.initial_map(broker());
    if (map.GetConstructor(broker()).equals(function)) {
      implicit_receiver = BuildInlinedAllocation(CreateJSConstructor(function),
                                                 AllocationType::kYoung);
      // TODO(leszeks): Don't eagerly clear the raw allocation, have the
      // next side effect clear it.
      ClearCurrentAllocationBlock();
    }
  }
  if (implicit_receiver == nullptr) {
    implicit_receiver = BuildCallBuiltin<Builtin::kFastNewObject>(
        {GetTaggedValue(target), GetTaggedValue(new_target)});
  }
  EnsureType(implicit_receiver, NodeType::kJSReceiver);

  args.set_receiver(implicit_receiver);
  ValueNode* call_result;
  {
    DeoptFrameScope construct(this, implicit_receiver);
    MaybeReduceResult result = TryBuildCallKnownJSFunction(
        function, new_target, args, feedback_source);
    RETURN_IF_ABORT(result);
    call_result = result.value();
  }
  if (CheckType(call_result, NodeType::kJSReceiver)) return call_result;
  if (!call_result->properties().is_tagged()) return implicit_receiver;
  ValueNode* constant_node;
  if (compiler::OptionalHeapObjectRef maybe_constant =
          TryGetConstant(call_result, &constant_node)) {
    compiler::HeapObjectRef constant = maybe_constant.value();
    DCHECK(CheckType(implicit_receiver, NodeType::kJSReceiver));
    if (constant.IsJSReceiver()) return constant_node;
    return implicit_receiver;
  }
  return AddNewNode<CheckConstructResult>({call_result, implicit_receiver});
}

MaybeReduceResult MaglevGraphBuilder::TryReduceConstruct(
    compiler::HeapObjectRef feedback_target, ValueNode* target,
    ValueNode* new_target, CallArguments& args,
    compiler::FeedbackSource& feedback_source) {
  DCHECK(!feedback_target.IsAllocationSite());
  if (!feedback_target.map(broker()).is_constructor()) {
    // TODO(victorgomes): Deal the case where target is not a constructor.
    return {};
  }

  if (target != new_target) return {};

  // TODO(v8:7700): Add fast paths for other callables.
  if (!feedback_target.IsJSFunction()) return {};
  compiler::JSFunctionRef function = feedback_target.AsJSFunction();

  // Do not inline constructors with break points.
  compiler::SharedFunctionInfoRef shared_function_info =
      function.shared(broker());
  if (shared_function_info.HasBreakInfo(broker())) {
    return {};
  }

  // Do not inline cross natives context.
  if (function.native_context(broker()) != broker()->target_native_context()) {
    return {};
  }

  if (args.mode() != CallArguments::kDefault) {
    // TODO(victorgomes): Maybe inline the spread stub? Or call known
    // function directly if arguments list is an array.
    return {};
  }

  if (shared_function_info.HasBuiltinId()) {
    RETURN_IF_DONE(TryReduceConstructBuiltin(function, shared_function_info,
                                             target, args));
  }

  if (shared_function_info.construct_as_builtin()) {
    // TODO(victorgomes): Inline JSBuiltinsConstructStub.
    return {};
  }

  return TryReduceConstructGeneric(function, shared_function_info, target,
                                   new_target, args, feedback_source);
}

ReduceResult MaglevGraphBuilder::BuildConstruct(
    ValueNode* target, ValueNode* new_target, CallArguments& args,
    compiler::FeedbackSource& feedback_source) {
  compiler::ProcessedFeedback const& processed_feedback =
      broker()->GetFeedbackForCall(feedback_source);
  if (processed_feedback.IsInsufficient()) {
    return EmitUnconditionalDeopt(
        DeoptimizeReason::kInsufficientTypeFeedbackForConstruct);
  }

  DCHECK_EQ(processed_feedback.kind(), compiler::ProcessedFeedback::kCall);
  compiler::OptionalHeapObjectRef feedback_target =
      processed_feedback.AsCall().target();
  if (feedback_target.has_value() && feedback_target->IsAllocationSite()) {
    // The feedback is an AllocationSite, which means we have called the
    // Array function and collected transition (and pretenuring) feedback
    // for the resulting arrays.
    compiler::JSFunctionRef array_function =
        broker()->target_native_context().array_function(broker());
    RETURN_IF_ABORT(BuildCheckValue(target, array_function));
    PROCESS_AND_RETURN_IF_DONE(
        TryReduceConstructArrayConstructor(array_function, args,
                                           feedback_target->AsAllocationSite()),
        SetAccumulator);
  } else {
    if (feedback_target.has_value()) {
      PROCESS_AND_RETURN_IF_DONE(
          TryReduceConstruct(feedback_target.value(), target, new_target, args,
                             feedback_source),
          SetAccumulator);
    }
    if (compiler::OptionalHeapObjectRef maybe_constant =
            TryGetConstant(target)) {
      PROCESS_AND_RETURN_IF_DONE(
          TryReduceConstruct(maybe_constant.value(), target, new_target, args,
                             feedback_source),
          SetAccumulator);
    }
  }
  ValueNode* context = GetContext();
  SetAccumulator(BuildGenericConstruct(target, new_target, context, args,
                                       feedback_source));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitConstruct() {
  ValueNode* new_target = GetAccumulator();
  ValueNode* target = LoadRegister(0);
  interpreter::RegisterList reg_list = iterator_.GetRegisterListOperand(1);
  FeedbackSlot slot = GetSlotOperand(3);
  compiler::FeedbackSource feedback_source{feedback(), slot};
  CallArguments args(ConvertReceiverMode::kNullOrUndefined, reg_list,
                     current_interpreter_frame_);
  return BuildConstruct(target, new_target, args, feedback_source);
}

ReduceResult MaglevGraphBuilder::VisitConstructWithSpread() {
  ValueNode* new_target = GetAccumulator();
  ValueNode* constructor = LoadRegister(0);
  interpreter::RegisterList args = iterator_.GetRegisterListOperand(1);
  ValueNode* context = GetContext();
  FeedbackSlot slot = GetSlotOperand(3);
  compiler::FeedbackSource feedback_source(feedback(), slot);

  int kReceiver = 1;
  size_t input_count =
      args.register_count() + kReceiver + ConstructWithSpread::kFixedInputCount;
  ConstructWithSpread* construct = AddNewNode<ConstructWithSpread>(
      input_count,
      [&](ConstructWithSpread* construct) {
        int arg_index = 0;
        // Add undefined receiver.
        construct->set_arg(arg_index++,
                           GetRootConstant(RootIndex::kUndefinedValue));
        for (int i = 0; i < args.register_count(); i++) {
          construct->set_arg(arg_index++, GetTaggedValue(args[i]));
        }
      },
      feedback_source, GetTaggedValue(constructor), GetTaggedValue(new_target),
      GetTaggedValue(context));
  SetAccumulator(construct);
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitConstructForwardAllArgs() {
  ValueNode* new_target = GetAccumulator();
  ValueNode* target = LoadRegister(0);
  FeedbackSlot slot = GetSlotOperand(1);
  compiler::FeedbackSource feedback_source{feedback(), slot};

  if (is_inline()) {
    base::SmallVector<ValueNode*, 8> forwarded_args(argument_count());
    for (int i = 1 /* skip receiver */; i < argument_count(); ++i) {
      forwarded_args[i] = GetInlinedArgument(i);
    }
    CallArguments args(ConvertReceiverMode::kNullOrUndefined,
                       std::move(forwarded_args));
    return BuildConstruct(target, new_target, args, feedback_source);
  } else {
    // TODO(syg): Add ConstructForwardAllArgs reductions and support inlining.
    SetAccumulator(
        BuildCallBuiltin<Builtin::kConstructForwardAllArgs_WithFeedback>(
            {GetTaggedValue(target), GetTaggedValue(new_target)},
            feedback_source));
    return ReduceResult::Done();
  }
}

ReduceResult MaglevGraphBuilder::VisitTestEqual() {
  return VisitCompareOperation<Operation::kEqual>();
}
ReduceResult MaglevGraphBuilder::VisitTestEqualStrict() {
  return VisitCompareOperation<Operation::kStrictEqual>();
}
ReduceResult MaglevGraphBuilder::VisitTestLessThan() {
  return VisitCompareOperation<Operation::kLessThan>();
}
ReduceResult MaglevGraphBuilder::VisitTestLessThanOrEqual() {
  return VisitCompareOperation<Operation::kLessThanOrEqual>();
}
ReduceResult MaglevGraphBuilder::VisitTestGreaterThan() {
  return VisitCompareOperation<Operation::kGreaterThan>();
}
ReduceResult MaglevGraphBuilder::VisitTestGreaterThanOrEqual() {
  return VisitCompareOperation<Operation::kGreaterThanOrEqual>();
}

MaglevGraphBuilder::InferHasInPrototypeChainResult
MaglevGraphBuilder::InferHasInPrototypeChain(
    ValueNode* receiver, compiler::HeapObjectRef prototype) {
  auto node_info = known_node_aspects().TryGetInfoFor(receiver);
  // If the map set is not found, then we don't know anything about the map of
  // the receiver, so bail.
  if (!node_info || !node_info->possible_maps_are_known()) {
    return kMayBeInPrototypeChain;
  }

  // If the set of possible maps is empty, then there's no possible map for this
  // receiver, therefore this path is unreachable at runtime. We're unlikely to
  // ever hit this case, BuildCheckMaps should already unconditionally deopt,
  // but check it in case another checking operation fails to statically
  // unconditionally deopt.
  if (node_info->possible_maps().is_empty()) {
    // TODO(leszeks): Add an unreachable assert here.
    return kIsNotInPrototypeChain;
  }

  ZoneVector<compiler::MapRef> receiver_map_refs(zone());

  // Try to determine either that all of the {receiver_maps} have the given
  // {prototype} in their chain, or that none do. If we can't tell, return
  // kMayBeInPrototypeChain.
  bool all = true;
  bool none = true;
  for (compiler::MapRef map : node_info->possible_maps()) {
    receiver_map_refs.push_back(map);
    while (true) {
      if (IsSpecialReceiverInstanceType(map.instance_type())) {
        return kMayBeInPrototypeChain;
      }
      if (!map.IsJSObjectMap()) {
        all = false;
        break;
      }
      compiler::HeapObjectRef map_prototype = map.prototype(broker());
      if (map_prototype.equals(prototype)) {
        none = false;
        break;
      }
      map = map_prototype.map(broker());
      // TODO(v8:11457) Support dictionary mode protoypes here.
      if (!map.is_stable() || map.is_dictionary_map()) {
        return kMayBeInPrototypeChain;
      }
      if (map.oddball_type(broker()) == compiler::OddballType::kNull) {
        all = false;
        break;
      }
    }
  }
  DCHECK(!receiver_map_refs.empty());
  DCHECK_IMPLIES(all, !none);
  if (!all && !none) return kMayBeInPrototypeChain;

  {
    compiler::OptionalJSObjectRef last_prototype;
    if (all) {
      // We don't need to protect the full chain if we found the prototype, we
      // can stop at {prototype}.  In fact we could stop at the one before
      // {prototype} but since we're dealing with multiple receiver maps this
      // might be a different object each time, so it's much simpler to include
      // {prototype}. That does, however, mean that we must check {prototype}'s
      // map stability.
      if (!prototype.IsJSObject() || !prototype.map(broker()).is_stable()) {
        return kMayBeInPrototypeChain;
      }
      last_prototype = prototype.AsJSObject();
    }
    broker()->dependencies()->DependOnStablePrototypeChains(
        receiver_map_refs, kStartAtPrototype, last_prototype);
  }

  DCHECK_EQ(all, !none);
  return all ? kIsInPrototypeChain : kIsNotInPrototypeChain;
}

MaybeReduceResult MaglevGraphBuilder::TryBuildFastHasInPrototypeChain(
    ValueNode* object, compiler::HeapObjectRef prototype) {
  auto in_prototype_chain = InferHasInPrototypeChain(object, prototype);
  if (in_prototype_chain == kMayBeInPrototypeChain) return {};

  return GetBooleanConstant(in_prototype_chain == kIsInPrototypeChain);
}

ReduceResult MaglevGraphBuilder::BuildHasInPrototypeChain(
    ValueNode* object, compiler::HeapObjectRef prototype) {
  RETURN_IF_DONE(TryBuildFastHasInPrototypeChain(object, prototype));
  return AddNewNode<HasInPrototypeChain>({object}, prototype);
}

MaybeReduceResult MaglevGraphBuilder::TryBuildFastOrdinaryHasInstance(
    ValueNode* object, compiler::JSObjectRef callable,
    ValueNode* callable_node_if_not_constant) {
  const bool is_constant = callable_node_if_not_constant == nullptr;
  if (!is_constant) return {};

  if (callable.IsJSBoundFunction()) {
    // OrdinaryHasInstance on bound functions turns into a recursive
    // invocation of the instanceof operator again.
    compiler::JSBoundFunctionRef function = callable.AsJSBoundFunction();
    compiler::JSReceiverRef bound_target_function =
        function.bound_target_function(broker());

    if (bound_target_function.IsJSObject()) {
      RETURN_IF_DONE(TryBuildFastInstanceOf(
          object, bound_target_function.AsJSObject(), nullptr));
    }

    // If we can't build a fast instance-of, build a slow one with the
    // partial optimisation of using the bound target function constant.
    return BuildCallBuiltin<Builtin::kInstanceOf>(
        {GetTaggedValue(object), GetConstant(bound_target_function)});
  }

  if (callable.IsJSFunction()) {
    // Optimize if we currently know the "prototype" property.
    compiler::JSFunctionRef function = callable.AsJSFunction();

    // TODO(v8:7700): Remove the has_prototype_slot condition once the broker
    // is always enabled.
    if (!function.map(broker()).has_prototype_slot() ||
        !function.has_instance_prototype(broker()) ||
        function.PrototypeRequiresRuntimeLookup(broker())) {
      return {};
    }

    compiler::HeapObjectRef prototype =
        broker()->dependencies()->DependOnPrototypeProperty(function);
    return BuildHasInPrototypeChain(object, prototype);
  }

  return {};
}

ReduceResult MaglevGraphBuilder::BuildOrdinaryHasInstance(
    ValueNode* object, compiler::JSObjectRef callable,
    ValueNode* callable_node_if_not_constant) {
  RETURN_IF_DONE(TryBuildFastOrdinaryHasInstance(
      object, callable, callable_node_if_not_constant));

  return BuildCallBuiltin<Builtin::kOrdinaryHasInstance>(
      {callable_node_if_not_constant
           ? GetTaggedValue(callable_node_if_not_constant)
           : GetConstant(callable),
       GetTaggedValue(object)});
}

MaybeReduceResult MaglevGraphBuilder::TryBuildFastInstanceOf(
    ValueNode* object, compiler::JSObjectRef callable,
    ValueNode* callable_node_if_not_constant) {
  compiler::MapRef receiver_map = callable.map(broker());
  compiler::NameRef name = broker()->has_instance_symbol();
  compiler::PropertyAccessInfo access_info = broker()->GetPropertyAccessInfo(
      receiver_map, name, compiler::AccessMode::kLoad);

  // TODO(v8:11457) Support dictionary mode holders here.
  if (access_info.IsInvalid() || access_info.HasDictionaryHolder()) {
    return {};
  }
  access_info.RecordDependencies(broker()->dependencies());

  if (access_info.IsNotFound()) {
    // If there's no @@hasInstance handler, the OrdinaryHasInstance operation
    // takes over, but that requires the constructor to be callable.
    if (!receiver_map.is_callable()) return {};

    broker()->dependencies()->DependOnStablePrototypeChains(
        access_info.lookup_start_object_maps(), kStartAtPrototype);

    // Monomorphic property access.
    if (callable_node_if_not_constant) {
      RETURN_IF_ABORT(BuildCheckMaps(
          callable_node_if_not_constant,
          base::VectorOf(access_info.lookup_start_object_maps())));
    } else {
      // Even if we have a constant receiver, we still have to make sure its
      // map is correct, in case it migrates.
      if (receiver_map.is_stable()) {
        broker()->dependencies()->DependOnStableMap(receiver_map);
      } else {
        RETURN_IF_ABORT(BuildCheckMaps(
            GetConstant(callable),
            base::VectorOf(access_info.lookup_start_object_maps())));
      }
    }

    return BuildOrdinaryHasInstance(object, callable,
                                    callable_node_if_not_constant);
  }

  if (access_info.IsFastDataConstant()) {
    compiler::OptionalJSObjectRef holder = access_info.holder();
    bool found_on_proto = holder.has_value();
    compiler::JSObjectRef holder_ref =
        found_on_proto ? holder.value() : callable;
    if (access_info.field_representation().IsDouble()) return {};
    compiler::OptionalObjectRef has_instance_field =
        holder_ref.GetOwnFastConstantDataProperty(
            broker(), access_info.field_representation(),
            access_info.field_index(), broker()->dependencies());
    if (!has_instance_field.has_value() ||
        !has_instance_field->IsHeapObject() ||
        !has_instance_field->AsHeapObject().map(broker()).is_callable()) {
      return {};
    }

    if (found_on_proto) {
      broker()->dependencies()->DependOnStablePrototypeChains(
          access_info.lookup_start_object_maps(), kStartAtPrototype,
          holder.value());
    }

    ValueNode* callable_node;
    if (callable_node_if_not_constant) {
      // Check that {callable_node_if_not_constant} is actually {callable}.
      RETURN_IF_ABORT(BuildCheckValue(callable_node_if_not_constant, callable));
      callable_node = callable_node_if_not_constant;
    } else {
      callable_node = GetConstant(callable);
    }
    RETURN_IF_ABORT(BuildCheckMaps(
        callable_node, base::VectorOf(access_info.lookup_start_object_maps())));

    // Special case the common case, where @@hasInstance is
    // Function.p.hasInstance. In this case we don't need to call ToBoolean (or
    // use the continuation), since OrdinaryHasInstance is guaranteed to return
    // a boolean.
    if (has_instance_field->IsJSFunction()) {
      compiler::SharedFunctionInfoRef shared =
          has_instance_field->AsJSFunction().shared(broker());
      if (shared.HasBuiltinId() &&
          shared.builtin_id() == Builtin::kFunctionPrototypeHasInstance) {
        return BuildOrdinaryHasInstance(object, callable,
                                        callable_node_if_not_constant);
      }
    }

    // Call @@hasInstance
    CallArguments args(ConvertReceiverMode::kNotNullOrUndefined,
                       {callable_node, object});
    ValueNode* call_result;
    {
      // Make sure that a lazy deopt after the @@hasInstance call also performs
      // ToBoolean before returning to the interpreter.
      DeoptFrameScope continuation_scope(
          this, Builtin::kToBooleanLazyDeoptContinuation);

      if (has_instance_field->IsJSFunction()) {
        SaveCallSpeculationScope saved(this);
        GET_VALUE_OR_ABORT(
            call_result,
            TryReduceCallForConstant(has_instance_field->AsJSFunction(), args));
      } else {
        call_result = BuildGenericCall(GetConstant(*has_instance_field),
                                       Call::TargetType::kAny, args);
      }
      // TODO(victorgomes): Propagate the case if we need to soft deopt.
    }

    return BuildToBoolean(call_result);
  }

  return {};
}

template <bool flip>
ValueNode* MaglevGraphBuilder::BuildToBoolean(ValueNode* value) {
  if (IsConstantNode(value->opcode())) {
    return GetBooleanConstant(FromConstantToBool(local_isolate(), value) ^
                              flip);
  }

  switch (value->value_representation()) {
    case ValueRepresentation::kFloat64:
    case ValueRepresentation::kHoleyFloat64:
      // The ToBoolean of both the_hole and NaN is false, so we can use the
      // same operation for HoleyFloat64 and Float64.
      return AddNewNode<Float64ToBoolean>({value}, flip);

    case ValueRepresentation::kUint32:
      // Uint32 has the same logic as Int32 when converting ToBoolean, namely
      // comparison against zero, so we can cast it and ignore the signedness.
      value = AddNewNode<TruncateUint32ToInt32>({value});
      [[fallthrough]];
    case ValueRepresentation::kInt32:
      return AddNewNode<Int32ToBoolean>({value}, flip);

    case ValueRepresentation::kIntPtr:
      return AddNewNode<IntPtrToBoolean>({value}, flip);

    case ValueRepresentation::kTagged:
      break;
  }

  NodeInfo* node_info = known_node_aspects().TryGetInfoFor(value);
  if (node_info) {
    if (ValueNode* as_int32 = node_info->alternative().int32()) {
      return AddNewNode<Int32ToBoolean>({as_int32}, flip);
    }
    if (ValueNode* as_float64 = node_info->alternative().float64()) {
      return AddNewNode<Float64ToBoolean>({as_float64}, flip);
    }
  }

  NodeType value_type;
  if (CheckType(value, NodeType::kJSReceiver, &value_type)) {
    ValueNode* result = BuildTestUndetectable(value);
    // TODO(victorgomes): Check if it is worth to create
    // TestUndetectableLogicalNot or to remove ToBooleanLogicalNot, since we
    // already optimize LogicalNots by swapping the branches.
    if constexpr (!flip) {
      result = BuildLogicalNot(result);
    }
    return result;
  }
  ValueNode* falsy_value = nullptr;
  if (CheckType(value, NodeType::kString)) {
    falsy_value = GetRootConstant(RootIndex::kempty_string);
  } else if (CheckType(value, NodeType::kSmi)) {
    falsy_value = GetSmiConstant(0);
  }
  if (falsy_value != nullptr) {
    return AddNewNode<std::conditional_t<flip, TaggedEqual, TaggedNotEqual>>(
        {value, falsy_value});
  }
  if (CheckType(value, NodeType::kBoolean)) {
    if constexpr (flip) {
      value = BuildLogicalNot(value);
    }
    return value;
  }
  return AddNewNode<std::conditional_t<flip, ToBooleanLogicalNot, ToBoolean>>(
      {value}, GetCheckType(value_type));
}

MaybeReduceResult MaglevGraphBuilder::TryBuildFastInstanceOfWithFeedback(
    ValueNode* object, ValueNode* callable,
    compiler::FeedbackSource feedback_source) {
  compiler::ProcessedFeedback const& feedback =
      broker()->GetFeedbackForInstanceOf(feedback_source);

  if (feedback.IsInsufficient()) {
    return EmitUnconditionalDeopt(
        DeoptimizeReason::kInsufficientTypeFeedbackForInstanceOf);
  }

  // Check if the right hand side is a known receiver, or
  // we have feedback from the InstanceOfIC.
  compiler::OptionalHeapObjectRef maybe_constant;
  if ((maybe_constant = TryGetConstant(callable)) &&
      maybe_constant.value().IsJSObject()) {
    compiler::JSObjectRef callable_ref = maybe_constant.value().AsJSObject();
    return TryBuildFastInstanceOf(object, callable_ref, nullptr);
  }
  if (feedback_source.IsValid()) {
    compiler::OptionalJSObjectRef callable_from_feedback =
        feedback.AsInstanceOf().value();
    if (callable_from_feedback) {
      return TryBuildFastInstanceOf(object, *callable_from_feedback, callable);
    }
  }
  return {};
}

ReduceResult MaglevGraphBuilder::VisitTestInstanceOf() {
  // TestInstanceOf <src> <feedback_slot>
  ValueNode* object = LoadRegister(0);
  ValueNode* callable = GetAccumulator();
  FeedbackSlot slot = GetSlotOperand(1);
  compiler::FeedbackSource feedback_source{feedback(), slot};

  MaybeReduceResult result =
      TryBuildFastInstanceOfWithFeedback(object, callable, feedback_source);
  PROCESS_AND_RETURN_IF_DONE(result, SetAccumulator);

  ValueNode* context = GetContext();
  SetAccumulator(
      AddNewNode<TestInstanceOf>({context, object, callable}, feedback_source));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitTestIn() {
  // TestIn <src> <feedback_slot>
  ValueNode* object = GetAccumulator();
  ValueNode* name = LoadRegister(0);
  FeedbackSlot slot = GetSlotOperand(1);
  compiler::FeedbackSource feedback_source{feedback(), slot};

  // TODO(victorgomes): Create fast path using feedback.
  USE(feedback_source);

  SetAccumulator(BuildCallBuiltin<Builtin::kKeyedHasIC>(
      {GetTaggedValue(object), GetTaggedValue(name)}, feedback_source));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitToName() {
  // ToObject <dst>
  if (!CheckType(GetAccumulator(), NodeType::kName)) {
    SetAccumulator(AddNewNode<ToName>({GetContext(), GetAccumulator()}));
  }
  return ReduceResult::Done();
}

ValueNode* MaglevGraphBuilder::BuildToString(ValueNode* value,
                                             ToString::ConversionMode mode) {
  if (CheckType(value, NodeType::kString)) return value;
  // TODO(victorgomes): Add fast path for constant primitives.
  if (CheckType(value, NodeType::kNumber)) {
    // TODO(verwaest): Float64ToString if float.
    return AddNewNode<NumberToString>({value});
  }
  return AddNewNode<ToString>({GetContext(), value}, mode);
}

ReduceResult MaglevGraphBuilder::BuildToNumberOrToNumeric(
    Object::Conversion mode) {
  ValueNode* value = GetAccumulator();
  switch (value->value_representation()) {
    case ValueRepresentation::kInt32:
    case ValueRepresentation::kUint32:
    case ValueRepresentation::kFloat64:
    case ValueRepresentation::kIntPtr:
      return ReduceResult::Done();

    case ValueRepresentation::kHoleyFloat64: {
      SetAccumulator(AddNewNode<HoleyFloat64ToMaybeNanFloat64>({value}));
      return ReduceResult::Done();
    }

    case ValueRepresentation::kTagged:
      // We'll insert the required checks depending on the feedback.
      break;
  }

  FeedbackSlot slot = GetSlotOperand(0);
  switch (broker()->GetFeedbackForBinaryOperation(
      compiler::FeedbackSource(feedback(), slot))) {
    case BinaryOperationHint::kSignedSmall:
      RETURN_IF_ABORT(BuildCheckSmi(value));
      break;
    case BinaryOperationHint::kSignedSmallInputs:
      UNREACHABLE();
    case BinaryOperationHint::kNumber:
    case BinaryOperationHint::kBigInt:
    case BinaryOperationHint::kBigInt64:
      if (mode == Object::Conversion::kToNumber &&
          EnsureType(value, NodeType::kNumber)) {
        return ReduceResult::Done();
      }
      AddNewNode<CheckNumber>({value}, mode);
      break;
    case BinaryOperationHint::kNone:
    // TODO(leszeks): Faster ToNumber for kNumberOrOddball
    case BinaryOperationHint::kNumberOrOddball:
    case BinaryOperationHint::kString:
    case BinaryOperationHint::kStringOrStringWrapper:
    case BinaryOperationHint::kAny:
      if (CheckType(value, NodeType::kNumber)) return ReduceResult::Done();
      SetAccumulator(AddNewNode<ToNumberOrNumeric>({value}, mode));
      break;
  }
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitToNumber() {
  return BuildToNumberOrToNumeric(Object::Conversion::kToNumber);
}
ReduceResult MaglevGraphBuilder::VisitToNumeric() {
  return BuildToNumberOrToNumeric(Object::Conversion::kToNumeric);
}

ReduceResult MaglevGraphBuilder::VisitToObject() {
  // ToObject <dst>
  ValueNode* value = GetAccumulator();
  interpreter::Register destination = iterator_.GetRegisterOperand(0);
  NodeType old_type;
  if (CheckType(value, NodeType::kJSReceiver, &old_type)) {
    MoveNodeBetweenRegisters(interpreter::Register::virtual_accumulator(),
                             destination);
  } else {
    StoreRegister(destination, AddNewNode<ToObject>({GetContext(), value},
                                                    GetCheckType(old_type)));
  }
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitToString() {
  // ToString
  SetAccumulator(BuildToString(GetAccumulator(), ToString::kThrowOnSymbol));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitToBoolean() {
  SetAccumulator(BuildToBoolean(GetAccumulator()));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitCreateRegExpLiteral() {
  // CreateRegExpLiteral <pattern_idx> <literal_idx> <flags>
  compiler::StringRef pattern = GetRefOperand<String>(0);
  FeedbackSlot slot = GetSlotOperand(1);
  uint32_t flags = GetFlag16Operand(2);
  compiler::FeedbackSource feedback_source{feedback(), slot};
  compiler::ProcessedFeedback const& processed_feedback =
      broker()->GetFeedbackForRegExpLiteral(feedback_source);
  if (!processed_feedback.IsInsufficient()) {
    compiler::RegExpBoilerplateDescriptionRef literal =
        processed_feedback.AsRegExpLiteral().value();
    compiler::NativeContextRef native_context =
        broker()->target_native_context();
    compiler::MapRef map =
        native_context.regexp_function(broker()).initial_map(broker());
    SetAccumulator(BuildInlinedAllocation(
        CreateRegExpLiteralObject(map, literal), AllocationType::kYoung));
    // TODO(leszeks): Don't eagerly clear the raw allocation, have the next side
    // effect clear it.
    ClearCurrentAllocationBlock();
    return ReduceResult::Done();
  }
  // Fallback.
  SetAccumulator(
      AddNewNode<CreateRegExpLiteral>({}, pattern, feedback_source, flags));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitCreateArrayLiteral() {
  compiler::HeapObjectRef constant_elements = GetRefOperand<HeapObject>(0);
  FeedbackSlot slot_index = GetSlotOperand(1);
  int bytecode_flags = GetFlag8Operand(2);
  int literal_flags =
      interpreter::CreateArrayLiteralFlags::FlagsBits::decode(bytecode_flags);
  compiler::FeedbackSource feedback_source(feedback(), slot_index);

  compiler::ProcessedFeedback const& processed_feedback =
      broker()->GetFeedbackForArrayOrObjectLiteral(feedback_source);

  if (processed_feedback.IsInsufficient()) {
    return EmitUnconditionalDeopt(
        DeoptimizeReason::kInsufficientTypeFeedbackForArrayLiteral);
  }

  MaybeReduceResult result =
      TryBuildFastCreateObjectOrArrayLiteral(processed_feedback.AsLiteral());
  PROCESS_AND_RETURN_IF_DONE(result, SetAccumulator);

  if (interpreter::CreateArrayLiteralFlags::FastCloneSupportedBit::decode(
          bytecode_flags)) {
    // TODO(victorgomes): CreateShallowArrayLiteral should not need the
    // boilerplate descriptor. However the current builtin checks that the
    // feedback exists and fallsback to CreateArrayLiteral if it doesn't.
    SetAccumulator(AddNewNode<CreateShallowArrayLiteral>(
        {}, constant_elements, feedback_source, literal_flags));
  } else {
    SetAccumulator(AddNewNode<CreateArrayLiteral>(
        {}, constant_elements, feedback_source, literal_flags));
  }
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitCreateArrayFromIterable() {
  ValueNode* iterable = GetAccumulator();
  SetAccumulator(BuildCallBuiltin<Builtin::kIterableToListWithSymbolLookup>(
      {GetTaggedValue(iterable)}));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitCreateEmptyArrayLiteral() {
  FeedbackSlot slot_index = GetSlotOperand(0);
  compiler::FeedbackSource feedback_source(feedback(), slot_index);
  compiler::ProcessedFeedback const& processed_feedback =
      broker()->GetFeedbackForArrayOrObjectLiteral(feedback_source);
  if (processed_feedback.IsInsufficient()) {
    return EmitUnconditionalDeopt(
        DeoptimizeReason::kInsufficientTypeFeedbackForArrayLiteral);
  }
  compiler::AllocationSiteRef site = processed_feedback.AsLiteral().value();

  broker()->dependencies()->DependOnElementsKind(site);
  ElementsKind kind = site.GetElementsKind();

  compiler::NativeContextRef native_context = broker()->target_native_context();
  compiler::MapRef map = native_context.GetInitialJSArrayMap(broker(), kind);
  // Initial JSArray map shouldn't have any in-object properties.
  SBXCHECK_EQ(map.GetInObjectProperties(), 0);
  VirtualObject* array;
  GET_VALUE_OR_ABORT(
      array, CreateJSArray(map, map.instance_size(), GetSmiConstant(0)));
  SetAccumulator(BuildInlinedAllocation(array, AllocationType::kYoung));
  // TODO(leszeks): Don't eagerly clear the raw allocation, have the next side
  // effect clear it.
  ClearCurrentAllocationBlock();
  return ReduceResult::Done();
}

std::optional<VirtualObject*>
MaglevGraphBuilder::TryReadBoilerplateForFastLiteral(
    compiler::JSObjectRef boilerplate, AllocationType allocation, int max_depth,
    int* max_properties) {
  DCHECK_GE(max_depth, 0);
  DCHECK_GE(*max_properties, 0);

  if (max_depth == 0) return {};

  // Prevent concurrent migrations of boilerplate objects.
  compiler::JSHeapBroker::BoilerplateMigrationGuardIfNeeded
      boilerplate_access_guard(broker());

  // Now that we hold the migration lock, get the current map.
  compiler::MapRef boilerplate_map = boilerplate.map(broker());
  // Protect against concurrent changes to the boilerplate object by checking
  // for an identical value at the end of the compilation.
  broker()->dependencies()->DependOnObjectSlotValue(
      boilerplate, HeapObject::kMapOffset, boilerplate_map);
  {
    compiler::OptionalMapRef current_boilerplate_map =
        boilerplate.map_direct_read(broker());
    if (!current_boilerplate_map.has_value() ||
        !current_boilerplate_map->equals(boilerplate_map)) {
      // TODO(leszeks): Emit an eager deopt for this case, so that we can
      // re-learn the boilerplate. This will be easier once we get rid of the
      // two-pass approach, since we'll be able to create the eager deopt here
      // and return a ReduceResult::DoneWithAbort().
      return {};
    }
  }

  // Bail out if the boilerplate map has been deprecated.  The map could of
  // course be deprecated at some point after the line below, but it's not a
  // correctness issue -- it only means the literal won't be created with the
  // most up to date map(s).
  if (boilerplate_map.is_deprecated()) return {};

  // We currently only support in-object properties.
  if (boilerplate.map(broker()).elements_kind() == DICTIONARY_ELEMENTS ||
      boilerplate.map(broker()).is_dictionary_map() ||
      !boilerplate.raw_properties_or_hash(broker()).has_value()) {
    return {};
  }
  {
    compiler::ObjectRef properties =
        *boilerplate.raw_properties_or_hash(broker());
    bool const empty =
        properties.IsSmi() ||
        properties.equals(MakeRef(
            broker(), local_isolate()->factory()->empty_fixed_array())) ||
        properties.equals(MakeRef(
            broker(),
            Cast<Object>(local_isolate()->factory()->empty_property_array())));
    if (!empty) return {};
  }

  compiler::OptionalFixedArrayBaseRef maybe_elements =
      boilerplate.elements(broker(), kRelaxedLoad);
  if (!maybe_elements.has_value()) return {};
  compiler::FixedArrayBaseRef boilerplate_elements = maybe_elements.value();
  broker()->dependencies()->DependOnObjectSlotValue(
      boilerplate, JSObject::kElementsOffset, boilerplate_elements);
  const uint32_t elements_length = boilerplate_elements.length();

  VirtualObject* fast_literal;
  if (boilerplate_map.IsJSArrayMap()) {
    MaybeReduceResult fast_array = CreateJSArray(
        boilerplate_map, boilerplate_map.instance_size(),
        GetConstant(boilerplate.AsJSArray().GetBoilerplateLength(broker())));
    CHECK(fast_array.HasValue());
    fast_literal = fast_array.value()->Cast<VirtualObject>();
  } else {
    fast_literal = CreateJSObject(boilerplate_map);
  }

  int inobject_properties = boilerplate_map.GetInObjectProperties();

  // Compute the in-object properties to store first.
  int index = 0;
  for (InternalIndex i :
       InternalIndex::Range(boilerplate_map.NumberOfOwnDescriptors())) {
    PropertyDetails const property_details =
        boilerplate_map.GetPropertyDetails(broker(), i);
    if (property_details.location() != PropertyLocation::kField) continue;
    DCHECK_EQ(PropertyKind::kData, property_details.kind());
    if ((*max_properties)-- == 0) return {};

    int offset = boilerplate_map.GetInObjectPropertyOffset(index);
#ifdef DEBUG
    FieldIndex field_index =
        FieldIndex::ForDetails(*boilerplate_map.object(), property_details);
    DCHECK(field_index.is_inobject());
    DCHECK_EQ(index, field_index.property_index());
    DCHECK_EQ(field_index.offset(), offset);
#endif

    // The index is derived from the in-sandbox `NumberOfOwnDescriptors` value,
    // but the access is out-of-sandbox fast_literal fields.
    SBXCHECK_LT(index, inobject_properties);

    // Note: the use of RawInobjectPropertyAt (vs. the higher-level
    // GetOwnFastConstantDataProperty) here is necessary, since the underlying
    // value may be `uninitialized`, which the latter explicitly does not
    // support.
    compiler::OptionalObjectRef maybe_boilerplate_value =
        boilerplate.RawInobjectPropertyAt(
            broker(),
            FieldIndex::ForInObjectOffset(offset, FieldIndex::kTagged));
    if (!maybe_boilerplate_value.has_value()) return {};

    // Note: We don't need to take a compilation dependency verifying the value
    // of `boilerplate_value`, since boilerplate properties are constant after
    // initialization modulo map migration. We protect against concurrent map
    // migrations (other than elements kind transition, which don't affect us)
    // via the boilerplate_migration_access lock.
    compiler::ObjectRef boilerplate_value = maybe_boilerplate_value.value();

    if (boilerplate_value.IsJSObject()) {
      compiler::JSObjectRef boilerplate_object = boilerplate_value.AsJSObject();
      std::optional<VirtualObject*> maybe_object_value =
          TryReadBoilerplateForFastLiteral(boilerplate_object, allocation,
                                           max_depth - 1, max_properties);
      if (!maybe_object_value.has_value()) return {};
      fast_literal->set(offset, maybe_object_value.value());
    } else if (property_details.representation().IsDouble()) {
      fast_literal->set(offset,
                        CreateHeapNumber(Float64::FromBits(
                            boilerplate_value.AsHeapNumber().value_as_bits())));
    } else {
      // It's fine to store the 'uninitialized' Oddball into a Smi field since
      // it will get overwritten anyway.
      DCHECK_IMPLIES(property_details.representation().IsSmi() &&
                         !boilerplate_value.IsSmi(),
                     IsUninitialized(*boilerplate_value.object()));
      fast_literal->set(offset, GetConstant(boilerplate_value));
    }
    index++;
  }

  // Fill slack at the end of the boilerplate object with filler maps.
  for (; index < inobject_properties; ++index) {
    DCHECK(!V8_MAP_PACKING_BOOL);
    // TODO(wenyuzhao): Fix incorrect MachineType when V8_MAP_PACKING is
    // enabled.
    int offset = boilerplate_map.GetInObjectPropertyOffset(index);
    fast_literal->set(offset, GetRootConstant(RootIndex::kOnePointerFillerMap));
  }

  DCHECK_EQ(JSObject::kElementsOffset, JSArray::kElementsOffset);
  // Empty or copy-on-write elements just store a constant.
  compiler::MapRef elements_map = boilerplate_elements.map(broker());
  // Protect against concurrent changes to the boilerplate object by checking
  // for an identical value at the end of the compilation.
  broker()->dependencies()->DependOnObjectSlotValue(
      boilerplate_elements, HeapObject::kMapOffset, elements_map);
  if (boilerplate_elements.length() == 0 ||
      elements_map.IsFixedCowArrayMap(broker())) {
    if (allocation == AllocationType::kOld &&
        !boilerplate.IsElementsTenured(boilerplate_elements)) {
      return {};
    }
    fast_literal->set(JSObject::kElementsOffset,
                      GetConstant(boilerplate_elements));
  } else {
    // Compute the elements to store first (might have effects).
    if (boilerplate_elements.IsFixedDoubleArray()) {
      int const size = FixedDoubleArray::SizeFor(elements_length);
      if (size > kMaxRegularHeapObjectSize) return {};
      fast_literal->set(
          JSObject::kElementsOffset,
          CreateDoubleFixedArray(elements_length,
                                 boilerplate_elements.AsFixedDoubleArray()));
    } else {
      int const size = FixedArray::SizeFor(elements_length);
      if (size > kMaxRegularHeapObjectSize) return {};
      VirtualObject* elements =
          CreateFixedArray(broker()->fixed_array_map(), elements_length);
      compiler::FixedArrayRef boilerplate_elements_as_fixed_array =
          boilerplate_elements.AsFixedArray();
      for (uint32_t i = 0; i < elements_length; ++i) {
        if ((*max_properties)-- == 0) return {};
        compiler::OptionalObjectRef element_value =
            boilerplate_elements_as_fixed_array.TryGet(broker(), i);
        if (!element_value.has_value()) return {};
        if (element_value->IsJSObject()) {
          std::optional<VirtualObject*> object =
              TryReadBoilerplateForFastLiteral(element_value->AsJSObject(),
                                               allocation, max_depth - 1,
                                               max_properties);
          if (!object.has_value()) return {};
          elements->set(FixedArray::OffsetOfElementAt(i), *object);
        } else {
          elements->set(FixedArray::OffsetOfElementAt(i),
                        GetConstant(*element_value));
        }
      }

      fast_literal->set(JSObject::kElementsOffset, elements);
    }
  }

  return fast_literal;
}

VirtualObject* MaglevGraphBuilder::DeepCopyVirtualObject(VirtualObject* old) {
  CHECK_EQ(old->type(), VirtualObject::kDefault);
  ValueNode** slots = zone()->AllocateArray<ValueNode*>(old->slot_count());
  VirtualObject* vobject = NodeBase::New<VirtualObject>(
      zone(), 0, old->map(), NewObjectId(), old->slot_count(), slots);
  current_interpreter_frame_.add_object(vobject);
  for (int i = 0; i < static_cast<int>(old->slot_count()); i++) {
    vobject->set_by_index(i, old->get_by_index(i));
  }
  vobject->set_allocation(old->allocation());
  old->allocation()->UpdateObject(vobject);
  return vobject;
}

VirtualObject* MaglevGraphBuilder::CreateVirtualObjectForMerge(
    compiler::MapRef map, uint32_t slot_count) {
  ValueNode** slots = zone()->AllocateArray<ValueNode*>(slot_count);
  VirtualObject* vobject = NodeBase::New<VirtualObject>(
      zone(), 0, map, NewObjectId(), slot_count, slots);
  return vobject;
}

VirtualObject* MaglevGraphBuilder::CreateVirtualObject(
    compiler::MapRef map, uint32_t slot_count_including_map) {
  // VirtualObjects are not added to the Maglev graph.
  DCHECK_GT(slot_count_including_map, 0);
  uint32_t slot_count = slot_count_including_map - 1;
  ValueNode** slots = zone()->AllocateArray<ValueNode*>(slot_count);
  VirtualObject* vobject = NodeBase::New<VirtualObject>(
      zone(), 0, map, NewObjectId(), slot_count, slots);
  return vobject;
}

VirtualObject* MaglevGraphBuilder::CreateHeapNumber(Float64 value) {
  // VirtualObjects are not added to the Maglev graph.
  VirtualObject* vobject = NodeBase::New<VirtualObject>(
      zone(), 0, broker()->heap_number_map(), NewObjectId(), value);
  return vobject;
}

VirtualObject* MaglevGraphBuilder::CreateDoubleFixedArray(
    uint32_t elements_length, compiler::FixedDoubleArrayRef elements) {
  // VirtualObjects are not added to the Maglev graph.
  VirtualObject* vobject = NodeBase::New<VirtualObject>(
      zone(), 0, broker()->fixed_double_array_map(), NewObjectId(),
      elements_length, elements);
  return vobject;
}

VirtualObject* MaglevGraphBuilder::CreateJSObject(compiler::MapRef map) {
  DCHECK(!map.is_dictionary_map());
  DCHECK(!map.IsInobjectSlackTrackingInProgress());
  int slot_count = map.instance_size() / kTaggedSize;
  SBXCHECK_GE(slot_count, 3);
  VirtualObject* object = CreateVirtualObject(map, slot_count);
  object->set(JSObject::kPropertiesOrHashOffset,
              GetRootConstant(RootIndex::kEmptyFixedArray));
  object->set(JSObject::kElementsOffset,
              GetRootConstant(RootIndex::kEmptyFixedArray));
  object->ClearSlots(JSObject::kElementsOffset,
                     GetRootConstant(RootIndex::kOnePointerFillerMap));
  return object;
}

ReduceResult MaglevGraphBuilder::CreateJSArray(compiler::MapRef map,
                                               int instance_size,
                                               ValueNode* length) {
  int slot_count = instance_size / kTaggedSize;
  SBXCHECK_GE(slot_count, 4);
  VirtualObject* object = CreateVirtualObject(map, slot_count);
  object->set(JSArray::kPropertiesOrHashOffset,
              GetRootConstant(RootIndex::kEmptyFixedArray));
  // Either the value is a Smi already, or we force a conversion to Smi and
  // cache the value in its alternative representation node.
  RETURN_IF_ABORT(GetSmiValue(length));
  object->set(JSArray::kElementsOffset,
              GetRootConstant(RootIndex::kEmptyFixedArray));
  object->set(JSArray::kLengthOffset, length);
  object->ClearSlots(JSArray::kLengthOffset,
                     GetRootConstant(RootIndex::kOnePointerFillerMap));
  return object;
}

VirtualObject* MaglevGraphBuilder::CreateJSArrayIterator(
    compiler::MapRef map, ValueNode* iterated_object, IterationKind kind) {
  int slot_count = map.instance_size() / kTaggedSize;
  SBXCHECK_EQ(slot_count, 6);
  VirtualObject* object = CreateVirtualObject(map, slot_count);
  object->set(JSArrayIterator::kPropertiesOrHashOffset,
              GetRootConstant(RootIndex::kEmptyFixedArray));
  object->set(JSArrayIterator::kElementsOffset,
              GetRootConstant(RootIndex::kEmptyFixedArray));
  object->set(JSArrayIterator::kIteratedObjectOffset, iterated_object);
  object->set(JSArrayIterator::kNextIndexOffset, GetInt32Constant(0));
  object->set(JSArrayIterator::kKindOffset,
              GetInt32Constant(static_cast<int>(kind)));
  return object;
}

VirtualObject* MaglevGraphBuilder::CreateJSConstructor(
    compiler::JSFunctionRef constructor) {
  compiler::SlackTrackingPrediction prediction =
      broker()->dependencies()->DependOnInitialMapInstanceSizePrediction(
          constructor);
  int slot_count = prediction.instance_size() / kTaggedSize;
  VirtualObject* object =
      CreateVirtualObject(constructor.initial_map(broker()), slot_count);
  SBXCHECK_GE(slot_count, 3);
  object->set(JSObject::kPropertiesOrHashOffset,
              GetRootConstant(RootIndex::kEmptyFixedArray));
  object->set(JSObject::kElementsOffset,
              GetRootConstant(RootIndex::kEmptyFixedArray));
  object->ClearSlots(JSObject::kElementsOffset,
                     GetRootConstant(RootIndex::kOnePointerFillerMap));
  return object;
}

VirtualObject* MaglevGraphBuilder::CreateFixedArray(compiler::MapRef map,
                                                    int length) {
  int slot_count = FixedArray::SizeFor(length) / kTaggedSize;
  VirtualObject* array = CreateVirtualObject(map, slot_count);
  array->set(offsetof(FixedArray, length_), GetInt32Constant(length));
  array->ClearSlots(offsetof(FixedArray, length_),
                    GetRootConstant(RootIndex::kOnePointerFillerMap));
  return array;
}

VirtualObject* MaglevGraphBuilder::CreateContext(
    compiler::MapRef map, int length, compiler::ScopeInfoRef scope_info,
    ValueNode* previous_context, std::optional<ValueNode*> extension) {
  int slot_count = FixedArray::SizeFor(length) / kTaggedSize;
  VirtualObject* context = CreateVirtualObject(map, slot_count);
  context->set(Context::kLengthOffset, GetInt32Constant(length));
  context->set(Context::OffsetOfElementAt(Context::SCOPE_INFO_INDEX),
               GetConstant(scope_info));
  context->set(Context::OffsetOfElementAt(Context::PREVIOUS_INDEX),
               previous_context);
  int index = Context::PREVIOUS_INDEX + 1;
  if (extension.has_value()) {
    context->set(Context::OffsetOfElementAt(Context::EXTENSION_INDEX),
                 extension.value());
    index++;
  }
  for (; index < length; index++) {
    context->set(Context::OffsetOfElementAt(index),
                 GetRootConstant(RootIndex::kUndefinedValue));
  }
  return context;
}

VirtualObject* MaglevGraphBuilder::CreateArgumentsObject(
    compiler::MapRef map, ValueNode* length, ValueNode* elements,
    std::optional<ValueNode*> callee) {
  DCHECK_EQ(JSSloppyArgumentsObject::kLengthOffset, JSArray::kLengthOffset);
  DCHECK_EQ(JSStrictArgumentsObject::kLengthOffset, JSArray::kLengthOffset);
  int slot_count = map.instance_size() / kTaggedSize;
  SBXCHECK_EQ(slot_count, callee.has_value() ? 5 : 4);
  VirtualObject* arguments = CreateVirtualObject(map, slot_count);
  arguments->set(JSArray::kPropertiesOrHashOffset,
                 GetRootConstant(RootIndex::kEmptyFixedArray));
  arguments->set(JSArray::kElementsOffset, elements);
  CHECK(length->Is<Int32Constant>() || length->Is<ArgumentsLength>() ||
        length->Is<RestLength>());
  arguments->set(JSArray::kLengthOffset, length);
  if (callee.has_value()) {
    arguments->set(JSSloppyArgumentsObject::kCalleeOffset, callee.value());
  }
  DCHECK(arguments->map().IsJSArgumentsObjectMap() ||
         arguments->map().IsJSArrayMap());
  return arguments;
}

VirtualObject* MaglevGraphBuilder::CreateMappedArgumentsElements(
    compiler::MapRef map, int mapped_count, ValueNode* context,
    ValueNode* unmapped_elements) {
  int slot_count = SloppyArgumentsElements::SizeFor(mapped_count) / kTaggedSize;
  VirtualObject* elements = CreateVirtualObject(map, slot_count);
  elements->set(offsetof(SloppyArgumentsElements, length_),
                GetInt32Constant(mapped_count));
  elements->set(offsetof(SloppyArgumentsElements, context_), context);
  elements->set(offsetof(SloppyArgumentsElements, arguments_),
                unmapped_elements);
  return elements;
}

VirtualObject* MaglevGraphBuilder::CreateRegExpLiteralObject(
    compiler::MapRef map, compiler::RegExpBoilerplateDescriptionRef literal) {
  DCHECK_EQ(JSRegExp::Size(), JSRegExp::kLastIndexOffset + kTaggedSize);
  int slot_count = JSRegExp::Size() / kTaggedSize;
  VirtualObject* regexp = CreateVirtualObject(map, slot_count);
  regexp->set(JSRegExp::kPropertiesOrHashOffset,
              GetRootConstant(RootIndex::kEmptyFixedArray));
  regexp->set(JSRegExp::kElementsOffset,
              GetRootConstant(RootIndex::kEmptyFixedArray));
  regexp->set(JSRegExp::kDataOffset,
              GetTrustedConstant(literal.data(broker()),
                                 kRegExpDataIndirectPointerTag));
  regexp->set(JSRegExp::kSourceOffset, GetConstant(literal.source(broker())));
  regexp->set(JSRegExp::kFlagsOffset, GetInt32Constant(literal.flags()));
  regexp->set(JSRegExp::kLastIndexOffset,
              GetInt32Constant(JSRegExp::kInitialLastIndexValue));
  return regexp;
}

VirtualObject* MaglevGraphBuilder::CreateJSGeneratorObject(
    compiler::MapRef map, int instance_size, ValueNode* context,
    ValueNode* closure, ValueNode* receiver, ValueNode* register_file) {
  int slot_count = instance_size / kTaggedSize;
  InstanceType instance_type = map.instance_type();
  DCHECK(instance_type == JS_GENERATOR_OBJECT_TYPE ||
         instance_type == JS_ASYNC_GENERATOR_OBJECT_TYPE);
  SBXCHECK_GE(slot_count, instance_type == JS_GENERATOR_OBJECT_TYPE ? 10 : 12);
  VirtualObject* object = CreateVirtualObject(map, slot_count);
  object->set(JSGeneratorObject::kPropertiesOrHashOffset,
              GetRootConstant(RootIndex::kEmptyFixedArray));
  object->set(JSGeneratorObject::kElementsOffset,
              GetRootConstant(RootIndex::kEmptyFixedArray));
  object->set(JSGeneratorObject::kContextOffset, context);
  object->set(JSGeneratorObject::kFunctionOffset, closure);
  object->set(JSGeneratorObject::kReceiverOffset, receiver);
  object->set(JSGeneratorObject::kInputOrDebugPosOffset,
              GetRootConstant(RootIndex::kUndefinedValue));
  object->set(JSGeneratorObject::kResumeModeOffset,
              GetInt32Constant(JSGeneratorObject::kNext));
  object->set(JSGeneratorObject::kContinuationOffset,
              GetInt32Constant(JSGeneratorObject::kGeneratorExecuting));
  object->set(JSGeneratorObject::kParametersAndRegistersOffset, register_file);
  if (instance_type == JS_ASYNC_GENERATOR_OBJECT_TYPE) {
    object->set(JSAsyncGeneratorObject::kQueueOffset,
                GetRootConstant(RootIndex::kUndefinedValue));
    object->set(JSAsyncGeneratorObject::kIsAwaitingOffset, GetInt32Constant(0));
  }
  return object;
}

VirtualObject* MaglevGraphBuilder::CreateJSIteratorResult(compiler::MapRef map,
                                                          ValueNode* value,
                                                          ValueNode* done) {
  static_assert(JSIteratorResult::kSize == 5 * kTaggedSize);
  int slot_count = JSIteratorResult::kSize / kTaggedSize;
  VirtualObject* iter_result = CreateVirtualObject(map, slot_count);
  iter_result->set(JSIteratorResult::kPropertiesOrHashOffset,
                   GetRootConstant(RootIndex::kEmptyFixedArray));
  iter_result->set(JSIteratorResult::kElementsOffset,
                   GetRootConstant(RootIndex::kEmptyFixedArray));
  iter_result->set(JSIteratorResult::kValueOffset, value);
  iter_result->set(JSIteratorResult::kDoneOffset, done);
  return iter_result;
}

VirtualObject* MaglevGraphBuilder::CreateJSStringIterator(compiler::MapRef map,
                                                          ValueNode* string) {
  static_assert(JSStringIterator::kHeaderSize == 5 * kTaggedSize);
  int slot_count = JSStringIterator::kHeaderSize / kTaggedSize;
  VirtualObject* string_iter = CreateVirtualObject(map, slot_count);
  string_iter->set(JSStringIterator::kPropertiesOrHashOffset,
                   GetRootConstant(RootIndex::kEmptyFixedArray));
  string_iter->set(JSStringIterator::kElementsOffset,
                   GetRootConstant(RootIndex::kEmptyFixedArray));
  string_iter->set(JSStringIterator::kStringOffset, string);
  string_iter->set(JSStringIterator::kIndexOffset, GetInt32Constant(0));
  return string_iter;
}

InlinedAllocation* MaglevGraphBuilder::ExtendOrReallocateCurrentAllocationBlock(
    AllocationType allocation_type, VirtualObject* vobject) {
  DCHECK_LE(vobject->size(), kMaxRegularHeapObjectSize);
  if (!current_allocation_block_ ||
      current_allocation_block_->allocation_type() != allocation_type ||
      !v8_flags.inline_new ||
      compilation_unit()->info()->for_turboshaft_frontend()) {
    current_allocation_block_ =
        AddNewNode<AllocationBlock>({}, allocation_type);
  }

  int current_size = current_allocation_block_->size();
  if (current_size + vobject->size() > kMaxRegularHeapObjectSize) {
    current_allocation_block_ =
        AddNewNode<AllocationBlock>({}, allocation_type);
  }

  DCHECK_GE(current_size, 0);
  InlinedAllocation* allocation =
      AddNewNode<InlinedAllocation>({current_allocation_block_}, vobject);
  graph()->allocations_escape_map().emplace(allocation, zone());
  current_allocation_block_->Add(allocation);
  vobject->set_allocation(allocation);
  return allocation;
}

void MaglevGraphBuilder::ClearCurrentAllocationBlock() {
  current_allocation_block_ = nullptr;
}

void MaglevGraphBuilder::AddNonEscapingUses(InlinedAllocation* allocation,
                                            int use_count) {
  if (!v8_flags.maglev_escape_analysis) return;
  allocation->AddNonEscapingUses(use_count);
}

void MaglevGraphBuilder::AddDeoptUse(VirtualObject* vobject) {
  if (vobject->type() != VirtualObject::kDefault) return;
  for (uint32_t i = 0; i < vobject->slot_count(); i++) {
    ValueNode* value = vobject->get_by_index(i);
    if (InlinedAllocation* nested_allocation =
            value->TryCast<InlinedAllocation>()) {
      VirtualObject* nested_object =
          current_interpreter_frame_.virtual_objects().FindAllocatedWith(
              nested_allocation);
      CHECK_NOT_NULL(nested_object);
      AddDeoptUse(nested_object);
    } else if (!IsConstantNode(value->opcode()) &&
               value->opcode() != Opcode::kArgumentsElements &&
               value->opcode() != Opcode::kArgumentsLength &&
               value->opcode() != Opcode::kRestLength) {
      AddDeoptUse(value);
    }
  }
}

ValueNode* MaglevGraphBuilder::BuildInlinedAllocationForHeapNumber(
    VirtualObject* vobject, AllocationType allocation_type) {
  DCHECK(vobject->map().IsHeapNumberMap());
  InlinedAllocation* allocation =
      ExtendOrReallocateCurrentAllocationBlock(allocation_type, vobject);
  AddNonEscapingUses(allocation, 2);
  BuildStoreMap(allocation, broker()->heap_number_map(),
                StoreMap::initializing_kind(allocation_type));
  AddNewNode<StoreFloat64>({allocation, GetFloat64Constant(vobject->number())},
                           static_cast<int>(offsetof(HeapNumber, value_)));
  return allocation;
}

ValueNode* MaglevGraphBuilder::BuildInlinedAllocationForDoubleFixedArray(
    VirtualObject* vobject, AllocationType allocation_type) {
  DCHECK(vobject->map().IsFixedDoubleArrayMap());
  InlinedAllocation* allocation =
      ExtendOrReallocateCurrentAllocationBlock(allocation_type, vobject);
  int length = vobject->double_elements_length();
  AddNonEscapingUses(allocation, length + 2);
  BuildStoreMap(allocation, broker()->fixed_double_array_map(),
                StoreMap::initializing_kind(allocation_type));
  AddNewNode<StoreTaggedFieldNoWriteBarrier>(
      {allocation, GetSmiConstant(length)},
      static_cast<int>(offsetof(FixedDoubleArray, length_)),
      StoreTaggedMode::kDefault);
  for (int i = 0; i < length; ++i) {
    AddNewNode<StoreFloat64>(
        {allocation,
         GetFloat64Constant(
             vobject->double_elements().GetFromImmutableFixedDoubleArray(i))},
        FixedDoubleArray::OffsetOfElementAt(i));
  }
  return allocation;
}

ValueNode* MaglevGraphBuilder::BuildInlinedAllocation(
    VirtualObject* vobject, AllocationType allocation_type) {
  current_interpreter_frame_.add_object(vobject);
  if (vobject->type() == VirtualObject::kHeapNumber) {
    return BuildInlinedAllocationForHeapNumber(vobject, allocation_type);
  }
  if (vobject->type() == VirtualObject::kFixedDoubleArray) {
    return BuildInlinedAllocationForDoubleFixedArray(vobject, allocation_type);
  }
  SmallZoneVector<ValueNode*, 8> values(vobject->slot_count(), zone());
  for (uint32_t i = 0; i < vobject->slot_count(); i++) {
    ValueNode* node = vobject->get_by_index(i);
    if (node->Is<VirtualObject>()) {
      VirtualObject* nested = node->Cast<VirtualObject>();
      node = BuildInlinedAllocation(nested, allocation_type);
      vobject->set_by_index(i, node);
    } else if (node->Is<Float64Constant>()) {
      node = BuildInlinedAllocationForHeapNumber(
          CreateHeapNumber(node->Cast<Float64Constant>()->value()),
          allocation_type);
    } else {
      node = GetTaggedValue(node);
    }
    values[i] = node;
  }
  InlinedAllocation* allocation =
      ExtendOrReallocateCurrentAllocationBlock(allocation_type, vobject);
  AddNonEscapingUses(allocation, vobject->slot_count() + 1);
  BuildStoreMap(allocation, vobject->map(),
                StoreMap::initializing_kind(allocation_type));
  for (uint32_t i = 0; i < vobject->slot_count(); i++) {
    BuildInitializeStore(allocation, values[i], (i + 1) * kTaggedSize);
  }
  if (is_loop_effect_tracking()) {
    loop_effects_->allocations.insert(allocation);
  }
  return allocation;
}

ValueNode* MaglevGraphBuilder::BuildInlinedArgumentsElements(int start_index,
                                                             int length) {
  DCHECK(is_inline());
  if (length == 0) {
    return GetRootConstant(RootIndex::kEmptyFixedArray);
  }
  VirtualObject* elements =
      CreateFixedArray(broker()->fixed_array_map(), length);
  for (int i = 0; i < length; i++) {
    elements->set(FixedArray::OffsetOfElementAt(i),
                  inlined_arguments_[i + start_index + 1]);
  }
  return elements;
}

ValueNode* MaglevGraphBuilder::BuildInlinedUnmappedArgumentsElements(
    int mapped_count) {
  int length = argument_count_without_receiver();
  if (length == 0) {
    return GetRootConstant(RootIndex::kEmptyFixedArray);
  }
  VirtualObject* unmapped_elements =
      CreateFixedArray(broker()->fixed_array_map(), length);
  int i = 0;
  for (; i < mapped_count; i++) {
    unmapped_elements->set(FixedArray::OffsetOfElementAt(i),
                           GetRootConstant(RootIndex::kTheHoleValue));
  }
  for (; i < length; i++) {
    unmapped_elements->set(FixedArray::OffsetOfElementAt(i),
                           inlined_arguments_[i + 1]);
  }
  return unmapped_elements;
}

template <CreateArgumentsType type>
VirtualObject* MaglevGraphBuilder::BuildVirtualArgumentsObject() {
  switch (type) {
    case CreateArgumentsType::kMappedArguments:
      if (parameter_count_without_receiver() == 0) {
        // If there is no aliasing, the arguments object elements are not
        // special in any way, we can just return an unmapped backing store.
        if (is_inline()) {
          int length = argument_count_without_receiver();
          ValueNode* elements = BuildInlinedArgumentsElements(0, length);
          return CreateArgumentsObject(
              broker()->target_native_context().sloppy_arguments_map(broker()),
              GetInt32Constant(length), elements, GetClosure());
        } else {
          ArgumentsLength* length = AddNewNode<ArgumentsLength>({});
          EnsureType(length, NodeType::kSmi);
          ArgumentsElements* elements = AddNewNode<ArgumentsElements>(
              {length}, CreateArgumentsType::kUnmappedArguments,
              parameter_count_without_receiver());
          return CreateArgumentsObject(
              broker()->target_native_context().sloppy_arguments_map(broker()),
              length, elements, GetClosure());
        }
      } else {
        // If the parameter count is zero, we should have used the unmapped
        // backing store.
        int param_count = parameter_count_without_receiver();
        DCHECK_GT(param_count, 0);
        DCHECK(CanAllocateSloppyArgumentElements());
        int param_idx_in_ctxt = compilation_unit_->shared_function_info()
                                    .context_parameters_start() +
                                param_count - 1;
        // The {unmapped_elements} correspond to the extra arguments
        // (overapplication) that do not need be "mapped" to the actual
        // arguments. Mapped arguments are accessed via the context, whereas
        // unmapped arguments are simply accessed via this fixed array. See
        // SloppyArgumentsElements in src/object/arguments.h.
        if (is_inline()) {
          int length = argument_count_without_receiver();
          int mapped_count = std::min(param_count, length);
          ValueNode* unmapped_elements =
              BuildInlinedUnmappedArgumentsElements(mapped_count);
          VirtualObject* elements = CreateMappedArgumentsElements(
              broker()->sloppy_arguments_elements_map(), mapped_count,
              GetContext(), unmapped_elements);
          for (int i = 0; i < mapped_count; i++, param_idx_in_ctxt--) {
            elements->set(SloppyArgumentsElements::OffsetOfElementAt(i),
                          GetInt32Constant(param_idx_in_ctxt));
          }
          return CreateArgumentsObject(
              broker()->target_native_context().fast_aliased_arguments_map(
                  broker()),
              GetInt32Constant(length), elements, GetClosure());
        } else {
          ArgumentsLength* length = AddNewNode<ArgumentsLength>({});
          EnsureType(length, NodeType::kSmi);
          ArgumentsElements* unmapped_elements = AddNewNode<ArgumentsElements>(
              {length}, CreateArgumentsType::kMappedArguments, param_count);
          VirtualObject* elements = CreateMappedArgumentsElements(
              broker()->sloppy_arguments_elements_map(), param_count,
              GetContext(), unmapped_elements);
          ValueNode* the_hole_value = GetConstant(broker()->the_hole_value());
          for (int i = 0; i < param_count; i++, param_idx_in_ctxt--) {
            ValueNode* value = Select(
                [&](auto& builder) {
                  return BuildBranchIfInt32Compare(builder,
                                                   Operation::kLessThan,
                                                   GetInt32Constant(i), length);
                },
                [&] { return GetSmiConstant(param_idx_in_ctxt); },
                [&] { return the_hole_value; });
            elements->set(SloppyArgumentsElements::OffsetOfElementAt(i), value);
          }
          return CreateArgumentsObject(
              broker()->target_native_context().fast_aliased_arguments_map(
                  broker()),
              length, elements, GetClosure());
        }
      }
    case CreateArgumentsType::kUnmappedArguments:
      if (is_inline()) {
        int length = argument_count_without_receiver();
        ValueNode* elements = BuildInlinedArgumentsElements(0, length);
        return CreateArgumentsObject(
            broker()->target_native_context().strict_arguments_map(broker()),
            GetInt32Constant(length), elements);
      } else {
        ArgumentsLength* length = AddNewNode<ArgumentsLength>({});
        EnsureType(length, NodeType::kSmi);
        ArgumentsElements* elements = AddNewNode<ArgumentsElements>(
            {length}, CreateArgumentsType::kUnmappedArguments,
            parameter_count_without_receiver());
        return CreateArgumentsObject(
            broker()->target_native_context().strict_arguments_map(broker()),
            length, elements);
      }
    case CreateArgumentsType::kRestParameter:
      if (is_inline()) {
        int start_index = parameter_count_without_receiver();
        int length =
            std::max(0, argument_count_without_receiver() - start_index);
        ValueNode* elements =
            BuildInlinedArgumentsElements(start_index, length);
        return CreateArgumentsObject(
            broker()->target_native_context().js_array_packed_elements_map(
                broker()),
            GetInt32Constant(length), elements);
      } else {
        ArgumentsLength* length = AddNewNode<ArgumentsLength>({});
        EnsureType(length, NodeType::kSmi);
        ArgumentsElements* elements = AddNewNode<ArgumentsElements>(
            {length}, CreateArgumentsType::kRestParameter,
            parameter_count_without_receiver());
        RestLength* rest_length =
            AddNewNode<RestLength>({}, parameter_count_without_receiver());
        return CreateArgumentsObject(
            broker()->target_native_context().js_array_packed_elements_map(
                broker()),
            rest_length, elements);
      }
  }
}

template <CreateArgumentsType type>
ValueNode* MaglevGraphBuilder::BuildAndAllocateArgumentsObject() {
  auto arguments = BuildVirtualArgumentsObject<type>();
  ValueNode* allocation =
      BuildInlinedAllocation(arguments, AllocationType::kYoung);
  // TODO(leszeks): Don't eagerly clear the raw allocation, have the next side
  // effect clear it.
  ClearCurrentAllocationBlock();
  return allocation;
}

MaybeReduceResult MaglevGraphBuilder::TryBuildFastCreateObjectOrArrayLiteral(
    const compiler::LiteralFeedback& feedback) {
  compiler::AllocationSiteRef site = feedback.value();
  if (!site.boilerplate(broker()).has_value()) return {};
  AllocationType allocation_type =
      broker()->dependencies()->DependOnPretenureMode(site);

  // First try to extract out the shape and values of the boilerplate, bailing
  // out on complex boilerplates.
  int max_properties = compiler::kMaxFastLiteralProperties;
  std::optional<VirtualObject*> maybe_value = TryReadBoilerplateForFastLiteral(
      *site.boilerplate(broker()), allocation_type,
      compiler::kMaxFastLiteralDepth, &max_properties);
  if (!maybe_value.has_value()) return {};

  // Then, use the collected information to actually create nodes in the graph.
  // TODO(leszeks): Add support for unwinding graph modifications, so that we
  // can get rid of this two pass approach.
  broker()->dependencies()->DependOnElementsKinds(site);
  MaybeReduceResult result =
      BuildInlinedAllocation(*maybe_value, allocation_type);
  // TODO(leszeks): Don't eagerly clear the raw allocation, have the next side
  // effect clear it.
  ClearCurrentAllocationBlock();
  return result;
}

ReduceResult MaglevGraphBuilder::VisitCreateObjectLiteral() {
  compiler::ObjectBoilerplateDescriptionRef boilerplate_desc =
      GetRefOperand<ObjectBoilerplateDescription>(0);
  FeedbackSlot slot_index = GetSlotOperand(1);
  int bytecode_flags = GetFlag8Operand(2);
  int literal_flags =
      interpreter::CreateObjectLiteralFlags::FlagsBits::decode(bytecode_flags);
  compiler::FeedbackSource feedback_source(feedback(), slot_index);

  compiler::ProcessedFeedback const& processed_feedback =
      broker()->GetFeedbackForArrayOrObjectLiteral(feedback_source);
  if (processed_feedback.IsInsufficient()) {
    return EmitUnconditionalDeopt(
        DeoptimizeReason::kInsufficientTypeFeedbackForObjectLiteral);
  }

  MaybeReduceResult result =
      TryBuildFastCreateObjectOrArrayLiteral(processed_feedback.AsLiteral());
  PROCESS_AND_RETURN_IF_DONE(result, SetAccumulator);

  if (interpreter::CreateObjectLiteralFlags::FastCloneSupportedBit::decode(
          bytecode_flags)) {
    // TODO(victorgomes): CreateShallowObjectLiteral should not need the
    // boilerplate descriptor. However the current builtin checks that the
    // feedback exists and fallsback to CreateObjectLiteral if it doesn't.
    SetAccumulator(AddNewNode<CreateShallowObjectLiteral>(
        {}, boilerplate_desc, feedback_source, literal_flags));
  } else {
    SetAccumulator(AddNewNode<CreateObjectLiteral>(
        {}, boilerplate_desc, feedback_source, literal_flags));
  }

  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitCreateEmptyObjectLiteral() {
  compiler::NativeContextRef native_context = broker()->target_native_context();
  compiler::MapRef map =
      native_context.object_function(broker()).initial_map(broker());
  DCHECK(!map.is_dictionary_map());
  DCHECK(!map.IsInobjectSlackTrackingInProgress());
  SetAccumulator(
      BuildInlinedAllocation(CreateJSObject(map), AllocationType::kYoung));
  // TODO(leszeks): Don't eagerly clear the raw allocation, have the next side
  // effect clear it.
  ClearCurrentAllocationBlock();
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitCloneObject() {
  // CloneObject <source_idx> <flags> <feedback_slot>
  ValueNode* source = LoadRegister(0);
  ValueNode* flags =
      GetSmiConstant(interpreter::CreateObjectLiteralFlags::FlagsBits::decode(
          GetFlag8Operand(1)));
  FeedbackSlot slot = GetSlotOperand(2);
  compiler::FeedbackSource feedback_source{feedback(), slot};
  SetAccumulator(BuildCallBuiltin<Builtin::kCloneObjectIC>(
      {GetTaggedValue(source), flags}, feedback_source));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitGetTemplateObject() {
  // GetTemplateObject <descriptor_idx> <literal_idx>
  compiler::SharedFunctionInfoRef shared_function_info =
      compilation_unit_->shared_function_info();
  ValueNode* description = GetConstant(GetRefOperand<HeapObject>(0));
  FeedbackSlot slot = GetSlotOperand(1);
  compiler::FeedbackSource feedback_source{feedback(), slot};

  const compiler::ProcessedFeedback& feedback =
      broker()->GetFeedbackForTemplateObject(feedback_source);
  if (feedback.IsInsufficient()) {
    SetAccumulator(AddNewNode<GetTemplateObject>(
        {description}, shared_function_info, feedback_source));
    return ReduceResult::Done();
  }
  compiler::JSArrayRef template_object = feedback.AsTemplateObject().value();
  SetAccumulator(GetConstant(template_object));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitCreateClosure() {
  compiler::SharedFunctionInfoRef shared_function_info =
      GetRefOperand<SharedFunctionInfo>(0);
  compiler::FeedbackCellRef feedback_cell =
      feedback().GetClosureFeedbackCell(broker(), iterator_.GetIndexOperand(1));
  uint32_t flags = GetFlag8Operand(2);

  if (interpreter::CreateClosureFlags::FastNewClosureBit::decode(flags)) {
    SetAccumulator(AddNewNode<FastCreateClosure>(
        {GetContext()}, shared_function_info, feedback_cell));
  } else {
    bool pretenured =
        interpreter::CreateClosureFlags::PretenuredBit::decode(flags);
    SetAccumulator(AddNewNode<CreateClosure>(
        {GetContext()}, shared_function_info, feedback_cell, pretenured));
  }
  return ReduceResult::Done();
}

MaybeReduceResult MaglevGraphBuilder::TryBuildInlinedAllocatedContext(
    compiler::MapRef map, compiler::ScopeInfoRef scope, int context_length) {
  const int kContextAllocationLimit = 16;
  if (context_length > kContextAllocationLimit) return {};
  DCHECK_GE(context_length, Context::MIN_CONTEXT_SLOTS);
  auto context = CreateContext(map, context_length, scope, GetContext());
  ValueNode* result = BuildInlinedAllocation(context, AllocationType::kYoung);
  // TODO(leszeks): Don't eagerly clear the raw allocation, have the next side
  // effect clear it.
  ClearCurrentAllocationBlock();
  return result;
}

ReduceResult MaglevGraphBuilder::VisitCreateBlockContext() {
  // CreateBlockContext <scope_info_idx>
  compiler::ScopeInfoRef scope_info = GetRefOperand<ScopeInfo>(0);
  compiler::MapRef map =
      broker()->target_native_context().block_context_map(broker());

  auto done = [&](ValueNode* res) {
    graph()->record_scope_info(res, scope_info);
    SetAccumulator(res);
  };

  PROCESS_AND_RETURN_IF_DONE(TryBuildInlinedAllocatedContext(
                                 map, scope_info, scope_info.ContextLength()),
                             done);
  // Fallback.
  done(BuildCallRuntime(Runtime::kPushBlockContext, {GetConstant(scope_info)})
           .value());
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitCreateCatchContext() {
  // CreateCatchContext <exception> <scope_info_idx>
  ValueNode* exception = LoadRegister(0);
  compiler::ScopeInfoRef scope_info = GetRefOperand<ScopeInfo>(1);
  auto context = CreateContext(
      broker()->target_native_context().catch_context_map(broker()),
      Context::MIN_CONTEXT_EXTENDED_SLOTS, scope_info, GetContext(), exception);
  SetAccumulator(BuildInlinedAllocation(context, AllocationType::kYoung));
  graph()->record_scope_info(GetAccumulator(), scope_info);
  // TODO(leszeks): Don't eagerly clear the raw allocation, have the next side
  // effect clear it.
  ClearCurrentAllocationBlock();
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitCreateFunctionContext() {
  compiler::ScopeInfoRef info = GetRefOperand<ScopeInfo>(0);
  uint32_t slot_count = iterator_.GetUnsignedImmediateOperand(1);
  compiler::MapRef map =
      broker()->target_native_context().function_context_map(broker());

  auto done = [&](ValueNode* res) {
    graph()->record_scope_info(res, info);
    SetAccumulator(res);
  };

  PROCESS_AND_RETURN_IF_DONE(
      TryBuildInlinedAllocatedContext(map, info,
                                      slot_count + Context::MIN_CONTEXT_SLOTS),
      done);
  // Fallback.
  done(AddNewNode<CreateFunctionContext>({GetContext()}, info, slot_count,
                                         ScopeType::FUNCTION_SCOPE));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitCreateEvalContext() {
  compiler::ScopeInfoRef info = GetRefOperand<ScopeInfo>(0);
  uint32_t slot_count = iterator_.GetUnsignedImmediateOperand(1);
  compiler::MapRef map =
      broker()->target_native_context().eval_context_map(broker());

  auto done = [&](ValueNode* res) {
    graph()->record_scope_info(res, info);
    SetAccumulator(res);
  };

  PROCESS_AND_RETURN_IF_DONE(
      TryBuildInlinedAllocatedContext(map, info,
                                      slot_count + Context::MIN_CONTEXT_SLOTS),
      done);
  if (slot_count <= static_cast<uint32_t>(
                        ConstructorBuiltins::MaximumFunctionContextSlots())) {
    done(AddNewNode<CreateFunctionContext>({GetContext()}, info, slot_count,
                                           ScopeType::EVAL_SCOPE));
  } else {
    done(BuildCallRuntime(Runtime::kNewFunctionContext, {GetConstant(info)})
             .value());
  }
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitCreateWithContext() {
  // CreateWithContext <register> <scope_info_idx>
  ValueNode* object = LoadRegister(0);
  compiler::ScopeInfoRef scope_info = GetRefOperand<ScopeInfo>(1);
  auto context = CreateContext(
      broker()->target_native_context().with_context_map(broker()),
      Context::MIN_CONTEXT_EXTENDED_SLOTS, scope_info, GetContext(), object);
  SetAccumulator(BuildInlinedAllocation(context, AllocationType::kYoung));
  graph()->record_scope_info(GetAccumulator(), scope_info);
  // TODO(leszeks): Don't eagerly clear the raw allocation, have the next side
  // effect clear it.
  ClearCurrentAllocationBlock();
  return ReduceResult::Done();
}

bool MaglevGraphBuilder::CanAllocateSloppyArgumentElements() {
  return SloppyArgumentsElements::SizeFor(parameter_count()) <=
         kMaxRegularHeapObjectSize;
}

bool MaglevGraphBuilder::CanAllocateInlinedArgumentElements() {
  DCHECK(is_inline());
  return FixedArray::SizeFor(argument_count_without_receiver()) <=
         kMaxRegularHeapObjectSize;
}

ReduceResult MaglevGraphBuilder::VisitCreateMappedArguments() {
  compiler::SharedFunctionInfoRef shared =
      compilation_unit_->shared_function_info();
  if (!shared.object()->has_duplicate_parameters()) {
    if (((is_inline() && CanAllocateInlinedArgumentElements()) ||
         (!is_inline() && CanAllocateSloppyArgumentElements()))) {
      SetAccumulator(BuildAndAllocateArgumentsObject<
                     CreateArgumentsType::kMappedArguments>());
      return ReduceResult::Done();
    } else if (!is_inline()) {
      SetAccumulator(
          BuildCallBuiltin<Builtin::kFastNewSloppyArguments>({GetClosure()}));
      return ReduceResult::Done();
    }
  }
  // Generic fallback.
  SetAccumulator(
      BuildCallRuntime(Runtime::kNewSloppyArguments, {GetClosure()}).value());
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitCreateUnmappedArguments() {
  if (!is_inline() || CanAllocateInlinedArgumentElements()) {
    SetAccumulator(BuildAndAllocateArgumentsObject<
                   CreateArgumentsType::kUnmappedArguments>());
    return ReduceResult::Done();
  }
  // Generic fallback.
  SetAccumulator(
      BuildCallRuntime(Runtime::kNewStrictArguments, {GetClosure()}).value());
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitCreateRestParameter() {
  if (!is_inline() || CanAllocateInlinedArgumentElements()) {
    SetAccumulator(
        BuildAndAllocateArgumentsObject<CreateArgumentsType::kRestParameter>());
    return ReduceResult::Done();
  }
  // Generic fallback.
  SetAccumulator(
      BuildCallRuntime(Runtime::kNewRestParameter, {GetClosure()}).value());
  return ReduceResult::Done();
}

void MaglevGraphBuilder::PeelLoop() {
  int loop_header = iterator_.current_offset();
  DCHECK(loop_headers_to_peel_.Contains(loop_header));
  DCHECK(!in_peeled_iteration());
  peeled_iteration_count_ = v8_flags.maglev_optimistic_peeled_loops ? 2 : 1;
  any_peeled_loop_ = true;
  allow_loop_peeling_ = false;

  if (v8_flags.trace_maglev_graph_building) {
    std::cout << "  * Begin loop peeling...." << std::endl;
  }

  while (in_peeled_iteration()) {
    BuildLoopForPeeling();
  }
  // Emit the actual (not peeled) loop if needed.
  if (loop_header == iterator_.current_offset()) {
    BuildLoopForPeeling();
  }
  allow_loop_peeling_ = true;
}

void MaglevGraphBuilder::BuildLoopForPeeling() {
  int loop_header = iterator_.current_offset();
  DCHECK(loop_headers_to_peel_.Contains(loop_header));

  // Since peeled loops do not start with a loop merge state, we need to
  // explicitly enter e loop effect tracking scope for the peeled iteration.
  bool track_peeled_effects =
      v8_flags.maglev_optimistic_peeled_loops && peeled_iteration_count_ == 2;
  if (track_peeled_effects) {
    BeginLoopEffects(loop_header);
  }

#ifdef DEBUG
  bool was_in_peeled_iteration = in_peeled_iteration();
#endif  // DEBUG

  while (iterator_.current_bytecode() != interpreter::Bytecode::kJumpLoop) {
    local_isolate_->heap()->Safepoint();
    VisitSingleBytecode();
    iterator_.Advance();
  }

  VisitSingleBytecode();  // VisitJumpLoop

  DCHECK_EQ(was_in_peeled_iteration, in_peeled_iteration());
  if (!in_peeled_iteration()) {
    return;
  }

  // In case the peeled iteration was mergeable (see TryMergeLoop) or the
  // JumpLoop was dead, we are done.
  if (!current_block_) {
    decremented_predecessor_offsets_.clear();
    KillPeeledLoopTargets(peeled_iteration_count_);
    peeled_iteration_count_ = 0;
    if (track_peeled_effects) {
      EndLoopEffects(loop_header);
    }
    return;
  }

  peeled_iteration_count_--;

  // After processing the peeled iteration and reaching the `JumpLoop`, we
  // re-process the loop body. For this, we need to reset the graph building
  // state roughly as if we didn't process it yet.

  // Reset position in exception handler table to before the loop.
  HandlerTable table(*bytecode().object());
  while (next_handler_table_index_ > 0) {
    next_handler_table_index_--;
    int start = table.GetRangeStart(next_handler_table_index_);
    if (start < loop_header) break;
  }

  // Re-create catch handler merge states.
  for (int offset = loop_header; offset <= iterator_.current_offset();
       ++offset) {
    if (auto& merge_state = merge_states_[offset]) {
      if (merge_state->is_exception_handler()) {
        merge_state = MergePointInterpreterFrameState::NewForCatchBlock(
            *compilation_unit_, merge_state->frame_state().liveness(), offset,
            merge_state->exception_handler_was_used(),
            merge_state->catch_block_context_register(), graph_);
      } else {
        // We only peel innermost loops.
        DCHECK(!merge_state->is_loop());
        merge_state = nullptr;
      }
    }
    new (&jump_targets_[offset]) BasicBlockRef();
  }

  // Reset predecessors as if the loop body had not been visited.
  for (int offset : decremented_predecessor_offsets_) {
    DCHECK_GE(offset, loop_header);
    if (offset <= iterator_.current_offset()) {
      UpdatePredecessorCount(offset, 1);
    }
  }
  decremented_predecessor_offsets_.clear();

  DCHECK(current_block_);
  // After resetting, the actual loop header always has exactly 2
  // predecessors: the two copies of `JumpLoop`.
  InitializePredecessorCount(loop_header, 2);
  merge_states_[loop_header] = MergePointInterpreterFrameState::NewForLoop(
      current_interpreter_frame_, *compilation_unit_, loop_header, 2,
      GetInLivenessFor(loop_header),
      &bytecode_analysis_.GetLoopInfoFor(loop_header),
      /* has_been_peeled */ true);

  BasicBlock* block = FinishBlock<Jump>({}, &jump_targets_[loop_header]);
  // If we ever want more peelings, we should ensure that only the last one
  // creates a loop header.
  DCHECK_LE(peeled_iteration_count_, 1);
  DCHECK_IMPLIES(in_peeled_iteration(),
                 v8_flags.maglev_optimistic_peeled_loops);
  merge_states_[loop_header]->InitializeLoop(
      this, *compilation_unit_, current_interpreter_frame_, block,
      in_peeled_iteration(), loop_effects_);

  if (track_peeled_effects) {
    EndLoopEffects(loop_header);
  }
  DCHECK_NE(iterator_.current_offset(), loop_header);
  iterator_.SetOffset(loop_header);
}

void MaglevGraphBuilder::OsrAnalyzePrequel() {
  DCHECK_EQ(compilation_unit_->info()->toplevel_compilation_unit(),
            compilation_unit_);

  // TODO(olivf) We might want to start collecting known_node_aspects_ here.
  for (iterator_.SetOffset(0); iterator_.current_offset() != entrypoint_;
       iterator_.Advance()) {
    switch (iterator_.current_bytecode()) {
      case interpreter::Bytecode::kPushContext: {
        graph()->record_scope_info(GetContext(), {});
        // Nothing left to analyze...
        return;
      }
      default:
        continue;
    }
  }
}

void MaglevGraphBuilder::BeginLoopEffects(int loop_header) {
  loop_effects_stack_.push_back(zone()->New<LoopEffects>(loop_header, zone()));
  loop_effects_ = loop_effects_stack_.back();
}

void MaglevGraphBuilder::EndLoopEffects(int loop_header) {
  DCHECK_EQ(loop_effects_, loop_effects_stack_.back());
  DCHECK_EQ(loop_effects_->loop_header, loop_header);
  // TODO(olivf): Update merge states dominated by the loop header with
  // information we know to be unaffected by the loop.
  if (merge_states_[loop_header] && merge_states_[loop_header]->is_loop()) {
    merge_states_[loop_header]->set_loop_effects(loop_effects_);
  }
  if (loop_effects_stack_.size() > 1) {
    LoopEffects* inner_effects = loop_effects_;
    loop_effects_ = *(loop_effects_stack_.end() - 2);
    loop_effects_->Merge(inner_effects);
  } else {
    loop_effects_ = nullptr;
  }
  loop_effects_stack_.pop_back();
}

ReduceResult MaglevGraphBuilder::VisitJumpLoop() {
  const uint32_t relative_jump_bytecode_offset =
      iterator_.GetUnsignedImmediateOperand(0);
  const int32_t loop_offset = iterator_.GetImmediateOperand(1);
  const FeedbackSlot feedback_slot = iterator_.GetSlotOperand(2);
  int target = iterator_.GetJumpTargetOffset();

  if (ShouldEmitInterruptBudgetChecks()) {
    int reduction = relative_jump_bytecode_offset *
                    v8_flags.osr_from_maglev_interrupt_scale_factor;
    AddNewNode<ReduceInterruptBudgetForLoop>({}, reduction > 0 ? reduction : 1);
  } else {
    AddNewNode<HandleNoHeapWritesInterrupt>({});
  }

  if (ShouldEmitOsrInterruptBudgetChecks()) {
    AddNewNode<TryOnStackReplacement>(
        {GetClosure()}, loop_offset, feedback_slot,
        BytecodeOffset(iterator_.current_offset()), compilation_unit_);
  }

  bool is_peeled_loop = loop_headers_to_peel_.Contains(target);
  auto FinishLoopBlock = [&]() {
    return FinishBlock<JumpLoop>({}, jump_targets_[target].block_ptr());
  };
  if (is_peeled_loop && in_peeled_iteration()) {
    ClobberAccumulator();
    if (in_optimistic_peeling_iteration()) {
      // Let's see if we can finish this loop without peeling it.
      if (!merge_states_[target]->TryMergeLoop(this, current_interpreter_frame_,
                                               FinishLoopBlock)) {
        merge_states_[target]->MergeDeadLoop(*compilation_unit());
      }
      if (is_loop_effect_tracking_enabled()) {
        EndLoopEffects(target);
      }
    }
  } else {
    BasicBlock* block = FinishLoopBlock();
    merge_states_[target]->MergeLoop(this, current_interpreter_frame_, block);
    block->set_predecessor_id(merge_states_[target]->predecessor_count() - 1);
    if (is_peeled_loop) {
      DCHECK(!in_peeled_iteration());
    }
    if (is_loop_effect_tracking_enabled()) {
      EndLoopEffects(target);
    }
  }
  return ReduceResult::Done();
}
ReduceResult MaglevGraphBuilder::VisitJump() {
  BasicBlock* block =
      FinishBlock<Jump>({}, &jump_targets_[iterator_.GetJumpTargetOffset()]);
  MergeIntoFrameState(block, iterator_.GetJumpTargetOffset());
  DCHECK_EQ(current_block_, nullptr);
  DCHECK_LT(next_offset(), bytecode().length());
  return ReduceResult::Done();
}
ReduceResult MaglevGraphBuilder::VisitJumpConstant() { return VisitJump(); }
ReduceResult MaglevGraphBuilder::VisitJumpIfNullConstant() {
  return VisitJumpIfNull();
}
ReduceResult MaglevGraphBuilder::VisitJumpIfNotNullConstant() {
  return VisitJumpIfNotNull();
}
ReduceResult MaglevGraphBuilder::VisitJumpIfUndefinedConstant() {
  return VisitJumpIfUndefined();
}
ReduceResult MaglevGraphBuilder::VisitJumpIfNotUndefinedConstant() {
  return VisitJumpIfNotUndefined();
}
ReduceResult MaglevGraphBuilder::VisitJumpIfUndefinedOrNullConstant() {
  return VisitJumpIfUndefinedOrNull();
}
ReduceResult MaglevGraphBuilder::VisitJumpIfTrueConstant() {
  return VisitJumpIfTrue();
}
ReduceResult MaglevGraphBuilder::VisitJumpIfFalseConstant() {
  return VisitJumpIfFalse();
}
ReduceResult MaglevGraphBuilder::VisitJumpIfJSReceiverConstant() {
  return VisitJumpIfJSReceiver();
}
ReduceResult MaglevGraphBuilder::VisitJumpIfForInDoneConstant() {
  return VisitJumpIfForInDone();
}
ReduceResult MaglevGraphBuilder::VisitJumpIfToBooleanTrueConstant() {
  return VisitJumpIfToBooleanTrue();
}
ReduceResult MaglevGraphBuilder::VisitJumpIfToBooleanFalseConstant() {
  return VisitJumpIfToBooleanFalse();
}

void MaglevGraphBuilder::MergeIntoFrameState(BasicBlock* predecessor,
                                             int target) {
  if (merge_states_[target] == nullptr) {
    bool jumping_to_peeled_iteration = bytecode_analysis().IsLoopHeader(target);
    DCHECK_EQ(jumping_to_peeled_iteration,
              loop_headers_to_peel_.Contains(target));
    const compiler::BytecodeLivenessState* liveness = GetInLivenessFor(target);
    if (jumping_to_peeled_iteration) {
      // The peeled iteration is missing the backedge.
      DecrementDeadPredecessorAndAccountForPeeling(target);
    }
    // If there's no target frame state, allocate a new one.
    merge_states_[target] = MergePointInterpreterFrameState::New(
        *compilation_unit_, current_interpreter_frame_, target,
        predecessor_count(target), predecessor, liveness);
  } else {
    // If there already is a frame state, merge.
    merge_states_[target]->Merge(this, current_interpreter_frame_, predecessor);
  }
}

void MaglevGraphBuilder::MergeDeadIntoFrameState(int target) {
  // If there already is a frame state, merge.
  if (merge_states_[target]) {
    DCHECK_EQ(merge_states_[target]->predecessor_count(),
              predecessor_count(target));
    merge_states_[target]->MergeDead(*compilation_unit_);
    // If this merge is the last one which kills a loop merge, remove that
    // merge state.
    if (merge_states_[target]->is_unmerged_unreachable_loop()) {
      if (v8_flags.trace_maglev_graph_building) {
        std::cout << "! Killing loop merge state at @" << target << std::endl;
      }
      merge_states_[target] = nullptr;
    }
  }
  // If there is no merge state yet, don't create one, but just reduce the
  // number of possible predecessors to zero.
  DecrementDeadPredecessorAndAccountForPeeling(target);
}

void MaglevGraphBuilder::MergeDeadLoopIntoFrameState(int target) {
  // Check if the Loop entry is dead already (e.g. an outer loop from OSR).
  if (V8_UNLIKELY(!merge_states_[target]) && predecessor_count(target) == 0) {
    static_assert(kLoopsMustBeEnteredThroughHeader);
    return;
  }
  // If there already is a frame state, merge.
  if (V8_LIKELY(merge_states_[target])) {
    DCHECK_EQ(merge_states_[target]->predecessor_count(),
              predecessor_count(target));
    if (is_loop_effect_tracking_enabled() &&
        !merge_states_[target]->is_unmerged_unreachable_loop()) {
      EndLoopEffects(target);
    }
    merge_states_[target]->MergeDeadLoop(*compilation_unit_);
  }
  // If there is no merge state yet, don't create one, but just reduce the
  // number of possible predecessors to zero.
  DecrementDeadPredecessorAndAccountForPeeling(target);
}

void MaglevGraphBuilder::MergeIntoInlinedReturnFrameState(
    BasicBlock* predecessor) {
  int target = inline_exit_offset();
  if (merge_states_[target] == nullptr) {
    // All returns should have the same liveness, which is that only the
    // accumulator is live.
    const compiler::BytecodeLivenessState* liveness = GetInLiveness();
    DCHECK(liveness->AccumulatorIsLive());
    DCHECK_EQ(liveness->live_value_count(), 1);

    // If there's no target frame state, allocate a new one.
    merge_states_[target] = MergePointInterpreterFrameState::New(
        *compilation_unit_, current_interpreter_frame_, target,
        predecessor_count(target), predecessor, liveness);
  } else {
    // Again, all returns should have the same liveness, so double check this.
    DCHECK(GetInLiveness()->Equals(
        *merge_states_[target]->frame_state().liveness()));
    merge_states_[target]->Merge(this, current_interpreter_frame_, predecessor);
  }
}

MaglevGraphBuilder::BranchResult
MaglevGraphBuilder::BuildBranchIfReferenceEqual(BranchBuilder& builder,
                                                ValueNode* lhs,
                                                ValueNode* rhs) {
  if (RootConstant* root_constant = rhs->TryCast<RootConstant>()) {
    return builder.Build<BranchIfRootConstant>({lhs}, root_constant->index());
  }
  if (RootConstant* root_constant = lhs->TryCast<RootConstant>()) {
    return builder.Build<BranchIfRootConstant>({rhs}, root_constant->index());
  }
  if (InlinedAllocation* alloc_lhs = lhs->TryCast<InlinedAllocation>()) {
    if (InlinedAllocation* alloc_rhs = rhs->TryCast<InlinedAllocation>()) {
      return builder.FromBool(alloc_lhs == alloc_rhs);
    }
  }

  return builder.Build<BranchIfReferenceEqual>({lhs, rhs});
}

void MaglevGraphBuilder::MarkBranchDeadAndJumpIfNeeded(bool is_jump_taken) {
  int jump_offset = iterator_.GetJumpTargetOffset();
  if (is_jump_taken) {
    BasicBlock* block = FinishBlock<Jump>({}, &jump_targets_[jump_offset]);
    MergeDeadIntoFrameState(next_offset());
    MergeIntoFrameState(block, jump_offset);
  } else {
    MergeDeadIntoFrameState(jump_offset);
  }
}

#ifdef DEBUG
namespace {
bool IsNumberRootConstant(RootIndex root_index) {
  switch (root_index) {
#define CASE(type, name, label) case RootIndex::k##label:
    SMI_ROOT_LIST(CASE)
    STRONG_READ_ONLY_HEAP_NUMBER_ROOT_LIST(CASE)
    return true;
    default:
      return false;
  }
#undef CASE
}
}  // namespace
#endif

MaglevGraphBuilder::BranchResult MaglevGraphBuilder::BuildBranchIfRootConstant(
    BranchBuilder& builder, ValueNode* node, RootIndex root_index) {
  // We assume that Maglev never emits a comparison to a root number.
  DCHECK(!IsNumberRootConstant(root_index));

  // If the node we're checking is in the accumulator, swap it in the branch
  // with the checked value. Cache whether we want to swap, since after we've
  // swapped the accumulator isn't the original node anymore.
  BranchBuilder::PatchAccumulatorInBranchScope scope(builder, node, root_index);

  if (node->properties().value_representation() ==
      ValueRepresentation::kHoleyFloat64) {
    if (root_index == RootIndex::kUndefinedValue) {
      return builder.Build<BranchIfFloat64IsHole>({node});
    }
    return builder.AlwaysFalse();
  }

  if (CheckType(node, NodeType::kNumber)) {
    return builder.AlwaysFalse();
  }
  CHECK(node->is_tagged());

  if (root_index != RootIndex::kTrueValue &&
      root_index != RootIndex::kFalseValue &&
      CheckType(node, NodeType::kBoolean)) {
    return builder.AlwaysFalse();
  }

  while (LogicalNot* logical_not = node->TryCast<LogicalNot>()) {
    // Bypassing logical not(s) on the input and swapping true/false
    // destinations.
    node = logical_not->value().node();
    builder.SwapTargets();
  }

  if (RootConstant* constant = node->TryCast<RootConstant>()) {
    return builder.FromBool(constant->index() == root_index);
  }

  if (root_index == RootIndex::kUndefinedValue) {
    if (Constant* constant = node->TryCast<Constant>()) {
      return builder.FromBool(constant->object().IsUndefined());
    }
  }

  if (root_index != RootIndex::kTrueValue &&
      root_index != RootIndex::kFalseValue) {
    return builder.Build<BranchIfRootConstant>({node}, root_index);
  }
  if (root_index == RootIndex::kFalseValue) {
    builder.SwapTargets();
  }
  switch (node->opcode()) {
    case Opcode::kTaggedEqual:
      return BuildBranchIfReferenceEqual(
          builder, node->Cast<TaggedEqual>()->lhs().node(),
          node->Cast<TaggedEqual>()->rhs().node());
    case Opcode::kTaggedNotEqual:
      // Swapped true and false targets.
      builder.SwapTargets();
      return BuildBranchIfReferenceEqual(
          builder, node->Cast<TaggedNotEqual>()->lhs().node(),
          node->Cast<TaggedNotEqual>()->rhs().node());
    case Opcode::kInt32Compare:
      return builder.Build<BranchIfInt32Compare>(
          {node->Cast<Int32Compare>()->left_input().node(),
           node->Cast<Int32Compare>()->right_input().node()},
          node->Cast<Int32Compare>()->operation());
    case Opcode::kFloat64Compare:
      return builder.Build<BranchIfFloat64Compare>(
          {node->Cast<Float64Compare>()->left_input().node(),
           node->Cast<Float64Compare>()->right_input().node()},
          node->Cast<Float64Compare>()->operation());
    case Opcode::kInt32ToBoolean:
      if (node->Cast<Int32ToBoolean>()->flip()) {
        builder.SwapTargets();
      }
      return builder.Build<BranchIfInt32ToBooleanTrue>(
          {node->Cast<Int32ToBoolean>()->value().node()});
    case Opcode::kIntPtrToBoolean:
      if (node->Cast<IntPtrToBoolean>()->flip()) {
        builder.SwapTargets();
      }
      return builder.Build<BranchIfIntPtrToBooleanTrue>(
          {node->Cast<IntPtrToBoolean>()->value().node()});
    case Opcode::kFloat64ToBoolean:
      if (node->Cast<Float64ToBoolean>()->flip()) {
        builder.SwapTargets();
      }
      return builder.Build<BranchIfFloat64ToBooleanTrue>(
          {node->Cast<Float64ToBoolean>()->value().node()});
    case Opcode::kTestUndetectable:
      return builder.Build<BranchIfUndetectable>(
          {node->Cast<TestUndetectable>()->value().node()},
          node->Cast<TestUndetectable>()->check_type());
    case Opcode::kHoleyFloat64IsHole:
      return builder.Build<BranchIfFloat64IsHole>(
          {node->Cast<HoleyFloat64IsHole>()->input().node()});
    default:
      return builder.Build<BranchIfRootConstant>({node}, RootIndex::kTrueValue);
  }
}

MaglevGraphBuilder::BranchResult MaglevGraphBuilder::BuildBranchIfTrue(
    BranchBuilder& builder, ValueNode* node) {
  builder.SetBranchSpecializationMode(BranchSpecializationMode::kAlwaysBoolean);
  return BuildBranchIfRootConstant(builder, node, RootIndex::kTrueValue);
}

MaglevGraphBuilder::BranchResult MaglevGraphBuilder::BuildBranchIfNull(
    BranchBuilder& builder, ValueNode* node) {
  return BuildBranchIfRootConstant(builder, node, RootIndex::kNullValue);
}

MaglevGraphBuilder::BranchResult MaglevGraphBuilder::BuildBranchIfUndefined(
    BranchBuilder& builder, ValueNode* node) {
  return BuildBranchIfRootConstant(builder, node, RootIndex::kUndefinedValue);
}

MaglevGraphBuilder::BranchResult
MaglevGraphBuilder::BuildBranchIfUndefinedOrNull(BranchBuilder& builder,
                                                 ValueNode* node) {
  compiler::OptionalHeapObjectRef maybe_constant = TryGetConstant(node);
  if (maybe_constant.has_value()) {
    return builder.FromBool(maybe_constant->IsNullOrUndefined());
  }
  if (!node->is_tagged()) {
    if (node->properties().value_representation() ==
        ValueRepresentation::kHoleyFloat64) {
      return BuildBranchIfFloat64IsHole(builder, node);
    }
    return builder.AlwaysFalse();
  }
  if (HasDisjointType(node, NodeType::kOddball)) {
    return builder.AlwaysFalse();
  }
  return builder.Build<BranchIfUndefinedOrNull>({node});
}

MaglevGraphBuilder::BranchResult MaglevGraphBuilder::BuildBranchIfToBooleanTrue(
    BranchBuilder& builder, ValueNode* node) {
  // If this is a known boolean, use the non-ToBoolean version.
  if (CheckType(node, NodeType::kBoolean)) {
    return BuildBranchIfTrue(builder, node);
  }

  // There shouldn't be any LogicalNots here, for swapping true/false, since
  // these are known to be boolean and should have gone throught the
  // non-ToBoolean path.
  DCHECK(!node->Is<LogicalNot>());

  bool known_to_boolean_value = false;
  bool direction_is_true = true;
  if (IsConstantNode(node->opcode())) {
    known_to_boolean_value = true;
    direction_is_true = FromConstantToBool(local_isolate(), node);
  } else {
    // TODO(victorgomes): Unify this with TestUndetectable?
    // JSReceivers are true iff they are not marked as undetectable. Check if
    // all maps have the same detectability, and if yes, the boolean value is
    // known.
    NodeInfo* node_info = known_node_aspects().TryGetInfoFor(node);
    if (node_info && NodeTypeIs(node_info->type(), NodeType::kJSReceiver) &&
        node_info->possible_maps_are_known()) {
      bool all_detectable = true;
      bool all_undetectable = true;
      for (compiler::MapRef map : node_info->possible_maps()) {
        bool is_undetectable = map.is_undetectable();
        all_detectable &= !is_undetectable;
        all_undetectable &= is_undetectable;
      }
      if (all_detectable || all_undetectable) {
        known_to_boolean_value = true;
        direction_is_true = all_detectable;
      }
    }
  }
  if (known_to_boolean_value) {
    return builder.FromBool(direction_is_true);
  }

  switch (node->value_representation()) {
    // The ToBoolean of both the_hole and NaN is false, so we can use the
    // same operation for HoleyFloat64 and Float64.
    case ValueRepresentation::kFloat64:
    case ValueRepresentation::kHoleyFloat64:
      return BuildBranchIfFloat64ToBooleanTrue(builder, node);

    case ValueRepresentation::kUint32:
      // Uint32 has the same logic as Int32 when converting ToBoolean, namely
      // comparison against zero, so we can cast it and ignore the signedness.
      node = AddNewNode<TruncateUint32ToInt32>({node});
      [[fallthrough]];
    case ValueRepresentation::kInt32:
      return BuildBranchIfInt32ToBooleanTrue(builder, node);

    case ValueRepresentation::kIntPtr:
      return BuildBranchIfIntPtrToBooleanTrue(builder, node);

    case ValueRepresentation::kTagged:
      break;
  }

  NodeInfo* node_info = known_node_aspects().TryGetInfoFor(node);
  if (node_info) {
    if (ValueNode* as_int32 = node_info->alternative().int32()) {
      return BuildBranchIfInt32ToBooleanTrue(builder, as_int32);
    }
    if (ValueNode* as_float64 = node_info->alternative().float64()) {
      return BuildBranchIfFloat64ToBooleanTrue(builder, as_float64);
    }
  }

  NodeType old_type;
  if (CheckType(node, NodeType::kBoolean, &old_type)) {
    return builder.Build<BranchIfRootConstant>({node}, RootIndex::kTrueValue);
  }
  if (CheckType(node, NodeType::kSmi)) {
    builder.SwapTargets();
    return builder.Build<BranchIfReferenceEqual>({node, GetSmiConstant(0)});
  }
  if (CheckType(node, NodeType::kString)) {
    builder.SwapTargets();
    return builder.Build<BranchIfRootConstant>({node},
                                               RootIndex::kempty_string);
  }
  // TODO(verwaest): Number or oddball.
  return builder.Build<BranchIfToBooleanTrue>({node}, GetCheckType(old_type));
}

MaglevGraphBuilder::BranchResult
MaglevGraphBuilder::BuildBranchIfInt32ToBooleanTrue(BranchBuilder& builder,
                                                    ValueNode* node) {
  // TODO(victorgomes): Optimize.
  return builder.Build<BranchIfInt32ToBooleanTrue>({node});
}

MaglevGraphBuilder::BranchResult
MaglevGraphBuilder::BuildBranchIfIntPtrToBooleanTrue(BranchBuilder& builder,
                                                     ValueNode* node) {
  // TODO(victorgomes): Optimize.
  return builder.Build<BranchIfIntPtrToBooleanTrue>({node});
}

MaglevGraphBuilder::BranchResult
MaglevGraphBuilder::BuildBranchIfFloat64ToBooleanTrue(BranchBuilder& builder,
                                                      ValueNode* node) {
  // TODO(victorgomes): Optimize.
  return builder.Build<BranchIfFloat64ToBooleanTrue>({node});
}

MaglevGraphBuilder::BranchResult MaglevGraphBuilder::BuildBranchIfFloat64IsHole(
    BranchBuilder& builder, ValueNode* node) {
  // TODO(victorgomes): Optimize.
  return builder.Build<BranchIfFloat64IsHole>({node});
}

ReduceResult MaglevGraphBuilder::VisitJumpIfToBooleanTrue() {
  auto branch_builder = CreateBranchBuilder(BranchType::kBranchIfTrue);
  BuildBranchIfToBooleanTrue(branch_builder, GetAccumulator());
  return ReduceResult::Done();
}
ReduceResult MaglevGraphBuilder::VisitJumpIfToBooleanFalse() {
  auto branch_builder = CreateBranchBuilder(BranchType::kBranchIfFalse);
  BuildBranchIfToBooleanTrue(branch_builder, GetAccumulator());
  return ReduceResult::Done();
}
ReduceResult MaglevGraphBuilder::VisitJumpIfTrue() {
  auto branch_builder = CreateBranchBuilder(BranchType::kBranchIfTrue);
  BuildBranchIfTrue(branch_builder, GetAccumulator());
  return ReduceResult::Done();
}
ReduceResult MaglevGraphBuilder::VisitJumpIfFalse() {
  auto branch_builder = CreateBranchBuilder(BranchType::kBranchIfFalse);
  BuildBranchIfTrue(branch_builder, GetAccumulator());
  return ReduceResult::Done();
}
ReduceResult MaglevGraphBuilder::VisitJumpIfNull() {
  auto branch_builder = CreateBranchBuilder(BranchType::kBranchIfTrue);
  BuildBranchIfNull(branch_builder, GetAccumulator());
  return ReduceResult::Done();
}
ReduceResult MaglevGraphBuilder::VisitJumpIfNotNull() {
  auto branch_builder = CreateBranchBuilder(BranchType::kBranchIfFalse);
  BuildBranchIfNull(branch_builder, GetAccumulator());
  return ReduceResult::Done();
}
ReduceResult MaglevGraphBuilder::VisitJumpIfUndefined() {
  auto branch_builder = CreateBranchBuilder(BranchType::kBranchIfTrue);
  BuildBranchIfUndefined(branch_builder, GetAccumulator());
  return ReduceResult::Done();
}
ReduceResult MaglevGraphBuilder::VisitJumpIfNotUndefined() {
  auto branch_builder = CreateBranchBuilder(BranchType::kBranchIfFalse);
  BuildBranchIfUndefined(branch_builder, GetAccumulator());
  return ReduceResult::Done();
}
ReduceResult MaglevGraphBuilder::VisitJumpIfUndefinedOrNull() {
  auto branch_builder = CreateBranchBuilder();
  BuildBranchIfUndefinedOrNull(branch_builder, GetAccumulator());
  return ReduceResult::Done();
}

MaglevGraphBuilder::BranchResult MaglevGraphBuilder::BuildBranchIfJSReceiver(
    BranchBuilder& builder, ValueNode* value) {
  if (!value->is_tagged() && value->properties().value_representation() !=
                                 ValueRepresentation::kHoleyFloat64) {
    return builder.AlwaysFalse();
  }
  if (CheckType(value, NodeType::kJSReceiver)) {
    return builder.AlwaysTrue();
  } else if (HasDisjointType(value, NodeType::kJSReceiver)) {
    return builder.AlwaysFalse();
  }
  return builder.Build<BranchIfJSReceiver>({value});
}

MaglevGraphBuilder::BranchResult MaglevGraphBuilder::BuildBranchIfInt32Compare(
    BranchBuilder& builder, Operation op, ValueNode* lhs, ValueNode* rhs) {
  auto lhs_const = TryGetInt32Constant(lhs);
  if (lhs_const) {
    auto rhs_const = TryGetInt32Constant(rhs);
    if (rhs_const) {
      return builder.FromBool(
          CompareInt32(lhs_const.value(), rhs_const.value(), op));
    }
  }
  return builder.Build<BranchIfInt32Compare>({lhs, rhs}, op);
}

MaglevGraphBuilder::BranchResult MaglevGraphBuilder::BuildBranchIfUint32Compare(
    BranchBuilder& builder, Operation op, ValueNode* lhs, ValueNode* rhs) {
  auto lhs_const = TryGetUint32Constant(lhs);
  if (lhs_const) {
    auto rhs_const = TryGetUint32Constant(rhs);
    if (rhs_const) {
      return builder.FromBool(
          CompareUint32(lhs_const.value(), rhs_const.value(), op));
    }
  }
  return builder.Build<BranchIfUint32Compare>({lhs, rhs}, op);
}

ReduceResult MaglevGraphBuilder::VisitJumpIfJSReceiver() {
  auto branch_builder = CreateBranchBuilder();
  BuildBranchIfJSReceiver(branch_builder, GetAccumulator());
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitJumpIfForInDone() {
  // JumpIfForInDone <target> <index> <cache_length>
  ValueNode* index = LoadRegister(1);
  ValueNode* cache_length = LoadRegister(2);
  auto branch_builder = CreateBranchBuilder();
  BuildBranchIfInt32Compare(branch_builder, Operation::kEqual, index,
                            cache_length);
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitSwitchOnSmiNoFeedback() {
  // SwitchOnSmiNoFeedback <table_start> <table_length> <case_value_base>
  interpreter::JumpTableTargetOffsets offsets =
      iterator_.GetJumpTableTargetOffsets();

  if (offsets.size() == 0) return ReduceResult::Done();

  int case_value_base = (*offsets.begin()).case_value;
  BasicBlockRef* targets = zone()->AllocateArray<BasicBlockRef>(offsets.size());
  for (interpreter::JumpTableTargetOffset offset : offsets) {
    BasicBlockRef* ref = &targets[offset.case_value - case_value_base];
    new (ref) BasicBlockRef(&jump_targets_[offset.target_offset]);
  }

  ValueNode* case_value = GetAccumulator();
  BasicBlock* block =
      FinishBlock<Switch>({case_value}, case_value_base, targets,
                          offsets.size(), &jump_targets_[next_offset()]);
  for (interpreter::JumpTableTargetOffset offset : offsets) {
    MergeIntoFrameState(block, offset.target_offset);
  }
  StartFallthroughBlock(next_offset(), block);
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitForInEnumerate() {
  // ForInEnumerate <receiver>
  ValueNode* receiver = LoadRegister(0);
  // Pass receiver to ForInPrepare.
  current_for_in_state.receiver = receiver;
  SetAccumulator(
      BuildCallBuiltin<Builtin::kForInEnumerate>({GetTaggedValue(receiver)}));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitForInPrepare() {
  // ForInPrepare <cache_info_triple>
  ValueNode* enumerator = GetAccumulator();
  // Catch the receiver value passed from ForInEnumerate.
  ValueNode* receiver = current_for_in_state.receiver;
  FeedbackSlot slot = GetSlotOperand(1);
  compiler::FeedbackSource feedback_source{feedback(), slot};
  // TODO(v8:7700): Use feedback and create fast path.
  ValueNode* context = GetContext();
  interpreter::Register cache_type_reg = iterator_.GetRegisterOperand(0);
  interpreter::Register cache_array_reg{cache_type_reg.index() + 1};
  interpreter::Register cache_length_reg{cache_type_reg.index() + 2};

  ForInHint hint = broker()->GetFeedbackForForIn(feedback_source);

  current_for_in_state = ForInState();
  switch (hint) {
    case ForInHint::kNone:
    case ForInHint::kEnumCacheKeysAndIndices:
    case ForInHint::kEnumCacheKeys: {
      // Check that the {enumerator} is a Map.
      // The direct IsMap check requires reading of an instance type, so in
      // order to avoid additional load we compare the {enumerator} against
      // receiver's Map instead (by definition, the {enumerator} is either
      // the receiver's Map or a FixedArray).
      auto* receiver_map =
          BuildLoadTaggedField(receiver, HeapObject::kMapOffset);
      AddNewNode<CheckDynamicValue>({receiver_map, enumerator});

      auto* descriptor_array =
          BuildLoadTaggedField(enumerator, Map::kInstanceDescriptorsOffset);
      auto* enum_cache = BuildLoadTaggedField(
          descriptor_array, DescriptorArray::kEnumCacheOffset);
      auto* cache_array =
          BuildLoadTaggedField(enum_cache, EnumCache::kKeysOffset);

      auto* cache_length = AddNewNode<LoadEnumCacheLength>({enumerator});

      if (hint == ForInHint::kEnumCacheKeysAndIndices) {
        auto* cache_indices =
            BuildLoadTaggedField(enum_cache, EnumCache::kIndicesOffset);
        current_for_in_state.enum_cache_indices = cache_indices;
        AddNewNode<CheckCacheIndicesNotCleared>({cache_indices, cache_length});
      } else {
        current_for_in_state.enum_cache_indices = nullptr;
      }

      MoveNodeBetweenRegisters(interpreter::Register::virtual_accumulator(),
                               cache_type_reg);
      StoreRegister(cache_array_reg, cache_array);
      StoreRegister(cache_length_reg, cache_length);
      break;
    }
    case ForInHint::kAny: {
      // The result of the bytecode is output in registers |cache_info_triple|
      // to |cache_info_triple + 2|, with the registers holding cache_type,
      // cache_array, and cache_length respectively.
      //
      // We set the cache type first (to the accumulator value), and write
      // the other two with a ForInPrepare builtin call. This can lazy deopt,
      // which will write to cache_array and cache_length, with cache_type
      // already set on the translation frame.

      // This move needs to happen before ForInPrepare to avoid lazy deopt
      // extending the lifetime of the {cache_type} register.
      MoveNodeBetweenRegisters(interpreter::Register::virtual_accumulator(),
                               cache_type_reg);
      ForInPrepare* result =
          AddNewNode<ForInPrepare>({context, enumerator}, feedback_source);
      StoreRegisterPair({cache_array_reg, cache_length_reg}, result);
      // Force a conversion to Int32 for the cache length value.
      EnsureInt32(cache_length_reg);
      break;
    }
  }
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitForInNext() {
  // ForInNext <receiver> <index> <cache_info_pair>
  ValueNode* receiver = LoadRegister(0);
  interpreter::Register cache_type_reg, cache_array_reg;
  std::tie(cache_type_reg, cache_array_reg) =
      iterator_.GetRegisterPairOperand(2);
  ValueNode* cache_type = current_interpreter_frame_.get(cache_type_reg);
  ValueNode* cache_array = current_interpreter_frame_.get(cache_array_reg);
  FeedbackSlot slot = GetSlotOperand(3);
  compiler::FeedbackSource feedback_source{feedback(), slot};

  ForInHint hint = broker()->GetFeedbackForForIn(feedback_source);

  switch (hint) {
    case ForInHint::kNone:
    case ForInHint::kEnumCacheKeysAndIndices:
    case ForInHint::kEnumCacheKeys: {
      ValueNode* index = LoadRegister(1);
      // Ensure that the expected map still matches that of the {receiver}.
      auto* receiver_map =
          BuildLoadTaggedField(receiver, HeapObject::kMapOffset);
      AddNewNode<CheckDynamicValue>({receiver_map, cache_type});
      auto* key = BuildLoadFixedArrayElement(cache_array, index);
      EnsureType(key, NodeType::kInternalizedString);
      SetAccumulator(key);

      current_for_in_state.receiver = receiver;
      if (ToObject* to_object =
              current_for_in_state.receiver->TryCast<ToObject>()) {
        current_for_in_state.receiver = to_object->value_input().node();
      }
      current_for_in_state.receiver_needs_map_check = false;
      current_for_in_state.cache_type = cache_type;
      current_for_in_state.key = key;
      if (hint == ForInHint::kEnumCacheKeysAndIndices) {
        current_for_in_state.index = index;
      }
      // We know that the enum cache entry is not undefined, so skip over the
      // next JumpIfUndefined.
      DCHECK(iterator_.next_bytecode() ==
                 interpreter::Bytecode::kJumpIfUndefined ||
             iterator_.next_bytecode() ==
                 interpreter::Bytecode::kJumpIfUndefinedConstant);
      iterator_.Advance();
      MergeDeadIntoFrameState(iterator_.GetJumpTargetOffset());
      break;
    }
    case ForInHint::kAny: {
      ValueNode* index = LoadRegister(1);
      ValueNode* context = GetContext();
      SetAccumulator(AddNewNode<ForInNext>(
          {context, receiver, cache_array, cache_type, index},
          feedback_source));
      break;
    };
  }
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitForInStep() {
  interpreter::Register index_reg = iterator_.GetRegisterOperand(0);
  ValueNode* index = current_interpreter_frame_.get(index_reg);
  StoreRegister(index_reg,
                AddNewNode<Int32NodeFor<Operation::kIncrement>>({index}));
  if (!in_peeled_iteration()) {
    // With loop peeling, only the `ForInStep` in the non-peeled loop body marks
    // the end of for-in.
    current_for_in_state = ForInState();
  }
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitSetPendingMessage() {
  ValueNode* message = GetAccumulator();
  SetAccumulator(AddNewNode<SetPendingMessage>({message}));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitThrow() {
  ValueNode* exception = GetAccumulator();
  return BuildCallRuntime(Runtime::kThrow, {exception});
}
ReduceResult MaglevGraphBuilder::VisitReThrow() {
  ValueNode* exception = GetAccumulator();
  return BuildCallRuntime(Runtime::kReThrow, {exception});
}

ReduceResult MaglevGraphBuilder::VisitReturn() {
  // See also: InterpreterAssembler::UpdateInterruptBudgetOnReturn.
  const uint32_t relative_jump_bytecode_offset = iterator_.current_offset();
  if (ShouldEmitInterruptBudgetChecks() && relative_jump_bytecode_offset > 0) {
    AddNewNode<ReduceInterruptBudgetForReturn>({},
                                               relative_jump_bytecode_offset);
  }

  if (!is_inline()) {
    FinishBlock<Return>({GetAccumulator()});
    return ReduceResult::Done();
  }

  // All inlined function returns instead jump to one past the end of the
  // bytecode, where we'll later create a final basic block which resumes
  // execution of the caller. If there is only one return, at the end of the
  // function, we can elide this jump and just continue in the same basic block.
  if (iterator_.next_offset() != inline_exit_offset() ||
      predecessor_count(inline_exit_offset()) > 1) {
    BasicBlock* block =
        FinishBlock<Jump>({}, &jump_targets_[inline_exit_offset()]);
    // The context is dead by now, set it to optimized out to avoid creating
    // unnecessary phis.
    SetContext(GetRootConstant(RootIndex::kOptimizedOut));
    MergeIntoInlinedReturnFrameState(block);
  }
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitThrowReferenceErrorIfHole() {
  // ThrowReferenceErrorIfHole <variable_name>
  compiler::NameRef name = GetRefOperand<Name>(0);
  ValueNode* value = GetAccumulator();

  // Avoid the check if we know it is not the hole.
  if (IsConstantNode(value->opcode())) {
    if (IsTheHoleValue(value)) {
      ValueNode* constant = GetConstant(name);
      return BuildCallRuntime(Runtime::kThrowAccessedUninitializedVariable,
                              {constant});
    }
    return ReduceResult::Done();
  }

  // Avoid the check if {value}'s representation doesn't allow the hole.
  switch (value->value_representation()) {
    case ValueRepresentation::kInt32:
    case ValueRepresentation::kUint32:
    case ValueRepresentation::kFloat64:
    case ValueRepresentation::kHoleyFloat64:
    case ValueRepresentation::kIntPtr:
      // Can't be the hole.
      // Note that HoleyFloat64 when converted to Tagged becomes Undefined
      // rather than the_hole, hence the early return for HoleyFloat64.
      return ReduceResult::Done();

    case ValueRepresentation::kTagged:
      // Could be the hole.
      break;
  }

  // Avoid the check if {value} has an alternative whose representation doesn't
  // allow the hole.
  if (const NodeInfo* info = known_node_aspects().TryGetInfoFor(value)) {
    auto& alt = info->alternative();
    if (alt.int32() || alt.truncated_int32_to_number() || alt.float64()) {
      return ReduceResult::Done();
    }
  }

  DCHECK(value->value_representation() == ValueRepresentation::kTagged);
  AddNewNode<ThrowReferenceErrorIfHole>({value}, name);
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitThrowSuperNotCalledIfHole() {
  // ThrowSuperNotCalledIfHole
  ValueNode* value = GetAccumulator();
  if (CheckType(value, NodeType::kJSReceiver)) return ReduceResult::Done();
  // Avoid the check if we know it is not the hole.
  if (IsConstantNode(value->opcode())) {
    if (IsTheHoleValue(value)) {
      return BuildCallRuntime(Runtime::kThrowSuperNotCalled, {});
    }
    return ReduceResult::Done();
  }
  AddNewNode<ThrowSuperNotCalledIfHole>({value});
  return ReduceResult::Done();
}
ReduceResult MaglevGraphBuilder::VisitThrowSuperAlreadyCalledIfNotHole() {
  // ThrowSuperAlreadyCalledIfNotHole
  ValueNode* value = GetAccumulator();
  // Avoid the check if we know it is the hole.
  if (IsConstantNode(value->opcode())) {
    if (!IsTheHoleValue(value)) {
      return BuildCallRuntime(Runtime::kThrowSuperAlreadyCalledError, {});
    }
    return ReduceResult::Done();
  }
  AddNewNode<ThrowSuperAlreadyCalledIfNotHole>({value});
  return ReduceResult::Done();
}
ReduceResult MaglevGraphBuilder::VisitThrowIfNotSuperConstructor() {
  // ThrowIfNotSuperConstructor <constructor>
  ValueNode* constructor = LoadRegister(0);
  ValueNode* function = GetClosure();
  AddNewNode<ThrowIfNotSuperConstructor>({constructor, function});
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitSwitchOnGeneratorState() {
  // SwitchOnGeneratorState <generator> <table_start> <table_length>
  // It should be the first bytecode in the bytecode array.
  DCHECK_EQ(iterator_.current_offset(), 0);
  int generator_prologue_block_offset = 1;
  DCHECK_LT(generator_prologue_block_offset, next_offset());

  interpreter::JumpTableTargetOffsets offsets =
      iterator_.GetJumpTableTargetOffsets();
  // If there are no jump offsets, then this generator is not resumable, which
  // means we can skip checking for it and switching on its state.
  if (offsets.size() == 0) return ReduceResult::Done();

  graph()->set_has_resumable_generator();

  // We create an initial block that checks if the generator is undefined.
  ValueNode* maybe_generator = LoadRegister(0);
  // Neither the true nor the false path jump over any bytecode
  BasicBlock* block_is_generator_undefined = FinishBlock<BranchIfRootConstant>(
      {maybe_generator}, RootIndex::kUndefinedValue,
      &jump_targets_[next_offset()],
      &jump_targets_[generator_prologue_block_offset]);
  MergeIntoFrameState(block_is_generator_undefined, next_offset());

  // We create the generator prologue block.
  StartNewBlock(generator_prologue_block_offset, block_is_generator_undefined);

  // Generator prologue.
  ValueNode* generator = maybe_generator;
  ValueNode* state =
      BuildLoadTaggedField(generator, JSGeneratorObject::kContinuationOffset);
  ValueNode* new_state = GetSmiConstant(JSGeneratorObject::kGeneratorExecuting);
  BuildStoreTaggedFieldNoWriteBarrier(generator, new_state,
                                      JSGeneratorObject::kContinuationOffset,
                                      StoreTaggedMode::kDefault);
  ValueNode* context =
      BuildLoadTaggedField(generator, JSGeneratorObject::kContextOffset);
  graph()->record_scope_info(context, {});
  SetContext(context);

  // Guarantee that we have something in the accumulator.
  MoveNodeBetweenRegisters(iterator_.GetRegisterOperand(0),
                           interpreter::Register::virtual_accumulator());

  // Switch on generator state.
  int case_value_base = (*offsets.begin()).case_value;
  BasicBlockRef* targets = zone()->AllocateArray<BasicBlockRef>(offsets.size());
  for (interpreter::JumpTableTargetOffset offset : offsets) {
    BasicBlockRef* ref = &targets[offset.case_value - case_value_base];
    new (ref) BasicBlockRef(&jump_targets_[offset.target_offset]);
  }
  ValueNode* case_value =
      state->is_tagged() ? AddNewNode<UnsafeSmiUntag>({state}) : state;
  BasicBlock* generator_prologue_block = FinishBlock<Switch>(
      {case_value}, case_value_base, targets, offsets.size());
  for (interpreter::JumpTableTargetOffset offset : offsets) {
    MergeIntoFrameState(generator_prologue_block, offset.target_offset);
  }
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitSuspendGenerator() {
  // SuspendGenerator <generator> <first input register> <register count>
  // <suspend_id>
  ValueNode* generator = LoadRegister(0);
  ValueNode* context = GetContext();
  interpreter::RegisterList args = iterator_.GetRegisterListOperand(1);
  uint32_t suspend_id = iterator_.GetUnsignedImmediateOperand(3);

  int input_count = parameter_count_without_receiver() + args.register_count() +
                    GeneratorStore::kFixedInputCount;
  int debug_pos_offset = iterator_.current_offset() +
                         (BytecodeArray::kHeaderSize - kHeapObjectTag);
  AddNewNode<GeneratorStore>(
      input_count,
      [&](GeneratorStore* node) {
        int arg_index = 0;
        for (int i = 1 /* skip receiver */; i < parameter_count(); ++i) {
          node->set_parameters_and_registers(arg_index++,
                                             GetTaggedValue(GetArgument(i)));
        }
        const compiler::BytecodeLivenessState* liveness = GetOutLiveness();
        for (int i = 0; i < args.register_count(); ++i) {
          ValueNode* value = liveness->RegisterIsLive(args[i].index())
                                 ? GetTaggedValue(args[i])
                                 : GetRootConstant(RootIndex::kOptimizedOut);
          node->set_parameters_and_registers(arg_index++, value);
        }
      },

      context, generator, suspend_id, debug_pos_offset);

  FinishBlock<Return>({GetAccumulator()});
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitResumeGenerator() {
  // ResumeGenerator <generator> <first output register> <register count>
  ValueNode* generator = LoadRegister(0);
  ValueNode* array = BuildLoadTaggedField(
      generator, JSGeneratorObject::kParametersAndRegistersOffset);
  interpreter::RegisterList registers = iterator_.GetRegisterListOperand(1);

  if (v8_flags.maglev_assert) {
    // Check if register count is invalid, that is, larger than the
    // register file length.
    ValueNode* array_length = BuildLoadFixedArrayLength(array);
    ValueNode* register_size = GetInt32Constant(
        parameter_count_without_receiver() + registers.register_count());
    AddNewNode<AssertInt32>(
        {register_size, array_length}, AssertCondition::kLessThanEqual,
        AbortReason::kInvalidParametersAndRegistersInGenerator);
  }

  const compiler::BytecodeLivenessState* liveness =
      GetOutLivenessFor(next_offset());
  RootConstant* stale = GetRootConstant(RootIndex::kStaleRegister);
  for (int i = 0; i < registers.register_count(); ++i) {
    if (liveness->RegisterIsLive(registers[i].index())) {
      int array_index = parameter_count_without_receiver() + i;
      StoreRegister(registers[i], AddNewNode<GeneratorRestoreRegister>(
                                      {array, stale}, array_index));
    }
  }
  SetAccumulator(BuildLoadTaggedField(
      generator, JSGeneratorObject::kInputOrDebugPosOffset));
  return ReduceResult::Done();
}

MaybeReduceResult MaglevGraphBuilder::TryReduceGetIterator(
    ValueNode* receiver, int load_slot_index, int call_slot_index) {
  // Load iterator method property.
  FeedbackSlot load_slot = FeedbackVector::ToSlot(load_slot_index);
  compiler::FeedbackSource load_feedback{feedback(), load_slot};
  compiler::NameRef iterator_symbol = broker()->iterator_symbol();
  ValueNode* iterator_method;
  {
    DeoptFrameScope deopt_continuation(
        this, Builtin::kGetIteratorWithFeedbackLazyDeoptContinuation, {},
        base::VectorOf<ValueNode*>({receiver, GetSmiConstant(call_slot_index),
                                    GetConstant(feedback())}));
    MaybeReduceResult result_load =
        TryBuildLoadNamedProperty(receiver, iterator_symbol, load_feedback);
    if (result_load.IsDoneWithAbort() || result_load.IsFail()) {
      return result_load;
    }
    DCHECK(result_load.IsDoneWithValue());
    iterator_method = result_load.value();
  }
  auto throw_iterator_error = [&] {
    return BuildCallRuntime(Runtime::kThrowIteratorError, {receiver});
  };
  if (!iterator_method->is_tagged()) {
    return throw_iterator_error();
  }
  auto throw_symbol_iterator_invalid = [&] {
    return BuildCallRuntime(Runtime::kThrowSymbolIteratorInvalid, {});
  };
  auto call_iterator_method = [&] {
    DeoptFrameScope deopt_continuation(
        this, Builtin::kCallIteratorWithFeedbackLazyDeoptContinuation);

    FeedbackSlot call_slot = FeedbackVector::ToSlot(call_slot_index);
    compiler::FeedbackSource call_feedback{feedback(), call_slot};
    CallArguments args(ConvertReceiverMode::kAny, {receiver});
    MaybeReduceResult result_call =
        ReduceCall(iterator_method, args, call_feedback);

    if (result_call.IsDoneWithAbort()) return result_call;
    DCHECK(result_call.IsDoneWithValue());
    return SelectReduction(
        [&](auto& builder) {
          return BuildBranchIfJSReceiver(builder, result_call.value());
        },
        [&] { return result_call; }, throw_symbol_iterator_invalid);
  };
  // Check if the iterator_method is undefined and call the method otherwise.
  return SelectReduction(
      [&](auto& builder) {
        return BuildBranchIfUndefined(builder, iterator_method);
      },
      throw_iterator_error, call_iterator_method);
}

ReduceResult MaglevGraphBuilder::VisitGetIterator() {
  // GetIterator <object>
  ValueNode* receiver = LoadRegister(0);
  int load_slot = iterator_.GetIndexOperand(1);
  int call_slot = iterator_.GetIndexOperand(2);
  PROCESS_AND_RETURN_IF_DONE(
      TryReduceGetIterator(receiver, load_slot, call_slot), SetAccumulator);
  // Fallback to the builtin.
  ValueNode* context = GetContext();
  SetAccumulator(AddNewNode<GetIterator>({context, receiver}, load_slot,
                                         call_slot, feedback()));
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitDebugger() {
  return BuildCallRuntime(Runtime::kHandleDebuggerStatement, {});
}

ReduceResult MaglevGraphBuilder::VisitIncBlockCounter() {
  ValueNode* closure = GetClosure();
  ValueNode* coverage_array_slot = GetSmiConstant(iterator_.GetIndexOperand(0));
  BuildCallBuiltin<Builtin::kIncBlockCounter>(
      {GetTaggedValue(closure), coverage_array_slot});
  return ReduceResult::Done();
}

ReduceResult MaglevGraphBuilder::VisitAbort() {
  AbortReason reason = static_cast<AbortReason>(GetFlag8Operand(0));
  return BuildAbort(reason);
}

ReduceResult MaglevGraphBuilder::VisitWide() { UNREACHABLE(); }
ReduceResult MaglevGraphBuilder::VisitExtraWide() { UNREACHABLE(); }
#define DEBUG_BREAK(Name, ...) \
  ReduceResult MaglevGraphBuilder::Visit##Name() { UNREACHABLE(); }
DEBUG_BREAK_BYTECODE_LIST(DEBUG_BREAK)
#undef DEBUG_BREAK
ReduceResult MaglevGraphBuilder::VisitIllegal() { UNREACHABLE(); }

}  // namespace v8::internal::maglev
