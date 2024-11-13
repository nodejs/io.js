// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/bytecode-graph-builder.h"

#include <optional>

#include "src/ast/ast.h"
#include "src/codegen/source-position-table.h"
#include "src/codegen/tick-counter.h"
#include "src/common/assert-scope.h"
#include "src/compiler/bytecode-analysis.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/js-type-hint-lowering.h"
#include "src/compiler/linkage.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-observer.h"
#include "src/compiler/node-origin-table.h"
#include "src/compiler/operator-properties.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/state-values-utils.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/interpreter/bytecode-flags-and-tokens.h"
#include "src/interpreter/bytecode-register.h"
#include "src/interpreter/bytecodes.h"
#include "src/objects/elements-kind.h"
#include "src/objects/js-generator.h"
#include "src/objects/literal-objects-inl.h"
#include "src/objects/scope-info.h"
#include "src/objects/template-objects-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

class BytecodeGraphBuilder {
 public:
  BytecodeGraphBuilder(
      JSHeapBroker* broker, Zone* local_zone, NativeContextRef native_context,
      SharedFunctionInfoRef shared_info, FeedbackCellRef feedback_cell,
      BytecodeOffset osr_offset, JSGraph* jsgraph,
      CallFrequency const& invocation_frequency,
      SourcePositionTable* source_positions, NodeOriginTable* node_origins,
      int inlining_id, CodeKind code_kind, BytecodeGraphBuilderFlags flags,
      TickCounter* tick_counter, ObserveNodeInfo const& observe_node_info);

  BytecodeGraphBuilder(const BytecodeGraphBuilder&) = delete;
  BytecodeGraphBuilder& operator=(const BytecodeGraphBuilder&) = delete;

  // Creates a graph by visiting bytecodes.
  void CreateGraph();

 private:
  class Environment;
  class OsrIteratorState;
  class BytecodePositionDecorator;
  struct SubEnvironment;

  void RemoveMergeEnvironmentsBeforeOffset(int limit_offset);
  void AdvanceToOsrEntryAndPeelLoops();

  // Advance {bytecode_iterator} to the given offset. If possible, also advance
  // {source_position_iterator} while updating the source position table.
  void AdvanceIteratorsTo(int bytecode_offset);

  void VisitSingleBytecode();
  void VisitBytecodes();

  void AddBytecodePositionDecorator();
  void RemoveBytecodePositionDecorator();

  // Get or create the node that represents the outer function closure.
  Node* GetFunctionClosure();

  // Get or create the node for this parameter index. If such a node is
  // already cached, it is returned directly and the {debug_name_hint} is
  // ignored.
  Node* GetParameter(int index, const char* debug_name_hint = nullptr);

  CodeKind code_kind() const { return code_kind_; }

  // The node representing the current feedback vector is generated once prior
  // to visiting bytecodes, and is later passed as input to other nodes that
  // may need it.
  // TODO(jgruber): Remove feedback_vector() and rename feedback_vector_node()
  // to feedback_vector() once all uses of the direct heap object reference
  // have been replaced with a Node* reference.
  void CreateFeedbackVectorNode();
  Node* feedback_vector_node() const {
    DCHECK_NOT_NULL(feedback_vector_node_);
    return feedback_vector_node_;
  }

  // Same as above for the feedback vector node.
  void CreateNativeContextNode();
  Node* native_context_node() const {
    DCHECK_NOT_NULL(native_context_node_);
    return native_context_node_;
  }

  Node* BuildLoadFeedbackCell(int index);

  // Builder for loading a native context field.
  Node* BuildLoadNativeContextField(int index);

  // Helper function for creating a feedback source containing type feedback
  // vector and a feedback slot.
  FeedbackSource CreateFeedbackSource(int slot_id);
  FeedbackSource CreateFeedbackSource(FeedbackSlot slot);

  void set_environment(Environment* env) { environment_ = env; }
  const Environment* environment() const { return environment_; }
  Environment* environment() { return environment_; }

  // Node creation helpers
  Node* NewNode(const Operator* op, bool incomplete = false) {
    return MakeNode(op, 0, static_cast<Node**>(nullptr), incomplete);
  }

  template <class... Args>
  Node* NewNode(const Operator* op, Node* n0, Args... nodes) {
    Node* buffer[] = {n0, nodes...};
    return MakeNode(op, arraysize(buffer), buffer);
  }

  // Helpers to create new control nodes.
  Node* NewIfTrue() { return NewNode(common()->IfTrue()); }
  Node* NewIfFalse() { return NewNode(common()->IfFalse()); }
  Node* NewIfValue(int32_t value) { return NewNode(common()->IfValue(value)); }
  Node* NewIfDefault() { return NewNode(common()->IfDefault()); }
  Node* NewMerge() { return NewNode(common()->Merge(1), true); }
  Node* NewLoop() { return NewNode(common()->Loop(1), true); }
  Node* NewBranch(Node* condition, BranchHint hint = BranchHint::kNone) {
    return NewNode(common()->Branch(hint), condition);
  }
  Node* NewSwitch(Node* condition, int control_output_count) {
    return NewNode(common()->Switch(control_output_count), condition);
  }

  // Creates a new Phi node having {count} input values.
  Node* NewPhi(int count, Node* input, Node* control);
  Node* NewEffectPhi(int count, Node* input, Node* control);

  // Helpers for merging control, effect or value dependencies.
  Node* MergeControl(Node* control, Node* other);
  Node* MergeEffect(Node* effect, Node* other_effect, Node* control);
  Node* MergeValue(Node* value, Node* other_value, Node* control);

  // The main node creation chokepoint. Adds context, frame state, effect,
  // and control dependencies depending on the operator.
  Node* MakeNode(const Operator* op, int value_input_count,
                 Node* const* value_inputs, bool incomplete = false);

  Node** EnsureInputBufferSize(int size);

  Node* const* GetCallArgumentsFromRegisters(Node* callee, Node* receiver,
                                              interpreter::Register first_arg,
                                              int arg_count);
  Node* const* ProcessCallVarArgs(ConvertReceiverMode receiver_mode,
                                  Node* callee, interpreter::Register first_reg,
                                  int arg_count);
  Node* const* GetConstructArgumentsFromRegister(
      Node* target, Node* new_target, interpreter::Register first_arg,
      int arg_count);
  Node* ProcessCallRuntimeArguments(const Operator* call_runtime_op,
                                    interpreter::Register receiver,
                                    size_t reg_count);

  // Prepare information for eager deoptimization. This information is carried
  // by dedicated {Checkpoint} nodes that are wired into the effect chain.
  // Conceptually this frame state is "before" a given operation.
  void PrepareEagerCheckpoint();

  // Prepare information for lazy deoptimization. This information is attached
  // to the given node and the output value produced by the node is combined.
  //
  // The low-level chokepoint - use the variants below instead.
  void PrepareFrameState(Node* node, OutputFrameStateCombine combine,
                         BytecodeOffset bailout_id,
                         const BytecodeLivenessState* liveness);

  // In the common case, frame states are conceptually "after" a given
  // operation and at the current bytecode offset.
  void PrepareFrameState(Node* node, OutputFrameStateCombine combine) {
    if (!OperatorProperties::HasFrameStateInput(node->op())) return;
    const int offset = bytecode_iterator().current_offset();
    return PrepareFrameState(node, combine, BytecodeOffset(offset),
                             bytecode_analysis().GetOutLivenessFor(offset));
  }

  // For function-entry stack checks, they're conceptually "before" the first
  // bytecode and at a special marker bytecode offset.
  // In the case of FE stack checks, the current bytecode is also the first
  // bytecode, so we use a special marker bytecode offset to signify a virtual
  // bytecode before the first physical bytecode.
  void PrepareFrameStateForFunctionEntryStackCheck(Node* node) {
    DCHECK_EQ(bytecode_iterator().current_offset(), 0);
    DCHECK(OperatorProperties::HasFrameStateInput(node->op()));
    DCHECK(node->opcode() == IrOpcode::kJSStackCheck);
    return PrepareFrameState(node, OutputFrameStateCombine::Ignore(),
                             BytecodeOffset(kFunctionEntryBytecodeOffset),
                             bytecode_analysis().GetInLivenessFor(0));
  }

  // For OSR-entry stack checks, they're conceptually "before" the first
  // bytecode of the current loop. We implement this in a similar manner to
  // function-entry (FE) stack checks above, i.e. we deopt at the predecessor
  // of the current bytecode.
  // In the case of OSR-entry stack checks, a physical predecessor bytecode
  // exists: the JumpLoop bytecode. We attach to JumpLoop by using
  // `bytecode_analysis().osr_bailout_id()` instead of current_offset (the
  // former points at JumpLoop, the latter at the loop header, i.e. the target
  // of JumpLoop).
  void PrepareFrameStateForOSREntryStackCheck(Node* node) {
    DCHECK(OperatorProperties::HasFrameStateInput(node->op()));
    DCHECK(node->opcode() == IrOpcode::kJSStackCheck);
    const int offset = bytecode_analysis().osr_bailout_id().ToInt();
    return PrepareFrameState(node, OutputFrameStateCombine::Ignore(),
                             BytecodeOffset(offset),
                             bytecode_analysis().GetOutLivenessFor(offset));
  }

  void BuildCreateArguments(CreateArgumentsType type);
  Node* BuildLoadGlobal(NameRef name, uint32_t feedback_slot_index,
                        TypeofMode typeof_mode);

  enum class NamedStoreMode {
    // Check the prototype chain before storing.
    kSet,
    // Define value to the receiver without checking the prototype chain.
    kDefineOwn,
  };
  void BuildNamedStore(NamedStoreMode store_mode);
  void BuildLdaLookupSlot(TypeofMode typeof_mode);
  void BuildLdaLookupContextSlot(TypeofMode typeof_mode);
  void BuildLdaLookupGlobalSlot(TypeofMode typeof_mode);
  void BuildCallVarArgs(ConvertReceiverMode receiver_mode);
  void BuildCall(ConvertReceiverMode receiver_mode, Node* const* args,
                 size_t arg_count, int slot_id);
  void BuildCall(ConvertReceiverMode receiver_mode,
                 std::initializer_list<Node*> args, int slot_id) {
    BuildCall(receiver_mode, args.begin(), args.size(), slot_id);
  }
  void BuildUnaryOp(const Operator* op);
  void BuildBinaryOp(const Operator* op);
  void BuildBinaryOpWithImmediate(const Operator* op);
  void BuildCompareOp(const Operator* op);
  void BuildDelete(LanguageMode language_mode);
  void BuildCastOperator(const Operator* op);
  void BuildHoleCheckAndThrow(Node* condition, Runtime::FunctionId runtime_id,
                              Node* name = nullptr);

  // Optional early lowering to the simplified operator level.  Note that
  // the result has already been wired into the environment just like
  // any other invocation of {NewNode} would do.
  JSTypeHintLowering::LoweringResult TryBuildSimplifiedUnaryOp(
      const Operator* op, Node* operand, FeedbackSlot slot);
  JSTypeHintLowering::LoweringResult TryBuildSimplifiedBinaryOp(
      const Operator* op, Node* left, Node* right, FeedbackSlot slot);
  JSTypeHintLowering::LoweringResult TryBuildSimplifiedForInNext(
      Node* receiver, Node* cache_array, Node* cache_type, Node* index,
      FeedbackSlot slot);
  JSTypeHintLowering::LoweringResult TryBuildSimplifiedForInPrepare(
      Node* receiver, FeedbackSlot slot);
  JSTypeHintLowering::LoweringResult TryBuildSimplifiedToNumber(
      Node* input, FeedbackSlot slot);
  JSTypeHintLowering::LoweringResult TryBuildSimplifiedCall(const Operator* op,
                                                            Node* const* args,
                                                            int arg_count,
                                                            FeedbackSlot slot);
  JSTypeHintLowering::LoweringResult TryBuildSimplifiedConstruct(
      const Operator* op, Node* const* args, int arg_count, FeedbackSlot slot);
  JSTypeHintLowering::LoweringResult TryBuildSimplifiedGetIterator(
      const Operator* op, Node* receiver, FeedbackSlot load_slot,
      FeedbackSlot call_slot);
  JSTypeHintLowering::LoweringResult TryBuildSimplifiedLoadNamed(
      const Operator* op, FeedbackSlot slot);
  JSTypeHintLowering::LoweringResult TryBuildSimplifiedLoadKeyed(
      const Operator* op, Node* receiver, Node* key, FeedbackSlot slot);
  JSTypeHintLowering::LoweringResult TryBuildSimplifiedStoreNamed(
      const Operator* op, Node* receiver, Node* value, FeedbackSlot slot);
  JSTypeHintLowering::LoweringResult TryBuildSimplifiedStoreKeyed(
      const Operator* op, Node* receiver, Node* key, Node* value,
      FeedbackSlot slot);

  // Applies the given early reduction onto the current environment.
  void ApplyEarlyReduction(JSTypeHintLowering::LoweringResult reduction);

  // Check the context chain for extensions, for lookup fast paths.
  Environment* CheckContextExtensions(uint32_t depth);
  // Slow path taken when we cannot figure out the current scope info.
  Environment* CheckContextExtensionsSlowPath(uint32_t depth);
  // Helper function that tries to get the current scope info.
  OptionalScopeInfoRef TryGetScopeInfo();
  // Helper function to create a context extension check.
  Environment* CheckContextExtensionAtDepth(Environment* slow_environment,
                                            uint32_t depth);

  // Helper function to create for-in mode from the recorded type feedback.
  ForInMode GetForInMode(FeedbackSlot slot);

  // Helper function to compute call frequency from the recorded type
  // feedback. Returns unknown if invocation count is unknown. Returns 0 if
  // feedback is insufficient.
  CallFrequency ComputeCallFrequency(int slot_id) const;

  // Helper function to extract the speculation mode from the recorded type
  // feedback. Returns kDisallowSpeculation if feedback is insufficient.
  SpeculationMode GetSpeculationMode(int slot_id) const;

  // Helper function to determine the call feedback relation from the recorded
  // type feedback. Returns kUnrelated if feedback is insufficient.
  CallFeedbackRelation ComputeCallFeedbackRelation(int slot_id) const;

  // Helpers for building the implicit FunctionEntry and IterationBody
  // StackChecks.
  void BuildFunctionEntryStackCheck();
  void BuildIterationBodyStackCheck();
  void BuildOSREntryStackCheck();

  // Control flow plumbing.
  void BuildJump();
  void BuildJumpIf(Node* condition);
  void BuildJumpIfNot(Node* condition);
  void BuildJumpIfEqual(Node* comperand);
  void BuildJumpIfNotEqual(Node* comperand);
  void BuildJumpIfTrue();
  void BuildJumpIfFalse();
  void BuildJumpIfToBooleanTrue();
  void BuildJumpIfToBooleanFalse();
  void BuildJumpIfNotHole();
  void BuildJumpIfJSReceiver();
  void BuildJumpIfForInDone();

  void BuildSwitchOnSmi(Node* condition);
  void BuildSwitchOnGeneratorState(
      const ZoneVector<ResumeJumpTarget>& resume_jump_targets,
      bool allow_fallthrough_on_executing);

  // Simulates control flow by forward-propagating environments.
  void MergeIntoSuccessorEnvironment(int target_offset);
  void BuildLoopHeaderEnvironment(int current_offset);
  void SwitchToMergeEnvironment(int current_offset);

  // Simulates control flow that exits the function body.
  void MergeControlToLeaveFunction(Node* exit);

  // Builds loop exit nodes for every exited loop between the current bytecode
  // offset and {target_offset}.
  void BuildLoopExitsForBranch(int target_offset);
  void BuildLoopExitsForFunctionExit(const BytecodeLivenessState* liveness);
  void BuildLoopExitsUntilLoop(int loop_offset,
                               const BytecodeLivenessState* liveness);

  // Helper for building a return (from an actual return or a suspend).
  void BuildReturn(const BytecodeLivenessState* liveness);

  // Simulates entry and exit of exception handlers.
  void ExitThenEnterExceptionHandlers(int current_offset);

  // Update the current position of {SourcePositionTable} and
  // {NodeOriginTable} to that bytecode at {offset}, if any.
  void UpdateSourceAndBytecodePosition(int offset);

  // Growth increment for the temporary buffer used to construct input lists to
  // new nodes.
  static const int kInputBufferSizeIncrement = 64;

  // An abstract representation for an exception handler that is being
  // entered and exited while the graph builder is iterating over the
  // underlying bytecode. The exception handlers within the bytecode are
  // well scoped, hence will form a stack during iteration.
  struct ExceptionHandler {
    int start_offset_;      // Start offset of the handled area in the bytecode.
    int end_offset_;        // End offset of the handled area in the bytecode.
    int handler_offset_;    // Handler entry offset within the bytecode.
    int context_register_;  // Index of register holding handler context.
  };

  template <class T = Object>
  typename ref_traits<T>::ref_type MakeRefForConstantForIndexOperand(
      int operand_index) {
    // The BytecodeArray itself was fetched by using a barrier so all reads
    // from the constant pool are safe.
    return MakeRefAssumeMemoryFence(
        broker(), broker()->CanonicalPersistentHandle(
                      Cast<T>(bytecode_iterator().GetConstantForIndexOperand(
                          operand_index, local_isolate_))));
  }

  Graph* graph() const { return jsgraph_->graph(); }
  CommonOperatorBuilder* common() const { return jsgraph_->common(); }
  Zone* graph_zone() const { return graph()->zone(); }
  JSGraph* jsgraph() const { return jsgraph_; }
  Isolate* isolate() const { return jsgraph_->isolate(); }
  JSOperatorBuilder* javascript() const { return jsgraph_->javascript(); }
  SimplifiedOperatorBuilder* simplified() const {
    return jsgraph_->simplified();
  }
  Zone* local_zone() const { return local_zone_; }
  BytecodeArrayRef bytecode_array() const { return bytecode_array_; }
  FeedbackVectorRef feedback_vector() const { return feedback_vector_; }
  const JSTypeHintLowering& type_hint_lowering() const {
    return type_hint_lowering_;
  }
  const FrameStateFunctionInfo* frame_state_function_info() const {
    return frame_state_function_info_;
  }
  SourcePositionTableIterator& source_position_iterator() {
    return *source_position_iterator_;
  }
  interpreter::BytecodeArrayIterator const& bytecode_iterator() const {
    return bytecode_iterator_;
  }
  interpreter::BytecodeArrayIterator& bytecode_iterator() {
    return bytecode_iterator_;
  }
  BytecodeAnalysis const& bytecode_analysis() const {
    return bytecode_analysis_;
  }
  int currently_peeled_loop_offset() const {
    return currently_peeled_loop_offset_;
  }
  void set_currently_peeled_loop_offset(int offset) {
    currently_peeled_loop_offset_ = offset;
  }
  bool skip_first_stack_check() const {
    return skip_first_stack_and_tierup_check_;
  }
  bool skip_tierup_check() const {
    return skip_first_stack_and_tierup_check_ || osr_;
  }
  int current_exception_handler() const { return current_exception_handler_; }
  void set_current_exception_handler(int index) {
    current_exception_handler_ = index;
  }
  bool needs_eager_checkpoint() const { return needs_eager_checkpoint_; }
  void mark_as_needing_eager_checkpoint(bool value) {
    needs_eager_checkpoint_ = value;
  }
  JSHeapBroker* broker() const { return broker_; }
  NativeContextRef native_context() const { return native_context_; }
  SharedFunctionInfoRef shared_info() const { return shared_info_; }

#define DECLARE_VISIT_BYTECODE(name, ...) void Visit##name();
  BYTECODE_LIST(DECLARE_VISIT_BYTECODE)
#undef DECLARE_VISIT_BYTECODE

  JSHeapBroker* const broker_;
  LocalIsolate* const local_isolate_;
  Zone* const local_zone_;
  JSGraph* const jsgraph_;
  // The native context for which we optimize.
  NativeContextRef const native_context_;
  SharedFunctionInfoRef const shared_info_;
  BytecodeArrayRef const bytecode_array_;
  FeedbackCellRef const feedback_cell_;
  FeedbackVectorRef const feedback_vector_;
  CallFrequency const invocation_frequency_;
  JSTypeHintLowering const type_hint_lowering_;
  const FrameStateFunctionInfo* const frame_state_function_info_;
  std::unique_ptr<SourcePositionTableIterator> source_position_iterator_;
  interpreter::BytecodeArrayIterator bytecode_iterator_;
  BytecodeAnalysis const bytecode_analysis_;
  Environment* environment_;
  BytecodePositionDecorator* decorator_;
  bool const osr_;
  int currently_peeled_loop_offset_;

  const bool skip_first_stack_and_tierup_check_;

  // Merge environments are snapshots of the environment at points where the
  // control flow merges. This models a forward data flow propagation of all
  // values from all predecessors of the merge in question. They are indexed by
  // the bytecode offset
  ZoneMap<int, Environment*> merge_environments_;

  // Generator merge environments are snapshots of the current resume
  // environment, tracing back through loop headers to the resume switch of a
  // generator. They allow us to model a single resume jump as several switch
  // statements across loop headers, keeping those loop headers reducible,
  // without having to merge the "executing" environments of the generator into
  // the "resuming" ones. They are indexed by the suspend id of the resume.
  ZoneMap<int, Environment*> generator_merge_environments_;

  ZoneVector<Node*> cached_parameters_;

  // Exception handlers currently entered by the iteration.
  ZoneStack<ExceptionHandler> exception_handlers_;
  int current_exception_handler_;

  // Temporary storage for building node input lists.
  int input_buffer_size_;
  Node** input_buffer_;

  const CodeKind code_kind_;
  Node* feedback_vector_node_;
  Node* native_context_node_;

  // Optimization to only create checkpoints when the current position in the
  // control-flow is not effect-dominated by another checkpoint already. All
  // operations that do not have observable side-effects can be re-evaluated.
  bool needs_eager_checkpoint_;

  // Nodes representing values in the activation record.
  SetOncePointer<Node> function_closure_;

  // Control nodes that exit the function body.
  ZoneVector<Node*> exit_controls_;

  StateValuesCache state_values_cache_;

  // The node origins table, to store bytecode origins.
  NodeOriginTable* const node_origins_;

  // The source position table, to be populated.
  SourcePositionTable* const source_positions_;

  SourcePosition const start_position_;

  TickCounter* const tick_counter_;

  ObserveNodeInfo const observe_node_info_;

  static constexpr int kBinaryOperationHintIndex = 1;
  static constexpr int kBinaryOperationSmiHintIndex = 1;
  static constexpr int kCompareOperationHintIndex = 1;
  static constexpr int kCountOperationHintIndex = 0;
  static constexpr int kUnaryOperationHintIndex = 0;
};

// The abstract execution environment simulates the content of the interpreter
// register file. The environment performs SSA-renaming of all tracked nodes at
// split and merge points in the control flow.
class BytecodeGraphBuilder::Environment : public ZoneObject {
 public:
  Environment(BytecodeGraphBuilder* builder, int register_count,
              int parameter_count,
              interpreter::Register incoming_new_target_or_generator,
              Node* control_dependency);

  // Specifies whether environment binding methods should attach frame state
  // inputs to nodes representing the value being bound. This is done because
  // the {OutputFrameStateCombine} is closely related to the binding method.
  enum FrameStateAttachmentMode { kAttachFrameState, kDontAttachFrameState };

  int parameter_count() const { return parameter_count_; }
  int register_count() const { return register_count_; }

  Node* LookupAccumulator() const;
  Node* LookupRegister(interpreter::Register the_register) const;
  Node* LookupGeneratorState() const;

  void BindAccumulator(Node* node,
                       FrameStateAttachmentMode mode = kDontAttachFrameState);
  void BindRegister(interpreter::Register the_register, Node* node,
                    FrameStateAttachmentMode mode = kDontAttachFrameState);
  void BindRegistersToProjections(
      interpreter::Register first_reg, Node* node,
      FrameStateAttachmentMode mode = kDontAttachFrameState);
  void BindGeneratorState(Node* node);
  void RecordAfterState(Node* node,
                        FrameStateAttachmentMode mode = kDontAttachFrameState);

  // Effect dependency tracked by this environment.
  Node* GetEffectDependency() { return effect_dependency_; }
  void UpdateEffectDependency(Node* dependency) {
    effect_dependency_ = dependency;
  }

  // Preserve a checkpoint of the environment for the IR graph. Any
  // further mutation of the environment will not affect checkpoints.
  Node* Checkpoint(BytecodeOffset bytecode_offset,
                   OutputFrameStateCombine combine,
                   const BytecodeLivenessState* liveness);

  // Control dependency tracked by this environment.
  Node* GetControlDependency() const { return control_dependency_; }
  void UpdateControlDependency(Node* dependency) {
    control_dependency_ = dependency;
  }

  Node* Context() const { return context_; }
  void SetContext(Node* new_context) { context_ = new_context; }

  Environment* Copy();
  void Merge(Environment* other, const BytecodeLivenessState* liveness);

  void FillWithOsrValues();
  void PrepareForLoop(const BytecodeLoopAssignments& assignments,
                      const BytecodeLivenessState* liveness);
  void PrepareForLoopExit(Node* loop,
                          const BytecodeLoopAssignments& assignments,
                          const BytecodeLivenessState* liveness);

 private:
  friend Zone;

  explicit Environment(const Environment* copy);

  bool StateValuesRequireUpdate(Node** state_values, Node** values, int count);
  void UpdateStateValues(Node** state_values, Node** values, int count);
  Node* GetStateValuesFromCache(Node** values, int count,
                                const BytecodeLivenessState* liveness);

  int RegisterToValuesIndex(interpreter::Register the_register) const;

  Zone* zone() const { return builder_->local_zone(); }
  Graph* graph() const { return builder_->graph(); }
  CommonOperatorBuilder* common() const { return builder_->common(); }
  BytecodeGraphBuilder* builder() const { return builder_; }
  const NodeVector* values() const { return &values_; }
  NodeVector* values() { return &values_; }
  int register_base() const { return register_base_; }
  int accumulator_base() const { return accumulator_base_; }

  BytecodeGraphBuilder* builder_;
  int register_count_;
  int parameter_count_;
  Node* context_;
  Node* control_dependency_;
  Node* effect_dependency_;
  NodeVector values_;
  Node* parameters_state_values_;
  Node* generator_state_;
  int register_base_;
  int accumulator_base_;
};

// A helper for creating a temporary sub-environment for simple branches.
struct BytecodeGraphBuilder::SubEnvironment final {
 public:
  explicit SubEnvironment(BytecodeGraphBuilder* builder)
      : builder_(builder), parent_(builder->environment()->Copy()) {}

  ~SubEnvironment() { builder_->set_environment(parent_); }

 private:
  BytecodeGraphBuilder* builder_;
  BytecodeGraphBuilder::Environment* parent_;
};

// Issues:
// - Scopes - intimately tied to AST. Need to eval what is needed.
// - Need to resolve closure parameter treatment.
BytecodeGraphBuilder::Environment::Environment(
    BytecodeGraphBuilder* builder, int register_count, int parameter_count,
    interpreter::Register incoming_new_target_or_generator,
    Node* control_dependency)
    : builder_(builder),
      register_count_(register_count),
      parameter_count_(parameter_count),
      control_dependency_(control_dependency),
      effect_dependency_(control_dependency),
      values_(builder->local_zone()),
      parameters_state_values_(nullptr),
      generator_state_(nullptr) {
  // The layout of values_ is:
  //
  // [receiver] [parameters] [registers] [accumulator]
  //
  // parameter[0] is the receiver (this), parameters 1..N are the
  // parameters supplied to the method (arg0..argN-1). The accumulator
  // is stored separately.

  // Parameters including the receiver
  for (int i = 0; i < parameter_count; i++) {
    const char* debug_name = (i == 0) ? "%this" : nullptr;
    Node* parameter = builder->GetParameter(i, debug_name);
    values()->push_back(parameter);
  }

  // Registers
  register_base_ = static_cast<int>(values()->size());
  Node* undefined_constant = builder->jsgraph()->UndefinedConstant();
  values()->insert(values()->end(), register_count, undefined_constant);

  // Accumulator
  accumulator_base_ = static_cast<int>(values()->size());
  values()->push_back(undefined_constant);

  // Context
  int context_index = Linkage::GetJSCallContextParamIndex(parameter_count);
  context_ = builder->GetParameter(context_index, "%context");

  // Incoming new.target or generator register
  if (incoming_new_target_or_generator.is_valid()) {
    int new_target_index =
        Linkage::GetJSCallNewTargetParamIndex(parameter_count);
    Node* new_target_node =
        builder->GetParameter(new_target_index, "%new.target");

    int values_index = RegisterToValuesIndex(incoming_new_target_or_generator);
    values()->at(values_index) = new_target_node;
  }
}

BytecodeGraphBuilder::Environment::Environment(
    const BytecodeGraphBuilder::Environment* other)
    : builder_(other->builder_),
      register_count_(other->register_count_),
      parameter_count_(other->parameter_count_),
      context_(other->context_),
      control_dependency_(other->control_dependency_),
      effect_dependency_(other->effect_dependency_),
      values_(other->zone()),
      parameters_state_values_(other->parameters_state_values_),
      generator_state_(other->generator_state_),
      register_base_(other->register_base_),
      accumulator_base_(other->accumulator_base_) {
  values_ = other->values_;
}


int BytecodeGraphBuilder::Environment::RegisterToValuesIndex(
    interpreter::Register the_register) const {
  if (the_register.is_parameter()) {
    return the_register.ToParameterIndex();
  } else {
    return the_register.index() + register_base();
  }
}

Node* BytecodeGraphBuilder::Environment::LookupAccumulator() const {
  return values()->at(accumulator_base_);
}

Node* BytecodeGraphBuilder::Environment::LookupGeneratorState() const {
  DCHECK_NOT_NULL(generator_state_);
  return generator_state_;
}

Node* BytecodeGraphBuilder::Environment::LookupRegister(
    interpreter::Register the_register) const {
  if (the_register.is_current_context()) {
    return Context();
  } else if (the_register.is_function_closure()) {
    return builder()->GetFunctionClosure();
  } else {
    int values_index = RegisterToValuesIndex(the_register);
    return values()->at(values_index);
  }
}

void BytecodeGraphBuilder::Environment::BindAccumulator(
    Node* node, FrameStateAttachmentMode mode) {
  if (mode == FrameStateAttachmentMode::kAttachFrameState) {
    builder()->PrepareFrameState(node, OutputFrameStateCombine::PokeAt(0));
  }
  values()->at(accumulator_base_) = node;
}

void BytecodeGraphBuilder::Environment::BindGeneratorState(Node* node) {
  generator_state_ = node;
}

void BytecodeGraphBuilder::Environment::BindRegister(
    interpreter::Register the_register, Node* node,
    FrameStateAttachmentMode mode) {
  int values_index = RegisterToValuesIndex(the_register);
  if (mode == FrameStateAttachmentMode::kAttachFrameState) {
    builder()->PrepareFrameState(node, OutputFrameStateCombine::PokeAt(
                                           accumulator_base_ - values_index));
  }
  values()->at(values_index) = node;
}

void BytecodeGraphBuilder::Environment::BindRegistersToProjections(
    interpreter::Register first_reg, Node* node,
    FrameStateAttachmentMode mode) {
  int values_index = RegisterToValuesIndex(first_reg);
  if (mode == FrameStateAttachmentMode::kAttachFrameState) {
    builder()->PrepareFrameState(node, OutputFrameStateCombine::PokeAt(
                                           accumulator_base_ - values_index));
  }
  for (int i = 0; i < node->op()->ValueOutputCount(); i++) {
    values()->at(values_index + i) =
        builder()->NewNode(common()->Projection(i), node);
  }
}

void BytecodeGraphBuilder::Environment::RecordAfterState(
    Node* node, FrameStateAttachmentMode mode) {
  if (mode == FrameStateAttachmentMode::kAttachFrameState) {
    builder()->PrepareFrameState(node, OutputFrameStateCombine::Ignore());
  }
}

BytecodeGraphBuilder::Environment* BytecodeGraphBuilder::Environment::Copy() {
  return zone()->New<Environment>(this);
}

void BytecodeGraphBuilder::Environment::Merge(
    BytecodeGraphBuilder::Environment* other,
    const BytecodeLivenessState* liveness) {
  // Create a merge of the control dependencies of both environments and update
  // the current environment's control dependency accordingly.
  Node* control = builder()->MergeControl(GetControlDependency(),
                                          other->GetControlDependency());
  UpdateControlDependency(control);

  // Create a merge of the effect dependencies of both environments and update
  // the current environment's effect dependency accordingly.
  Node* effect = builder()->MergeEffect(GetEffectDependency(),
                                        other->GetEffectDependency(), control);
  UpdateEffectDependency(effect);

  // Introduce Phi nodes for values that are live and have differing inputs at
  // the merge point, potentially extending an existing Phi node if possible.
  context_ = builder()->MergeValue(context_, other->context_, control);
  for (int i = 0; i < parameter_count(); i++) {
    values_[i] = builder()->MergeValue(values_[i], other->values_[i], control);
  }
  for (int i = 0; i < register_count(); i++) {
    int index = register_base() + i;
    if (liveness == nullptr || liveness->RegisterIsLive(i)) {
#if DEBUG
      // We only do these DCHECKs when we are not in the resume path of a
      // generator -- this is, when either there is no generator state at all,
      // or the generator state is not the constant "executing" value.
      if (generator_state_ == nullptr ||
          NumberMatcher(generator_state_)
              .Is(JSGeneratorObject::kGeneratorExecuting)) {
        DCHECK_NE(values_[index], builder()->jsgraph()->OptimizedOutConstant());
        DCHECK_NE(other->values_[index],
                  builder()->jsgraph()->OptimizedOutConstant());
      }
#endif

      values_[index] =
          builder()->MergeValue(values_[index], other->values_[index], control);

    } else {
      values_[index] = builder()->jsgraph()->OptimizedOutConstant();
    }
  }

  if (liveness == nullptr || liveness->AccumulatorIsLive()) {
    DCHECK_NE(values_[accumulator_base()],
              builder()->jsgraph()->OptimizedOutConstant());
    DCHECK_NE(other->values_[accumulator_base()],
              builder()->jsgraph()->OptimizedOutConstant());

    values_[accumulator_base()] =
        builder()->MergeValue(values_[accumulator_base()],
                              other->values_[accumulator_base()], control);
  } else {
    values_[accumulator_base()] = builder()->jsgraph()->OptimizedOutConstant();
  }

  if (generator_state_ != nullptr) {
    DCHECK_NOT_NULL(other->generator_state_);
    generator_state_ = builder()->MergeValue(generator_state_,
                                             other->generator_state_, control);
  }
}

void BytecodeGraphBuilder::Environment::PrepareForLoop(
    const BytecodeLoopAssignments& assignments,
    const BytecodeLivenessState* liveness) {
  // Create a control node for the loop header.
  Node* control = builder()->NewLoop();

  // Create a Phi for external effects.
  Node* effect = builder()->NewEffectPhi(1, GetEffectDependency(), control);
  UpdateEffectDependency(effect);

  // Create Phis for any values that are live on entry to the loop and may be
  // updated by the end of the loop.
  context_ = builder()->NewPhi(1, context_, control);
  for (int i = 0; i < parameter_count(); i++) {
    if (assignments.ContainsParameter(i)) {
      values_[i] = builder()->NewPhi(1, values_[i], control);
    }
  }
  for (int i = 0; i < register_count(); i++) {
    if (assignments.ContainsLocal(i) &&
        (liveness == nullptr || liveness->RegisterIsLive(i))) {
      int index = register_base() + i;
      values_[index] = builder()->NewPhi(1, values_[index], control);
    }
  }
  // The accumulator should not be live on entry.
  DCHECK_IMPLIES(liveness != nullptr, !liveness->AccumulatorIsLive());

  if (generator_state_ != nullptr) {
    generator_state_ = builder()->NewPhi(1, generator_state_, control);
  }

  // Connect to the loop end.
  Node* terminate = builder()->graph()->NewNode(
      builder()->common()->Terminate(), effect, control);
  builder()->exit_controls_.push_back(terminate);
}

void BytecodeGraphBuilder::Environment::FillWithOsrValues() {
  Node* start = graph()->start();

  // Create OSR values for each environment value.
  SetContext(graph()->NewNode(
      common()->OsrValue(Linkage::kOsrContextSpillSlotIndex), start));
  int size = static_cast<int>(values()->size());
  for (int i = 0; i < size; i++) {
    int idx = i;  // Indexing scheme follows {StandardFrame}, adapt accordingly.
    if (i >= register_base()) idx += InterpreterFrameConstants::kExtraSlotCount;
    if (i >= accumulator_base()) idx = Linkage::kOsrAccumulatorRegisterIndex;
    values()->at(i) = graph()->NewNode(common()->OsrValue(idx), start);
  }
}

bool BytecodeGraphBuilder::Environment::StateValuesRequireUpdate(
    Node** state_values, Node** values, int count) {
  if (*state_values == nullptr) {
    return true;
  }
  Node::Inputs inputs = (*state_values)->inputs();
  if (inputs.count() != count) return true;
  for (int i = 0; i < count; i++) {
    if (inputs[i] != values[i]) {
      return true;
    }
  }
  return false;
}

void BytecodeGraphBuilder::Environment::PrepareForLoopExit(
    Node* loop, const BytecodeLoopAssignments& assignments,
    const BytecodeLivenessState* liveness) {
  DCHECK_EQ(loop->opcode(), IrOpcode::kLoop);

  Node* control = GetControlDependency();

  // Create the loop exit node.
  Node* loop_exit = graph()->NewNode(common()->LoopExit(), control, loop);
  UpdateControlDependency(loop_exit);

  // Rename the effect.
  Node* effect_rename = graph()->NewNode(common()->LoopExitEffect(),
                                         GetEffectDependency(), loop_exit);
  UpdateEffectDependency(effect_rename);

  // TODO(jarin) We should also rename context here. However, unconditional
  // renaming confuses global object and native context specialization.
  // We should only rename if the context is assigned in the loop.

  // Rename the environment values if they were assigned in the loop and are
  // live after exiting the loop.
  for (int i = 0; i < parameter_count(); i++) {
    if (assignments.ContainsParameter(i)) {
      Node* rename = graph()->NewNode(
          common()->LoopExitValue(MachineRepresentation::kTagged), values_[i],
          loop_exit);
      values_[i] = rename;
    }
  }
  for (int i = 0; i < register_count(); i++) {
    if (assignments.ContainsLocal(i) &&
        (liveness == nullptr || liveness->RegisterIsLive(i))) {
      Node* rename = graph()->NewNode(
          common()->LoopExitValue(MachineRepresentation::kTagged),
          values_[register_base() + i], loop_exit);
      values_[register_base() + i] = rename;
    }
  }
  if (liveness == nullptr || liveness->AccumulatorIsLive()) {
    Node* rename = graph()->NewNode(
        common()->LoopExitValue(MachineRepresentation::kTagged),
        values_[accumulator_base()], loop_exit);
    values_[accumulator_base()] = rename;
  }

  if (generator_state_ != nullptr) {
    generator_state_ = graph()->NewNode(
        common()->LoopExitValue(MachineRepresentation::kTagged),
        generator_state_, loop_exit);
  }
}

void BytecodeGraphBuilder::Environment::UpdateStateValues(Node** state_values,
                                                          Node** values,
                                                          int count) {
  if (StateValuesRequireUpdate(state_values, values, count)) {
    const Operator* op = common()->StateValues(count, SparseInputMask::Dense());
    (*state_values) = graph()->NewNode(op, count, values);
  }
}

Node* BytecodeGraphBuilder::Environment::GetStateValuesFromCache(
    Node** values, int count, const BytecodeLivenessState* liveness) {
  return builder_->state_values_cache_.GetNodeForValues(
      values, static_cast<size_t>(count), liveness);
}

Node* BytecodeGraphBuilder::Environment::Checkpoint(
    BytecodeOffset bailout_id, OutputFrameStateCombine combine,
    const BytecodeLivenessState* liveness) {
  if (parameter_count() == register_count()) {
    // Re-use the state-value cache if the number of local registers happens
    // to match the parameter count.
    parameters_state_values_ =
        GetStateValuesFromCache(&values()->at(0), parameter_count(), nullptr);
  } else {
    UpdateStateValues(&parameters_state_values_, &values()->at(0),
                      parameter_count());
  }

  Node* registers_state_values = GetStateValuesFromCache(
      &values()->at(register_base()), register_count(), liveness);

  bool accumulator_is_live = !liveness || liveness->AccumulatorIsLive();
  Node* accumulator_state_value =
      accumulator_is_live && combine != OutputFrameStateCombine::PokeAt(0)
          ? values()->at(accumulator_base())
          : builder()->jsgraph()->OptimizedOutConstant();

  const Operator* op = common()->FrameState(
      bailout_id, combine, builder()->frame_state_function_info());
  Node* result = graph()->NewNode(
      op, parameters_state_values_, registers_state_values,
      accumulator_state_value, Context(), builder()->GetFunctionClosure(),
      builder()->graph()->start());

  return result;
}

class BytecodeGraphBuilder::BytecodePositionDecorator final :
public GraphDecorator {
 public:
  explicit BytecodePositionDecorator(NodeOriginTable* node_origins)
      :  node_origins_(node_origins) {}

  void Decorate(Node* node) final {
    node_origins_->SetNodeOrigin(node->id(), NodeOrigin::kJSBytecode,
                                 node_origins_->GetCurrentBytecodePosition());
  }

 private:
  NodeOriginTable* node_origins_;
};

BytecodeGraphBuilder::BytecodeGraphBuilder(
    JSHeapBroker* broker, Zone* local_zone, NativeContextRef native_context,
    SharedFunctionInfoRef shared_info, FeedbackCellRef feedback_cell,
    BytecodeOffset osr_offset, JSGraph* jsgraph,
    CallFrequency const& invocation_frequency,
    SourcePositionTable* source_positions, NodeOriginTable* node_origins,
    int inlining_id, CodeKind code_kind, BytecodeGraphBuilderFlags flags,
    TickCounter* tick_counter, ObserveNodeInfo const& observe_node_info)
    : broker_(broker),
      local_isolate_(broker_->local_isolate()
                         ? broker_->local_isolate()
                         : broker_->isolate()->AsLocalIsolate()),
      local_zone_(local_zone),
      jsgraph_(jsgraph),
      native_context_(native_context),
      shared_info_(shared_info),
      bytecode_array_(shared_info.GetBytecodeArray(broker)),
      feedback_cell_(feedback_cell),
      feedback_vector_(feedback_cell.feedback_vector(broker).value()),
      invocation_frequency_(invocation_frequency),
      type_hint_lowering_(
          broker, jsgraph, feedback_vector_,
          (flags & BytecodeGraphBuilderFlag::kBailoutOnUninitialized)
              ? JSTypeHintLowering::kBailoutOnUninitialized
              : JSTypeHintLowering::kNoFlags),
      frame_state_function_info_(common()->CreateFrameStateFunctionInfo(
          FrameStateType::kUnoptimizedFunction,
          bytecode_array().parameter_count(), bytecode_array().max_arguments(),
          bytecode_array().register_count(), shared_info.object())),
      source_position_iterator_(std::make_unique<SourcePositionTableIterator>(
          bytecode_array().SourcePositionTable(broker))),
      bytecode_iterator_(bytecode_array().object()),
      bytecode_analysis_(
          bytecode_array().object(), local_zone, osr_offset,
          flags & BytecodeGraphBuilderFlag::kAnalyzeEnvironmentLiveness),
      environment_(nullptr),
      decorator_(nullptr),
      osr_(!osr_offset.IsNone()),
      currently_peeled_loop_offset_(-1),
      skip_first_stack_and_tierup_check_(
          flags & BytecodeGraphBuilderFlag::kSkipFirstStackAndTierupCheck),
      merge_environments_(local_zone),
      generator_merge_environments_(local_zone),
      cached_parameters_(local_zone),
      exception_handlers_(local_zone),
      current_exception_handler_(0),
      input_buffer_size_(0),
      input_buffer_(nullptr),
      code_kind_(code_kind),
      feedback_vector_node_(nullptr),
      native_context_node_(nullptr),
      needs_eager_checkpoint_(true),
      exit_controls_(local_zone),
      state_values_cache_(jsgraph),
      node_origins_(node_origins),
      source_positions_(source_positions),
      start_position_(shared_info.StartPosition(), inlining_id),
      tick_counter_(tick_counter),
      observe_node_info_(observe_node_info) {}

Node* BytecodeGraphBuilder::GetFunctionClosure() {
  if (!function_closure_.is_set()) {
    int index = Linkage::kJSCallClosureParamIndex;
    Node* node = GetParameter(index, "%closure");
    function_closure_.set(node);
  }
  return function_closure_.get();
}

Node* BytecodeGraphBuilder::GetParameter(int parameter_index,
                                         const char* debug_name_hint) {
  // We use negative indices for some parameters.
  DCHECK_LE(ParameterInfo::kMinIndex, parameter_index);
  const size_t index =
      static_cast<size_t>(parameter_index - ParameterInfo::kMinIndex);

  if (cached_parameters_.size() <= index) {
    cached_parameters_.resize(index + 1, nullptr);
  }

  if (cached_parameters_[index] == nullptr) {
    cached_parameters_[index] =
        NewNode(common()->Parameter(parameter_index, debug_name_hint),
                graph()->start());
  }

  return cached_parameters_[index];
}

void BytecodeGraphBuilder::CreateFeedbackVectorNode() {
  DCHECK_NULL(feedback_vector_node_);
  feedback_vector_node_ =
      jsgraph()->ConstantNoHole(feedback_vector(), broker());
}

Node* BytecodeGraphBuilder::BuildLoadFeedbackCell(int index) {
  return jsgraph()->ConstantNoHole(
      feedback_vector().GetClosureFeedbackCell(broker(), index), broker());
}

void BytecodeGraphBuilder::CreateNativeContextNode() {
  DCHECK_NULL(native_context_node_);
  native_context_node_ = jsgraph()->ConstantNoHole(native_context(), broker());
}

Node* BytecodeGraphBuilder::BuildLoadNativeContextField(int index) {
  Node* result = NewNode(javascript()->LoadContext(0, index, true));
  NodeProperties::ReplaceContextInput(result, native_context_node());
  return result;
}

FeedbackSource BytecodeGraphBuilder::CreateFeedbackSource(int slot_id) {
  return CreateFeedbackSource(FeedbackVector::ToSlot(slot_id));
}

FeedbackSource BytecodeGraphBuilder::CreateFeedbackSource(FeedbackSlot slot) {
  return FeedbackSource(feedback_vector(), slot);
}

void BytecodeGraphBuilder::CreateGraph() {
  SourcePositionTable::Scope pos_scope(source_positions_, start_position_);
  if (node_origins_) {
    AddBytecodePositionDecorator();
  }
  // Set up the basic structure of the graph. Outputs for {Start} are the formal
  // parameters (including the receiver) plus new target, number of arguments,
  // context and closure.
  int start_output_arity = StartNode::OutputArityForFormalParameterCount(
      bytecode_array().parameter_count());
  graph()->SetStart(graph()->NewNode(common()->Start(start_output_arity)));

  Environment env(this, bytecode_array().register_count(),
                  bytecode_array().parameter_count(),
                  bytecode_array().incoming_new_target_or_generator_register(),
                  graph()->start());
  set_environment(&env);

  CreateFeedbackVectorNode();
  CreateNativeContextNode();

  VisitBytecodes();

  // Finish the basic structure of the graph.
  DCHECK_NE(0u, exit_controls_.size());
  int const input_count = static_cast<int>(exit_controls_.size());
  Node** const inputs = &exit_controls_.front();
  Node* end = graph()->NewNode(common()->End(input_count), input_count, inputs);
  graph()->SetEnd(end);
  if (node_origins_) {
    RemoveBytecodePositionDecorator();
  }
}

void BytecodeGraphBuilder::PrepareEagerCheckpoint() {
  if (needs_eager_checkpoint()) {
    // Create an explicit checkpoint node for before the operation. This only
    // needs to happen if we aren't effect-dominated by a {Checkpoint} already.
    mark_as_needing_eager_checkpoint(false);
    Node* node = NewNode(common()->Checkpoint());
    DCHECK_EQ(1, OperatorProperties::GetFrameStateInputCount(node->op()));
    DCHECK_EQ(IrOpcode::kDead,
              NodeProperties::GetFrameStateInput(node)->opcode());
    BytecodeOffset bailout_id(bytecode_iterator().current_offset());

    const BytecodeLivenessState* liveness_before =
        bytecode_analysis().GetInLivenessFor(
            bytecode_iterator().current_offset());

    Node* frame_state_before = environment()->Checkpoint(
        bailout_id, OutputFrameStateCombine::Ignore(), liveness_before);
    NodeProperties::ReplaceFrameStateInput(node, frame_state_before);
#ifdef DEBUG
  } else {
    // In case we skipped checkpoint creation above, we must be able to find an
    // existing checkpoint that effect-dominates the nodes about to be created.
    // Starting a search from the current effect-dependency has to succeed.
    Node* effect = environment()->GetEffectDependency();
    while (effect->opcode() != IrOpcode::kCheckpoint) {
      DCHECK(effect->op()->HasProperty(Operator::kNoWrite));
      DCHECK_EQ(1, effect->op()->EffectInputCount());
      effect = NodeProperties::GetEffectInput(effect);
    }
  }
#else
  }
#endif  // DEBUG
}

void BytecodeGraphBuilder::PrepareFrameState(
    Node* node, OutputFrameStateCombine combine, BytecodeOffset bailout_id,
    const BytecodeLivenessState* liveness) {
  if (OperatorProperties::HasFrameStateInput(node->op())) {
    // Add the frame state for after the operation. The node in question has
    // already been created and had a {Dead} frame state input up until now.
    DCHECK_EQ(1, OperatorProperties::GetFrameStateInputCount(node->op()));
    DCHECK_EQ(IrOpcode::kDead,
              NodeProperties::GetFrameStateInput(node)->opcode());

    Node* frame_state_after =
        environment()->Checkpoint(bailout_id, combine, liveness);
    NodeProperties::ReplaceFrameStateInput(node, frame_state_after);
  }
}

void BytecodeGraphBuilder::AdvanceIteratorsTo(int bytecode_offset) {
  for (; bytecode_iterator().current_offset() != bytecode_offset;
       bytecode_iterator().Advance()) {
    int current_offset = bytecode_iterator().current_offset();
    UpdateSourceAndBytecodePosition(current_offset);
  }
}

// Stores the state of the SourcePosition iterator, and the index to the
// current exception handlers stack. We need, during the OSR graph generation,
// to backup the states of these iterators at the LoopHeader offset of each
// outer loop which contains the OSR loop. The iterators are then restored when
// peeling the loops, so that both exception handling and synchronisation with
// the source position can be achieved.
class BytecodeGraphBuilder::OsrIteratorState {
 public:
  explicit OsrIteratorState(BytecodeGraphBuilder* graph_builder)
      : graph_builder_(graph_builder),
        saved_states_(graph_builder->local_zone()) {}

  void ProcessOsrPrelude() {
    ZoneVector<int> outer_loop_offsets(graph_builder_->local_zone());
    int osr_entry = graph_builder_->bytecode_analysis().osr_entry_point();

    // We find here the outermost loop which contains the OSR loop.
    int outermost_loop_offset = osr_entry;
    while ((outermost_loop_offset = graph_builder_->bytecode_analysis()
                                        .GetLoopInfoFor(outermost_loop_offset)
                                        .parent_offset()) != -1) {
      outer_loop_offsets.push_back(outermost_loop_offset);
    }
    outermost_loop_offset =
        outer_loop_offsets.empty() ? osr_entry : outer_loop_offsets.back();
    graph_builder_->AdvanceIteratorsTo(outermost_loop_offset);

    // We save some iterators states at the offsets of the loop headers of the
    // outer loops (the ones containing the OSR loop). They will be used for
    // jumping back in the bytecode.
    for (ZoneVector<int>::const_reverse_iterator it =
             outer_loop_offsets.crbegin();
         it != outer_loop_offsets.crend(); ++it) {
      graph_builder_->AdvanceIteratorsTo(*it);
      graph_builder_->ExitThenEnterExceptionHandlers(
          graph_builder_->bytecode_iterator().current_offset());
      saved_states_.push(IteratorsStates(
          graph_builder_->current_exception_handler(),
          graph_builder_->source_position_iterator().GetState()));
    }

    // Finishing by advancing to the OSR entry
    graph_builder_->AdvanceIteratorsTo(osr_entry);

    // Enters all remaining exception handler which end before the OSR loop
    // so that on next call of VisitSingleBytecode they will get popped from
    // the exception handlers stack.
    graph_builder_->ExitThenEnterExceptionHandlers(osr_entry);
    graph_builder_->set_currently_peeled_loop_offset(
        graph_builder_->bytecode_analysis()
            .GetLoopInfoFor(osr_entry)
            .parent_offset());
  }

  void RestoreState(int target_offset, int new_parent_offset) {
    graph_builder_->bytecode_iterator().SetOffset(target_offset);
    // In case of a return, we must not build loop exits for
    // not-yet-built outer loops.
    graph_builder_->set_currently_peeled_loop_offset(new_parent_offset);
    IteratorsStates saved_state = saved_states_.top();
    graph_builder_->source_position_iterator().RestoreState(
        saved_state.source_iterator_state_);
    graph_builder_->set_current_exception_handler(
        saved_state.exception_handler_index_);
    saved_states_.pop();
  }

 private:
  struct IteratorsStates {
    int exception_handler_index_;
    SourcePositionTableIterator::IndexAndPositionState source_iterator_state_;

    IteratorsStates(int exception_handler_index,
                    SourcePositionTableIterator::IndexAndPositionState
                        source_iterator_state)
        : exception_handler_index_(exception_handler_index),
          source_iterator_state_(source_iterator_state) {}
  };

  BytecodeGraphBuilder* graph_builder_;
  ZoneStack<IteratorsStates> saved_states_;
};

void BytecodeGraphBuilder::RemoveMergeEnvironmentsBeforeOffset(
    int limit_offset) {
  if (!merge_environments_.empty()) {
    ZoneMap<int, Environment*>::iterator it = merge_environments_.begin();
    ZoneMap<int, Environment*>::iterator stop_it = merge_environments_.end();
    while (it != stop_it && it->first <= limit_offset) {
      it = merge_environments_.erase(it);
    }
  }
}

void BytecodeGraphBuilder::BuildFunctionEntryStackCheck() {
  if (!skip_first_stack_check()) {
    DCHECK(exception_handlers_.empty());
    Node* node =
        NewNode(javascript()->StackCheck(StackCheckKind::kJSFunctionEntry));
    PrepareFrameStateForFunctionEntryStackCheck(node);
  }
}

void BytecodeGraphBuilder::BuildIterationBodyStackCheck() {
  Node* node =
      NewNode(javascript()->StackCheck(StackCheckKind::kJSIterationBody));
  environment()->RecordAfterState(node, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::BuildOSREntryStackCheck() {
  DCHECK(exception_handlers_.empty());
  Node* node =
      NewNode(javascript()->StackCheck(StackCheckKind::kJSFunctionEntry));
  PrepareFrameStateForOSREntryStackCheck(node);
}

// We will iterate through the OSR loop, then its parent, and so on
// until we have reached the outmost loop containing the OSR loop. We do
// not generate nodes for anything before the outermost loop.
void BytecodeGraphBuilder::AdvanceToOsrEntryAndPeelLoops() {
  environment()->FillWithOsrValues();

  // The entry stack check has to happen *before* initialising the OSR prelude;
  // it has to happen before setting up exception handlers, so that the
  // optimized code can't accidentally catch a failingstack with a OSR-ed loop
  // inside a try-catch, e.g.
  //
  //   try {
  //     loop { OSR(); }
  //   } catch {
  //     // Ignore failed stack check.
  //   }
  BuildOSREntryStackCheck();

  OsrIteratorState iterator_states(this);
  iterator_states.ProcessOsrPrelude();
  int osr_entry = bytecode_analysis().osr_entry_point();
  DCHECK_EQ(bytecode_iterator().current_offset(), osr_entry);

  // Suppose we have n nested loops, loop_0 being the outermost one, and
  // loop_n being the OSR loop. We start iterating the bytecode at the header
  // of loop_n (the OSR loop), and then we peel the part of the the body of
  // loop_{n-1} following the end of loop_n. We then rewind the iterator to
  // the header of loop_{n-1}, and so on until we have partly peeled loop 0.
  // The full loop_0 body will be generating with the rest of the function,
  // outside the OSR generation.

  // To do so, if we are visiting a loop, we continue to visit what's left
  // of its parent, and then when reaching the parent's JumpLoop, we do not
  // create any jump for that but rewind the bytecode iterator to visit the
  // parent loop entirely, and so on.

  int current_parent_offset =
      bytecode_analysis().GetLoopInfoFor(osr_entry).parent_offset();
  while (current_parent_offset != -1) {
    const LoopInfo& current_parent_loop =
        bytecode_analysis().GetLoopInfoFor(current_parent_offset);
    // We iterate until the back edge of the parent loop, which we detect by
    // the offset that the JumpLoop targets.
    for (; !bytecode_iterator().done(); bytecode_iterator().Advance()) {
      if (bytecode_iterator().current_bytecode() ==
              interpreter::Bytecode::kJumpLoop &&
          bytecode_iterator().GetJumpTargetOffset() == current_parent_offset) {
        // Reached the end of the current parent loop.
        break;
      }
      VisitSingleBytecode();
    }
    DCHECK(!bytecode_iterator()
                .done());  // Should have found the loop's jump target.

    // We also need to take care of the merge environments and exceptions
    // handlers here because the omitted JumpLoop bytecode can still be the
    // target of jumps or the first bytecode after a try block.
    ExitThenEnterExceptionHandlers(bytecode_iterator().current_offset());
    SwitchToMergeEnvironment(bytecode_iterator().current_offset());

    // This jump is the jump of our parent loop, which is not yet created.
    // So we do not build the jump nodes, but restore the bytecode and the
    // SourcePosition iterators to the values they had when we were visiting
    // the offset pointed at by the JumpLoop we've just reached.
    // We have already built nodes for inner loops, but now we will
    // iterate again over them and build new nodes corresponding to the same
    // bytecode offsets. Any jump or reference to this inner loops must now
    // point to the new nodes we will build, hence we clear the relevant part
    // of the environment.
    // Completely clearing the environment is not possible because merge
    // environments for forward jumps out of the loop need to be preserved
    // (e.g. a return or a labeled break in the middle of a loop).
    RemoveMergeEnvironmentsBeforeOffset(bytecode_iterator().current_offset());
    iterator_states.RestoreState(current_parent_offset,
                                 current_parent_loop.parent_offset());
    current_parent_offset = current_parent_loop.parent_offset();
  }
}

void BytecodeGraphBuilder::VisitSingleBytecode() {
  tick_counter_->TickAndMaybeEnterSafepoint();
  int current_offset = bytecode_iterator().current_offset();
  UpdateSourceAndBytecodePosition(current_offset);
  ExitThenEnterExceptionHandlers(current_offset);
  DCHECK_GE(exception_handlers_.empty() ? current_offset
                                        : exception_handlers_.top().end_offset_,
            current_offset);
  SwitchToMergeEnvironment(current_offset);

  if (environment() != nullptr) {
    BuildLoopHeaderEnvironment(current_offset);

    switch (bytecode_iterator().current_bytecode()) {
#define BYTECODE_CASE(name, ...)       \
  case interpreter::Bytecode::k##name: \
    Visit##name();                     \
    break;
      BYTECODE_LIST(BYTECODE_CASE)
#undef BYTECODE_CASE
    }
  }
}

void BytecodeGraphBuilder::VisitBytecodes() {
  if (!bytecode_analysis().resume_jump_targets().empty()) {
    environment()->BindGeneratorState(
        jsgraph()->SmiConstant(JSGeneratorObject::kGeneratorExecuting));
  }

  if (osr_) {
    // We peel the OSR loop and any outer loop containing it except that we
    // leave the nodes corresponding to the whole outermost loop (including
    // the last copies of the loops it contains) to be generated by the normal
    // bytecode iteration below.
    AdvanceToOsrEntryAndPeelLoops();
  } else {
    BuildFunctionEntryStackCheck();
  }

  for (; !bytecode_iterator().done(); bytecode_iterator().Advance()) {
    VisitSingleBytecode();
  }

  DCHECK(exception_handlers_.empty());
}

void BytecodeGraphBuilder::AddBytecodePositionDecorator() {
  DCHECK_NULL(decorator_);
  decorator_ = graph_zone()->New<BytecodePositionDecorator>(node_origins_);
  graph()->AddDecorator(decorator_);
}

void BytecodeGraphBuilder::RemoveBytecodePositionDecorator() {
  DCHECK_NOT_NULL(decorator_);
  graph()->RemoveDecorator(decorator_);
  decorator_ = nullptr;
}

void BytecodeGraphBuilder::VisitLdaZero() {
  Node* node = jsgraph()->ZeroConstant();
  environment()->BindAccumulator(node);
}

void BytecodeGraphBuilder::VisitLdaSmi() {
  Node* node =
      jsgraph()->ConstantNoHole(bytecode_iterator().GetImmediateOperand(0));
  environment()->BindAccumulator(node);
}

void BytecodeGraphBuilder::VisitLdaConstant() {
  ObjectRef object = MakeRefForConstantForIndexOperand(0);
  Node* node = jsgraph()->ConstantNoHole(object, broker());
  environment()->BindAccumulator(node);
}

void BytecodeGraphBuilder::VisitLdaUndefined() {
  Node* node = jsgraph()->UndefinedConstant();
  environment()->BindAccumulator(node);
}

void BytecodeGraphBuilder::VisitLdaNull() {
  Node* node = jsgraph()->NullConstant();
  environment()->BindAccumulator(node);
}

void BytecodeGraphBuilder::VisitLdaTheHole() {
  Node* node = jsgraph()->TheHoleConstant();
  environment()->BindAccumulator(node);
}

void BytecodeGraphBuilder::VisitLdaTrue() {
  Node* node = jsgraph()->TrueConstant();
  environment()->BindAccumulator(node);
}

void BytecodeGraphBuilder::VisitLdaFalse() {
  Node* node = jsgraph()->FalseConstant();
  environment()->BindAccumulator(node);
}

void BytecodeGraphBuilder::VisitLdar() {
  Node* value =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  environment()->BindAccumulator(value);
}

void BytecodeGraphBuilder::VisitStar() {
  Node* value = environment()->LookupAccumulator();
  environment()->BindRegister(bytecode_iterator().GetRegisterOperand(0), value);
}

#define SHORT_STAR_VISITOR(Name, ...)                                         \
  void BytecodeGraphBuilder::Visit##Name() {                                  \
    Node* value = environment()->LookupAccumulator();                         \
    environment()->BindRegister(                                              \
        interpreter::Register::FromShortStar(interpreter::Bytecode::k##Name), \
        value);                                                               \
  }
SHORT_STAR_BYTECODE_LIST(SHORT_STAR_VISITOR)
#undef SHORT_STAR_VISITOR

void BytecodeGraphBuilder::VisitMov() {
  Node* value =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  environment()->BindRegister(bytecode_iterator().GetRegisterOperand(1), value);
}

Node* BytecodeGraphBuilder::BuildLoadGlobal(NameRef name,
                                            uint32_t feedback_slot_index,
                                            TypeofMode typeof_mode) {
  FeedbackSource feedback = CreateFeedbackSource(feedback_slot_index);
  DCHECK(IsLoadGlobalICKind(broker()->GetFeedbackSlotKind(feedback)));
  const Operator* op = javascript()->LoadGlobal(name, feedback, typeof_mode);
  DCHECK(IrOpcode::IsFeedbackCollectingOpcode(op->opcode()));
  return NewNode(op, feedback_vector_node());
}

void BytecodeGraphBuilder::VisitLdaGlobal() {
  PrepareEagerCheckpoint();
  NameRef name = MakeRefForConstantForIndexOperand<Name>(0);
  uint32_t feedback_slot_index = bytecode_iterator().GetIndexOperand(1);
  Node* node =
      BuildLoadGlobal(name, feedback_slot_index, TypeofMode::kNotInside);
  environment()->BindAccumulator(node, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitLdaGlobalInsideTypeof() {
  PrepareEagerCheckpoint();
  NameRef name = MakeRefForConstantForIndexOperand<Name>(0);
  uint32_t feedback_slot_index = bytecode_iterator().GetIndexOperand(1);
  Node* node = BuildLoadGlobal(name, feedback_slot_index, TypeofMode::kInside);
  environment()->BindAccumulator(node, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitStaGlobal() {
  PrepareEagerCheckpoint();
  NameRef name = MakeRefForConstantForIndexOperand<Name>(0);
  FeedbackSource feedback =
      CreateFeedbackSource(bytecode_iterator().GetIndexOperand(1));
  Node* value = environment()->LookupAccumulator();

  LanguageMode language_mode =
      GetLanguageModeFromSlotKind(broker()->GetFeedbackSlotKind(feedback));
  const Operator* op = javascript()->StoreGlobal(language_mode, name, feedback);
  DCHECK(IrOpcode::IsFeedbackCollectingOpcode(op->opcode()));
  Node* node = NewNode(op, value, feedback_vector_node());
  environment()->RecordAfterState(node, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitStaInArrayLiteral() {
  PrepareEagerCheckpoint();
  Node* value = environment()->LookupAccumulator();
  Node* array =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  Node* index =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(1));
  FeedbackSource feedback =
      CreateFeedbackSource(bytecode_iterator().GetIndexOperand(2));
  const Operator* op = javascript()->StoreInArrayLiteral(feedback);

  JSTypeHintLowering::LoweringResult lowering =
      TryBuildSimplifiedStoreKeyed(op, array, index, value, feedback.slot);
  if (lowering.IsExit()) return;

  Node* node = nullptr;
  if (lowering.IsSideEffectFree()) {
    node = lowering.value();
  } else {
    DCHECK(!lowering.Changed());
    DCHECK(IrOpcode::IsFeedbackCollectingOpcode(op->opcode()));
    node = NewNode(op, array, index, value, feedback_vector_node());
  }

  environment()->RecordAfterState(node, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitDefineKeyedOwnPropertyInLiteral() {
  PrepareEagerCheckpoint();

  Node* object =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  Node* name =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(1));
  Node* value = environment()->LookupAccumulator();
  int flags = bytecode_iterator().GetFlag8Operand(2);
  FeedbackSource feedback =
      CreateFeedbackSource(bytecode_iterator().GetIndexOperand(3));
  const Operator* op = javascript()->DefineKeyedOwnPropertyInLiteral(feedback);

  JSTypeHintLowering::LoweringResult lowering =
      TryBuildSimplifiedStoreKeyed(op, object, name, value, feedback.slot);
  if (lowering.IsExit()) return;

  Node* node = nullptr;
  if (lowering.IsSideEffectFree()) {
    node = lowering.value();
  } else {
    DCHECK(!lowering.Changed());
    DCHECK(IrOpcode::IsFeedbackCollectingOpcode(op->opcode()));
    node = NewNode(op, object, name, value, jsgraph()->ConstantNoHole(flags),
                   feedback_vector_node());
  }

  environment()->RecordAfterState(node, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitLdaContextSlot() {
  const Operator* op = javascript()->LoadContext(
      bytecode_iterator().GetUnsignedImmediateOperand(2),
      bytecode_iterator().GetIndexOperand(1), false);
  Node* node = NewNode(op);
  Node* context =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  NodeProperties::ReplaceContextInput(node, context);
  environment()->BindAccumulator(node);
}

void BytecodeGraphBuilder::VisitLdaImmutableContextSlot() {
  const Operator* op = javascript()->LoadContext(
      bytecode_iterator().GetUnsignedImmediateOperand(2),
      bytecode_iterator().GetIndexOperand(1), true);
  Node* node = NewNode(op);
  Node* context =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  NodeProperties::ReplaceContextInput(node, context);
  environment()->BindAccumulator(node);
}

void BytecodeGraphBuilder::VisitLdaCurrentContextSlot() {
  const Operator* op = javascript()->LoadContext(
      0, bytecode_iterator().GetIndexOperand(0), false);
  Node* node = NewNode(op);
  environment()->BindAccumulator(node);
}

void BytecodeGraphBuilder::VisitLdaImmutableCurrentContextSlot() {
  const Operator* op = javascript()->LoadContext(
      0, bytecode_iterator().GetIndexOperand(0), true);
  Node* node = NewNode(op);
  environment()->BindAccumulator(node);
}

void BytecodeGraphBuilder::VisitStaContextSlot() {
  const Operator* op = javascript()->StoreContext(
      bytecode_iterator().GetUnsignedImmediateOperand(2),
      bytecode_iterator().GetIndexOperand(1));
  Node* value = environment()->LookupAccumulator();
  Node* node = NewNode(op, value);
  Node* context =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  NodeProperties::ReplaceContextInput(node, context);
}

void BytecodeGraphBuilder::VisitStaCurrentContextSlot() {
  const Operator* op =
      javascript()->StoreContext(0, bytecode_iterator().GetIndexOperand(0));
  Node* value = environment()->LookupAccumulator();
  NewNode(op, value);
}

void BytecodeGraphBuilder::VisitStaScriptContextSlot() {
  PrepareEagerCheckpoint();
  const Operator* op = javascript()->StoreScriptContext(
      bytecode_iterator().GetUnsignedImmediateOperand(2),
      bytecode_iterator().GetIndexOperand(1));
  Node* value = environment()->LookupAccumulator();
  Node* node = NewNode(op, value);
  Node* context =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  NodeProperties::ReplaceContextInput(node, context);
}

void BytecodeGraphBuilder::VisitStaCurrentScriptContextSlot() {
  PrepareEagerCheckpoint();
  const Operator* op = javascript()->StoreScriptContext(
      0, bytecode_iterator().GetIndexOperand(0));
  Node* value = environment()->LookupAccumulator();
  NewNode(op, value);
}

void BytecodeGraphBuilder::BuildLdaLookupSlot(TypeofMode typeof_mode) {
  PrepareEagerCheckpoint();
  Node* name =
      jsgraph()->ConstantNoHole(MakeRefForConstantForIndexOperand(0), broker());
  const Operator* op =
      javascript()->CallRuntime(typeof_mode == TypeofMode::kNotInside
                                    ? Runtime::kLoadLookupSlot
                                    : Runtime::kLoadLookupSlotInsideTypeof);
  Node* value = NewNode(op, name);
  environment()->BindAccumulator(value, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitLdaLookupSlot() {
  BuildLdaLookupSlot(TypeofMode::kNotInside);
}

void BytecodeGraphBuilder::VisitLdaLookupSlotInsideTypeof() {
  BuildLdaLookupSlot(TypeofMode::kInside);
}

BytecodeGraphBuilder::Environment*
BytecodeGraphBuilder::CheckContextExtensionAtDepth(
    Environment* slow_environment, uint32_t depth) {
  Node* extension_slot = NewNode(
      javascript()->LoadContext(depth, Context::EXTENSION_INDEX, false));
  Node* check_no_extension =
      NewNode(simplified()->ReferenceEqual(), extension_slot,
              jsgraph()->UndefinedConstant());
  NewBranch(check_no_extension);
  {
    SubEnvironment sub_environment(this);
    NewIfFalse();
    // If there is an extension, merge into the slow path.
    if (slow_environment == nullptr) {
      slow_environment = environment();
      NewMerge();
    } else {
      slow_environment->Merge(environment(),
                              bytecode_analysis().GetInLivenessFor(
                                  bytecode_iterator().current_offset()));
    }
  }
  NewIfTrue();
  // Do nothing on if there is no extension, eventually falling through to
  // the fast path.
  DCHECK_NOT_NULL(slow_environment);
  return slow_environment;
}

OptionalScopeInfoRef BytecodeGraphBuilder::TryGetScopeInfo() {
  Node* context = environment()->Context();
  switch (context->opcode()) {
    case IrOpcode::kJSCreateFunctionContext:
      return CreateFunctionContextParametersOf(context->op()).scope_info();
    case IrOpcode::kJSCreateBlockContext:
    case IrOpcode::kJSCreateCatchContext:
    case IrOpcode::kJSCreateWithContext:
      return ScopeInfoOf(context->op());
    case IrOpcode::kParameter: {
      ScopeInfoRef scope_info = shared_info_.scope_info(broker());
      if (scope_info.HasOuterScopeInfo()) {
        scope_info = scope_info.OuterScopeInfo(broker());
      }
      return scope_info;
    }
    default:
      return std::nullopt;
  }
}

BytecodeGraphBuilder::Environment* BytecodeGraphBuilder::CheckContextExtensions(
    uint32_t depth) {
  OptionalScopeInfoRef maybe_scope_info = TryGetScopeInfo();
  if (!maybe_scope_info.has_value()) {
    return CheckContextExtensionsSlowPath(depth);
  }

  ScopeInfoRef scope_info = maybe_scope_info.value();
  // We only need to check up to the last-but-one depth, because an eval
  // in the same scope as the variable itself has no way of shadowing it.
  Environment* slow_environment = nullptr;
  for (uint32_t d = 0; d < depth; d++) {
    // Const tracking let data is stored in the extension slot of a
    // ScriptContext - however, it's unrelated to the sloppy eval variable
    // extension. We should never iterate through a ScriptContext here.
    DCHECK_NE(scope_info.scope_type(), ScopeType::SCRIPT_SCOPE);
    DCHECK_NE(scope_info.scope_type(), ScopeType::REPL_MODE_SCOPE);

    if (scope_info.HasContextExtensionSlot()) {
      slow_environment = CheckContextExtensionAtDepth(slow_environment, d);
    }
    DCHECK_IMPLIES(!scope_info.HasOuterScopeInfo(), d + 1 == depth);
    if (scope_info.HasOuterScopeInfo()) {
      scope_info = scope_info.OuterScopeInfo(broker());
    }
  }

  // There should have been at least one slow path generated, otherwise we could
  // have already skipped the lookup in the bytecode.
  DCHECK_NOT_NULL(slow_environment);
  return slow_environment;
}

BytecodeGraphBuilder::Environment*
BytecodeGraphBuilder::CheckContextExtensionsSlowPath(uint32_t depth) {
  // Output environment where the context has an extension
  Environment* slow_environment = nullptr;

  // We only need to check up to the last-but-one depth, because an eval
  // in the same scope as the variable itself has no way of shadowing it.
  for (uint32_t d = 0; d < depth; d++) {
    Node* has_extension = NewNode(javascript()->HasContextExtension(d));

    Environment* undefined_extension_env;
    NewBranch(has_extension);
    {
      SubEnvironment sub_environment(this);
      NewIfTrue();
      slow_environment = CheckContextExtensionAtDepth(slow_environment, d);
      undefined_extension_env = environment();
    }
    NewIfFalse();
    environment()->Merge(undefined_extension_env,
                         bytecode_analysis().GetInLivenessFor(
                             bytecode_iterator().current_offset()));
    mark_as_needing_eager_checkpoint(true);
    // Do nothing on if there is no extension, eventually falling through to
    // the fast path.
  }

  // There should have been at least one slow path generated, otherwise we could
  // have already skipped the lookup in the bytecode.
  DCHECK_NOT_NULL(slow_environment);
  return slow_environment;
}

void BytecodeGraphBuilder::BuildLdaLookupContextSlot(TypeofMode typeof_mode) {
  uint32_t depth = bytecode_iterator().GetUnsignedImmediateOperand(2);

  // Check if any context in the depth has an extension.
  Environment* slow_environment = CheckContextExtensions(depth);

  // Fast path, do a context load.
  {
    uint32_t slot_index = bytecode_iterator().GetIndexOperand(1);

    const Operator* op = javascript()->LoadContext(depth, slot_index, false);
    environment()->BindAccumulator(NewNode(op));
  }

  // Add a merge to the fast environment.
  NewMerge();
  Environment* fast_environment = environment();

  // Slow path, do a runtime load lookup.
  set_environment(slow_environment);
  {
    Node* name = jsgraph()->ConstantNoHole(MakeRefForConstantForIndexOperand(0),
                                           broker());

    const Operator* op =
        javascript()->CallRuntime(typeof_mode == TypeofMode::kNotInside
                                      ? Runtime::kLoadLookupSlot
                                      : Runtime::kLoadLookupSlotInsideTypeof);
    Node* value = NewNode(op, name);
    environment()->BindAccumulator(value, Environment::kAttachFrameState);
  }

  fast_environment->Merge(environment(),
                          bytecode_analysis().GetOutLivenessFor(
                              bytecode_iterator().current_offset()));
  set_environment(fast_environment);
  mark_as_needing_eager_checkpoint(true);
}

void BytecodeGraphBuilder::VisitLdaLookupContextSlot() {
  BuildLdaLookupContextSlot(TypeofMode::kNotInside);
}

void BytecodeGraphBuilder::VisitLdaLookupContextSlotInsideTypeof() {
  BuildLdaLookupContextSlot(TypeofMode::kInside);
}

void BytecodeGraphBuilder::BuildLdaLookupGlobalSlot(TypeofMode typeof_mode) {
  uint32_t depth = bytecode_iterator().GetUnsignedImmediateOperand(2);

  // Check if any context in the depth has an extension.
  Environment* slow_environment = CheckContextExtensions(depth);

  // Fast path, do a global load.
  {
    PrepareEagerCheckpoint();
    NameRef name = MakeRefForConstantForIndexOperand<Name>(0);
    uint32_t feedback_slot_index = bytecode_iterator().GetIndexOperand(1);
    Node* node = BuildLoadGlobal(name, feedback_slot_index, typeof_mode);
    environment()->BindAccumulator(node, Environment::kAttachFrameState);
  }

  // Add a merge to the fast environment.
  NewMerge();
  Environment* fast_environment = environment();

  // Slow path, do a runtime load lookup.
  set_environment(slow_environment);
  {
    Node* name = jsgraph()->ConstantNoHole(
        MakeRefForConstantForIndexOperand<Name>(0), broker());

    const Operator* op =
        javascript()->CallRuntime(typeof_mode == TypeofMode::kNotInside
                                      ? Runtime::kLoadLookupSlot
                                      : Runtime::kLoadLookupSlotInsideTypeof);
    Node* value = NewNode(op, name);
    environment()->BindAccumulator(value, Environment::kAttachFrameState);
  }

  fast_environment->Merge(environment(),
                          bytecode_analysis().GetOutLivenessFor(
                              bytecode_iterator().current_offset()));
  set_environment(fast_environment);
  mark_as_needing_eager_checkpoint(true);
}

void BytecodeGraphBuilder::VisitLdaLookupGlobalSlot() {
  BuildLdaLookupGlobalSlot(TypeofMode::kNotInside);
}

void BytecodeGraphBuilder::VisitLdaLookupGlobalSlotInsideTypeof() {
  BuildLdaLookupGlobalSlot(TypeofMode::kInside);
}

void BytecodeGraphBuilder::VisitStaLookupSlot() {
  PrepareEagerCheckpoint();
  Node* value = environment()->LookupAccumulator();
  Node* name =
      jsgraph()->ConstantNoHole(MakeRefForConstantForIndexOperand(0), broker());
  int bytecode_flags = bytecode_iterator().GetFlag8Operand(1);
  LanguageMode language_mode = static_cast<LanguageMode>(
      interpreter::StoreLookupSlotFlags::LanguageModeBit::decode(
          bytecode_flags));
  LookupHoistingMode lookup_hoisting_mode = static_cast<LookupHoistingMode>(
      interpreter::StoreLookupSlotFlags::LookupHoistingModeBit::decode(
          bytecode_flags));
  DCHECK_IMPLIES(lookup_hoisting_mode == LookupHoistingMode::kLegacySloppy,
                 is_sloppy(language_mode));
  const Operator* op = javascript()->CallRuntime(
      is_strict(language_mode)
          ? Runtime::kStoreLookupSlot_Strict
          : lookup_hoisting_mode == LookupHoistingMode::kLegacySloppy
                ? Runtime::kStoreLookupSlot_SloppyHoisting
                : Runtime::kStoreLookupSlot_Sloppy);
  Node* store = NewNode(op, name, value);
  environment()->BindAccumulator(store, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitGetNamedProperty() {
  PrepareEagerCheckpoint();
  Node* object =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  NameRef name = MakeRefForConstantForIndexOperand<Name>(1);
  FeedbackSource feedback =
      CreateFeedbackSource(bytecode_iterator().GetIndexOperand(2));
  const Operator* op = javascript()->LoadNamed(name, feedback);

  JSTypeHintLowering::LoweringResult lowering =
      TryBuildSimplifiedLoadNamed(op, feedback.slot);
  if (lowering.IsExit()) return;

  Node* node = nullptr;
  if (lowering.IsSideEffectFree()) {
    node = lowering.value();
  } else {
    DCHECK(!lowering.Changed());
    DCHECK(IrOpcode::IsFeedbackCollectingOpcode(op->opcode()));
    node = NewNode(op, object, feedback_vector_node());
  }
  environment()->BindAccumulator(node, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitGetNamedPropertyFromSuper() {
  PrepareEagerCheckpoint();
  Node* receiver =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  Node* home_object = environment()->LookupAccumulator();
  NameRef name = MakeRefForConstantForIndexOperand<Name>(1);

  FeedbackSource feedback =
      CreateFeedbackSource(bytecode_iterator().GetIndexOperand(2));
  const Operator* op = javascript()->LoadNamedFromSuper(name, feedback);

  JSTypeHintLowering::LoweringResult lowering =
      TryBuildSimplifiedLoadNamed(op, feedback.slot);
  if (lowering.IsExit()) return;

  Node* node = nullptr;
  if (lowering.IsSideEffectFree()) {
    node = lowering.value();
  } else {
    DCHECK(!lowering.Changed());
    DCHECK(IrOpcode::IsFeedbackCollectingOpcode(op->opcode()));
    node = NewNode(op, receiver, home_object, feedback_vector_node());
  }
  environment()->BindAccumulator(node, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitGetKeyedProperty() {
  PrepareEagerCheckpoint();
  Node* key = environment()->LookupAccumulator();
  Node* object =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  FeedbackSource feedback =
      CreateFeedbackSource(bytecode_iterator().GetIndexOperand(1));
  const Operator* op = javascript()->LoadProperty(feedback);

  JSTypeHintLowering::LoweringResult lowering =
      TryBuildSimplifiedLoadKeyed(op, object, key, feedback.slot);
  if (lowering.IsExit()) return;

  Node* node = nullptr;
  if (lowering.IsSideEffectFree()) {
    node = lowering.value();
  } else {
    DCHECK(!lowering.Changed());
    static_assert(JSLoadPropertyNode::ObjectIndex() == 0);
    static_assert(JSLoadPropertyNode::KeyIndex() == 1);
    static_assert(JSLoadPropertyNode::FeedbackVectorIndex() == 2);
    DCHECK(IrOpcode::IsFeedbackCollectingOpcode(op->opcode()));
    node = NewNode(op, object, key, feedback_vector_node());
  }
  environment()->BindAccumulator(node, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitGetEnumeratedKeyedProperty() {
  // GetEnumeratedKeyedProperty <object> <enum_index> <cache_type> <slot>
  PrepareEagerCheckpoint();
  Node* key = environment()->LookupAccumulator();
  Node* object =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  FeedbackSource feedback =
      CreateFeedbackSource(bytecode_iterator().GetIndexOperand(3));
  const Operator* op = javascript()->LoadProperty(feedback);

  static_assert(JSLoadPropertyNode::ObjectIndex() == 0);
  static_assert(JSLoadPropertyNode::KeyIndex() == 1);
  static_assert(JSLoadPropertyNode::FeedbackVectorIndex() == 2);
  DCHECK(IrOpcode::IsFeedbackCollectingOpcode(op->opcode()));
  Node* node = NewNode(op, object, key, feedback_vector_node());
  environment()->BindAccumulator(node, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::BuildNamedStore(NamedStoreMode store_mode) {
  PrepareEagerCheckpoint();
  Node* value = environment()->LookupAccumulator();
  Node* object =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  NameRef name = MakeRefForConstantForIndexOperand<Name>(1);
  FeedbackSource feedback =
      CreateFeedbackSource(bytecode_iterator().GetIndexOperand(2));

  const Operator* op;
  if (store_mode == NamedStoreMode::kDefineOwn) {
    DCHECK_EQ(FeedbackSlotKind::kDefineNamedOwn,
              broker()->GetFeedbackSlotKind(feedback));

    op = javascript()->DefineNamedOwnProperty(name, feedback);
  } else {
    DCHECK_EQ(NamedStoreMode::kSet, store_mode);
    LanguageMode language_mode =
        GetLanguageModeFromSlotKind(broker()->GetFeedbackSlotKind(feedback));
    op = javascript()->SetNamedProperty(language_mode, name, feedback);
  }

  JSTypeHintLowering::LoweringResult lowering =
      TryBuildSimplifiedStoreNamed(op, object, value, feedback.slot);
  if (lowering.IsExit()) return;

  Node* node = nullptr;
  if (lowering.IsSideEffectFree()) {
    node = lowering.value();
  } else {
    DCHECK(!lowering.Changed());
    DCHECK(IrOpcode::IsFeedbackCollectingOpcode(op->opcode()));
    node = NewNode(op, object, value, feedback_vector_node());
  }
  environment()->RecordAfterState(node, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitSetNamedProperty() {
  BuildNamedStore(NamedStoreMode::kSet);
}

void BytecodeGraphBuilder::VisitDefineNamedOwnProperty() {
  BuildNamedStore(NamedStoreMode::kDefineOwn);
}

void BytecodeGraphBuilder::VisitSetKeyedProperty() {
  PrepareEagerCheckpoint();
  Node* value = environment()->LookupAccumulator();
  Node* object =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  Node* key =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(1));
  FeedbackSource source =
      CreateFeedbackSource(bytecode_iterator().GetIndexOperand(2));
  LanguageMode language_mode =
      GetLanguageModeFromSlotKind(broker()->GetFeedbackSlotKind(source));
  const Operator* op = javascript()->SetKeyedProperty(language_mode, source);

  JSTypeHintLowering::LoweringResult lowering =
      TryBuildSimplifiedStoreKeyed(op, object, key, value, source.slot);
  if (lowering.IsExit()) return;

  Node* node = nullptr;
  if (lowering.IsSideEffectFree()) {
    node = lowering.value();
  } else {
    DCHECK(!lowering.Changed());
    static_assert(JSSetKeyedPropertyNode::ObjectIndex() == 0);
    static_assert(JSSetKeyedPropertyNode::KeyIndex() == 1);
    static_assert(JSSetKeyedPropertyNode::ValueIndex() == 2);
    static_assert(JSSetKeyedPropertyNode::FeedbackVectorIndex() == 3);
    DCHECK(IrOpcode::IsFeedbackCollectingOpcode(op->opcode()));
    node = NewNode(op, object, key, value, feedback_vector_node());
  }

  environment()->RecordAfterState(node, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitDefineKeyedOwnProperty() {
  PrepareEagerCheckpoint();
  Node* value = environment()->LookupAccumulator();
  Node* object =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  Node* key =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(1));
  int flags = bytecode_iterator().GetFlag8Operand(2);
  FeedbackSource source =
      CreateFeedbackSource(bytecode_iterator().GetIndexOperand(3));
  LanguageMode language_mode =
      GetLanguageModeFromSlotKind(broker()->GetFeedbackSlotKind(source));

  const Operator* op =
      javascript()->DefineKeyedOwnProperty(language_mode, source);

  JSTypeHintLowering::LoweringResult lowering =
      TryBuildSimplifiedStoreKeyed(op, object, key, value, source.slot);
  if (lowering.IsExit()) return;

  Node* node = nullptr;
  if (lowering.IsSideEffectFree()) {
    node = lowering.value();
  } else {
    DCHECK(!lowering.Changed());
    static_assert(JSDefineKeyedOwnPropertyNode::ObjectIndex() == 0);
    static_assert(JSDefineKeyedOwnPropertyNode::KeyIndex() == 1);
    static_assert(JSDefineKeyedOwnPropertyNode::ValueIndex() == 2);
    static_assert(JSDefineKeyedOwnPropertyNode::FlagsIndex() == 3);
    static_assert(JSDefineKeyedOwnPropertyNode::FeedbackVectorIndex() == 4);
    DCHECK(IrOpcode::IsFeedbackCollectingOpcode(op->opcode()));
    node = NewNode(op, object, key, value, jsgraph()->ConstantNoHole(flags),
                   feedback_vector_node());
  }

  environment()->RecordAfterState(node, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitLdaModuleVariable() {
  int32_t cell_index = bytecode_iterator().GetImmediateOperand(0);
  uint32_t depth = bytecode_iterator().GetUnsignedImmediateOperand(1);
  Node* module =
      NewNode(javascript()->LoadContext(depth, Context::EXTENSION_INDEX, true));
  Node* value = NewNode(javascript()->LoadModule(cell_index), module);
  environment()->BindAccumulator(value);
}

void BytecodeGraphBuilder::VisitStaModuleVariable() {
  int32_t cell_index = bytecode_iterator().GetImmediateOperand(0);
  uint32_t depth = bytecode_iterator().GetUnsignedImmediateOperand(1);
  Node* module =
      NewNode(javascript()->LoadContext(depth, Context::EXTENSION_INDEX, true));
  Node* value = environment()->LookupAccumulator();
  NewNode(javascript()->StoreModule(cell_index), module, value);
}

void BytecodeGraphBuilder::VisitPushContext() {
  Node* new_context = environment()->LookupAccumulator();
  environment()->BindRegister(bytecode_iterator().GetRegisterOperand(0),
                              environment()->Context());
  environment()->SetContext(new_context);
}

void BytecodeGraphBuilder::VisitPopContext() {
  Node* context =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  environment()->SetContext(context);
}

void BytecodeGraphBuilder::VisitCreateClosure() {
  SharedFunctionInfoRef shared_info =
      MakeRefForConstantForIndexOperand<SharedFunctionInfo>(0);
  AllocationType allocation =
      interpreter::CreateClosureFlags::PretenuredBit::decode(
          bytecode_iterator().GetFlag8Operand(2))
          ? AllocationType::kOld
          : AllocationType::kYoung;
  CodeRef compile_lazy =
      MakeRef(broker(), *BUILTIN_CODE(jsgraph()->isolate(), CompileLazy));
  const Operator* op =
      javascript()->CreateClosure(shared_info, compile_lazy, allocation);
  Node* closure = NewNode(
      op, BuildLoadFeedbackCell(bytecode_iterator().GetIndexOperand(1)));
  environment()->BindAccumulator(closure);
}

void BytecodeGraphBuilder::VisitCreateBlockContext() {
  ScopeInfoRef scope_info = MakeRefForConstantForIndexOperand<ScopeInfo>(0);
  const Operator* op = javascript()->CreateBlockContext(scope_info);
  Node* context = NewNode(op);
  environment()->BindAccumulator(context);
}

void BytecodeGraphBuilder::VisitCreateFunctionContext() {
  ScopeInfoRef scope_info = MakeRefForConstantForIndexOperand<ScopeInfo>(0);
  uint32_t slots = bytecode_iterator().GetUnsignedImmediateOperand(1);
  const Operator* op =
      javascript()->CreateFunctionContext(scope_info, slots, FUNCTION_SCOPE);
  Node* context = NewNode(op);
  environment()->BindAccumulator(context);
}

void BytecodeGraphBuilder::VisitCreateEvalContext() {
  ScopeInfoRef scope_info = MakeRefForConstantForIndexOperand<ScopeInfo>(0);
  uint32_t slots = bytecode_iterator().GetUnsignedImmediateOperand(1);
  const Operator* op =
      javascript()->CreateFunctionContext(scope_info, slots, EVAL_SCOPE);
  Node* context = NewNode(op);
  environment()->BindAccumulator(context);
}

void BytecodeGraphBuilder::VisitCreateCatchContext() {
  interpreter::Register reg = bytecode_iterator().GetRegisterOperand(0);
  Node* exception = environment()->LookupRegister(reg);
  ScopeInfoRef scope_info = MakeRefForConstantForIndexOperand<ScopeInfo>(1);

  const Operator* op = javascript()->CreateCatchContext(scope_info);
  Node* context = NewNode(op, exception);
  environment()->BindAccumulator(context);
}

void BytecodeGraphBuilder::VisitCreateWithContext() {
  Node* object =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  ScopeInfoRef scope_info = MakeRefForConstantForIndexOperand<ScopeInfo>(1);

  const Operator* op = javascript()->CreateWithContext(scope_info);
  Node* context = NewNode(op, object);
  environment()->BindAccumulator(context);
}

void BytecodeGraphBuilder::BuildCreateArguments(CreateArgumentsType type) {
  const Operator* op = javascript()->CreateArguments(type);
  Node* object = NewNode(op, GetFunctionClosure());
  environment()->BindAccumulator(object, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitCreateMappedArguments() {
  BuildCreateArguments(CreateArgumentsType::kMappedArguments);
}

void BytecodeGraphBuilder::VisitCreateUnmappedArguments() {
  BuildCreateArguments(CreateArgumentsType::kUnmappedArguments);
}

void BytecodeGraphBuilder::VisitCreateRestParameter() {
  BuildCreateArguments(CreateArgumentsType::kRestParameter);
}

void BytecodeGraphBuilder::VisitCreateRegExpLiteral() {
  StringRef constant_pattern = MakeRefForConstantForIndexOperand<String>(0);
  int const slot_id = bytecode_iterator().GetIndexOperand(1);
  FeedbackSource pair = CreateFeedbackSource(slot_id);
  int literal_flags = bytecode_iterator().GetFlag16Operand(2);
  static_assert(JSCreateLiteralRegExpNode::FeedbackVectorIndex() == 0);
  const Operator* op =
      javascript()->CreateLiteralRegExp(constant_pattern, pair, literal_flags);
  DCHECK(IrOpcode::IsFeedbackCollectingOpcode(op->opcode()));
  Node* literal = NewNode(op, feedback_vector_node());
  environment()->BindAccumulator(literal, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitCreateArrayLiteral() {
  ArrayBoilerplateDescriptionRef array_boilerplate_description =
      MakeRefForConstantForIndexOperand<ArrayBoilerplateDescription>(0);
  int const slot_id = bytecode_iterator().GetIndexOperand(1);
  FeedbackSource pair = CreateFeedbackSource(slot_id);
  int bytecode_flags = bytecode_iterator().GetFlag8Operand(2);
  int literal_flags =
      interpreter::CreateArrayLiteralFlags::FlagsBits::decode(bytecode_flags);
  // Disable allocation site mementos. Only unoptimized code will collect
  // feedback about allocation site. Once the code is optimized we expect the
  // data to converge. So, we disable allocation site mementos in optimized
  // code. We can revisit this when we have data to the contrary.
  literal_flags |= ArrayLiteral::kDisableMementos;
  int number_of_elements =
      array_boilerplate_description.constants_elements_length();
  static_assert(JSCreateLiteralArrayNode::FeedbackVectorIndex() == 0);
  const Operator* op = javascript()->CreateLiteralArray(
      array_boilerplate_description, pair, literal_flags, number_of_elements);
  DCHECK(IrOpcode::IsFeedbackCollectingOpcode(op->opcode()));
  Node* literal = NewNode(op, feedback_vector_node());
  environment()->BindAccumulator(literal, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitCreateEmptyArrayLiteral() {
  int const slot_id = bytecode_iterator().GetIndexOperand(0);
  FeedbackSource pair = CreateFeedbackSource(slot_id);
  const Operator* op = javascript()->CreateEmptyLiteralArray(pair);
  DCHECK(IrOpcode::IsFeedbackCollectingOpcode(op->opcode()));
  Node* literal = NewNode(op, feedback_vector_node());
  environment()->BindAccumulator(literal);
}

void BytecodeGraphBuilder::VisitCreateArrayFromIterable() {
  Node* iterable = NewNode(javascript()->CreateArrayFromIterable(),
                           environment()->LookupAccumulator());
  environment()->BindAccumulator(iterable, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitCreateObjectLiteral() {
  ObjectBoilerplateDescriptionRef constant_properties =
      MakeRefForConstantForIndexOperand<ObjectBoilerplateDescription>(0);
  int const slot_id = bytecode_iterator().GetIndexOperand(1);
  FeedbackSource pair = CreateFeedbackSource(slot_id);
  int bytecode_flags = bytecode_iterator().GetFlag8Operand(2);
  int literal_flags =
      interpreter::CreateObjectLiteralFlags::FlagsBits::decode(bytecode_flags);
  int number_of_properties = constant_properties.boilerplate_properties_count();
  static_assert(JSCreateLiteralObjectNode::FeedbackVectorIndex() == 0);
  const Operator* op = javascript()->CreateLiteralObject(
      constant_properties, pair, literal_flags, number_of_properties);
  DCHECK(IrOpcode::IsFeedbackCollectingOpcode(op->opcode()));
  Node* literal = NewNode(op, feedback_vector_node());
  environment()->BindAccumulator(literal, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitCreateEmptyObjectLiteral() {
  Node* literal = NewNode(javascript()->CreateEmptyLiteralObject());
  environment()->BindAccumulator(literal);
}

void BytecodeGraphBuilder::VisitCloneObject() {
  PrepareEagerCheckpoint();
  Node* source =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  int flags = bytecode_iterator().GetFlag8Operand(1);
  int slot = bytecode_iterator().GetIndexOperand(2);
  const Operator* op =
      javascript()->CloneObject(CreateFeedbackSource(slot), flags);
  static_assert(JSCloneObjectNode::SourceIndex() == 0);
  static_assert(JSCloneObjectNode::FeedbackVectorIndex() == 1);
  DCHECK(IrOpcode::IsFeedbackCollectingOpcode(op->opcode()));
  Node* value = NewNode(op, source, feedback_vector_node());
  environment()->BindAccumulator(value, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitGetTemplateObject() {
  FeedbackSource source =
      CreateFeedbackSource(bytecode_iterator().GetIndexOperand(1));
  TemplateObjectDescriptionRef description =
      MakeRefForConstantForIndexOperand<TemplateObjectDescription>(0);
  static_assert(JSGetTemplateObjectNode::FeedbackVectorIndex() == 0);
  const Operator* op =
      javascript()->GetTemplateObject(description, shared_info(), source);
  DCHECK(IrOpcode::IsFeedbackCollectingOpcode(op->opcode()));
  Node* template_object = NewNode(op, feedback_vector_node());
  environment()->BindAccumulator(template_object);
}

Node* const* BytecodeGraphBuilder::GetCallArgumentsFromRegisters(
    Node* callee, Node* receiver, interpreter::Register first_arg,
    int arg_count) {
  const int arity = JSCallNode::ArityForArgc(arg_count);
  Node** all = local_zone()->AllocateArray<Node*>(static_cast<size_t>(arity));
  int cursor = 0;

  static_assert(JSCallNode::TargetIndex() == 0);
  static_assert(JSCallNode::ReceiverIndex() == 1);
  static_assert(JSCallNode::FirstArgumentIndex() == 2);
  static_assert(JSCallNode::kFeedbackVectorIsLastInput);

  all[cursor++] = callee;
  all[cursor++] = receiver;

  // The function arguments are in consecutive registers.
  const int arg_base = first_arg.index();
  for (int i = 0; i < arg_count; ++i) {
    all[cursor++] =
        environment()->LookupRegister(interpreter::Register(arg_base + i));
  }

  all[cursor++] = feedback_vector_node();

  DCHECK_EQ(cursor, arity);
  return all;
}

void BytecodeGraphBuilder::BuildCall(ConvertReceiverMode receiver_mode,
                                     Node* const* args, size_t arg_count,
                                     int slot_id) {
  DCHECK_EQ(interpreter::Bytecodes::GetReceiverMode(
                bytecode_iterator().current_bytecode()),
            receiver_mode);
  PrepareEagerCheckpoint();

  FeedbackSource feedback = CreateFeedbackSource(slot_id);
  CallFrequency frequency = ComputeCallFrequency(slot_id);
  SpeculationMode speculation_mode = GetSpeculationMode(slot_id);
  CallFeedbackRelation call_feedback_relation =
      ComputeCallFeedbackRelation(slot_id);
  const Operator* op =
      javascript()->Call(arg_count, frequency, feedback, receiver_mode,
                         speculation_mode, call_feedback_relation);
  DCHECK(IrOpcode::IsFeedbackCollectingOpcode(op->opcode()));

  JSTypeHintLowering::LoweringResult lowering = TryBuildSimplifiedCall(
      op, args, static_cast<int>(arg_count), feedback.slot);
  if (lowering.IsExit()) return;

  Node* node = nullptr;
  if (lowering.IsSideEffectFree()) {
    node = lowering.value();
  } else {
    DCHECK(!lowering.Changed());
    node = MakeNode(op, static_cast<int>(arg_count), args);
  }
  environment()->BindAccumulator(node, Environment::kAttachFrameState);
}

Node* const* BytecodeGraphBuilder::ProcessCallVarArgs(
    ConvertReceiverMode receiver_mode, Node* callee,
    interpreter::Register first_reg, int arg_count) {
  DCHECK_GE(arg_count, 0);
  Node* receiver_node;
  interpreter::Register first_arg;

  if (receiver_mode == ConvertReceiverMode::kNullOrUndefined) {
    // The receiver is implicit (and undefined), the arguments are in
    // consecutive registers.
    receiver_node = jsgraph()->UndefinedConstant();
    first_arg = first_reg;
  } else {
    // The receiver is the first register, followed by the arguments in the
    // consecutive registers.
    receiver_node = environment()->LookupRegister(first_reg);
    first_arg = interpreter::Register(first_reg.index() + 1);
  }

  Node* const* call_args = GetCallArgumentsFromRegisters(callee, receiver_node,
                                                         first_arg, arg_count);
  return call_args;
}

void BytecodeGraphBuilder::BuildCallVarArgs(ConvertReceiverMode receiver_mode) {
  DCHECK_EQ(interpreter::Bytecodes::GetReceiverMode(
                bytecode_iterator().current_bytecode()),
            receiver_mode);
  Node* callee =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  interpreter::Register first_reg = bytecode_iterator().GetRegisterOperand(1);
  size_t reg_count = bytecode_iterator().GetRegisterCountOperand(2);
  int const slot_id = bytecode_iterator().GetIndexOperand(3);

  int arg_count = receiver_mode == ConvertReceiverMode::kNullOrUndefined
                      ? static_cast<int>(reg_count)
                      : static_cast<int>(reg_count) - 1;
  Node* const* call_args =
      ProcessCallVarArgs(receiver_mode, callee, first_reg, arg_count);
  BuildCall(receiver_mode, call_args, JSCallNode::ArityForArgc(arg_count),
            slot_id);
}

void BytecodeGraphBuilder::VisitCallAnyReceiver() {
  BuildCallVarArgs(ConvertReceiverMode::kAny);
}

void BytecodeGraphBuilder::VisitCallProperty() {
  BuildCallVarArgs(ConvertReceiverMode::kNotNullOrUndefined);
}

void BytecodeGraphBuilder::VisitCallProperty0() {
  Node* callee =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  Node* receiver =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(1));
  int const slot_id = bytecode_iterator().GetIndexOperand(2);
  BuildCall(ConvertReceiverMode::kNotNullOrUndefined,
            {callee, receiver, feedback_vector_node()}, slot_id);
}

void BytecodeGraphBuilder::VisitCallProperty1() {
  Node* callee =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  Node* receiver =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(1));
  Node* arg0 =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(2));
  int const slot_id = bytecode_iterator().GetIndexOperand(3);
  BuildCall(ConvertReceiverMode::kNotNullOrUndefined,
            {callee, receiver, arg0, feedback_vector_node()}, slot_id);
}

void BytecodeGraphBuilder::VisitCallProperty2() {
  Node* callee =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  Node* receiver =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(1));
  Node* arg0 =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(2));
  Node* arg1 =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(3));
  int const slot_id = bytecode_iterator().GetIndexOperand(4);
  BuildCall(ConvertReceiverMode::kNotNullOrUndefined,
            {callee, receiver, arg0, arg1, feedback_vector_node()}, slot_id);
}

void BytecodeGraphBuilder::VisitCallUndefinedReceiver() {
  BuildCallVarArgs(ConvertReceiverMode::kNullOrUndefined);
}

void BytecodeGraphBuilder::VisitCallUndefinedReceiver0() {
  Node* callee =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  Node* receiver = jsgraph()->UndefinedConstant();
  int const slot_id = bytecode_iterator().GetIndexOperand(1);
  BuildCall(ConvertReceiverMode::kNullOrUndefined,
            {callee, receiver, feedback_vector_node()}, slot_id);
}

void BytecodeGraphBuilder::VisitCallUndefinedReceiver1() {
  Node* callee =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  Node* receiver = jsgraph()->UndefinedConstant();
  Node* arg0 =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(1));
  int const slot_id = bytecode_iterator().GetIndexOperand(2);
  BuildCall(ConvertReceiverMode::kNullOrUndefined,
            {callee, receiver, arg0, feedback_vector_node()}, slot_id);
}

void BytecodeGraphBuilder::VisitCallUndefinedReceiver2() {
  Node* callee =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  Node* receiver = jsgraph()->UndefinedConstant();
  Node* arg0 =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(1));
  Node* arg1 =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(2));
  int const slot_id = bytecode_iterator().GetIndexOperand(3);
  BuildCall(ConvertReceiverMode::kNullOrUndefined,
            {callee, receiver, arg0, arg1, feedback_vector_node()}, slot_id);
}

void BytecodeGraphBuilder::VisitCallWithSpread() {
  PrepareEagerCheckpoint();
  Node* callee =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  interpreter::Register receiver = bytecode_iterator().GetRegisterOperand(1);
  Node* receiver_node = environment()->LookupRegister(receiver);
  size_t reg_count = bytecode_iterator().GetRegisterCountOperand(2);
  interpreter::Register first_arg = interpreter::Register(receiver.index() + 1);
  int arg_count = static_cast<int>(reg_count) - 1;
  Node* const* args = GetCallArgumentsFromRegisters(callee, receiver_node,
                                                    first_arg, arg_count);
  int const slot_id = bytecode_iterator().GetIndexOperand(3);
  FeedbackSource feedback = CreateFeedbackSource(slot_id);
  CallFrequency frequency = ComputeCallFrequency(slot_id);
  SpeculationMode speculation_mode = GetSpeculationMode(slot_id);
  const Operator* op = javascript()->CallWithSpread(
      JSCallWithSpreadNode::ArityForArgc(arg_count), frequency, feedback,
      speculation_mode);
  DCHECK(IrOpcode::IsFeedbackCollectingOpcode(op->opcode()));

  JSTypeHintLowering::LoweringResult lowering = TryBuildSimplifiedCall(
      op, args, static_cast<int>(arg_count), feedback.slot);
  if (lowering.IsExit()) return;

  Node* node = nullptr;
  if (lowering.IsSideEffectFree()) {
    node = lowering.value();
  } else {
    DCHECK(!lowering.Changed());
    node = MakeNode(op, JSCallWithSpreadNode::ArityForArgc(arg_count), args);
  }
  environment()->BindAccumulator(node, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitCallJSRuntime() {
  PrepareEagerCheckpoint();
  Node* callee = BuildLoadNativeContextField(
      bytecode_iterator().GetNativeContextIndexOperand(0));
  interpreter::Register first_reg = bytecode_iterator().GetRegisterOperand(1);
  size_t reg_count = bytecode_iterator().GetRegisterCountOperand(2);
  int arg_count = static_cast<int>(reg_count);
  int arity = JSCallNode::ArityForArgc(arg_count);

  const Operator* call = javascript()->Call(arity);
  Node* const* call_args = ProcessCallVarArgs(
      ConvertReceiverMode::kNullOrUndefined, callee, first_reg, arg_count);
  Node* value = MakeNode(call, arity, call_args);
  environment()->BindAccumulator(value, Environment::kAttachFrameState);
}

Node* BytecodeGraphBuilder::ProcessCallRuntimeArguments(
    const Operator* call_runtime_op, interpreter::Register receiver,
    size_t reg_count) {
  int arg_count = static_cast<int>(reg_count);
  // arity is args.
  int arity = arg_count;
  Node** all = local_zone()->AllocateArray<Node*>(static_cast<size_t>(arity));
  int first_arg_index = receiver.index();
  for (int i = 0; i < static_cast<int>(reg_count); ++i) {
    all[i] = environment()->LookupRegister(
        interpreter::Register(first_arg_index + i));
  }
  Node* value = MakeNode(call_runtime_op, arity, all);
  return value;
}

void BytecodeGraphBuilder::VisitCallRuntime() {
  PrepareEagerCheckpoint();
  Runtime::FunctionId function_id = bytecode_iterator().GetRuntimeIdOperand(0);
  interpreter::Register receiver = bytecode_iterator().GetRegisterOperand(1);
  size_t reg_count = bytecode_iterator().GetRegisterCountOperand(2);

  // Handle %ObserveNode here (rather than in JSIntrinsicLowering) to observe
  // the node as early as possible.
  if (function_id == Runtime::FunctionId::kObserveNode) {
    DCHECK_EQ(1, reg_count);
    Node* value = environment()->LookupRegister(receiver);
    observe_node_info_.StartObserving(value);
    environment()->BindAccumulator(value);
  } else {
    // Create node to perform the runtime call.
    const Operator* call = javascript()->CallRuntime(function_id, reg_count);
    Node* value = ProcessCallRuntimeArguments(call, receiver, reg_count);
    environment()->BindAccumulator(value, Environment::kAttachFrameState);

    // Connect to the end if {function_id} is non-returning.
    if (Runtime::IsNonReturning(function_id)) {
      // TODO(7099): Investigate if we need LoopExit node here.
      Node* control = NewNode(common()->Throw());
      MergeControlToLeaveFunction(control);
    }
  }
}

void BytecodeGraphBuilder::VisitCallRuntimeForPair() {
  PrepareEagerCheckpoint();
  Runtime::FunctionId functionId = bytecode_iterator().GetRuntimeIdOperand(0);
  interpreter::Register receiver = bytecode_iterator().GetRegisterOperand(1);
  size_t reg_count = bytecode_iterator().GetRegisterCountOperand(2);
  interpreter::Register first_return =
      bytecode_iterator().GetRegisterOperand(3);

  // Create node to perform the runtime call.
  const Operator* call = javascript()->CallRuntime(functionId, reg_count);
  Node* return_pair = ProcessCallRuntimeArguments(call, receiver, reg_count);
  environment()->BindRegistersToProjections(first_return, return_pair,
                                            Environment::kAttachFrameState);
}

Node* const* BytecodeGraphBuilder::GetConstructArgumentsFromRegister(
    Node* target, Node* new_target, interpreter::Register first_arg,
    int arg_count) {
  const int arity = JSConstructNode::ArityForArgc(arg_count);
  Node** all = local_zone()->AllocateArray<Node*>(static_cast<size_t>(arity));
  int cursor = 0;

  static_assert(JSConstructNode::TargetIndex() == 0);
  static_assert(JSConstructNode::NewTargetIndex() == 1);
  static_assert(JSConstructNode::FirstArgumentIndex() == 2);
  static_assert(JSConstructNode::kFeedbackVectorIsLastInput);

  all[cursor++] = target;
  all[cursor++] = new_target;

  // The function arguments are in consecutive registers.
  int arg_base = first_arg.index();
  for (int i = 0; i < arg_count; ++i) {
    all[cursor++] =
        environment()->LookupRegister(interpreter::Register(arg_base + i));
  }

  all[cursor++] = feedback_vector_node();

  DCHECK_EQ(cursor, arity);
  return all;
}

void BytecodeGraphBuilder::VisitConstruct() {
  PrepareEagerCheckpoint();
  interpreter::Register callee_reg = bytecode_iterator().GetRegisterOperand(0);
  interpreter::Register first_reg = bytecode_iterator().GetRegisterOperand(1);
  size_t reg_count = bytecode_iterator().GetRegisterCountOperand(2);
  int const slot_id = bytecode_iterator().GetIndexOperand(3);
  FeedbackSource feedback = CreateFeedbackSource(slot_id);

  Node* new_target = environment()->LookupAccumulator();
  Node* callee = environment()->LookupRegister(callee_reg);

  CallFrequency frequency = ComputeCallFrequency(slot_id);
  const uint32_t arg_count = static_cast<uint32_t>(reg_count);
  const uint32_t arity = JSConstructNode::ArityForArgc(arg_count);
  const Operator* op = javascript()->Construct(arity, frequency, feedback);
  DCHECK(IrOpcode::IsFeedbackCollectingOpcode(op->opcode()));
  Node* const* args = GetConstructArgumentsFromRegister(callee, new_target,
                                                        first_reg, arg_count);
  JSTypeHintLowering::LoweringResult lowering = TryBuildSimplifiedConstruct(
      op, args, static_cast<int>(arg_count), feedback.slot);
  if (lowering.IsExit()) return;

  Node* node = nullptr;
  if (lowering.IsSideEffectFree()) {
    node = lowering.value();
  } else {
    DCHECK(!lowering.Changed());
    node = MakeNode(op, arity, args);
  }
  environment()->BindAccumulator(node, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitConstructWithSpread() {
  PrepareEagerCheckpoint();
  interpreter::Register callee_reg = bytecode_iterator().GetRegisterOperand(0);
  interpreter::Register first_reg = bytecode_iterator().GetRegisterOperand(1);
  size_t reg_count = bytecode_iterator().GetRegisterCountOperand(2);
  int const slot_id = bytecode_iterator().GetIndexOperand(3);
  FeedbackSource feedback = CreateFeedbackSource(slot_id);

  Node* new_target = environment()->LookupAccumulator();
  Node* callee = environment()->LookupRegister(callee_reg);

  CallFrequency frequency = ComputeCallFrequency(slot_id);
  const uint32_t arg_count = static_cast<uint32_t>(reg_count);
  const uint32_t arity = JSConstructNode::ArityForArgc(arg_count);
  const Operator* op =
      javascript()->ConstructWithSpread(arity, frequency, feedback);
  DCHECK(IrOpcode::IsFeedbackCollectingOpcode(op->opcode()));
  Node* const* args = GetConstructArgumentsFromRegister(callee, new_target,
                                                        first_reg, arg_count);
  JSTypeHintLowering::LoweringResult lowering = TryBuildSimplifiedConstruct(
      op, args, static_cast<int>(arg_count), feedback.slot);
  if (lowering.IsExit()) return;

  Node* node = nullptr;
  if (lowering.IsSideEffectFree()) {
    node = lowering.value();
  } else {
    DCHECK(!lowering.Changed());
    node = MakeNode(op, arity, args);
  }
  environment()->BindAccumulator(node, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitConstructForwardAllArgs() {
  PrepareEagerCheckpoint();
  interpreter::Register callee_reg = bytecode_iterator().GetRegisterOperand(0);
  int const slot_id = bytecode_iterator().GetIndexOperand(1);
  FeedbackSource feedback = CreateFeedbackSource(slot_id);
  Node* new_target = environment()->LookupAccumulator();
  Node* callee = environment()->LookupRegister(callee_reg);
  CallFrequency frequency = ComputeCallFrequency(slot_id);

  // Use 0 as a fake argument count.
  //
  // This op will be later reduced to either a builtin call (in the case of not
  // being inlined) or a normal JSConstruct with the inlined arguments
  // forwarded.
  constexpr int arg_count = 0;
  const int arity = JSConstructForwardAllArgsNode::ArityForArgc(arg_count);
  Node** construct_args =
      local_zone()->AllocateArray<Node*>(static_cast<size_t>(arity));
  static_assert(JSConstructForwardAllArgsNode::TargetIndex() == 0);
  static_assert(JSConstructForwardAllArgsNode::NewTargetIndex() == 1);
  static_assert(JSConstructNode::kFeedbackVectorIsLastInput);
  DCHECK_LT(JSConstructForwardAllArgsNode::NewTargetIndex(), arity);
  int cursor = 0;
  construct_args[cursor++] = callee;
  construct_args[cursor++] = new_target;
  construct_args[cursor++] = feedback_vector_node();

  const Operator* op =
      javascript()->ConstructForwardAllArgs(frequency, feedback);
  JSTypeHintLowering::LoweringResult lowering =
      TryBuildSimplifiedConstruct(op, construct_args, arg_count, feedback.slot);
  if (lowering.IsExit()) return;

  Node* node;
  if (lowering.IsSideEffectFree()) {
    node = lowering.value();
  } else {
    DCHECK(!lowering.Changed());
    node = MakeNode(op, arity, construct_args);
  }

  environment()->BindAccumulator(node, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitInvokeIntrinsic() {
  PrepareEagerCheckpoint();
  Runtime::FunctionId functionId = bytecode_iterator().GetIntrinsicIdOperand(0);
  interpreter::Register receiver = bytecode_iterator().GetRegisterOperand(1);
  size_t reg_count = bytecode_iterator().GetRegisterCountOperand(2);

  // Create node to perform the runtime call. Turbofan will take care of the
  // lowering.
  const Operator* call = javascript()->CallRuntime(functionId, reg_count);
  Node* value = ProcessCallRuntimeArguments(call, receiver, reg_count);
  environment()->BindAccumulator(value, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitThrow() {
  BuildLoopExitsForFunctionExit(bytecode_analysis().GetInLivenessFor(
      bytecode_iterator().current_offset()));
  Node* value = environment()->LookupAccumulator();
  Node* call = NewNode(javascript()->CallRuntime(Runtime::kThrow), value);
  environment()->BindAccumulator(call, Environment::kAttachFrameState);
  Node* control = NewNode(common()->Throw());
  MergeControlToLeaveFunction(control);
}

void BytecodeGraphBuilder::VisitAbort() {
  BuildLoopExitsForFunctionExit(bytecode_analysis().GetInLivenessFor(
      bytecode_iterator().current_offset()));
  AbortReason reason =
      static_cast<AbortReason>(bytecode_iterator().GetIndexOperand(0));
  NewNode(simplified()->RuntimeAbort(reason));
  Node* control = NewNode(common()->Throw());
  MergeControlToLeaveFunction(control);
}

void BytecodeGraphBuilder::VisitReThrow() {
  BuildLoopExitsForFunctionExit(bytecode_analysis().GetInLivenessFor(
      bytecode_iterator().current_offset()));
  Node* value = environment()->LookupAccumulator();
  NewNode(javascript()->CallRuntime(Runtime::kReThrow), value);
  Node* control = NewNode(common()->Throw());
  MergeControlToLeaveFunction(control);
}

void BytecodeGraphBuilder::BuildHoleCheckAndThrow(
    Node* condition, Runtime::FunctionId runtime_id, Node* name) {
  Node* accumulator = environment()->LookupAccumulator();
  NewBranch(condition, BranchHint::kFalse);
  {
    SubEnvironment sub_environment(this);

    NewIfTrue();
    BuildLoopExitsForFunctionExit(bytecode_analysis().GetInLivenessFor(
        bytecode_iterator().current_offset()));
    Node* node;
    const Operator* op = javascript()->CallRuntime(runtime_id);
    if (runtime_id == Runtime::kThrowAccessedUninitializedVariable) {
      DCHECK_NOT_NULL(name);
      node = NewNode(op, name);
    } else {
      DCHECK(runtime_id == Runtime::kThrowSuperAlreadyCalledError ||
             runtime_id == Runtime::kThrowSuperNotCalled);
      node = NewNode(op);
    }
    environment()->RecordAfterState(node, Environment::kAttachFrameState);
    Node* control = NewNode(common()->Throw());
    MergeControlToLeaveFunction(control);
  }
  NewIfFalse();
  environment()->BindAccumulator(accumulator);
}

void BytecodeGraphBuilder::VisitThrowReferenceErrorIfHole() {
  Node* accumulator = environment()->LookupAccumulator();
  Node* check_for_hole = NewNode(simplified()->ReferenceEqual(), accumulator,
                                 jsgraph()->TheHoleConstant());
  Node* name =
      jsgraph()->ConstantNoHole(MakeRefForConstantForIndexOperand(0), broker());
  BuildHoleCheckAndThrow(check_for_hole,
                         Runtime::kThrowAccessedUninitializedVariable, name);
}

void BytecodeGraphBuilder::VisitThrowSuperNotCalledIfHole() {
  Node* accumulator = environment()->LookupAccumulator();
  Node* check_for_hole = NewNode(simplified()->ReferenceEqual(), accumulator,
                                 jsgraph()->TheHoleConstant());
  BuildHoleCheckAndThrow(check_for_hole, Runtime::kThrowSuperNotCalled);
}

void BytecodeGraphBuilder::VisitThrowSuperAlreadyCalledIfNotHole() {
  Node* accumulator = environment()->LookupAccumulator();
  Node* check_for_hole = NewNode(simplified()->ReferenceEqual(), accumulator,
                                 jsgraph()->TheHoleConstant());
  Node* check_for_not_hole =
      NewNode(simplified()->BooleanNot(), check_for_hole);
  BuildHoleCheckAndThrow(check_for_not_hole,
                         Runtime::kThrowSuperAlreadyCalledError);
}

void BytecodeGraphBuilder::VisitThrowIfNotSuperConstructor() {
  Node* constructor =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  Node* check_is_constructor =
      NewNode(simplified()->ObjectIsConstructor(), constructor);
  NewBranch(check_is_constructor, BranchHint::kTrue);
  {
    SubEnvironment sub_environment(this);
    NewIfFalse();
    BuildLoopExitsForFunctionExit(bytecode_analysis().GetInLivenessFor(
        bytecode_iterator().current_offset()));
    Node* node =
        NewNode(javascript()->CallRuntime(Runtime::kThrowNotSuperConstructor),
                constructor, GetFunctionClosure());
    environment()->RecordAfterState(node, Environment::kAttachFrameState);
    Node* control = NewNode(common()->Throw());
    MergeControlToLeaveFunction(control);
  }
  NewIfTrue();

  constructor = NewNode(common()->TypeGuard(Type::Callable()), constructor);
  environment()->BindRegister(bytecode_iterator().GetRegisterOperand(0),
                              constructor);
}

void BytecodeGraphBuilder::BuildUnaryOp(const Operator* op) {
  DCHECK(JSOperator::IsUnaryWithFeedback(op->opcode()));
  PrepareEagerCheckpoint();
  Node* operand = environment()->LookupAccumulator();

  FeedbackSlot slot =
      bytecode_iterator().GetSlotOperand(kUnaryOperationHintIndex);
  JSTypeHintLowering::LoweringResult lowering =
      TryBuildSimplifiedUnaryOp(op, operand, slot);
  if (lowering.IsExit()) return;

  Node* node = nullptr;
  if (lowering.IsSideEffectFree()) {
    node = lowering.value();
  } else {
    DCHECK(!lowering.Changed());
    DCHECK(IrOpcode::IsFeedbackCollectingOpcode(op->opcode()));
    node = NewNode(op, operand, feedback_vector_node());
  }

  environment()->BindAccumulator(node, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::BuildBinaryOp(const Operator* op) {
  DCHECK(JSOperator::IsBinaryWithFeedback(op->opcode()));
  PrepareEagerCheckpoint();
  Node* left =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  Node* right = environment()->LookupAccumulator();

  FeedbackSlot slot =
      bytecode_iterator().GetSlotOperand(kBinaryOperationHintIndex);
  JSTypeHintLowering::LoweringResult lowering =
      TryBuildSimplifiedBinaryOp(op, left, right, slot);
  if (lowering.IsExit()) return;

  Node* node = nullptr;
  if (lowering.IsSideEffectFree()) {
    node = lowering.value();
  } else {
    DCHECK(!lowering.Changed());
    DCHECK(IrOpcode::IsFeedbackCollectingOpcode(op->opcode()));
    node = NewNode(op, left, right, feedback_vector_node());
  }

  environment()->BindAccumulator(node, Environment::kAttachFrameState);
}

// Helper function to create for-in mode from the recorded type feedback.
ForInMode BytecodeGraphBuilder::GetForInMode(FeedbackSlot slot) {
  FeedbackSource source(feedback_vector(), slot);
  switch (broker()->GetFeedbackForForIn(source)) {
    case ForInHint::kNone:
    case ForInHint::kEnumCacheKeysAndIndices:
      return ForInMode::kUseEnumCacheKeysAndIndices;
    case ForInHint::kEnumCacheKeys:
      return ForInMode::kUseEnumCacheKeys;
    case ForInHint::kAny:
      return ForInMode::kGeneric;
  }
  UNREACHABLE();
}

CallFrequency BytecodeGraphBuilder::ComputeCallFrequency(int slot_id) const {
  if (invocation_frequency_.IsUnknown()) return CallFrequency();
  FeedbackSlot slot = FeedbackVector::ToSlot(slot_id);
  FeedbackSource source(feedback_vector(), slot);
  ProcessedFeedback const& feedback = broker()->GetFeedbackForCall(source);
  float feedback_frequency =
      feedback.IsInsufficient() ? 0.0f : feedback.AsCall().frequency();
  if (feedback_frequency == 0.0f) {  // Prevent multiplying zero and infinity.
    return CallFrequency(0.0f);
  } else {
    return CallFrequency(feedback_frequency * invocation_frequency_.value());
  }
}

SpeculationMode BytecodeGraphBuilder::GetSpeculationMode(int slot_id) const {
  FeedbackSlot slot = FeedbackVector::ToSlot(slot_id);
  FeedbackSource source(feedback_vector(), slot);
  ProcessedFeedback const& feedback = broker()->GetFeedbackForCall(source);
  return feedback.IsInsufficient() ? SpeculationMode::kDisallowSpeculation
                                   : feedback.AsCall().speculation_mode();
}

CallFeedbackRelation BytecodeGraphBuilder::ComputeCallFeedbackRelation(
    int slot_id) const {
  FeedbackSlot slot = FeedbackVector::ToSlot(slot_id);
  FeedbackSource source(feedback_vector(), slot);
  ProcessedFeedback const& feedback = broker()->GetFeedbackForCall(source);
  if (feedback.IsInsufficient()) return CallFeedbackRelation::kUnrelated;
  CallFeedbackContent call_feedback_content =
      feedback.AsCall().call_feedback_content();
  return call_feedback_content == CallFeedbackContent::kTarget
             ? CallFeedbackRelation::kTarget
             : CallFeedbackRelation::kReceiver;
}

void BytecodeGraphBuilder::VisitBitwiseNot() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kUnaryOperationHintIndex));
  BuildUnaryOp(javascript()->BitwiseNot(feedback));
}

void BytecodeGraphBuilder::VisitDec() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kUnaryOperationHintIndex));
  BuildUnaryOp(javascript()->Decrement(feedback));
}

void BytecodeGraphBuilder::VisitInc() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kUnaryOperationHintIndex));
  BuildUnaryOp(javascript()->Increment(feedback));
}

void BytecodeGraphBuilder::VisitNegate() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kUnaryOperationHintIndex));
  BuildUnaryOp(javascript()->Negate(feedback));
}

void BytecodeGraphBuilder::VisitAdd() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kBinaryOperationHintIndex));
  BuildBinaryOp(javascript()->Add(feedback));
}

void BytecodeGraphBuilder::VisitSub() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kBinaryOperationHintIndex));
  BuildBinaryOp(javascript()->Subtract(feedback));
}

void BytecodeGraphBuilder::VisitMul() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kBinaryOperationHintIndex));
  BuildBinaryOp(javascript()->Multiply(feedback));
}

void BytecodeGraphBuilder::VisitDiv() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kBinaryOperationHintIndex));
  BuildBinaryOp(javascript()->Divide(feedback));
}

void BytecodeGraphBuilder::VisitMod() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kBinaryOperationHintIndex));
  BuildBinaryOp(javascript()->Modulus(feedback));
}

void BytecodeGraphBuilder::VisitExp() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kBinaryOperationHintIndex));
  BuildBinaryOp(javascript()->Exponentiate(feedback));
}

void BytecodeGraphBuilder::VisitBitwiseOr() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kBinaryOperationHintIndex));
  BuildBinaryOp(javascript()->BitwiseOr(feedback));
}

void BytecodeGraphBuilder::VisitBitwiseXor() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kBinaryOperationHintIndex));
  BuildBinaryOp(javascript()->BitwiseXor(feedback));
}

void BytecodeGraphBuilder::VisitBitwiseAnd() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kBinaryOperationHintIndex));
  BuildBinaryOp(javascript()->BitwiseAnd(feedback));
}

void BytecodeGraphBuilder::VisitShiftLeft() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kBinaryOperationHintIndex));
  BuildBinaryOp(javascript()->ShiftLeft(feedback));
}

void BytecodeGraphBuilder::VisitShiftRight() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kBinaryOperationHintIndex));
  BuildBinaryOp(javascript()->ShiftRight(feedback));
}

void BytecodeGraphBuilder::VisitShiftRightLogical() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kBinaryOperationHintIndex));
  BuildBinaryOp(javascript()->ShiftRightLogical(feedback));
}

void BytecodeGraphBuilder::BuildBinaryOpWithImmediate(const Operator* op) {
  DCHECK(JSOperator::IsBinaryWithFeedback(op->opcode()));
  PrepareEagerCheckpoint();
  Node* left = environment()->LookupAccumulator();
  Node* right =
      jsgraph()->ConstantNoHole(bytecode_iterator().GetImmediateOperand(0));

  FeedbackSlot slot =
      bytecode_iterator().GetSlotOperand(kBinaryOperationSmiHintIndex);
  JSTypeHintLowering::LoweringResult lowering =
      TryBuildSimplifiedBinaryOp(op, left, right, slot);
  if (lowering.IsExit()) return;

  Node* node = nullptr;
  if (lowering.IsSideEffectFree()) {
    node = lowering.value();
  } else {
    DCHECK(!lowering.Changed());
    DCHECK(IrOpcode::IsFeedbackCollectingOpcode(op->opcode()));
    node = NewNode(op, left, right, feedback_vector_node());
  }
  environment()->BindAccumulator(node, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitAddSmi() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kBinaryOperationSmiHintIndex));
  BuildBinaryOpWithImmediate(javascript()->Add(feedback));
}

void BytecodeGraphBuilder::VisitSubSmi() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kBinaryOperationSmiHintIndex));
  BuildBinaryOpWithImmediate(javascript()->Subtract(feedback));
}

void BytecodeGraphBuilder::VisitMulSmi() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kBinaryOperationSmiHintIndex));
  BuildBinaryOpWithImmediate(javascript()->Multiply(feedback));
}

void BytecodeGraphBuilder::VisitDivSmi() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kBinaryOperationSmiHintIndex));
  BuildBinaryOpWithImmediate(javascript()->Divide(feedback));
}

void BytecodeGraphBuilder::VisitModSmi() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kBinaryOperationSmiHintIndex));
  BuildBinaryOpWithImmediate(javascript()->Modulus(feedback));
}

void BytecodeGraphBuilder::VisitExpSmi() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kBinaryOperationSmiHintIndex));
  BuildBinaryOpWithImmediate(javascript()->Exponentiate(feedback));
}

void BytecodeGraphBuilder::VisitBitwiseOrSmi() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kBinaryOperationSmiHintIndex));
  BuildBinaryOpWithImmediate(javascript()->BitwiseOr(feedback));
}

void BytecodeGraphBuilder::VisitBitwiseXorSmi() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kBinaryOperationSmiHintIndex));
  BuildBinaryOpWithImmediate(javascript()->BitwiseXor(feedback));
}

void BytecodeGraphBuilder::VisitBitwiseAndSmi() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kBinaryOperationSmiHintIndex));
  BuildBinaryOpWithImmediate(javascript()->BitwiseAnd(feedback));
}

void BytecodeGraphBuilder::VisitShiftLeftSmi() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kBinaryOperationSmiHintIndex));
  BuildBinaryOpWithImmediate(javascript()->ShiftLeft(feedback));
}

void BytecodeGraphBuilder::VisitShiftRightSmi() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kBinaryOperationSmiHintIndex));
  BuildBinaryOpWithImmediate(javascript()->ShiftRight(feedback));
}

void BytecodeGraphBuilder::VisitShiftRightLogicalSmi() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kBinaryOperationSmiHintIndex));
  BuildBinaryOpWithImmediate(javascript()->ShiftRightLogical(feedback));
}

void BytecodeGraphBuilder::VisitLogicalNot() {
  Node* value = environment()->LookupAccumulator();
  Node* node = NewNode(simplified()->BooleanNot(), value);
  environment()->BindAccumulator(node);
}

void BytecodeGraphBuilder::VisitToBooleanLogicalNot() {
  Node* value =
      NewNode(simplified()->ToBoolean(), environment()->LookupAccumulator());
  Node* node = NewNode(simplified()->BooleanNot(), value);
  environment()->BindAccumulator(node);
}

void BytecodeGraphBuilder::VisitTypeOf() {
  PrepareEagerCheckpoint();
  Node* operand = environment()->LookupAccumulator();

  FeedbackSlot slot = bytecode_iterator().GetSlotOperand(0);
  JSTypeHintLowering::LoweringResult lowering =
      TryBuildSimplifiedUnaryOp(simplified()->TypeOf(), operand, slot);
  if (lowering.IsExit()) return;

  Node* node = nullptr;
  if (lowering.IsSideEffectFree()) {
    node = lowering.value();
  } else {
    DCHECK(!lowering.Changed());
    node = NewNode(simplified()->TypeOf(), environment()->LookupAccumulator());
  }

  environment()->BindAccumulator(node);
}

void BytecodeGraphBuilder::BuildDelete(LanguageMode language_mode) {
  PrepareEagerCheckpoint();
  Node* key = environment()->LookupAccumulator();
  Node* object =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  Node* mode = jsgraph()->ConstantNoHole(static_cast<int32_t>(language_mode));
  Node* node = NewNode(javascript()->DeleteProperty(), object, key, mode);
  environment()->BindAccumulator(node, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitDeletePropertyStrict() {
  BuildDelete(LanguageMode::kStrict);
}

void BytecodeGraphBuilder::VisitDeletePropertySloppy() {
  BuildDelete(LanguageMode::kSloppy);
}

void BytecodeGraphBuilder::VisitGetSuperConstructor() {
  Node* node = NewNode(javascript()->GetSuperConstructor(),
                       environment()->LookupAccumulator());
  environment()->BindRegister(bytecode_iterator().GetRegisterOperand(0), node,
                              Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitFindNonDefaultConstructorOrConstruct() {
  Node* this_function =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  Node* new_target =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(1));

  Node* node = NewNode(javascript()->FindNonDefaultConstructorOrConstruct(),
                       this_function, new_target);

  // The outputs of the JSFindNonDefaultConstructor node are [boolean_thing,
  // object_thing]. In some cases we reduce it to JSCreate, which has only one
  // output, [object_thing], and we also fix the poke location in that case.
  // Here we hard-wire the FrameState for [boolean_thing] to be `true`, which is
  // the correct value in the case where JSFindNonDefaultConstructor is reduced
  // to JSCreate.
  environment()->BindRegister(bytecode_iterator().GetRegisterOperand(2),
                              jsgraph()->TrueConstant());
  environment()->BindRegistersToProjections(
      bytecode_iterator().GetRegisterOperand(2), node,
      Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::BuildCompareOp(const Operator* op) {
  DCHECK(JSOperator::IsBinaryWithFeedback(op->opcode()));
  PrepareEagerCheckpoint();
  Node* left =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  Node* right = environment()->LookupAccumulator();

  FeedbackSlot slot = bytecode_iterator().GetSlotOperand(1);
  JSTypeHintLowering::LoweringResult lowering =
      TryBuildSimplifiedBinaryOp(op, left, right, slot);
  if (lowering.IsExit()) return;

  Node* node = nullptr;
  if (lowering.IsSideEffectFree()) {
    node = lowering.value();
  } else {
    DCHECK(!lowering.Changed());
    DCHECK(IrOpcode::IsFeedbackCollectingOpcode(op->opcode()));
    node = NewNode(op, left, right, feedback_vector_node());
  }
  environment()->BindAccumulator(node, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitTestEqual() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kCompareOperationHintIndex));
  BuildCompareOp(javascript()->Equal(feedback));
}

void BytecodeGraphBuilder::VisitTestEqualStrict() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kCompareOperationHintIndex));
  BuildCompareOp(javascript()->StrictEqual(feedback));
}

void BytecodeGraphBuilder::VisitTestLessThan() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kCompareOperationHintIndex));
  BuildCompareOp(javascript()->LessThan(feedback));
}

void BytecodeGraphBuilder::VisitTestGreaterThan() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kCompareOperationHintIndex));
  BuildCompareOp(javascript()->GreaterThan(feedback));
}

void BytecodeGraphBuilder::VisitTestLessThanOrEqual() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kCompareOperationHintIndex));
  BuildCompareOp(javascript()->LessThanOrEqual(feedback));
}

void BytecodeGraphBuilder::VisitTestGreaterThanOrEqual() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kCompareOperationHintIndex));
  BuildCompareOp(javascript()->GreaterThanOrEqual(feedback));
}

void BytecodeGraphBuilder::VisitTestReferenceEqual() {
  Node* left =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  Node* right = environment()->LookupAccumulator();
  Node* result = NewNode(simplified()->ReferenceEqual(), left, right);
  environment()->BindAccumulator(result);
}

void BytecodeGraphBuilder::VisitTestIn() {
  PrepareEagerCheckpoint();
  Node* object = environment()->LookupAccumulator();
  Node* key =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  FeedbackSource feedback =
      CreateFeedbackSource(bytecode_iterator().GetIndexOperand(1));
  static_assert(JSHasPropertyNode::ObjectIndex() == 0);
  static_assert(JSHasPropertyNode::KeyIndex() == 1);
  static_assert(JSHasPropertyNode::FeedbackVectorIndex() == 2);
  const Operator* op = javascript()->HasProperty(feedback);
  DCHECK(IrOpcode::IsFeedbackCollectingOpcode(op->opcode()));
  Node* node = NewNode(op, object, key, feedback_vector_node());
  environment()->BindAccumulator(node, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitTestInstanceOf() {
  FeedbackSource feedback = CreateFeedbackSource(
      bytecode_iterator().GetSlotOperand(kCompareOperationHintIndex));
  BuildCompareOp(javascript()->InstanceOf(feedback));
}

void BytecodeGraphBuilder::VisitTestUndetectable() {
  Node* object = environment()->LookupAccumulator();
  Node* node = NewNode(jsgraph()->simplified()->ObjectIsUndetectable(), object);
  environment()->BindAccumulator(node);
}

void BytecodeGraphBuilder::VisitTestNull() {
  Node* object = environment()->LookupAccumulator();
  Node* result = NewNode(simplified()->ReferenceEqual(), object,
                         jsgraph()->NullConstant());
  environment()->BindAccumulator(result);
}

void BytecodeGraphBuilder::VisitTestUndefined() {
  Node* object = environment()->LookupAccumulator();
  Node* result = NewNode(simplified()->ReferenceEqual(), object,
                         jsgraph()->UndefinedConstant());
  environment()->BindAccumulator(result);
}

void BytecodeGraphBuilder::VisitTestTypeOf() {
  Node* object = environment()->LookupAccumulator();
  auto literal_flag = interpreter::TestTypeOfFlags::Decode(
      bytecode_iterator().GetFlag8Operand(0));
  Node* result;
  switch (literal_flag) {
    case interpreter::TestTypeOfFlags::LiteralFlag::kNumber:
      result = NewNode(simplified()->ObjectIsNumber(), object);
      break;
    case interpreter::TestTypeOfFlags::LiteralFlag::kString:
      result = NewNode(simplified()->ObjectIsString(), object);
      break;
    case interpreter::TestTypeOfFlags::LiteralFlag::kSymbol:
      result = NewNode(simplified()->ObjectIsSymbol(), object);
      break;
    case interpreter::TestTypeOfFlags::LiteralFlag::kBigInt:
      result = NewNode(simplified()->ObjectIsBigInt(), object);
      break;
    case interpreter::TestTypeOfFlags::LiteralFlag::kBoolean:
      result = NewNode(common()->Select(MachineRepresentation::kTagged),
                       NewNode(simplified()->ReferenceEqual(), object,
                               jsgraph()->TrueConstant()),
                       jsgraph()->TrueConstant(),
                       NewNode(simplified()->ReferenceEqual(), object,
                               jsgraph()->FalseConstant()));
      break;
    case interpreter::TestTypeOfFlags::LiteralFlag::kUndefined:
      result = graph()->NewNode(
          common()->Select(MachineRepresentation::kTagged),
          graph()->NewNode(simplified()->ReferenceEqual(), object,
                           jsgraph()->NullConstant()),
          jsgraph()->FalseConstant(),
          graph()->NewNode(simplified()->ObjectIsUndetectable(), object));
      break;
    case interpreter::TestTypeOfFlags::LiteralFlag::kFunction:
      result =
          graph()->NewNode(simplified()->ObjectIsDetectableCallable(), object);
      break;
    case interpreter::TestTypeOfFlags::LiteralFlag::kObject:
      result = graph()->NewNode(
          common()->Select(MachineRepresentation::kTagged),
          graph()->NewNode(simplified()->ObjectIsNonCallable(), object),
          jsgraph()->TrueConstant(),
          graph()->NewNode(simplified()->ReferenceEqual(), object,
                           jsgraph()->NullConstant()));
      break;
    case interpreter::TestTypeOfFlags::LiteralFlag::kOther:
      UNREACHABLE();  // Should never be emitted.
  }
  environment()->BindAccumulator(result);
}

void BytecodeGraphBuilder::BuildCastOperator(const Operator* js_op) {
  Node* value = NewNode(js_op, environment()->LookupAccumulator());
  environment()->BindRegister(bytecode_iterator().GetRegisterOperand(0), value,
                              Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitToName() {
  Node* value =
      NewNode(javascript()->ToName(), environment()->LookupAccumulator());
  environment()->BindAccumulator(value, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitToObject() {
  BuildCastOperator(javascript()->ToObject());
}

void BytecodeGraphBuilder::VisitToString() {
  Node* value =
      NewNode(javascript()->ToString(), environment()->LookupAccumulator());
  environment()->BindAccumulator(value, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitToBoolean() {
  Node* value =
      NewNode(simplified()->ToBoolean(), environment()->LookupAccumulator());
  environment()->BindAccumulator(value, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitToNumber() {
  PrepareEagerCheckpoint();
  Node* object = environment()->LookupAccumulator();

  FeedbackSlot slot = bytecode_iterator().GetSlotOperand(0);
  JSTypeHintLowering::LoweringResult lowering =
      TryBuildSimplifiedToNumber(object, slot);

  Node* node = nullptr;
  if (lowering.IsSideEffectFree()) {
    node = lowering.value();
  } else {
    DCHECK(!lowering.Changed());
    node = NewNode(javascript()->ToNumber(), object);
  }

  environment()->BindAccumulator(node, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitToNumeric() {
  PrepareEagerCheckpoint();
  Node* object = environment()->LookupAccumulator();

  // If we have some kind of Number feedback, we do the same lowering as for
  // ToNumber.
  FeedbackSlot slot = bytecode_iterator().GetSlotOperand(0);
  JSTypeHintLowering::LoweringResult lowering =
      TryBuildSimplifiedToNumber(object, slot);

  Node* node = nullptr;
  if (lowering.IsSideEffectFree()) {
    node = lowering.value();
  } else {
    DCHECK(!lowering.Changed());
    node = NewNode(javascript()->ToNumeric(), object);
  }

  environment()->BindAccumulator(node, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitJump() { BuildJump(); }

void BytecodeGraphBuilder::VisitJumpConstant() { BuildJump(); }

void BytecodeGraphBuilder::VisitJumpIfTrue() { BuildJumpIfTrue(); }

void BytecodeGraphBuilder::VisitJumpIfTrueConstant() { BuildJumpIfTrue(); }

void BytecodeGraphBuilder::VisitJumpIfFalse() { BuildJumpIfFalse(); }

void BytecodeGraphBuilder::VisitJumpIfFalseConstant() { BuildJumpIfFalse(); }

void BytecodeGraphBuilder::VisitJumpIfToBooleanTrue() {
  BuildJumpIfToBooleanTrue();
}

void BytecodeGraphBuilder::VisitJumpIfToBooleanTrueConstant() {
  BuildJumpIfToBooleanTrue();
}

void BytecodeGraphBuilder::VisitJumpIfToBooleanFalse() {
  BuildJumpIfToBooleanFalse();
}

void BytecodeGraphBuilder::VisitJumpIfToBooleanFalseConstant() {
  BuildJumpIfToBooleanFalse();
}

void BytecodeGraphBuilder::VisitJumpIfJSReceiver() { BuildJumpIfJSReceiver(); }

void BytecodeGraphBuilder::VisitJumpIfJSReceiverConstant() {
  BuildJumpIfJSReceiver();
}

void BytecodeGraphBuilder::VisitJumpIfForInDone() { BuildJumpIfForInDone(); }

void BytecodeGraphBuilder::VisitJumpIfForInDoneConstant() {
  BuildJumpIfForInDone();
}

void BytecodeGraphBuilder::VisitJumpIfNull() {
  BuildJumpIfEqual(jsgraph()->NullConstant());
}

void BytecodeGraphBuilder::VisitJumpIfNullConstant() {
  BuildJumpIfEqual(jsgraph()->NullConstant());
}

void BytecodeGraphBuilder::VisitJumpIfNotNull() {
  BuildJumpIfNotEqual(jsgraph()->NullConstant());
}

void BytecodeGraphBuilder::VisitJumpIfNotNullConstant() {
  BuildJumpIfNotEqual(jsgraph()->NullConstant());
}

void BytecodeGraphBuilder::VisitJumpIfUndefined() {
  BuildJumpIfEqual(jsgraph()->UndefinedConstant());
}

void BytecodeGraphBuilder::VisitJumpIfUndefinedConstant() {
  BuildJumpIfEqual(jsgraph()->UndefinedConstant());
}

void BytecodeGraphBuilder::VisitJumpIfNotUndefined() {
  BuildJumpIfNotEqual(jsgraph()->UndefinedConstant());
}

void BytecodeGraphBuilder::VisitJumpIfNotUndefinedConstant() {
  BuildJumpIfNotEqual(jsgraph()->UndefinedConstant());
}

void BytecodeGraphBuilder::VisitJumpIfUndefinedOrNull() {
  BuildJumpIfEqual(jsgraph()->UndefinedConstant());
  BuildJumpIfEqual(jsgraph()->NullConstant());
}

void BytecodeGraphBuilder::VisitJumpIfUndefinedOrNullConstant() {
  BuildJumpIfEqual(jsgraph()->UndefinedConstant());
  BuildJumpIfEqual(jsgraph()->NullConstant());
}

void BytecodeGraphBuilder::VisitJumpLoop() {
  BuildIterationBodyStackCheck();
  BuildJump();
}

void BytecodeGraphBuilder::BuildSwitchOnSmi(Node* condition) {
  interpreter::JumpTableTargetOffsets offsets =
      bytecode_iterator().GetJumpTableTargetOffsets();

  NewSwitch(condition, offsets.size() + 1);
  for (interpreter::JumpTableTargetOffset entry : offsets) {
    SubEnvironment sub_environment(this);
    NewIfValue(entry.case_value);
    MergeIntoSuccessorEnvironment(entry.target_offset);
  }
  NewIfDefault();
}

void BytecodeGraphBuilder::VisitSwitchOnSmiNoFeedback() {
  PrepareEagerCheckpoint();

  Node* acc = environment()->LookupAccumulator();
  Node* acc_smi = NewNode(simplified()->CheckSmi(FeedbackSource()), acc);
  BuildSwitchOnSmi(acc_smi);
}

void BytecodeGraphBuilder::VisitSetPendingMessage() {
  Node* previous_message = NewNode(javascript()->LoadMessage());
  NewNode(javascript()->StoreMessage(), environment()->LookupAccumulator());
  environment()->BindAccumulator(previous_message);
}

void BytecodeGraphBuilder::BuildReturn(const BytecodeLivenessState* liveness) {
  BuildLoopExitsForFunctionExit(liveness);
  Node* pop_node = jsgraph()->ZeroConstant();
  Node* control =
      NewNode(common()->Return(), pop_node, environment()->LookupAccumulator());
  MergeControlToLeaveFunction(control);
}

void BytecodeGraphBuilder::VisitReturn() {
  BuildReturn(bytecode_analysis().GetInLivenessFor(
      bytecode_iterator().current_offset()));
}

void BytecodeGraphBuilder::VisitDebugger() {
  PrepareEagerCheckpoint();
  Node* call = NewNode(javascript()->Debugger());
  environment()->RecordAfterState(call, Environment::kAttachFrameState);
}

// We cannot create a graph from the debugger copy of the bytecode array.
#define DEBUG_BREAK(Name, ...) \
  void BytecodeGraphBuilder::Visit##Name() { UNREACHABLE(); }
DEBUG_BREAK_BYTECODE_LIST(DEBUG_BREAK)
#undef DEBUG_BREAK

void BytecodeGraphBuilder::VisitIncBlockCounter() {
  Node* closure = GetFunctionClosure();
  Node* coverage_array_slot =
      jsgraph()->ConstantNoHole(bytecode_iterator().GetIndexOperand(0));

  // Lowered by js-intrinsic-lowering to call Builtin::kIncBlockCounter.
  const Operator* op =
      javascript()->CallRuntime(Runtime::kInlineIncBlockCounter);

  NewNode(op, closure, coverage_array_slot);
}

void BytecodeGraphBuilder::VisitForInEnumerate() {
  Node* receiver =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  Node* enumerator = NewNode(javascript()->ForInEnumerate(), receiver);
  environment()->BindAccumulator(enumerator, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitForInPrepare() {
  PrepareEagerCheckpoint();
  Node* enumerator = environment()->LookupAccumulator();

  FeedbackSlot slot = bytecode_iterator().GetSlotOperand(1);
  JSTypeHintLowering::LoweringResult lowering =
      TryBuildSimplifiedForInPrepare(enumerator, slot);
  if (lowering.IsExit()) return;
  DCHECK(!lowering.Changed());
  FeedbackSource feedback = CreateFeedbackSource(slot);
  Node* node = NewNode(javascript()->ForInPrepare(GetForInMode(slot), feedback),
                       enumerator, feedback_vector_node());
  environment()->BindRegistersToProjections(
      bytecode_iterator().GetRegisterOperand(0), node);
}

void BytecodeGraphBuilder::VisitForInNext() {
  PrepareEagerCheckpoint();
  Node* receiver =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  Node* index =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(1));
  int catch_reg_pair_index = bytecode_iterator().GetRegisterOperand(2).index();
  Node* cache_type = environment()->LookupRegister(
      interpreter::Register(catch_reg_pair_index));
  Node* cache_array = environment()->LookupRegister(
      interpreter::Register(catch_reg_pair_index + 1));

  // We need to rename the {index} here, as in case of OSR we lose the
  // information that the {index} is always a valid unsigned Smi value.
  index = NewNode(common()->TypeGuard(Type::UnsignedSmall()), index);

  FeedbackSlot slot = bytecode_iterator().GetSlotOperand(3);
  JSTypeHintLowering::LoweringResult lowering = TryBuildSimplifiedForInNext(
      receiver, cache_array, cache_type, index, slot);
  if (lowering.IsExit()) return;

  DCHECK(!lowering.Changed());
  FeedbackSource feedback = CreateFeedbackSource(slot);
  Node* node =
      NewNode(javascript()->ForInNext(GetForInMode(slot), feedback), receiver,
              cache_array, cache_type, index, feedback_vector_node());
  environment()->BindAccumulator(node, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitForInStep() {
  PrepareEagerCheckpoint();
  interpreter::Register index_reg = bytecode_iterator().GetRegisterOperand(0);
  Node* index = environment()->LookupRegister(index_reg);
  index = NewNode(simplified()->SpeculativeSafeIntegerAdd(
                      NumberOperationHint::kSignedSmall),
                  index, jsgraph()->OneConstant());
  environment()->BindRegister(index_reg, index, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitGetIterator() {
  PrepareEagerCheckpoint();
  Node* receiver =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  FeedbackSource load_feedback =
      CreateFeedbackSource(bytecode_iterator().GetIndexOperand(1));
  FeedbackSource call_feedback =
      CreateFeedbackSource(bytecode_iterator().GetIndexOperand(2));
  const Operator* op = javascript()->GetIterator(load_feedback, call_feedback);

  JSTypeHintLowering::LoweringResult lowering = TryBuildSimplifiedGetIterator(
      op, receiver, load_feedback.slot, call_feedback.slot);
  if (lowering.IsExit()) return;

  DCHECK(!lowering.Changed());
  static_assert(JSGetIteratorNode::ReceiverIndex() == 0);
  static_assert(JSGetIteratorNode::FeedbackVectorIndex() == 1);
  DCHECK(IrOpcode::IsFeedbackCollectingOpcode(op->opcode()));
  Node* iterator = NewNode(op, receiver, feedback_vector_node());
  environment()->BindAccumulator(iterator, Environment::kAttachFrameState);
}

void BytecodeGraphBuilder::VisitSuspendGenerator() {
  Node* generator = environment()->LookupRegister(
      bytecode_iterator().GetRegisterOperand(0));
  interpreter::Register first_reg = bytecode_iterator().GetRegisterOperand(1);
  // We assume we are storing a range starting from index 0.
  CHECK_EQ(0, first_reg.index());
  int register_count =
      static_cast<int>(bytecode_iterator().GetRegisterCountOperand(2));
  int parameter_count_without_receiver = bytecode_array().parameter_count() - 1;

  Node* suspend_id = jsgraph()->SmiConstant(
      bytecode_iterator().GetUnsignedImmediateOperand(3));

  // The offsets used by the bytecode iterator are relative to a different base
  // than what is used in the interpreter, hence the addition.
  Node* offset =
      jsgraph()->ConstantNoHole(bytecode_iterator().current_offset() +
                                (BytecodeArray::kHeaderSize - kHeapObjectTag));

  const BytecodeLivenessState* liveness = bytecode_analysis().GetInLivenessFor(
      bytecode_iterator().current_offset());

  // Maybe overallocate the value list since we don't know how many registers
  // are live.
  // TODO(leszeks): We could get this count from liveness rather than the
  // register list.
  int value_input_count = 3 + parameter_count_without_receiver + register_count;

  Node** value_inputs = local_zone()->AllocateArray<Node*>(value_input_count);
  value_inputs[0] = generator;
  value_inputs[1] = suspend_id;
  value_inputs[2] = offset;

  int count_written = 0;
  // Store the parameters.
  for (int i = 0; i < parameter_count_without_receiver; i++) {
    value_inputs[3 + count_written++] =
        environment()->LookupRegister(bytecode_iterator().GetParameter(i));
  }

  // Store the registers.
  for (int i = 0; i < register_count; ++i) {
    if (liveness == nullptr || liveness->RegisterIsLive(i)) {
      int index_in_parameters_and_registers =
          parameter_count_without_receiver + i;
      while (count_written < index_in_parameters_and_registers) {
        value_inputs[3 + count_written++] = jsgraph()->OptimizedOutConstant();
      }
      value_inputs[3 + count_written++] =
          environment()->LookupRegister(interpreter::Register(i));
      DCHECK_EQ(count_written, index_in_parameters_and_registers + 1);
    }
  }

  // Use the actual written count rather than the register count to create the
  // node.
  MakeNode(javascript()->GeneratorStore(count_written), 3 + count_written,
           value_inputs, false);

  // TODO(leszeks): This over-approximates the liveness at exit, only the
  // accumulator should be live by this point.
  BuildReturn(bytecode_analysis().GetInLivenessFor(
      bytecode_iterator().current_offset()));
}

void BytecodeGraphBuilder::BuildSwitchOnGeneratorState(
    const ZoneVector<ResumeJumpTarget>& resume_jump_targets,
    bool allow_fallthrough_on_executing) {
  Node* generator_state = environment()->LookupGeneratorState();

  int extra_cases = allow_fallthrough_on_executing ? 2 : 1;
  NewSwitch(generator_state,
            static_cast<int>(resume_jump_targets.size() + extra_cases));
  for (const ResumeJumpTarget& target : resume_jump_targets) {
    SubEnvironment sub_environment(this);
    NewIfValue(target.suspend_id());
    if (target.is_leaf()) {
      // Mark that we are resuming executing.
      environment()->BindGeneratorState(
          jsgraph()->SmiConstant(JSGeneratorObject::kGeneratorExecuting));
    }
    // Jump to the target offset, whether it's a loop header or the resume.
    MergeIntoSuccessorEnvironment(target.target_offset());
  }

  {
    SubEnvironment sub_environment(this);
    // We should never hit the default case (assuming generator state cannot be
    // corrupted), so abort if we do.
    // TODO(leszeks): Maybe only check this in debug mode, and otherwise use
    // the default to represent one of the cases above/fallthrough below?
    NewIfDefault();
    NewNode(simplified()->RuntimeAbort(AbortReason::kInvalidJumpTableIndex));
    // TODO(7099): Investigate if we need LoopExit here.
    Node* control = NewNode(common()->Throw());
    MergeControlToLeaveFunction(control);
  }

  if (allow_fallthrough_on_executing) {
    // If we are executing (rather than resuming), and we allow it, just fall
    // through to the actual loop body.
    NewIfValue(JSGeneratorObject::kGeneratorExecuting);
  } else {
    // Otherwise, this environment is dead.
    set_environment(nullptr);
  }
}

void BytecodeGraphBuilder::VisitSwitchOnGeneratorState() {
  Node* generator =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));

  Node* generator_is_undefined =
      NewNode(simplified()->ReferenceEqual(), generator,
              jsgraph()->UndefinedConstant());

  NewBranch(generator_is_undefined);
  {
    SubEnvironment resume_env(this);
    NewIfFalse();

    Node* generator_state =
        NewNode(javascript()->GeneratorRestoreContinuation(), generator);
    environment()->BindGeneratorState(generator_state);

    Node* generator_context =
        NewNode(javascript()->GeneratorRestoreContext(), generator);
    environment()->SetContext(generator_context);

    BuildSwitchOnGeneratorState(bytecode_analysis().resume_jump_targets(),
                                false);
  }

  // Fallthrough for the first-call case.
  NewIfTrue();
}

void BytecodeGraphBuilder::VisitResumeGenerator() {
  Node* generator =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  interpreter::Register first_reg = bytecode_iterator().GetRegisterOperand(1);
  // We assume we are restoring registers starting fromm index 0.
  CHECK_EQ(0, first_reg.index());

  const BytecodeLivenessState* liveness = bytecode_analysis().GetOutLivenessFor(
      bytecode_iterator().current_offset());

  int parameter_count_without_receiver = bytecode_array().parameter_count() - 1;

  // Mapping between registers and array indices must match that used in
  // InterpreterAssembler::ExportParametersAndRegisterFile.
  for (int i = 0; i < environment()->register_count(); ++i) {
    if (liveness == nullptr || liveness->RegisterIsLive(i)) {
      Node* value = NewNode(javascript()->GeneratorRestoreRegister(
                                parameter_count_without_receiver + i),
                            generator);
      environment()->BindRegister(interpreter::Register(i), value);
    }
  }

  // Update the accumulator with the generator's input_or_debug_pos.
  Node* input_or_debug_pos =
      NewNode(javascript()->GeneratorRestoreInputOrDebugPos(), generator);
  environment()->BindAccumulator(input_or_debug_pos);
}

void BytecodeGraphBuilder::VisitWide() {
  // Consumed by the BytecodeArrayIterator.
  UNREACHABLE();
}

void BytecodeGraphBuilder::VisitExtraWide() {
  // Consumed by the BytecodeArrayIterator.
  UNREACHABLE();
}

void BytecodeGraphBuilder::VisitIllegal() {
  // Not emitted in valid bytecode.
  UNREACHABLE();
}

void BytecodeGraphBuilder::SwitchToMergeEnvironment(int current_offset) {
  auto it = merge_environments_.find(current_offset);
  if (it != merge_environments_.end()) {
    mark_as_needing_eager_checkpoint(true);
    if (environment() != nullptr) {
      it->second->Merge(environment(),
                        bytecode_analysis().GetInLivenessFor(current_offset));
    }
    set_environment(it->second);
  }
}

void BytecodeGraphBuilder::BuildLoopHeaderEnvironment(int current_offset) {
  if (bytecode_analysis().IsLoopHeader(current_offset)) {
    mark_as_needing_eager_checkpoint(true);
    const LoopInfo& loop_info =
        bytecode_analysis().GetLoopInfoFor(current_offset);
    const BytecodeLivenessState* liveness =
        bytecode_analysis().GetInLivenessFor(current_offset);

    const auto& resume_jump_targets = loop_info.resume_jump_targets();
    bool generate_suspend_switch = !resume_jump_targets.empty();

    // Add loop header.
    environment()->PrepareForLoop(loop_info.assignments(), liveness);

    // Store a copy of the environment so we can connect merged back edge inputs
    // to the loop header.
    merge_environments_[current_offset] = environment()->Copy();

    // If this loop contains resumes, create a new switch just after the loop
    // for those resumes.
    if (generate_suspend_switch) {
      BuildSwitchOnGeneratorState(loop_info.resume_jump_targets(), true);

      // TODO(leszeks): At this point we know we are executing rather than
      // resuming, so we should be able to prune off the phis in the environment
      // related to the resume path.

      // Set the generator state to a known constant.
      environment()->BindGeneratorState(
          jsgraph()->SmiConstant(JSGeneratorObject::kGeneratorExecuting));
    }
  }
}

void BytecodeGraphBuilder::MergeIntoSuccessorEnvironment(int target_offset) {
  BuildLoopExitsForBranch(target_offset);
  Environment*& merge_environment = merge_environments_[target_offset];

  if (merge_environment == nullptr) {
    // Append merge nodes to the environment. We may merge here with another
    // environment. So add a place holder for merge nodes. We may add redundant
    // but will be eliminated in a later pass.
    NewMerge();
    merge_environment = environment();
  } else {
    // Merge any values which are live coming into the successor.
    merge_environment->Merge(
        environment(), bytecode_analysis().GetInLivenessFor(target_offset));
  }
  set_environment(nullptr);
}

void BytecodeGraphBuilder::MergeControlToLeaveFunction(Node* exit) {
  exit_controls_.push_back(exit);
  set_environment(nullptr);
}

void BytecodeGraphBuilder::BuildLoopExitsForBranch(int target_offset) {
  int origin_offset = bytecode_iterator().current_offset();
  // Only build loop exits for forward edges.
  if (target_offset > origin_offset) {
    BuildLoopExitsUntilLoop(
        bytecode_analysis().GetLoopOffsetFor(target_offset),
        bytecode_analysis().GetInLivenessFor(target_offset));
  }
}

void BytecodeGraphBuilder::BuildLoopExitsUntilLoop(
    int loop_offset, const BytecodeLivenessState* liveness) {
  int origin_offset = bytecode_iterator().current_offset();
  int current_loop = bytecode_analysis().GetLoopOffsetFor(origin_offset);
  // The limit_offset is the stop offset for building loop exists, used for OSR.
  // It prevents the creations of loopexits for loops which do not exist.
  loop_offset = std::max(loop_offset, currently_peeled_loop_offset_);

  while (loop_offset < current_loop) {
    Node* loop_node = merge_environments_[current_loop]->GetControlDependency();
    const LoopInfo& loop_info =
        bytecode_analysis().GetLoopInfoFor(current_loop);
    environment()->PrepareForLoopExit(loop_node, loop_info.assignments(),
                                      liveness);
    current_loop = loop_info.parent_offset();
  }
}

void BytecodeGraphBuilder::BuildLoopExitsForFunctionExit(
    const BytecodeLivenessState* liveness) {
  BuildLoopExitsUntilLoop(-1, liveness);
}

void BytecodeGraphBuilder::BuildJump() {
  MergeIntoSuccessorEnvironment(bytecode_iterator().GetJumpTargetOffset());
}

void BytecodeGraphBuilder::BuildJumpIf(Node* condition) {
  NewBranch(condition, BranchHint::kNone);
  {
    SubEnvironment sub_environment(this);
    NewIfTrue();
    MergeIntoSuccessorEnvironment(bytecode_iterator().GetJumpTargetOffset());
  }
  NewIfFalse();
}

void BytecodeGraphBuilder::BuildJumpIfNot(Node* condition) {
  NewBranch(condition, BranchHint::kNone);
  {
    SubEnvironment sub_environment(this);
    NewIfFalse();
    MergeIntoSuccessorEnvironment(bytecode_iterator().GetJumpTargetOffset());
  }
  NewIfTrue();
}

void BytecodeGraphBuilder::BuildJumpIfEqual(Node* comperand) {
  Node* accumulator = environment()->LookupAccumulator();
  Node* condition =
      NewNode(simplified()->ReferenceEqual(), accumulator, comperand);
  BuildJumpIf(condition);
}

void BytecodeGraphBuilder::BuildJumpIfNotEqual(Node* comperand) {
  Node* accumulator = environment()->LookupAccumulator();
  Node* condition =
      NewNode(simplified()->ReferenceEqual(), accumulator, comperand);
  BuildJumpIfNot(condition);
}

void BytecodeGraphBuilder::BuildJumpIfFalse() {
  NewBranch(environment()->LookupAccumulator(), BranchHint::kNone);
  {
    SubEnvironment sub_environment(this);
    NewIfFalse();
    environment()->BindAccumulator(jsgraph()->FalseConstant());
    MergeIntoSuccessorEnvironment(bytecode_iterator().GetJumpTargetOffset());
  }
  NewIfTrue();
  environment()->BindAccumulator(jsgraph()->TrueConstant());
}

void BytecodeGraphBuilder::BuildJumpIfTrue() {
  NewBranch(environment()->LookupAccumulator(), BranchHint::kNone);
  {
    SubEnvironment sub_environment(this);
    NewIfTrue();
    environment()->BindAccumulator(jsgraph()->TrueConstant());
    MergeIntoSuccessorEnvironment(bytecode_iterator().GetJumpTargetOffset());
  }
  NewIfFalse();
  environment()->BindAccumulator(jsgraph()->FalseConstant());
}

void BytecodeGraphBuilder::BuildJumpIfToBooleanTrue() {
  Node* accumulator = environment()->LookupAccumulator();
  Node* condition = NewNode(simplified()->ToBoolean(), accumulator);
  BuildJumpIf(condition);
}

void BytecodeGraphBuilder::BuildJumpIfToBooleanFalse() {
  Node* accumulator = environment()->LookupAccumulator();
  Node* condition = NewNode(simplified()->ToBoolean(), accumulator);
  BuildJumpIfNot(condition);
}

void BytecodeGraphBuilder::BuildJumpIfNotHole() {
  Node* accumulator = environment()->LookupAccumulator();
  Node* condition = NewNode(simplified()->ReferenceEqual(), accumulator,
                            jsgraph()->TheHoleConstant());
  BuildJumpIfNot(condition);
}

void BytecodeGraphBuilder::BuildJumpIfJSReceiver() {
  Node* accumulator = environment()->LookupAccumulator();
  Node* condition = NewNode(simplified()->ObjectIsReceiver(), accumulator);
  BuildJumpIf(condition);
}

void BytecodeGraphBuilder::BuildJumpIfForInDone() {
  // There's an eager checkpoint here for the speculative comparison, but it can
  // never actually deopt because these are known to be Smi.
  PrepareEagerCheckpoint();
  Node* index =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(1));
  Node* cache_length =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(2));
  Node* condition = NewNode(
      simplified()->SpeculativeNumberEqual(NumberOperationHint::kSignedSmall),
      index, cache_length);
  BuildJumpIf(condition);
}

JSTypeHintLowering::LoweringResult
BytecodeGraphBuilder::TryBuildSimplifiedUnaryOp(const Operator* op,
                                                Node* operand,
                                                FeedbackSlot slot) {
  Node* effect = environment()->GetEffectDependency();
  Node* control = environment()->GetControlDependency();
  JSTypeHintLowering::LoweringResult result =
      type_hint_lowering().ReduceUnaryOperation(op, operand, effect, control,
                                                slot);
  ApplyEarlyReduction(result);
  return result;
}

JSTypeHintLowering::LoweringResult
BytecodeGraphBuilder::TryBuildSimplifiedBinaryOp(const Operator* op, Node* left,
                                                 Node* right,
                                                 FeedbackSlot slot) {
  Node* effect = environment()->GetEffectDependency();
  Node* control = environment()->GetControlDependency();
  JSTypeHintLowering::LoweringResult result =
      type_hint_lowering().ReduceBinaryOperation(op, left, right, effect,
                                                 control, slot);
  ApplyEarlyReduction(result);
  return result;
}

JSTypeHintLowering::LoweringResult
BytecodeGraphBuilder::TryBuildSimplifiedForInNext(Node* receiver,
                                                  Node* cache_array,
                                                  Node* cache_type, Node* index,
                                                  FeedbackSlot slot) {
  Node* effect = environment()->GetEffectDependency();
  Node* control = environment()->GetControlDependency();
  JSTypeHintLowering::LoweringResult result =
      type_hint_lowering().ReduceForInNextOperation(
          receiver, cache_array, cache_type, index, effect, control, slot);
  ApplyEarlyReduction(result);
  return result;
}

JSTypeHintLowering::LoweringResult
BytecodeGraphBuilder::TryBuildSimplifiedForInPrepare(Node* enumerator,
                                                     FeedbackSlot slot) {
  Node* effect = environment()->GetEffectDependency();
  Node* control = environment()->GetControlDependency();
  JSTypeHintLowering::LoweringResult result =
      type_hint_lowering().ReduceForInPrepareOperation(enumerator, effect,
                                                       control, slot);
  ApplyEarlyReduction(result);
  return result;
}

JSTypeHintLowering::LoweringResult
BytecodeGraphBuilder::TryBuildSimplifiedToNumber(Node* value,
                                                 FeedbackSlot slot) {
  Node* effect = environment()->GetEffectDependency();
  Node* control = environment()->GetControlDependency();
  JSTypeHintLowering::LoweringResult result =
      type_hint_lowering().ReduceToNumberOperation(value, effect, control,
                                                   slot);
  ApplyEarlyReduction(result);
  return result;
}

JSTypeHintLowering::LoweringResult BytecodeGraphBuilder::TryBuildSimplifiedCall(
    const Operator* op, Node* const* args, int arg_count, FeedbackSlot slot) {
  Node* effect = environment()->GetEffectDependency();
  Node* control = environment()->GetControlDependency();
  JSTypeHintLowering::LoweringResult result =
      type_hint_lowering().ReduceCallOperation(op, args, arg_count, effect,
                                               control, slot);
  ApplyEarlyReduction(result);
  return result;
}

JSTypeHintLowering::LoweringResult
BytecodeGraphBuilder::TryBuildSimplifiedConstruct(const Operator* op,
                                                  Node* const* args,
                                                  int arg_count,
                                                  FeedbackSlot slot) {
  Node* effect = environment()->GetEffectDependency();
  Node* control = environment()->GetControlDependency();
  JSTypeHintLowering::LoweringResult result =
      type_hint_lowering().ReduceConstructOperation(op, args, arg_count, effect,
                                                    control, slot);
  ApplyEarlyReduction(result);
  return result;
}

JSTypeHintLowering::LoweringResult
BytecodeGraphBuilder::TryBuildSimplifiedGetIterator(const Operator* op,
                                                    Node* receiver,
                                                    FeedbackSlot load_slot,
                                                    FeedbackSlot call_slot) {
  Node* effect = environment()->GetEffectDependency();
  Node* control = environment()->GetControlDependency();
  JSTypeHintLowering::LoweringResult early_reduction =
      type_hint_lowering().ReduceGetIteratorOperation(
          op, receiver, effect, control, load_slot, call_slot);
  ApplyEarlyReduction(early_reduction);
  return early_reduction;
}

JSTypeHintLowering::LoweringResult
BytecodeGraphBuilder::TryBuildSimplifiedLoadNamed(const Operator* op,
                                                  FeedbackSlot slot) {
  Node* effect = environment()->GetEffectDependency();
  Node* control = environment()->GetControlDependency();
  JSTypeHintLowering::LoweringResult early_reduction =
      type_hint_lowering().ReduceLoadNamedOperation(op, effect, control, slot);
  ApplyEarlyReduction(early_reduction);
  return early_reduction;
}

JSTypeHintLowering::LoweringResult
BytecodeGraphBuilder::TryBuildSimplifiedLoadKeyed(const Operator* op,
                                                  Node* receiver, Node* key,
                                                  FeedbackSlot slot) {
  Node* effect = environment()->GetEffectDependency();
  Node* control = environment()->GetControlDependency();
  JSTypeHintLowering::LoweringResult result =
      type_hint_lowering().ReduceLoadKeyedOperation(op, receiver, key, effect,
                                                    control, slot);
  ApplyEarlyReduction(result);
  return result;
}

JSTypeHintLowering::LoweringResult
BytecodeGraphBuilder::TryBuildSimplifiedStoreNamed(const Operator* op,
                                                   Node* receiver, Node* value,
                                                   FeedbackSlot slot) {
  Node* effect = environment()->GetEffectDependency();
  Node* control = environment()->GetControlDependency();
  JSTypeHintLowering::LoweringResult result =
      type_hint_lowering().ReduceStoreNamedOperation(op, receiver, value,
                                                     effect, control, slot);
  ApplyEarlyReduction(result);
  return result;
}

JSTypeHintLowering::LoweringResult
BytecodeGraphBuilder::TryBuildSimplifiedStoreKeyed(const Operator* op,
                                                   Node* receiver, Node* key,
                                                   Node* value,
                                                   FeedbackSlot slot) {
  Node* effect = environment()->GetEffectDependency();
  Node* control = environment()->GetControlDependency();
  JSTypeHintLowering::LoweringResult result =
      type_hint_lowering().ReduceStoreKeyedOperation(op, receiver, key, value,
                                                     effect, control, slot);
  ApplyEarlyReduction(result);
  return result;
}

void BytecodeGraphBuilder::ApplyEarlyReduction(
    JSTypeHintLowering::LoweringResult reduction) {
  if (reduction.IsExit()) {
    MergeControlToLeaveFunction(reduction.control());
  } else if (reduction.IsSideEffectFree()) {
    environment()->UpdateEffectDependency(reduction.effect());
    environment()->UpdateControlDependency(reduction.control());
  } else {
    DCHECK(!reduction.Changed());
    // At the moment, we assume side-effect free reduction. To support
    // side-effects, we would have to invalidate the eager checkpoint,
    // so that deoptimization does not repeat the side effect.
  }
}

Node** BytecodeGraphBuilder::EnsureInputBufferSize(int size) {
  if (size > input_buffer_size_) {
    size = size + kInputBufferSizeIncrement + input_buffer_size_;
    input_buffer_ = local_zone()->AllocateArray<Node*>(size);
    input_buffer_size_ = size;
  }
  return input_buffer_;
}

void BytecodeGraphBuilder::ExitThenEnterExceptionHandlers(int current_offset) {
  DisallowGarbageCollection no_gc;
  HandlerTable table(bytecode_array().handler_table_address(),
                     bytecode_array().handler_table_size(),
                     HandlerTable::kRangeBasedEncoding);

  // Potentially exit exception handlers.
  while (!exception_handlers_.empty()) {
    int current_end = exception_handlers_.top().end_offset_;
    if (current_offset < current_end) break;  // Still covered by range.
    exception_handlers_.pop();
  }

  // Potentially enter exception handlers.
  int num_entries = table.NumberOfRangeEntries();
  while (current_exception_handler_ < num_entries) {
    int next_start = table.GetRangeStart(current_exception_handler_);
    if (current_offset < next_start) break;  // Not yet covered by range.
    int next_end = table.GetRangeEnd(current_exception_handler_);
    int next_handler = table.GetRangeHandler(current_exception_handler_);
    int context_register = table.GetRangeData(current_exception_handler_);
    exception_handlers_.push(
        {next_start, next_end, next_handler, context_register});
    current_exception_handler_++;
  }
}

Node* BytecodeGraphBuilder::MakeNode(const Operator* op, int value_input_count,
                                     Node* const* value_inputs,
                                     bool incomplete) {
  DCHECK_EQ(op->ValueInputCount(), value_input_count);
  // Parameter nodes must be created through GetParameter.
  DCHECK_IMPLIES(
      op->opcode() == IrOpcode::kParameter,
      (nullptr == cached_parameters_[static_cast<std::size_t>(
                      ParameterIndexOf(op) - ParameterInfo::kMinIndex)]));

  bool has_context = OperatorProperties::HasContextInput(op);
  bool has_frame_state = OperatorProperties::HasFrameStateInput(op);
  bool has_control = op->ControlInputCount() == 1;
  bool has_effect = op->EffectInputCount() == 1;

  DCHECK_LT(op->ControlInputCount(), 2);
  DCHECK_LT(op->EffectInputCount(), 2);

  Node* result = nullptr;
  if (!has_context && !has_frame_state && !has_control && !has_effect) {
    result = graph()->NewNode(op, value_input_count, value_inputs, incomplete);
  } else {
    bool inside_handler = !exception_handlers_.empty();
    int input_count_with_deps = value_input_count;
    if (has_context) ++input_count_with_deps;
    if (has_frame_state) ++input_count_with_deps;
    if (has_control) ++input_count_with_deps;
    if (has_effect) ++input_count_with_deps;
    Node** buffer = EnsureInputBufferSize(input_count_with_deps);
    if (value_input_count > 0) {
      memcpy(buffer, value_inputs, kSystemPointerSize * value_input_count);
    }
    Node** current_input = buffer + value_input_count;
    if (has_context) {
      *current_input++ = OperatorProperties::NeedsExactContext(op)
                             ? environment()->Context()
                             : native_context_node();
    }
    if (has_frame_state) {
      // The frame state will be inserted later. Here we misuse the {Dead} node
      // as a sentinel to be later overwritten with the real frame state by the
      // calls to {PrepareFrameState} within individual visitor methods.
      *current_input++ = jsgraph()->Dead();
    }
    if (has_effect) {
      *current_input++ = environment()->GetEffectDependency();
    }
    if (has_control) {
      *current_input++ = environment()->GetControlDependency();
    }
    result = graph()->NewNode(op, input_count_with_deps, buffer, incomplete);
    // Update the current control dependency for control-producing nodes.
    if (result->op()->ControlOutputCount() > 0) {
      environment()->UpdateControlDependency(result);
    }
    // Update the current effect dependency for effect-producing nodes.
    if (result->op()->EffectOutputCount() > 0) {
      environment()->UpdateEffectDependency(result);
    }
    // Add implicit exception continuation for throwing nodes.
    if (!result->op()->HasProperty(Operator::kNoThrow) && inside_handler) {
      int handler_offset = exception_handlers_.top().handler_offset_;
      int context_index = exception_handlers_.top().context_register_;
      interpreter::Register context_register(context_index);
      Environment* success_env = environment()->Copy();
      const Operator* if_exception = common()->IfException();
      Node* effect = environment()->GetEffectDependency();
      Node* on_exception = graph()->NewNode(if_exception, effect, result);
      Node* context = environment()->LookupRegister(context_register);
      environment()->UpdateControlDependency(on_exception);
      environment()->UpdateEffectDependency(on_exception);
      environment()->BindAccumulator(on_exception);
      environment()->SetContext(context);
      MergeIntoSuccessorEnvironment(handler_offset);
      set_environment(success_env);
    }
    // Add implicit success continuation for throwing nodes.
    if (!result->op()->HasProperty(Operator::kNoThrow) && inside_handler) {
      const Operator* if_success = common()->IfSuccess();
      Node* on_success = graph()->NewNode(if_success, result);
      environment()->UpdateControlDependency(on_success);
    }
    // Ensure checkpoints are created after operations with side-effects.
    if (has_effect && !result->op()->HasProperty(Operator::kNoWrite)) {
      mark_as_needing_eager_checkpoint(true);
    }
  }

  return result;
}


Node* BytecodeGraphBuilder::NewPhi(int count, Node* input, Node* control) {
  const Operator* phi_op = common()->Phi(MachineRepresentation::kTagged, count);
  Node** buffer = EnsureInputBufferSize(count + 1);
  MemsetPointer(buffer, input, count);
  buffer[count] = control;
  return graph()->NewNode(phi_op, count + 1, buffer, true);
}

Node* BytecodeGraphBuilder::NewEffectPhi(int count, Node* input,
                                         Node* control) {
  const Operator* phi_op = common()->EffectPhi(count);
  Node** buffer = EnsureInputBufferSize(count + 1);
  MemsetPointer(buffer, input, count);
  buffer[count] = control;
  return graph()->NewNode(phi_op, count + 1, buffer, true);
}


Node* BytecodeGraphBuilder::MergeControl(Node* control, Node* other) {
  int inputs = control->op()->ControlInputCount() + 1;
  if (control->opcode() == IrOpcode::kLoop) {
    // Control node for loop exists, add input.
    const Operator* op = common()->Loop(inputs);
    control->AppendInput(graph_zone(), other);
    NodeProperties::ChangeOp(control, op);
  } else if (control->opcode() == IrOpcode::kMerge) {
    // Control node for merge exists, add input.
    const Operator* op = common()->Merge(inputs);
    control->AppendInput(graph_zone(), other);
    NodeProperties::ChangeOp(control, op);
  } else {
    // Control node is a singleton, introduce a merge.
    const Operator* op = common()->Merge(inputs);
    Node* merge_inputs[] = {control, other};
    control = graph()->NewNode(op, arraysize(merge_inputs), merge_inputs, true);
  }
  return control;
}

Node* BytecodeGraphBuilder::MergeEffect(Node* value, Node* other,
                                        Node* control) {
  int inputs = control->op()->ControlInputCount();
  if (value->opcode() == IrOpcode::kEffectPhi &&
      NodeProperties::GetControlInput(value) == control) {
    // Phi already exists, add input.
    value->InsertInput(graph_zone(), inputs - 1, other);
    NodeProperties::ChangeOp(value, common()->EffectPhi(inputs));
  } else if (value != other) {
    // Phi does not exist yet, introduce one.
    value = NewEffectPhi(inputs, value, control);
    value->ReplaceInput(inputs - 1, other);
  }
  return value;
}

Node* BytecodeGraphBuilder::MergeValue(Node* value, Node* other,
                                       Node* control) {
  int inputs = control->op()->ControlInputCount();
  if (value->opcode() == IrOpcode::kPhi &&
      NodeProperties::GetControlInput(value) == control) {
    // Phi already exists, add input.
    value->InsertInput(graph_zone(), inputs - 1, other);
    NodeProperties::ChangeOp(
        value, common()->Phi(MachineRepresentation::kTagged, inputs));
  } else if (value != other) {
    // Phi does not exist yet, introduce one.
    value = NewPhi(inputs, value, control);
    value->ReplaceInput(inputs - 1, other);
  }
  return value;
}

void BytecodeGraphBuilder::UpdateSourceAndBytecodePosition(int offset) {
  if (node_origins_) {
    node_origins_->SetCurrentBytecodePosition(offset);
  }
  if (source_position_iterator().done()) return;
  if (source_position_iterator().code_offset() == offset) {
    source_positions_->SetCurrentPosition(SourcePosition(
        source_position_iterator().source_position().ScriptOffset(),
        start_position_.InliningId()));
    source_position_iterator().Advance();
  } else {
    DCHECK_GT(source_position_iterator().code_offset(), offset);
  }
}

void BuildGraphFromBytecode(
    JSHeapBroker* broker, Zone* local_zone, SharedFunctionInfoRef shared_info,
    FeedbackCellRef feedback_cell, BytecodeOffset osr_offset, JSGraph* jsgraph,
    CallFrequency const& invocation_frequency,
    SourcePositionTable* source_positions, NodeOriginTable* node_origins,
    int inlining_id, CodeKind code_kind, BytecodeGraphBuilderFlags flags,
    TickCounter* tick_counter, ObserveNodeInfo const& observe_node_info) {
  BytecodeGraphBuilder builder(
      broker, local_zone, broker->target_native_context(), shared_info,
      feedback_cell, osr_offset, jsgraph, invocation_frequency,
      source_positions, node_origins, inlining_id, code_kind, flags,
      tick_counter, observe_node_info);
  builder.CreateGraph();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
